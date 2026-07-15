# KernelNucleusT

Modern C++17 library pack. High-performance, customizable, fully header-only.

## Philosophy

KernelNucleusT is built on the principle of **optimal balance between performance and safety**. Core principles:

- **noexcept + bool return** — Most functions return `bool` instead of throwing exceptions. This boosts performance while giving the developer full control over error handling.

- **Force Inline** — By default, functions are compiled with `force_inline`. No call overhead — code is copied directly to the call site. Result: faster execution, larger binary.

- **Adjustable Binary Size** — Define `KNST_SMALL_SIZE_CLASS` to replace `force_inline` with standard `inline`. The compiler decides, usually using `call` to reach the function. Smaller binary, potentially slower. Your choice.

- **Situational Flexibility** — Critical, frequently-used functions may include extra optimizations at the cost of binary size. This is not a bug — it's a deliberate trade-off.

This philosophy applies to all current and future libraries in the pack.

## 🚀 Libraries

### knst_c16string

`char16_t`-based, high-performance string class.
**By default, stores up to 22 bytes (10 characters) on the stack; overflows to heap.**

**Features:**
- **Multi-Character Support** — Constructors and functions directly accept `char16_t`, `char`, `wchar_t`, `char32_t`.
- **STL Compatible** — Works seamlessly with `std::string`, `std::wstring`, `std::u16string`, `std::u32string` and their views.
- **Macro Customization:**
  - `KNST_C16STRING_DEACTIVE_COW` — Disables Copy-On-Write
  - `KNST_C16_STRING_USING_ATOMIC_COW` — Makes COW counter thread-safe
  - `KNST_C16STRING_ALIGN_64` / `KNST_C16STRING_ALIGN_32` — Adjusts class alignment and SSO capacity
- **Safety** — Passed comprehensive test suite.

### knst_memory

Instance-based memory pool allocator. Thread-safe option available. All future libraries will include `knst_memory` support, just like `knst_c16string`.

### Goals

I plan to deliver cross-platform, highly customizable libraries such as `knst_vector`, `knst_functional`, `knst_regex`, `knst_window` — designed to fit any use case with maximum configurability.

## 📦 Installation

**Header-only** — no build configuration required.

```cpp
#include "../include/KernelNucleusT.hpp"  // That's it