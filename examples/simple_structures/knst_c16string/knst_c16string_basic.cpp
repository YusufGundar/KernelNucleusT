// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
  knst_c16string_basic.cpp

  Basic usage of knst_c16string — a UTF-16 string class.

  Shows: construction from many sources, SSO vs heap, append,
  search, substring, resize, iterators, comparison, conversions,
  copy/move with COW, and the pool allocator variant.
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>


// Enable UTF-8 output on Windows so std::cout can print sample text.
#if defined(_WIN32) || defined(_WIN64)
    static void knst_init_console() {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    }
#else
    static void knst_init_console() {}
#endif


// Prints a section header.
static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}


int main() {
    knst_init_console();


    // =====================================================================
    section("1) Construction from many sources");
    // =====================================================================
    {
        knst_c16string empty; // empty string
        knst_c16string from_u16 = u"Merhaba"; // UTF-16 literal
        knst_c16string from_utf8 = "Merhaba"; // UTF-8, auto-converted
        knst_c16string from_w = L"Merhaba"; // wchar_t
        knst_c16string from_u32 = U"Merhaba";// UTF-32
        knst_c16string from_std = std::string("Merhaba"); // std::string (UTF-8)
        knst_c16string from_int = 2026; // integer
        knst_c16string from_double = 3.14; // floating point
        knst_c16string from_fill = knst_c16string(5, u'*'); // "*****"
        knst_c16string from_list = { u'h', u'e', u'l', u'l', u'o' };

        std::cout << "empty       : '" << empty << "'\n";
        std::cout << "from_u16    : "  << from_u16 << "\n";
        std::cout << "from_utf8   : "  << from_utf8 << "\n";
        std::cout << "from_w      : "  << from_w << "\n";
        std::cout << "from_u32    : "  << from_u32 << "\n";
        std::cout << "from_std    : "  << from_std << "\n";
        std::cout << "from_int    : "  << from_int << "\n";
        std::cout << "from_double : "  << from_double << "\n";
        std::cout << "from_fill   : "  << from_fill<< "\n";
        std::cout << "from_list   : "  << from_list << "\n";
    }

    // =====================================================================
    section("2) SSO vs Heap");
    // =====================================================================
    // Short strings live on the stack (no allocation).
    // Longer ones are automatically moved to the heap.
    {
        knst_c16string stack_str = u"short";
        knst_c16string heap_str = u"this is a long string that lives on the heap";

        std::cout << "\"short\"\n"  << "length="   << stack_str.length() << "  capacity=" << stack_str.capacity() << "  is_heap="  << (stack_str.is_heap() ? "yes" : "no") << "\n";
        std::cout << "\"this is a long string that lives on the heap\"\n" << "   length="   << heap_str.length() << "  capacity=" << heap_str.capacity() << "  is_heap="  << (heap_str.is_heap() ? "yes" : "no") << "\n";
    }

    // =====================================================================
    section("3) append / operator+= / operator+");
    // =====================================================================
    // append() adds to the end and returns bool (false on allocation failure).
    // operator+= is shorthand for append.
    // operator+ creates a new string instead of modifying.
    {
        knst_c16string s = u"Merhaba";
        s.append(u", "); // UTF-16 literal
        s.append("dünya"); // UTF-8
        s.append(u'!'); // single character
        s.append(' '); // single ASCII character
        s.append(2026); // integer
        std::cout << "after append : " << s << "\n";

        knst_c16string t = u"Merhaba";
        t += u" dünya";
        t += u'!';
        t += " ";
        t += 3.14;
        std::cout << "after operator+= : " << t << "\n";

        knst_c16string combined = knst_c16string(u"[ ") + t + u" ]";
        std::cout << "operator+ : " << combined << "\n";
    }

    // =====================================================================
    section("4) find / contains / starts_with / ends_with");
    // =====================================================================
    // All four work with any supported encoding (char16_t*, char*, etc.).
    {
        knst_c16string text = u"The quick brown fox jumps over the lazy dog";
        std::cout << "text = " << text << "\n\n";

        std::cout << "contains(\"fox\")     : " << (text.contains(u"fox")     ? "yes" : "no") << "\n";
        std::cout << "contains(\"cat\")     : " << (text.contains(u"cat")     ? "yes" : "no") << "\n";
        std::cout << "find(\"the\", off 5)  : " << (text.find(u"the", 5)      ? "yes" : "no") << "\n";
        std::cout << "starts_with(\"The\")  : " << (text.starts_with(u"The")  ? "yes" : "no") << "\n";
        std::cout << "ends_with(\"dog\")    : " << (text.ends_with(u"dog")    ? "yes" : "no") << "\n";
        std::cout << "starts_with('T')     : " << (text.starts_with(u'T')     ? "yes" : "no") << "\n";
        std::cout << "ends_with('g')       : " << (text.ends_with(u'g')       ? "yes" : "no") << "\n";

        std::cout << "\nCross-encoding:\n" << "   starts_with(\"The\") : " << (text.starts_with("The") ? "yes" : "no") << "\n" << "   starts_with(U\"Th\") : " << (text.starts_with(U"Th") ? "yes" : "no") << "\n"<< "   ends_with(L\"dog\")  : " << (text.ends_with(L"dog")     ? "yes" : "no") << "\n";
    }

    // =====================================================================
    section("5) substr / resize / reserve / shrink_to_fit");
    // =====================================================================
    {
        knst_c16string t = u"The quick brown fox";
        std::cout << "substr(4, 5) of \"" << t << "\" = \"" << t.substr(4, 5) << "\"\n";

        knst_c16string r = u"hello";
        r.resize(10, u'*');                 // grow with '*'
        std::cout << "resize(10, '*') : " << r << "  (len=" << r.length() << ")\n";

        r.resize(3);                        // shrink
        std::cout << "resize(3) : " << r << "  (len=" << r.length() << ")\n";

        knst_c16string res = u"x";
        res.reserve(100);                   // ask for at least 100+1 capacity
        std::cout << "after reserve(100): capacity=" << res.capacity() << "  is_heap=" << (res.is_heap() ? "yes" : "no") << "\n";
        res.shrink_to_fit();                // drop unused capacity
        std::cout << "after shrink : capacity=" << res.capacity() << "  is_heap=" << (res.is_heap() ? "yes" : "no") << "\n";
    }

    // =====================================================================
    section("6) Iterators");
    // =====================================================================
    {
        knst_c16string it = u"abc";

        std::cout << "range-for     : ";
        for (char16_t c : it) std::cout << static_cast<char>(c) << ' ';
        std::cout << "\n";

        // Non-const iterators let you modify the string.
        for (auto& ch : it) ch = static_cast<char16_t>(ch - 32);
        std::cout << "after mutation: " << it << "\n";
    }

    // =====================================================================
    section("7) Comparison operators");
    // =====================================================================
    {
        knst_c16string a = u"apple";
        knst_c16string b = u"banana";

        std::cout << "a == b : " << (a == b) << "\n";
        std::cout << "a != b : " << (a != b) << "\n";
        std::cout << "a <  b : " << (a <  b) << "\n";
        std::cout << "a >  b : " << (a >  b)   << "\n";
        std::cout << "a == \"apple\" : " << (a == "apple")  << "\n";
        std::cout << "a == L\"apple\" : " << (a == L"apple") << "\n";
        std::cout << "a == U\"apple\" : " << (a == U"apple") << "\n";
        std::cout << "\"apple\" == a : " << ("apple" == a)  << "\n";
    }

    // =====================================================================
    section("8) at / front / back");
    // =====================================================================
    // operator[] has no bounds check; at() does.
    {
        knst_c16string s = u"hello";
        std::cout << "s[0]  = " << static_cast<char>(s[0])      << "\n";
        std::cout << "s.at(1) = " << static_cast<char>(s.at(1))   << "\n";
        std::cout << "s.front() = " << static_cast<char>(s.front()) << "\n";
        std::cout << "s.back() = " << static_cast<char>(s.back())  << "\n";

        s[0] = u'H';
        s.back() = u'!';
        std::cout << "after mutation: " << s << "\n";
    }

    // =====================================================================
    section("9) Conversions — to_char() / to_wchar_alloc()");
    // =====================================================================
    // Both allocate new memory; the caller must free it with the same
    // allocator.
    {
        knst_c16string s = u"ünïcödé";

        // UTF-16 → UTF-8
        char* u8 = s.to_char();
        if (u8) {
            std::cout << "to_char()        = " << u8 << "\n";
            knst_default_allocator().deallocate(u8, std::strlen(u8) + 1);
        }

        // UTF-16 → wchar_t*. We feed the result back into a c16string so
        // the output is portable across POSIX and Windows.
        wchar_t* w = s.to_wchar_alloc();
        if (w) {
            knst_c16string round_trip(w);
            std::cout << "to_wchar_alloc() round-trip = " << round_trip << "\n";
            knst_default_allocator().deallocate(w, (s.length() + 1) * sizeof(wchar_t));
        }
    }

    // =====================================================================
    section("10) Copy / Move + COW");
    // =====================================================================
    // Copies share the buffer until someone writes to it (copy-on-write).
    // Move steals the buffer without copying.
    {
        knst_c16string original = u"shared buffer content that lives on the heap";
        knst_c16string copy     = original;   // shared

        std::cout << "original = " << original << "\n";
        std::cout << "copy     = " << copy     << "\n";

        copy.append(u" [modified]");          // triggers detach

        std::cout << "\nafter modifying the copy:\n";
        std::cout << "  original = " << original << "\n";
        std::cout << "  copy     = " << copy     << "\n";

        // Move — the source ends up empty.
        knst_c16string src = u"movable content long enough to be on the heap";
        knst_c16string dst = std::move(src);
        std::cout << "\nmoved-to  dst  = " << dst << "\n";
        std::cout << "moved-from src = '" << src << "'  (empty=" << (src.empty() ? "yes" : "no") << ")\n";
    }

    // =====================================================================
    section("11) Allocator introspection");
    // =====================================================================
    {
        knst_c16string def = u"x";
        knst_c16string_sm pol = u"x";

        std::cout << "knst_c16string (default allocator):\n" << "pool_count() = " << def.pool_count() << "\n" << " max_block_size() = " << def.max_block_size() << " bytes\n";

        std::cout << "\nknst_c16string_sm (pool allocator):\n" << "pool_count() = " << pol.pool_count()  << "\n"<< " max_block_size() = " << pol.max_block_size() << " bytes\n";
    }

    // =====================================================================
    section("12) knst_pool_allocator configuration");
    // =====================================================================
    // Default sizes: 64, 256, 1024, 2048 bytes.
    {
        knst_pool_allocator a; // defaults
        std::cout << "default pool : pool_count=" << a.pool_count() << "  max_block=" << a.max_block_size() << "\n";
                  

        knst_pool_allocator b(128, 512, 4096); // custom sizes
        std::cout << "custom pool  : pool_count=" << b.pool_count() << "  max_block=" << b.max_block_size() << "\n";
                  

        knst_pool_allocator c( // size + block count
            knst_pool_config(128, 256),
            knst_pool_config(1024, 64)
        );
        std::cout << "config pool  : pool_count=" << c.pool_count() << "  max_block=" << c.max_block_size() << "\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}