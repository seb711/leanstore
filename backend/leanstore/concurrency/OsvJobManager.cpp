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
// -------------------------------------------------------------------------------------

#define USE_JOBS

namespace mean
{
// -------------------------------------------------------------------------------------
OsvJobManager::~OsvJobManager()
{
   shutdown();
}
// -------------------------------------------------------------------------------------
// env
// -------------------------------------------------------------------------------------
void OsvJobManager::init(int workers_count, int exclusiveThreads, IoOptions ioOptions, [[maybe_unused]] int threadAffinityOffset)
{
   pool = new LockFreeObjectPool<Job, JOB_QUEUE_SIZE>();
   waiter_pool = new boost::object_pool<WaitContext>(512);
   std::cout << "INIT OSV JOBBING MANAGER" << std::endl;
   ensure(ioOptions.engine == "osv", "ioOptions.engine == osv");
   // TODO: implement methods in OSv that show which cores are currently not used
   // 1. should there be core that just execute the operations with blocking io

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
                meta.task();
                meta.wt_ready = true;
                meta.job_done = true;
                meta.job_set = false;
                meta.cv.notify_one();
             }
             running_threads--;
          },
          "w_" + std::to_string(t_i), t_i);
      if (t_i < max_exclusive_threads) {
         thread->setCpuAffinityBeforeStart(t_i);
         thread->setNameBeforeStart("x_" + std::to_string(t_i));
      }
      exclusiveThreadList.push_back(std::move(thread));
      exclusiveThreadList.back()->start();
   }

   // for (auto& t : all_threads) {
   //    t.detach();
   // }
   //  -------------------------------------------------------------------------------------
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
// TODO: These methods are only callable from within a exclusive thread
// but for now we assume that
int OsvJobManager::execId()
{
   return ThreadBase::this_thread().id();
}
// -------------------------------------------------------------------------------------
IoChannel& OsvJobManager::execIoChannel()
{
   // int this_id = execId();
   // TODO check if in exclusive thread
   return IoInterface::instance().getIoChannel(0);
}
// -------------------------------------------------------------------------------------
// task
// -------------------------------------------------------------------------------------
void OsvJobManager::registerExclusiveThread(std::string name, int, TaskFunction taskFun)
{
   int id = exclusiveThreadCounter++;
   auto& ex = *exclusiveThreadList[id];
   ex.setNameBeforeStart(name);
   ex.sendTask(taskFun);
}
void OsvJobManager::registerPageProvider(void* bf_ptr, int partitions_count)
{
   auto buffer_manager = static_cast<leanstore::storage::BufferManager*>(bf_ptr);
   for (int t_i = 0; t_i < partitions_count; t_i++) {
      std::cout << "register exclusive thread with page provider on thread " << t_i << std::endl;
      registerExclusiveThread("pp", t_i, [buffer_manager, t_i, this]() {
         std::cout << "pp running on " << sched_getcpu() << std::endl;
         while (true) {
            buffer_manager->pageProviderCycle(t_i);
            execIoChannel().submit();
            execIoChannel().poll();
         }
      });
   }
}
// OsvJobManager

static void job_fn(void* args)
{
   auto* node = (LockFreeObjectPool<Job, JOB_QUEUE_SIZE>::Node*)args;
   auto* job = node->getObject();

   jumpmu::thread_local_jumpmu_ctx = new (&(job->jumpctx)) jumpmu::JumpMUContext;

   (*(job->fun))(job->args.key, job->args.cancelable);

   job->args.pool->release(node);
};

