// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
----------------------------
knst_file.hpp
----------------------------

    A cross-platform file system library — file/directory reading, writing, deleting, copying, moving, listing, permission/timestamp management, and extensive file type recognition.
    It encompasses many features such as files, file paths, data, paths, metadata, and data reading.

*/


#pragma once

#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <type_traits>
#include <cstdio>
#include <ctime>
#include <atomic>
#include <algorithm>

#if KNST_USING_PLATFORM_WINDOWS
    #include <winioctl.h>

    #ifndef SYMBOLIC_LINK_FLAG_DIRECTORY
        #define SYMBOLIC_LINK_FLAG_DIRECTORY 0x1
    #endif
    #ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
        #define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
    #endif
#else
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <sys/time.h>
    #include <sys/statvfs.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <dirent.h>
    #include <cerrno>
    #include <ctime>
    #include <climits>
    #include <cstdlib>
#endif


// You can check the error messages, structures, and statuses here, folks
enum class knst_file_error : uint8_t {
    None = 0,
    NotFound,
    PermissionDenied,
    IsDirectory,
    TooLarge,
    OutOfMemory,
    ReadError,
    InvalidPath,
    SymlinkLoop,
    NotAFile,
    AlreadyExists,
    NotEmpty,
    WriteError,
    DiskFull,
    TempFileError,
    Unknown
};
inline const char* knst_file_error_string(knst_file_error e) noexcept {
    switch (e) {
        case knst_file_error::None:             return "No error";
        case knst_file_error::NotFound:         return "File not found";
        case knst_file_error::PermissionDenied: return "Permission denied";
        case knst_file_error::IsDirectory:      return "Path is a directory";
        case knst_file_error::TooLarge:         return "File too large";
        case knst_file_error::OutOfMemory:      return "Out of memory";
        case knst_file_error::ReadError:        return "Read error";
        case knst_file_error::InvalidPath:      return "Invalid path";
        case knst_file_error::SymlinkLoop:      return "Symlink loop detected";
        case knst_file_error::NotAFile:         return "Not a regular file";
        case knst_file_error::AlreadyExists:    return "Already exists";
        case knst_file_error::NotEmpty:         return "Directory not empty";
        case knst_file_error::WriteError:       return "Write error";
        case knst_file_error::DiskFull:         return "Disk full";
        case knst_file_error::TempFileError:    return "Temporary file error";
        case knst_file_error::Unknown:          return "Unknown error";
    }
    return "Unknown error";
}
//____________________________________________________________________________


#if KNST_USING_PLATFORM_WINDOWS
    #define KNST_FILE_SEPARATOR u'\\'
    #define KNST_FILE_SEPARATOR_C '\\'
#else
    #define KNST_FILE_SEPARATOR u'/'
    #define KNST_FILE_SEPARATOR_C '/'
#endif



namespace knst_file_detail {

inline constexpr uint64_t KNST_EXT_NONASCII_SENTINEL = ~static_cast<uint64_t>(0);
inline constexpr int KNST_MAX_SAFE_RECURSION_DEPTH = 4096;

inline constexpr uint64_t PackExactImpl(const char* s, uint64_t acc, int i) noexcept {
    if (s[i] == '\0' || i >= 8) return acc;
    return PackExactImpl(s, acc | (static_cast<uint64_t>(static_cast<uint8_t>(s[i])) << (i * 8)), i + 1);
}

inline constexpr uint64_t PackLowerImpl(const char* s, uint64_t acc, int i) noexcept {
    if (s[i] == '\0' || i >= 8) return acc;
    char c = s[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return PackLowerImpl(s, acc | (static_cast<uint64_t>(static_cast<uint8_t>(c)) << (i * 8)), i + 1);
}

// It performs case normalization when checking or reading the extension.

inline constexpr uint64_t PackExact(const char* s) noexcept {
    return PackExactImpl(s, 0, 0);
}
inline constexpr uint64_t PackLower(const char* s) noexcept {
    return PackLowerImpl(s, 0, 0);
}

inline void PackChecked(const char16_t* s, uint32_t len, uint64_t& out_cs, uint64_t& out_ci) noexcept {
    uint64_t cs = 0, ci = 0;
    for (uint32_t i = 0; i < len; ++i) {
        char16_t c = s[i];
        if (c > 0x7F) {
            out_cs = KNST_EXT_NONASCII_SENTINEL;
            out_ci = KNST_EXT_NONASCII_SENTINEL;
            return;
        }
        char16_t cl = (c >= u'A' && c <= u'Z') ? static_cast<char16_t>(c + 32) : c;
        cs |= static_cast<uint64_t>(static_cast<uint8_t>(c))  << (i * 8);
        ci |= static_cast<uint64_t>(static_cast<uint8_t>(cl)) << (i * 8);
    }
    out_cs = cs;
    out_ci = ci;
}

inline constexpr bool IsSep(char16_t c) noexcept {
#if KNST_USING_PLATFORM_WINDOWS
    return c == u'/' || c == u'\\';
#else
    return c == u'/';
#endif
}

inline uint32_t FindLastSep(const knst_c16string& p) noexcept {
    uint32_t last = UINT32_MAX;
    uint32_t len = p.length();
    for (uint32_t i = 0; i < len; ++i) {
        if (IsSep(p[i])) last = i;
    }
    return last;
}

inline void ExtractExtension(const knst_c16string& name,
                              knst_c16string& out_ext,
                              knst_c16string& out_full,
                              uint64_t& out_ext_cs,
                              uint64_t& out_ext_ci,
                              uint64_t& out_full_cs,
                              uint64_t& out_full_ci,
                              uint32_t& out_ext_len) noexcept {
    out_ext.clear();
    out_full.clear();
    out_ext_cs = 0; out_ext_ci = 0;
    out_full_cs = 0; out_full_ci = 0;
    out_ext_len = 0;

    uint32_t name_len = name.length();
    if (name_len == 0) return;

    uint32_t last_dot = UINT32_MAX;
    for (uint32_t i = name_len; i > 0; --i) {
        if (name[i - 1] == u'.') { last_dot = i - 1; break; }
    }
    if (last_dot == UINT32_MAX || last_dot == 0) return;

    uint32_t ext_length = name_len - last_dot;
    out_ext = name.substr(last_dot, ext_length);
    out_ext_len = ext_length;

    if (ext_length <= 8) {
        PackChecked(name.data() + last_dot, ext_length, out_ext_cs, out_ext_ci);
    }

    uint32_t prev_dot = UINT32_MAX;
    for (uint32_t i = last_dot; i > 0; --i) {
        if (name[i - 1] == u'.') { prev_dot = i - 1; break; }
    }
    if (prev_dot == UINT32_MAX || prev_dot == 0) return;

    uint32_t prev_ext_len = last_dot - prev_dot;
    if (prev_ext_len == 0 || prev_ext_len > 8) return;

    uint64_t prev_cs = 0, prev_ci = 0;
    PackChecked(name.data() + prev_dot, prev_ext_len, prev_cs, prev_ci);

    bool is_double = false;
    if (prev_ci == PackLower(".tar"))  is_double = true;
    if (prev_ci == PackLower(".min"))  is_double = true;
    if (prev_ci == PackLower(".user")) is_double = true;
    if (prev_ci == PackLower(".test")) is_double = true;
    if (prev_ci == PackLower(".d") && out_ext_ci == PackLower(".ts")) is_double = true;

    if (!is_double) return;

    uint32_t full_len = name_len - prev_dot;
    out_full = name.substr(prev_dot, full_len);

    if (full_len <= 8) {
        PackChecked(name.data() + prev_dot, full_len, out_full_cs, out_full_ci);
    }
}

inline knst_c16string MakeTempSuffix() noexcept {
    static std::atomic<uint32_t> s_counter{0};
    uint32_t c = s_counter.fetch_add(1, std::memory_order_relaxed);
    uint64_t t = static_cast<uint64_t>(std::time(nullptr));

#if KNST_USING_PLATFORM_WINDOWS
    uint32_t pid = static_cast<uint32_t>(GetCurrentProcessId());
#else
    uint32_t pid = static_cast<uint32_t>(getpid());
#endif

    knst_c16string suffix(u".knsttmp");
    suffix.append(static_cast<unsigned long long>(t));
    suffix.append(u"_");
    suffix.append(static_cast<unsigned int>(pid));
    suffix.append(u"_");
    suffix.append(static_cast<unsigned int>(c));
    return suffix;
}

#if KNST_USING_PLATFORM_WINDOWS
inline uint32_t WindowsRootPrefixLength(const knst_c16string& p) noexcept {
    if (p.length() >= 4 &&
        p[0] == u'\\' && p[1] == u'\\' &&
        (p[2] == u'?' || p[2] == u'.') && p[3] == u'\\') {
        uint32_t i = 4;
        if (i + 1 < p.length() && p[i + 1] == u':') {
            i += 2;
            if (i < p.length() && IsSep(p[i])) i++;
            return i;
        }
        if (i + 3 <= p.length() &&
            p[i] == u'U' && p[i + 1] == u'N' && p[i + 2] == u'C' &&
            i + 3 < p.length() && IsSep(p[i + 3])) {
            i += 4;
            while (i < p.length() && !IsSep(p[i])) i++;
            if (i < p.length()) i++;
            while (i < p.length() && !IsSep(p[i])) i++;
            if (i < p.length()) i++;
            return i;
        }
        return i;
    }
    if (p.length() >= 2 && p[0] == u'\\' && p[1] == u'\\') {
        uint32_t i = 2;
        while (i < p.length() && !IsSep(p[i])) i++;
        if (i < p.length()) i++;
        while (i < p.length() && !IsSep(p[i])) i++;
        if (i < p.length()) i++;
        return i;
    }
    if (p.length() >= 2 && p[1] == u':') {
        uint32_t i = 2;
        if (i < p.length() && IsSep(p[i])) i++;
        return i;
    }
    if (p.length() >= 1 && IsSep(p[0])) {
        return 1;
    }
    return 0;
}

inline knst_file_error MapWinError(DWORD e) noexcept {
    switch (e) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_NAME:
            return knst_file_error::NotFound;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            return knst_file_error::PermissionDenied;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            return knst_file_error::OutOfMemory;
        case ERROR_INVALID_PARAMETER:
        case ERROR_INVALID_DRIVE:
        case ERROR_BAD_PATHNAME:
            return knst_file_error::InvalidPath;
        case ERROR_READ_FAULT:
        case ERROR_CRC:
            return knst_file_error::ReadError;
        case ERROR_WRITE_FAULT:
            return knst_file_error::WriteError;
        case ERROR_HANDLE_DISK_FULL:
        case ERROR_DISK_FULL:
            return knst_file_error::DiskFull;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            return knst_file_error::AlreadyExists;
        case ERROR_DIR_NOT_EMPTY:
            return knst_file_error::NotEmpty;
        default:
            return knst_file_error::Unknown;
    }
}
#else
inline knst_file_error MapErrno(int e) noexcept {
    switch (e) {
        case ENOENT:  return knst_file_error::NotFound;
        case EACCES:
        case EPERM:   return knst_file_error::PermissionDenied;
        case EISDIR:  return knst_file_error::IsDirectory;
        case ELOOP:   return knst_file_error::SymlinkLoop;
        case ENOMEM:  return knst_file_error::OutOfMemory;
        case EIO:     return knst_file_error::ReadError;
        case ENOSPC:  return knst_file_error::DiskFull;
        case ENAMETOOLONG:
        case EINVAL:  return knst_file_error::InvalidPath;
        case EEXIST:  return knst_file_error::AlreadyExists;
        case ENOTEMPTY: return knst_file_error::NotEmpty;
        default:      return knst_file_error::Unknown;
    }
}
#endif


inline void FormatUnixTime(uint64_t unix_seconds,
                           knst_c16string& out,
                           bool local_time = true) noexcept {
    out.clear();
    if (unix_seconds == 0) return;

    std::time_t t = static_cast<std::time_t>(unix_seconds);
    std::tm tm_buf{};

#if KNST_USING_PLATFORM_WINDOWS
    if (local_time) { if (localtime_s(&tm_buf, &t) != 0) return; }
    else            { if (gmtime_s(&tm_buf, &t)   != 0) return; }
#else
    if (local_time) { if (localtime_r(&t, &tm_buf) == nullptr) return; }
    else            { if (gmtime_r(&t, &tm_buf)   == nullptr) return; }
#endif

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    out = knst_c16string(buf);
}

inline void FormatRelativeTime(uint64_t unix_seconds,knst_c16string& out) noexcept {
    out.clear();
    if (unix_seconds == 0) return;

    std::time_t now = std::time(nullptr);
    int64_t diff = static_cast<int64_t>(now) - static_cast<int64_t>(unix_seconds);

    const char16_t* suffix = u" ago";
    if (diff < 0) { diff = -diff; suffix = u" from now"; }

    if (diff < 5) { out = u"just now"; return; }

    knst_c16string num;
    const char16_t* unit = u"";

    if      (diff < 60)         { num.append(static_cast<long long>(diff));            unit = u"s"; }
    else if (diff < 3600)       { num.append(static_cast<long long>(diff / 60));       unit = u"m"; }
    else if (diff < 86400)      { num.append(static_cast<long long>(diff / 3600));     unit = u"h"; }
    else if (diff < 2592000)    { num.append(static_cast<long long>(diff / 86400));    unit = u"d"; }
    else if (diff < 31536000)   { num.append(static_cast<long long>(diff / 2592000));  unit = u"mo"; }
    else                        { num.append(static_cast<long long>(diff / 31536000)); unit = u"y"; }

    out.append(num);
    out.append(unit);
    out.append(suffix);
}

inline void ResolvePathSegment(const knst_c16string& source,
                                uint32_t seg_start, uint32_t seg_len,
                                bool is_abs,
                                knst_vector<knst_c16string>& parts) noexcept {
    if (seg_len == 0) return;

    const char16_t* raw = source.data();

    if (seg_len == 1 && raw[seg_start] == u'.') {
        return;
    }

    if (seg_len == 2 && raw[seg_start] == u'.' && raw[seg_start + 1] == u'.') {
        if (!parts.empty() && parts.back() != u"..") {
            parts.pop_back();
        } else if (!is_abs) {
            parts.push_back(knst_c16string(u".."));
        }
        return;
    }

    parts.push_back(source.substr(seg_start, seg_len));
}

} // namespace knst_file_detail


#define KNST_FILE_PK(lit)  (::knst_file_detail::PackLower(lit))
#define KNST_FILE_PKX(lit) (::knst_file_detail::PackExact(lit))



// And yes, the magic part lies in the file metadata; I tried my best to ensure cross-platform compatibility while doing this
struct knst_file {

    knst_c16string path; // full path
    knst_c16string name; // name file
    knst_c16string extension; // ex (.txt)
    knst_c16string full_extension; // ex (.tar.gz)
    knst_c16string parent_dir; // parent path classic

