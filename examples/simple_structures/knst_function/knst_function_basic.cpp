// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
  knst_function_basic.cpp

  Basic usage of knst_function — a move-only callable wrapper that
  stores any `void()` callable.

  Shows: empty state, construction from lambda / function pointer,
  calling, move semantics, SSO vs heap (64-byte threshold), and reset().
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>


static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}


// A plain function pointer works too.
static void hello_from_function() {
    std::cout << "hello from a function pointer\n";
}


int main() {

    // =====================================================================
    section("1) Empty state");
    // =====================================================================
    // Default-constructed knst_function holds nothing.
    {
        knst_function f;

        std::cout << "empty() : " << (f.empty() ? "yes" : "no") << "\n";
        std::cout << "operator bool : " << (f ? "true" : "false") << "\n";

        // Calling an empty knst_function is safe — it simply does nothing.
        f();
        std::cout << "after f() call : still alive\n";
    }

    // =====================================================================
    section("2) Construction from a lambda");
    // =====================================================================
    // Any callable with signature `void()` can be stored.
    {
        int call_count = 0;

        knst_function f = [&call_count]() {
            ++call_count;
            std::cout << "lambda called (count=" << call_count << ")\n";
        };

        std::cout << "empty() : " << (f.empty() ? "yes" : "no") << "\n";
        std::cout << "operator bool : " << (f ? "true" : "false") << "\n";

        f(); // call_count = 1
        f(); // call_count = 2
        f(); // call_count = 3
    }

    // =====================================================================
    section("3) Construction from a function pointer");
    // =====================================================================
    {
        knst_function f = &hello_from_function;

        std::cout << "empty() : " << (f.empty() ? "yes" : "no") << "\n";
        f();
    }

    // =====================================================================
    section("4) Small lambda — stays on the stack (SSO)");
    // =====================================================================
    // Callables up to KNST_FUNCTION_INLINE_SIZE (64 bytes by default)
    // live inside the knst_function object itself — no heap allocation.
    {
        knst_function f = []() {
            std::cout << "small lambda (captures one int)\n";
        };

        std::cout << "sizeof(knst_function) = " << sizeof(knst_function) << " bytes\n";
        f();
    }

    // =====================================================================
    section("5) Large lambda — spills to the heap");
    // =====================================================================
    // Callables larger than 64 bytes are allocated on the heap.
    // The behaviour is transparent to the caller.
    {
        // A big capture array — 128 bytes of captured state.
        char big_data[128] = {};

        knst_function f = [big_data]() {
            // Only touch a couple of bytes to show it's really there.
            std::cout << "large lambda (captures 128 bytes, first byte=" << static_cast<int>(big_data[0]) << ")\n";
                      
        };

        std::cout << "sizeof(knst_function) = " << sizeof(knst_function) << " bytes\n";
        f();
    }

    // =====================================================================
    section("6) Move semantics");
    // =====================================================================
    // knst_function is move-only; copies are deleted.
    // After a move, the source becomes empty.
    {
        knst_function a = []() { std::cout << "called from 'a'\n"; };
        knst_function b = std::move(a);

        std::cout << "a.empty() after move : " << (a.empty() ? "yes" : "no") << "\n";
        std::cout << "b.empty() after move : " << (b.empty() ? "yes" : "no") << "\n";

        b(); // works
        a(); // safe no-op (empty)
    }

    // =====================================================================
    section("7) Move assignment");
    // =====================================================================
    {
        knst_function a = []() { std::cout << "a's original target\n";};
        knst_function b = []() { std::cout << "b's original target\n";};

        b = std::move(a); // b's old callable is destroyed

        std::cout << "a.empty() after move : " << (a.empty() ? "yes" : "no") << "\n";
        b(); // runs a's old lambda
    }

    // =====================================================================
    section("8) reset()");
    // =====================================================================
    // reset() destroys the stored callable and returns to the empty state.
    {
        knst_function f = []() { std::cout << "still here\n"; };
        f();
        std::cout << "empty before reset : " << (f.empty() ? "yes" : "no") << "\n";

        f.reset();
        std::cout << "empty after reset  : " << (f.empty() ? "yes" : "no") << "\n";

        f(); // safe no-op
    }

    // =====================================================================
    section("9) Reassigning with operator=");
    // =====================================================================
    // A knst_function can be assigned a new callable via the move
    // assignment (from another knst_function) — or a fresh construction.
    {
        int step = 0;

        knst_function f = [&step]() {
            step += 1;
            std::cout << "step " << step << "\n";
        };
        f();
        f();

        // Replace it with a different callable via move.
        knst_function other = []() { std::cout << "other callable\n"; };
        f = std::move(other);
        f();  // runs the new one
    }

    // =====================================================================
    section("10) Copy is deleted (compile-time)");
    // =====================================================================
    // The following lines would NOT compile — knst_function is move-only:
    //
    //     knst_function a = [](){};
    //     knst_function b = a;  // error: copy deleted
    //     knst_function c;
    //     c = a; // error: copy deleted
    //
    // Use std::move(a) if you need to transfer ownership.
    {
        std::cout << "see comment — copy is deleted at compile time\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}