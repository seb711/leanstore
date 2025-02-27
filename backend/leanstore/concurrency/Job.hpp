#include <functional>
#include "leanstore/sync-primitives/JumpMU.hpp"
#include <boost/pool/object_pool.hpp>

namespace mean
{
using JobFunction =  std::function<void(u64, std::atomic<bool>&)>;  // std::add_pointer_t<void()>;
struct JobArguments {
   boost::object_pool<Job>* pool; 
   u64 key; 
   std::atomic<bool>& cancleable; 
}; 


enum class JobState {
   Ready,
   BlockingDone,
   Done,
};

// -------------------------------------------------------------------------------------
class Job
{
   const u64 start; 
   jumpmu::JumpMUContext jumpctx;
   JobFunction fun;
   JobState state = JobState::Ready;
   JobArguments args; 

  public:
  Job(JobFunction fun, JobArguments args) : fun(fun), args(args), start(readTSC()) {}
   ~Job();
   // -------------------------------------------------------------------------------------
   JobState getState();
};
};  // namespace mean