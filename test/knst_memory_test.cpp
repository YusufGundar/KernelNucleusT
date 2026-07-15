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

void test_pool_allocator() {
    begin_group("Pool Allocator");

    check_fn("Custom pool string creation", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048); 
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"Hello World from Pool!";
        return str == u"Hello World from Pool!" && str.length() == 22;
    });

    check_fn("Pool string creatiaon 2", [] {
        knst_pool_allocator pool(
            knst_pool_config{64, 100},     // 64 byte, 100 block
            knst_pool_config{256, 50},     // 256 byte, 50 block
            knst_pool_config{1024, 25},     // 1024 byte, 25 block
            knst_pool_config{2048, 50}     // 1024 byte, 25 block
        );
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"Hello World from Pool!";
        return str == u"Hello World from Pool!" && str.length() == 22 && pool.pool_count() == 4;
    });


    check_fn("Default pool string creation", [] {
        knst_pool_allocator pool;
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"Default pool test";
        return str == u"Default pool test" && str.length() == 17;
    });

   
    check_fn("Append with pool allocator", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"Hello";
        str.append(u" World");
        str.append(u" from pool!");
        return str == u"Hello World from pool!";
    });

   
    check_fn("Large string from pool (heap)", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"This is a very long string that will definitely exceed the SSO buffer limit and be allocated from the memory pool.";
        return str.length() > 50 && str.is_heap();
    });

   
    check_fn("Resize with pool allocator", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"Hello World";
        str.resize(5);
        return str == u"Hello" && str.length() == 5;
    });

   
    check_fn("Clear with pool allocator", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"Hello World";
        str.clear();
        return str.empty() && str.length() == 0 && !str.is_heap();
    });

   
    check_fn("Multiple strings sharing same pool", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> s1(pool);
        knst_c16string_sm<knst_pool_allocator> s2(pool);
        knst_c16string_sm<knst_pool_allocator> s3(pool);
        
        s1 = u"First";
        s2 = u"Second";
        s3 = u"Third";
        
        return s1 == u"First" && s2 == u"Second" && s3 == u"Third";
    });

   
    check_fn("Copy with pool allocator", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> s1(pool);
        s1 = u"Hello";
        knst_c16string_sm<knst_pool_allocator> s2 = s1;
        return s1 == s2 && s1.length() == s2.length();
    });

    
    check_fn("Move with pool allocator", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> s1(pool);
        s1 = u"Hello";
        knst_c16string_sm<knst_pool_allocator> s2 = std::move(s1);
        return s2 == u"Hello" && s1.empty();
    });

   
    check_fn("Reserve with pool allocator", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"Hello";
        str.reserve(1000);
        return str.capacity() >= 1000 && str.is_heap();
    });

  
    check_fn("Shrink to fit with pool allocator", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str.reserve(1000);
        str = u"Hello";
        str.shrink_to_fit();
        return str == u"Hello" && str.length() == 5;
    });

   
    check_fn("Contains with pool allocator", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"Hello World from pool!";
        return str.contains(u"World") && str.contains(u"pool") && !str.contains(u"xyz");
    });

    
    check_fn("Starts/Ends with pool allocator", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"Hello World from pool!";
        return str.starts_with(u"Hello") && str.ends_with(u'!');
    });

  
    check_fn("Front/Back with pool allocator", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"Hello";
        return str.front() == u'H' && str.back() == u'o';
    });

  
    check_fn("Concat with pool allocator", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> s1(pool);
        knst_c16string_sm<knst_pool_allocator> s2(pool);
        s1 = u"Hello";
        s2 = u" World";
        knst_c16string_sm<knst_pool_allocator> s3 = s1 + s2;
        return s3 == u"Hello World";
    });

   
    check_fn("Pool info", [] {
        knst_pool_allocator pool(32, 64, 128, 256, 512);
        return pool.pool_count() > 0 && pool.max_block_size() > 0;
    });

   
    check_fn("Pool reset", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"Before reset";
        pool.reset();
        knst_c16string_sm<knst_pool_allocator> str2(pool);
        str2 = u"After reset";
        return str2 == u"After reset";
    });

   
    check_fn("Reuse after clear with pool", [] {
        knst_pool_allocator pool(64, 256, 1024, 2048);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"Initial string";  
        str.clear();              
        str = u"Reused string";  
        return str == u"Reused string" && str.is_heap(); 
    });

    // ═══════════════════════════════════════════════════════
    //  BRIDGE MEMORY & INFO TESTS                            
    // ═══════════════════════════════════════════════════════

    // 19. bridge_memory (copy)
    check_fn("bridge_memory (copy) same type", [] {
        knst_pool_allocator pool1(64, 256, 1024);
        knst_pool_allocator pool2(128, 512, 2048);
        
        knst_c16string_sm<knst_pool_allocator> str(pool1);
        str = u"Test";
        
        bool result = str.bridge_memory(pool2);
        return result && str.pool_count() == pool2.pool_count();
    });

    // 20. bridge_memory (move)
    check_fn("bridge_memory (move) same type", [] {
        knst_pool_allocator pool1(64, 256, 1024);
        knst_pool_allocator pool2(128, 512, 2048);
        size_t expected = pool2.pool_count();
        
        knst_c16string_sm<knst_pool_allocator> str(pool1);
        str = u"Test";
        
        bool result = str.bridge_memory(std::move(pool2));
        return result && str.pool_count() == expected;
    });

   
    check_fn("bridge_memory different type returns false", [] {
        knst_pool_allocator pool(64, 256, 1024);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        str = u"Test";
        
        return !str.bridge_memory(knst_default_allocator());
    });

    // 22. pool_count via string
    check_fn("pool_count via string (default)", [] {
        knst_pool_allocator pool;
        knst_c16string_sm<knst_pool_allocator> str(pool);
        return str.pool_count() == 4;  // 64, 256, 1024, 2048
    });

    // 23. pool_count via string (custom)
    check_fn("pool_count via string (custom)", [] {
        knst_pool_allocator pool(32, 64, 128, 256, 512);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        return str.pool_count() == 5;
    });

    // 24. max_block_size via string
    check_fn("max_block_size via string", [] {
        knst_pool_allocator pool(32, 128, 512);
        knst_c16string_sm<knst_pool_allocator> str(pool);
        return str.max_block_size() == 512;
    });

  
    check_fn("String works after bridge_memory", [] {
        knst_pool_allocator pool1(64, 256, 1024);
        knst_pool_allocator pool2(128, 512, 2048);
        
        knst_c16string_sm<knst_pool_allocator> str(pool1);
        str = u"Hello";
        str.bridge_memory(pool2);
        str = u"Changed!";
        
        return str == u"Changed!" && str.length() == 8;
    });

 
    check_fn("bridge_memory after copy", [] {
        knst_pool_allocator pool1(64, 256, 1024);
        knst_pool_allocator pool2(128, 512, 2048);
        
        knst_c16string_sm<knst_pool_allocator> str1(pool1);
        str1 = u"Original";
        
        knst_c16string_sm<knst_pool_allocator> str2 = str1;
        str2.bridge_memory(pool2);
        
        return str1 == u"Original" && str2 == u"Original";
    });
}


// ============================================================
//  main
// ============================================================
int main() {
    knst_init_console();

    test_pool_allocator();

    print_summary();

    return 0;



    
}