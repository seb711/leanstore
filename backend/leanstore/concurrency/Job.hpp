#include <functional>
#include <osv/jumpmu.hh>
#include <atomic>
#include "Time.hpp"

#define JOB_QUEUE_SIZE (64)

namespace mean
{

   template <typename T, size_t PoolSize>
class LockFreeObjectPool
{
public:
    struct Node
    {
        T object;
        std::atomic<Node *> next;
        u32 idx = 0;

        T *getObject() { return &object; };
    };

private:
    alignas(64) std::vector<std::unique_ptr<Node>> storage;
    std::atomic<Node *> free_list;
    std::atomic<u64> elements = {0}; 

public:
    LockFreeObjectPool()
    {
        storage.resize(PoolSize);
        for (size_t i = 0; i < PoolSize; i++)
        {
            storage[PoolSize - i - 1] = std::make_unique<Node>();
            storage[PoolSize - i - 1]->idx = i;
            storage[PoolSize - i - 1]->next = (PoolSize - i < PoolSize) ? storage[PoolSize - i].get() : nullptr;
        }
        free_list.store(storage[0].get());
    }

    u64 getSize() {
        return elements.load(); 
    }

    Node *acquire()
    {
        Node *node;
        do
        {
            node = free_list.load();
            if (node == nullptr)
                return nullptr;
        } while (!free_list.compare_exchange_weak(node, node->next.load()));
        elements += 1; 
        return node;
    }

    void release(Node *node)
    {
        assert(node);
        Node *old_head;
        do
        {
            old_head = free_list.load();
            node->next = old_head;
        } while (!free_list.compare_exchange_weak(old_head, node));
        elements -= 1; 
    }
}; 

class Job; 

using JobFunction =  std::function<void(u64, std::atomic<bool>&)>;  // std::add_pointer_t<void()>;
using CallbackFunction =  std::function<void(Job*)>;  // std::add_pointer_t<void()>;

struct JobArguments {
   LockFreeObjectPool<Job, JOB_QUEUE_SIZE>* pool; 
   uint64_t key; 
   std::atomic<bool> cancelable; 

   JobArguments() : pool(nullptr), key(0), cancelable({false}) {}; 
}; 

// -------------------------------------------------------------------------------------
class Job
{
public: 
   jumpmu::JumpMUContext jumpctx;
   JobFunction* fun = nullptr;
   JobArguments args; 

  public:
  Job() : args() {}; 
   // -------------------------------------------------------------------------------------
};
};  // namespace mean