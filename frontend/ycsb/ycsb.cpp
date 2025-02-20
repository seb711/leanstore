#include "../shared/LeanStoreAdapter.hpp"
#include "../shared/Schema.hpp"
#include "Time.hpp"
#include "Units.hpp"
#include "leanstore/Config.hpp"
#include "leanstore/LeanStore.hpp"
#include "leanstore/profiling/counters/WorkerCounters.hpp"
#include "leanstore/utils/FVector.hpp"
#include "leanstore/utils/Files.hpp"
#include "leanstore/utils/Parallelize.hpp"
#include "leanstore/utils/RandomGenerator.hpp"
#include "leanstore/utils/ScrambledZipfGenerator.hpp"
// -------------------------------------------------------------------------------------
#include <gflags/gflags.h>
#include <tbb/parallel_for.h>
// -------------------------------------------------------------------------------------
#include <osv/task.h>
#include <iostream>
#include <set>

// -------------------------------------------------------------------------------------
DEFINE_uint32(ycsb_read_ratio, 100, "");
DEFINE_uint64(ycsb_tuple_count, 0, "");
DEFINE_uint32(ycsb_payload_size, 100, "tuple size in bytes");
DEFINE_uint32(ycsb_warmup_rounds, 0, "");
DEFINE_uint32(ycsb_insert_threads, 0, "");
DEFINE_uint32(ycsb_threads, 0, "");
DEFINE_bool(ycsb_count_unique_lookup_keys, true, "");
DEFINE_bool(ycsb_warmup, true, "");
DEFINE_uint32(ycsb_sleepy_thread, 0, "");
DEFINE_uint32(ycsb_ops_per_tx, 1, "");
// -------------------------------------------------------------------------------------
using namespace leanstore;
// -------------------------------------------------------------------------------------
using YCSBKey = u64;
using YCSBPayload = BytesPayload<8>;
using KVTable = Relation<YCSBKey, YCSBPayload>;
// -------------------------------------------------------------------------------------
template<class Fn>
void parallel_for(uint64_t begin, uint64_t end, uint64_t nthreads, Fn fn) {
   std::vector<std::thread> threads;
   uint64_t n = end-begin;
   if (n<nthreads)
      nthreads = n;
   uint64_t perThread = n/nthreads;
   for (unsigned i=0; i<nthreads; i++) {
      threads.emplace_back([&,i]() {
         uint64_t b = (perThread*i) + begin;
         uint64_t e = (i==(nthreads-1)) ? end : ((b+perThread) + begin);
         fn(i, b, e);
      });
   }
   for (auto& t : threads)
      t.join();
}

