#ifndef KNST_GLOBAL_FUNCTIONS_HPP
#define KNST_GLOBAL_FUNCTIONS_HPP
#pragma once

#include "knst_memory.hpp" 
#include <cstdint> // uint8_t
#include <cstddef>


// This function reads the part up to '\0', meaning it only takes the part up to '\0'.
KNST_FORCE_INLINE uint32_t knst_convert_utf8_to_utf16(const char* src, uint32_t src_len, char16_t* dst) noexcept {
    uint32_t i = 0;
    uint32_t j = 0;

   
    while (i + 3 < src_len) {
        uint8_t c0 = static_cast<uint8_t>(src[i]);
        uint8_t c1 = static_cast<uint8_t>(src[i+1]);
        uint8_t c2 = static_cast<uint8_t>(src[i+2]);
        uint8_t c3 = static_cast<uint8_t>(src[i+3]);

      
        if (const uint8_t mask = (c0 | c1 | c2 | c3); mask < 0x80) {
            dst[j]   = static_cast<char16_t>(c0);
            dst[j+1] = static_cast<char16_t>(c1);
            dst[j+2] = static_cast<char16_t>(c2);
            dst[j+3] = static_cast<char16_t>(c3);
            j += 4;
            i += 4;
            continue; 
        }

       
        if (c0 < 0x80) { dst[j++] = c0; i += 1; }
        else if ((c0 & 0xE0) == 0xC0) { dst[j++] = static_cast<char16_t>(((c0 & 0x1F) << 6) | (c1 & 0x3F)); i += 2; }
        else if ((c0 & 0xF0) == 0xE0) { dst[j++] = static_cast<char16_t>(((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F)); i += 3; }
        else {
            uint32_t cp = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
            cp -= 0x10000; dst[j++] = static_cast<char16_t>((cp >> 10) + 0xD800); dst[j++] = static_cast<char16_t>((cp & 0x3FF) + 0xDC00); i += 4;
        }
    }

    while (i < src_len) {
        uint8_t c = static_cast<uint8_t>(src[i]);
        if (c < 0x80) { dst[j++] = c; i += 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < src_len) { dst[j++] = static_cast<char16_t>(((c & 0x1F) << 6) | (static_cast<uint8_t>(src[i + 1]) & 0x3F)); i += 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < src_len) { dst[j++] = static_cast<char16_t>(((c & 0x0F) << 12) | ((static_cast<uint8_t>(src[i + 1]) & 0x3F) << 6) | (static_cast<uint8_t>(src[i + 2]) & 0x3F)); i += 3; }
        else if ((c & 0xF8) == 0xF0 && i + 3 < src_len) {
            uint32_t cp = ((c & 0x07) << 18) | ((static_cast<uint8_t>(src[i + 1]) & 0x3F) << 12) | ((static_cast<uint8_t>(src[i + 2]) & 0x3F) << 6) | (static_cast<uint8_t>(src[i + 3]) & 0x3F);
            cp -= 0x10000; dst[j++] = static_cast<char16_t>((cp >> 10) + 0xD800); dst[j++] = static_cast<char16_t>((cp & 0x3FF) + 0xDC00); i += 4;
        } else { dst[j++] = u'\xFFFD'; i += 1; }
    }
    return j;
}


KNST_FORCE_INLINE uint32_t knst_get_utf8_to_utf16_exact_length(const char* utf8_str, uint32_t utf8_byte_count) noexcept {
    uint32_t utf16_count = 0;
    uint32_t i = 0;
    
    while(i < utf8_byte_count) {
        unsigned char c = utf8_str[i];
        
        if(c < 0x80) {
            i += 1;
        }
        else if((c & 0xE0) == 0xC0) {
            i += 2;
        }
        else if((c & 0xF0) == 0xE0) {
            i += 3;
        }
        else if((c & 0xF8) == 0xF0) {
            i += 4;  // 4 byte UTF-8 = surrogate pair in UTF-16 (2 char16_t)
            utf16_count++; // Surrogate pair for +1
        }
        else {
            i += 1; // Invalid, skip
        }
        utf16_count++;
    }
    
    return utf16_count;
}

// Returns the exact byte size needed for UTF-16 to UTF-8 conversion
KNST_FORCE_INLINE uint32_t knst_get_utf16_to_utf8_exact_byte_size(const char16_t* src, uint32_t src_len) noexcept {
    uint32_t total_bytes = 0;
    uint32_t i = 0;
    
    while (i < src_len) {
        uint16_t c = src[i];
        
        // ASCII (U+0000 - U+007F)
        if (c < 0x80) {
            total_bytes += 1;
            i += 1;
        }
        // 2-byte UTF-8 (U+0080 - U+07FF)
        else if (c < 0x800) {
            total_bytes += 2;
            i += 1;
        }
        // Surrogate pair (U+10000 - U+10FFFF)
        else if (c >= 0xD800 && c <= 0xDBFF) {
            // Check if it's a valid surrogate pair
            if (i + 1 < src_len && src[i+1] >= 0xDC00 && src[i+1] <= 0xDFFF) {
                total_bytes += 4;  // Surrogate pair = 4 bytes in UTF-8
                i += 2;
            } else {
                // Invalid surrogate, treat as 3-byte character (U+FFFD replacement)
                total_bytes += 3;
                i += 1;
            }
        }
        // 3-byte UTF-8 (U+0800 - U+FFFF, excluding surrogates)
        else {
            total_bytes += 3;
            i += 1;
        }
    }
    
    return total_bytes;
}

KNST_FORCE_INLINE uint32_t knst_convert_utf16_to_utf8(const char16_t* src, uint32_t src_len, char* dst) noexcept {
    uint32_t i = 0;
    uint32_t j = 0;

  
    while (i + 3 < src_len) {
        uint16_t c0 = src[i];
        uint16_t c1 = src[i+1];
        uint16_t c2 = src[i+2];
        uint16_t c3 = src[i+3];

        
        if (const uint16_t mask = (c0 | c1 | c2 | c3); mask < 0x80) {
            dst[j]   = static_cast<char>(c0);
            dst[j+1] = static_cast<char>(c1);
            dst[j+2] = static_cast<char>(c2);
            dst[j+3] = static_cast<char>(c3);
            j += 4;
            i += 4;
            continue;
        }

      
        if (c0 < 0x80) {
            dst[j++] = static_cast<char>(c0);
            i += 1;
        }
        else if (c0 < 0x800) { 
            dst[j++] = static_cast<char>((c0 >> 6) | 0xC0);
            dst[j++] = static_cast<char>((c0 & 0x3F) | 0x80);
            i += 1;
        }
        else if (c0 >= 0xD800 && c0 <= 0xDBFF) { 
            uint32_t high = c0;
            uint32_t low = src[i+1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                uint32_t cp = ((high - 0xD800) << 10) + (low - 0xDC00) + 0x10000;
                dst[j++] = static_cast<char>((cp >> 18) | 0xF0);
                dst[j++] = static_cast<char>(((cp >> 12) & 0x3F) | 0x80);
                dst[j++] = static_cast<char>(((cp >> 6) & 0x3F) | 0x80);
                dst[j++] = static_cast<char>((cp & 0x3F) | 0x80);
                i += 2;
            } else {
              
                dst[j++] = '\xEF'; dst[j++] = '\xBF'; dst[j++] = '\xBD';
                i += 1;
            }
        }
        else {
            dst[j++] = static_cast<char>((c0 >> 12) | 0xE0);
            dst[j++] = static_cast<char>(((c0 >> 6) & 0x3F) | 0x80);
            dst[j++] = static_cast<char>((c0 & 0x3F) | 0x80);
            i += 1;
        }
    }

  
    for (; i < src_len; ++i) {
        uint16_t c = src[i];
        if (c < 0x80) {
            dst[j++] = static_cast<char>(c);
        }
        else if (c < 0x800) {
            dst[j++] = static_cast<char>((c >> 6) | 0xC0);
            dst[j++] = static_cast<char>((c & 0x3F) | 0x80);
        }
        else if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 1 < src_len && src[i+1] >= 0xDC00 && src[i+1] <= 0xDFFF) {
                uint32_t cp = ((c - 0xD800) << 10) + (src[i+1] - 0xDC00) + 0x10000;
                dst[j++] = static_cast<char>((cp >> 18) | 0xF0);
                dst[j++] = static_cast<char>(((cp >> 12) & 0x3F) | 0x80);
                dst[j++] = static_cast<char>(((cp >> 6) & 0x3F) | 0x80);
                dst[j++] = static_cast<char>((cp & 0x3F) | 0x80);
                i++; 
            } else {
                dst[j++] = '\xEF'; dst[j++] = '\xBF'; dst[j++] = '\xBD';
            }
        }
        else {
            dst[j++] = static_cast<char>((c >> 12) | 0xE0);
            dst[j++] = static_cast<char>(((c >> 6) & 0x3F) | 0x80);
            dst[j++] = static_cast<char>((c & 0x3F) | 0x80);
        }
    }

    return j; 
}

