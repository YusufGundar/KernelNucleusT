/*
----------------------------
knst_byte_string.hpp
----------------------------

    It is a template-based unsgined char string class.
    It is a specialized unsgined char string  class with memory support
    You can use the version with a custom allocator—suffixed with `_sm`—which utilizes C++20's `[[no_unique_address]]`
    It is primarily designed for use with data network protocols, but you can also use it with UTF-8 strings; it will serve its purpose depending on your specific use cases
    An `unsigned char`-based string class for binary data and UTF-8. Features SSO (small-stack, large-heap) and is allocator-aware (specifically, the `_sm` pool version). No COW—each copy owns its own buffer. Suitable for network packets, file contents, and UTF-8 text. Includes a constructor for conversion from UTF-16.

*/




#pragma once

    


template <typename Allocator = knst_default_allocator>
class KNST_CLASS_ALIGNMENT basic_byte_string{



    private:

        [[no_unique_address]] mutable Allocator  m_allocator; // If the template is empty, it won't take up space.

           
       
    

        struct HeapDataLayout // The layout of the byte string in heap mode: m_capacity (total bytes), m_real_data (heap pointer), and m_length (bytes used). This is one member of the union; the other is the SSO stack data
        {
        
            uint32_t m_capacity;
            unsigned char * m_real_data;
            uint32_t m_length;
            
        };

    union{ // Union — two modes share the same memory. `heap_data` is for heap mode (capacity + pointer + length), while `stack_data` is for SSO mode (inline buffer + padding + flag). Padding equalizes the sizes of the two structs. The most significant bit of `m_flag` (0x80) indicates the current mode. It occupies space equal to the size of a single struct

        HeapDataLayout heap_data;
       
        struct
        {

            unsigned char m_real_data[KNST_SSO_BUFFER_CAPACITY]; 
            uint8_t padding[sizeof(HeapDataLayout) - KNST_SSO_BUFFER_CAPACITY - 1];
            uint8_t m_flag; 

        } stack_data;
        

    };

    

    KNST_FORCE_INLINE void set_stack_mode(uint32_t length) noexcept{

        this->stack_data.m_flag = static_cast<uint8_t>(length); // Since the stack can have a maximum of 11 bits, the longest bit will always be 0.
    }

    KNST_FORCE_INLINE void set_heap_mode() noexcept{

        this->stack_data.m_flag = 0x80; // fits the mask. (0x80) mask:   1  0  0  0  0  0  0  0   
    }

    KNST_FORCE_INLINE unsigned char * get_real_heap_m_data() const noexcept{ // Returns the actual `unsigned char*` pointer on the heap. There is no COW (in `byte_string`) — it is directly `heap_data.m_real_data`. All heap accesses go through here
       
        return this->heap_data.m_real_data;
    }

    KNST_FORCE_INLINE uint32_t get_sso_length() const noexcept { // Returns the byte length in SSO mode. The lower 7 bits of m_flag (masked by 0x7F) store the length, because the high bit (0x80) is used to distinguish between heap and SSO storage
    
        return static_cast<uint32_t>(this->stack_data.m_flag & 0x7F);

    }


    friend std::ostream& operator<<(std::ostream& os, const basic_byte_string& obj) { // The std::ostream operator writes the byte string exactly as is. It involves a cast from unsigned char* to char* using reinterpret_cast<const char*>. It does not look for a null terminator but writes up to length() (binary-safe). std::cout << bytestr works naturally
        os.write(reinterpret_cast<const char*>(obj.data()), obj.length());
        return os;
    }
       
public:

    using iterator = knst_iterator<unsigned char>; // iterator typedef — const_iterator<unsigned char>. begin()/end() returns this. It iterates over unsigned char elements
    using const_iterator = knst_const_iterator<unsigned char>; // The const_iterator typedef — knst_const_iterator<unsigned char>. cbegin()/cend() and const methods return this
   
    KNST_FORCE_INLINE ~basic_byte_string() noexcept { // Destructor
        if (is_heap()) {
            m_allocator.deallocate(heap_data.m_real_data, heap_data.m_capacity);
        }
    }

    KNST_FORCE_INLINE basic_byte_string() noexcept { // Default constructor — an empty byte string. In SSO mode, the first byte is null. No heap allocation
        set_stack_mode(0);
        this->stack_data.m_real_data[0] = '\0';
    }

    KNST_FORCE_INLINE basic_byte_string(const Allocator& allocator) noexcept // Constructor with allocator. Stores an empty byte string and the provided allocator. Includes a null terminator in SSO mode. Example: `basic_byte_string_sm<knst_pool_allocator> s(pool_alloc);`
            : m_allocator(allocator) {
            set_stack_mode(0);
            this->stack_data.m_real_data[0] = u'\0';
    }

    KNST_FORCE_INLINE basic_byte_string(std::initializer_list<unsigned char> list) noexcept { // Creates a byte string from a brace-enclosed list like `{0x01, 0x02, 0x03}`. If it fits within SSO limits, it copies it to the stack; otherwise, it moves it to the heap. Each byte is copied individually, plus a null terminator
        uint32_t count = static_cast<uint32_t>(list.size());
        
        if (count <= KNST_SSO_BUFFER_LENGTH) {
            set_stack_mode(count);
            unsigned char* dst = this->stack_data.m_real_data;
            for (auto it = list.begin(); it != list.end(); ++it) {
                *dst++ = *it;
            }
            *dst = '\0';
        } else {
            uint32_t str_capacity = count + 1;
            void* new_heap = this->m_allocator.allocate(str_capacity);
            
            if (!new_heap) {
                set_stack_mode(0);
                this->stack_data.m_real_data[0] = '\0';
                return;
            }
            
            this->heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
            unsigned char* dst = this->heap_data.m_real_data;
            for (auto it = list.begin(); it != list.end(); ++it) {
                *dst++ = *it;
            }
            
            dst[count] = '\0';
            this->heap_data.m_length = count;
            this->heap_data.m_capacity = str_capacity;
            set_heap_mode();
        }
    }

