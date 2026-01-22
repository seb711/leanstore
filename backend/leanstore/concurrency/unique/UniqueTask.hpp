#pragma once
// -------------------------------------------------------------------------------------
#include <osv/jumpmu.hh>
#include "leanstore/concurrency/batch/Task.hpp"
#include "leanstore/concurrency/utils/MessageHandler.hpp"
#include "leanstore/concurrency/utils/YieldLock.hpp"
#include "leanstore/io/IoAbstraction.hpp"
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
class UniqueTaskExecutor;
class UniqueTaskDeleter;
// -------------------------------------------------------------------------------------
struct UniqueTaskContext {
   bool init = false;
   bool wait = false;
   s64 worker_id = -1;
   void* stack;                                           // 8kb
   void* interrupt_stack;                                 // 8kb
   boost::context::detail::fcontext_t this_task_context;  // basically just a pointer
   jumpmu::JumpMUContext* jumpmuctx;  // this is also ~8kb (but the size is not dynamic you can get it with sizeof(jumpmu::JumpMUContext))
   // boost::context::continuation this_task_context;
   // boost::context::continuation* sink_process_context;
};
// -------------------------------------------------------------------------------------
class UniqueTask
{
  private:
   UniqueTaskContext context;
   TaskFunction fun;
   uint64_t arg;
   TaskState state = TaskState::Ready;
   static void trampoline(boost::context::detail::transfer_t t);
   friend UniqueTaskExecutor;
   friend UniqueTaskDeleter;

  public:
   YieldLock* lock;
   UniqueTask(TaskFunction fun) : fun(fun) {}
   ~UniqueTask();
   // -------------------------------------------------------------------------------------
   TaskState getState();
};
// -------------------------------------------------------------------------------------
}  // namespace mean
// -------------------------------------------------------------------------------------