    uint64_t size = 0; 
    uint64_t created_time = 0;
    uint64_t modified_time = 0;  // unix epoc seconds
    uint64_t accessed_time = 0;

    bool exists= false;
    bool is_file = false;
    bool is_directory = false;
    bool is_symlink = false;
    bool is_hidden = false;
    bool is_readonly = false;
    bool is_executable = false;


    // hash
    uint64_t ext_cs = 0;
    uint64_t ext_ci = 0;
    uint64_t full_ext_cs = 0;
    uint64_t full_ext_ci = 0;
    uint32_t ext_len = 0;

    //error or info
    knst_file_error error = knst_file_error::None;



    knst_file_error read_file(const knst_c16string& p) noexcept;
    knst_file_error refresh() noexcept;
    void reset() noexcept;

    static knst_c16string get_directory(const knst_c16string& p) noexcept;
    static knst_c16string get_filename(const knst_c16string& p) noexcept;
    static knst_c16string get_extension(const knst_c16string& p) noexcept;
    static knst_c16string get_stem(const knst_c16string& p) noexcept;
    static knst_c16string get_parent_dir(const knst_c16string& p) noexcept;
   

    static knst_c16string normalize_path(const knst_c16string& p) noexcept;
    static knst_c16string resolve_path(const knst_c16string& p) noexcept;
    static bool is_absolute_path(const knst_c16string& p) noexcept;
    static knst_c16string join_path(const knst_c16string& a,const knst_c16string& b) noexcept;
                                     
    static knst_c16string get_current_dir() noexcept;
    static bool set_current_dir(const knst_c16string& p) noexcept;
    static knst_c16string get_executable_dir() noexcept;
    static knst_c16string get_home_dir() noexcept;
    static knst_c16string get_temp_dir() noexcept;


    static bool path_exists(const knst_c16string& p) noexcept;
    static bool file_exists(const knst_c16string& p) noexcept;
    static bool dir_exists(const knst_c16string& p) noexcept;

    static bool symlink_exists(const knst_c16string& p) noexcept;


    template<typename Alloc = knst_default_allocator>
    static basic_byte_string<Alloc> read_file_data(const knst_c16string& p,knst_file_error* out_error = nullptr) noexcept;
                                                    

        template<typename Alloc = knst_default_allocator>
    static knst_c16string read_file_text(const knst_c16string& p) noexcept;

    static bool write_file_data(const knst_c16string& p,const unsigned char* data,uint32_t size) noexcept;

    template<typename Alloc>
    static bool write_file_data(const knst_c16string& p,const basic_byte_string<Alloc>& data) noexcept {
                                 
        return write_file_data(p, data.data(), data.length());
    }

    static bool write_file_text(const knst_c16string& p,const knst_c16string& text) noexcept;
                                 

    static bool append_file_data(const knst_c16string& p,const unsigned char* data,uint32_t size,bool sync = false) noexcept;
                                  

    template<typename Alloc>
    static bool append_file_data(const knst_c16string& p,const basic_byte_string<Alloc>& data,bool sync = false) noexcept {
                                  
        return append_file_data(p, data.data(), data.length(), sync);
    }

    static bool append_file_text(const knst_c16string& p,const knst_c16string& text,bool sync = false) noexcept;

    static bool write_file_atomic(const knst_c16string& p,const unsigned char* data,uint32_t size) noexcept;

    template<typename Alloc>
    static bool write_file_atomic(const knst_c16string& p,const basic_byte_string<Alloc>& data) noexcept {
                                   
        return write_file_atomic(p, data.data(), data.length());
    }

    static bool write_file_atomic_text(const knst_c16string& p,const knst_c16string& text) noexcept;

    static bool touch_file(const knst_c16string& p, bool create_parents = true) noexcept;
    static bool ensure_parent_dir(const knst_c16string& p) noexcept;

    static knst_c16string read_symlink_target(const knst_c16string& p) noexcept;

    static bool create_symlink(const knst_c16string& target,const knst_c16string& link_path,bool is_directory = false) noexcept;

    static bool list_directory(const knst_c16string& dir,knst_vector<knst_file>& out) noexcept;

    template<typename Predicate,typename = std::enable_if_t<std::is_invocable_r_v<bool, Predicate, const knst_file&>>>
    static bool list_directory(const knst_c16string& dir,knst_vector<knst_file>& out,Predicate filter) noexcept;

    static bool list_directory_recursive(const knst_c16string& dir,knst_vector<knst_file>& out,int max_depth = -1,bool follow_symlinks = false) noexcept;

    template<typename Predicate,typename = std::enable_if_t<std::is_invocable_r_v<bool, Predicate, const knst_file&>>>
    static bool list_directory_recursive(const knst_c16string& dir,knst_vector<knst_file>& out,Predicate filter,int max_depth = -1,bool follow_symlinks = false) noexcept;

    static bool create_directory(const knst_c16string& p) noexcept;
    static bool create_directories(const knst_c16string& p) noexcept;
    static bool remove_file(const knst_c16string& p) noexcept;
    static bool remove_directory(const knst_c16string& p) noexcept;
    static bool remove_all(const knst_c16string& p) noexcept;
    static bool rename(const knst_c16string& from,const knst_c16string& to) noexcept;

    static bool copy_file(const knst_c16string& from,const knst_c16string& to,bool overwrite = false) noexcept;
                          
    static bool copy_directory(const knst_c16string& from,const knst_c16string& to,bool overwrite = false,bool follow_symlinks = false) noexcept;

    static bool move_path(const knst_c16string& from,const knst_c16string& to,bool overwrite = false) noexcept;

    static bool set_file_times(const knst_c16string& p,uint64_t modified_time,uint64_t accessed_time = 0) noexcept;
                               
                               

    static bool get_disk_space(const knst_c16string& p,uint64_t& out_free_bytes,uint64_t& out_total_bytes) noexcept;
    static uint64_t get_file_size(const knst_c16string& p) noexcept;
    static bool is_valid_filename(const knst_c16string& name) noexcept;
    static knst_c16string sanitize_filename(const knst_c16string& name,char16_t replacement = u'_') noexcept;
                                            

    static knst_file open(const knst_c16string& p) noexcept {
        knst_file f;
        f.read_file(p);
        return f;
    }

    static knst_file open_or_create(const knst_c16string& p,bool create_parents = true) noexcept {
        knst_file f;
        f.read_file(p);
        if (!f.exists) {
            f.touch(create_parents);
            f.refresh();
        }
        return f;
    }

    static knst_file create_file(const knst_c16string& p,bool create_parents = true) noexcept {
        return open_or_create(p, create_parents);
    }

    static knst_file create_text(const knst_c16string& p,const knst_c16string& text) noexcept {
        knst_file f;
        f.read_file(p);
        if (!knst_file::ensure_parent_dir(p) ||
            !knst_file::write_file_text(p, text)) {
            f.error = knst_file_error::WriteError;
            return f;
        }
        f.refresh();
        return f;
    }

    static knst_file create_lines(const knst_c16string& p,const knst_vector<knst_c16string>& lines) noexcept {
        knst_file f;
        f.read_file(p);
        if (!knst_file::ensure_parent_dir(p) ||
            !f.write_lines(lines)) {
            f.error = knst_file_error::WriteError;
            return f;
        }
        f.refresh();
        return f;
    }

    static knst_file create_dir(const knst_c16string& p) noexcept {
        knst_file f;
        f.read_file(p);
        knst_file::create_directory(p);
        f.refresh();
        return f;
    }

    static knst_file create_dirs(const knst_c16string& p) noexcept {
        knst_file f;
        f.read_file(p);
        knst_file::create_directories(p);
        f.refresh();
        return f;
    }


    template<typename Alloc = knst_default_allocator>
    basic_byte_string<Alloc>
    read_file_data(knst_file_error* out_error = nullptr) const noexcept {
        return knst_file::read_file_data<Alloc>(path, out_error);
    }

        template<typename Alloc = knst_default_allocator>
    KNST_FORCE_INLINE knst_c16string read_file_text() const noexcept {
        return knst_file::read_file_text<Alloc>(path);
    }

        template<typename Alloc = knst_default_allocator>
    bool read_file_lines(knst_vector<knst_c16string>& out,
                         knst_file_error* out_error = nullptr) const noexcept;

    KNST_FORCE_INLINE bool write_data(const unsigned char* data, uint32_t sz) const noexcept {
        return knst_file::write_file_data(path, data, sz);
    }

    template<typename Alloc>
    KNST_FORCE_INLINE bool write_data(const basic_byte_string<Alloc>& data) const noexcept {
        return knst_file::write_file_data(path, data);
    }

    KNST_FORCE_INLINE bool write_text(const knst_c16string& text) const noexcept {
        return knst_file::write_file_text(path, text);
    }

    bool write_lines(const knst_vector<knst_c16string>& lines) const noexcept;

    KNST_FORCE_INLINE bool append_data(const unsigned char* data, uint32_t sz,bool sync = false) const noexcept {
        return knst_file::append_file_data(path, data, sz, sync);
    }

    template<typename Alloc>
    KNST_FORCE_INLINE bool append_data(const basic_byte_string<Alloc>& data,bool sync = false) const noexcept {
        return knst_file::append_file_data(path, data, sync);
    }

    KNST_FORCE_INLINE bool append_text(const knst_c16string& text,bool sync = false) const noexcept {
        return knst_file::append_file_text(path, text, sync);
    }

    bool append_line(const knst_c16string& line, bool sync = false) const noexcept;

    KNST_FORCE_INLINE bool write_atomic(const unsigned char* data, uint32_t sz) const noexcept {
        return knst_file::write_file_atomic(path, data, sz);
    }

    template<typename Alloc>
    KNST_FORCE_INLINE bool write_atomic(const basic_byte_string<Alloc>& data) const noexcept {
        return knst_file::write_file_atomic(path, data);
    }

    KNST_FORCE_INLINE bool write_atomic_text(const knst_c16string& text) const noexcept {
        return knst_file::write_file_atomic_text(path, text);
    }

    KNST_FORCE_INLINE bool touch(bool create_parents = true) const noexcept {
        return knst_file::touch_file(path, create_parents);
    }

    

    KNST_FORCE_INLINE bool save(const knst_c16string& text) const noexcept {
        return write_text(text);
    }

    KNST_FORCE_INLINE bool save_atomic(const knst_c16string& text) const noexcept {
        return write_atomic_text(text);
    }

    KNST_FORCE_INLINE bool save_lines(const knst_vector<knst_c16string>& lines) const noexcept {
        return write_lines(lines);
    }

    KNST_FORCE_INLINE knst_c16string load() const noexcept {
        return read_file_text();
    }

    KNST_FORCE_INLINE bool load_lines(knst_vector<knst_c16string>& out) const noexcept {
        return read_file_lines(out);
    }

    KNST_FORCE_INLINE bool ensure_exists(bool create_parents = true) const noexcept {
        return touch(create_parents);
    }

   

    KNST_FORCE_INLINE bool path_exists() const noexcept { return exists; }

    KNST_FORCE_INLINE bool create_directory() const noexcept {
        return knst_file::create_directory(path);
    }

    KNST_FORCE_INLINE bool create_directories() const noexcept {
        return knst_file::create_directories(path);
    }

    KNST_FORCE_INLINE bool ensure_parent_dir() const noexcept {
        return knst_file::ensure_parent_dir(path);
    }

    KNST_FORCE_INLINE bool list_directory(knst_vector<knst_file>& out) const noexcept {
        return knst_file::list_directory(path, out);
    }

    template<typename Predicate,typename = std::enable_if_t<std::is_invocable_r_v<bool, Predicate, const knst_file&>>>
             
                 
    KNST_FORCE_INLINE bool list_directory(knst_vector<knst_file>& out,Predicate filter) const noexcept {
                                          
        return knst_file::list_directory(path, out, filter);
    }

    KNST_FORCE_INLINE bool list_directory_recursive(
        knst_vector<knst_file>& out,
        int max_depth = -1,
        bool follow_symlinks = false) const noexcept {
        return knst_file::list_directory_recursive(path, out, max_depth, follow_symlinks);
    }

    template<typename Predicate,typename = std::enable_if_t<std::is_invocable_r_v<bool, Predicate, const knst_file&>>>
    KNST_FORCE_INLINE bool list_directory_recursive(
        knst_vector<knst_file>& out,
        Predicate filter,
        int max_depth = -1,
        bool follow_symlinks = false) const noexcept {
        return knst_file::list_directory_recursive(path, out, filter, max_depth, follow_symlinks);
    }

    bool remove() noexcept;
    bool remove_all() noexcept;
    bool rename_to(const knst_c16string& new_path) noexcept;

    KNST_FORCE_INLINE bool copy_to(const knst_c16string& dest,bool overwrite = false) const noexcept {
                                   
        return knst_file::copy_file(path, dest, overwrite);
    }

    KNST_FORCE_INLINE bool copy_directory(const knst_c16string& dest,bool overwrite = false,bool follow_symlinks = false) const noexcept {
                                          
        return knst_file::copy_directory(path, dest, overwrite, follow_symlinks);
    }

  
    bool move_to(const knst_c16string& dest, bool overwrite = false) noexcept;

    KNST_FORCE_INLINE knst_c16string read_symlink_target() const noexcept {
        return knst_file::read_symlink_target(path);
    }

    KNST_FORCE_INLINE bool create_symlink_here(const knst_c16string& target,
                                               bool as_directory = false) const noexcept {
        return knst_file::create_symlink(target, path, as_directory);
    }

    KNST_FORCE_INLINE bool set_times(uint64_t modified, uint64_t accessed = 0) const noexcept {
        return knst_file::set_file_times(path, modified, accessed);
    }

    KNST_FORCE_INLINE bool disk_space(uint64_t& free_bytes, uint64_t& total_bytes) const noexcept {
        return knst_file::get_disk_space(path, free_bytes, total_bytes);
    }

    KNST_FORCE_INLINE bool valid_filename() const noexcept {
        return knst_file::is_valid_filename(name);
    }

    KNST_FORCE_INLINE knst_c16string absolute_path() const noexcept {
        return knst_file::resolve_path(path);
    }

    KNST_FORCE_INLINE bool is_absolute() const noexcept {
        return knst_file::is_absolute_path(path);
    }

    KNST_FORCE_INLINE knst_c16string stem() const noexcept {
        return knst_file::get_stem(name);
    }

    KNST_FORCE_INLINE knst_c16string resolved_parent() const noexcept {
        return knst_file::get_parent_dir(path);
    }

    bool has_extension(const char16_t* ext, bool case_sensitive = false) const noexcept;

    KNST_FORCE_INLINE bool has_extension(const knst_c16string& ext,bool case_sensitive = false) const noexcept {
                                          
        return has_extension(ext.data(), case_sensitive);
    }