    KNST_FORCE_INLINE basic_byte_string(const char* data, uint32_t size) noexcept { // Creates a byte string from a `char*` and a byte count. No `strlen`—supports embedded nulls (binary data). Fast copying via `memcpy`. Automatic choice between SSO and heap. Usage: `basic_byte_string(buf, 100)`
        if (size <= KNST_SSO_BUFFER_LENGTH) {
            set_stack_mode(size);
            memcpy(this->stack_data.m_real_data, data, size);
            this->stack_data.m_real_data[size] = '\0';
        } else {
            uint32_t str_capacity = size + 1;
            void* new_heap = this->m_allocator.allocate(str_capacity);
            if (!new_heap) {
                set_stack_mode(0);
                this->stack_data.m_real_data[0] = '\0';
                return;
            }
            this->heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
            memcpy(this->heap_data.m_real_data, data, size);
            this->heap_data.m_real_data[size] = '\0';
            this->heap_data.m_length = size;
            this->heap_data.m_capacity = str_capacity;
            set_heap_mode();
        }
    }
    
    KNST_FORCE_INLINE basic_byte_string(const char* str) noexcept { // Constructs from a null-terminated char* string. It determines the length using strlen and copies the data to SSO or the heap—similar to `basic_byte_string s("hello")`. The difference from the previous version is that the length is derived from strlen, and there are no embedded nulls
        uint32_t count = static_cast<uint32_t>(strlen(str));
        
        if (count <= KNST_SSO_BUFFER_LENGTH) {
            set_stack_mode(count);
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wstringop-overread"
            #pragma GCC diagnostic ignored "-Wstringop-overflow"
                memcpy(this->stack_data.m_real_data, str, count);
            #pragma GCC diagnostic pop
            this->stack_data.m_real_data[count] = '\0';
        } else {
            uint32_t str_capacity = count + 1;
            void* new_heap = this->m_allocator.allocate(str_capacity);
            
            if (!new_heap) {
                set_stack_mode(0);
                this->stack_data.m_real_data[0] = '\0';
                return;
            }
            
            this->heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
            memcpy(this->heap_data.m_real_data, str, count);
            this->heap_data.m_real_data[count] = '\0';
            this->heap_data.m_length = count;
            this->heap_data.m_capacity = str_capacity;
            set_heap_mode();
        }
    }
    
    
    KNST_FORCE_INLINE basic_byte_string(const unsigned char* data, uint32_t size) noexcept { // Creates it using `unsigned char*` plus the size. The logic is the same as the `char*` version; the only difference is the type. `unsigned char` is the natural type for binary data—no casting required
        if (size <= KNST_SSO_BUFFER_LENGTH) {
            set_stack_mode(size);
            memcpy(this->stack_data.m_real_data, data, size);
            this->stack_data.m_real_data[size] = '\0';
        } else {
            uint32_t str_capacity = size + 1;
            void* new_heap = this->m_allocator.allocate(str_capacity);
            
            if (!new_heap) {
                set_stack_mode(0);
                this->stack_data.m_real_data[0] = '\0';
                return;
            }
            
            this->heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
            memcpy(this->heap_data.m_real_data, data, size);
            this->heap_data.m_real_data[size] = '\0';
            this->heap_data.m_length = size;
            this->heap_data.m_capacity = str_capacity;
            set_heap_mode();
        }
    }

    KNST_FORCE_INLINE basic_byte_string(const char16_t* str_data, uint32_t str_length) noexcept { // It creates a UTF-8 byte string from a UTF-16 `char16_t*` string. First, the number of UTF-8 bytes is calculated; then, the conversion is performed using `knst_convert_utf16_to_utf8`, storing the result either via SSO or on the heap. In other words: UTF-16 path ==> UTF-8 byte sequence
           
            uint32_t byte_size = knst_get_utf16_to_utf8_exact_byte_size(str_data, str_length);
            
            if (byte_size <= KNST_SSO_BUFFER_LENGTH) {
                
                set_stack_mode(byte_size);
                
                
                knst_convert_utf16_to_utf8(str_data,str_length,reinterpret_cast<char*>(this->stack_data.m_real_data)
                
                );
                
                this->stack_data.m_real_data[byte_size] = '\0'; 
                
            }
            else {
                uint32_t str_capacity = byte_size + 1;
                void* new_heap = this->m_allocator.allocate(str_capacity);
                
                if (!new_heap) {
                    set_stack_mode(0);
                    this->stack_data.m_real_data[0] = '\0';
                    return;
                }
                
              
                this->heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
                
              
                knst_convert_utf16_to_utf8(str_data, str_length,reinterpret_cast<char*>(this->heap_data.m_real_data));
                    
                   
                    
                
                
                this->heap_data.m_real_data[byte_size] = '\0';
                this->heap_data.m_length = byte_size;
                this->heap_data.m_capacity = str_capacity;
                
                set_heap_mode();
            }
    }

    KNST_FORCE_INLINE basic_byte_string(const knst_c16string & str) noexcept { // Creates a UTF-8 byte string from a `knst_c16string`. It follows the same logic as the previous `char16_t*` version—UTF-16 to UTF-8 conversion and SSO/heap selection. Similar to `knst_byte_string bytes(c16str)`. Intended for converting file paths from `c16string` to a byte string
            const char16_t * str_data = str.data();
            uint32_t str_length = str.length();
            uint32_t byte_size = knst_get_utf16_to_utf8_exact_byte_size(str_data, str_length);
            

            if (byte_size <= KNST_SSO_BUFFER_LENGTH) {
                
                set_stack_mode(byte_size);
                
                knst_convert_utf16_to_utf8(str_data,str_length,reinterpret_cast<char*>(this->stack_data.m_real_data));
                
                
                this->stack_data.m_real_data[byte_size] = '\0'; 
                
            } else {
                uint32_t str_capacity = byte_size + 1;
                void* new_heap = this->m_allocator.allocate(str_capacity);
                
                if (!new_heap) {
                    set_stack_mode(0);
                    this->stack_data.m_real_data[0] = '\0';
                    return;
                }
                
              
                this->heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
                
              
                knst_convert_utf16_to_utf8(
                    str_data, 
                    str_length, 
                    reinterpret_cast<char*>(this->heap_data.m_real_data)
                );
                
                this->heap_data.m_real_data[byte_size] = '\0';
                this->heap_data.m_length = byte_size;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();
            }
    }

