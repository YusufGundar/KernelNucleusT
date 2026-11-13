# knst_json — KernelNucleusT JSON Module

Hey! 👋 `knst_json` is KernelNucleusT's **RFC 8259 compliant, exception-free, allocator-hookable, binary-safe** JSON value type. A parser, serializer, and a large toolbox around it: JSON Pointer (RFC 6901), JSON Patch (RFC 6902), JSON Merge Patch (RFC 7386), JSONPath subset, JSON Schema draft-07 subset, flatten/unflatten, walk, hash, canonical (RFC 8785 JCS), NDJSON, atomic file save. **All in a single header, `noexcept` + `bool`/`result` return.**

**In short:** Header-only, C++17+. Windows / Linux / Android / macOS / iOS all supported. `knst_json` is **24 bytes** (SSO ≤14 byte strings). int64 + uint64 exact, double for the rest. Parser, serializer, destructor, deep copy, `operator==` — all **iterative** (no stack overflow). Remaining helpers (compare, hash, merge_patch, walk, query, schema, diff) **recursive but bounded by `KNST_JSON_MAX_RECURSION` (default 1000)**.

---

## 🎯 Honesty Matrix — What Exists, What Doesn't?

### General

| Feature | Windows | Linux | Android | macOS | iOS |
|---|---|---|---|---|---|
| **DOM parse (RFC 8259)** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **SAX parse** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Prefix parse** (concatenated) | ✅ | ✅ | ✅ | ✅ | ✅ |
| **NDJSON / JSON Lines** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Lenient mode** (comments, trailing commas, NaN, 'single quotes', unquoted keys) | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Dump** (pretty / compact) | ✅ | ✅ | ✅ | ✅ | ✅ |
| **RFC 8785 JCS canonical** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **sort_keys** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **ensure_ascii / escape_slash / escape_html** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Streaming dump (FILE\*, callback)** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **JSON Pointer (RFC 6901)** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **JSON Patch (RFC 6902)** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **JSON Merge Patch (RFC 7386)** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **JSONPath subset** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **JSON Schema draft-07 subset** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **flatten / unflatten** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **walk** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **compare / hash** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Atomic file save** | ✅ `MoveFileExW` | ✅ `rename`+`fsync` | ✅ | ✅ | ✅ |
| **SSE2 / NEON / SWAR** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **std::string bridge** | opt-in | opt-in | opt-in | opt-in | opt-in |
| **knst_byte_string / knst_c16string bridge** | auto-detect | auto-detect | auto-detect | auto-detect | auto-detect |
| **Full JSON5** | ❌ subset | ❌ | ❌ | ❌ | ❌ |
| **YAML / TOML / CBOR / MessagePack** | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Schema `pattern` (regex)** | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Schema `format`** | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Schema external `$ref`** | ❌ local only | ❌ | ❌ | ❌ | ❌ |
| **Full JSONPath RFC 9535** | ❌ subset | ❌ | ❌ | ❌ | ❌ |
| **Exceptions** | ❌ never thrown | ❌ | ❌ | ❌ | ❌ |

**Critical notes:**
- **`knst_json` is 24 bytes.** SSO strings ≤14 bytes on the stack. 15+ bytes goes to heap.
- **Object hash index threshold = 8.** ≤8 members → linear scan. 9+ → hash table.
- **Recursive helpers** (`compare`, `hash`, `merge_patch`, `walk`, `query`, `schema`, `diff`) capped by `KNST_JSON_MAX_RECURSION`. On exceed: compare → 0 (treated as equal, watch out!), hash → partial, walk → false.
- **Parser, serializer, destructor, deep copy, `operator==` are ITERATIVE.** No stack overflow even at extreme depth.
- **`pattern` and `format` are NOT evaluated in schema.** Only their presence is accepted.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **SSO string** | 24-byte value, ≤14 byte string on the stack |
| **Binary-safe** | Embedded `\0` in strings, byte arrays |
| **int64 + uint64 exact** | `> INT64_MAX` values keep exact precision |
| **Locale-independent** | `1.5` parses correctly on TR/DE systems (dot) |
| **Shortest round-trip double** | `1.5` → `"1.5"`, `0.1` → `"0.1"` (no 17-digit garbage) |
| **Iterative parser** | No crash even at 10,000 depth |
| **Iterative destructor** | Same |
| **SIMD scan** | SSE2/NEON/SWAR makes string parse 3-10× faster |
| **Exact-size containers** | Parser `realloc`s to exact size on close |
| **Zero-copy string write** | Parser writes straight into the value |
| **Streaming output** | 64 KiB chunk callback |
| **Atomic file save** | Unique temp + fsync + rename + dir fsync |
| **JSON Pointer + Patch + Merge Patch** | Full RFC 6901/6902/7386 |
| **JSONPath subset** | `$`, `.name`, `..`, `*`, `[n]`, `[-n]`, `[a:b:c]`, `[1,2,'x']`, `[?(@.x < 10)]` |
| **JSON Schema draft-07** | Local `$ref`, all/any/one/not, if/then/else, dependencies |
| **Flatten / Unflatten** | `{a:{b:1}}` ↔ `{"/a/b":1}` |
| **Walk** | Pointer-aware tree traversal |
| **Lenient mode** | Comments, trailing commas, NaN, single quotes, unquoted keys |
| **RFC 8785 JCS** | Canonical JSON (for signing/hash) |
| **Duplicate key policies** | `last_wins` / `first_wins` / `error` |
| **UTF-8 validation** | Strict mode, invalid byte → error |
| **Lone surrogate handling** | `\uD800` alone → error or U+FFFD |
| **Allocator hooks** | `KNST_JSON_MALLOC/REALLOC/FREE` |

---

## 🚀 Quick Start

### Simple parse + dump

```cpp
#include "KernelNucleusT.hpp"   // knst_json.hpp pulled in automatically

knst_json j;
knst_json_error err;

if (!knst_json::parse_cstr("{\"name\":\"knst\",\"ver\":2}", j, &err)) {
    std::cerr << "Parse error: " << err.message()
              << " at " << err.line << ":" << err.column << "\n";
    return 1;
}

const char* name;
size_t name_len;
if (j["name"].get_string(name, name_len)) {
    std::cout << "name = " << std::string(name, name_len) << "\n";
}

int64_t ver = j.value("ver", int64_t{0});
std::cout << "ver  = " << ver << "\n";

knst_json_buffer out;
if (j.dump(out, knst_json_dump_options::pretty(2))) {
    std::cout << out.data() << "\n";
}
```

### JSON Pointer

```cpp
knst_json cfg;
knst_json::parse_cstr(R"({"net":{"port":8080,"host":"0.0.0.0"}})", cfg);

// Read
const knst_json* port = cfg.find_pointer("/net/port");
int64_t p = port->value_or(int64_t{0});

// Shortcut
int64_t p2 = cfg.value_at("/net/port", 1234);

// Write
cfg.pointer_set("/net/port", knst_json(9090));
```

### JSON Patch

```cpp
knst_json doc;
knst_json::parse_cstr(R"({"a":1,"b":2})", doc);

knst_json patch;
knst_json::parse_cstr(R"([
    {"op":"replace","path":"/a","value":10},
    {"op":"add","path":"/c","value":3},
    {"op":"remove","path":"/b"}
])", patch);

knst_json_error err;
if (!doc.apply_patch(patch, &err, /*atomic=*/true)) {
    std::cerr << "Patch failed at op " << err.offset << ": " << err.message() << "\n";
}
// doc = {"a":10,"c":3}
```

### File I/O (atomic)

```cpp
knst_json j = knst_json::make_object();
j["port"] = knst_json(8080);
j["host"] = knst_json("0.0.0.0");

// Atomic write: temp + fsync + rename
j.save_file(u"/etc/myapp/config.json", knst_json_dump_options::pretty(2));
```

---

## 📋 General API

| Category | Methods |
|---|---|
| **Type queries** | `get_type`, `is_null/bool/integer/float/number/string/array/object/container/integral_value` |
| **Scalar access** | `get(bool/double/float/int*/uint*)`, `get_string`, `c_str`, `value_or`, `value(key, default)`, `value_at(ptr, default)` |
| **Scalar mutation** | `reset`, `set_null/bool/double/int64/uint64/string/string_u16/bytes`, `append_string` |
| **Array** | `reserve`, `push_back`, `insert`, `erase_at`, `erase_range`, `pop_back`, `at`, `append`, `append_move`, `remove_if`, `sort_array`, `unique_array`, `begin/end` |
| **Object** | `set`, `find`, `contains`, `get_or_insert`, `erase`, `rename_key`, `sort_keys`, `reserve_members`, `update`, `operator[]`, `members()` |
| **Whole-value** | `copy_from`, `operator==`, `operator!=`, `operator<`, `swap`, `compare`, `compare_numbers`, `hash`, `count_nodes` |
| **Merge Patch** | `merge_patch`, `merge_patch_atomic`, `make_merge_patch` |
| **Pointer** | `find_pointer`, `find_pointer_uri`, `pointer_slot`, `pointer_set`, `pointer_insert`, `pointer_remove`, `pointer_replace` |
| **Patch** | `apply_patch(atomic)`, `make_patch` |
| **Query** | `select(path)` (JSONPath) |
| **Schema** | `validate(schema, result)` |
| **Traversal** | `walk(fn)`, `flatten(out)`, `unflatten(flat, out)` |
| **Parse** | `parse`, `parse_cstr`, `parse_prefix`, `parse_sax`, `parse_lines`, `load_file_utf8/u16`, `load_lines_utf8`, `parse_stream` |
| **Dump** | `dump`, `dump_append`, `dump_lines`, `dump_stream`, `dump_file`, `save_file_utf8/u16` |

