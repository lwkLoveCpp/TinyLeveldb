//

// 预分配大块内存：

// Arena 类通过预分配大块内存来减少频繁的小块内存分配操作。这些大块内存被存储在 blocks_ 向量中。
// 小块内存分配：

// 当需要分配小块内存时，Arena 类首先检查当前预分配的大块内存是否有足够的空间。如果有足够的空间，则直接在当前大块内存中分配小块内存。
// 内存不足处理：

// 如果当前预分配的大块内存没有足够的空间，则调用 AllocateFallback 方法来处理内存不足的情况。AllocateFallback 方法会分配新的大块内存，并在新的大块内存中分配小块内存。
// 对齐分配：

//对于中等内存和大内存需求直接分配相应大小的内存。

// Arena 类还提供了 AllocateAligned 方法，用于分配具有特定对齐要求的内存块。

#pragma once
#include <vector>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>

using namespace std;

static size_t maxSize = 4096;
class Arena{
public:
    Arena();
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    ~Arena(){
        for(auto block : block_){
            delete[] block;
        }
    }
    size_t MemoryUsage() const{
        return using_block.load(std::memory_order_relaxed);
    }

    char* Allocate(size_t need_size){
        if(need_size < alloc_ptr_remain){
            char * result = alloc_ptr;
            alloc_ptr+=need_size;
            alloc_ptr_remain -=need_size;
            return result;
        }
        return alloc_fallback_block(need_size);
    }
    //对其分配法，CPU读取按字读取，一个字可能8字节也可能4字节，我们保证数据在一个字之内就可以提高读取速率，如果数据横跨两个字，那就要读取两次
    char* AllocateAligned(size_t need_size){
        const int align = sizeof(void*);
        size_t mod = reinterpret_cast<uintptr_t>(alloc_ptr) &(align-1);
        size_t need = need_size + (mod==0?0:align-mod);
        if(need < alloc_ptr_remain){
            char* result = alloc_ptr;
            alloc_ptr+=need;
            alloc_ptr_remain-=need;
            return result;
        }
        return alloc_fallback_block(need);
    }
private:
    vector<char*> block_;
    char* alloc_ptr;
    size_t alloc_ptr_remain;
    std::atomic<size_t> using_block;
    char* alloc_new_block(size_t block_size){
        char* new_block = new char[block_size];
        block_.push_back(new_block);
        using_block.fetch_add(block_size + sizeof(char*),
        std::memory_order_relaxed);
        return new_block;
    }
    char* alloc_fallback_block(size_t  need_size){
        char* result;
        if(need_size > maxSize/4){
            result = alloc_new_block(need_size);
            return result;
        }
        result = alloc_new_block(maxSize);
        alloc_ptr = result;
        alloc_ptr+= need_size;
        alloc_ptr_remain = maxSize-need_size;
        return result;
    }
};