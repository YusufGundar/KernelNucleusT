#ifndef KNST_DISPLAY_WIN32_HPP
#define KNST_DISPLAY_WIN32_HPP
#pragma once

#if KNST_USING_PLATFORM_WINDOWS

                inline void knst_display::refresh_monitors() noexcept {

                
                        m_monitor_list.clear();
                        
                    
                        DISPLAY_DEVICEW displayDevice;
                        displayDevice.cb = sizeof(DISPLAY_DEVICEW);
                        
                        for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &displayDevice, 0); i++) {
                        
                            if (!(displayDevice.StateFlags & DISPLAY_DEVICE_ACTIVE) ||
                                (displayDevice.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER)) {
                                continue;
                            }
                            
                            DISPLAY_DEVICEW monitorDevice;
                            monitorDevice.cb = sizeof(DISPLAY_DEVICEW);
                            EnumDisplayDevicesW(displayDevice.DeviceName, 0, &monitorDevice, 0);
                            
                            DEVMODEW devMode;
                            devMode.dmSize = sizeof(DEVMODEW);
                            EnumDisplaySettingsW(displayDevice.DeviceName, ENUM_CURRENT_SETTINGS, &devMode);
                            
                            knst_monitor monitor;
                            
                        
                            monitor.root_x = devMode.dmPosition.x;
                            monitor.root_y = devMode.dmPosition.y;
                            monitor.width  = devMode.dmPelsWidth;
                            monitor.height = devMode.dmPelsHeight;
                            
                            
                            monitor.is_primary = (displayDevice.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;
                            
                        
                            HDC hdc = CreateDCW(displayDevice.DeviceName, nullptr, nullptr, nullptr);
                            if (hdc) {
                                monitor.physical_width  = GetDeviceCaps(hdc, HORZSIZE);
                                monitor.physical_height = GetDeviceCaps(hdc, VERTSIZE);
                                DeleteDC(hdc);
                            } else {
                                monitor.physical_width  = 0;
                                monitor.physical_height = 0;
                            }
                            
                        
                            monitor.name = knst_c16string(monitorDevice.DeviceString);
                            
                            
                            monitor.handle = MonitorFromPoint(
                                {monitor.root_x + monitor.width / 2, monitor.root_y + monitor.height / 2},
                                MONITOR_DEFAULTTONEAREST
                            );
                            
                            
                            monitor.refresh_rate = 60.0f;
                            if (devMode.dmDisplayFrequency > 0) {
                                monitor.refresh_rate = static_cast<float>(devMode.dmDisplayFrequency);
                            }
                            
                        
                            monitor.dpi_scale = 96.0f;
                            UINT dpiX = 96, dpiY = 96;
                            if (monitor.handle) {
                                HRESULT hr = GetDpiForMonitor(monitor.handle, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
                                if (SUCCEEDED(hr)) {
                                    monitor.dpi_scale = static_cast<float>(dpiX);
                                }
                            }
                            
                        
                            if (monitor.dpi_scale == 96.0f && monitor.physical_width > 0 && monitor.physical_height > 0) {
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
                                    monitor.dpi_scale = static_cast<float>(
                                        std::round(diagonal_px / diagonal_inch * 1000.0) / 1000.0
                                    );
                                }
                            }
                            
                            m_monitor_list.push_back(monitor);
                        }


                }




#endif


#endif // KNST_DISPLAY_WIN32_HPP