---

## 🎯 1) Enums and Options

### 1.1) `knst_json_type`

```cpp
enum class knst_json_type : uint8_t {
    null_value = 0,
    boolean,
    integer,           // int64_t
    unsigned_integer,  // uint64_t, ONLY for > INT64_MAX
    floating,          // double
    string,
    array,
    object
};
```

**When is `unsigned_integer` used?** Only when `uint64_t > INT64_MAX`. Values `≤ INT64_MAX` use `integer`.

### 1.2) `knst_json_errc`

```cpp
enum class knst_json_errc : uint8_t {
    none = 0,
    out_of_memory, empty_input, unexpected_end, unexpected_char,
    invalid_literal, invalid_number, number_out_of_range,
    invalid_escape, invalid_unicode_escape, lone_surrogate,
    invalid_utf8, control_char_in_string,
    max_depth_exceeded, input_too_large, string_too_long,
    container_too_large, duplicate_key, trailing_characters,
    non_finite_number, io_error, invalid_argument,
    // v2:
    handler_aborted,       // SAX handler returned false
    pointer_syntax,        // JSON Pointer / JSONPath syntax
    path_not_found,
    patch_invalid,         // malformed JSON Patch
    patch_test_failed,     // "test" op failed
    schema_violation
};
```

**`err.message()`** returns a readable string.

### 1.3) `knst_json_error`

```cpp
struct knst_json_error {
    knst_json_errc code = knst_json_errc::none;
    size_t   offset = 0;    // byte offset (patch: op index)
    uint32_t line   = 0;    // 1-based
    uint32_t column = 0;    // 1-based (byte)

    explicit operator bool() const noexcept;
    const char* message() const noexcept;
};
```

### 1.4) `knst_json_parse_options`

```cpp
struct knst_json_parse_options {
    uint32_t max_depth = 512;                  // nesting limit (stack-safe)
    size_t   max_input_size = 0xFFFFFFF0u;
    uint32_t max_string_length = 0xFFFFFFF0u;
    uint32_t max_container_size = 0xFFFFFFF0u;

    knst_json_duplicate_keys duplicate_keys = knst_json_duplicate_keys::last_wins;

    bool validate_utf8 = true;                 // strict UTF-8
    bool allow_bom = true;                     // skip UTF-8 BOM
    bool allow_comments = false;               // // and /* */
    bool allow_trailing_commas = false;
    bool allow_nan_inf = false;                // NaN, Infinity
    bool allow_single_quotes = false;          // 'string', 'key'
    bool allow_unquoted_keys = false;          // {key: 1}
    bool replace_lone_surrogates = false;      // \uD800 → U+FFFD

    static knst_json_parse_options lenient() noexcept;   // enable everything
};
```

**`lenient()`** — comments + trailing commas + NaN/Inf + single quotes + unquoted keys + lone surrogate replacement. Good for JSON5-ish config files.

### 1.5) `knst_json_duplicate_keys`

```cpp
enum class knst_json_duplicate_keys : uint8_t {
    last_wins,    // last value wins (default)
    first_wins,   // first value kept (parser discards the later ones)
    error         // duplicate key → duplicate_key error
};
```

### 1.6) `knst_json_dump_options`

```cpp
struct knst_json_dump_options {
    uint32_t indent = 0;              // 0 = compact
    char     indent_char = ' ';       // ' ' or '\t'
    bool     sort_keys = false;
    bool     ensure_ascii = false;    // all non-ASCII → \uXXXX
    bool     trailing_newline = false;
    uint32_t max_depth = 512;

    knst_json_invalid_utf8 invalid_utf8 = error;    // error / replace / ignore
    knst_json_non_finite   non_finite = error;      // error / null_value / literal

    // v2:
    bool     escape_slash = false;    // '/' → "\/"
    bool     escape_html = false;     // '<' '>' '&' '\'' U+2028/2029 → \uXXXX
    bool     canonical = false;       // RFC 8785 JCS (overrides other settings)

    static knst_json_dump_options pretty(uint32_t n = 2) noexcept;
    static knst_json_dump_options canonical_form() noexcept;
};
```

**What does `canonical = true` do?** RFC 8785 JCS:
- `indent = 0`
- `sort_keys = true` (UTF-16 code unit order)
- `ensure_ascii = false` (raw UTF-8)
- `escape_slash = false`, `escape_html = false`
- `trailing_newline = false`
- `invalid_utf8 = error`, `non_finite = error`
- Numbers in ECMAScript Number::toString format

### 1.7) `knst_json_validation`

```cpp
struct knst_json_validation {
    bool valid = true;
    const char* keyword = "";    // keyword that failed
    knst_json_buffer path;        // JSON Pointer (instance location)
};
```

---

## 🎯 2) `knst_json` — Value Type

### 2.1) Size and SSO

```cpp
static_assert(sizeof(knst_json) <= 32);   // actual: 24 bytes
```

**Memory layout:**
- `type_` (1 byte) + 7 padding
- `Val v_` (union, 16 bytes) — largest member: `arr_t` (`knst_json* p; uint32_t n; uint32_t c;`)
- **Total: 24 bytes**

**SSO string ≤14 bytes** → on the stack (`sb[16]` union member). 15+ bytes → heap (`p - 8` = 4-byte capacity + 4-byte padding).

### 2.2) Factory

```cpp
static knst_json make_array() noexcept;
static knst_json make_object() noexcept;
static knst_json make_string(const char* s, size_t n) noexcept;
static knst_json make_array(std::initializer_list<knst_json> items) noexcept;
static knst_json make_object(std::initializer_list<std::pair<const char*, knst_json>> items) noexcept;
```

**Note:** `make_object()` can return **null** on allocation failure. `make_array()` likewise. Initializer list constructors return `null` on failure.

### 2.3) Constructors

```cpp
knst_json() noexcept;                                // null
knst_json(std::nullptr_t) noexcept;                  // null
knst_json(bool b) noexcept;
template<class T> knst_json(T v) noexcept;           // integral
template<class T> knst_json(T v) noexcept;           // floating
knst_json(const char* s) noexcept;                   // null if s null
knst_json(const char* s, size_t n) noexcept;

knst_json(knst_json&&) noexcept;
knst_json(const knst_json&) noexcept;                // deep copy, fail → null
```

**Copy constructor becomes null on allocation failure.** Use `copy_from()` to catch the failure.

### 2.4) Type queries

```cpp
type get_type() const noexcept;
bool is_null() const noexcept;
bool is_bool() const noexcept;
bool is_integer() const noexcept;      // integer or unsigned_integer
bool is_float() const noexcept;
bool is_number() const noexcept;
bool is_string() const noexcept;
bool is_array() const noexcept;
bool is_object() const noexcept;
bool is_container() const noexcept;    // array || object
bool is_scalar() const noexcept;
bool is_integral_value() const noexcept;   // integer or float with integral value
const char* type_name() const noexcept;
```

**`is_integral_value`** — true for float with integral value (`1.0` → true). Used in schema `type: integer` check.

### 2.5) Scalar access

```cpp
bool get(bool& out) const noexcept;
bool get(double& out) const noexcept;
bool get(float& out) const noexcept;
template<class T> bool get(T& out) const noexcept;   // integral
bool get_string(const char*& ptr, size_t& len) const noexcept;

const char* c_str(const char* fallback = "") const noexcept;
size_t length() const noexcept;
bool string_equals(const char* s, size_t n) const noexcept;

template<class T> T value_or(T def) const noexcept;
const char* value_or(const char* def) const noexcept;

template<class T> T value(const char* key, T def) const noexcept;
const char* value(const char* key, const char* def) const noexcept;
template<class T> T value_at(const char* ptr, T def) const noexcept;
```

**`get_string`** — the pointer points directly into the value's storage (NUL-terminated). Valid until the value is mutated/destroyed.

**`get(T&)` integral** — returns false if the value doesn't fit in `T`'s range.

### 2.6) Scalar mutation

