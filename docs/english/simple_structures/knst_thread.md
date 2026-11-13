# knst_thread — Move-Only Thread Wrapper

Hello! 👋 This document explains what the `knst_thread` class is, how to use it, and why it exists in KernelNucleusT.

In short: **a move-only thread wrapper that is more capable than `std::thread`.** It offers priority setting, callback support, timeout-based join, kill, detach, and **thread pool integration**. It does everything `std::thread` cannot.

---

## 🎯 Why Does This Class Exist?

`std::thread` is part of the C++ standard, but it has some gaps:

- **No priority setting** — you cannot assign thread priority
- **No timeout-based join** — `join()` waits forever
- **No callback support** — you cannot run something when the task finishes
- **No kill** — you cannot force it to stop
- **No state query** — "is it running, is it done?" is not clear
- **No pool integration** — a new OS thread every time

`knst_thread` solves all of these:

- ✅ **Priority** — 7 levels via `knst_thread_priority` enum
- ✅ **Timeout-based join** — `join_for(ms)`, `try_join()`
- ✅ **Callback** — runs automatically when the task finishes
- ✅ **Kill** — force-stop as a last resort
- ✅ **State query** — `running()`, `finished()`, `joinable()`
- ✅ **Pool support** — submit to a pool or spawn a new thread
- ✅ **Move-only** — clear ownership, no copying
- ✅ **`shared_ptr` sharing** — thread and wrapper hold the same data

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Move-only** | No copying, clear ownership transfer |
| **Priority** | 7 levels: `inherit`, `lowest`, `low`, `normal`, `high`, `highest`, `time_critical` |
| **Callback** | Invoked automatically when the task finishes |
| **Timeout-based join** | `join_for(ms)` and `try_join()` |
| **Kill** | Force-stop as a last resort |
| **Detach** | Run in the background, do not wait |
| **State query** | `running()`, `finished()`, `joinable()`, `detached()` |
| **Pool integration** | Submit to a pool via `start(pool, fn)` |
| **Self-cleanup** | The thread cleans itself up via `self_ref` |
| **Exception-safe** | Even if the task throws, the thread closes cleanly |

---

## 🚀 Quick Start

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    knst_thread t;

    // Start the thread
    t.start([]() {
        std::cout << "Hello from the thread!\n";
    });

    // Wait
    t.join();
    return 0;
}
```

---

## 📋 Overview API

| Category | Methods |
|---|---|
| **Starting** | `start(fn)`, `start(fn, cb)`, `start_with_priority(prio, fn)`, `start(pool, fn)` |
| **Waiting** | `join()`, `join_for(ms)`, `try_join()` |
| **Control** | `kill()`, `detach()` |
| **State** | `state()`, `running()`, `finished()`, `joinable()`, `detached()`, `is_pooled()` |
| **Priority** | `set_priority(prio)`, `priority()`, `achieved_priority()`, `native_priority()` |

---

## 🎯 1) Empty State

A default-constructed `knst_thread` holds no thread:

```cpp
knst_thread t;

std::cout << "state    : " << (int)t.state() << "\n";   // 0 (idle)
std::cout << "joinable : " << t.joinable() << "\n";     // false
std::cout << "running  : " << t.running()  << "\n";     // false
std::cout << "finished : " << t.finished() << "\n";     // false
```

**State enum:**

```cpp
enum class knst_thread_state : uint8_t {
    idle = 0,       // empty
    running = 1,    // running
    finished = 2,   // finished, waiting for join
    joined = 3,     // joined
    detached = 4    // detached
};
```

---

## 🚀 2) Starting a Thread

### `start(fn)` — Simple Start

```cpp
knst_thread t;

bool ok = t.start([]() {
    std::cout << "worker thread running\n";
});

if (!ok) {
    std::cerr << "Failed to start thread!\n";
}

