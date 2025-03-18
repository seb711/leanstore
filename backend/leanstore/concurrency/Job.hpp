#include <functional>
#include <osv/jumpmu.hh>
#include <atomic>
#include "Time.hpp"
#include <boost/lockfree/queue.hpp>

#define JOB_QUEUE_SIZE (128)

namespace mean
{
template <typename T, size_t Capacity>
class LockFreeObjectPool {
private:
    std::array<T, Capacity> storage; // Fixed storage for Jobs
    boost::lockfree::queue<T*,  boost::lockfree::fixed_sized<true>,
    boost::lockfree::capacity<Capacity>> queue;

public:
    LockFreeObjectPool() {
        for (auto& job : storage) {
            queue.push(&job); // Preload queue with Job pointers
        }
    }

    size_t getSize() const {
        return Capacity; // Returns the number of available elements
    }

    T* acquire() {
        T* job = nullptr;
        queue.pop(job);
        return job; // nullptr if queue is empty
    }

    void release(T* job) {
        queue.push(job);
    }
};

class Job; 

using JobFunction =  std::function<void(u64, std::atomic<bool>&)>;  // std::add_pointer_t<void()>;
using CallbackFunction =  std::function<void(Job*)>;  // std::add_pointer_t<void()>;

struct JobArguments {
   LockFreeObjectPool<Job, JOB_QUEUE_SIZE>* pool; 
   uint64_t key; 
   std::atomic<bool> cancelable; 

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