```cpp
void reset() noexcept;             // destroy + null
void set_null() noexcept;
void set_bool(bool) noexcept;
void set_double(double) noexcept;
void set_int64(int64_t) noexcept;
void set_uint64(uint64_t) noexcept;   // > INT64_MAX → unsigned_integer
bool set_string(const char*, size_t) noexcept;
bool set_string(const char*) noexcept;
bool set_string_u16(const char16_t*, size_t) noexcept;   // UTF-16 → UTF-8
bool set_bytes(const unsigned char*, size_t) noexcept;
bool append_string(const char*, size_t) noexcept;
```

**`set_*` for types requiring no allocation** is `noexcept` and returns void. **String uses `bool`** (allocation required).

### 2.7) Size / empty / clear

```cpp
size_t size() const noexcept;      // array size or object member count; string length
bool empty() const noexcept;
size_t capacity() const noexcept;
void clear() noexcept;             // empty container/string, type preserved
void shrink_to_fit() noexcept;     // capacity → size
```

**`clear()`** — array keeps capacity, object rebuilds index. Type unchanged.

---

## 📦 3) Array Operations

```cpp
bool reserve(size_t n) noexcept;
bool push_back(knst_json&& v) noexcept;
bool push_back(const knst_json& v) noexcept;
knst_json* push_back_slot() noexcept;      // pushes null slot, returns pointer
bool insert(size_t idx, knst_json&& v) noexcept;
bool insert(size_t idx, const knst_json& v) noexcept;
bool erase_at(size_t idx) noexcept;
bool erase_range(size_t first, size_t last) noexcept;
bool pop_back() noexcept;
knst_json* at(size_t idx) noexcept;
const knst_json* at(size_t idx) const noexcept;
bool append(const knst_json& other_array) noexcept;
bool append_move(knst_json& other_array) noexcept;
template<class Pred> size_t remove_if(Pred pred) noexcept;
template<class Cmp> void sort_array(Cmp less) noexcept;
void sort_array() noexcept;                // default compare()
size_t unique_array() noexcept;             // O(n²) worst, hash prefilter

knst_json* begin() noexcept;
knst_json* end() noexcept;
const knst_json* begin() const noexcept;
const knst_json* end() const noexcept;
```

**`null` → array conversion:** When `reserve`, `push_back`, or `insert` is called and the type is null, it becomes an array automatically. Other types return false.

**`insert(idx, ...)` shifting:** `memmove` shifts the rest. **O(n)** cost.

**`unique_array()`** — hash prefilter (optional) + O(n²) worst case. Slow but correct on unsorted elements.

---

## 🗂️ 4) Object Operations

```cpp
bool set(const char* key, size_t key_len, knst_json&& v) noexcept;
bool set(const char* key, size_t key_len, const knst_json& v) noexcept;
bool set(const char* key, knst_json&& v) noexcept;
bool set(const char* key, const knst_json& v) noexcept;

knst_json* find(const char* key, size_t key_len) noexcept;
const knst_json* find(const char* key, size_t key_len) const noexcept;
knst_json* find(const char* key) noexcept;
const knst_json* find(const char* key) const noexcept;
bool contains(const char* key) const noexcept;

knst_json* get_or_insert(const char* key, size_t key_len) noexcept;
bool erase(const char* key, size_t key_len) noexcept;
bool erase(const char* key) noexcept;
bool rename_key(const char* old_key, const char* new_key) noexcept;
bool sort_keys() noexcept;
bool reserve_members(size_t n) noexcept;
bool update(const knst_json& other) noexcept;

knst_json& operator[](const char* key) noexcept;      // get_or_insert
const knst_json& operator[](const char* key) const noexcept;

member* members_begin() noexcept;
member* members_end() noexcept;
member_range members() noexcept;                        // range-for
```

### 4.1) Hash index

```cpp
static constexpr uint32_t index_threshold = 8;
```

- **≤8 members** → linear scan (`O(n)` per lookup)
- **9+ members** → hash index (open addressing, capacity = 2×members min 16)
- **Rebuild:** every time `2×` threshold exceeded
- **Rebuild fail** → falls back to linear scan (`ix = nullptr`), object still valid

**Hash seed:** `hash_seed()` derived from ASLR. Crafted-collision DoS payloads can't be precomputed.

### 4.2) Insertion order

**Object members stored in insertion order.** Iteration preserves that order. `sort_keys()` sorts bytewise. In dump, `sort_keys=true` alters order (during dump only).

### 4.3) `set` behavior

- Type null → becomes object
- Type not object → false
- Key absent → new member (appended)
- Key present → value overwritten (position preserved)

### 4.4) `get_or_insert`

Creates with null value if key is missing, returns pointer. `operator[]` uses this.

### 4.5) `rename_key`

Renames key in place (no position change). Fails if `old` missing or `new` exists.

### 4.6) `update`

Deep-copies all members from `other`. Existing keys **overwritten**.

---

## 🔄 5) Deep Copy / Compare / Hash

### 5.1) `copy_from` (iterative, strict)

```cpp
bool copy_from(const knst_json& o) noexcept;
```

**Iterative** — explicit work stack. On failure, **`*this` unchanged**. On success, deep copy of `o`.

**Why it matters?** Copy constructor uses `copy_from` but drops to null on failure. Use `copy_from` directly to catch failures.

### 5.2) `operator==` (iterative)

```cpp
bool operator==(const knst_json& o) const noexcept;
```

**Semantics:**
- Numeric comparison exact: `1 == 1.0`, `1 == 1u`, `> INT64_MAX` correct
- `NaN != NaN` (correct for floating)
- Object: **order-insensitive** (same key-value set)
- Array: element-wise
- String: bytewise

### 5.3) `compare` (recursive, `KNST_JSON_MAX_RECURSION` limit)

```cpp
static int compare(const knst_json& a, const knst_json& b) noexcept;
static int compare_numbers(const knst_json& a, const knst_json& b) noexcept;
```

**Total order:**
```
null < boolean < number < string < array < object
```

**Numeric ordering:** exact, NaN at front.

**Recursive** — if `max_rec` is exceeded, returns 0 (**danger**: different deep trees can be treated as "equal").

### 5.4) `hash` (recursive)

```cpp
uint64_t hash() const noexcept;
```

**Consistent with `operator==`:**
- `1` and `1.0` hash alike
- Object order-independent

**Use:** hash map keys, deduplication, cache.

---

## 🎯 6) JSON Pointer (RFC 6901)

```cpp
const knst_json* find_pointer(const char* ptr, size_t len) const noexcept;
const knst_json* find_pointer(const char* ptr) const noexcept;
knst_json*       find_pointer(const char* ptr, size_t len) noexcept;
const knst_json* find_pointer_uri(const char* uri) const noexcept;   // "#/a/b%20c"

knst_json* pointer_slot(const char* ptr, size_t len) noexcept;       // creates intermediate
bool pointer_set(const char* ptr, knst_json&& v) noexcept;
bool pointer_set(const char* ptr, const knst_json& v) noexcept;
bool pointer_insert(const char* ptr, size_t len, knst_json&& v) noexcept;   // RFC 6902 add
bool pointer_insert(const char* ptr, knst_json&& v) noexcept;
bool pointer_remove(const char* ptr, size_t len, knst_json* removed = nullptr) noexcept;
bool pointer_remove(const char* ptr) noexcept;
bool pointer_replace(const char* ptr, knst_json&& v) noexcept;       // only if exists
```

**Format:** `/a/b/0` — `/`-separated tokens. `~0` → `~`, `~1` → `/`.

**`-`** — array append (only via `pointer_insert` and `pointer_slot`).

**`pointer_slot`** — creates intermediate containers (null → object/array). **Preserves** existing values.

**`pointer_set`** — target exists → overwrite, missing → create (parent exists).

**`pointer_insert`** — RFC 6902 "add" semantics:
- Object: set (no overwrite; set)
- Array: **shift** (add at index), `-` = append

**`pointer_remove`** — RFC 6902 "remove". `removed` parameter hands out the removed value.

**`pointer_replace`** — replaces only an **existing** value. Fails otherwise.

---

## 🧩 7) JSON Patch (RFC 6902)

```cpp
bool apply_patch(const knst_json& patch, knst_json_error* err = nullptr,
                 bool atomic = true) noexcept;
static bool make_patch(const knst_json& from, const knst_json& to,
                        knst_json& out) noexcept;
```

**`atomic = true` (default):** Applies to a copy of `*this`. Commits on success. On failure **`*this` unchanged**.

**`atomic = false`:** Applies directly to `*this`. On failure **partial state**.

### 7.1) Supported ops

