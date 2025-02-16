#pragma once
#include "AsyncWriteBuffer.hpp"
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
#include <libaio.h>
#include <functional>
#include <list>
#include <unordered_map>
// -------------------------------------------------------------------------------------
namespace leanstore
{
namespace storage
{
// -------------------------------------------------------------------------------------
class LibaioAsyncWriteBuffer : public AsyncWriteBuffer
{
  private:
   struct WriteCommand {
      BufferFrame* bf;
      std::function<void(BufferFrame&, u64, PID)> callback;
      PID pid;
   };
   io_context_t aio_context;
   int fd;
   u64 page_size, batch_max_size;
   u64 pending_requests = 0;

  public:
   std::unique_ptr<BufferFrame::Page[]> write_buffer;
   std::unique_ptr<WriteCommand[]> write_buffer_commands;
   std::unique_ptr<struct iocb[]> iocbs;
   std::unique_ptr<struct iocb*[]> iocbs_ptr;
   std::unique_ptr<struct io_event[]> events;
   // -------------------------------------------------------------------------------------
   // Debug
   // -------------------------------------------------------------------------------------
   LibaioAsyncWriteBuffer(int fd, u64 page_size, u64 batch_max_size);
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
