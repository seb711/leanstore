#include "ThreadCounters.hpp"
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
namespace leanstore
{
thread_local ThreadCounters localThreadCounters  __attribute__ ((tls_model ("local-exec")));
        std::mutex ThreadCounters::thread_counters_mut;
        std::vector<ThreadCounters*> ThreadCounters::thread_counters{};
        atomic<u64> ThreadCounters::threads_counter = {0};
        ThreadCounters& ThreadCounters::myCounters() {
            return localThreadCounters; 
        }
}  // namespace leanstore
