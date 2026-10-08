# knst_file — Cross-Platform File System Library

Hello! 👋 This document explains what the `knst_file` library is, how to use it, and why it exists in KernelNucleusT.

In short: **a cross-platform file system library that brings all file and directory operations under one API.** It works on Linux, Windows, and Android. It includes reading, writing, deleting, copying, moving, listing, permissions, timestamps, and **200+ file extension recognition** — everything.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Cross-platform** | Same API on Linux, Windows, Android |
| **UTF-8 everywhere** | UTF-8 on POSIX, automatically converted to UTF-16 on Windows |
| **Atomic write** | Crash-safe file saving (temp + rename) |
| **200+ extension recognition** | 25+ categories like `is_image()`, `is_video()`, `is_ai_model()` |
| **Binary-safe** | Embedded `\0` bytes preserved |
| **Pool allocator** | Fast reads/writes with `knst_pool_allocator` |
| **Path utilities** | `join_path`, `normalize_path`, `get_parent_dir` |
| **Time formatting** | Absolute + relative ("2h ago") |
| **Windows reserved names** | Automatic check for `CON`, `PRN`, `AUX` |
| **Symlink support** | Works on Windows with Developer Mode |

---

## 🚀 Quick Start

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // Write file
    knst_file::write_file_text(u"hello.txt", u"Hello World!");

    // Read
    knst_c16string content = knst_file::read_file_text(u"hello.txt");
    std::cout << content << "\n";   // "Hello World!"

    // Metadata
    knst_file f = knst_file::open(u"hello.txt");
    std::cout << "Size: " << f.size << " bytes\n";
    std::cout << "Extension: " << f.extension << "\n";
    std::cout << "Modified: " << f.get_modified_time_string() << "\n";

    // Delete
    knst_file::remove_file(u"hello.txt");
    return 0;
}
```

---

## 📋 Overview

| Category | Methods |
|---|---|
| **Path operations** | `join_path`, `normalize_path`, `get_parent_dir`, `get_filename`, `get_extension`, `get_stem`, `is_absolute_path` |
| **Well-known directories** | `get_current_dir`, `get_home_dir`, `get_temp_dir`, `get_executable_dir` |
| **Existence checks** | `path_exists`, `file_exists`, `dir_exists`, `symlink_exists` |
| **Reading** | `read_file_text`, `read_file_data`, `read_file_lines` |
| **Writing** | `write_file_text`, `write_file_data`, `write_file_atomic`, `append_file_text` |
| **Directory operations** | `create_directory`, `create_directories`, `list_directory`, `list_directory_recursive` |
| **Copying** | `copy_file`, `copy_directory`, `rename`, `move_path` |
| **Deleting** | `remove_file`, `remove_directory`, `remove_all` |
| **Metadata** | `get_file_size`, `get_disk_space`, `set_file_times` |
| **Extension classification** | `is_image`, `is_video`, `is_audio`, `is_code`, `is_ai_model`, ... (25+ categories) |
| **Validity** | `is_valid_filename`, `sanitize_filename` |
| **Symlink** | `create_symlink`, `read_symlink_target` |

---

## 🛤️ 1) Path Operations

Pure string operations — they do not touch the file system.

```cpp
knst_c16string p = u"/home/user/docs/report.tar.gz";

knst_file::get_filename(p);        // "report.tar.gz"
knst_file::get_directory(p);       // "/home/user/docs/"
knst_file::get_extension(p);       // ".gz"
knst_file::get_full_extension(p);  // ".tar.gz"
knst_file::get_stem(p);            // "report.tar"
knst_file::get_parent_dir(p);      // "/home/user"
```

### `join_path` — Safe Joining

```cpp
auto p = knst_file::join_path(u"/home/user", u"docs/file.txt");
// "/home/user/docs/file.txt"