KNST_FORCE_INLINE uint32_t knst_get_wchar_to_utf16_exact_length(const wchar_t* wstr, uint32_t wchar_count) noexcept {
    uint32_t utf16_count = 0;
    uint32_t i = 0;
    
    #ifndef _WIN32
    
    while(i + 3 < wchar_count) {
        char32_t c0 = static_cast<char32_t>(wstr[i]);
        char32_t c1 = static_cast<char32_t>(wstr[i+1]);
        char32_t c2 = static_cast<char32_t>(wstr[i+2]);
        char32_t c3 = static_cast<char32_t>(wstr[i+3]);
        
        // Count surrogate pairs needed
        utf16_count += (c0 < 0x10000) ? 1 : 2;
        utf16_count += (c1 < 0x10000) ? 1 : 2;
        utf16_count += (c2 < 0x10000) ? 1 : 2;
        utf16_count += (c3 < 0x10000) ? 1 : 2;
        
        i += 4;
    }
    #endif
    
    // Handle remaining characters
    for(; i < wchar_count; ++i) {
        #ifdef _WIN32
            utf16_count += 1;
        #else
            char32_t cp = static_cast<char32_t>(wstr[i]);
            utf16_count += (cp < 0x10000) ? 1 : 2;
        #endif
    }
    
    return utf16_count;
}