    KNST_FORCE_INLINE basic_byte_string(const basic_byte_string & other) noexcept : m_allocator(other.m_allocator) { // Copy constructor — allocator'ı da paylaşır (pool allocator senaryosunda gereksiz yeni havuz oluşumunu engeller)
            if (other.is_heap()) {
                uint32_t str_capacity = other.heap_data.m_length + 1;
                void* new_heap = this->m_allocator.allocate(str_capacity);
                if (!new_heap) {
                    set_stack_mode(0);
                    this->stack_data.m_real_data[0] = '\0';
                    return;
                }
                
                this->heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
                memcpy(this->heap_data.m_real_data, other.heap_data.m_real_data, other.heap_data.m_length);
                this->heap_data.m_real_data[other.heap_data.m_length] = '\0';
                this->heap_data.m_length = other.heap_data.m_length;
                this->heap_data.m_capacity = str_capacity;
                set_heap_mode();
            }
            else {
                uint32_t sso_length = other.stack_data.m_flag & 0x7F; 
                set_stack_mode(sso_length);
                memcpy(this->stack_data.m_real_data, other.stack_data.m_real_data, sso_length);
                this->stack_data.m_real_data[sso_length] = '\0';
            }
    }


    KNST_FORCE_INLINE basic_byte_string(basic_byte_string && other) noexcept: m_allocator(std::move(other.m_allocator)) {  // Move constructor. Moves the allocator; steals the pointer if on the heap, or copies it if using SSO. Empties the source. Zero allocations
        if (other.is_heap()) {
            
            this->heap_data = other.heap_data;
            set_heap_mode();
            
            
            other.set_stack_mode(0);
            other.stack_data.m_real_data[0] = '\0';
        }
        else {
            
            uint32_t sso_length = other.get_sso_length();
            set_stack_mode(sso_length);
            memcpy(this->stack_data.m_real_data, other.stack_data.m_real_data, sso_length);
            this->stack_data.m_real_data[sso_length] = '\0';
            
            
            other.set_stack_mode(0);
            other.stack_data.m_real_data[0] = '\0';
        }
    }

    template<typename OtherAlloc>
    KNST_FORCE_INLINE bool bridge_memory(OtherAlloc& alloc) { // Moves the byte string to another allocator. If it is on the heap, it allocates memory using the new allocator, copies the data, and deallocates the old memory. In the case of SSO, it simply changes the allocator. Returns false if the allocator types differ
        if constexpr (std::is_same_v<Allocator, OtherAlloc>) {
            if (is_heap()) {
                uint32_t old_length = this->heap_data.m_length;
                uint32_t old_capacity = this->heap_data.m_capacity;
                
       
                unsigned char* new_data = static_cast<unsigned char*>(
                    alloc.allocate(old_capacity)
                );
                if (!new_data) return false;
                
              
                memcpy(new_data, this->heap_data.m_real_data, old_length);
                new_data[old_length] = '\0';
                
              
                m_allocator.deallocate(this->heap_data.m_real_data, old_capacity);
                
               
                m_allocator = alloc;
                
               
                this->heap_data.m_real_data = new_data;
                this->heap_data.m_length = old_length;
                this->heap_data.m_capacity = old_capacity;
                
            } else {
              
                m_allocator = alloc;
            }
            return true;
        } else {
            return false; 
        }
    }

   
    template<typename OtherAlloc>
    KNST_FORCE_INLINE bool bridge_memory(const OtherAlloc& alloc) { // The const lvalue version of bridge_memory. Same logic — it changes the allocator. It takes only a const OtherAlloc& parameter
        if constexpr (std::is_same_v<Allocator, OtherAlloc>) {
            if (is_heap()) {
                uint32_t old_length = this->heap_data.m_length;
                uint32_t old_capacity = this->heap_data.m_capacity;
                
                unsigned char* new_data = static_cast<unsigned char*>(
                    alloc.allocate(old_capacity)
                );
                if (!new_data) return false;
                
                memcpy(new_data, this->heap_data.m_real_data, old_length);
                new_data[old_length] = '\0';
                
                m_allocator.deallocate(this->heap_data.m_real_data, old_capacity);
                
                m_allocator = alloc;
                
                this->heap_data.m_real_data = new_data;
                this->heap_data.m_length = old_length;
                this->heap_data.m_capacity = old_capacity;
                
            } else {
                m_allocator = alloc;
            }
            return true;
        } else {
            return false;
        }
    }

   
    template<typename OtherAlloc>
    KNST_FORCE_INLINE bool bridge_memory(OtherAlloc&& alloc) { // The rvalue version of `bridge_memory`. It moves the allocator. The logic is the same—it moves the data based on whether it is in the heap or SSO state. It works efficiently with temporary allocators
        if constexpr (std::is_same_v<Allocator, std::decay_t<OtherAlloc>>) {
            if (is_heap()) {
                uint32_t old_length = this->heap_data.m_length;
                uint32_t old_capacity = this->heap_data.m_capacity;
                
                unsigned char* new_data = static_cast<unsigned char*>(
                    alloc.allocate(old_capacity)
                );
                if (!new_data) return false;
                
                memcpy(new_data, this->heap_data.m_real_data, old_length);
                new_data[old_length] = '\0';
                
                m_allocator.deallocate(this->heap_data.m_real_data, old_capacity);
                
                m_allocator = std::move(alloc);
                
                this->heap_data.m_real_data = new_data;
                this->heap_data.m_length = old_length;
                this->heap_data.m_capacity = old_capacity;
                
            } else {
                m_allocator = std::move(alloc);
            }
            return true;
        } else {
            return false;
        }
    }