t.join();   // wait
```

**Return:** `true` = success, `false` = failure (already running or OS error)

### `start(fn, cb)` — With Callback

The callback is invoked automatically when the task finishes:

```cpp
knst_thread t;

t.start(
    []() { std::cout << "[task] running\n"; },
    []() { std::cout << "[callback] done\n"; }
);

t.join();
```

**Output:**
```
[task] running
[callback] done
```

**Use case:** Logging, cleanup, result processing

### `start_with_priority(prio, fn)` — With Priority

```cpp
knst_thread t;

t.start_with_priority(knst_thread_priority::high, []() {
    // high-priority work
});

t.join();
```

### `start(pool, fn)` — Submit to a Pool

```cpp
knst_thread_pool pool;

knst_thread t;
t.start(pool, []() {
    // runs in the pool
});
```

**For details see:** the `knst_thread_pool` documentation.

---

## 🎯 3) Priority System

### `knst_thread_priority` Enum

```cpp
enum class knst_thread_priority : int8_t {
    inherit       = -128,   // inherit parent priority
    lowest        = -2,     // lowest
    low           = -1,     // low
    normal        = 0,      // normal (default)
    high          = 1,      // high
    highest       = 2,      // highest
    time_critical = 3       // critical (use with care!)
};
```

### Setting Priority

```cpp
knst_thread t;

t.start_with_priority(knst_thread_priority::high, []() {
    // ...
});

// Requested priority
std::cout << "Requested : " << (int)t.priority() << "\n";   // 1 (high)

t.join();

// Actually achieved priority
std::cout << "Achieved  : " << (int)t.achieved_priority() << "\n";
```

**Why are there two different priorities?**

- `priority()` → what you requested
- `achieved_priority()` → what the OS granted

The OS sometimes refuses the requested level (permission issues). In that case, it **drops to a lower level** and `achieved_priority()` reflects that.

### Changing Priority Later

```cpp
knst_thread t;
t.start([]() { /* ... */ });

// Change priority while running
t.set_priority(knst_thread_priority::low);

t.join();
```

**Ladder mechanism:** If the requested level is refused, it tries one level lower. At worst, it drops to `lowest`.

### Native Priority

```cpp
// Read directly from the OS (even if you did not request it)
knst_thread_priority native = t.native_priority();
```

**Platform differences:**

| Priority | Windows | POSIX (nice) |
|---|---|---|
| `time_critical` | `THREAD_PRIORITY_TIME_CRITICAL` | -20 |
| `highest` | `THREAD_PRIORITY_HIGHEST` | -15 |
| `high` | `THREAD_PRIORITY_ABOVE_NORMAL` | -10 |
| `normal` | `THREAD_PRIORITY_NORMAL` | 0 |
| `low` | `THREAD_PRIORITY_BELOW_NORMAL` | 10 |
| `lowest` | `THREAD_PRIORITY_LOWEST` | 19 |
| `inherit` | Do not change | Do not change |

**Note:** On POSIX, lowering priority (raising nice) is possible without privileges, but raising priority requires `CAP_SYS_NICE`.

---

## ⏱️ 4) Waiting Methods

### `join()` — Wait Forever

```cpp
knst_thread t;
t.start([]() { /* ... */ });

t.join();   // waits until the thread ends
```

**Return:** `true` = success, `false` = already joined/detached/idle

### `join_for(ms)` — Wait with Timeout

```cpp
knst_thread t;
t.start([]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
});

bool done = t.join_for(50);   // wait 50 ms

if (!done) {
    std::cout << "Still running\n";
    t.join_for(1000);   // wait another 1000 ms
}
```

**Use case:** Waiting without blocking the UI thread, timeout checks

### `try_join()` — Non-Blocking Check

```cpp
knst_thread t;
t.start([]() { /* ... */ });

