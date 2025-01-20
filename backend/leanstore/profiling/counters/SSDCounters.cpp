#include "SSDCounters.hpp"
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
namespace leanstore
{
        thread_local SSDCounters localSSDCounters  __attribute__ ((tls_model ("local-exec")));
        std::mutex SSDCounters::ssd_counters_mut;
        std::vector<SSDCounters*> SSDCounters::ssd_counters{};
        atomic<u64> SSDCounters::ssds_counter = {0};
        SSDCounters& SSDCounters::myCounters() {
            return localSSDCounters; 
        }
}  // namespace leanstore
