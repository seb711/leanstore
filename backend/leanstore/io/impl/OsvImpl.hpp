#pragma once
#include "./Osv.hpp"
// -------------------------------------------------------------------------------------
#include "../IoAbstraction.hpp"
#include "../RequestStack.hpp"
// -------------------------------------------------------------------------------------
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>
// -------------------------------------------------------------------------------------
namespace mean
{
// -------------------------------------------------------------------------------------
class OsvChannel;
class OsvEnv
{
   std::unique_ptr<NVMeMultiController> controller;
   std::vector<std::unique_ptr<OsvChannel>> channels;

  public:
   ~OsvEnv();
   void init(IoOptions options);
   OsvChannel& getIoChannel(int channel);
   void* allocIoMemory(size_t size, size_t align);
   void* allocIoMemoryChecked(size_t size, size_t align);
   void freeIoMemory(void* ptr, size_t size);
   int deviceCount();
   int channelCount();
   u64 storageSize();
   DeviceInformation getDeviceInfo();
};
// -------------------------------------------------------------------------------------
class OsvChannel
{
   IoOptions options;
   NVMeMultiController& controller;
   int queue;
   const int lbaSize;
   // -------------------------------------------------------------------------------------
   void prepare_request(RaidRequest<OsvIoReq>* req, OsvIoReqCallback spdkCb);
   // -------------------------------------------------------------------------------------
  public:
   OsvChannel(IoOptions options, NVMeMultiController& controller, int queue);
   ~OsvChannel();
   // -------------------------------------------------------------------------------------
   std::atomic<RaidRequest<OsvIoReq>*> write_request_head = {nullptr};
   std::atomic<uint64_t> submitable = {0}; 
   std::vector<int> outstanding;
   std::vector<void*> qpairs;

   void _push(RaidRequest<OsvIoReq>* req);
   void pushBlocking(IoRequestType type, char* data, s64 addr, u64 len, bool write_back) { throw std::logic_error("not implemented"); }
   int _submit()
   {
      int submitted = 0;

      while (write_request_head.load() != nullptr) {
         RaidRequest<OsvIoReq>* req = write_request_head.load(); 

         int ret = OsvEnvironment::osv_req_type_fun_lookup[(int)req->impl.type](1, qpairs[req->base.device], req->impl.buf, req->impl.lba,
                                                                                 req->impl.lba_count, NVMeController::completion, req, 0);

         if (ret == 0) {
            while (req && !write_request_head.compare_exchange_weak(req, req->impl.next)) {}
            outstanding[req->base.device]++;
            submitted++;
            submitable--; 
            continue;
         } else {
            break; 
         }

         break;
      }

      return submitted;
   }

   int _poll(int)
   {
      int done = 0;

      for (unsigned int i = 0; i < qpairs.size(); i++) {
         int ok = OsvEnvironment::qpair_process_completions(qpairs[i], 128);
         outstanding[i] -= ok;
         // ensure(ok >= 0, "ok >= 0");
         done += ok;
      }
      // printf("completed %i ios\n", done);
      // }
      assert(done >= 0);
      return done;
   }
   void _printSpecializedCounters(std::ostream& ss);
};
}  // namespace mean
