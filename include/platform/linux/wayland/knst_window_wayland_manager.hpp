#ifndef KNST_WINDOW_WAYLAND_MANAGER_HPP
#define KNST_WINDOW_WAYLAND_MANAGER_HPP
#pragma once

#include <poll.h>       

#include <fcntl.h>       





#if KNST_USING_LINUX_PLATFORM_WAYLAND


static void knst_parse_uri_list_wayland(const std::string& uriList, knst_vector<knst_c16string>& out);


inline void knst_window::creation() noexcept
{
    m_surface = wl_compositor_create_surface(KnstWindowSources::compositor);
    s_windows.push_back(this);
    xdgSurface = xdg_wm_base_get_xdg_surface(KnstWindowSources::wmBase, m_surface);
    toplevel = xdg_surface_get_toplevel(xdgSurface);
    knst_byte_string title(m_title);
    xdg_toplevel_set_title(toplevel, reinterpret_cast<const char*>(title.data()));
    xdg_surface_add_listener(xdgSurface, &knst_window_wayland_funcs::xdgSurfaceListener, this);
    xdg_toplevel_add_listener(toplevel, &knst_window_wayland_funcs::xdgToplevelListener, this);
    wl_surface_commit(m_surface);
    
    knst_window_event_system::register_window(this);
}

inline void knst_window::show() noexcept
{
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
}

inline void knst_window::destroy() noexcept
{
    knst_window_event_system::unregister_window(this);
    if (m_layer_surface) { 
        
            wl_proxy_destroy((struct wl_proxy*)m_layer_surface);
            m_layer_surface = nullptr;
        
        m_layer_surface = nullptr; 
    }
    if (toplevel) { xdg_toplevel_destroy(toplevel); toplevel = nullptr; }
    if (xdgSurface) { xdg_surface_destroy(xdgSurface); xdgSurface = nullptr; }
    if (m_surface) { wl_surface_destroy(m_surface); m_surface = nullptr; }
    s_windows.erase_value(this);
}

inline knst_window* knst_window::find_from_surface(wl_surface* surface) noexcept
{
    for (knst_window* window : s_windows)
        if (window && window->m_surface == surface) return window;
    return nullptr;
}

inline resize_edge knst_window::get_resize_edge(double x, double y) const noexcept
{
    constexpr double border = 12.0;
    const double width = static_cast<double>(m_knst_event.window_width);
    const double height = static_cast<double>(m_knst_event.window_height);
    const bool left = x <= border;
    const bool right = x >= width - border;
    const bool top = y <= border;
    const bool bottom = y >= height - border;
    if (left && top) return resize_edge::top_left;
    if (right && top) return resize_edge::top_right;
    if (left && bottom) return resize_edge::bottom_left;
    if (right && bottom) return resize_edge::bottom_right;
    if (left)  return resize_edge::left;
    if (right) return resize_edge::right;
    if (top) return resize_edge::top;
    if (bottom) return resize_edge::bottom;
    return resize_edge::none;
}

inline void knst_window::apply_wayland_configure_size(int width, int height) noexcept
{
    if (width <= 0 || height <= 0) return;
    m_knst_event.window_width  = width;
    m_knst_event.window_height = height;
}

inline void knst_window::set_title(const knst_c16string& title) noexcept
{
    if (!toplevel) return;
    knst_byte_string new_title(title);
    xdg_toplevel_set_title(toplevel, reinterpret_cast<const char*>(new_title.data()));
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
    m_title = title;
}

inline void knst_window::move(int root_x, int root_y, const knst_monitor& monitor) noexcept { 
    (void)root_x; (void)root_y; (void)monitor;
}
inline void knst_window::move(int root_x, int root_y) noexcept { 
    (void)root_x; (void)root_y;
}

inline void knst_window::toggle_fullscreen(bool fullscreen) noexcept
{
    if (!toplevel) return;
    if (fullscreen) xdg_toplevel_set_fullscreen(toplevel, nullptr);
    else            xdg_toplevel_unset_fullscreen(toplevel);
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
    m_knst_event.is_full_screen = fullscreen;
}

