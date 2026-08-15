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
    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    bool m_initialized = false;

    
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
    bool Init(const knst_window& window) {
        if (m_initialized) {
            return true;
        }

      
        const char* surfaceExtension = nullptr;
        
        #if KNST_USING_PLATFORM_WINDOWS
            surfaceExtension = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
        #elif KNST_USING_LINUX_PLATFORM_X11
            surfaceExtension = VK_KHR_XCB_SURFACE_EXTENSION_NAME;
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            surfaceExtension = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
        #elif defined(KNST_USING_PLATFORM_ANDROID)
            surfaceExtension = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
        #endif

        if (!surfaceExtension) {
            return false;
        }

       
        const char* extensions[] = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            surfaceExtension
        };
        uint32_t extensionCount = 2;

        

        const char* validationLayers[] = {
            "VK_LAYER_KHRONOS_validation"
        };
        uint32_t layerCount = 0;
        
        #ifdef KNST_VULKAN_VALIDATION
            layerCount = 1;
        #endif

      
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "KernelNucleusT Vulkan";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "KernelNucleusT";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = extensionCount;
        createInfo.ppEnabledExtensionNames = extensions;
        createInfo.enabledLayerCount = layerCount;
        createInfo.ppEnabledLayerNames = validationLayers;
        
        VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
        if (result != VK_SUCCESS) {
            return false;
        }

       
        #if KNST_USING_PLATFORM_WINDOWS
            VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
            surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
            surfaceInfo.hinstance = KnstWindowSources::get_windows_native_instance_handle();
            surfaceInfo.hwnd = window.get_windows_window_handle();

            if (!surfaceInfo.hwnd) {
                vkDestroyInstance(m_instance, nullptr);
                m_instance = VK_NULL_HANDLE;
                return false;
            }

            result = vkCreateWin32SurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface);
            
        #elif KNST_USING_LINUX_PLATFORM_X11
            VkXcbSurfaceCreateInfoKHR surfaceInfo = {};
            surfaceInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
            surfaceInfo.connection = KnstWindowSources::get_native_x11_connection_handle();
            surfaceInfo.window = window.get_x11_window_handle();

            if (!surfaceInfo.connection || !surfaceInfo.window) {
                vkDestroyInstance(m_instance, nullptr);
                m_instance = VK_NULL_HANDLE;
                return false;
            }

            result = vkCreateXcbSurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface);
            
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            VkWaylandSurfaceCreateInfoKHR surfaceInfo = {};
            surfaceInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
            surfaceInfo.display = KnstWindowSources::wayland_display;
            surfaceInfo.surface = window.get_wayland_surface_handle();

            if (!surfaceInfo.display || !surfaceInfo.surface) {
                vkDestroyInstance(m_instance, nullptr);
                m_instance = VK_NULL_HANDLE;
                return false;
            }

            result = vkCreateWaylandSurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface);

        #elif defined(KNST_USING_PLATFORM_ANDROID)
         
            android_app* app = KnstWindowSources::get_android_app();
            if (app == nullptr || app->window == nullptr) {
                vkDestroyInstance(m_instance, nullptr);
                m_instance = VK_NULL_HANDLE;
                return false;
            }

            VkAndroidSurfaceCreateInfoKHR surfaceInfo = {};
            surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
            surfaceInfo.window = app->window;

            if (!surfaceInfo.window) {
                vkDestroyInstance(m_instance, nullptr);
                m_instance = VK_NULL_HANDLE;
                return false;
            }

            result = vkCreateAndroidSurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface);
        #endif

        if (result != VK_SUCCESS) {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
            return false;
        }

       
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
        if (deviceCount == 0) {
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
            vkDestroyInstance(m_instance, nullptr);
            return false;
        }

        VkPhysicalDevice* devices = new VkPhysicalDevice[deviceCount];
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices);
        
      
        for (uint32_t i = 0; i < deviceCount; i++) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(devices[i], &props);
            
      
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                m_physicalDevice = devices[i];
                break;
            }
        }
        
        if (m_physicalDevice == VK_NULL_HANDLE && deviceCount > 0) {
            m_physicalDevice = devices[0];
        }
        
        delete[] devices;
        
        if (m_physicalDevice == VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
            vkDestroyInstance(m_instance, nullptr);
            return false;
        }

     
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
        VkQueueFamilyProperties* queueFamilies = new VkQueueFamilyProperties[queueFamilyCount];
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies);
        
        uint32_t graphicsFamily = 0;
        bool found = false;
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphicsFamily = i;
                found = true;
                break;
            }
        }
        delete[] queueFamilies;
        
        if (!found) {
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
            vkDestroyInstance(m_instance, nullptr);
            return false;
        }

       
        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = graphicsFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

       
        const char* deviceExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        deviceCreateInfo.enabledExtensionCount = 1;
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;

        result = vkCreateDevice(m_physicalDevice, &deviceCreateInfo, nullptr, &m_device);
        if (result != VK_SUCCESS) {
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
            vkDestroyInstance(m_instance, nullptr);
            return false;
        }

        vkGetDeviceQueue(m_device, graphicsFamily, 0, &m_graphicsQueue);

        m_initialized = true;
        return true;
    }

    
    static const char** GetVulkanExtensions(uint32_t* count) {
        static const char* extensions[3] = { nullptr, nullptr, nullptr };
        
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

    
    VkSurfaceKHR GetSurface() const { return m_surface; }
    VkInstance GetInstance() const { return m_instance; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    VkDevice GetDevice() const { return m_device; }
    bool IsInitialized() const { return m_initialized; }

    
    void SetPhysicalDevice(VkPhysicalDevice device) { m_physicalDevice = device; }
    void SetDevice(VkDevice device) { m_device = device; }
    void SetGraphicsQueue(VkQueue queue) { m_graphicsQueue = queue; }
    void SetSwapchain(VkSwapchainKHR swapchain) { m_swapchain = swapchain; }

    
    void Destroy() {
        if (m_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
        }
        
        if (m_device != VK_NULL_HANDLE) {
            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }
        
        if (m_surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
            m_surface = VK_NULL_HANDLE;
        }
        
        if (m_instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }
        
        m_physicalDevice = VK_NULL_HANDLE;
        m_graphicsQueue = VK_NULL_HANDLE;
        m_initialized = false;
    }
};

#endif 

#endif // KNST_WINDOW_VULKAN_MANAGER_HPP