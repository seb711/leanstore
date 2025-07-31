// -------------------------------------------------------------------------------------
#include "ThreadingManager.hpp"
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
#include <osv/leanstore_debug.hh>
#include <sstream>
#include <stdexcept>
#include <string>
// -------------------------------------------------------------------------------------
namespace mean
{
// -------------------------------------------------------------------------------------
ThreadingManager::~ThreadingManager()
{
   shutdown();
}
// -------------------------------------------------------------------------------------
// env
// -------------------------------------------------------------------------------------
void ThreadingManager::init(int workers_count, int exclusiveThreads, IoOptions ioOptions, [[maybe_unused]] int threadAffinityOffset)
{
   // ensure(ioOptions.engine == "libaio" || ioOptions.engine == "liburing");
   total_threads_count = (workers_count * FLAGS_worker_per_threads) + exclusiveThreads;
   max_exclusive_threads = exclusiveThreads;
   ensure(max_exclusive_threads > 0, "in threading mode there must be at least one pp thread. Be sure to not use --nopp flag.");
   IoInterface::initInstance(ioOptions);
   ensure(total_threads_count < MAX_WORKER_THREADS);

   for (int t_i = 0; t_i < exclusiveThreads; t_i++) {
      auto thread = std::make_unique<ThreadWithJump>(
          [&, t_i]() {
             // this is the setup code so to say

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
      thread->setCpuAffinityBeforeStart(t_i);
      thread->setNameBeforeStart("exclusive_" + std::to_string(t_i));
      exclusive_threads.push_back(std::move(thread));
      exclusive_threads.back()->start();
   }

   for (int t_i = 0; t_i < workers_count; t_i++) {
      worker_threads[t_i] = std::move(std::vector<std::unique_ptr<ThreadWithJump>>());
      workers[t_i] = new leanstore::cr::Worker(t_i, workers, workers_count);

      for (int c_i = 0; c_i < FLAGS_worker_per_threads; c_i++) {
         auto thread = std::make_unique<ThreadWithJump>(
             [&, t_i]() {
                // this is the setup code so to say

                // -------------------------------------------------------------------------------------
                std::string name = std::to_string(t_i);
                leanstore::CPUCounters::registerThread(name, false);
                // -------------------------------------------------------------------------------------
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
         thread->setCpuAffinityBeforeStart(t_i + exclusiveThreads);
         thread->setNameBeforeStart("worker_" + std::to_string(t_i) + "_" + std::to_string(c_i));
         worker_threads[t_i].push_back(std::move(thread));
         worker_threads[t_i].back()->start();
      }
   }

   // for (auto& t : all_threads) {
   //    t.detach();
   // }
   //  -------------------------------------------------------------------------------------
   //  Wait until all worker threads are initialized
   while (running_threads < total_threads_count) {
   }
}
// -------------------------------------------------------------------------------------
void ThreadingManager::start(TaskFunction taskFun)
{
   // all_threads[max_exclusive_threads]->sendTask(taskFun);
   taskFun();
}
// -------------------------------------------------------------------------------------
void ThreadingManager::shutdown()
{
   for (auto& exe : exclusive_threads) {
      exe->shutdown();
   }

   for (auto& l_exe : worker_threads) {
      for (auto& exe : l_exe.second) {
         exe->shutdown();
      }
   }
}
// -------------------------------------------------------------------------------------
void ThreadingManager::join()
{
   for (auto& exe : exclusive_threads) {
      exe->join();
   }

   for (auto& l_exe : worker_threads) {
      for (auto& exe : l_exe.second) {
         exe->join();
      }
   }
}
// -------------------------------------------------------------------------------------
std::string ThreadingManager::stats()
{
   /*
   std::stringstream ss;
   for (auto& exe: execs) {
           ss << exe->id()  << ": "<<  exe->getName() << " ";
           exe->counters.printCounters(ss);
           exe->counters.reset();
           ss << "\t";
           exe->ioChannel.printCounters(ss);
           ss << std::endl;
   }
   ss << std::endl;
   return ss.str();
   */
   return "";
}
void ThreadingManager::adjustWorkerCount(int workerThreads) {}
std::string ThreadingManager::printCountersHeader()
{
   return "a";
}
std::string ThreadingManager::printCounters(int te_id)
{
   return "a";
}
// -------------------------------------------------------------------------------------
// exec
// -------------------------------------------------------------------------------------
int ThreadingManager::execId()
{
   return 0;
}
// -------------------------------------------------------------------------------------
IoChannel& ThreadingManager::execIoChannel()
{
   // int this_id = 0;
   // TODO check if in exclusive thread
   return IoInterface::instance().getIoChannel(0);
}
// -------------------------------------------------------------------------------------
// task
// -------------------------------------------------------------------------------------
void ThreadingManager::registerExclusiveThread(std::string name, int, TaskFunction taskFun)
{
   int id = exclusiveThreadCounter++;
   auto& ex = *exclusive_threads[id];
   ex.setNameBeforeStart(name);
   ex.sendTask(taskFun);
}
void ThreadingManager::registerPageProvider(void* bf_ptr, int partitions_count)
{
   auto buffer_manager = static_cast<leanstore::storage::BufferManager*>(bf_ptr);
   for (int t_i = 0; t_i < partitions_count; t_i++) {
      printf("register pp thread\n");
      registerExclusiveThread("pp", t_i, [buffer_manager, t_i, this]() {
         auto& iochannel = execIoChannel();
         while (true) {
            buffer_manager->pageProviderCycle(t_i);
            iochannel.submit();
            iochannel.poll();
         }
      });
   }
}
// -------------------------------------------------------------------------------------
void ThreadingManager::parallelFor(BlockedRange bb,
                                   std::function<void(u64, std::atomic<bool>& cancelable)> fun,
                                   const int tasks,
                                   s64 bbgranularity,
                                   bool rate_active)
{
   ensure(tasks > 0);
   const int threads = workerCount();
   u64 range = (bb.end - bb.begin) / threads;
   std::mutex allDoneMutex;
   std::condition_variable allDone;
   std::atomic<int> threadsDone = {0};
   std::atomic<bool> cancelable = {false};

   assert(range > 0);
   u64 remaining = (bb.end - bb.begin) % threads;
   if (range == 0) {
      range = 1;
      remaining = 0;
   }
   u64 start = bb.begin;

   // work stealing

   // TaskExecutor::localExec().disableMessagePoller = false;

   for (int thr = 0; thr < threads; thr++) {
      unsigned c_i = thr / FLAGS_worker_per_threads;
      BlockedRange rangePart(start, start + range);

      worker_threads[c_i][thr % FLAGS_worker_per_threads]->sendTask(
          [&, thr, c_i] {
             jumpmu::thread_local_jumpmu_ctx = new jumpmu::JumpMUContext();
             // work stealing
             u64 start = rangePart.begin;
             u64 end = rangePart.end;

             auto nextStartTime = mean::readTSC();
             u64 longLat = 0;

             const float rate = FLAGS_tx_rate / (threads);
             std::random_device rd;
             std::mt19937 gen(rd());
             std::exponential_distribution<> expDist(rate);

             for (u64 id = start; id < end; id++) {
                fun(id, cancelable);

                // this has to be done in order to simulate the latency
                while (true) {
                   mean::task::yield();
                   auto now = mean::readTSC();
                   if (rate == 0 or !rate_active)
                      break;
                   if (now >= nextStartTime) {
                      if (mean::tscDifferenceS(now, jumpmu::thread_local_jumpmu_ctx->tx_start_time) > 1) {
                         longLat++;
                         nextStartTime = now;
                         // std::cout << "reset start time" << std::endl;
                         if (longLat % 100000 == 0) {
                            // std::cout << "thr: " << mean::exec::getId() << " long latency: " << longLat << std::endl;
                         }
                      }
                      auto d = expDist(gen);
                      jumpmu::thread_local_jumpmu_ctx->tx_start_time = nextStartTime;
                      nextStartTime += mean::nsToTSC(d * 1e9);
                      // std::cout << "next: " << nextStartTime << std::flush << std::endl;
                      break;
                   }
                }
                // END: LATENCY TESTS
             }

             threadsDone++;
             if (threadsDone == threads) {
                std::unique_lock<std::mutex> lk(allDoneMutex);
                allDone.notify_one();
             }
             delete jumpmu::thread_local_jumpmu_ctx;
          });
      start += range;
   }

   std::unique_lock<std::mutex> lk(allDoneMutex);
   allDone.wait(lk);
}
// -------------------------------------------------------------------------------------
void ThreadingManager::scheduleTaskSync(TaskFunction fun)
{
   jumpmu::thread_local_jumpmu_ctx = new jumpmu::JumpMUContext();
   fun();
   delete jumpmu::thread_local_jumpmu_ctx;
}
// -------------------------------------------------------------------------------------
void ThreadingManager::yield([[maybe_unused]] TaskState ts)
{
   #ifdef IS_LINUX
   std::this_thread::yield(); 
   #else
   leanstore_osv_debug::yield(); 
   #endif
}
// -------------------------------------------------------------------------------------
void ThreadingManager::blockingIo(IoRequestType type, char* data, s64 addr, u64 len)
{
   leanstore_osv_debug::Waiter waiter{};

   UserIoCallback cb;
   cb.callback = [](IoBaseRequest* req) {
      leanstore_osv_debug::Waiter* waiter = (leanstore_osv_debug::Waiter*)(req->user.user_data.val.ptr);
      {
         waiter->wake();
      }
   };
   cb.user_data.val.ptr = &waiter;

   assert(type == IoRequestType::Read);
   execIoChannel().push(type, data, addr, len, cb);

   auto start = mean::readTSC();
   {
      waiter.wait();
   }
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