inline void knst_window::set_minimized() noexcept
{
    if (!toplevel) return;
    xdg_toplevel_set_minimized(toplevel);
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
    m_knst_event.is_minimized = true;
}

inline void knst_window::set_maximized() noexcept
{
    if (!toplevel) return;
    xdg_toplevel_set_maximized(toplevel);
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
    m_knst_event.is_maximized = true;
}

inline void knst_window::restore() noexcept
{
    if (!toplevel) return;
    if (m_knst_event.is_maximized)   xdg_toplevel_unset_maximized(toplevel);
    if (m_knst_event.is_full_screen) xdg_toplevel_unset_fullscreen(toplevel);
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
    m_knst_event.is_maximized = false;
    m_knst_event.is_full_screen = false;
    m_knst_event.is_minimized = false;
}

inline void knst_window::hide() noexcept
{
    if (!m_surface) return;
    wl_surface_attach(m_surface, nullptr, 0, 0);
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
}

inline void knst_window::focus() noexcept { /* Wayland doesn't allow it */ }

inline void knst_window::set_cursor(uint16_t cursor_type) noexcept
{
    if (!KnstWindowSources::pointer || !KnstWindowSources::cursor_surface || 
        !KnstWindowSources::cursor_theme) return;
    
    const char* cursor_name = "left_ptr";
    switch (cursor_type) {
        case KNST_CURSOR_ARROW:         cursor_name = "left_ptr"; break;
        case KNST_CURSOR_IBEAM:         cursor_name = "xterm"; break;
        case KNST_CURSOR_CROSSHAIR:     cursor_name = "crosshair"; break;
        case KNST_CURSOR_HAND:          cursor_name = "hand2"; break;
        case KNST_CURSOR_HRESIZE:       cursor_name = "sb_h_double_arrow"; break;
        case KNST_CURSOR_VRESIZE:       cursor_name = "sb_v_double_arrow"; break;
        case KNST_CURSOR_MOVE:          cursor_name = "fleur"; break;
        case KNST_CURSOR_WAIT:          cursor_name = "watch"; break;
        case KNST_CURSOR_HELP:          cursor_name = "question_arrow"; break;
        case KNST_CURSOR_NOT_ALLOWED:   cursor_name = "X_cursor"; break;
        default:                        cursor_name = "left_ptr"; break;
    }
    
    struct wl_cursor* cursor = wl_cursor_theme_get_cursor(KnstWindowSources::cursor_theme, cursor_name);
    if (!cursor || cursor->image_count == 0)
        cursor = wl_cursor_theme_get_cursor(KnstWindowSources::cursor_theme, "left_ptr");
    if (!cursor || cursor->image_count == 0) return;
    
    struct wl_cursor_image* image = cursor->images[0];
    struct wl_buffer* buffer = wl_cursor_image_get_buffer(image);
    if (!buffer) return;
    
    wl_pointer_set_cursor(KnstWindowSources::pointer, m_pointer_serial,
                          KnstWindowSources::cursor_surface, image->hotspot_x, image->hotspot_y);
    wl_surface_attach(KnstWindowSources::cursor_surface, buffer, 0, 0);
    wl_surface_damage_buffer(KnstWindowSources::cursor_surface, 0, 0, image->width, image->height);
    wl_surface_commit(KnstWindowSources::cursor_surface);
    m_using_custom_cursor = false;
}

inline void knst_window::reset_cursor() noexcept
{
    if (KnstWindowSources::pointer && KnstWindowSources::cursor_surface && KnstWindowSources::cursor_default) {
        wl_cursor_image* image = KnstWindowSources::cursor_default->images[0];
        wl_buffer* buffer = wl_cursor_image_get_buffer(image);
        if (buffer) {
            wl_surface_attach(KnstWindowSources::cursor_surface, buffer, 0, 0);
            wl_surface_damage(KnstWindowSources::cursor_surface, 0, 0, image->width, image->height);
            wl_surface_commit(KnstWindowSources::cursor_surface);
            wl_pointer_set_cursor(KnstWindowSources::pointer, m_pointer_serial,KnstWindowSources::cursor_surface, image->hotspot_x, image->hotspot_y);
                                  
        }
    }
    
    m_using_custom_cursor = false;
}

