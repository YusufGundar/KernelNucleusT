# knst_c16string — UTF-16 String Class

Hello! 👋 This document explains what the `knst_c16string` class is, how to use it, and why it exists in KernelNucleusT.

In short: **a UTF-16-based, SSO (small string optimization) and COW (copy-on-write) enabled, allocator-aware string class.** It saves your day especially when working with Windows APIs and Unicode text.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **UTF-16-based** | Directly compatible with Windows APIs, easily convertible to UTF-8 on POSIX |
| **SSO** | Small strings (up to 10 characters by default) live on the stack, no heap allocation |
| **COW** | On copy, data is not duplicated — only the reference count is incremented |
| **Allocator-aware** | Pool allocator gives serious speedups for small strings |
| **`[[no_unique_address]]`** | Zero overhead if you use an empty allocator (C++20) |
| **Rich constructor set** | `char16_t*`, `char*` (UTF-8), `wchar_t*`, `char32_t*`, `std::string`, `initializer_list`, numbers, `float`, `double` |
| **Fast `find`** | Naive algorithm for short patterns, Two-Way algorithm for long patterns |
| **STL-compatible** | Iterators, ostream operators, `std::string` conversion |

---

## 🚀 Quick Start

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // Empty string
    knst_c16string s1;

    // From char16_t*
    knst_c16string s2 = u"Hello World";

    // From UTF-8 (POSIX path, JSON, etc.)
    knst_c16string s3 = "File path: /home/knst";

    // From wide char (Windows API)
    knst_c16string s4 = L"Windows style";

    // From UTF-32 (emoji, rare characters)
    knst_c16string s5 = U"Hello 🌍";

    // From number
    knst_c16string s6 = 42;
    knst_c16string s7 = 3.14;

    // Repeated character
    knst_c16string s8(5, u'a');   // "aaaaa"

    // From STL string
    std::string std_str = "hello";
    knst_c16string s9 = std_str;

    // initializer_list
    knst_c16string s10 = {u'H', u'e', u'l', u'l', u'o'};

    std::cout << s2 << "\n";      // "Hello World"
    return 0;
}
```

---

## 🏗️ Constructors

### Raw Strings

```cpp
knst_c16string a;                    // empty
knst_c16string b(u"UTF-16 text");    // char16_t*
knst_c16string c(u"UTF-16", 6);      // with length
knst_c16string d("UTF-8 text");      // converted from UTF-8
knst_c16string e(L"wchar_t text");   // wchar_t*
knst_c16string f(U"UTF-32 text");    // char32_t*
```

### Numbers

```cpp
knst_c16string n1 = 42;
knst_c16string n2 = -123;
knst_c16string n3 = 100L;
knst_c16string n4 = 999999999ULL;
knst_c16string n5 = 3.14f;
knst_c16string n6 = 2.718281828;
```

### Repeated Strings

```cpp
knst_c16string a(5, u'a');      // "aaaaa"
knst_c16string b(3, u"ab");     // "ababab" (exponential replication)
knst_c16string c(4, 'x');       // "xxxx"
knst_c16string d(2, L"mer");    // "mermer"
```

### STL Types

```cpp
std::string       s1 = "hello";
std::wstring      s2 = L"world";
std::u16string    s3 = u"u16";
std::u32string    s4 = U"u32";
std::string_view  sv = "view";
std::vector<char> vec = {'a','b','c'};

knst_c16string k1 = s1;
knst_c16string k2 = s2;
knst_c16string k3 = s3;
knst_c16string k4 = s4;
knst_c16string k5 = sv;
knst_c16string k6 = vec;
```

### Allocator Usage

```cpp
knst_pool_allocator pool;
knst_c16string_sm<> s(pool);   // with pool allocator
```

`knst_c16string_sm<>` = `basic_c16string<knst_pool_allocator>`. Fast for small strings, allocates from the pool.

---

## 📝 Core Methods

### Reading

```cpp
knst_c16string s = u"Hello";

s.data();      // const char16_t* — raw pointer
s.length();    // character count (excluding null)
s.capacity();  // capacity
s.empty();     // true/false
s.is_heap();   // heap or SSO?
```

### Writing / Modifying

```cpp
knst_c16string s;

s.append(u"World");          // char16_t*
s.append("UTF-8 text");      // from UTF-8
s.append(L"wide");           // wchar_t*
s.append(U"UTF-32");         // char32_t*
s.append(42);                // int
s.append(3.14);              // double
s.append(u'!');              // single character

