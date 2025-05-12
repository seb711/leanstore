#pragma once
// -------------------------------------------------------------------------------------
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include "Exceptions.hpp"
#include "leanstore/io/IoRequest.hpp"
// -------------------------------------------------------------------------------------
namespace mean
{
// -------------------------------------------------------------------------------------
/**
 * Thread-safe implementation of RequestStack
 */
template <typename R>
class RequestStackLock
{
public:
    std::unique_ptr<R[]> requests;
    std::unique_ptr<R*[]> free_stack;
    std::unique_ptr<R*[]> submit_stack;
    
#if false
    std::unordered_set<R*> outstanding_set;
#endif
    const int max_entries;
    std::atomic<int> free;
    std::atomic<int> pushed = {0};
    
    // Mutexes for thread-safety
    std::mutex free_stack_mutex;
    std::mutex submit_stack_mutex;
    std::mutex outstanding_mutex;
    
    RequestStackLock(int max_entries) : max_entries(max_entries), free(max_entries)
    {
        requests = std::make_unique<R[]>(max_entries);
        free_stack = std::make_unique<R*[]>(max_entries);
        submit_stack = std::make_unique<R*[]>(max_entries);
        for (int i = 0; i < max_entries; i++) {
            free_stack[i] = &requests[i];
        }
    };
    
    ~RequestStackLock() {}
    
    int outstanding()
    {
        assert(max_entries - free - pushed == outstanding_set.size());
        return max_entries - free.load() - pushed.load();
    }
    
    int submitStackSize() {
        return pushed.load();
    }
    
    bool full() {
        return free.load() == 0;
    }
    
    /* free -> to user (untracked)*/
    bool popFromFreeStack(R*& out)
    {
        std::lock_guard<std::mutex> lock(free_stack_mutex);
        
        assert(free >= 0);
        if (free == 0) {
            return false;
        }
        free--;
        out = free_stack[free];
        return true;
    }
    
    /* user -> to submit */
    void pushToSubmitStack(R* req)
    {
        std::lock_guard<std::mutex> lock(submit_stack_mutex);
        
        submit_stack[pushed] = req;
        pushed++;
    }
    
    /* free -> submit / direct path (not like popFromFree and pushToSubmit ) */
    bool moveFreeToSubmitStack(R*& out)
    {
        // Need to lock both stacks to ensure atomic operation
        std::lock(free_stack_mutex, submit_stack_mutex);
        std::lock_guard<std::mutex> free_lock(free_stack_mutex, std::adopt_lock);
        std::lock_guard<std::mutex> submit_lock(submit_stack_mutex, std::adopt_lock);
        
        assert(free >= 0);
        if (free == 0) {
            return false;
        }
        free--;
        assert(free < max_entries);
        out = free_stack[free];
        assert(pushed < max_entries);
        submit_stack[pushed] = out;
        pushed++;
        return true;
    }
    
    /* submit -> outstanding */
    void emptySubmitStack()
    {
#if false
        // Need to lock both submit stack and the outstanding set
        std::lock(submit_stack_mutex, outstanding_mutex);
        std::lock_guard<std::mutex> submit_lock(submit_stack_mutex, std::adopt_lock);
        std::lock_guard<std::mutex> outstanding_lock(outstanding_mutex, std::adopt_lock);
        
        for (int i = 0; i < pushed; i++) {
            auto found = outstanding_set.find(submit_stack[i]);
            if (found == outstanding_set.end()) {
                outstanding_set.insert(submit_stack[i]);
            }
            submit_stack[i] = nullptr;
        }
#else
        std::lock_guard<std::mutex> submit_lock(submit_stack_mutex);
        for (int i = 0; i < pushed; i++) {
            submit_stack[i] = nullptr;
        }
#endif
        pushed = 0;
    }
    
    /* submit -> outstanding */
    bool popFromSubmitStack(R*& out)
    {
#if false
        std::lock(submit_stack_mutex, outstanding_mutex);
        std::lock_guard<std::mutex> submit_lock(submit_stack_mutex, std::adopt_lock);
        std::lock_guard<std::mutex> outstanding_lock(outstanding_mutex, std::adopt_lock);
#else
        std::lock_guard<std::mutex> submit_lock(submit_stack_mutex);
#endif
        
        if (pushed <= 0) {
            return false;
        }
        pushed--;
        out = submit_stack[pushed];
        
#if false
        auto found = outstanding_set.find(out);
        // ensure(found == outstanding_set.end());
        if (found == outstanding_set.end()) {
            outstanding_set.insert(submit_stack[pushed]);
        }
#endif
        return true;
    }
    
    /* outstanding -> free */
    void returnToFreeList(R* ptr)
    {
#if false
        // Need to lock both free stack and the outstanding set
        std::lock(free_stack_mutex, outstanding_mutex);
        std::lock_guard<std::mutex> free_lock(free_stack_mutex, std::adopt_lock);
        std::lock_guard<std::mutex> outstanding_lock(outstanding_mutex, std::adopt_lock);
        
        auto found = outstanding_set.find(ptr);
        ensure(found != outstanding_set.end());
        outstanding_set.erase(found);
#else
        std::lock_guard<std::mutex> free_lock(free_stack_mutex);
#endif
        
        free_stack[free] = ptr;
        free++;
    }
};
// -------------------------------------------------------------------------------------
} // namespace mean
// -------------------------------------------------------------------------------------