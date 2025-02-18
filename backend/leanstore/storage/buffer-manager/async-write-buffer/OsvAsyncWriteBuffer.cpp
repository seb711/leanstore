#include "OsvAsyncWriteBuffer.hpp"
#include "leanstore/storage/buffer-manager/Tracing.hpp"

#include "Exceptions.hpp"
#include "leanstore/profiling/counters/WorkerCounters.hpp"
// -------------------------------------------------------------------------------------
#include "gflags/gflags.h"
// -------------------------------------------------------------------------------------
#include <signal.h>

#include <cstring>
#include <osv/nvme.hh>
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
   const uint32_t queue_size = 128; 
   
   // TODO: assert or guarantee that the batch_max_size is smaller than the io queue otherwise it will deadlock
   assert(batch_max_size <= queue_size); 

   // TODO: here we need to create a ioqueue of OSv
   auto ssds = osv_get_available_ssds(); 
   assert(ssds.size() > 0); 
   queue = osv_create_io_user_queue(ssds.front(), queue_size); 
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
   std::memcpy(&write_buffer[slot], &bf.page, page_size);
   void* write_buffer_slot_ptr = &write_buffer[slot];
   
   // TODO: here we need to call osv_nvme_io_queue_write
   auto ret = osv_nvme_nv_cmd_write(1, queue, &write_buffer[slot], page_size * pid, page_size, osv_nvme_callback, &iocbs[slot], 0);
   assert(ret == 0); 
}
// -------------------------------------------------------------------------------------
u64 OsvAsyncWriteBuffer::submit()
{
   // FIXME: yeah this call does nothing; this was taken from Libaio thing -> probably delete in the future
   return pending_requests; 
}  
// -------------------------------------------------------------------------------------
u64 OsvAsyncWriteBuffer::pollSync()
{
   // TODO: here we just need to process the queue items in a busy loop until we processed all bufferframes
   size_t tries = 0;
   size_t done_requests = 0; 
   while (pending_requests > 0 && tries++ < 200) {
      int ok = osv_nvme_qpair_process_completions(queue, 32);
      pending_requests -= ok;
      done_requests += ok; 
      usleep(5000); // FIXME: this is just for testing purposes we need to take this out at a later point
   }
   // printf("pending reqs: %u done_requests: %u\n", pending_requests, done_requests); 
   assert(pending_requests == 0); 
   return done_requests; 
}
}  // namespace storage
}  // namespace leanstore
   // -------------------------------------------------------------------------------------