    bool has_full_extension(const char16_t* ext, bool case_sensitive = false) const noexcept;

    KNST_FORCE_INLINE bool has_full_extension(const knst_c16string& ext,bool case_sensitive = false) const noexcept {
                                                
        return has_full_extension(ext.data(), case_sensitive);
    }

    bool has_any_extension(std::initializer_list<const char16_t*> exts,bool case_sensitive = false) const noexcept;

    bool is_image() const noexcept;
    bool is_video() const noexcept;
    bool is_audio() const noexcept;
    bool is_text() const noexcept;
    bool is_document() const noexcept;
    bool is_code() const noexcept;
    bool is_script() const noexcept;
    bool is_shader() const noexcept;
    bool is_archive() const noexcept;
    bool is_executable_extension() const noexcept;
    bool is_font() const noexcept;
    bool is_model() const noexcept;
    bool is_web() const noexcept;
    bool is_config() const noexcept;
    bool is_database() const noexcept;
    bool is_ebook() const noexcept;
    bool is_subtitle() const noexcept;
    bool is_data() const noexcept;
    bool is_build() const noexcept;
    bool is_scientific() const noexcept;
    bool is_disk_image() const noexcept;
    bool is_ai_model() const noexcept;
    bool is_game_asset() const noexcept;
    bool is_cad() const noexcept;


    knst_c16string get_size_string() const noexcept;

    knst_c16string get_created_time_string(bool local = true)  const noexcept;
    knst_c16string get_modified_time_string(bool local = true) const noexcept;
    knst_c16string get_accessed_time_string(bool local = true) const noexcept;

    knst_c16string get_created_time_relative()  const noexcept;
    knst_c16string get_modified_time_relative() const noexcept;
    knst_c16string get_accessed_time_relative() const noexcept;

    KNST_FORCE_INLINE const char* error_string() const noexcept {
        return knst_file_error_string(error);
    }
};



using knst_file_system = knst_file;



inline bool knst_file::has_extension(const char16_t* ext, bool case_sensitive) const noexcept {
    if (ext == nullptr || ext_len == 0) return false;

    uint32_t arg_len = knst_get_str_length(ext);
    if (arg_len == 0) return false;

    if (ext_len <= 8 && arg_len <= 8 && ext_ci != knst_file_detail::KNST_EXT_NONASCII_SENTINEL) {
        uint64_t arg_cs = 0, arg_ci = 0;
        knst_file_detail::PackChecked(ext, arg_len, arg_cs, arg_ci);
        if (arg_ci == knst_file_detail::KNST_EXT_NONASCII_SENTINEL) return false;
        return case_sensitive ? (ext_cs == arg_cs) : (ext_ci == arg_ci);
    }

    if (arg_len != ext_len) return false;
    if (case_sensitive) {
        for (uint32_t i = 0; i < ext_len; ++i) {
            if (extension[i] != ext[i]) return false;
        }
    } else {
        for (uint32_t i = 0; i < ext_len; ++i) {
            char16_t a = extension[i], b = ext[i];
            if (a >= u'A' && a <= u'Z') a = static_cast<char16_t>(a + 32);
            if (b >= u'A' && b <= u'Z') b = static_cast<char16_t>(b + 32);
            if (a != b) return false;
        }
    }
    return true;
}

inline bool knst_file::has_full_extension(const char16_t* ext, bool case_sensitive) const noexcept {
    if (ext == nullptr || full_extension.empty()) return false;

    uint32_t arg_len = knst_get_str_length(ext);
    if (arg_len == 0) return false;

    uint32_t full_len = full_extension.length();

    if (full_len <= 8 && arg_len <= 8 && full_ext_ci != knst_file_detail::KNST_EXT_NONASCII_SENTINEL) {
        uint64_t arg_cs = 0, arg_ci = 0;
        knst_file_detail::PackChecked(ext, arg_len, arg_cs, arg_ci);
        if (arg_ci == knst_file_detail::KNST_EXT_NONASCII_SENTINEL) return false;
        return case_sensitive ? (full_ext_cs == arg_cs) : (full_ext_ci == arg_ci);
    }

    if (arg_len != full_len) return false;
    if (case_sensitive) {
        for (uint32_t i = 0; i < full_len; ++i) {
            if (full_extension[i] != ext[i]) return false;
        }
    } else {
        for (uint32_t i = 0; i < full_len; ++i) {
            char16_t a = full_extension[i], b = ext[i];
            if (a >= u'A' && a <= u'Z') a = static_cast<char16_t>(a + 32);
            if (b >= u'A' && b <= u'Z') b = static_cast<char16_t>(b + 32);
            if (a != b) return false;
        }
    }
    return true;
}

inline bool knst_file::has_any_extension(std::initializer_list<const char16_t*> exts,
                                           bool case_sensitive) const noexcept {
    if (ext_len == 0) return false;
    for (const char16_t* ext : exts) {
        if (has_extension(ext, case_sensitive)) return true;
    }
    return false;
}



