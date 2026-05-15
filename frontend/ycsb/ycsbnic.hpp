// ============== YCSBNIC.hpp ==============
#pragma once

#ifndef YCSBNIC_DEF
#define YCSBNIC_DEF
#include <array>
#include "leanstore/concurrency/utils/SharedConfig.hpp"
#include "leanstore/profiling/counters/WorkerCounters.hpp"
#include "leanstore/workload/NIC.hpp"

namespace mean
{

// ---------------------------------------------------------------------------
// YCSB-specific NIC
// ---------------------------------------------------------------------------
struct YCSBWorkloadConfig {
   uint64_t version;
   uint64_t freq;  // aggregate req/s (all threads combined)
   uint64_t var;   // variance 0-100  (reserved, not used yet)

   // cumulative mix percentages [0,100]
   // Example YCSB-D: read_pct=95  insert_pct=100  update_pct=100  scan_pct=100
   uint8_t read_pct;
   uint8_t insert_pct;
   uint8_t update_pct;
   uint8_t scan_pct;
   // remainder → type E (read-modify-write)

   // key-distribution knobs
   double zipf_factor;        // e.g. 0.99 (A-C,F) or 0.75 (D)
   uint64_t key_range;        // number of keys in the "hot" window
   bool latest_distribution;  // true → offset from highest_inserted
};

class YCSBNIC : public AbstractNIC
{
   SharedConfig<YCSBWorkloadConfig> config_;

   // Workload mix — cached from config (cumulative percentages 0-100)
   uint8_t read_pct_ = 95;
   uint8_t update_pct_ = 100;
   uint8_t insert_pct_ = 100;
   uint8_t scan_pct_ = 100;
   // remainder → READ_MODIFY_WRITE

   // Key distribution
   // TODO: plug in your ScrambledZipfGenerator / LatestZipfGenerator here
   std::uniform_int_distribution<uint64_t> key_dist_{
       0, static_cast<u64>((FLAGS_ycsb_tuple_count) ? FLAGS_ycsb_tuple_count
                                                    : FLAGS_target_gib * 1024 * 1024 * 1024 * 1.0 / 2.0 / (sizeof(YCSBKey) + sizeof(YCSBPayload))) -
              1};
   // double zipf_factor_ = 0.99;
   // bool latest_distribution_ = false;
   // uint64_t key_range_ = 1'000'000;

   std::uniform_int_distribution<int> type_roll_{0, 99};

   BaseRequestType pick_type()
   {
      int r = type_roll_(gen_);
      if (r < read_pct_)
         return BaseRequestType::A;
      if (r < update_pct_)
         return BaseRequestType::C;
      if (r < insert_pct_)
         return BaseRequestType::B;
      if (r < scan_pct_)
         return BaseRequestType::D;
      return BaseRequestType::E;
   }

   uint64_t pick_key()
   {
      // Replace with zipf / latest logic from your plan
      return key_dist_(gen_);
   }

  public:
   YCSBNIC(const char* filepath, double rate) : AbstractNIC(rate), config_(filepath) {}

   void sync() override
   {
      if (prev_version_ < config_->version) {
         update_rate_from_config();
         prev_version_ = config_->version;
      }

      uint64_t now = mean::readTSC();
      if (!active_)
         return;
      if (next_time_ == 0)
         next_time_ = now;

      while (now >= next_time_) {
         if (size_buf_ >= BUF_CAP) {
            leanstore::WorkerCounters::myCounters().time_counter_2 += 1;
            next_time_ = now;
            size_buf_ = 0;
            head_ = 0;
            tail_ = 0;
            return;
         }
         auto& req = buffer_[tail_];
         req.timestamp = next_time_;
         req.type = pick_type();
         req.key = pick_key();

         tail_ = (tail_ + 1) % BUF_CAP;
         size_buf_++;
         next_time_ += mean::nsToTSC(uint64_t(dist_(gen_) * 1e9));
      }
   }

   void update_rate_from_config() override
   {
      if (!config_.operator->())
         return;

      double base_freq = static_cast<double>(config_->freq) / FLAGS_worker_threads;
      rate_ = base_freq;
      dist_ = std::exponential_distribution<double>(rate_);

      // Update mix from config
      read_pct_ = config_->read_pct;
      insert_pct_ = config_->insert_pct;
      update_pct_ = config_->update_pct;
      scan_pct_ = config_->scan_pct;

      // TODO: rebuild zipf / latest generators from config_->zipf_factor,
      //       config_->key_range, config_->latest_distribution

      // Reset buffer
      next_time_ = mean::readTSC();
      size_buf_ = 0;
      head_ = 0;
      tail_ = 0;
   }

   size_t size() const override { return size_buf_; }

   BaseRequest* get(size_t i) override { return i < size_buf_ ? &buffer_[(head_ + i) % BUF_CAP] : nullptr; }

   BaseRequest get() override { return (active_ ? BaseRequest{mean::readTSC(), pick_key(), pick_type()} : BaseRequest{0, 0, BaseRequestType::A}); }

   void consume(size_t n) override
   {
      head_ = (head_ + n) % BUF_CAP;
      size_buf_ -= n;
   }
};

class YCSBNICCreator : public NICCreator
{
  public:
   std::unique_ptr<AbstractNIC> createNIC(double initial_rate) override { return std::make_unique<YCSBNIC>("/dev/shm/myshm", initial_rate); }
};

}  // namespace mean

#endif