// ============================================================================
//  KernelNucleusT - Modern C++ Library
// ============================================================================
//  Description: Starts the window's Vulkan resources.
//  Copyright (c) 2026 Yusuf Gündar
//  Licensed under the MIT License. See LICENSE file for details.
// ============================================================================


#ifndef KNST_WINDOW_VULKAN_MANAGER_HPP
#define KNST_WINDOW_VULKAN_MANAGER_HPP
#pragma once

#ifdef KNST_USING_VULKAN

#if KNST_USING_PLATFORM_WINDOWS
    #ifndef VK_USE_PLATFORM_WIN32_KHR
        #define VK_USE_PLATFORM_WIN32_KHR
    #endif
    
#elif KNST_USING_LINUX_PLATFORM_X11
    #ifndef VK_USE_PLATFORM_XCB_KHR
        #define VK_USE_PLATFORM_XCB_KHR
    #endif

#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    #ifndef VK_USE_PLATFORM_WAYLAND_KHR
        #define VK_USE_PLATFORM_WAYLAND_KHR
    #endif

#elif defined(KNST_USING_PLATFORM_ANDROID)
    #ifndef VK_USE_PLATFORM_ANDROID_KHR
        #define VK_USE_PLATFORM_ANDROID_KHR
    #endif
#endif

#include <vulkan/vulkan.h>

class knst_window_vulkan_content {
private:
    static inline VkInstance m_instance = VK_NULL_HANDLE;
    static inline VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    static inline VkDevice m_device = VK_NULL_HANDLE;
    static inline VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    static inline uint32_t m_graphicsFamilyIndex = 0;
    static inline bool m_coreInitialized = false;
    
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    bool m_windowInitialized = false;
    
    static inline const char* m_surfaceExtension = nullptr;
    
    static void DetermineSurfaceExtension() {
        #if KNST_USING_PLATFORM_WINDOWS
            m_surfaceExtension = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
        #elif KNST_USING_LINUX_PLATFORM_X11
            m_surfaceExtension = VK_KHR_XCB_SURFACE_EXTENSION_NAME;
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            m_surfaceExtension = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
        #elif defined(KNST_USING_PLATFORM_ANDROID)
            m_surfaceExtension = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
        #endif
    }
    
    static bool InitCore() {
    if (m_coreInitialized) {
        return true;
    }
    
    DetermineSurfaceExtension();
    if (!m_surfaceExtension) {
        return false;
    }
    
    const char* validationLayers[] = {"VK_LAYER_KHRONOS_validation"};
    uint32_t layerCount = 0;
    #ifdef KNST_VULKAN_VALIDATION
        layerCount = 1;
    #endif
    
    //★★★★★★ VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME its important for gui framework★★★★★★ 
    const char* instanceExtensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        m_surfaceExtension
    };
    
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "KernelNucleusT Vulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 1);
    appInfo.pEngineName = "KernelNucleusT";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 1);
    appInfo.apiVersion = VK_API_VERSION_1_3;
    
    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = 2;
    createInfo.ppEnabledExtensionNames = instanceExtensions;
    createInfo.enabledLayerCount = layerCount;
    createInfo.ppEnabledLayerNames = validationLayers;
    
    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        return false;
    }
    
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        return false;
    }
    
    knst_vector<VkPhysicalDevice> devices;
    devices.resize(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
    
    for (uint32_t i = 0; i < devices.size(); i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            m_physicalDevice = devices[i];
            break;
        }
    }
    
    if (m_physicalDevice == VK_NULL_HANDLE && devices.size() > 0) {
        m_physicalDevice = devices[0];
    }
    
    if (m_physicalDevice == VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        return false;
    }
    
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    
    knst_vector<VkQueueFamilyProperties> queueFamilies;
    queueFamilies.resize(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());
    
    bool found = false;
    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            m_graphicsFamilyIndex = i;
            found = true;
            break;
        }
    }
    
    if (!found) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        return false;
    }
    
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = m_graphicsFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    
  
    // ★★★★★★ DEVICE EXTENSIONS - VK_EXT_vertex_input_dynamic_state  ★★★★★★
    // If you do not get this add-on, the GUI framework will not work (for now). ★★★★★★★★★★★★★★★★★★★★★★★★ You can change it.You can change it.
    const char* deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME
    };
    
    // ★★★★★★ ★★★★★★ ★★★★★★ 
    // ★★★ We activate it. ★★★
    // ★★★★★★ ★★★★★★ ★★★★★★ 
    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT vertexInputDynState{};
    vertexInputDynState.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT;
    vertexInputDynState.vertexInputDynamicState = VK_TRUE;
    
    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pNext = &vertexInputDynState; //this feature
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.enabledExtensionCount = 2;
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;
    
    result = vkCreateDevice(m_physicalDevice, &deviceCreateInfo, nullptr, &m_device);
    if (result != VK_SUCCESS) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        return false;
    }
    
    vkGetDeviceQueue(m_device, m_graphicsFamilyIndex, 0, &m_graphicsQueue);
    
    m_coreInitialized = true;
    return true;
}
    
    static void DestroyCore() {
        if (m_device != VK_NULL_HANDLE) {
            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }
        if (m_instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }
        m_physicalDevice = VK_NULL_HANDLE;
        m_graphicsQueue = VK_NULL_HANDLE;
        m_coreInitialized = false;
    }
    
    static const char* VkResultToString(VkResult result) {
        switch (result) {
            case VK_SUCCESS: return "VK_SUCCESS";
            case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
            case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
            case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
            case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
            case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
            case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
            case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
            case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
            default: return "UNKNOWN_ERROR";
        }
    }