KNST_FORCE_INLINE uint32_t knst_get_char32_to_utf16_exact_length(const char32_t* str, uint32_t char32_count) noexcept {
    uint32_t utf16_count = 0;
    uint32_t i = 0;
    
    
    while(i + 3 < char32_count) {
        char32_t c0 = str[i];
        char32_t c1 = str[i+1];
        char32_t c2 = str[i+2];
        char32_t c3 = str[i+3];
        
        
        utf16_count += (c0 < 0x10000) ? 1 : 2;
        utf16_count += (c1 < 0x10000) ? 1 : 2;
        utf16_count += (c2 < 0x10000) ? 1 : 2;
        utf16_count += (c3 < 0x10000) ? 1 : 2;
        
        i += 4;
    }
    
    
    for(; i < char32_count; ++i) {
        char32_t cp = str[i];
        utf16_count += (cp < 0x10000) ? 1 : 2;
    }
    
    return utf16_count;
}

KNST_FORCE_INLINE void knst_convert_char32_to_utf16(const char32_t* src, uint32_t src_len, char16_t* dst) noexcept {
    uint32_t out_pos = 0;
    for(uint32_t i = 0; i < src_len; ++i) {
        char32_t cp = src[i];
        
        if(cp < 0x10000) {
            dst[out_pos++] = static_cast<char16_t>(cp);
        }
        else {
            // Surrogate pair
            cp -= 0x10000;
            dst[out_pos++] = static_cast<char16_t>(0xD800 + (cp >> 10));
            dst[out_pos++] = static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
        }
    }
}

// Convert wchar_t to UTF-16
KNST_FORCE_INLINE void knst_convert_wchar_to_utf16(const wchar_t* src, uint32_t src_len, char16_t* dst) noexcept {
    #ifdef _WIN32
        // Windows ** Direct copy (same encoding)
        for(uint32_t i = 0; i < src_len; ++i) {
            dst[i] = static_cast<char16_t>(src[i]);
        }
    #else
        // Linux/macOS ** UTF-32 to UTF-16 conversion
        uint32_t out_pos = 0;
        for(uint32_t i = 0; i < src_len; ++i) {
            char32_t cp = static_cast<char32_t>(src[i]);
            
            if(cp < 0x10000) {
                dst[out_pos++] = static_cast<char16_t>(cp);
            }
            else {
                // Surrogate pair
                cp -= 0x10000;
                dst[out_pos++] = static_cast<char16_t>(0xD800 + (cp >> 10));
                dst[out_pos++] = static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
            }
        }
    #endif
}


