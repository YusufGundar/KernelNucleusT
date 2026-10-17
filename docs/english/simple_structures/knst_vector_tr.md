# knst_vector — Dynamic Array Class

Hello! 👋 This document explains what the `knst_vector` class is, how to use it, and why it exists in KernelNucleusT.

In short: **a dynamic array class similar to `std::vector`, but allocator-aware, `[[no_unique_address]]`-enabled, and exception-free.** You can use a custom allocator (including the pool allocator), it returns `bool` on error instead of throwing exceptions, and with the pool allocator it gives serious speedups for small vectors.

---

## 🎯 Why Does This Class Exist?

`std::vector` is a cornerstone of the C++ standard, but it has some gaps:

- **Throws exceptions** — you are forced to catch `bad_alloc`
- **Allocator propagation is complex** — unclear when it is copied vs. moved
- **No pool allocator support** — every `push_back` calls `malloc`
- **Does not use `[[no_unique_address]]`** — even an empty allocator takes space

`knst_vector` solves these:

- ✅ **Exception-free** — returns `false` on error
- ✅ **Allocator-aware** — serious speedups with `knst_pool_allocator`
- ✅ **`[[no_unique_address]]`** — empty allocator takes 0 bytes
- ✅ **`_sm` suffix** — a pool-backed version ready to go
- ✅ **Bridge memory** — move to a different allocator
- ✅ **Custom iterators** — compatible with `knst_iterator`
- ✅ **`emplace_back`** — perfect forwarding

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Exception-free** | Returns `false` instead of `bad_alloc` |
| **`[[no_unique_address]]`** | Empty allocator takes zero space (C++20) |
| **Pool allocator** | Serious speedups for small vectors |
| **`_sm` version** | `knst_vector_sm<T>` is pool-backed |
| **`emplace_back`** | In-place construction, no copies |
| **Self-reference safe** | `v.push_back(v[0])` is safe |
| **Rich insert/erase** | Iterator, index, range, count — all present |
| **`find` / `erase_value`** | Value-based search/erase |
| **`shrink_to_fit`** | Shrink capacity, save memory |
| **Bridge memory** | Move to a different allocator |
| **Custom iterators** | STL-compatible, random access |

---

## 🚀 Quick Start

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // Empty vector
    knst_vector<int> v;

    // Add elements
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);

    // Iterate
    for (int x : v) {
        std::cout << x << " ";   // 10 20 30
    }
    std::cout << "\n";

    return 0;
}
```

---

## 📋 Overview API

| Category | Methods |
|---|---|
| **Constructors** | Default, `(count, value)`, `initializer_list`, `(ptr, count)`, copy, move |
| **Adding** | `push_back`, `emplace_back`, `insert` (6 overloads) |
| **Erasing** | `pop_back`, `erase` (iterator/index/range), `erase_value` |
| **Access** | `operator[]`, `data`, `back` |
| **Search** | `find` |
| **Size** | `size`, `capacity`, `empty` |
| **Resizing** | `resize`, `reserve`, `shrink_to_fit`, `clear`, `assign` |
| **Comparison** | `==`, `!=` |
| **Iterators** | `begin`, `end`, `cbegin`, `cend` |
| **Allocator** | `bridge_memory` (3 overloads) |

---

## 🏗️ 1) Constructors

### Default Constructor

```cpp
knst_vector<int> v;   // empty, capacity 0
```

### `(count, value)` — N Copies

```cpp
knst_vector<int> v(5, 42);   // {42, 42, 42, 42, 42}
```

### `initializer_list` — Braces

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};
```

### `(ptr, count)` — From a C Array

```cpp
const int raw[] = {10, 20, 30};
knst_vector<int> v(raw, 3);   // {10, 20, 30}
```

### Copy / Move

```cpp
knst_vector<int> a = {1, 2, 3};

knst_vector<int> b = a;              // copy
knst_vector<int> c = std::move(a);   // move — a becomes empty

std::cout << a.size() << "\n";   // 0
std::cout << c.size() << "\n";   // 3
```

### With Allocator

```cpp
knst_pool_allocator pool;
knst_vector<int> v(pool);   // uses the pool
```

---

## 📥 2) Adding Elements

### `push_back(value)` — Append

```cpp
knst_vector<int> v;

v.push_back(10);
v.push_back(20);
v.push_back(30);

// {10, 20, 30}
```

**Copy:** The value is copied (`const T&` overload)
**Move:** Moved via `std::move` (`T&&` overload)

### `emplace_back(args...)` — In-Place Construction

