#include "../shared/LeanStoreAdapter.hpp"
#include "../shared/Schema.hpp"
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
   if (FLAGS_tmp4) {
      // -------------------------------------------------------------------------------------
      std::ofstream csv;
      csv.open("zipf.csv", ios::trunc);
      csv.seekp(0, ios::end);
      csv << std::setprecision(2) << std::fixed;
      std::unordered_map<u64, u64> ht;
      auto zipf_random = std::make_unique<utils::ScrambledZipfGenerator>(0, ycsb_tuple_count, FLAGS_zipf_factor);
      for (u64 t_i = 0; t_i < (FLAGS_tmp4 ? FLAGS_tmp4 : 1e6); t_i++) {
         u64 key = zipf_random->rand();
         if (ht.find(key) == ht.end()) {
            ht[key] = 0;
         } else {
            ht[key]++;
         }
      }
      csv << "key,count" << endl;
      for (auto& [key, value] : ht) {
         csv << key << "," << value << endl;
      }
      cout << ht.size() << endl;
      return 0;
   }
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
   for (u64 i = 0; i < n; i++) {
      YCSBPayload payload;
      utils::RandomGenerator::getRandString(reinterpret_cast<u8*>(&payload), sizeof(YCSBPayload));
      YCSBKey key = i;
      // cr::Worker::my().startTX(tx_type, leanstore::TX_ISOLATION_LEVEL::SNAPSHOT_ISOLATION);
      table.insert({key}, {payload});
      WorkerCounters::myCounters().tx++;
      // cr::Worker::my().commitTX();
   }

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

#ifdef OSV
   for (u64 t_i = 0; t_i < exec_threads - ((FLAGS_ycsb_sleepy_thread) ? 1 : 0); t_i++) {
      crm.scheduleJobAsync(t_i, [&]() {
         running_threads_counter++;
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
               cr::Worker::my().startTX(tx_type, isolation_level);
               for (u64 op_i = 0; op_i < FLAGS_ycsb_ops_per_tx; op_i++) {
                  if (FLAGS_ycsb_read_ratio == 100 || utils::RandomGenerator::getRandU64(0, 100) < FLAGS_ycsb_read_ratio) {
                     table.lookup1({key}, [&](const KVTable&) {});         // result = record.my_payload;
                     leanstore::storage::BMC::global_bf->evictLastPage();  // to ignore the replacement strategy effect on MVCC experiment
                  } else {
                     UpdateDescriptorGenerator1(tabular_update_descriptor, KVTable, my_payload);
                     utils::RandomGenerator::getRandString(reinterpret_cast<u8*>(&result), sizeof(YCSBPayload));
                     // -------------------------------------------------------------------------------------
                     table.update1({key}, [&](KVTable& rec) { rec.my_payload = result; }, tabular_update_descriptor);
                     leanstore::storage::BMC::global_bf->evictLastPage();  // to ignore the replacement strategy effect on MVCC experiment
                  }
               }
               cr::Worker::my().commitTX();
               WorkerCounters::myCounters().tx++;
            }
            jumpmuCatch()
            {
               WorkerCounters::myCounters().tx_abort++;
            }
         }
         running_threads_counter--;
      });
   }

#else
   struct YCSBArgs {
      LeanStoreAdapter<KVTable>* table;
      YCSBKey key;
   };

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
         YCSBArgs* a = new YCSBArgs{&table, key};
         // cr::Worker::my().startTX(tx_type, isolation_level);
         for (u64 op_i = 0; op_i < FLAGS_ycsb_ops_per_tx; op_i++) {
            osv_task_enqueue(
                [](void* args) {
                   YCSBArgs* ycsb_args = (YCSBArgs*)args;

                   ycsb_args->table->lookup1({ycsb_args->key}, [&](const KVTable&) {});  // result = record.my_payload;
                   leanstore::storage::BMC::global_bf->evictLastPage();  // to ignore the replacement strategy effect on MVCC experiment
                }, a);
                usleep(10000); 
         }
         // cr::Worker::my().commitTX();
         WorkerCounters::myCounters().tx++;
      }
      jumpmuCatch()
      {
         WorkerCounters::myCounters().tx_abort++;
      }
   }
#endif
   // -------------------------------------------------------------------------------------

   // -------------------------------------------------------------------------------------

   cout << "-------------------------------------------------------------------------------------" << endl;
   return 0;
}
