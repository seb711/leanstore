#include "JumpMU.hpp"
//#include "TaskManager.hpp"

#include <signal.h>
// -------------------------------------------------------------------------------------
namespace jumpmu
{
__thread JumpMUContext* thread_local_jumpmu_ctx __attribute__ ((tls_model("local-exec"))) = nullptr;
}  // namespace jumpmu
