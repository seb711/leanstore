#pragma once
// -------------------------------------------------------------------------------------
#include "BlockedRange.hpp"
#include "MessageHandler.hpp"
#include "Job.hpp"
#include "ThreadBase.hpp"
#include "ThreadingManager.hpp"
#include "leanstore/concurrency-recovery/Worker.hpp"
#include "leanstore/io/IoInterface.hpp"
#include "leanstore/concurrency-recovery/Worker.hpp"
// -------------------------------------------------------------------------------------
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <condition_variable>
#include <mutex>
// -------------------------------------------------------------------------------------
#include <boost/pool/object_pool.hpp>
// -------------------------------------------------------------------------------------
namespace mean
{
// -------------------------------------------------------------------------------------
class OsvJobManager
{

   struct WaitContext {
      std::mutex mtx;
      std::condition_variable cv; 
      std::atomic<bool> ready;
      u64 magic; 

   WaitContext() : ready(false), magic(0) {} 

    // Delete copy constructor & copy assignment
    WaitContext(const WaitContext&) = delete;
    WaitContext& operator=(const WaitContext&) = delete;

    // Allow move semantics if needed
    WaitContext(WaitContext&&) = default;
    WaitContext& operator=(WaitContext&&) = default;   };

   struct meta {
      std::mutex mutex;
      std::condition_variable cv;
      TaskFunction task;
      bool wt_ready = true;
      bool job_set = false;
      bool job_done = false;
   } syncJobMeta;

   LockFreeObjectPool<Job, JOB_QUEUE_SIZE>* pool;
   LockFreeObjectPool<WaitContext, JOB_QUEUE_SIZE>* waiter_pool;

   int total_threads_count;
   int max_exclusive_threads;
   std::atomic<int> running_threads = {0};
   std::atomic<int> exclusiveThreadCounter = {0};
   std::vector<std::unique_ptr<ThreadWithJump>> exclusiveThreadList;
   std::unordered_map<int, std::reference_wrapper<ThreadWithJump>> exclusiveThreadMap;
   static constexpr int MAX_WORKER_THREADS = 2048;
   std::mutex mtx;
public:
   leanstore::cr::Worker* workers[MAX_WORKER_THREADS];   
// -------------------------------------------------------------------------------------
   ~OsvJobManager();
   // -------------------------------------------------------------------------------------
   // env
   // -------------------------------------------------------------------------------------
   void init(int workerThreads, [[maybe_unused]] int exclusvieThreads, IoOptions ioOptions, [[maybe_unused]] int threadAffinityOffset = 0);
   void start(TaskFunction taskFun);
   void shutdown();
   void join();
   void adjustWorkerCount(int workerThreads);
   void registerPageProvider(void* bf_ptr, int partitions_count);
   std::string printCountersHeader();
   std::string printCounters(int te_id);
   // -------------------------------------------------------------------------------------
   // exec
   // -------------------------------------------------------------------------------------
   int execId();
   IoChannel& execIoChannel(); // FIXME: for now we only use one io channel; 
   IoChannel& noExecIoChannel(); // FIXME: for now we only use one io channel; 
   // -------------------------------------------------------------------------------------
   // task
   // -------------------------------------------------------------------------------------
   void registerExclusiveThread(std::string name, int t_i, TaskFunction fun);
   void parallelFor(BlockedRange range, std::function<void(u64, std::atomic<bool>& cancelable)> fun, int tasks, s64 bbgranularity = -1);
   void registerPoller(int to, TaskFunction poller);
   void scheduleTaskSync(TaskFunction fun);
   void yield(TaskState ts);
   void blockingIo(IoRequestType type, char* data, s64 addr, u64 len);
   Task& this_task();
   void sleepAll(float sleep);
   // -------------------------------------------------------------------------------------
   // int getFd();
   // -------------------------------------------------------------------------------------
   int workerCount();
};
// -------------------------------------------------------------------------------------
}  // namespace mean
// -------------------------------------------------------------------------------------
