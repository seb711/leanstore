// -------------------------------------------------------------------------------------
#include "DefaultThreadingManager.hpp"
#include "leanstore/concurrency/ConnectedIoChannel.hpp"
#include "leanstore/concurrency/Task.hpp"
#include "leanstore/io/IoInterface.hpp"
#include "leanstore/io/impl/LibaioImpl.hpp"
#include "leanstore/profiling/counters/CPUCounters.hpp"
#include "leanstore/storage/buffer-manager/BufferManager.hpp"
// -------------------------------------------------------------------------------------
#include <pthread.h>
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
DefaultThreadingManager::~DefaultThreadingManager()
{
   shutdown();
}
// -------------------------------------------------------------------------------------
// env
// -------------------------------------------------------------------------------------
void DefaultThreadingManager::init(int workers_count, int exclusiveThreads, IoOptions ioOptions, [[maybe_unused]] int threadAffinityOffset)
{
   // ensure(ioOptions.engine == "libaio" || ioOptions.engine == "liburing");
#ifdef USE_SAME_THREAD
   exclusiveThreads = workers_count; 
#endif

#ifdef USE_THREAD_POOL
   total_threads_count = (workers_count * FLAGS_worker_per_threads) + exclusiveThreads;
#else
   total_threads_count = exclusiveThreads;
#endif
   max_exclusive_threads = exclusiveThreads;
   ensure(max_exclusive_threads > 0, "in threading mode there must be at least one pp thread. Be sure to not use --nopp flag.");
   IoInterface::initInstance(ioOptions);
   ensure(total_threads_count < MAX_WORKER_THREADS);

   for (int t_i = 0; t_i < exclusiveThreads; t_i++) {
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
      thread->setCpuAffinityBeforeStart(t_i);
      thread->setNameBeforeStart("exclusive_" + std::to_string(t_i));
      exclusive_threads.push_back(std::move(thread));
      exclusive_threads.back()->start();
   }

#ifdef USE_THREAD_POOL
   for (int w_i = 0; w_i < workers_count; w_i++) {

   for (size_t t_i = 0; t_i < FLAGS_worker_per_threads; t_i++) {
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
                auto* this_thread = static_cast<ThreadWithJump*>(&ThreadBase::this_thread());
                auto& meta = this_thread->meta;
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

                ThreadWithJump* prev_top;
                do {
                   prev_top = thread_pool_head.load();
                   this_thread->next = prev_top;
                } while (!thread_pool_head.compare_exchange_weak(prev_top, this_thread));

                if (prev_top == nullptr) {
                   std::unique_lock<std::mutex> lock(threadPoolMutex);
                   threadPoolCV.notify_one();
                }
             }
             running_threads--;
          },
          "w_" + std::to_string(t_i), t_i);
#ifdef USE_SAME_THREAD
      thread->setCpuAffinityBeforeStart(w_i);
#else
      thread->setCpuAffinityBeforeStart(w_i + exclusiveThreads);
#endif
         thread->setNameBeforeStart("worker_" + std::to_string(t_i) + "_" + std::to_string(w_i));
      worker_threads.push_back(std::move(thread));
      worker_threads.back()->start();

      auto* thread_head = worker_threads.back().get();
      thread_head->next = thread_pool_head.load();
      thread_pool_head.store(thread_head);
   }

}

#else
   for (size_t t_i = 0; t_i < FLAGS_worker_per_threads; t_i++) {
      auto thread_data_obj = std::make_unique<ThreadData>(
         &thread_data_pool_head,   // head_pointer
         &threadDataPoolMutex,     // threadDataPoolMutex
                                                          &threadDataPoolCV        // threadDataPoolCV
      );
      thread_data.push_back(std::move(thread_data_obj));

      auto* thread_data_head = thread_data.back().get();
      thread_data_head->next = thread_data_pool_head.load();
      thread_data_pool_head.store(thread_data_head);
   }
