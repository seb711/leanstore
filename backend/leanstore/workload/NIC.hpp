// ============== NIC.hpp ==============
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include "leanstore/Config.hpp"
#include "leanstore/concurrency/utils/SharedConfig.hpp"

#ifndef NIC_DEF
#define NIC_DEF

namespace mean
{

enum class BaseRequestType : uint8_t {
   A = 0,
   B,
   C,
   D,
   E,
   BACKGROUND, 
   SYSTEM
};

// ---------------------------------------------------------------------------
// Base Request — every NIC produces these; concrete NICs extend it
// ---------------------------------------------------------------------------
struct BaseRequest {
   uint64_t timestamp = 0;
   uint64_t key = 0;
   BaseRequestType type;
   BaseRequest() = default;
   BaseRequest(uint64_t ts, uint64_t k, BaseRequestType t) : timestamp(ts), key(k), type(t) {}
   virtual ~BaseRequest() = default;
};

// ---------------------------------------------------------------------------
// Abstract NIC — the Product interface
// ---------------------------------------------------------------------------
class AbstractNIC
{
  protected:
   // Shared timing infrastructure
   static constexpr size_t BUF_CAP = 1 << 16;
   std::array<BaseRequest, BUF_CAP> buffer_;
   uint64_t next_time_ = 0;
   double rate_ = 0.0;
   std::mt19937_64 gen_{std::random_device{}()};
   std::exponential_distribution<double> dist_;
   uint64_t prev_version_ = 0;
   bool active_ = false;

   // Ring buffer sizing — subclasses define element size via virtual
   size_t head_ = 0, tail_ = 0, size_buf_ = 0;

  public:
   AbstractNIC(double rate) : buffer_(), rate_(rate), dist_(rate) {}
   virtual ~AbstractNIC() = default;

   // --- Core interface (the "concept" you listed) ---
   virtual void sync() = 0;
   virtual size_t size() const = 0;
   virtual BaseRequest* get(size_t i) = 0;
   virtual BaseRequest get() = 0;
   virtual void consume(size_t n) = 0;

   virtual void turn_on() { active_ = true; }
   virtual void turn_off() { active_ = false; }

   // --- Config-driven rate update (common timing logic) ---
   // Subclasses override to also rebuild distributions, mixes, etc.
   virtual void update_rate_from_config() = 0;

   double get_rate() const { return rate_; }
   bool is_active() const { return active_; }
};

// ---------------------------------------------------------------------------
// Factory Method
// ---------------------------------------------------------------------------
class NICCreator
{
  public:
   virtual ~NICCreator() = default;
   virtual std::unique_ptr<AbstractNIC> createNIC(double initial_rate) = 0;
};

}  // namespace mean

#endif