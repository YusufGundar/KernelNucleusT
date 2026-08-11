#ifndef KNST_DISPLAY_HPP
#define KNST_DISPLAY_HPP
#pragma once

#include <cmath>

#if KNST_USING_PLATFORM_WINDOWS

    #include <ShellScalingApi.h>

    
#elif KNST_USING_LINUX_PLATFORM_X11

    #include <xcb/randr.h>

#endif






class knst_display;

class knst_monitor {
private:
    friend class knst_display;

    #if KNST_USING_PLATFORM_WINDOWS
        HMONITOR handle;   
    #elif KNST_USING_LINUX_PLATFORM_X11
        xcb_randr_output_t output_id;
    #elif KNST_USING_LINUX_PLATFORM_WAYLAND
        friend class KnstWindowSources;
        struct wl_output* output = nullptr;
        uint32_t output_id = 0;
    #endif

public:
    int root_x = 0;
    int root_y = 0;
    int width = 0;
    int height = 0;
    int physical_width = 0;
    int physical_height = 0;
    float refresh_rate = 60;
    float dpi_scale = 96.0f;
    bool is_primary = false;
    knst_c16string name = u"Undefined";
};

class knst_display {
private:
    static inline knst_vector<knst_monitor> m_monitor_list;
    friend class KnstWindowSources;

    #if KNST_USING_LINUX_PLATFORM_WAYLAND

 
    static inline void wayland_output_geometry(void* data, wl_output*, int32_t x, int32_t y,int32_t pw, int32_t ph, int32_t,const char* make, const char* model, int32_t) {
                                        
        auto* m = (knst_monitor*)data;
        m->root_x = x;
        m->root_y = y;
        m->physical_width = pw;
        m->physical_height = ph;
        
        knst_byte_string name;
        if (make && strlen(make) > 0) {
            name.append(reinterpret_cast<const unsigned char*>(make), strlen(make));
        }
        if (model && strlen(model) > 0) {
            if (!name.empty()) name.append(" ");
            name.append(reinterpret_cast<const unsigned char*>(model), strlen(model));
        }
        if (!name.empty()) {
            m->name = knst_c16string(reinterpret_cast<const char*>(name.data()));
        }
    }

    static inline void wayland_output_mode(void* data, wl_output*, uint32_t flags,
                                    int32_t w, int32_t h, int32_t refresh) {
        auto* m = (knst_monitor*)data;
        if (flags & WL_OUTPUT_MODE_CURRENT) {
            m->width = w;
            m->height = h;
            m->refresh_rate = refresh / 1000.0f;
        }
        if (flags & WL_OUTPUT_MODE_PREFERRED) {
            m->is_primary = true;
        }
    }

    static inline void wayland_output_done(void* data, wl_output*) {
        auto* m = (knst_monitor*)data;
        
       
        if (m->physical_width > 0 && m->physical_height > 0) {
            double dp = std::sqrt((double)(m->width * m->width) + (double)(m->height * m->height));
            double dm = std::sqrt((double)(m->physical_width * m->physical_width) + (double)(m->physical_height * m->physical_height));
            double di = dm / 25.4;
            if (di > 0.0) m->dpi_scale = (float)(dp / di);
        }
    }

    static inline void wayland_output_scale(void* data, wl_output*, int32_t factor) {
        auto* m = (knst_monitor*)data;
        m->dpi_scale = m->dpi_scale * (float)factor; 
    }

    static inline const wl_output_listener wayland_output_listener = {
        .geometry = wayland_output_geometry,
        .mode = wayland_output_mode,
        .done = wayland_output_done,
        .scale = wayland_output_scale,
    };

    #endif


public:
    

        static void refresh_monitors() noexcept;


    static const knst_vector<knst_monitor>& get_monitor_list() noexcept {
        return m_monitor_list;
    }
    
    static const knst_monitor* get_primary_monitor() noexcept {
        for (const auto& mon : m_monitor_list) {
            if (mon.is_primary) {
                return &mon;
            }
        }
        return m_monitor_list.empty() ? nullptr : &m_monitor_list[0];
    }
};

#endif // KNST_DISPLAY_HPP