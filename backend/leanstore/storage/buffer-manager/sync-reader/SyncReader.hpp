#pragma once
#include <osv/nvme-structs.h>
#include <osv/nvme.hh>

#include "Units.hpp"

#include <condition_variable>
#include <functional>
#include <list>
#include <unordered_map>
// this is used for seperate osv and linux pread -> because pread in osv is slow af

namespace leanstore
{
namespace storage
{

class SyncReader
{
  public:
   virtual const int syncRead(u8* destination, size_t length, size_t offset) = 0;
   virtual int pollQueues() = 0;
};
}  // namespace storage
}  // namespace leanstore