#pragma once
#include <atomic>
#include <cstdint>
#include <exception>
#include <osv/leanstore_debug.hh>
#include <random>
#include <vector>
#include <array>
#include "leanstore/concurrency/Mean.hpp"

namespace mean
{
struct Request {
   uint64_t timestamp;
};

struct SharedConfig {
   volatile uint64_t version;
   uint64_t freq;  // Base rate (requests per second)
   uint64_t var;   // Variance/deviation percentage (0-100)
} __attribute__((packed));

class DummyNIC
{
   std::array<Request, 1 << 12> buffer_;
   size_t head_ = 0, tail_ = 0, size_ = 0;
   uint64_t next_time_, next_key_ = 0;
   double rate_;
   std::mt19937_64 gen_;
   std::exponential_distribution<double> dist_;
   volatile SharedConfig* config_;  // Pointer to shared memory config
   uint64_t prev_version = 0;

  public:
   bool active = false;

  public:
   DummyNIC(double rate)
       : buffer_(), next_time_(0), rate_(rate), gen_(std::random_device{}()), dist_(rate), config_(nullptr)
   {
      config_ = (volatile SharedConfig*)leanstore_osv_debug::get_shared_memory();
   }

   size_t poll()
   {
      // Check for config updates
      if (config_ && prev_version < config_->version) {
         // std::cout << "upgrade rate" << std::endl; 
         update_rate_from_config();
         prev_version = config_->version;
      }

      uint64_t now = mean::readTSC();
      if (!active)
         return 0;
      if (next_time_ == 0)
         next_time_ = now;
      size_t count = 0;
      while (now >= next_time_) {
         if (size_ >= buffer_.size()) {
            std::cout << "reset overflow" << std::endl;
            next_time_ = now;
            return count;
         }
         buffer_[tail_] = {next_time_};
         tail_ = (tail_ + 1) % buffer_.size();
         size_++;
         count++;
         next_time_ += mean::nsToTSC(uint64_t(dist_(gen_) * 1e9));
      }
      return count;
   }

   void update_rate_from_config()
   {
      if (!config_)
         return;

      uint64_t base_freq = config_->freq;    // Requests per second
      uint64_t variance_pct = config_->var;  // Variance 0-100%

      // Calculate new rate with variance applied
      // variance_pct of 0 means exact rate, 100 means full uniform distribution
      double new_rate = static_cast<double>(base_freq);

      // std::cout << "Updating rate: freq=" << base_freq << " var=" << variance_pct << " new_rate=" << new_rate << std::endl;

      rate_ = new_rate;
      dist_ = std::exponential_distribution<double>(rate_);
   }

   void turn_on() { active = true; }
   void turn_off() { active = false; }

   size_t size() const { return size_; }
   size_t head() const { return head_; }
   size_t tail() const { return tail_; }
   const Request* get(size_t i) const { return i < size_ ? &buffer_[(head_ + i) % buffer_.size()] : nullptr; }
   void consume(size_t n)
   {
      head_ = (head_ + n) % buffer_.size();
      size_ -= n;
   }

   double get_rate() const { return rate_; }
};
}  // namespace mean