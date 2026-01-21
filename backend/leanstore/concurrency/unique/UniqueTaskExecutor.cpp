// -------------------------------------------------------------------------------------
#include "UniqueTaskExecutor.hpp"
// -------------------------------------------------------------------------------------
#include "Exceptions.hpp"
#include "Time.hpp"
#include "leanstore/concurrency-recovery/Worker.hpp"
#include "leanstore/concurrency/Mean.hpp"
#include "leanstore/concurrency/utils/MessageHandler.hpp"
#include "leanstore/concurrency/utils/ThreadBase.hpp"
#include "leanstore/concurrency/unique/UniqueTask.hpp"
#include "leanstore/profiling/counters/CPUCounters.hpp"
#include "leanstore/profiling/counters/ThreadCounters.hpp"
#include "leanstore/profiling/counters/WorkerCounters.hpp"
#include "leanstore/utils/RandomGenerator.hpp"
// -------------------------------------------------------------------------------------
#include "boost/context/continuation.hpp"
// -------------------------------------------------------------------------------------
#include <boost/context/preallocated.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <drivers/clockevent.hh>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <osv/clock.hh>
#include <osv/leanstore_debug.hh>
#include <queue>
#include <thread>
#include <tuple>
#include "arch.hh"
#include "exceptions.hh"
#include "processor.hh"
// -------------------------------------------------------------------------------------
namespace mean
{
// -------------------------------------------------------------------------------------
#if true  // MACRO_COUNTERS_ALL
#define DEBUG_TASK_COUNTERS_BLOCK(X) X
#else
#define DEBUG_TASK_COUNTERS_BLOCK(X)
#endif

thread_local uint64_t opentasks = 0;
// std::atomic<bool> TaskExecutor::pause = true;
// std::atomic<int> TaskExecutor::pause_seen = 0;
//  HERE WE NEED TO DEFINE ALL THE FUNCTION OF FIBERS
//  trampoline
thread_local TaskContextPool* tl_task_context_pool;
thread_local bool run_task = false;
thread_local std::atomic<uint64_t> last_timestamp = {0};
// onjump
boost::context::detail::transfer_t UniqueTaskExecutor::store_sink_on_yield(boost::context::detail::transfer_t t)
{
   auto* self = reinterpret_cast<UniqueTaskExecutor*>(t.data);
   self->updateCurrentSink(t.fctx);
   return {nullptr, nullptr};
}

boost::context::detail::transfer_t UniqueTaskExecutor::store_task_on_yield(boost::context::detail::transfer_t t)
{
   auto* self = reinterpret_cast<UniqueTask*>(t.data);
   // self->updateCurrentSink(t.fctx);
   self->context.this_task_context = t.fctx;
   return {nullptr, nullptr};
}

// RAII wrapper implementation
void UniqueTaskDeleter::operator()(UniqueTask* task) const
{
   opentasks--; 
   // leanstore_osv_debug::trace_unlock(task, &UniqueTaskExecutor::localExec()); 
   if (task) {
      UniqueTaskExecutor::localExec().g_task_context_pool->deallocate(task->context);
   }
   delete task;
}

void UniqueTaskExecutor::trampoline(boost::context::detail::transfer_t t)
{
   // assert(!arch::irq_enabled());
   auto& self = UniqueTaskExecutor::localExec();
   uint64_t arg = reinterpret_cast<uint64_t>(t.data);
   self.updateCurrentSink(t.fctx);

#ifdef USE_INTERRUPTS
   arch::irq_enable();
#if !defined(USE_PERIODIC_TIMER) && !defined(USE_WATCHDOG)
   clock_event->set(std::chrono::nanoseconds(INTERRUPT_TIME));
#endif
#endif
   // enable_interrupts();
   self._currentTask->fun();

#ifdef USE_WATCHDOG
   last_timestamp = 0;
#endif
   // disable_interrupts();

   // Run fiber function once

   // Finished - jump back to sink and never return
   {
#if defined(USE_INTERRUPTS)
      // clock_event->disable();
      leanstore_osv_debug::set_interrupt_stack((char*)localExec()._sinkInterruptStack);
#endif
      // sched::current_cpu->arch.set_ist_entry(2, sink_istack, 8192);
      // self._currentTask = nullptr; // i guess this is not needed here but we do it nonetheless
      // std::cout << "TASK HAS ENDED " << std::hex << self._currentTask.get() << std::endl;
      self._currentTask->state = TaskState::Done;
      jumpmu::thread_local_jumpmu_ctx = &self.defaultExecutorContext;  // same here
      boost::context::detail::jump_fcontext(self.getCurrentSink(), nullptr);
   }
}

// Global pool management
void UniqueTaskExecutor::initTaskContextPool(size_t capacity)
{
   if (g_task_context_pool)
      throw std::runtime_error("Pool already initialized");
   g_task_context_pool = new TaskContextPool(capacity);
}

void UniqueTaskExecutor::destroyTaskContextPool()
{
   delete g_task_context_pool;
   g_task_context_pool = nullptr;
}

// Helper to create initialized context with boost make_fcontext
void UniqueTaskExecutor::initializeTaskContext(UniqueTaskContext& ctx,
                                               void* stack_base,
                                               size_t stack_size,
                                               void (*fn)(boost::context::detail::transfer_t))
{
   ctx.this_task_context = boost::context::detail::make_fcontext(static_cast<char*>(stack_base) + stack_size, stack_size, fn);
   // ctx.init = true;
}

// Factory function to create a task with pre-allocated memory
UniqueTaskExecutor::UniqueTaskPtr UniqueTaskExecutor::createTask(TaskFunction fun, void (*entry_fn)(boost::context::detail::transfer_t))
{
   if (!g_task_context_pool)
      throw std::runtime_error("Pool not initialized");

   auto task = new UniqueTask(fun);
   g_task_context_pool->allocate(task->context);
   initializeTaskContext(task->context, task->context.stack, TaskContextPool::getStackSize(), entry_fn);

   // leanstore_osv_debug::trace_lock(task, &UniqueTaskExecutor::localExec()); 

   opentasks++;

   return UniqueTaskPtr(task);
}

// -------------------------------------------------------------------------------------
UniqueTaskExecutor::UniqueTaskExecutor(MessageHandler& msg, IoChannel& ioChannel, DummyNIC& nic, int id)
    : ThreadBase("te_" /*+ std::to_string(id)*/, id), messageHandler(msg), ioChannel(ioChannel), nic(nic)
{
   jumpmu::thread_local_jumpmu_ctx = &defaultExecutorContext;

   initTaskContextPool(5000);
   tl_task_context_pool = g_task_context_pool;

// we need to save the interrupt stack here
#ifdef USE_INTERRUPTS
   arch::irq_disable();

   auto timer_handler = []() {
      leanstore::WorkerCounters::myCounters().time_counter_1++;
      if (!run_task) {
         leanstore::WorkerCounters::myCounters().time_counter_2++;
         return;
      }

      assert(!arch::irq_enabled());

      void* b;
      __asm__ volatile("mov %%rbp, %0" : "=r"(b) : :);

      if (UniqueTaskExecutor::localExec()._currentTask != nullptr && UniqueTaskExecutor::localExec()._currentSink != nullptr) {
         // for now we here do nothing more than just interrupting and yield to the next one
         // this is enough for now but in the future we need here a more thorough logic

#ifdef USE_WATCHDOG
         last_timestamp = 0;
#endif

         leanstore_osv_debug::set_interrupt_stack((char*)UniqueTaskExecutor::localExec()._sinkInterruptStack);
         assert(UniqueTaskExecutor::localExec()._currentSink != nullptr);
         assert(UniqueTaskExecutor::localExec()._currentTask.get() != nullptr);

         boost::context::detail::ontop_fcontext(UniqueTaskExecutor::localExec().getCurrentSink(),
                                                (void*)UniqueTaskExecutor::localExec()._currentTask.get(), UniqueTaskExecutor::store_task_on_yield);

#if !defined(USE_PERIODIC_TIMER) && !defined(USE_WATCHDOG)
         clock_event->set(std::chrono::nanoseconds(INTERRUPT_TIME));
#endif
         return;
      } else {
         // in the case that we are not running a job right now we can just return and skip the logic
         return;
      }
   };

   // Register the timer handler
   auto vector = idt.register_handler(timer_handler);

#ifndef USE_WATCHDOG
   clock_event->reset_vector(vector);
#ifdef USE_PERIODIC_TIMER
   clock_event->set_periodic(true);
   clock_event->set(std::chrono::nanoseconds(INTERRUPT_TIME));
#endif
#else
   clock_event->set_periodic(true);
   clock_event->disable();
   // then we are basically good to go

   // what we need to do in the other logics
   // set the working flag correct

   // IF LAPIC TIMER SET
   // just removes the current LAPIC timer handler from the core
   // bend the lapic timer to the vector we have just created
   // set timer to 0
   // IF WATCHDOG THREAD
   // register worker thread
   // END
   // how would you implement a watchdog thread?
   // 0. store the starting times for the thread to know if we should preempt
   // 1. start watchdog thread that continously checks on the current core the timer
   // 2. if too high sends the interrupt
   leanstore_osv_debug::create_watchdog(INTERRUPT_TIME, 2, vector, last_timestamp);
#endif
   arch::irq_enable();
#endif

   printf("create unique task executor %i\n", id);
   _sinkInterruptStack = static_cast<char*>(malloc(8192)) + 8192;
   // leanstore_osv_debug::get_interrupt_stack();

   // setup the interrupt here
}
UniqueTaskExecutor::~UniqueTaskExecutor()
{
   destroyTaskContextPool();
}
// -------------------------------------------------------------------------------------
TaskState UniqueTaskExecutor::runCurrentTask()
{
   // std::cout << "currentCtx " << std::hex << _currentTask.get() << " runUserThread swap oucp: " << std::hex <<  _currentTask->context.jumpmuctx <<
   // std::dec <<  std::endl << std::flush; _currentTask = task;
   auto task = _currentTask.get();
   jumpmu::thread_local_jumpmu_ctx = _currentTask->context.jumpmuctx;

// printf("run current task\n");
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
#ifdef USE_INTERRUPTS
   arch::irq_disable();
   run_task = true;
#ifdef USE_WATCHDOG
   last_timestamp = processor::rdtsc();
#endif
   leanstore_osv_debug::set_interrupt_stack((char*)task->context.interrupt_stack);
#endif

   if (!_currentTask->context.init) {
      // normal start
      // std::cout << "xx runUserThread init done  " << std::endl;
      _currentTask->context.init = true;
      boost::context::detail::jump_fcontext(_currentTask->context.this_task_context, (void*)_currentTask->arg);

   } else {
      // std::cout << "xx runUserThread resume " << std::endl;
      // switch to
      boost::context::detail::ontop_fcontext(_currentTask->context.this_task_context, this, this->store_sink_on_yield);
   }
#ifdef USE_INTERRUPTS
   run_task = false;
#ifdef USE_WATCHDOG
   last_timestamp = 0;
#endif
   arch::irq_enable();
#endif
   // in case of an IO the currentTask is now invalid
   // else it is still valid
   jumpmu::thread_local_jumpmu_ctx = &defaultExecutorContext;
   // std::cout << "rundUserThread swap end " << (uint64_t) task->getState() <<  std::endl << std::flush;
   return task->getState();
}
// -------------------------------------------------------------------------------------
void UniqueTaskExecutor::yieldCurrentTask(TaskState ts)
{
   assert(arch::irq_enabled());

   if (localExec()._currentTask.get() == nullptr) {
      // return;
      abort();
   }
   // std::cout << "yield with state " << (uint64_t) ts << std::endl << std::flush;
#ifdef USE_INTERRUPTS
   arch::irq_disable();
#if !defined(USE_PERIODIC_TIMER) && !defined(USE_WATCHDOG)
   clock_event->disable();
#endif
#endif

   UniqueTask& task = currentTask();
   task.state = ts;
   // cycle(); TODO directly run scheduler and jump to next context
   // this would return to process task.
   // *task.context.sink_process_context = task.context.sink_process_context->resume();
   auto& localTaskExecutor = UniqueTaskExecutor::localExec();
   auto sink_fcontext = localTaskExecutor.getCurrentSink();

#ifdef USE_INTERRUPTS
   leanstore_osv_debug::set_interrupt_stack((char*)UniqueTaskExecutor::localExec()._sinkInterruptStack);
#endif
   boost::context::detail::ontop_fcontext(sink_fcontext, (void*)&task, localTaskExecutor.store_task_on_yield);
   // Careful: function will continue here only when the task is being resumed
   // std::cout << "continue" << std::endl << std::flush;
#if defined(USE_INTERRUPTS)
#if !defined(USE_PERIODIC_TIMER) && !defined(USE_WATCHDOG)
   clock_event->set(std::chrono::nanoseconds(INTERRUPT_TIME));
#endif
   arch::irq_enable();
#endif
   return;
}
void UniqueTaskExecutor::yieldRunningTask(UniqueTask* task, TaskState ts)
{
   assert(arch::irq_enabled());
#ifdef USE_INTERRUPTS
   arch::irq_disable();
#if !defined(USE_PERIODIC_TIMER) && !defined(USE_WATCHDOG)
   clock_event->disable();
#endif
#endif

   task->state = ts;

   // cycle(); TODO directly run scheduler and jump to next context
   // this would return to process task.
   // *task.context.sink_process_context = task.context.sink_process_context->resume();
   auto& localTaskExecutor = UniqueTaskExecutor::localExec();
   auto sink_fcontext = localTaskExecutor.getCurrentSink();

#ifdef USE_INTERRUPTS
   leanstore_osv_debug::set_interrupt_stack((char*)UniqueTaskExecutor::localExec()._sinkInterruptStack);
#endif

   boost::context::detail::ontop_fcontext(sink_fcontext, (void*)task, localTaskExecutor.store_task_on_yield);
#if defined(USE_INTERRUPTS)
#if !defined(USE_PERIODIC_TIMER) && !defined(USE_WATCHDOG)
   clock_event->set(std::chrono::nanoseconds(INTERRUPT_TIME));
#endif
   arch::irq_enable();
#endif
   return;
}
// -------------------------------------------------------------------------------------
int UniqueTaskExecutor::process()
{
   ensure(tasks.size() == 0);
   leanstore::cr::Worker::tls_ptr = this_worker;
   leanstore::CPUCounters::registerThread(std::to_string(id()), false);
   cycle();
   return 0;
}

bool UniqueTaskExecutor::popTask(UniqueTaskPtr& task)
{
   if (tasks_io_done.try_pop(task)) {
      return true;
   }
   return tasks.try_pop(task);
}
void UniqueTaskExecutor::cycle()
{
   // -------------------------------------------------------------------------------------
   u64 cycles = 0;
   u64 cyclesNothingRun = 0;
   const u64 sleepIfNothingRunForCycles = 100000;
   u64 delaySubmitUntilCycle = 0;
   auto counterUpdateTime = getSeconds();
   random_generator.seed(mean::exec::getId());

   while (_keep_running) {
      if (sleep != 0) {
         float s = sleep.exchange(0);
         std::cout << "sleep for: " << s << std::endl;
         std::this_thread::sleep_for(std::chrono::nanoseconds((uint64_t)(s * 1e9)));
      }
      cycles++;
      // good for ycsb 60 thr: p: 2, pp: 32, d: 0
      constexpr int everyPoll = 64;
      constexpr int everyPP = 32;
      constexpr int delaySubmit = 64;

      // TODO for runs with >> 60 threads, this hast to be changed
      if (cycles % (8 * 1024) == 0 || cyclesNothingRun > sleepIfNothingRunForCycles) {
         messageHandler.poll(this);
         counters.msgPollCalled++;
      }

      if (cycles % everyPP == 0) {
         pageProviderCycle();
      }
      if (delaySubmit == 0) {
         int submitted = ioChannel.submit();
         DEBUG_TASK_COUNTERS_BLOCK(counters.submitCalls++; counters.submitted += submitted;)
      } else {
         if (ioChannel.submitable() > 0) {
            if (delaySubmitUntilCycle < cycles) {
               delaySubmitUntilCycle = cycles + delaySubmit;
            } else if (delaySubmitUntilCycle == cycles) {
               int submitted = ioChannel.submit();
               DEBUG_TASK_COUNTERS_BLOCK(counters.submitCalls++; counters.submitted += submitted;)
            }
         }
      }
      if (cycles % everyPoll == 0) {
         leanstore::ThreadCounters::myCounters().exec_cycles += everyPoll;
         counters.cycles = cycles;
         ioChannel.poll();
      }

      // i want something here that
      // we can poll here for new work on the "nic"
      if (cycles % everyPoll == 0 and nic.active) {
         if (FLAGS_tx_rate > 0) {
            // nic.poll();
            // problem: we only poll for new "keys" not for what kind of functions we want to poll...
            // nic.poll(); in the best case polls keys
            size_t requests = nic.poll();
            // if (cycles % (everyPoll * 128) == 0) {
            //    std::cout << "test " << requests << std::endl;
            // }
            // leanstore::WorkerCounters::myCounters().total_time_sum_0 += requests;
            // leanstore::WorkerCounters::myCounters().time_counter_0++;

            for (int i = 0; i < requests; i++) {
               UniqueTaskPtr task = createTask(workloadFunction, trampoline);  // new UniqueTask(fun);
               task->context.jumpmuctx->tx_start_time = nic.get(i)->timestamp;
               tasks.push_back(std::move(task));
            }

            nic.consume(requests);
         } else {
            if (FLAGS_worker_tasks - opentasks > 0) {
               size_t newtasks = FLAGS_worker_tasks - opentasks;
               for (int i = 0; i < newtasks; i++) {
                  this->pushTask(workloadFunction);
               }
            }
         }
      }

      const int maxTasksRun = 1;
      int tasksRun = 0;
      // WE HAVE HERE A INDIVIUAL SCHEDULING DECISION -> IO TASKS GET WORKED THROUGH FIRST -> WHY?
      while (tasksRun < maxTasksRun && popTask(_currentTask)) {  // pop after maxTaskRun check
         counters.tasksRun++;
         if (_currentTask->state == TaskState::ReadyLock) {
            tasksRun++;
            if (!_currentTask->lock->try_lock()) {
               leanstore::WorkerCounters::myCounters().time_counter_0++;
               tasks.push_back(std::move(_currentTask));
               COUNTERS_BLOCK()
               {
                  leanstore::ThreadCounters::myCounters().exec_tasks_st_ready_lckskip++;
               }
               break;
            }
            // _currentTask->lock->unlock();
         } else {
            tasksRun++;
         }
         TaskState state = runCurrentTask();
         switch (state) {
            case TaskState::Done:
               counters.tasksCompleted++;
               COUNTERS_BLOCK()
               {
                  leanstore::ThreadCounters::myCounters().exec_tasks_st_comp++;
               }
               // memset((void*)task, 0, sizeof(Task));
               // delete task; //FIXME
               // here the task should be raii deleted then
               break;
            case TaskState::Waiting:
               COUNTERS_BLOCK()
               {
                  leanstore::ThreadCounters::myCounters().exec_tasks_st_wait++;
               }
               counters.tasksWaiting++;
               waitingTaskCount++;
               break;
            case TaskState::WaitIo:
               COUNTERS_BLOCK()
               {
                  leanstore::ThreadCounters::myCounters().exec_tasks_st_wait_io++;
               }
               counters.tasksWaiting++;
               waitingTaskCount++;
               waitIoTaskCount++;
               break;
            case TaskState::ReadyMem:
               COUNTERS_BLOCK()
               {
                  leanstore::ThreadCounters::myCounters().exec_tasks_st_ready_mem++;
               }
               counters.tasksReady++;
               tasks.push_back(std::move(_currentTask));
               break;
            case TaskState::ReadyLock:
               COUNTERS_BLOCK()
               {
                  leanstore::ThreadCounters::myCounters().exec_tasks_st_ready_lck++;
               }
               counters.tasksReady++;
               tasks.push_back(std::move(_currentTask));
               break;
            case TaskState::ReadyJumpLock:
               COUNTERS_BLOCK()
               {
                  leanstore::ThreadCounters::myCounters().exec_tasks_st_ready_jumplck++;
               }
               counters.tasksReady++;
               tasks.push_back(std::move(_currentTask));
               break;
            case TaskState::Ready:
               COUNTERS_BLOCK()
               {
                  leanstore::ThreadCounters::myCounters().exec_tasks_st_ready++;
               }
               counters.tasksReady++;
               tasks.push_back(std::move(_currentTask));
               break;
            default:
               throw std::logic_error("should never happen");
         }
         COUNTERS_BLOCK()
         {
            leanstore::ThreadCounters::myCounters().exec_tasks_run++;
         }
      }
      if (tasksRun == 0) {
         COUNTERS_BLOCK()
         {
            leanstore::ThreadCounters::myCounters().exec_no_tasks_run++;
         }
         cyclesNothingRun++;
      } else {
         cyclesNothingRun = 0;
      }
   }
}
void UniqueTaskExecutor::registerPageProvider(void* bm_ptr, u64 partition_id)
{
   printf("register page provider %lu\n", partition_id); 
   this->partition_id = partition_id;
   this->buffer_manager = static_cast<BufferManager*>(bm_ptr);
   ensure(buffer_manager->cooling_partitions_count > partition_id);
   buffer_manager->cooling_partitions[partition_id].state.debug_thread = mean::exec::getId();
}
void UniqueTaskExecutor::pageProviderCycle()
{
   if (buffer_manager && partition_id >= 0) {
      buffer_manager->pageProviderCycle(partition_id);
   }
}
void UniqueTaskExecutor::pushTask(UniqueTaskPtr task)
{
   tasks.push_back(std::move(task));
}
void UniqueTaskExecutor::pushTask(TaskFunction fun)
{
   UniqueTaskPtr task = createTask(fun, trampoline);  // new UniqueTask(fun);
   tasks.push_back(std::move(task));
}
void UniqueTaskExecutor::moveReady(UniqueTaskPtr task)
{
   // std::cout << "move task " << std::hex << task.get() << " to ready" << std::endl;
   task->state = TaskState::Ready;
   waitingTaskCount--;
   waitIoTaskCount--;
   tasks_io_done.push_back(std::move(task));
}
int UniqueTaskExecutor::taskCount()
{
   return tasks.size();
}
void UniqueTaskExecutor::sendMessage(int toId, MessageFunction fun, uintptr_t userData)
{
   messageHandler.sendMessage(toId, fun, userData);
}
UniqueTaskExecutor& UniqueTaskExecutor::localExec()
{
   return static_cast<UniqueTaskExecutor&>(ThreadBase::this_thread());
}
UniqueTask& UniqueTaskExecutor::currentTask()
{
   auto t = localExec()._currentTask.get();
   ensure(t);
   return *t;
}
UniqueTaskExecutor::UniqueTaskPtr UniqueTaskExecutor::getCurrentTaskOwnership()
{
   return std::move(localExec()._currentTask);
}
// -------------------------------------------------------------------------------------
}  // namespace mean
// -------------------------------------------------------------------------------------