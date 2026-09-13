/*
----------------------------
knst_vector.hpp
----------------------------

    It is a template-based vector class.
    It is a specialized vector class with memory support
    You can use the version with a custom allocator—suffixed with `_sm`—which utilizes C++20's `[[no_unique_address]]`
    It contains basic functions and methods required within the library
    
*/


#pragma once


template<typename T , typename Allocator = knst_default_allocator>
class basic_vector{

private:

    [[no_unique_address]] mutable Allocator  m_allocator; // If the template is empty, it won't take up space


    T * m_data; // element type
    uint32_t m_size; // how many elements there are
    uint32_t m_capacity; // the total number of elements it can accommodate

public:

    using iterator = knst_iterator<T>;
    using const_iterator = knst_const_iterator<T>;

    KNST_FORCE_INLINE basic_vector(const Allocator& allocator) noexcept: m_allocator(allocator), m_data(nullptr), m_size(0),m_capacity(0){}; // It is used to initialize with a custom allocator

    KNST_FORCE_INLINE basic_vector() noexcept : m_data(nullptr), m_size(0),m_capacity(0){}; // default constructor

    KNST_FORCE_INLINE ~basic_vector() noexcept{ // destructor , The destructor for each element must be called
        for (uint32_t i = 0; i < m_size; i++) {
            m_data[i].~T();
        }
        if(m_data) m_allocator.deallocate(m_data,sizeof(T) * m_capacity);
    }
      
    KNST_FORCE_INLINE basic_vector(uint32_t count, const T& value = T()) noexcept : m_data(nullptr), m_size(0), m_capacity(0) { // It is used to create `count` elements, all set to the value `value`
        if (count > 0) {
            if (!reserve(count)) return;
            for (uint32_t i = 0; i < count; i++) {
                new (m_data + i) T(value);
                m_size++;
            }
        }
    }
   
    KNST_FORCE_INLINE basic_vector(std::initializer_list<T> list) noexcept : m_data(nullptr), m_size(0), m_capacity(0) { // Creates a vector from a brace-enclosed list like {1, 2, 3}. Allocates memory using `reserve` and copies each element using placement new
        uint32_t count = static_cast<uint32_t>(list.size());
        if (count > 0) {
            if (!reserve(count)) return;
            for (const auto& item : list) {
                new (m_data + m_size) T(item);
                m_size++;
            }
        }
    }

    KNST_FORCE_INLINE basic_vector(const T* data, uint32_t count) noexcept  : m_data(nullptr), m_size(0), m_capacity(0) { // Creates a vector from a C-style array (T* + size). Allocates memory using `reserve` and copies each element
        if (count > 0 && data) {
            if (!reserve(count)) return;
            for (uint32_t i = 0; i < count; i++) {
                new (m_data + i) T(data[i]);
                m_size++;
            }
        }
    }

    KNST_FORCE_INLINE basic_vector(const basic_vector& other) noexcept : m_allocator(other.m_allocator), m_data(nullptr), m_size(0), m_capacity(0) { // Copy constructor — copies another vector, allocator'ı da paylaşır (pool allocator kullanımında havuzu paylaşmak için gerekli)
        if (other.m_size > 0) {
            m_data = static_cast<T*>(
                m_allocator.allocate(sizeof(T) * other.m_size)
            );

            if (!m_data) return;
            
            m_capacity = other.m_size;
            for (uint32_t i = 0; i < other.m_size; i++) {
                new (m_data + i) T(other.m_data[i]);
            }
            m_size = other.m_size;
        }
    }

    KNST_FORCE_INLINE basic_vector(basic_vector&& other) noexcept : m_allocator(std::move(other.m_allocator)), m_data(other.m_data),m_size(other.m_size), m_capacity(other.m_capacity) { // Move constructor — steals pointers; does not make a copy. Resets the source (other)
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
    }

