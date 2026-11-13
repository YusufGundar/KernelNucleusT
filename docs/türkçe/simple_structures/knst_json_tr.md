# knst_json — KernelNucleusT JSON Module

Selamlar! 👋 `knst_json`, KernelNucleusT'nin **RFC 8259 uyumlu, exception-free, allocator-hookable, binary-safe** JSON value type'ı. Parser, serializer ve etrafında geniş bir toolbox: JSON Pointer (RFC 6901), JSON Patch (RFC 6902), JSON Merge Patch (RFC 7386), JSONPath subset, JSON Schema draft-07 subset, flatten/unflatten, walk, hash, canonical (RFC 8785 JCS), NDJSON, atomic file save. **Hepsi tek header'da, `noexcept` + `bool`/`result` dönüşümü.**

**Kısaca:** Header-only, C++17+. Windows / Linux / Android / macOS / iOS hepsi destekli. `knst_json` **24 byte** (SSO ≤14 byte string). int64 + uint64 tam hassasiyet, double ile diğer sayılar. Parser, serializer, destructor, deep copy, `operator==` — hepsi **iterative** (stack overflow yok). Kalan yardımcılar (compare, hash, merge_patch, walk, query, schema, diff) **recursive ama `KNST_JSON_MAX_RECURSION` (default 1000) ile sınırlı**.

---

## 🎯 Honesty Matrix — Ne Var, Ne Yok?

### Genel

| Özellik | Windows | Linux | Android | macOS | iOS |
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
| **JSON5 tam destek** | ❌ subset | ❌ | ❌ | ❌ | ❌ |
| **YAML / TOML / CBOR / MessagePack** | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Schema `pattern` (regex)** | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Schema `format`** | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Schema external `$ref`** | ❌ sadece lokal | ❌ | ❌ | ❌ | ❌ |
| **JSONPath full RFC 9535** | ❌ subset | ❌ | ❌ | ❌ | ❌ |
| **Exceptions** | ❌ hiç atılmaz | ❌ | ❌ | ❌ | ❌ |

**Kritik notlar:**
- **`knst_json` 24 byte.** SSO string'ler ≤14 byte stack'te. 15+ byte heap'e geçer.
- **Object hash index threshold = 8.** ≤8 üye → linear scan. 9+ → hash table.
- **Recursive helpers** (`compare`, `hash`, `merge_patch`, `walk`, `query`, `schema`, `diff`) `KNST_JSON_MAX_RECURSION` ile sınırlı. Aşarsa davranış: compare → 0 (eşit sayılır, dikkat!), hash → partial, walk → false.
- **Parser, serializer, destructor, deep copy, `operator==` ITERATIVE.** Sonsuz derinlikte bile stack overflow yok.
- **`pattern` ve `format` schema'da evaluate EDİLMEZ.** Sadece varlığı kabul edilir.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **SSO string** | 24-byte value, ≤14 byte string stack'te |
| **Binary-safe** | `\0` gömülü string, byte array |
| **int64 + uint64 exact** | `> INT64_MAX` değerler tam hassasiyetle |
| **Locale-independent** | TR/DE sistemlerde `1.5` doğru (nokta) |
| **Shortest round-trip double** | `1.5` → `"1.5"`, `0.1` → `"0.1"` (17-digit garbage yok) |
| **Iterative parser** | 10.000 derinlikte bile çökme yok |
| **Iterative destructor** | Aynı şekilde |
| **SIMD scan** | SSE2/NEON/SWAR ile string parse 3-10× hızlı |
| **Exact-size containers** | Parser kapatırken `realloc` ile küçültür |
| **Zero-copy string write** | Parser doğrudan value'ya yazar |
| **Streaming output** | 64 KiB chunk callback |
| **Atomic file save** | Unique temp + fsync + rename + dir fsync |
| **JSON Pointer + Patch + Merge Patch** | RFC 6901/6902/7386 tam destek |
| **JSONPath subset** | `$`, `.name`, `..`, `*`, `[n]`, `[-n]`, `[a:b:c]`, `[1,2,'x']`, `[?(@.x < 10)]` |
| **JSON Schema draft-07** | Local `$ref`, all/any/one/not, if/then/else, dependencies |
| **Flatten / Unflatten** | `{a:{b:1}}` ↔ `{"/a/b":1}` |
| **Walk** | Pointer-aware tree traversal |
| **Lenient mode** | Comments, trailing commas, NaN, single quotes, unquoted keys |
| **RFC 8785 JCS** | Canonical JSON (imza/hash için) |
| **Duplicate key policies** | `last_wins` / `first_wins` / `error` |
| **UTF-8 validation** | Strict mode, invalid byte → error |
| **Lone surrogate handling** | `\uD800` alone → error veya U+FFFD |
| **Allocator hooks** | `KNST_JSON_MALLOC/REALLOC/FREE` |

---

## 🚀 Hızlı Başlangıç

### Basit parse + dump

```cpp
#include "KernelNucleusT.hpp"   // knst_json.hpp otomatik

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

// Okuma
const knst_json* port = cfg.find_pointer("/net/port");
int64_t p = port->value_or(int64_t{0});

// Kısayol
int64_t p2 = cfg.value_at("/net/port", 1234);

// Yazma
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

## 📋 Genel API

| Kategori | Metodlar |
|---|---|
| **Tip kontrol** | `get_type`, `is_null/bool/integer/float/number/string/array/object/container/integral_value` |
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

## 🎯 1) Enum'lar ve Options

### 1.1) `knst_json_type`

```cpp
enum class knst_json_type : uint8_t {
    null_value = 0,
    boolean,
    integer,           // int64_t
    unsigned_integer,  // uint64_t, SADECE > INT64_MAX için
    floating,          // double
    string,
    array,
    object
};
```

**`unsigned_integer` ne zaman?** Sadece `uint64_t > INT64_MAX` iken. Değeri `≤ INT64_MAX` ise `integer` kullanılır.

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
    handler_aborted,       // SAX handler false döndü
    pointer_syntax,        // JSON Pointer / JSONPath syntax
    path_not_found,
    patch_invalid,         // JSON Patch malformed
    patch_test_failed,     // "test" op fail
    schema_violation
};
```