s.clear();                   // clear
s.resize(20);                // resize to 20 characters
s.resize(20, u'x');          // 20 characters, fill the rest with 'x'
s.reserve(100);              // reserve at least 100 characters
s.shrink_to_fit();           // shrink capacity to length+1
```

### Access

```cpp
knst_c16string s = u"Hello";

s[0];         // 'H' (non-const: triggers COW detach)
s.at(0);      // 'H' + bounds check (asserts in debug)
s.front();    // first character
s.back();     // last character

// const versions — no detach
const knst_c16string& cs = s;
cs[0];
cs.front();
```

### Substring

```cpp
knst_c16string s = u"Hello World";

knst_c16string sub   = s.substr(0, 5);    // "Hello"
knst_c16string tail  = s.substr(6, 100);  // "World" (clamped)
knst_c16string empty = s.substr(100);     // empty string
```

---

## 🔍 Searching — `find`, `contains`, `starts_with`, `ends_with`

### `find` / `contains`

Uses a **naive** algorithm for short patterns (≤8 characters) and a **Two-Way** algorithm for long patterns. Both are **O(n)** guaranteed.

```cpp
knst_c16string text = u"Hello World";

text.find(u"Hel");         // true
text.find("World");        // from UTF-8
text.find(L"o");           // wchar_t
text.find(u'o');           // single character
text.find(u"xyz");         // false

// With offset
text.find(u"o", 5);        // search after the 5th character

// contains is an alias for find (for readability)
if (text.contains(u"Hello")) {
    // ...
}
```

### `starts_with` / `ends_with`

```cpp
knst_c16string path = u"/home/user/file.txt";

path.starts_with(u"/home");     // true
path.ends_with(u".txt");        // true
path.ends_with("txt");          // also works with UTF-8
path.starts_with(u'/');         // single character
path.ends_with(L't');           // wchar_t
```

**Performance note:**
- The `char16_t*` version uses `memcmp` → very fast
- Other types are converted to UTF-16 first (stack buffer for short strings)

---

## ⚙️ Operators

### Comparison

All comparison operators exist — both against `basic_c16string` and raw pointer types:

```cpp
knst_c16string a = u"apple";
knst_c16string b = u"banana";

a == b;      // false
a != b;      // true
a < b;       // true (lexicographic)
a <= b;      // true
a > b;       // false
a >= b;      // false

// With raw pointers
a == u"apple";       // true
a < "banana";        // also works with UTF-8
L"cherry" > a;       // wchar_t on the left
```

### Concatenation

```cpp
knst_c16string s = u"Hello";
s += u" World";      // "Hello World"
s += '!';            // single character
s += 42;             // number
s += L" (wchar)";    // wchar_t

knst_c16string greeting = u"Dear " + u"Yusuf";    // + operator
knst_c16string mixed    = u"Result: " + 42;       // with number
knst_c16string fromChar = 'X' + u"marks";         // char on the left
```

### Stream Operators

```cpp
knst_c16string s = u"Hello";

std::cout  << s;    // converts to UTF-8 and writes
std::wcout << s;    // writes to wide stream
```

---

## 🔁 Iterators

Standard STL-compatible iterators:

```cpp
knst_c16string s = u"Hello";

for (auto it = s.begin(); it != s.end(); ++it) {
    char16_t c = *it;
}

// const versions
for (auto it = s.cbegin(); it != s.cend(); ++it) {
    // read-only
}

// Range-based for
for (char16_t c : s) {
    // ...
}

// Modify with a non-const iterator
for (auto& ch : s) {
    ch = static_cast<char16_t>(ch - 32);   // ASCII lowercase → uppercase
}
```

**Note:** Calling non-const `begin()` / `end()` triggers a **COW detach**. This is intentional, since you are assumed to be able to write through the iterator.

### Iterator Classes

There are two iterator classes:

| Class | Purpose |
|---|---|
| **`knst_iterator<T>`** | Mutable (writable) random access iterator |
| **`knst_const_iterator<T>`** | Const (read-only) random access iterator |

Both behave exactly the same as `std::vector<T>::iterator`. They support all random access operators: `*`, `->`, `[]`, `++`, `--`, `+=`, `-=`, `+`, `-`, `==`, `!=`, `<`, `>`, `<=`, `>=`.

**Automatic mutable → const conversion:**

```cpp
knst_iterator<int> it = v.begin();              // mutable
knst_const_iterator<int> cit = it;              // ✅ automatic conversion
knst_const_iterator<int> cit2 = v.cbegin();     // ✅ direct
```

This lets you pass `v.begin()` to functions expecting `const_iterator` (just like with `std::vector`).

---

## 🎛️ COW (Copy-on-Write) Mechanism

### How It Works

```cpp
knst_c16string a = u"a long string here";   // heap allocation

