# knst_c16string — Usage Guide (English)

A `char16_t`-based, high-performance string class.
By default it keeps up to 10 characters on the **stack** (SSO), switching to the **heap** beyond that.
**COW (Copy-On-Write)** is enabled by default and can be disabled via a macro.

> Requirement: **C++17** or newer.


---

## Constructors

```cpp
knst_c16string s1;                          // Creates an empty string
knst_c16string s2 = u"Hello World";         // From a char16_t (Unicode) string
knst_c16string s3 = "Hello World";          // From a char (UTF-8) string
knst_c16string s4 = L"Hello World";         // From a wchar_t string
knst_c16string s5 = U"Hello World";         // From a char32_t (UTF-32) string
knst_c16string s6(u"Hello", 3);             // Takes a fixed-length prefix -> "Hel"
knst_c16string s7(10, u'A');                // Creates a repeated character -> "AAAAAAAAAA"
knst_c16string s8(3, u"Test");              // Creates a repeated substring -> "TestTestTest"
knst_c16string s9(12345);                   // From a numeric value -> "12345"
knst_c16string s10(-42);                    // From a negative numeric value -> "-42"
knst_c16string s11(3.14);                   // From a floating-point value -> "3.14"
knst_c16string s12(std::string("Test"));    // Converts from std::string
knst_c16string s13(std::u16string(u"X"));   // Converts from std::u16string
knstc16string s14(std::u32string(U"X"));   // Converts from std::u32string
knst_c16string s15(std::wstring(L"X"));     // Converts from std::wstring
knst_c16string s16(std::string_view("X"));  // Converts from string_view
knst_c16string s17{u'A', u'B', u'C'};       // From an initializer_list -> "ABC"
```
*Note:* In the `count + str` constructor (like `s8`), repetition is done internally via "exponential doubling" — only `O(log n)` `memcpy` calls are needed.

## Copy & Move

```cpp
knst_c16string original = u"This is a long string";  // Original string
knst_c16string copy = original;              // Copy (data is shared, not duplicated, when COW is enabled)
knst_c16string moved = std::move(original);  // Moved
// original is now empty (stack mode, length() == 0)
```

## Assignment Operators

```cpp
knst_c16string s;
s = u"char16_t string";       // Assigns a char16_t literal
s = "char string";            // Assigns a char (UTF-8) literal
s = L"wchar_t string";        // Assigns a wchar_t literal
s = U"char32_t string";       // Assigns a char32_t literal
s = another_string;           // Copies from another knst_c16string
s = std::move(other);         // Move assignment
```
*Note:* If the target is already in heap mode and has enough capacity, the new content is written **in place** without reallocating — a performance optimization.

## String Info

```cpp
knst_c16string s = u"Hello World";
s.length();          // Returns length -> 11
s.capacity();        // Returns allocated capacity (SSO or heap based)
s.empty();           // Checks emptiness -> false
s.is_heap();         // Is it currently in heap mode -> false (stack)
s.data();            // Returns a const char16_t* pointer
s.pool_count();      // Returns the pool count of the current allocator
s.max_block_size();  // Returns the largest block size the allocator has
```
*Note:* `pool_count()` / `max_block_size()` always return `0` for `knst_default_allocator`; they're only meaningful with `knst_pool_allocator`.

## Appending (Append / +=)

```cpp
knst_c16string s = u"Hello";
s.append(u" World");           // Appends a char16_t string
s.append(" how are you");      // Appends a char (UTF-8) string
s.append(L" I'm fine");        // Appends a wchar_t string
s.append(U" there");           // Appends a char32_t string
s.append(another_string);      // Appends another knst_c16string
s.append(123456);      // It supports types such as int, unsigned int, long, long long, unsigned long, float, and double
s += u" test";                  // Appends via the += operator
s += "!";                      // Appends a char literal
```
*Note:* `append` grows **in place** if capacity allows; otherwise it doubles capacity (or grows to the required minimum) and reallocates.

## Find & Contains

```cpp
knst_c16string s = u"Hello World, how are you?";
s.find(u"World");              // Searches for a substring -> true
s.find("Hello");               // Search with char* (UTF-8) -> true
s.find(u"World", 10);          // Search starting from a given offset
s.find(u'o');                  // Search for a single character -> true
s.find(u"Mars");               // Not found -> false
s.contains(u"how");            // -> true
s.contains(u"xyz");            // -> false
s.contains('e');               // -> true
```
*Note:* Short patterns (`<= 8` chars) use naive search; longer ones use the **Two-Way string matching algorithm** — both are chosen automatically inside `find()`.