| Op | Behavior |
|---|---|
| **`add`** | Target exists → overwrite (object) or shift (array); missing → create |
| **`remove`** | Deletes target |
| **`replace`** | Overwrites if target exists; fails otherwise |
| **`test`** | Target value `==` given value |
| **`move`** | `from` → `to` (target can't be nested) |
| **`copy`** | Deep copy `from` → `to` |

**`test` semantics:** RFC 6902 "mathematical equality" — `1 == 1.0` passes. Not byte-level.

### 7.2) `make_patch` / `diff_rec`

**Recursive** (`KNST_JSON_MAX_RECURSION`). Diffs objects and arrays. Scalars become `replace`.

**Array diff:**
- Common prefix recurses element-wise
- Extra tail → `remove` (from tail to head, so indices stay valid)
- Missing tail → `add` (in order)

---

## 🎯 8) JSON Merge Patch (RFC 7386)

```cpp
bool merge_patch(const knst_json& patch) noexcept;
bool merge_patch_atomic(const knst_json& patch) noexcept;
static bool make_merge_patch(const knst_json& from, const knst_json& to,
                              knst_json& out) noexcept;
```

**RFC 7386 semantics:**
- `patch` not an object → **target replaced**
- `patch` object → target must be object (else replace)
- `patch.member = null` → **member removed**
- `patch.member = value` → recurse

**`merge_patch`** — in-place. Partial state on failure.
**`merge_patch_atomic`** — copy + apply + commit. Unchanged on failure.

**`make_merge_patch`** — produces the minimal patch turning `from` into `to`.

---

## 🎯 9) JSONPath Subset

```cpp
bool select(const char* path, size_t len, knst_json_ptr_list& out,
            knst_json_error* err = nullptr) const noexcept;
bool select(const char* path, knst_json_ptr_list& out,
            knst_json_error* err = nullptr) const noexcept;
```

### 9.1) Supported syntax

| Syntax | Meaning |
|---|---|
| `$` | Root |
| `.name` | Child (identifier) |
| `['name']` / `["name"]` | Child (quoted) |
| `..name` | Recursive descent (children + self) |
| `..[*]` / `.. *` | All descendants |
| `.*` | All children |
| `[*]` | All children |
| `[n]` | Index (0-based) |
| `[-n]` | Negative index (-1 = last) |
| `[a:b]` / `[a:b:c]` | Slice (start:end:step) |
| `[1,2,'x']` | Union |
| `[?(@.x < 10)]` | Filter (existence, `==`, `!=`, `<`, `<=`, `>`, `>=`) |
| `[?(@.tags)]` | Existence check |

**NOT supported:** Functions (`length()`, `match()`), regex, script expressions, `$..` combined with filters over huge trees (performance).

### 9.2) `select` example

```cpp
knst_json j;
knst_json::parse_cstr(R"({
    "store": {
        "book": [
            {"title": "A", "price": 10},
            {"title": "B", "price": 25},
            {"title": "C", "price": 5}
        ]
    }
})", j);

knst_json_ptr_list out;
if (j.select("$.store.book[?(@.price < 15)].title", out)) {
    for (const knst_json* p : out) {
        std::cout << p->c_str() << "\n";    // "A", "C"
    }
}
```

**Result pointers** point into `j`. Invalid if `j` is destroyed/mutated.

---

## 🎯 10) JSON Schema (draft-07 subset)

```cpp
bool validate(const knst_json& schema,
              knst_json_validation* result = nullptr) const noexcept;
```

### 10.1) Supported keywords

| Category | Keywords |
|---|---|
| **Type** | `type` (string or array), `enum`, `const` |
| **Number** | `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `multipleOf` |
| **String** | `minLength`, `maxLength` (codepoints) |
| **Array** | `minItems`, `maxItems`, `uniqueItems`, `items`, `additionalItems`, `contains` |
| **Object** | `minProperties`, `maxProperties`, `required`, `properties`, `additionalProperties`, `dependencies` |
| **Combinator** | `allOf`, `anyOf`, `oneOf`, `not`, `if`/`then`/`else` |
| **Ref** | `$ref` (local — `#/...` only) |

### 10.2) Not supported

- **`pattern`** (regex) — not evaluated, ignored
- **`format`** (email, uri, date-time, ...) — not evaluated, ignored
- **External `$ref`** (`http://...`, `file://...`)
- **Draft-04 `id`** / draft-06 `exclusiveMinimum: true` vs draft-07 numeric

**Result:** Schema validation passes but **no pattern/format checks**. Handle those yourself.

### 10.3) `knst_json_validation` result

```cpp
knst_json_validation v;
if (!doc.validate(schema, &v)) {
    std::cout << "Schema fail: " << v.keyword << "\n";
    std::cout << "At path: " << (v.path.data() ? v.path.data() : "") << "\n";
    // e.g.: keyword="minimum", path="/age"
}
```

**First failure** captured (`recorded_` flag). Path is JSON Pointer.

---

## 🎯 11) Walk / Flatten / Unflatten

### 11.1) `walk`

```cpp
template<class F> bool walk(F&& fn) const noexcept(false);
```

**Callback signature:** `bool fn(const knst_json& value, const char* pointer, size_t pointer_len)`

- **Pre-order** — parent before children
- Returning `false` **stops walk** (returns false)
- **Recursive** — `KNST_JSON_MAX_RECURSION` limit
- Path passed as JSON Pointer on each call

```cpp
knst_json j;
knst_json::parse_cstr(R"({"a":{"b":[1,2]}})", j);

j.walk([](const knst_json& v, const char* ptr, size_t len) {
    std::cout << (len ? std::string(ptr, len) : "/")
              << " = " << v.type_name() << "\n";
    return true;
});
// / = object
// /a = object
// /a/b = array
// /a/b/0 = integer
// /a/b/1 = integer
```

### 11.2) `flatten`

```cpp
bool flatten(knst_json& out) const noexcept;
```

`{"a":{"b":[1,2]}}` → `{"/a/b/0":1,"/a/b/1":2}`

**Empty containers** (empty array/object) → **stored as leaves**.

### 11.3) `unflatten`

```cpp
static bool unflatten(const knst_json& flat, knst_json& out) noexcept;
```

`{"/a/b/0":1,"/a/b/1":2}` → `{"a":{"b":[1,2]}}`

**Pointer token** that is numeric → array; otherwise object.

---

## 🎯 12) Parser

### 12.1) `parse` (DOM)

```cpp
static bool parse(const char* data, size_t size, knst_json& out,
                  knst_json_error* err = nullptr,
                  const knst_json_parse_options& opt = {}) noexcept;

static bool parse_cstr(const char* cstr, knst_json& out,
                       knst_json_error* err = nullptr,
                       const knst_json_parse_options& opt = {}) noexcept;
```

**`out` changes only on success.** On failure, `out` untouched.

**Iterative** — `dom_sink` explicit frame stack. 512 (default) depth limit but no stack overflow.

### 12.2) `parse_prefix`

```cpp
static bool parse_prefix(const char* data, size_t size, knst_json& out,
                         size_t& consumed, knst_json_error* err = nullptr,
                         const knst_json_parse_options& opt = {}) noexcept;
```

Parses the first value, `consumed` = bytes used (trailing whitespace included). For concatenated/streamed values.

### 12.3) `parse_sax`

```cpp
struct knst_json_sax_base {
    bool null_value();
    bool boolean(bool);
    bool integer(int64_t);
    bool uinteger(uint64_t);
    bool number_float(double);
    bool string(const char*, size_t);
    bool key(const char*, size_t);
    bool start_object();
    bool end_object();
    bool start_array();
    bool end_array();
};

template<class Handler>
static bool parse_sax(const char* data, size_t size, Handler& handler,
                      knst_json_error* err = nullptr,
                      const knst_json_parse_options& opt = {}) noexcept;
```

**No tree is built.** Handler callbacks invoked. Returning `false` → `handler_aborted` error.

### 12.4) `parse_lines` (NDJSON / JSON Lines)

```cpp
template<class Callback>
static bool parse_lines(const char* data, size_t size, Callback&& cb,
                        knst_json_error* err = nullptr,
                        const knst_json_parse_options& opt = {}) noexcept;
```

**Callback:** `bool cb(size_t line_index, knst_json&& value)`

- **Blank lines skipped** (whitespace only)
- If `cb` returns false, **parse stops**, returns true
- Syntax error → **error line/offset** (input line, column)
- Line numbers tracked

### 12.5) File load

```cpp
#if !defined(KNST_JSON_NO_FILE_IO)
static bool load_file_utf8(const char* path, knst_json& out, ...) noexcept;
static bool load_file_u16(const char16_t* path, size_t n, knst_json& out, ...) noexcept;
template<class Callback>
static bool load_lines_utf8(const char* path, Callback&& cb, ...) noexcept;
static bool parse_stream(std::FILE* f, knst_json& out, ...) noexcept;
#endif
```

**`load_lines_utf8`** — processes NDJSON files line by line in **constant memory**.

### 12.6) `allow_bom`

UTF-8 BOM (`0xEF 0xBB 0xBF`) skipped automatically.

### 12.7) Invalid syntax caught by parser

- **`01`** — leading zero → `invalid_number`
- **`+1`** — plus sign → `unexpected_char`
- **`.5`** — leading dot → `invalid_number`
- **`1.`** — trailing dot → `invalid_number`
- **`1e`** — missing exponent → `invalid_number`
- **`tru`** — truncated literal → `invalid_literal`
- **Control char in string** → `control_char_in_string`
- **Invalid UTF-8** → `invalid_utf8` (strict)
- **Lone surrogate** → `lone_surrogate` (unless `replace_lone_surrogates`)

---

## 🎯 13) Serializer

### 13.1) `dump`

```cpp
bool dump(knst_json_buffer& out,
          const knst_json_dump_options& opt = {},
          knst_json_error* err = nullptr) const noexcept;

bool dump_append(knst_json_buffer& out,
                 const knst_json_dump_options& opt = {},
                 knst_json_error* err = nullptr) const noexcept;
```

**`dump`** — `out` cleared. Empty on failure.

**`dump_append`** — appends to `out`. On failure **rolls back to previous size**.

### 13.2) `dump_lines`

```cpp
bool dump_lines(knst_json_buffer& out,
                const knst_json_dump_options& opt = {},
                knst_json_error* err = nullptr) const noexcept;
```

- Array → **one element per line**
- Everything else → single line

**Compact mode forced** (`indent=0`).

### 13.3) `dump_stream`

```cpp
bool dump_stream(bool (*sink)(void* ctx, const char* data, size_t len),
                 void* ctx,
                 const knst_json_dump_options& opt = {},
                 knst_json_error* err = nullptr) const noexcept;
```

**Streaming:** writes to sink in 64 KiB chunks. Sink returns true → continue, false → `io_error`.

**Memory:** Not the full doc in RAM; only 64 KiB chunks.

### 13.4) File save

```cpp
#if !defined(KNST_JSON_NO_FILE_IO)
bool dump_file(std::FILE* f, ...) const noexcept;
bool save_file_utf8(const char* path, const knst_json_dump_options& opt = {},
                     bool atomic = true, knst_json_error* err = nullptr) const noexcept;
bool save_file_u16(const char16_t* path, size_t n, ...) const noexcept;
#endif
```

**`atomic = true` (default):**
1. Unique temp file (`<path>.<pid><counter>.tmp~`)
2. Write + fflush + fsync
3. POSIX: `chmod` copies original permission to temp
4. `rename(tmp, path)` — POSIX `rename`, Windows `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`
5. POSIX: `open(parent_dir, O_RDONLY)` + `fsync(fd)` — makes rename durable
6. On failure `unlink(tmp)`

**Non-atomic** — writes directly to path. Partial file on failure.

---

## 🔧 14) String Bridges

### 14.1) `knst_byte_string` (auto-detect)

Active if **`__has_include("knst_byte_string.hpp")`**:

```cpp
explicit knst_json(const knst_byte_string& s) noexcept;
bool set_string(const knst_byte_string& s) noexcept;
knst_byte_string to_byte_string() const noexcept;
bool dump(knst_byte_string& out, ...) const noexcept;      // byte string overload
static bool parse(const knst_byte_string& s, ...) noexcept;
```

### 14.2) `knst_c16string` (auto-detect)

```cpp
explicit knst_json(const knst_c16string& s) noexcept;
bool set_string(const knst_c16string& s) noexcept;         // UTF-16 → UTF-8
knst_c16string to_c16string() const noexcept;              // UTF-8 → UTF-16
static bool load_file(const knst_c16string& path, ...) noexcept;
bool save_file(const knst_c16string& path, ...) const noexcept;
```

### 14.3) `std::string` (opt-in)

```cpp
#if defined(KNST_JSON_ENABLE_STD_STRING)
knst_json(const std::string& s) noexcept;
bool set_string(const std::string& s) noexcept;
std::string to_std_string() const;
bool get(std::string& out) const;
std::string dump_string(...) const;
static bool parse(const std::string& s, ...) noexcept;
const knst_json* find(const std::string& key) const noexcept;
knst_json& operator[](const std::string& key) noexcept;
// + string_view bridge (optional)
#endif
```

### 14.4) Macros

| Macro | Effect |
|---|---|
| **`KNST_JSON_NO_KNST_STRINGS`** | Disable `knst_byte_string` / `knst_c16string` bridges |
| **`KNST_JSON_HAS_BYTE_STRING`** | Force-enable `knst_byte_string` bridge |
| **`KNST_JSON_HAS_C16STRING`** | Force-enable `knst_c16string` bridge |
| **`KNST_JSON_ENABLE_STD_STRING`** | Enable `std::string` / `std::string_view` bridge |

---

## 🎯 15) Configuration Macros

Define **before** including the header:

| Macro | Default | Effect |
|---|---|---|
| **`KNST_JSON_MALLOC(n)`** | `std::malloc` | Allocator hook |
| **`KNST_JSON_REALLOC(p, n)`** | `std::realloc` | Realloc hook |
| **`KNST_JSON_FREE(p)`** | `std::free` | Free hook |
| **`KNST_JSON_NO_CHARCONV`** | unset | Don't use `std::from_chars/to_chars` |
| **`KNST_JSON_NO_KNST_STRINGS`** | unset | Disable `knst_*string` bridge |
| **`KNST_JSON_HAS_BYTE_STRING`** | auto | Force `knst_byte_string` |
| **`KNST_JSON_HAS_C16STRING`** | auto | Force `knst_c16string` |
| **`KNST_JSON_ENABLE_STD_STRING`** | unset | Enable `std::string` bridge |
| **`KNST_JSON_NO_FILE_IO`** | unset | Disable all file I/O |
| **`KNST_JSON_NO_SIMD`** | unset | Disable SSE2/NEON paths |
| **`KNST_JSON_MAX_RECURSION`** | `1000` | Limit for recursive helpers |
| **`KNST_SMALL_SIZE_CLASS`** | unset | Use `inline` instead of `forceinline` |

---

## 🌍 16) Platform Differences

| Concern | Windows | Linux | Android |
|---|---|---|---|
| **Native path** | `wchar_t` | `char` | `char` |
| **File open** | `_wfopen` | `fopen` | `fopen` |
| **Atomic rename** | `MoveFileExW(REPLACE_EXISTING\|WRITE_THROUGH)` | `rename` + `fsync(dir)` | `rename` + `fsync(dir)` |
| **fsync** | `_commit(_fileno(f))` | `fsync(fileno(f))` | `fsync(fileno(f))` |
| **SIMD** | SSE2 | SSE2 / NEON | NEON |
| **`__has_include`** | MSVC/Clang | GCC/Clang | Clang |
| **`_BitScanForward`** | ✅ | `__builtin_ctz` | `__builtin_ctz` |

**JSON contents are the same across all platforms.** Only file path handling and SIMD detection differ.

---

## ⚠️ 17) Common Gotchas

### 1) `KNST_JSON_MAX_RECURSION` limits recursive helpers

`compare`, `hash`, `merge_patch`, `walk`, `select` (query), `validate` (schema), `diff_rec` — all **recursive**.

**Default 1000.** For deeper trees:
- **`compare` → 0** (treated as equal — can be **WRONG**)
- **`hash` → partial hash**
- **`walk` → false**
- **`validate` → fail("depth")**

**Parser, serializer, destructor, `operator==`, `copy_from`, `count_nodes` are ITERATIVE — unaffected by this limit.**

### 2) `operator==` and `compare` differ

`==` is iterative, `1 == 1.0` is true.
`compare` is recursive, total order, `1 < 1.0` is false (equal as numbers).

**Use `compare` for `std::sort`.** Use `operator==` for equality.

### 3) SSO threshold = 14 bytes

**≤14 bytes** → no heap. **15+ bytes** → heap alloc. Long strings go through pool allocator (`KNST_JSON_MALLOC`).

### 4) Hash index threshold = 8

**≤8 members** → `O(n)` linear scan. **9+ members** → hash table.

Small objects aren't slow, but there's a sudden **rebuild on the 9th member**. Fine for tiny objects.

### 5) Duplicate key policy

**Default `last_wins`** — parser takes the last value.

`first_wins` → first value stays, later values parsed into the `discard` slot (thrown away).

`error` → `duplicate_key` error.

### 6) `make_object()` / `make_array()` can return null

Returns **null knst_json** on allocation failure. Check with `is_null()`. `make_array(initializer_list)` returns null on failure.

### 7) Copy constructor failure → null

```cpp
knst_json a = ...;
knst_json b = a;      // fail → b is null, no error indication!
```

Use `copy_from()` to catch:
```cpp
knst_json b;
if (!b.copy_from(a)) { /* allocation failed */ }
```

### 8) `operator=` copy failure → null

```cpp
knst_json a = ..., b = ...;
b = a;   // fail → b becomes null (old b lost)
```

**Why?** Temp copy + swap pattern. Even in failure, `b = null`.

### 9) `knst_json::parse` leaves `out` unchanged on failure

```cpp
knst_json j = /* some value */;
if (!knst_json::parse("invalid", j)) {
    // j still has old value!
}
```

### 10) `strtod_c` locale workaround

Checks `std::localeconv()->decimal_point`. On TR/DE systems, `.` → `,` is substituted so strtod works. **`format_double` also does locale-check** — `,` in output is converted to `.`.

### 11) `-0.0` becomes `"0"` in canonical mode

`format_double_jcs`:
```cpp
if (d == 0) { buf[0] = '0'; return 1; }
```

RFC 8785: `-0` → `"0"`. Correct.

**Regular mode** `format_double` → `"-0.0"` (depending on std::to_chars or snprintf output).

### 12) `canonical = true` overrides other settings

```cpp
knst_json_dump_options o;
o.canonical = true;
o.indent = 4;              // ignored
o.sort_keys = false;       // ignored → forced true
o.ensure_ascii = true;     // ignored → forced false
```

### 13) `escape_html` escapes U+2028/U+2029

JavaScript `\u2028` and `\u2029` (line separators) break inside `<script>`. `escape_html = true` escapes them to `\u2028`/`\u2029`.

### 14) `escape_slash` for XSS

`/` → `\/`. For some legacy JSONP/XSS protection layers. Not needed in modern browsers but useful as defense-in-depth.

### 15) `apply_patch` non-atomic = partial state

```cpp
doc.apply_patch(patch, &err, /*atomic=*/false);
// On failure, doc may be partially patched
```

**Use atomic=true in prod** (default).

### 16) `merge_patch` also non-atomic

```cpp
doc.merge_patch(patch);          // fail → partial state
doc.merge_patch_atomic(patch);   // fail → unchanged
```

### 17) `test` op — `1 == 1.0` passes

RFC 6902 "mathematical equality". No int vs float distinction.

**For byte-level test:** use your own `compare`.

### 18) Schema `pattern` / `format` NOT evaluated

```cpp
knst_json schema;
knst_json::parse_cstr(R"({"pattern": "^\\d+$"})", schema);
// This schema passes ANY string! pattern isn't evaluated.
```

**Check yourself.**

### 19) Schema external `$ref` NOT supported

```cpp
{"$ref": "http://example.com/schema.json"}   // not resolved
{"$ref": "#/definitions/foo"}                // ✅ local works
```

### 20) `pointer_slot` does NOT overwrite

`pointer_set` overwrites, but `pointer_slot` only creates **missing** intermediates. Preserves existing.

```cpp
j.pointer_slot("/a/b");   // a, b created if missing; existing value unchanged
```

### 21) `pointer_insert` vs `pointer_set`

- **`pointer_set`** — target exists → overwrite; missing → create
- **`pointer_insert`** — RFC 6902 "add": array **shift**, object **set**

```cpp
knst_json arr = knst_json::make_array();
arr.push_back(knst_json(10));
arr.push_back(knst_json(20));

arr.pointer_insert("/0", knst_json(5));
// arr = [5, 10, 20]  (shift)
```

### 22) Object `pointer_set` has no "add" behavior

`pointer_set("/new_key", val)` → appends new key at end. No reordering.

### 23) `pointer_remove` doesn't support `-`

`-` is array **append** only. For removal, use an explicit index.

### 24) `walk` visits empty containers too

`{}` or `[]` → callback invoked, but no children.

### 25) `flatten` stores empty containers as leaves

`{"a": {}, "b": []}` → `{"/a": {}, "/b": []}`

### 26) `unflatten` makes arrays if pointer token is numeric

`{"/0": 1, "/1": 2}` → `[1, 2]` (array, not object)

Mixed:
`{"/a/0": 1, "/b": 2}` → `{"a": [1], "b": 2}` (a is array, top is object)

**Note:** `{"/0/a": 1}` → `[{"a": 1}]` (0 numeric → array element)

### 27) `parse_lines` line numbers and offsets

Callback `(index, value)` — index = **zero-based line index** (increments even when blank lines skipped).

**Error offset** — byte offset in original input + line number.

### 28) `load_lines_utf8` constant memory

**Whole file not loaded into RAM.** 64 KiB chunks read, parsed line by line. For very large NDJSON.

### 29) Atomic save temp file size

Atomic save creates a **temp file as large as the full file**. For huge JSON, you may need **2× disk space**.

### 30) `save_file` preserves file permission (POSIX only)

Before `rename`, POSIX `chmod`s original permission onto temp. Windows ACLs behave differently.

### 31) `.tmp~` suffix collision

**Unique temp** — `<path>.<pid><counter>.tmp~`. Counter increments within a process, pid differs across processes. Almost certainly unique.

**Extremely rare edge case:** if pid is recycled AND counter reset → collision. Not seen in practice.

### 32) `strict UTF-8` validation

`validate_utf8 = true` (default) → invalid byte → `invalid_utf8` error.

**Set false** → invalid bytes accepted as-is, written into string. Dump applies `invalid_utf8` policy.

### 33) Dump invalid UTF-8 policy

- **`error`** — fail
- **`replace`** — U+FFFD (`\xEF\xBF\xBD` or `\uFFFD`)
- **`ignore`** — pass byte as-is (produces invalid UTF-8 output)

**Default `error`.** `replace` recommended in prod.

### 34) `non_finite` policy

NaN/Infinity in dump:
- **`error`** — fail (`non_finite_number`)
- **`null_value`** — writes `null`
- **`literal`** — `NaN`, `Infinity`, `-Infinity` (invalid JSON!)

### 35) `canonical = true` forces non_finite = error

Canonical JSON RFC 8785 **does not accept NaN/Infinity**. Fails on non-finite values.

### 36) Integer overflow caught by parser

```cpp
// "18446744073709551616" (uint64 max + 1)
// → overflow flag set, e10 incremented, falls to double
// → precision loss
```

**`> INT64_MAX` but `≤ UINT64_MAX`** → `unsigned_integer` exact.

### 37) `object` key order follows `set` order

```cpp
j["a"] = 1;
j["b"] = 2;
j["a"] = 3;   // a's value becomes 3, a stays in first position
```

**Rename or erase+set** → new key appended at end.

### 38) `sort_keys` rebuilds index

`sort_keys()` always **rebuilds the hash index** (index changes). Large objects cost **O(n log n)** sort + **O(n)** rebuild.

### 39) `remove_if` on objects by predicate signature

```cpp
j.remove_if([](const knst_json& v) { ... });          // array element
j.remove_if([](const knst_json_member& m) { ... });   // object member
```

**`if constexpr` dispatch** — predicate signature determines which type it applies to.

### 40) `scratch()` is thread_local

```cpp
j["nonexistent"]["also"]["nope"] = 42;
```

Chained `operator[]` returns `scratch()` (thread_local null) on each failed step. **Write is a no-op** (goes into null, discarded).

**`null_ref()` is a static const** — for const `operator[]`.

### 41) `str::append` aliasing

```cpp
knst_json s = "abc";
// internally: s.append_string(s.c_str(), s.length())  → still works
```

`str::append` **detects aliasing** and works safely.

### 42) `make_object(initializer_list)` returns null on failure

```cpp
knst_json j = knst_json::make_object({
    {"a", knst_json(1)},
    {"b", knst_json(2)},   // if allocation fails, whole j = null
});
```

### 43) `update()` self-update is a no-op

```cpp
j.update(j);   // OK, returns true, no-op
```

### 44) `pointer_remove` invalidates parent pointers

After removing an array element, **parent pointers may be invalid** (realloc). Re-acquire.

### 45) `find_pointer` with `""` returns `this`

```cpp
j.find_pointer("");   // returns this
j.find_pointer("/");  // last token is "", empty key
```

### 46) JSON Patch "move" with nested target rejected

```json
[
    {"op":"move", "from":"/a", "path":"/a/b"}
]
```

`path` inside `from` → `patch_invalid`. Correct per RFC 6902.

### 47) `escape_html` escapes single quote

`'` → `\u0027`. Safe inside `<script>`.

### 48) Very large `indent`

`indent_char × depth × size` → huge memory. `indent = 100`, size = 10,000 → 100 MB+. Use `indent = 2-4`.

### 49) `dump_lines` on non-array → single line

```cpp
knst_json obj = ...;
obj.dump_lines(out);   // single line, not JSON Lines format
```

### 50) `count_nodes` on deep tree

Iterative but **allocates** for its stack buffer. On huge trees, `O(n)` memory (pointer list).

---

## 📊 18) Performance

| Operation | Complexity | Note |
|---|---|---|
| **Parse (small)** | O(n) | 3-10× faster with SSE2 |
| **Parse (large)** | O(n) | ~1-5 MB/s per CPU core |
| **Dump** | O(n) | Write burst |
| **String copy** | O(len) | SSO ≤14 → no heap alloc |
| **Array push_back** | Amortized O(1) | Capacity ×2 |
| **Array insert(idx)** | O(n) | memmove shift |
| **Object set (new)** | Amortized O(1) | Hash index |
| **Object find** | O(1) amortized | Hash; ≤8 linear |
| **Object erase** | O(n) | Hash rebuild |
| **Object sort_keys** | O(n log n) | + hash rebuild |
| **deep copy** | O(n) | Iterative |
| **operator==** | O(n) | Iterative |
| **compare** | O(n log k) | k = avg container size |
| **hash** | O(n) | |
| **merge_patch** | O(n) | Recursive |
| **JSON Pointer** | O(path) | Token parse |
| **JSON Patch** | O(ops × path) | Each op O(path) |
| **JSONPath** | O(n × path) | Worst case |
| **Schema validate** | O(n × schema) | |
| **Flatten** | O(n) | Recursive |
| **Walk** | O(n) | Recursive |

**Typical latencies:**
- **Parse 1 KB JSON:** 5-20 μs
- **Parse 1 MB JSON:** 5-20 ms
- **Dump 1 MB:** 3-10 ms
- **`j["key"]`:** ~50 ns (hash hit)
- **`j.value_at("/a/b", 0)`:** ~200 ns

**Optimizations:**
- **`KNST_JSON_HAS_FP_CHARCONV`** — `std::from_chars`/`to_chars` makes double 2-3× faster
- **`KNST_SMALL_SIZE_CLASS`** — `inline` → `forceinline` (larger binary, faster hot path)
- **Buffer reuse** — allocate `knst_json_buffer` once, reuse
- **Streaming dump** — memory savings
- **SSO string** — short keys never hit heap
- **`reserve_members(n)`** — pre-alloc for large objects
- **`reserve(n)`** — pre-alloc for large arrays

---

## ✅ 19) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Use copy_from() to catch allocation failure
knst_json dst;
if (!dst.copy_from(src)) {
    std::cerr << "Copy failed (OOM)\n";
    return;
}

// 2. Atomic patch
knst_json_error err;
if (!doc.apply_patch(patch, &err, /*atomic=*/true)) {
    std::cerr << "Patch failed: " << err.message() << "\n";
}

// 3. Atomic file save
j.save_file(u"config.json", knst_json_dump_options::pretty(2), /*atomic=*/true);

// 4. In production: reject invalid UTF-8
knst_json_dump_options o;
o.invalid_utf8 = knst_json_invalid_utf8::error;   // default
o.non_finite = knst_json_non_finite::error;       // default

// 5. Canonical for signature/hash
auto jcs = knst_json_dump_options::canonical_form();
knst_json_buffer out;
j.dump(out, jcs);
uint64_t hash = knst_json_detail::hash_bytes64(out.data(), out.size());

// 6. Strict parse options
knst_json_parse_options opt;
opt.max_depth = 64;
opt.max_input_size = 10 * 1024 * 1024;   // 10 MB cap
opt.validate_utf8 = true;
opt.duplicate_keys = knst_json_duplicate_keys::error;

// 7. Stream dump for huge docs
j.dump_stream(my_sink, my_ctx);

// 8. JSON Lines for large arrays
knst_json::load_lines_utf8("big.ndjson", [](size_t i, knst_json&& v) {
    // constant memory
    return true;
});
```

### ❌ Don't

```cpp
// 1. Ignoring copy constructor failures
knst_json a = ...;
knst_json b = a;   // fail → b is null, silently!
// ✅ use copy_from()

// 2. Non-atomic patch in prod
doc.apply_patch(patch, &err, /*atomic=*/false);   // ❌ partial state risk

// 3. Use pointer after pointer_remove
const knst_json* p = arr.find_pointer("/0");
arr.pointer_remove("/0");
// p is invalid!   ❌
// ✅ re-acquire

// 4. Confusing compare() result with ==
if (j.compare(knst_json(1)) == 0) { /* j == 1 */ }   // ✅ OK
if (j == knst_json(1)) { /* j == 1 */ }              // ✅ OK
// Both correct, but compare has recursion limit risk

// 5. Assuming pattern/format are in schema
knst_json::parse(R"({"pattern": "^\\d+$"})", schema);
j.validate(schema);   // ❌ pattern NOT evaluated
// ✅ check yourself

// 6. Inside <script> without escape_html
<span id="data">{json}</span>   // ❌ </script> breakout
// ✅ escape_html = true

// 7. Non-finite in canonical
// ✅ canonical=true already errors, catch it

// 8. skip_verify (not TLS but...) — nope, JSON has nothing like this
```

---

## 🔥 20) Real-World Examples

### Example 1: Config file

```cpp
knst_json cfg;
knst_json_error err;

if (!knst_json::load_file(u"/etc/myapp/config.json", cfg, &err)) {
    std::cerr << "Config load failed: " << err.message() << "\n";
    return 1;
}

int64_t port = cfg.value_at("/net/port", 8080);
knst_c16string host_c16 = cfg["net"]["host"].to_c16string();

std::cout << "Listening on " << host_c16 << ":" << port << "\n";
```

### Example 2: API response parse

```cpp
knst_http_client c;
knst_http_response r;
knst_network_error nerr;

knst_http_client::options o;
o.timeout_ms = 5000;

if (!c.get(u"https://api.example.com/users", o, r, nerr)) return;

knst_json data;
knst_json_error jerr;

if (!knst_json::parse(r.body.data(),
                      static_cast<size_t>(r.body.length()),
                      data, &jerr)) {
    std::cerr << "JSON parse: " << jerr.message() << "\n";
    return;
}

// iterate array
for (const knst_json& user : data) {
    std::cout << user["name"].c_str() << " (id="
              << user.value("id", int64_t{-1}) << ")\n";
}
```

### Example 3: Config diff + patch

```cpp
knst_json old_cfg, new_cfg;
knst_json::load_file(u"old.json", old_cfg);
knst_json::load_file(u"new.json", new_cfg);

knst_json patch;
if (knst_json::make_patch(old_cfg, new_cfg, patch)) {
    knst_json_buffer out;
    patch.dump(out, knst_json_dump_options::pretty(2));
    std::cout << out.data() << "\n";
    // RFC 6902 patch; another system can apply it
}
```

### Example 4: JSON Schema validation

```cpp
knst_json schema;
knst_json::parse_cstr(R"({
    "type": "object",
    "required": ["name", "age"],
    "properties": {
        "name": {"type": "string", "minLength": 1},
        "age":  {"type": "integer", "minimum": 0, "maximum": 150}
    }
})", schema);

