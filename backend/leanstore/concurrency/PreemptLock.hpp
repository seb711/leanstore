#pragma once
#include <mutex>
#include <functional>
#include <osv/leanstore_debug.hh>
#include <osv/mutex.h>

class PreemptLock {
private:
    lockfree::mutex mutex_ = {};
public:
    PreemptLock() = default;
    
    void lock() {
        if (sched_getcpu() > 0) {
            leanstore_osv_debug::yield();
        }
        auto t = mutex_.owner.load(); 
        assert(t == nullptr); 
        leanstore_osv_debug::disable_preempt(); 
        mutex_.lock();
    }

    void unlock() {
        mutex_.unlock();
        leanstore_osv_debug::enable_preempt(); 
    }

    bool try_lock() {
        bool locked = mutex_.try_lock();
        if (locked) {
            leanstore_osv_debug::disable_preempt(); 
        }
        return locked; 
    }
};