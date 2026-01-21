#pragma once
// -------------------------------------------------------------------------------------
#include "leanstore/concurrency/utils/ThreadBase.hpp"
#include "leanstore/concurrency/utils/YieldLock.hpp"
#include "leanstore/concurrency/utils/MessageHandler.hpp"
#include "leanstore/io/IoAbstraction.hpp"
#include <osv/jumpmu.hh>
// -------------------------------------------------------------------------------------
#include "boost/context/continuation.hpp"
#include "boost/context/continuation_fcontext.hpp"
// -------------------------------------------------------------------------------------
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <queue>
// -------------------------------------------------------------------------------------
namespace mean
{
// -------------------------------------------------------------------------------------
class Task;
class TaskExecutor;
enum class TaskState {
   New = 0,
   Ready = 1, // general yield, basically push back in queue
   ReadyMem = 2, // ready, but waiting for mem
   ReadyLock = 3, // ready, but waiting for lock
   ReadyJumpLock = 4,
   Waiting =5 , // general waiting, requires manual push ready
   WaitIo =6 ,
   Done =7,
};
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
using TaskFunction = std::function<void()>;  // std::add_pointer_t<void()>;
using StacklessFunction = std::function<void()>;
// -------------------------------------------------------------------------------------
struct TaskContext {
   bool init = false;
   boost::context::continuation this_task_context;
   boost::context::continuation* sink_process_context;
   s64 worker_id = -1;
   bool wait = false;
};
// -------------------------------------------------------------------------------------
class Task
{
   TaskContext context;
   jumpmu::JumpMUContext jumpctx;
   friend TaskExecutor;
   TaskFunction fun;
   TaskState state = TaskState::Ready;
  public:
   YieldLock* lock;
   Task(TaskFunction fun) : fun(fun) {}
   ~Task();
   void* userContext;
   // -------------------------------------------------------------------------------------
   long dbg = 0;
   long dbgAddr = 0;
   // -------------------------------------------------------------------------------------
   TaskState getState();
};
// -------------------------------------------------------------------------------------
class ThreadWithJump : public Thread
{
   jumpmu::JumpMUContext jump_context;
  public:
   // -------------------------------------------------------------------------------------
   struct meta_t {
      std::mutex mutex;
      std::condition_variable cv;
      TaskFunction task;
      bool wt_ready = true;
      bool job_set = false;
      bool job_done = false;
   } meta;
   std::atomic<ThreadWithJump*> next; 
   leanstore::cr::Worker* this_worker;
   ThreadWithJump(std::function<void()> fun, std::string name = "Thread", int id = -1) : Thread(fun, name, id) {}
   int process() override
   {
      jumpmu::thread_local_jumpmu_ctx = &jump_context;
      fun();
      jumpmu::thread_local_jumpmu_ctx = nullptr;
      return 0;
   }
   void sendTask(TaskFunction taskFun) {
      std::unique_lock guard(meta.mutex);
      meta.cv.wait(guard, [&]() { return !meta.job_set && meta.wt_ready; });
      meta.job_set = true;
      meta.job_done = false;
      meta.task = taskFun;
      guard.unlock();
      meta.cv.notify_one();
      //guard.lock();
      //meta.cv.wait(guard, [&]() { return meta.job_done; });
   }
   void sendTaskBlocking(TaskFunction taskFun) {
      sendTask(taskFun);
      std::unique_lock guard(meta.mutex);
      meta.cv.wait(guard, [&]() { return meta.job_done; });
   }
   void shutdown() {
      stop();
      meta.cv.notify_one();
   }
};
// -------------------------------------------------------------------------------------
}  // namespace mean
// -------------------------------------------------------------------------------------
