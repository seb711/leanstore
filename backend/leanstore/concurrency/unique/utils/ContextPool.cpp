#include "ContextPool.hpp"
#include "leanstore/concurrency/Task.hpp"
#include <stdexcept>
#include <cstdlib>

namespace mean {

// MemoryPool implementation
template<size_t BlockSize>
MemoryPool<BlockSize>::MemoryPool(size_t capacity) : capacity_(capacity) {
    free_list_.reserve(capacity);
    all_blocks_.reserve(capacity);
    for (size_t i = 0; i < capacity; i++) {
        void* block = aligned_alloc(64, BlockSize);
        if (!block) throw std::bad_alloc();
        all_blocks_.push_back(block);
        free_list_.push_back(block);
    }
}

template<size_t BlockSize>
MemoryPool<BlockSize>::~MemoryPool() {
    for (void* block : all_blocks_) {
        free(block);
    }
}

template<size_t BlockSize>
void* MemoryPool<BlockSize>::allocate() {
    if (free_list_.empty()) throw std::runtime_error("MemoryPool exhausted");
    void* block = free_list_.back();
    free_list_.pop_back();
    return block;
}

template<size_t BlockSize>
void MemoryPool<BlockSize>::deallocate(void* ptr) {
    if (!ptr) return;
    free_list_.push_back(ptr);
}

// Explicit template instantiations
template class MemoryPool<8192>;
template class MemoryPool<sizeof(jumpmu::JumpMUContext)>;

// TaskContextPool implementation
TaskContextPool::TaskContextPool(size_t capacity)
    : stack_pool_(capacity),
      interrupt_stack_pool_(capacity),
      jumpmu_pool_(capacity) {}

void TaskContextPool::allocate(UniqueTaskContext& ctx) {
    ctx.stack = stack_pool_.allocate();
    ctx.interrupt_stack = interrupt_stack_pool_.allocate();
    ctx.jumpmuctx = (jumpmu::JumpMUContext*) jumpmu_pool_.allocate(); 
    new (ctx.jumpmuctx) jumpmu::JumpMUContext();  // Placement new
}

void TaskContextPool::deallocate(UniqueTaskContext& ctx) {
    if (ctx.stack) {
        stack_pool_.deallocate(ctx.stack);
        ctx.stack = nullptr;
    }
    if (ctx.interrupt_stack) {
        interrupt_stack_pool_.deallocate(ctx.interrupt_stack);
        ctx.interrupt_stack = nullptr;
    }
    ctx.jumpmuctx->~JumpMUContext();  // Explicit destructor call
    jumpmu_pool_.deallocate(ctx.jumpmuctx); 
    ctx.jumpmuctx = nullptr; 
}

size_t TaskContextPool::available() const {
    return std::min({stack_pool_.available(),
                    interrupt_stack_pool_.available(),
                    jumpmu_pool_.available()});
}

} // namespace mean