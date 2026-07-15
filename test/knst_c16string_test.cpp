#include <iostream>
#include <functional>


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

// Begin a named group of tests
void begin_group(const std::string& name) {
    g_current_group = name;
    std::cout << "\n=== " << name << " ===\n";
}

// Core check: records result and prints pass/fail immediately
bool check(bool condition, const std::string& test_name, const std::string& detail = "") {
    g_results.push_back({g_current_group, test_name, condition, detail});
    if (condition)
        std::cout << "  [PASS] " << test_name << "\n";
    else
        std::cout << "  [FAIL] " << test_name<< (detail.empty() ? "" : "  -> " + detail) << "\n";
                  
    return condition;
}

// Convenience: wrap a lambda that returns bool
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
    std::cout << "  RESULTS:  " << passed << " passed,  " << failed << " failed"<< "  (total " << (passed + failed) << ")\n";
              
    if (failed) {
        std::cout << "\n  Failed tests:\n";
        for (auto& r : g_results)
            if (!r.passed)
                std::cout << "    [" << r.group << "] " << r.name<< (r.detail.empty() ? "" : "  -> " + r.detail) << "\n";
                          
    } else {
        std::cout << "\n  ! All tests were passed successfully !\n";
    }
    std::cout << "============================================================\n";
}

// ============================================================
//  Helper: convert char16_t pointer to printable narrow string
// ============================================================
std::string to_narrow(const char16_t* p) {
    std::string out;
    while (*p) out += (char)(unsigned char)(*p & 0xFF);
    return out;
}

// ============================================================
//  TEST SECTIONS
// ============================================================

// ---- COW (Copy-On-Write) -----------------------------------
#ifndef KNST_C16STRING_DEACTIVE_COW
void test_cow() {
    begin_group("COW – Copy On Write");

    // Shared pointer after copy
    check_fn("Shared data pointer after copy", [] {
        knst_c16string a(u"This is a long string that goes onto the heap definitely");
        knst_c16string b = a;
        return (void*)a.data() == (void*)b.data();
    });

    // Detach on write
    check_fn("Detach on write: pointers diverge", [] {
        knst_c16string a(u"This is a long string that goes onto the heap definitely");
        knst_c16string b = a;
        b.append(u" MODIFIED");
        return (void*)a.data() != (void*)b.data();
    });

    // Original unchanged after modification of copy
    check_fn("Original unchanged after modifying copy", [] {
        knst_c16string a(u"This is a long string that goes onto the heap definitely");
        knst_c16string b = a;
        b.append(u" MODIFIED");
        return a == u"This is a long string that goes onto the heap definitely" && a != b;
    });

    // Copy assignment COW
    check_fn("Copy-assign then modify: original intact", [] {
        knst_c16string a(u"Another long heap string for testing purposes here");
        knst_c16string b;
        b = a;
        b += u" extra";
        return a == u"Another long heap string for testing purposes here" && b != a;
    });

    // Three-way share: b modifies, a and c unchanged
    check_fn("Three-way share: only b modified", [] {
        knst_c16string a(u"Shared data across three different string instances");
        knst_c16string b = a;
        knst_c16string c = a;
        b.append(u"!!!");
        return a == u"Shared data across three different string instances"
            && c == u"Shared data across three different string instances"
            && b != a;
    });

    // Explicit detach returns true when shared
    check_fn("Explicit detach() returns true when shared", [] {
        knst_c16string a(u"Test string for explicit detach functionality check");
        knst_c16string b = a;
        return b.detach();
    });

    // Second detach returns false (already unique)
    check_fn("Second detach() returns false when already unique", [] {
        knst_c16string a(u"Test string for explicit detach functionality check");
        knst_c16string b = a;
        b.detach();
        return !b.detach();
    });
}
#endif // KNST_C16STRING_DEACTIVE_COW

// ---- Constructors ------------------------------------------

void test_constructors_char16() {
    begin_group("Constructors – char16_t");

    check_fn("Heap: u\"Hello World\" len=11 cap=12", [] {
        knst_c16string s(u"Hello World");
        return s.length() == 11 && s.capacity() == 12 && s.is_heap();
    });

    check_fn("Stack: u\"Hello\" len=5 cap=11 !heap", [] {
        knst_c16string s(u"Hello");
        return s.length() == 5 && s.capacity() == 11 && !s.is_heap();
    });

    check_fn("Ptr+len: (data,9) len=9 cap=11 !heap", [] {
        const char16_t data[] = u"Hello C++";
        knst_c16string s(data, 9);
        return s.length() == 9 && s.capacity() == 11 && !s.is_heap();
    });

    check_fn("Fill char16_t: (12, u'A') len=12 cap=13 heap", [] {
        knst_c16string s(12, u'A');
        return s.length() == 12 && s.capacity() == 13 && s.is_heap();
    });

    check_fn("Repeat string: (8, u\"-Hello-\") len=56 cap=57 heap", [] {
        knst_c16string s(8, u"-Hello-");
        return s.length() == 56 && s.capacity() == 57 && s.is_heap();
    });
}

