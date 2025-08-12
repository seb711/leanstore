#pragma once
#include <mutex>
#include <functional>
#include <osv/leanstore_debug.hh>
#include <osv/mutex.h>

class PreemptLock {
private:
// MAKE SURE THAT THIS LOCK IS ONLY USED CORE LOCAL LOL
public:
    PreemptLock() = default;
    
    void lock() {
        leanstore_osv_debug::disable_preempt(); 
    }

    void unlock() {
        leanstore_osv_debug::enable_preempt(); 
    }

    bool try_lock() {
        if (leanstore_osv_debug::preemptable()) {
            leanstore_osv_debug::disable_preempt(); 
            return true; 
        }
        return false; 
    }
};