// Leading/trailing separators are fixed automatically
auto q = knst_file::join_path(u"/home/user/", u"/docs/file.txt");
// "/home/user/docs/file.txt"
```

### `normalize_path` — Resolving `..` and `.`

```cpp
knst_c16string messy = u"/home/user/../user/./docs//file.txt";
std::cout << knst_file::normalize_path(messy);
// "/home/user/docs/file.txt"
```

### `is_absolute_path`

```cpp
knst_file::is_absolute_path(u"/etc/hosts");            // Linux: true
knst_file::is_absolute_path(u"C:\\Windows\\System32"); // Windows: true
knst_file::is_absolute_path(u"docs/file.txt");         // false
```

---

## 📁 2) Well-Known Directories

```cpp
knst_file::get_current_dir();     // "/home/knst/Desktop"
knst_file::get_home_dir();        // "/home/knst" (Win: "C:\Users\knst")
knst_file::get_temp_dir();        // "/tmp" (Win: "C:\Users\...\Temp")
knst_file::get_executable_dir();  // Directory of the running executable
```

**Note:** On Windows, `get_executable_dir()` uses `GetModuleFileNameW`. On Linux, it reads the `/proc/self/exe` symlink.

---

## 📂 3) Existence Checks

```cpp
knst_file::path_exists(u"/etc/hosts");             // anything there?
knst_file::file_exists(u"/etc/hosts");             // is it a regular file?
knst_file::dir_exists(u"/etc");                    // is it a directory?
knst_file::symlink_exists(u"/usr/lib/libc.so.6");  // is it a symlink?
```

---

## 📖 4) Reading

### `read_file_text` — UTF-8 Text

```cpp
knst_c16string content = knst_file::read_file_text(u"data.txt");
std::cout << content << "\n";
```

**Features:**
- Automatic UTF-8 → UTF-16 conversion
- BOM (byte-order mark) is skipped automatically
- Returns an empty string on error

### `read_file_data` — Raw Bytes

```cpp
knst_byte_string data = knst_file::read_file_data<>(u"image.png");
std::cout << "Size: " << data.length() << " bytes\n";
```

**Binary-safe:** Embedded `\0` bytes are preserved.

### `read_file_lines` — Line by Line

```cpp
knst_vector<knst_c16string> lines;
knst_file f = knst_file::open(u"data.txt");
f.read_file_lines(lines);

for (const auto& line : lines) {
    std::cout << line << "\n";
}
```

**Features:**
- Recognizes both `\n` and `\r\n`
- BOM is skipped automatically
- Empty lines are preserved

---

## ✍️ 5) Writing

### `write_file_text` — UTF-16 → UTF-8

```cpp
knst_file::write_file_text(u"note.txt", u"Hello world 🌍");
```

### `write_file_data` — Raw Bytes

```cpp
unsigned char blob[] = { 0x00, 0x11, 0x22, 0xFF };
knst_file::write_file_data(u"data.bin", blob, sizeof(blob));

// or
knst_byte_string bytes = /* ... */;
knst_file::write_file_data(u"data.bin", bytes);
```

### `append_file_text` / `append_file_data`

```cpp
knst_file::append_file_text(u"log.txt", u"new line\n");
knst_file::append_file_text(u"log.txt", u"synced write\n", /*sync=*/true);
```

**`sync=true`** → calls `fsync()` / `FlushFileBuffers()` (slow but safe).

### 🌟 `write_file_atomic` — Crash-Safe Writing

```cpp
knst_file::write_file_atomic_text(u"config.json", u"{ \"version\": 2 }");
```

**How it works:**
1. A temporary file is created, e.g. `config.json.knsttmp_12345_678`
2. Data is written + `fsync`
3. `rename()` moves it to the real file (atomic)
4. On POSIX, the directory is also fsync'ed

**Why does this matter?** If power is cut during write, the old file remains intact.

---

## 📋 6) Directory Operations

### Creating

```cpp
knst_file::create_directory(u"new_dir");
knst_file::create_directories(u"a/b/c/d/e");   // recursive
```

### Listing

```cpp
knst_vector<knst_file> entries;
knst_file::list_directory(u"/home/user", entries);