## Starts/Ends With

```cpp
knst_c16string s = u"Hello World";
s.starts_with(u"Hel");         // -> true
s.starts_with("Hey");          // -> false
s.starts_with(u'H');           // -> true
s.ends_with(u"rld");           // -> true
s.ends_with(u'd');             // -> true
```

## Character Access (At, Front, Back, `[]`)

```cpp
knst_c16string s = u"Hello";

// Bounds-checked access
s.at(0);          // 'H'
// s.at(10);      // triggers KNST_ASSERT (out of range)

// Unchecked access
s[0];             // 'H'
s[0] = u'h';      // Write -> "hello" (triggers detach() when COW is enabled)
s.front();        // 'H'
s.back();         // 'o'

// Mutating via reference
s.front() = u'X'; // -> "Xello"
s.back() = u'Y';  // -> "XellY"

// Const access (read-only)
const knst_c16string& cs = s;
cs[0];
cs.at(1);
```

## Substring (Substr)

```cpp
knst_c16string s = u"Hello World";
auto part1 = s.substr(0, 5);            // "Hello"
auto part2 = s.substr(6, 5);            // "World"
auto part3 = s.substr(6, s.length());   // "World" (to the end)
auto part4 = s.substr(20, 5);           // pos beyond length() -> empty string
```
*Note:* `substr` always requires **both** parameters (`pos` and `count`); there's no single-argument overload. To take everything to the end, pass `s.length()` (or a larger value) as `count` — the function internally clamps via `std::min(count, length()-pos)`.

## Reserve & Resize

```cpp
knst_c16string s = u"Hello";

// Reserve — pre-allocates capacity
s.reserve(1000);           // capacity = 1001, switches to heap

// Resize with a fill character
s.resize(20, u'X');        // "Hello" + 15 'X' characters
s.resize(20, 'X');         // same, with char
s.resize(20, L'X');        // same, with wchar_t
s.resize(20, U'X');        // same, with char32_t

// Resize — shrinking
s.resize(5);               // "Hello" (truncates)
s.resize(3, u'A');         // "Hel" (fill char unused when shrinking)

// Resize — default fill character (u'\0')
s.resize(10);               // "Hello" + 5 x u'\0'
```

## Shrink To Fit

```cpp
knst_c16string s;

s.reserve(1000);           // capacity = 1001
s = u"Hello";               // length = 5
s.shrink_to_fit();          // capacity shrinks to SSO limit (may switch heap->stack)

s = u"This is a very long string stored on heap";
s.reserve(10000);           // capacity = 10001
s.shrink_to_fit();          // capacity = length + 1
```

## Clear

```cpp
knst_c16string s = u"Hello World";
s.clear();                 // length = 0, returns to stack mode
s.empty();                 // true
```

## Concatenation (`+` Operator)

```cpp
knst_c16string s1 = u"Hello";
knst_c16string s2 = u" World";

// String + String
auto s3 = s1 + s2;              // "Hello World"

// String + Literal
auto s4 = s1 + u" Mars";        // "Hello Mars"
auto s5 = "Venus" + s1;         // "VenusHello"
auto s6 = s1 + L" Jupiter";     // "Hello Jupiter"
auto s7 = U"Saturn" + s1;       // "SaturnHello"

// String + Character
auto s8 = s1 + u'!';            // "Hello!"
auto s9 = u'!' + s1;            // "!Hello"

// Chained appending
s1 += u" Planet";               // "Hello Planet"
s1 += " Earth";                 // "Hello Planet Earth"
```

## Comparison Operators

```cpp
knst_c16string s1 = u"abc";
knst_c16string s2 = u"def";

s1 == s2;          // false
s1 != s2;          // true
s1 < s2;           // true  ("abc" < "def")
s1 <= s2;          // true
s1 > s2;           // false
s1 >= s2;          // false

// Comparisons against literals (work both ways)
s1 == u"abc";      // true
s1 == "abc";       // true
"abc" == s1;       // true
s1 < "def";        // true
"abc" < s2;        // true
```
*Note:* All comparisons compare length first, then content via `memcmp`; comparisons against `char*`/`wchar_t*`/`char32_t*` first convert the other side to UTF-16.

