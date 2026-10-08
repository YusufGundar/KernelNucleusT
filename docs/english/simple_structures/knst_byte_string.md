# knst_byte_string — Binary-Safe Byte Container

Hello! 👋 This document explains what the `knst_byte_string` class is, how to use it, and why it exists in KernelNucleusT.

In short: **an `unsigned char`-based, binary-safe (preserves embedded nulls), SSO-enabled, allocator-aware byte container.** It saves your day when working with network protocols, file contents, and UTF-8 text.

---

## 🎯 Why Does This Class Exist?

`std::string` is fine most of the time, but:

- **Not binary-safe** — it stops at embedded `\0` and behaves like `strlen`
- **No `unsigned char`** — network packets, cryptography, and image data are all `unsigned char`
- **No UTF-16 conversion** — when working with Windows APIs you need it manually
- **No `take_ownership`** — adopting a `new[]`-allocated buffer requires extra code

`knst_byte_string` solves all of these. 🎉

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Binary-safe** | Preserves embedded `\0` bytes, no data loss |
| **`unsigned char`-based** | Natural type for network packets, crypto, image data |
| **SSO** | Small strings (up to 10 bytes by default) live on the stack, no heap allocation |
| **No COW** | Every copy owns its own buffer — thread-safe, predictable |
| **Allocator-aware** | Pool allocator gives serious speedups for small byte strings |
| **`[[no_unique_address]]`** | Zero overhead if you use an empty allocator (C++20) |
| **Rich constructor set** | `char*`, `unsigned char*`, `char16_t*` (UTF-16 → UTF-8), `knst_c16string`, `initializer_list` |
| **`take_ownership`** | Adopts a `new[]`-allocated buffer and deletes it properly |
| **STL-compatible** | Iterators, comparison operators, stream operators |

---

## 🚀 Quick Start

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // Empty
    knst_byte_string b1;

    // From char* (uses strlen)
    knst_byte_string b2 = "hello";

    // Sized raw data (supports embedded nulls)
    const char raw[] = {'A', '\0', 'B', '\0', 'C'};
    knst_byte_string b3(raw, sizeof(raw));      // ✅ binary-safe

    // From unsigned char* (network packet)
    const unsigned char packet[] = {0xDE, 0xAD, 0xBE, 0xEF};
    knst_byte_string b4(packet, sizeof(packet));

    // UTF-16 → UTF-8 conversion
    const char16_t utf16[] = u"Merhaba Dünya";
    knst_byte_string b5(utf16, 13);              // UTF-8 bytes

    // From knst_c16string
    knst_c16string c16 = u"merhaba";
    knst_byte_string b6(c16);

    // initializer_list
    knst_byte_string b7 = {0x01, 0x02, 0x03, 0x04};

    // Output (binary-safe, writes exactly length() bytes)
    std::cout << b2 << "\n";                     // "hello"
    return 0;
}
```

---

## 🏗️ Constructors

### Raw Data

```cpp
knst_byte_string a;  // empty
knst_byte_string b("hello");  // char* (strlen)
knst_byte_string c("hello", 5); // char* + size
knst_byte_string d(data, size);  // unsigned char* + size
knst_byte_string e = {0xDE, 0xAD, 0xBE, 0xEF}; // initializer_list
```

### Encoding Conversions

```cpp
// UTF-16 → UTF-8
const char16_t* utf16 = u"Merhaba 🌍";
knst_byte_string s1(utf16, 10);

// knst_c16string → UTF-8 byte string
knst_c16string c16 = u"ünïcödé";
knst_byte_string s2(c16);
```

### Allocator Usage

```cpp
knst_pool_allocator pool;
knst_byte_string_sm<> s(pool);   // with pool allocator
```

`knst_byte_string_sm<>` = `basic_byte_string<knst_pool_allocator>`. Fast for small byte strings, allocates from the pool.

---

## 📝 Core Methods

### Reading

```cpp
knst_byte_string s = "merhaba";

s.data();      // const unsigned char* — raw pointer
s.length();    // byte count (excluding null)
s.capacity();  // capacity (bytes)
s.empty();     // true/false
s.is_heap();   // heap or SSO?
```

### Writing / Modifying

```cpp
knst_byte_string s;

s.append(data, size);        // append raw data
s.append(other);             // append another byte_string
s.push_back(0xFF);           // append a single byte

s.prepend(data, size);       // prepend to the front
s.prepend(other);            // prepend another byte_string

s.resize(20);                // resize to 20 bytes (zero-filled)
s.resize(20, 0xAA);          // resize to 20 bytes, fill the rest with 0xAA
s.reserve(100);              // reserve at least 100 bytes
s.shrink_to_fit();           // shrink capacity to length+1
s.clear();                   // clear
```

### Access

```cpp
knst_byte_string s = "abc";

s[0];         // 'a' (unsigned char)
s[1];         // 'b'
s[2];         // 'c'

s[0] = 'A';   // modify
```

---

## ⚙️ Operators

### Concatenation

```cpp
knst_byte_string s = "Hello";
s += " ";
s += "World";
s += '!';
// s == "Hello World!"