void test_constructors_char() {
    begin_group("Constructors – char");

    check_fn("Heap: \"Hello World\" len=11 cap=12", [] {
        knst_c16string s("Hello World");
        return s.length() == 11 && s.capacity() == 12 && s.is_heap();
    });

    check_fn("Stack: \"Hello\" len=5 cap=11 !heap", [] {
        knst_c16string s("Hello");
        return s.length() == 5 && s.capacity() == 11 && !s.is_heap();
    });

    check_fn("Ptr+len: (data,9) len=9 cap=11 !heap", [] {
        const char data[] = "Hello C++";
        knst_c16string s(data, 9);
        return s.length() == 9 && s.capacity() == 11 && !s.is_heap();
    });

    check_fn("Fill char: (12, 'A') len=12 cap=13 heap", [] {
        knst_c16string s(12, 'A');
        return s.length() == 12 && s.capacity() == 13 && s.is_heap();
    });

    check_fn("Repeat string: (8, \"-Hello-\") len=56 cap=57 heap", [] {
        knst_c16string s(8, "-Hello-");
        return s.length() == 56 && s.capacity() == 57 && s.is_heap();
    });
}

void test_constructors_wchar() {
    begin_group("Constructors – wchar_t");

    check_fn("Heap: L\"Hello World\" len=11 cap=12", [] {
        knst_c16string s(L"Hello World");
        return s.length() == 11 && s.capacity() == 12 && s.is_heap();
    });

    check_fn("Stack: L\"Hello\" len=5 cap=11 !heap", [] {
        knst_c16string s(L"Hello");
        return s.length() == 5 && s.capacity() == 11 && !s.is_heap();
    });

    check_fn("Ptr+len: (data,9) len=9 cap=11 !heap", [] {
        const wchar_t data[] = L"Hello C++";
        knst_c16string s(data, 9);
        return s.length() == 9 && s.capacity() == 11 && !s.is_heap();
    });

    check_fn("Fill wchar_t: (12, L'A') len=12 cap=13 heap", [] {
        knst_c16string s(12, L'A');
        return s.length() == 12 && s.capacity() == 13 && s.is_heap();
    });

    check_fn("Repeat string char: (8, \"-Hello-\") len=56 cap=57 heap", [] {
        knst_c16string s(8, "-Hello-");
        return s.length() == 56 && s.capacity() == 57 && s.is_heap();
    });
}

void test_constructors_char32() {
    begin_group("Constructors – char32_t");

    check_fn("Heap: U\"Hello World\" len=11 cap=12", [] {
        knst_c16string s(U"Hello World");
        return s.length() == 11 && s.capacity() == 12 && s.is_heap();
    });

    check_fn("Stack: U\"Hello\" len=5 cap=11 !heap", [] {
        knst_c16string s(U"Hello");
        return s.length() == 5 && s.capacity() == 11 && !s.is_heap();
    });

    check_fn("Ptr+len: (data,9) len=9 cap=11 !heap", [] {
        const char32_t data[] = U"Hello C++";
        knst_c16string s(data, 9);
        return s.length() == 9 && s.capacity() == 11 && !s.is_heap();
    });

    check_fn("Fill char32_t: (12, U'A') len=12 cap=13 heap", [] {
        knst_c16string s(12, U'A');
        return s.length() == 12 && s.capacity() == 13 && s.is_heap();
    });
}

void test_constructors_copy_move() {
    begin_group("Constructors – Copy & Move");

    check_fn("Copy ctor: heap string copied correctly", [] {
        knst_c16string a(u"Hello World");
        knst_c16string b(a);
        return b.length() == 11 && b.capacity() == 12 && b.is_heap();
    });

    check_fn("Move ctor: source becomes empty stack", [] {
        knst_c16string a(u"Hello World");
        knst_c16string b(std::move(a));
        return a.length() == 0 && a.capacity() == 11 && !a.is_heap();
    });

    check_fn("Move ctor: destination has original data", [] {
        knst_c16string a(u"Hello World");
        knst_c16string b(std::move(a));
        return b.length() == 11 && b.capacity() == 12 && b.is_heap();
    });
}

void test_constructors_numeric() {
    begin_group("Constructors – Numeric types");

    check_fn("int positive: 123456789", [] {
        knst_c16string s(123456789);
        return s == "123456789";
    });

    check_fn("int negative: -123456789", [] {
        knst_c16string s(-123456789);
        return s == "-123456789";
    });

    check_fn("long long: 12345678910", [] {
        knst_c16string s(12345678910LL);
        return s == "12345678910";
    });

    check_fn("float: 13.13", [] {
        knst_c16string s(13.13f);
        return s == "13.13";
    });

    check_fn("double: 132.13", [] {
        knst_c16string s(132.13);
        return s == "132.13";
    });
}

