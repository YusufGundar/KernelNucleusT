// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
  knst_byte_string_basic.cpp

  Basic usage of knst_byte_string — a binary-safe byte container.

  Shows: construction, embedded nulls, SSO vs heap, append/prepend,
  operators, resize/reserve, indexing/iteration, UTF-16 → UTF-8,
  take_ownership, stream output, and the pool allocator variant.
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>


#if defined(_WIN32) || defined(_WIN64)
    static void knst_init_console() {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    }
#else
    static void knst_init_console() {}
#endif


static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}


// Hex + ASCII dump for binary data.
static void print_hex(const unsigned char* p, uint32_t len) {
    std::cout << "  hex   : ";
    for (uint32_t i = 0; i < len; ++i) {
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%02X", static_cast<unsigned>(p[i]));
        std::cout << buf << ' ';
    }
    std::cout << "\n  ascii : ";
    for (uint32_t i = 0; i < len; ++i) {
        unsigned char c = p[i];
        std::cout << ((c >= 32 && c < 127) ? static_cast<char>(c) : '.');
    }
    std::cout << "\n";
}


int main() {
    knst_init_console();


    // =====================================================================
    section("1) Construction from many sources");
    // =====================================================================
    // Every constructor takes a size, except the char* one which uses strlen.
    // This is what makes the class binary-safe.
    {
        basic_byte_string empty;

        basic_byte_string from_cstr = "hello"; // strlen

        const char raw[] = { 'A', '\0', 'B', '\0', 'C' }; // with nulls
        basic_byte_string from_cstr_sz(raw, sizeof(raw));

        const unsigned char packet[] = { 0xDE, 0xAD, 0xBE, 0xEF };
        basic_byte_string from_uchar(packet, sizeof(packet));

        basic_byte_string from_list = { 0x01, 0x02, 0x03, 0x04 };

        const char16_t u16_text[] = u"Merhaba"; // UTF-16 → UTF-8
        basic_byte_string from_utf16(u16_text, 7);

        knst_c16string c16 = u"dünya";
        basic_byte_string from_c16(c16);

        std::cout << "empty : length=" << empty.length() << "  empty=" << (empty.empty() ? "yes" : "no") << "\n";
                  
        std::cout << "from_cstr : " << from_cstr << "\n";

        std::cout << "from_cstr_sz : length=" << from_cstr_sz.length() << "\n";
        print_hex(from_cstr_sz.data(), from_cstr_sz.length());

        std::cout << "from_uchar : length=" << from_uchar.length() << "\n";
        print_hex(from_uchar.data(), from_uchar.length());

        std::cout << "from_list : length=" << from_list.length() << "\n";
        print_hex(from_list.data(), from_list.length());

        std::cout << "from_utf16 : " << from_utf16 << "\n";
        std::cout << "from_c16 : " << from_c16 << "\n";
    }

    // =====================================================================
    section("2) Binary-safe — embedded nulls are preserved");
    // =====================================================================
    {
        const unsigned char blob[] = { 0x00, 0x11, 0x00, 0x22, 0x00, 0x33 };
        basic_byte_string b(blob, sizeof(blob));

        std::cout << "length = " << b.length() << " (not stopped at the first 0x00)\n";
                  
        print_hex(b.data(), b.length());
    }

    // =====================================================================
    section("3) SSO vs Heap");
    // =====================================================================
    {
        basic_byte_string small = "short";
        basic_byte_string big  = "this is a very long byte string that must go to the heap";

        std::cout << "\"short\"\n" << "   length="   << small.length() << "  capacity=" << small.capacity() << "  is_heap="  << (small.is_heap() ? "yes" : "no") << "\n";
        std::cout << "\"long one\"\n" << "   length="   << big.length() << "  capacity=" << big.capacity() << "  is_heap="  << (big.is_heap() ? "yes" : "no") << "\n";
    }

    // =====================================================================
    section("4) append / push_back / prepend");
    // =====================================================================
    {
        basic_byte_string s = "Hello";
        s.append(reinterpret_cast<const unsigned char*>(" "), 1);
        s.append(reinterpret_cast<const unsigned char*>("World"), 5);
        std::cout << "after append : " << s << "\n";

        s.push_back('!');
        std::cout << "after push_back : " << s << "\n";

        basic_byte_string pre = "World";
        pre.prepend(reinterpret_cast<const unsigned char*>("Hello "), 6);
        std::cout << "after prepend : " << pre << "\n";

        basic_byte_string bin;
        bin.append(reinterpret_cast<const unsigned char*>("AB"), 2);
        bin.append(reinterpret_cast<const unsigned char*>("CD"), 2);
        std::cout << "binary append   :\n";
        print_hex(bin.data(), bin.length());
    }

    // =====================================================================
    section("5) operator+= / operator+ / operator=");
    // =====================================================================
    {
        basic_byte_string s = "Hello";
        s += " ";
        s += "World";
        s += '!';
        std::cout << "after +=  : " << s << "\n";

        basic_byte_string combined = basic_byte_string("[") + s + "]";
        std::cout << "operator+ : " << combined << "\n";

        basic_byte_string assigned;
        assigned = "reassigned";
        std::cout << "operator= : " << assigned << "\n";

        assigned = { 0xAA, 0xBB, 0xCC };
        std::cout << "operator= {0xAA,0xBB,0xCC} :\n";
        print_hex(assigned.data(), assigned.length());
    }

    // =====================================================================
    section("6) Comparison operators");
    // =====================================================================
    {
        basic_byte_string a = "apple";
        basic_byte_string b = "banana";

        std::cout << "a == b : " << (a == b)          << "\n";
        std::cout << "a != b : " << (a != b)          << "\n";
        std::cout << "a <  b : " << (a <  b)          << "\n";
        std::cout << "a >  b : " << (a >  b)          << "\n";
        std::cout << "a == \"apple\" : " << (a == "apple")    << "\n";
        std::cout << "\"apple\" == a : " << ("apple" == a)    << "\n";
    }

    // =====================================================================
    section("7) resize / reserve / shrink_to_fit");
    // =====================================================================
    // reserve(n) guarantees room for n payload bytes + 1 null terminator.
    // Same convention as knst_c16string::reserve.
    {
        basic_byte_string r = "hello";
        r.resize(10, '*');
        std::cout << "resize(10, '*')  : " << r << "  (len=" << r.length() << ")\n";
                  

        r.resize(3);
        std::cout << "resize(3) : " << r << "  (len=" << r.length() << ")\n";
                  

        basic_byte_string res = "x";
        res.reserve(100);
        std::cout << "after reserve(100): capacity=" << res.capacity() << "  is_heap=" << (res.is_heap() ? "yes" : "no") << "\n";
                  

        res.shrink_to_fit();
        std::cout << "after shrink : capacity=" << res.capacity() << "  is_heap=" << (res.is_heap() ? "yes" : "no") << "\n";
                  
    }

    // =====================================================================
    section("8) Indexing and iteration");
    // =====================================================================
    {
        basic_byte_string s = "abc";

        std::cout << "s[0] = " << static_cast<char>(s[0]) << "\n";
        std::cout << "s[1] = " << static_cast<char>(s[1]) << "\n";
        std::cout << "s[2] = " << static_cast<char>(s[2]) << "\n";

        s[0] = 'A';
        s[2] = 'C';
        std::cout << "after mutation : " << s << "\n";

        std::cout << "range-for : ";
        for (unsigned char c : s) std::cout << static_cast<char>(c) << ' ';
        std::cout << "\n";

        std::cout << "cbegin/cend : ";
        for (auto it = s.cbegin(); it != s.cend(); ++it)
            std::cout << static_cast<char>(*it) << ' ';
        std::cout << "\n";
    }

    // =====================================================================
    section("9) UTF-16 → UTF-8 conversion");
    // =====================================================================
    // Non-ASCII characters take 2+ bytes, so the byte length can be
    // larger than the UTF-16 code-unit count.
    {
        const char16_t u16[] = u"Merhaba dünya";
        basic_byte_string b1(u16, 13);
        std::cout << "from char16_t*  : " << b1 << "\n";
        std::cout << "   length (bytes) = " << b1.length() << "\n";

        knst_c16string c16 = u"ünïcödé";
        basic_byte_string b2(c16);
        std::cout << "from c16string  : " << b2 << "\n";
        std::cout << "   length (bytes) = " << b2.length() << "   (c16 length = " << c16.length() << " code units)\n";
    }

    // =====================================================================
    section("10) take_ownership — adopting a new[] buffer");
    // =====================================================================
    // Copies the bytes into the string, then delete[]s the original.
    // After the call, the pointer is invalid.
    {
        unsigned char* raw = new unsigned char[8];
        std::memcpy(raw, "network", 7);
        raw[7] = '\0';

        knst_byte_string owned = knst_byte_string::take_ownership(raw, 7);

        std::cout << "adopted buffer : " << owned << "\n";
        std::cout << " length=" << owned.length() << "  is_heap=" << (owned.is_heap() ? "yes" : "no") << "\n";
    }

    // =====================================================================
    section("11) Stream output");
    // =====================================================================
    // operator<< writes exactly `length()` bytes — no null terminator
    // search. For raw binary, dump to hex.
    {
        basic_byte_string text = "plain text output";
        std::cout << "std::cout << s : " << text << "\n";

        basic_byte_string binary = { 0x00, 0x01, 0xFF, 0x7F };
        std::cout << "binary string (length " << binary.length() << "):\n";
        print_hex(binary.data(), binary.length());
    }

    // =====================================================================
    section("12) Allocator introspection");
    // =====================================================================
    {
        basic_byte_string def = "x";
        basic_byte_string<knst_pool_allocator> pol = "x";

        std::cout << "knst_byte_string (default allocator):\n" << "   pool_count() = " << def.pool_count() << "\n" << "   max_block_size() = " << def.max_block_size() << " bytes\n";
                  

        std::cout << "\nknst_byte_string_sm (pool allocator):\n" << "   pool_count()     = " << pol.pool_count()     << "\n" << "   max_block_size() = " << pol.max_block_size() << " bytes\n";
    }

    // =====================================================================
    section("13) knst_pool_allocator configuration");
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

    std::cout << "\nDone.\n";
    return 0;
}