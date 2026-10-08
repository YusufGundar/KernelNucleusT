# knst_function — Move-Only Callable Wrapper

Hello! 👋 This document explains what the `knst_function` class is, how to use it, and why it exists in KernelNucleusT.

In short: **a move-only, SSO (Small Object Optimization) enabled wrapper that can hold any callable (lambda, function pointer, functor) with the `void()` signature.** Similar to `std::function`, but **lighter, faster, and exception-free**.

---

## 🎯 Why Does This Class Exist?

`std::function` is part of the C++ standard, but it has some drawbacks:

- **Heavy** — carries a lot of type information inside
- **Uses exceptions** — `bad_function_call`, heap allocation errors
- **Copyable** — no move-only semantics, which is slow in some scenarios
- **Supports return types** — but `void()` is usually enough

`knst_function` solves all of this:

- ✅ **Move-only** — no copying, ownership transfer only
- ✅ **SSO** — up to 64 bytes on the stack, no heap allocation
- ✅ **`nothrow` guarantee** — does not throw exceptions
- ✅ **`void()`-focused** — simple and fast
- ✅ **Thread pool compatible** — works perfectly with `knst_thread_pool`

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Move-only** | No copying, clear ownership transfer |
| **SSO (64 bytes)** | Small lambdas on the stack, no heap |
| **`nothrow`** | No exception-safety worries |
| **`void()` signature** | Simple, single-purpose |
| **`std::function`-compatible** | Same usage patterns |
| **Small size** | Only ~72 bytes (SSO buffer + pointers) |
| **`constexpr`-friendly** | Fixed size, predictable |
| **No virtual functions** | Manual vtable, customizable |

---

## 🚀 Quick Start

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // 1. Empty
    knst_function f;
    std::cout << "empty: " << (f.empty() ? "yes" : "no") << "\n";

    // 2. With lambda
    f = []() { std::cout << "Hello!\n"; };
    f();   // "Hello!"

    // 3. With function pointer
    knst_function g = &some_function;
    g();

    // 4. Call (safe even when empty)
    knst_function h;
    h();   // no-op, does not crash

    return 0;
}
```

---

## 📋 Core API

### Constructors

```cpp
// Empty
knst_function f;

// With lambda
knst_function f1 = [](){ /* ... */ };

// With function pointer
void my_func();
knst_function f2 = &my_func;

// With functor (class with operator())
struct MyFunctor {
    void operator()() { /* ... */ }
};
knst_function f3 = MyFunctor{};

// Move constructor
knst_function f4 = std::move(f1);   // f1 is now empty
```

### Methods

| Method | What it does |
|---|---|
| `f()` | Calls the stored callable. No-op when empty. |
| `f.empty()` | Is there anything inside? `true`/`false` |
| `static_cast<bool>(f)` | Inverse of `empty()` — is a callable present? |
| `f.reset()` | Destroys the stored callable, empties it |
| `f = std::move(other)` | Ownership transfer |

### Operators

```cpp
// Call
f();                    // call operator

// Bool check
if (f) { /* non-empty */ }
if (!f) { /* empty */ }

// Move assignment
knst_function a = [](){};
knst_function b;
b = std::move(a);       // moves to b, a becomes empty

// Copy is FORBIDDEN (compile-time error)
knst_function c = a;    // ❌ Won't compile!
```

---

## 1) Empty State

```cpp
knst_function f;

std::cout << f.empty() << "\n";  // true
std::cout << (f ? "full" : "empty") << "\n";  // empty

// Calling an empty function is SAFE — does nothing
f();  // nothing happens, does not crash
```

**This is an important feature.** When `std::function` is empty and called, it throws `std::bad_function_call`. `knst_function` simply does nothing.

---

## 2) Usage with Lambdas

The most common usage. Any lambda with captures:

```cpp
int counter = 0;

knst_function increment = [&counter]() {
    ++counter;
    std::cout << "counter = " << counter << "\n";
};

increment();  // counter = 1
increment();  // counter = 2
increment();  // counter = 3