    KNST_FORCE_INLINE basic_byte_string& append(const unsigned char* data, uint32_t size) noexcept { // Appends bytes to the end of the byte string. If on the heap: append directly if there is space; otherwise, double the capacity and copy. If using SSO: append to the stack if it fits; otherwise, move to the heap. A null terminator is always added. Like `s.append(buf, 100)`
        if (size == 0) return *this;
        
        uint32_t old_len = length();
        uint32_t new_len = old_len + size;
        
        if (is_heap()) {
            if (new_len + 1 > heap_data.m_capacity) {
                uint32_t new_cap = (new_len + 1) * 2;
                void* new_heap = m_allocator.allocate(new_cap);
                if (!new_heap) return *this;
                
                memcpy(new_heap, heap_data.m_real_data, old_len);
                memcpy(static_cast<unsigned char*>(new_heap) + old_len, data, size);
                static_cast<unsigned char*>(new_heap)[new_len] = '\0';
                
                m_allocator.deallocate(heap_data.m_real_data, heap_data.m_capacity);
                heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
                heap_data.m_length = new_len;
                heap_data.m_capacity = new_cap;
            } else {
                memcpy(heap_data.m_real_data + old_len, data, size);
                heap_data.m_real_data[new_len] = '\0';
                heap_data.m_length = new_len;
            }
        } else {
            uint32_t sso_len = get_sso_length();
            if (sso_len + size <= KNST_SSO_BUFFER_LENGTH) {
                memcpy(stack_data.m_real_data + sso_len, data, size);
                stack_data.m_real_data[sso_len + size] = '\0';
                set_stack_mode(sso_len + size);
            } else {
                uint32_t new_cap = (new_len + 1) * 2;
                void* new_heap = m_allocator.allocate(new_cap);
                if (!new_heap) return *this;
                
                memcpy(new_heap, stack_data.m_real_data, sso_len);
                memcpy(static_cast<unsigned char*>(new_heap) + sso_len, data, size);
                static_cast<unsigned char*>(new_heap)[new_len] = '\0';
                
                heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
                heap_data.m_length = new_len;
                heap_data.m_capacity = new_cap;
                set_heap_mode();
            }
        }
        return *this;
    }

    KNST_FORCE_INLINE void shrink_to_fit() noexcept { // Reduces the capacity to `length + 1`. If it fits within the SSO (Small String Optimization) limit, it moves the data from the heap to the stack; otherwise, it shrinks the allocation via reallocation. Excess memory is freed. If the size is already optimal, it does nothing
        if (!is_heap()) return;
        
        uint32_t current_len = heap_data.m_length;
        uint32_t current_cap = heap_data.m_capacity;
        
     
        if (current_len <= KNST_SSO_BUFFER_LENGTH) {
            unsigned char* old_data = heap_data.m_real_data;
            uint32_t old_cap = current_cap;
            
           
            memcpy(stack_data.m_real_data, old_data, current_len);
            stack_data.m_real_data[current_len] = '\0';
            set_stack_mode(current_len);
            
           
            m_allocator.deallocate(old_data, old_cap);
            return;
        }
        
        
        uint32_t exact_cap = current_len + 1;
        
        
        if (current_cap == exact_cap) return;
        
        
        void* new_heap = m_allocator.allocate(exact_cap);
        if (!new_heap) return;
        
       
        unsigned char* new_data = static_cast<unsigned char*>(new_heap);
        memcpy(new_data, heap_data.m_real_data, current_len);
        new_data[current_len] = '\0';
        
       
        m_allocator.deallocate(heap_data.m_real_data, current_cap);
        
       
        heap_data.m_real_data = new_data;
        heap_data.m_capacity = exact_cap;
       
    }

    KNST_FORCE_INLINE basic_byte_string& append(const basic_byte_string& other) noexcept { // Appends another byte string to the end. Delegates to the append(data, size) version. Like s1.append(s2)
        return append(static_cast<const unsigned char*>(other.data()), other.length());
    }


    KNST_FORCE_INLINE void push_back(unsigned char value) noexcept { // Appends a single byte to the end; a special single-element version of `append`. If on the heap: append directly if space permits, otherwise double the capacity. If using SSO: append to the stack if it fits, otherwise move to the heap. A null terminator is added
        uint32_t old_len = length();
        uint32_t new_len = old_len + 1;
        
        if (is_heap()) {
            if (new_len + 1 > heap_data.m_capacity) {
                uint32_t new_cap = (new_len + 1) * 2;
                void* new_heap = m_allocator.allocate(new_cap);
                if (!new_heap) return;
                
                memcpy(new_heap, heap_data.m_real_data, old_len);
                static_cast<unsigned char*>(new_heap)[old_len] = value;
                static_cast<unsigned char*>(new_heap)[new_len] = '\0';
                
                m_allocator.deallocate(heap_data.m_real_data, heap_data.m_capacity);
                heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
                heap_data.m_length = new_len;
                heap_data.m_capacity = new_cap;
            } else {
                heap_data.m_real_data[old_len] = value;
                heap_data.m_real_data[new_len] = '\0';
                heap_data.m_length = new_len;
            }
        } else {
            uint32_t sso_len = get_sso_length();
            if (sso_len + 1 <= KNST_SSO_BUFFER_LENGTH) {
                stack_data.m_real_data[sso_len] = value;
                stack_data.m_real_data[sso_len + 1] = '\0';
                set_stack_mode(sso_len + 1);
            } else {
                uint32_t new_cap = (new_len + 1) * 2;
                void* new_heap = m_allocator.allocate(new_cap);
                if (!new_heap) return;
                
                memcpy(new_heap, stack_data.m_real_data, sso_len);
                static_cast<unsigned char*>(new_heap)[sso_len] = value;
                static_cast<unsigned char*>(new_heap)[new_len] = '\0';
                
                heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
                heap_data.m_length = new_len;
                heap_data.m_capacity = new_cap;
                set_heap_mode();
            }
        }
    }


    const KNST_FORCE_INLINE unsigned char* data() const noexcept{ // Returns the raw `unsigned char*` pointer to the byte string. It points to heap data if stored on the heap, or to the stack buffer if using SSO. This is the fundamental accessor for all read operations

        return is_heap() ? get_real_heap_m_data() : this->stack_data.m_real_data;

    }