void test_constructors_std_types() {
    begin_group("Constructors – std string types & views");

    check_fn("std::string", [] {
        knst_c16string s(std::string("Hello"));
        return s == "Hello";
    });

    check_fn("std::wstring", [] {
        knst_c16string s(std::wstring(L"Hello"));
        return s == "Hello";
    });

    check_fn("std::u16string", [] {
        knst_c16string s(std::u16string(u"Hello"));
        return s == "Hello";
    });

    check_fn("std::u32string", [] {
        knst_c16string s(std::u32string(U"Hello"));
        return s == "Hello";
    });

    check_fn("std::string_view", [] {
        std::string_view sv("Hello");
        knst_c16string s(sv);
        return s == "Hello";
    });

    check_fn("std::wstring_view", [] {
        std::wstring_view sv(L"Hello");
        knst_c16string s(sv);
        return s == "Hello";
    });

    check_fn("std::u16string_view", [] {
        std::u16string_view sv(u"Hello");
        knst_c16string s(sv);
        return s == "Hello";
    });

    check_fn("std::u32string_view", [] {
        std::u32string_view sv(U"Hello");
        knst_c16string s(sv);
        return s == "Hello";
    });

    check_fn("initializer_list<char16_t>", [] {
        knst_c16string s{u'H', u'e', u'l', u'l', u'o'};
        return s == "Hello" && s.length() == 5;
    });

    check_fn("std::vector<char16_t>", [] {
        std::vector<char16_t> v = {u'H', u'e', u'l', u'l', u'o'};
        knst_c16string s(v);
        return s == "Hello" && s.length() == 5;
    });
}

// ---- Operators ---------------------------------------------

void test_operators_assignment() {
    begin_group("Operators – Assignment");

    check_fn("Copy operator= updates data correctly", [] {
        knst_c16string a(u"Hello World");
        knst_c16string b("Hello");
        b = a;
        return b.length() == 11 && b.capacity() == 12 && b.is_heap();
    });

    check_fn("Move operator= source becomes empty", [] {
        knst_c16string a(u"Hello World");
        knst_c16string b("Hello");
        b = std::move(a);
        return a.length() == 0 && a.capacity() == 11 && !a.is_heap() && a.empty();
    });

    check_fn("Move operator= destination has data", [] {
        knst_c16string a(u"Hello World");
        knst_c16string b("Hello");
        b = std::move(a);
        return b.length() == 11 && b.capacity() == 12 && b.is_heap();
    });

    check_fn("Self copy-assignment is safe", [] {
        knst_c16string a(u"Self assignment test string");
        a = a;
        return a == u"Self assignment test string";
    });

    check_fn("Self move-assignment does not crash", [] {
        knst_c16string b(u"Self move test");
        b = std::move(b);
        return true; // must not crash/assert
    });
}

void test_operators_comparison() {
    begin_group("Operators – Comparison (==, !=, <, <=, >, >=)");

    // == with all literal types
    check_fn("== char*", []  { knst_c16string s("Hello"); return s == "Hello"; });
    check_fn("== char16_t*", [] { knst_c16string s("Hello"); return s == u"Hello"; });
    check_fn("== wchar_t*", []  { knst_c16string s("Hello"); return s == L"Hello"; });
    check_fn("== char32_t*", [] { knst_c16string s("Hello"); return s == U"Hello"; });

    // reversed operand
    check_fn("char* == knst",    [] { knst_c16string s("Hello"); return "Hello"  == s; });
    check_fn("char16_t* == knst",[] { knst_c16string s("Hello"); return u"Hello" == s; });
    check_fn("wchar_t* == knst", [] { knst_c16string s("Hello"); return L"Hello" == s; });
    check_fn("char32_t* == knst",[] { knst_c16string s("Hello"); return U"Hello" == s; });

    // !=
    check_fn("!= different string", [] {
        knst_c16string s("Hello");
        return U"Hello World" != s;
    });

    // < <= > >=
    check_fn("apple < banana", [] {
        knst_c16string a(u"apple"), b(u"banana");
        return a < b && !(b < a);
    });
    check_fn("apple <= banana and apple <= apple", [] {
        knst_c16string a(u"apple"), b(u"banana");
        return a <= b && a <= a;
    });
    check_fn("banana > apple", [] {
        knst_c16string a(u"apple"), b(u"banana");
        return b > a && !(a > b);
    });
    check_fn("banana >= apple and apple >= apple", [] {
        knst_c16string a(u"apple"), b(u"banana");
        return b >= a && a >= a;
    });

    // < with literals
    check_fn("knst < char16_t* literal", [] {
        knst_c16string a(u"apple");
        return a < u"banana" && a < "banana" && a < L"banana";
    });
    check_fn("char* literal < knst", [] {
        knst_c16string b(u"banana");
        return u"apple" < b && "apple" < b && L"apple" < b && U"apple" < b;
    });

    // prefix comparison
    check_fn("abc < abd", [] {
        knst_c16string c(u"abd"), d(u"abc");
        return d < c;
    });

    // prefix length difference
    check_fn("hello < hello world (prefix shorter)", [] {
        knst_c16string e(u"hello"), f(u"hello world");
        return e < f;
    });
}

void test_operators_index() {
    begin_group("Operators – operator[]");

    check_fn("operator[] write: change second char", [] {
        knst_c16string s(u"Hello World");
        s[1] = u'E';
        return s == U"HEllo World";
    });

    check_fn("operator[] read: first char is H", [] {
        knst_c16string s(u"Hello World");
        return s[0] == u'H';
    });
}