**`err.message()`** okunabilir string döner.

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
    bool allow_bom = true;                     // UTF-8 BOM skip
    bool allow_comments = false;               // // ve /* */
    bool allow_trailing_commas = false;
    bool allow_nan_inf = false;                // NaN, Infinity
    bool allow_single_quotes = false;          // 'string', 'key'
    bool allow_unquoted_keys = false;          // {key: 1}
    bool replace_lone_surrogates = false;      // \uD800 → U+FFFD

    static knst_json_parse_options lenient() noexcept;   // hepsini aç
};
```

**`lenient()`** — comments + trailing commas + NaN/Inf + single quotes + unquoted keys + lone surrogate replacement. JSON5 benzeri config dosyaları için.

### 1.5) `knst_json_duplicate_keys`

```cpp
enum class knst_json_duplicate_keys : uint8_t {
    last_wins,    // son değer kazanır (default)
    first_wins,   // ilk değer kalır (parser sonrakini discard eder)
    error         // duplicate key → duplicate_key error
};
```

### 1.6) `knst_json_dump_options`

```cpp
struct knst_json_dump_options {
    uint32_t indent = 0;              // 0 = compact
    char     indent_char = ' ';       // ' ' veya '\t'
    bool     sort_keys = false;
    bool     ensure_ascii = false;    // tüm non-ASCII → \uXXXX
    bool     trailing_newline = false;
    uint32_t max_depth = 512;

    knst_json_invalid_utf8 invalid_utf8 = error;    // error / replace / ignore
    knst_json_non_finite   non_finite = error;      // error / null_value / literal

    // v2:
    bool     escape_slash = false;    // '/' → "\/"
    bool     escape_html = false;     // '<' '>' '&' '\'' U+2028/2029 → \uXXXX
    bool     canonical = false;       // RFC 8785 JCS (diğer ayarları override eder)

    static knst_json_dump_options pretty(uint32_t n = 2) noexcept;
    static knst_json_dump_options canonical_form() noexcept;
};
```

**`canonical = true` ne yapar?** RFC 8785 JCS:
- `indent = 0`
- `sort_keys = true` (UTF-16 code unit sırası)
- `ensure_ascii = false` (ham UTF-8)
- `escape_slash = false`, `escape_html = false`
- `trailing_newline = false`
- `invalid_utf8 = error`, `non_finite = error`
- Sayılar ECMAScript Number::toString formatında

### 1.7) `knst_json_validation`

```cpp
struct knst_json_validation {
    bool valid = true;
    const char* keyword = "";    // fail eden keyword
    knst_json_buffer path;        // JSON Pointer (instance location)
};
```

---

## 🎯 2) `knst_json` — Value Type

### 2.1) Boyut ve SSO

```cpp
static_assert(sizeof(knst_json) <= 32);   // gerçek: 24 byte
```

**Bellek düzeni:**
- `type_` (1 byte) + 7 padding
- `Val v_` (union, 16 byte) — en büyük üye: `arr_t` (`knst_json* p; uint32_t n; uint32_t c;`)
- **Total: 24 byte**

**SSO string ≤14 byte** → stack'te (`sb[16]` union member). 15+ byte → heap (`p - 8` = 4-byte capacity + 4-byte padding).

### 2.2) Fabrika

```cpp
static knst_json make_array() noexcept;
static knst_json make_object() noexcept;
static knst_json make_string(const char* s, size_t n) noexcept;
static knst_json make_array(std::initializer_list<knst_json> items) noexcept;
static knst_json make_object(std::initializer_list<std::pair<const char*, knst_json>> items) noexcept;
```

**Not:** `make_object()` **null döner** allocation fail'de. `make_array()` null döner. Initializer list constructor'ları `null` döner fail'de.

### 2.3) Constructor'lar

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

**Copy constructor allocation fail ederse null olur.** `copy_from()` kullanarak failure'ı yakalayabilirsin.

### 2.4) Type query

```cpp
type get_type() const noexcept;
bool is_null() const noexcept;
bool is_bool() const noexcept;
bool is_integer() const noexcept;      // integer veya unsigned_integer
bool is_float() const noexcept;
bool is_number() const noexcept;
bool is_string() const noexcept;
bool is_array() const noexcept;
bool is_object() const noexcept;
bool is_container() const noexcept;    // array || object
bool is_scalar() const noexcept;
bool is_integral_value() const noexcept;   // integer veya float tam değerli
const char* type_name() const noexcept;
```

**`is_integral_value`** — float ama tam sayı (`1.0` → true). Schema `type: integer` kontrolünde kullanılır.

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

**`get_string`** — pointer doğrudan value'nun storage'ına işaret eder (NUL-terminated). Value mutate/destroy edilene kadar geçerli.

**`get(T&)` integral** — değer `T` aralığına sığmıyorsa false.

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

**`set_*` allocation gerektirmeyen türler** için `noexcept` ve void döner. **String için `bool`** (allocation gerekir).

### 2.7) Size / empty / clear

```cpp
size_t size() const noexcept;      // array size veya object member count, string ise length
bool empty() const noexcept;
size_t capacity() const noexcept;
void clear() noexcept;             // container/string boşalt, type korunur
void shrink_to_fit() noexcept;     // capacity → size
```

**`clear()`** — array'de capacity korunur, object'te index rebuild edilir. Type değişmez.

---

## 📦 3) Array Operations

```cpp
bool reserve(size_t n) noexcept;
bool push_back(knst_json&& v) noexcept;
bool push_back(const knst_json& v) noexcept;
knst_json* push_back_slot() noexcept;      // null slot push, pointer döner
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
void sort_array() noexcept;                // compare() default
size_t unique_array() noexcept;             // O(n²) worst, hash prefilter