#endif
   while (running_threads < total_threads_count) {
   }
}
// -------------------------------------------------------------------------------------
void DefaultThreadingManager::start(TaskFunction taskFun)
{
   // all_threads[max_exclusive_threads]->sendTask(taskFun);
   taskFun();
}
// -------------------------------------------------------------------------------------
void DefaultThreadingManager::shutdown()
{
   for (auto& exe : exclusive_threads) {
      exe->shutdown();
   }
}
// -------------------------------------------------------------------------------------
void DefaultThreadingManager::join()
{
   for (auto& exe : exclusive_threads) {
      exe->join();
   }
}
// -------------------------------------------------------------------------------------
std::string DefaultThreadingManager::stats()
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
void DefaultThreadingManager::adjustWorkerCount(int workerThreads) {}
std::string DefaultThreadingManager::printCountersHeader()
{
   return "a";
}
std::string DefaultThreadingManager::printCounters(int te_id)
{
   return "a";
}
// -------------------------------------------------------------------------------------
// exec
// -------------------------------------------------------------------------------------
int DefaultThreadingManager::execId()
{
   return 0;
}
// -------------------------------------------------------------------------------------
IoChannel& DefaultThreadingManager::execIoChannel()
{
   // int this_id = 0;
   // TODO check if in exclusive thread
   return IoInterface::instance().getIoChannel(0);
}
// -------------------------------------------------------------------------------------
// task
// -------------------------------------------------------------------------------------
void DefaultThreadingManager::registerExclusiveThread(std::string name, int, TaskFunction taskFun)
{
   int id = exclusiveThreadCounter++;
   auto& ex = *exclusive_threads[id];
   ex.setNameBeforeStart(name);
   ex.sendTask(taskFun);
}
void DefaultThreadingManager::registerPageProvider(void* bf_ptr, int partitions_count)
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

#ifndef USE_THREAD_POOL
void* threadFunction(void* arg)
{
   ThreadData* data = static_cast<ThreadData*>(arg);

   // Thread starts here already pinned to core 1
   jumpmu::thread_local_jumpmu_ctx = &data->ctx;
   jumpmu::thread_local_jumpmu_ctx->tx_start_time = data->timestamp; 
   // Execute the function for this specific id
   (*data->fun)(data->id, *data->cancelable);

   // Return token to bucket
   {
      ThreadData* prev_top;
      do {
         prev_top = data->head_pointer->load();
         data->next = prev_top;
      } while (!(*data->head_pointer).compare_exchange_weak(prev_top, data));

      if (prev_top == nullptr) {
         std::unique_lock<std::mutex> lock(*data->threadDataPoolMutex);
         (*data->threadDataPoolCV).notify_one();
      }
   }

   return nullptr;
};
#endif

void DefaultThreadingManager::parallelFor(BlockedRange bb,
                                          std::function<void(u64, std::atomic<bool>& cancelable)> fun,
                                          const int tasks,
                                          s64 bbgranularity,
                                          bool rate_active)
{
   jumpmu::thread_local_jumpmu_ctx = new jumpmu::JumpMUContext();

   // TODO: PIN THE THREAD TO CORE 0

#ifndef IS_LINUX
   if (FLAGS_tx_rate == 0) {
         cpu_set_t cpuset;
         CPU_ZERO(&cpuset);
         CPU_SET(3, &cpuset);
         pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

      leanstore_osv_debug::set_priority(0.01); 
   } else {
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(1, &cpuset);
      pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
   }
#else
   pthread_t thread = pthread_self();  // Or another thread's ID
   struct sched_param param;

   // Set priority
   param.sched_priority = 10;

   // Apply to existing thread
   int ressched = pthread_setschedparam(thread, SCHED_FIFO, &param);
   if (ressched != 0) {
      perror("pthread_setschedparam failed");
   }
#endif
   ensure(tasks > 0);
   // Token bucket for parallelization limit
   const int maxConcurrentThreads = FLAGS_worker_per_threads;
   std::mutex tokenMutex;
   std::condition_variable tokenCV;
   std::atomic<int> availableTokens = {maxConcurrentThreads};

   std::atomic<bool> cancelable = {false};
   std::random_device rd;
   std::mt19937 gen(rd());
   std::exponential_distribution<> expDist(FLAGS_tx_rate);
   auto nextStartTime = mean::readTSC();
   u64 longLat = 0;
   auto localNextStartTime = mean::readTSC();
   u64 localLongLat = 0;

   u64 start = bb.begin;
   u64 end = bb.end;

#ifndef USE_THREAD_POOL
   pthread_attr_t attr;

   void* result;
   cpu_set_t cpuset2;
   pthread_attr_init(&attr);
   CPU_ZERO(&cpuset2);
   CPU_SET(1, &cpuset2);
   pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset2);

   // Vector to hold active threads (we'll manage them as they complete)
   std::vector<pthread_t> activeThreads;
   std::mutex activeThreadsMutex;