inline void knst_window::set_bmp_cursor(
    const knst_byte_string& data, 
    int width, 
    int height, 
    int hot_x, 
    int hot_y
) noexcept {
    if (!KnstWindowSources::shm || !KnstWindowSources::cursor_surface) {
        return;
    }

    if (data.empty() || width <= 0 || height <= 0) {
        return;
    }

    if (hot_x < 0 || hot_x >= width)  hot_x = width  / 2;
    if (hot_y < 0 || hot_y >= height) hot_y = height / 2;

    const size_t expected_size = (size_t)width * (size_t)height * 4;
    if (data.length() < expected_size) {
        return;
    }

   
    uint8_t* converted = new uint8_t[expected_size];
    const uint8_t* src = (const uint8_t*)data.data();
    
    for (size_t i = 0; i < (size_t)width * height; i++) {
        uint8_t r = src[i * 4 + 0]; 
        uint8_t g = src[i * 4 + 1];
        uint8_t b = src[i * 4 + 2];
        uint8_t a = src[i * 4 + 3];
        
       
        converted[i * 4 + 0] = b;  // B
        converted[i * 4 + 1] = g;  // G
        converted[i * 4 + 2] = r;  // R
        converted[i * 4 + 3] = a;  // A
    }

   
    knst_byte_string converted_data = knst_byte_string::take_ownership(converted, expected_size);

    m_pending_cursor_data  = converted_data;
    m_pending_cursor_w     = width;
    m_pending_cursor_h     = height;
    m_pending_cursor_hot_x = hot_x;
    m_pending_cursor_hot_y = hot_y;

    m_using_custom_cursor = true;

    if (!KnstWindowSources::pointer || m_pointer_serial == 0) {
        m_has_pending_cursor = true;
        return;
    }

    apply_bmp_cursor_now(converted_data, width, height, hot_x, hot_y);
}

inline void knst_window::apply_bmp_cursor_now(const knst_byte_string& data, int width, int height, int hot_x, int hot_y) noexcept
{
    int stride = width * 4;
    int size = stride * height;

    int fd = shm_open("/knst-cursor", O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) {
        return;
    }

    if (ftruncate(fd, size) != 0) {
        close(fd);
        shm_unlink("/knst-cursor");
        return;
    }

    void* shm_data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm_data == MAP_FAILED) {
        close(fd);
        shm_unlink("/knst-cursor");
        return;
    }

    const uint8_t* src = data.data();
uint8_t* dst = (uint8_t*)shm_data;

for (int i = 0; i < width * height; i++) {
    uint8_t b = src[i*4 + 0];
    uint8_t g = src[i*4 + 1];
    uint8_t r = src[i*4 + 2];
    uint8_t a = src[i*4 + 3];
    dst[i*4 + 0] = (uint8_t)((b * a) / 255);
    dst[i*4 + 1] = (uint8_t)((g * a) / 255);
    dst[i*4 + 2] = (uint8_t)((r * a) / 255);
    dst[i*4 + 3] = a;
}

    munmap(shm_data, size);

    struct wl_shm_pool* pool = wl_shm_create_pool(KnstWindowSources::shm, fd, size);
    if (!pool) {
        close(fd);
        shm_unlink("/knst-cursor");
        return;
    }

    struct wl_buffer* buffer = wl_shm_pool_create_buffer(
        pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);

    if (!buffer) {
        wl_shm_pool_destroy(pool);
        close(fd);
        shm_unlink("/knst-cursor");
        return;
    }

    wl_surface_attach(KnstWindowSources::cursor_surface, buffer, 0, 0);
    wl_surface_damage(KnstWindowSources::cursor_surface, 0, 0, width, height);
    wl_surface_commit(KnstWindowSources::cursor_surface);

    wl_pointer_set_cursor(
        KnstWindowSources::pointer,
        m_pointer_serial,
        KnstWindowSources::cursor_surface,
        hot_x,
        hot_y
    );

    wl_display_flush(KnstWindowSources::wayland_display);


    wl_buffer_destroy(buffer);
    wl_shm_pool_destroy(pool);
    close(fd);
    shm_unlink("/knst-cursor");

    m_using_custom_cursor = true;
}