knst_c16string b = a;   // ⚡ Only the reference count went to 2, data was not copied!

b.append(u"!");         // 🔄 Now it detached; b got its own copy
                        //    a still points to the old data

// a: "a long string here"
// b: "a long string here!"
```

### When Is It Useful?

- **Passing parameters** — you can pass by value to functions; copying is free
- **Container usage** — appending to `std::vector<knst_c16string>` is cheap
- **Functional style** — `auto copy = original;` is instantaneous and cheap

### When Should You Disable It?

In multithreaded code, COW is **dangerous** because the reference count can be modified by two threads simultaneously. For that:

```cpp
#define KNST_C16_STRING_USING_ATOMIC_COW
```

This macro makes the reference count `std::atomic`.

**To disable COW completely:**

```cpp
#define KNST_C16STRING_DEACTIVE_COW
```

Every copy becomes a deep copy.

---

## 🧮 SSO (Small String Optimization)

Small strings never hit the heap — they live inside a **union** in the class.

| Setting | SSO Capacity | Alignment |
|---|---|---|
| Default | **10 characters** | `alignas(8)` |
| `KNST_C16STRING_ALIGN_32` | **14 characters** | `alignas(32)` |
| `KNST_C16STRING_ALIGN_64` | **30 characters** | `alignas(64)` |

**Example:** With the default setting, `knst_c16string(u"Hello")` (5 characters) never touches the heap.

### Which Setting Should You Choose?

- **Default (10)** — general purpose, most balanced
- **`_ALIGN_32` (14)** — file paths, short usernames
- **`_ALIGN_64` (30)** — long text, cache-line aligned access

**Note:** The larger the `alignas` value, the larger the class's `sizeof`. 64-byte alignment = each string takes 64 bytes.

---

## 🧵 Thread Safety

By default, `knst_c16string` is **not thread-safe.** Because:

- The COW reference count is not atomic
- SSO data lives inside the class; sharing it would race

**To make it thread-safe:**

```cpp
#define KNST_C16_STRING_USING_ATOMIC_COW
```

This makes the COW counter atomic. But note: **you still cannot write to the same string from two threads simultaneously.** Only copying/reading becomes safe.

---

## 📊 Performance Notes

| Operation | Complexity | Note |
|---|---|---|
| Small string creation (< SSO) | **O(1)** | No heap |
| Copy (COW enabled) | **O(1)** | Only ref count++ |
| Write (COW detach) | **O(n)** | Copies on first write |
| `append` | **Amortized O(1)** | Capacity grows 2x |
| `find` (short pattern) | **O(n)** | Naive |
| `find` (long pattern) | **O(n)** | Two-Way, guaranteed |
| `starts_with` / `ends_with` | **O(m)** | `m` = prefix/suffix length |
| `substr` | **O(m)** | Creates a new string |

### When Is It Slow?

- **Frequent writes with COW enabled** — every write may trigger detach
- **Very long strings** — go to heap; `realloc` is costly
- **`starts_with` with types other than `char16_t`** — encoding conversion is costly

---

## 🌍 Platform Support

| Platform | Behavior |
|---|---|
| **Windows** | `wchar_t` = UTF-16 → `to_wchar_alloc()` does a direct `memcpy`, very fast |
| **Linux / macOS** | `wchar_t` = UTF-32 → requires conversion from UTF-16 |
| **Android** | Like Windows (`wchar_t` is 32-bit, but we use UTF-16 internally) |

### File Path Operations

```cpp
#ifdef KNST_USING_PLATFORM_WINDOWS
    // UTF-16 can be passed directly to Windows APIs
    knst_c16string path = u"C:\\Users\\Yusuf\\file.txt";
    // Windows API call...
#else
    // POSIX: convert to UTF-8
    knst_c16string path = u"/home/knst/file.txt";
    char* utf8_path = path.to_char();   // use it, then free
#endif
```

### UTF-8 Conversion

```cpp
knst_c16string s = u"ünïcödé";

// UTF-16 → UTF-8 (caller must free)
char* u8 = s.to_char();
if (u8) {
    std::cout << u8 << "\n";
    knst_default_allocator().deallocate(u8, std::strlen(u8) + 1);
}

