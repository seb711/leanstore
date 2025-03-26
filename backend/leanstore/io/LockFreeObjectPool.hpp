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
    template <typename T, size_t Capacity = 128>
    class LockFreeObjectPool {
    private:
        std::array<T, Capacity> storage; // Fixed storage for Jobs
        boost::lockfree::queue<T*,  boost::lockfree::fixed_sized<true>,
        boost::lockfree::capacity<Capacity>> queue;
    
    public:
        const u64 max_entries = Capacity; 

        T& getIndex(u64 i) {
            assert(i < Capacity); 
            return storage[i]; 
        }

        LockFreeObjectPool() {
            for (size_t t = 0; t < Capacity; t++) {
                auto* pos = new (&storage[t]) T(); 
                queue.push(pos); 
            }
        }
    
        size_t getSize() const {
            return Capacity - queue.read_available(); // Returns the number of available elements
        }
    
        T* acquire() {
            T* job = nullptr;
            return queue.pop(job) ? job : nullptr; 
        }
    
        void release(T* job) {
            assert(job != nullptr); 
            queue.push(job);
        }
    };

    template <typename T, size_t Capacity = 128>
    class LockFreeRequestPool {
    private:
        std::array<T, Capacity> storage; // Fixed storage for Jobs
        boost::lockfree::queue<T*,  boost::lockfree::fixed_sized<true>,
        boost::lockfree::capacity<Capacity>> free_list;
        boost::lockfree::queue<T*,  boost::lockfree::fixed_sized<true>,
        boost::lockfree::capacity<Capacity>> submit_list;
    
    public:
        const u64 max_entries = Capacity; 

        T& getIndex(u64 i) {
            assert(i < Capacity); 
            return storage[i]; 
        }

        LockFreeObjectPool() {
            for (size_t t = 0; t < Capacity; t++) {
                auto* pos = new (&storage[t]) T(); 
                queue.push(pos); 
            }
        }
    
        size_t getSize() const {
            return Capacity - queue.read_available(); // Returns the number of available elements
        }
    
        T* acquire() {
            T* job = nullptr;
            return queue.pop(job) ? job : nullptr; 
        }
    
        void release(T* job) {
            assert(job != nullptr); 
            queue.push(job);
        }
    };
}  // namespace mean
// -------------------------------------------------------------------------------------
