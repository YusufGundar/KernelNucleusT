// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
  knst_file_basic.cpp

  Basic usage of knst_file — a cross-platform filesystem library.

  Shows: path utilities, well-known directories, file metadata,
  text/binary I/O, line-based reading, atomic writes, directory
  listing, file type classification, copy/move/rename, symlinks,
  disk space, time formatting, filename validation, and the pool
  allocator variant.

  Section 18 highlights the Windows-vs-POSIX differences explicitly.

  Everything runs inside a sandbox in the system temp directory.
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>
#include <algorithm>


static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}


static const char* platform_name() {
#if KNST_USING_PLATFORM_WINDOWS
    return "Windows";
#else
    return "POSIX";
#endif
}


int main() {

    std::cout << "Platform: " << platform_name() << "\n";

    // =====================================================================
    section("1) Path utilities — pure string operations");
    // =====================================================================
    {
        knst_c16string p = u"/home/user/docs/report.tar.gz";

        std::cout << "path       : " << p << "\n";
        std::cout << "filename   : " << knst_file::get_filename(p)   << "\n";
        std::cout << "directory  : " << knst_file::get_directory(p)  << "\n";
        std::cout << "extension  : " << knst_file::get_extension(p)  << "\n";
        std::cout << "stem       : " << knst_file::get_stem(p)       << "\n";
        std::cout << "parent_dir : " << knst_file::get_parent_dir(p) << "\n";

        knst_c16string joined = knst_file::join_path(u"/home/user", u"docs/file.txt");
        std::cout << "join_path  : " << joined << "\n";

        knst_c16string messy = u"/home/user/../user/./docs//file.txt";
        std::cout << "normalize  : " << knst_file::normalize_path(messy) << "\n";
    }

    // =====================================================================
    section("2) Well-known directories");
    // =====================================================================
    {
        std::cout << "current dir : " << knst_file::get_current_dir()    << "\n";
        std::cout << "home dir    : " << knst_file::get_home_dir()       << "\n";
        std::cout << "temp dir    : " << knst_file::get_temp_dir()       << "\n";
        std::cout << "executable  : " << knst_file::get_executable_dir() << "\n";

        knst_c16string posix_abs = u"/etc/hosts";
        #if KNST_USING_PLATFORM_WINDOWS
            knst_c16string win_abs = u"C:\\Windows\\System32";
            std::cout << "is_absolute(/etc/hosts)        : " << (knst_file::is_absolute_path(posix_abs) ? "yes" : "no") << "\n";
            std::cout << "is_absolute(C:\\Windows\\...)   : " << (knst_file::is_absolute_path(win_abs)  ? "yes" : "no") << "\n";
        #else
            std::cout << "is_absolute(/etc/hosts)        : " << (knst_file::is_absolute_path(posix_abs) ? "yes" : "no") << "\n";
            std::cout << "is_absolute(C:\\Windows\\...)   : (POSIX — not applicable)\n";
        #endif
    }

    // =====================================================================
    section("3) Prepare a sandbox in the temp dir");
    // =====================================================================
    knst_c16string sandbox = knst_file::join_path(
        knst_file::get_temp_dir(), u"knst_file_demo");

    knst_file::remove_all(sandbox);
    knst_file::create_directories(sandbox);

    std::cout << "sandbox : " << sandbox << "\n";
    std::cout << "exists  : " << (knst_file::dir_exists(sandbox) ? "yes" : "no") << "\n";

    // =====================================================================
    section("4) File metadata via knst_file::open()");
    // =====================================================================
    {
        knst_c16string readme = knst_file::join_path(sandbox, u"readme.txt");
        knst_file::write_file_text(readme, u"Hello, knst_file!\n");

        knst_file f = knst_file::open(readme);

        std::cout << "path        : " << f.path      << "\n";
        std::cout << "name        : " << f.name      << "\n";
        std::cout << "extension   : " << f.extension << "\n";
        std::cout << "size        : " << f.size << " bytes\n";
        std::cout << "size string : " << f.get_size_string() << "\n";
        std::cout << "exists      : " << (f.exists    ? "yes" : "no") << "\n";
        std::cout << "is_file     : " << (f.is_file   ? "yes" : "no") << "\n";
        std::cout << "is_readonly : " << (f.is_readonly ? "yes" : "no") << "\n";
        std::cout << "is_hidden   : " << (f.is_hidden ? "yes" : "no") << "\n";
        std::cout << "is_exec     : " << (f.is_executable ? "yes" : "no")
                  << "  (Linux: file permission bit, Windows: N/A)\n";
        std::cout << "modified    : " << f.get_modified_time_string()   << "\n";
        std::cout << "relative    : " << f.get_modified_time_relative() << "\n";
    }

    // =====================================================================
    section("5) Reading and writing text (UTF-8 round-trip)");
    // =====================================================================
    {
        knst_c16string p = knst_file::join_path(sandbox, u"note.txt");

        knst_file::write_file_text(p, u"Merhaba dünya 🌍");
        std::cout << "round-trip   : " << knst_file::read_file_text(p) << "\n";

        knst_file f = knst_file::open(p);
        std::cout << "via instance : " << f.load() << "\n";
    }

    // =====================================================================
    section("6) Line-based I/O");
    // =====================================================================
    {
        knst_c16string p = knst_file::join_path(sandbox, u"lines.txt");

        knst_vector<knst_c16string> lines;
        lines.push_back(u"first line");
        lines.push_back(u"second line");
        lines.push_back(u"third line");

        knst_file f;
        f.read_file(p);
        f.write_lines(lines);

        knst_vector<knst_c16string> loaded;
        f.read_file_lines(loaded);

        std::cout << "written lines : " << lines.size()  << "\n";
        std::cout << "loaded lines  : " << loaded.size() << "\n";
        for (uint32_t i = 0; i < loaded.size(); ++i) {
            std::cout << "  [" << i << "] " << loaded[i] << "\n";
        }

        f.append_line(u"fourth line");
        f.read_file_lines(loaded);
        std::cout << "after append  : " << loaded.size() << " lines\n";
    }

    // =====================================================================
    section("7) Binary data with embedded nulls");
    // =====================================================================
    {
        knst_c16string p = knst_file::join_path(sandbox, u"data.bin");

        unsigned char blob[] = { 0x00, 0x11, 0x22, 0x33, 0x00, 0xFF };
        knst_file::write_file_data(p, blob, sizeof(blob));

        knst_byte_string read_back = knst_file::read_file_data<>(p);

        std::cout << "wrote : " << sizeof(blob)      << " bytes\n";
        std::cout << "read  : " << read_back.length() << " bytes\n";
        std::cout << "hex   : ";
        for (uint32_t i = 0; i < read_back.length(); ++i) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02X ", static_cast<unsigned>(read_back[i]));
            std::cout << buf;
        }
        std::cout << "\n";
    }

    // =====================================================================
    section("8) Atomic write — crash-safe save");
    // =====================================================================
    {
        knst_c16string p = knst_file::join_path(sandbox, u"atomic.txt");

        knst_file::write_file_atomic_text(p, u"version 1");
        std::cout << "first  : " << knst_file::read_file_text(p) << "\n";

        knst_file::write_file_atomic_text(p, u"version 2");
        std::cout << "second : " << knst_file::read_file_text(p) << "\n";
    }

    // =====================================================================
    section("9) Directory listing");
    // =====================================================================
    {
        knst_file::write_file_text(knst_file::join_path(sandbox, u"a.txt"),     u"a");
        knst_file::write_file_text(knst_file::join_path(sandbox, u"b.md"),      u"b");
        knst_file::write_file_text(knst_file::join_path(sandbox, u"c.cpp"),     u"c");
        knst_file::write_file_text(knst_file::join_path(sandbox, u"image.png"), u"fake");
        knst_file::create_directory  (knst_file::join_path(sandbox, u"subdir"));

        knst_vector<knst_file> entries;
        knst_file::list_directory(sandbox, entries);

        std::sort(entries.begin(), entries.end(),
                  [](const knst_file& a, const knst_file& b) {
                      return a.name < b.name;
                  });

        std::cout << "entries : " << entries.size() << "\n";
        for (uint32_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            std::cout << "  " << e.name << "  size=" << e.size
                      << "  " << (e.is_directory ? "[dir]" : "[file]")
                      << "  ext=" << e.extension << "\n";
        }
    }

    // =====================================================================
    section("10) Filtered listing — only .txt");
    // =====================================================================
    {
        knst_vector<knst_file> txt_files;
        knst_file::list_directory(sandbox, txt_files, [](const knst_file& f) {
            return f.has_extension(u".txt");
        });

        std::sort(txt_files.begin(), txt_files.end(),
                  [](const knst_file& a, const knst_file& b) {
                      return a.name < b.name;
                  });

        std::cout << ".txt files : " << txt_files.size() << "\n";
        for (uint32_t i = 0; i < txt_files.size(); ++i) {
            std::cout << "  " << txt_files[i].name << "\n";
        }
    }

    // =====================================================================
    section("11) Recursive listing");
    // =====================================================================
    {
        knst_c16string deep = knst_file::join_path(sandbox, u"subdir/deep/nested");
        knst_file::create_directories(deep);
        knst_file::write_file_text(knst_file::join_path(deep, u"leaf.txt"), u"leaf");

        knst_vector<knst_file> all;
        knst_file::list_directory_recursive(sandbox, all);

        std::sort(all.begin(), all.end(),
                  [](const knst_file& a, const knst_file& b) {
                      return a.path < b.path;
                  });

        std::cout << "recursive count : " << all.size() << "\n";
        for (uint32_t i = 0; i < all.size() && i < 8; ++i) {
            std::cout << "  " << all[i].path << "\n";
        }
        if (all.size() > 8) std::cout << "  ... " << (all.size() - 8) << " more\n";
    }

    // =====================================================================
    section("12) File type classification");
    // =====================================================================
    {
        knst_c16string samples[] = {
            u"photo.png", u"movie.mp4", u"music.mp3", u"script.py",
            u"page.html", u"config.json", u"archive.tar.gz", u"library.so",
            u"font.ttf", u"model.gltf", u"book.epub", u"subtitle.srt"
        };

        for (const auto& name : samples) {
            knst_file f;
            f.read_file(knst_file::join_path(sandbox, name));

            std::cout << "  " << name << "  ->";
            if (f.is_image())                 std::cout << " image";
            if (f.is_video())                 std::cout << " video";
            if (f.is_audio())                 std::cout << " audio";
            if (f.is_code())                  std::cout << " code";
            if (f.is_web())                   std::cout << " web";
            if (f.is_config())                std::cout << " config";
            if (f.is_archive())               std::cout << " archive";
            if (f.is_executable_extension())  std::cout << " executable";
            if (f.is_font())                  std::cout << " font";
            if (f.is_model())                 std::cout << " 3D-model";
            if (f.is_ebook())                 std::cout << " ebook";
            if (f.is_subtitle())              std::cout << " subtitle";
            std::cout << "\n";
        }
    }

    // =====================================================================
    section("13) Copy / rename / move");
    // =====================================================================
    {
        knst_c16string src = knst_file::join_path(sandbox, u"source.txt");
        knst_file::write_file_text(src, u"original content");

        knst_c16string dst = knst_file::join_path(sandbox, u"copy.txt");
        knst_file::copy_file(src, dst, true);
        std::cout << "copied        : " << knst_file::read_file_text(dst) << "\n";

        knst_c16string renamed = knst_file::join_path(sandbox, u"renamed.txt");
        knst_file::rename(dst, renamed);
        std::cout << "renamed exists: "
                  << (knst_file::file_exists(renamed) ? "yes" : "no") << "\n";

        knst_c16string moved = knst_file::join_path(sandbox, u"moved.txt");
        knst_file::move_path(renamed, moved, true);
        std::cout << "moved content : " << knst_file::read_file_text(moved) << "\n";
    }

    // =====================================================================
    section("14) Symlinks");
    // =====================================================================
    {
        knst_c16string target = knst_file::join_path(sandbox, u"note.txt");
        knst_c16string link   = knst_file::join_path(sandbox, u"note_link.txt");

        if (knst_file::create_symlink(target, link, false)) {
            std::cout << "symlink target : "
                      << knst_file::read_symlink_target(link) << "\n";

            knst_file lf;
            lf.read_file(link);
            std::cout << "is_symlink     : "
                      << (lf.is_symlink ? "yes" : "no") << "\n";
        } else {
            std::cout << "symlink creation failed\n";
            #if KNST_USING_PLATFORM_WINDOWS
                std::cout << "(Windows: enable Developer Mode or run as admin)\n";
            #else
                std::cout << "(POSIX: check permissions on the target dir)\n";
            #endif
        }
    }

    // =====================================================================
    section("15) Disk space");
    // =====================================================================
    {
        uint64_t free_bytes = 0, total_bytes = 0;
        if (knst_file::get_disk_space(sandbox, free_bytes, total_bytes)) {
            std::cout << "total : " << (total_bytes / (1024ULL * 1024 * 1024)) << " GB\n";
            std::cout << "free  : " << (free_bytes  / (1024ULL * 1024 * 1024)) << " GB\n";
        } else {
            std::cout << "disk space query failed\n";
        }
    }

    // =====================================================================
    section("16) Time formatting");
    // =====================================================================
    {
        knst_file f = knst_file::open(knst_file::join_path(sandbox, u"note.txt"));

        std::cout << "created  : " << f.get_created_time_string()    << "\n";
        std::cout << "modified : " << f.get_modified_time_string()   << "\n";
        std::cout << "accessed : " << f.get_accessed_time_string()   << "\n";
        std::cout << "rel mtime: " << f.get_modified_time_relative() << "\n";
        std::cout << "(POSIX: 'created' = ctime, not birth-time)\n";
    }

    // =====================================================================
    section("17) Filename validation & sanitization");
    // =====================================================================
    {
        knst_c16string tests[] = {
            u"valid_name.txt",
            u"inva/lid.txt",
            u"with:colon.txt",
            u"CON",
            u"normal file.md"
        };

        for (const auto& name : tests) {
            std::cout << "  \"" << name << "\"  valid="
                      << (knst_file::is_valid_filename(name) ? "yes" : "no")
                      << "  sanitized=\"" << knst_file::sanitize_filename(name)
                      << "\"\n";
        }
    }

    // =====================================================================
    section("18) Platform-specific behaviour summary");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_WINDOWS
        std::cout << "Path separator     : '\\' (also accepts '/' in input)\n";
        std::cout << "Absolute path      : 'C:\\...' or '\\\\server\\share\\...'\n";
        std::cout << "Case sensitivity   : case-insensitive (NTFS default)\n";
        std::cout << "Hidden files       : FILE_ATTRIBUTE_HIDDEN flag\n";
        std::cout << "Executable bit     : no such concept (uses .exe extension)\n";
        std::cout << "Symlink creation   : requires admin / Developer Mode\n";
        std::cout << "Reserved names     : CON, PRN, AUX, NUL, COM1-9, LPT1-9\n";
        std::cout << "Forbidden chars    : < > : \" / \\ | ? *\n";
