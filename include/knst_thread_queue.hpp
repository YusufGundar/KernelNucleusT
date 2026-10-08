// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
----------------------------
knst_thread_queue.hpp
----------------------------

`noexcept` + `bool` return. No exceptions are thrown; it returns `false` on OOM. This is the `knst_thread_pool` queue—worker threads pull tasks from here

*/


#pragma once


template<typename T>
class knst_thread_queue {

private:
    KNST_FORCE_INLINE bool ensure_room() noexcept { // Prepares space for a push operation. If the buffer is empty, it starts at a size of 64; if full, it doubles the size. Returns true if space is available
        if (m_buffer.size() == 0) {
            return grow(64);
        }
        if (m_count == m_buffer.size()) {
            return grow((uint32_t)m_buffer.size() * 2);
        }
        return true;
    }

    inline bool grow(uint32_t new_cap) noexcept { // It expands the tail. It allocates a new `knst_vector` and moves the existing elements from the circular layout to a linear sequence (relinearizes them). It sets `head` to 0 and `tail` to `count`
        knst_vector<T> new_buf;
        if (!new_buf.reserve(new_cap)) return false;
        new_buf.resize(new_cap);

        for (uint32_t i = 0; i < m_count; ++i) {
            uint32_t idx = (m_head + i) % (uint32_t)m_buffer.size();
            new_buf[i] = std::move(m_buffer[idx]);
        }

        m_buffer = std::move(new_buf);
        m_head = 0;
        m_tail = m_count;
        return true;
    }

    knst_vector<T> m_buffer; // The actual storage is `knst_vector<T>`. The queue elements are arranged circularly within it
    uint32_t m_head  = 0; // The index of the first element. pop/front reads from here
    uint32_t m_tail  = 0; // The index of the next empty slot. push writes here
    uint32_t m_count = 0; // The number of elements in the queue. size()/empty() reads this


public:
    KNST_FORCE_INLINE knst_thread_queue() noexcept = default; // Default constructor — creates an empty queue. m_head=0, m_tail=0, m_count=0, m_buffer is empty. noexcept ==>  does not throw an exception

    KNST_FORCE_INLINE knst_thread_queue(const knst_thread_queue&) = delete; // Deletes the copy constructor. The queue cannot be copied—it is move-only. Reason: `knst_vector` might also be move-only, and it makes sense for the queue to have a single owner
    KNST_FORCE_INLINE knst_thread_queue& operator=(const knst_thread_queue&) = delete; // It deletes the copy assignment operator. The queue cannot be copied to another queue; it remains move-only

    KNST_FORCE_INLINE bool empty() const noexcept { return m_count == 0; } // Check if the queue is empty. true ==>  no elements
    
    KNST_FORCE_INLINE uint32_t size() const noexcept { return m_count; } // Returns the number of elements in the queue

    KNST_FORCE_INLINE uint32_t capacity() const noexcept { return (uint32_t)m_buffer.size(); } // Returns the total capacity of the queue. `m_buffer.size()` ==> the number of elements in the vector (i.e., how many slots there are). Cast to `uint32_t`
   
    KNST_FORCE_INLINE bool reserve(uint32_t cap) noexcept { // You called the same function twice, mate. `reserve` ensures the capacity is at least `cap`. It doesn't do anything if it's already sufficient; otherwise, it expands it using `grow`
        if (cap == 0) return true;
        if (cap <= m_buffer.size()) return true;
        return grow(cap);
    }

    KNST_FORCE_INLINE bool push(const T& value) noexcept { // Copies an element to the queue. `ensure_room` expands the queue if there is no space. Writes to the `tail` slot, advances `tail` (circularly), and increments `count`
        if (!ensure_room()) return false;
        m_buffer[m_tail] = value;
        m_tail = (m_tail + 1) % (uint32_t)m_buffer.size();
        ++m_count;
        return true;
    }

    KNST_FORCE_INLINE bool push(T&& value) noexcept { // The "push" move version. It adds the element by moving it—faster than copying. The underlying logic remains the same: write to the tail, advance the tail, and increment the count
        if (!ensure_room()) return false;
        m_buffer[m_tail] = std::move(value);
        m_tail = (m_tail + 1) % (uint32_t)m_buffer.size();
        ++m_count;
        return true;
    }

    KNST_FORCE_INLINE bool pop(T& out) noexcept { // Removes an element from the tail. Returns false if empty. Moves the element at the head to `out`, advances the head, and decrements the count. Follows FIFO logic
        if (m_count == 0) return false;
        out = std::move(m_buffer[m_head]);
        m_head = (m_head + 1) % (uint32_t)m_buffer.size();
        --m_count;
        return true;
    }

    KNST_FORCE_INLINE T& front() noexcept { return m_buffer[m_head]; } // Returns the first element of the queue by reference. Does not remove it; only accesses it. Undefined behavior if empty

    KNST_FORCE_INLINE const T& front() const noexcept { return m_buffer[m_head]; } // The `const` version — returns the first element for reading. It is immutable

    KNST_FORCE_INLINE void clear() noexcept { // Clears the queue. Resets all elements using T{}, and resets head, tail, and count. Memory is not deallocated
        for (uint32_t i = 0; i < m_count; ++i) {
            uint32_t idx = (m_head + i) % (uint32_t)m_buffer.size();
            m_buffer[idx] = T{};
        }
        m_head = 0;
        m_tail = 0;
        m_count = 0;
    }

};