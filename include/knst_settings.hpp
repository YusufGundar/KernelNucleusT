#pragma once
/*

    You can define the macros you want here; they will become active throughout the entire KernelNucleusT.

*/


/* Special Macros


    #define KNST_C16STRING_DEACTIVE_COW   // It disables the Cow feature and ensures that a deep copy is created with every copy.

    #define KNST_C16_STRING_USING_ATOMIC_COW   // Makes the cow reference number atomic so that it can be read thread-safe.

    #if defined(KNST_C16STRING_ALIGN_64)
      
        #define KNST_STRING_ALIGNMENT alignas(64)
        KNST_SSO_BUFFER_CAPACITY = 31; // 31 * 2 == 62 byte Stack Data; 61 bayt character 1 byte u'/0'; max 30 character count;
        KNST_SSO_BUFFER_LENGTH = 30;

    #elif defined(KNST_C16STRING_ALIGN_32)
        
        #define KNST_STRING_ALIGNMENT alignas(32)
        KNST_SSO_BUFFER_CAPACITY = 15; // 15 * 2 == 30 byte Stack Data; 29 bayt character 1 byte u'/0'; max 14 character count;
        KNST_SSO_BUFFER_LENGTH = 14;

    #else
       
        #define KNST_STRING_ALIGNMENT alignas(8)
        KNST_SSO_BUFFER_CAPACITY = 11; // 11 * 2 == 22 byte Stack Data; 29 bayt character 1 byte u'/0'; max 10 character count;
        KNST_SSO_BUFFER_LENGTH = 10;

    #endif



    
    #define KNST_MEMORY_POOL_USE_MUTEX   // thread-safe pool (std::mutex)


    #define KNST_SMALL_SIZE_CLASS //`force inline` turns it into an inline function; the class size decreases, but there may be a loss in performance.


*/








#include "knst_definitions.hpp"










