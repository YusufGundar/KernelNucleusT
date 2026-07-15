#ifndef KNST_MACROS_HPP
#define KNST_MACROS_HPP
#pragma once

    #define KNST_USING_GLOBAL_SOURCE

    #if defined(_WIN32) || defined(_WIN64)
            #include <windows.h>
    #else
            #include <cstdlib>
    #endif

    #ifdef KNST_SMALL_SIZE_CLASS
        
        #define KNST_FORCE_INLINE inline
    #elif defined(_MSC_VER)
        #define KNST_FORCE_INLINE __forceinline
    #elif defined(__GNUC__) || defined(__clang__)
        #define KNST_FORCE_INLINE __attribute__((always_inline)) inline
    #else
        #define KNST_FORCE_INLINE inline  // fallback
    #endif

    #include <cstdio>


    #ifndef NDEBUG
        #define KNST_ASSERT(expr) \
            do { \
                if(!(expr)) { \
                    std::fprintf(stderr, "Assertion failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
                    std::abort(); \
                } \
            } while(false)
    #else
        #define KNST_ASSERT(expr) ((void)0)
    #endif

#endif // KNST_MACROS_HPP