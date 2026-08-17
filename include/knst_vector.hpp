#ifndef KNST_VECTOR_HPP
#define KNST_VECTOR_HPP
#pragma once


template<typename T , typename Allocator = knst_default_allocator>
class basic_vector{

private:

    [[no_unique_address]] mutable Allocator  m_allocator; // If the template is empty, it won't take up space.


    T * m_data;
    uint32_t m_size;
    uint32_t m_capacity;

public:
    using iterator = knst_iterator<T>;
    using const_iterator = knst_const_iterator<T>;


    KNST_FORCE_INLINE basic_vector(const Allocator& allocator) noexcept: m_data(nullptr), m_size(0),m_capacity(0){};

    KNST_FORCE_INLINE basic_vector() noexcept : m_data(nullptr), m_size(0),m_capacity(0){};

    KNST_FORCE_INLINE ~basic_vector() noexcept{
        for (uint32_t i = 0; i < m_size; i++) {
            m_data[i].~T();
        }
        if(m_data) m_allocator.deallocate(m_data,sizeof(T) * m_capacity);
    }

      
    KNST_FORCE_INLINE basic_vector(uint32_t count, const T& value = T()) noexcept 
        : m_data(nullptr), m_size(0), m_capacity(0) {
        
        if (count > 0) {
            reserve(count);
            for (uint32_t i = 0; i < count; i++) {
                new (m_data + i) T(value);
                m_size++;
            }
        }
    }

   
    KNST_FORCE_INLINE basic_vector(std::initializer_list<T> list) noexcept 
        : m_data(nullptr), m_size(0), m_capacity(0) {
        
        uint32_t count = static_cast<uint32_t>(list.size());
        if (count > 0) {
            reserve(count);
            for (const auto& item : list) {
                new (m_data + m_size) T(item);
                m_size++;
            }
        }
    }

  
    KNST_FORCE_INLINE basic_vector(const T* data, uint32_t count) noexcept 
        : m_data(nullptr), m_size(0), m_capacity(0) {
        
        if (count > 0 && data) {
            reserve(count);
            for (uint32_t i = 0; i < count; i++) {
                new (m_data + i) T(data[i]);
                m_size++;
            }
        }
    }