    KNST_FORCE_INLINE bool push_back(const T&value) noexcept{ // Copies the element to the end. Adds it directly if there is space; otherwise, doubles the capacity and then adds it. Includes self-reference protection for `value_copy` (safe to push an element belonging to the vector itself). Returns `false` on failure.
        if(m_capacity > m_size){
            new (m_data + m_size) T(value);
            ++m_size;
            return true;
        }

        T value_copy(value);
        uint32_t new_cap = m_capacity == 0 ? 4 : m_capacity * 2; // 2x growth
        if(reserve(new_cap)){
            new (m_data + m_size) T(std::move(value_copy));
            ++m_size;
            return true;

        }

        return false;

    }

    KNST_FORCE_INLINE bool push_back(T&& value) noexcept{ // Adds the element by moving it to the end. Performs a direct move if there is space; otherwise, doubles the capacity and then moves. Includes self-reference protection for `value_copy`. Returns `false` on failure.
        if(m_capacity > m_size){
            new (m_data + m_size) T(std::move(value));
            ++m_size;
            return true;
        }

        T value_copy(std::move(value));
        uint32_t new_cap = m_capacity == 0 ? 4 : m_capacity * 2; // 2x growth same
        if(reserve(new_cap)){
            new (m_data + m_size) T(std::move(value_copy));
            ++m_size;
            return true;

        }

        return false;

    }

    KNST_FORCE_INLINE void pop_back() noexcept { // Deletes the last element. It manually calls the destructor and decrements *m*. The memory is not returned
        if (m_size > 0) {
            m_data[m_size - 1].~T();
            m_size--;
        }
    }

    KNST_FORCE_INLINE iterator erase(const_iterator pos) noexcept { // It deletes the element pointed to by the iterator. Upon deletion, it calls the destructor and shifts the subsequent elements one position to the left. It returns the new position (the location of the deleted element) as an iterator
        uint32_t index = static_cast<uint32_t>(pos.ptr - m_data);
        if (index >= m_size) return end();
        
        m_data[index].~T();
        
        for (uint32_t i = index; i < m_size - 1; i++) {
            new (m_data + i) T(std::move(m_data[i + 1]));
            m_data[i + 1].~T();
        }
        
        m_size--;
        return iterator(m_data + index);
    }

    KNST_FORCE_INLINE iterator erase(const_iterator first, const_iterator last) noexcept {
        uint32_t start_idx = static_cast<uint32_t>(first.ptr - m_data);
        uint32_t end_idx = static_cast<uint32_t>(last.ptr - m_data);
        uint32_t count = end_idx - start_idx;
        
        for (uint32_t i = start_idx; i < end_idx; i++) {
            m_data[i].~T();
        }
        
        for (uint32_t i = end_idx; i < m_size; i++) {
            new (m_data + i - count) T(std::move(m_data[i]));
            m_data[i].~T();
        }
        
        m_size -= count;
        return iterator(m_data + start_idx);
    }

    KNST_FORCE_INLINE void erase(uint32_t index) noexcept { // Removes the element at the specified index. It destroys the removed element and shifts the subsequent elements to the left. It is the same as erase(const_iterator), except it takes an index
        if (index >= m_size) return;
        
        m_data[index].~T();
        
        for (uint32_t i = index; i < m_size - 1; i++) {
            new (m_data + i) T(std::move(m_data[i + 1]));
            m_data[i + 1].~T();
        }
        
        m_size--;
    }
    
    KNST_FORCE_INLINE T& back() noexcept { // Returns a reference to the last element. Calling it on an empty vector results in undefined behavior
        return m_data[m_size - 1];
    }

    KNST_FORCE_INLINE const T& back() const noexcept { // The `const` version returns the last element for read-only access. It is immutable
        return m_data[m_size - 1];
    }

    KNST_FORCE_INLINE bool erase_value(const T& value) noexcept { // Deletes the first matching element. Returns true if found, false otherwise
        for (uint32_t i = 0; i < m_size; i++) {
            if (m_data[i] == value) {
                erase(i);
                return true;
            }
        }
        return false;
    }