```cpp
struct Point { int x, y; Point(int x, int y) : x(x), y(y) {} };

knst_vector<Point> v;
v.emplace_back(10, 20);   // constructs Point(10, 20) directly — NO copy
```

**The gain:** No temporary object; the target memory is built in place.

### `insert(pos, value)` — Insert in the Middle

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};

// At an iterator position
v.insert(v.begin() + 2, 99);   // {1, 2, 99, 3, 4, 5}

// At an index
v.insert(0, 0);   // {0, 1, 2, 99, 3, 4, 5}
```

### `insert(pos, count, value)` — Insert N Copies

```cpp
knst_vector<int> v = {1, 2, 3};
v.insert(v.begin(), 3, 0);   // {0, 0, 0, 1, 2, 3}
```

### `insert(pos, first, last)` — Insert a Range

```cpp
knst_vector<int> src = {10, 20, 30};
knst_vector<int> dst = {1, 2};

dst.insert(dst.end(), src.begin(), src.end());
// {1, 2, 10, 20, 30}
```

### `insert(pos, initializer_list)` — Braces

```cpp
knst_vector<int> v = {1, 2};
v.insert(v.end(), {3, 4, 5});   // {1, 2, 3, 4, 5}
```

**Note:** All `insert` overloads protect against self-reference:

```cpp
knst_vector<int> v = {1, 2, 3};
v.insert(v.begin(), v[0]);   // ✅ safe — takes a copy
```

---

## 📤 3) Erasing Elements

### `pop_back()` — Erase from the Back

```cpp
knst_vector<int> v = {1, 2, 3};
v.pop_back();   // {1, 2}
```

**Note:** Capacity **does not change**, only `size` shrinks.

### `erase(pos)` — Erase via Iterator

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};
auto it = v.erase(v.begin() + 2);   // 3 erased
std::cout << *it << "\n";   // 4
// {1, 2, 4, 5}
```

**Return:** Iterator pointing to the position of the erased element.

### `erase(index)` — Erase via Index

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};
v.erase(1);   // index 1 = 2 erased
// {1, 3, 4, 5}
```

### `erase(first, last)` — Erase a Range

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};
v.erase(v.begin(), v.begin() + 3);   // first 3 elements erased
// {4, 5}
```

### `erase_value(value)` — Erase by Value

```cpp
knst_vector<int> v = {10, 20, 30, 40, 30};
bool removed = v.erase_value(30);   // erases the first match
// {10, 20, 40, 30}
```

**Return:** `true` = found and erased, `false` = not found.

### `clear()` — Erase Everything

```cpp
knst_vector<int> v = {1, 2, 3};
v.clear();
// size = 0, capacity unchanged
```

---

## 🔍 4) Access

### `operator[]` — Fast Access (No Bounds Check)

```cpp
knst_vector<int> v = {100, 200, 300};

int x = v[0];    // 100
v[1] = 999;      // modify
```

**⚠️ Warning:** An invalid index like `v[100]` is **undefined behavior**. No bounds check.

### `data()` — Raw Pointer

```cpp
knst_vector<int> v = {1, 2, 3};
int* ptr = v.data();
// ptr[0] == 1, ptr[1] == 2, ptr[2] == 3
```

**Use case:** Passing to C APIs, pointer arithmetic.

### `back()` — Last Element

```cpp
knst_vector<int> v = {10, 20, 30};
int last = v.back();   // 30
v.back() = 99;         // modify
```

**⚠️ Warning:** `back()` on an empty vector is **undefined behavior**.

### `front()` — First Element

**Note:** There is **no** `front()` method. Use `v[0]` or `v.data()[0]` instead.

---

## 🔎 5) Search

### `find(value)` — Search by Value

```cpp
knst_vector<int> v = {10, 20, 30, 40};

auto it = v.find(30);
if (it != v.end()) {
    std::cout << "Found: index " << (it.get() - v.data()) << "\n";
} else {
    std::cout << "Not found\n";
}
```

**Return:** Iterator if found, `end()` otherwise.

---

## 📏 6) Size and Capacity

### `size()` — Element Count

```cpp
knst_vector<int> v = {1, 2, 3};
std::cout << v.size() << "\n";   // 3
```

### `capacity()` — Capacity

```cpp
knst_vector<int> v = {1, 2, 3};
std::cout << v.capacity() << "\n";   // 3 or larger
```

### `empty()` — Is It Empty?

```cpp
if (v.empty()) { /* empty */ }
```

### Capacity Growth

`knst_vector` uses the **2x growth** strategy:

