#ifndef UNIQUETASKEXEC
#define UNIQUETASKEXEC
// -------------------------------------------------------------------------------------
#include "./helper/ContextPool.hpp"
#include "./helper/DummyNic.hpp"
#include "Exceptions.hpp"
#include "UniqueTask.hpp"
#include "Units.hpp"
#include "leanstore/Config.hpp"
#include "leanstore/concurrency-recovery/Worker.hpp"
#include "leanstore/concurrency/utils/MessageHandler.hpp"
#include "leanstore/concurrency/utils/ThreadBase.hpp"
#include "leanstore/io/IoAbstraction.hpp"
#include "leanstore/profiling/counters/TaskExecutorCounters.hpp"
#include "leanstore/storage/btree/core/BTreeInterface.hpp"
#include "leanstore/storage/buffer-manager/BufferFrame.hpp"
#include "leanstore/storage/buffer-manager/BufferManager.hpp"
// -------------------------------------------------------------------------------------
#include "boost/context/continuation.hpp"
#include "boost/context/continuation_fcontext.hpp"
// -------------------------------------------------------------------------------------
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <queue>
#include <unordered_map>

// #define USE_INTERRUPTS
// #define USE_PERIODIC_TIMER
// #define USE_WATCHDOG
#define INTERRUPT_TIME FLAGS_tmp

// -------------------------------------------------------------------------------------
namespace mean
{
// -------------------------------------------------------------------------------------
// RAII wrapper for UniqueTask to auto-release memory
class UniqueTaskDeleter
{
   friend UniqueTask;

  public:
   void operator()(UniqueTask* task) const;
};

class UniqueTaskExecutor : public ThreadBase
{
  public:
   using UniqueTaskPtr = std::unique_ptr<UniqueTask, UniqueTaskDeleter>;
   TaskContextPool* g_task_context_pool;
   void* _sinkInterruptStack;
   UniqueTaskPtr _currentTask = nullptr;
   boost::context::detail::fcontext_t _currentSink = nullptr;

  private:
   // EVERY TASK EXECUTER MANAGES OWN THREAD POOL
   // THIS REDUCES CONCURRENCY ISSUES
   // Global pool management
   void initTaskContextPool(size_t capacity);
   void destroyTaskContextPool();

   // Helper to create initialized context with boost make_fcontext
   void initializeTaskContext(UniqueTaskContext& ctx, void* stack_base, size_t stack_size, void (*fn)(boost::context::detail::transfer_t));

   // Factory function to create a task with pre-allocated memory
   UniqueTaskPtr createTask(TaskFunction fun, void (*entry_fn)(boost::context::detail::transfer_t));

   static const int MAX_TASKS = 1 << 14;
   leanstore::utils::RingBuffer<UniqueTaskPtr> tasks{MAX_TASKS};
   leanstore::utils::RingBuffer<UniqueTaskPtr> tasks_io_done{MAX_TASKS};
#ifndef NDEBUG
   std::unordered_map<UniqueTask*, std::tuple<TaskState, TimePoint, bool>> waiting_tasks;
#endif
   TaskFunction workloadFunction;

   // -------------------------------------------------------------------------------------
   //
   MessageHandler& messageHandler;
   jumpmu::JumpMUContext defaultExecutorContext;
   boost::context::continuation main_process_context;
   // -------------------------------------------------------------------------------------
   // -------------------------------------------------------------------------------------
   // -------------------------------------------------------------------------------------
   std::atomic<u64> waitingTaskCount = {0};
   std::atomic<u64> waitIoTaskCount = {0};
   // -------------------------------------------------------------------------------------
   TaskState runCurrentTask();
   int process() override;
   void cycle();
   // -------------------------------------------------------------------------------------
   // -------------------------------------------------------------------------------------
   s64 partition_id = -1;
   BufferManager* buffer_manager;
   s64 pp_required = 0;
   bool local_pause_seen = false;

  public:
   std::atomic<float> sleep;
   IoChannel& ioChannel;
   DummyNIC& nic;
   TaskExecutorCounters counters;
   // -------------------------------------------------------------------------------------
   leanstore::cr::Worker* this_worker;
   // -------------------------------------------------------------------------------------
   UniqueTaskExecutor(MessageHandler& msg, IoChannel& io, DummyNIC& nic, int id);
   ~UniqueTaskExecutor();
   // -------------------------------------------------------------------------------------
   UniqueTaskExecutor(const UniqueTaskExecutor&) = delete;
   UniqueTaskExecutor(UniqueTaskExecutor&&) = delete;
   UniqueTaskExecutor& operator=(const UniqueTaskExecutor&) = delete;
   UniqueTaskExecutor& operator=(UniqueTaskExecutor&&) = delete;
   // -------------------------------------------------------------------------------------
   static void trampoline(boost::context::detail::transfer_t t);
   static boost::context::detail::transfer_t store_sink_on_yield(boost::context::detail::transfer_t t);
   static boost::context::detail::transfer_t store_task_on_yield(boost::context::detail::transfer_t t);
   // -------------------------------------------------------------------------------------
   bool popTask(UniqueTaskPtr& task);
   // -------------------------------------------------------------------------------------
   void registerPageProvider(void*, u64 partition_id);
   void pageProviderCycle();
   void pushTask(UniqueTaskPtr task);
   void pushTask(TaskFunction fun);
   void moveReady(UniqueTaskPtr task);
   int taskCount();
   void sendMessage(int toId, MessageFunction fun, uintptr_t userData);
   // -------------------------------------------------------------------------------------
   void updateCurrentSink(boost::context::detail::fcontext_t t)
   {
      // std::cout << "current sink set: " << std::hex << t << std::endl << std::hex;
      _currentSink = t;
   }
   boost::context::detail::fcontext_t getCurrentSink()
   {
      // std::cout << "current sink get: " << std::hex << _currentSink << std::endl << std::flush;
      // assert(false); 
      ensure(_currentSink != nullptr);
      assert(_currentSink != nullptr); 

      auto tmp = _currentSink;
      _currentSink = nullptr;
      return tmp;
   }
   void set_workload_function(TaskFunction fun) { workloadFunction = fun; }
   // -------------------------------------------------------------------------------------
   static UniqueTaskExecutor& localExec();
   static UniqueTask& currentTask();
   static UniqueTaskPtr getCurrentTaskOwnership();
   static void yieldCurrentTask(TaskState ts);
   static void yieldRunningTask(UniqueTask* task, TaskState ts);
};
// -------------------------------------------------------------------------------------
extern thread_local bool run_task;
extern thread_local std::atomic<uint64_t> last_timestamp;
}  // namespace mean
// -------------------------------------------------------------------------------------
#endif