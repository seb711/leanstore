#pragma once
#include <atomic>
#include <cstdint>
#include <exception>
#include <osv/leanstore_debug.hh>

#ifndef LEANSTORE_INCLUDE_OSV
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace mean
{

template <typename WorkloadConfigType>
struct SharedConfig {
   volatile WorkloadConfigType* config_ = nullptr;
#ifdef LEANSTORE_INCLUDE_OSV
   SharedConfig(const char* filepath) { config_ = (WorkloadConfigType*)leanstore_osv_debug::get_shared_memory(); }
#else
   SharedConfig(const char* filepath)
   {
      static void* mapped_addr = nullptr;

      if (!mapped_addr) {
         int fd = open(filepath, O_RDWR);
         if (fd == -1) {
            perror("open");
            std::cout << "failed to open file" << std::endl;
            // return nullptr;
         }

         mapped_addr = mmap(nullptr, sizeof(SharedConfig), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

         close(fd);  // Can close fd after mmap

         if (mapped_addr == MAP_FAILED) {
            perror("mmap");
            std::cout << "failed to mmap file" << std::endl;
            // return nullptr;
         }
      }

      config_ = (WorkloadConfigType*)mapped_addr;
   }
#endif
   volatile WorkloadConfigType* operator->() { return config_; }

} __attribute__((packed));

}  // namespace mean