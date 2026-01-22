// -------------------------------------------------------------------------------------
#include "YieldLock.hpp"
#include "leanstore/concurrency/batch/Task.hpp"
// -------------------------------------------------------------------------------------
#include "Units.hpp"
#include "leanstore/concurrency/Mean.hpp"
// -------------------------------------------------------------------------------------
#include <atomic>
#include <mutex>
#include <osv/leanstore_debug.hh>
#include "arch.hh"
#include "leanstore/concurrency/unique/UniqueTaskExecutor.hpp"
// -------------------------------------------------------------------------------------
namespace mean
{
// -------------------------------------------------------------------------------------
bool YieldLock::try_lock()
{
   leanstore_osv_debug::trace_try_lock(this, mean::UniqueTaskExecutor::localExec()._currentTask.get());
   arch::irq_disable();
   bool b = !_lock.test_and_set(std::memory_order_acquire);
   if (b) {
      jumpmu::thread_local_jumpmu_ctx->lock_counter++;
      _owner = (uintptr_t)mean::UniqueTaskExecutor::localExec()._currentTask.get();  // mean::exec::getId();
      leanstore_osv_debug::trace_lock(this, mean::UniqueTaskExecutor::localExec()._currentTask.get());
   } else {
      arch::irq_enable();
   }
   return b;
}
void YieldLock::lock()
{
   // this implementation is super unsafe -> you have to be sure that you have the lock when you
   // resume after locking here -> not documented...
   // if (!try_lock()) {
   while (!try_lock()) {
      _waiting++;
      // if (_waiting > 10)
      //   abort();
      /*
       */
      // auto& this_task = mean::task::this_task();
      // this_task.lock = this;
      mean::task::set_current_task_lock(*this);
      mean::task::yield(mean::TaskState::ReadyLock);
      // must be locked at this point
      _waiting--;

#ifndef MEAN_USE_TASKING
      // break;
#endif
   }
   assert(jumpmu::thread_local_jumpmu_ctx->lock_counter > 0);
}
void YieldLock::unlock()
{
   if (_owner >= 0) {
      _owner = -1;
      _lock.clear(std::memory_order_release);
      jumpmu::thread_local_jumpmu_ctx->lock_counter--;
      assert(jumpmu::thread_local_jumpmu_ctx->lock_counter >= 0);
      arch::irq_enable();
      leanstore_osv_debug::trace_unlock(this, mean::UniqueTaskExecutor::localExec()._currentTask.get());
   }
}
}  // namespace mean
// -------------------------------------------------------------------------------------
