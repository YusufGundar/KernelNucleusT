#include <iostream>
#include <functional>
#include <cstring>
#include <vector>
#include <algorithm>

#include "../include/KernelNucleusT.hpp"

inline void knst_init_console() {
#if defined(_WIN32) || defined(_WIN64)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

struct TestResult {
    std::string group;
    std::string name;
    bool passed;
    std::string detail;
};

static std::vector<TestResult> g_results;
static std::string g_current_group;

void begin_group(const std::string& name) {
    g_current_group = name;
    std::cout << "\n=== " << name << " ===\n";
}

bool check(bool condition, const std::string& test_name, const std::string& detail = "") {
    g_results.push_back({g_current_group, test_name, condition, detail});
    if (condition)
        std::cout << "  [PASS] " << test_name << "\n";
    else
        std::cout << "  [FAIL] " << test_name << (detail.empty() ? "" : "  -> " + detail) << "\n";
    return condition;
}

bool check_fn(const std::string& name, std::function<bool()> fn) {
    bool ok = false;
    try { ok = fn(); } catch (const std::exception& e) {
        return check(false, name, std::string("exception: ") + e.what());
    }
    return check(ok, name);
}

void print_summary() {
    int passed = 0, failed = 0;
    for (auto& r : g_results) {
        if (r.passed) ++passed; else ++failed;
    }
    std::cout << "\n============================================================\n";
    std::cout << "  RESULTS:  " << passed << " passed,  " << failed << " failed"
              << "  (total " << (passed + failed) << ")\n";
    if (failed) {
        std::cout << "\n  Failed tests:\n";
        for (auto& r : g_results)
            if (!r.passed)
                std::cout << "    [" << r.group << "] " << r.name
                          << (r.detail.empty() ? "" : "  -> " + r.detail) << "\n";
    } else {
        std::cout << "\n  ! All tests were passed successfully !\n";
    }
    std::cout << "============================================================\n";
}



void test_constructors_default() {
    begin_group("Constructors – Default");

    // Verify default constructor creates empty SSO string
    check_fn("Default constructor: empty, SSO", [] {
        knst_byte_string s;
        return s.empty() && s.length() == 0 && !s.is_heap();
    });

    // Verify allocator constructor creates empty string with custom allocator
    check_fn("Allocator constructor: empty", [] {
        knst_default_allocator alloc;
        knst_byte_string s(alloc);
        return s.empty() && s.length() == 0;
    });
}

void test_constructors_char_ptr() {
    begin_group("Constructors – char*");

    // Construct from C-string, should stay in SSO for short strings
    check_fn("char* constructor: SSO", [] {
        knst_byte_string s("Hello");
        return s.length() == 5 && !s.is_heap() && s.data()[0] == 'H' && s.data()[4] == 'o';
    });

    // Construct with explicit size from char pointer
    check_fn("char* + size", [] {
        knst_byte_string s("HelloWorld", 5);  // Take only first 5 chars
        return s.length() == 5;
    });

    // Long string should be allocated on heap
    check_fn("char* constructor: heap size", [] {
        knst_byte_string s("This is a long string that will go to heap definitely");
        return s.is_heap() && s.length() > 11;
    });
}

void test_constructors_initializer_list() {
    begin_group("Constructors – initializer_list");

    // Small initializer list should fit in SSO buffer
    check_fn("SSO: 5 elements", [] {
        knst_byte_string s{1, 2, 3, 4, 5};
        return s.length() == 5 && !s.is_heap() && s.data()[0] == 1 && s.data()[4] == 5;
    });

    // Large initializer list should trigger heap allocation
    check_fn("Heap: 20 elements", [] {
        std::initializer_list<unsigned char> list = {
            1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20
        };
        knst_byte_string s(list);
        return s.length() == 20 && s.is_heap() && s.data()[0] == 1 && s.data()[19] == 20;
    });
}

void test_constructors_copy() {
    begin_group("Constructors – Copy");

    // Copy SSO string, should remain SSO in destination
    check_fn("Copy SSO", [] {
        knst_byte_string a("Hello");
        knst_byte_string b(a);  // Copy constructor
        return b.length() == 5 && !b.is_heap() && b.data()[0] == 'H';
    });

    // Copy heap string, destination should also be heap with same content
    check_fn("Copy heap", [] {
        knst_byte_string a("This is a long heap string for testing");
        knst_byte_string b(a);  // Deep copy of heap data
        return b.length() == a.length() && b.is_heap();
    });
}

void test_constructors_move() {
    begin_group("Constructors – Move");

    // After move, source should be empty and on SSO
    check_fn("Move: source becomes empty", [] {
        knst_byte_string a("Hello World");
        knst_byte_string b(std::move(a));  // Move constructor
        return a.empty() && a.length() == 0;
    });

    // Destination should take ownership of heap data
    check_fn("Move: destination has data", [] {
        knst_byte_string a("Hello World");
        size_t len = a.length();
        knst_byte_string b(std::move(a));  // Steals heap pointer
        return b.length() == len && b.is_heap();
    });
}



void test_methods_basic() {
    begin_group("Methods – Basic");

    // Verify length returns correct character count
    check_fn("length()", [] {
        knst_byte_string s("Hello");
        return s.length() == 5;
    });

    // Empty string should report empty=true
    check_fn("empty() true", [] {
        knst_byte_string s;
        return s.empty();
    });

    // Non-empty string should report empty=false
    check_fn("empty() false", [] {
        knst_byte_string s("Hello");
        return !s.empty();
    });

    // SSO capacity should be the predefined buffer size
    check_fn("capacity() SSO", [] {
        knst_byte_string s("Hello");
        return s.capacity() == KNST_SSO_BUFFER_CAPACITY && !s.is_heap();
    });

    // Heap capacity should be greater than string length
    check_fn("capacity() heap", [] {
        knst_byte_string s("This is a long string that goes to heap");
        return s.is_heap() && s.capacity() > s.length();
    });

    // data() should return valid non-null pointer
    check_fn("data() returns valid pointer", [] {
        knst_byte_string s("Hello");
        return s.data() != nullptr && s.data()[0] == 'H';
    });

    // Short string should be in SSO, not heap
    check_fn("is_heap() SSO = false", [] {
        knst_byte_string s("Hello");
        return !s.is_heap();
    });

    // Long string should be in heap mode
    check_fn("is_heap() heap = true", [] {
        knst_byte_string s("This is a long string that goes to heap");
        return s.is_heap();
    });
}

void test_methods_append() {
    begin_group("Methods – append");

    // Append one byte_string to another
    check_fn("append basic_byte_string", [] {
        knst_byte_string a("Hello");
        knst_byte_string b(" World");
        a.append(b);
        return a.length() == 11;
    });

    // Append raw unsigned char pointer with size
    check_fn("append unsigned char*", [] {
        knst_byte_string s("Hello");
        const unsigned char* data = (const unsigned char*)" World";
        s.append(data, 6);
        return s.length() == 11;
    });

    // Append within SSO capacity should stay SSO
    check_fn("append stays SSO", [] {
        knst_byte_string s("12345");
        s.append((const unsigned char*)"67890", 5);
        return s.length() == 10 && !s.is_heap();
    });

    // Append exceeding SSO capacity should move to heap
    check_fn("append moves to heap", [] {
        knst_byte_string s("12345");
        s.append((const unsigned char*)"678901", 6);
        return s.length() == 11 && s.is_heap();
    });

    // Append raw binary data with high bytes
    check_fn("append binary data", [] {
        knst_byte_string s;
        unsigned char data[] = {0xDE, 0xAD, 0xBE, 0xEF};
        s.append(data, 4);
        return s.length() == 4 && s.data()[0] == 0xDE && s.data()[3] == 0xEF;
    });
}

void test_methods_prepend() {
    begin_group("Methods – prepend");

    // Prepend one byte_string to another
    check_fn("prepend(basic_byte_string)", [] {
        knst_byte_string a(" World"), b("Hello");
        a.prepend(b);
        return a == "Hello World";
    });

    // Prepend raw unsigned char pointer with size
    check_fn("prepend(unsigned char*, size)", [] {
        knst_byte_string s(" World");
        const unsigned char* data = (const unsigned char*)"Hello";
        s.prepend(data, 5);
        return s == "Hello World";
    });

    // Prepend within SSO capacity should stay SSO
    check_fn("prepend stays SSO", [] {
        knst_byte_string s("67890");
        const unsigned char* data = (const unsigned char*)"12345";
        s.prepend(data, 5);
        return s.length() == 10 && !s.is_heap();
    });

    // Prepend exceeding SSO capacity should move to heap
    check_fn("prepend moves to heap", [] {
        knst_byte_string s("678901");
        const unsigned char* data = (const unsigned char*)"12345";
        s.prepend(data, 5);
        return s.length() == 11 && s.is_heap();
    });

    // Prepend with size=0 or null should not change string
    check_fn("prepend empty: no change", [] {
        knst_byte_string s("Hello");
        s.prepend(nullptr, 0);
        return s.length() == 5 && s == "Hello";
    });
}

void test_methods_shrink_to_fit() {
    begin_group("Methods – shrink_to_fit");

    // Shrinking a heap string to SSO-fit size should move back to SSO
    check_fn("Heap to SSO", [] {
        knst_byte_string s("This is a long string that will be shortened");
        s = knst_byte_string("Short");  // Now fits SSO
        s.shrink_to_fit();              // Should move to SSO
        return !s.is_heap() && s.length() == 5;
    });

    // Shrink should reduce capacity to length+1 for heap strings
    check_fn("Reduce capacity", [] {
        knst_byte_string s("Hello World");
        s.shrink_to_fit();
        return s.capacity() == s.length() + 1;
    });

    // Shrink on SSO string should be no-op
    check_fn("No-op on SSO", [] {
        knst_byte_string s("Hello");
        auto old_cap = s.capacity();
        s.shrink_to_fit();
        return s.capacity() == old_cap && !s.is_heap();
    });
}

void test_methods_clear() {
    begin_group("Methods – clear");

    // Clear SSO string should result in empty SSO
    check_fn("Clear SSO", [] {
        knst_byte_string s("Hello");
        s.clear();
        return s.empty() && s.length() == 0;
    });

    // Clear heap string should free heap and move to SSO
    check_fn("Clear heap", [] {
        knst_byte_string s("This is a long heap string");
        s.clear();
        return s.empty() && !s.is_heap();
    });

    // String should be reusable after clear
    check_fn("Clear then reuse", [] {
        knst_byte_string s("Hello World");
        s.clear();
        s = knst_byte_string("New");
        return s.length() == 3;
    });
}

void test_take_ownership() {
    begin_group("take_ownership – Static Factory");

    // Small data: ownership taken, copied to SSO, original deleted
    check_fn("take_ownership: SSO (small data)", [] {
        unsigned char* data = new unsigned char[5]{'H', 'e', 'l', 'l', 'o'};
        knst_byte_string s = knst_byte_string::take_ownership(data, 5);
        return s.length() == 5 && !s.is_heap() && s[0] == 'H' && s[4] == 'o';
    });

    // Large data: ownership taken, heap pointer stolen (no copy)
    check_fn("take_ownership: heap (large data)", [] {
        uint32_t size = KNST_SSO_BUFFER_LENGTH + 10;
        unsigned char* data = new unsigned char[size];
        memset(data, 0xAB, size);
        
        knst_byte_string s = knst_byte_string::take_ownership(data, size);
        return s.length() == size && s.is_heap() && s[0] == 0xAB && s[size-1] == 0xAB;
    });

    // Size=0 should result in empty string, original deleted
    check_fn("take_ownership: empty data (size 0)", [] {
        unsigned char* data = new unsigned char[1]{0x42};
        knst_byte_string s = knst_byte_string::take_ownership(data, 0);
        return s.empty() && s.length() == 0;
    });

    // Nullptr with size>0 should be handled gracefully
    check_fn("take_ownership: nullptr", [] {
        knst_byte_string s = knst_byte_string::take_ownership(nullptr, 10);
        return s.empty() && s.length() == 0;
    });

    // Exact SSO boundary: should stay SSO, original deleted
    check_fn("take_ownership: exact SSO boundary", [] {
        uint32_t size = KNST_SSO_BUFFER_LENGTH;
        unsigned char* data = new unsigned char[size];
        memset(data, 0xCC, size);
        
        knst_byte_string s = knst_byte_string::take_ownership(data, size);
        return s.length() == size && !s.is_heap() && s[0] == 0xCC && s[size-1] == 0xCC;
    });

    // SSO+1: should go to heap, pointer stolen (no copy)
    check_fn("take_ownership: SSO + 1 (heap)", [] {
        uint32_t size = KNST_SSO_BUFFER_LENGTH + 1;
        unsigned char* data = new unsigned char[size];
        memset(data, 0xDD, size);
        
        knst_byte_string s = knst_byte_string::take_ownership(data, size);
        return s.length() == size && s.is_heap() && s[0] == 0xDD && s[size-1] == 0xDD;
    });

    // Binary data with embedded null bytes should be preserved
    check_fn("take_ownership: binary data with nulls", [] {
        unsigned char* data = new unsigned char[5]{0x48, 0x00, 0x4C, 0x00, 0x4F};
        knst_byte_string s = knst_byte_string::take_ownership(data, 5);
        return s.length() == 5 && s[0] == 0x48 && s[1] == 0x00 && s[4] == 0x4F;
    });

    // Heap capacity should be size+1 (null terminator)
    check_fn("take_ownership: capacity is correct", [] {
        uint32_t size = KNST_SSO_BUFFER_LENGTH + 5;
        unsigned char* data = new unsigned char[size];
        memset(data, 0xEE, size);
        
        knst_byte_string s = knst_byte_string::take_ownership(data, size);
        return s.capacity() == size + 1;
    });

    // All bytes should be accessible and correct after ownership transfer
    check_fn("take_ownership: data integrity", [] {
        uint32_t size = 100;
        unsigned char* data = new unsigned char[size];
        for (uint32_t i = 0; i < size; i++) {
            data[i] = (unsigned char)(i % 256);
        }
        
        knst_byte_string s = knst_byte_string::take_ownership(data, size);

        for (uint32_t i = 0; i < size; i++) {
            if (s[i] != (unsigned char)(i % 256)) return false;
        }
        return true;
    });
}



void test_operators_assignment() {
    begin_group("Operators – Assignment");

    // Copy assignment between two SSO strings
    check_fn("Copy assignment: SSO to SSO", [] {
        knst_byte_string a("Hello");
        knst_byte_string b("World");
        b = a;  // Copy assignment operator
        return b == "Hello" && b.length() == 5;
    });

    // Copy assignment between two heap strings
    check_fn("Copy assignment: heap to heap", [] {
        knst_byte_string a("This is a long heap string for testing");
        knst_byte_string b("Another long heap string here");
        b = a;  // Deep copy
        return b == a;
    });

    // Move assignment should leave source empty
    check_fn("Move assignment: source becomes empty", [] {
        knst_byte_string a("Hello World");
        knst_byte_string b("Test");
        b = std::move(a);  // Move assignment operator
        return a.empty() && a.length() == 0;
    });

    // Move assignment should transfer data correctly
    check_fn("Move assignment: destination has data", [] {
        knst_byte_string a("Hello World");
        knst_byte_string b("Test");
        b = std::move(a);
        return b == "Hello World";
    });

    // Self copy-assignment should be harmless
    check_fn("Self copy-assignment: no crash", [] {
        knst_byte_string a("Hello");
        a = a;  // Self assignment
        return a == "Hello";
    });

    // Self move-assignment should not cause undefined behavior
    check_fn("Self move-assignment: no crash", [] {
        knst_byte_string b("Test");
        b = std::move(b);  // Self move assignment
        return true;  // Just checking it doesn't crash
    });

    // Assign a single char to the string
    check_fn("char assignment: 'A'", [] {
        knst_byte_string s("Hello");
        s = 'A';
        return s.length() == 1 && s[0] == 'A';
    });

    // Assign a single unsigned char (0xFF) to the string
    check_fn("unsigned char assignment: 0xFF", [] {
        knst_byte_string s;
        s = (unsigned char)0xFF;
        return s.length() == 1 && s[0] == 0xFF;
    });

    // Assign initializer_list with binary values
    check_fn("initializer_list assignment", [] {
        knst_byte_string s("Test");
        s = {0xDE, 0xAD, 0xBE, 0xEF};
        return s.length() == 4 && s[0] == 0xDE && s[3] == 0xEF;
    });

    // Assign C-string literal
    check_fn("const char* assignment", [] {
        knst_byte_string s;
        s = "Hello World";
        return s == "Hello World";
    });

    // Assign unsigned char pointer
    check_fn("const unsigned char* assignment", [] {
        knst_byte_string s;
        const unsigned char* ptr = (const unsigned char*)"Test";
        s = ptr;
        return s.length() == 4;
    });
}


void test_operators_comparison() {
    begin_group("Operators – Comparison");

    // Equality between two byte_strings
    check_fn("== basic_byte_string", [] {
        knst_byte_string a("Hello"), b("Hello"), c("World");
        return a == b && !(a == c);
    });

    // Inequality between two byte_strings
    check_fn("!= basic_byte_string", [] {
        knst_byte_string a("Hello"), b("World");
        return a != b;
    });

    // Lexicographical less-than comparison
    check_fn("< basic_byte_string", [] {
        knst_byte_string a("apple"), b("banana");
        return a < b && !(b < a);
    });

    // Less-than-or-equal with equal and greater strings
    check_fn("<= basic_byte_string", [] {
        knst_byte_string a("apple"), b("apple"), c("banana");
        return a <= b && a <= c;
    });

    // Greater-than comparison
    check_fn("> basic_byte_string", [] {
        knst_byte_string a("banana"), b("apple");
        return a > b;
    });

    // Greater-than-or-equal with equal and smaller strings
    check_fn(">= basic_byte_string", [] {
        knst_byte_string a("banana"), b("apple"), c("banana");
        return a >= b && a >= c;
    });

    // Equality with C-string
    check_fn("== const char*", [] {
        knst_byte_string s("Hello");
        return s == "Hello" && !(s == "World");
    });

    // Inequality with C-string
    check_fn("!= const char*", [] {
        knst_byte_string s("Hello");
        return s != "World";
    });

    // C-string on left side of equality
    check_fn("const char* == knst", [] {
        knst_byte_string s("Hello");
        return "Hello" == s;
    });

    // C-string on left side of inequality
    check_fn("const char* != knst", [] {
        knst_byte_string s("Hello");
        return "World" != s;
    });

    // Lexicographical comparison with C-string on left
    check_fn("< const char*", [] {
        knst_byte_string s("banana");
        return "apple" < s && s < "cherry";
    });

    // Greater-than comparison with C-string on left
    check_fn("> const char*", [] {
        knst_byte_string s("banana");
        return "cherry" > s && s > "apple";
    });

    // Equality with unsigned char pointer
    check_fn("== const unsigned char*", [] {
        knst_byte_string s("Test");
        const unsigned char* ptr = (const unsigned char*)"Test";
        return s == ptr;
    });

    // Inequality with unsigned char pointer
    check_fn("!= const unsigned char*", [] {
        knst_byte_string s("Test");
        const unsigned char* ptr = (const unsigned char*)"Different";
        return s != ptr;
    });
}



void test_operators_index() {
    begin_group("Operators – operator[]");

    // Read first byte via const operator[]
    check_fn("Read first byte", [] {
        knst_byte_string s("Hello");
        return s[0] == 'H';
    });

    // Read last byte
    check_fn("Read last byte", [] {
        knst_byte_string s("Hello");
        return s[4] == 'o';
    });

    // Write via non-const operator[]
    check_fn("Write second byte", [] {
        knst_byte_string s("Hello");
        s[1] = 'a';
        return s[1] == 'a';
    });

    // Read/write binary data with high bytes
    check_fn("Read/write binary data", [] {
        knst_byte_string s{0x00, 0x01, 0x02};
        s[1] = 0xFF;
        return s[1] == 0xFF;
    });

    // Index operator works correctly on heap strings
    check_fn("operator[] on heap string", [] {
        knst_byte_string s("Hello World from heap");
        return s[6] == 'W' && s[10] == 'd';
    });
}

void test_operators_bool() {
    begin_group("Operators – Bool");

    // Non-empty string converts to true
    check_fn("Non-empty string is true", [] {
        knst_byte_string s("Hello");
        return (bool)s == true;
    });

    // Empty string converts to false
    check_fn("Empty string is false", [] {
        knst_byte_string s;
        return (bool)s == false;
    });

    // operator! on non-empty returns false
    check_fn("operator! on non-empty", [] {
        knst_byte_string s("Hello");
        return !s == false;
    });

    // operator! on empty returns true
    check_fn("operator! on empty", [] {
        knst_byte_string s;
        return !s == true;
    });
}

void test_operators_concat() {
    begin_group("Operators – Concatenation (+=, +)");

    // += with another byte_string
    check_fn("+= basic_byte_string", [] {
        knst_byte_string a("Hello"), b(" World");
        a += b;
        return a == "Hello World" && a.length() == 11;
    });

    // += with C-string
    check_fn("+= const char*", [] {
        knst_byte_string s("Hello");
        s += " World";
        return s == "Hello World";
    });

    // += with unsigned char pointer
    check_fn("+= const unsigned char*", [] {
        knst_byte_string s("Hello");
        const unsigned char* ptr = (const unsigned char*)" World";
        s += ptr;
        return s.length() == 11;
    });

    // += with single char
    check_fn("+= char", [] {
        knst_byte_string s("Test");
        s += '!';
        return s.length() == 5 && s[4] == '!';
    });

    // += with single unsigned char (high byte)
    check_fn("+= unsigned char", [] {
        knst_byte_string s("Test");
        s += (unsigned char)0xFF;
        return s.length() == 5 && s[4] == 0xFF;
    });

    // += with initializer_list of bytes
    check_fn("+= initializer_list", [] {
        knst_byte_string s("AB");
        s += {0x01, 0x02, 0x03};
        return s.length() == 5 && s[2] == 0x01 && s[4] == 0x03;
    });

    // += with empty string should not change length
    check_fn("+= empty: no change", [] {
        knst_byte_string s("Hello");
        s += "";
        return s.length() == 5;
    });
}



void test_operators_edge_cases() {
    begin_group("Edge Cases");

    // Two empty strings should be equal
    check_fn("Empty string comparison", [] {
        knst_byte_string a, b;
        return a == b && a == "" && "" == a;
    });

    // Null C-string should compare equal to empty string
    check_fn("Null char* comparison", [] {
        knst_byte_string s;
        const char* null_ptr = nullptr;
        return s == null_ptr;
    });

    // Assign null C-string should result in empty string
    check_fn("Assign null char*", [] {
        knst_byte_string s("Hello");
        s = (const char*)nullptr;
        return s.empty();
    });

    // String exactly at SSO boundary should stay in SSO
    check_fn("Exact SSO boundary", [] {
        std::vector<char> buffer(KNST_SSO_BUFFER_LENGTH + 1, 'A');
        buffer[KNST_SSO_BUFFER_LENGTH] = '\0';
        knst_byte_string s(buffer.data());
        return s.length() == KNST_SSO_BUFFER_LENGTH && !s.is_heap();
    });

    // String exceeding SSO by 1 should move to heap
    check_fn("SSO + 1 goes to heap", [] {
        uint32_t size = KNST_SSO_BUFFER_LENGTH + 1;
        std::vector<char> buffer(size + 1, 'A');
        buffer[size] = '\0';
        
        knst_byte_string s(buffer.data(), size);  
        return s.is_heap() && s.length() == size;
    });
}

void test_debug_binary() {
    begin_group("Debug - Binary Only");

    // Single byte binary
    check_fn("Binary 1 byte", [] {
        unsigned char data[] = {0x48};
        knst_byte_string s(data, 1);
        return s.length() == 1 && s[0] == 0x48;
    });

    // Two bytes without null
    check_fn("Binary 2 bytes", [] {
        unsigned char data[] = {0x48, 0x49};
        knst_byte_string s(data, 2);
        return s.length() == 2 && s[0] == 0x48 && s[1] == 0x49;
    });

    // Three bytes without null
    check_fn("Binary 3 bytes", [] {
        unsigned char data[] = {0x48, 0x49, 0x4A};
        knst_byte_string s(data, 3);
        return s.length() == 3 && s[0] == 0x48 && s[2] == 0x4A;
    });

    // Four bytes without null
    check_fn("Binary 4 bytes", [] {
        unsigned char data[] = {0x48, 0x49, 0x4A, 0x4B};
        knst_byte_string s(data, 4);
        return s.length() == 4 && s[0] == 0x48 && s[3] == 0x4B;
    });

    // Five bytes WITH embedded null bytes (0x00)
    check_fn("Binary 5 bytes - original", [] {
        unsigned char data[] = {0x48, 0x00, 0x4C, 0x00, 0x4F};
        knst_byte_string s(data, 5);
        return s.length() == 5 && s[0] == 0x48 && s[4] == 0x4F;
    });
}



void test_console_output() {
    begin_group("Console Output");

    
    check_fn("cout SSO", [] {
        knst_byte_string s("Hello");
        std::cout << "  cout: " << s << std::endl;
        return true;
    });

   
    check_fn("cout heap", [] {
        knst_byte_string s("This is a long heap string for cout testing!");
        std::cout << "  cout: " << s << std::endl;
        return true;
    });

   
    check_fn("cout empty", [] {
        knst_byte_string s;
        std::cout << "  cout empty: '" << s << "'" << std::endl;
        return true;
    });
}


int main() {
    knst_init_console();

    
    test_constructors_default();
    test_constructors_char_ptr();
    test_constructors_initializer_list();
    test_constructors_copy();
    test_constructors_move();

   
    test_methods_basic();
    test_methods_append();
    test_methods_prepend();          
    test_methods_shrink_to_fit();
    test_methods_clear();
    test_take_ownership();

   
    test_operators_assignment();
    test_operators_comparison();
    test_operators_index();
    test_operators_bool();
    test_operators_concat();

   
    test_operators_edge_cases();
    test_debug_binary();

    
    test_console_output();

    print_summary();

    return 0;
}