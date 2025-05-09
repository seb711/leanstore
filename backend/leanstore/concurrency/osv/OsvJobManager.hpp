#pragma once
// -------------------------------------------------------------------------------------
#include "leanstore/concurrency/MessageHandler.hpp"
#include "leanstore/concurrency/ThreadBase.hpp"
#include "leanstore/concurrency/ThreadingManager.hpp"
#include "leanstore/concurrency-recovery/Worker.hpp"
#include "leanstore/io/IoInterface.hpp"
#include "leanstore/concurrency-recovery/Worker.hpp"
#include "leanstore/storage/buffer-manager/BufferManager.hpp"
#include "BlockingIoContext.hpp"
#include "OsvJob.hpp"
// -------------------------------------------------------------------------------------
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <condition_variable>
#include <mutex>
// -------------------------------------------------------------------------------------
#include "leanstore/concurrency/osv/background/OsvBackgroundThreadBase.hpp"
#include "leanstore/concurrency/osv/LockfreeObjectPool.hpp"
// -------------------------------------------------------------------------------------
namespace mean
{
// -------------------------------------------------------------------------------------
class OsvJobManager
{
   static const int MAX_REQUESTS = 2048;
   LockfreeObjectPool<Job, JOB_QUEUE_SIZE>* pool;
   LockfreeObjectPool<BlockingIoContext, MAX_REQUESTS>* waiter_pool;

   leanstore::storage::BufferManager* buffer_manager; 
   std::vector<std::unique_ptr<OsvBackgroundThreadBase>> backgroundThreads;

public:
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
   // -------------------------------------------------------------------------------------
   // THE IDEA WITH THE IO CHANNELS WOULD BE THAT THE IO CHANNELS
   // GET PERCPU OBJECTS AND WE HAVE ONE IO CHANNEL PER CPU
   // MAYBE WE ALSO CAN THEN INSERT THE IO ON THE CPU THAT WE ARE 
   // PUSHING THE TASK TO 
   // 
   // FOR NOW WE JUST GO WITH ONE IO CHANNEL BECAUSE THIS IS ENOUGH
   // -------------------------------------------------------------------------------------
   IoChannel& execIoChannel(); // FIXME: for now we only use one io channel; 
   // -------------------------------------------------------------------------------------
   // -------------------------------------------------------------------------------------
   // task
   // -------------------------------------------------------------------------------------
   void registerExclusiveThread(std::string name, int t_i, TaskFunction fun);
   void parallelFor(BlockedRange range, std::function<void(u64, std::atomic<bool>& cancelable)> fun, int tasks, s64 bbgranularity = -1);
   void scheduleTaskSync(TaskFunction fun);
   void yield(TaskState ts);
   void blockingIo(IoRequestType type, char* data, s64 addr, u64 len);
   Task& this_task();
   void sleepAll(float sleep);
   static void registerSyncPageProvider(); 
   // -------------------------------------------------------------------------------------
   // int getFd();
   // -------------------------------------------------------------------------------------
   int workerCount();
};
// -------------------------------------------------------------------------------------
}  // namespace mean
// -------------------------------------------------------------------------------------
