#pragma once
#include "AsyncWriteBuffer.hpp"
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
#include <osv/nvme.hh>
// -------------------------------------------------------------------------------------
namespace leanstore
{
namespace storage
{
// -------------------------------------------------------------------------------------
class OsvAsyncWriteBuffer : public AsyncWriteBuffer
{
  private:
   struct CallbackArguments {
      std::function<void(BufferFrame&, u64, PID)> callback; 
      BufferFrame* bf;
      PID pid;
   };

   void* queue;
   u64 page_size, batch_max_size;
   u64 pending_requests = 0;

  public:
   osv_nvme_cmd_cb callback = [](void *ctx, const nvme_sq_entry_t* cpl)->void {
    CallbackArguments* args = (CallbackArguments*) ctx; 
    args->callback(*args->bf, (*args->bf).page.PLSN, args->pid); 
   }; 



   std::unique_ptr<BufferFrame::Page[]> write_buffer;
   std::unique_ptr<struct CallbackArguments[]> iocbs;
   // -------------------------------------------------------------------------------------
   // Debug
   // -------------------------------------------------------------------------------------
   OsvAsyncWriteBuffer(u64 page_size, u64 batch_max_size);
   // Caller takes care of sync
   bool full();
   virtual void add(BufferFrame& bf, std::function<void(BufferFrame&, u64, PID)> callback, PID pid) override;
   virtual u64 submit() override;
   virtual u64 pollSync() override;
};
// -------------------------------------------------------------------------------------
}  // namespace storage
}  // namespace leanstore
// -------------------------------------------------------------------------------------
