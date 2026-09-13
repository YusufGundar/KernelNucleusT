/*
----------------------------
knst_c16string.hpp
----------------------------

    It is a template-based string class.
    It is a specialized string class with memory support
    You can use the version with a custom allocator—suffixed with `_sm`—which utilizes C++20's `[[no_unique_address]]`
    UTF-16 string class. Features SSO (small string optimization/stack allocation), COW (copy-on-write via reference counting), and allocator awareness. Offers an extensive set of constructors: char16_t*, char* (UTF-8), wchar_t*, char32_t*, std::string, std::u16string, initializer_list, and numeric types (int, long, float, double). Automatic conversions between UTF-8, UTF-16, UTF-32, and wchar. Rich API including append, find (using the two-way algorithm), starts_with/ends_with, substr, resize, and reserve. Supports ostream/wostream operators. knst_c16string_sm is a version utilizing a pool allocator. Ideal for file paths—native on Windows, converts to UTF-8 on POSIX
*/


#pragma once




/* Special Macros


    #define KNST_C16STRING_DEACTIVE_COW   // It disables the Cow feature and ensures that a deep copy is created with every copy.

    #define KNST_C16_STRING_USING_ATOMIC_COW   // Makes the cow reference number atomic so that it can be read thread-safe.



*/

    // align macros

    /*#if defined(KNST_C16STRING_ALIGN_64)
      
        #define KNST_CLASS_ALIGNMENT alignas(64)
        static constexpr uint32_t KNST_SSO_BUFFER_CAPACITY = 31; // 31 * 2 == 62 byte Stack Data; 61 bayt character 1 byte u'/0'; max 30 character count;
        static constexpr uint32_t KNST_SSO_BUFFER_LENGTH = 30;

    #elif defined(KNST_C16STRING_ALIGN_32)
        
        #define KNST_CLASS_ALIGNMENT alignas(32)
        static constexpr uint32_t KNST_SSO_BUFFER_CAPACITY = 15; // 15 * 2 == 30 byte Stack Data; 29 bayt character 1 byte u'/0'; max 14 character count;
        static constexpr uint32_t KNST_SSO_BUFFER_LENGTH = 14;

    #else
       
        #define KNST_CLASS_ALIGNMENT alignas(8)
        static constexpr uint32_t KNST_SSO_BUFFER_CAPACITY = 11; // 11 * 2 == 22 byte Stack Data; 29 bayt character 1 byte u'/0'; max 10 character count;
        static constexpr uint32_t KNST_SSO_BUFFER_LENGTH = 10;

    #endif*/ 


template <typename Allocator = knst_default_allocator>
class KNST_CLASS_ALIGNMENT basic_c16string{



   private:

    [[no_unique_address]] mutable Allocator  m_allocator; // If the template is empty, it won't take up space.

    static constexpr uint32_t KNST_FIND_NAIVE_THRESHOLD = 8; // Threshold for the `find` algorithm: A naive search is used if the pattern length is 8 or less, while the two-way algorithm is used for longer patterns. The naive approach is already fast for short patterns, whereas the two-way algorithm guarantees O(n) performance for long ones
    
    #ifndef KNST_C16STRING_DEACTIVE_COW // If you use the 'cow' feature, attributes such as heap data and the reference counter are stored in this structure
        struct SharingHeapData{
            #ifdef KNST_C16_STRING_USING_ATOMIC_COW
                std::atomic<int> m_ref_count;
            #else
                int m_ref_count;
            #endif
                            
            char16_t m_real_data[];
                            
        };
    #endif

    struct HeapDataLayout // The layout of the string in heap mode. If COW is disabled ==> raw `char16_t*` pointer. If COW is enabled ==> `SharingHeapData*` (ref count + data). In both cases, `m_capacity` and `m_length` fields are present. This struct constitutes one branch of the union, while the other holds stack (SSO) data.
    {
        #ifdef KNST_C16STRING_DEACTIVE_COW
            char16_t *m_real_data;
        #else
            SharingHeapData * sharing_heap_data;
        #endif

        uint32_t m_capacity;
        uint32_t m_length;
        
    };

    union{

        HeapDataLayout heap_data;

        // Default Alignas :10 characters + 1 u'\0' total 22 byte  ,  KNST_C16STRING_ALIGN_64 :30 characters + 1 u'\0' total 64 byte  ,  KNST_C16STRING_ALIGN_32 :14 characters + 1 u'\0' total 32 byte
        struct
        {

            char16_t m_real_data[KNST_SSO_BUFFER_CAPACITY]; 
            uint8_t padding[sizeof(heap_data) > (KNST_SSO_BUFFER_CAPACITY * sizeof(char16_t) + 2) ? sizeof(heap_data) - (KNST_SSO_BUFFER_CAPACITY * sizeof(char16_t) + 2) : 1];  // Padding is adjusted according to size.
                            
            uint8_t m_flag; // heap status and length , (0x80) mask:   1  0  0  0  0  0  0  0     

        } stack_data;
        

    };

    KNST_FORCE_INLINE void set_stack_mode(uint32_t length) noexcept{

        this->stack_data.m_flag = static_cast<uint8_t>(length); // Since the stack can have a maximum of 11 bits, the longest bit will always be 0.
    }

    KNST_FORCE_INLINE void set_heap_mode() noexcept{

        this->stack_data.m_flag = 0x80; // fits the mask. (0x80) mask:   1  0  0  0  0  0  0  0   
    }

    KNST_FORCE_INLINE uint32_t get_sso_length() const noexcept {
    
        return static_cast<uint32_t>(this->stack_data.m_flag & 0x7F);

    }

    // Provides the appropriate size for allocation.
    KNST_FORCE_INLINE uint32_t get_new_heap_size(uint32_t size)const noexcept{
        
        #ifdef KNST_C16STRING_DEACTIVE_COW

            return size * sizeof(char16_t);

        #else

            return sizeof(SharingHeapData) + size * sizeof(char16_t);

        #endif


    }  
   
    #ifdef KNST_C16STRING_DEACTIVE_COW

        #define set_cow_ref_count_plus_plus()  
    
    #else

        KNST_FORCE_INLINE void set_cow_ref_count_plus_plus() const noexcept{ // COW increments the reference count—it is called within the copy constructor. It uses `fetch_add` in atomic mode, or `++` otherwise. Essentially, it signifies that "one more person is sharing this string." Memory is not copied; only the counter increases

            #ifdef KNST_C16_STRING_USING_ATOMIC_COW

                this->heap_data.sharing_heap_data->m_ref_count.fetch_add(1,std::memory_order::memory_order_relaxed);

            #else

                ++this->heap_data.sharing_heap_data->m_ref_count;

            #endif

        }

        KNST_FORCE_INLINE bool reduce_and_control_ref_count() noexcept{ // Decrements the reference count and checks if it is the last reference. Returns `true` if it is the last owner (memory should be freed) and `false` if others are still using it (do not touch). Called within the destructor

            #ifdef KNST_C16_STRING_USING_ATOMIC_COW

                if(this->heap_data.sharing_heap_data->m_ref_count.fetch_sub(1,std::memory_order::memory_order_relaxed) <= 1) {
                    return true;
                }

            #else

                if(--this->heap_data.sharing_heap_data->m_ref_count < 1) {
                    return true;
                }

            #endif

            return false;
        }

        KNST_FORCE_INLINE bool control_ref_count() noexcept{ // It checks if the reference count is greater than 1. If true, others are sharing this buffer (detachment is required before writing). If false, it is the sole owner, and you can write safely. This is the "pre-write check" step of the COW mechanism

            #ifdef KNST_C16_STRING_USING_ATOMIC_COW

                if(this->heap_data.sharing_heap_data->m_ref_count.load(std::memory_order::memory_order_relaxed) > 1) {
                    return true;
                }

            #else

                if(this->heap_data.sharing_heap_data->m_ref_count > 1) {
                    return true;
                }

            #endif

            return false;
        }

        KNST_FORCE_INLINE void set_cow_ref_count(uint32_t size) noexcept{ // Sets the reference count to the specified value. It is set to 1 when a new heap block is created (indicating a single owner). It uses a direct assignment rather than increment/decrement operations, as this occurs at the moment of allocation

            #ifdef KNST_C16_STRING_USING_ATOMIC_COW

                this->heap_data.sharing_heap_data->m_ref_count.store(size,std::memory_order::memory_order_relaxed);

            #else

                this->heap_data.sharing_heap_data->m_ref_count = size;

            #endif

        }

        KNST_FORCE_INLINE bool m_detach() noexcept{ // COW detach — detaches from the shared buffer. It allocates a new heap block, copies the existing data, and decrements the old buffer's reference count. The string now has its own private buffer—you can write to it without affecting others. It is the counterpart to `set_cow_ref_count_plus_plus`

            const char16_t * heap_data = this->heap_data.sharing_heap_data->m_real_data;
            uint32_t str_capacity = this->heap_data.m_capacity;

            void * new_heap = this->m_allocator.allocate(get_new_heap_size(str_capacity));

            if(!new_heap) return false;
           
            set_clean_heap_varible(new_heap,heap_data, this->heap_data.m_length,str_capacity);

            #ifdef KNST_C16_STRING_USING_ATOMIC_COW

                this->heap_data.sharing_heap_data->m_ref_count.fetch_sub(1,std::memory_order::memory_order_relaxed);
            
            #else

                --this->heap_data.sharing_heap_data->m_ref_count;

            #endif
            
            
            return true;
        }

    #endif



      
    KNST_FORCE_INLINE char16_t * get_real_heap_m_data() const noexcept{ // Returns the actual `char16_t*` data pointer on the heap. If COW is disabled, it returns the direct pointer; if enabled, it returns `m_real_data` from `SharingHeapData`. This is the fundamental accessor used everywhere

        #ifdef KNST_C16STRING_DEACTIVE_COW   

            return this->heap_data.m_real_data;
            
        #else

            return this->heap_data.sharing_heap_data->m_real_data;

        #endif

    }

    KNST_FORCE_INLINE void * get_heap_m_data_for_begin() const noexcept{ // It returns the starting pointer of the heap block. If COW is disabled, it returns `char16_t*` (the data); if enabled, it returns `SharingHeapData*` (reference count + data). In other words, it indicates where the entire block begins. This is necessary for `reallocate`/`deallocate` operations—to provide the correct address to the allocator

        #ifdef KNST_C16STRING_DEACTIVE_COW   

            return this->heap_data.m_real_data;
            
        #else

            return this->heap_data.sharing_heap_data;

        #endif

    }

    KNST_FORCE_INLINE void set_real_heap_m_data(void* data) noexcept{ // The inverse of get_heap_m_data_for_begin — sets the heap pointer. It is called when a new allocation is returned. It casts to the correct type based on the COW state
        #ifdef KNST_C16STRING_DEACTIVE_COW
            this->heap_data.m_real_data = reinterpret_cast<char16_t*>(data);
        #else
            this->heap_data.sharing_heap_data = reinterpret_cast<SharingHeapData*>(data);
        #endif

    }


  
    // It is used when assigning new heap data; it is created based on the clean values ​​provided.
    KNST_FORCE_INLINE void set_clean_heap_varible(void * heap_varible ,const char16_t *str , uint32_t str_length , uint32_t str_capacity) noexcept{

        
        
        #ifdef KNST_C16STRING_DEACTIVE_COW   

            this->heap_data.m_real_data = reinterpret_cast<char16_t*>(heap_varible);
            
        #else

            this->heap_data.sharing_heap_data = reinterpret_cast<SharingHeapData*>(heap_varible);
            
        #endif

        

        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wstringop-overread"
            memcpy(get_real_heap_m_data(), str, str_length * sizeof(char16_t));
        #pragma GCC diagnostic pop
        get_real_heap_m_data()[str_length]= u'\0';

        

        #ifndef KNST_C16STRING_DEACTIVE_COW

            set_cow_ref_count(1);

        #endif
        
        

        this->heap_data.m_length = str_length;
        this->heap_data.m_capacity = str_capacity;
        

    }

 
   
    KNST_FORCE_INLINE static void compute_kritik_faktorization(const char16_t* pat, uint32_t pat_len,uint32_t& crit_pos, uint32_t& period) noexcept // The critical factorization step of the Two-Way string matching algorithm. It calculates the pattern's period and critical position. It performs a two-way scan and selects the more suitable option. Result: `crit_pos` (where to start the comparison) and `period` (how far to shift upon a mismatch). This is required for guaranteed O(n) searching
    {
       
        int32_t ms = -1;
        int32_t j = 0;
        uint32_t k = 1;
        uint32_t p= 1; 

        while(j + k < (int32_t)pat_len){
            char16_t a = pat[j + k];
            char16_t b = pat[ms + k];
            if(a < b){
                j += k; k = 1; p = j - ms;
            } else if(a == b){
                if(k == p){ j += p; k = 1; }
                else       { ++k; }
            } else { // a > b
                ms = j; j = ms + 1; k = 1; p = 1;
            }
        }
        crit_pos = static_cast<uint32_t>(ms + 1);
        period = p;

        int32_t ms2 = -1;
        j = 0; k = 1;
        uint32_t p2 = 1;

        while(j + k < (int32_t)pat_len){
            char16_t a = pat[j + k];
            char16_t b = pat[ms2 + k];
            if(a > b){
                j += k; k = 1; p2 = j - ms2;
            } else if(a == b){
                if(k == p2){ j += p2; k = 1; }
                else{ ++k; }
            } else {
                ms2 = j; j = ms2 + 1; k = 1; p2 = 1;
            }
        }

       
        if(static_cast<uint32_t>(ms + 1) < static_cast<uint32_t>(ms2 + 1)){
            crit_pos = static_cast<uint32_t>(ms2 + 1);
            period   = p2;
        }
    }

   
    KNST_FORCE_INLINE static bool find_two_way( // Two-way string searching algorithm. It splits the pattern into two parts at a critical position: the right side is compared from left to right, and the left side from right to left. A memory variable stores the previous comparison, eliminating redundant checks. It guarantees O(n) performance—far superior to the naive algorithm's O(n·m) worst-case—and is used for long patterns (>8 characters)
        const char16_t* text, uint32_t text_len,
        const char16_t* pat,  uint32_t pat_len) noexcept
    {
        uint32_t crit_pos, period;
        compute_kritik_faktorization(pat, pat_len, crit_pos, period);

        uint32_t pos = 0;
        uint32_t memory = static_cast<uint32_t>(-1);
        while(pos + pat_len <= text_len){

            uint32_t i = (crit_pos > memory + 1) ? crit_pos : memory + 1;

           
            while(i < pat_len && pat[i] == text[pos + i]){
                ++i;
            }

            if(i < pat_len){
             
                pos += (i - crit_pos + 1);
                memory = static_cast<uint32_t>(-1);
                continue;
            }

        
            uint32_t j = (memory == static_cast<uint32_t>(-1)) ? crit_pos : memory + 1;
            while(j > 0 && pat[j - 1] == text[pos + j - 1]){
                --j;
            }

            if(j == 0){
                return true;
            }

           
            pos += period;
            memory = pat_len - period - 1;
        }

        return false;
    }

   
    KNST_FORCE_INLINE static bool find_naive( // Naive string search. It checks the first and last characters at each position—skipping quickly if they don't match. If they do match, it compares the middle section using `memcmp`. It is faster than the Two-Way algorithm for short patterns (≤8 characters). Since `memcmp` is SIMD-optimized, the middle comparison is very fast
        const char16_t* text, uint32_t text_len,
        const char16_t* pat,  uint32_t pat_len) noexcept
    {
        const char16_t first = pat[0];
        const char16_t last  = pat[pat_len - 1];
        const uint32_t limit = text_len - pat_len;

        for(uint32_t i = 0; i <= limit; ++i){
            if(text[i] != first) continue;
            if(text[i + pat_len - 1] != last) continue;
            if(pat_len <= 2 || memcmp(text + i + 1, pat + 1, (pat_len - 2) * sizeof(char16_t)) == 0){
                return true;
            }
        }
        return false;
    }
    
   
    KNST_FORCE_INLINE void m_destructor() noexcept{  // It destroys the heap string. If COW is disabled, it deallocates immediately. If enabled, it decrements the reference count—freeing the memory if it is the last reference, or simply lowering the count otherwise (leaving the buffer for other strings). It is called in many places, such as the destructor, `clear`, and `resize`

        #ifdef KNST_C16STRING_DEACTIVE_COW

            this->m_allocator.deallocate(this->heap_data.m_real_data,get_new_heap_size(this->heap_data.m_capacity));
            
        #else

            if(reduce_and_control_ref_count()){
                this->m_allocator.deallocate(this->heap_data.sharing_heap_data,get_new_heap_size(this->heap_data.m_capacity));
            }

        #endif
    
    }

    friend std::ostream& operator<<(std::ostream& os, const basic_c16string& obj){ // std::ostream operator — converts a UTF-16 string to UTF-8 and prints it. It uses a stack buffer if the string is small (≤256 bytes) and the heap via the allocator if it is large. In other words, `std::cout << c16str` works naturally
        const char16_t* str_data = obj.data();
        uint32_t str_size = obj.length();
        uint32_t char_str_size = knst_get_utf16_to_utf8_exact_byte_size(str_data, str_size);

        
        constexpr uint32_t STACK_BUFFER_SIZE = 256;
        
        if (char_str_size <= STACK_BUFFER_SIZE) {
            
            char stack_buffer[STACK_BUFFER_SIZE + 1];
            knst_convert_utf16_to_utf8(str_data, str_size, stack_buffer);
            stack_buffer[char_str_size] = '\0';
            os << stack_buffer;
        } else {
        
            void* str = obj.m_allocator.allocate(char_str_size + 1);
            if (!str) return os;
            
            char* char_str = static_cast<char*>(str);
            knst_convert_utf16_to_utf8(str_data, str_size, char_str);
            char_str[char_str_size] = '\0';
            os << char_str;
            
            obj.m_allocator.deallocate(str, char_str_size + 1);
        }

        return os;
    }

    friend std::wostream& operator<<(std::wostream& os, const basic_c16string& obj){ // The std::wostream operator — writes to a wide-character stream. On Windows, since wchar_t is UTF-16, a direct write involves zero conversion. On POSIX, wchar_t is UTF-32, so conversion is required. The stack buffer with heap fallback approach remains the same
        const char16_t* str_data = obj.data();
        uint32_t str_size = obj.length();

    
        #if KNST_USING_PLATFORM_WINDOWS
        
            os.write(reinterpret_cast<const wchar_t*>(str_data), str_size);
        #else
        
            constexpr uint32_t STACK_BUFFER_SIZE = 256;
            
            if (str_size <= STACK_BUFFER_SIZE) {
                wchar_t stack_buffer[STACK_BUFFER_SIZE + 1];
                knst_convert_utf16_to_wchar(str_data, str_size, stack_buffer);
                stack_buffer[str_size] = L'\0';
                os << stack_buffer;
            } else {
                void* wstr = obj.m_allocator.allocate((str_size + 1) * sizeof(wchar_t));
                if (!wstr) return os;
                
                wchar_t* wchar_str = static_cast<wchar_t*>(wstr);
                knst_convert_utf16_to_wchar(str_data, str_size, wchar_str);
                wchar_str[str_size] = L'\0';
                os << wchar_str;
                
                obj.m_allocator.deallocate(wstr, (str_size + 1) * sizeof(wchar_t));
            }
        #endif

        return os;
    }


public:
    using iterator = knst_iterator<char16_t>; // The iterator typedef is `const_iterator<char16_t>`. `begin()`/`end()` return this. For standard STL compatibility
    using const_iterator = knst_const_iterator<char16_t>; // The const_iterator typedef — knst_const_iterator<char16_t>. cbegin()/cend() and const methods return this

    KNST_FORCE_INLINE bool is_heap() const noexcept{ // A check to determine whether it is SSO or heap. If the highest bit (0x80) of `m_flag` is 1, it is on the heap; if 0, it is on the stack. A single-bit check—fast and elegant

        return (this->stack_data.m_flag & 0x80) != 0; // If the highest bit is 1, it means we are in heap mode and it returns true
    }


