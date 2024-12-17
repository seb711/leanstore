#if LEANSTORE_INCLUDE_OSV
#include "OsvImpl.hpp"
// -------------------------------------------------------------------------------------
#include <cstring>
#include "leanstore/io/IoChannel.hpp"
#include <cstdlib>
#include <algorithm>
#include <sys/mman.h>

namespace mean
{
// -------------------------------------------------------------------------------------
OsvEnv::~OsvEnv()
{
   controller.reset(nullptr);
   //  yes idk what we will do here
   std::cout << "SpdkEnv deinit" << std::endl;
   OsvEnvironment::deinit();
}
// -------------------------------------------------------------------------------------
void OsvEnv::init(IoOptions options)
{
   // std::thread([] {  // hack so that the spdk pinning has no influence on leanstore threads
   //    SpdkEnvironment::init();
   // }).join();
   OsvEnvironment::init();
   controller = std::make_unique<NVMeMultiController>();
   controller->connect("test"); // path is currently not used -> just use the basic osv
   controller->allocateQPairs();
   int qs = controller->qpairSize();
   for (int i = 0; i < qs; i++) {
     channels.push_back(std::make_unique<OsvChannel>(options, *controller, i));
   }
}

OsvChannel& OsvEnv::getIoChannel(int channel)
{
   ensure(channels.size() > 0, "don't forget to initizalize the io env first");
   ensure(channel < (int)channels.size(), "There are only " + std::to_string(channels.size()) + " channels available.");
   // std::cout << "getChannel: " << channel << std::endl << std::flush;
   return *channels.at(channel);
}

int OsvEnv::channelCount()
{
   return controller->qpairSize();
}

u64 OsvEnv::storageSize()
{
   return controller->nsSize();
}

#define MAP_UNINITIALIZED 0x4000000

void* OsvEnv::allocIoMemory(size_t size, size_t align)
{
   /*char *buffer = nullptr;
   int t = posix_memalign((void **)&buffer, std::max(align, (size_t) 4096), size);
   memset(buffer, 0x0, size);
   std::cout << "allocate " << size << std::endl; 
   // char *buffer = nullptr;
   // int t = posix_memalign((void **)&buffer, std::max(align, (size_t) 4096), size);*/   
   void* buffer = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_UNINITIALIZED, -1, 0);

   null_check(buffer, "Memory allocation failed");   
   
   madvise(buffer, size, MADV_NOHUGEPAGE); 

   memset(buffer, 0x0, size); 
   return buffer;
}

void* OsvEnv::allocIoMemoryChecked(size_t size, size_t align)
{
    /*char *buffer = nullptr;
   int t = posix_memalign((void **)&buffer, std::max(align, (size_t) 4096), size);
   memset(buffer, 0x0, size);*/
  void* buffer = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

   assert(buffer != MAP_FAILED); 
   madvise(buffer, size, MADV_NOHUGEPAGE); 
   // TODO: same as allocIoMemory -> should also be straightforward
   null_check(buffer, "Memory allocation failed");
   return buffer;
}

void OsvEnv::freeIoMemory(void* ptr, [[maybe_unused]]size_t size)
{
   std::free(ptr);
}

int OsvEnv::deviceCount() {
   return OsvEnvironment::available_ssd_ids.size(); 
}

// -------------------------------------------------------------------------------------
// Channel 
// -------------------------------------------------------------------------------------
OsvChannel::OsvChannel(IoOptions ioOptions, NVMeMultiController& controller, int queue) 
   : options(ioOptions), controller(controller), queue(queue), lbaSize(controller.nsLbaDataSize()), outstanding(controller.deviceCount())
{
   write_request_stack.reserve(ioOptions.iodepth);
    int c = controller.deviceCount();
   for (int i = 0; i < c; i++) {
      qpairs.emplace_back(controller.controller[i].qpairs[queue]);
   }
}

OsvChannel::~OsvChannel()
{
}

void OsvChannel::prepare_request(RaidRequest<OsvIoReq>* req, OsvIoReqCallback osvCb)
{
    // TODO: add the requests to osv and then just use them here
   // base
   switch (req->base.type) {
      case IoRequestType::Read:
         req->impl.type = OsvIoReqType::Read;
         break;
      case IoRequestType::Write:
         req->impl.type = OsvIoReqType::Write;
         break;
      default:
         throw std::logic_error("IoRequestType not supported" + std::to_string((int)req->base.type));
   }
   // TODO: use the things from the microbenchmark 
   req->impl.buf = req->base.buffer();
   req->impl.lba = req->base.offset; // FIXME this is the storage::PAGE_SIZE
   req->impl.lba_count = req->base.len;

   // req->base.print(std::cout); 
   
   req->impl.callback = osvCb;
}

void OsvChannel::_push(RaidRequest<OsvIoReq>* req)
{
   req->impl.this_ptr = this;
   prepare_request(req, [](OsvIoReq* io_req) {
         auto req = reinterpret_cast<RaidRequest<OsvIoReq>*>(io_req);

         req->base.innerCallback.callback(&req->base);
   });
   write_request_stack.push_back(req);
}

void OsvChannel::_printSpecializedCounters(std::ostream& ss)
{
   ss << "osv: ";
}

}
#endif