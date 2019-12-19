#pragma once
#include "BufferFrame.hpp"
#include "DTRegistry.hpp"
#include "FreeList.hpp"
#include "PartitionTable.hpp"
#include "Swip.hpp"
#include "Units.hpp"
// -------------------------------------------------------------------------------------
#include "PerfEvent.hpp"
// -------------------------------------------------------------------------------------
#include <libaio.h>
#include <sys/mman.h>
#include <cstring>
#include <list>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
// -------------------------------------------------------------------------------------
namespace leanstore
{
class LeanStore;
namespace buffermanager
{
// -------------------------------------------------------------------------------------
/*
 * Swizzle a page:
 * 1- bf_s_lock global bf_s_lock
 * 2- if it is in cooling stage:
 *    a- yes: bf_s_lock write (spin till you can), remove from stage, swizzle in
 *    b- no: set the state to IO, increment the counter, hold the mutex, p_read
 * 3- if it is in IOFlight:
 *    a- increment counter,
 */
class BufferManager
{
 private:
  friend class leanstore::LeanStore;
  // -------------------------------------------------------------------------------------
  BufferFrame* bfs;
  // -------------------------------------------------------------------------------------
  int ssd_fd;
  // -------------------------------------------------------------------------------------
  // Free  Pages
  const u8 safety_pages = 10;  // we reserve these extra pages to prevent segfaults
  u64 dram_pool_size;          // total number of dram buffer frames
  FreeList dram_free_list;
  atomic<u64> ssd_used_pages_counter = 0;   // used as a hack for pid generation
  atomic<u64> ssd_freed_pages_counter = 0;  // used to track how many pages did we really allocate
  // -------------------------------------------------------------------------------------
  // -------------------------------------------------------------------------------------
  // For cooling and inflight io
  u64 partitions_count;
  u64 partitions_mask;
  PartitionTable* partitions;
  atomic<u64> cooling_bfs_counter = 0;

 private:
  // -------------------------------------------------------------------------------------
  // Threads managements
  void pageProviderThread(u64 p_begin, u64 p_end);  // [p_begin, p_end)
  atomic<u64> bg_threads_counter = 0;
  atomic<bool> bg_threads_keep_running = true;
  // -------------------------------------------------------------------------------------
  // Datastructures managements
  DTRegistry dt_registry;
  // -------------------------------------------------------------------------------------
  // Misc
  BufferFrame& randomBufferFrame();
  PartitionTable& getPartition(PID);
  u64 getPartitionID(PID);
 public:
  // -------------------------------------------------------------------------------------
  BufferManager();
  ~BufferManager();
  // -------------------------------------------------------------------------------------
  BufferFrame& allocatePage();
  BufferFrame& resolveSwip(ReadGuard& swip_guard, Swip<BufferFrame>& swip_value);
  void reclaimPage(BufferFrame& bf);
  void reclaimBufferFrame(BufferFrame& bf);
  // -------------------------------------------------------------------------------------
  void flushDropAllPages();
  void stopBackgroundThreads();
  /*
   * Life cycle of a fix:
   * 1- Check if the pid is swizzled, if yes then store the BufferFrame address
   * temporarily 2- if not, then posix_check if it exists in cooling stage
   * queue, yes? remove it from the queue and return the buffer frame 3- in
   * anycase, posix_check if the threshold is exceeded, yes ? unswizzle a random
   * BufferFrame (or its children if needed) then add it to the cooling stage.
   */
  // -------------------------------------------------------------------------------------
  void readPageSync(PID pid, u8* destination);
  void fDataSync();
  // -------------------------------------------------------------------------------------
  void registerDatastructureType(DTType type, DTRegistry::DTMeta dt_meta);
  DTID registerDatastructureInstance(DTType type, void* root_object, string name);
  // -------------------------------------------------------------------------------------
  void clearSSD();
  void restore();
  void persist();
  // -------------------------------------------------------------------------------------
  u64 consumedPages();
};
// -------------------------------------------------------------------------------------
class BMC
{
 public:
  static BufferManager* global_bf;
};
}  // namespace buffermanager
}  // namespace leanstore
// -------------------------------------------------------------------------------------
