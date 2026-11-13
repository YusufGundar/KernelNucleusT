# knst_thread_pool — Worker-Thread Pool

Hello! 👋 This document explains what the `knst_thread_pool` class is, how to use it, and why it exists in KernelNucleusT.

In short: **a worker-thread pool that amortizes thread-creation cost.** It queues tasks and lets workers process them. When the queue fills up, it **automatically spawns overflow threads** (up to 32) and shuts them down as the workload decreases. **Priority support** is available both for workers and per task.

---

## 🎯 Why Does This Class Exist?

Spawning a new `std::thread` for every task is **expensive** — not as much as fork/exec, but it still consumes OS resources:

- Spawning 10,000 OS threads for 10,000 small tasks takes **seconds**
- Each thread consumes ~1 MB of stack space
- Context-switch overhead multiplies

`knst_thread_pool` solves this:

- ✅ **8-16 workers** are created once, all pulling from the queue
- ✅ **Thread reuse** — OS thread cost is amortized
- ✅ **Priority** — worker base priority + per-task temporary priority
- ✅ **Overflow support** — spawns temporary threads when the queue swells
- ✅ **Graceful/Force shutdown** — complete or cancel queued tasks
- ✅ **`knst_thread` integration** — `submit()` returns a `knst_thread`

**The gain:** For 1000 tasks, **10-50x** speedup.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Worker reuse** | Thread creation cost is amortized |
| **Batch pulling** | Each worker pulls 8 tasks at once (reduces lock cost) |
| **Overflow threads** | Temporary helper threads when the queue swells (max 32) |
| **Priority** | Worker base priority + per-task temporary priority |
| **Graceful shutdown** | Queued tasks are completed |
| **Force shutdown** | Queued tasks are cancelled |
| **`wait_all`** | Wait until all tasks finish |
| **`wait_all_for`** | Wait with timeout |
| **Introspection** | `pending_count`, `active_count`, `queue_size`, `overflow_count` |
| **`knst_thread` compatible** | `submit()` returns a joinable thread |
| **Auto worker count** | Automatic based on `hardware_concurrency()` |

---

## 🚀 Quick Start

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // Create a pool with an automatic worker count
    knst_thread_pool pool;
    pool.start();

    // Submit a task
    pool.submit([]() {
        std::cout << "Hello from the pool!\n";
    });

    // Wait until done
    pool.wait_all();

    pool.shutdown();
    return 0;
}
```

---

## 📋 Overview API

| Category | Methods |
|---|---|
| **Lifecycle** | `start`, `shutdown`, `is_running` |
| **Task submission** | `submit`, `submit_with_priority` |
| **Waiting** | `wait_all`, `wait_all_for` |
| **Introspection** | `worker_count`, `pending_count`, `queue_size`, `active_count`, `overflow_count` |
| **Priority** | `set_worker_priority`, `worker_priority`, `worker_priority_achieved` |

---

## 🏗️ 1) Creating a Pool

### Automatic Worker Count

```cpp
knst_thread_pool pool;   // uses hardware_concurrency()
pool.start();
```

**Default behavior:**
- `hardware_concurrency()` → number of CPU cores
- If it returns 0 (unknown) → 3

### Manual Worker Count

```cpp
knst_thread_pool pool(4);   // 4 workers
pool.start();
```

### With Priority

```cpp
knst_thread_pool pool(4, knst_thread_priority::high);
pool.start();
```

### Start and Shutdown

```cpp
knst_thread_pool pool;

// Start
bool ok = pool.start();
if (!ok) {
    std::cerr << "Pool failed to start!\n";
}

// Is it running?
if (pool.is_running()) {
    // ...
}

// Shut down
pool.shutdown();
```

**Note:** The destructor calls `shutdown()` — even if you do not call it manually, there is no leak.

---

## 🚀 2) Task Submission

### `submit(fn)` — Simple Submission

```cpp
knst_thread_pool pool(2);
pool.start();

knst_thread t = pool.submit([]() {
    std::cout << "Task running\n";
});

t.join();   // Wait for completion (optional)

