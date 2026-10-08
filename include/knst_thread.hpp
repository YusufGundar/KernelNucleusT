// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
----------------------------
knst_thread.hpp
----------------------------

    It is an advanced thread library used for thread management; it can utilize pre-existing threads via a thread pool if desired and includes features such as priority setting, allowing you to use it for various purposes as you see fit

*/



#pragma once


#include <condition_variable>


#if KNST_USING_PLATFORM_WINDOWS
#else
    #include <pthread.h>
    #if defined(__GLIBCXX__) || defined(__GLIBC__)

        #include <cxxabi.h>
    #endif
#endif

class knst_thread_pool;

enum class knst_shutdown : uint8_t { // Specifies the shutdown mode. graceful ==> finish queued jobs; force ==>  cancel queued jobs
    graceful,
    force
};

enum class knst_thread_state : uint8_t { // It tracks the state of the thread: `idle` (inactive), `running` (executing), `finished` (task complete), `joined` (joined), and `detached` (left to run independently)
    idle = 0,
    running = 1,
    finished = 2,
    joined = 3,
    detached = 4
};


/*
    Data shared between the `Thread` and the `knst_thread` object: `mtx`,
    `cv`, and `finished_flag` for synchronization; `task` for the work to be executed;
    `state` for the current status; `is_pooled` to indicate if it is in the pool;
    and `self_ref`, a `shared_ptr` holding the thread itself (to allow for a single allocation). At the lower level, there are the platform-specific handle and `native_tid`
*/ 
struct knst_thread_data { 
    std::mutex mtx;
    std::condition_variable cv;
    bool finished_flag = false;
    std::atomic<uint8_t> state{0};
    knst_function task;
    bool is_pooled = false;

    std::atomic<int8_t> requested_priority{static_cast<int8_t>(knst_thread_priority::inherit)};
        
    std::atomic<int8_t> achieved_priority{static_cast<int8_t>(knst_thread_priority::inherit)};

    std::shared_ptr<knst_thread_data> self_ref;

    #if KNST_USING_PLATFORM_WINDOWS
        HANDLE handle = nullptr;
    #else
        pthread_t handle{};
        bool handle_valid = false;
        std::atomic<int32_t> native_tid{-1};
    #endif

    knst_thread_data() = default;
    knst_thread_data(const knst_thread_data&) = delete;
    knst_thread_data& operator=(const knst_thread_data&) = delete;
};


class knst_thread {
private:
    friend class knst_thread_pool; // It declares the `knst_thread_pool` class as a friend. This means the pool class can access the private members of `knst_thread` (such as `m_data`). This is necessary for the pool to manage the thread internally
    std::shared_ptr<knst_thread_data> m_data; // The shared_ptr holding knst_thread_data. The thread object and the OS thread share this data. When moved, the pointer changes hands; it is not copied

    template<typename F>
    bool start_impl(F fn, knst_thread_priority prio) { // The primary startup engine. It creates a new `knst_thread_data`, sets the task, holds a reference to itself via `self_ref`, and then spawns the OS thread (`CreateThread`/`pthread_create`). If an error occurs, it performs cleanup and returns `false`
        if (m_data) {
            auto s = state();
            if (s == knst_thread_state::running ||s == knst_thread_state::finished) return false;
                
        }
        m_data.reset();

        auto data = std::make_shared<knst_thread_data>();
        data->task = knst_function(std::move(fn));
        if (data->task.empty()) return false;

        data->is_pooled = false;
        data->requested_priority.store(static_cast<int8_t>(prio),std::memory_order_relaxed);
                                       
        data->state.store((uint8_t)knst_thread_state::running,std::memory_order_release);
                          
      
        data->self_ref = data;

        #if KNST_USING_PLATFORM_WINDOWS
            HANDLE h = CreateThread(nullptr, 0,&knst_thread::trampoline_win, data.get(), 0, nullptr);
            if (h == nullptr) {
                data->self_ref.reset();
                data->state.store((uint8_t)knst_thread_state::idle,std::memory_order_release);
                                
                return false;
            }
            data->handle = h;
        #else
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

            int rc = pthread_create(&data->handle, &attr,&knst_thread::trampoline_posix, data.get());
                                    
            pthread_attr_destroy(&attr);

            if (rc != 0) {
                data->self_ref.reset();
                data->state.store((uint8_t)knst_thread_state::idle,std::memory_order_release);
                                
                return false;
            }
            data->handle_valid = true;
        #endif

        m_data = std::move(data);
        return true;
    }

