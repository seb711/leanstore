#pragma once
#include "leanstore/storage/buffer-manager/BufferFrame.hpp"
#include "Units.hpp"

#include <functional>
#include <list>
#include <unordered_map>

namespace leanstore
{
namespace storage
{
class AsyncWriteBuffer
{
  public:
   virtual void add(BufferFrame& bf, std::function<void(BufferFrame&, u64, PID)> callback, PID pid) = 0;
   virtual u64 submit() = 0;
   virtual u64 pollSync() = 0;
};
}  // namespace storage
}  // namespace leanstore