pool.shutdown();
```

**Return:** `knst_thread` — joinable, with `join()`, `join_for()`, `finished()` and similar methods.

### `submit(fn, cb)` — With Callback

```cpp
knst_thread t = pool.submit(
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

### `submit_with_priority(prio, fn)` — With Priority

```cpp
knst_thread t = pool.submit_with_priority(
    knst_thread_priority::high,
    []() {
        // high-priority task
    }
);

t.join();

// Requested priority
std::cout << "Requested : " << (int)t.priority() << "\n";       // 1 (high)

// Achieved (the worker temporarily took this priority)
std::cout << "Achieved  : " << (int)t.achieved_priority() << "\n";
```

**How it works:**
1. The worker temporarily raises its base priority to `high`
2. Runs the task
3. Restores its base priority when done

### `submit_with_priority(prio, fn, cb)` — Priority + Callback

```cpp
knst_thread t = pool.submit_with_priority(
    knst_thread_priority::low,
    []() { /* task */ },
    []() { /* callback */ }
);

t.join();
```

---

## ⏱️ 3) Waiting Methods

### `wait_all()` — Wait Forever

Waits until all tasks finish:

```cpp
knst_thread_pool pool(4);
pool.start();

for (int i = 0; i < 100; ++i) {
    pool.submit([i]() {
        // do work
    });
}

pool.wait_all();   // Waits until all 100 tasks finish
pool.shutdown();
```

**When to use?** When you want to do something after the batch is done.

### `wait_all_for(ms)` — Wait with Timeout

```cpp
for (int i = 0; i < 100; ++i) {
    pool.submit([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });
}

// Wait 20 ms
if (pool.wait_all_for(20)) {
    std::cout << "Done\n";
} else {
    std::cout << "Still running\n";
}

// Wait 5000 more ms
pool.wait_all_for(5000);
```

**Use case:** Waiting without blocking the UI thread, shutdown checks.

### `knst_thread::join()` — For a Single Task

```cpp
knst_thread t = pool.submit([]() { /* ... */ });
t.join();      // Wait specifically for this task
```

---

## 🔍 4) Introspection

### `worker_count()` — Worker Count

```cpp
std::cout << "Workers: " << pool.worker_count() << "\n";
// E.g.: 8
```

### `pending_count()` — Queue + Running

```cpp
std::cout << "Pending: " << pool.pending_count() << "\n";
// Queued + currently running
```

### `queue_size()` — Waiting in Queue

```cpp
std::cout << "Queued: " << pool.queue_size() << "\n";
// Not yet picked up by a worker
```

### `active_count()` — Running Tasks

```cpp
std::cout << "Active: " << pool.active_count() << "\n";
// pending_count() - queue_size()
```

### `overflow_count()` — Active Overflow Threads

```cpp
std::cout << "Overflow: " << pool.overflow_count() << "\n";
// Temporary threads spawned when the queue swelled
```

### Relationship

```
pending_count() = queue_size() + active_count() + overflow_working

Example:
  pending = 100
  queue   = 60 (waiting)
  active  = 40 (handled by workers + overflow threads)
```

---

## 🎛️ 5) Priority System

### Worker Base Priority

The starting priority of all workers:

```cpp
knst_thread_pool pool(4, knst_thread_priority::high);

pool.start();

// Requested
std::cout << "Requested : " << (int)pool.worker_priority() << "\n";           // 1

// What the OS granted (can be refused)
std::cout << "Achieved  : " << (int)pool.worker_priority_achieved() << "\n";
```

### Dynamic Change

```cpp
pool.set_worker_priority(knst_thread_priority::low);
// Applies to new workers and subsequent tasks
```

### Per-Task Priority

Use `submit_with_priority()`:

```cpp
// This task should run at high priority
pool.submit_with_priority(knst_thread_priority::highest, []() {
    // critical work
});

// This task should run at low priority
pool.submit_with_priority(knst_thread_priority::lowest, []() {
    // background work
});
```

**How it works:**
1. The worker saves its base priority
2. Applies the task priority
3. Runs the task
4. **Restores its base priority**

**So:** Each task runs at its own priority without affecting the next one.

### Priority Enum

```cpp
enum class knst_thread_priority : int8_t {
    inherit       = -128,
    lowest        = -2,
    low           = -1,
    normal        = 0,
    high          = 1,
    highest       = 2,
    time_critical = 3
};
```

---

## 🌊 6) Overflow Mechanism

### When Does Overflow Open?

Worker count = 4, queue = 10 tasks:

```
Time 0: 4 workers + 10 tasks
         → 4 workers take 4 tasks
         → 6 tasks wait in queue
         → Queue larger than worker count (6 > 4)
         → Overflow thread is spawned!
```

### How It Works

```cpp
// If the queue exceeds the worker count
if (m_queue.size() > m_workers.size() &&
    m_overflow_count < KNST_OVERFLOW_MAX) {
    spawn_overflow();   // Spawns a temporary thread
}
```

**The overflow thread:**
1. Applies the priority
2. Takes **one** task from the queue
3. Runs it
4. Destroys itself

**Limit:** `KNST_OVERFLOW_MAX` (default 32)

### When Does It Close?

Overflow threads live for **one task**. When the task finishes, the `std::thread` ends and the counter decreases.

### Example Scenario

```cpp
knst_thread_pool pool(1);   // 1 worker!
pool.start();

// Submit 200 tasks
for (int i = 0; i < 200; ++i) {
    pool.submit([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });
}

// Immediately:
// - 1 worker is running
// - 32 overflow threads spawned (limit)
// - ~167 tasks waiting in queue
std::cout << "Active  : " << pool.active_count()  << "\n";   // ~33
std::cout << "Queued  : " << pool.queue_size()    << "\n";   // ~167
std::cout << "Overflow: " << pool.overflow_count() << "\n";   // ~32
```

**So:** Even with 1 worker, 200 tasks are processed with **33 parallel jobs**.

---

## 🔌 7) Shutdown Modes

### Graceful Shutdown (Default)

All queued tasks finish, then workers shut down:

```cpp
knst_thread_pool pool(2);
pool.start();

for (int i = 0; i < 100; ++i) {
    pool.submit([]() { /* work */ });
}

pool.shutdown(knst_shutdown::graceful);   // or just shutdown()

// All 100 tasks are now completed
```

**What it does:**
1. Sets `running = false`
2. Workers keep going **until the queue drains**
3. Then they shut down

### Force Shutdown

Queued tasks are **cancelled**, running tasks complete:

```cpp
pool.shutdown(knst_shutdown::force);

// Queued ones cancelled, running ones finished
```

**What it does:**
1. **Cancels** all queued tasks
   - Sets `finished_flag = true` for each
   - Pending `join()` calls wake up (return empty)
2. Sets `running = false`
3. Running tasks finish normally

**Use case:** Fast shutdown on application exit.

### Force Shutdown Example

```cpp
knst_thread_pool pool(1);
pool.start();

// 200 tasks — ~33 start immediately, 167 wait in queue
for (int i = 0; i < 200; ++i) {
    pool.submit([&started, &completed]() {
        started.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        completed.fetch_add(1);
    });
}

std::this_thread::sleep_for(std::chrono::milliseconds(30));

pool.shutdown(knst_shutdown::force);

std::cout << "Started  : " << started.load() << " / 200\n";   // ~33
std::cout << "Completed: " << completed.load() << " / 200\n"; // ~33
// 167 tasks cancelled
```

---

## 🔗 8) `knst_thread` Integration

A task submitted to the pool returns a `knst_thread` — joinable:

### `knst_thread::start(pool, fn)`

```cpp
knst_thread_pool pool(2);
pool.start();

knst_thread t;
bool ok = t.start(pool, []() {
    std::cout << "Running in pool\n";
});

std::cout << "is_pooled(): " << t.is_pooled() << "\n";   // true

t.join();
pool.shutdown();
```

### `knst_thread::start_with_priority(pool, prio, fn)`

```cpp
knst_thread t;
t.start_with_priority(
    pool,
    knst_thread_priority::high,
    []() { /* critical work */ }
);

t.join();
```

### Differences of a Pooled Thread

**Methods that do not work on pooled threads:**

- ❌ `kill()` — can corrupt the pool
- ❌ `detach()` — the pool manages it
- ✅ `join()` — works (the pool signals "finished")

---

## 📊 9) Performance Notes

### Batch Pulling

Each worker pulls **8 tasks** at a time from the queue (`KNST_WORKER_BATCH_SIZE`):

```cpp
while (batch.size() < KNST_WORKER_BATCH_SIZE && m_queue.pop(item)) {
    batch.push_back(std::move(item));
}
```

**The gain:** Lock cost is reduced **8x** — no separate lock per task.

### When Is It Slow?

- **Very small tasks** — overhead remains despite batch pulling
- **Long-running tasks** — new workers are not spawned while others are busy (except overflow)
- **Excessive overflow** — beyond 32 temporary threads, the OS pays the price

### Comparison

```cpp
// ❌ Slow: new thread per task
auto start = std::chrono::steady_clock::now();
knst_vector<knst_thread> threads;
for (int i = 0; i < 10000; ++i) {
    knst_thread t;
    t.start([]() { /* light work */ });
    threads.push_back(std::move(t));
}
for (auto& t : threads) t.join();
auto end = std::chrono::steady_clock::now();
// ~2000-5000 ms

// ✅ Fast: use a pool
start = std::chrono::steady_clock::now();
knst_thread_pool pool;
pool.start();
for (int i = 0; i < 10000; ++i) {
    pool.submit([]() { /* light work */ });
}
pool.wait_all();
pool.shutdown();
end = std::chrono::steady_clock::now();
// ~50-200 ms
```

**The gain:** **10-100x**.

### Choosing Worker Count

| Workload | Recommended workers |
|---|---|
| CPU-bound (computation) | `hardware_concurrency()` |
| I/O-bound (file, network) | 2x `hardware_concurrency()` |
| Mixed | 1.5x `hardware_concurrency()` |
| Just a few heavy tasks | 2-4 |

---

## ⚙️ 10) Configuration Macros

Put these in `knst_settings.hpp`:

```cpp
#define KNST_OVERFLOW_MAX 32          // Overflow thread limit
#define KNST_QUEUE_MAX 1024           // Queue capacity
#define KNST_DEFAULT_WORKER_COUNT 0   // 0 = auto
#define KNST_WORKER_BATCH_SIZE 8      // Tasks pulled per worker
```

### Macro Descriptions

| Macro | Default | What it does |
|---|---|---|
| `KNST_OVERFLOW_MAX` | 32 | Max temporary threads |
| `KNST_QUEUE_MAX` | 1024 | `submit()` returns false when the queue is full |
| `KNST_DEFAULT_WORKER_COUNT` | 0 | 0 = `hardware_concurrency()` |
| `KNST_WORKER_BATCH_SIZE` | 8 | Amortizes lock cost |

---

## 🌍 11) Platform Differences

| Topic | Windows | POSIX |
|---|---|---|
| **Thread** | `std::thread` | `std::thread` |
| **Priority** | `SetThreadPriority` | `setpriority` |
| **Baseline** | `IDLE_PRIORITY_CLASS` vs | nice value |
| **Overflow** | Same | Same |

**Note:** Because `std::thread` is used, the behavior is largely identical.

---

## 🔥 12) Real-World Examples

### Example 1: Parallel Data Processing

```cpp
void process_data(knst_vector<int>& data) {
    knst_thread_pool pool;
    pool.start();

    std::atomic<int> sum{0};

    for (int value : data) {
        pool.submit([&sum, value]() {
            // Heavy computation
            sum.fetch_add(value * value);
        });
    }

    pool.wait_all();
    pool.shutdown();

    std::cout << "Total: " << sum.load() << "\n";
}
```

### Example 2: Web Crawler

```cpp
void crawl_urls(const knst_vector<knst_c16string>& urls) {
    knst_thread_pool pool(8);   // I/O-bound, 2x CPU
    pool.start();

    std::atomic<int> downloaded{0};

    for (const auto& url : urls) {
        pool.submit([url, &downloaded]() {
            // HTTP request (I/O)
            auto response = http_get(url);
            save_to_file(url, response);
            downloaded.fetch_add(1);
        });
    }

    pool.wait_all();
    std::cout << "Downloaded: " << downloaded.load() << "\n";
    pool.shutdown();
}
```

### Example 3: Server Request Handler

```cpp
class Server {
    knst_thread_pool m_pool;

public:
    Server() : m_pool(16) {
        m_pool.start();
    }

    void handle_request(int client_fd) {
        m_pool.submit([client_fd]() {
            // Process the request
            process_request(client_fd);
            close(client_fd);
        });
    }

    ~Server() {
        m_pool.shutdown(knst_shutdown::graceful);
    }
};
```

### Example 4: Priority Scheduling

```cpp
void process_jobs() {
    knst_thread_pool pool(4);
    pool.start();

    // High-priority jobs
    for (int i = 0; i < 10; ++i) {
        pool.submit_with_priority(
            knst_thread_priority::high,
            [i]() { handle_urgent(i); }
        );
    }

    // Normal jobs
    for (int i = 0; i < 100; ++i) {
        pool.submit([i]() { handle_normal(i); });
    }

    // Low-priority jobs
    for (int i = 0; i < 1000; ++i) {
        pool.submit_with_priority(
            knst_thread_priority::lowest,
            [i]() { handle_background(i); }
        );
    }

    pool.wait_all();
    pool.shutdown();
}
```

### Example 5: Progress Monitoring

```cpp
void process_with_progress() {
    knst_thread_pool pool;
    pool.start();

    std::atomic<int> done{0};
    constexpr int total = 1000;

    for (int i = 0; i < total; ++i) {
        pool.submit([&done]() {
            // Work
            done.fetch_add(1);
        });
    }

    // Show progress
    while (done.load() < total) {
        std::cout << "\rProgress: " << done.load() << "/" << total
                  << "  (pending=" << pool.pending_count()
                  << ", active=" << pool.active_count() << ")" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "\nDone!\n";

    pool.shutdown();
}
```

### Example 6: Graceful vs Force Shutdown

```cpp
void test_shutdown_modes() {
    // Graceful — everything finishes
    {
        knst_thread_pool pool(2);
        pool.start();

        std::atomic<int> done{0};
        for (int i = 0; i < 100; ++i) {
            pool.submit([&done]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                done.fetch_add(1);
            });
        }

        pool.shutdown(knst_shutdown::graceful);
        std::cout << "Graceful: " << done.load() << " / 100\n";   // 100
    }

    // Force — queued tasks cancelled
    {
        knst_thread_pool pool(2);
        pool.start();

        std::atomic<int> done{0};
        for (int i = 0; i < 100; ++i) {
            pool.submit([&done]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                done.fetch_add(1);
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        pool.shutdown(knst_shutdown::force);
        std::cout << "Force: " << done.load() << " / 100\n";   // ~10-40
    }
}
```

### Example 7: Batch Image Processing

```cpp
void process_images(const knst_vector<knst_c16string>& paths) {
    knst_thread_pool pool(8);
    pool.start();

    std::atomic<int> processed{0};

    for (const auto& path : paths) {
        pool.submit([path, &processed]() {
            knst_image img = knst_image_loader::load(path);

            if (img) {
                // Generate a thumbnail
                knst_image_load_options o;
                o.resize_width  = 256;
                o.resize_height = 256;
                o.keep_aspect   = true;

                auto thumb = knst_image_loader::load(path, o);
                // thumbs.push_back(thumb);   // sync required
            }

            processed.fetch_add(1);
        });
    }

    pool.wait_all();
    std::cout << processed.load() << " images processed\n";
    pool.shutdown();
}
```

---

## 🛡️ 13) Exception Safety

### Exception Inside a Task

If a task throws, the pool **swallows** it and continues normally:

```cpp
knst_thread_pool pool(2);
pool.start();

for (int i = 0; i < 10; ++i) {
    pool.submit([i]() {
        if (i == 5) throw std::runtime_error("error!");
        // the rest run normally
    });
}

pool.wait_all();   // ✅ Finishes fine
pool.shutdown();
```

**But you cannot see the exception!** For that, use `std::exception_ptr`:

```cpp
std::mutex err_mtx;
knst_vector<std::exception_ptr> errors;

for (int i = 0; i < 10; ++i) {
    pool.submit([&]() {
        try {
            // risky work
            throw std::runtime_error("error");
        } catch (...) {
            std::lock_guard<std::mutex> lock(err_mtx);
            errors.push_back(std::current_exception());
        }
    });
}

pool.wait_all();

for (const auto& e : errors) {
    try { std::rethrow_exception(e); }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
    }
}
```

### Destructor Behavior

The pool destructor does an **automatic force shutdown**:

```cpp
{
    knst_thread_pool pool(2);
    pool.start();
    // submit(...);
    // shutdown was never called
}   // destructor → force shutdown → queue is not drained
```

**But:** If you want a graceful shutdown, call `pool.shutdown(knst_shutdown::graceful)` manually.

---

## 🎯 What It Buys You

`knst_thread_pool` provides serious advantage in these situations:

- ✅ **Many tasks** — a pool is essential when you have 100+ tasks
- ✅ **Server applications** — request handling
- ✅ **Batch processing** — images, data, files
- ✅ **Parallel computation** — CPU-bound work
- ✅ **Web scraping** — I/O-bound, many requests
- ✅ **Priority scheduling** — critical work moves to the front