    void cleanup() noexcept { // Destructor cleanup: If it is running, it detaches; if finished, it joins (to prevent resource leaks). If pooled, it does not touch the OS handle. Then, `m_data` is released
        if (!m_data) return;
        auto s = state();

        if (s == knst_thread_state::running) {
            if (!m_data->is_pooled) {
            #if KNST_USING_PLATFORM_WINDOWS
                if (m_data->handle) {
                    CloseHandle(m_data->handle);
                    m_data->handle = nullptr;
                }
            #else
                if (m_data->handle_valid) {
                    pthread_detach(m_data->handle);
                    m_data->handle_valid = false;
                }
            #endif
            }
        } else if (s == knst_thread_state::finished) {
            if (!m_data->is_pooled) {
                #if KNST_USING_PLATFORM_WINDOWS
                    if (m_data->handle) {
                        WaitForSingleObject(m_data->handle, INFINITE);
                        CloseHandle(m_data->handle);
                        m_data->handle = nullptr;
                    }
                #else
                    if (m_data->handle_valid) {
                        pthread_join(m_data->handle, nullptr);
                        m_data->handle_valid = false;
                    }
                #endif
            }
        }
        m_data.reset(); 
    }

    #if KNST_USING_PLATFORM_WINDOWS
        static DWORD WINAPI trampoline_win(LPVOID arg) {

            auto* self = static_cast<knst_thread_data*>(arg);

            knst_thread_priority requested = static_cast<knst_thread_priority>(
                self->requested_priority.load(std::memory_order_acquire));
            knst_thread_priority achieved;
            if (knst_apply_priority_self_graceful(requested, &achieved)) {
                self->achieved_priority.store(static_cast<int8_t>(achieved), std::memory_order_relaxed);
                                           
            }

            try { self->task(); } catch (...) {}

            {
                std::lock_guard<std::mutex> lock(self->mtx);
                self->finished_flag = true;
            }
            self->cv.notify_all();
            self->state.store((uint8_t)knst_thread_state::finished,std::memory_order_release);
                            

            self->self_ref.reset();

            return 0;
        }
    #else
        static void* trampoline_posix(void* arg) {
            auto* self = static_cast<knst_thread_data*>(arg);

            self->native_tid.store(knst_current_native_tid(),std::memory_order_release);
                                

            knst_thread_priority requested = static_cast<knst_thread_priority>(self->requested_priority.load(std::memory_order_acquire));
                
            knst_thread_priority achieved;
            if (knst_apply_priority_self_graceful(requested, &achieved)) {
                self->achieved_priority.store(static_cast<int8_t>(achieved),std::memory_order_relaxed);
                                            
            }

            try {
                self->task();
            }

    #if defined(__GLIBCXX__) || defined(__GLIBC__)
            catch (abi::__forced_unwind&) {
                throw;
            }
    #endif
            catch (...) {
            }

            {
                std::lock_guard<std::mutex> lock(self->mtx);
                self->finished_flag = true;
            }
            self->cv.notify_all();
            self->state.store((uint8_t)knst_thread_state::finished,std::memory_order_release);
                            

            self->self_ref.reset();
            return nullptr;
        }
    #endif


public:
    knst_thread() noexcept = default; // Default constructor — creates an empty thread object. m_data is null; not yet initialized

    ~knst_thread() { cleanup(); } // Destructor — calls cleanup() when the object is destroyed. If the thread has not been joined, it automatically performs a detach or join, ensuring no resource leaks

    knst_thread(knst_thread&& other) noexcept : m_data(std::move(other.m_data)) {} // The move constructor steals the `shared_ptr` from the `m_data` member of the `other` object. There is no copying; the source is emptied

    knst_thread& operator=(knst_thread&& other) noexcept { // Move assignment — first cleans up its own contents, then steals `other`'s `m_data`. It includes a self-assignment check
        if (this != &other) {
            cleanup();
            m_data = std::move(other.m_data);
        }
        return *this;
    }

