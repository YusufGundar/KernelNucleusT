/*
----------------------------
knst_thread_pool.hpp
----------------------------

    The thread pool distributes tasks to worker threads, amortizing the cost of thread creation. If the queue fills up, it spawns overflow threads (up to a maximum of 32), which shut down once the workload subsides. It supports priority settings: both the base priority of workers and temporary, task-specific priorities can be configured. The `submit` method dispatches tasks, and the returned `knst_thread` object allows for waiting on completion (via `join` or `join_for`)

*/


#pragma once
#include <thread>

#ifndef KNST_OVERFLOW_MAX
    #define KNST_OVERFLOW_MAX 32 // Maximum number of temporary threads that can be opened when the queue fills up
#endif

#ifndef KNST_QUEUE_MAX
    #define KNST_QUEUE_MAX 1024 // Maximum number of jobs the queue can hold
#endif

#ifndef KNST_DEFAULT_WORKER_COUNT
    #define KNST_DEFAULT_WORKER_COUNT 0 // Default number of workers (0 ==> "auto-detect", hardware_concurrency())
#endif

#ifndef KNST_WORKER_BATCH_SIZE 
    #define KNST_WORKER_BATCH_SIZE 8 // Number of jobs a worker pulls from the queue at once (amortizes the locking cost)
#endif

class knst_thread_pool {

private:
    friend class knst_thread; // It declares the `knst_thread` class as a friend. This means `knst_thread` can access the private members of `knst_thread_pool` (such as `submit_to`), because the body of `knst_thread::start(pool, fn)` calls `submit_to`—which is private and inaccessible from the outside

    static size_t resolve_default_workers() noexcept { // It determines the default number of workers. It reads the number of CPU cores; if it returns 0 (meaning the count is unknown), it uses 3. In other words, it answers the question: "How many threads should be started if the user hasn't specified a number?"
        unsigned hc = std::thread::hardware_concurrency();
        return hc == 0 ? 3 : (size_t)hc;
    }

    bool submit_to(knst_thread& t, knst_function fn, knst_thread_priority prio = knst_thread_priority::inherit) noexcept { // It dispatches work to the pool—the actual engine behind `submit` calls. It creates a new `knst_thread_data` object, sets `is_pooled` to `true`, pushes it onto the queue, and increments the pending count. If the queue size exceeds the number of workers, it spawns an overflow thread. Finally, it links the data to the thread object (`t.m_data = data`) so that the user can call `join`
                   
        if (!m_running.load(std::memory_order_acquire)) return false;
        if (fn.empty()) return false;

        auto data = std::make_shared<knst_thread_data>();
        data->task = std::move(fn);
        data->is_pooled = true;
        data->requested_priority.store(static_cast<int8_t>(prio),std::memory_order_relaxed);
                                       
        data->state.store((uint8_t)knst_thread_state::running,std::memory_order_release);
                          
        bool spawn_overflow_now = false;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            if (!m_running.load(std::memory_order_acquire)) return false;
            if (m_queue.size() >= KNST_QUEUE_MAX) return false;

            if (!m_queue.push(data)) return false;
            m_pending.fetch_add(1, std::memory_order_acq_rel);

            if (m_queue.size() > m_workers.size() &&
                m_overflow_count.load(std::memory_order_acquire) < KNST_OVERFLOW_MAX) {
                spawn_overflow_now = true;
            }
        }

        if (spawn_overflow_now) spawn_overflow();
        m_cv.notify_one();