    KNST_FORCE_INLINE void resize(uint32_t new_size) noexcept { // A shortcut for `resize` — fills with 0 bytes. It delegates to the actual `resize(new_size, unsigned char)`
        resize(new_size, 0);
    }

    KNST_FORCE_INLINE void resize(uint32_t new_size, unsigned char fill_value) noexcept { // Changes the length of the byte string. Shortening: moves to the stack if currently on the heap and fits within SSO limits; otherwise, places a null terminator. Extending: fills with `fill_value` if capacity suffices; otherwise, allocates, copies, and fills. SSO/heap transition is automatic. A null terminator is always added. Returns `void` — fails silently on OOM
        uint32_t old_len = length();
        
        if (new_size == old_len) return;
        
        if (new_size < old_len) {
            if (is_heap()) {
                if (new_size <= KNST_SSO_BUFFER_LENGTH) {
                    unsigned char temp[KNST_SSO_BUFFER_CAPACITY];
                    memcpy(temp, heap_data.m_real_data, new_size);
                    temp[new_size] = '\0';
                    
                    m_allocator.deallocate(heap_data.m_real_data, heap_data.m_capacity);
                    set_stack_mode(new_size);
                    memcpy(stack_data.m_real_data, temp, new_size);
                    stack_data.m_real_data[new_size] = '\0';
                } else {
                    heap_data.m_real_data[new_size] = '\0';
                    heap_data.m_length = new_size;
                }
            } else {
                stack_data.m_real_data[new_size] = '\0';
                set_stack_mode(new_size);
            }
        } else {
            if (is_heap()) {
                if (new_size + 1 > heap_data.m_capacity) {
                    uint32_t new_cap = (new_size + 1) * 2;
                    void* new_heap = m_allocator.allocate(new_cap);
                    if (!new_heap) return;
                    
                    memcpy(new_heap, heap_data.m_real_data, old_len);
                    std::fill_n(static_cast<unsigned char*>(new_heap) + old_len, new_size - old_len, fill_value);
                    static_cast<unsigned char*>(new_heap)[new_size] = '\0';
                    
                    m_allocator.deallocate(heap_data.m_real_data, heap_data.m_capacity);
                    heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
                    heap_data.m_length = new_size;
                    heap_data.m_capacity = new_cap;
                } else {
                    std::fill_n(heap_data.m_real_data + old_len, new_size - old_len, fill_value);
                    heap_data.m_real_data[new_size] = '\0';
                    heap_data.m_length = new_size;
                }
            } else {
                if (new_size <= KNST_SSO_BUFFER_LENGTH) {
                    std::fill_n(stack_data.m_real_data + old_len, new_size - old_len, fill_value);
                    stack_data.m_real_data[new_size] = '\0';
                    set_stack_mode(new_size);
                } else {
                    uint32_t new_cap = (new_size + 1) * 2;
                    void* new_heap = m_allocator.allocate(new_cap);
                    if (!new_heap) return;
                    
                    memcpy(new_heap, stack_data.m_real_data, old_len);
                    std::fill_n(static_cast<unsigned char*>(new_heap) + old_len, new_size - old_len, fill_value);
                    static_cast<unsigned char*>(new_heap)[new_size] = '\0';
                    
                    heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
                    heap_data.m_length = new_size;
                    heap_data.m_capacity = new_cap;
                    set_heap_mode();
                }
            }
        }
    }




    static KNST_FORCE_INLINE basic_byte_string take_ownership(unsigned char* data, uint32_t size) noexcept { // It takes an `unsigned char*`—originally allocated via `new[]`—and converts it into a byte string. If the data fits within the SSO (Small String Optimization) limit, it copies it to the stack; otherwise, it allocates new memory using the allocator and copies it there. In either case, it deletes the original `new[]` allocation using `delete[]`. Essentially, "taking ownership" amounts to copying the data and deleting the original, thereby preventing the risks associated with mixing `free` and `new[]`
        basic_byte_string result;
        if (size > 0 && data) {
            if (size <= KNST_SSO_BUFFER_LENGTH) {
            
                result.set_stack_mode(size);
                #pragma GCC diagnostic push
                #pragma GCC diagnostic ignored "-Wstringop-overread"
                #pragma GCC diagnostic ignored "-Wstringop-overflow"
                    memcpy(result.stack_data.m_real_data, data, size);
                #pragma GCC diagnostic pop
                
                result.stack_data.m_real_data[size] = '\0';
                delete[] data;
            } else {
                uint32_t str_capacity = size + 1;
                void* new_heap = result.m_allocator.allocate(str_capacity);
                if (!new_heap) {
                    delete[] data;
                    result.set_stack_mode(0);
                    result.stack_data.m_real_data[0] = '\0';
                    return result;
                }
                memcpy(new_heap, data, size);
                static_cast<unsigned char*>(new_heap)[size] = '\0';
                delete[] data;

                result.heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
                result.heap_data.m_length = size;
                result.heap_data.m_capacity = str_capacity;
                result.set_heap_mode();
            }
        }
        return result;
    }

    KNST_FORCE_INLINE uint32_t length() const noexcept { // Returns the length of the byte string. It is `m_length` if on the heap, or the lower 7 bits of `m_flag` if using SSO. Excludes the null terminator

        return is_heap() ? this->heap_data.m_length : get_sso_length();
    } 

    KNST_FORCE_INLINE uint32_t empty() const noexcept { // Check if the byte string is empty. Returns true if length() == 0

        return length() == 0;
    } 
        
    KNST_FORCE_INLINE uint32_t capacity() const noexcept { // Returns the capacity of the byte string. If in Heap, m_capacity is fixed, if in SSO, KNST_SSO_BUFFER_CAPACITY is fixed. Null terminator included
    
        return is_heap() ? this->heap_data.m_capacity : KNST_SSO_BUFFER_CAPACITY;

    }

    KNST_FORCE_INLINE bool is_heap() const noexcept{

        return (this->stack_data.m_flag & 0x80) != 0; // If the highest bit is 1, it means we are in heap mode and it returns true.
    }

  

