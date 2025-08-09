#include "types.hpp"
#include "leanstore/LeanStore.hpp"
#include <osv/leanstore_debug.hh>

#define CYCLE 1000000
#define PROBABLITY 5000
#define LONGRUNNING 1
#define MAX_ENTRIES 134217728

class Workload {
   private: 
      utils::ScrambledZipfGenerator zipf_random; 
      std::atomic<unsigned> current_idx;
      std::atomic<unsigned> highest_inserted;
      utils::MersenneTwister twister; 
      LeanStoreAdapter<item_t>& kv_store;
      std::atomic<uint64_t> last_scan; 


      void lookupTblRnd(uint64_t w_id) {
         uint64_t key = zipf_random.rand();
         BytesPayload<120> result;  /// FIXME remove this check
         kv_store.lookup1({w_id}, [&](const item_t& item) { result = item.i_data; });
      }
      // we need one easy look up
      void newIncrEntry() {
         BytesPayload<120> payload;
         utils::RandomGenerator::getRandString(reinterpret_cast<u8*>(&payload), sizeof(BytesPayload<120>));
         auto key = current_idx.fetch_add(1);
         kv_store.insert({key}, {payload});
         // BytesPayload<120> result;  /// FIXME remove this check
         // kv_store.lookup1({key}, [&](const item_t& item) { result = item.i_data; });

         if (highest_inserted.load() < key) {
            highest_inserted.store(key);
         }

      }

      void updateRnd() {
         uint64_t key = zipf_random.rand() % (current_idx.load(std::memory_order_relaxed) & 0xffff0000);
         BytesPayload<120> payload;
         utils::RandomGenerator::getRandString(reinterpret_cast<u8*>(&payload), sizeof(BytesPayload<120>));

         kv_store.update1({key}, [&](item_t& item) { item.i_data = payload; }, WALUpdate1(item_t, i_data));
      }

      // we need one scan
      void scanSeqTbl() {
         unsigned curr = highest_inserted.load(); 

         kv_store.scanDesc({highest_inserted}, [&] (const item_t::Key& key, const item_t& payload) {
            if (curr - key.i_id > CYCLE) return false; 
            return true; 
         }, [](){}); 
      }

   public: 

      Workload(LeanStoreAdapter<item_t>& kv_store) : zipf_random(0, MAX_ENTRIES, FLAGS_zipf_factor),  current_idx(0), highest_inserted(0), twister(), kv_store(kv_store), last_scan(mean::readTSC()) {}; 

      int tx() {

         if (mean::tscDifferenceMs(mean::readTSC(), last_scan) > 500) {
             last_scan.store(mean::readTSC()); 
             scanSeqTbl(); 
             return 1; 
         } else {
            int rnd = leanstore::utils::RandomGenerator::getRand(0, 100);

            if (rnd < 50) {
               newIncrEntry(); 
            } else {
               updateRnd(); 
            }
            return 0; 
         }
      }

      int insert() {
         if (current_idx.load() % 1000000 == 0) {
            std::cout << current_idx.load() << std::endl; 
         }
         newIncrEntry(); 
         return 0; 
      }
}; 