#include "CRCounters.hpp"
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
namespace leanstore
{
      std::atomic<CRCounters*> CRCounters::cr_counters[MAX_CORES] = {nullptr};
        atomic<u64> CRCounters::cr_counter = {0};

CRCounters& CRCounters::myCounters()
{
   int core_id = sched_getcpu();  // Get the current core ID
   CRCounters* expected = cr_counters[core_id].load(std::memory_order_acquire);

   if (!expected) {
      CRCounters* new_instance = new CRCounters(core_id);
      if (!cr_counters[core_id].compare_exchange_strong(expected, new_instance)) {
         // Another thread initialized it first, delete our instance
         delete new_instance;
      }
      expected = cr_counters[core_id].load(std::memory_order_acquire);
   }

   return *expected;
}}  // namespace leanstore
