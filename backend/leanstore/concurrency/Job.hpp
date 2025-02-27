#include <functional>
#include <osv/jumpmu.hh>
#include <boost/pool/object_pool.hpp>
#include <atomic>
#include "Time.hpp"

namespace mean
{

class Job; 

using JobFunction =  std::function<void(u64, std::atomic<bool>&)>;  // std::add_pointer_t<void()>;
struct JobArguments {
   boost::object_pool<Job>* pool; 
   uint64_t key; 
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
public: 
   const u64 start; 
   jumpmu::JumpMUContext jumpctx;
   JobFunction fun;
   JobState state = JobState::Ready;
   JobArguments args; 

  public:
  Job(JobFunction fun, JobArguments args) : start(mean::readTSC()), fun(fun), args(args) {}
   ~Job();
   // -------------------------------------------------------------------------------------
   JobState getState();
};
};  // namespace mean