#else
        std::cout << "Path separator     : '/'\n";
        std::cout << "Absolute path      : '/etc/hosts'\n";
        std::cout << "Case sensitivity   : case-sensitive (ext4/btrfs default)\n";
        std::cout << "Hidden files       : filename starts with '.'\n";
        std::cout << "Executable bit     : 'x' permission on the file\n";
        std::cout << "Symlink creation   : always allowed for the owner\n";
        std::cout << "Reserved names     : none\n";
        std::cout << "Forbidden chars    : '/' and control characters\n";
#endif
    }

    // =====================================================================
    section("19) Pool allocator — basic_byte_string<knst_pool_allocator>");
    // =====================================================================
    // read_file_data<>, read_file_text<> and read_file_lines<> accept an
    // optional allocator template parameter. Default is the malloc-based
    // allocator (knst_default_allocator); pass knst_pool_allocator to
    // serve reads from a pre-allocated pool.
    {
        knst_c16string p = knst_file::join_path(sandbox, u"note.txt");
        knst_file::write_file_text(p, u"pool allocator test content");

        // 1) Read as bytes with pool allocator.
        auto pool_bytes = knst_file::read_file_data<knst_pool_allocator>(p);
        std::cout << "read_file_data<pool> : " << pool_bytes.length() << " bytes\n";

        // 2) Read as text with pool allocator.
        knst_c16string pool_text = knst_file::read_file_text<knst_pool_allocator>(p);
        std::cout << "read_file_text<pool> : " << pool_text << "\n";

        // 3) Read as lines with pool allocator.
        knst_c16string lp = knst_file::join_path(sandbox, u"lines.txt");
        knst_file::write_file_text(lp, u"alpha\nbeta\ngamma\n");

        knst_file lf;
        lf.read_file(lp);

        knst_vector<knst_c16string> pool_lines;
        lf.read_file_lines<knst_pool_allocator>(pool_lines);
        std::cout << "read_file_lines<pool>: " << pool_lines.size() << " lines\n";
        for (uint32_t i = 0; i < pool_lines.size(); ++i) {
            std::cout << "   [" << i << "] " << pool_lines[i] << "\n";
        }

        // 4) Write the pool-backed buffer back out (write accepts any allocator).
        knst_c16string out_p = knst_file::join_path(sandbox, u"roundtrip.txt");
        knst_file::write_file_data(out_p, pool_bytes);
        std::cout << "round-trip bytes     : "
                  << knst_file::get_file_size(out_p) << "\n";
    }

    // =====================================================================
    section("20) Cleanup");
    // =====================================================================
    {
        bool removed = knst_file::remove_all(sandbox);
        std::cout << "sandbox removed : " << (removed ? "yes" : "no") << "\n";
        std::cout << "still exists    : "
                  << (knst_file::path_exists(sandbox) ? "yes" : "no") << "\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}