for (const auto& e : entries) {
    std::cout << e.name 
              << "  (" << (e.is_directory ? "dir" : "file") << ")"
              << "  " << e.size << " bytes\n";
}
```

### Filtered Listing

```cpp
knst_vector<knst_file> txt_files;
knst_file::list_directory(u"/home/user", txt_files,
    [](const knst_file& f) {
        return f.has_extension(u".txt");
    });
```

### Recursive Listing

```cpp
knst_vector<knst_file> all;
knst_file::list_directory_recursive(u"/home/user/project", all);

// Only up to depth 2
knst_file::list_directory_recursive(u"/home/user/project", all, 2);

// Follow symlinks
knst_file::list_directory_recursive(u"/home/user/project", all, -1, true);
```

---

## 🗑️ 7) Deleting

```cpp
knst_file::remove_file(u"file.txt");            // single file
knst_file::remove_directory(u"empty_dir");      // empty directory
knst_file::remove_all(u"dir");                  // recursive (even if not empty)
```

**Is `remove_all` safe?** Yes — it first lists all contents and then deletes in reverse order (deep → shallow).

---

## 📋 8) Copying and Moving

### Copying

```cpp
// Single file
knst_file::copy_file(u"source.txt", u"target.txt", true);  // overwrite=true

// Directory (recursive)
knst_file::copy_directory(u"src_dir", u"dst_dir", true);
```

**Note:** On POSIX, `copy_file` reads in 64 KB chunks — memory-friendly for large files.

### Renaming

```cpp
knst_file::rename(u"old.txt", u"new.txt");
```

### Moving

```cpp
knst_file::move_path(u"src.txt", u"dst.txt", true);
```

**How it works:**
1. First tries `rename()` (fast)
2. If it fails (different disk) → copy + delete
3. If overwrite is requested, the target is deleted first

---

## 🔗 9) Symlink

```cpp
// Create
knst_file::create_symlink(u"/etc/hosts", u"hosts_link");

// Read the target
knst_c16string target = knst_file::read_symlink_target(u"hosts_link");
std::cout << target << "\n";   // "/etc/hosts"

// Check
knst_file f = knst_file::open(u"hosts_link");
if (f.is_symlink) {
    std::cout << "This is a symlink\n";
}
```

**Windows Note:** Creating a symlink requires **Developer Mode** or **admin**. The library automatically tries the `SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE` flag.

---

## 💾 10) Disk Space

```cpp
uint64_t free_bytes = 0, total_bytes = 0;
if (knst_file::get_disk_space(u"/home", free_bytes, total_bytes)) {
    std::cout << "Total: " << total_bytes / (1024ULL*1024*1024) << " GB\n";
    std::cout << "Free : " << free_bytes  / (1024ULL*1024*1024) << " GB\n";
}
```

**Platform differences:**
- **Linux:** `statvfs()`
- **Windows:** `GetDiskFreeSpaceExW()`

---

## ⏰ 11) Timestamps

```cpp
knst_file f = knst_file::open(u"data.txt");

// Absolute — "2025-01-15 14:23:45"
f.get_created_time_string();
f.get_modified_time_string();
f.get_accessed_time_string();

// Relative — "2h ago", "3d ago", "just now"
f.get_created_time_relative();
f.get_modified_time_relative();
f.get_accessed_time_relative();
```

**Note:** On POSIX, `created_time` is actually `ctime` (change time) — not a true birth time. On Windows it is the real creation time.

### Setting Time

```cpp
uint64_t now = static_cast<uint64_t>(std::time(nullptr));
knst_file::set_file_times(u"data.txt", now /*modified*/, now /*accessed*/);
```

---

## 🗂️ 12) Extension Classification (200+ Recognitions)

This is the **strongest point** of the library. It recognizes 200+ extensions and splits them into 25+ categories.

### Usage

```cpp
knst_file f = knst_file::open(u"photo.png");