    const KNST_FORCE_INLINE char16_t* data() const noexcept{ // Returns the raw `char16_t*` pointer of the `String`. It points to heap data if stored on the heap, or to the stack buffer if using SSO. It is the fundamental accessor for all read operations

        return is_heap() ? get_real_heap_m_data() : this->stack_data.m_real_data;

    }

    KNST_FORCE_INLINE uint32_t length() const noexcept { // Returns the character count of the string. It is `m_length` if stored on the heap, or the lower 7 bits of `m_flag` if using SSO. Excludes the null terminator

        return is_heap() ? this->heap_data.m_length : get_sso_length();
    } 

    KNST_FORCE_INLINE uint32_t empty() const noexcept { // Check if the string is empty. Returns true if length() == 0

        return length() == 0;
    } 
    
    KNST_FORCE_INLINE uint32_t capacity() const noexcept { // Returns the capacity of the string. m_capacity in Heap, KNST_SSO_BUFFER_CAPACITY (fixed, 11 default) in SSO. Null terminator included
 
       return is_heap() ? this->heap_data.m_capacity : KNST_SSO_BUFFER_CAPACITY;

    }

    KNST_FORCE_INLINE char* to_char() const noexcept { // Converts a UTF-16 string to a UTF-8 `char*`. It allocates new memory, performs the conversion, and adds a null terminator. The returned pointer is the caller's responsibility and must be deallocated using the allocator. Required for POSIX paths
        const char16_t* str_data = data();
        uint32_t str_size = length();
        uint32_t char_str_size = knst_get_utf16_to_utf8_exact_byte_size(str_data, str_size);
        
       
        void* str = this->m_allocator.allocate(char_str_size + 1);
        if (!str) return nullptr;
        
        char* char_str = static_cast<char*>(str);
        knst_convert_utf16_to_utf8(str_data, str_size, char_str);
        char_str[char_str_size] = '\0';
        
        return char_str; 
    }

    KNST_FORCE_INLINE wchar_t* to_wchar_alloc() const noexcept { // Converts a UTF-16 string to `wchar_t*`. On Windows, since `wchar_t` is UTF-16, a direct `memcpy` is used; on POSIX, `wchar_t` is UTF-32, so conversion is required. It allocates new memory—the caller is responsible for deallocation. Used for passing strings to Windows APIs
        const char16_t* str_data = data();
        uint32_t str_size = length();
        
    #if KNST_USING_PLATFORM_WINDOWS
       
        wchar_t* result = static_cast<wchar_t*>(
            this->m_allocator.allocate((str_size + 1) * sizeof(wchar_t))
        );
        if (!result) return nullptr;
        
        memcpy(result, str_data, str_size * sizeof(wchar_t));
        result[str_size] = L'\0';
        return result;
    #else
       
        wchar_t* result = static_cast<wchar_t*>(
            this->m_allocator.allocate((str_size + 1) * sizeof(wchar_t))
        );
        if (!result) return nullptr;
        
        knst_convert_utf16_to_wchar(str_data, str_size, result);
        result[str_size] = L'\0';
        return result;
    #endif
    }


    KNST_FORCE_INLINE void clear() noexcept{ // It clears the string. If it is on the heap, it frees the memory, then switches to SSO mode and sets the first character to null. In other words, the string is reset and can be reused

        if(is_heap()){
            m_destructor();
        }
        set_stack_mode(0);
        this->stack_data.m_real_data[0] = u'\0';

    }
  
    KNST_FORCE_INLINE bool detach() noexcept{ // COW detach — detaches the string from shared storage. Always returns `true` if COW is disabled. If enabled: calls `m_detach()` (creating a new buffer) if the data is on the heap and the reference count > 1; otherwise, returns `false` (as it is already exclusive). A guarantee before writing

        #ifdef KNST_C16STRING_DEACTIVE_COW

            return true;

        #else

            if(is_heap() && control_ref_count()){
                
                return m_detach();

            }
            return false;
        #endif

      
    }

   
 

    KNST_FORCE_INLINE ~basic_c16string() noexcept{ // Destructor

       if(is_heap()){
           m_destructor();
       }

    }