knst_json* begin() noexcept;
knst_json* end() noexcept;
const knst_json* begin() const noexcept;
const knst_json* end() const noexcept;
```

**`null` → array conversion:** `reserve`, `push_back`, `insert` çağrıldığında tip null ise otomatik array olur. Diğer tipte ise false döner.

**`insert(idx, ...)` shift:** `memmove` ile sonraki elemanlar kaydırılır. **O(n)** maliyet.

**`unique_array()`** — hash prefilter (optional) + O(n²) worst case. Elementler sıralı değilse yavaş ama doğru.

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

- **≤8 üye** → linear scan (`O(n)` per lookup)
- **9+ üye** → hash index (open addressing, capacity = 2×members min 16)
- **Rebuild:** her büyümede `2×` threshold aşıldığında
- **Rebuild fail** → falls back to linear scan (`ix = nullptr`), object still valid

**Hash seed:** `hash_seed()` ASLR-derived. Hash collision DoS payload'ları önceden hazırlanamaz.

### 4.2) Insertion order

**Object members insertion order'da saklanır.** İterasyon bu sırayı korur. `sort_keys()` ile bytewise sıralayabilirsin. Dump'ta `sort_keys=true` ile değiştirebilirsin (dump sırasında).

### 4.3) `set` davranışı

- Tip null ise → object
- Tip object değilse → false
- Key yoksa → yeni üye (sona)
- Key varsa → value overwrite (yer aynı kalır)

### 4.4) `get_or_insert`

Key yoksa null value ile create eder, pointer döner. `operator[]` bunu kullanır.

### 4.5) `rename_key`

Yer değiştirmeden key'i değiştirir. `old` yoksa veya `new` zaten varsa false.

### 4.6) `update`

`other` object'teki tüm üyeleri `this`'e deep copy eder. Varolan key'ler **overwrite edilir**.

---

## 🔄 5) Deep Copy / Compare / Hash

### 5.1) `copy_from` (iterative, strict)

```cpp
bool copy_from(const knst_json& o) noexcept;
```

**Iterative** — explicit work stack. Başarısızsa **`*this` unchanged**. Başarıysa `o`'nun derin kopyası.

**Neden önemli?** Copy constructor `copy_from` kullanır ama fail durumunda null'a düşer. `copy_from` direkt kullanarak failure'ı yakalayabilirsin.

### 5.2) `operator==` (iterative)

```cpp
bool operator==(const knst_json& o) const noexcept;
```

**Semantik:**
- Sayısal karşılaştırma exact: `1 == 1.0`, `1 == 1u`, `> INT64_MAX` doğru
- `NaN != NaN` (floating sayılar için doğru)
- Object: **order-insensitive** (aynı key-value set'i)
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

**Sayılar arası sıralama:** exact, NaN en başta.

**Recursive** — `max_rec` aşılırsa 0 döner (bu **tehlike**: farklı deep tree'ler "eşit" sanılabilir).

### 5.4) `hash` (recursive)

```cpp
uint64_t hash() const noexcept;
```

**Uyumlu `operator==` ile:**
- `1` ve `1.0` aynı hash
- Object order-independent

**Kullanım:** Hash map key, deduplication, cache.

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

**Format:** `/a/b/0` — `/` ayrılmış token'lar. `~0` → `~`, `~1` → `/`.

**`-`** — array append (sadece `pointer_insert` ve `pointer_slot`).

**`pointer_slot`** — intermediate container'ları oluşturur (null → object/array). Varolan değerleri **koru**.

**`pointer_set`** — target varsa overwrite, yoksa create (parent exists ise).

**`pointer_insert`** — RFC 6902 "add" semantics:
- Object: set (overwrite yok, sadece set)
- Array: **shift** (add at index), `-` = append

**`pointer_remove`** — RFC 6902 "remove". `removed` parametresi ile silinen value'yu dışarı alabilirsin.

**`pointer_replace`** — sadece **var olan** değeri değiştirir. Yoksa false.

---

## 🧩 7) JSON Patch (RFC 6902)

```cpp
bool apply_patch(const knst_json& patch, knst_json_error* err = nullptr,
                 bool atomic = true) noexcept;
static bool make_patch(const knst_json& from, const knst_json& to,
                        knst_json& out) noexcept;
```

**`atomic = true` (default):** `*this`'in kopyasına uygular. Başarıysa commit eder. Failure'da **`*this` unchanged**.

**`atomic = false`:** Doğrudan `*this`'e uygular. Failure'da **partial state**.

### 7.1) Desteklenen op'lar

| Op | Davranış |
|---|---|
| **`add`** | Target var → overwrite (object) veya shift (array); yoksa create |
| **`remove`** | Target silinir |
| **`replace`** | Target varsa overwrite; yoksa fail |
| **`test`** | Target value `==` verilen value |
| **`move`** | `from` → `to` (target nested olamaz) |
| **`copy`** | `from` deep copy → `to` |

**`test` semantiği:** RFC 6902 "mathematical equality" — `1 == 1.0` pass. Byte-level değil.

### 7.2) `make_patch` / `diff_rec`

**Recursive** (`KNST_JSON_MAX_RECURSION`). Object ve array'leri diff eder. Scalarlar `replace`.

**Array diff:**
- Common prefix element-wise recurse
- Tail extra → `remove` (sondan başa, index kaymasın)
- Tail eksik → `add` (sıraya)

---

## 🎯 8) JSON Merge Patch (RFC 7386)

```cpp
bool merge_patch(const knst_json& patch) noexcept;
bool merge_patch_atomic(const knst_json& patch) noexcept;
static bool make_merge_patch(const knst_json& from, const knst_json& to,
                              knst_json& out) noexcept;
