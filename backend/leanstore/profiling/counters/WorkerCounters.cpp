#include "WorkerCounters.hpp"
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
namespace leanstore
{
   std::atomic<WorkerCounters*> WorkerCounters::worker_counters[MAX_CORES] = {nullptr};
        atomic<u64> WorkerCounters::workers_counter = {0};

WorkerCounters& WorkerCounters::myCounters()
{
   int core_id = sched_getcpu();  // Get the current core ID
   WorkerCounters* expected = worker_counters[core_id].load(std::memory_order_acquire);

   if (!expected) {
      WorkerCounters* new_instance = new WorkerCounters(core_id);
      if (!worker_counters[core_id].compare_exchange_strong(expected, new_instance)) {
         // Another thread initialized it first, delete our instance
         delete new_instance;
      }
      expected = worker_counters[core_id].load(std::memory_order_acquire);
   }

   return *expected;
}
}  // namespace leanstore