    // It prohibits copying. The Thread object is move-only—resource ownership remains in a single place
    knst_thread(const knst_thread&) = delete;
    knst_thread& operator=(const knst_thread&) = delete;
    
    template<typename F> 
    bool start(F fn) { // Starts the thread with the given function. Delegates to start_impl with the default priority (inherit). Returns true on success
        return start_impl(std::move(fn), knst_thread_priority::inherit);
    }

    template<typename F, typename Cb>
    bool start(F fn, Cb cb) { // It starts the thread using a function and a callback. The callback is invoked when the task completes. It wraps both into a single lambda and delegates to `start`
        return start([f = std::move(fn), c = std::move(cb)]() mutable {
            f();
            c();
        });
    }

    template<typename F>
    bool start_with_priority(knst_thread_priority prio, F fn) { // Starts the thread with the specified priority. It passes the priority to start_impl. It differs from start(fn) in that it accepts a priority parameter
        return start_impl(std::move(fn), prio);
    }

    template<typename F, typename Cb> 
    bool start_with_priority(knst_thread_priority prio, F fn, Cb cb) { // Launch with priority and callback. Wraps the task and callback into a single lambda and delegates to `start_with_priority(prio, lambda)`
        return start_with_priority(prio, [f = std::move(fn), c = std::move(cb)]() mutable {
                f();
                c();
            });
    }

    template<typename F>
    bool start(knst_thread_pool& pool, F fn); // It submits the thread to the pool—it does not spawn its own OS thread. Its body is defined in `knst_thread_pool.hpp` (the full definition of the pool is required)

    template<typename F, typename Cb>
    bool start(knst_thread_pool& pool, F fn, Cb cb) { // It sends the thread to the pool along with a function and a callback. It wraps both into a single lambda and delegates the task to `start(pool, lambda)`
        return start(pool, [f = std::move(fn), c = std::move(cb)]() mutable {
            f();
            c();
        });
    }

    template<typename F>
    bool start_with_priority(knst_thread_pool& pool, knst_thread_priority prio, F fn); // Priority dispatch to the pool — notification only. Its implementation will be defined in knst_thread_pool.hpp            

    template<typename F, typename Cb>
    bool start_with_priority(knst_thread_pool& pool, knst_thread_priority prio, F fn, Cb cb) { // Submission to the pool with priority and a callback. It wraps the task and the callback into a single lambda and delegates to `start_with_priority(pool, prio, lambda)`
        return start_with_priority(pool, prio,[f = std::move(fn), c = std::move(cb)]() mutable {
                f();
                c();
            });
    }
  
    bool join() { // It waits for the thread to finish. First, it waits for the "work complete" signal via the condition variable; then, if it is a standalone thread, it cleans up the OS handle. It sets the status to "joined" and returns `true` to indicate success
        if (!m_data) return false;
        auto s = state();
        if (s == knst_thread_state::joined ||
            s == knst_thread_state::detached ||
            s == knst_thread_state::idle) return false;

        {
            std::unique_lock<std::mutex> lock(m_data->mtx);
            m_data->cv.wait(lock, [&]{ return m_data->finished_flag; });
        }

        if (!m_data->is_pooled) {
        #if KNST_USING_PLATFORM_WINDOWS
            if (m_data->handle) {
                    WaitForSingleObject(m_data->handle, INFINITE);
                    CloseHandle(m_data->handle);
                    m_data->handle = nullptr;
                }
        #else
            if (m_data->handle_valid) {
                pthread_join(m_data->handle, nullptr);
                m_data->handle_valid = false;
            }
        #endif
        }

        m_data->state.store((uint8_t)knst_thread_state::joined,std::memory_order_release);
                            
        return true;
    }