double calculateMTPS(chrono::high_resolution_clock::time_point begin, chrono::high_resolution_clock::time_point end, u64 factor)
{
   double tps = ((factor * 1.0 / (chrono::duration_cast<chrono::microseconds>(end - begin).count() / 1000000.0)));
   return (tps / 1000000.0);
}
// -------------------------------------------------------------------------------------
int main(int argc, char** argv)
{
   gflags::SetUsageMessage("Leanstore Frontend");
   gflags::ParseCommandLineFlags(&argc, &argv, true);
   // -------------------------------------------------------------------------------------
   chrono::high_resolution_clock::time_point begin, end;
   // -------------------------------------------------------------------------------------
   // Always init with the maximum number of threads (FLAGS_worker_threads)
   LeanStore db;
   // auto& crm = db.getCRManager();
   LeanStoreAdapter<KVTable> table;
   table = LeanStoreAdapter<KVTable>(db, "YCSB");
   db.registerConfigEntry("ycsb_read_ratio", FLAGS_ycsb_read_ratio);
   db.registerConfigEntry("ycsb_threads", FLAGS_ycsb_threads);
   db.registerConfigEntry("ycsb_ops_per_tx", FLAGS_ycsb_ops_per_tx);
   // -------------------------------------------------------------------------------------
   leanstore::TX_ISOLATION_LEVEL isolation_level = leanstore::parseIsolationLevel(FLAGS_isolation_level);
   const TX_MODE tx_type = TX_MODE::OLTP;
   // -------------------------------------------------------------------------------------
   const u64 ycsb_tuple_count = (FLAGS_ycsb_tuple_count)
                                    ? FLAGS_ycsb_tuple_count
                                    : FLAGS_target_gib * 1024 * 1024 * 1024 * 1.0 / 2.0 / (sizeof(YCSBKey) + sizeof(YCSBPayload));
   // Insert values
   const u64 n = ycsb_tuple_count;
   // -------------------------------------------------------------------------------------

   cout << "Inserting " << ycsb_tuple_count << " values" << endl;
   begin = chrono::high_resolution_clock::now();

   // this is the old parallelize loop

   // comment: this is for insertion and therefore only one big loop without
   // switches... so no multiple schedule here

   // 1. it parallelizes the workload over the threads
   // 2. the threads (worker) work on them one by one -> no spikes here

   // what we want to change
   // 1. workload distribution is handled not by us but by osv
   // 2. the task creation returns instantly (because it just gets enqueued in the loop) -> how to handle load balancing

   /* utils::Parallelize::range(FLAGS_ycsb_insert_threads ? FLAGS_ycsb_insert_threads : FLAGS_worker_threads, n, [&](u64 t_i, u64 begin, u64 end) {
      crm.scheduleJobAsync(t_i, [&, begin, end]() {
         for (u64 i = begin; i < end; i++) {
            YCSBPayload payload;
            utils::RandomGenerator::getRandString(reinterpret_cast<u8*>(&payload), sizeof(YCSBPayload));
            YCSBKey key = i;
            cr::Worker::my().startTX(tx_type, leanstore::TX_ISOLATION_LEVEL::SNAPSHOT_ISOLATION);
            table.insert({key}, {payload});
            cr::Worker::my().commitTX();
         }
      });
   });

   crm.joinAll(); */

   // in the osv way we probably send one big task that would be get converted to a thread
   // and just wait until that is done
   // we will check that with a semphore

   // for now we wont do that at all because we do not use a lot of threads and therefore
   // the parallelization is not important -> it also would be interesting on how to work with this
   // parallelization in the future? should we build other abstractions for it? parallel for loop for osv tasks

   // FIXME: in the future this should be distributed to multiple threads
   // FIXME: WAL things are currently not used
      parallel_for(0, n, 2, [&](u64 thread_id, u64 begin, u64 end) {
         for (u64 i = begin; i < end; i++) {
            YCSBPayload payload;
            utils::RandomGenerator::getRandString(reinterpret_cast<u8*>(&payload), sizeof(YCSBPayload));
            YCSBKey key = i;
            // cr::Worker::my().startTX(tx_type, leanstore::TX_ISOLATION_LEVEL::SNAPSHOT_ISOLATION);
            table.insert({key}, {payload});
            // cr::Worker::my().commitTX();
         }
      });

   end = chrono::high_resolution_clock::now();
   cout << "time elapsed = " << (chrono::duration_cast<chrono::microseconds>(end - begin).count() / 1000000.0) << endl;
   cout << calculateMTPS(begin, end, n) << " M tps" << endl;
   // -------------------------------------------------------------------------------------
   const u64 written_pages = db.getBufferManager().consumedPages();
   const u64 mib = written_pages * PAGE_SIZE / 1024 / 1024;
   cout << "Inserted volume: (pages, MiB) = (" << written_pages << ", " << mib << ")" << endl;
   cout << "-------------------------------------------------------------------------------------" << endl;
   // -------------------------------------------------------------------------------------
   auto zipf_random = std::make_unique<utils::ScrambledZipfGenerator>(0, ycsb_tuple_count, FLAGS_zipf_factor);
   cout << setprecision(4);
   // -------------------------------------------------------------------------------------
   cout << "~Transactions" << endl;
   db.startProfilingThread();
   atomic<bool> keep_running = true;
   atomic<u64> running_threads_counter = 0;
   const u32 exec_threads = FLAGS_ycsb_threads ? FLAGS_ycsb_threads : FLAGS_worker_threads;

   // this is the loop we want to figure out on how to implement it in osv
   // problem is that in our case we just run them in order
   // now we are putting them in some runqueue and run them
   // -> this is just what is possible if we run it on full throttle but is not in any case realistic
   // TODO: how to translate it in leanstore?
   //          - for now we can just execute them in a loop with a nsleep in between

   if (FLAGS_is_linux) {
      for (u64 t_i = 0; t_i < exec_threads - ((FLAGS_ycsb_sleepy_thread) ? 1 : 0); t_i++) {
         while (keep_running) {
            jumpmuTry()
            {
               YCSBKey key;
               if (FLAGS_zipf_factor == 0) {
                  key = utils::RandomGenerator::getRandU64(0, ycsb_tuple_count);
               } else {
                  key = zipf_random->rand();
               }
               assert(key < ycsb_tuple_count);
               YCSBPayload result;
               auto before = readTSC();
               table.lookup1({key}, [&](const KVTable&) {});         // result = record.my_payload;
               leanstore::storage::BMC::global_bf->evictLastPage();  // to ignore the replacement strategy effect on MVCC experiment

               auto now = readTSC();
               auto timeDiff = tscDifferenceUs(now, before);
               WorkerCounters::myCounters().total_tx_time += timeDiff;
               WorkerCounters::myCounters().tx_latency_hist.increaseSlot(timeDiff);

               WorkerCounters::myCounters().tx++;
            }
            jumpmuCatch()
            {
               WorkerCounters::myCounters().tx_abort++;
            }
         }
      }
   } else {
      struct YCSBArgs {
         LeanStoreAdapter<KVTable>* table;
         YCSBKey key;
         u64 start;
      };

      size_t it = 0;

      while (keep_running) {
         YCSBKey key;
         if (FLAGS_zipf_factor == 0) {
            key = utils::RandomGenerator::getRandU64(0, ycsb_tuple_count);
         } else {
            key = zipf_random->rand();
         }
         assert(key < ycsb_tuple_count);
         YCSBPayload result;
         // cr::Worker::my().startTX(tx_type, isolation_level);
         // TODO args should be in some kind of pool but works for now
         YCSBArgs* a = new YCSBArgs{&table, key, readTSC()};
#ifdef OSV_ENQUEUE

         std::thread(
             [](void* args) {
                jumpmuTry()
                {
                   YCSBArgs* ycsb_args = (YCSBArgs*)args;

                   ycsb_args->table->lookup1({ycsb_args->key}, [&](const KVTable&) {});  // result = record.my_payload;
                   // leanstore::storage::BMC::global_bf->evictLastPage();  // to ignore the replacement strategy effect on MVCC experiment
                   auto now = readTSC();
                   // printf("finished2 on cpu: %u\n", sched_getcpu());
                   auto timeDiff = tscDifferenceUs(now, ycsb_args->start);
                   WorkerCounters::myCounters().total_tx_time += timeDiff;
                   WorkerCounters::myCounters().tx_latency_hist.increaseSlot(timeDiff);
                   WorkerCounters::myCounters().tx++;

                   printf("finished %u on cpu: %u in %u\n", ycsb_args->it, sched_getcpu(), timeDiff);

                   // delete ycsb_args;
                }
                jumpmuCatch()
                {
                   WorkerCounters::myCounters().tx_abort++;
                }
             },
             a)
             .detach();

#else
         if (!osv_task_enqueue(
                 [](void* args) {
                    jumpmuTry()
                    {
                       YCSBArgs* ycsb_args = (YCSBArgs*)args;

                       ycsb_args->table->lookup1({ycsb_args->key}, [&](const KVTable&) {});  // result = record.my_payload;
                       // leanstore::storage::BMC::global_bf->evictLastPage();  // to ignore the replacement strategy effect on MVCC experiment
                       auto now = readTSC();
                       // printf("finished2 on cpu: %u\n", sched_getcpu());
                       auto timeDiff = tscDifferenceUs(now, ycsb_args->start);
                       WorkerCounters::myCounters().total_tx_time += timeDiff;
                       WorkerCounters::myCounters().tx_latency_hist.increaseSlot(timeDiff);
                       WorkerCounters::myCounters().tx++;

                       // delete ycsb_args;
                    }
                    jumpmuCatch()
                    {
                       WorkerCounters::myCounters().tx_abort++;
                    }
                 },
                 a)) {
            std::cerr << "osv_task_enqueue failed" << std::endl;
            return EXIT_FAILURE;
         }
#endif

         usleep(60000);

         // cr::Worker::my().commitTX();

      }
   }
   // -------------------------------------------------------------------------------------

   // -------------------------------------------------------------------------------------

   cout << "-------------------------------------------------------------------------------------" << endl;
   return 0;
}
