#pragma once
// -------------------------------------------------------------------------------------
#include "BlockedRange.hpp"
#include "PreemptLock.hpp"
#include "Task.hpp"
#include "YieldLock.hpp"
#include "DebugLock.hpp"
#include "leanstore/io/IoInterface.hpp"
#include <osv/mutex.h>
// -------------------------------------------------------------------------------------
#include <functional>
#include <string>
// -------------------------------------------------------------------------------------
namespace mean
{
// -------------------------------------------------------------------------------------
#if !defined(MEAN_USE_TASKING) && !defined(MEAN_USE_THREADING) && !defined(MEAN_USE_JOBBING) && !defined(MEAN_USE_DEFAULT_THREADING)
#define MEAN_USE_TASKING
#endif



#if defined(MEAN_USE_THREADING) || defined(MEAN_USE_DEFAULT_THREADING)
using mutex = lockfree::mutex;
#elif defined(MEAN_USE_JOBBING) && defined(NDEBUG)
using mutex = lockfree::mutex;
#elif defined(MEAN_USE_JOBBING)
using mutex = lockfree::mutex; // DebugLock;
#else
using mutex = YieldLock;
#endif

#ifdef MEAN_USE_TASKING
using io_mutex = YieldLock;
#else
using io_mutex = lockfree::mutex;
#endif

using TaskFunction = std::function<void()>;  // std::add_pointer_t<void()>;
// -------------------------------------------------------------------------------------
namespace env
{
void init(int workerThreads, int exclusiveThreads, IoOptions ioOptions, int threadAffinityOffset = 0);
// ExecEnv& instance();
void start(TaskFunction fun);
void shutdown();
void join();
void sleepAll(float sleep);
int workerCount();
void adjustWorkerCount(int workerThreads);
void registerPageProvider(void* bm_ptr, u64 partitions_count);
std::string printCountersHeader();
std::string printCounters(int te_id);
template <typename impl>
impl& implementation();  // for internal use only
}  // namespace env
// -------------------------------------------------------------------------------------
namespace exec
{
// void* exec();
IoChannel& ioChannel();
int getId();
}  // namespace exec
// -------------------------------------------------------------------------------------
namespace task
{
void registerExclusiveThread(std::string name, int id, TaskFunction fun);
void parallelFor(BlockedRange bb, std::function<void(u64, std::atomic<bool>&)> fun, int tasks, s64 granularity = -1, bool rate_active=false);
void scheduleTaskSync(TaskFunction fun);
// -------------------------------------------------------------------------------------
void yield(TaskState ts = TaskState::Ready);
void read(char* data, s64 addr, u64 len);
void write(char* data, s64 addr, u64 len);
Task& this_task();
}  // namespace task
// -------------------------------------------------------------------------------------
}  // namespace mean