```

**RFC 7386 semantiği:**
- `patch` object değil → **target replace**
- `patch` object → target object olmalı (değilse replace)
- `patch.member = null` → **member silinir**
- `patch.member = value` → recurse

**`merge_patch`** — in-place. Failure'da partial state.
**`merge_patch_atomic`** — copy + apply + commit. Failure'da unchanged.

**`make_merge_patch`** — `from`'u `to`'ya çevirmek için minimal patch üretir.

---

## 🎯 9) JSONPath Subset

```cpp
bool select(const char* path, size_t len, knst_json_ptr_list& out,
            knst_json_error* err = nullptr) const noexcept;
bool select(const char* path, knst_json_ptr_list& out,
            knst_json_error* err = nullptr) const noexcept;
```

### 9.1) Desteklenen syntax

| Syntax | Anlamı |
|---|---|
| `$` | Root |
| `.name` | Child (identifier) |
| `['name']` / `["name"]` | Child (quoted) |
| `..name` | Recursive descent (children + self) |
| `..[*]` / `.. *` | All descendants |
| `.*` | All children |
| `[*]` | All children |
| `[n]` | Index (0-based) |
| `[-n]` | Negative index (-1 = son) |
| `[a:b]` / `[a:b:c]` | Slice (start:end:step) |
| `[1,2,'x']` | Union |
| `[?(@.x < 10)]` | Filter (existence, `==`, `!=`, `<`, `<=`, `>`, `>=`) |
| `[?(@.tags)]` | Existence check |

**NOT supported:** Functions (`length()`, `match()`), regex, script expressions, `$..` combined with filters over huge trees (performance).

### 9.2) `select` örneği

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

**Sonuç pointer'ları** `j`'nin içine işaret eder. `j` destroy/mutate edilirse invalid.

---

## 🎯 10) JSON Schema (draft-07 subset)

```cpp
bool validate(const knst_json& schema,
              knst_json_validation* result = nullptr) const noexcept;
```

### 10.1) Desteklenen keyword'ler

| Kategori | Keyword'ler |
|---|---|
| **Type** | `type` (string veya array), `enum`, `const` |
| **Number** | `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `multipleOf` |
| **String** | `minLength`, `maxLength` (codepoints) |
| **Array** | `minItems`, `maxItems`, `uniqueItems`, `items`, `additionalItems`, `contains` |
| **Object** | `minProperties`, `maxProperties`, `required`, `properties`, `additionalProperties`, `dependencies` |
| **Combinator** | `allOf`, `anyOf`, `oneOf`, `not`, `if`/`then`/`else` |
| **Ref** | `$ref` (local — `#/...` only) |

### 10.2) Desteklenmeyen

- **`pattern`** (regex) — evaluate edilmez, yok sayılır
- **`format`** (email, uri, date-time, ...) — evaluate edilmez, yok sayılır
- **External `$ref`** (`http://...`, `file://...`)
- **Draft-04 `id`** / draft-06 `exclusiveMinimum: true` vs draft-07 numeric

**Sonuç:** Schema validation geçer ama **pattern/format kontrolü yok**. Bu arada siz kontrol edin.

### 10.3) `knst_json_validation` sonucu

```cpp
knst_json_validation v;
if (!doc.validate(schema, &v)) {
    std::cout << "Schema fail: " << v.keyword << "\n";
    std::cout << "At path: " << (v.path.data() ? v.path.data() : "") << "\n";
    // ör: keyword="minimum", path="/age"
}
```

**İlk failure** yakalanır (`recorded_` flag). Path JSON Pointer.

---

## 🎯 11) Walk / Flatten / Unflatten

### 11.1) `walk`

```cpp
template<class F> bool walk(F&& fn) const noexcept(false);
```

**Callback signature:** `bool fn(const knst_json& value, const char* pointer, size_t pointer_len)`

- **Pre-order** — parent önce, sonra children
- `false` dönerse **walk durur** (return false)
- **Recursive** — `KNST_JSON_MAX_RECURSION` limit
- Her çağrıda path JSON Pointer olarak verilir

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

**Empty container** (array veya object boş) → **leaf olarak saklanır**.

### 11.3) `unflatten`

```cpp
static bool unflatten(const knst_json& flat, knst_json& out) noexcept;
```

`{"/a/b/0":1,"/a/b/1":2}` → `{"a":{"b":[1,2]}}`

**Pointer token** sayı ise array, değilse object yapılır.

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

**`out` sadece başarıda değişir.** Failure'da `out` untouched.

**Iterative** — `dom_sink` explicit frame stack. 512 (default) derinlik limit ama stack overflow yok.

### 12.2) `parse_prefix`

```cpp
static bool parse_prefix(const char* data, size_t size, knst_json& out,
                         size_t& consumed, knst_json_error* err = nullptr,
                         const knst_json_parse_options& opt = {}) noexcept;
```

İlk value'yu parse eder, `consumed` = kullanılan byte sayısı (trailing whitespace dahil). Concatenated / streamed value'lar için.

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

**Tree build edilmez.** Handler callback'leri çağrılır. `false` dönerse `handler_aborted` error.

### 12.4) `parse_lines` (NDJSON / JSON Lines)

```cpp
template<class Callback>
static bool parse_lines(const char* data, size_t size, Callback&& cb,
                        knst_json_error* err = nullptr,
                        const knst_json_parse_options& opt = {}) noexcept;
```

**Callback:** `bool cb(size_t line_index, knst_json&& value)`

- **Boş satırlar atlanır** (whitespace only)
- `cb` false dönerse **parse durur**, `true` döner
- Syntax error → **error line/offset** (input line, column)
- Line numarası track edilir

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

**`load_lines_utf8`** — NDJSON dosyasını **constant memory** ile satır satır işler.

### 12.6) `allow_bom`