void test_operators_concat() {
    begin_group("Operators – += and +");

    check_fn("+= knst_c16string: Hello + World = Hello World len=11 cap=22 (2x growth)", [] {
        knst_c16string a(u"Hello"), b(u" World");
        a += b;
        // SSO cap=11, overflow -> 2x growth: 11*2=22
        return a == u"Hello World" && a.length() == 11 && a.is_heap() && a.capacity() == 22;
    });

    check_fn("+= char*", [] {
        knst_c16string s(u"Hello");
        s += " World";
        return s == "Hello World";
    });

    check_fn("+= char16_t*", [] {
        knst_c16string s(u"Hello");
        s += u" World";
        return s == u"Hello World";
    });

    check_fn("+= wchar_t*", [] {
        knst_c16string s(u"Hello");
        s += L" World";
        return s == "Hello World";
    });

    check_fn("+= char32_t*", [] {
        knst_c16string s(u"Hello");
        s += U" World";
        return s == "Hello World";
    });



    check_fn("adding a numeric value using += (int)", [] {
        knst_c16string s("test");
        s += 1234567;
        return s.length() == 11 && s.is_heap() && s == u"test1234567";
    });

    check_fn("adding a numeric value using += (-int)", [] {
        knst_c16string s("test");
        s += -1234567;
        return s.length() == 12 && s.is_heap() && s == u"test-1234567";
    });

    check_fn("adding a numeric value using += (long)", [] {
        knst_c16string s("test");
        s += 1234567891011;
        return s.length() == 17 && s.is_heap() && s == u"test1234567891011";
    });

    check_fn("adding a numeric value using += (-long)", [] {
        knst_c16string s("test");
        s += -1234567891011;
        return s.length() == 18 && s.is_heap() && s == u"test-1234567891011";
    });



    check_fn("operator+ knst+knst: Hello World len=11 cap=22 (2x growth)", [] {
        knst_c16string a(u"Hello"), b(u" World");
        knst_c16string c = a + b;
        // SSO cap=11, overflow -> 2x growth: 11*2=22
        return c == u"Hello World" && c.length() == 11 && c.is_heap() && c.capacity() == 22;
    });

    check_fn("operator+ knst + char*", [] {
        knst_c16string a(u"Hello");
        knst_c16string c = a + " World";
        return c == "Hello World";
    });

    check_fn("operator+ knst + char16_t*", [] {
        knst_c16string a(u"Hello");
        knst_c16string c = a + u" World";
        return c == u"Hello World";
    });

    check_fn("operator+ char* + knst", [] {
        knst_c16string b(u" World");
        knst_c16string c = "Hello" + b;
        return c == "Hello World";
    });

    check_fn("operator+ char16_t* + knst", [] {
        knst_c16string b(u" World");
        knst_c16string c = u"Hello" + b;
        return c == u"Hello World";
    });
}

// ---- Methods -----------------------------------------------

void test_methods_basic() {
    begin_group("Methods – length, capacity, empty, is_heap");

    check_fn("length() of Hello == 5", [] {
        knst_c16string s(u"Hello");
        return s.length() == 5;
    });

    check_fn("capacity() of Hello == 11 (SSO)", [] {
        knst_c16string s(u"Hello");
        return s.capacity() == 11;
    });

    check_fn("empty() on empty string", [] {
        knst_c16string s(u"");
        return s.empty() && s.length() == 0;
    });

    check_fn("!empty() on non-empty string", [] {
        knst_c16string s(u"Hello");
        return !s.empty();
    });

    check_fn("Exactly 10 chars stays on stack", [] {
        knst_c16string s(u"1234567890");
        return s.length() == 10 && !s.is_heap();
    });

    check_fn("11 chars goes to heap", [] {
        knst_c16string s(u"12345678901");
        return s.length() == 11 && s.is_heap();
    });
}

void test_methods_append() {
    begin_group("Methods – append");

    check_fn("append(knst_c16string)", [] {
        knst_c16string a(u"Hello"), b(u" World");
        a.append(b);
        return a == u"Hello World" && a.length() == 11;
    });

    check_fn("append(char16_t*)", [] {
        knst_c16string s(u"Hello");
        s.append(u" World");
        return s == u"Hello World";
    });

    check_fn("append(char*)", [] {
        knst_c16string s(u"Hello");
        s.append(" World");
        return s == "Hello World";
    });

    check_fn("append(wchar_t*)", [] {
        knst_c16string s(u"Hello");
        s.append(L" World");
        return s == "Hello World";
    });

    check_fn("append(char32_t*)", [] {
        knst_c16string s(u"Hello");
        s.append(U" World");
        return s == "Hello World";
    });

    check_fn("append stays on stack if fits", [] {
        knst_c16string s(u"12345");
        s.append(u"67890");
        return s.length() == 10 && !s.is_heap();
    });

    check_fn("append moves to heap when overflow", [] {
        knst_c16string s(u"12345");
        s.append(u"678901");
        return s.length() == 11 && s.is_heap();
    });

    check_fn("adding a numeric value using append (int)", [] {
        knst_c16string s("test");
        s.append(1234567);
        return s.length() == 11 && s.is_heap() && s == u"test1234567";
    });

    check_fn("adding a numeric value using append (-int)", [] {
        knst_c16string s("test");
        s.append(-1234567);
        return s.length() == 12 && s.is_heap() && s == u"test-1234567";
    });

    check_fn("adding a numeric value using append (long)", [] {
        knst_c16string s("test");
        s.append(1234567891011);
        return s.length() == 17 && s.is_heap() && s == u"test1234567891011";
    });

    check_fn("adding a numeric value using append (-long)", [] {
        knst_c16string s("test");
        s.append(-1234567891011);
        return s.length() == 18 && s.is_heap() && s == u"test-1234567891011";
    });


}