KNST_FORCE_INLINE bool knst_file::is_image() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".png"):   case KNST_FILE_PK(".jpg"):
        case KNST_FILE_PK(".jpeg"):  case KNST_FILE_PK(".bmp"):
        case KNST_FILE_PK(".tga"):   case KNST_FILE_PK(".gif"):
        case KNST_FILE_PK(".tiff"):  case KNST_FILE_PK(".tif"):
        case KNST_FILE_PK(".webp"):  case KNST_FILE_PK(".heic"):
        case KNST_FILE_PK(".heif"):  case KNST_FILE_PK(".avif"):
        case KNST_FILE_PK(".jxl"):   case KNST_FILE_PK(".ico"):
        case KNST_FILE_PK(".cur"):   case KNST_FILE_PK(".ppm"):
        case KNST_FILE_PK(".pgm"):   case KNST_FILE_PK(".pbm"):
        case KNST_FILE_PK(".pnm"):   case KNST_FILE_PK(".exr"):
        case KNST_FILE_PK(".hdr"):   case KNST_FILE_PK(".dds"):
        case KNST_FILE_PK(".ktx"):   case KNST_FILE_PK(".ktx2"):
        case KNST_FILE_PK(".astc"):  case KNST_FILE_PK(".pvr"):
        case KNST_FILE_PK(".basis"): case KNST_FILE_PK(".qoi"):
        case KNST_FILE_PK(".pcx"):   case KNST_FILE_PK(".svg"):
        case KNST_FILE_PK(".svgz"):  case KNST_FILE_PK(".eps"):
        case KNST_FILE_PK(".iff"):   case KNST_FILE_PK(".ilbm"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_video() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".mp4"):  case KNST_FILE_PK(".avi"):
        case KNST_FILE_PK(".mkv"):  case KNST_FILE_PK(".mov"):
        case KNST_FILE_PK(".wmv"):  case KNST_FILE_PK(".flv"):
        case KNST_FILE_PK(".webm"): case KNST_FILE_PK(".m4v"):
        case KNST_FILE_PK(".mpg"):  case KNST_FILE_PK(".mpeg"):
        case KNST_FILE_PK(".3gp"):  case KNST_FILE_PK(".3g2"):
        case KNST_FILE_PK(".ogv"):  case KNST_FILE_PK(".ts"):
        case KNST_FILE_PK(".m2ts"): case KNST_FILE_PK(".mts"):
        case KNST_FILE_PK(".vob"):  case KNST_FILE_PK(".rm"):
        case KNST_FILE_PK(".rmvb"): case KNST_FILE_PK(".asf"):
        case KNST_FILE_PK(".divx"): case KNST_FILE_PK(".xvid"):
        case KNST_FILE_PK(".f4v"):  case KNST_FILE_PK(".mxf"):
        case KNST_FILE_PK(".m2v"):  case KNST_FILE_PK(".yuv"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_audio() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".mp3"):  case KNST_FILE_PK(".wav"):
        case KNST_FILE_PK(".ogg"):  case KNST_FILE_PK(".flac"):
        case KNST_FILE_PK(".aac"):  case KNST_FILE_PK(".m4a"):
        case KNST_FILE_PK(".wma"):  case KNST_FILE_PK(".opus"):
        case KNST_FILE_PK(".aiff"): case KNST_FILE_PK(".aif"):
        case KNST_FILE_PK(".alac"): case KNST_FILE_PK(".ape"):
        case KNST_FILE_PK(".mid"):  case KNST_FILE_PK(".midi"):
        case KNST_FILE_PK(".amr"):  case KNST_FILE_PK(".ac3"):
        case KNST_FILE_PK(".dts"):  case KNST_FILE_PK(".mka"):
        case KNST_FILE_PK(".oga"):  case KNST_FILE_PK(".au"):
        case KNST_FILE_PK(".snd"):  case KNST_FILE_PK(".m4b"):
        case KNST_FILE_PK(".m4p"):  case KNST_FILE_PK(".m4r"):
        case KNST_FILE_PK(".weba"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_text() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".txt"):  case KNST_FILE_PK(".text"):
        case KNST_FILE_PK(".md"):   case KNST_FILE_PK(".log"):
        case KNST_FILE_PK(".nfo"):  case KNST_FILE_PK(".csv"):
        case KNST_FILE_PK(".tsv"):  case KNST_FILE_PK(".rtf"):
        case KNST_FILE_PK(".tex"):  case KNST_FILE_PK(".latex"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_document() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".doc"):   case KNST_FILE_PK(".docx"):
        case KNST_FILE_PK(".odt"):   case KNST_FILE_PK(".pdf"):
        case KNST_FILE_PK(".pages"): case KNST_FILE_PK(".xls"):
        case KNST_FILE_PK(".xlsx"):  case KNST_FILE_PK(".ods"):
        case KNST_FILE_PK(".ppt"):   case KNST_FILE_PK(".pptx"):
        case KNST_FILE_PK(".odp"):   case KNST_FILE_PK(".key"):
        case KNST_FILE_PK(".xps"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_code() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".c"):     case KNST_FILE_PK(".h"):
        case KNST_FILE_PK(".cpp"):   case KNST_FILE_PK(".hpp"):
        case KNST_FILE_PK(".cc"):    case KNST_FILE_PK(".cxx"):
        case KNST_FILE_PK(".hxx"):   case KNST_FILE_PK(".c++"):
        case KNST_FILE_PK(".h++"):   case KNST_FILE_PK(".inl"):
        case KNST_FILE_PK(".ipp"):   case KNST_FILE_PK(".tpp"):
        case KNST_FILE_PK(".tcc"):   case KNST_FILE_PK(".m"):
        case KNST_FILE_PK(".mm"):    case KNST_FILE_PK(".cu"):
        case KNST_FILE_PK(".cuh"):
        case KNST_FILE_PK(".cs"):    case KNST_FILE_PK(".java"):
        case KNST_FILE_PK(".kt"):    case KNST_FILE_PK(".kts"):
        case KNST_FILE_PK(".scala"): case KNST_FILE_PK(".sc"):
        case KNST_FILE_PK(".py"):    case KNST_FILE_PK(".pyw"):
        case KNST_FILE_PK(".pyx"):   case KNST_FILE_PK(".pyi"):
        case KNST_FILE_PK(".rb"):    case KNST_FILE_PK(".erb"):
        case KNST_FILE_PK(".php"):   case KNST_FILE_PK(".phtml"):
        case KNST_FILE_PK(".pl"):    case KNST_FILE_PK(".pm"):
        case KNST_FILE_PK(".lua"):   case KNST_FILE_PK(".wlua"):
        case KNST_FILE_PK(".r"):
        case KNST_FILE_PK(".js"):    case KNST_FILE_PK(".mjs"):
        case KNST_FILE_PK(".cjs"):   case KNST_FILE_PK(".jsx"):
        case KNST_FILE_PK(".ts"):    case KNST_FILE_PK(".tsx"):
        case KNST_FILE_PK(".html"):  case KNST_FILE_PK(".htm"):
        case KNST_FILE_PK(".xhtml"): case KNST_FILE_PK(".shtml"):
        case KNST_FILE_PK(".css"):   case KNST_FILE_PK(".scss"):
        case KNST_FILE_PK(".sass"):  case KNST_FILE_PK(".less"):
        case KNST_FILE_PK(".styl"):  case KNST_FILE_PK(".vue"):
        case KNST_FILE_PK(".svelte"):case KNST_FILE_PK(".astro"):
        case KNST_FILE_PK(".rs"):    case KNST_FILE_PK(".go"):
        case KNST_FILE_PK(".swift"): case KNST_FILE_PK(".d"):
        case KNST_FILE_PK(".nim"):   case KNST_FILE_PK(".zig"):
        case KNST_FILE_PK(".v"):     case KNST_FILE_PK(".cr"):
        case KNST_FILE_PK(".hs"):    case KNST_FILE_PK(".lhs"):
        case KNST_FILE_PK(".erl"):   case KNST_FILE_PK(".hrl"):
        case KNST_FILE_PK(".ex"):    case KNST_FILE_PK(".exs"):
        case KNST_FILE_PK(".clj"):   case KNST_FILE_PK(".cljs"):
        case KNST_FILE_PK(".ml"):    case KNST_FILE_PK(".mli"):
        case KNST_FILE_PK(".fs"):    case KNST_FILE_PK(".fsi"):
        case KNST_FILE_PK(".fsx"):   case KNST_FILE_PK(".lisp"):
        case KNST_FILE_PK(".el"):    case KNST_FILE_PK(".scm"):
        case KNST_FILE_PK(".rkt"):
        case KNST_FILE_PK(".jl"):    case KNST_FILE_PK(".sol"):
        case KNST_FILE_PK(".dart"):  case KNST_FILE_PK(".elm"):
        case KNST_FILE_PK(".sql"):   case KNST_FILE_PK(".vim"):
        case KNST_FILE_PK(".groovy"):case KNST_FILE_PK(".gradle"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_script() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".sh"):    case KNST_FILE_PK(".bash"):
        case KNST_FILE_PK(".zsh"):   case KNST_FILE_PK(".fish"):
        case KNST_FILE_PK(".ksh"):   case KNST_FILE_PK(".csh"):
        case KNST_FILE_PK(".bat"):   case KNST_FILE_PK(".cmd"):
        case KNST_FILE_PK(".ps1"):   case KNST_FILE_PK(".psm1"):
        case KNST_FILE_PK(".psd1"):  case KNST_FILE_PK(".vbs"):
        case KNST_FILE_PK(".vbe"):   case KNST_FILE_PK(".wsf"):
        case KNST_FILE_PK(".au3"):   case KNST_FILE_PK(".ahk"):
        case KNST_FILE_PK(".nsi"):   case KNST_FILE_PK(".nsh"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_shader() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".glsl"):  case KNST_FILE_PK(".vert"):
        case KNST_FILE_PK(".frag"):  case KNST_FILE_PK(".geom"):
        case KNST_FILE_PK(".tesc"):  case KNST_FILE_PK(".tese"):
        case KNST_FILE_PK(".comp"):  case KNST_FILE_PK(".hlsl"):
        case KNST_FILE_PK(".fx"):    case KNST_FILE_PK(".fxh"):
        case KNST_FILE_PK(".cginc"): case KNST_FILE_PK(".hlsli"):
        case KNST_FILE_PK(".spv"):   case KNST_FILE_PK(".wgsl"):
        case KNST_FILE_PK(".metal"): case KNST_FILE_PK(".msl"):
        case KNST_FILE_PK(".shader"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_archive() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".zip"):   case KNST_FILE_PK(".rar"):
        case KNST_FILE_PK(".7z"):    case KNST_FILE_PK(".tar"):
        case KNST_FILE_PK(".gz"):    case KNST_FILE_PK(".tgz"):
        case KNST_FILE_PK(".bz2"):   case KNST_FILE_PK(".tbz2"):
        case KNST_FILE_PK(".xz"):    case KNST_FILE_PK(".txz"):
        case KNST_FILE_PK(".lz"):    case KNST_FILE_PK(".lzma"):
        case KNST_FILE_PK(".lzo"):   case KNST_FILE_PK(".zst"):
        case KNST_FILE_PK(".lz4"):   case KNST_FILE_PK(".z"):
        case KNST_FILE_PK(".cab"):   case KNST_FILE_PK(".arj"):
        case KNST_FILE_PK(".lzh"):   case KNST_FILE_PK(".ace"):
        case KNST_FILE_PK(".zoo"):   case KNST_FILE_PK(".pak"):
        case KNST_FILE_PK(".war"):   case KNST_FILE_PK(".ear"):
        case KNST_FILE_PK(".jar"):   case KNST_FILE_PK(".apk"):
        case KNST_FILE_PK(".ipa"):   case KNST_FILE_PK(".xap"):
        case KNST_FILE_PK(".crx"):   case KNST_FILE_PK(".xpi"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_executable_extension() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".exe"):   case KNST_FILE_PK(".dll"):
        case KNST_FILE_PK(".so"):    case KNST_FILE_PK(".dylib"):
        case KNST_FILE_PK(".a"):     case KNST_FILE_PK(".lib"):
        case KNST_FILE_PK(".o"):
        case KNST_FILE_PK(".bin"):   case KNST_FILE_PK(".elf"):
        case KNST_FILE_PK(".com"):   case KNST_FILE_PK(".scr"):
        case KNST_FILE_PK(".pif"):   case KNST_FILE_PK(".msi"):
        case KNST_FILE_PK(".msix"):  case KNST_FILE_PK(".appx"):
        case KNST_FILE_PK(".deb"):   case KNST_FILE_PK(".rpm"):
        case KNST_FILE_PK(".pkg"):   case KNST_FILE_PK(".app"):
        case KNST_FILE_PK(".run"):   case KNST_FILE_PK(".out"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_font() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".ttf"):   case KNST_FILE_PK(".otf"):
        case KNST_FILE_PK(".ttc"):   case KNST_FILE_PK(".otc"):
        case KNST_FILE_PK(".woff"):  case KNST_FILE_PK(".woff2"):
        case KNST_FILE_PK(".eot"):   case KNST_FILE_PK(".fon"):
        case KNST_FILE_PK(".fnt"):   case KNST_FILE_PK(".pfb"):
        case KNST_FILE_PK(".pfm"):   case KNST_FILE_PK(".afm"):
        case KNST_FILE_PK(".bdf"):   case KNST_FILE_PK(".pcf"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_model() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".obj"):   case KNST_FILE_PK(".mtl"):
        case KNST_FILE_PK(".fbx"):   case KNST_FILE_PK(".gltf"):
        case KNST_FILE_PK(".glb"):   case KNST_FILE_PK(".dae"):
        case KNST_FILE_PK(".3ds"):   case KNST_FILE_PK(".max"):
        case KNST_FILE_PK(".blend"): case KNST_FILE_PK(".c4d"):
        case KNST_FILE_PK(".ma"):    case KNST_FILE_PK(".mb"):
        case KNST_FILE_PK(".lxo"):   case KNST_FILE_PK(".stl"):
        case KNST_FILE_PK(".ply"):   case KNST_FILE_PK(".step"):
        case KNST_FILE_PK(".stp"):   case KNST_FILE_PK(".iges"):
        case KNST_FILE_PK(".igs"):   case KNST_FILE_PK(".dwg"):
        case KNST_FILE_PK(".dxf"):   case KNST_FILE_PK(".3mf"):
        case KNST_FILE_PK(".amf"):   case KNST_FILE_PK(".vrm"):
        case KNST_FILE_PK(".pcd"):   case KNST_FILE_PK(".las"):
        case KNST_FILE_PK(".laz"):   case KNST_FILE_PK(".e57"):
        case KNST_FILE_PK(".pts"):   case KNST_FILE_PK(".xyz"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_web() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".html"):  case KNST_FILE_PK(".htm"):
        case KNST_FILE_PK(".xhtml"): case KNST_FILE_PK(".shtml"):
        case KNST_FILE_PK(".dhtml"): case KNST_FILE_PK(".hta"):
        case KNST_FILE_PK(".xml"):   case KNST_FILE_PK(".rss"):
        case KNST_FILE_PK(".atom"):  case KNST_FILE_PK(".kml"):
        case KNST_FILE_PK(".gml"):   case KNST_FILE_PK(".css"):
        case KNST_FILE_PK(".scss"):  case KNST_FILE_PK(".sass"):
        case KNST_FILE_PK(".less"):  case KNST_FILE_PK(".styl"):
        case KNST_FILE_PK(".pcss"):  case KNST_FILE_PK(".js"):
        case KNST_FILE_PK(".mjs"):   case KNST_FILE_PK(".cjs"):
        case KNST_FILE_PK(".jsx"):   case KNST_FILE_PK(".ts"):
        case KNST_FILE_PK(".tsx"):   case KNST_FILE_PK(".vue"):
        case KNST_FILE_PK(".svelte"):case KNST_FILE_PK(".astro"):
        case KNST_FILE_PK(".pug"):   case KNST_FILE_PK(".jade"):
        case KNST_FILE_PK(".ejs"):   case KNST_FILE_PK(".hbs"):
        case KNST_FILE_PK(".haml"):  case KNST_FILE_PK(".twig"):
        case KNST_FILE_PK(".njk"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_config() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".json"):  case KNST_FILE_PK(".json5"):
        case KNST_FILE_PK(".jsonc"): case KNST_FILE_PK(".yaml"):
        case KNST_FILE_PK(".yml"):   case KNST_FILE_PK(".toml"):
        case KNST_FILE_PK(".xml"):   case KNST_FILE_PK(".plist"):
        case KNST_FILE_PK(".ini"):   case KNST_FILE_PK(".cfg"):
        case KNST_FILE_PK(".conf"):  case KNST_FILE_PK(".config"):
        case KNST_FILE_PK(".env"):   case KNST_FILE_PK(".lock"):
        case KNST_FILE_PK(".prop"):  case KNST_FILE_PK(".properties"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_database() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".db"):    case KNST_FILE_PK(".sqlite"):
        case KNST_FILE_PK(".db3"):   case KNST_FILE_PK(".mdb"):
        case KNST_FILE_PK(".accdb"): case KNST_FILE_PK(".dbf"):
        case KNST_FILE_PK(".fdb"):   case KNST_FILE_PK(".gdb"):
        case KNST_FILE_PK(".ibd"):   case KNST_FILE_PK(".myd"):
        case KNST_FILE_PK(".myi"):   case KNST_FILE_PK(".bson"):
        case KNST_FILE_PK(".sql"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_ebook() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".epub"):  case KNST_FILE_PK(".mobi"):
        case KNST_FILE_PK(".azw"):   case KNST_FILE_PK(".azw3"):
        case KNST_FILE_PK(".azw4"):  case KNST_FILE_PK(".fb2"):
        case KNST_FILE_PK(".lit"):   case KNST_FILE_PK(".lrf"):
        case KNST_FILE_PK(".prc"):   case KNST_FILE_PK(".cbr"):
        case KNST_FILE_PK(".cbz"):   case KNST_FILE_PK(".cb7"):
        case KNST_FILE_PK(".cbt"):   case KNST_FILE_PK(".cba"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_subtitle() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".srt"):   case KNST_FILE_PK(".vtt"):
        case KNST_FILE_PK(".ass"):   case KNST_FILE_PK(".ssa"):
        case KNST_FILE_PK(".sub"):   case KNST_FILE_PK(".idx"):
        case KNST_FILE_PK(".sbv"):   case KNST_FILE_PK(".smi"):
        case KNST_FILE_PK(".ttml"):  case KNST_FILE_PK(".usf"):
        case KNST_FILE_PK(".stl"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_data() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".bin"):   case KNST_FILE_PK(".dat"):
        case KNST_FILE_PK(".data"):  case KNST_FILE_PK(".dump"):
        case KNST_FILE_PK(".raw"):   case KNST_FILE_PK(".parquet"):
        case KNST_FILE_PK(".avro"):  case KNST_FILE_PK(".orc"):
        case KNST_FILE_PK(".arrow"): case KNST_FILE_PK(".feather"):
        case KNST_FILE_PK(".proto"): case KNST_FILE_PK(".npy"):
        case KNST_FILE_PK(".npz"):   case KNST_FILE_PK(".pkl"):
        case KNST_FILE_PK(".pickle"):case KNST_FILE_PK(".joblib"):
        case KNST_FILE_PK(".geojson"):case KNST_FILE_PK(".topojson"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_build() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".mk"):    case KNST_FILE_PK(".make"):
        case KNST_FILE_PK(".cmake"): case KNST_FILE_PK(".ninja"):
        case KNST_FILE_PK(".bazel"): case KNST_FILE_PK(".gradle"):
        case KNST_FILE_PK(".sbt"):   case KNST_FILE_PK(".pom"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_scientific() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".hdf"):   case KNST_FILE_PK(".hdf5"):
        case KNST_FILE_PK(".h5"):    case KNST_FILE_PK(".nc"):
        case KNST_FILE_PK(".netcdf"):case KNST_FILE_PK(".cdf"):
        case KNST_FILE_PK(".fits"):  case KNST_FILE_PK(".fit"):
        case KNST_FILE_PK(".mat"):   case KNST_FILE_PK(".dcm"):
        case KNST_FILE_PK(".dicom"): case KNST_FILE_PK(".nii"):
        case KNST_FILE_PK(".mha"):   case KNST_FILE_PK(".mhd"):
        case KNST_FILE_PK(".nrrd"):  case KNST_FILE_PK(".fasta"):
        case KNST_FILE_PK(".fa"):    case KNST_FILE_PK(".fastq"):
        case KNST_FILE_PK(".fq"):    case KNST_FILE_PK(".sam"):
        case KNST_FILE_PK(".bam"):   case KNST_FILE_PK(".vcf"):
        case KNST_FILE_PK(".gff"):   case KNST_FILE_PK(".gtf"):
        case KNST_FILE_PK(".bed"):   case KNST_FILE_PK(".pdb"):
        case KNST_FILE_PK(".cif"):   case KNST_FILE_PK(".mol"):
        case KNST_FILE_PK(".mol2"):  case KNST_FILE_PK(".sdf"):
        case KNST_FILE_PK(".smi"):   case KNST_FILE_PK(".smiles"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_disk_image() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".iso"):   case KNST_FILE_PK(".img"):
        case KNST_FILE_PK(".vhd"):   case KNST_FILE_PK(".vhdx"):
        case KNST_FILE_PK(".qcow"):  case KNST_FILE_PK(".qcow2"):
        case KNST_FILE_PK(".vmdk"):  case KNST_FILE_PK(".vdi"):
        case KNST_FILE_PK(".vbox"):  case KNST_FILE_PK(".dmg"):
        case KNST_FILE_PK(".nrg"):   case KNST_FILE_PK(".mdf"):
        case KNST_FILE_PK(".mds"):   case KNST_FILE_PK(".ccd"):
        case KNST_FILE_PK(".toast"): case KNST_FILE_PK(".cdr"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_ai_model() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".pt"):    case KNST_FILE_PK(".pth"):
        case KNST_FILE_PK(".ckpt"):  case KNST_FILE_PK(".pb"):
        case KNST_FILE_PK(".h5"):    case KNST_FILE_PK(".tflite"):
        case KNST_FILE_PK(".onnx"):  case KNST_FILE_PK(".keras"):
        case KNST_FILE_PK(".ggml"):  case KNST_FILE_PK(".gguf"):
        case KNST_FILE_PK(".mlmodel"):
            return true;
        default: break;
    }
    if (extension == u".caffemodel" ||
        extension == u".pdparams"   ||
        extension == u".safetensors") {
        return true;
    }
    return false;
}

KNST_FORCE_INLINE bool knst_file::is_game_asset() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".unity"): case KNST_FILE_PK(".prefab"):
        case KNST_FILE_PK(".asset"): case KNST_FILE_PK(".uasset"):
        case KNST_FILE_PK(".umap"):  case KNST_FILE_PK(".upk"):
        case KNST_FILE_PK(".pak"):   case KNST_FILE_PK(".utoc"):
        case KNST_FILE_PK(".ucas"):  case KNST_FILE_PK(".tscn"):
        case KNST_FILE_PK(".tres"):  case KNST_FILE_PK(".escn"):
        case KNST_FILE_PK(".gd"):    case KNST_FILE_PK(".p3d"):
        case KNST_FILE_PK(".wad"):   case KNST_FILE_PK(".bsp"):
        case KNST_FILE_PK(".mdl"):   case KNST_FILE_PK(".vvd"):
        case KNST_FILE_PK(".vtx"):   case KNST_FILE_PK(".anim"):
            return true;
        default: return false;
    }
}

KNST_FORCE_INLINE bool knst_file::is_cad() const noexcept {
    switch (ext_ci) {
        case KNST_FILE_PK(".dwg"):   case KNST_FILE_PK(".dxf"):
        case KNST_FILE_PK(".dwf"):   case KNST_FILE_PK(".dwfx"):
        case KNST_FILE_PK(".step"):  case KNST_FILE_PK(".stp"):
        case KNST_FILE_PK(".iges"):  case KNST_FILE_PK(".igs"):
        case KNST_FILE_PK(".prt"):   case KNST_FILE_PK(".asm"):
        case KNST_FILE_PK(".sldprt"):case KNST_FILE_PK(".sldasm"):
            return true;
        default: return false;
    }
}