    KNST_FORCE_INLINE iterator find(const T& value) noexcept { // Returns an iterator to the first matching element. Returns end() if not found
        for (uint32_t i = 0; i < m_size; i++) {
            if (m_data[i] == value) {
                return iterator(m_data + i);
            }
        }
        return end();
    }

    KNST_FORCE_INLINE const_iterator find(const T& value) const noexcept { // const version — returns the matching element as a const_iterator. Returns end() if not found
        for (uint32_t i = 0; i < m_size; i++) {
            if (m_data[i] == value) {
                return const_iterator(m_data + i);
            }
        }
        return end();
    }

    KNST_FORCE_INLINE bool reserve (uint32_t new_total_memner_count) noexcept{ // Increases capacity. Allocates new memory, moves elements, and deallocates the old memory. Returns false if unsuccessful
        if(new_total_memner_count <= m_capacity) return true;

        T * new_data = static_cast<T*>(m_allocator.allocate(sizeof(T) * new_total_memner_count));
        if(!new_data)return false;
            
        if(m_data){

            for(uint32_t i = 0; i < m_size; i++){
                new (new_data + i) T(std::move(m_data[i]));
                m_data[i].~T();
            }

            m_allocator.deallocate(m_data,sizeof(T) * m_capacity);
        }
        
        m_data = new_data;
        m_capacity = new_total_memner_count;

        return true;
    }

    KNST_FORCE_INLINE void clear() noexcept { // It destroys all elements and sets `m_size` to 0. Memory is not deallocated—the capacity is preserved
        for (uint32_t i = 0; i < m_size; i++) {
            m_data[i].~T();
        }
        m_size = 0;
    }
    
    T* data() noexcept { // Returns a raw pointer — direct access to the elements. The non-const version is mutable
        return m_data;
    }

    KNST_FORCE_INLINE const T* data() const noexcept { // const version — a raw pointer for read-only access. Immutable
        return m_data;
    }

    KNST_FORCE_INLINE bool resize(uint32_t new_size) noexcept { // Changes the number of elements. If the size decreases, it removes the excess elements; if it increases, it adds new elements using reservation and default construction
        if (new_size < m_size) {
            
            for (uint32_t i = new_size; i < m_size; i++) {
                m_data[i].~T();
            }
            m_size = new_size;
        }
        else if (new_size > m_size) {
            
            if (new_size > m_capacity) {
                if (!reserve(new_size)) return false; 
            }
            
            for (uint32_t i = m_size; i < new_size; i++) {
                new (m_data + i) T();
            }
            m_size = new_size;
        }
        
        return true; 
    }

    KNST_FORCE_INLINE bool resize(uint32_t new_size, const T& default_value) noexcept { // Same resize, but new elements are initialized with the default value
        if (new_size < m_size) {
        
            for (uint32_t i = new_size; i < m_size; i++) {
                m_data[i].~T();
            }
            m_size = new_size;
        }
        else if (new_size > m_size) {
            
            if (new_size > m_capacity) {
                if (!reserve(new_size)) return false;
            }
            
            for (uint32_t i = m_size; i < new_size; i++) {
                new (m_data + i) T(default_value);
            }
            m_size = new_size;
        }
        
        return true;
    }

template<typename... Args>
KNST_FORCE_INLINE bool emplace_back(Args&&... args) noexcept {
    if (m_capacity <= m_size) {
        uint32_t new_cap = (m_capacity == 0) ? 4 : m_capacity * 2;
        if (!reserve(new_cap)) return false;
    }
    
    new(m_data + m_size) T(std::forward<Args>(args)...);
    ++m_size;
    return true;
}