| Start | After push_back |
|---|---|
| 0 | 4 |
| 4 | 8 |
| 8 | 16 |
| 16 | 32 |
| ... | ... |

**So:** Amortized O(1) `push_back`.

---

## 🔧 7) Resizing

### `reserve(n)` — Reserve Capacity

```cpp
knst_vector<int> v;
v.reserve(100);   // reserve space for at least 100 elements
// size = 0, capacity >= 100
```

**When to use?** If you know how many elements there will be, avoid reallocation during `push_back`.

### `resize(n)` — Change Size

```cpp
knst_vector<int> v = {1, 2, 3};

v.resize(6);      // {1, 2, 3, 0, 0, 0} — new slots default-initialized
v.resize(8, 7);   // {1, 2, 3, 0, 0, 0, 7, 7}
v.resize(2);      // {1, 2}
```

**Rules:**
- `new_size < size` → extra elements are destroyed
- `new_size > size` → new elements default (or given value)
- `new_size > capacity` → automatically calls `reserve`

### `shrink_to_fit()` — Shrink Capacity

```cpp
knst_vector<int> v;
v.reserve(1000);
v.push_back(1);
v.push_back(2);
// size = 2, capacity = 1000

v.shrink_to_fit();
// size = 2, capacity = 2
```

**The gain:** Memory savings. Important for large vectors.

### `assign(count, value)` — Refill from Scratch

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};
v.assign(3, 7);
// {7, 7, 7}
```

### `assign(first, last)` — Refill from a Range

```cpp
knst_vector<int> src = {10, 20, 30};
knst_vector<int> v;
v.assign(src.begin(), src.end());
// {10, 20, 30}
```

### `assign(initializer_list)` — Braces

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};
v.assign({100, 200, 300});
// {100, 200, 300}
```

---

## 🔁 8) Iterators

### Range-Based For

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};

for (int x : v) {
    std::cout << x << " ";
}
```

### Iterator Loop

```cpp
for (auto it = v.begin(); it != v.end(); ++it) {
    std::cout << *it << " ";
}
```

### Modification

```cpp
// Via non-const iterator/reference
for (auto& x : v) {
    x *= 10;
}
// {10, 20, 30, 40, 50}
```

### `const_iterator`

```cpp
const knst_vector<int>& cv = v;

for (auto it = cv.cbegin(); it != cv.cend(); ++it) {
    // read-only
}
```

### Iterator Types

- **`knst_iterator<T>`** — Mutable, random access
- **`knst_const_iterator<T>`** — Const, random access
- **Automatic conversion:** `iterator` → `const_iterator`

```cpp
knst_vector<int> v = {1, 2, 3};
knst_iterator<int> it = v.begin();
knst_const_iterator<int> cit = it;   // ✅ automatic conversion
```

---

## ⚖️ 9) Comparison

```cpp
knst_vector<int> a = {1, 2, 3};
knst_vector<int> b = {1, 2, 3};
knst_vector<int> c = {1, 2, 4};

a == b;   // true
a == c;   // false
a != c;   // true
```

**Note:** Only `==` and `!=` are available. `<`, `>`, `<=`, `>=` are **not supported** (no lexicographic comparison).

---

## 🧠 10) Using the Pool Allocator

### Default Allocator (malloc)

```cpp
knst_vector<int> v;   // knst_default_allocator
```

### Pool Allocator — `_sm` Suffix

```cpp
knst_vector_sm<int> v;   // knst_pool_allocator
v.push_back(1);
v.push_back(2);
v.push_back(3);
```

**`_sm` = "small memory"** — allocates from the pool, fast, but not ideal for large data.

### Comparison

| | `knst_vector<T>` | `knst_vector_sm<T>` |
|---|---|---|
| **Allocator** | `knst_default_allocator` (malloc) | `knst_pool_allocator` |
| **Small vectors** | Slow (malloc each time) | Fast (from pool) |
| **Large vectors** | Good | Falls back to malloc past pool limit |
| **Thread safety** | Safe | Requires `KNST_MEMORY_POOL_USE_MUTEX` |

### When Should You Use the Pool?

| Situation | Pool? |
|---|---|
| Many small vectors | ✅ Yes |
| Parser, tokenizer | ✅ Yes |
| Game engine, short-lived containers | ✅ Yes |
| Occasional single large vector | ❌ No, malloc is more suitable |
| Shared across threads | ⚠️ Requires mutex |

### Pool Size Example

```cpp
knst_pool_allocator pool(128, 512, 4096);

