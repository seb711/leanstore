#include "WorkerCounters.hpp"
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
namespace leanstore
{
        thread_local WorkerCounters localWorkerCounters  __attribute__ ((tls_model ("local-exec")));
        std::mutex WorkerCounters::worker_counters_mut;
        std::vector<WorkerCounters*> WorkerCounters::worker_counters{};
        atomic<u64> WorkerCounters::workers_counter = {0};
        WorkerCounters& WorkerCounters::myCounters() {
            return localWorkerCounters; 
        }

}  // namespace leanstore
