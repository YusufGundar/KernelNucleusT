#ifndef KNST_DISPLAY_WAYLAND_HPP
#define KNST_DISPLAY_WAYLAND_HPP
#pragma once

#if KNST_USING_LINUX_PLATFORM_WAYLAND





                inline void knst_display::refresh_monitors() noexcept {
                    if (!KnstWindowSources::wayland_display || !KnstWindowSources::registery) return;
                    
                
                    wl_display_roundtrip(KnstWindowSources::wayland_display);
                    wl_display_roundtrip(KnstWindowSources::wayland_display);
                    
                
                    std::sort(m_monitor_list.begin(), m_monitor_list.end(),
                        [](const knst_monitor& a, const knst_monitor& b) {
                            return a.root_x < b.root_x;
                        });
                    
                
                    bool has_primary = false;
                    for (auto& m : m_monitor_list) {
                        if (m.is_primary) { has_primary = true; break; }
                    }
                    if (!has_primary && !m_monitor_list.empty()) {
                        m_monitor_list[0].is_primary = true;
                    }
                }

                #endif 
                #endif // KNST_DISPLAY_WAYLAND_HPP