    KNST_FORCE_INLINE iterator insert(const_iterator pos, const T& value) noexcept {
    uint32_t index = static_cast<uint32_t>(pos.ptr - m_data);
    
    if (index > m_size) {
        push_back(value);
        return iterator(m_data + m_size - 1);
    }
    

    const T* value_ptr = &value;
    bool self_ref = (value_ptr >= m_data && value_ptr < m_data + m_size);
    T value_copy_storage = self_ref ? value : T();
    const T& value_ref = self_ref ? value_copy_storage : value;
    
    if (m_size >= m_capacity) {
        uint32_t new_cap = m_capacity == 0 ? 4 : m_capacity * 2;
        if (!reserve(new_cap)) {
            return end();
        }
    }
    
    for (uint32_t i = m_size; i > index; i--) {
        new (m_data + i) T(std::move(m_data[i - 1]));
        m_data[i - 1].~T();
    }
    
    new (m_data + index) T(value_ref);
    m_size++;
    
    return iterator(m_data + index);
}

    KNST_FORCE_INLINE iterator insert(const_iterator pos, T&& value) noexcept { // The move version of `insert`. It adds the element by moving it—faster than copying. The rest of the logic remains the same
        uint32_t index = static_cast<uint32_t>(pos.ptr - m_data);
        
        if (index > m_size) {
            push_back(std::move(value));
            return iterator(m_data + m_size - 1);
        }
        
        if (m_size >= m_capacity) {
            uint32_t new_cap = m_capacity == 0 ? 4 : m_capacity * 2;
            if (!reserve(new_cap)) {
                return end();
            }
        }
        
        for (uint32_t i = m_size; i > index; i--) {
            new (m_data + i) T(std::move(m_data[i - 1]));
            m_data[i - 1].~T();
        }
        
        new (m_data + index) T(std::move(value));
        m_size++;
        
        return iterator(m_data + index);
    }
   
    KNST_FORCE_INLINE iterator insert(const_iterator pos, uint32_t count, const T& value) noexcept { // Inserts `count` copies of `value` at position `pos`. If there is insufficient space, it expands the container as needed and shifts subsequent elements to the right. Returns the position of the first inserted element
        if (count == 0) return iterator(const_cast<T*>(pos.ptr));
        
        uint32_t index = static_cast<uint32_t>(pos.ptr - m_data);
        
        if (index > m_size) {
            for (uint32_t i = 0; i < count; i++) {
                push_back(value);
            }
            return iterator(m_data + m_size - count);
        }
        
        if (m_size + count > m_capacity) {
            uint32_t new_cap = m_capacity;
            while (new_cap < m_size + count) {
                new_cap = new_cap == 0 ? 4 : new_cap * 2;
            }
            if (!reserve(new_cap)) {
                return end();
            }
        }
        
        for (uint32_t i = m_size + count - 1; i >= index + count; i--) {
            new (m_data + i) T(std::move(m_data[i - count]));
            m_data[i - count].~T();
        }
        
        for (uint32_t i = 0; i < count; i++) {
            new (m_data + index + i) T(value);
        }
        
        m_size += count;
        
        return iterator(m_data + index);
    }
    
    template<typename InputIt>
    KNST_FORCE_INLINE iterator insert(const_iterator pos, InputIt first, InputIt last) noexcept { // Inserts the elements in the range `[first, last)` at position `pos`. It first counts the elements, expands the container if there is insufficient space, shifts existing elements, and then copies the new ones. It returns the position of the first inserted element
        if (first == last) return iterator(const_cast<T*>(pos.ptr));
        
        uint32_t index = static_cast<uint32_t>(pos.ptr - m_data);
        uint32_t count = 0;
        
        for (InputIt it = first; it != last; ++it) {
            count++;
        }
        
        if (count == 0) return iterator(const_cast<T*>(pos.ptr));
        
        if (index > m_size) {
            for (InputIt it = first; it != last; ++it) {
                push_back(*it);
            }
            return iterator(m_data + m_size - count);
        }
        
        if (m_size + count > m_capacity) {
            uint32_t new_cap = m_capacity;
            while (new_cap < m_size + count) {
                new_cap = new_cap == 0 ? 4 : new_cap * 2;
            }
            if (!reserve(new_cap)) {
                return end();
            }
        }
        
        for (uint32_t i = m_size + count - 1; i >= index + count; i--) {
            new (m_data + i) T(std::move(m_data[i - count]));
            m_data[i - count].~T();
        }
        
        uint32_t i = 0;
        for (InputIt it = first; it != last; ++it, ++i) {
            new (m_data + index + i) T(*it);
        }
        
        m_size += count;
        
        return iterator(m_data + index);
    }