UTF-8 BOM (`0xEF 0xBB 0xBF`) otomatik skip.

### 12.7) Parser'ın yakaladığı invalid syntax

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

**`dump`** — `out` cleared. Failure'da boş.

**`dump_append`** — `out`'a ekler. Failure'da previous size'a **rollback**.

### 13.2) `dump_lines`

```cpp
bool dump_lines(knst_json_buffer& out,
                const knst_json_dump_options& opt = {},
                knst_json_error* err = nullptr) const noexcept;
```

- Array → **her element bir satır**
- Diğer → tek satır

**Compact mode zorunlu** (`indent=0`).

### 13.3) `dump_stream`

```cpp
bool dump_stream(bool (*sink)(void* ctx, const char* data, size_t len),
                 void* ctx,
                 const knst_json_dump_options& opt = {},
                 knst_json_error* err = nullptr) const noexcept;
```

**Streaming:** 64 KiB chunk'lar halinde sink'e yazar. `sink` true dönerse devam, false → `io_error`.

**Bellek:** Full doc RAM'de değil, sadece 64 KiB chunk.

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
3. POSIX: `chmod` orjinal permission'ı kopyalar
4. `rename(tmp, path)` — POSIX `rename`, Windows `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`
5. POSIX: `open(parent_dir, O_RDONLY)` + `fsync(fd)` — rename durable
6. Failure'da `unlink(tmp)`

**Atomic olmayan** — doğrudan path'e yazar. Failure'da partial file.

---

## 🔧 14) String Bridges

### 14.1) `knst_byte_string` (auto-detect)

**`__has_include("knst_byte_string.hpp")`** varsa aktif:

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
// + string_view bridge (opsiyonel)
#endif
```

### 14.4) Macro'lar

| Macro | Etkisi |
|---|---|
| **`KNST_JSON_NO_KNST_STRINGS`** | `knst_byte_string` / `knst_c16string` bridge'lerini kapat |
| **`KNST_JSON_HAS_BYTE_STRING`** | `knst_byte_string` bridge'i zorla aç |
| **`KNST_JSON_HAS_C16STRING`** | `knst_c16string` bridge'i zorla aç |
| **`KNST_JSON_ENABLE_STD_STRING`** | `std::string` / `std::string_view` bridge aç |

---

## 🎯 15) Configuration Macro'ları

**Header'dan ÖNCE** define edilmeli:

| Macro | Default | Etkisi |
|---|---|---|
| **`KNST_JSON_MALLOC(n)`** | `std::malloc` | Allocator hook |
| **`KNST_JSON_REALLOC(p, n)`** | `std::realloc` | Realloc hook |
| **`KNST_JSON_FREE(p)`** | `std::free` | Free hook |
| **`KNST_JSON_NO_CHARCONV`** | unset | `std::from_chars/to_chars` kullanma |
| **`KNST_JSON_NO_KNST_STRINGS`** | unset | `knst_*string` bridge kapat |
| **`KNST_JSON_HAS_BYTE_STRING`** | auto | `knst_byte_string` zorla |
| **`KNST_JSON_HAS_C16STRING`** | auto | `knst_c16string` zorla |
| **`KNST_JSON_ENABLE_STD_STRING`** | unset | `std::string` bridge aç |
| **`KNST_JSON_NO_FILE_IO`** | unset | File I/O tamamen kapat |
| **`KNST_JSON_NO_SIMD`** | unset | SSE2/NEON path'leri kapat |
| **`KNST_JSON_MAX_RECURSION`** | `1000` | Recursive helper'lar için limit |
| **`KNST_SMALL_SIZE_CLASS`** | unset | `inline` yerine `forceinline` kullan |

---

## 🌍 16) Platform Farkları

| Konu | Windows | Linux | Android |
|---|---|---|---|
| **Native path** | `wchar_t` | `char` | `char` |
| **File open** | `_wfopen` | `fopen` | `fopen` |
| **Atomic rename** | `MoveFileExW(REPLACE_EXISTING\|WRITE_THROUGH)` | `rename` + `fsync(dir)` | `rename` + `fsync(dir)` |
| **fsync** | `_commit(_fileno(f))` | `fsync(fileno(f))` | `fsync(fileno(f))` |
| **SIMD** | SSE2 | SSE2 / NEON | NEON |
| **`__has_include`** | MSVC/Clang | GCC/Clang | Clang |
| **`_BitScanForward`** | ✅ | `__builtin_ctz` | `__builtin_ctz` |

**Tüm platformlarda JSON içeriği aynı.** Sadece file path handling ve SIMD detection farklı.

---

## ⚠️ 17) Sık Tuzaklar

### 1) `KNST_JSON_MAX_RECURSION` recursion helper'ları sınırlar

`compare`, `hash`, `merge_patch`, `walk`, `select` (query), `validate` (schema), `diff_rec` — **recursive**.

**Default 1000.** Daha derin tree'de:
- **`compare` → 0 döner** (eşit sayılır, **YANLIŞ** olabilir)
- **`hash` → partial hash**
- **`walk` → false**
- **`validate` → fail("depth")**

**Parser, serializer, destructor, `operator==`, `copy_from`, `count_nodes` ITERATIVE — bu limiti etkilemez.**

### 2) `operator==` ve `compare` farklı

`==` iteratif, `1 == 1.0` true.
`compare` recursive, total order verir, `1 < 1.0` false (eşit sayılır numeric olarak).

**`std::sort` için `compare` kullan.** `==` için `operator==`.

### 3) SSO threshold = 14 byte

**≤14 byte** → heap yok. **15+ byte** → heap alloc. Çok uzun string'lerde pool allocator (`KNST_JSON_MALLOC`) devreye girer.

### 4) Hash index threshold = 8

**≤8 üye** → `O(n)` linear scan. **9+ üye** → hash table.

Küçük object'lerde `find` yavaş değil ama **9. üyede ani rebuild** var. Çok küçük object'ler için sorun değil.

### 5) Duplicate key policy

**Default `last_wins`** — parser son değeri alır.

`first_wins` → ilk değer kalır, sonraki değer `discard` slot'a parse edilir (throw away).

`error` → `duplicate_key` error.

### 6) `make_object()` / `make_array()` null dönebilir

Allocation fail'de **null knst_json** döner. `is_null()` ile kontrol et. `make_array(initializer_list)` fail durumunda null.

### 7) Copy constructor failure → null

```cpp
knst_json a = ...;
knst_json b = a;      // fail → b is null, no error indication!
```

`copy_from()` kullanarak fail yakala:
```cpp
knst_json b;
if (!b.copy_from(a)) { /* allocation failed */ }
```

### 8) `operator=` copy failure → null

```cpp
knst_json a = ..., b = ...;
b = a;   // fail → b becomes null (old b lost)
```

**Neden?** Temp copy + swap pattern. Fail durumunda bile `b = null`.

### 9) `knst_json::parse` `out` unchanged on failure

```cpp
knst_json j = /* some value */;
if (!knst_json::parse("invalid", j)) {
    // j still has old value!
}
```

### 10) `strtod_c` locale workaround

`std::localeconv()->decimal_point` kontrol edilir. TR/DE sistemlerde `,` varsa `.` → `,` çevrilir, strtod doğru çalışsın diye. **Ama `format_double` de locale-check yapıyor** — çıktıdaki `,` → `.` çevrilir.

### 11) `-0.0` canonical mode'da "0" olur

`format_double_jcs`:
```cpp
if (d == 0) { buf[0] = '0'; return 1; }
```

RFC 8785: `-0` → `"0"`. Doğru.

**Regular mode** `format_double` → `"-0.0"` (std::to_chars veya snprintf sonucuna göre).

### 12) `canonical = true` diğer ayarları override eder

```cpp
knst_json_dump_options o;
o.canonical = true;
o.indent = 4;              // ignored
o.sort_keys = false;       // ignored → forced true
o.ensure_ascii = true;     // ignored → forced false
```

### 13) `escape_html` U+2028/U+2029 kaçırır

JavaScript `\u2028` ve `\u2029` (satır ayırıcı) `<script>` içinde hata verir. `escape_html = true` bunları `\u2028`/`\u2029` olarak kaçırır.

### 14) `escape_slash` XSS için

`/` → `\/`. Bazı eski JSONP/XSS koruma katmanları için. Modern tarayıcılarda gerek yok ama defense-in-depth.

### 15) `apply_patch` non-atomic = partial state

```cpp
doc.apply_patch(patch, &err, /*atomic=*/false);
// Fail durumunda doc yarım patch'lenmiş olabilir
```

**Prod'da atomic=true kullan** (default).

### 16) `merge_patch` da non-atomic

```cpp
doc.merge_patch(patch);          // fail → partial state
doc.merge_patch_atomic(patch);   // fail → unchanged
```

### 17) `test` op `1 == 1.0` pass eder

RFC 6902 "mathematical equality". `int` vs `float` farkı yok.

**Byte-level test istiyorsan:** kendi `compare`'ini kullan.

### 18) Schema `pattern` / `format` NOT evaluated

```cpp
knst_json schema;
knst_json::parse_cstr(R"({"pattern": "^\\d+$"})", schema);
// Bu schema her string'i pass eder! pattern kontrol edilmiyor.
```

**Kendin kontrol et.**

### 19) Schema external `$ref` NOT supported

```cpp
{"$ref": "http://example.com/schema.json"}   // resolve edilmez
{"$ref": "#/definitions/foo"}                // ✅ local works
```

### 20) `pointer_slot` **overwrite** yapmaz

`pointer_set` overwrite yapar ama `pointer_slot` sadece **eksik** intermediate'ları oluşturur. Varolanı korur.

```cpp
j.pointer_slot("/a/b");   // a, b created if missing; varolan value unchanged
```

### 21) `pointer_insert` vs `pointer_set`

- **`pointer_set`** — target exists → overwrite; missing → create
- **`pointer_insert`** — RFC 6902 "add": array'de **shift**, object'te **set**

```cpp
knst_json arr = knst_json::make_array();
arr.push_back(knst_json(10));
arr.push_back(knst_json(20));

