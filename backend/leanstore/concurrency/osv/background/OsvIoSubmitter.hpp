#pragma once

#include "OsvBackgroundThreadBase.hpp"
#include "leanstore/io/RequestStackLockfree.hpp"
#include "leanstore/io/IoChannel.hpp"
#include "leanstore/Config.hpp"
#include <osv/leanstore_debug.hh>
namespace mean
{

template <typename TImplRequest, typename TIoChannel>
class OsvIoSubmitter : public OsvBackgroundThreadBase
{
  private:
   IoChannel& abstraction_io_channel; 
  TIoChannel& io_channel; 
  RequestStackLockfree<RaidRequest<TImplRequest>>& request_stack;
   size_t counter = 0; 
    // will poll and submit
   unsigned getPriority() override {
       
    // request_stack.pushed stores the io_requests that could be submitted but are not submitted yet
    // policy: run it if more than 1/4 of the queue size is used
    // attention: could starve if at some point no more items are pushed (should not happen in leanstore)
    volatile auto submitStackSize = abstraction_io_channel.submitable();
    volatile auto ioOutstanding =  io_channel.outstanding[0]; 
    // return (submitStackSize > FLAGS_background_batching && ioOutstanding < 127) ? 5 : 0; 
   // return (submitStackSize > 0) || (writeRequestStackSize > 0 && ioOutstanding < 63) ? 5 : 0; 
    // return counter++ > 3; // has_n_completion_entries(io_channel.qpairs[0], FLAGS_background_batching) ? 5 : 0; 
    leanstore_osv_debug::trace_submitter_state(submitStackSize); 
    return submitStackSize > FLAGS_background_batching  ? 5 : 0; // || tscDifferenceUs(readTSC(), counter) >= 2500
   }; 
   // will poll and submit
   int process() override {
    while (true) {
        counter = readTSC(); 
       // this is it for now
       // maybe we also need two threads for submit and for polling
       // submit depends on the request_stack
       // poll depends on the io_channel
       leanstore_osv_debug::trace_io_channel_state( io_channel.outstanding[0], io_channel.submitable.load());

              // leanstore_osv_debug::trace_background_result(abstraction_io_channel.submit());
        abstraction_io_channel.submit(); 
       leanstore_osv_debug::yield(); 
    }
    return 0;
 };

   public: 
   // -------------------------------------------------------------------------------------
   OsvIoSubmitter(IoChannel& abstraction_io_channel, TIoChannel& io_channel, RequestStackLockfree<RaidRequest<TImplRequest>>& request_stack, int id) : OsvBackgroundThreadBase("io_submitter", sched::thread_background::io_submitter, id), abstraction_io_channel(abstraction_io_channel), io_channel(io_channel), request_stack(request_stack) {
    start_background_work(); 
   };
   ~OsvIoSubmitter() = default;
   // -------------------------------------------------------------------------------------
   OsvIoSubmitter(const OsvIoSubmitter&) = delete;
   OsvIoSubmitter(OsvIoSubmitter&&) = delete;
   OsvIoSubmitter& operator=(const OsvIoSubmitter&) = delete;
   OsvIoSubmitter& operator=(OsvIoSubmitter&&) = delete;
};
}  // namespace mean