knst_vector<int, knst_pool_allocator> v(pool);
v.push_back(1);
```

---

## 🌉 11) Bridge Memory — Changing the Allocator

Move a vector to a different allocator:

```cpp
knst_vector<int> v = {1, 2, 3};

knst_pool_allocator pool;
if (v.bridge_memory(pool)) {
    // v now uses the pool allocator
}
```

**3 overloads:**

```cpp
knst_pool_allocator pool;

// 1. lvalue
v.bridge_memory(pool);

// 2. const lvalue
const knst_pool_allocator cpool;
v.bridge_memory(cpool);

// 3. rvalue (temporary)
v.bridge_memory(knst_pool_allocator{});
```

**What it does:**
1. Allocates from the new allocator
2. **Moves** the existing elements
3. Frees the old allocator's memory
4. Records the new allocator

**Constraint:** Allocator types must be **identical**. Otherwise it returns `false`.

---

## 🎁 12) `[[no_unique_address]]` — Zero Space

Inside the `knst_vector` class:

```cpp
[[no_unique_address]] mutable Allocator m_allocator;
```

**What it means:**
- **Empty allocator** (e.g., `knst_default_allocator`) → **0 bytes**
- **Non-empty allocator** (e.g., `knst_pool_allocator` = 8-byte pointer) → 8 bytes

**So:** For a default vector the allocator is **free**; for a pool vector it costs 8 bytes.

---

## 📊 13) Performance Notes

| Operation | Complexity | Note |
|---|---|---|
| `push_back` | **Amortized O(1)** | 2x growth |
| `pop_back` | **O(1)** | |
| `insert` (at end) | **Amortized O(1)** | Like push_back |
| `insert` (at front) | **O(n)** | Requires shift |
| `erase` (at end) | **O(1)** | |
| `erase` (at front) | **O(n)** | Requires shift |
| `operator[]` | **O(1)** | |
| `find` | **O(n)** | Linear search |
| `reserve` | **O(n)** | Reallocation |
| `resize` | **O(n)** | |
| `shrink_to_fit` | **O(n)** | |

### When Is It Slow?

- **Frequent `insert` at the front** — shifts each time
- **Frequent `erase` at the front** — shifts each time
- **1M `push_back` without reserving** — too many reallocations
- **Large `T` types** (e.g., `knst_c16string`) — move cost

### Optimization Tips

```cpp
// ❌ Slow: no reserve
knst_vector<int> v;
for (int i = 0; i < 1000000; ++i) {
    v.push_back(i);   // ~20 reallocations
}

// ✅ Fast: reserve
knst_vector<int> v;
v.reserve(1000000);
for (int i = 0; i < 1000000; ++i) {
    v.push_back(i);   // 0 reallocations
}
```

**The gain:** 5-10x speedup.

### `emplace_back` vs `push_back`

```cpp
struct Point { int x, y; Point(int x, int y) : x(x), y(y) {} };

// ❌ Copy: a temporary Point is created
v.push_back(Point(10, 20));

// ✅ Direct: built in place
v.emplace_back(10, 20);
```

**The gain:** No copy/move cost.

---

## 🛡️ 14) Exception Safety

### Design Philosophy: Exception-Free

`knst_vector` **does not throw exceptions**. It returns `false` on error:

```cpp
knst_vector<int> v;

if (!v.push_back(42)) {
    std::cerr << "Append failed — likely OOM\n";
}

if (!v.reserve(1000000)) {
    std::cerr << "Reservation failed\n";
}
```

**The advantage:**
- No `try/catch` block needed
- Usable in embedded/RT environments
- `nothrow` guarantee

### When Does It Return `false`?

- **Out of Memory** — `allocate` failed
- **Huge capacity** — `uint32_t` limit exceeded
- **Pool exhausted** and fallback failed

### `[[nodiscard]]` warning?

No — checking the return value is **your responsibility**:

```cpp
v.push_back(42);   // ⚠️ return not checked
// vs
if (!v.push_back(42)) { /* error */ }   // ✅
```

---

## 📋 15) Type Requirements

For `knst_vector<T>`, `T` must support:

| Requirement | Why? |
|---|---|
| **Copy or move constructor** | When adding elements |
| **Destructor** | For cleanup |
| **`operator==`** | For `find`/`erase_value` |
| **`operator!=`** | For the `operator!=` overload |

**Note:** `trivially_copyable` is **not required**. It works with complex types like `knst_c16string`.

### Supported Types

```cpp
knst_vector<int> v1;
knst_vector<knst_c16string> v2;
knst_vector<knst_byte_string> v3;
knst_vector<knst_image> v4;
knst_vector<Point> v5;   // custom struct
```

### Unsupported Types

```cpp
// ❌ Cannot hold references
knst_vector<int&> v;   // won't compile