void OsvJobManager::parallelFor(BlockedRange bb, std::function<void(u64, std::atomic<bool>& cancelable)> fun, const int tasks, s64 bbgranularity)
{
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
   std::cout << "bb end " << bb.end << std::endl;

   for (u64 id = bb.begin; id < bb.end; id++) {
      // we just need to send one job after the other i guess

      // TODO: HERE WE JUST NEED TO CALL THE FUNCTION WITH THE CORRECT PARAMETERS

      // we need a osv job enqueue wrapper obj
      // 1. with jumpmu ctx
      // 2. with function to execute
      // 3. with some kind of state that we know it is executed

      LockFreeObjectPool<mean::Job, JOB_QUEUE_SIZE>::Node* node = pool->acquire();
      while (node == nullptr) {
         usleep((pool->getSize() >> 2));  // Optimized for spin-wait on x86 (use __builtin_arm_yield() on ARM)
         node = pool->acquire();
      }
      assert(node);
      auto* job = node->getObject();
      assert(job);
      // auto start = mean::readTSC();
      job->fun = &fun;
      job->args.pool = pool;
      job->args.key = id;

      if (!osv_task_enqueue(job_fn, node)) {
      }
      // auto now = mean::readTSC();
      // auto timeDiff = mean::tscDifferenceNs(now, start);
      // printf("%lu\n", timeDiff);
      // leanstore::WorkerCounters::myCounters().total_setup_tx_time += timeDiff;
      // leanstore::WorkerCounters::myCounters().setup_tx++;
   }

   while (pool->getSize() > 0) {
      _mm_pause();
   }
}
// -------------------------------------------------------------------------------------
void OsvJobManager::registerPoller([[maybe_unused]] int to, TaskFunction poller)
{
   throw std::logic_error("cannot be called when running with threads");
}
std::string OsvJobManager::printCountersHeader()
{
   return "a";
}
std::string OsvJobManager::printCounters(int te_id)
{
   return "a";
}
// -------------------------------------------------------------------------------------
void OsvJobManager::scheduleTaskSync(TaskFunction fun)
{
   // schedule task sync has to be like a normal job but we wait for the it to return
   // exclusiveThreadList.front()->sendTaskBlocking(fun);
   // std::atomic<bool> cancelable = {false};

   /* u64 id = 0;
   struct SyncJob {
      std::mutex mutex;
      std::condition_variable cv;
      jumpmu::JumpMUContext jumpmu_ctx;
      TaskFunction fun;
      bool job_done = false;
   };

   std::cout << "pre pre setup sync" << std::endl;


   SyncJob* job_args = new SyncJob{{}, {}, {}, fun, false};
   std::cout << "jobargs" << std::endl;


   if (!osv_task_enqueue(
           [](void* args) {
            std::cout << "pre setup sync" << std::endl;

              auto* job = (SyncJob*)args;
              std::unique_lock guard(job->mutex);

              jumpmu::thread_local_jumpmu_ctx = new (&(job->jumpmu_ctx)) jumpmu::JumpMUContext;
              std::cout << "setup sync" << std::endl;

              job->fun();

              job->job_done = true;
              guard.unlock();
              job->cv.notify_one();
           },
           job_args)) {
      std::cerr << "osv_task_enqueue failed" << std::endl;
   }

   std::cout << "sync task finished" << std::endl;

   std::unique_lock guard(job_args->mutex);
   job_args->cv.wait(guard, [&]() { return job_args->job_done; });
   delete job_args; */
   jumpmu::thread_local_jumpmu_ctx = new jumpmu::JumpMUContext{};
   fun();
   delete jumpmu::thread_local_jumpmu_ctx;

   // wait here for the job to finish
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
   throw std::logic_error("not implemented right now. come back tomorrow");
   /*
   WaitContext* waitargs = waiter_pool->construct();

   UserIoCallback cb;
   cb.callback = [](IoBaseRequest* req) {
      WaitContext* waitDone = (WaitContext*) (req->user.user_data.val.ptr);
      {
         std::lock_guard<std::mutex> lock(waitDone->mtx);
         waitDone->ready.store(true);
     }
     waitDone->cv.notify_one();
   };
   cb.user_data.val.ptr = waitargs;

   assert(type == IoRequestType::Read);
   execIoChannel().push(type, data, addr, len, cb);

   {
      std::unique_lock<std::mutex> lock(waitargs->mtx);
      waitargs->cv.wait(lock, [waitargs] { return waitargs->ready.load(); });
   }

   waiter_pool->free(waitargs);*/
   // throw std::logic_error("not implemented right now. come back tomorrow");
}

Task& OsvJobManager::this_task()
{
   throw std::logic_error("cannot be called when running with threads");
}
// -------------------------------------------------------------------------------------
// other
// -------------------------------------------------------------------------------------
int OsvJobManager::workerCount()
{
   return total_threads_count;
}
// -------------------------------------------------------------------------------------
}  // namespace mean
