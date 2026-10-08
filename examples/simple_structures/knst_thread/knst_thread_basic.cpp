// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
  knst_thread_basic.cpp

  Basic usage of knst_thread — a move-only thread wrapper with priority
  and callback support.

  Shows: start, join, join_for, try_join, detach, state queries, and
  the callback variant.
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>



static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}


static const char* state_name(knst_thread_state s) {
    switch (s) {
        case knst_thread_state::idle: return "idle";
        case knst_thread_state::running: return "running";
        case knst_thread_state::finished:return "finished";
        case knst_thread_state::joined: return "joined";
        case knst_thread_state::detached: return "detached";
    }
    return "unknown";
}


int main() {

    // =====================================================================
    section("1) Default state");
    // =====================================================================
    // A default-constructed knst_thread holds no thread.
    {
        knst_thread t;

        std::cout << "state() : " << state_name(t.state()) << "\n";
        std::cout << "joinable(): " << (t.joinable() ? "yes" : "no") << "\n";
        std::cout << "running() : " << (t.running()  ? "yes" : "no") << "\n";
        std::cout << "finished() : " << (t.finished() ? "yes" : "no") << "\n";
        std::cout << "is_pooled() : " << (t.is_pooled() ? "yes" : "no") << "\n";
    }

    // =====================================================================
    section("2) start() + join()");
    // =====================================================================
    // start() launches the callable on a new OS thread.
    // join() waits for it to finish.
    {
        knst_thread t;

        bool ok = t.start([]() {
            std::cout << "worker thread running\n";
        });
        std::cout << "start() : " << (ok ? "ok" : "failed") << "\n";
        std::cout << "state() : " << state_name(t.state()) << "\n";

        t.join();
        std::cout << "after join : " << state_name(t.state()) << "\n";
    }

    // =====================================================================
    section("3) start() with callback");
    // =====================================================================
    // The second argument is invoked after the main task completes.
    {
        knst_thread t;

        t.start(
            []() { std::cout << " [task]     running\n"; },
            []() { std::cout << " [callback] task finished\n"; }
        );

        t.join();
    }

    // =====================================================================
    section("4) join_for() with timeout");
    // =====================================================================
    // join_for(ms) waits up to `ms` milliseconds. Returns false on timeout.
    {
        knst_thread t;

        t.start([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::cout << "slow task done\n";
        });

        bool got_it = t.join_for(50);
        std::cout << "join_for(50) : " << (got_it ? "finished" : "timed out") << "\n";

        // Give it more time — the second wait succeeds.
        got_it = t.join_for(1000);
        std::cout << "join_for(1000): " << (got_it ? "finished" : "timed out") << "\n";
    }

    // =====================================================================
    section("5) try_join()");
    // =====================================================================
    // try_join() returns immediately: true only if the task has already
    // finished. Otherwise, it returns false (no waiting).
    {
        knst_thread t;

        t.start([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        });

        std::cout << "try_join() immediately : " << (t.try_join() ? "joined" : "not ready") << "\n";

        // Wait for the task, then try again.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        std::cout << "try_join() after 200ms : " << (t.try_join() ? "joined" : "not ready") << "\n";
    }

    // =====================================================================
    section("6) Shared state — atomic counter");
    // =====================================================================
    // Lambdas can capture references to shared state; sync is the
    // user's responsibility.
    {
        std::atomic<int> counter{0};

        knst_thread t;
        t.start([&counter]() {
            for (int i = 0; i < 100000; ++i) counter.fetch_add(1);
        });
        t.join();

        std::cout << "counter after 100000 increments : " << counter.load() << "\n";
    }

    // =====================================================================
    section("7) Move semantics");
    // =====================================================================
    // knst_thread is move-only. After a move, the source is empty.
    {
        knst_thread a;
        a.start([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        });

        std::cout << "a.joinable() before move : " << (a.joinable() ? "yes" : "no") << "\n";

        knst_thread b = std::move(a);

        std::cout << "a.joinable() after move : " << (a.joinable() ? "yes" : "no") << "\n";
        std::cout << "b.joinable() after move : " << (b.joinable() ? "yes" : "no") << "\n";

        b.join();
    }

    // =====================================================================
    section("8) detach()");
    // =====================================================================
    // detach() releases the thread — it will clean itself up once done.
    // After detach, we cannot join it, but the OS thread keeps running.
    {
        knst_thread t;

        t.start([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::cout << "detached task finished in the background\n";
        });

        bool ok = t.detach();
        std::cout << "detach() : " << (ok ? "ok" : "failed") << "\n";
        std::cout << "state() : " << state_name(t.state()) << "\n";

        // Give the detached thread time to finish before main exits.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // =====================================================================
    section("9) Priority — requested vs achieved");
    // =====================================================================
    // start_with_priority() asks the OS for a specific priority.
    // priority()          — what we requested.
    // achieved_priority() — what the OS actually granted.
    {
        knst_thread t;

        t.start_with_priority(knst_thread_priority::high, []() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        });

        // requested_priority is set synchronously by start_with_priority —
        // safe to read immediately.
        std::cout << "requested priority : " << static_cast<int>(t.priority()) << "\n";

        // achieved_priority is written by the worker thread inside the
        // trampoline — we must wait for the task to finish before reading.
        t.join();

        std::cout << "achieved priority : " << static_cast<int>(t.achieved_priority()) << "\n";
        std::cout << "(enum: inherit=-128, lowest=-2, low=-1, normal=0, high=1, highest=2, time_critical=3)\n";
    }

    // =====================================================================
    section("10) kill() — last resort");
    // =====================================================================
    // kill() forcibly terminates the thread at the OS level. Use only
    // when a graceful shutdown is impossible.
    {
        knst_thread t;

        t.start([]() {
            // Simulate an infinite loop.
            while (true) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        bool ok = t.kill();
        std::cout << "kill() : " << (ok ? "terminated" : "failed") << "\n";
        std::cout << "state() : " << state_name(t.state()) << "\n";
    }

    // =====================================================================
    section("11) State query helpers");
    // =====================================================================
    // joinable() — running or finished (can be joined).
    // running()  — currently executing.
    // finished() — done and ready for join.
    // detached() — released; not joinable anymore.
    {
        knst_thread t;

        std::cout << "before start : " << "joinable=" << (t.joinable() ? "yes" : "no") << " running="  << (t.running()  ? "yes" : "no") << " finished=" << (t.finished() ? "yes" : "no") << "\n";
        t.start([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        });

        std::cout << "after start  : " << "joinable=" << (t.joinable() ? "yes" : "no") << " running="  << (t.running()  ? "yes" : "no") << " finished=" << (t.finished() ? "yes" : "no") << "\n";

        t.join();

        std::cout << "after join   : " << "joinable=" << (t.joinable() ? "yes" : "no") << " running="  << (t.running()  ? "yes" : "no") << " finished=" << (t.finished() ? "yes" : "no") << "\n";
                  
    }

    std::cout << "\nDone.\n";
    return 0;
}