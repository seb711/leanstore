#pragma once
#include "Units.hpp"
// -------------------------------------------------------------------------------------

#include "PerfEvent.hpp"
#include "leanstore/utils/Hist.hpp"
// -------------------------------------------------------------------------------------
#include <atomic>
#include <unordered_map>
// -------------------------------------------------------------------------------------
namespace leanstore
{
struct CRCounters {
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
   explicit CRCounters(int core) : core_id(core), t_id(cr_counter++) {}
   // -------------------------------------------------------------------------------------
   static std::atomic<uint64_t> cr_counter;
   static std::array<std::atomic<CRCounters*>, MAX_CORES> cr_counters; // Per-core storage
   static CRCounters& myCounters(); 

   int core_id;
   int t_id;
};
}  // namespace leanstore
// -------------------------------------------------------------------------------------