    KNST_FORCE_INLINE iterator insert(const_iterator pos, std::initializer_list<T> list) noexcept { // Inserts the list {1,2,3} at position pos. Delegates to the iterator range version
        return insert(pos, list.begin(), list.end());
    }
 
    KNST_FORCE_INLINE iterator insert(uint32_t index, const T& value) noexcept { // Insertion by index. If the index is at the end or beyond, it delegates to `push_back`; otherwise, it delegates to the iterator-based version
        if (index >= m_size) {
            push_back(value);
            return iterator(m_data + m_size - 1);
        }
        return insert(const_iterator(m_data + index), value);
    }
    
    KNST_FORCE_INLINE iterator insert(uint32_t index, T&& value) noexcept { // It's the same index-based insert, but the move version. It delegates to `push_back` if at the end, or to the iterator-based insert otherwise
        if (index >= m_size) {
            push_back(std::move(value));
            return iterator(m_data + m_size - 1);
        }
        return insert(const_iterator(m_data + index), std::move(value));
    }
    
    KNST_FORCE_INLINE iterator insert(uint32_t index, uint32_t count, const T& value) noexcept { // Inserts `count` values ​​at the specified index. If at the end, it uses a loop with `push_back`; otherwise, it delegates to the iterator-based version
        if (index >= m_size) {
            for (uint32_t i = 0; i < count; i++) {
                push_back(value);
            }
            return iterator(m_data + m_size - count);
        }
        return insert(const_iterator(m_data + index), count, value);
    }

    KNST_FORCE_INLINE void erase_at(uint32_t index) noexcept { // It does the **same thing** as `erase(uint32_t)` — it deletes the element at the index and shifts the others. It's code duplication; you can remove one of them
        if (index >= m_size) return;
        
        m_data[index].~T();
        
        for (uint32_t i = index; i < m_size - 1; i++) {
            new (m_data + i) T(std::move(m_data[i + 1]));
            m_data[i + 1].~T();
        }
        
        m_size--;
    }

    KNST_FORCE_INLINE void assign(uint32_t count, const T& value) noexcept { // It completely replaces the contents—inserting `count` instances of `value`. It first clears the container, reserves space if necessary, and then populates it.
        clear();
    
        if (count > m_capacity) {
            if (!reserve(count)) return;
        }
        
        
        for (uint32_t i = 0; i < count; i++) {
            new (m_data + i) T(value);
            m_size++;
        }
    }

    template<typename InputIt>
    KNST_FORCE_INLINE void assign(InputIt first, InputIt last) noexcept { // Replaces the contents with the elements in the range [first, last). It performs clear, then reserve if necessary, and finally copies all the elements
        clear();
        
    
        uint32_t count = 0;
        for (InputIt it = first; it != last; ++it) {
            count++;
        }
        

        if (count > m_capacity) {
            if (!reserve(count)) return;
        }
        
    
        for (InputIt it = first; it != last; ++it) {
            new (m_data + m_size) T(*it);
            m_size++;
        }
    }

    KNST_FORCE_INLINE void assign(std::initializer_list<T> list) noexcept { // Replaces the contents with the list `{1,2,3}`. Delegates to the iterator range version
        assign(list.begin(), list.end());
    }