## Iterators

```cpp
knst_c16string s = u"Hello";

// Mutable iterator
for (auto it = s.begin(); it != s.end(); ++it) {
    char16_t c = *it;
    *it = u'a';               // Modifies the character (triggers detach() when COW is enabled)
}

// Const iterator (read-only)
for (auto it = s.cbegin(); it != s.cend(); ++it) {
    char16_t c = *it;
}

// Range-based for
for (char16_t c : s) { /* ... */ }
for (char16_t& c : s) { c = u'X'; }   // Mutates, triggers detach()

// STL algorithms
knst_c16string s2 = u"cba";
std::sort(s2.begin(), s2.end());       // "abc"
std::reverse(s2.begin(), s2.end());    // "cba"
auto it = std::find(s.begin(), s.end(), u'e');
```
*Note:* The `iterator`/`const_iterator` types (`knst_iterator<char16_t>`, `knst_const_iterator<char16_t>`) are **not defined** in this header — they must come from another header in the project. They're assumed to satisfy random-access iterator requirements for algorithms like `std::sort`; check that header to confirm.

## Console Output

```cpp
knst_c16string s = u"Hello World";

std::cout << s << std::endl;    // Converts UTF-16 -> UTF-8 before writing
std::wcout << s << std::endl;   // Direct on Windows, converted on Linux

// Manual UTF-8 conversion
size_t utf8_size = knst_get_utf16_to_utf8_exact_byte_size(s.data(), s.length());
char* utf8_buffer = (char*)malloc(utf8_size + 1);
knst_convert_utf16_to_utf8(s.data(), s.length(), utf8_buffer);
utf8_buffer[utf8_size] = '\0';
printf("%s", utf8_buffer);
free(utf8_buffer);
```

## COW (Copy-On-Write)

```cpp
// COW is enabled by default
knst_c16string a = u"This is a long string stored on heap";
knst_c16string b = a;           // Data is not copied, only shared (ref_count++)

a.data() == b.data();          // true (same pointer)

b.append(u" modified");        // Write -> detach() is triggered automatically
a.data() != b.data();          // true (now separate)

// Explicit (manual) detach
knst_c16string c = a;
c.detach();                    // true (was shared, now detached)
c.detach();                    // false (already detached, nothing to do)
```

```cpp
// To disable COW at compile time:
// #define KNST_C16STRING_DEACTIVE_COW
// With this defined, every copy becomes a deep copy; there is no sharing.
```

## Using the Pool Allocator

```cpp
// Creates a pool allocator with custom block sizes
knst_pool_allocator pool(64, 256, 1024, 2048);

// Creates a string that uses the pool allocator
knst_c16string_sm<knst_pool_allocator> s(pool);
s = u"String using pool memory";

s.pool_count();                // 4 (four distinct pools)
s.max_block_size();            // 2048

// Switching the allocator (must be the same Allocator type)
knst_pool_allocator pool2(128, 512, 4096);
s.bridge_memory(pool2);        // Moves data to the new pool

// Default pool allocator (default sizes: 64, 256, 1024, 2048)
knst_c16string_sm<> default_pool;

// Custom allocator via constructor
knst_c16string_sm<knst_pool_allocator> s2(knst_pool_allocator(32, 128, 512));
s2 = u"Custom pool string";

// Resets the pool with new sizes
pool.reset(16, 64, 256, 1024);
```

## Compile-Time Macros

| Macro | Effect |
|---|---|
| `KNST_C16STRING_DEACTIVE_COW` | Disables COW; every copy becomes a deep copy. |
| `KNST_C16_STRING_USING_ATOMIC_COW` | Makes the COW ref-count `std::atomic<int>` (thread-safe). |
| `KNST_C16STRING_ALIGN_64` | 64-byte alignment, SSO capacity rises to 30 characters. |
| `KNST_C16STRING_ALIGN_32` | 32-byte alignment, SSO capacity rises to 14 characters. |
| (none) | Default: 8-byte alignment, 10-character SSO capacity. |
| `KNST_MEMORY_POOL_USE_MUTEX` | Guards `knst_pool_allocator` with a mutex, making it thread-safe. |

