# KernelNucleusT

Modern C++17 library package. High-performance, customizable, cross-platform supported.

## Philosophy

KernelNucleusT is built on the principle of **optimal balance between performance and safety**. Core principles:

- **noexcept + bool return** — Most functions return `bool` instead of throwing exceptions. This boosts performance while giving the developer full control over error handling.

- **Force Inline** — By default, functions are compiled with `force_inline`. No call overhead — code is copied directly to the call site. Result: faster execution, larger binary.

- **Adjustable Binary Size** — Define `KNST_SMALL_SIZE_CLASS` to replace `force_inline` with standard `inline`. The compiler decides, usually using `call` to reach the function. Smaller binary, potentially slower. Your choice.

- **Situational Flexibility** — Critical, frequently-used functions may include extra optimizations at the cost of binary size. This is not a bug — it's a deliberate trade-off.

This philosophy applies to all current and future libraries in the pack.

## 🚀 Libraries

### knst_window  ---BETA---


  * Simple Example


    ```cpp
        #include <iostream>
        #include "../../include/KernelNucleusT.hpp"

        int main() {
            
            KnstWindowSources::Init();

            knst_window window(300, 300, u"Test");
            
            window.creation_and_show();

            while (!window.is_should_close()) {

                knst_window_event_system::block_pool_event();

                if (window.get_window_event_handle().type == KNST_CLOSE_WINDOW || 
                    window.get_window_event_handle().type == KNST_DISCONNECT) {
                    std::cout << "cleaning window..." << std::endl;
                    window.destroy();
                    window.should_close();
                }
            window.clear_temporary_events(); // resets the event
            }

            std::cout << "cleaning sources..." << std::endl;
            KnstWindowSources::CleanUp();

            return 0;
        }
        ```

  *


**A comprehensive window management library for Windows and Linux.**

**Features:**
- **Performance and Size Balance — Aims to provide the best balance between performance and binary size**
- **Modern C++ — Offers clean code writing with modern C++ features**
- **Event-Driven — Handles events similar to operating system event loops**
- **Security — Currently in beta stage, security is continuously improving**


**Usage and Important Notes**
 — First, you must call KnstWindowSources::Init() at the beginning of your program to initialize the library. This prepares and initializes the static resources defined for the library.
 Additionally, this call internally retrieves the list of monitors based on the operating system, meaning it automatically calls knst_display::refresh_monitors() internally. Therefore, you don't need to call ::refresh_monitors() separately to read monitor information from knst_display. However, you can call it again if you need to refresh the list (e.g., after connecting a new monitor).

 — Some events are restricted on Wayland. Due to the Wayland compositor's security model, certain events are blocked.

 — Wayland Limitations: Due to Wayland's security model, some functions like move() and set_cursor_pos_*() are not supported.

**About Event Handling**
 — Events that come to your window are stored in the event structure inside the window object. You can access it as const using the get_window_event_handle() function and check the structures within using switch-case and if-else statements.

**Loops**
  — There are 3 main loop types available in knst_window_event_system:
  — block_pool_event == triggers only when an event arrives
  — non_block_pool_event == processes the event if available, returns immediately if not
  — timeout_pool_event == waits for the specified duration, processes the event if available, returns immediately if not

**OpenGL and Vulkan**
— With knst_window_opengl_content and knst_window_vulkan_content, you can use drawing APIs with your window.

You can disable the title bar with `#define KNST_DISABLE_TITLE_BAR` and enable custom title bars (currently available for OpenGL, Vulkan support coming soon...).
Also, it is recommended to start with BeginFrame() for OpenGL.


 ```cpp
 #define KNST_WINDOW_USING_KNST_TITLE_BAR_WHITE_MODERN
 
 #define KNST_WINDOW_USING_KNST_TITLE_BAR_BLUE_MODERN
 
 #define KNST_WINDOW_USING_KNST_TITLE_BAR_FUTURISTIC
 
 #define KNST_WINDOW_USING_KNST_TITLE_BAR_SUNSET_GLOW
 ```


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

### knst_byte_string

An alternative to knst_c16string based on 'unsigned char', designed to store binary data and UTF-8 values.
 -  **UTF-8 Support:** Stores as raw bytes
 -  **Security** — Successfully passed all tests
 -  **Binary Safe:** Can contain \0 (null) bytes


### knst_vector
Contains methods optimized for the library's internal needs. New methods will be added for user-facing usage.

 - **Security** — Successfully passed all tests
 

### knst_memory

Instance-based memory pool allocator. Thread-safe option available. All future libraries will include `knst_memory` support, just like `knst_c16string`.

### Goals

 In the future, I plan to bring cross-platform, comprehensive versions of structures such as knst_vector, knst_functional, knst_regex, and a GUI framework. I strive to make them highly customizable for every use case.



## 📦 Installation

Detailed CMake information is provided in the 'cmake_compile_helper.txt' file.

```cpp
#include "../include/KernelNucleusT.hpp"
```

🤝 Contributing

  # Use the Issues page for bug reports and feature requests
  # Feel free to contact me directly for any special requests or to share feedback



📄 License

  MIT License — Freely use, develop, and share.
