#include <atomic>
#include <boost/lockfree/queue.hpp>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <osv/jumpmu.hh>
#include "leanstore/concurrency/Mean.hpp"
#include "Time.hpp"
#include "LockfreeObjectPool.hpp"

#define JOB_QUEUE_SIZE (512)

constexpr size_t wait_for_count = JOB_QUEUE_SIZE / 4; 

namespace mean
{
class Job;

using JobFunction = std::function<void(u64)>;  // std::add_pointer_t<void()>;
using CallbackFunction = std::function<void(Job*)>;                // std::add_pointer_t<void()>;

struct JobArguments {
   LockfreeObjectPool<Job, JOB_QUEUE_SIZE>* pool;
   uint64_t key;
   std::atomic<u64>* done;
   std::atomic<u64>* started;

   JobArguments() : pool(nullptr), key(0) {};
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