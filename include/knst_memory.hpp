// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
----------------------------
knst_memory.hpp
----------------------------

   knst_pool_allocator is an alternative to malloc that provides fast memory allocation from pre-allocated block pools. It is significantly faster than malloc for small allocations (64–2048 bytes) and falls back to malloc for larger ones. It uses reference counting, so copying does not result in heap duplication. Thread safety is optional (via KNST_MEMORY_POOL_USE_MUTEX). Aliases ending in `_sm`—such as `knst_vector_sm` and `knst_byte_string_sm`—utilize this allocator
    For most basic structures, the special memory feature will be activated via the '_sm' suffix.


*/



#pragma once

#include "knst_settings.hpp"
#include <cstring>
#include <algorithm>
#include <vector>
#include <atomic>
#include <type_traits>





#ifdef KNST_MEMORY_POOL_USE_MUTEX // This macro makes the class thread-safe
    #include <mutex>
#endif


struct knst_default_allocator { // A basic (standard) allocator. Uses HeapAlloc/HeapFree on Windows and malloc/free on POSIX. Returns zero-initialized memory (HEAP_ZERO_MEMORY / memset). rebind ==> same allocator for a different type. pool_count/max_block_size ==> no pool, returns 0. All operator== >>> always equal (stateless). The default allocator for structures like knst_vector<T>

    using value_type = void;
    using size_type = size_t;
    using difference_type = ptrdiff_t;

    template<typename U>
    struct rebind { using other = knst_default_allocator; };

    static KNST_FORCE_INLINE void* allocate(size_t size) {
        #if defined(_WIN32) || defined(_WIN64)
            return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
        #else
            if (size == 0) return nullptr;
            void* ptr = ::malloc(size);
            if (ptr) std::memset(ptr, 0, size);
            return ptr;
        #endif
    }

    static KNST_FORCE_INLINE void deallocate(void* ptr, size_t) {
        if (!ptr) return;
        #if defined(_WIN32) || defined(_WIN64)
            HeapFree(GetProcessHeap(), 0, ptr);
        #else
            ::free(ptr);
        #endif
    }

    static KNST_FORCE_INLINE void* reallocate(void* ptr, size_t new_size) {
        if (!ptr) return allocate(new_size);
        if (new_size == 0) { deallocate(ptr, 0); return nullptr; }
        #if defined(_WIN32) || defined(_WIN64)
            return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ptr, new_size);
        #else
            return ::realloc(ptr, new_size);
        #endif
    }

    size_t pool_count() const noexcept { return 0; }
    size_t max_block_size() const noexcept { return 0; }

    friend bool operator==(const knst_default_allocator&, const knst_default_allocator&) noexcept { return true; }
    friend bool operator!=(const knst_default_allocator&, const knst_default_allocator&) noexcept { return false; }
};


struct knst_pool_config { // Pool configuration. `block_size` ==> byte size of each block; `block_count` ==> number of blocks in the pool. The user defines a custom pool by calling `knst_pool_allocator(knst_pool_config(128, 512), ...)`
    size_t block_size;
    size_t block_count;
    
    constexpr knst_pool_config(size_t bs, size_t bc) noexcept : block_size(bs), block_count(bc) {}
        
};


class knst_pool_allocator {

    static constexpr size_t MIN_BLOCK = sizeof(void*) * 2; // Minimum block size — two pointers (16 bytes on a 64-bit system). Why? The "next" pointer is written into the free block within the free list, so a block must be large enough to hold at least that pointer. A 2x safety margin

    struct block_pool { // A single fixed-block pool. `init` acquires a large `malloc` block and links the internal blocks together into a free list (linked list). `allocate` is O(1): provide the head block and advance the free list. `deallocate` is O(1): add the block back to the head. `owns` checks if a pointer belongs to this pool; `fits` checks if a size fits within this pool. The `malloc` call occurs only once during `init`; subsequent operations involve only list manipulation
        void*  memory = nullptr;
        void*  free_list = nullptr;
        size_t block_size = 0;
        size_t capacity = 0;

        block_pool() = default;
        ~block_pool() { destroy(); }

        block_pool(const block_pool&) = delete;
        block_pool& operator=(const block_pool&) = delete;

