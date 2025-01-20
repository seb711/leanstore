#include "PPCounters.hpp"
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
namespace leanstore
{
thread_local PPCounters localPPCounters  __attribute__ ((tls_model ("local-exec")));
        std::mutex PPCounters::pp_counters_mut;
        std::vector<PPCounters*> PPCounters::pp_counters{};
        atomic<u64> PPCounters::pps_counter = {0};
        PPCounters& PPCounters::myCounters() {
            return localPPCounters; 
        }}
