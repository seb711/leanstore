#pragma once
#if LEANSTORE_INCLUDE_OSV
// -------------------------------------------------------------------------------------
#include "../Raid.hpp"
// -------------------------------------------------------------------------------------
// TODO WHICH INCLUDES FROM OSV
// -------------------------------------------------------------------------------------
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include "drivers/nvme-user-queue.hh"

// -------------------------------------------------------------------------------------
#define checkThrow(test, message)         \
   do {                                   \
      if (!test) {                        \
         throw std::logic_error(message); \
      }                                   \
   } while (false);
#define checkMessage(test, message)         \
   do {                                     \
      if (!test) {                          \
         std::cout << message << std::endl; \
      }                                     \
   } while (false);
// -------------------------------------------------------------------------------------
enum class OsvIoReqType {
   Write = 0,
   Read = 1,
   // Flush =2, 
   COUNT = 2  // always last
              // Don't forget to add pointers to  spdk_req_type_fun_lookup in SpdkEnv init
};
// -------------------------------------------------------------------------------------
struct OsvIoReq;
using OsvIoReqCallback = void (*)(OsvIoReq* req);
struct OsvIoReq {
   char* buf;
   uint64_t lba;
   uint64_t append_lba; // do not know why we needed that initially
   OsvIoReqCallback callback;
   void* qpair;
   uint32_t lba_count;
   //
   void* this_ptr;
   OsvIoReqType type;
};
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
using cmd_fun = std::function<int(int, void*, void*, uint64_t, uint32_t, osv_nvme_cmd_cb, void *, uint32_t)>;  // FIXME first namespace

class OsvEnvironment
{
   static bool initialized;
   static bool isInitialized(bool init = false);
   ;

  public:
   static cmd_fun osv_req_type_fun_lookup[(int)OsvIoReqType::COUNT + 1];
   static int osv_nvme_qpair_process_completions(nvme::io_user_queue_pair* qpair, uint32_t max); 
   static void ensureInitialized();
   static void init();
   static void deinit();

   // TODO: dont think that we need them here
   // static void* dma_malloc(size_t size, size_t alignment = 0, uint64_t* phys_addr = nullptr);
   // static void dma_free(void* buf);
};
// -------------------------------------------------------------------------------------
class NVMeInterface
{
  public:
   virtual void connect(std::string connectionString) = 0;
   virtual void allocateQPairs() = 0;
   virtual int32_t qpairSize() = 0;
   virtual uint32_t nsLbaDataSize() = 0;
   virtual uint64_t nsSize() = 0;
   virtual void submit(int queue, OsvIoReq* req) = 0;
   virtual int32_t process(int queue, int max) = 0;
   virtual ~NVMeInterface() {};
};