void test_methods_reserve() {
    begin_group("Methods – reserve");

    check_fn("reserve(100): cap=101, len unchanged, heap", [] {
        knst_c16string s(u"Hello");
        s.reserve(100);
        return s.capacity() == 101 && s.length() == 5 && s.is_heap();
    });

    check_fn("reserve smaller than current: no-op", [] {
        knst_c16string s(u"Hello");
        s.reserve(100);
        size_t old_cap = s.capacity();
        s.reserve(3);
        return s.capacity() == old_cap; // should not shrink
    });
}

void test_methods_resize() {
    begin_group("Methods – resize");

    check_fn("resize(5, 'a'): stack, len=5", [] {
        knst_c16string s;
        s.resize(5, u'a');
        return !s.is_heap() && s.length() == 5;
    });

    check_fn("resize(14, 'b'): heap, len=14", [] {
        knst_c16string s;
        s.resize(5, u'a');
        s.resize(14, U'b');
        return s.is_heap() && s.length() == 14;
    });

    check_fn("resize(20, 'c'): heap, len=20", [] {
        knst_c16string s;
        s.resize(5, u'a');
        s.resize(14, U'b');
        s.resize(20, u'c');
        return s.is_heap() && s.length() == 20;
    });

    check_fn("resize(15) truncation: heap, len=15", [] {
        knst_c16string s;
        s.resize(20, u'x');
        s.resize(15);
        return s.is_heap() && s.length() == 15;
    });

    check_fn("resize(5) moves back to stack", [] {
        knst_c16string s;
        s.resize(20, u'x');
        s.resize(5);
        return !s.is_heap() && s.length() == 5;
    });

    check_fn("resize(25, 'x') content correct", [] {
        knst_c16string s;
        s.resize(5, u'a');
        s.resize(25, L'x');
        return s.is_heap() && s.length() == 25 && s == "aaaaaxxxxxxxxxxxxxxxxxxxx";
    });
}

void test_methods_substr() {
    begin_group("Methods – substr");

    check_fn("substr(1,2) of Hello == el", [] {
        knst_c16string s(u"Hello");
        return s.substr(1, 2) == "el";
    });

    check_fn("substr(0,5) == whole string", [] {
        knst_c16string s(u"Hello");
        return s.substr(0, 5) == "Hello";
    });

    check_fn("substr(6,5) of Hello World == World", [] {
        knst_c16string s(u"Hello World");
        return s.substr(6, 5) == "World";
    });
}

void test_methods_find_contains() {
    begin_group("Methods – find & contains");

    check_fn("find char16_t* existing", [] {
        knst_c16string s(u"Hello World");
        return (bool)s.find(u"World");
    });

    check_fn("find char* existing", [] {
        knst_c16string s(u"Hello World");
        return (bool)s.find("Hello");
    });

    check_fn("find char16_t* not found", [] {
        knst_c16string s(u"Hello World");
        return !s.find(u"Mars");
    });

    check_fn("find char16_t single char", [] {
        knst_c16string s(u"Hello World");
        return (bool)s.find(u'o');
    });

    check_fn("contains emoji char16_t", [] {
        knst_c16string s(u"Hello 😀 World 🎉");
        return s.contains(u"😀");
    });

    check_fn("contains returns false for missing", [] {
        knst_c16string s(u"Hello 😀 World 🎉");
        return !s.contains(u"xyz");
    });

    check_fn("find char32_t*", [] {
        knst_c16string s(U"Hello World");
        return (bool)s.find(U"World");
    });

    check_fn("contains char32_t*", [] {
        knst_c16string s(U"Hello World");
        return s.contains(U"Hello");
    });

    check_fn("find in long string – middle", [] {
        knst_c16string s(u"This is a very long test string for searching purposes");
        return (bool)s.find(u"searching");
    });

    check_fn("find in long string – beginning", [] {
        knst_c16string s(u"This is a very long test string for searching purposes");
        return (bool)s.find(u"This");
    });

    check_fn("find in long string – end", [] {
        knst_c16string s(u"This is a very long test string for searching purposes");
        return (bool)s.find(u"purposes");
    });

    check_fn("emoji only string: contains bomb", [] {
        knst_c16string s(u"👋🌍🎉💣🔥");
        return s.contains(u"💣");
    });

    check_fn("emoji only string: heart not found", [] {
        knst_c16string s(u"👋🌍🎉💣🔥");
        return !s.contains(u"❤");
    });
}

