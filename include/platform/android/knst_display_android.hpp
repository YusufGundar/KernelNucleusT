#ifndef KNST_DISPLAY_ANDROID_HPP
#define KNST_DISPLAY_ANDROID_HPP
#pragma once

#if defined(KNST_USING_PLATFORM_ANDROID)

inline void knst_display::refresh_screens() noexcept {
    m_monitor_list.clear();
    
    knst_monitor monitor;
    
  
    if (KnstWindowSources::m_app != nullptr && KnstWindowSources::m_app ->window != nullptr) {
        monitor.width = ANativeWindow_getWidth(KnstWindowSources::m_app ->window);
        monitor.height = ANativeWindow_getHeight(KnstWindowSources::m_app ->window);
    } else {
        monitor.width = 1080;
        monitor.height = 2400;
    }
    
   
    monitor.dpi_scale = 160.0f;
    
    if (KnstWindowSources::m_app  != nullptr && KnstWindowSources::m_app ->activity != nullptr) {
        AConfiguration* config = AConfiguration_new();
        if (config != nullptr) {
            AConfiguration_fromAssetManager(config, KnstWindowSources::m_app ->activity->assetManager);
            int32_t dpi = AConfiguration_getDensity(config);
            AConfiguration_delete(config);
            
            if (dpi > 0) {
                monitor.dpi_scale = (float)dpi;
            }
        }
    }
    
    
    float dpi = monitor.dpi_scale;
    if (dpi > 0) {
        monitor.physical_width = (int)((float)monitor.width / dpi * 25.4f);
        monitor.physical_height = (int)((float)monitor.height / dpi * 25.4f);
    } else {
        monitor.physical_width = 0;
        monitor.physical_height = 0;
    }
    
    
    monitor.root_x = 0;
    monitor.root_y = 0;
    monitor.is_primary = true;
    monitor.refresh_rate = 60.0f;
    monitor.name = u"Android Screen";
    
    m_monitor_list.push_back(monitor);
}

#endif
#endif // KNST_DISPLAY_ANDROID_HPP