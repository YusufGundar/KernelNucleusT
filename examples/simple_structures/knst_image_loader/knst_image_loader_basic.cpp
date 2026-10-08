// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
  knst_image_loader_basic.cpp

  Basic usage of knst_image_loader - a self-contained image decoder
  with no external dependencies (no libpng / libjpeg / zlib).

  Shows: loading from disk, metadata, custom options (output format,
  resize, flip, premultiply, flatten), memory loading, format
  detection, the legacy API, and the pool allocator variant.

  To stay portable, the example tries to find an image shipped with
  the OS:
    Windows: C:\Windows\Web\Wallpaper\Windows\img0.jpg  (and friends)
    Linux:   /usr/share/backgrounds/...                 (fallbacks)
  If nothing is found, the demo just skips the load steps - that is
  not an error.

  Build:
        g++ -std=c++20 -O2 knst_image_loader_basic.cpp -o knst_image_loader_basic
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>
#include <cstdio>


// X11/Xlib.h defines `None` as a macro; the library clears it in
// KernelNucleusT.hpp, but we keep this guard for extra safety.
#ifdef None
    #undef None
#endif


static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}


static const char* platform_name() {
#if KNST_USING_PLATFORM_WINDOWS
    return "Windows";
#else
    return "POSIX (Linux/macOS)";
#endif
}


// Returns the first existing path from a list of common wallpaper
// locations, or an empty string if none is found.
static knst_c16string find_system_image() {

#if KNST_USING_PLATFORM_WINDOWS
    knst_c16string candidates[] = {
        u"C:\\Windows\\Web\\Wallpaper\\Windows\\img0.jpg",
        u"C:\\Windows\\Web\\Screen\\img100.jpg",
        u"C:\\Windows\\Web\\Wallpaper\\ThemeA\\img1.jpg",
        u"C:\\Windows\\Web\\Wallpaper\\ThemeB\\img1.jpg",
        u"C:\\ProgramData\\Microsoft\\User Account Pictures\\user.bmp",
    };
#else
    knst_c16string candidates[] = {
        u"/usr/share/backgrounds/warty-final-ubuntu.png",
        u"/usr/share/backgrounds/ubuntu-wallpaper-d.png",
        u"/usr/share/backgrounds/gnome/adwaita-l.jpg",
        u"/usr/share/backgrounds/Fedora/generic.png",
        u"/usr/share/backgrounds/Fedora/generic-4k.png",
        u"/usr/share/pixmaps/debian-logo.png",
        u"/usr/share/icons/hicolor/256x256/apps/firefox.png",
    };
#endif

    for (const auto& c : candidates) {
        if (knst_file::file_exists(c)) return c;
    }
    return knst_c16string();
}