if (f.is_image())   std::cout << "Image\n";
if (f.is_video())   std::cout << "Video\n";
if (f.is_audio())   std::cout << "Audio\n";
if (f.is_code())    std::cout << "Code\n";
// ...
```

### All Categories

| Category | What it recognizes | Example extensions |
|---|---|---|
| **`is_image()`** | Image formats | `.png`, `.jpg`, `.webp`, `.svg`, `.dds`, `.ktx2` |
| **`is_video()`** | Video formats | `.mp4`, `.mkv`, `.webm`, `.mov`, `.avi` |
| **`is_audio()`** | Audio formats | `.mp3`, `.flac`, `.opus`, `.m4a`, `.wav` |
| **`is_text()`** | Plain text | `.txt`, `.md`, `.log`, `.csv` |
| **`is_document()`** | Office documents | `.docx`, `.pdf`, `.xlsx`, `.pptx` |
| **`is_code()`** | Source code | `.cpp`, `.py`, `.rs`, `.js`, `.go`, 100+ |
| **`is_script()`** | Scripts | `.sh`, `.bat`, `.ps1`, `.vbs` |
| **`is_shader()`** | Shaders | `.glsl`, `.hlsl`, `.frag`, `.wgsl` |
| **`is_archive()`** | Archives | `.zip`, `.tar.gz`, `.7z`, `.apk` |
| **`is_executable_extension()`** | Executables | `.exe`, `.so`, `.dll`, `.deb`, `.rpm` |
| **`is_font()`** | Fonts | `.ttf`, `.otf`, `.woff`, `.woff2` |
| **`is_model()`** | 3D models | `.obj`, `.fbx`, `.gltf`, `.stl` |
| **`is_web()`** | Web | `.html`, `.css`, `.vue`, `.svelte` |
| **`is_config()`** | Configuration | `.json`, `.yaml`, `.toml`, `.ini` |
| **`is_database()`** | Databases | `.db`, `.sqlite`, `.mdb` |
| **`is_ebook()`** | E-books | `.epub`, `.mobi`, `.azw3`, `.cbz` |
| **`is_subtitle()`** | Subtitles | `.srt`, `.vtt`, `.ass`, `.ssa` |
| **`is_data()`** | Data | `.parquet`, `.avro`, `.npy`, `.pkl` |
| **`is_build()`** | Build | `.cmake`, `.ninja`, `.gradle` |
| **`is_scientific()`** | Scientific | `.hdf5`, `.fits`, `.dcm`, `.fasta` |
| **`is_disk_image()`** | Disk images | `.iso`, `.img`, `.vhd`, `.qcow2` |
| **`is_ai_model()`** | AI models | `.pt`, `.onnx`, `.gguf`, `.safetensors` |
| **`is_game_asset()`** | Game assets | `.unity`, `.uasset`, `.pak`, `.bsp` |
| **`is_cad()`** | CAD | `.dwg`, `.dxf`, `.step`, `.sldprt` |

### Extension Check

```cpp
f.has_extension(u".txt");               // single extension
f.has_extension(u".TXT", false);        // case-insensitive (default)
f.has_extension(u".TXT", true);         // case-sensitive
f.has_full_extension(u".tar.gz");       // double extension
f.has_any_extension({u".txt", u".md"}); // multiple
```

**Performance:** Extension comparison is done in **O(1) via bit-pack** up to 8 bytes. So you can filter a thousand files per second.

---

## ✅ 13) Filename Validation

```cpp
knst_file::is_valid_filename(u"normal.txt");      // true
knst_file::is_valid_filename(u"inva/lid.txt");    // false (Linux: /, Win: /)
knst_file::is_valid_filename(u"with:colon.txt");  // false (Windows)
knst_file::is_valid_filename(u"CON");             // false (Windows reserved)
knst_file::is_valid_filename(u"file.");           // false (Windows trailing dot)
```

### Sanitization

```cpp
knst_c16string bad = u"my:file?name*.txt";
knst_c16string safe = knst_file::sanitize_filename(bad);
// "my_file_name_.txt"

