// DebugLock.hpp
#pragma once

#include <atomic>
#include <mutex>
#include "Units.hpp"
#include <emmintrin.h>
#include <osv/jumpmu.hh>
#include <osv/mutex.h>

// Define USE_SPIN externally or here if needed
// #define USE_SPIN

namespace mean
{

#ifdef USE_SPIN
// Simple spinlock implementation
class OsvSpinLock
{
private:
    std::atomic<int> owner{-3};  // 0 means no owner
    std::atomic<bool> flag{false};

public:
    void lock() {
        auto current = jumpmu::thread_local_jumpmu_ctx->pid;
        
        // For non-recursive mutex, we need to detect double-locking
        if (owner.load() == current) {
            // Current thread already owns the lock - this is an error in non-recursive mutex
            fprintf(stderr, "[lock] Double lock attempt by same thread %i\n", current);

            throw std::runtime_error("Double lock attempt by same thread");
        }
        
        // Otherwise do normal locking
        while (!try_lock()) {
            // Pause instruction to reduce CPU consumption
            asm volatile("pause" ::: "memory");
        }
    }
    
    bool try_lock() {
        auto current = jumpmu::thread_local_jumpmu_ctx->pid;
        
        // For non-recursive mutex, we need to detect double-locking
        if (owner.load() == current) {
            // Current thread already owns the lock - this is an error in non-recursive mutex
            fprintf(stderr, "[try lock] Double lock attempt by same thread %i\n", current);
            throw std::runtime_error("Double lock attempt by same thread");
        }
        
        bool expected = false;
        if (flag.compare_exchange_strong(expected, true)) {
            // Got the lock
            owner.store(current);
            return true;
        }
        return false;
    }
    
    void unlock() {
        auto current = jumpmu::thread_local_jumpmu_ctx->pid;
        
        if (owner.load() != current) {
            // Error: thread trying to unlock mutex it doesn't own
            printf("%i %i\n", current, owner.load());
            throw std::runtime_error("Unlocking mutex not owned by current thread");
        }
        
        // Properly reset the owner to indicate no owner (use 0 instead of pid)
        owner.store(-3);
        flag.store(false);
    }
};
#endif

class DebugLock
{
  private:
   static u64 global_id;
   std::atomic<int> owner{-3};  // 0 means no owner
   const u64 id;

#ifdef USE_SPIN
   OsvSpinLock mtx;
#else
   // std::mutex mtx;
   lockfree::mutex mtx; 
#endif

  public:
   DebugLock();
   void lock();
   bool try_lock();
   void unlock();
   void checkInit();
};

}  // namespace mean