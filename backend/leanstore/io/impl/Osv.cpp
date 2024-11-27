#if LEANSTORE_INCLUDE_OSV
#include "Osv.hpp"
#include <cstddef>
#include <regex>
#include "drivers/nvme-user-queue.hh"

bool OsvEnvironment::initialized = false;
cmd_fun OsvEnvironment::osv_req_type_fun_lookup[(int)OsvIoReqType::COUNT + 1];

int OsvEnvironment::osv_nvme_qpair_process_completions(nvme::io_user_queue_pair* qpair, uint32_t max) 
{
   // fixme that can be done better
   return leanstore_osv_nvme_qpair_process_completions(qpair, max);
}
void OsvEnvironment::ensureInitialized()
{
   if (!isInitialized()) {
      throw std::logic_error("OsvEnvironment not initialized");
   }
}
bool OsvEnvironment::isInitialized(bool init)
{
   if (init) {
      initialized = init;
   }
   return initialized;
};
void OsvEnvironment::init()
{
   if (isInitialized()) {
      throw std::logic_error("OsvEnvironment already initialized");
   }

   std::cout << "set the functions" << std::endl; 

   // TODO: these are the functions we need -> therefore it would be really good to have the whole osv_nvme_nvme package importable
   OsvEnvironment::osv_req_type_fun_lookup[(int)OsvIoReqType::Read] = leanstore_osv_nvme_nv_cmd_read; 
   OsvEnvironment::osv_req_type_fun_lookup[(int)OsvIoReqType::Write] = leanstore_osv_nvme_nv_cmd_write; 
   OsvEnvironment::osv_req_type_fun_lookup[(int)OsvIoReqType::COUNT] = nullptr;
   /*
       // TODO: do we need to set env vars in osv?
    struct spdk_env_opts opts;
    int g_shm_id = -1;
    int g_dpdk_mem = 0;
    int g_main_core = 0;           // master must be in core_mask e.g. 0 in 0x1
    char g_core_mask[16] = "0x1";  //"0xffffffff";
    // bool g_vmd = false;


    spdk_env_opts_init(&opts);
    opts.name = "meanio_spdk_env_name";
    opts.shm_id = g_shm_id;
    opts.mem_size = g_dpdk_mem;
    opts.mem_channel = 1;
    opts.main_core = g_main_core;
    opts.core_mask = g_core_mask;

    // opts.iova_mode = "va";
    // opts.env_context = (void*)"--log-level=lib.eal:8";

    if (spdk_env_init(&opts) < 0) {
       throw std::logic_error(
           "Unable to initialize SPDK env. \n 1) Are you sure you are running with sufficien privileges? \n 2) Is the SPDK environment setup(.sh)?");
    }
    */

   /*
      if (g_vmd && spdk_vmd_init()) {
      throw std::logic_error("Failed to initialize VMD. Some NVMe devices can be unavailable.");
      }
      */
   isInitialized(true);
}

void OsvEnvironment::deinit()
{
   if (isInitialized()) {
      // TODO: check if we need to do this -> i do not think that we need to do this in osv
      // spdk_env_fini();
   }
}
// -------------------------------------------------------------------------------------
// NVMeController
// -------------------------------------------------------------------------------------
NVMeController::NVMeController()
{
   OsvEnvironment::ensureInitialized();
}
// -------------------------------------------------------------------------------------
NVMeController::~NVMeController()
{
   /* if (ctrlr) {
      std::cout << "NVMeController destructor submitted: " << submitted << " dones: " << dones << std::endl;
      // printPciQpairStats();
      for (auto q : qpairs) {
         spdk_nvme_ctrlr_free_io_qpair(q);
      }
      spdk_nvme_detach(ctrlr);
   } */

   // TODO: somehow we should see if we release the io_queues but for now we wont do that -> just exit
   for (auto& qpair : qpairs) {
      remove_io_user_queue(qpair->_id);
   }
   qpairs.clear();
}
// -------------------------------------------------------------------------------------
void NVMeController::connect(std::string nvmedev)
{
   // in here we should connect to the osv driver
   // for now we go with the easiest option and just statically bind to 
   // the default ones because we also do not have any architecture for nvme mapping
   remove_io_user_queue = std::bind(leanstore_remove_io_user_queue, std::placeholders::_1);
   create_io_user_queue = std::bind(leanstore_create_io_user_queue, std::placeholders::_1);
}
// -------------------------------------------------------------------------------------
uint32_t NVMeController::nsLbaDataSize()
{
   // return spdk_nvme_ns_get_sector_size(nameSpace);
   // TODO: hopefully these can be retrieved by osv driver
   return 4096; 
}
// -------------------------------------------------------------------------------------
uint64_t NVMeController::nsSize()
{
   // return spdk_nvme_ns_get_size(nameSpace);
   // TODO: hopefully these can be retrieved by osv driver
   return 1 << 14; 
}
// -------------------------------------------------------------------------------------
void NVMeController::allocateQPairs()
{
   allocateQPairs(-1);
}
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
uint32_t NVMeController::numberNamespaces()
{
   // currently we only have one
   return 1;
}
// -------------------------------------------------------------------------------------
uint64_t NVMeController::nsNumLbas()
{
   // return spdk_nvme_ns_get_num_sectors(nameSpace);
   // TODO: also i do not know what is needed here -> we need to find that out afterwards
   return 100000000;  // FIXME idt that we need this
}
// -------------------------------------------------------------------------------------
void NVMeController::allocateQPairs(int number)
{
   if (number < 0) {
      number = 8;
   }

   for (int i = 0; i < number; i++) {
      auto* qpair = (nvme::io_user_queue_pair*) create_io_user_queue(i);

      if (!qpair) {
         throw std::logic_error("ERROR: leanstore_create_io_user_queue() failed\n");
      }

      qpairs.push_back(qpair);
   }
}
// -------------------------------------------------------------------------------------
void NVMeController::completion(void* cb_arg, const nvme_sq_entry_t* sqe) {
   auto request = static_cast<OsvIoReq*>(cb_arg);

   // TODO: WE WOULD NEED ALSO TO CHECK HERE FOR ERRORS -> THIS IS IN CQ ENTRY BUT SHOULD BE OK FOR NOW
   // yea this shouldnt be casted without any checks -> will be a problem but yea fuck it

	// request->append_lba = ((nvme_command_rw_t*) sqe)->slba; // slba = cdw10 // TODO: DO NOT KNOW WHY WE NEED THIS ATM OG COMMENT: new lba set by zone_append
   // std::cout << "completion: req: "<< std::hex << (uint64_t)request << " buf: " << (uint64_t) request->buf << std::endl;
   request->callback(request);
};

// -------------------------------------------------------------------------------------
int32_t NVMeController::qpairSize()
{
   return qpairs.size();
}
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
#endif
