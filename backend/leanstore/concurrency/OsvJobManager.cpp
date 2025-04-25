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

#define USE_JOBS
#define USE_BATCH_IPI_ENQUEUE
// #define USE_IO_AS_TASKS

#define USE_TIME_MEASURE

namespace mean
{
// -------------------------------------------------------------------------------------
OsvJobManager::~OsvJobManager()
{
   if (pool)
      delete pool;
   if (waiter_pool)
      delete waiter_pool;
   shutdown();
}
// -------------------------------------------------------------------------------------
static std::mutex cb_mtx;
// -------------------------------------------------------------------------------------
// env
// -------------------------------------------------------------------------------------
void OsvJobManager::init(int workers_count, int exclusiveThreads, IoOptions ioOptions, [[maybe_unused]] int threadAffinityOffset)
{
   pool = new LockFreeObjectPool<Job, JOB_QUEUE_SIZE>{};
   waiter_pool = new LockFreeObjectPool<WaitContext, JOB_QUEUE_SIZE>{};
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
   return 1; 
   // return ThreadBase::this_thread().id();
}
// -------------------------------------------------------------------------------------
IoChannel& OsvJobManager::execIoChannel()
{
   // int this_id = execId();
   // TODO check if in exclusive thread
   auto& ioChannel = IoInterface::instance().getIoChannel(0);
   return ioChannel;
}
IoChannel& OsvJobManager::noExecIoChannel()
{
   // int this_id = execId();
   // TODO check if in exclusive thread
   return IoInterface::instance().getIoChannel(1);
}
IoChannel& OsvJobManager::noExecIoChannel2()
{
   // int this_id = execId();
   // TODO check if in exclusive thread
   return IoInterface::instance().getIoChannel(2);
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
   buffer_manager = static_cast<leanstore::storage::BufferManager*>(bf_ptr);
   for (int t_i = 0; t_i < partitions_count; t_i++) {
      std::cout << "register exclusive thread with page provider on thread " << t_i << std::endl;
      registerExclusiveThread("pp", t_i, [t_i, this]() {
         jumpmu::thread_local_jumpmu_ctx = new jumpmu::JumpMUContext{};
         jumpmu::thread_local_jumpmu_ctx->pid = -1;

         std::cout << "pp running on " << sched_getcpu() << std::endl;
         size_t counter = 0;
         size_t ccounter = 0;
         size_t completed = 0;
         bool setup = true; 

         leanstore_osv_debug::set_priority(0.05);

         auto start = mean::readTSC();
         IoChannel& io_channel = IoInterface::instance().getIoChannel(0);
         // IoChannel& io_channel2 = IoInterface::instance().getIoChannel(1);

         while (setup_mem.load()) {
            auto now = mean::readTSC();

            /*
               THIS IS JUST A TEMPORARY FIX FOR A SITUATION IN WHICH THE PAGEPROVIDER
               CANNOT ACCESS THE LOCKS DUE TO HOW LOCKS ARE IMPLEMENTED IN OSV

               - THE JOB THREADS WAIT FOR FREE PAGES AND REQUEST A LOCK AND THEREFORE
               INCREMENT THE LOCK-COUNTER IN LFMUTEX.CC
               - THE PAGEPROVIDER ALSO WANTS THE LOCK TO FREE PAGES; BUT THE PAGEPROVIDER
               DOES THIS WITH TRY-LOCK AND NOT WITH LOCK AND THEREFORE HAS LEAST PRIORITY

               WE CURRENTLY RESOLVE THIS BY ASSESSING WHEN THIS SITUATION IS ACTIVE (NO FREE PAGES AND THE SITUATION IS NOT HANDLED)

               - IN THAT CASE WE GET LOCKS ON ALL IO PARTITIONS AND CALL THE PAGEPROVIDER

               THIS SOLUTION IS CURRENTLY ONLY POSSIBLE IF WE HAVE ONE COOLING PARTITION
               FIXME: ADD SUPPORT FOR MULTIPLE COOLING PARTITIONS
            */
            if (buffer_manager->cooling_partitions[t_i].dram_free_list.counter == 0 && counter++ > 10) {
               std::vector<std::unique_ptr<std::unique_lock<mean::mutex>>> locks;

               for (size_t io_partition_idx = 0; io_partition_idx < buffer_manager->io_partitions_count; io_partition_idx++) {
                  locks.push_back(std::make_unique<std::unique_lock<mean::mutex>>(buffer_manager->io_partitions[io_partition_idx].io_mutex));
               }

               buffer_manager->pageProviderCycle(t_i);

               counter = 0;
            } else {
               buffer_manager->pageProviderCycle(t_i);
            }


            // if (counter % 2 == 0) {
             unsigned submitted = io_channel.submit();
               // std::cout << "submitted " << submitted << std::endl; 


               unsigned polled = io_channel.poll();

            leanstore::WorkerCounters::myCounters().time_counter_0 += polled;
            // std::cout << "polled " << polled<< std::endl; 

            // leanstore::WorkerCounters::myCounters().time_counter_0++;
            // io_channel2.submit();
            // leanstore::WorkerCounters::myCounters().time_counter_1 += io_channel2.poll();
            // leanstore::WorkerCounters::myCounters().time_counter_1++;
            // } 
            
#ifdef USE_TIME_MEASURE
            auto timeDiff = mean::tscDifferenceUs(now, start);
            // leanstore::WorkerCounters::myCounters().total_time_sum_1 += timeDiff;
            // leanstore::WorkerCounters::myCounters().time_counter_1++;
            // leanstore::WorkerCounters::myCounters().total_ios += completed;

            start = now;
#endif
            // usleep(10);
            // std::this_thread::sleep_for(std::chrono::microseconds(20));
         }
         printf("finished\n"); 
      });
   }
}
// OsvJobManager



static void job_fn(void* args)
{
   auto* job = (Job*)args;

   jumpmu::thread_local_jumpmu_ctx = &(job->jumpctx);

   (*(job->fun))(job->args.key, job->args.cancelable);

   // jumpmu::thread_local_jumpmu_ctx->~JumpMUContext();

   job->args.pool->release(job);
};

using PageProviderFunc = std::function<void(u64, std::atomic<bool>&)>;

void OsvJobManager::parallelFor(BlockedRange bb, std::function<void(u64, std::atomic<bool>& cancelable)> fun, const int tasks, s64 bbgranularity)
{
   // this is just for testing purposes: 
#ifdef USE_IO_AS_TASKS
   setup_mem = false; 
   std::this_thread::sleep_for(std::chrono::seconds(2));
   std::array<mean::mutex, 2> io_mtx2 = {}; 
   std::function<void(u64, std::atomic<bool>& cancelable)> pageprovider_ptr =
      [this, &io_mtx2](u64 arg, std::atomic<bool>& cancelable) {
          assert(jumpmu::thread_local_jumpmu_ctx->task_cpu_id == 0); 
          std::unique_lock<mean::mutex> lock(io_mtx2[jumpmu::thread_local_jumpmu_ctx->task_cpu_id]); 


            
                     assert(buffer_manager != nullptr);
          /*
          * THIS IS JUST A TEMPORARY FIX FOR A SITUATION IN WHICH THE PAGEPROVIDER
          * CANNOT ACCESS THE LOCKS DUE TO HOW LOCKS ARE IMPLEMENTED IN OSV
          * - THE JOB THREADS WAIT FOR FREE PAGES AND REQUEST A LOCK AND THEREFORE
          * INCREMENT THE LOCK-COUNTER IN LFMUTEX.CC
          * - THE PAGEPROVIDER ALSO WANTS THE LOCK TO FREE PAGES; BUT THE PAGEPROVIDER
          * DOES THIS WITH TRY-LOCK AND NOT WITH LOCK AND THEREFORE HAS LEAST PRIORITY
          * WE CURRENTLY RESOLVE THIS BY ASSESSING WHEN THIS SITUATION IS ACTIVE (NO FREE PAGES AND THE SITUATION IS NOT HANDLED)
          * - IN THAT CASE WE GET LOCKS ON ALL IO PARTITIONS AND CALL THE PAGEPROVIDER
          * THIS SOLUTION IS CURRENTLY ONLY POSSIBLE IF WE HAVE ONE COOLING PARTITION
          * FIXME: ADD SUPPORT FOR MULTIPLE COOLING PARTITIONS
          */
          if (buffer_manager->cooling_partitions[0].dram_free_list.counter == 0) {
            std::vector<std::unique_ptr<std::unique_lock<mean::mutex>>> locks;

            for (size_t io_partition_idx = 0; io_partition_idx < buffer_manager->io_partitions_count; io_partition_idx++) {
               locks.push_back(std::make_unique<std::unique_lock<mean::mutex>>(buffer_manager->io_partitions[io_partition_idx].io_mutex));
            }

            buffer_manager->pageProviderCycle(0);
         } else {
            buffer_manager->pageProviderCycle(0);
         }

         IoInterface::instance().getIoChannel(jumpmu::thread_local_jumpmu_ctx->task_cpu_id).submit();
         unsigned polled = IoInterface::instance().getIoChannel(jumpmu::thread_local_jumpmu_ctx->task_cpu_id).poll();
         assert(polled < 256); 
         leanstore::WorkerCounters::myCounters().total_time_sum_1 += polled;
         leanstore::WorkerCounters::myCounters().time_counter_1++;
      };

      std::function<void(u64, std::atomic<bool>& cancelable)> iofn_ptr =
      [this, &io_mtx2](u64 arg, std::atomic<bool>& cancelable) {
         std::unique_lock<mean::mutex> lock(io_mtx2[jumpmu::thread_local_jumpmu_ctx->task_cpu_id]); 

         assert(jumpmu::thread_local_jumpmu_ctx->task_cpu_id == 1); 
            IoInterface::instance().getIoChannel(jumpmu::thread_local_jumpmu_ctx->task_cpu_id).submit();
            leanstore::WorkerCounters::myCounters().total_time_sum_0 += IoInterface::instance().getIoChannel(jumpmu::thread_local_jumpmu_ctx->task_cpu_id).poll();
            leanstore::WorkerCounters::myCounters().time_counter_0++;

         
      };
#endif

   // leanstore_osv_debug::set_priority(0.5);
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
      size_t it = 0;
      auto* job = pool->acquire();
      while (job == nullptr) {
         leanstore_osv_debug::rcu_flush();
         leanstore_osv_debug::wait_until_zombies_reaped();
         pool->waitUntilAvailable();
         job = pool->acquire();
      }

      leanstore::WorkerCounters::myCounters().total_time_sum_2 += pool->getAvailable();
      leanstore::WorkerCounters::myCounters().time_counter_2++;

      auto start = mean::readTSC();

#ifdef USE_IO_AS_TASKS
      job->fun = (id % 32 == 0) or (pool->getAvailable() < 128) ? (leanstore_osv_debug::current_worker == 0 ? &pageprovider_ptr : &iofn_ptr) : &fun; 
#else 
job->fun =  &fun; 
#endif

      // new (job) Job;
      assert(job);
      // auto start = mean::readTSC();
      // job->fun = &fun;
      job->args.pool = pool;
      job->args.key = id;
      job->args.done = &done_tasks;
      job->args.started = &started_tasks;
      // job->jumpctx.task_cpu_id = leanstore_osv_debug::current_worker; 

#ifdef USE_JOBS
#ifdef USE_BATCH_IPI_ENQUEUE
      assert(leanstore_osv_debug::task_stack.size() < 2048);
      leanstore_osv_debug::task_stack.push({job_fn, job});
#else
      osv_task_enqueue(job_fn, job); 
#endif
#else
      // JUST EXECUTE IT IN THE MAIN THREAD; THIS IS EASIER FOR DEBUGGING THE MAIN CODE
      jumpmu::thread_local_jumpmu_ctx = new (&(job->jumpctx)) jumpmu::JumpMUContext;
      fun(id, job->args.cancelable);
      pool->release(node);
#endif
#ifdef USE_TIME_MEASURE
      auto now = mean::readTSC();
      auto timeDiff = mean::tscDifferenceNs(now, start);
#endif

#ifdef USE_BATCH_IPI_ENQUEUE
      if (id % 16 == 0) {
         leanstore_osv_debug::flush_to_runqueue(); 
      }
#endif 
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

   // new (waitargs) WaitContext;

   waitargs->ready = false;
   waitargs->magic = mean::readTSC();

   assert(!waitargs->ready);

   UserIoCallback cb;
   cb.callback = [](IoBaseRequest* req) {
      WaitContext* waitDone = (WaitContext*)(req->user.user_data.val.ptr);
      {
         std::lock_guard<std::mutex> lock(waitDone->mtx);
         waitDone->ready.store(true);
      }
      waitDone->cv.notify_one();
      // -------------------------------------------------------------------------------------
   }; 
   cb.user_data.val.ptr = waitargs;

   assert(type == IoRequestType::Read);

// NOTE: FOR NOW WE GO EXTRA SAFE AND ADD A MUTEX FOR IO CHANNEL
//       ACCESS. BUT WE NORMALLY SHOULD NOT NEED THEM.
#ifndef NDEBUG
   waiting_threads++;
#endif
   // std::cout << "waiting threads " << waiting_threads << " overall free " << pool->getSize() << std::endl;
   {
      // IoInterface::instance().getIoChannel(jumpmu::thread_local_jumpmu_ctx->task_cpu_id).push(type, data, addr, len, cb);
      IoInterface::instance().getIoChannel(0).push(type, data, addr, len, cb);
      // std::unique_lock l(cb_mtx);
   }

   auto start = mean::readTSC();

   // First check atomically without locking
   {
      // Only if not ready, use the condition variable
      std::unique_lock<std::mutex> lock(waitargs->mtx);
      waitargs->cv.wait(lock, [waitargs] { return waitargs->ready.load(); });
   }
#ifndef NDEBUG
   waiting_threads--;
#endif

#ifdef USE_TIME_MEASURE
   auto done = mean::readTSC();
   auto timeDiff = mean::tscDifferenceUs(done, start);
   leanstore::WorkerCounters::myCounters().total_time_sum_3 += timeDiff;
   leanstore::WorkerCounters::myCounters().time_counter_3++;
#endif

   waiter_pool->release(waitargs);
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