arr.pointer_insert("/0", knst_json(5));
// arr = [5, 10, 20]  (shift)
```

### 22) Object `pointer_set` "add" davranışı yok

`pointer_set("/new_key", val)` → yeni key ekler (sona). Sıra değişmez.

### 23) `pointer_remove` `-` desteklemez

`-` sadece array **append** için. Remove için explicit index.

### 24) `walk` empty containers'ı da ziyaret eder

`{}` veya `[]` → callback çağrılır, ama child yok.

### 25) `flatten` empty containers'ı leaf olarak saklar

`{"a": {}, "b": []}` → `{"/a": {}, "/b": []}`

### 26) `unflatten` pointer token'ı sayı ise array yapar

`{"/0": 1, "/1": 2}` → `[1, 2]` (array, not object)

Karışık:
`{"/a/0": 1, "/b": 2}` → `{"a": [1], "b": 2}` (a array, top object)

**Not:** `{"/0/a": 1}` → `[{"a": 1}]` (0 numeric → array element)

### 27) `parse_lines` satır numarası ve offset

Callback `(index, value)` alır — index = **sıfırdan başlayan line index** (blank satırlar atlanmış olsa da artar).

**Error offset** — original input'taki byte offset + line numarası.

### 28) `load_lines_utf8` constant memory

**Tüm dosya RAM'e yüklenmez.** 64 KiB chunk'lar okunur, satır satır parse edilir. Çok büyük NDJSON için.

### 29) Atomic save temp file boyutu

Atomic save **tam dosya kadar temp file** oluşturur. Çok büyük JSON için **2× disk alanı** gerekebilir.

### 30) `save_file` file permission preserve (POSIX only)

POSIX `rename` öncesi `chmod` orjinal permission'ı temp'e kopyalar. Windows'ta ACL farklı işler.

### 31) `.tmp~` suffix collision

**Unique temp** — `<path>.<pid><counter>.tmp~`. Aynı process içinde counter artar, farklı process'te pid farklı. Çok yüksek ihtimalle unique.

**Extremely rare edge case:** aynı pid reuse edildiyse (pid recycling) + counter sıfırlandıysa → collision. Pratikte olmaz.

### 32) `strict UTF-8` validation

`validate_utf8 = true` (default) → invalid byte → `invalid_utf8` error.

**False yaparsan** → invalid byte olduğu gibi kabul edilir, string'e yazılır. Dump'ta `invalid_utf8` policy devreye girer.

### 33) Dump invalid UTF-8 policy

- **`error`** — fail
- **`replace`** — U+FFFD (`\xEF\xBF\xBD` veya `\uFFFD`)
- **`ignore`** — byte'ı olduğu gibi geçir (produces invalid UTF-8 output)

**Default `error`.** Prod'da `replace` önerilir.

### 34) `non_finite` policy

NaN/Infinity dump'ta:
- **`error`** — fail (`non_finite_number`)
- **`null_value`** — `null` yaz
- **`literal`** — `NaN`, `Infinity`, `-Infinity` (geçerli JSON değil!)

### 35) `canonical = true` non_finite = error forced

Canonical JSON RFC 8785 **NaN/Infinity kabul etmez**. Non-finite değer varsa **fail**.

### 36) Integer overflow parser'da yakalanır

```cpp
// "18446744073709551616" (uint64 max + 1)
// → overflow flag set, e10 arttırılır, double'a düşer
// → tam değer olarak saklanamaz (precision loss)
```

**`> INT64_MAX` ama `≤ UINT64_MAX`** → `unsigned_integer` tam doğru.

### 37) `object` key sırası `set` sırasına bağlı

```cpp
j["a"] = 1;
j["b"] = 2;
j["a"] = 3;   // a'nın value'su 3 olur, a hâlâ ilk sırada
```

**Rename veya erase sonrası yeniden set** → yeni key sona eklenir.

### 38) `sort_keys` rebuild index

`sort_keys()` her zaman **hash index rebuild** eder (index değişir). Büyük object'te **O(n log n)** sort + **O(n)** rebuild.

### 39) `remove_if` object'lerde tip imzasına göre

```cpp
j.remove_if([](const knst_json& v) { ... });          // array element
j.remove_if([](const knst_json_member& m) { ... });   // object member
```

**`if constexpr` dispatch** — predicate signature'ı hangi tip için çalışacağını belirler.

### 40) `scratch()` thread_local

```cpp
j["nonexistent"]["also"]["nope"] = 42;
```

Zincirleme `operator[]` başarısız her adımda `scratch()` döner (thread_local null). **Yazma no-op** (yazma null'a gider, discard edilir).

**`null_ref()` static const** — const operator[] için.

### 41) `str::append` aliasing

```cpp
knst_json s = "abc";
// intern: s.append_string(s.c_str(), s.length())  → hâlâ çalışır
```

`str::append` **alias detect** eder, güvenli çalışır.

### 42) `make_object(initializer_list)` fail durumunda null

```cpp
knst_json j = knst_json::make_object({
    {"a", knst_json(1)},
    {"b", knst_json(2)},   // allocation fail olursa tüm j = null
});
```

### 43) `update()` self-update no-op

```cpp
j.update(j);   // OK, returns true, no-op
```

### 44) `pointer_remove` parent destroy

Array element silindikten sonra **parent pointer'ları invalid** olabilir (realloc). Yeni pointer al.

### 45) `find_pointer` `""` empty → `this`

```cpp
j.find_pointer("");   // returns this
j.find_pointer("/");  // last token is "", empty key
```

### 46) JSON Patch "move" nested target reject

```json
[
    {"op":"move", "from":"/a", "path":"/a/b"}
]
```

`path` içinde `from` var → `patch_invalid`. Doğru davranış (RFC 6902).

### 47) `escape_html` in single quotes

`'` → `\u0027`. `<script>` içinde güvenli.

