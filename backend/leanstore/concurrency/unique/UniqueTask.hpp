#pragma once
// -------------------------------------------------------------------------------------
#include "leanstore/concurrency/batch/Task.hpp"
#include "leanstore/concurrency/utils/MessageHandler.hpp"
#include "leanstore/concurrency/utils/YieldLock.hpp"
#include "leanstore/io/IoAbstraction.hpp"
#include "leanstore/sync-primitives/JumpMU.hpp"
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
// -------------------------------------------------------------------------------------
struct UniqueTaskContext {
   static constexpr size_t STACK_SIZE = 8192;  // 8KB

   bool init = false;
   bool wait = false;
   s64 worker_id = -1;
   alignas(64) uint8_t stack[STACK_SIZE];
   alignas(64) uint8_t interrupt_stack[STACK_SIZE];
   boost::context::detail::fcontext_t this_task_context;
   jumpmu::JumpMUContext jumpmuctx;
};
// -------------------------------------------------------------------------------------
class UniqueTask
{
  public:
   UniqueTaskContext context;

  private:
   TaskFunction fun;
   uint64_t arg;
   TaskState state = TaskState::Ready;
   static void trampoline(boost::context::detail::transfer_t t);
   friend UniqueTaskExecutor;
   friend class UniqueTaskDeleter;

  public:
   YieldLock* lock;
   UniqueTask(TaskFunction fun) : fun(fun) {}
   ~UniqueTask();
   TaskState getState();
};
// -------------------------------------------------------------------------------------
class UniqueTaskDeleter
{
   friend UniqueTask;

  public:
   UniqueTaskDeleter() noexcept = default;
   UniqueTaskDeleter(const UniqueTaskDeleter&) noexcept = default;
   UniqueTaskDeleter(UniqueTaskDeleter&&) noexcept = default;
   UniqueTaskDeleter& operator=(const UniqueTaskDeleter&) noexcept = default;
   UniqueTaskDeleter& operator=(UniqueTaskDeleter&&) noexcept = default;

   void operator()(UniqueTask* task) const;
};
// -------------------------------------------------------------------------------------
}  // namespace mean