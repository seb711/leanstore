#if LEANSTORE_INCLUDE_OSV
#include "Osv.hpp"
#include <cstddef>
#include <regex>

bool OsvEnvironment::initialized = false;
cmd_fun OsvEnvironment::osv_req_type_fun_lookup[(int)OsvIoReqType::COUNT + 1];
bool OsvEnvironment::isInitialized(bool init)
{
   if (init) {
      initialized = init;
   }
   return initialized;
};
void OsvEnvironment::ensureInitialized()
{
   if (!isInitialized()) {
      throw std::logic_error("SpdkEnvironment not initialized");
   }
}
void OsvEnvironment::init()
{
   if (isInitialized()) {
      throw std::logic_error("SpdkEnvironment already initialized");
   }

   // TODO: these are the functions we need -> therefore it would be really good to have the whole osv_nvme_nvme package importable
   // SpdkEnvironment::spdk_req_type_fun_lookup[(int)SpdkIoReqType::Read] = &spdk_nvme_ns_cmd_read;
   // SpdkEnvironment::spdk_req_type_fun_lookup[(int)SpdkIoReqType::Write] = &spdk_nvme_ns_cmd_write;
   // SpdkEnvironment::spdk_req_type_fun_lookup[(int)SpdkIoReqType::ZnsAppend] = &spdk_nvme_zns_zone_append;
   // SpdkEnvironment::spdk_req_type_fun_lookup[(int)SpdkIoReqType::COUNT] = nullptr;
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
}
// -------------------------------------------------------------------------------------
void NVMeController::connect(std::string nvmedev)
{
    // in here we should connect to the osv driver
}
// -------------------------------------------------------------------------------------
uint32_t NVMeController::nsLbaDataSize()
{
   // return spdk_nvme_ns_get_sector_size(nameSpace);
   // TODO: hopefully these can be retrieved by osv driver
}
// -------------------------------------------------------------------------------------
uint64_t NVMeController::nsSize()
{
   // return spdk_nvme_ns_get_size(nameSpace);
      // TODO: hopefully these can be retrieved by osv driver
}
// -------------------------------------------------------------------------------------
void NVMeController::allocateQPairs()
{
   allocateQPairs(-1);
}
// -------------------------------------------------------------------------------------
void NVMeController::completion(void* cb_arg, const struct spdk_nvme_cpl* cpl)
{
   // yea probably we need that but i do not know when atm 
    // -> check when implementation is ready what to do here
};
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
}
// -------------------------------------------------------------------------------------
void NVMeController::allocateQPairs(int number)
{
   number = requestMaxQPairs();
}
// -------------------------------------------------------------------------------------
int32_t NVMeController::qpairSize()
{
   return qpairs.size();
}
// -------------------------------------------------------------------------------------
int NVMeController::requestMaxQPairs()
{
   int sub, comp;
   requestMaxQPairs(sub, comp);
   assert(sub == comp);
   return sub;
}
// -------------------------------------------------------------------------------------
void NVMeController::requestMaxQPairs(int32_t& subQs, int32_t& compQs)
{
   /* struct spdk_nvme_cpl cpl;
   requestFeature(SPDK_NVME_FEAT_NUMBER_OF_QUEUES, cpl);
   int32_t ret = cpl.cdw0;
   subQs = (ret & 0xFFFF) + 1;
   compQs = (ret & 0xFFFF0000 >> 16) + 1; */
   // TODO: just get here all io_queues that were created
}
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
#endif