void test_methods_starts_ends() {
    begin_group("Methods – starts_with & ends_with");

    // ends_with string literals
    check_fn("ends_with char*", [] {
        knst_c16string s("Hello World");
        return s.ends_with("World");
    });
    check_fn("ends_with char16_t*", [] {
        knst_c16string s(u"Hello World");
        return s.ends_with(u"World");
    });
    check_fn("ends_with wchar_t*", [] {
        knst_c16string s(u"Hello World");
        return s.ends_with(L"World");
    });
    check_fn("ends_with char32_t*", [] {
        knst_c16string s(u"Hello World");
        return s.ends_with(U"World");
    });

    // starts_with string literals
    check_fn("starts_with char*", [] {
        knst_c16string s("Hello World");
        return s.starts_with("Hello");
    });
    check_fn("starts_with char16_t*", [] {
        knst_c16string s(u"Hello World");
        return s.starts_with(u"Hello");
    });
    check_fn("starts_with wchar_t*", [] {
        knst_c16string s(u"Hello World");
        return s.starts_with(L"Hello");
    });
    check_fn("starts_with char32_t*", [] {
        knst_c16string s(u"Hello World");
        return s.starts_with(U"Hello");
    });

    // single char overloads
    check_fn("starts_with char16_t 'H'", [] {
        knst_c16string s("Hello World");
        return s.starts_with(u'H');
    });
    check_fn("starts_with wchar_t L'H'", [] {
        knst_c16string s("Hello World");
        return s.starts_with(L'H');
    });
    check_fn("ends_with char16_t 'd'", [] {
        knst_c16string s("Hello World");
        return s.ends_with(u'd');
    });
    check_fn("ends_with char 'd'", [] {
        knst_c16string s("Hello World");
        return s.ends_with('d');
    });

    // negative cases
    check_fn("starts_with false case", [] {
        knst_c16string s("Hello World");
        return !s.starts_with("World");
    });
    check_fn("ends_with false case", [] {
        knst_c16string s("Hello World");
        return !s.ends_with("Hello");
    });
}

void test_methods_at_front_back() {
    begin_group("Methods – at, front, back");

    check_fn("at(1) of Hello == 'e'", [] {
        knst_c16string s("Hello");
        return s.at(1) == u'e';
    });
    check_fn("front() == 'H'", [] {
        knst_c16string s("Hello");
        return s.front() == u'H';
    });
    check_fn("back() == 'o'", [] {
        knst_c16string s("Hello");
        return s.back() == u'o';
    });
    check_fn("at() on heap string", [] {
        knst_c16string s("Hello World");
        return s.at(6) == u'W';
    });
    check_fn("front() on heap string", [] {
        knst_c16string s("Hello World");
        return s.front() == u'H';
    });
    check_fn("back() on heap string", [] {
        knst_c16string s("Hello World");
        return s.back() == u'd';
    });
}

void test_methods_shrink_to_fit() {
    begin_group("Methods – shrink_to_fit");

    // 1. Heap string: shrink reduces capacity to length+1 (or SSO if fits)
    check_fn("shrink_to_fit reduces capacity to length+1", [] {
        knst_c16string s;
        s.reserve(1000);        // capacity = 1001
        s = u"Hello";           // length = 5, capacity = 1001
        s.shrink_to_fit();
      
        return s.capacity() == 11 && s.length() == 5;
    });

    // 2. Heap to stack transition (fits SSO)
    check_fn("shrink_to_fit moves heap string to stack when fits SSO", [] {
        knst_c16string s(u"This is a long string that will be shortened");
        s.resize(5);  // "This " -> 5 characters
        s.shrink_to_fit();
        return !s.is_heap() && s.length() == 5;
    });

    // 3. Already on stack, nothing changes
    check_fn("shrink_to_fit does nothing on stack string", [] {
        knst_c16string s(u"Hello");
        uint32_t old_cap = s.capacity();  // 11 (SSO)
        s.shrink_to_fit();
        return s.capacity() == old_cap && !s.is_heap();
    });

    // 4. Already optimal capacity, nothing changes
    check_fn("shrink_to_fit does nothing when capacity == length+1", [] {
        knst_c16string s(u"This is a long string on the heap");
        uint32_t old_cap = s.capacity();
        s.shrink_to_fit();
        // If already optimized, capacity should not change
        return s.capacity() == old_cap || s.capacity() == s.length() + 1;
    });

    // 5. Empty heap string moves to stack
    check_fn("shrink_to_fit on empty heap string moves to stack", [] {
        knst_c16string s;
        s.reserve(100);
        // s is still empty, but on heap
        s.shrink_to_fit();
        return !s.is_heap() && s.empty();
    });

    // 6. Content preserved after shrink
    check_fn("shrink_to_fit preserves content", [] {
        knst_c16string s(u"The content of this string must remain unchanged after shrink");
        std::u16string original = std::u16string(s.data(), s.length());
        s.shrink_to_fit();
        std::u16string after = std::u16string(s.data(), s.length());
        return original == after && s.length() == original.length();
    });

    // 7. COW with shrink: if not unique, detach first
    #ifndef KNST_C16STRING_DEACTIVE_COW
    check_fn("shrink_to_fit on shared COW string detaches first", [] {
        knst_c16string a(u"This long string will be shared via COW");
        knst_c16string b = a;  // a and b share the same heap
        void* old_ptr = (void*)a.data();
        b.shrink_to_fit();  // b should detach
        return (void*)a.data() != (void*)b.data() && a == b;
    });
    #endif

    // 8. Large -> small -> shrink_to_fit moves to stack
    check_fn("Large -> small -> shrink_to_fit moves to stack", [] {
        knst_c16string s(u"This long string will be shortened to fit on stack");
        s.resize(3);  // "Thi"
        s.shrink_to_fit();
        return !s.is_heap() && s.length() == 3 && s == "Thi";
    });

    // 9. shrink_to_fit sets exact capacity (or SSO if fits)
    check_fn("shrink_to_fit sets capacity to exact length+1", [] {
        knst_c16string s(u"Hello World");
        s.shrink_to_fit();
        // 11 + 1 = 12 > SSO_LIMIT (11) → stays on heap, capacity = 12
        return s.capacity() == s.length() + 1 && s.is_heap();
    });

    // 10. Multiple shrink_to_fit calls are harmless
    check_fn("Multiple shrink_to_fit calls are harmless", [] {
        knst_c16string s(u"This string will be shrunk multiple times");
        s.shrink_to_fit();
        uint32_t cap1 = s.capacity();
        s.shrink_to_fit();
        uint32_t cap2 = s.capacity();
        return cap1 == cap2;
    });
}