// Return immediately — no blocking
if (t.try_join()) {
    std::cout << "Done, joined\n";
} else {
    std::cout << "Still running\n";
}
```

**What it does:**
1. Tries to lock the mutex **non-blocking**
2. If the lock cannot be acquired → false (thread busy)
3. Checks `finished_flag`
4. If finished → calls `join()`, returns true
5. If not finished → false

---

## 🔪 5) Kill and Detach

### `kill()` — Last Resort

Force-stops the thread. **Use with great care!**

```cpp
knst_thread t;
t.start([]() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
});

std::this_thread::sleep_for(std::chrono::milliseconds(50));

bool ok = t.kill();
std::cout << "kill: " << (ok ? "success" : "failed") << "\n";
```

**Platform behavior:**

| Platform | What it does |
|---|---|
| **Windows** | `TerminateThread()` — thread dies immediately |
| **POSIX** | `pthread_cancel()` — stops at cancellation points |

**⚠️ Warnings:**
- **Mutexes may remain locked** → deadlock risk
- **Resources may leak** → file handles etc.
- **`volatile` data may be corrupted**
- **Does not work on pooled threads** (`is_pooled() == true` → returns `false`)

**When to use?** Only when graceful shutdown is impossible.

### `detach()` — Release to the Background

Releases the thread to itself. The OS cleans it up when done.

```cpp
knst_thread t;
t.start([]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Finished in the background\n";
});

t.detach();

// You do not have to wait
std::cout << "Main continues\n";

// But a detached thread must finish before main exits
std::this_thread::sleep_for(std::chrono::milliseconds(200));
```

**When to use?** Fire-and-forget tasks.

**⚠️ Warning:** If main exits, the detached thread dies too. Wait for it to finish before the program ends.

---

## 🔍 6) State Query

### Current State

```cpp
knst_thread t;
std::cout << (int)t.state() << "\n";   // 0 (idle)