KNST_FORCE_INLINE knst_c16string knst_file::get_size_string() const noexcept {
    knst_c16string result;

    if (size < 1024ULL) {
        result.append(static_cast<long long>(size));
        result.append(u" B");
    } else if (size < 1024ULL * 1024) {
        result.append(static_cast<long long>(size / 1024ULL));
        result.append(u" KB");
    } else if (size < 1024ULL * 1024 * 1024) {
        result.append(static_cast<long long>(size / (1024ULL * 1024)));
        result.append(u" MB");
    } else if (size < 1024ULL * 1024 * 1024 * 1024) {
        result.append(static_cast<long long>(size / (1024ULL * 1024 * 1024)));
        result.append(u" GB");
    } else {
        result.append(static_cast<long long>(size / (1024ULL * 1024 * 1024 * 1024)));
        result.append(u" TB");
    }

    return result;
}



KNST_FORCE_INLINE knst_c16string
knst_file::get_created_time_string(bool local) const noexcept {
    knst_c16string out;
    knst_file_detail::FormatUnixTime(created_time, out, local);
    return out;
}

KNST_FORCE_INLINE knst_c16string
knst_file::get_modified_time_string(bool local) const noexcept {
    knst_c16string out;
    knst_file_detail::FormatUnixTime(modified_time, out, local);
    return out;
}

KNST_FORCE_INLINE knst_c16string
knst_file::get_accessed_time_string(bool local) const noexcept {
    knst_c16string out;
    knst_file_detail::FormatUnixTime(accessed_time, out, local);
    return out;
}

KNST_FORCE_INLINE knst_c16string
knst_file::get_created_time_relative() const noexcept {
    knst_c16string out;
    knst_file_detail::FormatRelativeTime(created_time, out);
    return out;
}

KNST_FORCE_INLINE knst_c16string
knst_file::get_modified_time_relative() const noexcept {
    knst_c16string out;
    knst_file_detail::FormatRelativeTime(modified_time, out);
    return out;
}

KNST_FORCE_INLINE knst_c16string
knst_file::get_accessed_time_relative() const noexcept {
    knst_c16string out;
    knst_file_detail::FormatRelativeTime(accessed_time, out);
    return out;
}


inline knst_c16string knst_file::get_directory(const knst_c16string& p) noexcept {
    if (p.empty()) return knst_c16string();

    knst_c16string normalized = normalize_path(p);
    uint32_t last_sep = knst_file_detail::FindLastSep(normalized);


    if (last_sep == UINT32_MAX) return knst_c16string();

#if KNST_USING_PLATFORM_WINDOWS

    if (normalized.length() >= 2 && normalized[1] == u':' && last_sep == 2) {
        return normalized.substr(0, 3);
    }
#endif


    return normalized.substr(0, last_sep + 1);
}

inline knst_c16string knst_file::get_filename(const knst_c16string& p) noexcept {
    uint32_t last_sep = knst_file_detail::FindLastSep(p);
    if (last_sep == UINT32_MAX) return p;
    return p.substr(last_sep + 1, p.length() - last_sep - 1);
}

inline knst_c16string knst_file::get_extension(const knst_c16string& p) noexcept {
    knst_c16string name = get_filename(p);
    uint32_t name_len = name.length();
    if (name_len == 0) return knst_c16string();

    for (uint32_t i = name_len; i > 0; --i) {
        if (name[i - 1] == u'.') {
            if (i - 1 == 0) return knst_c16string();
            return name.substr(i - 1, name_len - (i - 1));
        }
    }
    return knst_c16string();
}

inline knst_c16string knst_file::get_stem(const knst_c16string& p) noexcept {
    knst_c16string name = get_filename(p);
    uint32_t name_len = name.length();
    if (name_len == 0) return knst_c16string();

    for (uint32_t i = name_len; i > 0; --i) {
        if (name[i - 1] == u'.') {
            if (i - 1 == 0) return name;
            return name.substr(0, i - 1);
        }
    }
    return name;
}

inline knst_c16string knst_file::normalize_path(const knst_c16string& p) noexcept {
    if (p.empty()) return u".";

    #if KNST_USING_PLATFORM_WINDOWS
        constexpr bool is_win = true;
    #else
        constexpr bool is_win = false;
    #endif

    const char16_t SEP = KNST_FILE_SEPARATOR;

    knst_c16string prefix;
    uint32_t start = 0;
    bool is_abs = false;

    if (is_win) {
        if (p.length() >= 4 &&
            p[0] == u'\\' && p[1] == u'\\' &&
            (p[2] == u'?' || p[2] == u'.') && p[3] == u'\\') {
            return p;
        }

        if (p.length() >= 2 && p[1] == u':') {
            prefix.append(p.substr(0, 2));
            start = 2;
            if (start < p.length() && knst_file_detail::IsSep(p[start])) {
                char16_t sep_arr[2] = { SEP, u'\0' };
                prefix.append(sep_arr);
                start++;
                is_abs = true;
            }
        } else if (p[0] == u'\\' && p.length() >= 2 && p[1] == u'\\') {
            prefix.append(u"\\\\");
            start = 2;
            is_abs = true;
        } else if (p[0] == u'/' || p[0] == u'\\') {
            char16_t sep_arr[2] = { SEP, u'\0' };
            prefix.append(sep_arr);
            start = 1;
            is_abs = true;
        }
    } else {
        if (p[0] == u'/') {
            char16_t sep_arr[2] = { SEP, u'\0' };
            prefix.append(sep_arr);
            start = 1;
            is_abs = true;
        }
    }

    knst_vector<knst_c16string> parts;

    uint32_t seg_start = start;
    uint32_t total_len = p.length();
    for (uint32_t i = start; i <= total_len; ++i) {
        char16_t c = (i < total_len) ? p[i] : SEP;
        if (knst_file_detail::IsSep(c)) {
            knst_file_detail::ResolvePathSegment(p, seg_start, i - seg_start, is_abs, parts);
            seg_start = i + 1;
        }
    }

    uint32_t result_len = prefix.length();
    for (size_t i = 0; i < parts.size(); ++i) {
        result_len += static_cast<uint32_t>(parts[i].length());
        if (i > 0) result_len += 1;
    }

    knst_c16string result = prefix;
    result.reserve(result_len);

    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            char16_t sep_arr[2] = { SEP, u'\0' };
            result.append(sep_arr);
        }
        result.append(parts[i]);
    }

    if (result.empty()) return u".";
    return result;
}


inline knst_c16string knst_file::join_path(const knst_c16string& a,const knst_c16string& b) noexcept {
    if (a.empty()) return b;
    if (b.empty()) return a;

   
    char16_t sep = KNST_FILE_SEPARATOR;
    for (uint32_t i = 0; i < a.length(); ++i) {
        if (a[i] == u'/') { sep = u'/'; break; }
#if KNST_USING_PLATFORM_WINDOWS
        if (a[i] == u'\\') { sep = u'\\'; break; }
#endif
    }

    knst_c16string result;
    result.reserve(a.length() + b.length() + 2);
    result.append(a);

  
    while (result.length() > 0 && knst_file_detail::IsSep(result[result.length() - 1])) {
        result.resize(result.length() - 1);
    }

  
    bool a_is_root = false;
    if (result.length() == 0) {
        a_is_root = true;
    }
#if KNST_USING_PLATFORM_WINDOWS
    else if (result.length() == 2 && result[1] == u':') {
        a_is_root = true;
    }
#endif

    if (a_is_root) {
        if (result.length() == 0) result.append(sep);
        else result.append(sep);
    } else {
        result.append(sep);
    }

   
    uint32_t b_start = 0;
    while (b_start < b.length() && knst_file_detail::IsSep(b[b_start])) b_start++;

    if (b_start < b.length()) {
        result.append(b.substr(b_start, b.length() - b_start));
    }

    return result;
}

inline knst_c16string knst_file::get_parent_dir(const knst_c16string& p) noexcept {
    if (p.empty()) return u".";

    if (p == u".") return u"..";
    if (p == u"..") return u"../..";

    knst_c16string joined = join_path(p, u"..");
    return normalize_path(joined);
}




#if KNST_USING_PLATFORM_WINDOWS

namespace knst_file_detail {
KNST_FORCE_INLINE uint64_t FileTimeToUnix(const FILETIME& ft) noexcept {
    ULARGE_INTEGER ull;
    ull.LowPart  = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    constexpr uint64_t EPOCH_DIFF = 116444736000000000ULL;
    if (ull.QuadPart < EPOCH_DIFF) return 0;
    return (ull.QuadPart - EPOCH_DIFF) / 10000000ULL;
}
}

inline knst_c16string knst_file::get_current_dir() noexcept {
    wchar_t buffer[MAX_PATH];
    DWORD n = GetCurrentDirectoryW(MAX_PATH, buffer);
    if (n == 0 || n >= MAX_PATH) return knst_c16string();
    return knst_c16string(reinterpret_cast<const char16_t*>(buffer));
}

inline bool knst_file::set_current_dir(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    return SetCurrentDirectoryW(reinterpret_cast<const wchar_t*>(p.data())) != 0;
}

inline knst_c16string knst_file::resolve_path(const knst_c16string& p) noexcept {
    if (p.empty()) return get_current_dir();

    knst_c16string abs;
    if (is_absolute_path(p)) {
        abs = p;
    } else {
        abs = get_current_dir();
        abs = join_path(abs, p);
    }
    return normalize_path(abs);
}

inline bool knst_file::is_absolute_path(const knst_c16string& p) noexcept {
    if (p.length() < 1) return false;

    if (p.length() >= 2 && p[1] == u':') {
        if (p.length() >= 3 && knst_file_detail::IsSep(p[2])) return true;
        return false;
    }

    if (p.length() >= 2 && p[0] == u'\\' && p[1] == u'\\') return true;
    if (p[0] == u'/' || p[0] == u'\\') return true;
    return false;
}