class NVMeController : public NVMeInterface {
public:
	// TODO: check if we need this in the near future -> i do not think so 
    // std::string pciefile;
	// struct spdk_nvme_transport_id trid;
	// struct spdk_nvme_ctrlr *ctrlr = nullptr;
	// struct spdk_nvme_ns* nameSpace;
   
   
   // -------------------------------------------------------------------------------------
   uint64_t ns_capcity_lbas = 0;
   // -------------------------------------------------------------------------------------
   uint64_t dones = 0;
   uint64_t submitted = 0;
   // Info
   // TODO: what is this needed for? this has only the nvme pcie device information in it -> not needed atm 
   // const struct spdk_nvme_ctrlr_data *cdata;
   int maxQPairs = -1;
   std::vector<nvme::io_user_queue_pair*> qpairs;
   // the poll groups aren't really used. They're needed to access pci statistics.
   // see printPciQpairStats
   // TODO: do we need the poll groups? 
   // std::vector<osv_nvme_poll_group*> pollGroups; 
   // -------------------------------------------------------------------------------------
   NVMeController();
   ~NVMeController() override; 
   // -------------------------------------------------------------------------------------
   void setDeviceId(int id) {
      device_id =id; 
   }; 
   void connect(std::string pciefile) override {};
   uint32_t nsLbaDataSize() override; 
   uint64_t nsSize() override; 
	void allocateQPairs() override; 
   uint32_t requestMaxQPairs(); 
   // -------------------------------------------------------------------------------------
    // TODO: what is process doing anyways? 
   int32_t process(int qpair, int max) {
      int ok = 0;
      if (submitted - dones > 0) {
            ok = OsvEnvironment::osv_nvme_qpair_process_completions(qpairs[qpair], max); // req_page_done function 
         // TODO: check if we call process somewhere
         dones += ok;
         assert(ok >= 0);
      }
      return ok; 
   }
   // -------------------------------------------------------------------------------------
   void submitCheck(int ret, int submitted, int dones) {
      if (ret == -ENOMEM) {
         throw std::logic_error("Could not submit io. ret: " + std::to_string(ret) + " ENOMEM (outstanding ios:  " + std::to_string(submitted - dones));
      } else {
         throw std::logic_error("Could not submit io. ret: " + std::to_string(ret) + " sub: " + std::to_string(submitted) + " done: " + std::to_string(dones));
      }
   }
   // -------------------------------------------------------------------------------------
   void submit(int qpair, OsvIoReq* req)  { //, SpdkIoCallback callback) {
      //std::stringstream ss;
      //ss << pciefile << " qpair: "<< qpair << " submit: t: " << (int)req->type << " lba: "<< req->lba << " cnt: " << req->lba_count << std::endl;
      //std:: cout << ss.str();
      assert(req->lba_count > 0);
      //if (req->lba_count != 1 ) throw "";
      req->qpair = qpairs[qpair];
      int ret = OsvEnvironment::osv_req_type_fun_lookup[(int)req->type](1, (void*) qpairs[qpair], req->buf, req->lba, req->lba_count, completion, req, 0); // TODO: check if namespace is correct
      

      /*
         int ret;
         if (req->type == SpdkIoReqType::Read) {
         ret = spdk_nvme_ns_cmd_read(nameSpace, qpairs[qpair], req->buf, req->lba, req->lba_count, completion, req, 0);
         } else {
      //std::cout << "write: cb_arg: " << std::hex << (u64)&req <<
      ret = spdk_nvme_ns_cmd_write(nameSpace, qpairs[qpair], req->buf, req->lba, req->lba_count, completion, req, 0);
      }
      ///*/
      if (ret != 0) {
         submitCheck(ret, submitted, dones);
      }
      submitted++;
   }

   static void completion(void *cb_arg, const nvme_sq_entry_t *cpl);

   uint32_t queueDepth(); 
	uint32_t numberNamespaces(); 
	uint64_t nsNumLbas(); 
	void allocateQPairs(int number); 
	int32_t qpairSize() override;  
    // TODO: these are all methods that we do not need at the moment -> maybe revisit them later
	// bool requestFeature(int32_t feature, struct spdk_nvme_cpl& cpl); 
	// bool pushRawCmd(struct spdk_nvme_cmd& cmd, struct spdk_nvme_cpl& cpl); 
	// bool checkArbitrationRoundRobinCap(); 
	// void setArbitration(int low, int middle, int high); 
	// void printArbitrationFeatures(); 
	// void printPciQpairStats(); 

private: 
   // here we bind the methods from the nvme driver
   int device_id = -1; 
};
// -------------------------------------------------------------------------------------
class NVMeMultiController {
public:
	std::vector<NVMeController> controller;
	void connect(std::string connectionString) ; 
	void allocateQPairs() ; 
	int32_t qpairSize() ; 
	// OPTIMIZE
   uint32_t nsLbaDataSize() ; 
   uint64_t nsSize() ; 
   int deviceCount(); 
   // -------------------------------------------------------------------------------------
   void submit(int device, int queue, OsvIoReq* req)  {
      controller[device].submit(queue, req);
   }
   // -------------------------------------------------------------------------------------
   int32_t process(int queue, int max)  {
      int32_t done = 0;
      for (auto& c: controller) {
         done += c.process(queue, max);
      }
      return done;
   }
};
#endif