knst_json user;
knst_json::parse_cstr(R"({"name": "Alice", "age": 30})", user);

knst_json_validation v;
if (!user.validate(schema, &v)) {
    std::cerr << "Invalid: " << v.keyword
              << " at " << (v.path.data() ? v.path.data() : "") << "\n";
}
```

### Example 5: NDJSON stream processing

```cpp
knst_json::load_lines_utf8("huge.ndjson", [](size_t line, knst_json&& v) {
    // Constant memory, one object at a time
    if (!v.is_object()) return true;
    std::cout << line << ": " << v.value("id", int64_t{-1}) << "\n";
    return true;
});
```

### Example 6: Canonical JSON for signing

```cpp
knst_json doc;
knst_json::parse_cstr(R"({"b":2,"a":1})", doc);

// RFC 8785 JCS
auto jcs = knst_json_dump_options::canonical_form();
knst_json_buffer out;
doc.dump(out, jcs);
// out: {"a":1,"b":2}

// Sign
knst_byte_string sig = sign_hmac(secret, out.data(), out.size());

// Send doc + sig; verifier runs same JCS
```

### Example 7: JSON Pointer query

```cpp
knst_json cfg;
knst_json::parse_cstr(R"({
    "servers": [
        {"name":"a","url":"https://a.example.com"},
        {"name":"b","url":"https://b.example.com"}
    ]
})", cfg);