    KNST_FORCE_INLINE basic_vector(const basic_vector& other) noexcept : m_data(nullptr), m_size(0), m_capacity(0) {
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

    KNST_FORCE_INLINE basic_vector(basic_vector&& other) noexcept : m_data(other.m_data),m_size(other.m_size), m_capacity(other.m_capacity) {
        
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
    }




    KNST_FORCE_INLINE bool push_back(const T&value) noexcept{ // classic append

        if(m_capacity > m_size){
            new (m_data + m_size) T(value);
            ++m_size;
            return true;
        }
         
        uint32_t new_cap = m_capacity == 0 ? 4 : m_capacity * 2; // 2x growth
        if(reserve(new_cap)){
            new (m_data + m_size) T(value);
            ++m_size;
            return true;

        }

        return false;

    }

    KNST_FORCE_INLINE bool push_back(const T && value) noexcept{ // move type append

        if(m_capacity > m_size){
            new (m_data + m_size) T(std::move(value));
            ++m_size;
            return true;
        }
         
        uint32_t new_cap = m_capacity == 0 ? 4 : m_capacity * 2; // 2x growth same
        if(reserve(new_cap)){
            new (m_data + m_size) T(std::move(value));
            ++m_size;
            return true;

        }

        return false;

    }

    KNST_FORCE_INLINE void pop_back() noexcept {
        if (m_size > 0) {
            m_data[m_size - 1].~T();
            m_size--;
        }
    }

    KNST_FORCE_INLINE iterator erase(const_iterator pos) noexcept {
        uint32_t index = static_cast<uint32_t>(pos.ptr - m_data);
        
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

    KNST_FORCE_INLINE void erase(uint32_t index) noexcept {
        if (index >= m_size) return;
        
        m_data[index].~T();
        
        for (uint32_t i = index; i < m_size - 1; i++) {
            new (m_data + i) T(std::move(m_data[i + 1]));
            m_data[i + 1].~T();
        }
        
        m_size--;
    }
    
    KNST_FORCE_INLINE T& back() noexcept {
        return m_data[m_size - 1];
    }

    KNST_FORCE_INLINE const T& back() const noexcept {
        return m_data[m_size - 1];
    }

    KNST_FORCE_INLINE bool erase_value(const T& value) noexcept {
        for (uint32_t i = 0; i < m_size; i++) {
            if (m_data[i] == value) {
                erase(i);
                return true;
            }
        }
        return false;
    }

    KNST_FORCE_INLINE iterator find(const T& value) noexcept {
        for (uint32_t i = 0; i < m_size; i++) {
            if (m_data[i] == value) {
                return iterator(m_data + i);
            }
        }
        return end();
    }


    KNST_FORCE_INLINE const_iterator find(const T& value) const noexcept {
        for (uint32_t i = 0; i < m_size; i++) {
            if (m_data[i] == value) {
                return const_iterator(m_data + i);
            }
        }
        return end();
    }
    KNST_FORCE_INLINE bool reserve (uint32_t new_total_memner_count) noexcept{ // reserve

        if(new_total_memner_count <= m_capacity) return true;

        T * new_data = static_cast<T*>(m_allocator.allocate(sizeof(T) * new_total_memner_count));
        if(!new_data)return false;
            
        if(m_data){

            for(uint32_t i = 0; i < m_size; i++){
                new_data[i] = std::move(m_data[i]);
            }

            m_allocator.deallocate(m_data,sizeof(T) * m_capacity);
        }
        
        m_data = new_data;
        m_capacity = new_total_memner_count;

        return true;
    }



    KNST_FORCE_INLINE void clear() noexcept {
        
        for (uint32_t i = 0; i < m_size; i++) {
            m_data[i].~T();
        }
        m_size = 0;
    }
    
    T* data() noexcept {
        return m_data;
    }
    KNST_FORCE_INLINE const T* data() const noexcept {
        return m_data;
    }

    KNST_FORCE_INLINE void resize(uint32_t new_size) noexcept { // resize
        if (new_size < m_size) {
            
            for (uint32_t i = new_size; i < m_size; i++) {
                m_data[i].~T();
            }
            m_size = new_size;
        }
        else if (new_size > m_size) {
            
            if (new_size > m_capacity) {
                reserve(new_size);
            }
            
            for (uint32_t i = m_size; i < new_size; i++) {
                new (m_data + i) T();
            }
            m_size = new_size;
        }
        
    }

    KNST_FORCE_INLINE void resize(uint32_t new_size, const T& default_value) noexcept { // size + value type
        if (new_size < m_size) {
           
            for (uint32_t i = new_size; i < m_size; i++) {
                m_data[i].~T();
            }
            m_size = new_size;
        }
        else if (new_size > m_size) {
            
            if (new_size > m_capacity) {
                reserve(new_size);
            }
            
            for (uint32_t i = m_size; i < new_size; i++) {
                new (m_data + i) T(default_value);
            }
            m_size = new_size;
        }
    }

    template<typename... Args>
    KNST_FORCE_INLINE bool emplace_back(Args&&... args) noexcept {
        if (m_capacity > m_size) {
           
            new(m_data + m_size) T(std::forward<Args>(args)...);
            ++m_size;
            return true;
        }
        
       
        uint32_t new_cap = (m_capacity == 0) ? 4 : m_capacity * 2;
        
       
        if (reserve(new_cap)) {
          
            new(m_data + m_size) T(std::forward<Args>(args)...);
            ++m_size;
            return true;
        }
        
        return false;
    }

   
    KNST_FORCE_INLINE bool shrink_to_fit() noexcept {
       
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
    KNST_FORCE_INLINE bool bridge_memory(OtherAlloc& alloc) {
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
    KNST_FORCE_INLINE bool bridge_memory(const OtherAlloc& alloc) {
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
    KNST_FORCE_INLINE bool bridge_memory(OtherAlloc&& alloc) {
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

    KNST_FORCE_INLINE bool empty() const noexcept {
        return m_size == 0;
    }

    KNST_FORCE_INLINE uint32_t size() const noexcept {
        return m_size;
    }

    KNST_FORCE_INLINE uint32_t capacity() const noexcept {
        return m_capacity;
    }


    KNST_FORCE_INLINE basic_vector& operator=(const basic_vector& other) noexcept { // copy assignment operator
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

    KNST_FORCE_INLINE basic_vector& operator=(basic_vector&& other) noexcept { // move assignment operator
        if (this != &other) {
           
            
            if (m_data) {
                for (uint32_t i = 0; i < m_size; i++) {
                    m_data[i].~T();
                }
                m_allocator.deallocate(m_data, sizeof(T) * m_capacity);
            }
            
           
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            
            
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }
        return *this;
    }


    KNST_FORCE_INLINE T& operator[](uint32_t index) noexcept {
        return m_data[index];
    }

    
    KNST_FORCE_INLINE const T& operator[](uint32_t index) const noexcept {
        return m_data[index];
    }

    KNST_FORCE_INLINE bool operator==(const basic_vector& other) const noexcept {
        if (m_size != other.m_size) return false;
        for (uint32_t i = 0; i < m_size; i++) {
            if (!(m_data[i] == other.m_data[i])) return false;
        }
        return true;
    }

    KNST_FORCE_INLINE bool operator!=(const basic_vector& other) const noexcept {
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


#endif