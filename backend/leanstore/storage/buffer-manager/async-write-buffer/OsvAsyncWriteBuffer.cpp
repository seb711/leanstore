#include "OsvAsyncWriteBuffer.hpp"
#include "leanstore/storage/buffer-manager/Tracing.hpp"

#include "Exceptions.hpp"
#include "leanstore/profiling/counters/WorkerCounters.hpp"
// -------------------------------------------------------------------------------------
#include "gflags/gflags.h"
// -------------------------------------------------------------------------------------
#include <signal.h>

#include <cstring>
// -------------------------------------------------------------------------------------
DEFINE_uint32(insistence_limit, 1, "");
// -------------------------------------------------------------------------------------
namespace leanstore
{
namespace storage
{
// -------------------------------------------------------------------------------------
OsvAsyncWriteBuffer::OsvAsyncWriteBuffer(u64 page_size, u64 batch_max_size) : page_size(page_size), batch_max_size(batch_max_size)
{
   write_buffer = make_unique<BufferFrame::Page[]>(batch_max_size);
   iocbs = make_unique<CallbackArguments[]>(batch_max_size);
   // -------------------------------------------------------------------------------------
   
   // TODO: here we need to create a ioqueue of OSv
   // TODO: assert or guarantee that the batch_max_size is smaller than the io queue otherwise it will deadlock
}
// -------------------------------------------------------------------------------------
bool OsvAsyncWriteBuffer::full()
{
   if (pending_requests >= batch_max_size - 2) {
      return true;
   } else {
      return false;
   }
}
// -------------------------------------------------------------------------------------
void OsvAsyncWriteBuffer::add(BufferFrame& bf, std::function<void(BufferFrame&, u64, PID)> callback, PID pid)
{
   assert(!full());
   assert(u64(&bf.page) % 512 == 0);
   assert(pending_requests <= batch_max_size);
   COUNTERS_BLOCK() { WorkerCounters::myCounters().dt_page_writes[bf.page.dt_id]++; }
   // -------------------------------------------------------------------------------------
   PARANOID_BLOCK()
   {
      if (FLAGS_pid_tracing && !FLAGS_recycle_pages) {
         Tracing::mutex.lock();
         if (Tracing::ht.contains(pid)) {
            auto& entry = Tracing::ht[pid];
            ensure(std::get<0>(entry) == bf.page.dt_id);
         }
         Tracing::mutex.unlock();
      }
   }
   // -------------------------------------------------------------------------------------
   auto slot = pending_requests++;
   iocbs[slot].bf = &bf;
   iocbs[slot].callback = callback;
   iocbs[slot].pid = pid;
   bf.page.magic_debugging_number = pid;
   std::memcpy(&write_buffer[slot], bf.page, page_size);
   void* write_buffer_slot_ptr = &write_buffer[slot];
   
   // TODO: here we need to call osv_nvme_io_queue_write
}
// -------------------------------------------------------------------------------------
u64 OsvAsyncWriteBuffer::submit()
{
   // FIXME: yeah this call does nothing; this was taken from Libaio thing -> probably delete in the future
}
// -------------------------------------------------------------------------------------
u64 OsvAsyncWriteBuffer::pollSync()
{
   // TODO: here we just need to process the queue items in a busy loop until we processed all bufferframes
}
}  // namespace storage
}  // namespace leanstore
   // -------------------------------------------------------------------------------------