KNST_FORCE_INLINE void knst_convert_utf16_to_wchar(const char16_t* src, uint32_t src_len, wchar_t* dst) noexcept {
    #ifdef _WIN32
        // Windows: wchar_t is 16-bit, direct copy
        for(uint32_t i = 0; i < src_len; ++i) {
            dst[i] = static_cast<wchar_t>(src[i]);
        }
    #else
        // Linux/macOS: wchar_t is 32-bit (UTF-32), convert UTF-16 to UTF-32
        uint32_t out_pos = 0;
        for(uint32_t i = 0; i < src_len; ++i) {
            char16_t ch = src[i];
            
            // Check if it's a high surrogate (0xD800 - 0xDBFF)
            if(ch >= 0xD800 && ch <= 0xDBFF) {
                // Need a low surrogate
                if(i + 1 < src_len) {
                    char16_t low = src[i + 1];
                    if(low >= 0xDC00 && low <= 0xDFFF) {
                        // Valid surrogate pair -> combine to UTF-32
                        uint32_t cp = 0x10000 + ((static_cast<uint32_t>(ch) - 0xD800) << 10) + (static_cast<uint32_t>(low) - 0xDC00);
                        dst[out_pos++] = static_cast<wchar_t>(cp);
                        i++; // Skip low surrogate
                    }
                    else {
                        // Invalid low surrogate, handle as replacement char
                        dst[out_pos++] = static_cast<wchar_t>(0xFFFD);
                    }
                }
                else {
                    // Unpaired high surrogate at end
                    dst[out_pos++] = static_cast<wchar_t>(0xFFFD);
                }
            }
            else if(ch >= 0xDC00 && ch <= 0xDFFF) {
                // Unexpected low surrogate without high surrogate
                dst[out_pos++] = static_cast<wchar_t>(0xFFFD);
            }
            else {
                // BMP character (including surrogates that are valid singles)
                dst[out_pos++] = static_cast<wchar_t>(ch);
            }
        }
    #endif
}


// Returns in length , character count.
KNST_FORCE_INLINE uint32_t knst_get_str_length(const char16_t * str) noexcept{

    return std::char_traits<char16_t>::length(str);
}

 
KNST_FORCE_INLINE uint32_t knst_get_str_length(const char * str) noexcept{

    return std::char_traits<char>::length(str);
}

  
KNST_FORCE_INLINE uint32_t knst_get_str_length(const wchar_t * str) noexcept{

    return std::char_traits<wchar_t>::length(str);
}

KNST_FORCE_INLINE uint32_t knst_get_str_length(const char32_t * str) noexcept{

    return std::char_traits<char32_t>::length(str);
}







// ITERATOR


template<typename T>
class knst_iterator{

    T* ptr;

    public:

    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;
    using iterator_category = std::random_access_iterator_tag;


    KNST_FORCE_INLINE knst_iterator(T* p = nullptr) noexcept : ptr(p){};


    // Basic Operators

    KNST_FORCE_INLINE reference operator*() const noexcept {return *ptr;}
    KNST_FORCE_INLINE pointer operator->() const noexcept {return *ptr;}

    KNST_FORCE_INLINE knst_iterator& operator++() noexcept {++ptr; return *this;}
    KNST_FORCE_INLINE knst_iterator operator++(int) noexcept {auto tmp = *this; ++ptr; return tmp;}
    KNST_FORCE_INLINE knst_iterator& operator--() noexcept {--ptr; return *this;}
    KNST_FORCE_INLINE knst_iterator operator--(int) noexcept {auto tmp = *this; --ptr; return tmp;}

    KNST_FORCE_INLINE knst_iterator& operator+=(difference_type n) noexcept {ptr += n; return *this;}
    KNST_FORCE_INLINE knst_iterator& operator-=(difference_type n) noexcept {ptr -= n; return *this;}

    KNST_FORCE_INLINE friend knst_iterator operator+(knst_iterator it, difference_type n) noexcept { return it.ptr + n; }
    KNST_FORCE_INLINE friend knst_iterator operator+(difference_type n, knst_iterator it) noexcept { return it.ptr + n; }
    KNST_FORCE_INLINE friend knst_iterator operator-(knst_iterator it, difference_type n) noexcept { return it.ptr - n; }
    KNST_FORCE_INLINE friend difference_type operator-(knst_iterator a, knst_iterator b) noexcept { return a.ptr - b.ptr; }
    
