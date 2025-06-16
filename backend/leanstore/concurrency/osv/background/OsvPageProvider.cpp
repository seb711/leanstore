#include "OsvPageProvider.hpp"

namespace mean
{
OsvPageProvider::OsvPageProvider(leanstore::storage::BufferManager* bf_ptr, int pid)
    : OsvBackgroundThreadBase("page_provider", sched::thread_background::page_provider, pid), bf_ptr(bf_ptr), partition_id(pid) {
      start_background_work(); 
    };

OsvPageProvider::~OsvPageProvider() {};

unsigned OsvPageProvider::getPriority() {
   // bf_ptr->cooling_partitions[partition_id].dram_free_list.counter counts the currently free lists
   // policy: run it if <10% are free
   auto counter = bf_ptr->cooling_partitions[partition_id].dram_free_list.counter.load();
   // std::cout << "[page provider] counter = " << counter << std::endl;
   // return bf_ptr->cooling_partitions[partition_id].dram_free_list.counter < 100 ? 1 : 0; 

   leanstore_osv_debug::trace_pageprovider_state(counter); 
   return (counter < 500) ? 1 : 0; 
}

int OsvPageProvider::process()
{


   while (true) {
         // std::cout << "empty freelist " << bf_ptr->cooling_partitions[partition_id].dram_free_list.counter << std::endl; 
         leanstore_osv_debug::trace_background_result(bf_ptr->cooling_partitions[partition_id].dram_free_list.counter); 
         bf_ptr->pageProviderCycle(partition_id);

      leanstore_osv_debug::yield(); 
   }

   return 0;
}
}  // namespace mean
