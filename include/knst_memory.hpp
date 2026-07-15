#ifndef KNST_MEMORY_HPP
#define KNST_MEMORY_HPP
#pragma once

#include "knst_settings.hpp"
#include <cstring>
#include <algorithm>
#include <vector>
#include <atomic>
#include <type_traits>


// KNST_MEMORY_POOL_USE_MUTEX: This macro makes the class thread-safe


#ifdef KNST_MEMORY_POOL_USE_MUTEX
    #include <mutex>
#endif

// ===================================================================
// DEFAULT ALLOCATOR (heap)
// ===================================================================
struct knst_default_allocator {
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

// ===================================================================
// POOL CONFIGURATION STRUCT
// ===================================================================
struct knst_pool_config {
    size_t block_size;
    size_t block_count;
    
    constexpr knst_pool_config(size_t bs, size_t bc) noexcept : block_size(bs), block_count(bc) {}
        
};

// ===================================================================
// MEMORY POOL ALLOCATOR
// ===================================================================
class knst_pool_allocator {

    static constexpr size_t MIN_BLOCK = sizeof(void*) * 2;

    struct block_pool {
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

    struct pool_impl {
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

            for (size_t i = 0; i < count; ++i) {
                size_t bs = configs[i].block_size;
                size_t cap = configs[i].block_count;

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

    pool_impl* m_impl = nullptr;

    void release() noexcept {
        if (m_impl) {
            if (m_impl->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1)
                delete m_impl;
            m_impl = nullptr;
        }
    }

public:
    using value_type = void;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;

    template<typename U>
    struct rebind { using other = knst_pool_allocator; };

    // ─── CONSTRUCTORS ───────────────────────────────────────

    

    // Default constructor
    knst_pool_allocator() {
        constexpr size_t def[] = {64, 256, 1024, 2048};
        m_impl = new pool_impl();
        m_impl->init(def, 4);
    }

    template<typename... Args, typename = std::enable_if_t<(std::is_integral_v<Args> && ...) &&!(std::is_same_v<Args, knst_pool_config> || ...)>>
    explicit knst_pool_allocator(Args... sizes) {
        constexpr size_t N = sizeof...(Args);
        size_t arr[N] = { static_cast<size_t>(sizes)... };
        m_impl = new pool_impl();
        m_impl->init(arr, N);
    }

    template<typename... Args>
    explicit knst_pool_allocator(knst_pool_config first, Args... rest) {
        constexpr size_t N = 1 + sizeof...(Args);
        knst_pool_config arr[N] = { first, static_cast<knst_pool_config>(rest)... };
        m_impl = new pool_impl();
        m_impl->init_with_configs(arr, N);
    }

    // Copy constructor
    knst_pool_allocator(const knst_pool_allocator& other) noexcept : m_impl(other.m_impl) {
        
        if (m_impl)
            m_impl->ref_count.fetch_add(1, std::memory_order_relaxed);
    }

    // Move constructor
    knst_pool_allocator(knst_pool_allocator&& other) noexcept: m_impl(other.m_impl) {
        
        other.m_impl = nullptr;
    }

    // ─── ASSIGNMENT ─────────────────────────────────────────

    knst_pool_allocator& operator=(const knst_pool_allocator& other) noexcept {
        if (this != &other) {
            release();
            m_impl = other.m_impl;
            if (m_impl)
                m_impl->ref_count.fetch_add(1, std::memory_order_relaxed);
        }
        return *this;
    }

    knst_pool_allocator& operator=(knst_pool_allocator&& other) noexcept {
        if (this != &other) {
            release();
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }
        return *this;
    }

    // ─── DESTRUCTOR ─────────────────────────────────────────

    ~knst_pool_allocator() { release(); }

    // ─── ALLOCATE / DEALLOCATE / REALLOCATE ─────────────────

    KNST_FORCE_INLINE void* allocate(size_t size) const {
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

    KNST_FORCE_INLINE void deallocate(void* ptr, size_t size_hint = 0) const {
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

    KNST_FORCE_INLINE void* reallocate(void* ptr, size_t new_size) const {
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

    // ─── RESET ──────────────────────────────────────────────

    void reset() {
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

   
    template<typename... Args, 
            typename = std::enable_if_t<(std::is_integral_v<Args> && ...) &&
                                        !(std::is_same_v<Args, knst_pool_config> || ...)>>
    void reset(Args... sizes) {
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

    // knst_pool_config
    template<typename... Args>
    void reset(knst_pool_config first, Args... rest) {
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
    // ─── INFO ───────────────────────────────────────────────

    size_t pool_count() const noexcept {
        if (!m_impl) return 0;
        #ifdef KNST_MEMORY_POOL_USE_MUTEX
            std::lock_guard<std::mutex> lk(m_impl->mtx);
        #endif
        return m_impl->pools.size();
    }

    size_t max_block_size() const noexcept {
        return m_impl ? m_impl->max_block_size : 0;
    }

    // ─── COMPARISON ─────────────────────────────────────────

    friend bool operator==(const knst_pool_allocator& a, const knst_pool_allocator& b) noexcept {
        return a.m_impl == b.m_impl;
    }
    friend bool operator!=(const knst_pool_allocator& a, const knst_pool_allocator& b) noexcept {
        return !(a == b);
    }
};

static_assert(sizeof(knst_pool_allocator) == sizeof(void*),
    "knst_pool_allocator must be exactly one pointer in size.");

#endif // KNST_MEMORY_HPP