    template<typename OtherAlloc>
    KNST_FORCE_INLINE bool bridge_memory(OtherAlloc& alloc) { // Moves the string to another allocator. If it is on the heap, it allocates memory using the new allocator, copies the data, and deallocates the old memory. In the case of SSO, it simply changes the allocator. Returns false if the allocator types differ. There are three versions (lvalue, const lvalue, rvalue)
        if constexpr (std::is_same_v<Allocator, OtherAlloc>) {
            if (is_heap()) {
                uint32_t old_length = this->heap_data.m_length;
                uint32_t old_capacity = this->heap_data.m_capacity;
                
                if (old_length <= KNST_SSO_BUFFER_LENGTH) {
                    char16_t temp[KNST_SSO_BUFFER_CAPACITY];
                    memcpy(temp, get_real_heap_m_data(), old_length * sizeof(char16_t));
                    temp[old_length] = u'\0';
                    
                    m_destructor();
                    m_allocator = alloc;
                    
                    set_stack_mode(old_length);
                    memcpy(this->stack_data.m_real_data, temp, old_length * sizeof(char16_t));
                    this->stack_data.m_real_data[old_length] = u'\0';
                } else {
                    void* new_heap = alloc.allocate(get_new_heap_size(old_capacity));
                    if (!new_heap) return false;
                    
                    #ifdef KNST_C16STRING_DEACTIVE_COW
                        memcpy(new_heap, get_real_heap_m_data(), old_length * sizeof(char16_t));
                    #else
                        memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data, 
                            get_real_heap_m_data(), old_length * sizeof(char16_t));
                    #endif
                    
                    m_destructor();
                    m_allocator = alloc;
                    
                    set_real_heap_m_data(new_heap);
                    get_real_heap_m_data()[old_length] = u'\0';
                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        set_cow_ref_count(1);
                    #endif
                    this->heap_data.m_length = old_length;
                    this->heap_data.m_capacity = old_capacity;
                    set_heap_mode();
                }
            } else {
                m_allocator = alloc;
            }
            return true;
        } else {
            return false;
        }
    }

   
    template<typename OtherAlloc>
    KNST_FORCE_INLINE bool bridge_memory(const OtherAlloc& alloc) { // The const lvalue version of bridge_memory. The same logic applies—it moves the string to another allocator. It simply takes a const OtherAlloc& parameter
        if constexpr (std::is_same_v<Allocator, OtherAlloc>) {
            if (is_heap()) {
                uint32_t old_length = this->heap_data.m_length;
                uint32_t old_capacity = this->heap_data.m_capacity;
                
                if (old_length <= KNST_SSO_BUFFER_LENGTH) {
                    char16_t temp[KNST_SSO_BUFFER_CAPACITY];
                    memcpy(temp, get_real_heap_m_data(), old_length * sizeof(char16_t));
                    temp[old_length] = u'\0';
                    
                    m_destructor();
                    m_allocator = alloc;
                    
                    set_stack_mode(old_length);
                    memcpy(this->stack_data.m_real_data, temp, old_length * sizeof(char16_t));
                    this->stack_data.m_real_data[old_length] = u'\0';
                } else {
                void* new_heap = alloc.allocate(get_new_heap_size(old_capacity));
                    if (!new_heap) return false;
                    
                    #ifdef KNST_C16STRING_DEACTIVE_COW
                        memcpy(new_heap, get_real_heap_m_data(), old_length * sizeof(char16_t));
                    #else
                        memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data, 
                            get_real_heap_m_data(), old_length * sizeof(char16_t));
                    #endif
                    
                    m_destructor();
                    m_allocator = alloc;
                    
                    set_real_heap_m_data(new_heap);
                    get_real_heap_m_data()[old_length] = u'\0';
                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        set_cow_ref_count(1);
                    #endif
                    this->heap_data.m_length = old_length;
                    this->heap_data.m_capacity = old_capacity;
                    set_heap_mode();
                }
            } else {
                m_allocator = alloc;
            }
            return true;
        } else {
            return false;
        }
    }

  
    template<typename OtherAlloc>
    KNST_FORCE_INLINE bool bridge_memory(OtherAlloc&& alloc) { // The rvalue version of bridge_memory. It moves the allocator (std::move(alloc)). The logic is the same—it moves the data based on whether it is in the heap or SSO state. It works efficiently with temporary allocators; there is no copying
        if constexpr (std::is_same_v<Allocator, std::decay_t<OtherAlloc>>) {
            if (is_heap()) {
                uint32_t old_length = this->heap_data.m_length;
                uint32_t old_capacity = this->heap_data.m_capacity;
                
                if (old_length <= KNST_SSO_BUFFER_LENGTH) {
                    char16_t temp[KNST_SSO_BUFFER_CAPACITY];
                    memcpy(temp, get_real_heap_m_data(), old_length * sizeof(char16_t));
                    temp[old_length] = u'\0';
                    
                    m_destructor();
                    m_allocator = std::move(alloc);
                    
                    set_stack_mode(old_length);
                    memcpy(this->stack_data.m_real_data, temp, old_length * sizeof(char16_t));
                    this->stack_data.m_real_data[old_length] = u'\0';
                } else {
                    void* new_heap = alloc.allocate(get_new_heap_size(old_capacity));
                    if (!new_heap) return false;
                    
                    #ifdef KNST_C16STRING_DEACTIVE_COW
                        memcpy(new_heap, get_real_heap_m_data(), old_length * sizeof(char16_t));
                    #else
                        memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data, 
                            get_real_heap_m_data(), old_length * sizeof(char16_t));
                    #endif
                    
                    m_destructor();
                    m_allocator = std::move(alloc);
                    
                    set_real_heap_m_data(new_heap);
                    get_real_heap_m_data()[old_length] = u'\0';
                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        set_cow_ref_count(1);
                    #endif
                    this->heap_data.m_length = old_length;
                    this->heap_data.m_capacity = old_capacity;
                    set_heap_mode();
                }
            } else {
                m_allocator = std::move(alloc);
            }
            return true;
        } else {
            return false;
        }
    }

   
    KNST_FORCE_INLINE size_t pool_count() const { // Returns the number of pools for the allocator. Indicates how many size pools exist for strings using a pool allocator. Intended for debugging/statistics
        return m_allocator.pool_count();
    }


    KNST_FORCE_INLINE size_t max_block_size() const { // Returns the allocator's maximum block size. It indicates the maximum size for which fast allocations can be performed in the pool allocator
        return m_allocator.max_block_size();
    }


    inline bool append(const basic_c16string & new_append_str) noexcept{ // Appends another c16string to the end of this string. If there is sufficient space, it copies directly; otherwise, it doubles the capacity (or increases it sufficiently). If COW is enabled, it detaches from shared storage via `m_detach` before writing. The transition from SSO to heap storage happens automatically. The new `m_length` and `m_capacity` values ​​are updated

        uint32_t old_str_length = length();
        uint32_t other_str_length = new_append_str.length();
        uint32_t new_str_length = old_str_length + other_str_length;
        
        
        

        if(new_str_length < capacity()){
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                memcpy(get_real_heap_m_data() + old_str_length , new_append_str.data() , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
                this->heap_data.m_length = new_str_length;
            }
            else{
                memcpy(this->stack_data.m_real_data + old_str_length , new_append_str.data() , other_str_length * sizeof(char16_t));
                this->stack_data.m_real_data[new_str_length] = u'\0';
                set_stack_mode(new_str_length);
            }
            
        }
        else{
            
            uint32_t new_capacity = capacity() * 2;

           
            if(new_capacity < new_str_length + 1) {
                new_capacity = new_str_length + 1;
            }

            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                void * new_heap = this->m_allocator.reallocate(get_heap_m_data_for_begin(),get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                set_real_heap_m_data(new_heap);

                memcpy(get_real_heap_m_data() + old_str_length , new_append_str.data() , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
            }
            else{

                void * new_heap = this->m_allocator.allocate(get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                #ifdef KNST_C16STRING_DEACTIVE_COW
                    memcpy(new_heap,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #else
                    memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #endif

               
                set_real_heap_m_data(new_heap);
                memcpy(get_real_heap_m_data() + old_str_length , new_append_str.data() , other_str_length * sizeof(char16_t));
                
                get_real_heap_m_data()[new_str_length] = u'\0';
            }

            
            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = new_str_length;
            this->heap_data.m_capacity = new_capacity;

            set_heap_mode();

        }


        return true;
    }

    inline bool append(const char16_t * str) noexcept{ // Appends a null-terminated `char16_t*` string. It measures the length using `knst_get_str_length`, then follows the same logic as `append(basic_c16string)`: copy if there is space, or double the capacity if not. COW detachment and the transition between SSO and heap storage all work the same way

        uint32_t old_str_length = length();
        uint32_t other_str_length = knst_get_str_length(str);
        uint32_t new_str_length = old_str_length + other_str_length;
        
        
        if(new_str_length < capacity()){
            
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
                this->heap_data.m_length = new_str_length;
            }
            else{
                memcpy(this->stack_data.m_real_data + old_str_length , str , other_str_length * sizeof(char16_t));
                this->stack_data.m_real_data[new_str_length] = u'\0';
                set_stack_mode(new_str_length);
            }
            
        }
        else{
           
            uint32_t new_capacity = capacity() * 2;

           
            if(new_capacity < new_str_length + 1) {
                new_capacity = new_str_length + 1;
            }
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                
                void * new_heap = this->m_allocator.reallocate(get_heap_m_data_for_begin(),get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                set_real_heap_m_data(new_heap);
                    
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
            }
            else{

                void * new_heap = this->m_allocator.allocate(get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                #ifdef KNST_C16STRING_DEACTIVE_COW
                    memcpy(new_heap,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #else
                    memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #endif

               
                set_real_heap_m_data(new_heap);
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                
                get_real_heap_m_data()[new_str_length] = u'\0';
            }

            
            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = new_str_length;
            this->heap_data.m_capacity = new_capacity;

            set_heap_mode();

        }

        return true;
    }

    inline bool append(const char * str) noexcept{ // It converts a UTF-8 `char*` string to UTF-16 and appends it. It first calculates the UTF-8 byte count, then the UTF-16 character count (`utf16_char_size`). If there is space, it converts directly; otherwise, it doubles the capacity. It performs the conversion using `knst_convert_utf8_to_utf16`. The COW/SSO logic remains the same

        uint32_t old_str_length = length();
        uint32_t utf8_byte_count = knst_get_str_length(str);
        uint32_t utf16_char_size = knst_get_utf8_to_utf16_exact_length(str,utf8_byte_count);
        uint32_t new_str_length = old_str_length + utf16_char_size;
        
       
        

        if(new_str_length < capacity()){

           
            
            

            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                knst_convert_utf8_to_utf16(str,utf8_byte_count ,get_real_heap_m_data() + old_str_length);
                get_real_heap_m_data()[new_str_length] = u'\0';
                this->heap_data.m_length = new_str_length;
            }
            else{
                knst_convert_utf8_to_utf16(str,utf8_byte_count ,this->stack_data.m_real_data + old_str_length);
                this->stack_data.m_real_data[new_str_length] = u'\0';
                set_stack_mode(new_str_length);
            }
            
        }
        else{
           
            uint32_t new_capacity = capacity() * 2;

           
            if(new_capacity < new_str_length + 1) {
                new_capacity = new_str_length + 1;
            }

            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                void * new_heap = this->m_allocator.reallocate(get_heap_m_data_for_begin(),get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                set_real_heap_m_data(new_heap);

                knst_convert_utf8_to_utf16(str,utf8_byte_count ,get_real_heap_m_data() + old_str_length);
                get_real_heap_m_data()[new_str_length] = u'\0';
            }
            else{

                void * new_heap = this->m_allocator.allocate(get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                #ifdef KNST_C16STRING_DEACTIVE_COW
                    memcpy(new_heap,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #else
                    memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                    
                #endif

               
                set_real_heap_m_data(new_heap);
              
                knst_convert_utf8_to_utf16(str,utf8_byte_count ,get_real_heap_m_data() + old_str_length);
                get_real_heap_m_data()[new_str_length] = u'\0';
            }

            
            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = new_str_length;
            this->heap_data.m_capacity = new_capacity;

            set_heap_mode();

        }


        return true;
    }

    inline bool append(const wchar_t * str) noexcept{ // Converts a `wchar_t*` string to UTF-16 and appends it. On Windows, `wchar_t` is UTF-16 (direct mapping); on POSIX, it involves a UTF-32 to UTF-16 conversion. The length is calculated beforehand using `knst_get_wchar_to_utf16_exact_length`. The rest of the logic is identical to `append(char*)`

        uint32_t old_str_length = length();
        uint32_t wchar_count = knst_get_str_length(str);
        uint32_t utf16_char_size = knst_get_wchar_to_utf16_exact_length(str,wchar_count);
        uint32_t new_str_length = old_str_length + utf16_char_size;
    
        

        if(new_str_length < capacity()){
            

            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                knst_convert_wchar_to_utf16(str,wchar_count ,get_real_heap_m_data() + old_str_length);
                get_real_heap_m_data()[new_str_length] = u'\0';
                this->heap_data.m_length = new_str_length;
            }
            else{
                knst_convert_wchar_to_utf16(str,wchar_count ,this->stack_data.m_real_data + old_str_length);
                this->stack_data.m_real_data[new_str_length] = u'\0';
                set_stack_mode(new_str_length);
            }
            
        }
        else{
           
            uint32_t new_capacity = capacity() * 2;

           
            if(new_capacity < new_str_length + 1) {
                new_capacity = new_str_length + 1;
            }
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                void * new_heap = this->m_allocator.reallocate(get_heap_m_data_for_begin(),get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                set_real_heap_m_data(new_heap);

                knst_convert_wchar_to_utf16(str,wchar_count ,get_real_heap_m_data() + old_str_length);
                get_real_heap_m_data()[new_str_length] = u'\0';
            }
            else{

                void * new_heap = this->m_allocator.allocate(get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                #ifdef KNST_C16STRING_DEACTIVE_COW
                    memcpy(new_heap,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #else
                    memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                    
                #endif

               
                set_real_heap_m_data(new_heap);
              
                knst_convert_wchar_to_utf16(str,wchar_count ,get_real_heap_m_data() + old_str_length);
                get_real_heap_m_data()[new_str_length] = u'\0';
            }

            
            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = new_str_length;
            this->heap_data.m_capacity = new_capacity;

            set_heap_mode();

        }


        return true;
    }
    
    inline bool append(const char32_t * str) noexcept{ // Converts a char32_t* (UTF-32) string to UTF-16 and appends it. First, the length is determined using knst_get_char32_to_utf16_exact_length, followed by the conversion using knst_convert_char32_to_utf16. Emojis and rare characters are converted from 4-byte UTF-32 to a 2×char16_t surrogate pair. It follows the same structure as the other append functions

        uint32_t old_str_length = length();
        uint32_t char32_count = knst_get_str_length(str);
        uint32_t utf16_char_size = knst_get_char32_to_utf16_exact_length(str,char32_count);
        uint32_t new_str_length = old_str_length + utf16_char_size;
    
        

        if(new_str_length < capacity()){
            

            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                knst_convert_char32_to_utf16(str,char32_count ,get_real_heap_m_data() + old_str_length);
                get_real_heap_m_data()[new_str_length] = u'\0';
                this->heap_data.m_length = new_str_length;
            }
            else{
                knst_convert_char32_to_utf16(str,char32_count ,this->stack_data.m_real_data + old_str_length);
                this->stack_data.m_real_data[new_str_length] = u'\0';
                set_stack_mode(new_str_length);
            }
            
        }
        else{
          
            uint32_t new_capacity = capacity() * 2;

           
            if(new_capacity < new_str_length + 1) {
                new_capacity = new_str_length + 1;
            }
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                void * new_heap = this->m_allocator.reallocate(get_heap_m_data_for_begin(),get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                set_real_heap_m_data(new_heap);

                knst_convert_char32_to_utf16(str,char32_count ,get_real_heap_m_data() + old_str_length);
                get_real_heap_m_data()[new_str_length] = u'\0';
            }
            else{

                void * new_heap = this->m_allocator.allocate(get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                #ifdef KNST_C16STRING_DEACTIVE_COW
                    memcpy(new_heap,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #else
                    memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                    
                #endif

               
                set_real_heap_m_data(new_heap);
              
                knst_convert_char32_to_utf16(str,char32_count ,get_real_heap_m_data() + old_str_length);
                get_real_heap_m_data()[new_str_length] = u'\0';
            }

            
            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = new_str_length;
            this->heap_data.m_capacity = new_capacity;

            set_heap_mode();

        }


        return true;
    }


    inline bool append(int value) noexcept{ // It converts the `int` value to a string and appends it to the end. It performs a fast conversion without `snprintf` by writing the digits from back to front (starting from `buffer + 15`). It supports negative signs. Then, it follows standard append logic—using the `+` operator to write the number into the string

        char16_t buffer[16];
        char16_t* str = buffer + 15;
        *str = u'\0';

        bool negative = (value < 0);
        unsigned int uval = negative ? -static_cast<unsigned int>(value) : value;


        do{
            *--str = static_cast<char16_t>('0' + (uval % 10));
            uval /=10;

        }while(uval > 0);

        if(negative) *--str = u'-';
        uint32_t other_str_length = static_cast<uint32_t>(buffer + 15 - str);

        uint32_t old_str_length = length();
        
        uint32_t new_str_length = old_str_length + other_str_length;
        
        
        if(new_str_length < capacity()){
            
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
                this->heap_data.m_length = new_str_length;
            }
            else{
                memcpy(this->stack_data.m_real_data + old_str_length , str , other_str_length * sizeof(char16_t));
                this->stack_data.m_real_data[new_str_length] = u'\0';
                set_stack_mode(new_str_length);
            }
            
        }
        else{
           
            uint32_t new_capacity = capacity() * 2;

           
            if(new_capacity < new_str_length + 1) {
                new_capacity = new_str_length + 1;
            }
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                
                void * new_heap = this->m_allocator.reallocate(get_heap_m_data_for_begin(),get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                set_real_heap_m_data(new_heap);
                    
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
            }
            else{

                void * new_heap = this->m_allocator.allocate(get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                #ifdef KNST_C16STRING_DEACTIVE_COW
                    memcpy(new_heap,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #else
                    memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #endif

               
                set_real_heap_m_data(new_heap);
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                
                get_real_heap_m_data()[new_str_length] = u'\0';
            }

            
            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = new_str_length;
            this->heap_data.m_capacity = new_capacity;

            set_heap_mode();

        }

        return true;

    }

    inline bool append(long value) noexcept{ // Converts the `long` value to a string and appends it. It follows the same logic as the `int` version, but uses a `buffer[32]` (since a `long` can be larger) and an `unsigned long` cast. It may be 32-bit or 64-bit depending on the platform. It shares the same queue as the other `append` operations
        char16_t buffer[32];
        char16_t* str = buffer + 31;
        *str = u'\0';

        bool negative = (value < 0);
        unsigned long uval = negative ? -static_cast<unsigned long>(value) : value;

        do{
            *--str = static_cast<char16_t>('0' + (uval % 10));
            uval /=10;
        }while(uval > 0);

        if(negative) *--str = u'-';
        uint32_t other_str_length = static_cast<uint32_t>(buffer + 31 - str);
        
        uint32_t old_str_length = length();

        uint32_t new_str_length = old_str_length + other_str_length;
        
        
        if(new_str_length < capacity()){
            
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
                this->heap_data.m_length = new_str_length;
            }
            else{
                memcpy(this->stack_data.m_real_data + old_str_length , str , other_str_length * sizeof(char16_t));
                this->stack_data.m_real_data[new_str_length] = u'\0';
                set_stack_mode(new_str_length);
            }
            
        }
        else{
           
            uint32_t new_capacity = capacity() * 2;

           
            if(new_capacity < new_str_length + 1) {
                new_capacity = new_str_length + 1;
            }
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                
                void * new_heap = this->m_allocator.reallocate(get_heap_m_data_for_begin(),get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                set_real_heap_m_data(new_heap);
                    
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
            }
            else{

                void * new_heap = this->m_allocator.allocate(get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                #ifdef KNST_C16STRING_DEACTIVE_COW
                    memcpy(new_heap,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #else
                    memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #endif

               
                set_real_heap_m_data(new_heap);
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                
                get_real_heap_m_data()[new_str_length] = u'\0';
            }

            
            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = new_str_length;
            this->heap_data.m_capacity = new_capacity;

            set_heap_mode();

        }

        return true;
        
    }

    inline bool append(unsigned long value) noexcept{ // Converts an unsigned long value to a string and appends it. There is no check for negative values ​​(it is always positive). The digits are processed from last to first, followed by standard append logic
        char16_t buffer[32];
        char16_t* str = buffer + 31;
        *str = u'\0';

        unsigned long uval = value;

        do{
            *--str = static_cast<char16_t>('0' + (uval % 10));
            uval /=10;
        }while(uval > 0);

        uint32_t other_str_length = static_cast<uint32_t>(buffer + 31 - str);
        uint32_t old_str_length = length();

        uint32_t new_str_length = old_str_length + other_str_length;
        
        
        if(new_str_length < capacity()){
            
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
                this->heap_data.m_length = new_str_length;
            }
            else{
                memcpy(this->stack_data.m_real_data + old_str_length , str , other_str_length * sizeof(char16_t));
                this->stack_data.m_real_data[new_str_length] = u'\0';
                set_stack_mode(new_str_length);
            }
            
        }
        else{
           
            uint32_t new_capacity = capacity() * 2;

           
            if(new_capacity < new_str_length + 1) {
                new_capacity = new_str_length + 1;
            }
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                
                void * new_heap = this->m_allocator.reallocate(get_heap_m_data_for_begin(),get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                set_real_heap_m_data(new_heap);
                    
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
            }
            else{

                void * new_heap = this->m_allocator.allocate(get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                #ifdef KNST_C16STRING_DEACTIVE_COW
                    memcpy(new_heap,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #else
                    memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #endif

               
                set_real_heap_m_data(new_heap);
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                
                get_real_heap_m_data()[new_str_length] = u'\0';
            }

            
            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = new_str_length;
            this->heap_data.m_capacity = new_capacity;

            set_heap_mode();

        }

        return true;

    }

    inline bool append(long long value) noexcept{ // Converts a long long value to a string and appends it. Supports 64-bit integers, including negative values. Uses the same logic as the long version

        char16_t buffer[32];
        char16_t* str = buffer + 31;
        *str = u'\0';

        bool negative = (value < 0);
        long long uval = negative ? -static_cast<long long>(value) : value;


        do{
            *--str = static_cast<char16_t>('0' + (uval % 10));
            uval /=10;

        }while(uval > 0);

        if(negative) *--str = u'-';
        uint32_t other_str_length = static_cast<uint32_t>(buffer + 31 - str);

        uint32_t old_str_length = length();

        uint32_t new_str_length = old_str_length + other_str_length;
        
        
        if(new_str_length < capacity()){
            
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
                this->heap_data.m_length = new_str_length;
            }
            else{
                memcpy(this->stack_data.m_real_data + old_str_length , str , other_str_length * sizeof(char16_t));
                this->stack_data.m_real_data[new_str_length] = u'\0';
                set_stack_mode(new_str_length);
            }
            
        }
        else{
           
            uint32_t new_capacity = capacity() * 2;

           
            if(new_capacity < new_str_length + 1) {
                new_capacity = new_str_length + 1;
            }
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                
                void * new_heap = this->m_allocator.reallocate(get_heap_m_data_for_begin(),get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                set_real_heap_m_data(new_heap);
                    
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
            }
            else{

                void * new_heap = this->m_allocator.allocate(get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                #ifdef KNST_C16STRING_DEACTIVE_COW
                    memcpy(new_heap,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #else
                    memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #endif

               
                set_real_heap_m_data(new_heap);
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                
                get_real_heap_m_data()[new_str_length] = u'\0';
            }

            
            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = new_str_length;
            this->heap_data.m_capacity = new_capacity;

            set_heap_mode();

        }

        return true;

    }

    inline bool append(unsigned int value) noexcept{ // Converts an unsigned int value to a string and appends it. Since it is unsigned, a buffer of size 16 suffices. It follows the same structure as append(int), simply omitting the sign-handling step

        char16_t buffer[16];
        char16_t* str = buffer + 15;
        *str = u'\0';


        unsigned int uval =  value;


        do{
            *--str = static_cast<char16_t>('0' + (uval % 10));
            uval /=10;

        }while(uval > 0);


        uint32_t other_str_length = static_cast<uint32_t>(buffer + 15 - str);

        uint32_t old_str_length = length();

        uint32_t new_str_length = old_str_length + other_str_length;
        
        
        if(new_str_length < capacity()){
            
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
                this->heap_data.m_length = new_str_length;
            }
            else{
                memcpy(this->stack_data.m_real_data + old_str_length , str , other_str_length * sizeof(char16_t));
                this->stack_data.m_real_data[new_str_length] = u'\0';
                set_stack_mode(new_str_length);
            }
            
        }
        else{
           
            uint32_t new_capacity = capacity() * 2;

           
            if(new_capacity < new_str_length + 1) {
                new_capacity = new_str_length + 1;
            }
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                
                void * new_heap = this->m_allocator.reallocate(get_heap_m_data_for_begin(),get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                set_real_heap_m_data(new_heap);
                    
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
            }
            else{

                void * new_heap = this->m_allocator.allocate(get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                #ifdef KNST_C16STRING_DEACTIVE_COW
                    memcpy(new_heap,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #else
                    memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #endif

               
                set_real_heap_m_data(new_heap);
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                
                get_real_heap_m_data()[new_str_length] = u'\0';
            }

            
            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = new_str_length;
            this->heap_data.m_capacity = new_capacity;

            set_heap_mode();

        }

        return true;

    }


    inline bool append(unsigned long long value) noexcept{ // Converts an unsigned long long value to a string and appends it. 64-bit, unsigned. A buffer of size 32 is sufficient (max 20 digits). Same structure

        char16_t buffer[32];
        char16_t* str = buffer + 31;
        *str = u'\0';


        unsigned long long uval =  value;


        do{
            *--str = static_cast<char16_t>('0' + (uval % 10));
            uval /=10;

        }while(uval > 0);


        uint32_t other_str_length = static_cast<uint32_t>(buffer + 31 - str);

        uint32_t old_str_length = length();

        uint32_t new_str_length = old_str_length + other_str_length;
        
        
        if(new_str_length < capacity()){
            
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
                this->heap_data.m_length = new_str_length;
            }
            else{
                memcpy(this->stack_data.m_real_data + old_str_length , str , other_str_length * sizeof(char16_t));
                this->stack_data.m_real_data[new_str_length] = u'\0';
                set_stack_mode(new_str_length);
            }
            
        }
        else{
           
            uint32_t new_capacity = capacity() * 2;

           
            if(new_capacity < new_str_length + 1) {
                new_capacity = new_str_length + 1;
            }
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                
                void * new_heap = this->m_allocator.reallocate(get_heap_m_data_for_begin(),get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                set_real_heap_m_data(new_heap);
                    
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                get_real_heap_m_data()[new_str_length] = u'\0';
            }
            else{

                void * new_heap = this->m_allocator.allocate(get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                #ifdef KNST_C16STRING_DEACTIVE_COW
                    memcpy(new_heap,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #else
                    memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data,this->stack_data.m_real_data,old_str_length * sizeof(char16_t));
                #endif

               
                set_real_heap_m_data(new_heap);
                memcpy(get_real_heap_m_data() + old_str_length , str , other_str_length * sizeof(char16_t));
                
                get_real_heap_m_data()[new_str_length] = u'\0';
            }

            
            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = new_str_length;
            this->heap_data.m_capacity = new_capacity;

            set_heap_mode();

        }

        return true;

    }

    inline bool append(float value) noexcept{ // It converts a float value to a string and appends it. It formats the value using `snprintf("%g")` (compact notation) and then converts the `char` bytes to `char16_t`. It differs from other numeric append operations by using `snprintf` and performing the cast individually within a loop
        char buffer[32];
        int len = snprintf(buffer, sizeof(buffer), "%g", value);

        if(len <= 0) {
            return false;
        }
        
        uint32_t other_str_length = static_cast<uint32_t>(len);
        uint32_t old_str_length = length();
        uint32_t new_str_length = old_str_length + other_str_length;
        
        if(new_str_length < capacity()){
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                char16_t* dst = get_real_heap_m_data() + old_str_length;
                for(uint32_t i = 0; i < other_str_length; ++i) {
                    dst[i] = static_cast<char16_t>(buffer[i]);
                }
                dst[other_str_length] = u'\0';
                this->heap_data.m_length = new_str_length;
            }
            else{
                char16_t* dst = this->stack_data.m_real_data + old_str_length;
                for(uint32_t i = 0; i < other_str_length; ++i) {
                    dst[i] = static_cast<char16_t>(buffer[i]);
                }
                dst[other_str_length] = u'\0';
                set_stack_mode(new_str_length);
            }
        }
        else{
            uint32_t new_capacity = capacity() * 2;
            if(new_capacity < new_str_length + 1) {
                new_capacity = new_str_length + 1;
            }
            
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                
                void* new_heap = this->m_allocator.reallocate(get_heap_m_data_for_begin(), get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                set_real_heap_m_data(new_heap);
                char16_t* dst = get_real_heap_m_data() + old_str_length;
                for(uint32_t i = 0; i < other_str_length; ++i) {
                    dst[i] = static_cast<char16_t>(buffer[i]);
                }
                dst[other_str_length] = u'\0';
            }
            else{
                void* new_heap = this->m_allocator.allocate(get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                #ifdef KNST_C16STRING_DEACTIVE_COW
                    memcpy(new_heap, this->stack_data.m_real_data, old_str_length * sizeof(char16_t));
                #else
                    memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data, this->stack_data.m_real_data, old_str_length * sizeof(char16_t));
                #endif

                set_real_heap_m_data(new_heap);
                char16_t* dst = get_real_heap_m_data() + old_str_length;
                for(uint32_t i = 0; i < other_str_length; ++i) {
                    dst[i] = static_cast<char16_t>(buffer[i]);
                }
                dst[other_str_length] = u'\0';
            }

            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = new_str_length;
            this->heap_data.m_capacity = new_capacity;
            set_heap_mode();
        }

        return true;
    }

    inline bool append(double value) noexcept{ // Converts the double value to a string and appends it. It is the same as the float version, but uses a larger buffer (buffer[64]). The %g format provides a compact representation without unnecessary zeros.
        char buffer[64];
        int len = snprintf(buffer, sizeof(buffer), "%g", value);

        if(len <= 0) {
            return false;
        }
        
        uint32_t other_str_length = static_cast<uint32_t>(len);
        uint32_t old_str_length = length();
        uint32_t new_str_length = old_str_length + other_str_length;
        
        if(new_str_length < capacity()){
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                char16_t* dst = get_real_heap_m_data() + old_str_length;
                for(uint32_t i = 0; i < other_str_length; ++i) {
                    dst[i] = static_cast<char16_t>(buffer[i]);
                }
                dst[other_str_length] = u'\0';
                this->heap_data.m_length = new_str_length;
            }
            else{
                char16_t* dst = this->stack_data.m_real_data + old_str_length;
                for(uint32_t i = 0; i < other_str_length; ++i) {
                    dst[i] = static_cast<char16_t>(buffer[i]);
                }
                dst[other_str_length] = u'\0';
                set_stack_mode(new_str_length);
            }
        }
        else{
            uint32_t new_capacity = capacity() * 2;
            if(new_capacity < new_str_length + 1) {
                new_capacity = new_str_length + 1;
            }
            
            if(is_heap()){
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif
                
                void* new_heap = this->m_allocator.reallocate(get_heap_m_data_for_begin(), get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                set_real_heap_m_data(new_heap);
                char16_t* dst = get_real_heap_m_data() + old_str_length;
                for(uint32_t i = 0; i < other_str_length; ++i) {
                    dst[i] = static_cast<char16_t>(buffer[i]);
                }
                dst[other_str_length] = u'\0';
            }
            else{
                void* new_heap = this->m_allocator.allocate(get_new_heap_size(new_capacity));
                if(!new_heap) return false;
                
                #ifdef KNST_C16STRING_DEACTIVE_COW
                    memcpy(new_heap, this->stack_data.m_real_data, old_str_length * sizeof(char16_t));
                #else
                    memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data, this->stack_data.m_real_data, old_str_length * sizeof(char16_t));
                #endif

                set_real_heap_m_data(new_heap);
                char16_t* dst = get_real_heap_m_data() + old_str_length;
                for(uint32_t i = 0; i < other_str_length; ++i) {
                    dst[i] = static_cast<char16_t>(buffer[i]);
                }
                dst[other_str_length] = u'\0';
            }

            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = new_str_length;
            this->heap_data.m_capacity = new_capacity;
            set_heap_mode();
        }

        return true;
    }


    KNST_FORCE_INLINE basic_c16string substr(uint32_t pos , uint32_t count) const noexcept{ // Returns a substring. It starts at `pos` and takes at most `count` characters. If `pos` is out of bounds, it returns an empty string; if `count` exceeds the remaining length, it takes the rest. It creates a new `basic_c16string`

        uint32_t length_str = length();
        if(pos >= length_str) return basic_c16string();

      
        return basic_c16string(data() + pos , std::min(count,length_str - pos));
    }


    KNST_FORCE_INLINE bool reserve(uint32_t new_total_character_count) noexcept{ // Sets the capacity to at least `new_total_character_count + 1` (to account for the null terminator). Does nothing if the capacity is already sufficient. If currently on the heap, reallocates; if using SSO, allocates on the heap, copies the data, and switches to heap mode. Detaches if COW is enabled. Returns `false` on failure
        uint32_t new_cap = new_total_character_count +1;
        if(new_cap <= capacity()) return true;
        
        if(is_heap()){
            #ifndef KNST_C16STRING_DEACTIVE_COW
                if(control_ref_count()){
                    if(!m_detach()) return false;
                }
            #endif
            void * new_heap = this->m_allocator.reallocate(get_heap_m_data_for_begin(),get_new_heap_size(new_cap));
            if(!new_heap) return false;
            set_real_heap_m_data(new_heap);
            this->heap_data.m_capacity = new_cap;


        }
        else{

            void * new_heap = this->m_allocator.allocate(get_new_heap_size(new_cap));
            if(!new_heap) return false;
            uint32_t data_length = get_sso_length();
            #ifdef KNST_C16STRING_DEACTIVE_COW
                memcpy(new_heap, this->stack_data.m_real_data, data_length * sizeof(char16_t));
            #else
                memcpy(static_cast<SharingHeapData*>(new_heap)->m_real_data,this->stack_data.m_real_data, data_length * sizeof(char16_t));
            #endif
            set_real_heap_m_data(new_heap);
            get_real_heap_m_data()[data_length] = u'\0';
            this->heap_data.m_length = data_length;
            this->heap_data.m_capacity = new_cap;
           
            set_heap_mode();

        }
        

        return true;
    }

    


    //  The core of the `find` family. Character search ==> simple linear scan. String search ==> initial boundary checks; falls back to character search for single characters, uses `find_naive` for short patterns (≤8), and `find_two_way` for long ones. Supports offsets. All overloads delegate to these two


    KNST_FORCE_INLINE bool find(const char16_t* str, uint32_t offset = 0) const noexcept {
        return find(basic_c16string(str), offset);
    }
    KNST_FORCE_INLINE bool find(const char* str, uint32_t offset = 0) const noexcept {
        return find(basic_c16string(str), offset);
    }
    KNST_FORCE_INLINE bool find(const wchar_t* str, uint32_t offset = 0) const noexcept {
        return find(basic_c16string(str), offset);
    }
    KNST_FORCE_INLINE bool find(char chr, uint32_t offset = 0) const noexcept {
        return find(static_cast<char16_t>(chr), offset);
    }
    KNST_FORCE_INLINE bool find(wchar_t chr, uint32_t offset = 0) const noexcept {
        return find(static_cast<char16_t>(chr), offset);
    }
    KNST_FORCE_INLINE bool find(const char32_t* str, uint32_t offset = 0) const noexcept {
        return find(basic_c16string(str), offset);
    }
    KNST_FORCE_INLINE bool find(char32_t chr, uint32_t offset = 0) const noexcept {
        return find(static_cast<char16_t>(chr), offset);
    }

   
    KNST_FORCE_INLINE bool find(char16_t chr, uint32_t offset = 0) const noexcept {

        if(offset >= length()) return false;

        const char16_t* d   = data() + offset;
        const uint32_t  len = length() - offset;

        for(uint32_t i = 0; i < len; ++i){
            if(d[i] == chr) return true;
        }
        return false;
    }

   
    KNST_FORCE_INLINE bool find(const basic_c16string& pattern, uint32_t offset = 0) const noexcept {

        const uint32_t text_len = length();
        const uint32_t pat_len  = pattern.length();

       
        if(pat_len == 0)                        return true;
        if(offset >= text_len)                  return false;
        if(pat_len > text_len - offset)         return false;

        const char16_t* text = data() + offset;
        const uint32_t  slen = text_len - offset;

       
        if(pat_len == 1) return find(pattern.data()[0], offset);

        
        if(pat_len <= KNST_FIND_NAIVE_THRESHOLD){
            return find_naive(text, slen, pattern.data(), pat_len);
        }

       
        return find_two_way(text, slen, pattern.data(), pat_len);
    }





    // It includes overloads—an alias for `find`. It performs the same function but has a more readable name: "does it contain?". There is a separate overload for each type, and all of them delegate to `find`

    KNST_FORCE_INLINE bool contains(const char16_t* str) const noexcept {
        return find(str);
    }
    KNST_FORCE_INLINE bool contains(const char* str) const noexcept {
        return find(str);
    }
    KNST_FORCE_INLINE bool contains(const wchar_t* str) const noexcept {
        return find(str);
    }
    KNST_FORCE_INLINE bool contains(const char32_t* str) const noexcept {
        return find(str);
    }
    KNST_FORCE_INLINE bool contains(char16_t chr) const noexcept {
        return find(chr);
    }
    KNST_FORCE_INLINE bool contains(char chr) const noexcept {
        return find(chr);
    }
    KNST_FORCE_INLINE bool contains(wchar_t chr) const noexcept {
        return find(chr);
    }
    KNST_FORCE_INLINE bool contains(char32_t chr) const noexcept {
        return find(chr);
    }
    KNST_FORCE_INLINE bool contains(const basic_c16string& str) const noexcept {
        return find(str);
    }






    // starts_with — does the string begin with this prefix? The char16_t* version uses memcmp directly. Other types (char*, wchar_t*, char32_t*) are first converted to UTF-16: short prefixes use a stack buffer, while long ones involve heap allocation, memcmp, and deallocation. Length is checked first—if the prefix is ​​longer than the string, it immediately returns false.



    KNST_FORCE_INLINE bool starts_with(const char16_t * str) const noexcept{
        uint32_t text_len = knst_get_str_length(str);
        if(text_len > length()) return false;
        return memcmp(data(),str,text_len * sizeof(char16_t)) == 0;

    }

    KNST_FORCE_INLINE bool starts_with(const char * str) const noexcept{
        uint32_t utf8_byte_count = knst_get_str_length(str);
        uint32_t utf16_len = knst_get_utf8_to_utf16_exact_length(str,utf8_byte_count);
        if(utf16_len > length()) return false;
        
        if(utf16_len <= KNST_SSO_BUFFER_LENGTH){
            char16_t buffer[KNST_SSO_BUFFER_CAPACITY];
            knst_convert_utf8_to_utf16(str,utf8_byte_count,buffer);
            return memcmp(data(),buffer,utf16_len * sizeof(char16_t)) == 0;
        }
        else{
            uint32_t data_capacity = utf16_len * sizeof(char16_t);
            void * new_data = this->m_allocator.allocate(data_capacity);
            if(!new_data) return false;
            knst_convert_utf8_to_utf16(str,utf8_byte_count,static_cast<char16_t*>(new_data));
            bool result = memcmp(data(),new_data,data_capacity) == 0;
            this->m_allocator.deallocate(new_data,data_capacity);
            return result;

        }

    }
    KNST_FORCE_INLINE bool starts_with(const wchar_t * str) const noexcept{
        uint32_t wchar_count = knst_get_str_length(str);
        uint32_t utf16_len = knst_get_wchar_to_utf16_exact_length(str,wchar_count);
        if(utf16_len > length()) return false;
        
        if(utf16_len <= KNST_SSO_BUFFER_LENGTH){
            char16_t buffer[KNST_SSO_BUFFER_CAPACITY];
            knst_convert_wchar_to_utf16(str,wchar_count,buffer);
            return memcmp(data(),buffer,utf16_len * sizeof(char16_t)) == 0;
        }
        else{
            uint32_t data_capacity = utf16_len * sizeof(char16_t);
            void * new_data = this->m_allocator.allocate(data_capacity);
            if(!new_data) return false;
            knst_convert_wchar_to_utf16(str,wchar_count,static_cast<char16_t*>(new_data));
            bool result = memcmp(data(),new_data,data_capacity) == 0;
            this->m_allocator.deallocate(new_data,data_capacity);
            return result;

        }

    }
    KNST_FORCE_INLINE bool starts_with(const char32_t * str) const noexcept{
        uint32_t char32_count = knst_get_str_length(str);
        uint32_t utf16_len = knst_get_char32_to_utf16_exact_length(str,char32_count);
        if(utf16_len > length()) return false;
        
        if(utf16_len <= KNST_SSO_BUFFER_LENGTH){
            char16_t buffer[KNST_SSO_BUFFER_CAPACITY];
            knst_convert_char32_to_utf16(str,char32_count,buffer);
            return memcmp(data(),buffer,utf16_len * sizeof(char16_t)) == 0;
        }
        else{
            uint32_t data_capacity = utf16_len * sizeof(char16_t);
            void * new_data = this->m_allocator.allocate(data_capacity);
            if(!new_data) return false;
            knst_convert_char32_to_utf16(str,char32_count,static_cast<char16_t*>(new_data));
            bool result = memcmp(data(),new_data,data_capacity) == 0;
            this->m_allocator.deallocate(new_data,data_capacity);
            return result;

        }

    }




    // ends_with — does the string end with this suffix? The reverse of starts_with — memcmp is performed from the end (data() + length() - suffix_len). For other types, the UTF-16 conversion and stack/heap buffer logic remain the same. Length check comes first
    


    KNST_FORCE_INLINE bool ends_with(const char16_t * str) const noexcept{
        uint32_t text_len = knst_get_str_length(str);
        if(text_len > length()) return false;
        return memcmp(data() +length() - text_len,str,text_len * sizeof(char16_t)) == 0;

    }

    KNST_FORCE_INLINE bool ends_with(const char * str) const noexcept{
        uint32_t utf8_byte_count = knst_get_str_length(str);
        uint32_t utf16_len = knst_get_utf8_to_utf16_exact_length(str,utf8_byte_count);
        if(utf16_len > length()) return false;
        
        if(utf16_len <= KNST_SSO_BUFFER_LENGTH){
            char16_t buffer[KNST_SSO_BUFFER_CAPACITY];
            knst_convert_utf8_to_utf16(str,utf8_byte_count,buffer);
            return memcmp(data() +length() - utf16_len,buffer,utf16_len * sizeof(char16_t)) == 0;
        }
        else{
            uint32_t data_capacity = utf16_len * sizeof(char16_t);
            void * new_data = this->m_allocator.allocate(data_capacity);
            if(!new_data) return false;
            knst_convert_utf8_to_utf16(str,utf8_byte_count,static_cast<char16_t*>(new_data));
            bool result = memcmp(data() +length() - utf16_len,new_data,utf16_len * sizeof(char16_t)) == 0;
            this->m_allocator.deallocate(new_data,data_capacity);
            return result;

        }

    }
    KNST_FORCE_INLINE bool ends_with(const wchar_t * str) const noexcept{
        uint32_t wchar_count = knst_get_str_length(str);
        uint32_t utf16_len = knst_get_wchar_to_utf16_exact_length(str,wchar_count);
        if(utf16_len > length()) return false;
        
        if(utf16_len <= KNST_SSO_BUFFER_LENGTH){
            char16_t buffer[KNST_SSO_BUFFER_CAPACITY];
            knst_convert_wchar_to_utf16(str,wchar_count,buffer);
            return memcmp(data() +length() - utf16_len,buffer,utf16_len * sizeof(char16_t)) == 0;
        }
        else{
            uint32_t data_capacity = utf16_len * sizeof(char16_t);
            void * new_data = this->m_allocator.allocate(data_capacity);
            if(!new_data) return false;
            knst_convert_wchar_to_utf16(str,wchar_count,static_cast<char16_t*>(new_data));
            bool result = memcmp(data() +length() - utf16_len,new_data,utf16_len * sizeof(char16_t)) == 0;
            this->m_allocator.deallocate(new_data,data_capacity);
            return result;

        }

    }
    KNST_FORCE_INLINE bool ends_with(const char32_t * str) const noexcept{
        uint32_t char32_count = knst_get_str_length(str);
        uint32_t utf16_len = knst_get_char32_to_utf16_exact_length(str,char32_count);
        if(utf16_len > length()) return false;
        
        if(utf16_len <= KNST_SSO_BUFFER_LENGTH){
            char16_t buffer[KNST_SSO_BUFFER_CAPACITY];
            knst_convert_char32_to_utf16(str,char32_count,buffer);
            return memcmp(data() +length() - utf16_len,buffer,utf16_len * sizeof(char16_t)) == 0;
        }
        else{
            uint32_t data_capacity = utf16_len * sizeof(char16_t);
            void * new_data = this->m_allocator.allocate(data_capacity);
            if(!new_data) return false;
            knst_convert_char32_to_utf16(str,char32_count,static_cast<char16_t*>(new_data));
            bool result = memcmp(data() +length() - utf16_len,new_data,utf16_len * sizeof(char16_t)) == 0;
            this->m_allocator.deallocate(new_data,data_capacity);
            return result;

        }

    }






    // Single-character `starts_with` — is the first character equal? ​​`front() == chr`. Other types cast to `char16_t` and delegate. Simple and fast


    KNST_FORCE_INLINE bool starts_with(char16_t chr) const noexcept{

        return front() == chr;

    }
    KNST_FORCE_INLINE bool starts_with(char chr) const noexcept{

        return starts_with(static_cast<char16_t>(chr));

    }
    KNST_FORCE_INLINE bool starts_with(wchar_t chr) const noexcept{

        return starts_with(static_cast<char16_t>(chr));

    }
    KNST_FORCE_INLINE bool starts_with(char32_t chr) const noexcept{

        return starts_with(static_cast<char16_t>(chr));

    }





    // For a single character, `ends_with` checks if the last character is equal—i.e., `back() == chr`. For other types, it involves casting and delegation; it is essentially the "from-the-end" version of `starts_with`

    KNST_FORCE_INLINE bool ends_with(char16_t chr) const noexcept{

        return back() == chr;

    }
    KNST_FORCE_INLINE bool ends_with(char chr) const noexcept{

        return ends_with(static_cast<char16_t>(chr));

    }
    KNST_FORCE_INLINE bool ends_with(wchar_t chr) const noexcept{

        return ends_with(static_cast<char16_t>(chr));

    }
    KNST_FORCE_INLINE bool ends_with(char32_t chr) const noexcept{

        return ends_with(static_cast<char16_t>(chr));

    }



    KNST_FORCE_INLINE char16_t& at(uint32_t index) { // at — element access with bounds checking. Bounds checking via KNST_ASSERT (in debug mode). Detaches if COW is enabled (separates from shared state before writing). Returns a mutable reference via const_cast. Difference from operator[]: includes bounds checking (assert)
        KNST_ASSERT(index < length() && "basic_c16string::at: index out of range");
        
        #ifndef KNST_C16STRING_DEACTIVE_COW
            detach();
        #endif
        return const_cast<char16_t&>(data()[index]);
    }


    KNST_FORCE_INLINE char16_t& front() { // A mutable reference to the first character. `data()[0]` after a COW detach. Grants write access
      
        #ifndef KNST_C16STRING_DEACTIVE_COW
            detach();
        #endif
        return const_cast<char16_t&>(data()[0]);
    }


    KNST_FORCE_INLINE char16_t& back() { // Mutable reference to the last character. `data()[length()-1]` after COW detachment. Undefined behavior on an empty string
       
        
        #ifndef KNST_C16STRING_DEACTIVE_COW
            detach();
        #endif
        return const_cast<char16_t&>(data()[length() - 1]);
    }


    KNST_FORCE_INLINE const char16_t& at(uint32_t index) const { // The `const` version of `at()`. Bounds check (assert) + `const` reference. No writing involved; no need to detach
        KNST_ASSERT(index < length() && "basic_c16string::at: index out of range");
        return data()[index];
    }

    KNST_FORCE_INLINE const char16_t& front() const { // Do not read the first character. const reference, no detach
        
        return data()[0];
    }

    KNST_FORCE_INLINE const char16_t& back() const { // Do not read the last character. Const reference, no detach. Undefined behavior with an empty string
       
        return data()[length() - 1];
    }

    KNST_FORCE_INLINE void resize(uint32_t new_size) noexcept{ // A shortcut for resize — fills with the null character (u'\0'). Delegates to the actual resize(new_size, char16_t)

        resize(new_size,u'\0');
    }


    KNST_FORCE_INLINE bool resize(uint32_t new_size , char16_t fill_char) noexcept{ // Changes the string's length. Shortening: if it is on the heap and fits within SSO, moves it back to the stack; otherwise, simply places a null terminator. Extending: if capacity is sufficient, fills with `fill_char`; if not, calls `reserve` and then fills. COW detach occurs before every write operation. Adds a null terminator at `new_size`
        
        uint32_t str_lenght = length();


        if(new_size < str_lenght){

            // shortening

            if(is_heap()){

                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif

                if(new_size <=KNST_SSO_BUFFER_LENGTH){
                    #pragma GCC diagnostic push
                    #pragma GCC diagnostic ignored "-Wstringop-overread"
                    #pragma GCC diagnostic ignored "-Wstringop-overflow"
                    char16_t temp[KNST_SSO_BUFFER_CAPACITY];
                    memcpy(temp, get_real_heap_m_data(), new_size * sizeof(char16_t));
                    temp[new_size] = u'\0';
                    
                    m_destructor();
                    
                    set_stack_mode(new_size);
                    memcpy(this->stack_data.m_real_data, temp, new_size * sizeof(char16_t));
                    this->stack_data.m_real_data[new_size] = u'\0';
                    #pragma GCC diagnostic pop

                }
                else{

                    get_real_heap_m_data()[new_size] = u'\0';
                    this->heap_data.m_length = new_size;

                }



            }
            else{

                
                this->stack_data.m_real_data[new_size] = u'\0';
                set_stack_mode(new_size);
            }



        }
        else{

            //lengthening
            
                if(new_size <= capacity()) {
                if(is_heap()) {
                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        if(control_ref_count()){
                            if(!m_detach()) return false;
                        }
                    #endif
                    std::fill_n(get_real_heap_m_data() + str_lenght, new_size - str_lenght, fill_char);
                    get_real_heap_m_data()[new_size] = u'\0';
                    this->heap_data.m_length = new_size;
                    set_heap_mode();
                } 
                else {
                   
                    std::fill_n(this->stack_data.m_real_data + str_lenght, new_size - str_lenght, fill_char);
                    this->stack_data.m_real_data[new_size] = u'\0';
                    set_stack_mode(new_size);
                }
            } 
            else {
                
                if(!reserve(new_size)) return false;
                uint32_t diff = new_size - str_lenght;
            
               
                std::fill_n(get_real_heap_m_data() + str_lenght, diff, fill_char);
                get_real_heap_m_data()[new_size] = u'\0';
                this->heap_data.m_length = new_size;
                set_heap_mode();
            }
        }

        return true;
    }




    KNST_FORCE_INLINE bool resize(uint32_t new_size , char fill_char) noexcept{ // The `char fill` version of `resize`. The logic is the same—shortening/lengthening, SSO/heap transition, COW detach. The difference: `fill_char` is cast to `char16_t`

        
        uint32_t str_lenght = length();

        if(new_size < str_lenght){

            
            if(is_heap()){

                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif

                if(new_size <=KNST_SSO_BUFFER_LENGTH){

                    char16_t temp[KNST_SSO_BUFFER_CAPACITY];
                    memcpy(temp, get_real_heap_m_data(), new_size * sizeof(char16_t));
                    temp[new_size] = u'\0';
                    
                    m_destructor();
                    
                    set_stack_mode(new_size);
                    memcpy(this->stack_data.m_real_data, temp, new_size * sizeof(char16_t));
                    this->stack_data.m_real_data[new_size] = u'\0';

                }
                else{

                    get_real_heap_m_data()[new_size] = u'\0';
                    this->heap_data.m_length = new_size;

                }



            }
            else{

                
                this->stack_data.m_real_data[new_size] = u'\0';
                set_stack_mode(new_size);
            }



        }
        else{

            
            if(new_size <= capacity()) {
                if(is_heap()) {
                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        if(control_ref_count()){
                            if(!m_detach()) return false;
                        }
                    #endif
                    std::fill_n(get_real_heap_m_data() + str_lenght, new_size - str_lenght,static_cast<char16_t>(fill_char));
                    get_real_heap_m_data()[new_size] = u'\0';
                    this->heap_data.m_length = new_size;
                    set_heap_mode();
                }
                else {
                   
                    std::fill_n(this->stack_data.m_real_data + str_lenght, new_size - str_lenght, static_cast<char16_t>(fill_char));
                    this->stack_data.m_real_data[new_size] = u'\0';
                    set_stack_mode(new_size);
                }
            } 
            else {
                
                if(!reserve(new_size)) return false;
                uint32_t diff = new_size - str_lenght;
            
               
                std::fill_n(get_real_heap_m_data() + str_lenght, diff, static_cast<char16_t>(fill_char));
                get_real_heap_m_data()[new_size] = u'\0';
                this->heap_data.m_length = new_size;
                set_heap_mode();
            }
        }

        return true;
    }

    KNST_FORCE_INLINE bool resize(uint32_t new_size , wchar_t fill_char) noexcept{ // The `wchar_t fill` version of `resize`. The logic is the same; `fill_char` is cast to `char16_t`. On Windows, `wchar_t` is 16-bit (lossless), while on POSIX it is 32-bit (truncated)—but it is fine for simple use cases
        
        uint32_t str_lenght = length();

        if(new_size < str_lenght){

           

            if(is_heap()){

                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif

                if(new_size <=KNST_SSO_BUFFER_LENGTH){
                    #pragma GCC diagnostic push
                    #pragma GCC diagnostic ignored "-Wstringop-overread"
                    #pragma GCC diagnostic ignored "-Wstringop-overflow"
                    char16_t temp[KNST_SSO_BUFFER_CAPACITY];
                    memcpy(temp, get_real_heap_m_data(), new_size * sizeof(char16_t));
                    temp[new_size] = u'\0';
                    
                    m_destructor();
                    
                    set_stack_mode(new_size);
                    memcpy(this->stack_data.m_real_data, temp, new_size * sizeof(char16_t));
                    this->stack_data.m_real_data[new_size] = u'\0';
                    #pragma GCC diagnostic pop

                }
                else{

                    get_real_heap_m_data()[new_size] = u'\0';
                    this->heap_data.m_length = new_size;

                }



            }
            else{

                
                this->stack_data.m_real_data[new_size] = u'\0';
                set_stack_mode(new_size);
            }



        }
        else{

            
            
            if(new_size <= capacity()) {
                if(is_heap()) {
                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        if(control_ref_count()){
                            if(!m_detach()) return false;
                        }
                    #endif
                    std::fill_n(get_real_heap_m_data() + str_lenght, new_size - str_lenght,static_cast<char16_t>(fill_char));
                    get_real_heap_m_data()[new_size] = u'\0';
                    this->heap_data.m_length = new_size;
                    set_heap_mode();
                }
                else {
                   
                    std::fill_n(this->stack_data.m_real_data + str_lenght, new_size - str_lenght, static_cast<char16_t>(fill_char));
                    this->stack_data.m_real_data[new_size] = u'\0';
                    set_stack_mode(new_size);
                }
            } 
            else {
                
                if(!reserve(new_size)) return false;
                uint32_t diff = new_size - str_lenght;
            
               
                std::fill_n(get_real_heap_m_data() + str_lenght, diff, static_cast<char16_t>(fill_char));
                get_real_heap_m_data()[new_size] = u'\0';
                this->heap_data.m_length = new_size;
                set_heap_mode();
            }
        }

        return true;
    }


    KNST_FORCE_INLINE bool resize(uint32_t new_size , char32_t fill_char) noexcept{ // The `char32_t` fill version of `resize`. Same logic: `fill_char` is cast to `char16_t`. The 4-byte character is truncated to 2 bytes—fine for ASCII and BMP, but incorrect for emojis. Acceptable for simple filling
        
        uint32_t str_lenght = length();

        if(new_size < str_lenght){

           

            if(is_heap()){

                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return false;
                    }
                #endif

                if(new_size <=KNST_SSO_BUFFER_LENGTH){
                    #pragma GCC diagnostic push
                    #pragma GCC diagnostic ignored "-Wstringop-overread"
                    #pragma GCC diagnostic ignored "-Wstringop-overflow"
                    char16_t temp[KNST_SSO_BUFFER_CAPACITY];
                    memcpy(temp, get_real_heap_m_data(), new_size * sizeof(char16_t));
                    temp[new_size] = u'\0';
                    
                    m_destructor();
                    
                    set_stack_mode(new_size);
                    memcpy(this->stack_data.m_real_data, temp, new_size * sizeof(char16_t));
                    this->stack_data.m_real_data[new_size] = u'\0';
                    #pragma GCC diagnostic pop

                }
                else{

                    get_real_heap_m_data()[new_size] = u'\0';
                    this->heap_data.m_length = new_size;

                }


            }
            else{
                
                this->stack_data.m_real_data[new_size] = u'\0';
                set_stack_mode(new_size);

            }


        }
        else{

           
            if(new_size <= capacity()) {
                if(is_heap()) {
                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        if(control_ref_count()){
                            if(!m_detach()) return false;
                        }
                    #endif
                    std::fill_n(get_real_heap_m_data() + str_lenght, new_size - str_lenght,static_cast<char16_t>(fill_char));
                    get_real_heap_m_data()[new_size] = u'\0';
                    this->heap_data.m_length = new_size;
                    set_heap_mode();
                }
                else {
                   
                    std::fill_n(this->stack_data.m_real_data + str_lenght, new_size - str_lenght, static_cast<char16_t>(fill_char));
                    this->stack_data.m_real_data[new_size] = u'\0';
                    set_stack_mode(new_size);
                }
            } 
            else {
                
                if(!reserve(new_size)) return false;
                uint32_t diff = new_size - str_lenght;
            
               
                std::fill_n(get_real_heap_m_data() + str_lenght, diff, static_cast<char16_t>(fill_char));
                get_real_heap_m_data()[new_size] = u'\0';
                this->heap_data.m_length = new_size;
                set_heap_mode();
            }
        }

        return true;
    }


      
    KNST_FORCE_INLINE void shrink_to_fit() noexcept{ // Reduces the capacity to `length + 1`—freeing up excess memory. If it fits within SSO, it moves the data from the heap back to the stack; otherwise, it shrinks the allocation via reallocation. Performs a COW detach first. Similar to `std::vector::shrink_to_fit`

        if(!is_heap()) return;

        uint32_t str_lenght = this->heap_data.m_length;
        uint32_t str_capacity = this->heap_data.m_capacity;


        if(str_lenght <= KNST_SSO_BUFFER_LENGTH) {
            char16_t temp[KNST_SSO_BUFFER_CAPACITY];
            memcpy(temp, get_real_heap_m_data(), str_lenght * sizeof(char16_t));
            temp[str_lenght] = u'\0';
                
            m_destructor();
                
            set_stack_mode(str_lenght);
            memcpy(this->stack_data.m_real_data, temp, str_lenght * sizeof(char16_t));
            this->stack_data.m_real_data[str_lenght] = u'\0';
            return;
        }
            
            #ifndef KNST_C16STRING_DEACTIVE_COW
                if(control_ref_count()) {
                    if(!m_detach()) return;
                }
            #endif
            
            uint32_t new_capacity = str_lenght + 1;
            void* new_heap = this->m_allocator.reallocate(
                get_heap_m_data_for_begin(),
                get_new_heap_size(new_capacity)
            );
            
            if(new_heap) {
                set_real_heap_m_data(new_heap);
                this->heap_data.m_capacity = new_capacity;
                
            }


    }



    //*-*-*-*-*-*-*-*-*-*-*_CONSTRUCTORS_*-*-*-*-*-*-*-*-*-*-*

        KNST_FORCE_INLINE basic_c16string(const Allocator& allocator) noexcept // Constructor with allocator. Creates an empty string and stores the provided allocator. m_allocator(allocator) — note: the parameter is taken and assigned (we fixed this bug in the previous knst_vector). Null terminator in SSO mode
            : m_allocator(allocator) {
            set_stack_mode(0);
            this->stack_data.m_real_data[0] = u'\0';
        }

        KNST_FORCE_INLINE basic_c16string() noexcept{ // Default constructor — empty string. In SSO mode, the first character is null. No heap allocation

            set_stack_mode(0);
            this->stack_data.m_real_data[0] = u'\0';

        }

        KNST_FORCE_INLINE basic_c16string(int value) noexcept{ // Creates a string from an `int` value. It writes the digits from last to first and adds a negative sign. It stores the string on the stack if it fits within the SSO limit, or moves it to the heap if it does not. `set_cow_ref_count(1)` — new allocation, single owner. Similar to `basic_c16string(42)`

            char16_t buffer[16];
            char16_t* ptr = buffer + 15;
            *ptr = u'\0';

            bool negative = (value < 0);
            unsigned int uval = negative ? -static_cast<unsigned int>(value) : value;


            do{
                *--ptr = static_cast<char16_t>('0' + (uval % 10));
                uval /=10;

            }while(uval > 0);

            if(negative) *--ptr = u'-';
            uint32_t len = static_cast<uint32_t>(buffer + 15 - ptr);


            if(len <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(len);
                memcpy(this->stack_data.m_real_data,ptr,len * sizeof(char16_t));
                this->stack_data.m_real_data[len] = u'\0';
            }
            else{

                uint32_t str_capacity = len +1;
                void* new_heap = this->m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap) {set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);
                memcpy(get_real_heap_m_data(), ptr, len * sizeof(char16_t));
                get_real_heap_m_data()[len] = u'\0';
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    set_cow_ref_count(1);
                #endif
                this->heap_data.m_length = len;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();

            }


        }

        KNST_FORCE_INLINE basic_c16string(long long value) noexcept{ // Creates a string from a `long long` value. Uses a `buffer[32]` (sufficient for 64-bit) and supports negative numbers. Follows the same SSO/heap logic as the other numeric constructors

            char16_t buffer[32];
            char16_t* ptr = buffer + 31;
            *ptr = u'\0';

            bool negative = (value < 0);
            long long uval = negative ? -static_cast<long long>(value) : value;


            do{
                *--ptr = static_cast<char16_t>('0' + (uval % 10));
                uval /=10;

            }while(uval > 0);

            if(negative) *--ptr = u'-';
            uint32_t len = static_cast<uint32_t>(buffer + 31 - ptr);


            if(len <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(len);
                memcpy(this->stack_data.m_real_data,ptr,len * sizeof(char16_t));
                this->stack_data.m_real_data[len] = u'\0';
            }
            else{

                uint32_t  str_capacity = len +1;
                void* new_heap = this->m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap) {set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);
                memcpy(get_real_heap_m_data(), ptr, len * sizeof(char16_t));
                get_real_heap_m_data()[len] = u'\0';
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    set_cow_ref_count(1);
                #endif
                this->heap_data.m_length = len;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();

            }


        }

    KNST_FORCE_INLINE basic_c16string(unsigned int value) noexcept{ // Creates a string from an `unsigned int` value. There is no negative step; `buffer[16]` suffices. It follows the same SSO/heap logic

            char16_t buffer[16];
            char16_t* ptr = buffer + 15;
            *ptr = u'\0';

         
            unsigned int uval =  value;


            do{
                *--ptr = static_cast<char16_t>('0' + (uval % 10));
                uval /=10;

            }while(uval > 0);

           
            uint32_t len = static_cast<uint32_t>(buffer + 15 - ptr);


            if(len <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(len);
                memcpy(this->stack_data.m_real_data,ptr,len * sizeof(char16_t));
                this->stack_data.m_real_data[len] = u'\0';
            }
            else{

                uint32_t str_capacity = len +1;
                void* new_heap = this->m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap) {set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);
                memcpy(get_real_heap_m_data(), ptr, len * sizeof(char16_t));
                get_real_heap_m_data()[len] = u'\0';
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    set_cow_ref_count(1);
                #endif
                this->heap_data.m_length = len;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();

            }


    }

    KNST_FORCE_INLINE basic_c16string(long value) noexcept{ // Creates a string from a long value. Varies based on the 32/64-bit platform; supports negative values. Uses the same SSO/heap logic
        char16_t buffer[32];
        char16_t* ptr = buffer + 31;
        *ptr = u'\0';

        bool negative = (value < 0);
        unsigned long uval = negative ? -static_cast<unsigned long>(value) : value;

        do{
            *--ptr = static_cast<char16_t>('0' + (uval % 10));
            uval /=10;
        }while(uval > 0);

        if(negative) *--ptr = u'-';
        uint32_t len = static_cast<uint32_t>(buffer + 31 - ptr);

        if(len <= KNST_SSO_BUFFER_LENGTH){
            set_stack_mode(len);
            memcpy(this->stack_data.m_real_data, ptr, len * sizeof(char16_t));
            this->stack_data.m_real_data[len] = u'\0';
        }
        else{
            uint32_t str_capacity = len + 1;
            void* new_heap = this->m_allocator.allocate(get_new_heap_size(str_capacity));
            if(!new_heap) {
                set_stack_mode(0); 
                this->stack_data.m_real_data[0] = u'\0'; 
                return;
            }
            set_real_heap_m_data(new_heap);
            memcpy(get_real_heap_m_data(), ptr, len * sizeof(char16_t));
            get_real_heap_m_data()[len] = u'\0';
            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = len;
            this->heap_data.m_capacity = str_capacity;
            set_heap_mode();
        }
    }

    KNST_FORCE_INLINE basic_c16string(unsigned long value) noexcept{ // Creates a string from an unsigned long value. Unsigned, buffer[32]. Same SSO/heap logic
        char16_t buffer[32];
        char16_t* ptr = buffer + 31;
        *ptr = u'\0';

        unsigned long uval = value;

        do{
            *--ptr = static_cast<char16_t>('0' + (uval % 10));
            uval /=10;
        }while(uval > 0);

        uint32_t len = static_cast<uint32_t>(buffer + 31 - ptr);

        if(len <= KNST_SSO_BUFFER_LENGTH){
            set_stack_mode(len);
            memcpy(this->stack_data.m_real_data, ptr, len * sizeof(char16_t));
            this->stack_data.m_real_data[len] = u'\0';
        }
        else{
            uint32_t str_capacity = len + 1;
            void* new_heap = this->m_allocator.allocate(get_new_heap_size(str_capacity));
            if(!new_heap) {
                set_stack_mode(0); 
                this->stack_data.m_real_data[0] = u'\0'; 
                return;
            }
            set_real_heap_m_data(new_heap);
            memcpy(get_real_heap_m_data(), ptr, len * sizeof(char16_t));
            get_real_heap_m_data()[len] = u'\0';
            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = len;
            this->heap_data.m_capacity = str_capacity;
            set_heap_mode();
        }
    }

    KNST_FORCE_INLINE basic_c16string(unsigned long long value) noexcept{ // Creates a string from an `unsigned long long` value (64-bit, unsigned). A buffer of size 32 is sufficient (max 20 digits). Uses the same SSO/heap logic

            char16_t buffer[32];
            char16_t* ptr = buffer + 31;
            *ptr = u'\0';

         
            unsigned long long uval =  value;


            do{
                *--ptr = static_cast<char16_t>('0' + (uval % 10));
                uval /=10;

            }while(uval > 0);

          
            uint32_t len = static_cast<uint32_t>(buffer + 31 - ptr);


            if(len <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(len);
                memcpy(this->stack_data.m_real_data,ptr,len * sizeof(char16_t));
                this->stack_data.m_real_data[len] = u'\0';
            }
            else{

                uint32_t str_capacity = len +1;
                void* new_heap = this->m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap) {set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);
                memcpy(get_real_heap_m_data(), ptr, len * sizeof(char16_t));
                get_real_heap_m_data()[len] = u'\0';
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    set_cow_ref_count(1);
                #endif
                this->heap_data.m_length = len;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();

            }


    }



    KNST_FORCE_INLINE basic_c16string(float value) noexcept { // It creates a string from a float value. It uses `snprintf("%g")` for compact formatting, then copies the result via a `char` to `char16_t` cast. It uses the stack if it fits within SSO, or the heap otherwise
        char buffer[32];
        int len = snprintf(buffer, sizeof(buffer), "%g", value);
        
        if(len <= 0) {
            set_stack_mode(0);
            this->stack_data.m_real_data[0] = u'\0';
            return;
        }
        
        uint32_t ulen = static_cast<uint32_t>(len);
        
        if(ulen <= KNST_SSO_BUFFER_LENGTH) {
            set_stack_mode(ulen);
            for(uint32_t i = 0; i < ulen; ++i) {
                this->stack_data.m_real_data[i] = static_cast<char16_t>(buffer[i]);
            }
            this->stack_data.m_real_data[ulen] = u'\0';
        }
        else {
            uint32_t str_capacity = ulen + 1;
            void* new_heap = m_allocator.allocate(get_new_heap_size(str_capacity));
            if(!new_heap) { 
                set_stack_mode(0); 
                this->stack_data.m_real_data[0] = u'\0'; 
                return; 
            }
            set_real_heap_m_data(new_heap);
            char16_t* dst = get_real_heap_m_data();
            for(uint32_t i = 0; i < ulen; ++i) {
                dst[i] = static_cast<char16_t>(buffer[i]);
            }
            dst[ulen] = u'\0';
            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = ulen;
            this->heap_data.m_capacity = str_capacity;
            set_heap_mode();
        }
    }

    KNST_FORCE_INLINE basic_c16string(double value) noexcept { // Creates a string from a double value. Same as float, but uses a larger buffer (buffer[64]). Uses the %g compact format. The SSO/heap logic is the same
        char buffer[64];
        int len = snprintf(buffer, sizeof(buffer), "%g", value);
        
        if(len <= 0) {
            set_stack_mode(0);
            this->stack_data.m_real_data[0] = u'\0';
            return;
        }
        
        uint32_t ulen = static_cast<uint32_t>(len);
        
        if(ulen <= KNST_SSO_BUFFER_LENGTH) {
            set_stack_mode(ulen);
            for(uint32_t i = 0; i < ulen; ++i) {
                this->stack_data.m_real_data[i] = static_cast<char16_t>(buffer[i]);
            }
            this->stack_data.m_real_data[ulen] = u'\0';
        }
        else {
            uint32_t str_capacity = ulen + 1;
            void* new_heap = m_allocator.allocate(get_new_heap_size(str_capacity));
            if(!new_heap) { 
                set_stack_mode(0); 
                this->stack_data.m_real_data[0] = u'\0'; 
                return; 
            }
            set_real_heap_m_data(new_heap);
            char16_t* dst = get_real_heap_m_data();
            for(uint32_t i = 0; i < ulen; ++i) {
                dst[i] = static_cast<char16_t>(buffer[i]);
            }
            dst[ulen] = u'\0';
            #ifndef KNST_C16STRING_DEACTIVE_COW
                set_cow_ref_count(1);
            #endif
            this->heap_data.m_length = ulen;
            this->heap_data.m_capacity = str_capacity;
            set_heap_mode();
        }
    }

        //_____~~~~~~~~ char16_t ~~~~~~~~_____

        KNST_FORCE_INLINE basic_c16string(const char16_t * str) noexcept { // Constructs from a null-terminated `char16_t*` string. The length is determined using `strlen`. If it fits within SSO, it uses the stack; otherwise, it uses the heap and `set_clean_heap_variable` (copy + ref count = 1). Similar to `basic_c16string(u"merhaba")`

            uint32_t str_length = knst_get_str_length(str);

            if(str_length <= KNST_SSO_BUFFER_LENGTH){

                // Stack Mode

                set_stack_mode(str_length);
                memcpy(this->stack_data.m_real_data,str,str_length  * sizeof(char16_t));
                this->stack_data.m_real_data[str_length] = u'\0'; // Null terminator.
                
            }
            else{

                // Heap Mode
                
                
                uint32_t str_capacity = str_length + 1;
                void * new_heap = this->m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;} // Set stack mode and return.
                set_clean_heap_varible(new_heap,str,str_length,str_capacity);
            
                set_heap_mode();


            }


        }


        KNST_FORCE_INLINE basic_c16string(const char16_t * str , uint32_t str_length) noexcept { // Constructs from a `char16_t*` and a given length. Difference from the previous version: `strlen` is not called; the length is already known. Supports embedded nulls. SSO/heap logic remains the same

        
            if(str_length <= KNST_SSO_BUFFER_LENGTH){

                // Stack Mode

                set_stack_mode(str_length);
                memcpy(this->stack_data.m_real_data,str,str_length  * sizeof(char16_t));
                this->stack_data.m_real_data[str_length] = u'\0'; // Null terminator.
                
            }
            else{

                // Heap Mode
                
                
                uint32_t str_capacity = str_length + 1;
                void * new_heap = this->m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;} 
                set_clean_heap_varible(new_heap,str,str_length,str_capacity);
            
                set_heap_mode();


            }


        }

        KNST_FORCE_INLINE basic_c16string(uint32_t count , char16_t chr) noexcept{ // Creates a string consisting of `count` identical `char16_t` characters. It populates the string using `std::fill_n`. The SSO/heap logic is the same. `basic_c16string(5, u'a')` ==> "aaaaa"

            
            

            if(count <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(count);
                
                std::fill_n(this->stack_data.m_real_data,count,chr);
                this->stack_data.m_real_data[count] = u'\0';


            }
            else{

                uint32_t str_capacity = count + 1;
                void * new_heap = m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);
                std::fill_n(get_real_heap_m_data(),count,chr);
                get_real_heap_m_data()[count] = u'\0';
                
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    set_cow_ref_count(1);
                #endif

                this->heap_data.m_length = count;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();

            }


        }

        KNST_FORCE_INLINE basic_c16string(uint32_t count , const char16_t * str) noexcept{ // Constructs a string from a `char16_t*` repeated `count` times.

            uint32_t str_Length = knst_get_str_length(str);

            if(count == 0 || str_Length == 0){
                set_stack_mode(0);
                this->stack_data.m_real_data[0] = u'\0';
                return;
            }

            uint32_t total_length = count * str_Length;
            

            if(total_length <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(total_length);
                
                // exponential replication
                
                char16_t * dst = this->stack_data.m_real_data;      

                memcpy(dst,str,str_Length * sizeof(char16_t)); // first copy
                uint32_t copied = str_Length;

                while(copied * 2 <= total_length){
                    memcpy(dst + copied , dst , copied * sizeof(char16_t)); 
                    copied *= 2;
                }

                if(copied < total_length){
                    memcpy(dst + copied,dst,(total_length - copied) * sizeof(char16_t));
                }

                dst[total_length] = u'\0';


            }
            else{

                uint32_t str_capacity = total_length + 1;
                void * new_heap = m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);


                char16_t * dst = get_real_heap_m_data();      

                memcpy(dst,str,str_Length * sizeof(char16_t)); // first copy
                uint32_t copied = str_Length;

                while(copied * 2 <= total_length){
                    memcpy(dst + copied , dst , copied * sizeof(char16_t)); 
                    copied *= 2;
                }

                if(copied < total_length){
                    memcpy(dst + copied,dst,(total_length - copied) * sizeof(char16_t));
                }



                dst[total_length] = u'\0';
                
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    set_cow_ref_count(1);
                #endif

                this->heap_data.m_length = total_length;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();

            }


        }

        //_____~~~~~~~~ char ~~~~~~~~_____

        KNST_FORCE_INLINE basic_c16string(const char * str) noexcept { // Creates a UTF-16 string from a UTF-8 `char*` string. First, the number of UTF-8 bytes is calculated, followed by the number of UTF-16 characters. Handles SSO/heap selection and conversion via `knst_convert_utf8_to_utf16`. Used for converting POSIX paths to `c16string`

            uint32_t utf8_byte_count = knst_get_str_length(str);

            uint32_t utf16_char_size = knst_get_utf8_to_utf16_exact_length(str,utf8_byte_count);

            if(utf16_char_size <= KNST_SSO_BUFFER_LENGTH){

                // Stack Mode


                set_stack_mode(utf16_char_size);
                knst_convert_utf8_to_utf16(str,utf8_byte_count,this->stack_data.m_real_data);
                this->stack_data.m_real_data[utf16_char_size] = u'\0';
            
            }
            else{

                // Heap Mode

                
                uint32_t str_capacity = utf16_char_size + 1;
                void * new_heap =this->m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);
                knst_convert_utf8_to_utf16(str,utf8_byte_count,get_real_heap_m_data());
                get_real_heap_m_data()[utf16_char_size]= u'\0';
                #ifndef KNST_C16STRING_DEACTIVE_COW

                    set_cow_ref_count(1);

                #endif
                
                this->heap_data.m_length = utf16_char_size;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();

            }

        }

        KNST_FORCE_INLINE basic_c16string(const char * str , uint32_t utf8_byte_count) noexcept { // Constructs from a UTF-8 `char*` and a specified byte count. `strlen` is not called; supports embedded nulls. The rest of the logic is the same—UTF-16 length calculation, SSO/heap, and conversion

        

            uint32_t utf16_char_size = knst_get_utf8_to_utf16_exact_length(str,utf8_byte_count);

            if(utf16_char_size <= KNST_SSO_BUFFER_LENGTH){

                // Stack Mode


                set_stack_mode(utf16_char_size);
                knst_convert_utf8_to_utf16(str,utf8_byte_count,this->stack_data.m_real_data);
                this->stack_data.m_real_data[utf16_char_size] = u'\0';
            
            }
            else{

                // Heap Mode

                
                uint32_t str_capacity = utf16_char_size + 1;
                void * new_heap =this->m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);
                knst_convert_utf8_to_utf16(str,utf8_byte_count,get_real_heap_m_data());
                get_real_heap_m_data()[utf16_char_size]= u'\0';
                #ifndef KNST_C16STRING_DEACTIVE_COW

                    set_cow_ref_count(1);

                #endif
                
                this->heap_data.m_length = utf16_char_size;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();

            }

        }

        KNST_FORCE_INLINE basic_c16string(uint32_t count , char chr) noexcept{ // Creates a string from `count` characters. Casts `char` to `char16_t` (ASCII-compatible). Fills using `std::fill_n`. SSO/heap logic remains the same

            
            

            if(count <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(count);
                
                std::fill_n(this->stack_data.m_real_data,count,static_cast<char16_t>(chr)); 
                this->stack_data.m_real_data[count] = u'\0';


            }
            else{

                uint32_t str_capacity = count + 1;
                void * new_heap = m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);
                std::fill_n(get_real_heap_m_data(),count,static_cast<char16_t>(chr));
                get_real_heap_m_data()[count] = u'\0';
                
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    set_cow_ref_count(1);
                #endif

                this->heap_data.m_length = count;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();

            }


        }

        KNST_FORCE_INLINE basic_c16string(uint32_t count , const char * str) noexcept{ // It creates a string from a UTF-8 `char*` repeated `count` times.

            uint32_t utf8_byte_count = knst_get_str_length(str);
            uint32_t str_Length = knst_get_utf8_to_utf16_exact_length(str,utf8_byte_count);

            if(count == 0 || str_Length == 0){
                set_stack_mode(0);
                this->stack_data.m_real_data[0] = u'\0';
                return;
            }

            uint32_t total_length = count * str_Length;
            

            if(total_length <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(total_length);
                
                // exponential replication
                
                char16_t * dst = this->stack_data.m_real_data;      

                knst_convert_utf8_to_utf16(str,utf8_byte_count,dst); // first copy
                uint32_t copied = str_Length;

                while(copied * 2 <= total_length){
                    memcpy(dst + copied , dst , copied * sizeof(char16_t)); 
                    copied *= 2;
                }

                if(copied < total_length){
                    memcpy(dst + copied,dst,(total_length - copied) * sizeof(char16_t));
                }

                dst[total_length] = u'\0';


            }
            else{

                uint32_t str_capacity = total_length + 1;
                void * new_heap = m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);


                char16_t * dst = get_real_heap_m_data();      

                knst_convert_utf8_to_utf16(str,utf8_byte_count,dst); // first copy
                uint32_t copied = str_Length;

                while(copied * 2 <= total_length){
                    memcpy(dst + copied , dst , copied * sizeof(char16_t)); 
                    copied *= 2;
                }

                if(copied < total_length){
                    memcpy(dst + copied,dst,(total_length - copied) * sizeof(char16_t));
                }



                dst[total_length] = u'\0';
                
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    set_cow_ref_count(1);
                #endif

                this->heap_data.m_length = total_length;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();

            }


        }

        //_____~~~~~~~~ wchar_t ~~~~~~~~_____

        KNST_FORCE_INLINE basic_c16string(const wchar_t * str) noexcept { // Creates UTF-16 from a `wchar_t*` string. On Windows, `wchar_t` is UTF-16 (direct); on POSIX, it involves a UTF-32 to UTF-16 conversion. `knst_get_wchar_to_utf16_exact_length` provides the length beforehand. The SSO/heap logic remains the same

            uint32_t wchar_count = knst_get_str_length(str);

            uint32_t utf16_char_size = knst_get_wchar_to_utf16_exact_length(str,wchar_count);

            if(utf16_char_size <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(utf16_char_size);
                knst_convert_wchar_to_utf16(str,wchar_count,this->stack_data.m_real_data);
                this->stack_data.m_real_data[utf16_char_size] = u'\0';
            }
            else{

                uint32_t str_capacity = utf16_char_size + 1;
                void * new_heap =this->m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);
                knst_convert_wchar_to_utf16(str,wchar_count,get_real_heap_m_data());
                get_real_heap_m_data()[utf16_char_size]= u'\0';
                #ifndef KNST_C16STRING_DEACTIVE_COW

                    set_cow_ref_count(1);

                #endif
                
                this->heap_data.m_length = utf16_char_size;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();


            }

        }

        KNST_FORCE_INLINE basic_c16string(const wchar_t * str , uint32_t wchar_count) noexcept { // Creates using `wchar_t*` plus a specified character count. `strlen` is not called; supports embedded nulls. The rest of the logic is the same
        

            uint32_t utf16_char_size = knst_get_wchar_to_utf16_exact_length(str,wchar_count);

            if(utf16_char_size <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(utf16_char_size);
                knst_convert_wchar_to_utf16(str,wchar_count,this->stack_data.m_real_data);
                this->stack_data.m_real_data[utf16_char_size] = u'\0';
            }
            else{

                uint32_t str_capacity = utf16_char_size + 1;
                void * new_heap =this->m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);
                knst_convert_wchar_to_utf16(str,wchar_count,get_real_heap_m_data());
                get_real_heap_m_data()[utf16_char_size]= u'\0';
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    set_cow_ref_count(1);
                #endif
                
                this->heap_data.m_length = utf16_char_size;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();


            }

        }

        KNST_FORCE_INLINE basic_c16string(uint32_t count , wchar_t chr) noexcept{ // Creates a string from `count` `wchar_t` characters. Casts `wchar_t` to `char16_t`. The SSO/heap logic remains the same

            if(count <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(count);
                
                std::fill_n(this->stack_data.m_real_data,count,static_cast<char16_t>(chr)); 
                this->stack_data.m_real_data[count] = u'\0';


            }
            else{

                uint32_t str_capacity = count + 1;
                void * new_heap = m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);
                std::fill_n(get_real_heap_m_data(),count,static_cast<char16_t>(chr)); 
                get_real_heap_m_data()[count] = u'\0';
                
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    set_cow_ref_count(1);
                #endif

                this->heap_data.m_length = count;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();

            }


        }

        KNST_FORCE_INLINE basic_c16string(uint32_t count , const wchar_t * str) noexcept{ // Creates a string from a `wchar_t*` string repeated `count` times.
            uint32_t wchar_count = knst_get_str_length(str);
            uint32_t str_Length = knst_get_wchar_to_utf16_exact_length(str,wchar_count);

            if(count == 0 || str_Length == 0){
                set_stack_mode(0);
                this->stack_data.m_real_data[0] = u'\0';
                return;
            }

            uint32_t total_length = count * str_Length;
            

            if(total_length <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(total_length);
                
                // exponential replication
                
                char16_t * dst = this->stack_data.m_real_data;      

                knst_convert_wchar_to_utf16(str,wchar_count,dst); // first copy
                uint32_t copied = str_Length;

                while(copied * 2 <= total_length){
                    memcpy(dst + copied , dst , copied * sizeof(char16_t)); 
                    copied *= 2;
                }

                if(copied < total_length){
                    memcpy(dst + copied,dst,(total_length - copied) * sizeof(char16_t));
                }

                dst[total_length] = u'\0';


            }
            else{

                uint32_t str_capacity = total_length + 1;
                void * new_heap = m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);


                char16_t * dst = get_real_heap_m_data();      

                knst_convert_wchar_to_utf16(str,wchar_count,dst); // first copy
                uint32_t copied = str_Length;

                while(copied * 2 <= total_length){
                    memcpy(dst + copied , dst , copied * sizeof(char16_t)); 
                    copied *= 2;
                }

                if(copied < total_length){
                    memcpy(dst + copied,dst,(total_length - copied) * sizeof(char16_t));
                }



                dst[total_length] = u'\0';
                
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    set_cow_ref_count(1);
                #endif

                this->heap_data.m_length = total_length;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();

            }


        }


        //_____~~~~~~~~ char32_t ~~~~~~~~_____

        KNST_FORCE_INLINE basic_c16string(const char32_t * str) noexcept { // Generates UTF-16 from a char32_t* (UTF-32) string. Emojis and 4-byte characters are converted into surrogate pairs. `knst_get_char32_to_utf16_exact_length` provides the length in advance. The SSO/heap logic remains the same

            uint32_t char32_count = knst_get_str_length(str);

            uint32_t utf16_char_size = knst_get_char32_to_utf16_exact_length(str,char32_count);

            if(utf16_char_size <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(utf16_char_size);
                knst_convert_char32_to_utf16(str,char32_count,this->stack_data.m_real_data);
                this->stack_data.m_real_data[utf16_char_size] = u'\0';
            }
            else{

                uint32_t str_capacity = utf16_char_size + 1;
                void * new_heap =this->m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);
                knst_convert_char32_to_utf16(str,char32_count,get_real_heap_m_data());
                get_real_heap_m_data()[utf16_char_size]= u'\0';
                #ifndef KNST_C16STRING_DEACTIVE_COW

                    set_cow_ref_count(1);

                #endif
                
                this->heap_data.m_length = utf16_char_size;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();


            }

        }

        KNST_FORCE_INLINE basic_c16string(const char32_t * str , uint32_t char32_count) noexcept { // Constructs using `char32_t*` and the given number. `strlen` is not called. The rest of the logic is the same

            uint32_t utf16_char_size = knst_get_char32_to_utf16_exact_length(str,char32_count);

            if(utf16_char_size <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(utf16_char_size);
                knst_convert_char32_to_utf16(str,char32_count,this->stack_data.m_real_data);
                this->stack_data.m_real_data[utf16_char_size] = u'\0';
            }
            else{

                uint32_t str_capacity = utf16_char_size + 1;
                void * new_heap =this->m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);
                knst_convert_char32_to_utf16(str,char32_count,get_real_heap_m_data());
                get_real_heap_m_data()[utf16_char_size]= u'\0';
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    set_cow_ref_count(1);
                #endif
                
                this->heap_data.m_length = utf16_char_size;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();


            }

        }

        KNST_FORCE_INLINE basic_c16string(uint32_t count , char32_t chr) noexcept{ // Creates a string from `count` `char32_t` characters. Performs a `char32_t` to `char16_t` cast (OK for BMP characters; emojis are truncated). SSO/heap logic remains the same
            

            if(count <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(count);
                
                std::fill_n(this->stack_data.m_real_data,count,static_cast<char16_t>(chr)); 
                this->stack_data.m_real_data[count] = u'\0';


            }
            else{

                uint32_t str_capacity = count + 1;
                void * new_heap = m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);
                std::fill_n(get_real_heap_m_data(),count,static_cast<char16_t>(chr)); 
                get_real_heap_m_data()[count] = u'\0';
                
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    set_cow_ref_count(1);
                #endif

                this->heap_data.m_length = count;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();

            }


        }

        KNST_FORCE_INLINE basic_c16string(uint32_t count , const char32_t * str) noexcept{ // Constructs a string from a `char32_t*` string repeated `count` times.
            uint32_t char32_count = knst_get_str_length(str);
            uint32_t str_Length = knst_get_char32_to_utf16_exact_length(str,char32_count);

            if(count == 0 || str_Length == 0){
                set_stack_mode(0);
                this->stack_data.m_real_data[0] = u'\0';
                return;
            }

            uint32_t total_length = count * str_Length;
            

            if(total_length <= KNST_SSO_BUFFER_LENGTH){

                set_stack_mode(total_length);
                
                // exponential replication
                
                char16_t * dst = this->stack_data.m_real_data;      

                knst_convert_char32_to_utf16(str,char32_count,dst); // first copy
                uint32_t copied = str_Length;

                while(copied * 2 <= total_length){
                    memcpy(dst + copied , dst , copied * sizeof(char16_t)); 
                    copied *= 2;
                }

                if(copied < total_length){
                    memcpy(dst + copied,dst,(total_length - copied) * sizeof(char16_t));
                }

                dst[total_length] = u'\0';


            }
            else{

                uint32_t str_capacity = total_length + 1;
                void * new_heap = m_allocator.allocate(get_new_heap_size(str_capacity));
                if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                set_real_heap_m_data(new_heap);


                char16_t * dst = get_real_heap_m_data();      

                knst_convert_char32_to_utf16(str,char32_count,dst); // first copy
                uint32_t copied = str_Length;

                while(copied * 2 <= total_length){
                    memcpy(dst + copied , dst , copied * sizeof(char16_t)); 
                    copied *= 2;
                }

                if(copied < total_length){
                    memcpy(dst + copied,dst,(total_length - copied) * sizeof(char16_t));
                }



                dst[total_length] = u'\0';
                
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    set_cow_ref_count(1);
                #endif

                this->heap_data.m_length = total_length;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();

            }


        }


        //,,,,,Std Support,,,,,

        // Constructors for STL type conversions: The `std::string`/`wstring`/`u32string`/`u16string` and `string_view` versions all delegate to the `(data, length)` constructor; the `std::vector` and `std::array` versions do the same. As for `initializer_list` constructors, they copy elements one by one from the `{a, b, c}` brace-enclosed list, utilizing SSO or heap logic as appropriate. This means that `basic_c16string s = std::string("hello")` or `basic_c16string s = {u'a', u'b'}` work naturally


            KNST_FORCE_INLINE basic_c16string(const std::string & str) noexcept : basic_c16string(str.data(),static_cast<uint32_t>(str.length())){}
            KNST_FORCE_INLINE basic_c16string(const std::wstring & str) noexcept : basic_c16string(str.data(),static_cast<uint32_t>(str.length())){}
            KNST_FORCE_INLINE basic_c16string(const std::u32string & str) noexcept : basic_c16string(str.data(),static_cast<uint32_t>(str.length())){}
            KNST_FORCE_INLINE basic_c16string(const std::u16string & str) noexcept : basic_c16string(str.data(),static_cast<uint32_t>(str.length())){}

            KNST_FORCE_INLINE basic_c16string(const std::string_view & sv) noexcept : basic_c16string(sv.data(),static_cast<uint32_t>(sv.length())){}
            KNST_FORCE_INLINE basic_c16string(const std::wstring_view & sv) noexcept : basic_c16string(sv.data(),static_cast<uint32_t>(sv.length())){}
            KNST_FORCE_INLINE basic_c16string(const std::u32string_view & sv) noexcept : basic_c16string(sv.data(),static_cast<uint32_t>(sv.length())){}
            KNST_FORCE_INLINE basic_c16string(const std::u16string_view & sv) noexcept : basic_c16string(sv.data(),static_cast<uint32_t>(sv.length())){}
            

            KNST_FORCE_INLINE basic_c16string(const std::vector<char> & vec) noexcept : basic_c16string(vec.data(),static_cast<uint32_t>(vec.size())){}
            KNST_FORCE_INLINE basic_c16string(const std::vector<wchar_t> & vec) noexcept : basic_c16string(vec.data(),static_cast<uint32_t>(vec.size())){}
            KNST_FORCE_INLINE basic_c16string(const std::vector<char32_t> & vec) noexcept : basic_c16string(vec.data(),static_cast<uint32_t>(vec.size())){}
            KNST_FORCE_INLINE basic_c16string(const std::vector<char16_t> & vec) noexcept : basic_c16string(vec.data(),static_cast<uint32_t>(vec.size())){}

            template<size_t N>
            KNST_FORCE_INLINE basic_c16string(std::array<char,N> & arr) noexcept : basic_c16string(arr.data(),static_cast<uint32_t>(N)){}
            template<size_t N>
            KNST_FORCE_INLINE basic_c16string(std::array<wchar_t,N> & arr) noexcept : basic_c16string(arr.data(),static_cast<uint32_t>(N)){}
            template<size_t N>
            KNST_FORCE_INLINE basic_c16string(std::array<char32_t,N> & arr) noexcept : basic_c16string(arr.data(),static_cast<uint32_t>(N)){}
            template<size_t N>
            KNST_FORCE_INLINE basic_c16string(std::array<char16_t,N> & arr) noexcept : basic_c16string(arr.data(),static_cast<uint32_t>(N)){}

            KNST_FORCE_INLINE basic_c16string(std::initializer_list<char> list) noexcept {

                uint32_t count = static_cast<uint32_t>(list.size());

                if(count <= KNST_SSO_BUFFER_LENGTH){

                    set_stack_mode(count);
                    char16_t* dst = this->stack_data.m_real_data;
                    for(auto it = list.begin(); it != list.end(); ++it) {
                        *dst++ = static_cast<char16_t>(*it);  // char ==> char16_t
                    }
                    *dst = u'\0';
                    

                }
                else{

                    uint32_t str_capacity = count + 1;
                    void * new_heap = m_allocator.allocate(get_new_heap_size(str_capacity));
                    if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                    set_real_heap_m_data(new_heap);
                    char16_t* dst = get_real_heap_m_data();
                    for(auto it = list.begin(); it != list.end(); ++it) {
                        *dst++ = static_cast<char16_t>(*it);  // char ==> char16_t
                    }

                    dst[count] = u'\0';
                
                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        set_cow_ref_count(1);
                    #endif

                    this->heap_data.m_length = count;
                    this->heap_data.m_capacity = str_capacity;
                    set_heap_mode();

                }

            }

            KNST_FORCE_INLINE basic_c16string(std::initializer_list<wchar_t> list) noexcept {

                uint32_t count = static_cast<uint32_t>(list.size());

                if(count <= KNST_SSO_BUFFER_LENGTH){

                    set_stack_mode(count);
                    char16_t* dst = this->stack_data.m_real_data;
                    for(auto it = list.begin(); it != list.end(); ++it) {
                        *dst++ = static_cast<char16_t>(*it);
                    }
                    *dst = u'\0';

                }
                else{

                    uint32_t str_capacity = count + 1;
                    void * new_heap = m_allocator.allocate(get_new_heap_size(str_capacity));
                    if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                    set_real_heap_m_data(new_heap);

                    char16_t* dst = get_real_heap_m_data();
                    for(auto it = list.begin(); it != list.end(); ++it) {
                        *dst++ = static_cast<char16_t>(*it);
                    }

                    dst[count] = u'\0';
                
                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        set_cow_ref_count(1);
                    #endif

                    this->heap_data.m_length = count;
                    this->heap_data.m_capacity = str_capacity;
                    set_heap_mode();

                }

            }

            KNST_FORCE_INLINE basic_c16string(std::initializer_list<char32_t> list) noexcept {

                uint32_t count = static_cast<uint32_t>(list.size());

                if(count <= KNST_SSO_BUFFER_LENGTH){

                    set_stack_mode(count);
                    char16_t* dst = this->stack_data.m_real_data;
                    for(auto it = list.begin(); it != list.end(); ++it) {
                        *dst++ = static_cast<char16_t>(*it);
                    }
                    *dst = u'\0';

                }
                else{

                    uint32_t str_capacity = count + 1;
                    void * new_heap = m_allocator.allocate(get_new_heap_size(str_capacity));
                    if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                    set_real_heap_m_data(new_heap);

                    char16_t* dst = get_real_heap_m_data();
                    for(auto it = list.begin(); it != list.end(); ++it) {
                        *dst++ = static_cast<char16_t>(*it);
                    }

                    dst[count] = u'\0';
                
                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        set_cow_ref_count(1);
                    #endif

                    this->heap_data.m_length = count;
                    this->heap_data.m_capacity = str_capacity;
                    set_heap_mode();

                }

            }

            KNST_FORCE_INLINE basic_c16string(std::initializer_list<char16_t> list) noexcept {

                uint32_t count = static_cast<uint32_t>(list.size());

                if(count <= KNST_SSO_BUFFER_LENGTH){

                    set_stack_mode(count);
                    std::copy_n(list.begin(),count,this->stack_data.m_real_data);
                    this->stack_data.m_real_data[count] = u'\0';

                }
                else{

                    uint32_t str_capacity = count + 1;
                    void * new_heap = m_allocator.allocate(get_new_heap_size(str_capacity));
                    if(!new_heap){ set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;}
                    set_real_heap_m_data(new_heap);
                    std::copy_n(list.begin(),count,get_real_heap_m_data());
                    get_real_heap_m_data()[count] = u'\0';
                
                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        set_cow_ref_count(1);
                    #endif

                    this->heap_data.m_length = count;
                    this->heap_data.m_capacity = str_capacity;
                    set_heap_mode();

                }

            }

           



        //,,,,,Std Support End,,,,,

        

        //______________~~Move & Copy Constructors~~______________

        KNST_FORCE_INLINE basic_c16string(const basic_c16string & other) noexcept : m_allocator(other.m_allocator) { // Copy constructor

            if(other.is_heap()){
            

                #ifdef KNST_C16STRING_DEACTIVE_COW
                    uint32_t str_capacity = other.heap_data.m_length + 1;
                    void * new_heap = this->m_allocator.allocate(get_new_heap_size(str_capacity));
                    if(!new_heap){set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return;} 
                    set_clean_heap_varible(new_heap,other.get_real_heap_m_data(),other.heap_data.m_length,str_capacity);
                    
                #else
                    
                    this->heap_data = other.heap_data;
                    set_cow_ref_count_plus_plus();

                #endif
                

                set_heap_mode();
            }
            else{
                uint32_t sso_length = other.get_sso_length();
                set_stack_mode(sso_length);
                memcpy(this->stack_data.m_real_data,other.stack_data.m_real_data,sso_length * sizeof(char16_t));
                this->stack_data.m_real_data[sso_length] = u'\0'; // Null terminator.

            }

        }

        KNST_FORCE_INLINE basic_c16string(basic_c16string && other) noexcept : m_allocator(std::move(other.m_allocator)) { // Move Constructor

            if(other.is_heap()){
                this->heap_data = other.heap_data;
                set_heap_mode();
            }
            else{

                uint32_t sso_length = other.get_sso_length();
                set_stack_mode(sso_length);
                memcpy(this->stack_data.m_real_data,other.stack_data.m_real_data,sso_length * sizeof(char16_t));
                this->stack_data.m_real_data[sso_length] = u'\0'; // Null terminator.

            }
           

            other.set_stack_mode(0);
            other.stack_data.m_real_data[0] = u'\0';

        }
        //________________________Move & Copy Constructors end_________


        



    //_____________________________________________ Constructors End_________





    //*-*-*-*-*-*-*-*-*-*-*_OPERATORS_*-*-*-*-*-*-*-*-*-*-*


        KNST_FORCE_INLINE const char16_t & operator[](uint32_t index)const noexcept{
            return data()[index];
        }

        KNST_FORCE_INLINE char16_t& operator[](uint32_t index) noexcept {
            #ifndef KNST_C16STRING_DEACTIVE_COW
                detach();
            #endif
            return const_cast<char16_t&>(data()[index]);
        }

        KNST_FORCE_INLINE basic_c16string & operator=(const basic_c16string & other) noexcept{ // Copy Operator

            if(this == &other) return *this;


                        if(is_heap() && capacity() >= other.length() + 1) {
                #ifndef KNST_C16STRING_DEACTIVE_COW
                    if(control_ref_count()){
                        if(!m_detach()) return *this;
                    }
                #endif
                memcpy(get_real_heap_m_data(), other.data(), other.length() * sizeof(char16_t));
                get_real_heap_m_data()[other.length()] = u'\0';
                this->heap_data.m_length = other.length();
                return *this;
            }

            if(is_heap()){
                m_destructor();
            }

            if(other.is_heap()){
            

                #ifdef KNST_C16STRING_DEACTIVE_COW
                    uint32_t str_capacity = other.heap_data.m_length + 1;
                    void * new_heap = this->m_allocator.allocate(get_new_heap_size(str_capacity));
                    if(!new_heap){set_stack_mode(0); this->stack_data.m_real_data[0] = u'\0'; return *this;} 
                    set_clean_heap_varible(new_heap,other.get_real_heap_m_data(),other.heap_data.m_length,str_capacity);
                    
                #else
                    
                    this->heap_data = other.heap_data;
                    set_cow_ref_count_plus_plus();

                #endif
                

                set_heap_mode();
            }
            else{
                uint32_t sso_length = other.get_sso_length();
                set_stack_mode(sso_length);
                memcpy(this->stack_data.m_real_data,other.stack_data.m_real_data,sso_length * sizeof(char16_t));
                this->stack_data.m_real_data[sso_length] = u'\0'; // Null terminator.

            }


            return *this;
        }



        KNST_FORCE_INLINE basic_c16string & operator=(basic_c16string && other) noexcept{ // Move Operator

            if(this == &other) return *this;

            if(is_heap()){
                m_destructor();
            }

            m_allocator = std::move(other.m_allocator);

            if(other.is_heap()){
                this->heap_data = other.heap_data;
                set_heap_mode();
            }
            else{

                uint32_t sso_length = other.get_sso_length();
                set_stack_mode(sso_length);
                memcpy(this->stack_data.m_real_data,other.stack_data.m_real_data,sso_length * sizeof(char16_t));
                this->stack_data.m_real_data[sso_length] = u'\0'; // Null terminator.

            }
           

            other.set_stack_mode(0);
            other.stack_data.m_real_data[0] = u'\0';


            return *this;
        }


        // operator =

            KNST_FORCE_INLINE basic_c16string & operator=(const char16_t * str) noexcept{

               

                uint32_t str_length = knst_get_str_length(str);
                uint32_t str_size = str_length * sizeof(char16_t);


                if(is_heap() && capacity() >= str_length + 1) {
                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        if(control_ref_count()){
                            if(!m_detach()) return *this;
                        }
                    #endif
                    memcpy(get_real_heap_m_data(), str, str_size);
                    get_real_heap_m_data()[str_length] = u'\0';
                    this->heap_data.m_length = str_length;
                   
                    return *this;
                }

                if(str_length <= KNST_SSO_BUFFER_LENGTH){
                    if(is_heap()){
                        m_destructor();
                    }
                    set_stack_mode(str_length);
                    memcpy(this->stack_data.m_real_data,str,str_size);
                    this->stack_data.m_real_data[str_length] = u'\0';

                }
                else{

                    uint32_t str_capacity = str_length + 1;
                    void * new_heap = this->m_allocator.allocate(get_new_heap_size(str_capacity));
                    if(!new_heap){return *this;}
                    if(is_heap()){
                        m_destructor();
                    }
                    set_clean_heap_varible(new_heap,str,str_length,str_capacity);
                
                    set_heap_mode();

                }

                return *this;
            }

            KNST_FORCE_INLINE basic_c16string & operator=(const char * str) noexcept{

                

                uint32_t str_length = knst_get_str_length(str);
                uint32_t utf16_char_size = knst_get_utf8_to_utf16_exact_length(str, str_length);


                if(is_heap() && capacity() >= utf16_char_size + 1) {
                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        if(control_ref_count()){
                            if(!m_detach()) return *this;
                        }
                    #endif
                    knst_convert_utf8_to_utf16(str, str_length, get_real_heap_m_data());
                    get_real_heap_m_data()[utf16_char_size] = u'\0';
                    this->heap_data.m_length = utf16_char_size;
                    return *this;
                }

                if(utf16_char_size <= KNST_SSO_BUFFER_LENGTH){
                    if(is_heap()){
                        m_destructor();
                    }
                    set_stack_mode(utf16_char_size);
                    knst_convert_utf8_to_utf16(str,str_length,this->stack_data.m_real_data);
                    this->stack_data.m_real_data[utf16_char_size] = u'\0';

                }
                else{

                    uint32_t str_capacity = utf16_char_size + 1;
                    void * new_heap = this->m_allocator.allocate(get_new_heap_size(str_capacity));
                    if(!new_heap){ return *this;}
                    if(is_heap()){
                        m_destructor();
                    }
                    set_real_heap_m_data(new_heap);
                    knst_convert_utf8_to_utf16(str,str_length,get_real_heap_m_data());

                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        set_cow_ref_count(1);
                    #endif

                    this->heap_data.m_length = utf16_char_size;
                    this->heap_data.m_capacity = str_capacity;
                
                    set_heap_mode();

                }

                return *this;
            }


            KNST_FORCE_INLINE basic_c16string & operator=(const wchar_t * str) noexcept{

                

                uint32_t str_length = knst_get_str_length(str);
                uint32_t utf16_char_size = knst_get_wchar_to_utf16_exact_length(str, str_length);

                 if(is_heap() && capacity() >= utf16_char_size + 1) {
                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        if(control_ref_count()){
                            if(!m_detach()) return *this;
                        }
                    #endif
                    knst_convert_wchar_to_utf16(str, str_length, get_real_heap_m_data());
                    get_real_heap_m_data()[utf16_char_size] = u'\0';
                    this->heap_data.m_length = utf16_char_size;
                    return *this;
                }

                if(utf16_char_size <= KNST_SSO_BUFFER_LENGTH){
                    if(is_heap()){
                        m_destructor();
                    }
                    set_stack_mode(utf16_char_size);
                    knst_convert_wchar_to_utf16(str,str_length,this->stack_data.m_real_data);
                    this->stack_data.m_real_data[utf16_char_size] = u'\0';

                }
                else{

                    uint32_t str_capacity = utf16_char_size + 1;
                    void * new_heap = this->m_allocator.allocate(get_new_heap_size(str_capacity));
                    if(!new_heap){return *this;}

                    if(is_heap()){
                        m_destructor();
                    }

                    set_real_heap_m_data(new_heap);
                    knst_convert_wchar_to_utf16(str,str_length,get_real_heap_m_data());

                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        set_cow_ref_count(1);
                    #endif

                    this->heap_data.m_length = utf16_char_size;
                    this->heap_data.m_capacity = str_capacity;
                
                    set_heap_mode();

                }

                return *this;
            }

            KNST_FORCE_INLINE basic_c16string & operator=(const char32_t * str) noexcept{

                

                uint32_t str_length = knst_get_str_length(str);
                uint32_t utf16_char_size = knst_get_char32_to_utf16_exact_length(str, str_length);

                    if(is_heap() && capacity() >= utf16_char_size + 1) {
                        #ifndef KNST_C16STRING_DEACTIVE_COW
                            if(control_ref_count()){
                                if(!m_detach()) return *this;
                            }
                        #endif
                        knst_convert_char32_to_utf16(str, str_length, get_real_heap_m_data());
                        get_real_heap_m_data()[utf16_char_size] = u'\0';
                        this->heap_data.m_length = utf16_char_size;
                        return *this;
                    }

                if(utf16_char_size <= KNST_SSO_BUFFER_LENGTH){
                    if(is_heap()){
                        m_destructor();
                    }
                    set_stack_mode(utf16_char_size);
                    knst_convert_char32_to_utf16(str,str_length,this->stack_data.m_real_data);
                    this->stack_data.m_real_data[utf16_char_size] = u'\0';

                }
                else{

                    uint32_t str_capacity = utf16_char_size + 1;
                    void * new_heap = this->m_allocator.allocate(get_new_heap_size(str_capacity));
                    if(!new_heap){return *this;}

                    if(is_heap()){
                        m_destructor();
                    }

                    set_real_heap_m_data(new_heap);
                    knst_convert_char32_to_utf16(str,str_length,get_real_heap_m_data());

                    #ifndef KNST_C16STRING_DEACTIVE_COW
                        set_cow_ref_count(1);
                    #endif

                    this->heap_data.m_length = utf16_char_size;
                    this->heap_data.m_capacity = str_capacity;
                
                    set_heap_mode();

                }

                return *this;
            }

        //_______________________operator =  end


        // operator +=

            KNST_FORCE_INLINE basic_c16string& operator+=(const basic_c16string& other) noexcept {
                append(other);
                return *this;
            }
            KNST_FORCE_INLINE basic_c16string& operator+=(const char16_t* str) noexcept {
                append(str);
                return *this;
            }
            KNST_FORCE_INLINE basic_c16string& operator+=(const char* str) noexcept {
                append(str);
                return *this;
            }
            KNST_FORCE_INLINE basic_c16string& operator+=(const wchar_t* str) noexcept {
                append(str);
                return *this;
            }
            KNST_FORCE_INLINE basic_c16string& operator+=(const char32_t* str) noexcept {
                append(str);
                return *this;
            }
            KNST_FORCE_INLINE basic_c16string& operator+=(int value) noexcept {
                append(value);
                return *this;
            }
            KNST_FORCE_INLINE basic_c16string& operator+=(long value) noexcept {
                append(value);
                return *this;
            }
            KNST_FORCE_INLINE basic_c16string& operator+=(unsigned long value) noexcept {
                append(value);
                return *this;
            }
            KNST_FORCE_INLINE basic_c16string& operator+=(long long value) noexcept {
                append(value);
                return *this;
            }
            KNST_FORCE_INLINE basic_c16string& operator+=(unsigned long long value) noexcept {
                append(value);
                return *this;
            }
            KNST_FORCE_INLINE basic_c16string& operator+=(float value) noexcept {
                append(value);
                return *this;
            }
            KNST_FORCE_INLINE basic_c16string& operator+=(double value) noexcept {
                append(value);
                return *this;
            }

        //_______________________operator +  end

        // operator +=

          
            friend KNST_FORCE_INLINE basic_c16string operator+(const basic_c16string& lhs, const basic_c16string& rhs) noexcept {
                basic_c16string result(lhs);
                result += rhs;
                return result;
            }

            
            friend KNST_FORCE_INLINE basic_c16string operator+(const basic_c16string& lhs, const char16_t* rhs) noexcept {
                basic_c16string result(lhs);
                result += rhs;
                return result;
            }

            
            friend KNST_FORCE_INLINE basic_c16string operator+(const char16_t* lhs, const basic_c16string& rhs) noexcept {
                basic_c16string result(lhs);
                result += rhs;
                return result;
            }

           
            friend KNST_FORCE_INLINE basic_c16string operator+(const basic_c16string& lhs, const char* rhs) noexcept {
                basic_c16string result(lhs);
                result += rhs;
                return result;
            }

          
            friend KNST_FORCE_INLINE basic_c16string operator+(const char* lhs, const basic_c16string& rhs) noexcept {
                basic_c16string result(lhs);
                result += rhs;
                return result;
            }

          
            friend KNST_FORCE_INLINE basic_c16string operator+(const basic_c16string& lhs, const wchar_t* rhs) noexcept {
                basic_c16string result(lhs);
                result += rhs;
                return result;
            }

          
            friend KNST_FORCE_INLINE basic_c16string operator+(const wchar_t* lhs, const basic_c16string& rhs) noexcept {
                basic_c16string result(lhs);
                result += rhs;
                return result;
            }

            friend KNST_FORCE_INLINE basic_c16string operator+(const basic_c16string& lhs, const char32_t* rhs) noexcept {
                basic_c16string result(lhs);
                result += rhs;
                return result;
            }

          
            friend KNST_FORCE_INLINE basic_c16string operator+(const char32_t* lhs, const basic_c16string& rhs) noexcept {
                basic_c16string result(lhs);
                result += rhs;
                return result;
            }

            friend KNST_FORCE_INLINE basic_c16string operator+(char16_t lhs, const basic_c16string& rhs) noexcept {
                basic_c16string result(&lhs, 1);
                result += rhs;
                return result;
            }
            friend KNST_FORCE_INLINE basic_c16string operator+(char lhs, const basic_c16string& rhs) noexcept {
                basic_c16string result(&lhs, 1);
                result += rhs;
                return result;
            }
            friend KNST_FORCE_INLINE basic_c16string operator+(wchar_t lhs, const basic_c16string& rhs) noexcept {
                basic_c16string result(&lhs, 1);
                result += rhs;
                return result;
            }
            friend KNST_FORCE_INLINE basic_c16string operator+(char32_t lhs, const basic_c16string& rhs) noexcept {
                basic_c16string result(&lhs, 1);
                result += rhs;
                return result;
            }
            friend KNST_FORCE_INLINE basic_c16string operator+(const basic_c16string& lhs, char16_t rhs) noexcept {
                basic_c16string result(lhs);
                result += rhs;
                return result;
            }
            friend KNST_FORCE_INLINE basic_c16string operator+(const basic_c16string& lhs, char rhs) noexcept {
                basic_c16string result(lhs);
                result += rhs;
                return result;
            }
            friend KNST_FORCE_INLINE basic_c16string operator+(const basic_c16string& lhs, wchar_t rhs) noexcept {
                basic_c16string result(lhs);
                result += rhs;
                return result;
            }
            friend KNST_FORCE_INLINE basic_c16string operator+(const basic_c16string& lhs, char32_t rhs) noexcept {
                basic_c16string result(lhs);
                result += rhs;
                return result;
            }

        
        //_______________________operator +  end


        // operator == 

            KNST_FORCE_INLINE friend bool operator==(const basic_c16string & lhs , const basic_c16string & rhs ) noexcept{

                if(lhs.length() != rhs.length()) return false;
                return memcmp(lhs.data(),rhs.data(),lhs.length() * sizeof(char16_t)) == 0; // To compare.

            }
            KNST_FORCE_INLINE friend bool operator==(const basic_c16string & lhs , const char16_t * rhs ) noexcept{

                uint32_t str_Length = knst_get_str_length(rhs);
                if(lhs.length() != str_Length) return false;
                return memcmp(lhs.data(),rhs,lhs.length() * sizeof(char16_t)) == 0;

            }
            KNST_FORCE_INLINE friend bool operator==(const basic_c16string & lhs , const char * rhs ) noexcept{

                uint32_t char_count = knst_get_str_length(rhs);
                uint32_t utf16_char_size = knst_get_utf8_to_utf16_exact_length(rhs,char_count);

                if(lhs.length() != utf16_char_size) return false;

                if(utf16_char_size <= KNST_SSO_BUFFER_LENGTH){
                    
                    char16_t stack_data[KNST_SSO_BUFFER_CAPACITY];
                    knst_convert_utf8_to_utf16(rhs,char_count,stack_data);
                    return memcmp(lhs.data(),stack_data,lhs.length() * sizeof(char16_t)) == 0;

                }
                else{
                    uint32_t total_byte = utf16_char_size * sizeof(char16_t);
                    char16_t * heap_data = (char16_t*)lhs.m_allocator.allocate(total_byte);
                    if(!heap_data) return false;
                    knst_convert_utf8_to_utf16(rhs,char_count,heap_data);
                    bool value;
                    memcmp(lhs.data(),heap_data,lhs.length() * sizeof(char16_t)) == 0 ? value = true : value = false;

                    lhs.m_allocator.deallocate(heap_data,total_byte);
                
                    return value;

                }
                

            }
            KNST_FORCE_INLINE friend bool operator==(const basic_c16string & lhs , const wchar_t * rhs ) noexcept{

                uint32_t wchar_count = knst_get_str_length(rhs);
                uint32_t utf16_char_size = knst_get_wchar_to_utf16_exact_length(rhs,wchar_count);

                if(lhs.length() != utf16_char_size) return false;

                if(utf16_char_size <= KNST_SSO_BUFFER_LENGTH){
                    
                    char16_t stack_data[KNST_SSO_BUFFER_CAPACITY];
                    knst_convert_wchar_to_utf16(rhs,wchar_count,stack_data);
                    return memcmp(lhs.data(),stack_data,lhs.length() * sizeof(char16_t)) == 0;

                }
                else{
                    uint32_t total_byte = utf16_char_size * sizeof(char16_t);
                    void * heap_data = lhs.m_allocator.allocate(total_byte);
                    if(!heap_data) return false;
                    knst_convert_wchar_to_utf16(rhs,wchar_count,static_cast<char16_t*>(heap_data));
                    bool value;
                    memcmp(lhs.data(),heap_data,lhs.length() * sizeof(char16_t)) == 0 ? value = true : value = false;
                        
                    lhs.m_allocator.deallocate(heap_data,total_byte);
                
                    return value;
                }
                

            }
            
            KNST_FORCE_INLINE friend bool operator==(const basic_c16string & lhs , const char32_t * rhs ) noexcept{

                uint32_t char32_count = knst_get_str_length(rhs);
                uint32_t utf16_char_size = knst_get_char32_to_utf16_exact_length(rhs,char32_count);

                if(lhs.length() != utf16_char_size) return false;

                if(utf16_char_size <= KNST_SSO_BUFFER_LENGTH){
                    
                    char16_t stack_data[KNST_SSO_BUFFER_CAPACITY];
                    knst_convert_char32_to_utf16(rhs,char32_count,stack_data);
                    return memcmp(lhs.data(),stack_data,lhs.length() * sizeof(char16_t)) == 0;

                }
                else{
                    uint32_t total_byte = utf16_char_size * sizeof(char16_t);
                    void * heap_data = lhs.m_allocator.allocate(total_byte);
                    if(!heap_data) return false;
                    knst_convert_char32_to_utf16(rhs,char32_count,static_cast<char16_t*>(heap_data));
                    bool value;
                    memcmp(lhs.data(),heap_data,lhs.length() * sizeof(char16_t)) == 0 ? value = true : value = false;
                        
                    lhs.m_allocator.deallocate(heap_data,total_byte);
                
                    return value;
                }
                

            }


            KNST_FORCE_INLINE friend bool operator==(const char16_t * rhs ,const basic_c16string & lhs) noexcept{

                return lhs == rhs;

            }
            KNST_FORCE_INLINE friend bool operator==(const char * rhs ,const basic_c16string & lhs) noexcept{

                return lhs == rhs;

            }
            KNST_FORCE_INLINE friend bool operator==(const wchar_t * rhs ,const basic_c16string & lhs) noexcept{

                return lhs == rhs;

            }
            KNST_FORCE_INLINE friend bool operator==(const char32_t * rhs ,const basic_c16string & lhs) noexcept{

                return lhs == rhs;

            }
        //_______________________operator ==  end
        
        // operator !=

            KNST_FORCE_INLINE friend bool operator!=(const basic_c16string & lhs , const basic_c16string & rhs ) noexcept{

                return !(lhs == rhs);


            }
            KNST_FORCE_INLINE friend bool operator!=(const basic_c16string & lhs , const char16_t * rhs ) noexcept{

                return !(lhs == rhs);

            }
            KNST_FORCE_INLINE friend bool operator!=(const basic_c16string & lhs , const char * rhs ) noexcept{

                return !(lhs == rhs);

            }
            KNST_FORCE_INLINE friend bool operator!=(const basic_c16string & lhs , const wchar_t * rhs ) noexcept{

                return !(lhs == rhs);

            }
            KNST_FORCE_INLINE friend bool operator!=(const basic_c16string & lhs , const char32_t * rhs ) noexcept{

                return !(lhs == rhs);

            }
            KNST_FORCE_INLINE friend bool operator!=(const char16_t * rhs , const basic_c16string & lhs) noexcept{

                return !(lhs == rhs);

            }
            KNST_FORCE_INLINE friend bool operator!=(const char * rhs , const basic_c16string & lhs) noexcept{

                return !(lhs == rhs);

            }
            KNST_FORCE_INLINE friend bool operator!=(const wchar_t * rhs ,const basic_c16string & lhs) noexcept{

                return !(lhs == rhs);

            }
            KNST_FORCE_INLINE friend bool operator!=(const char32_t * rhs ,const basic_c16string & lhs) noexcept{

                return !(lhs == rhs);

            }



        //_______________________operator != end

        // operator <


            KNST_FORCE_INLINE friend bool operator<(const basic_c16string & lhs , const basic_c16string & rhs) noexcept{

                uint32_t min_len = std::min(lhs.length(),rhs.length());
                int cmp = memcmp(lhs.data(),rhs.data(),min_len * sizeof(char16_t));
                if(cmp != 0) return cmp < 0;

                return lhs.length() < rhs.length();
               

            }

            KNST_FORCE_INLINE friend bool operator<(const basic_c16string & lhs ,const char16_t * rhs) noexcept{

                uint32_t str_Length = knst_get_str_length(rhs);
                uint32_t min_len = std::min(lhs.length(),str_Length);
                int cmp = memcmp(lhs.data(),rhs,min_len * sizeof(char16_t));
                if(cmp != 0) return cmp < 0;

                return lhs.length() < str_Length;
               

            }
            KNST_FORCE_INLINE friend bool operator<(const basic_c16string & lhs ,const char * rhs) noexcept{
                
                uint32_t char_count = knst_get_str_length(rhs);
                uint32_t utf16_char_size = knst_get_utf8_to_utf16_exact_length(rhs,char_count);
                uint32_t min_len = std::min(lhs.length() , utf16_char_size);
                if(utf16_char_size <= KNST_SSO_BUFFER_LENGTH){

                    char16_t stack_data[KNST_SSO_BUFFER_CAPACITY];
                    knst_convert_utf8_to_utf16(rhs,char_count,stack_data);
                    
                    int cmp = memcmp(lhs.data(),stack_data,min_len * sizeof(char16_t));
                    if(cmp != 0 ) return cmp < 0;

                    return lhs.length() < utf16_char_size;
                }
                else{
                    uint32_t total_byte = utf16_char_size * sizeof(char16_t);
                    void * heap_data = lhs.m_allocator.allocate(total_byte);
                    if(!heap_data) return false;
                    knst_convert_utf8_to_utf16(rhs,char_count, static_cast<char16_t*>(heap_data));
                    int cmp = memcmp(lhs.data(),heap_data,min_len * sizeof(char16_t));
                    lhs.m_allocator.deallocate(heap_data,total_byte);
                    if(cmp !=0) return cmp < 0;
                    
                    return lhs.length() < utf16_char_size;
               
                }
               

            }
            KNST_FORCE_INLINE friend bool operator<(const basic_c16string & lhs ,const wchar_t * rhs) noexcept{

                uint32_t wchar_count = knst_get_str_length(rhs);
                uint32_t utf16_char_size = knst_get_wchar_to_utf16_exact_length(rhs,wchar_count);
                uint32_t min_len = std::min(lhs.length() , utf16_char_size);
                if(utf16_char_size <= KNST_SSO_BUFFER_LENGTH){

                    char16_t stack_data[KNST_SSO_BUFFER_CAPACITY];
                    knst_convert_wchar_to_utf16(rhs,wchar_count,stack_data);
                    
                    int cmp = memcmp(lhs.data(),stack_data,min_len * sizeof(char16_t));
                    if(cmp != 0 ) return cmp < 0;

                    return lhs.length() < utf16_char_size;
                }
                else{
                    uint32_t total_byte = utf16_char_size * sizeof(char16_t);
                    void * heap_data = lhs.m_allocator.allocate(total_byte);
                    if(!heap_data) return false;
                    knst_convert_wchar_to_utf16(rhs,wchar_count,static_cast<char16_t*>(heap_data));
                    int cmp = memcmp(lhs.data(),heap_data,min_len * sizeof(char16_t));
                    lhs.m_allocator.deallocate(heap_data,total_byte);
                    if(cmp !=0) return cmp < 0;
                    
                    return lhs.length() < utf16_char_size;
               
                }

            }
            KNST_FORCE_INLINE friend bool operator<(const basic_c16string & lhs ,const char32_t * rhs) noexcept{

                uint32_t char32_count = knst_get_str_length(rhs);
                uint32_t utf16_char_size = knst_get_char32_to_utf16_exact_length(rhs,char32_count);
                uint32_t min_len = std::min(lhs.length() , utf16_char_size);
                if(utf16_char_size <= KNST_SSO_BUFFER_LENGTH){

                    char16_t stack_data[KNST_SSO_BUFFER_CAPACITY];
                    knst_convert_char32_to_utf16(rhs,char32_count,stack_data);
                    
                    int cmp = memcmp(lhs.data(),stack_data,min_len * sizeof(char16_t));
                    if(cmp != 0 ) return cmp < 0;

                    return lhs.length() < utf16_char_size;
                }
                else{
                    uint32_t total_byte = utf16_char_size * sizeof(char16_t);
                    void * heap_data = lhs.m_allocator.allocate(total_byte);
                    if(!heap_data) return false;
                    knst_convert_char32_to_utf16(rhs,char32_count,static_cast<char16_t*>(heap_data));
                    int cmp = memcmp(lhs.data(),heap_data,min_len * sizeof(char16_t));
                    lhs.m_allocator.deallocate(heap_data,total_byte);
                    if(cmp !=0) return cmp < 0;
                    
                    return lhs.length() < utf16_char_size;
               
                }

            }


            KNST_FORCE_INLINE friend bool operator<(const char16_t* rhs, const basic_c16string& lhs) noexcept {
                uint32_t rhs_len = knst_get_str_length(rhs);
                uint32_t min_len = std::min(rhs_len, lhs.length());
                int cmp = memcmp(rhs, lhs.data(), min_len * sizeof(char16_t));
                if(cmp != 0) return cmp < 0;
                return rhs_len < lhs.length();
            }


            KNST_FORCE_INLINE friend bool operator<(const char* rhs, const basic_c16string& lhs) noexcept {
                uint32_t char_count = knst_get_str_length(rhs);
                uint32_t utf16_len = knst_get_utf8_to_utf16_exact_length(rhs, char_count);
                
                if(utf16_len <= KNST_SSO_BUFFER_LENGTH) {
                    char16_t buffer[KNST_SSO_BUFFER_CAPACITY];
                    knst_convert_utf8_to_utf16(rhs, char_count, buffer);
                    uint32_t min_len = std::min(utf16_len, lhs.length());
                    int cmp = memcmp(buffer, lhs.data(), min_len * sizeof(char16_t));
                    if(cmp != 0) return cmp < 0;
                    return utf16_len < lhs.length();
                } else {
                
                    char16_t* heap_data = (char16_t*)lhs.m_allocator.allocate(utf16_len * sizeof(char16_t));
                    if(!heap_data) return false;
                    knst_convert_utf8_to_utf16(rhs, char_count, heap_data);
                    uint32_t min_len = std::min(utf16_len, lhs.length());
                    int cmp = memcmp(heap_data, lhs.data(), min_len * sizeof(char16_t));
                    lhs.m_allocator.deallocate(heap_data, utf16_len * sizeof(char16_t));
                    if(cmp != 0) return cmp < 0;
                    return utf16_len < lhs.length();
                }
            }


            KNST_FORCE_INLINE friend bool operator<(const wchar_t* rhs, const basic_c16string& lhs) noexcept {
                uint32_t wchar_count = knst_get_str_length(rhs);
                uint32_t utf16_len = knst_get_wchar_to_utf16_exact_length(rhs, wchar_count);
                
                if(utf16_len <= KNST_SSO_BUFFER_LENGTH) {
                    char16_t buffer[KNST_SSO_BUFFER_CAPACITY];
                    knst_convert_wchar_to_utf16(rhs, wchar_count, buffer);
                    uint32_t min_len = std::min(utf16_len, lhs.length());
                    int cmp = memcmp(buffer, lhs.data(), min_len * sizeof(char16_t));
                    if(cmp != 0) return cmp < 0;
                    return utf16_len < lhs.length();
                } else {
                
                    char16_t* heap_data = (char16_t*)lhs.m_allocator.allocate(utf16_len * sizeof(char16_t));
                    if(!heap_data) return false;
                    knst_convert_wchar_to_utf16(rhs, wchar_count, heap_data);
                    uint32_t min_len = std::min(utf16_len, lhs.length());
                    int cmp = memcmp(heap_data, lhs.data(), min_len * sizeof(char16_t));
                    lhs.m_allocator.deallocate(heap_data, utf16_len * sizeof(char16_t));
                    if(cmp != 0) return cmp < 0;
                    return utf16_len < lhs.length();
                }
            }

            KNST_FORCE_INLINE friend bool operator<(const char32_t* rhs, const basic_c16string& lhs) noexcept {
                uint32_t char32_count = knst_get_str_length(rhs);
                uint32_t utf16_len = knst_get_char32_to_utf16_exact_length(rhs, char32_count);
                
                if(utf16_len <= KNST_SSO_BUFFER_LENGTH) {
                    char16_t buffer[KNST_SSO_BUFFER_CAPACITY];
                    knst_convert_char32_to_utf16(rhs, char32_count, buffer);
                    uint32_t min_len = std::min(utf16_len, lhs.length());
                    int cmp = memcmp(buffer, lhs.data(), min_len * sizeof(char16_t));
                    if(cmp != 0) return cmp < 0;
                    return utf16_len < lhs.length();
                } else {
                
                    char16_t* heap_data = (char16_t*)lhs.m_allocator.allocate(utf16_len * sizeof(char16_t));
                    if(!heap_data) return false;
                    knst_convert_char32_to_utf16(rhs, char32_count, heap_data);
                    uint32_t min_len = std::min(utf16_len, lhs.length());
                    int cmp = memcmp(heap_data, lhs.data(), min_len * sizeof(char16_t));
                    lhs.m_allocator.deallocate(heap_data, utf16_len * sizeof(char16_t));
                    if(cmp != 0) return cmp < 0;
                    return utf16_len < lhs.length();
                }
            }
        //_______________________operator < end

        // operator >

            KNST_FORCE_INLINE friend bool operator>(const basic_c16string & lhs ,const basic_c16string & rhs) noexcept{

                return rhs < lhs;

            }
            KNST_FORCE_INLINE friend bool operator>(const basic_c16string & lhs ,const char16_t * rhs) noexcept{

                return rhs < lhs;

            }
            KNST_FORCE_INLINE friend bool operator>(const basic_c16string & lhs ,const char * rhs) noexcept{

                return rhs < lhs;

            }
            KNST_FORCE_INLINE friend bool operator>(const basic_c16string & lhs ,const wchar_t * rhs) noexcept{

                return rhs < lhs;

            }
            KNST_FORCE_INLINE friend bool operator>(const basic_c16string & lhs ,const char32_t * rhs) noexcept{

                return rhs < lhs;

            }
            KNST_FORCE_INLINE friend bool operator>(const char16_t * rhs ,const basic_c16string & lhs) noexcept{

                return rhs < lhs;

            }
            KNST_FORCE_INLINE friend bool operator>(const char * rhs ,const basic_c16string & lhs) noexcept{

                return rhs < lhs;

            }
            KNST_FORCE_INLINE friend bool operator>(const wchar_t * rhs ,const basic_c16string & lhs) noexcept{

                return rhs < lhs;

            }
            KNST_FORCE_INLINE friend bool operator>(const char32_t * rhs ,const basic_c16string & lhs) noexcept{

                return rhs < lhs;

            }


        //_______________________operator > end

        // operator <=

            KNST_FORCE_INLINE friend bool operator<=(const basic_c16string & lhs,const basic_c16string & rhs) noexcept{

                return !(rhs < lhs);

            }

            KNST_FORCE_INLINE friend bool operator<=(const basic_c16string & lhs,const char16_t * rhs) noexcept{

                return !(rhs < lhs);

            }

            KNST_FORCE_INLINE friend bool operator<=(const basic_c16string & lhs,const char * rhs) noexcept{

                return !(rhs < lhs);

            }

            KNST_FORCE_INLINE friend bool operator<=(const basic_c16string & lhs,const wchar_t * rhs) noexcept{

                return !(rhs < lhs);

            }

            KNST_FORCE_INLINE friend bool operator<=(const basic_c16string & lhs,const char32_t * rhs) noexcept{

                return !(rhs < lhs);

            }

            KNST_FORCE_INLINE friend bool operator<=(const char16_t * rhs ,const basic_c16string & lhs) noexcept{

                return !(lhs < rhs);

            }

            KNST_FORCE_INLINE friend bool operator<=(const char * rhs ,const basic_c16string & lhs) noexcept{

                return !(lhs < rhs);

            }

            KNST_FORCE_INLINE friend bool operator<=(const wchar_t * rhs ,const basic_c16string & lhs) noexcept{

                return !(lhs < rhs);

            }
            KNST_FORCE_INLINE friend bool operator<=(const char32_t * rhs ,const basic_c16string & lhs) noexcept{

                return !(lhs < rhs);

            }




        //_______________________operator <= end


        // operator >=

            KNST_FORCE_INLINE friend bool operator>=(const basic_c16string & lhs,const basic_c16string & rhs) noexcept{

                return !(lhs < rhs);

            }

            KNST_FORCE_INLINE friend bool operator>=(const basic_c16string & lhs,const char16_t * rhs) noexcept{

                return !(lhs < rhs);

            }

            KNST_FORCE_INLINE friend bool operator>=(const basic_c16string & lhs,const char * rhs) noexcept{

                return !(lhs < rhs);

            }

            KNST_FORCE_INLINE friend bool operator>=(const basic_c16string & lhs,const wchar_t * rhs) noexcept{

                return !(lhs < rhs);

            }
            KNST_FORCE_INLINE friend bool operator>=(const basic_c16string & lhs,const char32_t * rhs) noexcept{

                return !(lhs < rhs);

            }

            KNST_FORCE_INLINE friend bool operator>=(const char16_t * rhs ,const basic_c16string & lhs) noexcept{

                return !(rhs < lhs);

            }

            KNST_FORCE_INLINE friend bool operator>=(const char * rhs ,const basic_c16string & lhs) noexcept{

                return !(rhs < lhs);

            }

            KNST_FORCE_INLINE friend bool operator>=(const wchar_t * rhs ,const basic_c16string & lhs) noexcept{

                return !(rhs < lhs);

            }
            KNST_FORCE_INLINE friend bool operator>=(const char32_t * rhs ,const basic_c16string & lhs) noexcept{

                return !(rhs < lhs);

            }
            


        //_______________________operator >= end


    //_____________________________________________ Operators End_________



    // ITERATOR

    KNST_FORCE_INLINE iterator begin()noexcept{

        #ifndef KNST_C16STRING_DEACTIVE_COW
            detach();
        #endif

        return iterator(const_cast<char16_t*>(data()));
    }

    KNST_FORCE_INLINE iterator end()noexcept{

        #ifndef KNST_C16STRING_DEACTIVE_COW
            detach();
        #endif

        return iterator(const_cast<char16_t*>(data() + length()));
    }

    KNST_FORCE_INLINE const_iterator begin() const noexcept{

        return const_iterator(data());

    }

    KNST_FORCE_INLINE const_iterator end() const noexcept{

        return const_iterator(data() + length());

    }
    KNST_FORCE_INLINE const_iterator cbegin() const noexcept{

        return const_iterator(data());

    }
    KNST_FORCE_INLINE const_iterator cend() const noexcept{

        return const_iterator(data() + length());

    }

    //_____________________________________________ ITERATOR End_________

};



using knst_c16string = basic_c16string<knst_default_allocator>;

template <typename Allocator = knst_pool_allocator>
using knst_c16string_sm = basic_c16string<Allocator>;