// ❌ Move-only type (for sharing elements)
// However emplace_back or push_back(std::move(...)) works
knst_vector<std::unique_ptr<int>> v;
v.push_back(std::make_unique<int>(42));   // ✅ move
```

---

## 🌍 16) Platform and Allocators

### `knst_default_allocator`

- **Linux:** `malloc` / `free` / `realloc`
- **Windows:** `HeapAlloc` / `HeapFree` / `HeapReAlloc`
- **Feature:** Returns zero-initialized memory

### `knst_pool_allocator`

- **Small blocks:** From the pool (64, 256, 1024, 2048 bytes)
- **Large blocks:** `malloc` fallback
- **Reference counting:** Shares the same pool when copied
- **Thread safety:** Optional `KNST_MEMORY_POOL_USE_MUTEX`

---

## 💡 17) Usage Tips

### ✅ Do

```cpp
// 1. Reserve if you know the size
v.reserve(1000);

// 2. Use emplace_back for complex types
v.emplace_back(arg1, arg2);

// 3. Iterate by const reference — no copies
for (const auto& x : v) { /* ... */ }

// 4. Check bool returns
if (!v.push_back(42)) { /* error */ }

// 5. Use the pool for many small vectors
knst_vector_sm<int> small;
```

### ❌ Avoid

```cpp
// 1. Unchecked index
v[1000];   // ❌ UB risk

// 2. back/pop_back on an empty vector
knst_vector<int> empty;
empty.back();       // ❌ UB
empty.pop_back();   // ✅ safe (no-op)

// 3. 1M push_back without reserving
for (int i = 0; i < 1000000; ++i) v.push_back(i);   // ❌ slow

// 4. Ignoring the return value
v.push_back(42);   // ⚠️ no OOM check

// 5. Insert without self-reference handling
v.insert(v.begin(), v[0]);   // ✅ actually safe (takes a copy)
```

---

## 🔥 18) Real-World Examples

### Example 1: Dynamic List

```cpp
knst_vector<knst_c16string> read_lines(const knst_c16string& path) {
    knst_vector<knst_c16string> lines;
    knst_file f = knst_file::open(path);
    f.read_file_lines(lines);
    return lines;
}
```

### Example 2: Batch Processing

```cpp
knst_vector<int> data;
data.reserve(10000);

for (int i = 0; i < 10000; ++i) {
    data.push_back(compute(i));
}

// Process
for (int& x : data) x *= 2;
```

### Example 3: Filter

```cpp
knst_vector<int> filter_even(const knst_vector<int>& src) {
    knst_vector<int> result;
    result.reserve(src.size());

    for (int x : src) {
        if (x % 2 == 0) result.push_back(x);
    }
    return result;
}
```

### Example 4: Stack

```cpp
template<typename T>
class Stack {
    knst_vector<T> m_data;

public:
    void push(const T& value) { m_data.push_back(value); }
    void pop() { m_data.pop_back(); }
    T& top() { return m_data.back(); }
    bool empty() const { return m_data.empty(); }
    uint32_t size() const { return m_data.size(); }
};

Stack<int> s;
s.push(1);
s.push(2);
std::cout << s.top() << "\n";   // 2
s.pop();
```

### Example 5: Pool-Backed Cache

```cpp
class SmallObjectCache {
    knst_vector_sm<int> m_ids;

public:
    void add(int id) { m_ids.push_back(id); }

    bool contains(int id) {
        return m_ids.find(id) != m_ids.end();
    }

    void remove(int id) { m_ids.erase_value(id); }
};
```

### Example 6: Thread Pool Queue

```cpp
std::mutex mtx;
knst_vector<knst_function> queue;

void enqueue(knst_function fn) {
    std::lock_guard<std::mutex> lock(mtx);
    queue.push_back(std::move(fn));
}

void process_all() {
    knst_vector<knst_function> local;

    {
        std::lock_guard<std::mutex> lock(mtx);
        local = std::move(queue);
    }

    for (auto& fn : local) fn();
}
```

### Example 7: Matrix

```cpp
class Matrix {
    uint32_t rows, cols;
    knst_vector<double> data;

public:
    Matrix(uint32_t r, uint32_t c) : rows(r), cols(c) {
        data.resize(r * c, 0.0);
    }

    double& at(uint32_t r, uint32_t c) {
        return data[r * cols + c];
    }

    const double& at(uint32_t r, uint32_t c) const {
        return data[r * cols + c];
    }
};
```