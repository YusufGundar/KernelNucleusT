// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


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


/* ============================================================================
   PLATFORM SELECTION
   ----------------------------------------------------------------------------
   Only ONE of the top-level platforms must be selected.
   These are normally defined by CMake based on the target system.
   ============================================================================ */

// #define KNST_USING_PLATFORM_WINDOWS       // Windows (Win32 / Win64)
// #define KNST_USING_PLATFORM_LINUX         // Any Linux distro
// #define KNST_USING_PLATFORM_ANDROID       // Android NDK build

// If KNST_USING_PLATFORM_LINUX is selected, one display server must be chosen:
// #define KNST_USING_LINUX_PLATFORM_X11     // X11 (X.Org)
// #define KNST_USING_LINUX_PLATFORM_WAYLAND // Wayland


/* ============================================================================
   GRAPHICS BACKEND (optional)
   ----------------------------------------------------------------------------
   Only needed for windowing / rendering.
   ============================================================================ */


// #define KNST_USING_VULKAN                 // Enable the Vulkan backend

// On Android, additional backend must be specified:

// #define KNST_PLATFORM_ANDROID_VULKAN      // Android Vulkan


/* ============================================================================
   STRING – knst_c16string
   ============================================================================ */

// Disables Copy-on-Write. Every copy becomes a deep copy (safer, slower).
// #define KNST_C16STRING_DEACTIVE_COW

// Makes the COW reference counter atomic (thread-safe reads).
// #define KNST_C16_STRING_USING_ATOMIC_COW

// SSO buffer size class. Larger = more stack usage but fewer heap allocations.
// Pick at most ONE. Default is alignas(8) → 10 chars on stack.
// #define KNST_C16STRING_ALIGN_64           // 30 chars on stack, alignas(64)
// #define KNST_C16STRING_ALIGN_32           // 14 chars on stack, alignas(32)


/* ============================================================================
   FUNCTION – knst_function
   ============================================================================ */

// Stack storage for the callable (in bytes). Larger values avoid heap
// allocation for bigger lambdas. Default: 64 bytes.
// #define KNST_FUNCTION_INLINE_SIZE 64


/* ============================================================================
   MEMORY – knst_pool_allocator
   ============================================================================ */

// Makes the pool allocator thread-safe (adds a std::mutex).
// #define KNST_MEMORY_POOL_USE_MUTEX

// Marks small helper functions as inline (smaller binary, maybe slower).
// #define KNST_SMALL_SIZE_CLASS


/* ============================================================================
   THREAD POOL – knst_thread_pool
   ============================================================================ */

// Max temporary threads spawned when the queue is full. Default: 32.
// #define KNST_OVERFLOW_MAX 32

// Max jobs the queue can hold. Default: 1024.
// #define KNST_QUEUE_MAX 1024

// Default worker count. 0 = auto-detect (hardware_concurrency()).
// #define KNST_DEFAULT_WORKER_COUNT 0

// How many jobs a worker pulls per lock. Default: 8.
// #define KNST_WORKER_BATCH_SIZE 8


/* ============================================================================
   IMAGE LOADER – knst_image_loader
   ============================================================================ */

// Maximum width/height in pixels. Larger images are rejected with
// KNST_IMAGE_ERR_LIMIT. Default: 16384 (16K). Raising this increases
// the maximum output buffer accordingly (w*h*4 bytes per image).
// #define KNST_IMAGE_MAX_DIMENSION 16384

// How many custom decoders can be registered via
// knst_image_loader::register_decoder(). Default: 16.
// #define KNST_IMAGE_MAX_CUSTOM_DECODERS 16


/* ============================================================================
   WINDOW – knst_window
   ============================================================================ */

// Initial delay before key auto-repeat starts, in milliseconds.
// #define KNST_WINDOW_KEY_REPEAT_DELAY 500

// Interval between auto-repeat events, in milliseconds.
// #define KNST_WINDOW_KEY_REPEAT_INTERVAL 30

// Do not call the frame callback on every cycle (only on events).
// #define KNST_DISABLE_REDRAW_ON_EVENT_MANAGER

// Remove the native title bar (custom drawing expected).
// #define KNST_DISABLE_TITLE_BAR


/* ============================================================================
   INTERNAL – do not touch
   ============================================================================ */
#include "knst_definitions.hpp"