// First server URL
const knst_json* url = cfg.find_pointer("/servers/0/url");
if (url && url->is_string()) {
    std::cout << "Primary: " << url->c_str() << "\n";
}

// All servers
for (const knst_json& s : cfg["servers"]) {
    std::cout << s["name"].c_str() << ": " << s["url"].c_str() << "\n";
}
```

### Example 8: Lenient parse (config)

```cpp
knst_json cfg;
knst_json_parse_options opt = knst_json_parse_options::lenient();

const char* config_text = R"(
    // HTTP config
    {
        server: 'localhost',     // unquoted key + single quote
        port: 8080,
        timeout: Infinity,       // NaN allowed
    }
)";

if (knst_json::parse_cstr(config_text, cfg, nullptr, opt)) {
    std::cout << "host: " << cfg["server"].c_str() << "\n";
    std::cout << "port: " << cfg.value("port", int64_t{0}) << "\n";
}
```

---

## 📌 21) API Contract Summary

```cpp
// knst_json.hpp — header-only, C++17

// Enums
enum class knst_json_type : uint8_t { null_value, boolean, integer, unsigned_integer, floating, string, array, object };
enum class knst_json_errc : uint8_t { /* 22 values */ };
enum class knst_json_duplicate_keys : uint8_t { last_wins, first_wins, error };
enum class knst_json_invalid_utf8 : uint8_t { error, replace, ignore };
enum class knst_json_non_finite : uint8_t { error, null_value, literal };