    KNST_FORCE_INLINE friend bool operator==(knst_iterator a, knst_iterator b) noexcept { return a.ptr == b.ptr; }
    KNST_FORCE_INLINE friend bool operator!=(knst_iterator a, knst_iterator b) noexcept { return a.ptr != b.ptr; }
    KNST_FORCE_INLINE friend bool operator<(knst_iterator a, knst_iterator b) noexcept { return a.ptr < b.ptr; }
    KNST_FORCE_INLINE friend bool operator>(knst_iterator a, knst_iterator b) noexcept { return a.ptr > b.ptr; }
    KNST_FORCE_INLINE friend bool operator<=(knst_iterator a, knst_iterator b) noexcept { return a.ptr <= b.ptr; }
    KNST_FORCE_INLINE friend bool operator>=(knst_iterator a, knst_iterator b) noexcept { return a.ptr >= b.ptr; }
    
    KNST_FORCE_INLINE reference operator[](difference_type n) const noexcept { return ptr[n]; }

};

template<typename T>
class knst_const_iterator {
    const T* ptr;  // const T*

public:
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = const T*;           
    using reference = const T&;         
    using iterator_category = std::random_access_iterator_tag;

    KNST_FORCE_INLINE knst_const_iterator(const T* p = nullptr) noexcept : ptr(p) {}

    // Basic Operators
    KNST_FORCE_INLINE reference operator*() const noexcept { return *ptr; }
    KNST_FORCE_INLINE pointer operator->() const noexcept { return ptr; }

    KNST_FORCE_INLINE knst_const_iterator& operator++() noexcept { ++ptr; return *this; }
    KNST_FORCE_INLINE knst_const_iterator operator++(int) noexcept { auto tmp = *this; ++ptr; return tmp; }
    KNST_FORCE_INLINE knst_const_iterator& operator--() noexcept { --ptr; return *this; }
    KNST_FORCE_INLINE knst_const_iterator operator--(int) noexcept { auto tmp = *this; --ptr; return tmp; }

    KNST_FORCE_INLINE knst_const_iterator& operator+=(difference_type n) noexcept { ptr += n; return *this; }
    KNST_FORCE_INLINE knst_const_iterator& operator-=(difference_type n) noexcept { ptr -= n; return *this; }

    KNST_FORCE_INLINE friend knst_const_iterator operator+(knst_const_iterator it, difference_type n) noexcept { return it.ptr + n; }
    KNST_FORCE_INLINE friend knst_const_iterator operator+(difference_type n, knst_const_iterator it) noexcept { return it.ptr + n; }
    KNST_FORCE_INLINE friend knst_const_iterator operator-(knst_const_iterator it, difference_type n) noexcept { return it.ptr - n; }
    KNST_FORCE_INLINE friend difference_type operator-(knst_const_iterator a, knst_const_iterator b) noexcept { return a.ptr - b.ptr; }
    
    KNST_FORCE_INLINE friend bool operator==(knst_const_iterator a, knst_const_iterator b) noexcept { return a.ptr == b.ptr; }
    KNST_FORCE_INLINE friend bool operator!=(knst_const_iterator a, knst_const_iterator b) noexcept { return a.ptr != b.ptr; }
    KNST_FORCE_INLINE friend bool operator<(knst_const_iterator a, knst_const_iterator b) noexcept { return a.ptr < b.ptr; }
    KNST_FORCE_INLINE friend bool operator>(knst_const_iterator a, knst_const_iterator b) noexcept { return a.ptr > b.ptr; }
    KNST_FORCE_INLINE friend bool operator<=(knst_const_iterator a, knst_const_iterator b) noexcept { return a.ptr <= b.ptr; }
    KNST_FORCE_INLINE friend bool operator>=(knst_const_iterator a, knst_const_iterator b) noexcept { return a.ptr >= b.ptr; }
    
    KNST_FORCE_INLINE reference operator[](difference_type n) const noexcept { return ptr[n]; }
};


#endif // KNST_GLOBAL_FUNCTIONS_HPP