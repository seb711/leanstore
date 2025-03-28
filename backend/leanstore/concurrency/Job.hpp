#include <atomic>
#include <boost/lockfree/queue.hpp>
#include <functional>
#include <osv/jumpmu.hh>
#include "Time.hpp"
#include <mutex>
#include <condition_variable>
#include "Mean.hpp"

#define JOB_QUEUE_SIZE (256)

namespace mean
{
   template <typename T, size_t Capacity>
   class LockFreeObjectPool
   {
   private:
       std::array<T, Capacity> storage;  // Fixed storage for Jobs
       boost::lockfree::queue<T*, boost::lockfree::fixed_sized<true>, boost::lockfree::capacity<Capacity>> queue;
       std::atomic<u64> size{Capacity};
       std::atomic<bool> waiting{false};
       // for interrupts
       std::mutex interruptmtx;
       std::condition_variable cv;
   
   public:
       LockFreeObjectPool()
       {
           for (auto& job : storage) {
               queue.push(&job);  // Preload queue with Job pointers
           }
       }
   
       void waitUntilFull()
       {
           if (size > 0) return; 
           std::unique_lock<std::mutex> lock(interruptmtx); 
           waiting = true; 
           cv.wait(lock, [this] {return !waiting.load(std::memory_order_release); });
       }
   
       size_t getSize() const
       {
           return size.load();  // Returns the number of available elements
       }
   
       T* acquire()
       {
           T* job = nullptr;
           if (queue.pop(job)) {
               size--;
               return job;
           }
           return nullptr;  // Explicitly return nullptr if queue is empty
       }
   
       void release(T* job)
       {
           if (!job) return;  // Guard against null pointer
           
           // Attempt to push back to queue, check return value
           if (queue.push(job)) {
               size++;  // Increment size only if push is successful
               
               // Check and notify if waiting
               if (size > 32 && waiting.load(std::memory_order_acquire)) {
                   std::unique_lock<std::mutex> lock(interruptmtx);
                   waiting = false;
                   cv.notify_all();
               }
           }
       }
   };
   
template <typename T>
class function_ref;

template <typename Ret, typename... Args>
class function_ref<Ret(Args...)> {
    void* obj = nullptr;
    Ret (*callback)(void*, Args...) = nullptr;

public:
    // Constructor accepting callable objects
    template <typename F>
    function_ref(F&& f) noexcept
        : obj(reinterpret_cast<void*>(std::addressof(f))),
            callback([](void* obj, Args... args) -> Ret {
                return (*reinterpret_cast<F*>(obj))(std::forward<Args>(args)...);
            }) {}

    // Callable operator
    Ret operator()(Args... args) const {
        return callback(obj, std::forward<Args>(args)...);
    }
};

class Job; 

using JobFunction =  std::function<void(u64, std::atomic<bool>&)>;  // std::add_pointer_t<void()>;
using CallbackFunction =  std::function<void(Job*)>;  // std::add_pointer_t<void()>;

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
   JobFunction* fun = nullptr;
   JobArguments args;

  public:
   Job() : args() {};
   // -------------------------------------------------------------------------------------
};
};  // namespace mean