#include "CRCounters.hpp"
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
namespace leanstore
{
        thread_local CRCounters localCRCounters  __attribute__ ((tls_model ("local-exec")));
        std::mutex CRCounters::cr_counters_mut;
        std::vector<CRCounters*> CRCounters::cr_counters{};
        atomic<u64> CRCounters::crs_counter = {0};
        CRCounters& CRCounters::myCounters() {
            return localCRCounters; 
        }}  // namespace leanstore