// UTF-16 → wchar_t* (caller must free)
wchar_t* w = s.to_wchar_alloc();
if (w) {
    knst_c16string round_trip(w);
    knst_default_allocator().deallocate(w, (s.length() + 1) * sizeof(wchar_t));
}
```

---

## 🧠 Using with the Pool Allocator (`knst_c16string_sm`)

`knst_pool_allocator` hands out small allocations from **pre-reserved blocks**. If you are creating and destroying many short strings (parser, JSON processing, file path lists), this gives a **serious speed boost**.

### With the Default Pool

```cpp
#include "KernelNucleusT.hpp"

// knst_c16string_sm = basic_c16string<knst_pool_allocator>
knst_c16string_sm<> s = u"short text";

std::cout << "pool_count     : " << s.pool_count() << "\n";       // 4
std::cout << "max_block_size : " << s.max_block_size() << "\n";   // 2048
```

By default there are 4 sizes: **64, 256, 1024, 2048 bytes**.

### With Your Own Pool Sizes

```cpp
// Use only specific sizes
knst_pool_allocator pool(128, 512, 4096);

knst_c16string_sm<> s(pool);
s.append(u"merhaba");

std::cout << "pool_count     : " << s.pool_count() << "\n";       // 3
std::cout << "max_block_size : " << s.max_block_size() << "\n";   // 4096
```

### Size + Block Count Control

```cpp
// Specify block count per pool too
knst_pool_allocator pool(
    knst_pool_config(128, 256),   // 128-byte blocks × 256
    knst_pool_config(1024, 64)    // 1024-byte blocks × 64
);

knst_c16string_sm<> s(pool);
```

### When Should You Use the Pool?

| Situation | Use pool? |
|---|---|
| Many short strings (parser, tokenizer) | ✅ Yes, big win |
| Occasional string creation | ❌ Unnecessary |
| Need thread safety | ✅ But define `KNST_MEMORY_POOL_USE_MUTEX` |
| Long strings (2048+ bytes) | ⚠️ Pool does not apply, falls back to `malloc` |

### Pool and COW Together

```cpp
knst_c16string_sm<> a = u"a long text...";
knst_c16string_sm<> b = a;       // COW: no copy, only ref count++
b.append(u"!");                  // Detach: gets a new block from the pool
```

Pool allocator and COW complement each other very well — COW makes copying cheap, and the pool makes the allocation during detach fast.

---

## 🔤 Global UTF Conversion Functions

The encoding conversions underlying `knst_c16string` are defined in `knst_global_functions.hpp`. You can also call them **directly** — for example, to convert a buffer without creating a string object.

### Conversion Functions

| Function | What it does |
|---|---|
| `knst_convert_utf8_to_utf16(src, src_len, dst)` | UTF-8 → UTF-16 |
| `knst_convert_utf16_to_utf8(src, src_len, dst)` | UTF-16 → UTF-8 |
| `knst_convert_wchar_to_utf16(src, src_len, dst)` | wchar_t → UTF-16 (direct on Windows, via UTF-32 on POSIX) |
| `knst_convert_utf16_to_wchar(src, src_len, dst)` | UTF-16 → wchar_t |
| `knst_convert_char32_to_utf16(src, src_len, dst)` | UTF-32 → UTF-16 (including surrogate pairs) |

### Length Calculation Functions

To learn **how many characters / bytes** you need before conversion:

| Function | What it returns |
|---|---|
| `knst_get_utf8_to_utf16_exact_length(str, byte_count)` | UTF-16 character count |
| `knst_get_utf16_to_utf8_exact_byte_size(str, src_len)` | UTF-8 byte count |
| `knst_get_wchar_to_utf16_exact_length(str, count)` | UTF-16 character count |
| `knst_get_char32_to_utf16_exact_length(str, count)` | UTF-16 character count |

### C String Length

```cpp
knst_get_str_length(u"hello");   // char16_t* → 5
knst_get_str_length("hello");    // char* (UTF-8) → 5
knst_get_str_length(L"hello");   // wchar_t* → 5
knst_get_str_length(U"hello");   // char32_t* → 5
```

### Example: Manual Conversion

```cpp
#include "KernelNucleusT.hpp"

const char* utf8 = "Hello World 🌍";
uint32_t utf8_bytes = std::strlen(utf8);

// 1. Compute how many characters are needed for UTF-16
uint32_t utf16_len = knst_get_utf8_to_utf16_exact_length(utf8, utf8_bytes);

// 2. Allocate a buffer (stack or heap)
std::vector<char16_t> buffer(utf16_len + 1);

// 3. Convert
uint32_t written = knst_convert_utf8_to_utf16(utf8, utf8_bytes, buffer.data());
buffer[written] = u'\0';

