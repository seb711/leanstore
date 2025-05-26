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
    boost::lockfree::stack<R*> free_stack;
    boost::lockfree::queue<R*> submit_stack;
    
    // Note: Boost doesn't have a lockfree set, but we can use a concurrent_set from TBB
    // or implement our own atomic-based tracking for debug purposes
    std::atomic<int> outstanding_count{0};
    
    const int max_entries;
    std::atomic<int> free;
    std::atomic<int> pushed{0};
    
    RequestStackLockfree(int max_entries) : 
        free_stack(max_entries),
        submit_stack(max_entries),
        max_entries(max_entries), 
        free(max_entries)
    {
        requests = std::make_unique<R[]>(max_entries);
        for (int i = 0; i < max_entries; i++) {
            free_stack.push(&requests[i]);
        }
    }
    
    ~RequestStackLockfree() {}
    
    int outstanding()
    {
        // ensure(max_entries - free.load() - pushed.load() == outstanding_count.load());
        return outstanding_count.load();
    }
    
    int submitStackSize() {
        return pushed.load();
    }
    
    bool full() {
        return free.load() == 0;
    }
    
    /* free -> to user (untracked) */
    bool popFromFreeStack(R*& out)
    {
        ensure(free.load() >= 0);
        if (free.load() == 0) {
            return false;
        }
        
        if (free_stack.pop(out)) {
            free.fetch_sub(1);
            return true;
        }
        return false;
    }
    
    /* user -> to submit */
    void pushToSubmitStack(R* req)
    {
        ensure(submit_stack.push(req));
        pushed.fetch_add(1);
    }
    
    /* free -> submit / direct path (not like popFromFree and pushToSubmit) */
    bool moveFreeToSubmitStack(R*& out)
    {
        ensure(free.load() >= 0);
        if (free.load() == 0) {
            return false;
        }
        
        if (free_stack.pop(out)) {
            free.fetch_sub(1);
            ensure(submit_stack.push(out));
            pushed.fetch_add(1);
            return true;
        }
        return false;
    }
    
    /* submit -> outstanding */
    bool popFromSubmitStack(R*& out)
    {
        if (pushed.load() <= 0) {
            return false;
        }
        
        if (submit_stack.pop(out)) {
            pushed.fetch_sub(1);
            outstanding_count.fetch_add(1);
            return true;
        }
        return false;
    }
    
    /* outstanding -> free */
    void returnToFreeList(R* ptr)
    {
        outstanding_count.fetch_sub(1);
        ensure(free_stack.push(ptr));
        free.fetch_add(1);
    }
};
// -------------------------------------------------------------------------------------
}  // namespace mean
// -------------------------------------------------------------------------------------
