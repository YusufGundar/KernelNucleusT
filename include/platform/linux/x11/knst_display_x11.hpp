#ifndef KNST_DISPLAY_X11_HPP
#define KNST_DISPLAY_X11_HPP
#pragma once


#if KNST_USING_LINUX_PLATFORM_X11

inline void knst_display::refresh_screens() noexcept {

                xcb_randr_query_version_cookie_t version_cookie = 
                    xcb_randr_query_version(KnstWindowSources::m_connection, 1, 5);
                xcb_randr_query_version_reply_t* version_reply = 
                    xcb_randr_query_version_reply(KnstWindowSources::m_connection, version_cookie, nullptr);
                
                if (!version_reply) return;
                free(version_reply);
                
                xcb_randr_get_screen_resources_current_cookie_t res_cookie = 
                    xcb_randr_get_screen_resources_current(
                        KnstWindowSources::m_connection,
                        KnstWindowSources::m_root
                    );
                                                        
                xcb_randr_get_screen_resources_current_reply_t* res_reply =
                    xcb_randr_get_screen_resources_current_reply(
                        KnstWindowSources::m_connection,
                        res_cookie,
                        nullptr
                    );
                
                xcb_randr_get_monitors_cookie_t monitors_cookie = 
                    xcb_randr_get_monitors(
                        KnstWindowSources::m_connection,
                        KnstWindowSources::m_root,
                        1
                    );
                                        
                xcb_randr_get_monitors_reply_t* monitors_reply = 
                    xcb_randr_get_monitors_reply(
                        KnstWindowSources::m_connection,
                        monitors_cookie,
                        nullptr
                    );
                
                if (!monitors_reply) {
                    if (res_reply) free(res_reply);
                    return;
                }
                
                m_monitor_list.clear();
                
                xcb_randr_monitor_info_iterator_t iter = 
                    xcb_randr_get_monitors_monitors_iterator(monitors_reply);
                
                while (iter.rem) {
                    xcb_randr_monitor_info_t* info = iter.data;
                    knst_monitor monitor;
                
                    monitor.root_x = info->x;
                    monitor.root_y = info->y;
                    monitor.width = info->width;
                    monitor.height = info->height;
                    monitor.is_primary = info->primary;
                    monitor.physical_width = info->width_in_millimeters;
                    monitor.physical_height = info->height_in_millimeters;
                    
                    if (info->name != XCB_ATOM_NONE) {
                        xcb_get_atom_name_cookie_t name_cookie = xcb_get_atom_name(KnstWindowSources::m_connection, info->name);
                        xcb_get_atom_name_reply_t* name_reply = xcb_get_atom_name_reply(KnstWindowSources::m_connection, name_cookie, nullptr);
                            
                        
                        if (name_reply) {
                            const char* name_ptr = xcb_get_atom_name_name(name_reply);
                            int name_len = xcb_get_atom_name_name_length(name_reply);
                            monitor.name = knst_c16string(std::string(name_ptr, name_len).c_str());
                            free(name_reply);
                        } else {
                            monitor.name = knst_c16string("Unknown");
                        }
                    } else {
                        monitor.name = knst_c16string("Unknown");
                    }

                    xcb_randr_output_t* outputs = xcb_randr_monitor_info_outputs(info);
                    int num_outputs = xcb_randr_monitor_info_outputs_length(info);
                    
                    if (num_outputs > 0) {
                        monitor.output_id = outputs[0];
                    }
                    
                    monitor.refresh_rate = 60.0f;
                    
                    if (num_outputs > 0 && res_reply) {
                        xcb_randr_get_output_info_cookie_t out_cookie =
                            xcb_randr_get_output_info(
                                KnstWindowSources::m_connection,
                                outputs[0],
                                XCB_CURRENT_TIME
                            );
                                                    
                        xcb_randr_get_output_info_reply_t* out_reply =
                            xcb_randr_get_output_info_reply(
                                KnstWindowSources::m_connection,
                                out_cookie,
                                nullptr
                            );
                        
                        if (out_reply && out_reply->crtc != XCB_NONE) {
                            xcb_randr_get_crtc_info_cookie_t crtc_cookie =
                                xcb_randr_get_crtc_info(
                                    KnstWindowSources::m_connection,
                                    out_reply->crtc,
                                    XCB_CURRENT_TIME
                                );
                                                        
                            xcb_randr_get_crtc_info_reply_t* crtc_reply =
                                xcb_randr_get_crtc_info_reply(
                                    KnstWindowSources::m_connection,
                                    crtc_cookie,
                                    nullptr
                                );
                            
                            if (crtc_reply && crtc_reply->mode != XCB_NONE) {
                                xcb_randr_mode_info_iterator_t mode_iter =
                                    xcb_randr_get_screen_resources_current_modes_iterator(res_reply);
                                
                                while (mode_iter.rem) {
                                    xcb_randr_mode_info_t* mode = mode_iter.data;
                                    
                                    if (mode->id == crtc_reply->mode) {
                                        if (mode->htotal > 0 && mode->vtotal > 0) {
                                            monitor.refresh_rate = 
                                                (float)mode->dot_clock / (mode->htotal * mode->vtotal);
                                        }
                                        break;
                                    }
                                    xcb_randr_mode_info_next(&mode_iter);
                                }
                            }
                            if (crtc_reply) free(crtc_reply);
                        }
                        if (out_reply) free(out_reply);
                    }
                    
                    monitor.dpi_scale = 96.0f;
                    
                    if (monitor.physical_width > 0 && monitor.physical_height > 0) {
                        double diagonal_px = std::sqrt(
                            (double)(monitor.width * monitor.width) + 
                            (double)(monitor.height * monitor.height)
                        );
                        double diagonal_mm = std::sqrt(
                            (double)(monitor.physical_width * monitor.physical_width) + 
                            (double)(monitor.physical_height * monitor.physical_height)
                        );
                        double diagonal_inch = diagonal_mm / 25.4;
                        
                        if (diagonal_inch > 0.0) {
                            monitor.dpi_scale = std::round(diagonal_px / diagonal_inch * 1000.0) / 1000.0;
                        }
                    }
                    
                    m_monitor_list.push_back(monitor);
                    xcb_randr_monitor_info_next(&iter);
                }
                
                free(monitors_reply);
                if (res_reply) free(res_reply);

}
#endif
#endif // KNST_DISPLAY_X11_HPP