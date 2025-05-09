#include "leanstore/concurrency/osv/BlockingIoContext.hpp"
#include "OsvBackgroundThreadBase.hpp"
#include "leanstore/io/IoInterface.hpp"
#include "leanstore/storage/buffer-manager/BufferManager.hpp"

namespace mean
{
class OsvPageProvider : public OsvBackgroundThreadBase
{
  private:
  leanstore::storage::BufferManager* bf_ptr;
  u32 partition_id; 

    // will poll and submit
    unsigned getPriority() override; 

   // will poll and submit
   int process() override;

   // FIXME 
   u32 counter = 0; 

   public: 
   // -------------------------------------------------------------------------------------
   OsvPageProvider(leanstore::storage::BufferManager* bf_ptr, int pid);
   ~OsvPageProvider();
   // -------------------------------------------------------------------------------------
   OsvPageProvider(const OsvPageProvider&) = delete;
   OsvPageProvider(OsvPageProvider&&) = delete;
   OsvPageProvider& operator=(const OsvPageProvider&) = delete;
   OsvPageProvider& operator=(OsvPageProvider&&) = delete;
};
}  // namespace mean