    KNST_FORCE_INLINE void clear() noexcept { // Clears the byte string. If it is on the heap, it frees the memory, switches to SSO mode, and sets the first byte to null. The string is reset and can be reused
        if (is_heap()) {
            m_allocator.deallocate(heap_data.m_real_data, heap_data.m_capacity);
        }
        set_stack_mode(0);
        stack_data.m_real_data[0] = '\0';
    }

  

    KNST_FORCE_INLINE basic_byte_string& operator=(const basic_byte_string& other) noexcept {
        if (this == &other) return *this;
        
      
        if (is_heap()) {
            m_allocator.deallocate(heap_data.m_real_data, heap_data.m_capacity);
        }
        
  
        
        if (other.is_heap()) {
            uint32_t str_capacity = other.heap_data.m_length + 1;
            void* new_heap = this->m_allocator.allocate(str_capacity);
            if (!new_heap) {
                set_stack_mode(0);
                this->stack_data.m_real_data[0] = '\0';
                return *this;
            }
            
            this->heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
            memcpy(this->heap_data.m_real_data, other.heap_data.m_real_data, other.heap_data.m_length);
            this->heap_data.m_real_data[other.heap_data.m_length] = '\0';
            this->heap_data.m_length = other.heap_data.m_length;
            this->heap_data.m_capacity = str_capacity;
            set_heap_mode();
        } else {
            uint32_t sso_length = other.get_sso_length();
            set_stack_mode(sso_length);
            memcpy(this->stack_data.m_real_data, other.stack_data.m_real_data, sso_length);
            this->stack_data.m_real_data[sso_length] = '\0';
        }
        
        return *this;
    }



    KNST_FORCE_INLINE basic_byte_string& operator=(basic_byte_string&& other) noexcept {
        if (this == &other) return *this;
        
        if (is_heap()) {
            m_allocator.deallocate(heap_data.m_real_data, heap_data.m_capacity);
        }

        m_allocator = std::move(other.m_allocator);
        
        if (other.is_heap()) {
            this->heap_data = other.heap_data;
            set_heap_mode();
        } else {
            uint32_t sso_length = other.get_sso_length();
            set_stack_mode(sso_length);
            memcpy(this->stack_data.m_real_data, other.stack_data.m_real_data, sso_length);
            this->stack_data.m_real_data[sso_length] = '\0';
        }
        
     
        other.set_stack_mode(0);
        other.stack_data.m_real_data[0] = '\0';
        
        return *this;
    }


    KNST_FORCE_INLINE basic_byte_string& operator=(const char* str) noexcept {
        if (is_heap()) {
            m_allocator.deallocate(heap_data.m_real_data, heap_data.m_capacity);
        }
        
        if (!str) {
            set_stack_mode(0);
            this->stack_data.m_real_data[0] = '\0';
            return *this;
        }
        
        uint32_t count = static_cast<uint32_t>(strlen(str));
        
        if (count <= KNST_SSO_BUFFER_LENGTH) {
            set_stack_mode(count);
            memcpy(this->stack_data.m_real_data, str, count);
            this->stack_data.m_real_data[count] = '\0';
        } else {
            uint32_t str_capacity = count + 1;
            void* new_heap = this->m_allocator.allocate(str_capacity);
            
            if (!new_heap) {
                set_stack_mode(0);
                this->stack_data.m_real_data[0] = '\0';
                return *this;
            }
            
            this->heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
            memcpy(this->heap_data.m_real_data, str, count);
            this->heap_data.m_real_data[count] = '\0';
            this->heap_data.m_length = count;
            this->heap_data.m_capacity = str_capacity;
            set_heap_mode();
        }
        
        return *this;
    }

 


    KNST_FORCE_INLINE bool operator==(const basic_byte_string& other) const noexcept {
        uint32_t len = length();
        if (len != other.length()) return false;
        if (len == 0) return true;
        return memcmp(data(), other.data(), len) == 0;
    }

    KNST_FORCE_INLINE bool operator!=(const basic_byte_string& other) const noexcept {
        return !(*this == other);
    }

    KNST_FORCE_INLINE bool operator==(const char* str) const noexcept {
        if (!str) return empty();
        uint32_t len = length();
        uint32_t str_len = static_cast<uint32_t>(std::char_traits<char>::length(str));
        if (len != str_len) return false;
        if (len == 0) return true;
        return memcmp(data(), str, len) == 0;
    }

    KNST_FORCE_INLINE bool operator!=(const char* str) const noexcept {
        return !(*this == str);
    }


    KNST_FORCE_INLINE bool operator==(const unsigned char* str) const noexcept {
        return operator==(reinterpret_cast<const char*>(str));
    }

    KNST_FORCE_INLINE bool operator!=(const unsigned char* str) const noexcept {
        return !(*this == str);
    }


    KNST_FORCE_INLINE basic_byte_string& operator=(const unsigned char* str) noexcept {
        return operator=(reinterpret_cast<const char*>(str));
    }


    KNST_FORCE_INLINE basic_byte_string& operator=(char c) noexcept {
        unsigned char tmp[2] = {static_cast<unsigned char>(c), '\0'};
        return operator=(tmp);
    }

    KNST_FORCE_INLINE basic_byte_string& operator=(unsigned char c) noexcept {
        unsigned char tmp[2] = {c, '\0'};
        return operator=(tmp);
    }