std::cout << "final: " << counter << "\n";  // 3
```

**Note:** If the lambda captures by reference (`[&]`), the referenced object must outlive the `knst_function`. Otherwise it becomes a dangling reference.

```cpp
knst_function bad() {
    int local = 42;
    return [&local]() {           // ❌ Dangling!
        std::cout << local << "\n";
    };
}

knst_function good() {
    int local = 42;
    return [local]() {            // ✅ Value capture
        std::cout << local << "\n";
    };
}
```

---

## 3) Function Pointers

```cpp
void hello() {
    std::cout << "hello from pointer\n";
}

knst_function f = &hello;
f();
```

A **special vtable** is generated for function pointers, and SSO is used (pointer size = 8 bytes).

---

## 4) Functor (Function Object)

Any class with a defined `operator()`:

```cpp
struct Greeter {
    std::string name;
    
    void operator()() {
        std::cout << "Hello, " << name << "!\n";
    }
};

Greeter g{"Yusuf"};
knst_function f = g;
f();  // "Hello, Yusuf!"
```

---

## 5) SSO — Stack vs Heap

### SSO Threshold: 64 Bytes (Default)

Inside `knst_function` there is a **64-byte buffer**. Callables that fit within this size are stored **on the stack** (no heap allocation). Larger ones go to the **heap**.

### When Does It Stay on the Stack?

```cpp
// ✅ SSO — 8 bytes (int capture)
auto small = [x = 42]() { /* ... */ };
knst_function f1 = small;
// sizeof(small) <= 64 → stack

// ✅ SSO — 32 bytes (multiple captures)
int a, b, c, d, e, f, g, h;
auto medium = [a,b,c,d,e,f,g,h]() { /* ... */ };
knst_function f2 = medium;
// 8*8 = 64 bytes → still stack

// ❌ Heap — 128 bytes (large array)
char big[128] = {};
auto large = [big]() { /* ... */ };
knst_function f3 = large;
// 128 bytes > 64 bytes → heap
```

### `sizeof(knst_function)`

```cpp
std::cout << sizeof(knst_function) << " bytes\n";
// Typically 72-80 bytes:
//   - 64 bytes m_buffer (SSO)
//   - 8 bytes m_heap_ptr
//   - 8 bytes m_vtable
//   - 1 byte m_is_heap (+ padding)
```

### Changing the Threshold

Put this in `knst_settings.hpp`:

```cpp
#define KNST_FUNCTION_INLINE_SIZE 128
```

Now callables up to 128 bytes stay on the stack. **Trade-off:**

| Value | Advantage | Disadvantage |
|---|---|---|
| **32** | Small `sizeof` | Most lambdas go to heap |
| **64 (default)** | Balanced | Good average |
| **128** | Most lambdas on stack | `knst_function` is 2x bigger |
| **256** | Almost always on stack | Memory waste |

**Recommendation:** Keep the default 64. Only increase it if you hold many `knst_function` instances and each has a large capture.

---

## 6) Move Semantics

`knst_function` **cannot be copied** but **can be moved**. This clarifies the ownership semantics.

### Move Constructor

```cpp
knst_function a = [](){ std::cout << "a\n"; };
knst_function b = std::move(a);

std::cout << (a.empty() ? "a empty" : "a full") << "\n";  // a empty
std::cout << (b.empty() ? "b empty" : "b full") << "\n";  // b full

b();  // "a" runs
a();  // safe no-op
```

### Move Assignment

```cpp
knst_function a = [](){ std::cout << "A\n"; };
knst_function b = [](){ std::cout << "B\n"; };

b = std::move(a);  // b's previous content is destroyed, a is moved into b
b();               // "A"
```

### What Does `std::move` Do?

- **If on the stack:** No `memcpy` to the new object — the **move constructor** is called
- **If on the heap:** The pointer is transferred; the heap block is not copied
- **Source:** Becomes empty (`m_vtable = nullptr`)

**So move is always cheap** — even with SSO.

### Why Is Copy Forbidden?

Because lambdas are not always copyable. Also:

1. **Ownership is clear** — who owns it?
2. **No race conditions** — two threads should not call the same callable
3. **Nothrow guarantee** — copy allocation could fail

---

## 7) `reset()` — Clearing

```cpp
knst_function f = [](){ std::cout << "hello\n"; };
f();
std::cout << "empty: " << f.empty() << "\n";  // false