int main() {

    std::cout << "Platform: " << platform_name() << "\n";

    // =====================================================================
    section("1) Locate a system image");
    // =====================================================================
    knst_c16string path = find_system_image();

    if (path.empty()) {
        std::cout << "(no default image found on this system - load steps will be skipped)\n";
        std::cout << "\nDone (nothing to load).\n";
        return 0;
    }

    std::cout << "using: " << path << "\n";

    // =====================================================================
    section("2) Load with defaults (RGBA8)");
    // =====================================================================
    {
        knst_image img = knst_image_loader::load(path);

        if (!img) {
            std::cout << "load failed: " << knst_image_error_string(img.error) << "\n";
        } else {
            std::cout << "size        : " << img.width << " x " << img.height << "\n";
            std::cout << "channels    : " << img.channels << "\n";
            std::cout << "bytes       : " << img.pixels.length() << "\n";
            std::cout << "stride      : " << img.stride() << " bytes/row\n";
            std::cout << "source fmt  : "
                      << knst_image_loader::format_name(img.source_format) << "\n";
        }
    }

    // =====================================================================
    section("3) Detect format without decoding");
    // =====================================================================
    {
        knst_byte_string bytes = knst_file::read_file_data<>(path);
        if (bytes.empty()) {
            std::cout << "(could not read the file)\n";
        } else {
            int fmt = knst_image_loader::detect_format(bytes.data(), bytes.length());
            std::cout << "bytes       : " << bytes.length() << "\n";
            std::cout << "magic (hex) : ";
            for (uint32_t i = 0; i < bytes.length() && i < 8; ++i) {
                char buf[4];
                std::snprintf(buf, sizeof(buf), "%02X ", (unsigned)bytes[i]);
                std::cout << buf;
            }
            std::cout << "\n";
            std::cout << "detected    : "
                      << knst_image_loader::format_name(fmt) << "\n";
        }
    }

    // =====================================================================
    section("4) Custom output format (BGRA)");
    // =====================================================================
    {
        knst_image_load_options o;
        o.output_format = KNST_BITMAP_OUTPUT_BGRA;

        knst_image img = knst_image_loader::load(path, o);
        if (img) {
            std::cout << "channels    : " << img.channels << "\n";
            std::cout << "output fmt  : BGRA\n";
            std::cout << "bytes       : " << img.pixels.length() << "\n";
        } else {
            std::cout << "load failed: " << knst_image_error_string(img.error) << "\n";
        }
    }

    // =====================================================================
    section("5) Resize to 128x128 thumbnail");
    // =====================================================================
    {
        knst_image_load_options o;
        o.resize_width  = 128;
        o.resize_height = 128;
        o.keep_aspect   = true;
        o.resize_filter = KNST_IMAGE_RESIZE_LANCZOS3;

        knst_image img = knst_image_loader::load(path, o);
        if (img) {
            std::cout << "size        : " << img.width << " x " << img.height << "\n";
            std::cout << "bytes       : " << img.pixels.length() << "\n";
            std::cout << "(aspect kept, fits inside 128x128)\n";
        } else {
            std::cout << "load failed: " << knst_image_error_string(img.error) << "\n";
        }
    }

    // =====================================================================
    section("6) Flip vertical");
    // =====================================================================
    {
        knst_image_load_options o;
        o.flip_vertical = true;
        o.resize_width  = 32;
        o.resize_height = 32;

        knst_image img = knst_image_loader::load(path, o);
        if (img) {
            std::cout << "flipped     : yes\n";
            std::cout << "size        : " << img.width << " x " << img.height << "\n";
        } else {
            std::cout << "load failed: " << knst_image_error_string(img.error) << "\n";
        }
    }

    // =====================================================================
    section("7) Premultiply alpha");
    // =====================================================================
    {
        knst_image_load_options o;
        o.premultiply_alpha = true;
        o.resize_width      = 32;
        o.resize_height     = 32;

        knst_image img = knst_image_loader::load(path, o);
        if (img) {
            std::cout << "premultiplied : yes\n";
            std::cout << "size          : " << img.width << " x " << img.height << "\n";
        } else {
            std::cout << "load failed: " << knst_image_error_string(img.error) << "\n";
        }
    }

    // =====================================================================
    section("8) Flatten alpha onto a white background");
    // =====================================================================
    {
        knst_image_load_options o;
        o.flatten_alpha = true;
        o.background_r  = 255;
        o.background_g  = 255;
        o.background_b  = 255;
        o.resize_width  = 32;
        o.resize_height = 32;

        knst_image img = knst_image_loader::load(path, o);
        if (img) {
            std::cout << "flattened onto white\n";
            std::cout << "channels    : " << img.channels << "\n";
        } else {
            std::cout << "load failed: " << knst_image_error_string(img.error) << "\n";
        }
    }

    // =====================================================================
    section("9) Legacy API (unchanged signature)");
    // =====================================================================
    {
        int w = 0, h = 0;
        knst_byte_string px = knst_image_loader::load_image(
            path, &w, &h, KNST_BITMAP_OUTPUT_RGBA);

        if (px.empty()) {
            std::cout << "load_image failed\n";
        } else {
            std::cout << "size        : " << w << " x " << h << "\n";
            std::cout << "bytes       : " << px.length() << "\n";
            std::cout << "(w*h*4 == " << (w * h * 4) << ")\n";
        }
    }

    // =====================================================================
    section("10) Load from memory");
    // =====================================================================
    {
        knst_byte_string bytes = knst_file::read_file_data<>(path);
        if (bytes.empty()) {
            std::cout << "(could not read the file)\n";
        } else {
            knst_image_load_options o;
            o.resize_width  = 64;
            o.resize_height = 64;

            knst_image img = knst_image_loader::load_from_memory(
                bytes.data(), bytes.length(), o);

            if (img) {
                std::cout << "loaded from memory\n";
                std::cout << "size        : " << img.width << " x " << img.height << "\n";
            } else {
                std::cout << "load failed: " << knst_image_error_string(img.error) << "\n";
            }
        }
    }

    // =====================================================================
    section("11) Pool allocator — knst_image_t<knst_pool_allocator>");
    // =====================================================================
    // Every load() overload accepts an optional allocator template
    // parameter. The default is knst_default_allocator (backward
    // compatible — the plain knst_image alias is preserved). Pass a
    // pool allocator to serve the output pixels from a pre-allocated
    // pool. Note: the type of pixels changes accordingly.
    {
        auto img = knst_image_loader::load<knst_pool_allocator>(path);

        if (!img) {
            std::cout << "load<pool> failed: "
                      << knst_image_error_string(img.error) << "\n";
        } else {
            std::cout << "size        : " << img.width << " x " << img.height << "\n";
            std::cout << "channels    : " << img.channels << "\n";
            std::cout << "bytes       : " << img.pixels.length() << "\n";
            std::cout << "stride      : " << img.stride() << " bytes/row\n";
            std::cout << "(pixels is basic_byte_string<knst_pool_allocator>)\n";
        }

        // Pool-backed + resize + BGRA in one shot.
        knst_image_load_options o;
        o.output_format = KNST_BITMAP_OUTPUT_BGRA;
        o.resize_width  = 64;
        o.resize_height = 64;

        auto thumb = knst_image_loader::load<knst_pool_allocator>(path, o);
        if (thumb) {
            std::cout << "\nthumb<pool> : " << thumb.width << " x " << thumb.height
                      << "  BGRA  " << thumb.pixels.length() << " bytes\n";
        }

        // Also works from memory.
        knst_byte_string bytes = knst_file::read_file_data<>(path);
        if (!bytes.empty()) {
            auto mem_img = knst_image_loader::load_from_memory<knst_pool_allocator>(
                bytes.data(), bytes.length(), o);
            if (mem_img) {
                std::cout << "mem<pool>   : " << mem_img.width << " x " << mem_img.height
                          << "  " << mem_img.pixels.length() << " bytes\n";
            }
        }
    }

    std::cout << "\nDone.\n";
    return 0;
}