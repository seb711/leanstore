// -------------------------------------------------------------------------------------
#include "OsvJobManager.hpp"
#include "leanstore/concurrency/ConnectedIoChannel.hpp"
#include "leanstore/concurrency/Task.hpp"
#include "leanstore/io/IoInterface.hpp"
#include "leanstore/io/impl/LibaioImpl.hpp"
#include "leanstore/profiling/counters/CPUCounters.hpp"
#include "leanstore/profiling/counters/WorkerCounters.hpp"
#include "leanstore/storage/buffer-manager/BufferManager.hpp"
// -------------------------------------------------------------------------------------
#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
// -------------------------------------------------------------------------------------
#include <osv/task.h>
#include <osv/leanstore_debug.hh>
// -------------------------------------------------------------------------------------
#include "leanstore/concurrency/osv/background/OsvPageProvider.hpp"

#define USE_JOBS
#define USE_TIME_MEASURE

namespace mean
{
// -------------------------------------------------------------------------------------
OsvJobManager::~OsvJobManager()
{
   if (pool) delete pool; 
   if (waiter_pool) delete waiter_pool;
   shutdown();
}
// -------------------------------------------------------------------------------------
static std::mutex cb_mtx; 
// -------------------------------------------------------------------------------------
// env
// -------------------------------------------------------------------------------------
void OsvJobManager::init(int workers_count, int exclusiveThreads, IoOptions ioOptions, [[maybe_unused]] int threadAffinityOffset)
{
   
   pool = new LockfreeObjectPool<Job, JOB_QUEUE_SIZE>{};
   waiter_pool = new LockfreeObjectPool<BlockingIoContext, MAX_REQUESTS>{};

   // init the pools that we currently need
   std::cout << "INIT OSV JOBBING MANAGER" << std::endl;
   ensure(ioOptions.engine == "osv", "ioOptions.engine == osv");
   IoInterface::initInstance(ioOptions);
}
// -------------------------------------------------------------------------------------
void OsvJobManager::start(TaskFunction taskFun)
{
   // all_threads[max_exclusive_threads]->sendTask(taskFun);
   taskFun();
}
// -------------------------------------------------------------------------------------
void OsvJobManager::shutdown()
{
   std::cout << "Shutdown; but nothing is done here in OSv" << std::endl;
}
// -------------------------------------------------------------------------------------
void OsvJobManager::join()
{
   std::cout << "Join; but nothing is done here in OSv" << std::endl;
}
// -------------------------------------------------------------------------------------
// exec
// -------------------------------------------------------------------------------------
int OsvJobManager::execId()
{
   return -2;
}
// -------------------------------------------------------------------------------------
IoChannel& OsvJobManager::execIoChannel()
{
   // int this_id = execId();
   // TODO check if in exclusive thread
   auto& ioChannel = IoInterface::instance().getIoChannel(0);
   return ioChannel; 
}
// -------------------------------------------------------------------------------------
// task
// -------------------------------------------------------------------------------------
void OsvJobManager::registerPageProvider(void* bf_ptr, int partitions_count)
{
   buffer_manager = static_cast<leanstore::storage::BufferManager*>(bf_ptr);

   for (u32 partition_id = 0; partition_id < partitions_count; partition_id++) {
      std::cout << "INIT PAGE PROVIDER " << partition_id << std::endl;
      backgroundThreads.push_back(std::make_unique<OsvPageProvider>(buffer_manager, partition_id));
   }
}
// OsvJobManager

static void job_fn(void* args)
{
   auto* job = (Job*)args;

   jumpmu::thread_local_jumpmu_ctx = &(job->jumpctx);

   (*(job->fun))(job->args.key, job->args.cancelable);

   job->args.pool->release(job);
};

void OsvJobManager::parallelFor(BlockedRange bb, std::function<void(u64, std::atomic<bool>& cancelable)> fun, const int tasks, s64 bbgranularity)
{
   leanstore_osv_debug::set_priority(0.5); 

   ensure(tasks > 0, "tasks > 0");
   int startedJobs = 0;
   std::mutex allDoneMutex;
   std::condition_variable allDone;
   std::atomic<int> threadsDone = {0};
   const int threads = workerCount();
   std::atomic<bool> cancelable = {false};
   u64 range = (bb.end - bb.begin) / threads;
   u64 remaining = (bb.end - bb.begin) % threads;
   if (range == 0) {
      range = 1;
      remaining = 0;
   }

   std::atomic<int> used = {0};
   std::atomic<u64> finished = {0};

   u64 start = bb.begin;

   for (u64 id = bb.begin; id < bb.end; id++) {
      auto start = mean::readTSC();

      size_t it = 0; 
      auto* job = pool->acquire();
      while (job == nullptr) {
         leanstore_osv_debug::rcu_flush();
         leanstore_osv_debug::wait_until_zombies_reaped();
         pool->waitUntilAvailable();
         job = pool->acquire();
      } 

      job->fun = &fun;
      job->args.pool = pool;
      job->args.key = id;

      assert(leanstore_osv_debug::task_stack.size() < 2048);
      leanstore_osv_debug::task_stack.push({job_fn, job});

      if (leanstore_osv_debug::task_stack.size() > 96) { // FIXME: this is currently a constant 
         leanstore_osv_debug::flush_to_runqueue();
      }
   }

}
std::string OsvJobManager::printCountersHeader()
{
   throw leanstore::ex::GenericException("not implemented");
}
std::string OsvJobManager::printCounters(int te_id)
{
   throw leanstore::ex::GenericException("not implemented");
}
// -------------------------------------------------------------------------------------
void OsvJobManager::scheduleTaskSync(TaskFunction fun)
{
   jumpmu::thread_local_jumpmu_ctx = new jumpmu::JumpMUContext{};
   fun();
   delete jumpmu::thread_local_jumpmu_ctx;
}
// -------------------------------------------------------------------------------------
void OsvJobManager::yield([[maybe_unused]] TaskState ts)
{
   // do nothing?
}
void OsvJobManager::sleepAll(float sleep)
{
   // do nothing?
}
void OsvJobManager::adjustWorkerCount(int workerThreads) {}
// -------------------------------------------------------------------------------------
void OsvJobManager::blockingIo(IoRequestType type, char* data, s64 addr, u64 len)
{
   auto* waitargs = waiter_pool->acquire();

   waitargs->ready.store(false);
   waitargs->magic = mean::readTSC();

   assert(!waitargs->ready);

   UserIoCallback cb;
   cb.callback = [](IoBaseRequest* req) {
      BlockingIoContext* waitDone = (BlockingIoContext*)(req->user.user_data.val.ptr);
      {
         std::lock_guard<std::mutex> lock(waitDone->mtx);
         waitDone->ready.store(true);
      }
      waitDone->cv.notify_one();
   };
   cb.user_data.val.ptr = waitargs;

   assert(type == IoRequestType::Read);
   execIoChannel().push(type, data, addr, len, cb);

   auto start = mean::readTSC();

   {
      std::unique_lock<std::mutex> lock(waitargs->mtx);
      waitargs->cv.wait(lock, [waitargs] { return waitargs->ready.load(); });
   }

   waiter_pool->release(waitargs);
}

void OsvJobManager::registerExclusiveThread(std::string name, int, TaskFunction taskFun)
{
   throw leanstore::ex::GenericException("cannot be called in the osv job interface");
}
Task& OsvJobManager::this_task()
{
   throw leanstore::ex::GenericException("cannot be called in the osv job interface");
}
int OsvJobManager::workerCount()
{
   return 1;
}
// -------------------------------------------------------------------------------------
}  // namespace mean
