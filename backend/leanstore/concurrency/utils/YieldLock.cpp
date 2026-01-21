// -------------------------------------------------------------------------------------
#include "YieldLock.hpp"
#include "leanstore/concurrency/batch/Task.hpp"
// -------------------------------------------------------------------------------------
#include "leanstore/concurrency/Mean.hpp"
#include "Units.hpp"
// -------------------------------------------------------------------------------------
#include <atomic>
#include <mutex>
#include "arch.hh"
#include <osv/leanstore_debug.hh>
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
      _owner = (uintptr_t) mean::UniqueTaskExecutor::localExec()._currentTask.get(); // mean::exec::getId();
      leanstore_osv_debug::trace_lock(this, mean::UniqueTaskExecutor::localExec()._currentTask.get()); 
   }
   arch::irq_enable(); 
   return b;
}
void YieldLock::lock()
{
#ifdef MEAN_USE_TASKING
   // this implementation is super unsafe -> you have to be sure that you have the lock when you 
   // resume after locking here -> not documented... 
   if (!try_lock()) {
#else
   while (!try_lock()) {
#endif
      _waiting++;
      if(_waiting > 40)
         abort();
      /*
         */
      // auto& this_task = mean::task::this_task();
      // this_task.lock = this;
      mean::task::set_current_task_lock(*this); 
      mean::task::yield(mean::TaskState::ReadyLock);
      // must be locked at this point
      _waiting--;

#ifndef MEAN_USE_TASKING

      break; 

#endif
   }
}
void YieldLock::unlock()
{
   _owner = -1;
   _lock.clear(std::memory_order_release);
   arch::irq_enable(); 
   leanstore_osv_debug::trace_unlock(this, mean::UniqueTaskExecutor::localExec()._currentTask.get()); 
}
}  // namespace mean
// -------------------------------------------------------------------------------------
