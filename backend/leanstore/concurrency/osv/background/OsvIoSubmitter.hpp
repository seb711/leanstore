#pragma once

#include "OsvBackgroundThreadBase.hpp"
#include "leanstore/io/RequestStackLockfree.hpp"
#include "leanstore/io/IoChannel.hpp"

namespace mean
{

template <typename TImplRequest>
class OsvIoSubmitter : public OsvBackgroundThreadBase
{
  private:
  IoChannel& io_channel; 
  RequestStackLockfree<RaidRequest<TImplRequest>>& request_stack;

    // will poll and submit
   unsigned getPriority() override {
    // request_stack.outstanding stores the io_requests that could be pushed but are not pushed yet
    // policy: run it if more than 1/4 of the queue size is used
    // attention: could starve if at some point no more items are pushed (should not happen in leanstore)
    return request_stack.outstanding() > (request_stack.max_entries / 4) ? 5 : 0; 
 }; 
   // will poll and submit
   int process() override {
    while (true) {
       // this is it for now
       // maybe we also need two threads for submit and for polling
       // submit depends on the request_stack
       // poll depends on the io_channel
       io_channel.poll();
    }
    return 0;
 };

   public: 
   // -------------------------------------------------------------------------------------
   OsvIoSubmitter(IoChannel& io_channel, RequestStackLockfree<RaidRequest<TImplRequest>>& request_stack, int id) : OsvBackgroundThreadBase("io_submitter", sched::thread_background::io_submitter, id), io_channel(io_channel), request_stack(request_stack) {
    start(); 
   };
   ~OsvIoSubmitter() = default;
   // -------------------------------------------------------------------------------------
   OsvIoSubmitter(const OsvIoSubmitter&) = delete;
   OsvIoSubmitter(OsvIoSubmitter&&) = delete;
   OsvIoSubmitter& operator=(const OsvIoSubmitter&) = delete;
   OsvIoSubmitter& operator=(OsvIoSubmitter&&) = delete;
};
}  // namespace mean