f.reset();
std::cout << "empty: " << f.empty() << "\n";  // true

f();  // safe no-op
```

**What it does:**
1. Calls `destroy` through the vtable (the lambda's destructor)
2. If on heap, calls `operator delete`
3. Sets all pointers to `nullptr`

**Note:** The destructor automatically calls `reset()`, so you do not have to call it manually.

---

## 8) Usage with Thread Pool

This is the primary design goal of `knst_function`. To submit jobs to `knst_thread_pool`:

```cpp
knst_thread_pool pool;

// Submit a job
pool.submit([](){
    std::cout << "Thread: " << std::this_thread::get_id() << "\n";
});

// Job with capture
int data = 42;
pool.submit([data](){
    std::cout << "Data: " << data << "\n";
});
```

**Why `knst_function`?**
- Thread pool jobs have the `void()` signature
- Move-only makes queueing cheap
- SSO means most jobs do not cause heap allocation
- `nothrow` makes queue operations safe

---

## 9) Performance Comparison

| Operation | `std::function` | `knst_function` |
|---|---|---|
| Empty construction | ~8 bytes | ~72 bytes |
| Small lambda (SSO) | Heap alloc | **Stack** ✅ |
| Large lambda | Heap alloc | Heap alloc |
| Call | Virtual call | Virtual call |
| Move | Copy + destroy | **Pointer transfer** ✅ |
| Copy | Supported | **Forbidden** ❌ |
| Empty call | Exception | **No-op** ✅ |
| `sizeof` | ~32 bytes | ~72 bytes |

**Note:** `knst_function` is physically larger (because of the SSO buffer) but **much faster at runtime** because:

- No heap allocation thanks to SSO
- Move is just pointer transfer
- No exception handling

---

## 10) Error Handling

### Empty Call

```cpp
knst_function f;
f();  // ✅ Safe — no-op
```

### `nothrow` Guarantee

Only **`nothrow`-move-constructible** or **`nothrow`-copy-constructible** callables are accepted:

```cpp
// ✅ OK — lambda is nothrow-moveable
auto ok = [x = 42]() {};

// ❌ Compile error — string is not nothrow
std::string str = "hello";
auto bad = [str]() {};  // std::string is not nothrow
// static_assert fires
```

**Why?** Because `knst_function` must be exception-free. If copy/move fails, it has no way to recover.

### If Heap Allocation Fails

```cpp
// If heap allocation fails
knst_function f = [/* large capture */](){};

// f stays empty, calling is a no-op
if (f.empty()) {
    std::cerr << "Allocation failed\n";
}
```

**So:** If `operator new` returns `nullptr`, an empty `knst_function` is silently created. No exception is thrown.

---

## 11) Use Cases

### Scenario 1: Callback System

```cpp
class Button {
    knst_function m_on_click;
    
public:
    void set_on_click(knst_function fn) {
        m_on_click = std::move(fn);
    }
    
    void click() {
        if (m_on_click) m_on_click();
    }
};

Button btn;
btn.set_on_click([](){
    std::cout << "Clicked!\n";
});
btn.click();
```

### Scenario 2: Deferred Execution

```cpp
knst_vector<knst_function> deferred;

deferred.push_back([](){ std::cout << "Step 1\n"; });
deferred.push_back([](){ std::cout << "Step 2\n"; });
deferred.push_back([](){ std::cout << "Step 3\n"; });

for (auto& fn : deferred) fn();
```

### Scenario 3: Scope Guard

```cpp
class ScopeGuard {
    knst_function m_cleanup;
public:
    explicit ScopeGuard(knst_function fn) 
        : m_cleanup(std::move(fn)) {}
    
    ~ScopeGuard() {
        if (m_cleanup) m_cleanup();
    }
};

void do_work() {
    FILE* f = fopen("data.txt", "r");
    ScopeGuard guard([f](){ fclose(f); });
    
    // ... work ...
    
}  // guard destructor calls fclose
```

### Scenario 4: Event System

```cpp
class EventEmitter {
    std::map<std::string, knst_vector<knst_function>> m_listeners;
    
public:
    void on(const std::string& event, knst_function fn) {
        m_listeners[event].push_back(std::move(fn));
    }
    