        block_pool(block_pool&& o) noexcept : memory(o.memory), free_list(o.free_list), block_size(o.block_size), capacity(o.capacity) {
            o.memory = nullptr; o.free_list = nullptr;
            o.block_size = 0; o.capacity = 0;
        }

        block_pool& operator=(block_pool&& o) noexcept {
            if (this != &o) {
                destroy();
                memory = o.memory; free_list = o.free_list;
                block_size = o.block_size; capacity = o.capacity;
                o.memory = nullptr; o.free_list = nullptr;
                o.block_size = 0; o.capacity = 0;
            }
            return *this;
        }

        bool init(size_t bs, size_t cap) {
            block_size = bs;
            capacity = cap;
            memory = ::malloc(bs * cap);
            if (!memory) return false;

            auto* ptr = static_cast<uint8_t*>(memory);
            for (size_t i = 0; i < cap; ++i) {
                *reinterpret_cast<void**>(ptr) = ptr + bs;
                ptr += bs;
            }
            ptr -= bs;
            *reinterpret_cast<void**>(ptr) = nullptr;
            free_list = memory;
            return true;
        }

        void destroy() {
            if (memory) { ::free(memory); memory = nullptr; free_list = nullptr; }
        }

        void* allocate() noexcept {
            if (!free_list) return nullptr;
            void* blk = free_list;
            free_list = *reinterpret_cast<void**>(blk);
            return blk;
        }

        void deallocate(void* p) noexcept {
            if (!p) return;
            *reinterpret_cast<void**>(p) = free_list;
            free_list = p;
        }

        bool owns(const void* p) const noexcept {
            if (!p || !memory) return false;
            return p >= memory && p < static_cast<const uint8_t*>(memory) + (block_size * capacity);
        }

        bool fits(size_t sz) const noexcept { return sz <= block_size; }
    };

    struct pool_impl { // Manages multiple block pools—accommodating different sizes. It is shared via a reference count and made thread-safe with an optional mutex. `init` handles automatic sizing, `init_with_configs` allows for user-controlled configuration, and `destroy_all` clears everything
        std::vector<block_pool> pools;
        size_t max_block_size = 0;
        std::atomic<size_t> ref_count{1};

        #ifdef KNST_MEMORY_POOL_USE_MUTEX
            mutable std::mutex mtx;
        #endif

        pool_impl() = default;
        ~pool_impl() = default;

        void init(const size_t* sizes, size_t count) {
            std::vector<size_t> uniq(sizes, sizes + count);
            std::sort(uniq.begin(), uniq.end());
            uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());

            max_block_size = 0;
            pools.clear();
            pools.reserve(uniq.size());

            for (size_t bs : uniq) {
                if (bs < MIN_BLOCK) bs = MIN_BLOCK;

                size_t cap = 1024 / bs;
                if (cap < 8)    cap = 8;
                if (cap > 4096) cap = 4096;

                block_pool bp;
                if (bp.init(bs, cap)) {
                    pools.push_back(std::move(bp));
                    max_block_size = bs;
                }
            }
        }

        void init_with_configs(const knst_pool_config* configs, size_t count) {
            max_block_size = 0;
            pools.clear();
            pools.reserve(count);
            std::vector<knst_pool_config> sorted_configs(configs, configs + count);
            std::sort(sorted_configs.begin(), sorted_configs.end(),
                [](const knst_pool_config& a, const knst_pool_config& b) {
                    return a.block_size < b.block_size;
                });

            for (size_t i = 0; i < sorted_configs.size(); ++i) {
                size_t bs = sorted_configs[i].block_size;
                size_t cap = sorted_configs[i].block_count;

                if (bs < MIN_BLOCK) bs = MIN_BLOCK;
                if (cap < 1) cap = 1;

                block_pool bp;
                if (bp.init(bs, cap)) {
                    pools.push_back(std::move(bp));
                    if (bs > max_block_size) max_block_size = bs;
                }
            }
        }