### 48) `indent` very large

`indent_char × depth × size` → huge memory. `indent = 100`, size = 10000 → 100 MB'den fazla. `indent = 2-4` kullan.

### 49) `dump_lines` on non-array → single line

```cpp
knst_json obj = ...;
obj.dump_lines(out);   // tek satır, JSON Lines formatı değil
```

### 50) `count_nodes` on deep tree

Iterative ama **stack buffer'ı için allocation** yapar. Çok büyük tree'de `O(n)` bellek (pointer list).

---

## 📊 18) Performans

| İşlem | Karmaşıklık | Not |
|---|---|---|
| **Parse (small)** | O(n) | SSE2 ile 3-10× hızlı |
| **Parse (large)** | O(n) | ~1-5 MB/s per CPU core |
| **Dump** | O(n) | Write burst |
| **String copy** | O(len) | SSO ≤14 → heap alloc |
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

**Tipik latencies:**
- **Parse 1 KB JSON:** 5-20 μs
- **Parse 1 MB JSON:** 5-20 ms
- **Dump 1 MB:** 3-10 ms
- **`j["key"]`:** ~50 ns (hash hit)
- **`j.value_at("/a/b", 0)`:** ~200 ns

**Optimizasyon:**
- **`KNST_JSON_HAS_FP_CHARCONV`** — `std::from_chars`/`to_chars` ile double 2-3× hızlı
- **`KNST_SMALL_SIZE_CLASS`** — `inline` yerine `forceinline` (binary boyutu ↑, hot path ↑)
- **Buffer reuse** — `knst_json_buffer` bir kez allocate et, tekrar kullan
- **Streaming dump** — RAM tasarrufu
- **SSO string** — kısa key'ler heap'e çıkmaz
- **`reserve_members(n)`** — büyük object için pre-alloc
- **`reserve(n)`** — büyük array için pre-alloc

