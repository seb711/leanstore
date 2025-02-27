// -------------------------------------------------------------------------------------
#include "OsvJobManager.hpp"
#include "leanstore/concurrency/ConnectedIoChannel.hpp"
#include "leanstore/concurrency/Task.hpp"
#include "leanstore/io/IoInterface.hpp"
#include "leanstore/io/impl/LibaioImpl.hpp"
#include "leanstore/profiling/counters/CPUCounters.hpp"
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
   ensure(ioOptions.engine == "osv");
   // TODO: implement methods in OSv that show which cores are currently not used
   // 1. should there be core that just execute the operations with blocking io

   total_threads_count = exclusiveThreads;
   max_exclusive_threads = exclusiveThreads;
   ensure(max_exclusive_threads > 0, "in threading mode there must be at least one pp thread. Be sure to not use --nopp flag.");
   IoInterface::initInstance(ioOptions);

   // we need to setup the argument pool for the calls
   for (int t_i = 0; t_i < total_threads_count; t_i++) {
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
   int this_id = execId();
   // TODO check if in exclusive thread
   return IoInterface::instance().getIoChannel(this_id);
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
void ThreadingManager::registerPageProvider(void* bf_ptr, int partitions_count)
{
   auto buffer_manager = static_cast<leanstore::storage::BufferManager*>(bf_ptr);
   for (int t_i = 0; t_i < partitions_count; t_i++) {
      registerExclusiveThread("pp", t_i, [buffer_manager, t_i, this]() {
         while (true) {
            buffer_manager->pageProviderCycle(t_i);
            execIoChannel().submit();
            execIoChannel().poll();
         }
      });
   }
}
// OsvJobManager
void OsvJobManager::parallelFor(BlockedRange bb, std::function<void(u64, std::atomic<bool>& cancelable)> fun, const int tasks, s64 bbgranularity)
{
   ensure(tasks > 0);
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
   u64 start = bb.begin;
   for (u64 id = bb.begin; id < bb.end; id++) {
      // we just need to send one job after the other i guess

      // TODO: HERE WE JUST NEED TO CALL THE FUNCTION WITH THE CORRECT PARAMETERS

      // we need a osv job enqueue wrapper obj
      // 1. with jumpmu ctx
      // 2. with function to execute
      // 3. with some kind of state that we know it is executed

      // the taskmanager dont has this problem because one task is just a userthread that does the same as a thread -> there is not a task per query
      // (i guess that was somehow the idea but was not feasible)

      Job* job = pool.construct(Job{fun, JobArguments{id, cancelable, &pool}});

      if (!osv_task_enqueue(
              [](void* args) {
                 auto* job = (Job*)args;

                 jumpmu::thread_local_jumpmu_ctx = new (&(job->jumpctx)) jumpmu::JumpMUContext;

                 job->fun(job->args.key, job->args.cancleable);

                 job->pool->detroy(job);
              },
              job)) {
         std::cerr << "osv_task_enqueue failed" << std::endl;
         return EXIT_FAILURE;
      }

      // FIXME: for now this is ok; but later we need to check how and when to assign the 
      // jobs -> prob the OS takes care of it but for the benchmarks we definitely need that 
      usleep(200);

      // this just sends the ranges to the threads and waits that they are finished
      // this is done sequentially...

      // rangePart = bb;
      // all_threads.at(thr + max_exclusive_threads)->sendTask([&threadsDone, &allDone, threads, fun, rangePart, &cancelable] {
      //   fun(rangePart, cancelable);
      //   threadsDone++;
      //   if (threadsDone == threads) {
      //      allDone.notify_one();
      //   }
      // });
   }
   std::unique_lock<std::mutex> lk(allDoneMutex);
   allDone.wait(lk);
}
// -------------------------------------------------------------------------------------
void ThreadingManager::scheduleTaskSync(TaskFunction fun)
{
   all_threads.at(0 + max_exclusive_threads)->sendTaskBlocking(fun);
}
// -------------------------------------------------------------------------------------
void ThreadingManager::yield([[maybe_unused]] TaskState ts)
{
   // do nothing?
}
// -------------------------------------------------------------------------------------
void ThreadingManager::blockingIo(IoRequestType type, char* data, s64 addr, u64 len)
{
   execIoChannel().pushBlocking(type, data, addr, len);
}
Task& ThreadingManager::this_task()
{
   throw std::logic_error("cannot be called when running with threads");
}
// -------------------------------------------------------------------------------------
// other
// -------------------------------------------------------------------------------------
int ThreadingManager::workerCount()
{
   return total_threads_count - max_exclusive_threads;
}
// -------------------------------------------------------------------------------------
}  // namespace mean