inline void knst_window::resize(int width, int height) noexcept
{
    if (!toplevel || !m_surface) return;
    if (width <= 0) width = m_knst_event.window_width;
    if (height <= 0) height = m_knst_event.window_height;
    m_knst_event.window_width = width;
    m_knst_event.window_height = height;
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
}

inline void knst_window::set_cursor_mode(int mode) noexcept
{
    if (!KnstWindowSources::pointer || !KnstWindowSources::cursor_surface || 
        !KnstWindowSources::cursor_theme) return;
    
    if (KnstWindowSources::locked_pointer) { zwp_locked_pointer_v1_destroy(KnstWindowSources::locked_pointer); KnstWindowSources::locked_pointer = nullptr; }
    if (KnstWindowSources::confined_pointer) { zwp_confined_pointer_v1_destroy(KnstWindowSources::confined_pointer); KnstWindowSources::confined_pointer = nullptr; }
    
    if (mode == KNST_CURSOR_NORMAL) {
        struct wl_cursor* cursor = wl_cursor_theme_get_cursor(KnstWindowSources::cursor_theme, "left_ptr");
        if (cursor && cursor->image_count > 0) {
            struct wl_cursor_image* image = cursor->images[0];
            struct wl_buffer* buffer = wl_cursor_image_get_buffer(image);
            if (buffer) {
                wl_pointer_set_cursor(KnstWindowSources::pointer, m_pointer_serial,
                                      KnstWindowSources::cursor_surface, image->hotspot_x, image->hotspot_y);
                wl_surface_attach(KnstWindowSources::cursor_surface, buffer, 0, 0);
                wl_surface_damage_buffer(KnstWindowSources::cursor_surface, 0, 0, image->width, image->height);
                wl_surface_commit(KnstWindowSources::cursor_surface);
            }
        }
    } else if (mode == KNST_CURSOR_HIDDEN) {
        wl_surface_attach(KnstWindowSources::cursor_surface, nullptr, 0, 0);
        wl_surface_commit(KnstWindowSources::cursor_surface);
        wl_pointer_set_cursor(KnstWindowSources::pointer, m_pointer_serial, KnstWindowSources::cursor_surface, 0, 0);
    } else if (mode == KNST_CURSOR_DISABLED) {
        wl_surface_attach(KnstWindowSources::cursor_surface, nullptr, 0, 0);
        wl_surface_commit(KnstWindowSources::cursor_surface);
        if (KnstWindowSources::pointer_constraints && m_surface) {
            KnstWindowSources::locked_pointer = zwp_pointer_constraints_v1_lock_pointer(
                KnstWindowSources::pointer_constraints, m_surface, KnstWindowSources::pointer,
                nullptr, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
            KnstWindowSources::confined_pointer = zwp_pointer_constraints_v1_confine_pointer(
                KnstWindowSources::pointer_constraints, m_surface, KnstWindowSources::pointer,
                nullptr, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        }
    }
}

inline void knst_window::set_cursor_pos_on_window(int x, int y) noexcept { 
    (void)x; (void)y;
}
inline void knst_window::set_cursor_pos_global(int root_x, int root_y) noexcept { 
    (void)root_x; (void)root_y;
}



inline void knst_window::set_clipboard(const knst_c16string& text) noexcept {
    if (!KnstWindowSources::data_device_manager || !KnstWindowSources::data_device) return;

    KnstWindowSources::clipboard_pending_text = text;

    if (KnstWindowSources::clipboard_source) {
        wl_data_source_destroy(KnstWindowSources::clipboard_source);
    }

    KnstWindowSources::clipboard_source =
        wl_data_device_manager_create_data_source(KnstWindowSources::data_device_manager);

    wl_data_source_add_listener(KnstWindowSources::clipboard_source,
        &knst_window_wayland_funcs::dataSourceListener, nullptr);


    wl_data_source_offer(KnstWindowSources::clipboard_source, "text/plain;charset=utf-8");
    wl_data_source_offer(KnstWindowSources::clipboard_source, "text/plain");
    wl_data_source_offer(KnstWindowSources::clipboard_source, "UTF8_STRING");
    wl_data_source_offer(KnstWindowSources::clipboard_source, "TEXT");
    wl_data_source_offer(KnstWindowSources::clipboard_source, "STRING");

  
    uint32_t serial = m_pointer_serial;
    if (serial == 0 && KnstWindowSources::keyboard_serial != 0) {
        serial = KnstWindowSources::keyboard_serial;
    }

    wl_data_device_set_selection(KnstWindowSources::data_device,
        KnstWindowSources::clipboard_source, serial);

    wl_display_flush(KnstWindowSources::wayland_display);
}

inline void knst_window::request_clipboard() noexcept {
    if (!KnstWindowSources::selection_offer) {
        clipboard_text = u"";
        return;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) return;

    wl_data_offer_receive(KnstWindowSources::selection_offer, "text/plain;charset=utf-8", pipefd[1]);
    close(pipefd[1]);

    wl_display_flush(KnstWindowSources::wayland_display);


    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    std::string result;
    char buf[4096];
    bool got_data = false;

    auto start_time = std::chrono::steady_clock::now();

    while (!got_data) {
   
        while (wl_display_prepare_read(KnstWindowSources::wayland_display) != 0) {
            wl_display_dispatch_pending(KnstWindowSources::wayland_display);
        }
        wl_display_flush(KnstWindowSources::wayland_display);

        struct pollfd pfd;
        pfd.fd = wl_display_get_fd(KnstWindowSources::wayland_display);
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 10); 
        if (ret > 0 && (pfd.revents & POLLIN)) {
            wl_display_read_events(KnstWindowSources::wayland_display);
            wl_display_dispatch_pending(KnstWindowSources::wayland_display);
        } else {
            wl_display_cancel_read(KnstWindowSources::wayland_display);
        }

        ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            result.append(buf, n);
            got_data = true;
        } else if (n == 0) {
            break;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        if (elapsed > 100) break;
    }

    close(pipefd[0]);

    if (!result.empty()) {
        clipboard_text = knst_c16string(result.c_str());
    } else {
        clipboard_text = u"";
    }
}

inline void knst_window::set_drag_drop_status(bool enabled) noexcept {
    m_drag_drop_enabled = enabled;
}



inline void knst_window::set_opacity(float opacity) noexcept
{
    if (!m_surface || !KnstWindowSources::compositor) return;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    m_opacity = opacity;
    
    if (opacity >= 0.999f) {
        struct wl_region* region = wl_compositor_create_region(KnstWindowSources::compositor);
        if (region) {
            wl_region_add(region, 0, 0, m_knst_event.window_width, m_knst_event.window_height);
            wl_surface_set_opaque_region(m_surface, region);
            wl_region_destroy(region);
        }
    } else {
        wl_surface_set_opaque_region(m_surface, nullptr);
    }
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
}


inline void knst_window::set_attribute(int attribute, bool value) noexcept
{
    if (!toplevel) return;
    switch (attribute) {
        case KNST_WINDOW_ATTRIB_DECORATED: 
            m_draw_custom_title_bar = !value;
            break;
            
        case KNST_WINDOW_ATTRIB_RESIZABLE:
            if (!value) {
                xdg_toplevel_set_min_size(toplevel, m_knst_event.window_width, m_knst_event.window_height);
                xdg_toplevel_set_max_size(toplevel, m_knst_event.window_width, m_knst_event.window_height);
            } else {
                xdg_toplevel_set_min_size(toplevel, 300, 250);
                xdg_toplevel_set_max_size(toplevel, 0, 0);
            }
            wl_surface_commit(m_surface);
            wl_display_flush(KnstWindowSources::wayland_display);
            break;
            
        case KNST_WINDOW_ATTRIB_ALWAYS_ON_TOP: 
            (void)value; 
            break;
            
        case KNST_WINDOW_ATTRIB_TRANSPARENT:
            if (value) {
                
                if (m_surface && KnstWindowSources::compositor) {
                    struct wl_region* region = wl_compositor_create_region(KnstWindowSources::compositor);
                    if (region) {
                        #ifndef KNST_DISABLE_TITLE_BAR
                            
                            int titleBarH = get_title_bar_height();
                            wl_region_add(region, 0, 0, m_knst_event.window_width, titleBarH);
                          
                        #else
                          
                            wl_region_add(region, 0, 0, m_knst_event.window_width, 5);
                           
                            wl_region_add(region, 0, 0, 5, m_knst_event.window_height);
                            wl_region_add(region, m_knst_event.window_width - 5, 0, 5, m_knst_event.window_height);
                            wl_region_add(region, 0, m_knst_event.window_height - 5, m_knst_event.window_width, 5);
                        #endif
                        
                        wl_surface_set_input_region(m_surface, region);
                        wl_region_destroy(region);
                        wl_surface_commit(m_surface);
                        wl_display_flush(KnstWindowSources::wayland_display);
                    }
                }
                m_input_transparent = true;
            } else {
               
                if (m_surface) {
                    wl_surface_set_input_region(m_surface, nullptr);
                    wl_surface_commit(m_surface);
                    wl_display_flush(KnstWindowSources::wayland_display);
                }
                m_input_transparent = false;
            }
            break;
    }
}

inline bool knst_window::get_attribute(int attribute) const noexcept
{
    if (!toplevel) return false;
    switch (attribute) {
        case KNST_WINDOW_ATTRIB_DECORATED: 
            return !m_draw_custom_title_bar;
        case KNST_WINDOW_ATTRIB_RESIZABLE: 
            return true;
        case KNST_WINDOW_ATTRIB_ALWAYS_ON_TOP: 
            return false;
        case KNST_WINDOW_ATTRIB_TRANSPARENT: 
            return m_input_transparent;
        default:
            return false;
    }
}



inline void knst_window::apply_bmp_icon(const knst_byte_string& bytes, int icon_width, int icon_height) noexcept
{
    (void)bytes; (void)icon_width; (void)icon_height;
    // you can set up the icon file in a different way.
}

inline void knst_window::set_minimum_size(int width, int height) noexcept {
    if (!toplevel || !m_surface) return;
    
    if (width == KNST_DEFAULT) {
        width = 300;
    }
    if (height == KNST_DEFAULT) {
        height = 250;
    }
    
    xdg_toplevel_set_min_size(toplevel, width, height);
    
    if (m_knst_event.window_width < width || m_knst_event.window_height < height) {
        int newWidth = m_knst_event.window_width < width ? width : m_knst_event.window_width;
        int newHeight = m_knst_event.window_height < height ? height : m_knst_event.window_height;
        resize(newWidth, newHeight);
    }
    
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
}

inline void knst_window::set_maximum_size(int width, int height) noexcept {
    if (!toplevel || !m_surface) return;
    
    if (width == KNST_DEFAULT && height == KNST_DEFAULT) {
        xdg_toplevel_set_max_size(toplevel, 0, 0);
        wl_surface_commit(m_surface);
        wl_display_flush(KnstWindowSources::wayland_display);
        return;
    }
    
    int maxWidth = (width == KNST_DEFAULT) ? 0 : width;
    int maxHeight = (height == KNST_DEFAULT) ? 0 : height;
    
    xdg_toplevel_set_max_size(toplevel, maxWidth, maxHeight);
    
    if (maxWidth > 0 && m_knst_event.window_width > maxWidth) {
        resize(maxWidth, m_knst_event.window_height);
    }
    if (maxHeight > 0 && m_knst_event.window_height > maxHeight) {
        resize(m_knst_event.window_width, maxHeight);
    }
    
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
}

#endif
#endif // KNST_WINDOW_WAYLAND_MANAGER_HPP