#include "Worker.hpp"

#include "leanstore/Config.hpp"
#include "leanstore/profiling/counters/CRCounters.hpp"
#include "leanstore/profiling/counters/WorkerCounters.hpp"
#include "leanstore/storage/buffer-manager/DTRegistry.hpp"
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
#include <stdio.h>

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
// -------------------------------------------------------------------------------------
namespace leanstore
{
namespace cr
{
// -------------------------------------------------------------------------------------
thread_local Worker* Worker::tls_ptr = nullptr;
atomic<u64> Worker::global_snapshot_clock = 0;
// -------------------------------------------------------------------------------------
Worker::Worker(u64 worker_id, Worker** all_workers, u64 workers_count, s32 fd)
    : worker_id(worker_id), all_workers(all_workers), workers_count(workers_count), ssd_fd(fd)
{
   Worker::tls_ptr = this;
   CRCounters::myCounters().worker_id = worker_id;
   std::memset(wal_buffer, 0, WORKER_WAL_SIZE);
   my_snapshot = make_unique<atomic<u64>[]>(workers_count);
   sorted_active_workers = make_unique<u64[]>(workers_count);
}
Worker::~Worker()
{
   // static std::mutex m;
   // std::unique_lock guard(m);
   // cout << "WorkerID = " << worker_id << endl;
   // cout << worker_id << " high = " << high_water_mark << " - low = " << lower_water_mark << " todo# " << todo_list.size() << endl;
}
// -------------------------------------------------------------------------------------
u32 Worker::walFreeSpace()
{
   // A , B , C : a - b + c % c
   auto ww_cursor = wal_ww_cursor.load();
   if (ww_cursor == wal_wt_cursor) {
      return WORKER_WAL_SIZE;
   } else if (ww_cursor < wal_wt_cursor) {
      return ww_cursor + (WORKER_WAL_SIZE - wal_wt_cursor);
   } else {
      return ww_cursor - wal_wt_cursor;
   }
}
// -------------------------------------------------------------------------------------
u32 Worker::walContiguousFreeSpace()
{
   return WORKER_WAL_SIZE - wal_wt_cursor;
}
// -------------------------------------------------------------------------------------
void Worker::walEnsureEnoughSpace(u32 requested_size)
{
   if (FLAGS_wal) {
      // Spin until we have enough space
      while (walFreeSpace() < (requested_size + CR_ENTRY_SIZE)) {
      }
      if (walContiguousFreeSpace() < (requested_size + CR_ENTRY_SIZE)) {  // always keep place for CR entry
         WALMetaEntry& entry = reserveWALMetaEntry();
         entry.type = WALEntry::TYPE::CARRIAGE_RETURN;
         entry.size = WORKER_WAL_SIZE - wal_wt_cursor;
         DEBUG_BLOCK() { entry.computeCRC(); }
         // -------------------------------------------------------------------------------------
         invalidateEntriesUntil(WORKER_WAL_SIZE);
         wal_wt_cursor.store(0, std::memory_order_release);
         wal_next_to_clean = 0;
         wal_buffer_round++;  // Carriage Return
      }
   }
}
// -------------------------------------------------------------------------------------
void Worker::invalidateEntriesUntil(u64 until)
{
   if (wal_buffer_round > 0) {
      constexpr u64 INVALIDATE_LSN = std::numeric_limits<u64>::max();
      assert(wal_next_to_clean >= wal_wt_cursor);
      assert(wal_next_to_clean <= WORKER_WAL_SIZE);
      if (wal_next_to_clean < until) {
         u64 offset = wal_next_to_clean;
         while (offset < until) {
            auto entry = reinterpret_cast<WALEntry*>(wal_buffer + offset);
            DEBUG_BLOCK()
            {
               assert(offset + entry->size <= WORKER_WAL_SIZE);
               if (entry->type != WALEntry::TYPE::CARRIAGE_RETURN) {
                  entry->checkCRC();
               }
               assert(entry->lsn < INVALIDATE_LSN);
            }
            entry->lsn.store(INVALIDATE_LSN, std::memory_order_release);
            offset += entry->size;
         }
         wal_next_to_clean = offset;
      }
   }
}
// -------------------------------------------------------------------------------------
WALMetaEntry& Worker::reserveWALMetaEntry()
{
   walEnsureEnoughSpace(sizeof(WALMetaEntry));
   active_mt_entry = reinterpret_cast<WALMetaEntry*>(wal_buffer + wal_wt_cursor);
   invalidateEntriesUntil(wal_wt_cursor + sizeof(WALMetaEntry));
   active_mt_entry->lsn.store(wal_lsn_counter++, std::memory_order_release);
   active_mt_entry->size = sizeof(WALMetaEntry);
   return *active_mt_entry;
}
// -------------------------------------------------------------------------------------
void Worker::submitWALMetaEntry()
{
   DEBUG_BLOCK() { active_mt_entry->computeCRC(); }
   const u64 next_wal_wt_cursor = wal_wt_cursor + sizeof(WALMetaEntry);
   wal_wt_cursor.store(next_wal_wt_cursor, std::memory_order_release);
}
// -------------------------------------------------------------------------------------
void Worker::submitDTEntry(u64 total_size)
{
   DEBUG_BLOCK() { active_dt_entry->computeCRC(); }
   std::unique_lock<std::mutex> g(worker_group_commiter_mutex);
   const u64 next_wt_cursor = wal_wt_cursor + total_size;
   wal_wt_cursor.store(next_wt_cursor, std::memory_order_release);
   wal_max_gsn.store(clock_gsn, std::memory_order_release);
}
// -------------------------------------------------------------------------------------
void Worker::refreshSnapshot()
{
   tmp = std::numeric_limits<u64>::max();
   my_snapshot_order = global_snapshot_clock.fetch_add(WORKERS_INCREMENT) | worker_id;
   for (u64 w = 0; w < workers_count; w++) {
      my_snapshot[w].store(all_workers[w]->high_water_mark, std::memory_order_release);
      tmp = std::min<u64>(tmp, all_workers[w]->my_snapshot_order);
      //      sorted_active_workers[w] = all_workers[w]->my_snapshot_order;
   }
   tmp &= WORKERS_MASK;
   // TODO: Optimize
   // std::sort(sorted_active_workers.get(), sorted_active_workers.get() + workers_count, std::greater<int>());
   // for (u64 w = 0; w < workers_count; w++) {
   //    sorted_active_workers[w] &= WORKERS_MASK;
   // }
}
// -------------------------------------------------------------------------------------
void Worker::startTX()
{
   if (FLAGS_wal) {
      current_tx_wal_start = wal_wt_cursor;
      WALMetaEntry& entry = reserveWALMetaEntry();
      entry.type = WALEntry::TYPE::TX_START;
      submitWALMetaEntry();
      assert(active_tx.state != Transaction::STATE::STARTED);
      active_tx.state = Transaction::STATE::STARTED;
      active_tx.min_gsn = clock_gsn;
      if (FLAGS_si) {
         if (FLAGS_si_refresh_rate == 0 || active_tx.tts % FLAGS_si_refresh_rate == 0) {
            refreshSnapshot();
         }
         active_tx.tts = high_water_mark;
         if (FLAGS_todo && todo_list.size()) {  // Cleanup
            while (todo_list.size()) {
               auto& todo = todo_list.front();
               if (isVisibleForAll(worker_id, todo.tts)) {
                  leanstore::storage::DTRegistry::global_dt_registry.todo(todo.dt_id, todo.entry.get(), todo.tts);
                  todo_list.pop();
               } else {
                  break;
               }
            }
         }
      }
   }
}
// -------------------------------------------------------------------------------------
void Worker::commitTX()
{
   if (FLAGS_wal) {
      assert(active_tx.state == Transaction::STATE::STARTED);
      // -------------------------------------------------------------------------------------
      WALMetaEntry& entry = reserveWALMetaEntry();
      entry.type = WALEntry::TYPE::TX_COMMIT;
      submitWALMetaEntry();
      // -------------------------------------------------------------------------------------
      active_tx.max_gsn = clock_gsn;
      active_tx.state = Transaction::STATE::READY_TO_COMMIT;
      if (FLAGS_si) {
         high_water_mark.store(active_tx.tts + 1, std::memory_order_release);
      }
      {
         std::unique_lock<std::mutex> g(worker_group_commiter_mutex);
         ready_to_commit_queue.push_back(active_tx);
      }
   }
}
// -------------------------------------------------------------------------------------
void Worker::abortTX()
{
   if (FLAGS_wal) {
      ensure(active_tx.state == Transaction::STATE::STARTED);
      iterateOverCurrentTXEntries([&](const WALEntry& entry) {
         const u64 tts = active_tx.tts;
         if (entry.type == WALEntry::TYPE::DT_SPECIFIC) {
            const auto& dt_entry = *reinterpret_cast<const WALDTEntry*>(&entry);
            leanstore::storage::DTRegistry::global_dt_registry.undo(dt_entry.dt_id, dt_entry.payload, tts);
         }
      });
      // -------------------------------------------------------------------------------------
      WALMetaEntry& entry = reserveWALMetaEntry();
      entry.type = WALEntry::TYPE::TX_ABORT;
      submitWALMetaEntry();
      active_tx.state = Transaction::STATE::ABORTED;
   }
   jumpmu::jump();
}
// -------------------------------------------------------------------------------------
bool Worker::isVisibleForIt(u8 whom_worker_id, u8 what_worker_id, u64 tts)
{
   return what_worker_id == whom_worker_id || (all_workers[whom_worker_id]->my_snapshot[what_worker_id] > tts);
}
// -------------------------------------------------------------------------------------
bool Worker::isVisibleForMe(u8 other_worker_id, u64 tts)
{
   return worker_id == other_worker_id || my_snapshot[other_worker_id] > tts;
}
// -------------------------------------------------------------------------------------
bool Worker::isVisibleForMe(u64 wtts)
{
   const u64 other_worker_id = wtts % workers_count;
   const u64 tts = wtts & ~(255ull << 56);
   return isVisibleForMe(other_worker_id, tts);
}
// -------------------------------------------------------------------------------------
u64 Worker::getLowerWaterMark(const u8 other_worker_id)
{
   //  return all_workers[tmp]->my_snapshot[other_worker_id];
   // u64 min = std::numeric_limits<u64>::max();
   // for (u64 w = 0; w < workers_count; w++) {
   //    min = std::min<u64>(min, all_workers[w]->my_snapshot_order);
   // }
   // const u64 o_w_id = min & WORKERS_MASK;
   // return all_workers[o_w_id]->my_snapshot[other_worker_id];
   u64 min = std::numeric_limits<u64>::max();
   for (u64 w = 0; w < workers_count; w++) {
      min = std::min<u64>(min, all_workers[w]->my_snapshot[other_worker_id]);
   }
   return min;
   const u64 oldest_worker_id = sorted_active_workers[workers_count - 1];
   return all_workers[oldest_worker_id]->my_snapshot[other_worker_id];  // > primary_version->tts;
}
// -------------------------------------------------------------------------------------
bool Worker::isVisibleForAll(u8 other_worker_id, u64 tts)
{
   return getLowerWaterMark(other_worker_id) > tts;
}
// -------------------------------------------------------------------------------------
// Called by worker, so concurrent writes on the buffer
void Worker::iterateOverCurrentTXEntries(std::function<void(const WALEntry& entry)> callback)
{
   u64 cursor = current_tx_wal_start;
   while (cursor != wal_wt_cursor) {
      const WALEntry& entry = *reinterpret_cast<WALEntry*>(wal_buffer + cursor);
      DEBUG_BLOCK()
      {
         if (entry.type != WALEntry::TYPE::CARRIAGE_RETURN)
            entry.checkCRC();
      }
      if (entry.type == WALEntry::TYPE::CARRIAGE_RETURN) {
         cursor = 0;
      } else {
         callback(entry);
         cursor += entry.size;
      }
   }
}
// -------------------------------------------------------------------------------------
WALChunk::Slot Worker::WALFinder::getJumpPoint(LID lsn)
{
   std::unique_lock guard(m);
   // -------------------------------------------------------------------------------------
   if (ht.size() == 0) {
      return {0, 0};
   } else {
      auto iter = ht.lower_bound(lsn);
      if (iter != ht.end() && iter->first == lsn) {
         return iter->second;
      } else {
         iter = std::prev(iter);
         return iter->second;
      }
   }
}
// -------------------------------------------------------------------------------------
void Worker::WALFinder::insertJumpPoint(LID LSN, WALChunk::Slot slot)
{
   std::unique_lock guard(m);
   ht[LSN] = slot;
}
// -------------------------------------------------------------------------------------
Worker::WALFinder::~WALFinder() {}
// -------------------------------------------------------------------------------------
void Worker::getWALDTEntryPayload(u8 worker_id, LID lsn, u32 in_memory_offset, std::function<void(u8*)> callback)
{
   all_workers[worker_id]->getWALEntry(lsn, in_memory_offset, [&](WALEntry* entry) { callback(reinterpret_cast<WALDTEntry*>(entry)->payload); });
}
// -------------------------------------------------------------------------------------
void Worker::getWALEntry(u8 worker_id, LID lsn, u32 in_memory_offset, std::function<void(WALEntry*)> callback)
{
   all_workers[worker_id]->getWALEntry(lsn, in_memory_offset, callback);
}
// -------------------------------------------------------------------------------------
void Worker::getWALEntry(LID lsn, u32 in_memory_offset, std::function<void(WALEntry*)> callback)
{
   {
      // 1- Optimistically locate the entry
      auto dt_entry = reinterpret_cast<WALEntry*>(wal_buffer + in_memory_offset);
      const u16 dt_size = dt_entry->size;
      if (dt_entry->lsn != lsn) {
         goto outofmemory;
      }
      u8 log[dt_size];
      std::memcpy(log, wal_buffer + in_memory_offset, dt_size);
      if (dt_entry->lsn != lsn) {
         goto outofmemory;
      }
      auto entry = reinterpret_cast<WALEntry*>(log);
      assert(entry->lsn == lsn);
      DEBUG_BLOCK() { entry->checkCRC(); }
      callback(entry);
      COUNTERS_BLOCK() { WorkerCounters::myCounters().wal_buffer_hit++; }
      return;
   }
outofmemory : {
   COUNTERS_BLOCK() { WorkerCounters::myCounters().wal_buffer_miss++; }
   // 2- Read from SSD, accelerate using getLowerBound
   const auto slot = wal_finder.getJumpPoint(lsn);
   if (slot.offset == 0) {
      goto outofmemory;
   }
   const u64 lower_bound = slot.offset;
   const u64 lower_bound_aligned = utils::downAlign(lower_bound);
   const u64 read_size_aligned = utils::upAlign(slot.length + lower_bound - lower_bound_aligned);
   auto log_chunk = static_cast<u8*>(std::aligned_alloc(512, read_size_aligned));
   const u64 ret = pread(ssd_fd, log_chunk, read_size_aligned, lower_bound_aligned);
   posix_check(ret >= read_size_aligned);
   WorkerCounters::myCounters().wal_read_bytes += read_size_aligned;
   // -------------------------------------------------------------------------------------
   u64 offset = 0;
   u8* ptr = log_chunk + lower_bound - lower_bound_aligned;
   auto entry = reinterpret_cast<WALEntry*>(ptr + offset);
   auto prev_entry = entry;
   while (true) {
      DEBUG_BLOCK() { entry->checkCRC(); }
      assert(entry->size > 0 && entry->lsn <= lsn);
      if (entry->lsn == lsn) {
         callback(entry);
         std::free(log_chunk);
         return;
      }
      if ((offset + entry->size) < slot.length) {
         offset += entry->size;
         prev_entry = entry;
         entry = reinterpret_cast<WALEntry*>(ptr + offset);
      } else {
         break;
      }
   }
   std::free(log_chunk);
   goto outofmemory;
   ensure(false);
   return;
}
}
// -------------------------------------------------------------------------------------
void Worker::addTODO(u8 worker_id, u64 tts, DTID dt_id, u64 size, std::function<void(u8* entry)> cb)
{
   todo_list.push({worker_id, tts, dt_id, std::make_unique<u8[]>(size)});
   cb(todo_list.back().entry.get());
}
// -------------------------------------------------------------------------------------
}  // namespace cr
}  // namespace leanstore
