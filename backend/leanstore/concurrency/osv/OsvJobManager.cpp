// -------------------------------------------------------------------------------------
#include "OsvJobManager.hpp"
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

   // init the pools that we currently need
   std::cout << "INIT OSV JOBBING MANAGER" << std::endl;
   // ensure(ioOptions.engine == "osv", "ioOptions.engine == osv");
   IoInterface::initInstance(ioOptions);
   std::cout << "FINISHED INIT OSV JOBBING MANAGER" << std::endl;
}
// -------------------------------------------------------------------------------------
void OsvJobManager::start(TaskFunction taskFun)
{
   // all_threads[max_exclusive_threads]->sendTask(taskFun);
   std::cout << "run function" << std::endl; 
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

// OsvBackgroundThreadBase METHODS
unsigned OsvJobManager::getPriority() {
   auto task_queue_load = leanstore_osv_debug::get_task_queue_load(); 
   auto pool_load =  leanstore_osv_debug::get_thread_pool_load(); 
   auto task_stack_size = leanstore_osv_debug::task_stack.size(); 
   // return task_queue_load < 2048 && (pool->available.load() + leanstore_osv_debug::task_stack.size()) >= 128 ? 10 : 0; 

      // trigger when less than 512 tasks are in the queue
      // AND the tasks currently in the queue (512) + the tasks that are added (512) + extra buffer (10) threads are in the buffer that can theoretically could be migrated to
      // AND the local task pool has enough entries to push to the task_queue
      leanstore_osv_debug::trace_parallelfor_state(task_queue_load, pool_load, 0, task_stack_size); 
      return task_queue_load < 256 && (pool_load) > 2048 + 1024 + 512 + 128 ? 15 : 0; // && (pool_load - (task_queue_load)) > 2048 && (pool->available.load() + leanstore_osv_debug::task_stack.size()) >= 2048 ? 10 : 0; 

};

 int OsvJobManager::process() {
   std::atomic<int> used = {0};

   for (u64 id =0; id < 1000000000000000000; id++) {
      auto start = mean::readTSC();

      size_t it = 0; 

      assert(leanstore_osv_debug::task_stack.size() < 4096);
      leanstore_osv_debug::task_stack.push({executed_fn, id});

      if (leanstore_osv_debug::task_stack.size() >= 64) { // FIXME: this is currently a constant 
         leanstore_osv_debug::flush_to_runqueue();
         leanstore_osv_debug::yield(); 
      }
   }

   return 0; 
 };
// OsvBackgroundThreadBase METHODS END

void OsvJobManager::parallelFor(BlockedRange bb, std::function<void(u64)> fun, int tasks, s64 bbgranularity, bool rate_active)
{
   

   executed_fn = &fun; 
   start_background_work(); 
   std::unique_lock<std::mutex> lock(mtx); 
   condvar.wait(lock, [=] {return finished.load(); }); 
}
std::string OsvJobManager::printCountersHeader()
{
   // throw leanstore::ex::GenericException("not implemented");
   return ""; 
}
std::string OsvJobManager::printCounters(int te_id)
{
   // throw leanstore::ex::GenericException("not implemented");
      return ""; 

}
// -------------------------------------------------------------------------------------
void OsvJobManager::scheduleTaskSync(TaskFunction fun)
{
   fun();
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

   jumpmu::thread_local_jumpmu.ready.store(false);

   UserIoCallback cb;
   cb.callback = [](IoBaseRequest* req) {
      jumpmu::JumpMUContext* waitDone = (jumpmu::JumpMUContext*)(req->user.user_data.val.ptr);
      #if true   
      {
         // std::lock_guard<std::mutex> lock(waitDone->mtx);
         if (!waitDone->mtx.try_lock()) {
            // this is a hack that ONLY works in this specific situation and to resolve the situation 
            // this can only happen in the case that the workload thread has not entered the official wait
            // loop 
            waitDone->ready.store(true);
         } else {
            waitDone->ready.store(true); 
            waitDone->mtx.unlock(); 
            waitDone->cv.notify_one();
         }
      }
      #else 
         {
            std::lock_guard<std::mutex> lock(waitDone->mtx);
            waitDone->ready.store(true);
         }
         waitDone->cv.notify_one();
      #endif
   };
   cb.user_data.val.ptr = &jumpmu::thread_local_jumpmu;

   assert(type == IoRequestType::Read);
   execIoChannel().push(type, data, addr, len, cb);

   auto start = mean::readTSC();
   {
      std::unique_lock<std::mutex> lock(jumpmu::thread_local_jumpmu.mtx);
      if (jumpmu::thread_local_jumpmu.ready.load() == false) {
         jumpmu::thread_local_jumpmu.cv.wait(lock, [] { return jumpmu::thread_local_jumpmu.ready.load(); });
      }
   }
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
