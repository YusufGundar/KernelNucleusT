// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
----------------------------
KernelNucleusT.hpp
----------------------------

    It is the file where all the library's core and bundled structures are included together

*/




#pragma once



#if defined(_WIN32) || defined(_WIN64)
    #ifndef KNST_DISABLE_CONSOLE_UTF8
        extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int);
        extern "C" __declspec(dllimport) int __stdcall SetConsoleCP(unsigned int);
        namespace knst_detail {
            struct knst_console_utf8_initializer {
                knst_console_utf8_initializer() noexcept {
                    ::SetConsoleOutputCP(65001);  // CP_UTF8
                    ::SetConsoleCP(65001);
                }
            };
            inline knst_console_utf8_initializer g_console_utf8_init;
        }
    #endif
#endif

#if defined(_WIN32) || defined(KNST_USING_PLATFORM_WINDOWS)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#endif

// structural
#include "knst_global_functions.hpp"
#include "knst_c16string.hpp"
#include "knst_byte_string.hpp"
#include "knst_vector.hpp"

#include "knst_file.hpp"



#include "knst_function.hpp"
#include "knst_thread_priority.hpp"
#include "knst_thread_queue.hpp"
#include "knst_thread.hpp"
#include "knst_thread_pool.hpp"

#include "knst_process.hpp"
#include "knst_devices.hpp"

#include "knst_image_loader.hpp"

// _end structural




// knst_window

#include "knst_window.hpp"

// _end knst_window




#if !defined(KNST_KEEP_X11_NONE_MACRO) 
    #ifdef None // for knst_file none parameter
        #undef None 
    #endif
#endif
