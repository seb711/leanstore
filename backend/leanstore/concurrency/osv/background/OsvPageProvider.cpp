#include "OsvPageProvider.hpp"
#include <osv/leanstore_debug.hh> 
#include "leanstore/Config.hpp"
#include <gflags/gflags.h>

namespace mean
{
OsvPageProvider::OsvPageProvider(leanstore::storage::BufferManager* bf_ptr, int pid, int affinity)
    : OsvBackgroundThreadBase("page_provider", sched::thread_background::page_provider, pid, affinity), bf_ptr(bf_ptr), partition_id(pid) {
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
   return (counter < 250) ? 1 : 0; 
}

int OsvPageProvider::process()
{

   while (true) {
         // std::cout << "empty freelist " << bf_ptr->cooling_partitions[partition_id].dram_free_list.counter << std::endl; 
         leanstore_osv_debug::trace_background_result(bf_ptr->cooling_partitions[partition_id].dram_free_list.counter); 
         // bf_ptr->pageProviderCycle(partition_id);

/*
               THIS IS JUST A TEMPORARY FIX FOR A SITUATION IN WHICH THE PAGEPROVIDER
               CANNOT ACCESS THE LOCKS DUE TO HOW LOCKS ARE IMPLEMENTED IN OSV

               - THE JOB THREADS WAIT FOR FREE PAGES AND REQUEST A LOCK AND THEREFORE
               INCREMENT THE LOCK-COUNTER IN LFMUTEX.CC
               - THE PAGEPROVIDER ALSO WANTS THE LOCK TO FREE PAGES; BUT THE PAGEPROVIDER
               DOES THIS WITH TRY-LOCK AND NOT WITH LOCK AND THEREFORE HAS LEAST PRIORITY

               WE CURRENTLY RESOLVE THIS BY ASSESSING WHEN THIS SITUATION IS ACTIVE (NO FREE PAGES AND THE SITUATION IS NOT HANDLED)

               - IN THAT CASE WE GET LOCKS ON ALL IO PARTITIONS AND CALL THE PAGEPROVIDER

               THIS SOLUTION IS CURRENTLY ONLY POSSIBLE IF WE HAVE ONE COOLING PARTITION
               FIXME: ADD SUPPORT FOR MULTIPLE COOLING PARTITIONS
            */
            if (bf_ptr->cooling_partitions[partition_id].dram_free_list.counter == 0 && counter++ > 10) {
               std::cout << "pageprovider situation" << std::endl; 
               std::vector<std::unique_ptr<std::unique_lock<mean::io_mutex>>> locks;

               for (size_t io_partition_idx = 0; io_partition_idx < bf_ptr->io_partitions_count; io_partition_idx++) {
                  locks.push_back(std::make_unique<std::unique_lock<mean::io_mutex>>(bf_ptr->io_partitions[io_partition_idx].io_mutex));
               }

               bf_ptr->pageProviderCycle(partition_id);

               counter = 0;
            } else {
         bf_ptr->pageProviderCycle(partition_id);

            }
         // assert(bf_ptr->cooling_partitions[partition_id].dram_free_list.counter > 1000); 

      leanstore_osv_debug::yield(); 
   }

   return 0;
}
}  // namespace mean