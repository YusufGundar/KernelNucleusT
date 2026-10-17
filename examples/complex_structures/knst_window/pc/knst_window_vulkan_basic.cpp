/*
  knst_window_vulkan_basic.cpp

  Minimal Vulkan infrastructure test.

  This example deliberately does NOT show a window. It only validates
  the plumbing a real Vulkan renderer needs:

    1. Create a VkInstance with the right platform extensions.
    2. Create a VkSurfaceKHR bound to an invisible knst_window.
    3. Enumerate physical devices.
    4. For each device, check if a graphics queue family can present
       to our surface.

  When the test finishes, everything is torn down and the program exits
  — no event loop, no interaction.

  Things this teaches:
    1. Query the required VkInstance extensions for the current platform
       (RequiredInstanceExtensions).
    2. Create a knst_window WITHOUT mapping it (creation()).
    3. Bind a VkSurfaceKHR to that window (vulkan_content::Init).
    4. Verify a graphics queue family can also present
       (vkGetPhysicalDeviceSurfaceSupportKHR).
    5. Correct shutdown order: surface -> instance -> window -> platform.

  Why no window is shown:
    A validation test doesn't need user interaction. Showing the window
    would slow CI down and pop an empty frame on the desktop. On Wayland
    the compositor wants at least one surface commit — the library does
    that automatically inside creation(), so the test works on every
    platform without ever calling show().
*/

#include "../../../../include/KernelNucleusT.hpp"
#include <iostream>


// ─── 1. Global state ────────────────────────────────────────────────────────
// Only two Vulkan objects stay alive for the whole test. A real renderer
// would also create a logical device, swapchain, pipeline, etc. We stop
// at "is this device even usable?" on purpose.

static VkInstance g_instance = VK_NULL_HANDLE;
static VkSurfaceKHR g_surface = VK_NULL_HANDLE;


// ─── 2. VkInstance creation ─────────────────────────────────────────────────
// Each platform has its own surface extension:
//   X11      VK_KHR_xlib_surface
//   Wayland  VK_KHR_wayland_surface
//   Windows  VK_KHR_win32_surface
//   Android  VK_KHR_android_surface
//
// The library knows which ones apply and returns them from
// RequiredInstanceExtensions().

static bool create_instance() {
    uint32_t ext_count = 0;
    const char** exts =
        knst_window_vulkan_content::RequiredInstanceExtensions(&ext_count);

    if (!exts || ext_count == 0) {
        std::cerr << "  [!] No required instance extensions were returned.\n";
        return false;
    }

    std::cout << "Required instance extensions:\n";
    for (uint32_t i = 0; i < ext_count; ++i) {
        std::cout << "  - " << exts[i] << "\n";
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Knst Vulkan Basic";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "KernelNucleusT";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app_info;
    ci.enabledExtensionCount = ext_count;
    ci.ppEnabledExtensionNames = exts;
    ci.enabledLayerCount = 0;

    if (vkCreateInstance(&ci, nullptr, &g_instance) != VK_SUCCESS) {
        std::cerr << "  [!] vkCreateInstance failed.\n";
        return false;
    }

    return true;
}


// ─── 3. Physical device enumeration ─────────────────────────────────────────
// For each device, walk its queue families looking for one that:
//   - has the GRAPHICS bit set, and
//   - can present to our VkSurfaceKHR
//
// A device without such a family is unusable for a windowed renderer.

static void enumerate_devices() {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(g_instance, &device_count, nullptr);

    if (device_count == 0) {
        std::cout << "\nNo physical devices found.\n";
        return;
    }

    VkPhysicalDevice devices[16];
    if (device_count > 16) device_count = 16;
    vkEnumeratePhysicalDevices(g_instance, &device_count, devices);

    std::cout << "\nPhysical devices (" << device_count << "):\n";

    for (uint32_t i = 0; i < device_count; ++i) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(devices[i], &props);

        std::cout << "  [" << i << "] " << props.deviceName << "  (type " << props.deviceType << ")\n";

        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qf_count, nullptr);

        VkQueueFamilyProperties qfps[32]{};
        if (qf_count > 32) qf_count = 32;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qf_count, qfps);

        bool found_graphics_present = false;

        for (uint32_t q = 0; q < qf_count; ++q) {
            if (!(qfps[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;

            VkBool32 present = VK_FALSE;
            VkResult r = vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], q, g_surface, &present);

            if (r == VK_SUCCESS && present) {
                std::cout << "      -> queue family " << q << ": graphics + present  OK\n";
                found_graphics_present = true;
                break;
            }
        }

        if (!found_graphics_present) {
            std::cout << "      -> no queue family with graphics+present\n";
        }
    }
}


// ─── 4. Vulkan cleanup ──────────────────────────────────────────────────────
// Order matters: surface first, then instance. Destroying the instance
// first invalidates the surface handle — driver UB.

static void cleanup_vulkan() {
    if (g_surface != VK_NULL_HANDLE && g_instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(g_instance, g_surface, nullptr);
        g_surface = VK_NULL_HANDLE;
    }

    if (g_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(g_instance, nullptr);
        g_instance = VK_NULL_HANDLE;
    }
}


// ─── 5. Main ────────────────────────────────────────────────────────────────

int main() {
    std::cout << "========================================\n";
    std::cout << "  KernelNucleusT — Vulkan infrastructure\n";
    std::cout << "========================================\n\n";

    // STEP 1 — platform layer.
    KnstWindowSources::Init();
    if (KnstWindowSources::active_backend() == knst_backend::none) {
        std::cerr << "Failed to initialize the platform layer.\n";
        return 1;
    }

    // STEP 2 — VkInstance with the platform extensions.
    if (!create_instance()) {
        KnstWindowSources::CleanUp();
        return 1;
    }

    // STEP 3 — create the window WITHOUT showing it.
    //
    //   creation()           -> native surface exists, window unmapped
    //   creation_and_show()  -> also maps the window
    //
    // We only need the native surface (for VkSurfaceKHR). Not mapping
    // keeps the desktop clean and makes the example safe for CI.
    knst_window window(800, 600, u"Knst Vulkan Basic");
    window.creation();

    // STEP 4 — bind a VkSurfaceKHR to the window.
    knst_window_vulkan_content vk;
    if (!vk.Init(&window, g_instance)) {
        std::cerr << "  [!] VkSurfaceKHR creation failed.\n";
        cleanup_vulkan();
        window.destroy();
        KnstWindowSources::CleanUp();
        return 1;
    }
    g_surface = vk.GetSurface();

    std::cout << "\nVkSurfaceKHR created: " << g_surface << "\n";

    // STEP 5 — enumerate devices and report present support.
    enumerate_devices();

    // STEP 6 — teardown, correct order:
    // resources -> surface -> instance -> window -> platform
    std::cout << "\nShutting down...\n";

    vk.Shutdown(); // destroys VkSurfaceKHR
    g_surface = VK_NULL_HANDLE;

    cleanup_vulkan(); // destroys VkInstance

    window.destroy(); // window was never shown
    KnstWindowSources::CleanUp();

    std::cout << "\nDone — no window was shown.\n";
    return 0;
}