        void destroy_all() {
            for (auto& bp : pools) bp.destroy();
            pools.clear();
            max_block_size = 0;
        }
    };

    pool_impl* m_impl = nullptr; // The allocator's actual data is the `pool_impl` pointer. All copies share it (via `ref_count`), and it is deleted when the last reference is released. It is the size of a single pointer and—thanks to `[[no_unique_address]]`—incurs zero overhead

    void release() noexcept { // It releases the reference. If the counter drops to 1 (the last reference), it deletes `pool_impl`. Otherwise, it simply decrements the counter, as other copies are still using it. It is called within the destructor and `operator=` — there are no leaks
        if (m_impl) {
            if (m_impl->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1)
                delete m_impl;
            m_impl = nullptr;
        }
    }

public:
    using value_type = void; // The allocator's `value_type` — an STL allocator trait. It is `void` because this allocator is type-agnostic and allocates raw bytes. This is required for STL compatibility
    using size_type = size_t; // STL allocator trait. Size type is size_t. Required for standard compliance
    using difference_type = ptrdiff_t; // STL allocator trait. Pointer difference type ptrdiff_t. For standard compliance
    using propagate_on_container_copy_assignment = std::true_type; // An STL trait. It specifies that the allocator is also copied when the container is copied (vec2 = vec1). true ==> the allocator is moved, and the source and destination are linked to the same pool
    using propagate_on_container_move_assignment = std::true_type; // An STL trait. It specifies that the allocator is also moved during move assignment (vec2 = std::move(vec1)). true ==> the source is emptied, and the destination uses its pool
    using propagate_on_container_swap = std::true_type; // An STL trait. It specifies that allocators are also swapped when `swap(vec1, vec2)` is called. `true` ==> pointers are exchanged, and the allocators of both containers remain compatible

    template<typename U>
    struct rebind { using other = knst_pool_allocator; }; // STL allocator trait. `rebind<U>::other` ==> "the `U`-type version of this allocator." Since `knst_pool_allocator` is type-agnostic, `other` is simply the allocator itself. STL containers (list, map, etc.) use this for their internal nodes
   
    knst_pool_allocator() { // Default constructor. Sets up a pool with 4 standard sizes (64, 256, 1024, 2048 bytes). These sizes are available by default unless the user specifies otherwise
        constexpr size_t def[] = {64, 256, 1024, 2048};
        m_impl = new pool_impl();
        m_impl->init(def, 4);
    }

    template<typename... Args, typename = std::enable_if_t<(std::is_integral_v<Args> && ...) &&!(std::is_same_v<Args, knst_pool_config> || ...)>>
    explicit knst_pool_allocator(Args... sizes) { // Constructs with user-defined sizes—such as `knst_pool_allocator(128, 512, 4096)`. Uses `enable_if` to accept only integral arguments, not `knst_pool_config` (which is a separate overload). The sizes are placed into an array and passed to the initializer
        constexpr size_t N = sizeof...(Args);
        size_t arr[N] = { static_cast<size_t>(sizes)... };
        m_impl = new pool_impl();
        m_impl->init(arr, N);
    }

    template<typename... Args>
    explicit knst_pool_allocator(knst_pool_config first, Args... rest) { // It sets things up using `knst_pool_config`. The user specifies both the size and the number of blocks for each pool. It passes these to `init_with_configs`—for example: `knst_pool_allocator(knst_pool_config(128, 512), knst_pool_config(1024, 128))`

        constexpr size_t N = 1 + sizeof...(Args);
        knst_pool_config arr[N] = { first, static_cast<knst_pool_config>(rest)... };
        m_impl = new pool_impl();
        m_impl->init_with_configs(arr, N);
    }


    knst_pool_allocator(const knst_pool_allocator& other) noexcept : m_impl(other.m_impl) { // Copy constructor. Shares the m_impl pointer (does not copy it) and increments the ref_count. In other words, both allocators use the same pool—the heap is not duplicated
        
        if (m_impl)
            m_impl->ref_count.fetch_add(1, std::memory_order_relaxed);
    }


    knst_pool_allocator(knst_pool_allocator&& other) noexcept: m_impl(other.m_impl) { // Move constructor. Steals the pointer and nulls out the source. The reference count does not increase—ownership changes hands. Zero cost
        
        other.m_impl = nullptr;
    }


    knst_pool_allocator& operator=(const knst_pool_allocator& other) noexcept { // Copy assignment. First, it releases its own implementation, then shares the other's implementation and increments the reference count. It includes a self-assignment check
        if (this != &other) {
            release();
            m_impl = other.m_impl;
            if (m_impl)
                m_impl->ref_count.fetch_add(1, std::memory_order_relaxed);
        }
        return *this;
    }

    knst_pool_allocator& operator=(knst_pool_allocator&& other) noexcept { // Move assignment. First, it releases its own implementation; then, it steals the pointer from `other` and sets the source to null. It does not touch the reference count—ownership has changed hands
        if (this != &other) {
            release();
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }
        return *this;
    }


    ~knst_pool_allocator() { release(); } // The destructor calls `release()`—it deletes the pool if it is the last reference; otherwise, it decrements the counter


    KNST_FORCE_INLINE void* allocate(size_t size) const { // Allocates memory. If the requested size exceeds `max_block_size`, it falls back to `malloc`. Otherwise, it queries the appropriate pool; if a free block is available, it returns it in O(1) time. If the pool is exhausted, it falls back to `malloc`. It is thread-safe (provided the mutex is enabled)
        if (size == 0) return nullptr;
        if (!m_impl) return knst_default_allocator::allocate(size);

        #ifdef KNST_MEMORY_POOL_USE_MUTEX
            std::lock_guard<std::mutex> lk(m_impl->mtx);
        #endif

        if (size > m_impl->max_block_size)
            return knst_default_allocator::allocate(size);

        for (auto& bp : m_impl->pools) {
            if (bp.fits(size)) {
                if (void* ptr = bp.allocate())
                    return ptr;
            }
        }

        return knst_default_allocator::allocate(size);
    }

    KNST_FORCE_INLINE void deallocate(void* ptr, size_t size_hint = 0) const { // Returns the memory. If `size_hint` is provided, there is a fast path—it looks directly at the appropriate pool. Otherwise, it iterates through all pools and identifies the owner using `owns`. If it is not found in any of them, it means the memory originated from `malloc`, so it falls through to `free`
        if (!ptr) return;
        if (!m_impl) {
            knst_default_allocator::deallocate(ptr, size_hint);
            return;
        }

        #ifdef KNST_MEMORY_POOL_USE_MUTEX
            std::lock_guard<std::mutex> lk(m_impl->mtx);
        #endif

        if (size_hint > 0) {
            for (auto& bp : m_impl->pools) {
                if (bp.fits(size_hint) && bp.owns(ptr)) {
                    bp.deallocate(ptr);
                    return;
                }
            }
        }

        for (auto& bp : m_impl->pools) {
            if (bp.owns(ptr)) {
                bp.deallocate(ptr);
                return;
            }
        }

        knst_default_allocator::deallocate(ptr, size_hint);
    }

    KNST_FORCE_INLINE void* reallocate(void* ptr, size_t new_size) const { // Reallocation. If shrinking, return the same pointer (no new location). If growing, allocate a new block, copy the old data, and free the old block. If the pointer does not belong to the pool, it falls back to `realloc`. The copy size is the minimum of the old and new sizes
        if (!ptr) return allocate(new_size);
        if (new_size == 0) { deallocate(ptr); return nullptr; }
        if (!m_impl) return knst_default_allocator::reallocate(ptr, new_size);

        #ifdef KNST_MEMORY_POOL_USE_MUTEX
            std::lock_guard<std::mutex> lk(m_impl->mtx);
        #endif

        size_t old_bs = 0;
        for (auto& bp : m_impl->pools) {
            if (bp.owns(ptr)) { old_bs = bp.block_size; break; }
        }

        if (old_bs == 0)
            return knst_default_allocator::reallocate(ptr, new_size);

        if (new_size <= old_bs) return ptr;

        void* new_ptr = nullptr;
        if (new_size <= m_impl->max_block_size) {
            for (auto& bp : m_impl->pools) {
                if (bp.fits(new_size)) {
                    new_ptr = bp.allocate();
                    if (new_ptr) break;
                }
            }
        }
        if (!new_ptr) new_ptr = knst_default_allocator::allocate(new_size);
        if (!new_ptr) return nullptr;

        size_t copy_sz = old_bs < new_size ? old_bs : new_size;
        std::memcpy(new_ptr, ptr, copy_sz);
        deallocate(ptr, old_bs);
        return new_ptr;
    }

    

    void reset() { // Resets the pool—clears all blocks and reinitializes. If shared (ref_count > 1), it creates a new implementation and abandons the old one (so other copies remain unaffected). If not shared, it reinitializes the same implementation from scratch
        constexpr size_t def[] = {64, 256, 1024, 2048};
        if (!m_impl) {
            m_impl = new pool_impl();
            m_impl->init(def, 4);
            return;
        }

        if (m_impl->ref_count.load(std::memory_order_acquire) > 1) {
            auto* new_impl = new pool_impl();
            new_impl->init(def, 4);
            if (m_impl->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1)
                delete m_impl;
            m_impl = new_impl;
        } else {
            m_impl->destroy_all();
            m_impl->init(def, 4);
        }
    }

   
    template<typename... Args, typename = std::enable_if_t<(std::is_integral_v<Args> && ...) &&!(std::is_same_v<Args, knst_pool_config> || ...)>>
    void reset(Args... sizes) { // The version of `reset` that accepts dimensions. The user provides new dimensions (e.g., `reset(128, 512)`), and the pool is re-initialized with those dimensions. The sharing logic remains the same: if `ref_count > 1`, a new implementation is created
        constexpr size_t N = sizeof...(Args);
        size_t arr[N] = { static_cast<size_t>(sizes)... };

        if (!m_impl) {
            m_impl = new pool_impl();
            m_impl->init(arr, N);
            return;
        }

        if (m_impl->ref_count.load(std::memory_order_acquire) > 1) {
            auto* new_impl = new pool_impl();
            new_impl->init(arr, N);
            if (m_impl->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1)
                delete m_impl;
            m_impl = new_impl;
        } else {
            m_impl->destroy_all();
            m_impl->init(arr, N);
        }
    }

    
    template<typename... Args>
    void reset(knst_pool_config first, Args... rest) { // The `knst_pool_config` version of `reset`. The user specifies the size and number of blocks for each pool. The sharing logic remains the same: if `ref_count > 1`, a new implementation is created; otherwise, the existing implementation is reset from scratch.
        constexpr size_t N = 1 + sizeof...(Args);
        knst_pool_config arr[N] = { first, static_cast<knst_pool_config>(rest)... };

        if (!m_impl) {
            m_impl = new pool_impl();
            m_impl->init_with_configs(arr, N);
            return;
        }

        if (m_impl->ref_count.load(std::memory_order_acquire) > 1) {
            auto* new_impl = new pool_impl();
            new_impl->init_with_configs(arr, N);
            if (m_impl->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1)
                delete m_impl;
            m_impl = new_impl;
        } else {
            m_impl->destroy_all();
            m_impl->init_with_configs(arr, N);
        }
    }


    size_t pool_count() const noexcept { // Returns the number of pools. Returns 0 if m_impl is null. Reads under the mutex lock if it is enabled
        if (!m_impl) return 0;
        #ifdef KNST_MEMORY_POOL_USE_MUTEX
            std::lock_guard<std::mutex> lk(m_impl->mtx);
        #endif
        return m_impl->pools.size();
    }

    size_t max_block_size() const noexcept { // Returns the size of the largest pool block. If m_impl does not exist, it is 0
        return m_impl ? m_impl->max_block_size : 0;
    }

  

    friend bool operator==(const knst_pool_allocator& a, const knst_pool_allocator& b) noexcept { // Are the two allocators equal? ​​They are equal if they share the same `pool_impl`—meaning they allocate from the same pool. STL containers rely on this; if allocators are equal, elements can be swapped
        return a.m_impl == b.m_impl;
    }
    friend bool operator!=(const knst_pool_allocator& a, const knst_pool_allocator& b) noexcept { // The inverse of operator==. Returns true if they use different pools
        return !(a == b);
    }
};

static_assert(sizeof(knst_pool_allocator) == sizeof(void*), // Compile-time check. The allocator must be the size of a single pointer (8 bytes on 64-bit systems) so that it can be embedded into classes at zero cost using `[[no_unique_address]]`. If someone accidentally adds a member, the `static_assert` triggers and compilation halts.
    "knst_pool_allocator must be exactly one pointer in size.");

