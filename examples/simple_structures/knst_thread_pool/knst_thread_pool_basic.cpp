// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
  knst_thread_pool_basic.cpp

  Basic usage of knst_thread_pool — a worker-thread pool that amortizes
  the cost of thread creation.

  Shows: start/shutdown, submit, submit_with_priority, callbacks,
  wait_all, introspection, and knst_thread integrated with the pool
  (via knst_thread::start(pool, fn)).
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>



static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}


int main() {

    // =====================================================================
    section("1) Construct a pool (auto worker count)");
    // =====================================================================
    {
        knst_thread_pool pool;

        std::cout << "workers (before start): " << pool.worker_count() << "\n";
        std::cout << "is_running(): " << (pool.is_running() ? "yes" : "no") << "\n";

        bool ok = pool.start();
        std::cout << "start() : " << (ok ? "ok" : "failed") << "\n";
        std::cout << "workers (after start) : " << pool.worker_count() << "\n";
        std::cout << "is_running() : " << (pool.is_running() ? "yes" : "no") << "\n";

        pool.shutdown();
    }

    // =====================================================================
    section("2) Construct a pool with explicit worker count");
    // =====================================================================
    {
        knst_thread_pool pool(4);

        pool.start();
        std::cout << "workers : " << pool.worker_count() << "\n";

        pool.shutdown();
        std::cout << "after shutdown is_running: " << (pool.is_running() ? "yes" : "no") << "\n";
    }

    // =====================================================================
    section("3) submit + join");
    // =====================================================================
    {
        knst_thread_pool pool(2);
        pool.start();

        knst_thread t = pool.submit([]() {
            std::cout << "task running on a worker\n";
        });

        t.join();
        std::cout << "after join: " << (t.finished() ? "finished" : "running") << "\n";

        pool.shutdown();
    }

    // =====================================================================
    section("4) submit with callback");
    // =====================================================================
    {
        knst_thread_pool pool(2);
        pool.start();

        knst_thread t = pool.submit(
            []() { std::cout << "  [task]     running\n"; },
            []() { std::cout << "  [callback] done\n"; }
        );

        t.join();
        pool.shutdown();
    }

    // =====================================================================
    section("5) submit_with_priority");
    // =====================================================================
    {
        knst_thread_pool pool(2);
        pool.start();

        knst_thread t = pool.submit_with_priority(
            knst_thread_priority::high,
            []() { std::cout << "priority task running\n"; }
        );

        t.join();

        std::cout << "requested : " << static_cast<int>(t.priority()) << "\n";
        std::cout << "achieved  : " << static_cast<int>(t.achieved_priority()) << "\n";

        pool.shutdown();
    }

    // =====================================================================
    section("6) Batch submit + wait_all");
    // =====================================================================
    // Tasks do a tiny sleep so that the queue is visibly non-empty when
    // we query pending_count() right after submitting.
    {
        knst_thread_pool pool(4);
        pool.start();

        std::atomic<int> counter{0};

        constexpr int N = 32;
        for (int i = 0; i < N; ++i) {
            pool.submit([&counter]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }

        std::cout << "submitted " << N << " tasks\n";
        std::cout << "pending (before wait) : " << pool.pending_count() << "\n";

        pool.wait_all();

        std::cout << "pending (after wait) : " << pool.pending_count() << "\n";
        std::cout << "counter : " << counter.load() << "\n";

        pool.shutdown();
    }

    // =====================================================================
    section("7) wait_all_for with timeout");
    // =====================================================================
    {
        knst_thread_pool pool(1);
        pool.start();

        for (int i = 0; i < 5; ++i) {
            pool.submit([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            });
        }

        bool done = pool.wait_all_for(20);
        std::cout << "wait_all_for(20) : " << (done ? "done" : "timed out") << "\n";

        done = pool.wait_all_for(1000);
        std::cout << "wait_all_for(1000): " << (done ? "done" : "timed out") << "\n";

        pool.shutdown();
    }

    // =====================================================================
    section("8) Introspection — worker / pending / queue / active");
    // =====================================================================
    {
        knst_thread_pool pool(2);
        pool.start();

        std::cout << "workers : " << pool.worker_count() << "\n";
        std::cout << "pending : " << pool.pending_count() << "\n";
        std::cout << "queue_size : " << pool.queue_size()  << "\n";
        std::cout << "active : " << pool.active_count()  << "\n";
        std::cout << "overflow_count : " << pool.overflow_count() << "\n";

        for (int i = 0; i < 4; ++i) {
            pool.submit([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        std::cout << "\nafter submitting 4 slow tasks:\n";
        std::cout << "pending : " << pool.pending_count() << "\n";
        std::cout << "queue_size : " << pool.queue_size()  << "\n";
        std::cout << "active : " << pool.active_count() << "\n";
        std::cout << "overflow_count: " << pool.overflow_count() << "\n";

        pool.wait_all();
        pool.shutdown();
    }

    // =====================================================================
    section("9) knst_thread::start(pool, fn) — using the pool directly");
    // =====================================================================
    {
        knst_thread_pool pool(2);
        pool.start();

        knst_thread t;
        bool ok = t.start(pool, []() {
            std::cout << "task running on a pool worker\n";
        });

        std::cout << "start(pool, fn) : " << (ok ? "ok" : "failed") << "\n";
        std::cout << "is_pooled() : " << (t.is_pooled() ? "yes" : "no") << "\n";

        t.join();
        pool.shutdown();
    }

    // =====================================================================
    section("10) knst_thread::start_with_priority(pool, prio, fn)");
    // =====================================================================
    {
        knst_thread_pool pool(2);
        pool.start();

        knst_thread t;
        bool ok = t.start_with_priority(
            pool,
            knst_thread_priority::normal,
            []() { std::cout << "priority task on the pool\n"; }
        );

        std::cout << "start_with_priority : " << (ok ? "ok" : "failed") << "\n";
        std::cout << "is_pooled() : " << (t.is_pooled() ? "yes" : "no") << "\n";

        t.join();
        pool.shutdown();
    }

    // =====================================================================
    section("11) Worker priority — request vs achieved");
    // =====================================================================
    {
        knst_thread_pool pool(2, knst_thread_priority::low);
        pool.start();

        std::cout << "requested worker priority : " << static_cast<int>(pool.worker_priority()) << "\n";
        std::cout << "achieved  worker priority : " << static_cast<int>(pool.worker_priority_achieved()) << "\n";

        knst_thread t = pool.submit([](){});
        t.join();

        std::cout << "after running a task      :\n";
        std::cout << "achieved  worker priority : " << static_cast<int>(pool.worker_priority_achieved()) << "\n";

        pool.shutdown();
    }

    // =====================================================================
    section("12) Graceful shutdown");
    // =====================================================================
    // graceful: workers finish everything in the queue, then exit.
    {
        knst_thread_pool pool(2);
        pool.start();

        std::atomic<int> done{0};
        for (int i = 0; i < 8; ++i) {
            pool.submit([&done]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                done.fetch_add(1, std::memory_order_relaxed);
            });
        }

        pool.shutdown(knst_shutdown::graceful);

        std::cout << "graceful shutdown — tasks completed: " << done.load() << " / 8\n";
    }

    // =====================================================================
    section("13) Force shutdown — cancel queued tasks");
    // =====================================================================
    // Design note: the pool spawns up to KNST_OVERFLOW_MAX (32) temporary
    // threads when the queue grows. So with worker_count=1, up to 33
    // tasks start immediately; the rest sit in the queue.
    //
    // Force shutdown cancels the queued ones. Running tasks are still
    // awaited (they will finish normally).
    //
    // To see force-shutdown in its purest form, submit MORE than the
    // overflow capacity. We use 200 tasks — overflow caps at 32 + 1
    // worker = 33 running, so ~167 remain queued.
    {
        knst_thread_pool pool(1);
        pool.start();

        std::atomic<int> started{0};
        std::atomic<int> completed{0};

        constexpr int N = 200;
        for (int i = 0; i < N; ++i) {
            pool.submit([&started, &completed]() {
                started.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                completed.fetch_add(1, std::memory_order_relaxed);
            });
        }

        std::cout << "submitted : " << N << "\n";
        std::cout << "pending right away: " << pool.pending_count() << "\n";
        std::cout << "queue_size : " << pool.queue_size()    << "\n";
        std::cout << "active : " << pool.active_count()  << "\n";
        std::cout << "overflow_count : " << pool.overflow_count()<< "\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        std::cout << "\nafter 30ms:\n";
        std::cout << "started : " << started.load() << "\n";
        std::cout << "queue_size : " << pool.queue_size()  << "\n";
        std::cout << "active : " << pool.active_count() << "\n";

        // Force shutdown — anything still in the queue is cancelled.
        pool.shutdown(knst_shutdown::force);

        std::cout << "\nafter force shutdown:\n";
        std::cout << "started (from queue) : " << started.load()   << " / " << N << "\n";
        std::cout << "completed : " << completed.load() << " / " << N << "\n";
        std::cout << "cancelled : " << (N - started.load()) << " / " << N << "\n";
        std::cout << "(running tasks finish; queued ones are cancelled)\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}