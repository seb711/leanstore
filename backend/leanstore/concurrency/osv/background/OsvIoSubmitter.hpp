#pragma once

#include "OsvBackgroundThreadBase.hpp"
#include "leanstore/io/RequestStackLockfree.hpp"
#include "leanstore/io/IoChannel.hpp"
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

    // will poll and submit
   unsigned getPriority() override {
    // request_stack.pushed stores the io_requests that could be submitted but are not submitted yet
    // policy: run it if more than 1/4 of the queue size is used
    // attention: could starve if at some point no more items are pushed (should not happen in leanstore)
    volatile auto submitStackSize = request_stack.submitStackSize();
    volatile auto writeRequestStackSize = io_channel.write_request_stack.size();
    volatile auto ioOutstanding =  io_channel.outstanding[0]; 
    volatile auto desired = request_stack.max_entries / 8;
    // std::cout << "[io submit] submitStackSize: " << submitStackSize << " writeRequestStackSize: " << writeRequestStackSize << " ioOutstanding " << ioOutstanding
	 //   	<< ", desired: " << desired << std::endl;
    return (submitStackSize > 32) || (writeRequestStackSize > 0 && ioOutstanding < 63) ? 5 : 0; 
 }; 
   // will poll and submit
   int process() override {
    while (true) {
       // this is it for now
       // maybe we also need two threads for submit and for polling
       // submit depends on the request_stack
       // poll depends on the io_channel
       abstraction_io_channel.submit();
       leanstore_osv_debug::yield(); 
    }
    return 0;
 };

   public: 
   // -------------------------------------------------------------------------------------
   OsvIoSubmitter(IoChannel& abstraction_io_channel, TIoChannel& io_channel, RequestStackLockfree<RaidRequest<TImplRequest>>& request_stack, int id) : OsvBackgroundThreadBase("io_submitter", sched::thread_background::io_submitter, id), abstraction_io_channel(abstraction_io_channel), io_channel(io_channel), request_stack(request_stack) {
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
