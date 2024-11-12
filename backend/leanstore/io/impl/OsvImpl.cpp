#if LEANSTORE_INCLUDE_OSV
#include "OsvImpl.hpp"
// -------------------------------------------------------------------------------------
#include <cstring>
#include "leanstore/io/IoChannel.hpp"

namespace mean
{
// -------------------------------------------------------------------------------------
OsvEnv::~OsvEnv()
{
   //  yes idk what we will do here
}
// -------------------------------------------------------------------------------------
void OsvEnv::init(IoOptions options)
{
   // std::thread([] {  // hack so that the spdk pinning has no influence on leanstore threads
   //    SpdkEnvironment::init();
   // }).join();
   // controller = std::make_unique<NVMeMultiController>();
   // controller->connect(options.path);
   // controller->allocateQPairs();
   // int qs = controller->qpairSize();
   // for (int i = 0; i < qs; i++) {
   //   channels.push_back(std::make_unique<SpdkChannel>(options, *controller, i));
   // }

   // what needs to happen here 
   // 1. here we need to get the queuepairs of our nvme device
   // 2. build OsvChannels out of them 
}

OsvChannel& OsvEnv::getIoChannel(int channel)
{
   ensure(channels.size() > 0, "don't forget to initizalize the io env first");
   ensure(channel < (int)channels.size(), "There are only " + std::to_string(channels.size()) + " channels available.");
   // std::cout << "getChannel: " << channel << std::endl << std::flush;
   return *channels.at(channel);
}

int OsvEnv::deviceCount()
{
   return controller->deviceCount();
}

int OsvEnv::channelCount()
{
   return controller->qpairSize();
}

u64 OsvEnv::storageSize()
{
   return controller->nsSize();
}

void* OsvEnv::allocIoMemory(size_t size, size_t align)
{
   // return SpdkEnvironment::dma_malloc(size, align);

   // TODO: 
   // I think we can just use normal memory allocation here
   // should be pretty easy work 
   void* mem; 
   return mem; 
}

void* OsvEnv::allocIoMemoryChecked(size_t size, size_t align)
{
   // auto mem = SpdkEnvironment::dma_malloc(size, align);
   // TODO: same as allocIoMemory -> should also be straightforward
   void* mem; 
   null_check(mem, "Memory allocation failed");
   return mem;
}
void OsvEnv::freeIoMemory(void* ptr, [[maybe_unused]]size_t size)
{
   // SpdkEnvironment::dma_free(ptr);
   // TODO: just deallocate the memory
}

DeviceInformation OsvEnv::getDeviceInfo() {
   DeviceInformation d;
   d.devices.resize(deviceCount());
   // for (unsigned int i = 0; i < controller->controller.size(); i++) {
   //    d.devices[i].id = i;
   //    d.devices[i].name = controller->controller[i].pciefile;
   // }
   // TODO: first check if we really need that function
   // then just do something i guess not too important for now

   return d;
}
// -------------------------------------------------------------------------------------
// Channel 
// -------------------------------------------------------------------------------------
OsvChannel::OsvChannel(IoOptions ioOptions, OsvNVMeController& controller, int queue) 
   : options(ioOptions), controller(controller), queue(queue), lbaSize(controller.nsLbaDataSize()), outstanding(controller.deviceCount())
{
   write_request_stack.reserve(ioOptions.iodepth);
   int c = controller.deviceCount();
   for (int i = 0; i < c; i++) {
      qpairs.emplace_back(controller.controller[i].qpairs[queue]);
      // nameSpaces.emplace_back(controller.controller[i].nameSpace);
      // TODO: check if we can just init to 1
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
         // req->impl.type = SpdkIoReqType::Read;
         break;
      case IoRequestType::Write:
         // req->impl.type = SpdkIoReqType::Write;
         break;
      default:
         throw std::logic_error("IoRequestType not supported" + std::to_string((int)req->base.type));
   }
   // TODO: use the things from the microbenchmark 
   req->impl.buf = req->base.buffer();
   req->impl.lba = req->base.offset / lbaSize;
   req->impl.lba_count = req->base.len / lbaSize;
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