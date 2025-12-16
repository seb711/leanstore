#ifdef LEANSTORE_INCLUDE_OSV
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
   shutdown();
}
// -------------------------------------------------------------------------------------
static std::mutex cb_mtx;
// -------------------------------------------------------------------------------------
// env
// -------------------------------------------------------------------------------------
void OsvJobManager::init(int workers_count, int exclusiveThreads, IoOptions ioOptions, [[maybe_unused]] int threadAffinityOffset)
{
   total_threads_count = workers_count;
   max_exclusive_threads = exclusiveThreads;
   ensure(max_exclusive_threads > 0, "in threading mode there must be at least one pp thread. Be sure to not use --nopp flag.");
   IoInterface::initInstance(ioOptions);

   // we need to setup the argument pool for the calls
   for (int t_i = 0; t_i < max_exclusive_threads; t_i++) {
      auto thread = std::make_unique<ThreadWithJump>(
          [&, t_i]() {
             // -------------------------------------------------------------------------------------
             std::string name = std::to_string(t_i);
             leanstore::CPUCounters::registerThread(name, false);
             // -------------------------------------------------------------------------------------
             workers[t_i] = new leanstore::cr::Worker(t_i, workers, workers_count);
             leanstore::cr::Worker::tls_ptr = workers[t_i];
             // -------------------------------------------------------------------------------------
             running_threads++;
             while (ThreadBase::this_thread().keepRunning()) {
                auto& meta = static_cast<ThreadWithJump*>(&ThreadBase::this_thread())->meta;
                std::unique_lock guard(meta.mutex);
                meta.cv.wait(guard, [&]() { return ThreadBase::this_thread().keepRunning() == false || meta.job_set; });
                if (!ThreadBase::this_thread().keepRunning()) {
                   break;
                }
                meta.wt_ready = false;
                printf("started\n");
                meta.task();
                printf("finished\n");
                meta.wt_ready = true;
                meta.job_done = true;
                meta.job_set = false;
                meta.cv.notify_one();
             }
             running_threads--;
          },
          "w_" + std::to_string(t_i), t_i);
      if (t_i < max_exclusive_threads) {
         thread->setCpuAffinityBeforeStart(0);
         thread->setNameBeforeStart("x_" + std::to_string(t_i));
      }
      exclusiveThreadList.push_back(std::move(thread));
      exclusiveThreadList.back()->start();
   }
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
/* void OsvJobManager::registerPageProvider(void* bf_ptr, int partitions_count)
{
   buffer_manager = static_cast<leanstore::storage::BufferManager*>(bf_ptr);

   for (u32 partition_id = 0; partition_id < partitions_count; partition_id++) {
      std::cout << "INIT PAGE PROVIDER " << partition_id << std::endl;
      backgroundThreads.push_back(std::make_unique<OsvPageProvider>(buffer_manager, partition_id));
   }
} */

void OsvJobManager::registerPageProvider(void* bf_ptr, int partitions_count)
{
   auto buffer_manager = static_cast<leanstore::storage::BufferManager*>(bf_ptr);
   for (int t_i = 0; t_i < partitions_count; t_i++) {
      printf("register pp thread\n");

      registerExclusiveThread("pp", t_i, [buffer_manager, t_i, this]() {
         jumpmu::thread_local_jumpmu_ctx = new jumpmu::JumpMUContext{};
         jumpmu::thread_local_jumpmu_ctx->pid = -1;

         auto& iochannel = execIoChannel();
         while (true) {
            buffer_manager->pageProviderCycle(t_i);
            iochannel.submit();
            iochannel.poll();

            leanstore_osv_debug::yield(); 
         }
      });
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

void OsvJobManager::parallelFor(BlockedRange bb,
                                std::function<void(u64, std::atomic<bool>& cancelable)> fun,
                                const int tasks,
                                s64 bbgranularity,
                                bool rate_active)
{
   cpu_set_t cpuset;
   CPU_ZERO(&cpuset);
   CPU_SET(2, &cpuset);
   auto thread = pthread_self();
   int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
   if (s != 0) {
      ensure(false, "[setCpuAffinityThisThread] Affinity could not be set.");
   }

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

   std::function<void(u64)> forwardedFun = [&](u64 value) {
      fun(value, cancelable);
      open_tasks--; 
      if (open_tasks < 512) {
         std::unique_lock<std::mutex> t{queue_mtx}; 
         queue_cv.notify_one(); 
      }
   };

   for (u64 id = bb.begin; id < bb.end; id++) {
      auto start = mean::readTSC();

      assert(leanstore_osv_debug::task_stack.size() < 2048);
      leanstore_osv_debug::task_stack.push({&forwardedFun, id});

      if (leanstore_osv_debug::task_stack.size() > 256) {  // FIXME: this is currently a constant
         unsigned qsize = leanstore_osv_debug::task_stack.size(); 
         if (open_tasks > 1024) {
            std::unique_lock<std::mutex> t{queue_mtx}; 
            queue_cv.wait(t, [&] {return open_tasks.load() < 512;}); 
         }
         leanstore_osv_debug::flush_to_runqueue();
         open_tasks += qsize - leanstore_osv_debug::task_stack.size(); 
      }
   }
}
std::string OsvJobManager::printCountersHeader()
{
   // throw leanstore::ex::GenericException("not implemented");
   return "a"; 
}
std::string OsvJobManager::printCounters(int te_id)
{
   // throw leanstore::ex::GenericException("not implemented");
      return "a"; 
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
   leanstore::WorkerCounters::myCounters().time_counter_0++; 

   leanstore_osv_debug::Waiter waiter{}; 

   UserIoCallback cb;
   cb.callback = [](IoBaseRequest* req) {
      leanstore_osv_debug::Waiter* waiter = (leanstore_osv_debug::Waiter*)(req->user.user_data.val.ptr);
      #if true   
      {
         // std::lock_guard<std::mutex> lock(waitDone->mtx);
         waiter->wake(); 
      }
      #else 
         {
            std::lock_guard<std::mutex> lock(waitDone->mtx);
            waitDone->ready.store(true);
         }
         waitDone->cv.notify_one();
      #endif
   };
   cb.user_data.val.ptr = &waiter;

   assert(type == IoRequestType::Read);
   execIoChannel().push(type, data, addr, len, cb);

   auto start = mean::readTSC();
   {
      waiter.wait(); 
   }
}

void OsvJobManager::registerExclusiveThread(std::string name, int, TaskFunction taskFun)
{
   int id = exclusiveThreadCounter++;
   auto& ex = *exclusiveThreadList[id];
   ex.setNameBeforeStart(name);
   ex.sendTask(taskFun);
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
#endif