t.start([]() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
std::cout << (int)t.state() << "\n";   // 1 (running)

t.join();
std::cout << (int)t.state() << "\n";   // 3 (joined)
```

### Practical Queries

```cpp
knst_thread t;

// Before starting
t.joinable();   // false
t.running();    // false
t.finished();   // false

// After starting
t.start([]() { /* ... */ });
t.joinable();   // true (running or finished)
t.running();    // true
t.finished();   // false

// After finishing
t.join();
t.joinable();   // false
t.running();    // false
t.finished();   // true
t.state();      // joined
```

### All Methods

| Method | What it returns |
|---|---|
| `state()` | `knst_thread_state` enum value |
| `running()` | true: running |
| `finished()` | true: finished or joined |
| `joinable()` | true: running or finished |
| `detached()` | true: detached |
| `is_pooled()` | true: belongs to a pool |

---

## 📦 7) Move Semantics

`knst_thread` is **move-only**. No copying, only ownership transfer.

### Move Constructor

```cpp
knst_thread a;
a.start([]() { /* ... */ });

knst_thread b = std::move(a);   // Ownership moved to b

std::cout << a.joinable() << "\n";   // false (empty)
std::cout << b.joinable() << "\n";   // true

b.join();
```

### Move Assignment

```cpp
knst_thread a;
a.start([]() { /* ... */ });

knst_thread b;
b = std::move(a);   // a becomes empty, b takes ownership

b.join();
```

### Why Is Copy Forbidden?

- **Clear ownership** — who is going to join?
- **No resource leak** — one `shared_ptr`, one owner
- **Thread safety** — no race condition risk

### When Should You Use Move?

```cpp
// ✅ Pass to a function
void take_thread(knst_thread t) {
    t.join();
}

knst_thread t;
t.start([]() { /* ... */ });
take_thread(std::move(t));

// ✅ Add to a vector
knst_vector<knst_thread> threads;
for (int i = 0; i < 10; ++i) {
    knst_thread t;
    t.start([]() { /* ... */ });
    threads.push_back(std::move(t));
}
```

---

## 🎁 8) Callback System

### Simple Callback

```cpp
knst_thread t;

t.start(
    []() { std::cout << "Doing work\n"; },
    []() { std::cout << "Work done\n"; }
);

t.join();
```

**Output:**
```
Doing work
Work done
```

### Callback with Cleanup

```cpp
std::atomic<bool> finished{false};

knst_thread t;
t.start(
    []() {
        // Main work
    },
    [&finished]() {
        finished.store(true);
        // cleanup
    }
);
```

### Callback + Priority

```cpp
knst_thread t;

t.start_with_priority(
    knst_thread_priority::high,
    []() { /* task */ },
    []() { /* callback */ }
);
```

---

## 🧠 9) Shared State

Data sharing between threads is **your responsibility**. `knst_thread` does not provide synchronization.

### Safe Sharing with Atomic

```cpp
std::atomic<int> counter{0};

knst_thread t;
t.start([&counter]() {
    for (int i = 0; i < 100000; ++i) {
        counter.fetch_add(1);
    }
});
t.join();

std::cout << "Counter: " << counter.load() << "\n";   // 100000
```

### Safe Sharing with Mutex

```cpp
std::mutex mtx;
knst_vector<int> shared_data;

knst_thread t;
t.start([&]() {
    for (int i = 0; i < 100; ++i) {
        std::lock_guard<std::mutex> lock(mtx);
        shared_data.push_back(i);
    }
});
t.join();
```

### ❌ Wrong Usage

```cpp
int counter = 0;   // not atomic!

knst_thread t;
t.start([&counter]() {
    for (int i = 0; i < 100000; ++i) {
        ++counter;   // ❌ race condition!
    }
});
t.join();

// Result is not 100000, it is some random value
```

---

## 🔧 10) `knst_thread_data` Structure

The data shared between the thread and the wrapper:

```cpp
struct knst_thread_data {
    std::mutex mtx;                             // synchronization
    std::condition_variable cv;                 // "finished" signal
    bool finished_flag = false;                 // is it done?
    std::atomic<uint8_t> state{0};              // state
    knst_function task;                         // the work to run
    bool is_pooled = false;                     // belongs to a pool?

    std::atomic<int8_t> requested_priority;     // requested
    std::atomic<int8_t> achieved_priority;      // achieved

    std::shared_ptr<knst_thread_data> self_ref; // reference to itself

    // Platform-specific
    #if KNST_USING_PLATFORM_WINDOWS
        HANDLE handle = nullptr;
    #else
        pthread_t handle{};
        bool handle_valid = false;
        std::atomic<int32_t> native_tid{-1};
    #endif
};
```

### What Is `self_ref` For?

While the thread is running, the `knst_thread` object may be destroyed. Thanks to `self_ref`, `knst_thread_data` **stays alive while the thread runs**:

```cpp
{
    knst_thread t;
    t.start([]() { /* ... */ });
    // t leaves scope, destructor runs
    // BUT: the thread is still running
    // thanks to self_ref, the data stays alive
}
```

When the thread finishes, `self_ref.reset()` is called and the data is cleaned up automatically.

---

## ⚙️ 11) Pool Integration

### Submit to a Pool

```cpp
knst_thread_pool pool;

knst_thread t;
t.start(pool, []() {
    // will run inside the pool
});
```

**Advantage:** Does not create a new OS thread; uses an existing worker.

### Priority Inside the Pool

```cpp
t.start_with_priority(pool, knst_thread_priority::high, []() {
    // ...
});
```

**Note:** Inside the pool, priority is **informational only** — the OS thread does not change.

### Callback Inside the Pool

```cpp
t.start(
    pool,
    []() { /* task */ },
    []() { /* callback */ }
);
```

### Pooled vs Standalone

```cpp
knst_thread t;
t.start(pool, []() { /* ... */ });

std::cout << "Pooled: " << t.is_pooled() << "\n";   // true
```

**On pooled threads:**
- `kill()` **does not work** (could corrupt the pool)
- `detach()` **does not work**
- `join()` works (the pool signals "finished")

---

## 🌍 12) Platform Differences

| Topic | Windows | POSIX |
|---|---|---|
| **Thread creation** | `CreateThread` | `pthread_create` |
| **Priority** | `SetThreadPriority` | `setpriority` / `nice` |
| **Kill** | `TerminateThread` (immediate) | `pthread_cancel` (at cancellation point) |
| **Detach** | `CloseHandle` | `pthread_detach` |
| **Join** | `WaitForSingleObject` | `pthread_join` |
| **Native TID** | `GetCurrentThreadId` | `gettid()` (Linux) |
| **Priority permission** | Usually OK | `CAP_SYS_NICE` required (to raise) |

### Exception Handling Difference (POSIX)

On POSIX, when `pthread_cancel` is called, the thread throws the `abi::__forced_unwind` exception. `knst_thread` **catches this specially** and **rethrows** it (otherwise it would be undefined behavior):

```cpp
try {
    self->task();
}
#if defined(__GLIBCXX__) || defined(__GLIBC__)
catch (abi::__forced_unwind&) {
    throw;   // rethrow — pthread_cancel expects this
}
#endif
catch (...) {
    // other exceptions are swallowed
}
```

---

## 🔥 13) Real-World Examples

### Example 1: Parallel Processing

```cpp
knst_vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8};
std::atomic<int> sum{0};

