#pragma once
#include <algorithm>
#include <cstddef>
#include <new>

/*
Fixed-size node pool used by the order book's containers (std::list nodes,
std::unordered_map nodes, std::map nodes).

Why: every resting order used to cost several general-purpose heap allocations
(list node + index node, plus a map node per new price level). A free list of
same-sized slots makes allocate/deallocate a pointer pop/push, and LIFO reuse
hands back memory that is still hot in cache.

Design notes:
  * One pool per node size per thread (thread_local), so no locking. The engine
    is single-threaded; a book must not be shared across threads concurrently.
  * Slots are carved from geometrically growing chunks (256 -> 65,536 slots).
  * Chunks are intentionally never returned to the OS: memory is reused by later
    orders, and it avoids destruction-order hazards with static containers.
  * Requests for more than one object (e.g. hash bucket arrays) go straight to
    ::operator new.
*/
template <std::size_t Size, std::size_t Align>
class FixedPool {
    union Slot {
        Slot* next;
        alignas(Align) unsigned char storage[Size];
    };
    static_assert(alignof(Slot) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__, "over-aligned node type");

    Slot* free_ = nullptr;
    std::size_t nextChunk_ = 256;

    void refill() {
        auto* chunk = static_cast<Slot*>(::operator new(nextChunk_ * sizeof(Slot)));
        for (std::size_t i = 0; i + 1 < nextChunk_; ++i) chunk[i].next = &chunk[i + 1];
        chunk[nextChunk_ - 1].next = nullptr;
        free_ = chunk;
        nextChunk_ = std::min<std::size_t>(nextChunk_ * 2, 65'536);
    }

public:
    void* allocate() {
        if (!free_) refill();
        Slot* s = free_;
        free_ = s->next;
        return s;
    }
    void deallocate(void* p) noexcept {
        Slot* s = static_cast<Slot*>(p);
        s->next = free_;
        free_ = s;
    }
    static FixedPool& instance() {
        thread_local FixedPool* pool = new FixedPool(); // deliberately leaked, see notes
        return *pool;
    }
};

template <typename T>
class PoolAllocator {
public:
    using value_type = T;

    PoolAllocator() noexcept = default;
    template <typename U>
    PoolAllocator(const PoolAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        if (n != 1) return static_cast<T*>(::operator new(n * sizeof(T)));
        return static_cast<T*>(FixedPool<sizeof(T), alignof(T)>::instance().allocate());
    }
    void deallocate(T* p, std::size_t n) noexcept {
        if (n != 1) {
            ::operator delete(p);
            return;
        }
        FixedPool<sizeof(T), alignof(T)>::instance().deallocate(p);
    }

    // stateless: any instance can free memory from any other
    template <typename U>
    bool operator==(const PoolAllocator<U>&) const noexcept { return true; }
    template <typename U>
    bool operator!=(const PoolAllocator<U>&) const noexcept { return false; }
};
