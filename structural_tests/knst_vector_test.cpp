#include <iostream>
#include <string>
#include "../include/KernelNucleusT.hpp"


static int g_passed = 0, g_failed = 0;

void check(bool condition, const char* test_name) {
    if (condition) {
        std::cout << "  [PASS] " << test_name << "\n";
        g_passed++;
    } else {
        std::cout << "  [FAIL] " << test_name << "\n";
        g_failed++;
    }
}

void print_results() {
    std::cout << "\n==========================================\n";
    std::cout << "  RESULTS: " << g_passed << " passed, " 
              << g_failed << " failed (total " << (g_passed + g_failed) << ")\n";
    std::cout << "==========================================\n";
}

// ============================================================
//  Test helper struct with custom constructor
// ============================================================
struct TestObj {
    int x;
    float y;
    
    // Default constructor with default values
    TestObj(int a = 0, float b = 0.0f) : x(a), y(b) {}
    
    // Equality comparison for verification
    bool operator==(const TestObj& o) const { return x == o.x && y == o.y; }
    bool operator!=(const TestObj& o) const { return !(*this == o); }
};

// ============================================================
//  TEST: Basic Operations (push_back, clear, operator[])
// ============================================================
void test_basic() {
    std::cout << "\n--- Basic Operations ---\n";

    // Default constructor: vector should be empty with zero size and capacity
    knst_vector<int> v;
    check(v.empty() && v.size() == 0 && v.capacity() == 0, "Default: empty, size=0, cap=0");

    // push_back single element: size increases, element accessible via operator[]
    v.push_back(10);
    check(v.size() == 1 && v[0] == 10, "push_back: size=1, v[0]=10");
    
    // push_back multiple elements: size grows, all elements accessible
    v.push_back(20);
    v.push_back(30);
    check(v.size() == 3 && v[2] == 30, "push_back: size=3, v[2]=30");

    // operator[] for mutation: modify element at index
    v[1] = 99;
    check(v[1] == 99, "operator[]: v[1]=99");

    // clear(): size becomes zero, empty returns true
    v.clear();
    check(v.empty() && v.size() == 0, "clear: empty, size=0");

    // push_back after clear: vector reusable with new elements
    v.push_back(100);
    check(v.size() == 1 && v[0] == 100, "push_back after clear: works");
}

// ============================================================
//  TEST: Growth Strategy (2x capacity doubling)
// ============================================================
void test_growth() {
    std::cout << "\n--- Growth (2x) ---\n";

    knst_vector<int> v;
    
    // First push_back: initial capacity should be 4 (minimum default)
    v.push_back(1);
    check(v.capacity() == 4, "First push: cap=4");

    // Fill to capacity: size equals capacity, no growth yet
    v.push_back(2);
    v.push_back(3);
    v.push_back(4);
    check(v.capacity() == 4 && v.size() == 4, "Full: cap=4, size=4");

    // Exceed capacity: should trigger 2x growth (4 -> 8)
    v.push_back(5);
    check(v.capacity() == 8, "Growth: cap=8 (2x)");
    check(v.size() == 5 && v[4] == 5, "Size=5, v[4]=5");
}

// ============================================================
//  TEST: Reserve (pre-allocate capacity)
// ============================================================
void test_reserve() {
    std::cout << "\n--- Reserve ---\n";

    knst_vector<int> v;
    
    // reserve(100): capacity becomes 100, size remains 0
    v.reserve(100);
    check(v.capacity() == 100 && v.size() == 0, "reserve(100): cap=100, size=0");

    // push_back after reserve: capacity should not change
    v.push_back(1);
    check(v.capacity() == 100, "Capacity stays 100 after push");

    // reserve with smaller value: should be no-op (never shrinks)
    v.reserve(10);
    check(v.capacity() == 100, "reserve smaller: no-op");
}

// ============================================================
//  TEST: Resize (change size with optional fill value)
// ============================================================
void test_resize() {
    std::cout << "\n--- Resize ---\n";

    knst_vector<int> v;
    v.push_back(1);
    v.push_back(2);

    // resize larger with fill value: new elements initialized to 99
    v.resize(5, 99);
    check(v.size() == 5, "resize(5): size=5");
    check(v[0] == 1 && v[1] == 2 && v[2] == 99 && v[3] == 99 && v[4] == 99,
          "Values: 1,2,99,99,99");

    // resize smaller: truncates, preserves remaining elements
    v.resize(2);
    check(v.size() == 2, "resize(2): size=2");
    check(v[0] == 1 && v[1] == 2, "Values preserved");

    // resize with custom type using default constructor
    knst_vector<TestObj> obj_vec;
    obj_vec.emplace_back(1, 1.5f);
    obj_vec.resize(3);  // New elements default-constructed
    check(obj_vec.size() == 3, "resize with default: size=3");
    check(obj_vec[0] == TestObj(1, 1.5f), "Original preserved");
    check(obj_vec[1] == TestObj(0, 0.0f), "Default constructed");
}

// ============================================================
//  TEST: Shrink to Fit (reduce capacity to match size)
// ============================================================
void test_shrink_to_fit() {
    std::cout << "\n--- Shrink to Fit ---\n";

    knst_vector<int> v;
    v.reserve(100);
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    
    check(v.capacity() == 100 && v.size() == 3, "Before shrink: cap=100, size=3");
    
    // shrink_to_fit: capacity reduced to exact size
    v.shrink_to_fit();
    check(v.capacity() == 3, "After shrink: cap=3");
    check(v.size() == 3, "Size preserved: 3");
    check(v[0] == 1 && v[1] == 2 && v[2] == 3, "Values preserved");

    // shrink_to_fit on empty vector: capacity becomes 0
    v.clear();
    v.shrink_to_fit();
    check(v.capacity() == 0 && v.empty(), "Empty shrink: cap=0");
}