---

## ✅ 19) Do / ❌ Don't

### ✅ Do

```cpp
// 1. copy_from() kullan allocation fail'i yakalamak için
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

// 4. Production: reject invalid UTF-8
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

### ❌ Yapma

```cpp
// 1. copy constructor fail kontrolü yok
knst_json a = ...;
knst_json b = a;   // fail → b is null, sessiz!
// ✅ copy_from() kullan

// 2. Non-atomic patch prod'da
doc.apply_patch(patch, &err, /*atomic=*/false);   // ❌ partial state risk

// 3. pointer_remove sonrası pointer kullan
const knst_json* p = arr.find_pointer("/0");
arr.pointer_remove("/0");
// p invalid!   ❌
// ✅ yeniden al

// 4. compare() sonucunu == ile karıştır
if (j.compare(knst_json(1)) == 0) { /* j == 1 */ }   // ✅ OK
if (j == knst_json(1)) { /* j == 1 */ }              // ✅ OK
// İkisi de doğru ama compare recursive limit riskli

// 5. pattern/format schema'da var sanmak
knst_json::parse(R"({"pattern": "^\\d+$"})", schema);
j.validate(schema);   // ❌ pattern evaluate EDİLMEZ
// ✅ kendin kontrol et

// 6. escape_html olmadan <script> içine
<span id="data">{json}</span>   // ❌ </script> breakout
// ✅ escape_html = true

// 7. Non-finite canonical'da
// ✅ canonical=true zaten error döner, catch et

// 8. skip_verify (TLS değil ama...) — yok, JSON'da böyle bir şey yok
```

---

## 🔥 20) Gerçek Kullanım Örnekleri

### Örnek 1: Config dosyası

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

### Örnek 2: API response parse

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

### Örnek 3: Config diff + patch

```cpp
knst_json old_cfg, new_cfg;
knst_json::load_file(u"old.json", old_cfg);
knst_json::load_file(u"new.json", new_cfg);

knst_json patch;
if (knst_json::make_patch(old_cfg, new_cfg, patch)) {
    knst_json_buffer out;
    patch.dump(out, knst_json_dump_options::pretty(2));
    std::cout << out.data() << "\n";
    // RFC 6902 patch, another system can apply
}
```

### Örnek 4: JSON Schema validation

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

### Örnek 5: NDJSON stream processing

```cpp
knst_json::load_lines_utf8("huge.ndjson", [](size_t line, knst_json&& v) {
    // Constant memory, one object at a time
    if (!v.is_object()) return true;
    std::cout << line << ": " << v.value("id", int64_t{-1}) << "\n";
    return true;
});
```

### Örnek 6: Canonical JSON for signing

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

### Örnek 7: JSON Pointer query

```cpp
knst_json cfg;
knst_json::parse_cstr(R"({
    "servers": [
        {"name":"a","url":"https://a.example.com"},
        {"name":"b","url":"https://b.example.com"}
    ]
})", cfg);

// İlk server URL
const knst_json* url = cfg.find_pointer("/servers/0/url");
if (url && url->is_string()) {
    std::cout << "Primary: " << url->c_str() << "\n";
}

// Tüm server'lar
for (const knst_json& s : cfg["servers"]) {
    std::cout << s["name"].c_str() << ": " << s["url"].c_str() << "\n";
}
```

### Örnek 8: Lenient parse (config)

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

## 📌 21) API Kontrat Özeti

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

## 🎯 Size Getireceği Kazanç

`knst_json` şu durumlarda ciddi avantaj sağlar:

- ✅ **Header-only** — bağımlılık yok
- ✅ **RFC 8259 tam uyumlu** — parse, dump, canonical
- ✅ **RFC 6901 JSON Pointer** — get, set, insert, remove
- ✅ **RFC 6902 JSON Patch** — atomic + diff
- ✅ **RFC 7386 JSON Merge Patch** — atomic + generate
- ✅ **RFC 8785 JCS canonical** — signature/hash için
- ✅ **Exception-free** — `noexcept` + `bool`, `-fno-exceptions` build OK
- ✅ **Allocator hook** — `KNST_JSON_MALLOC/REALLOC/FREE` özelleştir
- ✅ **SSO string ≤14** — 24-byte value
- ✅ **int64 + uint64 exact** — `> INT64_MAX` tam doğru
- ✅ **Locale-independent** — TR/DE sistemde de çalışır
- ✅ **Shortest round-trip double** — `0.1` → `"0.1"`, garbage yok
- ✅ **Iterative parser** — 10.000 derinlik OK
- ✅ **Iterative destructor** — aynı şekilde
- ✅ **SIMD scan** — 3-10× hızlı string parse
- ✅ **Atomic file save** — crash-safe
- ✅ **JSONPath subset** — `$`, `.`, `..`, `*`, `[n]`, `[?()]`
- ✅ **JSON Schema draft-07 subset** — `type`, `required`, `properties`, `allOf`, ...
- ✅ **Streaming** — `dump_stream`, `parse_sax`, `load_lines_utf8`
- ✅ **Lenient mode** — comments, trailing commas, NaN, single quotes, unquoted keys
- ✅ **NDJSON / JSON Lines** — `parse_lines`, `load_lines_utf8`, `dump_lines`
- ✅ **`knst_c16string` / `knst_byte_string` bridge** — UTF-16 / byte string otomatik
- ✅ **`std::string` bridge** — `KNST_JSON_ENABLE_STD_STRING` ile
- ✅ **KernelNucleusT entegrasyonu** — `knst_network`, `knst_process`, `knst_prn` ile

---