// Replacement character is configurable
knst_c16string safe2 = knst_file::sanitize_filename(bad, u'-');
// "my-file-name-.txt"
```

**Windows reserved names:** `CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9`.

---

## 🧠 14) Fast Reading with the Pool Allocator

```cpp
// Default allocator (malloc/free)
auto data = knst_file::read_file_data<>(u"data.bin");

// Pool allocator — fast for many small reads
auto pool_data = knst_file::read_file_data<knst_pool_allocator>(u"data.bin");

// Text
knst_c16string text = knst_file::read_file_text<knst_pool_allocator>(u"data.txt");

// Lines
knst_vector<knst_c16string> lines;
knst_file f = knst_file::open(u"data.txt");
f.read_file_lines<knst_pool_allocator>(lines);
```

### When Should You Use the Pool?

| Situation | Use pool? |
|---|---|
| Reading occasional files | ❌ Unnecessary |
| Reading thousands of small files (log parser) | ✅ Yes, big win |
| Short strings (config, JSON key) | ✅ Yes |
| Large binary files (100 MB+) | ❌ malloc is better |

---

## 📊 15) `knst_file` Class (Instance)

Besides the static methods there is also an **instance API**. It caches metadata.

### Opening and Metadata

```cpp
knst_file f = knst_file::open(u"data.txt");

std::cout << f.path          << "\n";  // full path
std::cout << f.name          << "\n";  // "data.txt"
std::cout << f.extension     << "\n";  // ".txt"
std::cout << f.full_extension<< "\n";  // ".tar.gz" if present
std::cout << f.parent_dir    << "\n";  // "/home/user/"

std::cout << f.size          << "\n";  // bytes
std::cout << f.get_size_string() << "\n"; // "1.5 MB"

std::cout << (f.exists        ? "exists" : "missing") << "\n";
std::cout << (f.is_file       ? "file" : "") << "\n";
std::cout << (f.is_directory  ? "dir" : "") << "\n";
std::cout << (f.is_symlink    ? "symlink" : "") << "\n";
std::cout << (f.is_hidden     ? "hidden" : "") << "\n";
std::cout << (f.is_readonly   ? "readonly" : "") << "\n";
std::cout << (f.is_executable ? "executable" : "") << "\n";

std::cout << f.get_modified_time_string()   << "\n";
std::cout << f.get_modified_time_relative() << "\n";  // "2h ago"
```

### Instance Methods

```cpp
knst_file f = knst_file::open(u"data.txt");

// Reading
knst_c16string text = f.load();
knst_vector<knst_c16string> lines;
f.load_lines(lines);
knst_byte_string bytes = f.read_file_data<>();

// Writing
f.save(u"new content");
f.save_atomic(u"safe write");
f.save_lines({u"line1", u"line2"});
f.write_data(bytes);
f.append_text(u"extra text");
f.append_line(u"new line");

// Copy / Move
f.copy_to(u"copy.txt", true);
f.move_to(u"new_place.txt", true);
f.rename_to(u"new_name.txt");

// Delete
f.remove();         // single file/dir
f.remove_all();     // recursive

// Info
f.refresh();        // refresh metadata
f.reset();          // reset all info
```

### Factory Methods

```cpp
// Open or create
knst_file f1 = knst_file::open_or_create(u"new.txt");

// Create directly (with content)
knst_file f2 = knst_file::create_text(u"note.txt", u"hello");
knst_file f3 = knst_file::create_lines(u"list.txt", {u"a", u"b"});
knst_file f4 = knst_file::create_dir(u"new_dir");
knst_file f5 = knst_file::create_dirs(u"a/b/c");
```

---

## 🛡️ 16) Error Handling

Every method returns `false` or empty on error. But for more detailed error info:

```cpp
knst_file_error err;
auto data = knst_file::read_file_data<>(u"data.bin", &err);

