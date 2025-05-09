#pragma once
// -------------------------------------------------------------------------------------
#include <condition_variable>
#include <mutex>
#include <Units.hpp>
#include <atomic>

namespace mean {
    struct BlockingIoContext {
        std::mutex mtx;
        std::condition_variable cv; 
        std::atomic<bool> ready = {false};
        u64 magic; 
  
        BlockingIoContext() : ready(false), magic(0) {} 
  
      // Delete copy constructor & copy assignment
      BlockingIoContext(const BlockingIoContext&) = delete;
      BlockingIoContext& operator=(const BlockingIoContext&) = delete;
  
      // Allow move semantics if needed
      BlockingIoContext(BlockingIoContext&&) = default;
      BlockingIoContext& operator=(BlockingIoContext&&) = default;   };
}