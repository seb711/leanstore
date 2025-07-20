// -------------------------------------------------------------------------------------
#include "ThreadBase.hpp"
// -------------------------------------------------------------------------------------
namespace mean
{
thread_local ThreadBase* _this_thread  __attribute__ ((tls_model("local-exec")))  = nullptr;

}