if (err != knst_file_error::None) {
    std::cout << "Error: " << knst_file_error_string(err) << "\n";
}
```

### Error Enum

```cpp
enum class knst_file_error : uint8_t {
    None = 0,
    NotFound,          // "File not found"
    PermissionDenied,  // "Permission denied"
    IsDirectory,       // "Path is a directory"
    TooLarge,          // "File too large"
    OutOfMemory,       // "Out of memory"
    ReadError,         // "Read error"
    InvalidPath,       // "Invalid path"
    SymlinkLoop,       // "Symlink loop detected"
    NotAFile,          // "Not a regular file"
    AlreadyExists,     // "Already exists"
    NotEmpty,          // "Directory not empty"
    WriteError,        // "Write error"
    DiskFull,          // "Disk full"
    TempFileError,     // "Temporary file error"
    Unknown            // "Unknown error"
};
```

### Instance Error

```cpp
knst_file f = knst_file::open(u"missing.txt");
if (f.error != knst_file_error::None) {
    std::cout << f.error_string() << "\n";  // "File not found"
}
```

---

## 🌍 17) Platform Differences

| Topic | Linux (POSIX) | Windows |
|---|---|---|
| **Path separator** | `/` | `\` (also accepts `/`) |
| **Absolute path** | `/etc/hosts` | `C:\...`, `\\server\share` |
| **Case sensitivity** | Case-sensitive (ext4) | Case-insensitive (NTFS) |
| **Hidden file** | starts with `.` | `FILE_ATTRIBUTE_HIDDEN` flag |
| **Executable bit** | `x` permission | None — `.exe` extension |
| **Symlink** | Always allowed | Requires Developer Mode/admin |
| **Reserved names** | None | `CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9` |
| **Forbidden chars** | `/`, control characters | `<>:"/\|?*` |
| **Path length** | `PATH_MAX` (4096) | `MAX_PATH` (260) — `\\?\` prefix for long paths |

### Long Path Support (Windows)

```cpp
// On Windows, for paths longer than 260 characters:
knst_c16string long_path = u"\\\\?\\C:\\very\\long\\path\\...\\file.txt";
knst_file::write_file_text(long_path, u"data");
```

The library recognizes the `\\?\` prefix automatically.

---

## 🔥 18) Real-World Examples

### Example 1: Simple Log Writing

```cpp
void log_message(const knst_c16string& msg) {
    knst_c16string log_path = u"app.log";
    
    knst_c16string line = u"[";
    line.append(knst_file::open(log_path).get_modified_time_string());
    line.append(u"] ");
    line.append(msg);
    line.append(u"\n");
    
    knst_file::append_file_text(log_path, line, /*sync=*/true);
}
```

### Example 2: Reading a Config File

```cpp
struct Config {
    knst_c16string version;
    int max_threads = 4;
};

Config load_config(const knst_c16string& path) {
    Config cfg;
    
    knst_file f = knst_file::open(path);
    if (!f.exists) {
        return cfg;  // defaults
    }
    
    // Simple key=value parsing
    knst_vector<knst_c16string> lines;
    f.load_lines(lines);
    
    for (const auto& line : lines) {
        if (line.starts_with(u"version=")) {
            cfg.version = line.substr(8, line.length() - 8);
        }
        // ...
    }
    return cfg;
}
```

### Example 3: Recursive Directory Scanning

```cpp
// Find all .cpp files in a project
knst_vector<knst_file> cpp_files;
knst_file::list_directory_recursive(u"/home/user/project", cpp_files,
    [](const knst_file& f) {
        return f.is_code() && f.has_extension(u".cpp");
    });

std::cout << "Total " << cpp_files.size() << " .cpp files\n";