knst_byte_string combined = knst_byte_string("[") + s + "]";
// combined == "[Hello World!]"
```

### Comparison

All comparison operators exist — both against `basic_byte_string` and against `char*` / `unsigned char*`:

```cpp
knst_byte_string a = "apple";
knst_byte_string b = "banana";

a == b;         // false
a != b;         // true
a < b;          // true (lexicographic)
a > b;          // false
a == "apple";   // true
"apple" == a;   // true (friend)
```

### Stream Operator

```cpp
knst_byte_string s = "merhaba";

std::cout << s;   // writes exactly length() bytes — binary-safe
```

**Note:** `operator<<` does not look for a null terminator. It writes `length()` bytes. So data with embedded nulls comes out **without corruption**.

---

## 🔁 Iterators

Standard STL-compatible iterators:

```cpp
knst_byte_string s = "abc";

for (unsigned char c : s) {
    std::cout << static_cast<char>(c) << ' ';
}

// or
for (auto it = s.begin(); it != s.end(); ++it) {
    unsigned char c = *it;
}

// const versions
for (auto it = s.cbegin(); it != s.cend(); ++it) {
    // read-only
}
```

Uses `knst_iterator<unsigned char>` and `knst_const_iterator<unsigned char>` — the same iterator backbone as `knst_c16string`.

---

## 🔐 Binary-Safe Usage

`knst_byte_string`'s strongest feature is being **binary-safe**. Embedded `\0` bytes are **preserved**.

```cpp
const unsigned char blob[] = {0x00, 0x11, 0x00, 0x22, 0x00, 0x33};
knst_byte_string b(blob, sizeof(blob));

std::cout << "length = " << b.length() << "\n";   // 6 (does not stop at 0x00!)
```

Ideal for network packets, encryption output, and image data.

### Hex Dump Example

```cpp
void print_hex(const unsigned char* p, uint32_t len) {
    for (uint32_t i = 0; i < len; ++i) {
        std::printf("%02X ", static_cast<unsigned>(p[i]));
    }
    std::cout << "\n";
}

knst_byte_string packet = {0xDE, 0xAD, 0xBE, 0xEF};
print_hex(packet.data(), packet.length());
// Output: DE AD BE EF
```

---

## 🌍 UTF-16 → UTF-8 Conversion

Windows APIs use UTF-16, but network protocols and file formats (JSON, UTF-8 text) expect UTF-8. `knst_byte_string` performs this conversion automatically:

```cpp
// From a UTF-16 literal
const char16_t u16[] = u"Merhaba dünya";
knst_byte_string b1(u16, 13);                // UTF-8 bytes
// b1.length() is NOT 13, it is the UTF-8 byte count!

// From knst_c16string
knst_c16string c16 = u"ünïcödé";
knst_byte_string b2(c16);

std::cout << b1 << "\n";   // "Merhaba dünya"
std::cout << b2 << "\n";   // "ünïcödé"
```

**Important:** In UTF-8, non-ASCII characters take 2+ bytes. So `b2.length()` (in bytes) can be **larger** than `c16.length()` (UTF-16 code units).

---

## 🎁 take_ownership — Adopting a `new[]` Buffer

**Adopts** a `new[]`-allocated buffer into the byte string and deletes it properly:

```cpp
unsigned char* raw = new unsigned char[8];
std::memcpy(raw, "network", 7);
raw[7] = '\0';

knst_byte_string owned = knst_byte_string::take_ownership(raw, 7);
// raw is now invalid! The byte string copied the data and delete[]'d the original.

std::cout << owned << "\n";   // "network"
```

**What it does:**
- If the size fits in SSO → copies to the stack, then `delete[]`
- Otherwise → allocates new heap from the allocator, copies, then `delete[]`
- So "take_ownership" is effectively **copy + delete**

**Why?** Freeing a `new[]`-allocated buffer with `free` is **dangerous**. This method guarantees `delete[]`.

---

## 🧮 SSO (Small String Optimization)

Small byte strings never hit the heap — they live inside a **union** in the class.

| Setting | SSO Capacity |
|---|---|
| Default | **10 bytes** (KNST_SSO_BUFFER_LENGTH) |

**Example:** `knst_byte_string("short")` (5 bytes) never touches the heap.

**Note:** Alignment settings work the same way as in `knst_c16string` (`KNST_CLASS_ALIGNMENT`).

---

## 🧵 Thread Safety — No COW

`knst_byte_string` does **not** use COW (Copy-on-Write). This is intentional:

- Every copy **owns its own buffer**
- Sharing across threads is safe (each thread gets its own copy)
- Behavior is predictable (no detach on write)

**Comparison:**

| | `knst_c16string` | `knst_byte_string` |
|---|---|---|
| COW | ✅ Optional | ❌ No |
| Copy | O(1) (with COW) | O(n) (always) |
| Thread safety | Safe if COW disabled | Always safe |

For network protocols and binary data, **predictability** matters — so COW was avoided.

---

## 📊 Performance Notes

| Operation | Complexity | Note |
|---|---|---|
| Small string creation (< SSO) | **O(1)** | No heap |
| Copy | **O(n)** | Always a deep copy |
| `append` | **Amortized O(1)** | Capacity grows 2x |
| `push_back` | **Amortized O(1)** | Capacity grows 2x |
| `prepend` | **O(n)** | Requires data shift |
| `resize` | **O(n)** | May require realloc |
| `take_ownership` | **O(n)** | Copy required |

### When Is It Slow?

- **Very frequent `prepend`** — a `memmove` on each call
- **Very long byte strings** — heap allocation cost
- **UTF-16 conversion** — requires encoding computation

---

## 🧠 Using with the Pool Allocator (`knst_byte_string_sm`)

`knst_pool_allocator` hands out small allocations from **pre-reserved blocks**. If you are creating and destroying many network packets or short strings, this gives a **serious speed boost**.

### With the Default Pool

```cpp
knst_byte_string_sm<> s = "short text";

