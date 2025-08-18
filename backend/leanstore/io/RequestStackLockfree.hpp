#pragma once
// -------------------------------------------------------------------------------------
#include <chrono>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <boost/lockfree/queue.hpp>
#include <boost/atomic.hpp>
#include "Exceptions.hpp"
#include "leanstore/io/IoRequest.hpp"
// -------------------------------------------------------------------------------------
namespace mean
{
// -------------------------------------------------------------------------------------
template <typename R>
class RequestStackLockfree
{
public:
    std::unique_ptr<R[]> requests;
    
    // Using boost lockfree queue instead of manual atomic linked list
    // Fixed capacity queue for better performance
    boost::lockfree::queue<R*, boost::lockfree::fixed_sized<true>> free_queue;
    
    // Track outstanding requests for debugging
    std::atomic<int> outstanding_count{0};
    
    const int max_entries;
    std::atomic<int> free;
    std::atomic<int> pushed{0};
    
    RequestStackLockfree(int max_entries) : 
        free_queue(max_entries),  // Initialize with fixed capacity
                max_entries(max_entries),
                        free(max_entries)

    {
        requests = std::make_unique<R[]>(max_entries);
        
        // Push all requests to the free queue initially
        for (int i = 0; i < max_entries; i++) {
            bool success = free_queue.push(&requests[i]);
            // In constructor, this should always succeed since queue is empty
            assert(success && "Failed to initialize free queue");
        }
    }
    
    ~RequestStackLockfree() {
        // Queue will be destroyed automatically
        // Requests array will be destroyed by unique_ptr
    }
    
    int outstanding()
    {
        return outstanding_count.load();
    }
    
    bool full() {
        return free.load() == 0;
    }
    
    /* free -> to user (untracked) */
    bool popFromFreeStack(R*& out)
    {
        if (free.load() == 0) {
            return false;
        }
        
        R* ptr = nullptr;
        if (free_queue.pop(ptr)) {
            free.fetch_sub(1);
            outstanding_count.fetch_add(1);
            out = ptr;
            return true;
        }
        
        return false;
    }
    
    /* outstanding -> free */
    void returnToFreeList(R* ptr)
    {
        outstanding_count.fetch_sub(1);
        
        bool success = free_queue.push(ptr);
        if (!success) {
            // This should not happen if the logic is correct
            // (we can't return more than max_entries)
            throw std::runtime_error("Failed to return request to free queue - queue full");
        }
        
        free.fetch_add(1);
    }
    
    // Alternative: Get approximate size of free queue (may be useful for debugging)
    // Note: This is not exact in concurrent scenarios
    size_t approximateFreeSize() const {
        // boost::lockfree::queue doesn't provide a size() method that's lock-free
        // We're using the atomic 'free' counter instead
        return free.load();
    }
};
}