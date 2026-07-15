# knst_memory — Usage Guide (English)

`knst_default_allocator` and `knst_pool_allocator` — two allocator types usable by `knst_c16string` (and other knst components).


---

## 1. `knst_default_allocator` — Classic Heap Allocator

Stateless, a thin wrapper around `malloc/free/realloc`. All its functions are `static`, so no instance is needed.

```cpp
// Allocates memory (malloc + memset(0) on Linux/macOS, HeapAlloc + HEAP_ZERO_MEMORY on Windows)
void* p = knst_default_allocator::allocate(128);   // 128 bytes, zero-initialized

// Frees memory (the second parameter is unused, kept only for interface consistency)
knst_default_allocator::deallocate(p, 128);

// Resizes
void* p2 = knst_default_allocator::allocate(64);
p2 = knst_default_allocator::reallocate(p2, 256);  // grows to 256 bytes
knst_default_allocator::reallocate(p2, 0);          // new_size==0 -> frees, returns nullptr
```
*Note:* `allocate(0)` returns `nullptr` on Linux/macOS (Windows' `HeapAlloc` behavior is platform-dependent). Calling `reallocate(nullptr, n)` is equivalent to `allocate(n)`.

```cpp
knst_default_allocator a1, a2;
a1.pool_count();       // 0 (no pool concept, always returns 0)
a1.max_block_size();   // 0 (same reason)
a1 == a2;               // true (stateless, so always considered equal)
a1 != a2;               // false
```
*Note:* `pool_count()`/`max_block_size()` exist purely for interface compatibility (to share the same API surface as `knst_pool_allocator`); they carry no meaningful information for `knst_default_allocator`.

---

## 2. `knst_pool_allocator` — Pool-Based Allocator

Uses pools of fixed-size blocks. It's a thin, reference-counted (`ref_count`) wrapper around a shareable `pool_impl*` — the object itself is exactly **one pointer in size** (`static_assert(sizeof(knst_pool_allocator) == sizeof(void*))`).

### 2.1 Construction

```cpp
knst_pool_allocator pa1;                     // Default sizes: {64, 256, 1024, 2048}
knst_pool_allocator pa2(32, 128, 512);        // Custom block sizes (must be integral types)
knst_pool_allocator pa3(pa1);                 // Copy: shares the same pool_impl (ref_count++)
knst_pool_allocator pa4(std::move(pa1));      // Move: takes over the pool_impl pointer, pa1 becomes empty
knst_pool_allocator string_pool(
    knst_pool_config{32, 500},    // A large number of small strings (names, tags)
    knst_pool_config{128, 200},   // Medium-length strings (file paths)
    knst_pool_config{512, 50}     // A small number of large strings (JSON/XML)
); // It is sufficient to fill in the details within the custom `knst_pool_config` ; the first parameter is the block size, and the second parameter is the number of blocks.
```
*Note:* The variadic constructor is `explicit`; implicit conversions like `knst_pool_allocator pa = 64;` won't work — you must write `knst_pool_allocator pa(64);` directly. Also, the parameters must be **integral types** (enforced via `std::is_integral_v`).

### 2.2 Pool Setup — What Happens Internally?

```cpp
knst_pool_allocator pa(10, 300, 300, 2000);
// 1) The given sizes are sorted and deduplicated -> {10, 300, 2000}
// 2) Any size below MIN_BLOCK (sizeof(void*)*2, usually 16 bytes) is bumped up to 16 -> {16, 300, 2000}
// 3) Each pool's capacity = clamp(1024 / block_size, 8, 4096)
pa.pool_count();       // 3
pa.max_block_size();   // 2000
```

### 2.3 Allocate / Deallocate / Reallocate

```cpp
knst_pool_allocator pa; // {64, 256, 1024, 2048}

void* p1 = pa.allocate(50);     // returns a block from the 64-byte pool (50 <= 64 fits)
void* p2 = pa.allocate(5000);   // 5000 > max_block_size(2048) -> falls back to knst_default_allocator

pa.deallocate(p1, 50);          // giving a size_hint finds the right pool quickly
pa.deallocate(p2, 5000);        // no pool owns it (owns() is false) -> freed via default_allocator

void* p3 = pa.allocate(60);
p3 = pa.reallocate(p3, 60);     // new size fits within the old block -> same pointer returned, no copy
p3 = pa.reallocate(p3, 500);    // doesn't fit -> a new block is taken from the appropriate pool, data is memcpy'd, old block is freed
```
*Note:* `allocate(size)` tries to get a free block from the **first pool that fits** (pools are ordered smallest to largest); if that pool has no free blocks left (it's full), it moves on to the **next larger pool that fits**. If no pool has room, or `size` exceeds the largest pool, it falls back to `knst_default_allocator`.

### 2.4 Sharing (Reference Counting) Behavior

```cpp
knst_pool_allocator a;
knst_pool_allocator b = a;      // Copy: shares the same pool_impl, ref_count = 2

a == b;                         // true (they point to the same pool_impl)

void* p = a.allocate(100);      // Memory allocated via a is visible to / freeable by b too
b.deallocate(p, 100);           // Valid, since they share the same pools
```

### 2.5 `reset()` — Shared vs. Non-Shared Behavior

```cpp
knst_pool_allocator a;
knst_pool_allocator b = a;       // shared: ref_count = 2

a.reset(16, 64, 256);           // Since a is SHARED: a brand-new pool_impl is created,
                                 // the old (shared) pool_impl is left UNTOUCHED.
                                 // a now points to a different pool, b still points to the old one.
a == b;                          // false (now different pool_impl instances)

knst_pool_allocator c;            // not shared: ref_count = 1
c.reset(8, 32);                  // Since c is NOT shared: the existing pool_impl is destroyed
                                  // and rebuilt in place. Same pool_impl object is kept.
```
*Note:* This is a copy-on-write-like safety behavior — whoever calls `reset()` always ends up with an independent pool (or one it already exclusively owns), never silently mutating a pool that other allocators are still relying on.

### 2.6 Thread-Safety

```cpp
// #define KNST_MEMORY_POOL_USE_MUTEX   // must be defined BEFORE the header is included , Or it must be defined at the very top of knst_settings.hpp
```
*Note:* If this macro is defined, a `std::mutex` is added inside `pool_impl`, and `allocate`/`deallocate`/`reallocate`/`pool_count` calls are guarded by it. If it's **not** defined, `knst_pool_allocator` is **not** thread-safe — using two `knst_pool_allocator` copies that share the same pool from different threads concurrently causes a data race.

### 2.7 Size Guarantee

```cpp
static_assert(sizeof(knst_pool_allocator) == sizeof(void*),
    "knst_pool_allocator must be exactly one pointer in size.");
```
*Note:* This guarantees `knst_pool_allocator` adds no extra size when stored as a member (e.g., inside `knst_c16string` via `[[no_unique_address]]`)

---

## Summary Table

| Function | `knst_default_allocator` | `knst_pool_allocator` |
|---|---|---|
| `allocate(size)` | Always allocates from the heap (zeroed). | Allocates from a fitting pool, falls back to heap if none fits. |
| `deallocate(ptr, size_hint)` | Direct `free`. | Returns the block to its owning pool, or `free`s it otherwise. |
| `reallocate(ptr, new_size)` | `realloc`. | Grows smartly within pool bounds, or allocates a new block + `memcpy` when exceeding them. |
| `pool_count()` | Always `0`. | Number of configured pools. |
| `max_block_size()` | Always `0`. | Block size of the largest pool. |
| `operator==` | Always `true` (stateless). | `true` if they point to the same `pool_impl`. |
| Copy cost | Zero (empty struct). | Cheap (pointer copy + atomic ref_count increment). |
| Thread-safety | Inherently safe since it's stateless. | Only safe if `KNST_MEMORY_POOL_USE_MUTEX` is defined. |