// Error / options
struct knst_json_error { knst_json_errc code; size_t offset; uint32_t line, column; ... };
struct knst_json_parse_options { /* ~12 fields, lenient() */ };
struct knst_json_dump_options { /* ~12 fields, pretty(), canonical_form() */ };
struct knst_json_validation { bool valid; const char* keyword; knst_json_buffer path; };

// Buffer / pointer list
using knst_json_buffer = knst_json_detail::basic_buffer<char>;
using knst_json_ptr_list = knst_json_detail::basic_buffer<const knst_json*>;

// SAX
struct knst_json_sax_base { /* 11 virtuals */ };

// Core value type
class knst_json {
public:
    // Construction / destruction
    knst_json() noexcept;
    knst_json(std::nullptr_t) noexcept;
    knst_json(bool) noexcept;
    template<class T> knst_json(T) noexcept;      // integral / floating
    knst_json(const char*, size_t) noexcept;
    knst_json(const knst_json&) noexcept;         // deep, fail → null
    knst_json(knst_json&&) noexcept;
    ~knst_json();

    // Factory
    static knst_json make_array() noexcept;
    static knst_json make_object() noexcept;
    static knst_json make_string(const char*, size_t) noexcept;
    static knst_json make_array(std::initializer_list<knst_json>) noexcept;
    static knst_json make_object(std::initializer_list<std::pair<const char*, knst_json>>) noexcept;