std::cout << "pool_count     : " << s.pool_count() << "\n";       // 4
std::cout << "max_block_size : " << s.max_block_size() << "\n";   // 2048
```

By default there are 4 sizes: **64, 256, 1024, 2048 bytes**.

### With Your Own Pool Sizes

```cpp
knst_pool_allocator pool(128, 512, 4096);
knst_byte_string_sm<> s(pool);
s.append("merhaba");
```

### When Should You Use the Pool?

| Situation | Use pool? |
|---|---|
| Many short packets (network, parser) | ✅ Yes, big win |
| Occasional byte strings | ❌ Unnecessary |
| Need thread safety | ✅ But define `KNST_MEMORY_POOL_USE_MUTEX` |
| Long data (2048+ bytes) | ⚠️ Pool does not apply, falls back to `malloc` |

---

## 📚 Full Example

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // 1. Construct from various sources
    knst_byte_string a = "Hello";
    knst_byte_string b(data, size);              // raw data
    knst_byte_string c = {0x01, 0x02, 0x03};    // binary
    knst_byte_string d(u"UTF-16 text", 12);     // converted to UTF-8

    // 2. Concatenation
    knst_byte_string greeting = a + " World!";
    std::cout << greeting << "\n";

    // 3. Binary-safe data
    const unsigned char packet[] = {0xDE, 0xAD, 0x00, 0xBE, 0xEF};
    knst_byte_string p(packet, sizeof(packet));
    std::cout << "Length: " << p.length() << " (embedded 0x00 preserved)\n";

    // 4. Modification
    p.push_back(0xFF);
    p.prepend(reinterpret_cast<const unsigned char*>("HDR"), 3);

    // 5. take_ownership
    unsigned char* buf = new unsigned char[4];
    std::memcpy(buf, "data", 4);
    knst_byte_string owned = knst_byte_string::take_ownership(buf, 4);

    // 6. Pool allocator
    knst_byte_string_sm<> fast = "from pool";
    std::cout << "pool count: " << fast.pool_count() << "\n";

    return 0;
}
```

---

## 💡 Usage Tips

### ✅ Do

```cpp
// 1. Always supply the size for raw data
knst_byte_string p(packet_data, packet_size);   // ✅ binary-safe

// 2. Let the class handle UTF-16 conversion
knst_byte_string b(u"UTF-16 text", length);     // ✅ automatic UTF-8

// 3. Adopt new[] buffers with take_ownership
auto s = knst_byte_string::take_ownership(ptr, size);  // ✅ delete[] guaranteed

// 4. Use the pool allocator for very short byte strings
knst_byte_string_sm<> packet = read_packet();
```

### ❌ Avoid

```cpp
// 1. Measuring binary data with strlen
knst_byte_string s(some_binary_data);   // ❌ stops at embedded 0x00!
// ✅ Fix: always pass the size explicitly

// 2. Doing very frequent prepends
for (...) {
    s.prepend(byte, 1);   // ❌ O(n) — shifts each time
}
// ✅ Fix: collect with append, then reverse once

// 3. Confusing lengths in UTF-16 conversion
knst_byte_string b(u16_str, 13);       // ❌ 13 is UTF-16 code units, not bytes!
// ✅ Fix: pass the source code-unit count; bytes are computed automatically

// 4. Printing binary data with operator<<
std::cout << binary_packet;   // ❌ meaningless characters
// ✅ Fix: use a hex dump
```

---

## 🎯 What It Buys You

`knst_byte_string` provides serious advantage in these situations:

- ✅ **Writing network protocols** — packets, headers, checksum data
- ✅ **Handling binary data** — cryptography, image/audio codecs, file formats
- ✅ **Handling UTF-8 text** — JSON, XML, REST API responses
- ✅ **Converting UTF-16 from Windows APIs** — together with `knst_c16string`
- ✅ **Managing `new[]` buffers** — `take_ownership` saves your day
- ✅ **Using other KernelNucleusT modules** — `knst_file`, `knst_window`, etc. expect `knst_byte_string`