// 4. Use directly
std::wcout << reinterpret_cast<const wchar_t*>(buffer.data()) << L"\n";
```

### When Should You Call Them Directly?

- **If you manage the buffer yourself** — convert without creating a string object
- **If performance is critical** — to avoid a two-step allocation
- **If you already know the size** — to avoid unnecessary heap allocation

In normal usage you **do not need to call these directly** — `knst_c16string` constructors already do it automatically. But if you want lower-level control, the door is open. 👍

### Performance Note

These functions use a **SIMD-like loop** — processing 4 characters at a time to reduce branching cost:

```cpp
while (i + 3 < src_len) {
    uint8_t c0 = src[i], c1 = src[i+1], c2 = src[i+2], c3 = src[i+3];
    if ((c0 | c1 | c2 | c3) < 0x80) {   // Are all 4 characters ASCII?
        // fast path: copy all directly
    }
    // slow path: handle one at a time
}
```

This technique gives a serious speedup especially on **mostly-ASCII text** (JSON, XML, source code — all typically 90%+ ASCII).

---

## 📚 Full Example

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // 1. Construct from various sources
    knst_c16string a = u"Hello";
    knst_c16string b = "World";         // from UTF-8
    knst_c16string c = 2026;            // int
    knst_c16string d(5, u'*');          // "*****"

    // 2. Concatenation
    knst_c16string greeting = a + u", " + b + u"!";
    std::cout << greeting << "\n";

    // 3. Search
    if (greeting.contains(u"Hello")) {
        std::cout << "Contains Hello\n";
    }

    // 4. Substring
    knst_c16string sub = greeting.substr(0, 5);
    std::cout << "sub = " << sub << "\n";

    // 5. COW copy
    knst_c16string original = u"a long text here";
    knst_c16string copy = original;      // shared buffer
    copy.append(u" [modified]");         // triggers detach

    std::cout << "original: " << original << "\n";
    std::cout << "copy    : " << copy << "\n";

    // 6. Pool allocator
    knst_c16string_sm<> fast = u"string from pool";
    std::cout << "pool count: " << fast.pool_count() << "\n";

    return 0;
}
```

---

## ⚙️ All Configuration Macros

Put these in `knst_settings.hpp` **before including the library**:

| Macro | Default | What it does |
|---|---|---|
| `KNST_C16STRING_DEACTIVE_COW` | Undefined | Disables COW; every copy becomes a deep copy |
| `KNST_C16_STRING_USING_ATOMIC_COW` | Undefined | Makes the COW counter atomic |
| `KNST_C16STRING_ALIGN_32` | Undefined | Aligns SSO to 32 bytes (14 characters) |
| `KNST_C16STRING_ALIGN_64` | Undefined | Aligns SSO to 64 bytes (30 characters) |
| `KNST_MEMORY_POOL_USE_MUTEX` | Undefined | Makes the pool allocator thread-safe |

---

## 💡 Usage Tips

### ✅ Do

```cpp
// 1. Let the class handle UTF-8 conversions
knst_c16string path = get_utf8_path();  // converts automatically

// 2. Pass by value when passing parameters (cheap thanks to COW)
void process(knst_c16string text);   // ✅ good — copying is cheap

// 3. Use u"" for literal strings
if (path.starts_with(u"/home/")) { ... }   // ✅ fast (memcmp)

// 4. Use the pool allocator for very short strings
knst_c16string_sm<> token = tokenize_next();
```

### ❌ Avoid

```cpp
// 1. Sharing a COW-enabled string across threads
// ✅ Fix: define KNST_C16_STRING_USING_ATOMIC_COW

// 2. Appending one character repeatedly
for (char c : str) {
    s.append(c);   // ❌ slow, checks each time
}
// ✅ Fix: reserve first, then batch append

// 3. char-based starts_with on long strings
s.starts_with("a very long utf8 prefix");   // ❌ converts every call
// ✅ Fix: convert to knst_c16string once, then use that

// 4. Forgetting to free the result of to_char()
char* p = s.to_char();
// ... use ...
// ❌ didn't free it! Memory leak.
```

---

## 🎯 What It Buys You

`knst_c16string` provides serious advantage in these situations:

- ✅ **When working with Windows APIs** — file paths, message boxes, registry
- ✅ **When handling Unicode text** — emoji, multilingual content, Arabic/Devanagari-like scripts
- ✅ **When performance matters** — SSO/COW make short strings free and copying cheap
- ✅ **When you copy the same string many times** — COW makes copying almost free
- ✅ **When using the library's complex structures** — `knst_window`, `knst_file`, `knst_display`, etc. expect `knst_c16string`