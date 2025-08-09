#pragma once
#include <stdexcept>
#include <vector>
#include <atomic>
#include <array>
#include "Units.hpp"

#if false //NDEBUG
#define RB_DEBUG_COUNTER(x)
#else
#define RB_DEBUG_COUNTER(x) x
#endif

namespace leanstore {
namespace utils {

template<typename TValue>
class RingBuffer {
    std::vector<TValue> values;
    TValue* const vec_first;
    TValue* const vec_last;
    
    // Use atomic pointers for thread-safe access
    std::atomic<TValue*> read_ptr;
    std::atomic<TValue*> write_ptr;
    
public:
    RB_DEBUG_COUNTER(
        std::atomic<u64> inserted{0};
        std::atomic<u64> erased{0};
    )
    
    static constexpr int POP_MAX = 32;
    std::atomic<u64> contains{0};
    const u64 max_size;
    
    RingBuffer(u64 max_size) : values(max_size + 1), // +1 as one always stays empty
                               vec_first(&values[0]), 
                               vec_last(&values[max_size]),
                               read_ptr(&values[0]), 
                               write_ptr(&values[0]), 
                               max_size(max_size) {
    }
    
    RingBuffer(RingBuffer const&) = delete;
    
    TValue& push_back(const TValue& value) {
        TValue* current_write = write_ptr.load(std::memory_order_relaxed);
        TValue* next_write = current_write + 1;
        
        if (next_write > vec_last) { // overflow
            next_write = vec_first;
        }
        
        // Check if buffer is full
        TValue* current_read = read_ptr.load(std::memory_order_acquire);
        if (next_write == current_read) { // full
            throw std::logic_error("full");
        }
        
        // Write the value
        *current_write = value;
        
        // Update write pointer with release semantics to ensure the write is visible
        // before the pointer update
        write_ptr.store(next_write, std::memory_order_release);
        
        contains.fetch_add(1, std::memory_order_relaxed);
        RB_DEBUG_COUNTER(inserted.fetch_add(1, std::memory_order_relaxed););
        
        return *current_write;
    }
    
    TValue& front() {
        TValue* current_read = read_ptr.load(std::memory_order_relaxed);
        TValue* current_write = write_ptr.load(std::memory_order_acquire);
        
        if (current_read == current_write) {
            throw std::logic_error("empty");
        }
        return *current_read;
    }
    
    bool try_pop(TValue& ret) {
        TValue* current_read = read_ptr.load(std::memory_order_relaxed);
        TValue* current_write = write_ptr.load(std::memory_order_acquire);
        
        if (current_read == current_write) {
            return false;
        }
        
        // Read the value
        ret = *current_read;
        
        TValue* next_read = current_read + 1;
        if (next_read > vec_last) { // overflow
            next_read = vec_first;
        }
        
        RB_DEBUG_COUNTER(*current_read = TValue{};) // Clear for debugging
        
        // Update read pointer with release semantics
        read_ptr.store(next_read, std::memory_order_release);
        
        contains.fetch_sub(1, std::memory_order_relaxed);
        RB_DEBUG_COUNTER(erased.fetch_add(1, std::memory_order_relaxed););
        
        return true;
    }
    
    int pop_multiple(std::array<TValue, POP_MAX>& pop_into, int pop_max) {
        TValue* current_read = read_ptr.load(std::memory_order_relaxed);
        TValue* current_write = write_ptr.load(std::memory_order_acquire);
        
        int popped = 0;
        TValue* next_read = current_read;
        
        // Calculate how many items we can pop
        while (next_read != current_write && popped < POP_MAX && popped < pop_max) {
            pop_into[popped] = *next_read;
            RB_DEBUG_COUNTER(*next_read = TValue{};) // Clear for debugging
            popped++;
            
            next_read++;
            if (next_read > vec_last) { // overflow
                next_read = vec_first;
            }
        }
        
        if (popped > 0) {
            // Update read pointer atomically
            read_ptr.store(next_read, std::memory_order_release);
            contains.fetch_sub(popped, std::memory_order_relaxed);
            RB_DEBUG_COUNTER(erased.fetch_add(popped, std::memory_order_relaxed););
        }
        
        return popped;
    }
    
    u64 size() {
        return contains.load(std::memory_order_relaxed);
    }
    
    bool full() {
        TValue* current_write = write_ptr.load(std::memory_order_relaxed);
        TValue* next_write = current_write + 1;
        
        if (next_write > vec_last) { // overflow
            next_write = vec_first;
        }
        
        TValue* current_read = read_ptr.load(std::memory_order_acquire);
        return next_write == current_read;
    }
    
    bool empty() {
        TValue* current_read = read_ptr.load(std::memory_order_relaxed);
        TValue* current_write = write_ptr.load(std::memory_order_acquire);
        return current_read == current_write;
    }
};

} // namespace utils
} // namespace leanstore