// ============================================================
//  TEST: Emplace Back (in-place construction)
// ============================================================
void test_emplace_back() {
    std::cout << "\n--- Emplace Back ---\n";

    knst_vector<TestObj> v;
    
    // emplace_back: construct element directly in vector memory
    v.emplace_back(10, 5.5f);
    check(v.size() == 1, "emplace_back: size=1");
    check(v[0].x == 10 && v[0].y == 5.5f, "Values correct");

    // Multiple emplace_back calls
    v.emplace_back(20, 10.0f);
    v.emplace_back(30, 15.5f);
    check(v.size() == 3, "Three elements");
    check(v[2].x == 30 && v[2].y == 15.5f, "Third element correct");
}

// ============================================================
//  TEST: Copy & Move Semantics (Rule of 5)
// ============================================================
void test_copy_move() {
    std::cout << "\n--- Copy & Move ---\n";

    // Copy constructor: deep copy of elements
    knst_vector<int> v1;
    v1.push_back(1);
    v1.push_back(2);
    v1.push_back(3);
    
    knst_vector<int> v2 = v1;  // Copy constructor
    check(v2.size() == 3, "Copy ctor: size=3");
    check(v2[0] == 1 && v2[1] == 2 && v2[2] == 3, "Values copied");

    // Copy assignment: deep copy, source unchanged
    knst_vector<int> v3;
    v3 = v1;  // Copy assignment operator
    check(v3.size() == 3, "Copy assign: size=3");
    check(v3[2] == 3, "Value correct");

    // Move constructor: source becomes empty, dest takes ownership
    knst_vector<int> v4 = std::move(v1);  // Move constructor
    check(v1.empty() && v1.size() == 0, "Move ctor: source empty");
    check(v4.size() == 3, "Move ctor: dest size=3");
    check(v4[0] == 1 && v4[2] == 3, "Move ctor: values preserved");

    // Move assignment: source becomes empty, dest takes ownership
    knst_vector<int> v5;
    v5 = std::move(v4);  // Move assignment operator
    check(v4.empty(), "Move assign: source empty");
    check(v5.size() == 3, "Move assign: dest size=3");
}

// ============================================================
//  TEST: Iterators (begin/end, range-for, const iteration)
// ============================================================
void test_iterators() {
    std::cout << "\n--- Iterators ---\n";

    knst_vector<int> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);

    // Forward iteration with explicit iterators
    int sum = 0;
    for (auto it = v.begin(); it != v.end(); ++it) sum += *it;
    check(sum == 60, "Iterator sum: 60");

    // Range-based for loop (uses begin/end internally)
    sum = 0;
    for (auto x : v) sum += x;
    check(sum == 60, "Range-based for sum: 60");

    // Write access via non-const iterator
    *v.begin() = 100;
    check(v[0] == 100, "Write via iterator: v[0]=100");

    // Const iterator on const reference
    const auto& cv = v;
    sum = 0;
    for (auto it = cv.begin(); it != cv.end(); ++it) sum += *it;
    check(sum == 150, "Const iterator sum: 150");
}

// ============================================================
//  TEST: Equality Operators (== and !=)
// ============================================================
void test_equality() {
    std::cout << "\n--- Equality (== / !=) ---\n";

    // Two vectors with same elements: should be equal
    knst_vector<int> v1;
    v1.push_back(1);
    v1.push_back(2);
    
    knst_vector<int> v2;
    v2.push_back(1);
    v2.push_back(2);
    
    // Two vectors with different elements: should not be equal
    knst_vector<int> v3;
    v3.push_back(1);
    v3.push_back(99);

    check(v1 == v2, "v1 == v2");
    check(v1 != v3, "v1 != v3");
    check(!(v1 == v3), "!(v1 == v3)");
    
    // Different sizes: should not be equal
    v2.push_back(3);
    check(v1 != v2, "Different sizes: not equal");
}

// ============================================================
//  TEST: Bridge Memory (allocator migration)
// ============================================================
void test_bridge_memory() {
    std::cout << "\n--- Bridge Memory ---\n";

    // Create two pool allocators with different configurations
    knst_pool_allocator pool1(64, 256, 1024);
    knst_pool_allocator pool2(128, 512, 2048);
    
    // Same-type allocator migration (pool -> pool): should succeed
    knst_vector_sm<int> v1(pool1);
    v1.push_back(1);
    v1.push_back(2);
    
    bool result = v1.bridge_memory(pool2);
    check(result, "bridge_memory (pool->pool): success");
    check(v1.size() == 2, "Size preserved");
    check(v1[0] == 1 && v1[1] == 2, "Values preserved");

    // Move allocator migration: should succeed
    knst_vector_sm<int> v2(pool1);
    v2.push_back(42);
    
    result = v2.bridge_memory(std::move(pool2));
    check(result, "bridge_memory (move): success");
    check(v2[0] == 42, "Value preserved");

    // Same-type default allocator migration: should succeed
    knst_vector<int> v3;
    v3.push_back(99);
    
    knst_default_allocator def;
    result = v3.bridge_memory(def);
    check(result, "bridge_memory (default->default): success");
    
    // Different allocator types: should fail
    knst_vector_sm<int> v4(pool1);
    result = v4.bridge_memory(def);
    check(!result, "Different allocator: FAILS (expected)");
}

// ============================================================
//  MAIN
// ============================================================

int main() {
    test_basic();
    test_growth();
    test_reserve();
    test_resize();
    test_shrink_to_fit();
    test_emplace_back();
    test_copy_move();
    test_iterators();
    test_equality();
    test_bridge_memory();

    print_results();
    return 0;
}