        t.m_data = std::move(data);
        return true;
    }

    void apply_worker_baseline() noexcept { // It applies the worker's base priority. It reads the priority set by the pool and assigns it to the current thread. This is called after the task-based priority concludes—returning the worker to its normal state
        auto requested = static_cast<knst_thread_priority>(m_worker_priority.load(std::memory_order_acquire));
            
        knst_thread_priority achieved;
        if (knst_apply_priority_self_graceful(requested, &achieved)) {
            m_worker_priority_achieved.store(static_cast<int8_t>(achieved),std::memory_order_relaxed);
                                             
        }
    }

    void worker_loop() noexcept { // The worker thread's main loop. It first applies the base priority. Then, within an infinite loop: it waits on the condition variable (until work arrives or a shutdown occurs), pulls work from the queue in batches (of 8), and executes the tasks outside the lock. If there is no work and `!running` is true, it exits
        apply_worker_baseline();

        std::vector<std::shared_ptr<knst_thread_data>> batch;
        batch.reserve(KNST_WORKER_BATCH_SIZE);

        while (true) {
            batch.clear();
            {
                std::unique_lock<std::mutex> lock(m_mtx);
                m_cv.wait(lock, [&]{
                    return !m_running.load(std::memory_order_acquire) ||!m_queue.empty();
                });

                if (!m_running.load(std::memory_order_acquire) &&  m_queue.empty()) return;
                   

                std::shared_ptr<knst_thread_data> item;
                while (batch.size() < KNST_WORKER_BATCH_SIZE && m_queue.pop(item)) {
                       
                    batch.push_back(std::move(item));
                }
            }
            for (auto& data : batch) run_task(data);
        }
    }

    void overflow_loop() noexcept { // The task of the overflow thread: It applies base priority, pulls a single job from the queue, executes it, and terminates. In other words, it is a temporary helper thread—spawned for just one task
        apply_worker_baseline();

        std::shared_ptr<knst_thread_data> data;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            if (!m_queue.pop(data)) return;
        }
        run_task(data);
    }

    void run_task(std::shared_ptr<knst_thread_data>& data) noexcept { // Executes a single task. Applies task-based priority if applicable, calls `task()` (swallowing exceptions), and then reverts the worker to its base priority. Sets the `finished_flag`, decrements the pending count, and—if it is the final task—wakes up those waiting on `wait_all`
        auto task_prio = static_cast<knst_thread_priority>(data->requested_priority.load(std::memory_order_acquire));
            
        bool reprioritized = task_prio != knst_thread_priority::inherit;

        if (reprioritized) {
            knst_thread_priority achieved;
            if (knst_apply_priority_self_graceful(task_prio, &achieved)) {
                data->achieved_priority.store(static_cast<int8_t>(achieved),std::memory_order_relaxed);
                                              
            }
        }

        


       
        try {
            data->task();
        } catch (...) {}
         
        

        if (reprioritized) {
            apply_worker_baseline();
        }

        {


            std::lock_guard<std::mutex> lock(data->mtx);
            data->finished_flag = true;

        }
        data->cv.notify_all();
        data->state.store((uint8_t)knst_thread_state::finished,std::memory_order_release);
                          

        if (m_pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            m_done_cv.notify_all();
        }
    }

    void spawn_overflow() noexcept { // It spawns an overflow thread. This acts as a temporary helper when the queue is full: it picks up a task, executes it, and finishes. It is detached (terminates on its own). A counter tracks these threads (incrementing/decrementing), and the shutdown process waits for them to complete. If the `std::thread` cannot be launched, the counter is rolled back
        m_overflow_count.fetch_add(1, std::memory_order_acq_rel);
        try {
            std::thread([this]{
                overflow_loop();
                std::lock_guard<std::mutex> lock(m_overflow_mtx);
                m_overflow_count.fetch_sub(1, std::memory_order_acq_rel);
                m_overflow_done_cv.notify_all();
            }).detach();
        } catch (...) {
            std::lock_guard<std::mutex> lock(m_overflow_mtx);
            m_overflow_count.fetch_sub(1, std::memory_order_acq_rel);
            m_overflow_done_cv.notify_all();
        }
    }

    std::atomic<size_t> m_configured_workers; // How many workers will there be (atomic)
    std::atomic<int8_t> m_worker_priority; // The workers' grassroots priority
    std::atomic<int8_t> m_worker_priority_achieved; // The actual base priority obtained
    std::atomic<bool> m_running{false}; // Is the pool open
    std::atomic<size_t> m_pending{0}; // Total number of jobs in the queue plus those being processed

    mutable std::mutex m_mtx; // Queue and state protection
    std::condition_variable m_cv; // To wake up the workers (work has arrived)
    std::condition_variable m_done_cv; // For those waiting on wait_all (task complete)
    knst_thread_queue<std::shared_ptr<knst_thread_data>> m_queue; // Work queue — shared_ptr<knst_thread_data>
    std::vector<std::thread> m_workers; // Worker threads (std::thread vector)

    std::atomic<int> m_overflow_count{0}; // How many overflow threads are currently active
    std::mutex m_overflow_mtx; // Overflow counter protection
    std::condition_variable m_overflow_done_cv; // To wait for the overflows to finish



