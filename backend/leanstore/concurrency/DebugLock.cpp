// -------------------------------------------------------------------------------------
#include "DebugLock.hpp"
// -------------------------------------------------------------------------------------
#include "Mean.hpp"
#include "Task.hpp"
#include "Units.hpp"
// -------------------------------------------------------------------------------------
#include <osv/leanstore_debug.hh>
#include <atomic>
#include <mutex>
// -------------------------------------------------------------------------------------
#define TRACEMUTEXLVL 0 || true
namespace mean
{
// -------------------------------------------------------------------------------------

u64 DebugLock::global_id = 0;
DebugLock::DebugLock() : id(global_id++) {};

bool DebugLock::try_lock()
{
   if (jumpmu::thread_local_jumpmu_ctx->pid >= TRACEMUTEXLVL) {
      leanstore_osv_debug::trace_try_lock(id, jumpmu::thread_local_jumpmu_ctx->pid);
   }

   auto current = jumpmu::thread_local_jumpmu_ctx->pid;
   // For non-recursive mutex, we need to detect double-locking

   bool b = mtx.try_lock();

   if (b) {
      if (jumpmu::thread_local_jumpmu_ctx->pid >= TRACEMUTEXLVL) {
         leanstore_osv_debug::trace_lock(id, jumpmu::thread_local_jumpmu_ctx->pid);
         _mm_mfence(); 
         owner.store(current);
      }
   }
   return b;
}

void DebugLock::lock()
{
   auto current = jumpmu::thread_local_jumpmu_ctx->pid;

   if (jumpmu::thread_local_jumpmu_ctx->pid >= TRACEMUTEXLVL) {
      leanstore_osv_debug::trace_wait_lock(id, jumpmu::thread_local_jumpmu_ctx->pid);
   }

   mtx.lock();
   _mm_mfence(); 
   owner.store(current);
   if (jumpmu::thread_local_jumpmu_ctx->pid >= TRACEMUTEXLVL) {
      leanstore_osv_debug::trace_lock(id, jumpmu::thread_local_jumpmu_ctx->pid);
   }
   return;
}

void DebugLock::unlock()
{
   // if (owner.load() == -3) return; 
   if (jumpmu::thread_local_jumpmu_ctx->pid >= TRACEMUTEXLVL) {
      leanstore_osv_debug::trace_wait_unlock(id, jumpmu::thread_local_jumpmu_ctx->pid);
   }
   /* auto current = jumpmu::thread_local_jumpmu_ctx->pid;
   if (owner.load() != current) {
      // Error: thread trying to unlock mutex it doesn't own
      fprintf(stderr, "%i %i\n", current, owner.load());
      throw std::runtime_error("Unlocking mutex not owned by current thread");
   } */
   mtx.unlock();
   _mm_mfence(); 
   owner.store(-3, std::memory_order_release);
   if (jumpmu::thread_local_jumpmu_ctx->pid >= TRACEMUTEXLVL) {
      leanstore_osv_debug::trace_unlock(id, jumpmu::thread_local_jumpmu_ctx->pid);
   }
   return;
}

void DebugLock::checkInit() {
   assert(owner.load() == -3); 
}

}  // namespace mean
// -------------------------------------------------------------------------------------