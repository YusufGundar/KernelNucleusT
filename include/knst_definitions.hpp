#ifndef KNST_MACROS_HPP
#define KNST_MACROS_HPP
#pragma once
#include <cstdint> 


    #if defined(KNST_CLASS_ALIGN_64)
      
        #define KNST_STRING_ALIGNMENT alignas(64)
        static constexpr uint32_t KNST_SSO_BUFFER_CAPACITY = 31;
        static constexpr uint32_t KNST_SSO_BUFFER_LENGTH = 30;

    #elif defined(KNST_CLASS_ALIGN_32)
        
        #define KNST_STRING_ALIGNMENT alignas(32)
        static constexpr uint32_t KNST_SSO_BUFFER_CAPACITY = 15; 
        static constexpr uint32_t KNST_SSO_BUFFER_LENGTH = 14;

    #else
       
        #define KNST_CLASS_ALIGNMENT alignas(8)
        static constexpr uint32_t KNST_SSO_BUFFER_CAPACITY = 11;
        static constexpr uint32_t KNST_SSO_BUFFER_LENGTH = 10;

    #endif



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


    

    #if defined(_WIN32) || defined(_WIN64)
        #define KNST_USING_PLATFORM_WINDOWS 1
        #define KNST_USING_LINUX_PLATFORM_X11 0
        #define KNST_USING_LINUX_PLATFORM_WAYLAND 0
    #elif defined(__linux__)
        #define KNST_USING_PLATFORM_LINUX 1
        #define KNST_USING_PLATFORM_WINDOWS 0


        #if defined(KNST_LINUX_PLATFORM_X11)
            #define KNST_USING_LINUX_PLATFORM_X11 1
            #define KNST_USING_LINUX_PLATFORM_WAYLAND 0

        #elif defined(KNST_LINUX_PLATFORM_WAYLAND)
            #define KNST_USING_LINUX_PLATFORM_WAYLAND 1
            #define KNST_USING_LINUX_PLATFORM_X11 0

        #endif

    #endif

#endif // KNST_MACROS_HPP