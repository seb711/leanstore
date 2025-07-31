#pragma once
// -------------------------------------------------------------------------------------
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <boost/lockfree/stack.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/atomic.hpp>
#include "Exceptions.hpp"
#include "leanstore/io/IoRequest.hpp"
// -------------------------------------------------------------------------------------
namespace mean
{
// -------------------------------------------------------------------------------------
// NOTE: CURRENTLY THE REQUESTSTACK IS USED AS A THREAD-SAFE DATASTRUCTURE
//       THEREFORE I ADDED A MUTEX -> THIS SHOULD BE CHANGED IN THE FUTURE
template <typename R>
class RequestStackLockfree
{
public:
    std::unique_ptr<R[]> requests;
    // boost::lockfree::stack<R*> free_stack;
    std::atomic<R*> head = {}; 
    
    // Note: Boost doesn't have a lockfree set, but we can use a concurrent_set from TBB
    // or implement our own atomic-based tracking for debug purposes
    std::atomic<int> outstanding_count{0};
    
    const int max_entries;
    std::atomic<int> free;
    std::atomic<int> pushed{0};
    
    RequestStackLockfree(int max_entries) : 
        max_entries(max_entries),
        free(max_entries)
    {
        requests = std::make_unique<R[]>(max_entries);
        for (int i = 0; i < max_entries; i++) {
            requests[i].impl.next = head.load(); 
            head.store(&requests[i]); 
        }
    }
    
    ~RequestStackLockfree() {}
    
    int outstanding()
    {
        // ensure(max_entries - free.load() - pushed.load() == outstanding_count.load());
        return outstanding_count.load();
    }
    
    bool full() {
        return free.load() == 0;
    }
    
    /* free -> to user (untracked) */
    bool popFromFreeStack(R*& out)
    {
        // ensure(free.load() >= 0);
        if (free.load() == 0) {
            return false;
        }
        
        R* old_top = head.load();
        
        while (old_top && !head.compare_exchange_weak(old_top, old_top->impl.next)) {
            // CAS failed, retry with updated old_top
            // compare_exchange_weak updates old_top on failure
        }
        
        if (!old_top) {
            return false; // Stack was empty
        }
        
        free.fetch_sub(1);
        outstanding_count.fetch_add(1);
        out = old_top;
        return true;
    }
    
    /* outstanding -> free */
    void returnToFreeList(R* ptr)
    {
        outstanding_count.fetch_sub(1);

        R* old_top; 
        do {
            old_top = head.load(); 
            ptr->impl.next = old_top;
        } while (!head.compare_exchange_weak(old_top, ptr)); 

        free.fetch_add(1);
    }
};
// -------------------------------------------------------------------------------------
}  // namespace mean
// -------------------------------------------------------------------------------------
