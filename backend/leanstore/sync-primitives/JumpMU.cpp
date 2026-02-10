#include "JumpMU.hpp"
//#include "TaskManager.hpp"

#include <signal.h>
// -------------------------------------------------------------------------------------
namespace jumpmu
{
thread_local JumpMUContext* thread_local_jumpmu_ctx __attribute__ ((tls_model("initial-exec"))) = nullptr;
#ifdef NEW_JUMPMU
thread_local JumpMUContext thread_local_jumpmu __attribute__ ((tls_model("initial-exec"))); 
#endif
}  // namespace jumpmu