#endif

   for (u64 id = start; id < end; id++) {
#ifndef USE_THREAD_POOL
      // Create thread with CPU affinity set before it starts
      pthread_t thread;

      if (thread_data_pool_head.load() == nullptr) {
         std::unique_lock<std::mutex> lock(threadDataPoolMutex);
         threadDataPoolCV.wait(lock, [=] { return thread_data_pool_head.load() != nullptr; });
      }

      ThreadData* old_top = thread_data_pool_head.load();

      while (old_top && !thread_data_pool_head.compare_exchange_weak(old_top, old_top->next)) {
         // CAS failed, retry with updated old_top
         // compare_exchange_weak updates old_top on failure
         // old_top = thread_pool_head.load();
      }

      // printf("got pointer %p %p\n", old_top, old_top->next.load());

      assert(old_top);

      old_top->fun = &fun;
      old_top->id = id;
      old_top->cancelable = &cancelable;
      old_top->timestamp = jumpmu::thread_local_jumpmu_ctx->tx_start_time; 
      pthread_create(&thread, &attr, threadFunction, old_top);

      // Move thread to active threads list
      activeThreads.emplace_back(std::move(thread));

      // Periodically clean up completed threads to avoid memory growth
      if (id % 512 == 0) {
         while (!activeThreads.empty()) {
            auto t = activeThreads.back();
            if (pthread_join(t, &result) != 0) {
               perror("pthread_join");
               exit(1);
            }
            activeThreads.pop_back();
         }
      }
#else
      if (thread_pool_head.load() == nullptr) {
         std::unique_lock<std::mutex> lock(threadPoolMutex);
         threadPoolCV.wait(lock, [=] { return thread_pool_head.load() != nullptr; });
      }

      ThreadWithJump* old_top = thread_pool_head.load();

      while (old_top && !thread_pool_head.compare_exchange_weak(old_top, old_top->next)) {
         // CAS failed, retry with updated old_top
         // compare_exchange_weak updates old_top on failure
         // old_top = thread_pool_head.load();
      }

      // printf("got pointer %p %p\n", old_top, old_top->next.load());

      assert(old_top);

      assert(old_top->meta.job_set == false);
      auto startTime = jumpmu::thread_local_jumpmu_ctx->tx_start_time;

      old_top->sendTask([=, &fun, &id, &cancelable] {
         jumpmu::thread_local_jumpmu_ctx->tx_start_time = startTime;
         fun(id, cancelable);
      });
#endif

      // Rate limiting simulation
      while (true) {
         mean::task::yield();
         auto now = mean::readTSC();
         if (FLAGS_tx_rate == 0 or !rate_active)
            break;
         if (now >= nextStartTime) {
            if (mean::tscDifferenceS(now, jumpmu::thread_local_jumpmu_ctx->tx_start_time) > 1) {
               longLat++;
               nextStartTime = now;
               std::cout << "reset start time" << std::endl;
            }
            auto d = expDist(gen);
            jumpmu::thread_local_jumpmu_ctx->tx_start_time = nextStartTime;
            nextStartTime += mean::nsToTSC(d * 1e9);
            // std::cout << "next: " << nextStartTime << std::flush << std::endl;
            break;
         }
      }
   }
   
   delete jumpmu::thread_local_jumpmu_ctx;
}
// -------------------------------------------------------------------------------------
void DefaultThreadingManager::scheduleTaskSync(TaskFunction fun)
{
   jumpmu::thread_local_jumpmu_ctx = new jumpmu::JumpMUContext();
   fun();
   delete jumpmu::thread_local_jumpmu_ctx;
}
// -------------------------------------------------------------------------------------
void DefaultThreadingManager::yield([[maybe_unused]] TaskState ts)
{
   // do nothing?
   #ifdef IS_LINUX
   std::this_thread::yield(); 
   #else
   // leanstore_osv_debug::yield(); 
   #endif
}
// -------------------------------------------------------------------------------------
void DefaultThreadingManager::blockingIo(IoRequestType type, char* data, s64 addr, u64 len)
{
   // this is only relevant for the OSv runs / in Linux we can only use
   // pread because all the waits are not possible in Linux
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
Task& DefaultThreadingManager::this_task()
{
   throw std::logic_error("cannot be called when running with threads");
}
// -------------------------------------------------------------------------------------
// other
// -------------------------------------------------------------------------------------
int DefaultThreadingManager::workerCount()
{
   return total_threads_count - max_exclusive_threads;
}
// -------------------------------------------------------------------------------------
}  // namespace mean