    KNST_FORCE_INLINE bool shrink_to_fit() noexcept { // It reduces the capacity for you—freeing up excess memory. It allocates new space, moves the elements, and discards the old storage
        if (m_size == m_capacity) return true;
        
       
        if (m_size == 0) {
            if (m_data) {
                m_allocator.deallocate(m_data, sizeof(T) * m_capacity);
                m_data = nullptr;
                m_capacity = 0;
            }
            return true;
        }
        
        
        T* new_data = static_cast<T*>(
            m_allocator.allocate(sizeof(T) * m_size)
        );
        if (!new_data) return false;
        
      
        for (uint32_t i = 0; i < m_size; i++) {
            new (new_data + i) T(std::move(m_data[i]));
            m_data[i].~T();
        }
        
      
        m_allocator.deallocate(m_data, sizeof(T) * m_capacity);
        
       
        m_data = new_data;
        m_capacity = m_size;
        
        return true;
    }

    template<typename OtherAlloc>
    KNST_FORCE_INLINE bool bridge_memory(OtherAlloc& alloc) { // A vector holds another allocator. It allocates memory using the new allocator, moves the elements, and discards the old one. It returns false if the allocator types are different
        if constexpr (std::is_same_v<Allocator, OtherAlloc>) {
            if (m_size > 0 && m_data) {
                uint32_t old_size = m_size;
                uint32_t old_capacity = m_capacity;
                
                
                T* new_data = static_cast<T*>(
                    alloc.allocate(sizeof(T) * old_capacity)
                );
                if (!new_data) return false;
                
                
                for (uint32_t i = 0; i < old_size; i++) {
                    new (new_data + i) T(std::move(m_data[i]));
                    m_data[i].~T();  
                }
                
             
                m_allocator.deallocate(m_data, sizeof(T) * old_capacity);
                
                
                m_allocator = alloc;
                
              
                m_data = new_data;
                m_size = old_size;
                m_capacity = old_capacity;
                
            } else {
                m_allocator = alloc;
            }
            return true;
        } else {
            return false;
        }
    }
    
    template<typename OtherAlloc>
    KNST_FORCE_INLINE bool bridge_memory(const OtherAlloc& alloc) { // The const version of bridge_memory — takes a const OtherAlloc&. The logic is the same
        if constexpr (std::is_same_v<Allocator, OtherAlloc>) {
            if (m_size > 0 && m_data) {
                uint32_t old_size = m_size;
                uint32_t old_capacity = m_capacity;
                
                T* new_data = static_cast<T*>(
                    alloc.allocate(sizeof(T) * old_capacity)
                );
                if (!new_data) return false;
                
                for (uint32_t i = 0; i < old_size; i++) {
                    new (new_data + i) T(std::move(m_data[i]));
                    m_data[i].~T();
                }
                
                m_allocator.deallocate(m_data, sizeof(T) * old_capacity);
                m_allocator = alloc;
                
                m_data = new_data;
                m_size = old_size;
                m_capacity = old_capacity;
                
            } else {
                m_allocator = alloc;
            }
            return true;
        } else {
            return false;
        }
    }
    
    template<typename OtherAlloc>
    KNST_FORCE_INLINE bool bridge_memory(OtherAlloc&& alloc) { // The rvalue version of bridge_memory — it moves the allocator. The logic is the same; only the allocator is moved
        if constexpr (std::is_same_v<Allocator, std::decay_t<OtherAlloc>>) {
            if (m_size > 0 && m_data) {
                uint32_t old_size = m_size;
                uint32_t old_capacity = m_capacity;
                
                T* new_data = static_cast<T*>(
                    alloc.allocate(sizeof(T) * old_capacity)
                );
                if (!new_data) return false;
                
                for (uint32_t i = 0; i < old_size; i++) {
                    new (new_data + i) T(std::move(m_data[i]));
                    m_data[i].~T();
                }
                
                m_allocator.deallocate(m_data, sizeof(T) * old_capacity);
                m_allocator = std::move(alloc);
                
                m_data = new_data;
                m_size = old_size;
                m_capacity = old_capacity;
                
            } else {
                m_allocator = std::move(alloc);
            }
            return true;
        } else {
            return false;
        }
    }

