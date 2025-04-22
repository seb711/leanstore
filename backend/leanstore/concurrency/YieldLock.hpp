#pragma once
// -------------------------------------------------------------------------------------
#include <atomic>
#include <cassert>
#include <osv/jumpmu.hh>
#include <osv/leanstore_debug.hh>
#include <osv/mutex.h>

#include <emmintrin.h>
// -------------------------------------------------------------------------------------
namespace mean
{
// -------------------------------------------------------------------------------------
class SpinLock
{
   std::atomic_flag _lock = ATOMIC_FLAG_INIT;
  public:
   bool try_lock()
   {
      return !_lock.test_and_set(std::memory_order_acquire); 
   }
   void lock()
   {
      while (!try_lock()) { 
         _mm_pause();
      }
   }
   void unlock()
   {
      _lock.clear(std::memory_order_release);
   }
};
class YieldLock
{
   std::atomic_flag _lock = ATOMIC_FLAG_INIT;
   std::atomic<int> _waiting{0};
   std::atomic<int> _owner{0};
  public:
   // -------------------------------------------------------------------------------------
   bool try_lock();
   void lock();
   void unlock();
};

#ifndef MEAN_USE_JOBBING
class SharedYieldLock
{
   static constexpr int EXCLUSIVE_BIT = 1 << 31;
   std::atomic<int> _lock{0};
  public:
   bool try_lock() {
      int expected = 0;
      int desired = EXCLUSIVE_BIT;
      return _lock.compare_exchange_strong(expected, desired, std::memory_order_acquire);
   }
   void lock() {
      // TODO do something more intelligen, maybe?
      int spin = 0;
      while (!try_lock()) {
         spin++;
         if (spin > 40) {
            jumpmu::jump(jumpmu::UserJumpReason::Lock);
         }
         _mm_pause();
         _mm_pause();
         _mm_pause();
      }
   }
   void unlock() {
      assert(_lock == EXCLUSIVE_BIT);
      _lock.store(0, std::memory_order_release);
   }
   bool try_lock_shared() {
      int expected = 0;
      while (!_lock.compare_exchange_strong(expected, expected+1, std::memory_order_acquire)) {
         if (expected == EXCLUSIVE_BIT) return false; // if lock is excl lock, fail
      }
      return true;
   }
   void lock_shared() {
      // TODO do something more intelligen, maybe?
      while (!try_lock_shared()) { 
         _mm_pause();
         _mm_pause();
         _mm_pause();
      }
   }
   void unlock_shared() {
      assert(_lock != EXCLUSIVE_BIT);
      _lock.fetch_add(-1, std::memory_order_release);
   }
};
#else 
class SharedYieldLock {
   private:
      lockfree::mutex mutex;               // For writer exclusivity
       std::atomic<int> reader_count;  // Tracks active readers
       std::atomic<bool> writer_active; // Indicates if writer is active
   
   public:
   SharedYieldLock() : reader_count(0), writer_active(false) {}
   
       // Acquire read lock (shared access)
       void lock_shared() {
           // Wait if a writer is active
           while (writer_active.load(std::memory_order_acquire)) {
               leanstore_osv_debug::yield();
           }
           
           // Increment reader count
           reader_count.fetch_add(1, std::memory_order_acquire);
           
           // Double-check writer hasn't become active (writer preference)
           if (writer_active.load(std::memory_order_acquire)) {
               reader_count.fetch_sub(1, std::memory_order_release);
               lock_shared(); // Recursive call - try again
           }
       }
   
       // Try to acquire read lock (non-blocking)
       bool try_lock_shared() {
           // Fail if a writer is active
           if (writer_active.load(std::memory_order_acquire)) {
               return false;
           }
           
           // Increment reader count
           reader_count.fetch_add(1, std::memory_order_acquire);
           
           // If writer became active, rollback and fail
           if (writer_active.load(std::memory_order_acquire)) {
               reader_count.fetch_sub(1, std::memory_order_release);
               return false;
           }
           
           return true;
       }
   
       // Release read lock
       void unlock_shared() {
           reader_count.fetch_sub(1, std::memory_order_release);
       }
   
       // Acquire write lock (exclusive access)
       void lock() {
           bool expected = false;
           // Try to set writer_active from false to true
           while (!writer_active.compare_exchange_strong(expected, true,
                  std::memory_order_acquire)) {
               expected = false;
               leanstore_osv_debug::yield();
           }
           
           // Wait for all readers to finish
           while (reader_count.load(std::memory_order_acquire) > 0) {
            leanstore_osv_debug::yield();
           }
       }
   
       // Try to acquire write lock (non-blocking)
       bool try_lock() {
           bool expected = false;
           // Try to set writer_active from false to true
           if (!writer_active.compare_exchange_strong(expected, true,
               std::memory_order_acquire)) {
               return false;
           }
           
           // If there are readers, rollback and fail
           if (reader_count.load(std::memory_order_acquire) > 0) {
               writer_active.store(false, std::memory_order_release);
               return false;
           }
           
           return true;
       }
   
       // Release write lock
       void unlock() {
           writer_active.store(false, std::memory_order_release);
       }
   };
#endif
// -------------------------------------------------------------------------------------
}  // namespace mean
// -------------------------------------------------------------------------------------
