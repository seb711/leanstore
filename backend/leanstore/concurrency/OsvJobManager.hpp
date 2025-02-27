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
   boost::object_pool<Job> pool;
   int total_threads_count;
   int max_exclusive_threads;
   std::atomic<int> running_threads = {0};
   std::atomic<int> exclusiveThreadCounter = {0};
   std::vector<std::unique_ptr<ThreadWithJump>> exclusiveThreadList;
   std::unordered_map<int, std::reference_wrapper<ThreadWithJump>> exclusiveThreadMap;
   static constexpr int MAX_WORKER_THREADS = 2048;
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
   // -------------------------------------------------------------------------------------
   // exec
   // -------------------------------------------------------------------------------------
   int execId();
   IoChannel& execIoChannel(); // FIXME: for now we only use one io channel; 
   // -------------------------------------------------------------------------------------
   // task
   // -------------------------------------------------------------------------------------
   void registerExclusiveThread(std::string name, int t_i, TaskFunction fun);
   void parallelFor(BlockedRange range, std::function<void(u64, std::atomic<bool>& cancelable)> fun, int tasks, s64 bbgranularity = -1);
   void scheduleTaskSync(TaskFunction fun);
   void yield(TaskState ts);
   void blockingIo(IoRequestType type, char* data, s64 addr, u64 len);
   Task& this_task();
   // -------------------------------------------------------------------------------------
   // int getFd();
   // -------------------------------------------------------------------------------------
   int workerCount();
};
// -------------------------------------------------------------------------------------
}  // namespace mean
// -------------------------------------------------------------------------------------
