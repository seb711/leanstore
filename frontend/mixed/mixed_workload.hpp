#include "types.hpp"
#include "leanstore/LeanStore.hpp"

#define CYCLE 100000
#define PROBABLITY 5000
#define LONGRUNNING 1

// we need one easy look up
void lookupTblRnd(uint64_t w_id) {
   BytesPayload<120> result;  /// FIXME remove this check
   kv_store.lookup1({w_id}, [&](const item_t& item) { result = item.i_data; });
}
// we need one scan
void scanTbl(uint64_t w_id) {
   BytesPayload<120> result;  /// FIXME remove this check
   for (uint64_t t = 0 ; t < CYCLE; t++) {
      if ((w_id + t) >= 3355443) break; 
      kv_store.lookup1({w_id + t}, [&](const item_t& item) { result = item.i_data; });
   }
}

// was: [w_begin, w_end]
int tx(Integer w_id)
{
   // micro-optimized version of weighted distribution
   int rnd = leanstore::utils::RandomGenerator::getRand(0, PROBABLITY);
   if (rnd < (PROBABLITY - LONGRUNNING)) {
      lookupTblRnd(w_id);
      return 0;
   }
   scanTbl(w_id);
   return 1;
}