    KNST_FORCE_INLINE basic_byte_string& operator=(std::initializer_list<unsigned char> list) noexcept {
        if (is_heap()) {
            m_allocator.deallocate(heap_data.m_real_data, heap_data.m_capacity);
        }
        
        uint32_t count = static_cast<uint32_t>(list.size());
        
        if (count <= KNST_SSO_BUFFER_LENGTH) {
            set_stack_mode(count);
            unsigned char* dst = this->stack_data.m_real_data;
            auto it = list.begin();
            for (uint32_t i = 0; i < count; ++i, ++it) {
                dst[i] = *it;
            }
            dst[count] = '\0';
        } else {
            uint32_t str_capacity = count + 1;
            void* new_heap = this->m_allocator.allocate(str_capacity);
            if (!new_heap) {
                set_stack_mode(0);
                this->stack_data.m_real_data[0] = '\0';
                return *this;
            }
            this->heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
            unsigned char* dst = this->heap_data.m_real_data;
            auto it = list.begin();
            for (uint32_t i = 0; i < count; ++i, ++it) {
                dst[i] = *it;
            }
            dst[count] = '\0';
            this->heap_data.m_length = count;
            this->heap_data.m_capacity = str_capacity;
            set_heap_mode();
        }
        return *this;
    }



    KNST_FORCE_INLINE bool operator<(const basic_byte_string& other) const noexcept {
        uint32_t this_len = length();
        uint32_t other_len = other.length();
        uint32_t min_len = this_len < other_len ? this_len : other_len;
        if (min_len > 0) {
            int cmp = memcmp(data(), other.data(), min_len);
            if (cmp != 0) return cmp < 0;
        }
        return this_len < other_len;
    }

    KNST_FORCE_INLINE bool operator<=(const basic_byte_string& other) const noexcept {
        return !(other < *this);
    }

    KNST_FORCE_INLINE bool operator>(const basic_byte_string& other) const noexcept {
        return other < *this;
    }

    KNST_FORCE_INLINE bool operator>=(const basic_byte_string& other) const noexcept {
        return !(*this < other);
    }


    KNST_FORCE_INLINE bool reserve(uint32_t new_capacity) noexcept { // Ensures the capacity is at least `new_capacity`. If it is already sufficient, it does nothing. If currently using the heap, it reallocates; if using SSO, it allocates a new heap buffer, copies the data, and switches to heap mode. Returns `false` on failure
        if (new_capacity <= capacity()) {
            return true;
        }
        
        
        if (is_heap()) {
            void* new_heap = m_allocator.reallocate(
                heap_data.m_real_data,
                new_capacity
            );
            if (!new_heap) return false;
            
            heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
            heap_data.m_capacity = new_capacity;
            set_heap_mode();
            
        } else {
        
            uint32_t old_len = get_sso_length();
            
            void* new_heap = m_allocator.allocate(new_capacity);
            if (!new_heap) return false;
            

            memcpy(new_heap, stack_data.m_real_data, old_len);
            static_cast<unsigned char*>(new_heap)[old_len] = '\0';
        
            heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
            heap_data.m_length = old_len;
            heap_data.m_capacity = new_capacity;
            set_heap_mode();
        }
        
        return true;
    }

    KNST_FORCE_INLINE bool operator<(const char* str) const noexcept {
        if (!str) return false;
        uint32_t this_len = length();
        uint32_t str_len = static_cast<uint32_t>(std::char_traits<char>::length(str));
        uint32_t min_len = this_len < str_len ? this_len : str_len;
        if (min_len > 0) {
            int cmp = memcmp(data(), str, min_len);
            if (cmp != 0) return cmp < 0;
        }
        return this_len < str_len;
    }

    KNST_FORCE_INLINE bool operator<=(const char* str) const noexcept {
        return !(*this > str);
    }

    KNST_FORCE_INLINE bool operator>(const char* str) const noexcept {
        if (!str) return !empty();
        uint32_t this_len = length();
        uint32_t str_len = static_cast<uint32_t>(std::char_traits<char>::length(str));
        uint32_t min_len = this_len < str_len ? this_len : str_len;
        if (min_len > 0) {
            int cmp = memcmp(data(), str, min_len);
            if (cmp != 0) return cmp > 0;
        }
        return this_len > str_len;
    }

    KNST_FORCE_INLINE bool operator>=(const char* str) const noexcept {
        return !(*this < str);
    }


    KNST_FORCE_INLINE bool operator<(const unsigned char* str) const noexcept {
        return operator<(reinterpret_cast<const char*>(str));
    }

    KNST_FORCE_INLINE bool operator<=(const unsigned char* str) const noexcept {
        return operator<=(reinterpret_cast<const char*>(str));
    }

    KNST_FORCE_INLINE bool operator>(const unsigned char* str) const noexcept {
        return operator>(reinterpret_cast<const char*>(str));
    }

    KNST_FORCE_INLINE bool operator>=(const unsigned char* str) const noexcept {
        return operator>=(reinterpret_cast<const char*>(str));
    }


    friend KNST_FORCE_INLINE bool operator==(const char* lhs, const basic_byte_string& rhs) noexcept {
        return rhs == lhs;
    }

    friend KNST_FORCE_INLINE bool operator!=(const char* lhs, const basic_byte_string& rhs) noexcept {
        return rhs != lhs;
    }

    friend KNST_FORCE_INLINE bool operator<(const char* lhs, const basic_byte_string& rhs) noexcept {
        return rhs > lhs;
    }

    friend KNST_FORCE_INLINE bool operator<=(const char* lhs, const basic_byte_string& rhs) noexcept {
        return rhs >= lhs;
    }

    friend KNST_FORCE_INLINE bool operator>(const char* lhs, const basic_byte_string& rhs) noexcept {
        return rhs < lhs;
    }

    friend KNST_FORCE_INLINE bool operator>=(const char* lhs, const basic_byte_string& rhs) noexcept {
        return rhs <= lhs;
    }


    friend KNST_FORCE_INLINE bool operator==(const unsigned char* lhs, const basic_byte_string& rhs) noexcept {
        return rhs == lhs;
    }

    friend KNST_FORCE_INLINE bool operator!=(const unsigned char* lhs, const basic_byte_string& rhs) noexcept {
        return rhs != lhs;
    }


    KNST_FORCE_INLINE const unsigned char& operator[](uint32_t index) const noexcept {
        return data()[index];
    }

    KNST_FORCE_INLINE unsigned char& operator[](uint32_t index) noexcept {
        return const_cast<unsigned char&>(data()[index]);
    }


    KNST_FORCE_INLINE explicit operator bool() const noexcept {
        return !empty();
    }