    KNST_FORCE_INLINE bool empty() const noexcept { // Empty check — true if m_size == 0
        return m_size == 0;
    }

    KNST_FORCE_INLINE uint32_t size() const noexcept { // Returns the number of elements
        return m_size;
    }

    KNST_FORCE_INLINE uint32_t capacity() const noexcept { // It returns the capacity—how many elements it can hold
        return m_capacity;
    }

    KNST_FORCE_INLINE basic_vector& operator=(const basic_vector& other) noexcept { // Copy assignment — copies `other` to `this`. It first clears its own contents, then copies from `other`
        if (this != &other) {
            
            
            if (m_data) {
                for (uint32_t i = 0; i < m_size; i++) {
                    m_data[i].~T();
                }
                m_allocator.deallocate(m_data, sizeof(T) * m_capacity);
            }
            
            
            m_size = 0;
            m_capacity = 0;
            m_data = nullptr;
            m_allocator = other.m_allocator;
            
            if (other.m_size > 0) {
                if (!reserve(other.m_size)) return *this;
                
                
                for (uint32_t i = 0; i < other.m_size; i++) {
                    new (m_data + i) T(other[i]);
                    m_size++;
                }
            }
        }
        return *this;
    }

    KNST_FORCE_INLINE basic_vector& operator=(basic_vector&& other) noexcept { // Move assignment — steals `other`'s pointers. It first deletes its own contents, then resets `other`
        if (this != &other) {
           
            
            if (m_data) {
                for (uint32_t i = 0; i < m_size; i++) {
                    m_data[i].~T();
                }
                m_allocator.deallocate(m_data, sizeof(T) * m_capacity);
            }

            m_allocator = std::move(other.m_allocator);
           
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            
            
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }
        return *this;
    }

    KNST_FORCE_INLINE T& operator[](uint32_t index) noexcept { // Accessing an element by index. No bounds checking — fast but dangerous
        return m_data[index];
    }
    
    KNST_FORCE_INLINE const T& operator[](uint32_t index) const noexcept { // const version — read-only index access. No bounds checking
        return m_data[index];
    }

    KNST_FORCE_INLINE bool operator==(const basic_vector& other) const noexcept { // Compares the equality of two vectors. Returns true if the lengths are the same and all elements are equal
        if (m_size != other.m_size) return false;
        for (uint32_t i = 0; i < m_size; i++) {
            if (!(m_data[i] == other.m_data[i])) return false;
        }
        return true;
    }

    KNST_FORCE_INLINE bool operator!=(const basic_vector& other) const noexcept { // The inverse of operator==. Returns true if not equal
        return !(*this == other);
    }


    //ITERATOR


    KNST_FORCE_INLINE iterator begin() noexcept {
        return iterator(m_data);
    }

    KNST_FORCE_INLINE iterator end() noexcept {
        return iterator(m_data + m_size);
    }

    KNST_FORCE_INLINE const_iterator begin() const noexcept {
        return const_iterator(m_data);
    }

    KNST_FORCE_INLINE const_iterator end() const noexcept {
        return const_iterator(m_data + m_size);
    }
    
    KNST_FORCE_INLINE const_iterator cbegin() const noexcept {
        return const_iterator(m_data);
    }

    KNST_FORCE_INLINE const_iterator cend() const noexcept {
        return const_iterator(m_data + m_size);
    }


};

template <typename T>
using knst_vector = basic_vector<T,knst_default_allocator>;

template <typename T,typename Allocator = knst_pool_allocator>
using knst_vector_sm = basic_vector<T,Allocator>;