public:
    explicit knst_thread_pool( // Constructor. If the worker count is 0, it is determined automatically (based on the number of CPU cores). Sets the base priority of the workers. `explicit` ==> prevents single-parameter implicit conversion
        size_t worker_count = 0,
        knst_thread_priority worker_priority = knst_thread_priority::inherit) noexcept : m_configured_workers(worker_count != 0 ? worker_count : resolve_default_workers()),
        m_worker_priority(static_cast<int8_t>(worker_priority)),
        m_worker_priority_achieved(static_cast<int8_t>(knst_thread_priority::inherit)) {}

    ~knst_thread_pool() { // Destructor. Performs a forced shutdown if the pool is still running—to prevent resource leaks. Automatic cleanup in case the user forgets to call shutdown
        if (m_running.load(std::memory_order_acquire)) {
            shutdown(knst_shutdown::force);
        }
    }

    // It forbids copying. The pool is not merely move-only but entirely non-copyable—because it contains a mutex, a condition variable, and threads. It cannot be copied
    knst_thread_pool(const knst_thread_pool&) = delete;
    knst_thread_pool& operator=(const knst_thread_pool&) = delete;

    bool start() noexcept { // Starts the pool — with the number of workers specified in the constructor. Delegates to the actual start(worker_count)
        return start(m_configured_workers.load(std::memory_order_acquire));
    }

    bool start(size_t worker_count) noexcept { // Starts the pool. It sets `running` to true, allocates space for the queue, and spawns `worker_count` threads—all executing `worker_loop`. If a thread cannot be spawned, it cleans up and returns `false`. It rejects the request if the pool is already running or if the count is zero
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            if (m_running.load(std::memory_order_acquire)) return false;
            if (worker_count == 0) return false;
            m_running.store(true, std::memory_order_release);
            m_configured_workers.store(worker_count, std::memory_order_release);
            m_queue.reserve(KNST_QUEUE_MAX);
        }

        try {
            m_workers.reserve(worker_count);
            for (size_t i = 0; i < worker_count; ++i) {
                m_workers.emplace_back([this]{ worker_loop(); });
            }
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                m_running.store(false, std::memory_order_release);
            }
            m_cv.notify_all();
            for (auto& t : m_workers) {
                if (t.joinable()) t.join();
            }
            m_workers.clear();
            return false;
        }
        return true;
    }

    bool shutdown(knst_shutdown mode = knst_shutdown::graceful) noexcept { // It shuts down the pool. `force` cancels the tasks in the queue (marks them as finished and wakes up waiting threads). It sets `running` to `false`, wakes up the workers, and joins them all. It waits for overflow threads to complete
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            if (!m_running.load(std::memory_order_acquire)) return false;

            if (mode == knst_shutdown::force) {
                std::shared_ptr<knst_thread_data> data;
                while (m_queue.pop(data)) {
                    {
                        std::lock_guard<std::mutex> dlock(data->mtx);
                        data->finished_flag = true;
                    }
                    data->cv.notify_all();
                    data->state.store((uint8_t)knst_thread_state::finished,std::memory_order_release);
                    m_pending.fetch_sub(1, std::memory_order_acq_rel);
                }
                m_done_cv.notify_all();
            }

            m_running.store(false, std::memory_order_release);
        }
        m_cv.notify_all();

        for (auto& t : m_workers) {
            if (t.joinable()) t.join();
        }
        m_workers.clear();

        {
            std::unique_lock<std::mutex> lock(m_overflow_mtx);
            m_overflow_done_cv.wait(lock, [&]{
                return m_overflow_count.load(std::memory_order_acquire) == 0;
            });
        }
        return true;
    }

    bool is_running() const noexcept { // Check if the pool is running. true ==> active
        return m_running.load(std::memory_order_acquire);
    }

    template<typename F>
    knst_thread submit(F&& fn) noexcept { // It submits the job. It wraps the lambda in `knst_function` and delegates to the actual `submit(knst_function)`. The user provides the lambda directly
        return submit(knst_function(std::forward<F>(fn)));
    }

    knst_thread submit(knst_function fn) noexcept { // The actual submit takes a `knst_function`. It creates an empty `knst_thread`, passes it to `submit_to`, and returns the populated `t`. The user can then join using this `t`
        knst_thread t;
        submit_to(t, std::move(fn), knst_thread_priority::inherit);
        return t;
    }

    template<typename F, typename Cb>
    knst_thread submit(F&& fn, Cb&& cb) noexcept { // It takes the task and the callback, wraps both into a single lambda (executing f() followed by c()), converts it to a `knst_function`, and delegates it to `submit`
        return submit(knst_function(
            [f = std::forward<F>(fn), c = std::forward<Cb>(cb)]() mutable {
                f();
                c();
            }
        ));
    }

    template<typename F>
    knst_thread submit_with_priority(knst_thread_priority prio, F&& fn) noexcept { // Submits a job with a priority. It wraps the lambda in `knst_function` and passes it to `submit_to` with the specified priority. The worker temporarily changes the priority while executing the job, then reverts to the base priority
        knst_thread t;
        submit_to(t, knst_function(std::forward<F>(fn)), prio);
        return t;
    }

    template<typename F, typename Cb>
    knst_thread submit_with_priority(knst_thread_priority prio,F&& fn, Cb&& cb) noexcept { // It submits the priority task + the callback. It wraps both into a single lambda, converts it to a `knst_function`, and sends it to `submit_to` with the priority
        knst_thread t;
        submit_to(t, knst_function(
            [f = std::forward<F>(fn), c = std::forward<Cb>(cb)]() mutable {
                f();
                c();
            }
        ), prio);
        return t;
    }

    bool wait_all() noexcept { // It waits for all tasks to complete. It waits on `m_done_cv` until `m_pending == 0`. Whenever a task finishes, `run_task` calls `m_done_cv.notify_all()` if it is the last task
        std::unique_lock<std::mutex> lock(m_mtx);
        m_done_cv.wait(lock, [&]{
            return m_pending.load(std::memory_order_acquire) == 0;
        });
        return true;
    }

    bool wait_all_for(uint32_t ms) noexcept { // A version of `wait_all` with a timeout. It waits for a maximum of `ms` milliseconds. Returns `true` if the tasks complete within that time, or `false` if a timeout occurs
        std::unique_lock<std::mutex> lock(m_mtx);
        return m_done_cv.wait_for(
            lock, std::chrono::milliseconds(ms),
            [&]{ return m_pending.load(std::memory_order_acquire) == 0; });
    }

    size_t worker_count() const noexcept { // It returns the number of workers
        return m_configured_workers.load(std::memory_order_acquire);
    }

    size_t pending_count() const noexcept { // Returns the total number of pending and running jobs
        return m_pending.load(std::memory_order_acquire);
    }

    size_t queue_size() const noexcept { // Returns the number of jobs waiting in the queue (not yet picked up by a worker). Reads under a lock
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_queue.size();
    }

    size_t active_count() const noexcept { // It returns the number of jobs currently being processed. pending - queued = jobs held by workers + jobs being processed by the overflow
        size_t pending = m_pending.load(std::memory_order_acquire);
        std::lock_guard<std::mutex> lock(m_mtx);
        size_t queued = m_queue.size();
        return pending > queued ? pending - queued : 0;
    }

    size_t overflow_count() const noexcept { // Returns the current number of active overflow threads
        return (size_t)m_overflow_count.load(std::memory_order_acquire);
    }

    void set_worker_priority(knst_thread_priority p) noexcept { // Sets the baseline priority for workers. However, it does not apply immediately to active workers—it only takes effect for new workers and when `apply_worker_baseline` is called after a task
        m_worker_priority.store(static_cast<int8_t>(p), std::memory_order_relaxed);
    }

    knst_thread_priority worker_priority() const noexcept { // Returns the adjusted base priority of the workers
        return static_cast<knst_thread_priority>( m_worker_priority.load(std::memory_order_relaxed));
           
    }

    knst_thread_priority worker_priority_achieved() const noexcept { // The base priority actually obtained by the workers applies. If the OS has rejected the requested level, the result here may differ
        return static_cast<knst_thread_priority>(m_worker_priority_achieved.load(std::memory_order_relaxed));
            
    }



};


template<typename F>
bool knst_thread::start(knst_thread_pool& pool, F fn) { // The body of knst_thread::start(pool, fn). It dispatches the thread to the pool. It checks the existing m_data (rejecting it if already running), resets it, wraps it in a knst_function, and passes it to pool.submit_to. The circular dependency is resolved here—the full definition of the pool is in this file
    if (m_data) {
        auto s = state();
        if (s == knst_thread_state::running || s == knst_thread_state::finished) return false;
            
    }
    m_data.reset();

    knst_function f(std::move(fn));
    if (f.empty()) return false;

    return pool.submit_to(*this, std::move(f));
}

template<typename F>
bool knst_thread::start_with_priority(knst_thread_pool& pool,knst_thread_priority prio, F fn) { // The priority version of `knst_thread::start(pool, fn)`. It follows the same logic but passes the priority to `submit_to`. The worker applies a temporary priority while executing the task
    if (m_data) {
        auto s = state();
        if (s == knst_thread_state::running || s == knst_thread_state::finished) return false;
            
    }
    m_data.reset();

    knst_function f(std::move(fn));
    if (f.empty()) return false;

    return pool.submit_to(*this, std::move(f), prio);
}