    KNST_FORCE_INLINE bool operator!() const noexcept {
        return empty();
    }


    KNST_FORCE_INLINE basic_byte_string& operator+=(const basic_byte_string& other) noexcept {
        return append(other.data(), other.length());
    }

    KNST_FORCE_INLINE basic_byte_string& operator+=(const char* str) noexcept {
        if (!str) return *this;
        uint32_t str_len = static_cast<uint32_t>(std::char_traits<char>::length(str));
        return append(reinterpret_cast<const unsigned char*>(str), str_len);
    }

    KNST_FORCE_INLINE basic_byte_string& operator+=(const unsigned char* str) noexcept {
        if (!str) return *this;
        return operator+=(reinterpret_cast<const char*>(str));
    }

    KNST_FORCE_INLINE basic_byte_string& operator+=(char c) noexcept {
        unsigned char uc = static_cast<unsigned char>(c);
        return append(&uc, 1);
    }

    KNST_FORCE_INLINE basic_byte_string& operator+=(unsigned char c) noexcept {
        return append(&c, 1);
    }

    KNST_FORCE_INLINE basic_byte_string& operator+=(std::initializer_list<unsigned char> list) noexcept {
        if (list.size() == 0) return *this;
        return append(list.begin(), static_cast<uint32_t>(list.size()));
    }


    KNST_FORCE_INLINE basic_byte_string& prepend(const unsigned char* data, uint32_t size) noexcept { // It prepends a byte. If on the heap and there is space, it shifts the existing data to the right using `memmove` and places the new data at the beginning; otherwise, it allocates a new buffer and copies the new data first, followed by the old data. In the case of SSO, it shifts data on the stack if it fits, or moves it to the heap if it does not. The `prepend(other)` version delegates the operation
        if (size == 0 || !data) return *this;
        
        uint32_t old_len = length();
        uint32_t new_len = old_len + size;
        
        if (is_heap()) {
            if (new_len + 1 > heap_data.m_capacity) {
                uint32_t new_cap = (new_len + 1) * 2;
                void* new_heap = m_allocator.allocate(new_cap);
                if (!new_heap) return *this;
                memcpy(static_cast<unsigned char*>(new_heap), data, size);
                memcpy(static_cast<unsigned char*>(new_heap) + size, heap_data.m_real_data, old_len);
                static_cast<unsigned char*>(new_heap)[new_len] = '\0';
                m_allocator.deallocate(heap_data.m_real_data, heap_data.m_capacity);
                heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
                heap_data.m_length = new_len;
                heap_data.m_capacity = new_cap;
            } else {
                memmove(heap_data.m_real_data + size, heap_data.m_real_data, old_len);
                memcpy(heap_data.m_real_data, data, size);
                heap_data.m_real_data[new_len] = '\0';
                heap_data.m_length = new_len;
            }
        } else {
            uint32_t sso_len = get_sso_length();
            if (new_len <= KNST_SSO_BUFFER_LENGTH) {
                memmove(stack_data.m_real_data + size, stack_data.m_real_data, sso_len);
                memcpy(stack_data.m_real_data, data, size);
                stack_data.m_real_data[new_len] = '\0';
                set_stack_mode(new_len);
            } else {
                uint32_t new_cap = (new_len + 1) * 2;
                void* new_heap = m_allocator.allocate(new_cap);
                if (!new_heap) return *this;
                memcpy(static_cast<unsigned char*>(new_heap), data, size);
                memcpy(static_cast<unsigned char*>(new_heap) + size, stack_data.m_real_data, sso_len);
                static_cast<unsigned char*>(new_heap)[new_len] = '\0';
                heap_data.m_real_data = static_cast<unsigned char*>(new_heap);
                heap_data.m_length = new_len;
                heap_data.m_capacity = new_cap;
                set_heap_mode();
            }
        }
        return *this;
    }

    KNST_FORCE_INLINE basic_byte_string& prepend(const basic_byte_string& other) noexcept {
        return prepend(other.data(), other.length());
    }


    
    friend KNST_FORCE_INLINE basic_byte_string operator+(const basic_byte_string& lhs, const basic_byte_string& rhs) noexcept {
        basic_byte_string result(lhs);
        result.append(rhs.data(), rhs.length());
        return result;
    }
    
    friend KNST_FORCE_INLINE basic_byte_string operator+(const basic_byte_string& lhs, const char* rhs) noexcept {
        basic_byte_string result(lhs);
        if (rhs) result.append(reinterpret_cast<const unsigned char*>(rhs), static_cast<uint32_t>(strlen(rhs)));
        return result;
    }

    friend KNST_FORCE_INLINE basic_byte_string operator+(const char* lhs, const basic_byte_string& rhs) noexcept {
        if (!lhs) return basic_byte_string(rhs);
        basic_byte_string result(lhs);
        result.append(rhs.data(), rhs.length());
        return result;
    }

    friend KNST_FORCE_INLINE basic_byte_string operator+(const basic_byte_string& lhs, char rhs) noexcept {
        basic_byte_string result(lhs);
        unsigned char uc = static_cast<unsigned char>(rhs);
        result.append(&uc, 1);
        return result;
    }

    //Iterator

    KNST_FORCE_INLINE iterator begin() noexcept {
        return iterator(const_cast<unsigned char*>(data()));
    }
    
    KNST_FORCE_INLINE iterator end() noexcept {
        return iterator(const_cast<unsigned char*>(data() + length()));
    }
    
    KNST_FORCE_INLINE const_iterator begin() const noexcept {
        return const_iterator(data());
    }
    
    KNST_FORCE_INLINE const_iterator end() const noexcept {
        return const_iterator(data() + length());
    }
    
    KNST_FORCE_INLINE const_iterator cbegin() const noexcept {
        return const_iterator(data());
    }
    
    KNST_FORCE_INLINE const_iterator cend() const noexcept {
        return const_iterator(data() + length());
    }

};


using knst_byte_string = basic_byte_string<knst_default_allocator>;

template <typename Allocator = knst_pool_allocator>
using knst_byte_string_sm = basic_byte_string<Allocator>;


