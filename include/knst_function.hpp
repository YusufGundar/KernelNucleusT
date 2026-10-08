// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0



/*
----------------------------
knst_function.hpp
----------------------------

    It can store any lambda, function pointer, or function with the signature `void()`
    Callables up to 64 bytes can be stored on the stack using SO (Small Object Optimization); they spill over to the heap if they exceed 64 bytes—though, of course, you can adjust this value via a macro
    Move-only; it cannot be copied

    Usage scenarios: It is fully compatible with knst_thread structures; additionally, you can use it wherever it meets your needs. It supports only void functions; return types are not supported

*/


#pragma once


#ifndef KNST_FUNCTION_INLINE_SIZE
    #define KNST_FUNCTION_INLINE_SIZE 64 // the number of bytes required for it to reside on the stack
#endif


class knst_function {

private:
    struct vtable_t { // vtable struct
        void (*invoke)(void*);
        void (*destroy)(void*);
        void (*move)(void*, void*);
    };

    
    // invoke => call the lambda
    // destroy => destroy the lambda
    // move => move the lambda from one place to another


    template<typename F>
    static const vtable_t* vtable_for() noexcept { // Generates a table with 3 functions for the vtable.

        static const vtable_t vt = {
            
            [](void* s) { (*static_cast<F*>(s))(); },
            [](void* s) { static_cast<F*>(s)->~F(); },
            [](void* from, void* to) {::new (to) F(std::move(*static_cast<F*>(from)));

            }
        };
        return &vt;
    }

    KNST_FORCE_INLINE void* storage() noexcept { // returns the address of the lambda
        return m_is_heap ? m_heap_ptr : static_cast<void*>(m_buffer);
    }

    template<typename F>
    void assign(F&& fn) noexcept { // Place it in the necessary spots according to the size of the lamp.
        using T = std::decay_t<F>;

        static_assert(
            std::is_nothrow_move_constructible_v<T> ||
            std::is_nothrow_copy_constructible_v<T>,
            "!?!! knst_function requires nothrow-movable / copyable callable !?!!"
        );

        constexpr bool fits_inline =
            sizeof(T) <= KNST_FUNCTION_INLINE_SIZE &&
            alignof(T) <= alignof(std::max_align_t);

        if constexpr (fits_inline) {
            ::new (static_cast<void*>(m_buffer)) T(std::forward<F>(fn));
             m_is_heap = false;

        } else {
            void* p = ::operator new(sizeof(T), std::nothrow);
            if (!p) { 
                m_vtable = nullptr; return; 
            }
            ::new (p) T(std::forward<F>(fn));
            m_heap_ptr = p;
            m_is_heap  = true;
        }
        m_vtable = vtable_for<T>();
    }


    void move_from(knst_function& other) noexcept { // It retrieves data from another class and then clears the contents of that class.
        m_is_heap = other.m_is_heap;
        m_vtable  = other.m_vtable;

        if (other.m_vtable) {
            if (other.m_is_heap) {
                m_heap_ptr = other.m_heap_ptr;
                other.m_heap_ptr = nullptr;
            } else {
                other.m_vtable->move(other.storage(), storage());
                other.m_vtable->destroy(other.storage());

            }
        }

        other.m_vtable   = nullptr;
        other.m_heap_ptr = nullptr;
        other.m_is_heap  = false;
    }


    alignas(std::max_align_t) unsigned char m_buffer[KNST_FUNCTION_INLINE_SIZE]; // storage for the stack

    void* m_heap_ptr = nullptr; // the address of the lambda on the heap

    const vtable_t* m_vtable = nullptr; // type information

    bool m_is_heap = false; // heap status


public:
    KNST_FORCE_INLINE knst_function() noexcept = default; // default constructor

    template<typename F,typename = std::enable_if_t< !std::is_same_v<std::decay_t<F>, knst_function> >> // It prevents it from calling itself.
             
                 
    
    KNST_FORCE_INLINE knst_function(F&& fn) noexcept { // perfect fowarding for assign function
        assign(std::forward<F>(fn));
    }

    KNST_FORCE_INLINE knst_function(knst_function&& other) noexcept { // move constructor
        move_from(other);
    }

    KNST_FORCE_INLINE knst_function& operator=(knst_function&& other) noexcept { // move operator for another knst_function class
        if (this != &other) {
            reset();
            move_from(other);
        }
        return *this;
    }

    //They prevent the class from being copied
    KNST_FORCE_INLINE knst_function(const knst_function&) = delete;
    KNST_FORCE_INLINE knst_function& operator=(const knst_function&) = delete;
    //________________________________________



   
    ~knst_function() { reset(); }  // destructor
 
    KNST_FORCE_INLINE void operator()() { // call operator like a function
        if (m_vtable) m_vtable->invoke(storage());
    }


    KNST_FORCE_INLINE  bool empty() const noexcept { return m_vtable == nullptr; } // Returns the empty state

    KNST_FORCE_INLINE explicit operator bool() const noexcept { return m_vtable != nullptr; } // It allows you to check the class directly; essentially, it returns the status of whether the function exists or not

    

    KNST_FORCE_INLINE void reset() noexcept { // cleans the class and functions
        if (m_vtable) {

            m_vtable->destroy(storage());
            if (m_is_heap) ::operator delete(m_heap_ptr);
        }
        m_vtable = nullptr;
        m_heap_ptr = nullptr;
        m_is_heap = false;
    }
};