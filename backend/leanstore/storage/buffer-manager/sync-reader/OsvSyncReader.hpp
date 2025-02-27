#pragma once
#include "SyncReader.hpp"
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
#include <condition_variable>
#include <osv/nvme.hh>
// -------------------------------------------------------------------------------------
namespace leanstore
{
namespace storage
{

class OsvSyncReader : public SyncReader
{
   struct CVRead {
      std::mutex mtx;
      std::condition_variable cv;
      bool ready = false;

      void notify()
      {
         {
            std::unique_lock<std::mutex> lock(mtx);
            ready = true;
         }
         cv.notify_one();
      };
   };

   template <typename T, u32 PoolSize>
   class LockFreeList
   {
     public:
      struct Node {
         T object;
         std::atomic<Node*> next;
         u32 idx = 0;

         T* getObject() { return &object; };
      };

     private:
      alignas(64) std::vector<std::unique_ptr<Node>> storage;
      std::atomic<Node*> free_list;

     public:
      LockFreeList()
      {
         storage.resize(PoolSize);
         for (size_t i = 0; i < PoolSize; i++) {
            storage[PoolSize - i - 1] = std::make_unique<Node>();
            storage[PoolSize - i - 1]->idx = i;
            storage[PoolSize - i - 1]->next = (PoolSize - i < PoolSize) ? storage[PoolSize - i].get() : nullptr;
         }
         free_list.store(storage[0].get());
      }

      Node* acquire()
      {
         Node* node;
         do {
            node = free_list.load();
            if (node == nullptr)
               return nullptr;
         } while (!free_list.compare_exchange_weak(node, node->next.load()));
         return node;
      }

      void release(Node* node)
      {
         assert(node);
         Node* old_head;
         do {
            old_head = free_list.load();
            node->next = old_head;
         } while (!free_list.compare_exchange_strong(old_head, node));
      }
   };

   OsvSyncReader();
   OsvSyncReader(OsvSyncReader &other) = delete;
   void operator=(const OsvSyncReader &) = delete;

  private:
   static OsvSyncReader* singleton_;
   LockFreeList<CVRead, 500> cv_pool;
   void* queue;

   static void completion(void* cb_arg, const nvme_sq_entry_t* sqe)
   {
      auto request = static_cast<CVRead*>(cb_arg);
      request->notify();
   };

  public:
  static OsvSyncReader *getInstance();
   virtual const int syncRead(u8* destination, size_t length, size_t offset) override;
   virtual int pollQueues() override;
};
// -------------------------------------------------------------------------------------
}  // namespace storage
}  // namespace leanstore
// -------------------------------------------------------------------------------------
