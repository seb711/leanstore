#pragma once
#include "Units.hpp"
// -------------------------------------------------------------------------------------
#include "PerfEvent.hpp"
#include "leanstore/utils/Hist.hpp"
// -------------------------------------------------------------------------------------
#include <atomic>
#include <mutex>
#include <unordered_map>
// -------------------------------------------------------------------------------------
namespace leanstore
{
struct SSDCounters {
   static constexpr u64 max_ssds = 20;
   atomic<u64> t_id = 9999;                // used by tpcc
   // -------------------------------------------------------------------------------------
   // Space and contention management
   atomic<u64> pushed[max_ssds] = {0};
   atomic<u64> polled[max_ssds] = {0};
   atomic<s64> outstandingx_max[max_ssds] = {0};
   atomic<s64> outstandingx_min[max_ssds] = {0};
   atomic<u64> read_latncy50p[max_ssds] = {0};
   atomic<u64> read_latncy99p[max_ssds] = {0};
   atomic<u64> read_latncy99p9[max_ssds] = {0};
   atomic<u64> read_latncy_max[max_ssds] = {0};
   atomic<u64> write_latncy50p[max_ssds] = {0};
   atomic<u64> write_latncy99p[max_ssds] = {0};
   atomic<u64> write_latncy99p9[max_ssds] = {0};
   atomic<u64> writes[max_ssds] = {0};
   atomic<u64> reads[max_ssds] = {0};
   // -------------------------------------------------------------------------------------
   SSDCounters() { 
      t_id = ssds_counter++;  
      SSDCounters::ssd_counters_mut.lock(); 
      SSDCounters::ssd_counters.push_back(this); 
      SSDCounters::ssd_counters_mut.unlock(); 
   }
   // -------------------------------------------------------------------------------------
   static atomic<u64> ssds_counter;
   // static tbb::enumerable_thread_specific<WorkerCounters> worker_counters;
   static std::vector<SSDCounters*> ssd_counters;
   static std::mutex ssd_counters_mut;
   static SSDCounters& myCounters(); 
};
}  // namespace leanstore
// -------------------------------------------------------------------------------------