knst_vector<knst_thread> threads;

for (int value : data) {
    knst_thread t;
    t.start([&sum, value]() {
        sum.fetch_add(value * value);
    });
    threads.push_back(std::move(t));
}

// Wait for all
for (auto& t : threads) {
    t.join();
}

std::cout << "Sum of squares: " << sum.load() << "\n";   // 204
```

### Example 2: Task with Timeout

```cpp
bool run_with_timeout(knst_function task, uint32_t ms) {
    knst_thread t;
    t.start(std::move(task));

    if (!t.join_for(ms)) {
        std::cerr << "Timeout, killing\n";
        t.kill();
        return false;
    }
    return true;
}

run_with_timeout([]() {
    std::this_thread::sleep_for(std::chrono::seconds(10));
}, 500);   // killed after 500 ms
```

### Example 3: Background Worker

```cpp
class BackgroundWorker {
    knst_thread m_thread;
    std::atomic<bool> m_stop{false};

public:
    void start() {
        m_thread.start([this]() {
            while (!m_stop.load()) {
                // do work
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }

    void stop() {
        m_stop.store(true);
        m_thread.join_for(1000);   // wait 1 second
        if (m_thread.running()) {
            m_thread.kill();       // force stop
        }
    }
};
```

### Example 4: Pipeline with Callback

```cpp
void process_data(knst_vector<int>& data) {
    std::atomic<int> processed{0};

    knst_vector<knst_thread> threads;

    for (int value : data) {
        knst_thread t;

        t.start(
            [value]() {
                // Heavy work
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            },
            [&processed]() {
                processed.fetch_add(1);
                std::cout << "Processed: " << processed.load() << "\n";
            }
        );

        threads.push_back(std::move(t));
    }

    for (auto& t : threads) t.join();
}
```

### Example 5: Critical Work with Priority

```cpp
void run_critical_task() {
    knst_thread t;

    t.start_with_priority(
        knst_thread_priority::highest,
        []() {
            // Real-time work
            auto start = std::chrono::steady_clock::now();
            // ... work ...
            auto end = std::chrono::steady_clock::now();
        }
    );

    // Was the priority actually assigned?
    if (t.achieved_priority() != knst_thread_priority::highest) {
        std::cerr << "Warning: Requested priority not achieved\n";
    }

    t.join();
}
```

### Example 6: Detached Logger

```cpp
void log_async(const knst_c16string& message) {
    knst_thread t;
    t.start([message]() {
        // Write to file (may be slow)
        knst_file::append_file_text(u"app.log", message + u"\n");
    });
    t.detach();   // Main thread does not wait
}

int main() {
    log_async(u"Application started");
    log_async(u"Data loaded");
    log_async(u"Operation complete");

    // Wait for loggers to finish before main exits
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}
```

### Example 7: Thread Pool Comparison

```cpp
void compare_approaches() {
    // Approach 1: New thread for each task (slow)
    auto start = std::chrono::steady_clock::now();

    knst_vector<knst_thread> threads;
    for (int i = 0; i < 1000; ++i) {
        knst_thread t;
        t.start([]() { /* light work */ });
        threads.push_back(std::move(t));
    }
    for (auto& t : threads) t.join();

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "New thread: " << duration.count() << " ms\n";

    // Approach 2: Pool (fast)
    knst_thread_pool pool;
    start = std::chrono::steady_clock::now();

    for (int i = 0; i < 1000; ++i) {
        knst_thread t;
        t.start(pool, []() { /* light work */ });
    }
    pool.wait_all();

    end = std::chrono::steady_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Pool: " << duration.count() << " ms\n";
}
```

**Typical result:** The pool is **10-50x faster** (it uses 8-16 workers instead of spawning 1000 OS threads).

---

## 📊 14) Performance Notes

| Operation | Complexity | Note |
|---|---|---|
| `start()` | **O(1)** + OS cost | Spawning a new thread is ~10-100 μs |
| `start(pool, ...)` | **O(1)** | Queue to an existing worker — very fast |
| `join()` | **O(1)** | Waits until done |
| `join_for(ms)` | **O(1)** | Wait with timeout |
| `try_join()` | **O(1)** | Non-blocking check |
| `detach()` | **O(1)** | Releases the handle |
| `kill()` | **O(1)** + OS | Dangerous but fast |
| `set_priority()` | **O(1)** | OS syscall |

### When Is It Slow?

- **Calling `start()` 10,000 times** — OS thread cost (use a pool!)
- **Calling `kill()`** — last resort only
- **Frequent `join_for()` checks** — prefer event-driven design over busy-wait

### Optimization Tips

```cpp
// ❌ Slow: new thread for each task
for (int i = 0; i < 10000; ++i) {
    knst_thread t;
    t.start([]() { /* ... */ });
    t.join();
}

// ✅ Fast: pool + batch
knst_thread_pool pool;
for (int i = 0; i < 10000; ++i) {
    knst_thread t;
    t.start(pool, []() { /* ... */ });
}
pool.wait_all();
```

---

## 🛡️ 15) Exception Safety

### Exception Inside a Task

If a task throws, `knst_thread` **swallows it** and closes cleanly:

```cpp
knst_thread t;
t.start([]() {
    throw std::runtime_error("error!");
});

t.join();   // ✅ No problem — the exception was caught internally
```

**But you cannot see the exception!** For that, use `std::exception_ptr`:

```cpp
std::exception_ptr err;

knst_thread t;
t.start([&err]() {
    try {
        // risky work
        throw std::runtime_error("error");
    } catch (...) {
        err = std::current_exception();
    }
});
t.join();

if (err) {
    try { std::rethrow_exception(err); }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
}
```

### Destructor Behavior

The `knst_thread` destructor is **smart**:

- **Running** → auto-detach
- **Finished** → auto-join
- **Idle** → do nothing
- **Pooled** → does not touch the OS handle

```cpp
{
    knst_thread t;
    t.start([]() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    });
    // On scope exit: t's destructor auto-detaches
    // the thread keeps running in the background
}
```

**So:** No memory leaks or zombie threads.

---

## 🎯 What It Buys You

`knst_thread` provides serious advantage in these situations:

- ✅ **Priority control** — required for media, RT work
- ✅ **Timeout-bounded operations** — `join_for()`
- ✅ **Callback-based tasks** — pipelines, logging
- ✅ **If you use a thread pool** — `start(pool, fn)`
- ✅ **State query** — if you frequently ask "is it running?"
- ✅ **Move semantics** — adding threads to a vector