inline knst_c16string knst_file::get_executable_dir() noexcept {
    wchar_t buffer[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return knst_c16string();
    knst_c16string exe_path(reinterpret_cast<const char16_t*>(buffer));
    return get_directory(exe_path);
}

inline knst_c16string knst_file::get_home_dir() noexcept {
    wchar_t buffer[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buffer, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        n = GetEnvironmentVariableW(L"HOMEDRIVE", buffer, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            wchar_t buffer2[MAX_PATH];
            DWORD n2 = GetEnvironmentVariableW(L"HOMEPATH", buffer2, MAX_PATH);
            if (n2 > 0 && n2 < MAX_PATH) {
                knst_c16string result(reinterpret_cast<const char16_t*>(buffer));
                result.append(reinterpret_cast<const char16_t*>(buffer2));
                return result;
            }
        }
        return knst_c16string();
    }
    return knst_c16string(reinterpret_cast<const char16_t*>(buffer));
}

inline knst_c16string knst_file::get_temp_dir() noexcept {
    wchar_t buffer[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, buffer);
    if (n == 0 || n > MAX_PATH) return u"C:\\Temp";
    if (n > 1 && (buffer[n-1] == L'\\' || buffer[n-1] == L'/')) buffer[n-1] = L'\0';
    return knst_c16string(reinterpret_cast<const char16_t*>(buffer));
}

inline bool knst_file::path_exists(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    DWORD attrs = GetFileAttributesW(reinterpret_cast<const wchar_t*>(p.data()));
    return attrs != INVALID_FILE_ATTRIBUTES;
}

inline bool knst_file::file_exists(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    DWORD attrs = GetFileAttributesW(reinterpret_cast<const wchar_t*>(p.data()));
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

inline bool knst_file::dir_exists(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    DWORD attrs = GetFileAttributesW(reinterpret_cast<const wchar_t*>(p.data()));
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

inline bool knst_file::symlink_exists(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    DWORD attrs = GetFileAttributesW(reinterpret_cast<const wchar_t*>(p.data()));
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

inline knst_c16string knst_file::read_symlink_target(const knst_c16string& p) noexcept {
    if (p.empty()) return knst_c16string();

    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(p.data()),0,FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
                           
    if (h == INVALID_HANDLE_VALUE) return knst_c16string();

    struct KnstReparseDataBuffer {
        ULONG  ReparseTag;
        USHORT ReparseDataLength;
        USHORT Reserved;
        union {
            struct {
                USHORT SubstituteNameOffset;
                USHORT SubstituteNameLength;
                USHORT PrintNameOffset;
                USHORT PrintNameLength;
                ULONG  Flags;
                WCHAR  PathBuffer[1];
            } SymbolicLinkReparseBuffer;
            struct {
                USHORT SubstituteNameOffset;
                USHORT SubstituteNameLength;
                USHORT PrintNameOffset;
                USHORT PrintNameLength;
                WCHAR  PathBuffer[1];
            } MountPointReparseBuffer;
        };
    };

    constexpr DWORD kMaxReparseSize = 16 * 1024;
    unsigned char buffer[kMaxReparseSize];
    DWORD bytes_returned = 0;

    BOOL ok = DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, nullptr, 0,
                              buffer, kMaxReparseSize, &bytes_returned, nullptr);
    CloseHandle(h);
    if (!ok) return knst_c16string();

    auto* rb = reinterpret_cast<KnstReparseDataBuffer*>(buffer);

    const WCHAR* path_buf = nullptr;
    USHORT sub_offset = 0, sub_len = 0;

    if (rb->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
        path_buf   = rb->SymbolicLinkReparseBuffer.PathBuffer;
        sub_offset = rb->SymbolicLinkReparseBuffer.SubstituteNameOffset;
        sub_len    = rb->SymbolicLinkReparseBuffer.SubstituteNameLength;
    } else if (rb->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
        path_buf   = rb->MountPointReparseBuffer.PathBuffer;
        sub_offset = rb->MountPointReparseBuffer.SubstituteNameOffset;
        sub_len    = rb->MountPointReparseBuffer.SubstituteNameLength;
    } else {
        return knst_c16string();
    }

    const unsigned char* base = reinterpret_cast<const unsigned char*>(path_buf);
    const WCHAR* name = reinterpret_cast<const WCHAR*>(base + sub_offset);
    uint32_t name_len = static_cast<uint32_t>(sub_len / sizeof(WCHAR));

    knst_c16string target(reinterpret_cast<const char16_t*>(name), name_len);

    if (target.starts_with(u"\\??\\")) {
        target = target.substr(4, target.length() - 4);
    }

    return target;
}

inline bool knst_file::create_symlink(const knst_c16string& target,const knst_c16string& link_path,bool is_directory) noexcept {
    if (target.empty() || link_path.empty()) return false;
    DWORD flags = (is_directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0)| SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
                
    return CreateSymbolicLinkW(reinterpret_cast<const wchar_t*>(link_path.data()),reinterpret_cast<const wchar_t*>(target.data()),flags) != 0;
}

inline bool knst_file::create_directory(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    return CreateDirectoryW(reinterpret_cast<const wchar_t*>(p.data()), nullptr) != 0 ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

inline bool knst_file::create_directories(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    if (dir_exists(p)) return true;

    knst_c16string normalized = normalize_path(p);
    if (normalized.empty()) return false;

    uint32_t prefix_len = knst_file_detail::WindowsRootPrefixLength(normalized);
    uint32_t len = normalized.length();

    knst_c16string current = normalized.substr(0, prefix_len);
    uint32_t seg_start = prefix_len;

    for (uint32_t i = prefix_len; i <= len; ++i) {
        bool at_sep = (i < len) && knst_file_detail::IsSep(normalized[i]);
        bool at_end = (i == len);

        if (at_sep || at_end) {
            if (i > seg_start) {
                knst_c16string segment = normalized.substr(seg_start, i - seg_start);

                if (current.length() > 0 &&
                    !knst_file_detail::IsSep(current[current.length() - 1])) {
                    char16_t sep_arr[2] = { KNST_FILE_SEPARATOR, u'\0' };
                    current.append(sep_arr);
                }
                current.append(segment);

                if (current.length() > prefix_len) {
                    if (!dir_exists(current)) {
                        if (!create_directory(current)) return false;
                    }
                }
            }
            seg_start = i + 1;
        }
    }

    return dir_exists(p);
}

inline bool knst_file::remove_file(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    return DeleteFileW(reinterpret_cast<const wchar_t*>(p.data())) != 0;
}

inline bool knst_file::remove_directory(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    return RemoveDirectoryW(reinterpret_cast<const wchar_t*>(p.data())) != 0;
}

inline bool knst_file::rename(const knst_c16string& from,
                                const knst_c16string& to) noexcept {
    if (from.empty() || to.empty()) return false;
    return MoveFileExW(reinterpret_cast<const wchar_t*>(from.data()),
                       reinterpret_cast<const wchar_t*>(to.data()),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

inline bool knst_file::copy_file(const knst_c16string& from,
                                  const knst_c16string& to,
                                  bool overwrite) noexcept {
    if (from.empty() || to.empty()) return false;
    return CopyFileW(reinterpret_cast<const wchar_t*>(from.data()),
                     reinterpret_cast<const wchar_t*>(to.data()),
                     overwrite ? FALSE : TRUE) != 0;
}

inline bool knst_file::write_file_data(const knst_c16string& p,
                                        const unsigned char* data,
                                        uint32_t size) noexcept {
    if (p.empty()) return false;
    if (size > 0 && data == nullptr) return false;

    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(p.data()),
                           GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    bool write_ok = true;
    uint32_t written_total = 0;
    while (written_total < size) {
        DWORD to_write = static_cast<DWORD>(
            std::min<uint32_t>(size - written_total, 1u << 20));
        DWORD written = 0;
        if (!WriteFile(h, data + written_total, to_write, &written, nullptr) || written == 0) {
            write_ok = false;
            break;
        }
        written_total += written;
    }

    if (!write_ok) {
        CloseHandle(h);
        return false;
    }

    bool sync_ok = (FlushFileBuffers(h) != 0);
    if (CloseHandle(h) == 0) sync_ok = false;
    return sync_ok;
}

inline bool knst_file::append_file_data(const knst_c16string& p,
                                         const unsigned char* data,
                                         uint32_t size,
                                         bool sync) noexcept {
    if (p.empty()) return false;
    if (size > 0 && data == nullptr) return false;

    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(p.data()),
                           GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER zero{};
    zero.QuadPart = 0;
    if (!SetFilePointerEx(h, zero, nullptr, FILE_END)) {
        CloseHandle(h);
        return false;
    }

    bool ok = true;
    uint32_t written_total = 0;
    while (written_total < size) {
        DWORD to_write = static_cast<DWORD>(
            std::min<uint32_t>(size - written_total, 1u << 20));
        DWORD written = 0;
        if (!WriteFile(h, data + written_total, to_write, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        written_total += written;
    }

    if (sync) {
        if (!FlushFileBuffers(h)) ok = false;
    }
    CloseHandle(h);
    return ok;
}

inline bool knst_file::set_file_times(const knst_c16string& p,
                                       uint64_t modified_time_val,
                                       uint64_t accessed_time_val) noexcept {
    if (p.empty()) return false;
    if (accessed_time_val == 0) accessed_time_val = modified_time_val;

    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(p.data()),
                           FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    auto to_filetime = [](uint64_t unix_seconds) -> FILETIME {
        ULARGE_INTEGER ull;
        ull.QuadPart = unix_seconds * 10000000ULL + 116444736000000000ULL;
        FILETIME ft; ft.dwLowDateTime = ull.LowPart; ft.dwHighDateTime = ull.HighPart;
        return ft;
    };
    FILETIME atime = to_filetime(accessed_time_val);
    FILETIME mtime = to_filetime(modified_time_val);
    BOOL ok = SetFileTime(h, nullptr, &atime, &mtime);
    CloseHandle(h);
    return ok != 0;
}

inline bool knst_file::get_disk_space(const knst_c16string& p,
                                       uint64_t& out_free_bytes,
                                       uint64_t& out_total_bytes) noexcept {
    out_free_bytes = 0; out_total_bytes = 0;
    if (p.empty()) return false;

    ULARGE_INTEGER free_bytes, total_bytes, total_free_bytes;
    if (!GetDiskFreeSpaceExW(reinterpret_cast<const wchar_t*>(p.data()),
                             &free_bytes, &total_bytes, &total_free_bytes)) {
        return false;
    }
    out_free_bytes = free_bytes.QuadPart;
    out_total_bytes = total_bytes.QuadPart;
    return true;
}

inline uint64_t knst_file::get_file_size(const knst_c16string& p) noexcept {

    if (p.empty()) return 0;

    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(reinterpret_cast<const wchar_t*>(p.data()),
                              GetFileExInfoStandard, &info)) return 0; // returnnsss


    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 0;


    return (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
}

#else // POSIX

inline knst_c16string knst_file::get_current_dir() noexcept {
    char buffer[PATH_MAX];
    if (::getcwd(buffer, sizeof(buffer)) == nullptr) return knst_c16string();
    return knst_c16string(buffer);
}

inline bool knst_file::set_current_dir(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    knst_byte_string path_utf8(p);
    if (path_utf8.empty() || path_utf8.data() == nullptr) return false;
    return ::chdir(reinterpret_cast<const char*>(path_utf8.data())) == 0;
}

inline knst_c16string knst_file::resolve_path(const knst_c16string& p) noexcept {
    if (p.empty()) return get_current_dir();

    knst_c16string abs;
    if (is_absolute_path(p)) {
        abs = p;
    } else {
        abs = get_current_dir();
        abs = join_path(abs, p);
    }
    return normalize_path(abs);
}

inline bool knst_file::is_absolute_path(const knst_c16string& p) noexcept {
    return p.length() > 0 && p[0] == u'/';
}

inline knst_c16string knst_file::get_executable_dir() noexcept {
    char buffer[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        return get_current_dir();
    }
    buffer[n] = '\0';
    knst_c16string exe_path(buffer);
    return get_directory(exe_path);
}

inline knst_c16string knst_file::get_home_dir() noexcept {
    const char* home = getenv("HOME");
    if (home == nullptr || home[0] == '\0') return knst_c16string();
    return knst_c16string(home);
}

inline knst_c16string knst_file::get_temp_dir() noexcept {
    const char* tmp = getenv("TMPDIR");
    if (tmp != nullptr && tmp[0] != '\0') return knst_c16string(tmp);
    return u"/tmp";
}

inline bool knst_file::path_exists(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    knst_byte_string path_utf8(p);
    if (path_utf8.empty() || path_utf8.data() == nullptr) return false;
    struct stat st;
    return ::lstat(reinterpret_cast<const char*>(path_utf8.data()), &st) == 0;
}

inline bool knst_file::file_exists(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    knst_byte_string path_utf8(p);
    if (path_utf8.empty() || path_utf8.data() == nullptr) return false;
    struct stat st;
    if (::stat(reinterpret_cast<const char*>(path_utf8.data()), &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

inline bool knst_file::dir_exists(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    knst_byte_string path_utf8(p);
    if (path_utf8.empty() || path_utf8.data() == nullptr) return false;
    struct stat st;
    if (::stat(reinterpret_cast<const char*>(path_utf8.data()), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

inline bool knst_file::symlink_exists(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    knst_byte_string path_utf8(p);
    if (path_utf8.empty() || path_utf8.data() == nullptr) return false;
    struct stat st;
    if (::lstat(reinterpret_cast<const char*>(path_utf8.data()), &st) != 0) return false;
    return S_ISLNK(st.st_mode);
}

inline knst_c16string knst_file::read_symlink_target(const knst_c16string& p) noexcept {
    if (p.empty()) return knst_c16string();
    knst_byte_string path_utf8(p);
    if (path_utf8.empty() || path_utf8.data() == nullptr) return knst_c16string();

    char buffer[PATH_MAX];
    ssize_t n = ::readlink(reinterpret_cast<const char*>(path_utf8.data()), buffer, sizeof(buffer) - 1);
    if (n < 0) return knst_c16string();
    buffer[n] = '\0';
    return knst_c16string(buffer);
}

inline bool knst_file::create_symlink(const knst_c16string& target,const knst_c16string& link_path,bool is_directory) noexcept {
    (void)is_directory; // POSIX symlinks are typeless; nothing extra to set.
    if (target.empty() || link_path.empty()) return false;
    knst_byte_string target_utf8(target);
    knst_byte_string link_utf8(link_path);
    if (target_utf8.empty() || link_utf8.empty()) return false;
    return ::symlink(reinterpret_cast<const char*>(target_utf8.data()),reinterpret_cast<const char*>(link_utf8.data())) == 0;
}

inline bool knst_file::create_directory(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    knst_byte_string path_utf8(p);
    if (path_utf8.empty() || path_utf8.data() == nullptr) return false;
    if (::mkdir(reinterpret_cast<const char*>(path_utf8.data()), 0755) == 0) return true;
    return errno == EEXIST;
}

inline bool knst_file::create_directories(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    if (dir_exists(p)) return true;

    knst_c16string normalized = normalize_path(p);
    if (normalized.empty()) return false;

    uint32_t len = normalized.length();
    uint32_t start = 0;

    knst_c16string current;
    if (normalized[0] == u'/') {
        char16_t sep_arr[2] = { KNST_FILE_SEPARATOR, u'\0' };
        current.append(sep_arr);
        start = 1;
    }

    uint32_t seg_start = start;
    for (uint32_t i = start; i <= len; ++i) {
        bool at_sep = (i < len) && knst_file_detail::IsSep(normalized[i]);
        bool at_end = (i == len);

        if (at_sep || at_end) {
            if (i > seg_start) {
                knst_c16string segment = normalized.substr(seg_start, i - seg_start);

                if (current.empty() || current == u"/") {
                    current.append(segment);
                } else {
                    char16_t sep_arr[2] = { KNST_FILE_SEPARATOR, u'\0' };
                    current.append(sep_arr);
                    current.append(segment);
                }

                if (!dir_exists(current)) {
                    if (!create_directory(current)) return false;
                }
            }
            seg_start = i + 1;
        }
    }

    return dir_exists(p);
}

inline bool knst_file::remove_file(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    knst_byte_string path_utf8(p);
    if (path_utf8.empty() || path_utf8.data() == nullptr) return false;
    return ::unlink(reinterpret_cast<const char*>(path_utf8.data())) == 0;
}

inline bool knst_file::remove_directory(const knst_c16string& p) noexcept {
    if (p.empty()) return false;
    knst_byte_string path_utf8(p);
    if (path_utf8.empty() || path_utf8.data() == nullptr) return false;
    return ::rmdir(reinterpret_cast<const char*>(path_utf8.data())) == 0;
}

inline bool knst_file::rename(const knst_c16string& from, const knst_c16string& to) noexcept {
    if (from.empty() || to.empty()) return false;
    knst_byte_string f_utf8(from);
    knst_byte_string t_utf8(to);
    if (f_utf8.empty() || t_utf8.empty()) return false;
    return ::rename(reinterpret_cast<const char*>(f_utf8.data()),reinterpret_cast<const char*>(t_utf8.data())) == 0;
}

inline bool knst_file::copy_file(const knst_c16string& from, const knst_c16string& to, bool overwrite) noexcept {
    if (from.empty() || to.empty()) return false;

    knst_byte_string from_utf8(from);
    knst_byte_string to_utf8(to);
    if (from_utf8.empty() || to_utf8.empty()) return false;

    int src_fd = ::open(reinterpret_cast<const char*>(from_utf8.data()), O_RDONLY | O_CLOEXEC);
    if (src_fd < 0) return false;

    struct stat st;
    if (::fstat(src_fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(src_fd);
        return false;
    }

    int open_flags = O_WRONLY | O_CREAT | O_CLOEXEC | (overwrite ? O_TRUNC : O_EXCL);
    int dst_fd = ::open(reinterpret_cast<const char*>(to_utf8.data()), open_flags,
                        static_cast<mode_t>(st.st_mode & 0777));
    if (dst_fd < 0) {
        ::close(src_fd);
        return false;
    }

    constexpr size_t kChunk = 64 * 1024;
    unsigned char buffer[kChunk];
    bool ok = true;

    for (;;) {
        ssize_t n_read = ::read(src_fd, buffer, kChunk);
        if (n_read < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        if (n_read == 0) break;

        ssize_t written_total = 0;
        while (written_total < n_read) {
            ssize_t n_written = ::write(dst_fd, buffer + written_total,static_cast<size_t>(n_read - written_total));
                                        
            if (n_written < 0) {
                if (errno == EINTR) continue;
                ok = false;
                break;
            }
            written_total += n_written;
        }
        if (!ok) break;
    }

    ::close(src_fd);
    if (::close(dst_fd) != 0) ok = false;
    if (!ok) remove_file(to);
    return ok;
}

inline bool knst_file::write_file_data(const knst_c16string& p,const unsigned char* data, uint32_t size) noexcept {
    if (p.empty()) return false;
    if (size > 0 && data == nullptr) return false;

    knst_byte_string path_utf8(p);
    if (path_utf8.empty() || path_utf8.data() == nullptr) return false;

    int fd = ::open(reinterpret_cast<const char*>(path_utf8.data()), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
                   
    if (fd < 0) return false;

    bool write_ok = true;
    uint32_t written_total = 0;
    while (written_total < size) {
        ssize_t n = ::write(fd, data + written_total, size - written_total);
        if (n < 0) {
            if (errno == EINTR) continue;
            write_ok = false;
            break;
        }
        if (n == 0) { write_ok = false; break; }
        written_total += static_cast<uint32_t>(n);
    }

    if (!write_ok) {
        ::close(fd);
        return false;
    }

    bool sync_ok = (::fsync(fd) == 0);
    if (::close(fd) != 0) sync_ok = false;
    return sync_ok;
}

inline bool knst_file::append_file_data(const knst_c16string& p, const unsigned char* data, uint32_t size,bool sync) noexcept {
    if (p.empty()) return false;
    if (size > 0 && data == nullptr) return false;

    knst_byte_string path_utf8(p);
    if (path_utf8.empty() || path_utf8.data() == nullptr) return false;

    int fd = ::open(reinterpret_cast<const char*>(path_utf8.data()), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
                   
    if (fd < 0) return false;

    bool ok = true;
    uint32_t written_total = 0;
    while (written_total < size) {
        ssize_t n = ::write(fd, data + written_total, size - written_total);
        if (n < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        if (n == 0) { ok = false; break; }
        written_total += static_cast<uint32_t>(n);
    }

    if (sync) {
        if (::fsync(fd) != 0) ok = false;
    }
    if (::close(fd) != 0) ok = false;
    return ok;
}

inline bool knst_file::set_file_times(const knst_c16string& p,uint64_t modified_time_val,uint64_t accessed_time_val) noexcept {
    if (p.empty()) return false;
    if (accessed_time_val == 0) accessed_time_val = modified_time_val;

    knst_byte_string path_utf8(p);
    if (path_utf8.empty() || path_utf8.data() == nullptr) return false;

    struct timeval times[2];
    times[0].tv_sec = static_cast<time_t>(accessed_time_val);
    times[0].tv_usec = 0;
    times[1].tv_sec = static_cast<time_t>(modified_time_val);
    times[1].tv_usec = 0;
    return ::utimes(reinterpret_cast<const char*>(path_utf8.data()), times) == 0;
}

inline bool knst_file::get_disk_space(const knst_c16string& p, uint64_t& out_free_bytes, uint64_t& out_total_bytes) noexcept {
    out_free_bytes = 0; out_total_bytes = 0;
    if (p.empty()) return false;

    knst_byte_string path_utf8(p);
    if (path_utf8.empty() || path_utf8.data() == nullptr) return false;

    struct statvfs st;
    if (::statvfs(reinterpret_cast<const char*>(path_utf8.data()), &st) != 0) return false;
    out_free_bytes = static_cast<uint64_t>(st.f_bavail) * static_cast<uint64_t>(st.f_frsize);
    out_total_bytes = static_cast<uint64_t>(st.f_blocks) * static_cast<uint64_t>(st.f_frsize);
    return true;
}

inline uint64_t knst_file::get_file_size(const knst_c16string& p) noexcept {
    if (p.empty()) return 0;
    knst_byte_string path_utf8(p);
    if (path_utf8.empty() || path_utf8.data() == nullptr) return 0;
    struct stat st;
    if (::stat(reinterpret_cast<const char*>(path_utf8.data()), &st) != 0) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
    return static_cast<uint64_t>(st.st_size);
}

#endif // platform




#if KNST_USING_PLATFORM_WINDOWS

template<typename Alloc>
inline basic_byte_string<Alloc>
knst_file::read_file_data(const knst_c16string& p, knst_file_error* out_error) noexcept {
    if (out_error) *out_error = knst_file_error::None;

    basic_byte_string<Alloc> result;
    if (p.empty()) {
        if (out_error) *out_error = knst_file_error::InvalidPath;
        return result;
    }

    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(p.data()),
                           GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (out_error) *out_error = knst_file_detail::MapWinError(GetLastError());
        return result;
    }

    LARGE_INTEGER fs;
    if (!GetFileSizeEx(h, &fs)) {
        if (out_error) *out_error = knst_file_detail::MapWinError(GetLastError());
        CloseHandle(h); return result;
    }
    if (fs.QuadPart <= 0) {
        CloseHandle(h); return result;
    }
    if (fs.QuadPart > static_cast<int64_t>(UINT32_MAX)) {
        if (out_error) *out_error = knst_file_error::TooLarge;
        CloseHandle(h); return result;
    }

    uint32_t byte_size = static_cast<uint32_t>(fs.QuadPart);
    result.resize(byte_size);
    if (result.length() != byte_size || result.data() == nullptr) {
        if (out_error) *out_error = knst_file_error::OutOfMemory;
        result.clear(); CloseHandle(h); return result;
    }

    DWORD total_read = 0;
    DWORD to_read = static_cast<DWORD>(byte_size);
    BOOL ok = ReadFile(h, &result[0], to_read, &total_read, nullptr);
    CloseHandle(h);

    if (!ok || total_read != to_read) {
        if (out_error) *out_error = knst_file_error::ReadError;
        result.clear();
    }
    return result;
}

#else // POSIX

template<typename Alloc>
inline basic_byte_string<Alloc>
knst_file::read_file_data(const knst_c16string& p,knst_file_error* out_error) noexcept {
    if (out_error) *out_error = knst_file_error::None;

    basic_byte_string<Alloc> result;
    if (p.empty()) {
        if (out_error) *out_error = knst_file_error::InvalidPath;
        return result;
    }

    knst_byte_string path_utf8(p);
    if (path_utf8.empty() || path_utf8.data() == nullptr) {
        if (out_error) *out_error = knst_file_error::InvalidPath;
        return result;
    }

    int fd = ::open(reinterpret_cast<const char*>(path_utf8.data()), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (out_error) *out_error = knst_file_detail::MapErrno(errno);
        return result;
    }

    struct stat st;
    if (::fstat(fd, &st) != 0) {
        if (out_error) *out_error = knst_file_detail::MapErrno(errno);
        ::close(fd); return result;
    }
    if (!S_ISREG(st.st_mode)) {
        if (out_error) *out_error = S_ISDIR(st.st_mode)? knst_file_error::IsDirectory : knst_file_error::NotAFile;
        ::close(fd); return result;
    }
    if (st.st_size <= 0) {
        ::close(fd); return result;
    }
    if (static_cast<uint64_t>(st.st_size) > static_cast<uint64_t>(UINT32_MAX)) {
        if (out_error) *out_error = knst_file_error::TooLarge;
        ::close(fd); return result;
    }

    uint32_t byte_size = static_cast<uint32_t>(st.st_size);
    result.resize(byte_size);
    if (result.length() != byte_size || result.data() == nullptr) {
        if (out_error) *out_error = knst_file_error::OutOfMemory;
        result.clear(); ::close(fd); return result;
    }

    ssize_t total = 0;
    uint8_t* dst = &result[0];
    while (total < static_cast<ssize_t>(byte_size)) {
        ssize_t n = ::read(fd, dst + total, byte_size - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (out_error) *out_error = knst_file_detail::MapErrno(errno);
            break;
        }
        if (n == 0) {
            if (out_error) *out_error = knst_file_error::ReadError;
            break;
        }
        total += n;
    }
    ::close(fd);

    if (total != static_cast<ssize_t>(byte_size)) result.clear();
    return result;
}

#endif


template<typename Alloc>
inline knst_c16string knst_file::read_file_text(const knst_c16string& p) noexcept {
    basic_byte_string<Alloc> data = knst_file::read_file_data<Alloc>(p);
    if (data.empty() || data.data() == nullptr) return knst_c16string();

    const char* text = reinterpret_cast<const char*>(data.data());
    uint32_t len = data.length();

    if (len >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text += 3;
        len  -= 3;
    }

    return knst_c16string(text, len);
}


inline bool knst_file::ensure_parent_dir(const knst_c16string& p) noexcept {
    knst_c16string dir = get_directory(p);
    if (dir.empty()) return true;
    if (dir_exists(dir)) return true;
    return create_directories(dir);
}

inline bool knst_file::touch_file(const knst_c16string& p, bool create_parents) noexcept {
    if (p.empty()) return false;
    if (create_parents && !ensure_parent_dir(p)) return false;
    if (file_exists(p)) return true;
    return write_file_data(p, nullptr, 0);
}

inline bool knst_file::write_file_text(const knst_c16string& p,
                                        const knst_c16string& text) noexcept {
    knst_byte_string utf8(text);
    return write_file_data(p, utf8.data(), utf8.length());
}

inline bool knst_file::append_file_text(const knst_c16string& p,
                                         const knst_c16string& text,
                                         bool sync) noexcept {
    knst_byte_string utf8(text);
    return append_file_data(p, utf8.data(), utf8.length(), sync);
}

inline bool knst_file::write_file_atomic(const knst_c16string& p,const unsigned char* data,uint32_t size) noexcept {
    if (p.empty()) return false;
    if (size > 0 && data == nullptr) return false;

    if (!ensure_parent_dir(p)) return false;

    knst_c16string tmp_path = p;
    tmp_path.append(knst_file_detail::MakeTempSuffix());

    if (!write_file_data(tmp_path, data, size)) {
        remove_file(tmp_path);
        return false;
    }

    if (!rename(tmp_path, p)) {
        remove_file(tmp_path);
        return false;
    }

#if !KNST_USING_PLATFORM_WINDOWS
    knst_c16string dir = get_directory(p);
    if (!dir.empty()) {
        knst_byte_string dir_utf8(dir);
        if (!dir_utf8.empty() && dir_utf8.data() != nullptr) {
            int dfd = ::open(reinterpret_cast<const char*>(dir_utf8.data()),
                             O_RDONLY | O_CLOEXEC);
            if (dfd >= 0) {
                ::fsync(dfd);
                ::close(dfd);
            }
        }
    }
#endif

    return true;
}

inline bool knst_file::write_file_atomic_text(const knst_c16string& p,const knst_c16string& text) noexcept {
    knst_byte_string utf8(text);
    return write_file_atomic(p, utf8.data(), utf8.length());
}



inline bool knst_file::copy_directory(const knst_c16string& from,
                                       const knst_c16string& to,
                                       bool overwrite,
                                       bool follow_symlinks) noexcept {
    if (from.empty() || to.empty()) return false;
    if (!dir_exists(from)) return false;
    if (!create_directories(to)) return false;

    knst_vector<knst_file> entries;
    if (!list_directory(from, entries)) return false;

    bool ok = true;
    for (auto& entry : entries) {
        knst_c16string dest_path = join_path(to, entry.name);

        if (entry.is_symlink && !follow_symlinks) {
            knst_c16string target = read_symlink_target(entry.path);
            if (target.empty() ||
                !knst_file::create_symlink(target, dest_path, entry.is_directory)) {
                ok = false;
            }
            continue;
        }

        if (entry.is_directory) {
            if (!knst_file::copy_directory(entry.path, dest_path, overwrite, follow_symlinks)) {
                ok = false;
            }
        } else {
            if (!knst_file::copy_file(entry.path, dest_path, overwrite)) {
                ok = false;
            }
        }
    }
    return ok;
}

inline bool knst_file::move_path(const knst_c16string& from,const knst_c16string& to,bool overwrite) noexcept {
    if (from.empty() || to.empty()) return false;
    if (!path_exists(from)) return false;

    if (!overwrite && path_exists(to)) {
        return false;
    }

    if (overwrite && path_exists(to)) {
        knst_file::remove_all(to);
    }

    if (knst_file::rename(from, to)) return true;

   
    knst_file info;
    info.read_file(from);

    bool ok;
    if (info.is_directory && !info.is_symlink) {
        ok = knst_file::copy_directory(from, to, overwrite, false);
    } else {
        ok = knst_file::copy_file(from, to, overwrite);
    }
    if (!ok) return false;

    return knst_file::remove_all(from);
}

inline bool knst_file::is_valid_filename(const knst_c16string& name_) noexcept {
    if (name_.empty()) return false;
    if (name_ == u"." || name_ == u"..") return false;

    uint32_t len = name_.length();
    for (uint32_t i = 0; i < len; ++i) {
        char16_t c = name_[i];
        if (c < 0x20) return false;
#if KNST_USING_PLATFORM_WINDOWS
        switch (c) {
            case u'<': case u'>': case u':': case u'"':
            case u'/': case u'\\': case u'|': case u'?': case u'*':
                return false;
            default: break;
        }
#else
        if (c == u'/') return false;
#endif
    }

#if KNST_USING_PLATFORM_WINDOWS
    char16_t last = name_[len - 1];
    if (last == u'.' || last == u' ') return false;

    static const char16_t* const reserved[] = {
        u"CON", u"PRN", u"AUX", u"NUL",
        u"COM1", u"COM2", u"COM3", u"COM4", u"COM5", u"COM6", u"COM7", u"COM8", u"COM9",
        u"LPT1", u"LPT2", u"LPT3", u"LPT4", u"LPT5", u"LPT6", u"LPT7", u"LPT8", u"LPT9"
    };
    knst_c16string stem = get_stem(name_);
    for (const char16_t* r : reserved) {
        if (stem == r) return false;
    }
#endif

    return true;
}

inline knst_c16string knst_file::sanitize_filename(const knst_c16string& name_,char16_t replacement) noexcept {
    knst_c16string result = name_;
    uint32_t len = result.length();


    for (uint32_t i = 0; i < len; ++i) {
        char16_t c = result[i];
        bool bad = (c < 0x20);
#if KNST_USING_PLATFORM_WINDOWS
        if (!bad) {
            switch (c) {
                case u'<': case u'>': case u':': case u'"':
                case u'/': case u'\\': case u'|': case u'?': case u'*':
                    bad = true; break;
                default: break;
            }
        }
#else
        if (!bad && c == u'/') bad = true;
#endif
        if (bad) result[i] = replacement;
    }

#if KNST_USING_PLATFORM_WINDOWS

    knst_c16string stem = get_stem(result);
    static const char16_t* const reserved[] = {
        u"CON", u"PRN", u"AUX", u"NUL",
        u"COM1", u"COM2", u"COM3", u"COM4", u"COM5",
        u"COM6", u"COM7", u"COM8", u"COM9",
        u"LPT1", u"LPT2", u"LPT3", u"LPT4", u"LPT5",
        u"LPT6", u"LPT7", u"LPT8", u"LPT9"
    };
    bool is_reserved = false;
    for (const char16_t* r : reserved) {
        if (stem == r) { is_reserved = true; break; }
    }
    if (is_reserved) {
        knst_c16string prefixed;
        prefixed.append(replacement);
        prefixed.append(result);
        result = prefixed;
    }

  
    while (result.length() > 0) {
        char16_t last = result[result.length() - 1];
        if (last == u'.' || last == u' ') {
            result.resize(result.length() - 1);
        } else break;
    }

    if (result.empty()) result.append(replacement);
#endif

    return result;
}




inline void knst_file::reset() noexcept {
    path.clear();
    name.clear();
    extension.clear();
    full_extension.clear();
    parent_dir.clear();

    ext_cs = 0; ext_ci = 0;
    full_ext_cs = 0; full_ext_ci = 0;
    ext_len = 0;

    size = 0;
    created_time = 0;
    modified_time = 0;
    accessed_time = 0;

    exists = false;
    is_file = false;
    is_directory = false;
    is_symlink = false;
    is_hidden = false;
    is_readonly = false;
    is_executable = false;

    error = knst_file_error::None;
}

inline knst_file_error knst_file::refresh() noexcept {
    knst_c16string saved_path = path;
    if (saved_path.empty()) {
        reset();
        error = knst_file_error::InvalidPath;
        return error;
    }
    return read_file(saved_path);
}


inline knst_file_error knst_file::read_file(const knst_c16string& p) noexcept {
    reset();

    if (p.empty()) {
        error = knst_file_error::InvalidPath;
        return error;
    }

    path = p;

    uint32_t last_sep = knst_file_detail::FindLastSep(p);

    if (last_sep == UINT32_MAX) {
        name = p;
    } else {
        parent_dir = p.substr(0, last_sep + 1);
        name = p.substr(last_sep + 1, p.length() - last_sep - 1);
    }

    is_hidden = (name.length() > 0 && name[0] == u'.');

    knst_file_detail::ExtractExtension(name, extension, full_extension,
                                       ext_cs, ext_ci, full_ext_cs, full_ext_ci,
                                       ext_len);

    #if KNST_USING_PLATFORM_WINDOWS

        WIN32_FILE_ATTRIBUTE_DATA info;
        if (!GetFileAttributesExW(reinterpret_cast<const wchar_t*>(p.data()),
                                  GetFileExInfoStandard, &info)) {
            error = knst_file_detail::MapWinError(GetLastError());
            return error;
        }

        exists = true;
        size = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        created_time  = knst_file_detail::FileTimeToUnix(info.ftCreationTime);
        modified_time = knst_file_detail::FileTimeToUnix(info.ftLastWriteTime);
        accessed_time = knst_file_detail::FileTimeToUnix(info.ftLastAccessTime);

        DWORD attrs = info.dwFileAttributes;
        is_directory  = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        is_file       = !is_directory;
        is_readonly   = (attrs & FILE_ATTRIBUTE_READONLY) != 0;
        is_symlink    = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

        if ((attrs & FILE_ATTRIBUTE_HIDDEN) != 0) is_hidden = true;

    #else // POSIX

        knst_byte_string path_utf8(p);
        if (path_utf8.empty() || path_utf8.data() == nullptr) {
            error = knst_file_error::InvalidPath;
            return error;
        }

        struct stat st;
        if (::lstat(reinterpret_cast<const char*>(path_utf8.data()), &st) != 0) {
            error = knst_file_detail::MapErrno(errno);
            return error;
        }

        exists = true;
        size = static_cast<uint64_t>(st.st_size);
        created_time  = static_cast<uint64_t>(st.st_ctime);
        modified_time = static_cast<uint64_t>(st.st_mtime);
        accessed_time = static_cast<uint64_t>(st.st_atime);

        is_symlink    = S_ISLNK(st.st_mode);
        is_directory  = S_ISDIR(st.st_mode);
        is_file       = S_ISREG(st.st_mode);
        is_executable = (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
        is_readonly = (st.st_mode & S_IWUSR) == 0;

        if (is_symlink) {
            struct stat st_target;
            if (::stat(reinterpret_cast<const char*>(path_utf8.data()), &st_target) == 0) {
                is_directory = S_ISDIR(st_target.st_mode);
                is_file  = S_ISREG(st_target.st_mode);
            }
        }

    #endif

    return error;
}


template<typename Alloc>
inline bool knst_file::read_file_lines(knst_vector<knst_c16string>& out,
                                       knst_file_error* out_error) const noexcept {
    if (out_error) *out_error = knst_file_error::None;
    out.clear();

    knst_file_error local_err = knst_file_error::None;
    basic_byte_string<Alloc> data = read_file_data<Alloc>(&local_err);
    if (out_error) *out_error = local_err;
    if (local_err != knst_file_error::None) return false;

    if (data.empty() || data.data() == nullptr) return true;

    const char* text = reinterpret_cast<const char*>(data.data());
    uint32_t len = data.length();

    if (len >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text += 3;
        len  -= 3;
    }

    uint32_t guess_lines = len / 32 + 4;
    constexpr uint32_t kMaxReserve = 65536;
    if (guess_lines > kMaxReserve) guess_lines = kMaxReserve;
    out.reserve(guess_lines);

    uint32_t start = 0;
    for (uint32_t i = 0; i < len; ++i) {
        if (text[i] == '\n') {
            uint32_t end = i;
            if (end > start && text[end - 1] == '\r') end--;
            out.push_back(knst_c16string(text + start, end - start));
            start = i + 1;
        }
    }
    if (start < len) {
        uint32_t end = len;
        if (end > start && text[end - 1] == '\r') end--;
        out.push_back(knst_c16string(text + start, end - start));
    }

    return true;
}


inline bool knst_file::write_lines(const knst_vector<knst_c16string>& lines) const noexcept {
    uint64_t total64 = 0;
    for (uint32_t i = 0; i < lines.size(); ++i) {
        total64 += knst_get_utf16_to_utf8_exact_byte_size(lines[i].data(), lines[i].length());
        total64 += 1;
        if (total64 > static_cast<uint64_t>(UINT32_MAX)) {
            return false;
        }
    }
    uint32_t total = static_cast<uint32_t>(total64);

    knst_byte_string buf;
    if (total > 0) buf.reserve(total);

    for (uint32_t i = 0; i < lines.size(); ++i) {
        uint32_t line_bytes = knst_get_utf16_to_utf8_exact_byte_size(lines[i].data(),
                                                                     lines[i].length());
        if (line_bytes > 0) {
            uint32_t old_len = buf.length();
            buf.resize(old_len + line_bytes);
            if (buf.length() != old_len + line_bytes) {
                return false;
            }
            knst_convert_utf16_to_utf8(
                lines[i].data(),
                lines[i].length(),
                reinterpret_cast<char*>(&buf[old_len])
            );
        }
        buf.push_back(static_cast<unsigned char>('\n'));
    }

    return write_data(buf);
}


inline bool knst_file::append_line(const knst_c16string& line, bool sync) const noexcept {
    knst_byte_string buf(line);
    buf.push_back(static_cast<unsigned char>('\n'));
    return append_data(buf, sync);
}


inline bool knst_file::remove() noexcept {
    bool ok;
    if (is_directory && !is_symlink) {
        ok = knst_file::remove_directory(path);
    } else {
        ok = knst_file::remove_file(path);
    }
    if (ok) {
        refresh();
    }
    return ok;
}

inline bool knst_file::remove_all() noexcept {
    bool ok = knst_file::remove_all(path);
    if (ok) {
        refresh();
    }
    return ok;
}

inline bool knst_file::rename_to(const knst_c16string& new_path) noexcept {
    if (new_path.empty()) return false;
    if (!knst_file::rename(path, new_path)) return false;
    return read_file(new_path) == knst_file_error::None;
}

inline bool knst_file::move_to(const knst_c16string& dest, bool overwrite) noexcept {
    if (!knst_file::move_path(path, dest, overwrite)) return false;
    return read_file(dest) == knst_file_error::None;
}




#if KNST_USING_PLATFORM_WINDOWS

inline bool knst_file::list_directory(const knst_c16string& dir, knst_vector<knst_file>& out) noexcept {
    out.clear();
    if (dir.empty()) return false;

    knst_c16string search = dir;
    if (search.length() > 0 && !knst_file_detail::IsSep(search[search.length() - 1])) {
        char16_t sep_arr[2] = { KNST_FILE_SEPARATOR, u'\0' };
        search.append(sep_arr);
    }
    search.append(u"*");

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(reinterpret_cast<const wchar_t*>(search.data()), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;

    do {
        if (fd.cFileName[0] == u'.' && fd.cFileName[1] == u'\0') continue;
        if (fd.cFileName[0] == u'.' && fd.cFileName[1] == u'.' && fd.cFileName[2] == u'\0') continue;

        knst_file entry;
        entry.name = knst_c16string(reinterpret_cast<const char16_t*>(fd.cFileName));

        entry.parent_dir = dir;
        if (entry.parent_dir.length() > 0 && !knst_file_detail::IsSep(entry.parent_dir[entry.parent_dir.length() - 1])) {
            char16_t sep_arr[2] = { KNST_FILE_SEPARATOR, u'\0' };
            entry.parent_dir.append(sep_arr);
        }
        entry.path = join_path(entry.parent_dir, entry.name);

        entry.exists = true;
        entry.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        entry.created_time  = knst_file_detail::FileTimeToUnix(fd.ftCreationTime);
        entry.modified_time = knst_file_detail::FileTimeToUnix(fd.ftLastWriteTime);
        entry.accessed_time = knst_file_detail::FileTimeToUnix(fd.ftLastAccessTime);

        DWORD attrs = fd.dwFileAttributes;
        entry.is_directory = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.is_file      = !entry.is_directory;
        entry.is_readonly  = (attrs & FILE_ATTRIBUTE_READONLY) != 0;
        entry.is_symlink   = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        entry.is_hidden    = (attrs & FILE_ATTRIBUTE_HIDDEN) != 0 ||
                             (entry.name.length() > 0 && entry.name[0] == u'.');

        knst_file_detail::ExtractExtension(entry.name,
                                            entry.extension, entry.full_extension,
                                            entry.ext_cs, entry.ext_ci,
                                            entry.full_ext_cs, entry.full_ext_ci,
                                            entry.ext_len);

        out.push_back(std::move(entry));
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return true;
}

#else // POSIX

inline bool knst_file::list_directory(const knst_c16string& dir,
                                       knst_vector<knst_file>& out) noexcept {
    out.clear();
    if (dir.empty()) return false;

    knst_byte_string path_utf8(dir);
    if (path_utf8.empty() || path_utf8.data() == nullptr) return false;

    DIR* d = ::opendir(reinterpret_cast<const char*>(path_utf8.data()));
    if (d == nullptr) return false;

    struct dirent* de;
    while ((de = ::readdir(d)) != nullptr) {
        if (de->d_name[0] == '.' && de->d_name[1] == '\0') continue;
        if (de->d_name[0] == '.' && de->d_name[1] == '.' && de->d_name[2] == '\0') continue;

        knst_file entry;
        entry.name = knst_c16string(de->d_name);

        entry.parent_dir = dir;
        if (entry.parent_dir.length() > 0 && !knst_file_detail::IsSep(entry.parent_dir[entry.parent_dir.length() - 1])) {
            char16_t sep_arr[2] = { KNST_FILE_SEPARATOR, u'\0' };
            entry.parent_dir.append(sep_arr);
        }
        entry.path = join_path(entry.parent_dir, entry.name);

        struct stat st;
        knst_byte_string entry_utf8(entry.path);
        if (entry_utf8.empty() || entry_utf8.data() == nullptr) continue;
        if (::lstat(reinterpret_cast<const char*>(entry_utf8.data()), &st) != 0) continue;

        entry.exists = true;
        entry.size = static_cast<uint64_t>(st.st_size);
        entry.created_time  = static_cast<uint64_t>(st.st_ctime);
        entry.modified_time = static_cast<uint64_t>(st.st_mtime);
        entry.accessed_time = static_cast<uint64_t>(st.st_atime);

        entry.is_symlink    = S_ISLNK(st.st_mode);
        entry.is_directory  = S_ISDIR(st.st_mode);
        entry.is_file       = S_ISREG(st.st_mode);
        entry.is_executable = (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
        entry.is_readonly   = (st.st_mode & S_IWUSR) == 0;
        entry.is_hidden     = (entry.name.length() > 0 && entry.name[0] == u'.');

        if (entry.is_symlink) {
            struct stat st_target;
            if (::stat(reinterpret_cast<const char*>(entry_utf8.data()), &st_target) == 0) {
                entry.is_directory = S_ISDIR(st_target.st_mode);
                entry.is_file      = S_ISREG(st_target.st_mode);
            }
        }

        knst_file_detail::ExtractExtension(entry.name,
                                            entry.extension, entry.full_extension,
                                            entry.ext_cs, entry.ext_ci,
                                            entry.full_ext_cs, entry.full_ext_ci,
                                            entry.ext_len);

        out.push_back(std::move(entry));
    }

    ::closedir(d);
    return true;
}

#endif


template<typename Predicate, typename>
inline bool knst_file::list_directory(const knst_c16string& dir,
                                       knst_vector<knst_file>& out,
                                       Predicate filter) noexcept {
    knst_vector<knst_file> all;
    if (!list_directory(dir, all)) return false;

    out.clear();
    for (auto& f : all) {
        if (filter(f)) out.push_back(std::move(f));
    }
    return true;
}

namespace knst_file_detail {

template<typename Predicate>
inline bool list_dir_recursive_impl(const knst_c16string& dir,knst_vector<knst_file>& out,Predicate filter,int max_depth,bool follow_symlinks,int current_depth) noexcept {
                                     
                                     
    if (max_depth == 0) return true;

    if (follow_symlinks && max_depth < 0 &&
        current_depth >= KNST_MAX_SAFE_RECURSION_DEPTH) {
        return true;
    }

    knst_vector<knst_file> entries;
    if (!knst_file::list_directory(dir, entries)) return false;

    for (auto& entry : entries) {
        if (entry.is_directory) {
            if (entry.is_symlink && !follow_symlinks) {
                if (filter(entry)) out.push_back(entry);
                continue;
            }
            if (filter(entry)) out.push_back(entry);
            int next_depth = (max_depth < 0) ? -1 : (max_depth - 1);
            list_dir_recursive_impl(entry.path, out, filter, next_depth,
                                     follow_symlinks, current_depth + 1);
        } else {
            if (filter(entry)) out.push_back(std::move(entry));
        }
    }

    return true;
}

} // namespace knst_file_detail


inline bool knst_file::list_directory_recursive(const knst_c16string& dir,knst_vector<knst_file>& out,int max_depth,bool follow_symlinks) noexcept {
    out.clear();
    return knst_file_detail::list_dir_recursive_impl(
        dir, out,
        [](const knst_file&) -> bool { return true; },
        max_depth, follow_symlinks, 0);
}


template<typename Predicate, typename>
inline bool knst_file::list_directory_recursive(const knst_c16string& dir,knst_vector<knst_file>& out, Predicate filter,int max_depth,bool follow_symlinks) noexcept {
                                                 
    out.clear();
    return knst_file_detail::list_dir_recursive_impl(dir, out, filter, max_depth, follow_symlinks, 0);
        
}



inline bool knst_file::remove_all(const knst_c16string& p) noexcept {
    if (p.empty()) return false;

    knst_file root;
    root.read_file(p);
    if (!root.exists) return false;

    if (!root.is_directory || root.is_symlink) {
        return remove_file(p);
    }

    knst_vector<knst_file> all;
    if (!list_directory_recursive(p, all, -1, false)) {
        return false;
    }

    bool ok = true;
    for (size_t i = all.size(); i-- > 0; ) {
                const knst_file& e = all[static_cast<uint32_t>(i)];
        if (e.is_directory && !e.is_symlink) {
            if (!remove_directory(e.path)) ok = false;
        } else {
            if (!remove_file(e.path)) ok = false;
        }
    }

    if (!remove_directory(p)) ok = false;
    return ok;
}