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

    // static globals  --I'm about to lose my mind-- it's vulkan ahhh  There are too many definitions. :p The document is very long. https://docs.vulkan.org/
    static inline VkInstance m_instance = VK_NULL_HANDLE;
    static inline VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    static inline VkDevice m_device = VK_NULL_HANDLE;
    static inline VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    static inline uint32_t m_graphicsFamilyIndex = 0;
    static inline bool m_coreInitialized = false;
    
   
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
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
        
        DetermineSurfaceExtension(); // to acquire a feature
        if (!m_surfaceExtension) {
            return false;
        }
        
      
        const char* validationLayers[] = {"VK_LAYER_KHRONOS_validation"}; // to capture these debugging operations
        uint32_t layerCount = 0;
        #ifdef KNST_VULKAN_VALIDATION // If this macro is defined, error catching becomes fully active.
            layerCount = 1;
        #endif
        
      
        const char* extensions[] = {
            VK_KHR_SURFACE_EXTENSION_NAME, // basic surface properties
            m_surfaceExtension // It gets platform-specific features here.
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
        createInfo.pApplicationInfo = &appInfo; // adds the information above
        createInfo.enabledExtensionCount = 2; // extension count
        createInfo.ppEnabledExtensionNames = extensions; // extensions
        createInfo.enabledLayerCount = layerCount; // same
        createInfo.ppEnabledLayerNames = validationLayers; // same
        
        VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance); // and the instance is created
        if (result != VK_SUCCESS) {
            return false;
        }
        
        
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr); // Gets the number of GPUs and assigns it to a variable.
        if (deviceCount == 0) {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
            return false;
        }
        
   
        knst_vector<VkPhysicalDevice> devices;
        devices.resize(deviceCount); // It performs a resize operation for each one present.
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data()); // fills the content
        
       
        for (uint32_t i = 0; i < devices.size(); i++) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(devices[i], &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { // It picks up the external drive first when it detects it; consequently, that one is always the fastest (in most cases). :o
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
        
       
        uint32_t queueFamilyCount = 0; // get gpu queue family count
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr); // get number
        
        knst_vector<VkQueueFamilyProperties> queueFamilies;
        queueFamilies.resize(queueFamilyCount); // number resize
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data()); // fill vector
        
        bool found = false;
        for (uint32_t i = 0; i < queueFamilies.size(); i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { //It finds the queue that handles graphics operations; if it can't find it, that means there is a problem.
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
        
      
        float queuePriority = 1.0f; // very important
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = m_graphicsFamilyIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        
        const char* deviceExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        }; // Swapchain support will be sufficient.
        
        VkDeviceCreateInfo deviceCreateInfo = {}; // And finally, we create the device here.
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        deviceCreateInfo.enabledExtensionCount = 1;
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;
        
        result = vkCreateDevice(m_physicalDevice, &deviceCreateInfo, nullptr, &m_device);
        if (result != VK_SUCCESS) {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
            return false;
        }
        
        vkGetDeviceQueue(m_device, m_graphicsFamilyIndex, 0, &m_graphicsQueue); // And we've got the queue; if I use it later on—well, I'm actually trying to prepare the groundwork for a GUI framework.
        
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
    
    bool Init(const knst_window& window) {
        if (m_windowInitialized) {
            return true;
        }
        
       
        if (!InitCore()) { // initializes global Vulkan structuress
            return false;
        }
        
        
        #if KNST_USING_PLATFORM_WINDOWS
            VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
            surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
            surfaceInfo.hinstance = KnstWindowSources::get_windows_native_instance_handle();
            surfaceInfo.hwnd = window.get_windows_window_handle();
            
            if (!surfaceInfo.hwnd) {
                return false;
            }
            
            VkResult result = vkCreateWin32SurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface);
            if (result != VK_SUCCESS) {
                return false;
            }
            
        #elif KNST_USING_LINUX_PLATFORM_X11
            VkXcbSurfaceCreateInfoKHR surfaceInfo = {};
            surfaceInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
            surfaceInfo.connection = KnstWindowSources::get_native_x11_connection_handle();
            surfaceInfo.window = window.get_x11_window_handle();
            
            if (!surfaceInfo.connection || !surfaceInfo.window) {
                return false;
            }
            
            VkResult result = vkCreateXcbSurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface);
            if (result != VK_SUCCESS) {
                return false;
            }
            
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            VkWaylandSurfaceCreateInfoKHR surfaceInfo = {};
            surfaceInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
            surfaceInfo.display = KnstWindowSources::wayland_display;
            surfaceInfo.surface = window.get_wayland_surface_handle();
            
            if (!surfaceInfo.display || !surfaceInfo.surface) {
                return false;
            }
            
            VkResult result = vkCreateWaylandSurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface);
            if (result != VK_SUCCESS) {
                return false;
            }
            
        #elif defined(KNST_USING_PLATFORM_ANDROID)
            android_app* app = KnstWindowSources::get_android_app();
            if (app == nullptr || app->window == nullptr) {
                return false;
            }
            
            VkAndroidSurfaceCreateInfoKHR surfaceInfo = {};
            surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
            surfaceInfo.window = app->window;
            
            if (!surfaceInfo.window) {
                return false;
            }
            
            VkResult result = vkCreateAndroidSurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface);
            if (result != VK_SUCCESS) {
                return false;
            }
        #endif
        
        m_windowInitialized = true;
        return true;
    }

    // Swapchain = Ekrana görüntü göndermek için kullanılan bir görüntü zinciridir
    // Yes, I wrote in Turkish this time because I think I keep forgetting ;)
    // Swapchain = An image chain used to send images to the screen.
    bool CreateSwapchain(uint32_t width, uint32_t height,bool vsync_status = false) {

        if (!m_windowInitialized) { // Come on, buddy—first, initialize that library with Init().
            return false;
        }
        
       
        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr); // get count
        if (formatCount == 0) return false;
        
        knst_vector<VkSurfaceFormatKHR> formats;
        formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data()); // get real data
        
        VkSurfaceFormatKHR surfaceFormat = formats[0];
        for (uint32_t i = 0; i < formats.size(); i++) {
            if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&  // 8 bit SRGB
                formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { // standart SRGB
                surfaceFormat = formats[i];
                break;
            }
        }
        
      
        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr); // The number of present modes supported by the Surface
        
        knst_vector<VkPresentModeKHR> presentModes;
        presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes.data()); // fill knst_vector classic 
        
        VkPresentModeKHR presentMode;
        if (vsync_status) {
            
            presentMode = VK_PRESENT_MODE_FIFO_KHR;
            for (uint32_t i = 0; i < presentModes.size(); i++) {
                if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                    presentMode = presentModes[i];
                    break;
                }
            }
        } else {
           
            presentMode = VK_PRESENT_MODE_FIFO_KHR;
            for (uint32_t i = 0; i < presentModes.size(); i++) {
                if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                    presentMode = presentModes[i];
                    break;
                }
            }
        }
        
       
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities); // Retrieves the features supported by the surface.
        
        uint32_t imageCount = capabilities.minImageCount + 1; // Triple Buffering  ,,, If you delete the +1 here, you'll get double buffering.
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
            imageCount = capabilities.maxImageCount; // set max image count
        }
        
        VkExtent2D extent = {};
        if (capabilities.currentExtent.width != UINT32_MAX) { // UINT32_MAX = a special value indicating that the window manager resolution has not been set.
            extent = capabilities.currentExtent; 
        } else {
            extent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, width));
            extent.height = std::max(capabilities.minImageExtent.height,std::min(capabilities.maxImageExtent.height, height));
            // Exactly as we wanted.
                                     
        }
        
       
        VkSwapchainCreateInfoKHR swapchainInfo = {};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = m_surface;
        swapchainInfo.minImageCount = imageCount;
        swapchainInfo.imageFormat = surfaceFormat.format;
        swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
        swapchainInfo.imageExtent = extent; // WIDTH AND HEIGHT
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // It uses only a single queue family (highest performance).
        swapchainInfo.preTransform = capabilities.currentTransform;
        swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainInfo.presentMode = presentMode; // V-Sync setting
        swapchainInfo.clipped = VK_TRUE;
        swapchainInfo.oldSwapchain = VK_NULL_HANDLE;
        
        VkResult result = vkCreateSwapchainKHR(m_device, &swapchainInfo, nullptr, &m_swapchain);
        if (result != VK_SUCCESS) {
            return false;
        }
        
        return true;
    }
    
  
    VkInstance GetInstance() const { return m_instance; }
    VkSurfaceKHR GetSurface() const { return m_surface; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    VkDevice GetDevice() const { return m_device; }
    VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
    VkSwapchainKHR GetSwapchain() const { return m_swapchain; }
    uint32_t GetGraphicsFamilyIndex() const { return m_graphicsFamilyIndex; }
    bool IsInitialized() const { return m_windowInitialized; }
    static bool IsCoreInitialized() { return m_coreInitialized; }
    
   
    void Destroy() {
       
        if (m_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
        }
        
       
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
    
    
    static const char* GetResultString(VkResult result) {
        return VkResultToString(result);
    }
};

#endif // KNST_USING_VULKAN
#endif // KNST_WINDOW_VULKAN_MANAGER_HPP