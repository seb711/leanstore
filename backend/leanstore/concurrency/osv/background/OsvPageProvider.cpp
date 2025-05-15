#include "OsvPageProvider.hpp"

namespace mean
{
OsvPageProvider::OsvPageProvider(leanstore::storage::BufferManager* bf_ptr, int pid)
    : OsvBackgroundThreadBase("page_provider", sched::thread_background::page_provider, pid), bf_ptr(bf_ptr), partition_id(pid) {
      start(); 
    };

OsvPageProvider::~OsvPageProvider() {};

unsigned OsvPageProvider::getPriority() {
   // bf_ptr->cooling_partitions[partition_id].dram_free_list.counter counts the currently free lists
   // policy: run it if <10% are free
   auto counter = bf_ptr->cooling_partitions[partition_id].dram_free_list.counter.load();
   // std::cout << "[page provider] counter = " << counter << std::endl;
   return bf_ptr->cooling_partitions[partition_id].dram_free_list.counter < 100 ? 1 : 0; 
}

int OsvPageProvider::process()
{
   while (true) {
      if (bf_ptr->cooling_partitions[partition_id].dram_free_list.counter == 0 && counter++ > 10) {
         std::vector<std::unique_ptr<std::unique_lock<mean::mutex>>> locks;

         for (size_t io_partition_idx = 0; io_partition_idx < bf_ptr->io_partitions_count; io_partition_idx++) {
            locks.push_back(std::make_unique<std::unique_lock<mean::mutex>>(bf_ptr->io_partitions[io_partition_idx].io_mutex));
         }

         bf_ptr->pageProviderCycle(partition_id);
         counter = 0;
      } else {
         bf_ptr->pageProviderCycle(partition_id);
      }
      leanstore_osv_debug::yield(); 
   }

   return 0;
}
}  // namespace mean
