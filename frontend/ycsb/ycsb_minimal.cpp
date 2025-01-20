#include "Time.hpp"
#include "Units.hpp"
#include "leanstore/BTreeAdapter.hpp"
#include "leanstore/Config.hpp"
#include "leanstore/LeanStore.hpp"
#include "leanstore/profiling/counters/WorkerCounters.hpp"
#include "leanstore/profiling/counters/ThreadCounters.hpp"
#include "leanstore/utils/FVector.hpp"
#include "leanstore/utils/Files.hpp"
#include "leanstore/utils/RandomGenerator.hpp"
#include "leanstore/utils/ScrambledZipfGenerator.hpp"
// -------------------------------------------------------------------------------------
#include "leanstore/concurrency/Mean.hpp"
#include "leanstore/io/IoInterface.hpp"
// -------------------------------------------------------------------------------------
#include <gflags/gflags.h>
// -------------------------------------------------------------------------------------
#include <iostream>
#include <set>
// -------------------------------------------------------------------------------------
DEFINE_uint32(ycsb_read_ratio, 100, "");
DEFINE_uint64(ycsb_tuple_count, 0, "");
DEFINE_uint32(ycsb_payload_size, 100, "tuple size in bytes");
DEFINE_uint32(ycsb_warmup_rounds, 0, "");
DEFINE_uint32(ycsb_tx_rounds, 1, "");
DEFINE_uint32(ycsb_tx_count, 0, "default = tuples");
DEFINE_bool(verify, false, "");
DEFINE_bool(ycsb_scan, false, "");
DEFINE_bool(ycsb_tx, true, "");
DEFINE_bool(ycsb_count_unique_lookup_keys, true, "");
// -------------------------------------------------------------------------------------
using namespace leanstore;
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
using YCSBKey = u64;
using YCSBPayload = BytesPayload<120>;
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
double calculateMTPS(chrono::high_resolution_clock::time_point begin, chrono::high_resolution_clock::time_point end, u64 factor)
{
   double tps = ((factor * 1.0 / (chrono::duration_cast<chrono::microseconds>(end - begin).count() / 1000000.0)));
   return (tps / 1000000.0);
}
// -------------------------------------------------------------------------------------
void run_ycsb() {
   // -------------------------------------------------------------------------------------
   chrono::high_resolution_clock::time_point begin, end;
   // -------------------------------------------------------------------------------------
   // LeanStore DB
   LeanStore db;
   unique_ptr<BTreeInterface<YCSBKey, YCSBPayload>> adapter;
   mean::task::scheduleTaskSync([&]() {
         auto& vs_btree = db.registerBTreeLL("ycsb", "y");
         adapter.reset(new BTreeVSAdapter<YCSBKey, YCSBPayload>(vs_btree));
         db.registerConfigEntry("ycsb_read_ratio", FLAGS_ycsb_read_ratio);
         db.registerConfigEntry("ycsb_target_gib", FLAGS_target_gib);
   });
   // -------------------------------------------------------------------------------------
   auto& table = *adapter;
   const u64 ycsb_tuple_count = (FLAGS_ycsb_tuple_count)
                                    ? FLAGS_ycsb_tuple_count
                                    : FLAGS_target_gib * 1024 * 1024 * 1024 * 1.0 / 2.0 / (sizeof(YCSBKey) + sizeof(YCSBPayload));

   db.startProfilingThread();
   // -------------------------------------------------------------------------------------
   auto zipf_random = std::make_unique<utils::ScrambledZipfGenerator>(0, ycsb_tuple_count, FLAGS_zipf_factor);
   cout << setprecision(4);
   // -----------------------------------------------------------------------------------
   cout << "-------------------------------------------------------------------------------------" << endl;
   cout << "~Transactions" << endl;
   atomic<bool> keep_running = true;
   atomic<u64> running_threads_counter = 0;
   {
      auto start = mean::getSeconds();
      YCSBPayload result;
      YCSBKey key; 
      /* auto ycsb_tx = [&](mean::BlockedRange bb, std::atomic<bool>& cancelled){
      u64 i = bb.begin;
         running_threads_counter++;
         while (i < bb.end) {
            key = zipf_random->rand();
            assert(key < ycsb_tuple_count);
            // table.lookup(key, result);
            WorkerCounters::myCounters().tx++;
           i++;
         }
         running_threads_counter--;
      }; */

     //  mean::BlockedRange bb(0, (u64)1000000000000ul);
      auto startTsc = mean::readTSC();
      auto startTP = mean::getTimePoint();
      // mean::task::parallelFor(bb, ycsb_tx, FLAGS_worker_tasks, 50000000);
      u64 i = 0;
      u64 end = 1000000000000ul; 
      running_threads_counter++;
      while (i < end) {
         // key = zipf_random->rand();
         // assert(key < ycsb_tuple_count);
         // table.lookup(key, result);
         WorkerCounters::myCounters().tx++;
         i++;
      }


      auto diffTSC = mean::tscDifferenceNs(mean::readTSC(), startTsc) / 1e9;
      auto diffTP = mean::timePointDifference(mean::getTimePoint(), startTP) / 1e9;
      std::cout << "done: time: " << diffTP << " tsc: " << diffTSC << std::endl;
   }
   mean::env::shutdown();
   cout << "-------------------------------------------------------------------------------------" << endl;
   // -------------------------------------------------------------------------------------
}
// -------------------------------------------------------------------------------------
int main(int argc, char** argv)
{
   cpu_set_t cpuset;
   CPU_ZERO(&cpuset);
   CPU_SET(0, &cpuset);
   auto thread = pthread_self();

   int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
   if (s != 0) {
      ensure(false, "Affinity could not be set.");
   }
   s = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
   if (s != 0) {
      ensure(false, "Affinity could not be set.");
   }

   gflags::SetUsageMessage("Leanstore Frontend");
   gflags::ParseCommandLineFlags(&argc, &argv, true);
   // -------------------------------------------------------------------------------------
   // -------------------------------------------------------------------------------------
   using namespace mean;
   IoOptions ioOptions("auto", FLAGS_ssd_path);
   ioOptions.write_back_buffer_size = PAGE_SIZE;
   ioOptions.engine = FLAGS_ioengine;
   ioOptions.ioUringPollMode = FLAGS_io_uring_poll_mode;
   ioOptions.ioUringShareWq = FLAGS_io_uring_share_wq;
   ioOptions.raid5 = FLAGS_raid5;
   ioOptions.truncate = true;
   ioOptions.iodepth = (FLAGS_async_batch_size + FLAGS_worker_tasks)*2; // hacky, how to take into account for remotes 
   // -------------------------------------------------------------------------------------
   if (FLAGS_nopp) {
      ioOptions.channelCount = FLAGS_worker_threads;
      mean::env::init(
         FLAGS_worker_threads, //std::min(std::thread::hardware_concurrency(), FLAGS_tpcc_warehouse_count),
         0/*FLAGS_pp_threads*/, ioOptions);
   } else {
      ioOptions.channelCount = FLAGS_worker_threads + FLAGS_pp_threads;
      mean::env::init(FLAGS_worker_threads, FLAGS_pp_threads, ioOptions);
   }
   mean::env::start(run_ycsb);
   // -------------------------------------------------------------------------------------
   mean::env::join();
   // -------------------------------------------------------------------------------------
   return 0;
}
