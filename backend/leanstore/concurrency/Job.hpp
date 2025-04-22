#include <atomic>
#include <boost/lockfree/queue.hpp>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <osv/jumpmu.hh>
#include "Mean.hpp"
#include "Time.hpp"

#define JOB_QUEUE_SIZE (2048)

constexpr size_t wait_for_count = JOB_QUEUE_SIZE / 64; 

namespace mean
{
    template <typename T, size_t Capacity>
    class LockFreeObjectPool
    {
    private:
        std::array<T, Capacity> storage;  // Fixed storage for objects
        alignas(64) boost::lockfree::queue<T*, boost::lockfree::fixed_sized<true>, boost::lockfree::capacity<Capacity>> queue;
        std::atomic<size_t> available{Capacity};  // Count of available objects
        std::atomic<bool> waiting{false};  // Count of available objects

        // Synchronization for waiters
        std::mutex wait_mutex;
        std::condition_variable wait_cv;
    
    public:
        LockFreeObjectPool()
        {
            for (auto& item : storage) {
                queue.push(&item);  // Preload queue with object pointers
            }
        }
        
        // Wait until at least one object is available
        void waitUntilAvailable()
        {
            // Fast path - avoid mutex if objects are already available
            if (available.load(std::memory_order_acquire) > 0) {
                return;
            }
            
            // Slow path - wait for an object to become available
            std::unique_lock<std::mutex> lock(wait_mutex);
            waiting = true; 
            wait_cv.wait(lock, [this] { 
                return available.load(std::memory_order_acquire) > (wait_for_count); 
            });
            waiting = false; 
        }
        
        // Wait until the pool is completely full (all objects returned)
        void waitUntilFull()
        {
            // Fast path - avoid mutex if pool is already full
            if (available.load(std::memory_order_acquire) == Capacity) {
                return;
            }
            
            // Slow path - wait for pool to become full
            std::unique_lock<std::mutex> lock(wait_mutex);
            wait_cv.wait(lock, [this] { 
                return available.load(std::memory_order_acquire) == Capacity; 
            });
        }
        
        // Get number of available objects
        size_t getAvailable() const
        {
            return available.load(std::memory_order_acquire);
        }
        
        // Acquire an object from the pool
        T* acquire()
        {
            T* object = nullptr;
            if (queue.pop(object)) {
                available.fetch_sub(1, std::memory_order_release);
                return object;
            }
            return nullptr;  // Return nullptr if queue is empty
        }
        
        // Release an object back to the pool
        void release(T* object)
        {
            if (!object) {
                return;  // Guard against null pointer
            }
            
            if (queue.push(object)) {
                size_t prev_count = available.fetch_add(1, std::memory_order_release);
                
                // Notify waiters if this might satisfy a waiting condition
                // Only lock and notify if there's likely to be a waiter
                if (waiting.load() && prev_count >= wait_for_count) {
                    std::lock_guard<std::mutex> lock(wait_mutex);
                    wait_cv.notify_one();  // Wake all waiters
                }
            }
        }
    };

template <typename T>
class function_ref;

template <typename Ret, typename... Args>
class function_ref<Ret(Args...)>
{
   void* obj = nullptr;
   Ret (*callback)(void*, Args...) = nullptr;

  public:
   // Constructor accepting callable objects
   template <typename F>
   function_ref(F&& f) noexcept
       : obj(reinterpret_cast<void*>(std::addressof(f))),
         callback([](void* obj, Args... args) -> Ret { return (*reinterpret_cast<F*>(obj))(std::forward<Args>(args)...); })
   {
   }

   // Callable operator
   Ret operator()(Args... args) const { return callback(obj, std::forward<Args>(args)...); }
};

class Job;

using JobFunction = std::function<void(u64, std::atomic<bool>&)>;  // std::add_pointer_t<void()>;
using CallbackFunction = std::function<void(Job*)>;                // std::add_pointer_t<void()>;

struct JobArguments {
   LockFreeObjectPool<Job, JOB_QUEUE_SIZE>* pool;
   uint64_t key;
   std::atomic<bool> cancelable;
   std::atomic<u64>* done;
   std::atomic<u64>* started;

   JobArguments() : pool(nullptr), key(0), cancelable({false}) {};
};

// -------------------------------------------------------------------------------------
class Job
{
  public:
   jumpmu::JumpMUContext jumpctx;
   alignas(64) JobFunction* fun = nullptr;
   JobArguments args;

  public:
   Job() : args() {};
   // -------------------------------------------------------------------------------------
};
};  // namespace mean