    bool join_for(uint32_t ms) { // The version of `join` with a timeout. It waits for a maximum of `ms` milliseconds; returns `false` if a timeout occurs. If it completes within the time limit, it cleans up the handle and marks it as joined
        if (!m_data) return false;
        auto s = state();
        if (s == knst_thread_state::joined ||
            s == knst_thread_state::detached ||
            s == knst_thread_state::idle) return false;

        bool ok;
        {
            std::unique_lock<std::mutex> lock(m_data->mtx);
            ok = m_data->cv.wait_for(lock, std::chrono::milliseconds(ms),[&]{ return m_data->finished_flag; });
                
                
        }
        if (!ok) return false;

        if (!m_data->is_pooled) {
            #if KNST_USING_PLATFORM_WINDOWS
                if (m_data->handle) {
                    WaitForSingleObject(m_data->handle, INFINITE);
                    CloseHandle(m_data->handle);
                    m_data->handle = nullptr;
                }
            #else
                if (m_data->handle_valid) {
                    pthread_join(m_data->handle, nullptr);
                    m_data->handle_valid = false;
                }
            #endif
        }

        m_data->state.store((uint8_t)knst_thread_state::joined,std::memory_order_release);
                            
        return true;
    }

    bool try_join() { // It attempts to join without waiting. It returns `false` if it cannot immediately acquire the mutex or if the task has already finished. If the task has finished, it delegates to `join()`
        if (!m_data) return false;
        auto s = state();
        if (s == knst_thread_state::joined ||
            s == knst_thread_state::detached ||
            s == knst_thread_state::idle) return false;

        {
            std::unique_lock<std::mutex> lock(m_data->mtx, std::try_to_lock);
            if (!lock.owns_lock()) return false;
            if (!m_data->finished_flag) return false;
        }
        return join();
    }

    bool kill() { // Forcefully kills the thread (dangerous, last resort). It calls TerminateThread/pthread_cancel at the OS level, waits until it has truly terminated, and then manually cleans up the self_ref. It does not kill pooled threads
        if (!m_data) return false;
        auto s = state();
        if (s != knst_thread_state::running &&
            s != knst_thread_state::finished) return false;
        if (m_data->is_pooled) return false; 


        #if KNST_USING_PLATFORM_WINDOWS
            if (m_data->handle) {
                TerminateThread(m_data->handle, 0);
                WaitForSingleObject(m_data->handle, INFINITE);
                CloseHandle(m_data->handle);

                m_data->handle = nullptr;
            }
        #else
            if (m_data->handle_valid) {
                #if !defined(__ANDROID__)
                    pthread_cancel(m_data->handle);
                #endif
                pthread_join(m_data->handle, nullptr);

                m_data->handle_valid = false;
            }
        #endif

        {
            std::lock_guard<std::mutex> lock(m_data->mtx);
            m_data->finished_flag = true;
        }

        m_data->cv.notify_all();
        m_data->state.store((uint8_t)knst_thread_state::finished,std::memory_order_release);
       
        m_data->self_ref.reset();

        return true;
    }

    bool detach() { // It leaves the thread to its own devices—it does not wait for it to be joined. It closes or detaches the OS handle, sets the state to detached, and releases m_data. Thanks to self_ref, it cleans itself up once the thread finishes
        if (!m_data) return false;
        auto s = state();
        if (s != knst_thread_state::running && s != knst_thread_state::finished) return false;
            

        if (!m_data->is_pooled) {
            #if KNST_USING_PLATFORM_WINDOWS
                if (m_data->handle) {
                    CloseHandle(m_data->handle);
                    m_data->handle = nullptr;
                }
            #else
                if (m_data->handle_valid) {
                    pthread_detach(m_data->handle);
                    m_data->handle_valid = false;
                }
            #endif
        }

        m_data->state.store((uint8_t)knst_thread_state::detached,std::memory_order_release);
      
        return true;
    }

    knst_thread_state state() const noexcept { // Returns the current state of the thread. Returns `idle` if `m_data` is null; otherwise, reads the atomic state
        if (!m_data) return knst_thread_state::idle;
        return (knst_thread_state)m_data->state.load(std::memory_order_acquire);
    }

    bool joinable() const noexcept { // Check whether the thread is joinable. Returns true if it is running or finished
        auto s = state();
        return s == knst_thread_state::running || s == knst_thread_state::finished;
    }

    bool finished() const noexcept { // Check if the thread has finished its task. Returns true if finished or joined
        auto s = state();
        return s == knst_thread_state::finished || s == knst_thread_state::joined;
    }

