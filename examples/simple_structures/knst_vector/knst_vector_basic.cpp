// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
  knst_vector_basic.cpp

  Basic usage of knst_vector — a dynamic array class.

  Shows: construction, push_back, pop_back, insert, erase, iterators,
  resize/reserve/shrink_to_fit, comparison, and the pool allocator
  variant.
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>


static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}


// Prints a vector as [a, b, c].
template <typename V>
static void print_vec(const char* label, const V& v) {
    std::cout << label << " (size=" << v.size() << ", capacity=" << v.capacity() << "): [";
              
    for (uint32_t i = 0; i < v.size(); ++i) {
        std::cout << v[i];
        if (i + 1 < v.size()) std::cout << ", ";
    }
    std::cout << "]\n";
}


int main() {
    // =====================================================================
    section("1) Construction from many sources");
    // =====================================================================
    {
        knst_vector<int> empty; // empty

        knst_vector<int> fill(5, 42); // 5 elements, all 42

        knst_vector<int> from_list = { 1, 2, 3, 4, 5 }; // initializer_list

        const int raw[] = { 10, 20, 30 };
        knst_vector<int> from_array(raw, 3); // pointer + count

        // Copy and move.
        knst_vector<int> copied = from_list;
        knst_vector<int> moved  = std::move(copied);

        print_vec("empty ", empty);
        print_vec("fill(5,42) ", fill);
        print_vec("from_list ", from_list);
        print_vec("from_array ", from_array);
        print_vec("copied ", from_list);
        print_vec("moved ", moved);
        print_vec("moved-from ", copied);
    }

    // =====================================================================
    section("2) push_back / pop_back / emplace_back");
    // =====================================================================
    // push_back copies or moves an element in.
    // emplace_back constructs the element in place.
    // pop_back removes the last element (no capacity change).
    {
        knst_vector<int> v;

        v.push_back(10);
        v.push_back(20);
        v.push_back(30);
        print_vec("after 3 push_back ", v);

        v.emplace_back(40);   // same as push_back(40) for int
        print_vec("after emplace_back", v);

        v.pop_back();
        print_vec("after pop_back ", v);

        // Capacity grows automatically (4 → 8 → 16 ...).
        knst_vector<int> grow;
        for (int i = 0; i < 20; ++i) grow.push_back(i);
        print_vec("20 elements ", grow);
    }

    // =====================================================================
    section("3) Indexing");
    // =====================================================================
    // operator[] has no bounds check — fast.
    {
        knst_vector<int> v = { 100, 200, 300 };

        std::cout << "v[0] = " << v[0] << "\n";
        std::cout << "v[1] = " << v[1] << "\n";
        std::cout << "v[2] = " << v[2] << "\n";

        v[1] = 999;
        print_vec("after v[1] = 999 ", v);
    }

    // =====================================================================
    section("4) Iterators and range-for");
    // =====================================================================
    {
        knst_vector<int> v = { 1, 2, 3, 4, 5 };

        std::cout << "range-for        : ";
        for (int x : v) std::cout << x << ' ';
        std::cout << "\n";

        std::cout << "iterator loop    : ";
        for (auto it = v.begin(); it != v.end(); ++it)
            std::cout << *it << ' ';
        std::cout << "\n";

        // Non-const iterators let you modify elements.
        for (auto& x : v) x *= 10;
        print_vec("after x *= 10    ", v);
    }

    // =====================================================================
    section("5) insert / erase");
    // =====================================================================
    {
        knst_vector<int> v = { 1, 2, 3, 4, 5 };

        // Insert at iterator position — iterator converts to const_iterator.
        v.insert(v.begin() + 2, 99);
        print_vec("insert @2 ", v);

        // Insert multiple copies.
        v.insert(v.begin(), 3, 0);
        print_vec("insert 3x0 @0 ", v);

        // Erase by iterator.
        v.erase(v.begin());
        print_vec("erase begin ", v);

        // Erase by index.
        v.erase(1);
        print_vec("erase index 1 ", v);

        // Erase a range — both iterators convert implicitly.
        v.erase(v.begin(), v.begin() + 3);
        print_vec("erase range 0..3 ", v);
    }

    // =====================================================================
    section("6) find / erase_value");
    // =====================================================================
    {
        knst_vector<int> v = { 10, 20, 30, 40, 30 };

        auto it = v.find(30);
        if (it != v.end())
            std::cout << "find(30) : found at index " << (it.get() - v.data()) << "\n";
                      
        else
            std::cout << "find(30) : not found\n";

        bool removed = v.erase_value(30); // removes only the first match
        std::cout << "erase_value(30)  : " << (removed ? "removed" : "not found") << "\n";
        print_vec("after removal ", v);
    }

    // =====================================================================
    section("7) resize / reserve / shrink_to_fit");
    // =====================================================================
    {
        knst_vector<int> v = { 1, 2, 3 };

        v.reserve(50);
        print_vec("after reserve(50)", v);

        v.resize(6);
        print_vec("after resize(6) ", v); // new slots are 0

        v.resize(8, 7);
        print_vec("after resize(8,7)", v); // new slots are 7

        v.resize(4);
        print_vec("after resize(4) ", v); // shrink

        v.shrink_to_fit();
        print_vec("after shrink ", v);
    }

    // =====================================================================
    section("8) assign / clear");
    // =====================================================================
    {
        knst_vector<int> v = { 1, 2, 3, 4, 5 };
        print_vec("initial", v);

        v.assign(4, 7);
        print_vec("assign(4, 7)", v);

        v.assign({ 100, 200, 300 });
        print_vec("assign({..})", v);

        v.clear();
        print_vec("after clear ", v);  // size=0, capacity kept
    }

    // =====================================================================
    section("9) Comparison operators");
    // =====================================================================
    {
        knst_vector<int> a = { 1, 2, 3 };
        knst_vector<int> b = { 1, 2, 3 };
        knst_vector<int> c = { 1, 2, 4 };


        std::cout << "a == b : " << (a == b) << "\n";
        std::cout << "a != c : " << (a != c) << "\n";
    }

    // =====================================================================
    section("10) front / back");
    // =====================================================================
    // back() is provided. front() is data()[0].
    {
        knst_vector<int> v = { 10, 20, 30 };

        std::cout << "front (v[0]) : " << v.data()[0] << "\n";
        std::cout << "back() : " << v.back()    << "\n";

        v.back() = 99;
        print_vec("after back=99", v);
    }

    // =====================================================================
    section("11) Allocator introspection");
    // =====================================================================
    // knst_vector does not expose pool_count/max_block_size directly,
    // so we query the allocators.
    {
        knst_default_allocator def_alloc;
        knst_pool_allocator pool_alloc;

        std::cout << "knst_default_allocator:\n" << " pool_count()  = " << def_alloc.pool_count() << "\n" << "max_block_size() = " << def_alloc.max_block_size() << " bytes\n";
                  
        std::cout << "\nknst_pool_allocator:\n" << " pool_count() = " << pool_alloc.pool_count()  << "\n" << "max_block_size() = " << pool_alloc.max_block_size() << " bytes\n";
    }

    // =====================================================================
    section("12) knst_pool_allocator configuration");
    // =====================================================================
    // Default sizes: 64, 256, 1024, 2048 bytes.
    {
        knst_pool_allocator a;
        std::cout << "default pool : pool_count=" << a.pool_count() << "  max_block=" << a.max_block_size() << "\n";
                  

        knst_pool_allocator b(128, 512, 4096);
        std::cout << "custom pool  : pool_count=" << b.pool_count() << "  max_block=" << b.max_block_size() << "\n";
                  

        knst_pool_allocator c(
            knst_pool_config(128, 256),
            knst_pool_config(1024, 64)
        );
        std::cout << "config pool  : pool_count=" << c.pool_count() << "  max_block=" << c.max_block_size() << "\n";
                  
    }

    // =====================================================================
    section("13) knst_vector_sm — pool-backed vector");
    // =====================================================================
    {
        knst_vector_sm<int> v;
        v.push_back(1);
        v.push_back(2);
        v.push_back(3);

        print_vec("pool-backed vec  ", v);
    }

    std::cout << "\nDone.\n";
    return 0;
}