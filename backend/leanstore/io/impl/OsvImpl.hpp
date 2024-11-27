#pragma once
#if LEANSTORE_INCLUDE_OSV
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
   std::unique_ptr<NVMeController> controller;
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
   std::vector<RaidRequest<OsvIoReq>*> write_request_stack;

   IoOptions options;
   NVMeController& controller;
   int queue;
   const int lbaSize;
   nvme::io_user_queue_pair* qpair;
   int outstanding; 
   // -------------------------------------------------------------------------------------
   void prepare_request(RaidRequest<OsvIoReq>* req, OsvIoReqCallback spdkCb);
   // -------------------------------------------------------------------------------------
  public:
   OsvChannel(IoOptions options, NVMeController& controller, int queue);
   ~OsvChannel();
   // -------------------------------------------------------------------------------------
   void _push(RaidRequest<OsvIoReq>* req);
   void pushBlocking(IoRequestType type, char* data, s64 addr, u64 len, bool write_back) { throw std::logic_error("not implemented"); }
   int _submit()
   {
      for (auto& req : write_request_stack) {  // ok but this is done until stack empty
         int ret;
         if (true || outstanding < 5) {
            // TODO: here we need to insert the correct functions
            // but i think we do it the same as spdk (-> put the stuff in the correct functions)
            ret = OsvEnvironment::osv_req_type_fun_lookup[(int)req->impl.type](1, qpair, req->impl.buf, req->impl.lba, req->impl.lba_count, NVMeController::completion, req, 0);

            outstanding++;
            ensure(ret == 0);
            req = nullptr;
         }
         // controller.submit(req->base.device, queue, reinterpret_cast<SpdkIoReq*>(&req->impl));
      }
      int left = 0;
      for (int i = 0; i < write_request_stack.size(); i++) {
         if (write_request_stack[i] != nullptr) {
            write_request_stack[left++] = write_request_stack[i];
         }
      }
      int submitted = write_request_stack.size() - left;
      write_request_stack.resize(left);
      // write_request_stack.clear();
      return submitted;
   }

   int _poll(int)
   {
      int done = 0;

      // for (unsigned int i = 0; i < qpairs.size(); i++) {
      int ok = OsvEnvironment::osv_nvme_qpair_process_completions(qpair, 0);
      // TODO: here we need to insert the req_done logic -> here are the requests fetched
      // int ok;
      outstanding -= ok;
      ensure(ok >= 0);
      done += ok;
      // }
      assert(done >= 0);
      return done;
   }
   void _printSpecializedCounters(std::ostream& ss);
};
}  // namespace mean
#endif
