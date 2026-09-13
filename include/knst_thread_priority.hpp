/*
----------------------------
knst_thread_priority.hpp
----------------------------

    It makes thread priority levels platform-agnostic. It maps them to `THREAD_PRIORITY_*` on Windows and `nice` values ​​on POSIX. Using `knst_apply_priority_self_graceful`, it settles on the "closest achievable" level—for instance, if a user requests `time_critical` but lacks the necessary privileges, it silently falls back to `normal` instead of crashing

*/



#pragma once




#if KNST_USING_PLATFORM_WINDOWS
#else
    #include <sys/resource.h>
    #include <sys/syscall.h>
    #include <unistd.h>
#endif


enum class knst_thread_priority : int8_t { // Thread priority levels. inherit = inherit from parent; the rest range from low to high. High number = high priority
    inherit = -128,
    lowest = -2,
    low = -1,
    normal = 0,
    high = 1,
    highest = 2,
    time_critical = 3
};



#if KNST_USING_PLATFORM_WINDOWS  // Windows priority mapping. to_win ==> converts the enum to a THREAD_PRIORITY_* constant. from_win ==>  the reverse. try_apply_priority_self ==> assigns priority to the current thread

    KNST_FORCE_INLINE int knst_priority_to_win(knst_thread_priority p) noexcept { 
        switch (p) {
            case knst_thread_priority::lowest:return THREAD_PRIORITY_LOWEST;
            case knst_thread_priority::low: return THREAD_PRIORITY_BELOW_NORMAL;
            case knst_thread_priority::normal: return THREAD_PRIORITY_NORMAL;
            case knst_thread_priority::high: return THREAD_PRIORITY_ABOVE_NORMAL;
            case knst_thread_priority::highest: return THREAD_PRIORITY_HIGHEST;
            case knst_thread_priority::time_critical: return THREAD_PRIORITY_TIME_CRITICAL;
            default: return THREAD_PRIORITY_NORMAL;
        }
    }

    KNST_FORCE_INLINE knst_thread_priority knst_priority_from_win(int native) noexcept {
        switch (native) {
            case THREAD_PRIORITY_LOWEST:return knst_thread_priority::lowest;
            case THREAD_PRIORITY_BELOW_NORMAL: return knst_thread_priority::low;
            case THREAD_PRIORITY_NORMAL: return knst_thread_priority::normal;
            case THREAD_PRIORITY_ABOVE_NORMAL: return knst_thread_priority::high;
            case THREAD_PRIORITY_HIGHEST:return knst_thread_priority::highest;
            case THREAD_PRIORITY_TIME_CRITICAL: return knst_thread_priority::time_critical;
            default: return knst_thread_priority::normal;
        }
    }

    KNST_FORCE_INLINE bool knst_try_apply_priority_self(knst_thread_priority p) noexcept {
        return ::SetThreadPriority(::GetCurrentThread(), knst_priority_to_win(p)) != 0;
    }

#else // POSIX priority mapping. to_nice ==>  converts enum to nice value (-20 is highest, 19 is lowest — inverse logic). from_nice ==>  converts nice value to enum (threshold-based). current_native_tid ==>  retrieves the thread ID (gettid or getpid). try_apply_priority_self ==>  assigns priority to the current thread using setpriority
    
    KNST_FORCE_INLINE int knst_priority_to_nice(knst_thread_priority p) noexcept { 
        switch (p) {
            case knst_thread_priority::lowest: return 19;
            case knst_thread_priority::low: return 10;
            case knst_thread_priority::normal: return 0;
            case knst_thread_priority::high: return -5;
            case knst_thread_priority::highest: return -10;
            case knst_thread_priority::time_critical: return -20;
            default: return 0;
        }
    }

    KNST_FORCE_INLINE knst_thread_priority knst_priority_from_nice(int nice) noexcept {
        if (nice >= 15) return knst_thread_priority::lowest;
        if (nice >= 5) return knst_thread_priority::low;
        if (nice > -5) return knst_thread_priority::normal;
        if (nice > -10) return knst_thread_priority::high;
        if (nice > -20) return knst_thread_priority::highest;
        return knst_thread_priority::time_critical;
    }

    KNST_FORCE_INLINE int32_t knst_current_native_tid() noexcept {
        #if defined(SYS_gettid)
            return static_cast<int32_t>(::syscall(SYS_gettid));
        #else
            return static_cast<int32_t>(::getpid());
        #endif
    }

    KNST_FORCE_INLINE bool knst_try_apply_priority_self(knst_thread_priority p) noexcept {
        return ::setpriority(PRIO_PROCESS, 0, knst_priority_to_nice(p)) == 0;
    }

#endif

    inline bool knst_apply_priority_self_graceful(knst_thread_priority requested,knst_thread_priority* achieved_out) noexcept { // It attempts to apply the priority setting; if that fails, it falls back to the next level down. `inherit`, on the other hand, does not intervene at all. Otherwise, it starts from the desired level and tests lower ones, writing the first successful result to `achieved_out`. If none succeed, it returns `false` + `inherit`
        if (requested == knst_thread_priority::inherit) {
            if (achieved_out) *achieved_out = knst_thread_priority::inherit;
            return true;
        }

        static constexpr knst_thread_priority ladder[] = {
            knst_thread_priority::time_critical,
            knst_thread_priority::highest,
            knst_thread_priority::high,
            knst_thread_priority::normal,
            knst_thread_priority::low,
            knst_thread_priority::lowest
        };
        constexpr size_t ladder_size = sizeof(ladder) / sizeof(ladder[0]);
        const int8_t requested_v = static_cast<int8_t>(requested);

        size_t start = 0;
        while (start < ladder_size &&
               static_cast<int8_t>(ladder[start]) > requested_v) {
            ++start;
        }

        for (size_t i = start; i < ladder_size; ++i) {
            if (knst_try_apply_priority_self(ladder[i])) {
                if (achieved_out) *achieved_out = ladder[i];
                return true;
            }
        }

        if (achieved_out) *achieved_out = knst_thread_priority::inherit;
        return false;
    }