// Count lines
uint64_t total_lines = 0;
for (const auto& f : cpp_files) {
    knst_vector<knst_c16string> lines;
    knst_file file = knst_file::open(f.path);
    file.load_lines(lines);
    total_lines += lines.size();
}
std::cout << "Total lines: " << total_lines << "\n";
```

### Example 4: Crash-Safe Config Saving

```cpp
void save_config(const knst_c16string& path, const Config& cfg) {
    // Build JSON-like content
    knst_c16string content = u"{\n";
    content.append(u"  \"version\": \"");
    content.append(cfg.version);
    content.append(u"\",\n");
    content.append(u"  \"max_threads\": ");
    content.append(cfg.max_threads);
    content.append(u"\n}\n");

    // Atomic write — won't corrupt even on power loss
    knst_file::write_file_atomic_text(path, content);
}
```

### Example 5: File Type Analyzer

```cpp
void analyze_directory(const knst_c16string& dir) {
    knst_vector<knst_file> all;
    knst_file::list_directory(dir, all);

    uint32_t images = 0, videos = 0, code = 0, audio = 0, other = 0;

    for (const auto& f : all) {
        if (f.is_directory) continue;
        
        if (f.is_image())        images++;
        else if (f.is_video())   videos++;
        else if (f.is_audio())   audio++;
        else if (f.is_code())    code++;
        else                     other++;
    }

    std::cout << "Images : " << images << "\n";
    std::cout << "Videos : " << videos << "\n";
    std::cout << "Audio  : " << audio << "\n";
    std::cout << "Code   : " << code << "\n";
    std::cout << "Other  : " << other << "\n";
}
```

### Example 6: Safe Filename Sanitization

```cpp
knst_c16string user_input = u"user://input*file?name.txt";
knst_c16string safe_name = knst_file::sanitize_filename(user_input);

if (!knst_file::is_valid_filename(safe_name)) {
    std::cerr << "Still invalid!\n";
    return;
}

knst_c16string full_path = knst_file::join_path(u"/home/knst/files", safe_name);
knst_file::write_file_text(full_path, u"content");
```

---

## 📊 19) Performance Notes

| Operation | Complexity | Note |
|---|---|---|
| `read_file_text` | O(n) | UTF-8 → UTF-16 conversion |
| `read_file_data` | O(n) | 64 KB chunks (large-file friendly) |
| `write_file_atomic` | O(n) + fsync | Safe but slow |
| `list_directory` | O(entries) | One level only |
| `list_directory_recursive` | O(total_entries) | Recursive |
| `has_extension` | **O(1)** | Bit-pack up to 8 bytes |
| `is_image`, `is_video`... | **O(1)** | Compile-time hash switch |
| `normalize_path` | O(n) | Single pass |

### Caching Tip

```cpp
// ❌ Slow: reads from disk every time
for (int i = 0; i < 100; ++i) {
    auto content = knst_file::read_file_text(u"config.txt");
    // ...
}

// ✅ Fast: read once, keep in memory
auto content = knst_file::read_file_text(u"config.txt");
for (int i = 0; i < 100; ++i) {
    // use content
}
```

### Pool Allocator Tip

```cpp
// Use pool if you read hundreds of small files
for (int i = 0; i < 1000; ++i) {
    auto data = knst_file::read_file_data<knst_pool_allocator>(paths[i]);
    // ...
}
```

---

## 🎯 What It Buys You

`knst_file` provides serious advantage in these situations:

- ✅ **Cross-platform application** — same code on Windows + Linux
- ✅ **File type analysis** — 200+ extension recognition ready
- ✅ **Config file management** — atomic write support
- ✅ **Log system** — append + sync support
- ✅ **File browser / organizer** — recursive listing + filtering
- ✅ **Backup tool** — copy_directory + disk space
- ✅ **Compatible with other KernelNucleusT modules** (`knst_c16string`, `knst_byte_string`)