void test_methods_clear() {
    begin_group("Methods – clear");

    // 1. Clear stack string
    check_fn("clear() on stack string makes it empty", [] {
        knst_c16string s(u"Hello");
        s.clear();
        return s.empty() && s.length() == 0 && !s.is_heap();
    });

    // 2. Clear heap string
    check_fn("clear() on heap string makes it empty and moves to stack", [] {
        knst_c16string s(u"This is a long string on the heap");
        s.clear();
        return s.empty() && s.length() == 0 && !s.is_heap();
    });

    // 3. Clear empty string (no-op)
    check_fn("clear() on empty string does nothing", [] {
        knst_c16string s;
        s.clear();
        return s.empty() && s.length() == 0 && !s.is_heap();
    });

    // 4. Clear then reuse
    check_fn("clear() then reuse string works", [] {
        knst_c16string s(u"Hello World");
        s.clear();
        s = u"New String";
        return s == "New String" && s.length() == 10;
    });

    // 5. Clear heap string frees memory
    check_fn("clear() on heap string frees heap memory", [] {
        knst_c16string s(u"This is a long string on the heap");
        void* old_ptr = (void*)s.data();
        s.clear();
        // After clear, string should be on stack (SSO)
        return !s.is_heap() && s.empty();
    });

    // 6. Multiple clear calls are harmless
    check_fn("Multiple clear() calls are harmless", [] {
        knst_c16string s(u"Hello");
        s.clear();
        s.clear();
        s.clear();
        return s.empty() && s.length() == 0;
    });

    // 7. clear() after reserve
    check_fn("clear() after reserve keeps capacity", [] {
        knst_c16string s;
        s.reserve(100);
        s = u"Hello";
        s.clear();
        // After clear, should be on stack (SSO) with capacity 11
        return !s.is_heap() && s.capacity() == 11 && s.empty();
    });
}