    // Type queries
    type get_type() const noexcept;
    bool is_null/bool/integer/float/number/string/array/object/container/scalar/integral_value() const noexcept;
    const char* type_name() const noexcept;

    // Scalar access
    template<class T> bool get(T&) const noexcept;
    bool get_string(const char*&, size_t&) const noexcept;
    const char* c_str(const char* fallback = "") const noexcept;
    size_t length() const noexcept;
    template<class T> T value_or(T) const noexcept;
    template<class T> T value(const char* key, T) const noexcept;
    template<class T> T value_at(const char* ptr, T) const noexcept;

    // Scalar mutation
    void reset() noexcept;
    void set_null/set_bool/set_double/set_int64/set_uint64(...) noexcept;
    bool set_string(const char*, size_t) noexcept;
    bool set_string_u16(const char16_t*, size_t) noexcept;
    bool append_string(const char*, size_t) noexcept;

    // Array
    bool reserve(size_t) noexcept;
    bool push_back(knst_json&&) noexcept;
    bool push_back(const knst_json&) noexcept;
    bool insert(size_t, knst_json&&) noexcept;
    bool erase_at(size_t) noexcept;
    bool erase_range(size_t, size_t) noexcept;
    bool append(const knst_json&) noexcept;
    bool append_move(knst_json&) noexcept;
    template<class Pred> size_t remove_if(Pred) noexcept;
    void sort_array() noexcept;
    size_t unique_array() noexcept;
    knst_json* at(size_t) noexcept;
    knst_json* begin/end() noexcept;

    // Object
    bool set(const char*, size_t, knst_json&&) noexcept;
    knst_json* find(const char*, size_t) noexcept;
    knst_json* get_or_insert(const char*, size_t) noexcept;
    bool erase(const char*, size_t) noexcept;
    bool rename_key(const char*, const char*) noexcept;
    bool sort_keys() noexcept;
    bool update(const knst_json&) noexcept;
    member_range members() noexcept;

    // Whole-value
    bool copy_from(const knst_json&) noexcept;
    bool operator==(const knst_json&) const noexcept;
    bool operator<(const knst_json&) const noexcept;
    static int compare(const knst_json&, const knst_json&) noexcept;
    static int compare_numbers(const knst_json&, const knst_json&) noexcept;
    uint64_t hash() const noexcept;
    size_t count_nodes() const noexcept;

    // Merge Patch
    bool merge_patch(const knst_json&) noexcept;
    bool merge_patch_atomic(const knst_json&) noexcept;
    static bool make_merge_patch(const knst_json&, const knst_json&, knst_json&) noexcept;

    // Pointer
    const knst_json* find_pointer(const char*, size_t) const noexcept;
    const knst_json* find_pointer_uri(const char*) const noexcept;
    knst_json* pointer_slot(const char*, size_t) noexcept;
    bool pointer_set(const char*, knst_json&&) noexcept;
    bool pointer_insert(const char*, size_t, knst_json&&) noexcept;
    bool pointer_remove(const char*, size_t, knst_json* = nullptr) noexcept;
    bool pointer_replace(const char*, knst_json&&) noexcept;

    // Patch
    bool apply_patch(const knst_json&, knst_json_error* = nullptr, bool atomic = true) noexcept;
    static bool make_patch(const knst_json&, const knst_json&, knst_json&) noexcept;

    // Query
    bool select(const char*, size_t, knst_json_ptr_list&, knst_json_error* = nullptr) const noexcept;

    // Schema
    bool validate(const knst_json&, knst_json_validation* = nullptr) const noexcept;

    // Traversal
    template<class F> bool walk(F&&) const;
    bool flatten(knst_json&) const noexcept;
    static bool unflatten(const knst_json&, knst_json&) noexcept;

    // Parse
    static bool parse(const char*, size_t, knst_json&, knst_json_error* = nullptr,
                       const knst_json_parse_options& = {}) noexcept;
    static bool parse_cstr(const char*, knst_json&, ...) noexcept;
    static bool parse_prefix(const char*, size_t, knst_json&, size_t&, ...) noexcept;
    template<class H> static bool parse_sax(const char*, size_t, H&, ...) noexcept;
    template<class CB> static bool parse_lines(const char*, size_t, CB&&, ...) noexcept;
#if !defined(KNST_JSON_NO_FILE_IO)
    static bool load_file_utf8(const char*, knst_json&, ...) noexcept;
    static bool load_file_u16(const char16_t*, size_t, knst_json&, ...) noexcept;
    template<class CB> static bool load_lines_utf8(const char*, CB&&, ...) noexcept;
    static bool parse_stream(std::FILE*, knst_json&, ...) noexcept;
#endif

    // Dump
    bool dump(knst_json_buffer&, const knst_json_dump_options& = {}, knst_json_error* = nullptr) const noexcept;
    bool dump_append(knst_json_buffer&, ...) const noexcept;
    bool dump_lines(knst_json_buffer&, ...) const noexcept;
    bool dump_stream(bool (*)(void*, const char*, size_t), void*, ...) const noexcept;
#if !defined(KNST_JSON_NO_FILE_IO)
    bool dump_file(std::FILE*, ...) const noexcept;
    bool save_file_utf8(const char*, ..., bool atomic = true) const noexcept;
    bool save_file_u16(const char16_t*, size_t, ..., bool atomic = true) const noexcept;
#endif
};
```

---

## 🎯 What It Brings You

`knst_json` gives you serious advantages in these situations:

- ✅ **Header-only** — no dependencies
- ✅ **RFC 8259 compliant** — parse, dump, canonical
- ✅ **RFC 6901 JSON Pointer** — get, set, insert, remove
- ✅ **RFC 6902 JSON Patch** — atomic + diff
- ✅ **RFC 7386 JSON Merge Patch** — atomic + generate
- ✅ **RFC 8785 JCS canonical** — for signing/hash
- ✅ **Exception-free** — `noexcept` + `bool`, builds with `-fno-exceptions`
- ✅ **Allocator hooks** — override `KNST_JSON_MALLOC/REALLOC/FREE`
- ✅ **SSO string ≤14** — 24-byte value
- ✅ **int64 + uint64 exact** — `> INT64_MAX` preserved exactly
- ✅ **Locale-independent** — works on TR/DE systems
- ✅ **Shortest round-trip double** — `0.1` → `"0.1"`, no garbage
- ✅ **Iterative parser** — 10,000 depth OK
- ✅ **Iterative destructor** — same
- ✅ **SIMD scan** — 3-10× faster string parse
- ✅ **Atomic file save** — crash-safe
- ✅ **JSONPath subset** — `$`, `.`, `..`, `*`, `[n]`, `[?()]`
- ✅ **JSON Schema draft-07 subset** — `type`, `required`, `properties`, `allOf`, ...
- ✅ **Streaming** — `dump_stream`, `parse_sax`, `load_lines_utf8`
- ✅ **Lenient mode** — comments, trailing commas, NaN, single quotes, unquoted keys
- ✅ **NDJSON / JSON Lines** — `parse_lines`, `load_lines_utf8`, `dump_lines`
- ✅ **`knst_c16string` / `knst_byte_string` bridge** — UTF-16 / byte string auto
- ✅ **`std::string` bridge** — via `KNST_JSON_ENABLE_STD_STRING`
- ✅ **KernelNucleusT integration** — with `knst_network`, `knst_process`, `knst_prn`

---