    void emit(const std::string& event) {
        auto it = m_listeners.find(event);
        if (it == m_listeners.end()) return;
        for (auto& fn : it->second) fn();
    }
};

EventEmitter emitter;
emitter.on("start", [](){ std::cout << "Started\n"; });
emitter.on("start", [](){ std::cout << "Second handler\n"; });
emitter.emit("start");   // Both run
```

---

## 12) Integration with `knst_thread_pool`

```cpp
#include "KernelNucleusT.hpp"

int main() {
    knst_thread_pool pool;
    
    // Submit 100 jobs
    for (int i = 0; i < 100; ++i) {
        pool.submit([i](){
            std::cout << "Job " << i << "\n";
        });
    }
    
    pool.wait_all();
    return 0;
}
```

**The gain:** Each job is stored as `knst_function`. Thanks to SSO, jobs with an int capture make **no heap allocation**. So 100 jobs = **0 heap allocations** instead of 100.

---

## 13) Configuration Macro

Put this in `knst_settings.hpp`:

```cpp
// SSO threshold (bytes). Default: 64.
#define KNST_FUNCTION_INLINE_SIZE 64
```

### Choosing a Value

| Use case | Recommended |
|---|---|
| General purpose | **64** (default) |
| Many small lambdas | 32 |
| Frequent large captures (server, DB) | 128 |
| Embedded / memory-constrained | 16 or 32 |

---

## 14) Tips

### ✅ Do

```cpp
// 1. Use move, not copy
knst_function a = [](){};
knst_function b = std::move(a);  // ✅

// 2. Check for empty
if (f) f();  // ✅

// 3. Prefer small captures (SSO)
int x = 42;
knst_function f = [x](){};  // ✅ stack

// 4. Be careful with reference captures
auto fn = [&local]() { /* is local still alive? */ };  // ⚠️
```

### ❌ Avoid

```cpp
// 1. Attempting to copy
knst_function a = [](){};
knst_function b = a;  // ❌ Compile error

// 2. Calling an empty function — behaves differently from std::function
// (this is actually OK, but check if you want to be sure)
f();  // no-op, silent

// 3. Copying large data in a capture
char big[1024];
knst_function f = [big](){};  // ❌ Goes to heap
knst_function g = [&big](){}; // ✅ Reference — but be careful

// 4. Lambda that throws
auto f = [](){ throw std::runtime_error("error"); };  // ❌ Not recommended
// knst_function is designed to be exception-free
```

---

## 15) Full Example

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

void hello() {
    std::cout << "Hello!\n";
}

int main() {
    // 1. Empty
    knst_function f;
    std::cout << "Empty? " << (f.empty() ? "yes" : "no") << "\n";

    // 2. Lambda
    int counter = 0;
    f = [&counter]() {
        ++counter;
        std::cout << "Counter: " << counter << "\n";
    };
    f();
    f();
    f();

    // 3. Function pointer
    knst_function g = &hello;
    g();

    // 4. Move
    knst_function h = std::move(g);
    std::cout << "g empty? " << (g.empty() ? "yes" : "no") << "\n";
    h();  // "Hello!"

    // 5. Reset
    h.reset();
    std::cout << "h empty? " << (h.empty() ? "yes" : "no") << "\n";

    // 6. SSO size
    std::cout << "sizeof(knst_function): " << sizeof(knst_function) << " bytes\n";

    return 0;
}
```

### Example Output

```
Empty? yes
Counter: 1
Counter: 2
Counter: 3
Hello!
g empty? yes
Hello!
h empty? yes
sizeof(knst_function): 80 bytes
```

---

## 🎯 What It Buys You

`knst_function` provides serious advantage in these situations:

- ✅ **Thread pool job submission** — this is the design purpose
- ✅ **Callback systems** — event handler, GUI, buttons
- ✅ **Deferred execution** — queue, task scheduler
- ✅ **Scope guard / RAII** — for cleanup
- ✅ **When you want move-only semantics**
- ✅ **When you need SSO performance**
- ✅ **Exception-free environments** (embedded, kernel, real-time)