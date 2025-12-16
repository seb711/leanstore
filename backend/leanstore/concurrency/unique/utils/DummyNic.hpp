// we want a dummy nic that simulates new requests in a request queue
// we want to poll this queue for new jobs

// the queue should return a tuple of incoming_timestamp, and a key
// the incoming timestamp is caluclated based on a uniform distribution

// i want to reuse this logic to caculate the timestamps based on the last time
// when we have called the poll

// so the dummy nic is polled and returns a pointer to a list of tuples of incomping timestamp and key (both uint64)
// the class should only have a poll() as a parameter and return how how many items we polled

// we as the application are required to keep track of the pointer of the current queue. so the nic is
// responsible of like keeping a ringbuffer full and we as the application also indicate the queue
// when we have finished a request.

// so the workflow would be
// 1. we poll
// 2. the dummy nic would check when we have last polled and fill the queue with requests based on the rate
//  we have previously specified with a uniform deviation (if we have an overfill of the ringbuffer we abort)
// 3. we return to the caller
// 4. the caller gets the current tail pointer and head pointer and polls all requests into his request queue (this is not more important here)

// which parts should this dummy nic have
// 1/ a ringbuffer with a function for retrieving head and tail pointer
// 2 a function to move the tail pointer so that the virtual nic can add new ones when all are flushed
// 3 a update function which trigger the update function in the nic that basically fills up and simulates incoming requests.

#pragma once
#include <cstdint>
#include <exception>
#include <random>
#include <vector>
#include "leanstore/concurrency/Mean.hpp"

namespace mean
{
struct Request {
   uint64_t timestamp;
};

class DummyNIC
{
   std::vector<Request> buffer_;
   size_t head_ = 0, tail_ = 0, size_ = 0;
   uint64_t next_time_, next_key_ = 0;
   double rate_;
   std::mt19937_64 gen_;
   std::exponential_distribution<double> dist_;

  public:
   bool active = false;

  public:
   DummyNIC(double rate, uint64_t capacity = 1024) : buffer_(capacity), next_time_(0), rate_(rate), gen_(std::random_device{}()), dist_(rate) {}

   size_t poll()
   {
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
            // throw std::runtime_error("overflow");
         }
         buffer_[tail_] = {next_time_};
         tail_ = (tail_ + 1) % buffer_.size();
         size_++;
         count++;
         next_time_ += mean::nsToTSC(uint64_t(dist_(gen_) * 1e9));
      }
      return count;
   }

   void turn_on() { active = true; }
   void turn_off() { active = false; };
   size_t size() const { return size_; }
   size_t head() const { return head_; }
   size_t tail() const { return tail_; }
   const Request* get(size_t i) const { return i < size_ ? &buffer_[(head_ + i) % buffer_.size()] : nullptr; }
    void consume(size_t n) { head_ = (head_ + n) % buffer_.size(); size_ -= n; }
};
}  // namespace mean
