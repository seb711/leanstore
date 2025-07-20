#pragma once
#include "Units.hpp"
// -------------------------------------------------------------------------------------
#include <mutex>
#include <vector>

// -------------------------------------------------------------------------------------
#include <atomic>
// -------------------------------------------------------------------------------------
namespace leanstore
{
struct CRCounters {
   atomic<u64> t_id = 9999;                // used by tpcc
   atomic<s64> worker_id = -1;
   atomic<u64> written_log_bytes = 0;
   atomic<u64> wal_reserve_blocked = 0;
   atomic<u64> wal_reserve_immediate = 0;
   // -------------------------------------------------------------------------------------
   atomic<u64> gct_total_ms = 0;
   atomic<u64> gct_phase_1_ms = 0;
   atomic<u64> gct_phase_2_ms = 0;
   atomic<u64> gct_write_ms = 0;
   atomic<u64> gct_write_bytes = 0;
   // -------------------------------------------------------------------------------------
   atomic<u64> gct_rounds = 0;
   atomic<u64> gct_committed_tx = 0;
   // -------------------------------------------------------------------------------------
   // -------------------------------------------------------------------------------------
   CRCounters() { 
      t_id = crs_counter++;  
      CRCounters::cr_counters_mut.lock(); 
      CRCounters::cr_counters.push_back(this); 
      CRCounters::cr_counters_mut.unlock(); 
   }
   // -------------------------------------------------------------------------------------
   static atomic<u64> crs_counter;
   // static tbb::enumerable_thread_specific<WorkerCounters> worker_counters;
   static std::vector<CRCounters*> cr_counters;
   static std::mutex cr_counters_mut;
   static CRCounters& myCounters(); 
};
}  // namespace leanstore
// -------------------------------------------------------------------------------------
