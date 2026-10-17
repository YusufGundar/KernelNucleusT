/*
----------------------------
knst_settings.hpp
----------------------------

   KernelNucleusT — compile-time configuration and platform selection.

   Define macros here (before including any KernelNucleusT header) to
   customize the library. Every macro is optional — sensible defaults
   are used when they are not defined.
*/

#pragma once


// You can add or modify macros either at the very bottom of the file or, within the `int main` function, at the very top of the project.


/* ============================================================================
   PLATFORM SELECTION
   ============================================================================ */

// #define KNST_USING_PLATFORM_WINDOWS       // Windows (Win32 / Win64)
// #define KNST_USING_PLATFORM_LINUX         // Any Linux distro
// #define KNST_USING_PLATFORM_ANDROID       // Android NDK build

// #define KNST_USING_LINUX_PLATFORM_X11     // X11 (X.Org) — Linux only
// #define KNST_USING_LINUX_PLATFORM_WAYLAND // Wayland  — Linux only

// #define KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND  // Both X11 + Wayland in one binary (runtime-selected)


/* ============================================================================
   GPU BACKENDS
   ============================================================================ */

// #define KNST_WINDOW_USE_OPENGL            // Enable OpenGL backend (GLAD + EGL/WGL)
// #define KNST_WINDOW_USE_VULKAN            // Enable Vulkan backend


// Simple structures class aligment 

// #define KNST_CLASS_ALIGN_64
// #define KNST_CLASS_ALIGN_32


/* ============================================================================
   STRING – knst_c16string
   ============================================================================ */

// #define KNST_C16STRING_DEACTIVE_COW       // Disable copy-on-write (safer, slower)
// #define KNST_C16_STRING_USING_ATOMIC_COW  // Make COW refcounter atomic (thread-safe reads)



/* ============================================================================
   FUNCTION – knst_function
   ============================================================================ */

// #define KNST_FUNCTION_INLINE_SIZE 64      // Stack storage for the callable (bytes)


/* ============================================================================
   MEMORY – knst_pool_allocator
   ============================================================================ */

// #define KNST_MEMORY_POOL_USE_MUTEX        // Make pool allocator thread-safe (adds mutex)
// #define KNST_SMALL_SIZE_CLASS             // Small helpers become inline (smaller binary)


/* ============================================================================
   THREAD POOL – knst_thread_pool
   ============================================================================ */

// #define KNST_OVERFLOW_MAX 32              // Max temporary threads when queue full
// #define KNST_QUEUE_MAX 1024               // Max jobs the queue can hold
// #define KNST_DEFAULT_WORKER_COUNT 0       // Default workers (0 = auto-detect CPU cores)
// #define KNST_WORKER_BATCH_SIZE 8          // Jobs a worker pulls per lock


/* ============================================================================
   IMAGE LOADER – knst_image_loader
   ============================================================================ */

// #define KNST_IMAGE_MAX_DIMENSION 16384    // Max width/height in pixels (16K default)
// #define KNST_IMAGE_MAX_CUSTOM_DECODERS 16 // Max custom decoders registrable


/* ============================================================================
   WINDOW – knst_window
   ============================================================================ */

// #define KNST_WINDOW_KEY_REPEAT_DELAY 200      // Initial delay before auto-repeat (ms)
// #define KNST_WINDOW_KEY_REPEAT_INTERVAL 20    // Interval between auto-repeat events (ms)
// #define KNST_DISABLE_TITLE_BAR                // Remove native title bar (custom drawing)
// #define KNST_MAX_HELD_KEYS 8                  // Max simultaneously-held keys tracked (auto-repeat)


/* ============================================================================
   WINDOW – knst_window (touch screen)
   ----------------------------------------------------------------------------
   Opt-in on all platforms (Android always on). Without
   KNST_WINDOW_ENABLE_TOUCH_SCREEN, touch falls back to mouse emulation.
   ============================================================================ */

// #define KNST_WINDOW_ENABLE_TOUCH_SCREEN
// #define KNST_MAX_TOUCH_POINTS 10          // Max simultaneous touch points tracked


/* ============================================================================
   WINDOW – CSD measurement (Windows only)
   ----------------------------------------------------------------------------
   Hit-test distances and layout metrics used by the Windows WM_NCHITTEST
   handler and the CSD auto-detect path.

   NOTE: These are currently hard-coded with plain `#define` inside
   knst_window_win32_event_manager.hpp. Overriding them here only works
   if that block is wrapped with `#ifndef` guards first — otherwise the
   preprocessor will complain about redefinition.

   Left here as documentation / future-proofing.
   ============================================================================ */

// #define KNST_RESIZE_BORDER 8        // Resize hit-test border (pixels)
// #define KNST_CORNER_SIZE 12         // Corner grab zone (pixels)
// #define KNST_BUTTON_WIDTH 48        // CSD title bar button width (pixels)
// #define KNST_RESIZE_TIMER_ID 0xA5F1 // WM_TIMER id used during drag-resize


/* ============================================================================
   MACRO NORMALIZATION
   ----------------------------------------------------------------------------
   Some macros are tested with `#if KNST_FOO` (numeric). If FOO is not
   defined, the preprocessor silently treats it as 0, but -Wundef warns.

   We normalize ONLY those numeric-tested macros.

   DO NOT normalize macros that are tested with `#if defined(...)`:
   defining them to 0 would still make `defined(...)` return true and
   incorrectly enable the corresponding code path.

   Verified `defined()`-tested macros (left untouched here):
       • KNST_USING_PLATFORM_WINDOWS / _LINUX / _ANDROID
         (handled inside knst_definitions.hpp)
       • KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND
       • KNST_WINDOW_USE_OPENGL / _VULKAN
       • KNST_WINDOW_ENABLE_TOUCH_SCREEN
       • KNST_DISABLE_TITLE_BAR
   ============================================================================ */

/* ── Linux display server — tested with numeric `#if` ────────────────────── */
#ifndef KNST_USING_LINUX_PLATFORM_X11
    #define KNST_USING_LINUX_PLATFORM_X11 0
#endif
#ifndef KNST_USING_LINUX_PLATFORM_WAYLAND
    #define KNST_USING_LINUX_PLATFORM_WAYLAND 0
#endif

/* ============================================================================
   INTERNAL — do not touch
   ============================================================================ */
#include "knst_definitions.hpp"

/* ── Catch misconfiguration at compile time ──────────────────────────────── */
#if defined(__linux__) && !defined(__ANDROID__) && !defined(KNST_USING_PLATFORM_ANDROID) \
    && !KNST_USING_LINUX_PLATFORM_X11 && !KNST_USING_LINUX_PLATFORM_WAYLAND
    #error "Linux: KNST_USING_LINUX_PLATFORM_X11 or _WAYLAND must be defined (CMake: -DKNST_LINUX_PLATFORM=X11|Wayland)"
#endif