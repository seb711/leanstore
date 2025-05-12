#pragma once

#include "OsvBackgroundThreadBase.hpp"
#include "leanstore/io/IoChannel.hpp"
#include "leanstore/io/RequestStackLock.hpp"

namespace mean
{

template <typename TImplRequest>
class OsvIoPoller : public OsvBackgroundThreadBase
{
  private:
    IoChannel& io_channel;
   RequestStackLock<RaidRequest<TImplRequest>>& request_stack;

   // will poll and submit
   unsigned getPriority() override {
    // request_stack.outstanding stores the io_requests that were already handed to the nvme drive but 
    // not yet processed/completed
    // policy: run it if more than 1/4 of the queue size is used
    // attention: could starve if at some point no more items are submitted (should not happen in leanstore)
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
   OsvIoPoller(IoChannel& io_channel, RequestStackLock<RaidRequest<TImplRequest>>& request_stack, int id)
       : OsvBackgroundThreadBase("io_poller", sched::thread_background::io_poller, id), io_channel(io_channel), request_stack(request_stack) {
        start(); 
       };
   ~OsvIoPoller() = default;
   // -------------------------------------------------------------------------------------
   OsvIoPoller(const OsvIoPoller&) = delete;
   OsvIoPoller(OsvIoPoller&&) = delete;
   OsvIoPoller& operator=(const OsvIoPoller&) = delete;
   OsvIoPoller& operator=(OsvIoPoller&&) = delete;
};
}  // namespace mean