public:
    #if KNST_USING_LINUX_PLATFORM_X11
        knst_window* m_window;
    #endif

    bool Init(knst_window& window) {
        if (m_windowInitialized) return true;
        
        if (!InitCore()) return false;
        
        #if KNST_USING_PLATFORM_WINDOWS
            VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
            surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
            surfaceInfo.hinstance = KnstWindowSources::get_windows_native_instance_handle();
            surfaceInfo.hwnd = window.get_windows_window_handle();
            if (!surfaceInfo.hwnd) return false;
            VkResult result = vkCreateWin32SurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface);
            if (result != VK_SUCCESS) return false;
            
        #elif KNST_USING_LINUX_PLATFORM_X11
            VkXcbSurfaceCreateInfoKHR surfaceInfo = {};
            surfaceInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
            surfaceInfo.connection = KnstWindowSources::get_native_x11_connection_handle();
            surfaceInfo.window = window.get_x11_window_handle();
            if (!surfaceInfo.connection || !surfaceInfo.window) return false;
            VkResult result = vkCreateXcbSurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface);
            if (result != VK_SUCCESS) return false;
            m_window = &window;
            
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            VkWaylandSurfaceCreateInfoKHR surfaceInfo = {};
            surfaceInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
            surfaceInfo.display = KnstWindowSources::wayland_display;
            surfaceInfo.surface = const_cast<wl_surface*>(window.get_wayland_surface_handle());
            if (!surfaceInfo.display || !surfaceInfo.surface) return false;
            VkResult result = vkCreateWaylandSurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface);
            if (result != VK_SUCCESS) return false;
            
        #elif defined(KNST_USING_PLATFORM_ANDROID)
            android_app* app = KnstWindowSources::get_android_app();
            if (app == nullptr || app->window == nullptr) return false;
            VkAndroidSurfaceCreateInfoKHR surfaceInfo = {};
            surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
            surfaceInfo.window = app->window;
            if (!surfaceInfo.window) return false;
            VkResult result = vkCreateAndroidSurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface);
            if (result != VK_SUCCESS) return false;
        #endif
        
        m_windowInitialized = true;
        return true;
    }

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && 
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return 0;
    }
  
    VkInstance GetInstance() const { return m_instance; }
    VkSurfaceKHR GetSurface() const { return m_surface; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    VkDevice GetDevice() const { return m_device; }
    VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
    uint32_t GetGraphicsFamilyIndex() const { return m_graphicsFamilyIndex; }
    bool IsInitialized() const { return m_windowInitialized; }
    static bool IsCoreInitialized() { return m_coreInitialized; }
   
    void Destroy() {
        if (m_surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
            m_surface = VK_NULL_HANDLE;
        }
        m_windowInitialized = false;
    }
    
    static void DestroyGlobalSources() {
        DestroyCore();
    }
    
    static const char** GetVulkanExtensions(uint32_t* count) {
    static const char* extensions[2] = { nullptr, nullptr };
    #if KNST_USING_PLATFORM_WINDOWS
        extensions[0] = VK_KHR_SURFACE_EXTENSION_NAME;
        extensions[1] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
        *count = 2;
    #elif KNST_USING_LINUX_PLATFORM_X11
        extensions[0] = VK_KHR_SURFACE_EXTENSION_NAME;
        extensions[1] = VK_KHR_XCB_SURFACE_EXTENSION_NAME;
        *count = 2;
    #elif KNST_USING_LINUX_PLATFORM_WAYLAND
        extensions[0] = VK_KHR_SURFACE_EXTENSION_NAME;
        extensions[1] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
        *count = 2;
    #elif defined(KNST_USING_PLATFORM_ANDROID)
        extensions[0] = VK_KHR_SURFACE_EXTENSION_NAME;
        extensions[1] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
        *count = 2;
    #endif
    return extensions;
}
    
    static const char* GetResultString(VkResult result) {
        return VkResultToString(result);
    }
};

#endif // KNST_USING_VULKAN
#endif // KNST_WINDOW_VULKAN_MANAGER_HPP