void test_console_output() {
    begin_group("Console Output – std::cout & std::wcout");

    // 1. std::cout - stack string
    check_fn("std::cout stack string", [] {
        knst_c16string s(u"Hello");
        std::cout << "  cout stack: " << s << std::endl;
        return true;  // no crash = pass
    });

    // 2. std::cout - heap string
    check_fn("std::cout heap string", [] {
        knst_c16string s(u"Hello World from KNST String!");
        std::cout << "  cout heap: " << s << std::endl;
        return true;
    });

    // 3. std::cout - emoji
    check_fn("std::cout emoji string", [] {
        knst_c16string s(u"Hello 😀 World 🎉💣🔥");
        std::cout << "  cout emoji: " << s << std::endl;
        return true;
    });

    // 4. std::cout - empty
    check_fn("std::cout empty string", [] {
        knst_c16string s(u"");
        std::cout << "  cout empty: '" << s << "'" << std::endl;
        return true;
    });

    // 5. std::cout - Turkish characters
    check_fn("std::cout Turkish chars", [] {
        knst_c16string s(u"Turkish chars: ğüşıöçĞÜŞİÖÇ");
        std::cout << "  cout TR: " << s << std::endl;
        return true;
    });

    // 6. std::wcout - stack string
    check_fn("std::wcout stack string", [] {
        knst_c16string s(u"Hello");
        std::wcout << L"  wcout stack: " << s << std::endl;
        return true;
    });

    // 7. std::wcout - heap string
    check_fn("std::wcout heap string", [] {
        knst_c16string s(u"Hello World from KNST String!");
        std::wcout << L"  wcout heap: " << s << std::endl;
        return true;
    });

    // 8. std::wcout - emoji
    check_fn("std::wcout emoji string", [] {
        knst_c16string s(u"Hello 😀 World 🎉");
        std::wcout << L"  wcout emoji: " << s << std::endl;
        return true;
    });

    // 9. std::wcout - empty
    check_fn("std::wcout empty string", [] {
        knst_c16string s(u"");
        std::wcout << L"  wcout empty: '" << s << L"'" << std::endl;
        return true;
    });

    // 10. std::cout - large string (heap + large)
    check_fn("std::cout large string", [] {
        knst_c16string s(u"This is a very long string that will definitely exceed the SSO buffer limit and test the heap allocation path for console output!");
        std::cout << "  cout large: " << s << std::endl;
        return true;
    });

    // 11. std::wcout - large string
    check_fn("std::wcout large string", [] {
        knst_c16string s(u"Another very long string for wcout testing that exceeds the SSO buffer limit and verifies the heap allocation path works correctly!");
        std::wcout << L"  wcout large: " << s << std::endl;
        return true;
    });

    // 12. std::cout - pool allocator string
    check_fn("std::cout pool allocator string", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"Pool allocated string output!";
        std::cout << "  cout pool: " << str << std::endl;
        return true;
    });

    // 13. std::wcout - pool allocator string
    check_fn("std::wcout pool allocator string", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"Pool alloc wcout test!";
        std::wcout << L"  wcout pool: " << str << std::endl;
        return true;
    });

    // 14. Stress test - multiple outputs
    check_fn("std::cout stress test", [] {
        for (int i = 0; i < 10; ++i) {
            knst_c16string s(u"Stress test line ");
        }
        std::cout << "  cout stress: OK (10 iterations)" << std::endl;
        return true;
    });

    // 15. std::cout - numeric values
    check_fn("std::cout numeric values", [] {
        knst_c16string s1(123456789);
        knst_c16string s2(-987654321);
        knst_c16string s3(3.14159);
        std::cout << "  cout numeric: " << s1 << " | " << s2 << " | " << s3 << std::endl;
        return true;
    });

    // 16. std::wcout - numeric values
    check_fn("std::wcout numeric values", [] {
        knst_c16string s1(42);
        knst_c16string s2(-100);
        knst_c16string s3(2.71828);
        std::wcout << L"  wcout numeric: " << s1 << L" | " << s2 << L" | " << s3 << std::endl;
        return true;
    });

    // 17. std::cout - mixed content
    check_fn("std::cout mixed content", [] {
        knst_c16string s(u"Mixed: 12345 + Türkçe + 😀🎉 + English");
        std::cout << "  cout mixed: " << s << std::endl;
        return true;
    });

    // 18. std::wcout - mixed content
    check_fn("std::wcout mixed content", [] {
        knst_c16string s(u"WMixed: 67890 + ğüşı + 💣🔥 + Test");
        std::wcout << L"  wcout mixed: " << s << std::endl;
        return true;
    });
}

void test_iterator() {
    begin_group("Iterator – begin, end");

    check_fn("forward iteration produces correct chars", [] {
        knst_c16string s(u"Hello");
        std::string result;
        for (auto it = s.begin(); it != s.end(); ++it)
            result += (char)*it;
        return result == "Hello";
    });

    check_fn("write through begin(): changes first char", [] {
        knst_c16string s(u"Hello");
        *s.begin() = u'h';
        return s == u"hello";
    });

    check_fn("std::sort via iterators: cba -> abc", [] {
        knst_c16string s(u"cba");
        std::sort(s.begin(), s.end());
        return s == u"abc";
    });

    check_fn("const begin() dereference == 'H'", [] {
        const knst_c16string s(u"Hello");
        return *s.begin() == u'H';
    });

    check_fn("range-based for loop", [] {
        knst_c16string s(u"abc");
        std::u16string out;
        for (char16_t c : s) out += c;
        return out == u"abc";
    });
}

// ============================================================
//  main
// ============================================================
int main() {
    knst_init_console();

#ifndef KNST_C16STRING_DEACTIVE_COW
    test_cow();
#endif

    test_constructors_char16();
    test_constructors_char();
    test_constructors_wchar();
    test_constructors_char32();
    test_constructors_copy_move();
    test_constructors_numeric();
    test_constructors_std_types();

    test_operators_assignment();
    test_operators_comparison();
    test_operators_index();
    test_operators_concat();

    test_methods_basic();
    test_methods_append();
    test_methods_reserve();
    test_methods_resize();
    test_methods_substr();
    test_methods_find_contains();
    test_methods_starts_ends();
    test_methods_at_front_back();
    test_methods_shrink_to_fit();
    test_methods_clear();
   
    test_console_output();
    test_iterator();

    print_summary();


    return 0;



    
}