    bool running() const noexcept { // Check if the thread is currently running
        return state() == knst_thread_state::running;
    }

    bool detached() const noexcept { // Check if the thread is detached
        return state() == knst_thread_state::detached;
    }

    bool is_pooled() const noexcept { // Check whether the thread belongs to the pool. Returns true if `mdata` exists and `is_pooled` is true
        return m_data && m_data->is_pooled;
    }
   
    bool set_priority(knst_thread_priority p) noexcept { // Sets the thread priority. It saves the requested priority; if the OS does not support it, it attempts lower levels sequentially. It writes the actual level obtained to `achieved_priority`
        if (!m_data) return false;
        m_data->requested_priority.store(static_cast<int8_t>(p),std::memory_order_relaxed);
                                         

        auto s = state();
        if (s != knst_thread_state::running) return true;
        if (p == knst_thread_priority::inherit) return true;

        #if KNST_USING_PLATFORM_WINDOWS
            if (!m_data->handle) return false;
            HANDLE h = m_data->handle;
            static constexpr knst_thread_priority ladder[] = {
                knst_thread_priority::time_critical, knst_thread_priority::highest,
                knst_thread_priority::high, knst_thread_priority::normal,
                knst_thread_priority::low, knst_thread_priority::lowest
            };
            const int8_t requested_v = static_cast<int8_t>(p);
            for (auto candidate : ladder) {
                if (static_cast<int8_t>(candidate) > requested_v) continue;
                if (::SetThreadPriority(h, knst_priority_to_win(candidate)) != 0) {
                    m_data->achieved_priority.store(static_cast<int8_t>(candidate),std::memory_order_relaxed);
                                                    
                    return true;
                }
            }
            return false;
        #else
            int32_t tid = m_data->native_tid.load(std::memory_order_acquire);
            if (tid < 0) return false;

            static constexpr knst_thread_priority ladder[] = {
                knst_thread_priority::time_critical, knst_thread_priority::highest,
                knst_thread_priority::high, knst_thread_priority::normal,
                knst_thread_priority::low, knst_thread_priority::lowest
            };
            const int8_t requested_v = static_cast<int8_t>(p);
            for (auto candidate : ladder) {
                if (static_cast<int8_t>(candidate) > requested_v) continue;
                if (::setpriority(PRIO_PROCESS, tid,knst_priority_to_nice(candidate)) == 0) {
                    m_data->achieved_priority.store(static_cast<int8_t>(candidate),std::memory_order_relaxed);
                                                    
                    return true;
                }
            }
            return false;
        #endif
    }

    knst_thread_priority priority() const noexcept { // Returns the priority requested by the user. Inherits if m_data is missing
        if (!m_data) return knst_thread_priority::inherit;
        return static_cast<knst_thread_priority>(m_data->requested_priority.load(std::memory_order_relaxed));
    }

    knst_thread_priority achieved_priority() const noexcept { // It returns the priority the thread actually obtained. If the OS rejected the requested level, the applied level is the one returned here
        if (!m_data) return knst_thread_priority::inherit;
        return static_cast<knst_thread_priority>(m_data->achieved_priority.load(std::memory_order_relaxed));
    }

    knst_thread_priority native_priority() const noexcept { // Reads the current priority from the OS—using `GetThreadPriority` on Windows and `getpriority` on POSIX—and converts it to the `knst_thread_priority` enum
        if (!m_data) return knst_thread_priority::normal;

        #if KNST_USING_PLATFORM_WINDOWS
            if (!m_data->handle) return knst_thread_priority::normal;
            int native = ::GetThreadPriority(m_data->handle);
            if (native == THREAD_PRIORITY_ERROR_RETURN) return knst_thread_priority::normal;
            return knst_priority_from_win(native);
        #else
            int32_t tid = m_data->native_tid.load(std::memory_order_acquire);
            if (tid < 0) return knst_thread_priority::normal;
            errno = 0;
            int nice = ::getpriority(PRIO_PROCESS, tid);
            if (nice == -1 && errno != 0) return knst_thread_priority::normal;
            return knst_priority_from_nice(nice);
        #endif
    }


};