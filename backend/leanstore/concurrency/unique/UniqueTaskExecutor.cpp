#include "UniqueTaskExecutor.hpp"
#include "Exceptions.hpp"
#include "Time.hpp"
#include "leanstore/concurrency-recovery/Worker.hpp"
#include "leanstore/concurrency/Mean.hpp"
#include "leanstore/concurrency/unique/UniqueTask.hpp"
#include "leanstore/concurrency/utils/MessageHandler.hpp"
#include "leanstore/concurrency/utils/ThreadBase.hpp"
#include "leanstore/profiling/counters/CPUCounters.hpp"
#include "leanstore/profiling/counters/ThreadCounters.hpp"
#include "leanstore/profiling/counters/WorkerCounters.hpp"
#include "leanstore/utils/RandomGenerator.hpp"

#include <boost/context/preallocated.hpp>
#include "boost/context/continuation.hpp"

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <queue>
#include <thread>

#include <drivers/clockevent.hh>
#include <osv/clock.hh>
#include <osv/leanstore_debug.hh>
#include "arch.hh"
#include "exceptions.hh"
#include "processor.hh"

namespace mean
{

// ============================================================================
// Thread-Local Variables
// ============================================================================

thread_local uint64_t opentasks = 0;
thread_local TaskContextPool* tl_task_context_pool;
thread_local bool run_task = false;
thread_local std::atomic<uint64_t> last_timestamp = {0};

std::atomic<int> UniqueTaskExecutor::interruptVector = {-1}; 

// ============================================================================
// Context Switch Callbacks (CRITICAL - DO NOT MODIFY)
// ============================================================================
// These fcontext callbacks are used during context switches and must
// maintain their exact signature and behavior for boost::context

boost::context::detail::transfer_t UniqueTaskExecutor::store_sink_on_yield(boost::context::detail::transfer_t t)
{
   auto* self = reinterpret_cast<UniqueTaskExecutor*>(t.data);
   self->updateCurrentSink(t.fctx);
   return {nullptr, nullptr};
}

boost::context::detail::transfer_t UniqueTaskExecutor::store_task_on_yield(boost::context::detail::transfer_t t)
{
   auto* self = reinterpret_cast<UniqueTask*>(t.data);
   self->context.this_task_context = t.fctx;
   return {nullptr, nullptr};
}

// ============================================================================
// Task Context Management
// ============================================================================

void UniqueTaskDeleter::operator()(UniqueTask* task) const
{
   opentasks--;

   if (task) {
      UniqueTaskExecutor::localExec().g_task_context_pool->deallocate(task->context);
   }
   delete task;
}

void UniqueTaskExecutor::initTaskContextPool(size_t capacity)
{
   if (g_task_context_pool) {
      throw std::runtime_error("Pool already initialized");
   }
   g_task_context_pool = new TaskContextPool(capacity);
}

void UniqueTaskExecutor::destroyTaskContextPool()
{
   delete g_task_context_pool;
   g_task_context_pool = nullptr;
}

void UniqueTaskExecutor::initializeTaskContext(UniqueTaskContext& ctx,
                                               void* stack_base,
                                               size_t stack_size,
                                               void (*fn)(boost::context::detail::transfer_t))
{
   ctx.this_task_context = boost::context::detail::make_fcontext(static_cast<char*>(stack_base) + stack_size, stack_size, fn);
}

UniqueTaskExecutor::UniqueTaskPtr UniqueTaskExecutor::createTask(TaskFunction fun, void (*entry_fn)(boost::context::detail::transfer_t))
{
   if (!g_task_context_pool) {
      throw std::runtime_error("Pool not initialized");
   }

   auto task = new UniqueTask(fun);
   g_task_context_pool->allocate(task->context);
   initializeTaskContext(task->context, task->context.stack, TaskContextPool::getStackSize(), entry_fn);

   opentasks++;
   return UniqueTaskPtr(task);
}

// ============================================================================
// Task Trampoline (CRITICAL - DO NOT MODIFY)
// ============================================================================
// Entry point for new tasks - handles initialization and cleanup

void UniqueTaskExecutor::trampoline(boost::context::detail::transfer_t t)
{
   auto& self = UniqueTaskExecutor::localExec();
   uint64_t arg = reinterpret_cast<uint64_t>(t.data);
   self.updateCurrentSink(t.fctx);

#ifdef USE_INTERRUPTS
   arch::irq_enable();
#if !defined(USE_PERIODIC_TIMER) && !defined(USE_WATCHDOG)
   clock_event->set(std::chrono::nanoseconds(INTERRUPT_TIME));
#endif
#endif

   self._currentTask->fun();

#ifdef USE_WATCHDOG
   last_timestamp = 0;
#endif

   {
#if defined(USE_INTERRUPTS)
      leanstore_osv_debug::set_interrupt_stack((char*)localExec()._sinkInterruptStack);
#endif
      self._currentTask->state = TaskState::Done;
      jumpmu::thread_local_jumpmu_ctx = &self.defaultExecutorContext;
      boost::context::detail::jump_fcontext(self.getCurrentSink(), nullptr);
   }
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

UniqueTaskExecutor::UniqueTaskExecutor(MessageHandler& msg, IoChannel& io_channel, DummyNIC& nic, int executor_id)
    : ThreadBase("te_", executor_id), ioChannel(io_channel), nic(nic), messageHandler(msg)
{
   jumpmu::thread_local_jumpmu_ctx = &defaultExecutorContext;
   initTaskContextPool(5000);
   tl_task_context_pool = g_task_context_pool;

   // setupInterruptHandling();

   printf("create unique task executor %i\n", executor_id);
   _sinkInterruptStack = static_cast<char*>(malloc(8192)) + 8192;
}

UniqueTaskExecutor::~UniqueTaskExecutor()
{
   destroyTaskContextPool();
}

void UniqueTaskExecutor::setupInterruptVector() {
   int exp = -1; 
   if (interruptVector.compare_exchange_strong(exp, 1)) {
      #ifdef USE_INTERRUPTS 
      arch::irq_disable();
      
      auto timer_handler = []() {
         leanstore::WorkerCounters::myCounters().time_counter_1++;
         // return; 
      
         if (!run_task or jumpmu::thread_local_jumpmu_ctx->lock_counter > 0) {
            leanstore::WorkerCounters::myCounters().time_counter_2++;
            return;
         }
      
         assert(!arch::irq_enabled());
      
         if (UniqueTaskExecutor::localExec()._currentTask != nullptr && UniqueTaskExecutor::localExec()._currentSink != nullptr) {
      
      #ifdef USE_WATCHDOG
            last_timestamp = 0;
      #endif
            leanstore_osv_debug::set_interrupt_stack((char*)UniqueTaskExecutor::localExec()._sinkInterruptStack);
      
            assert(UniqueTaskExecutor::localExec()._currentSink != nullptr);
            assert(UniqueTaskExecutor::localExec()._currentTask.get() != nullptr);

            leanstore_osv_debug::trace_interrupted(&UniqueTaskExecutor::localExec()._currentTask, jumpmu::thread_local_jumpmu_ctx->lock_counter); 
      
            boost::context::detail::ontop_fcontext(UniqueTaskExecutor::localExec().getCurrentSink(),
                                                   (void*)UniqueTaskExecutor::localExec()._currentTask.get(), UniqueTaskExecutor::store_task_on_yield);
      
      #if !defined(USE_PERIODIC_TIMER) && !defined(USE_WATCHDOG)
            clock_event->set(std::chrono::nanoseconds(INTERRUPT_TIME));
      #endif
            return;
         }
      };
      
      auto vector = idt.register_handler(timer_handler);
      interruptVector.store(vector); 
   }
   
   while (interruptVector.load() <= 1) {}
}

void UniqueTaskExecutor::setupInterruptHandling()
{
   UniqueTaskExecutor::setupInterruptVector(); 

#ifndef USE_WATCHDOG
   clock_event->reset_vector(interruptVector.load());
#ifdef USE_PERIODIC_TIMER
   // printf("setup clock\n"); 
   clock_event->set_periodic(true);
   clock_event->set(std::chrono::nanoseconds(INTERRUPT_TIME));
#endif
#else
   clock_event->set_periodic(true);
   clock_event->disable();
   leanstore_osv_debug::create_watchdog(INTERRUPT_TIME, 2, vector, last_timestamp);
#endif

   arch::irq_enable();
#endif
}

// ============================================================================
// Task Execution (CRITICAL - Handle with care)
// ============================================================================

TaskState UniqueTaskExecutor::runCurrentTask()
{
   auto task = _currentTask.get();
   jumpmu::thread_local_jumpmu_ctx = _currentTask->context.jumpmuctx;

#ifdef USE_INTERRUPTS
   arch::irq_disable();
   run_task = true;
#ifdef USE_WATCHDOG
   last_timestamp = processor::rdtsc();
#endif
   leanstore_osv_debug::set_interrupt_stack((char*)task->context.interrupt_stack);
#endif

   if (!_currentTask->context.init) {
      _currentTask->context.init = true;
      boost::context::detail::jump_fcontext(_currentTask->context.this_task_context, (void*)_currentTask->arg);
   } else {
      boost::context::detail::ontop_fcontext(_currentTask->context.this_task_context, this, this->store_sink_on_yield);
   }

#ifdef USE_INTERRUPTS
   run_task = false;
#ifdef USE_WATCHDOG
   last_timestamp = 0;
#endif
   arch::irq_enable();
#endif

   jumpmu::thread_local_jumpmu_ctx = &defaultExecutorContext;
   return task->getState();
}

// ============================================================================
// Task Yielding (CRITICAL - DO NOT MODIFY)
// ============================================================================

void UniqueTaskExecutor::yieldCurrentTask(TaskState state)
{
   assert(arch::irq_enabled());

   if (localExec()._currentTask.get() == nullptr) {
      abort();
   }

#ifdef USE_INTERRUPTS
   arch::irq_disable();
#if !defined(USE_PERIODIC_TIMER) && !defined(USE_WATCHDOG)
   clock_event->disable();
#endif
#endif

   UniqueTask& task = currentTask();
   task.state = state;

   auto& local_executor = UniqueTaskExecutor::localExec();
   auto sink_fcontext = local_executor.getCurrentSink();

#ifdef USE_INTERRUPTS
   leanstore_osv_debug::set_interrupt_stack((char*)UniqueTaskExecutor::localExec()._sinkInterruptStack);
#endif

   boost::context::detail::ontop_fcontext(sink_fcontext, (void*)&task, local_executor.store_task_on_yield);

#if defined(USE_INTERRUPTS)
#if !defined(USE_PERIODIC_TIMER) && !defined(USE_WATCHDOG)
   clock_event->set(std::chrono::nanoseconds(INTERRUPT_TIME));
#endif
   arch::irq_enable();
#endif
}

void UniqueTaskExecutor::yieldRunningTask(UniqueTask* task, TaskState state)
{
   assert(arch::irq_enabled());

#ifdef USE_INTERRUPTS
   arch::irq_disable();
#if !defined(USE_PERIODIC_TIMER) && !defined(USE_WATCHDOG)
   clock_event->disable();
#endif
#endif

   task->state = state;

   auto& local_executor = UniqueTaskExecutor::localExec();
   auto sink_fcontext = local_executor.getCurrentSink();

#ifdef USE_INTERRUPTS
   leanstore_osv_debug::set_interrupt_stack((char*)UniqueTaskExecutor::localExec()._sinkInterruptStack);
#endif

   boost::context::detail::ontop_fcontext(sink_fcontext, (void*)task, local_executor.store_task_on_yield);

#if defined(USE_INTERRUPTS)
#if !defined(USE_PERIODIC_TIMER) && !defined(USE_WATCHDOG)
   clock_event->set(std::chrono::nanoseconds(INTERRUPT_TIME));
#endif
   arch::irq_enable();
#endif
}

// ============================================================================
// Main Execution Loop
// ============================================================================

int UniqueTaskExecutor::process()
{
   ensure(tasks.size() == 0);
   leanstore::cr::Worker::tls_ptr = this_worker;
   leanstore::CPUCounters::registerThread(std::to_string(id()), false);
   cycle();
   return 0;
}

void UniqueTaskExecutor::cycle()
{
   u64 cycles = 0;
   u64 cycles_nothing_run = 0;
   const u64 sleep_threshold_cycles = 100000;
   u64 delay_submit_until_cycle = 0;

   random_generator.seed(mean::exec::getId());

   while (_keep_running) {
      handleSleep();

      cycles++;

      if (shouldPollMessages(cycles, cycles_nothing_run, sleep_threshold_cycles)) {
         pollMessages();
      }

      if (shouldRunPageProvider(cycles)) {
         pageProviderCycle();
      }

      handleIoSubmission(cycles, delay_submit_until_cycle);

      if (shouldPollIo(cycles)) {
         pollIo(cycles);
      }

      if (shouldPollWorkload(cycles)) {
         pollWorkload();
      }

      int tasks_run = runScheduledTasks();
      updateCycleCounters(tasks_run, cycles_nothing_run);
   }
}

void UniqueTaskExecutor::handleSleep()
{
   if (sleep != 0) {
      float sleep_time = sleep.exchange(0);
      std::cout << "sleep for: " << sleep_time << std::endl;
      std::this_thread::sleep_for(std::chrono::nanoseconds(static_cast<uint64_t>(sleep_time * 1e9)));
   }
}

bool UniqueTaskExecutor::shouldPollMessages(u64 cycles, u64 cycles_nothing_run, u64 threshold) const
{
   return cycles % (8 * 1024) == 0 || cycles_nothing_run > threshold;
}

void UniqueTaskExecutor::pollMessages()
{
   messageHandler.poll(this);
   counters.msgPollCalled++;
}

bool UniqueTaskExecutor::shouldRunPageProvider(u64 cycles) const
{
   constexpr int every_pp = 32;
   return cycles % every_pp == 0;
}

bool UniqueTaskExecutor::shouldPollIo(u64 cycles) const
{
   constexpr int every_poll = 64;
   return cycles % every_poll == 0;
}

void UniqueTaskExecutor::pollIo(u64 cycles)
{
   constexpr int every_poll = 64;
   leanstore::ThreadCounters::myCounters().exec_cycles += every_poll;
   counters.cycles = cycles;
   ioChannel.poll();
}

void UniqueTaskExecutor::handleIoSubmission(u64 cycles, u64& delay_until_cycle)
{
   constexpr int delay_submit = 64;

   if (delay_submit == 0) {
      submitIo();
   } else {
      handleDelayedIoSubmission(cycles, delay_until_cycle, delay_submit);
   }
}

void UniqueTaskExecutor::submitIo()
{
   int submitted = ioChannel.submit();
   counters.submitCalls++;
   counters.submitted += submitted;
}

void UniqueTaskExecutor::handleDelayedIoSubmission(u64 cycles, u64& delay_until_cycle, int delay_amount)
{
   if (ioChannel.submitable() > 0) {
      if (delay_until_cycle < cycles) {
         delay_until_cycle = cycles + delay_amount;
      } else if (delay_until_cycle == cycles) {
         submitIo();
      }
   }
}

bool UniqueTaskExecutor::shouldPollWorkload(u64 cycles) const
{
   constexpr int every_poll = 64;
   return cycles % every_poll == 0 && nic.active;
}

void UniqueTaskExecutor::pollWorkload()
{
   if (FLAGS_tx_rate > 0) {
      pollWorkloadWithRate();
   } else {
      pollWorkloadWithoutRate();
   }
}

void UniqueTaskExecutor::pollWorkloadWithRate()
{
   size_t requests = nic.poll();

   for (size_t i = 0; i < requests; i++) {
      UniqueTaskPtr task = createTask(workloadFunction, trampoline);
      task->context.jumpmuctx->tx_start_time = nic.get(i)->timestamp;
      tasks.push_back(std::move(task));
   }

   nic.consume(requests);
}

void UniqueTaskExecutor::pollWorkloadWithoutRate()
{
   if (FLAGS_worker_tasks - opentasks > 0) {
      size_t new_tasks = FLAGS_worker_tasks - opentasks;
      for (size_t i = 0; i < new_tasks; i++) {
         this->pushTask(workloadFunction);
      }
   }
}

int UniqueTaskExecutor::runScheduledTasks()
{
   const int max_tasks_per_cycle = 1;
   int tasks_run = 0;

   while (tasks_run < max_tasks_per_cycle && popTask(_currentTask)) {
      counters.tasksRun++;

      if (!tryAcquireTaskLock()) {
         tasks_run++;
         break;
      }

      tasks_run++;
      TaskState state = runCurrentTask();
      handleTaskState(state);
   }

   return tasks_run;
}

bool UniqueTaskExecutor::tryAcquireTaskLock()
{
   if (_currentTask->state == TaskState::ReadyLock) {
      if (!_currentTask->lock->try_lock()) {
         leanstore::WorkerCounters::myCounters().time_counter_0++;
         tasks.push_back(std::move(_currentTask));
         leanstore::ThreadCounters::myCounters().exec_tasks_st_ready_lckskip++;
         return false;
      }
      _currentTask->lock->unlock(); 
      // jumpmu::thread_local_jumpmu_ctx->lock_counter--;
      // _currentTask->context.jumpmuctx->lock_counter++; 
   }
   return true;
}

void UniqueTaskExecutor::handleTaskState(TaskState state)
{
   switch (state) {
      case TaskState::Done:
         handleDoneTask();
         break;
      case TaskState::Waiting:
         handleWaitingTask();
         break;
      case TaskState::WaitIo:
         handleWaitIoTask();
         break;
      case TaskState::ReadyMem:
         handleReadyMemTask();
         break;
      case TaskState::ReadyLock:
         handleReadyLockTask();
         break;
      case TaskState::ReadyJumpLock:
         handleReadyJumpLockTask();
         break;
      case TaskState::Ready:
         handleReadyTask();
         break;
      default:
         throw std::logic_error("Invalid task state");
   }

   leanstore::ThreadCounters::myCounters().exec_tasks_run++;
}

void UniqueTaskExecutor::handleDoneTask()
{
   counters.tasksCompleted++;
   leanstore::ThreadCounters::myCounters().exec_tasks_st_comp++;
}

void UniqueTaskExecutor::handleWaitingTask()
{
   leanstore::ThreadCounters::myCounters().exec_tasks_st_wait++;
   counters.tasksWaiting++;
   waitingTaskCount++;
}

void UniqueTaskExecutor::handleWaitIoTask()
{
   leanstore::ThreadCounters::myCounters().exec_tasks_st_wait_io++;
   counters.tasksWaiting++;
   waitingTaskCount++;
   waitIoTaskCount++;
}

void UniqueTaskExecutor::handleReadyMemTask()
{
   leanstore::ThreadCounters::myCounters().exec_tasks_st_ready_mem++;
   counters.tasksReady++;
   tasks.push_back(std::move(_currentTask));
}

void UniqueTaskExecutor::handleReadyLockTask()
{
   leanstore::ThreadCounters::myCounters().exec_tasks_st_ready_lck++;
   counters.tasksReady++;
   tasks.push_back(std::move(_currentTask));
}

void UniqueTaskExecutor::handleReadyJumpLockTask()
{
   leanstore::ThreadCounters::myCounters().exec_tasks_st_ready_jumplck++;
   counters.tasksReady++;
   tasks.push_back(std::move(_currentTask));
}

void UniqueTaskExecutor::handleReadyTask()
{
   leanstore::ThreadCounters::myCounters().exec_tasks_st_ready++;
   counters.tasksReady++;
   tasks.push_back(std::move(_currentTask));
}

void UniqueTaskExecutor::updateCycleCounters(int tasks_run, u64& cycles_nothing_run)
{
   if (tasks_run == 0) {
      leanstore::ThreadCounters::myCounters().exec_no_tasks_run++;
      cycles_nothing_run++;
   } else {
      cycles_nothing_run = 0;
   }
}

// ============================================================================
// Page Provider Integration
// ============================================================================

void UniqueTaskExecutor::registerPageProvider(void* buffer_manager_ptr, u64 partition)
{
   printf("register page provider %lu\n", partition);

   this->partition_id = partition;
   this->buffer_manager = static_cast<BufferManager*>(buffer_manager_ptr);

   ensure(buffer_manager->cooling_partitions_count > partition);
   buffer_manager->cooling_partitions[partition].state.debug_thread = mean::exec::getId();
}

void UniqueTaskExecutor::pageProviderCycle()
{
   if (buffer_manager && partition_id >= 0) {
      buffer_manager->pageProviderCycle(partition_id);
   }
}

// ============================================================================
// Task Management
// ============================================================================

bool UniqueTaskExecutor::popTask(UniqueTaskPtr& task)
{
   if (tasks_io_done.try_pop(task)) {
      return true;
   }
   return tasks.try_pop(task);
}

void UniqueTaskExecutor::pushTask(UniqueTaskPtr task)
{
   tasks.push_back(std::move(task));
}

void UniqueTaskExecutor::pushTask(TaskFunction fun)
{
   UniqueTaskPtr task = createTask(fun, trampoline);
   tasks.push_back(std::move(task));
}

void UniqueTaskExecutor::moveReady(UniqueTaskPtr task)
{
   task->state = TaskState::Ready;
   waitingTaskCount--;
   waitIoTaskCount--;
   tasks_io_done.push_back(std::move(task));
}

int UniqueTaskExecutor::taskCount()
{
   return tasks.size();
}

// ============================================================================
// Messaging
// ============================================================================

void UniqueTaskExecutor::sendMessage(int to_id, MessageFunction fun, uintptr_t user_data)
{
   messageHandler.sendMessage(to_id, fun, user_data);
}

// ============================================================================
// Static Access Methods
// ============================================================================

UniqueTaskExecutor& UniqueTaskExecutor::localExec()
{
   return static_cast<UniqueTaskExecutor&>(ThreadBase::this_thread());
}

UniqueTask& UniqueTaskExecutor::currentTask()
{
   auto task = localExec()._currentTask.get();
   ensure(task);
   return *task;
}

UniqueTaskExecutor::UniqueTaskPtr UniqueTaskExecutor::getCurrentTaskOwnership()
{
   return std::move(localExec()._currentTask);
}

}  // namespace mean