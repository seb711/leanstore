#include "OsvSyncReader.hpp"

namespace leanstore
{
namespace storage
{
OsvSyncReader* OsvSyncReader::singleton_ = nullptr;

OsvSyncReader* OsvSyncReader::getInstance()
{
   if (singleton_ == nullptr) {
      singleton_ = new OsvSyncReader();
   }
   return singleton_;
}

OsvSyncReader::OsvSyncReader()
{
   // -------------------------------------------------------------------------------------
   const uint32_t queue_size = 128;

   // TODO: here we need to create a ioqueue of OSv
   auto ssds = osv_get_available_ssds();
   printf("OsvSyncReader using ssd %u\n", ssds.front()); 
   assert(ssds.size() > 0);
   queue = osv_create_io_user_queue(ssds.front(), queue_size);
}

const int OsvSyncReader::syncRead(u8* destination, size_t length, size_t offset)
{
   // first get an element from the pool
   LockFreeList<CVRead, 500>::Node* node = cv_pool.acquire();
   if (node == nullptr) {
      return 0;
   }

   // setup the io call with a callback that calls the notify all thing
   CVRead* read_cv = node->getObject();
   {
      std::unique_lock<std::mutex> lock(read_cv->mtx);
      read_cv->ready = false;

      // do the io call
      auto res = osv_nvme_nv_cmd_read(1, (void*)queue, destination, offset, length, OsvSyncReader::completion, (void*)read_cv, 0);
      assert(res == 0);

      read_cv->cv.wait(lock, [read_cv] { return read_cv->ready; });
   }

   // printf("lock should be unlocked now\n");
   cv_pool.release(node);

   return length;
}

int OsvSyncReader::pollQueues()
{
   return osv_nvme_qpair_process_completions(queue, 32);
}

}  // namespace storage
}  // namespace leanstore
   // -------------------------------------------------------------------------------------
