/*
  knst_window_android_vulkan_test.cpp

  Android twin of the desktop Vulkan example. Two structural differences:

      PC      :  int main()
      Android :  void android_main(struct android_app* app)

      PC      :  KnstWindowSources::Init()
      Android :  KnstWindowSources::Init(app)

  Everything else — VkInstance creation, platform extensions, VkSurfaceKHR
  binding, physical device enumeration, graphics+present queue lookup — is
  identical. No #if anywhere in this file.

  This example is a minimal infrastructure test. It does NOT render and
  does NOT listen to input. It only verifies:

    1. VkInstance + platform extensions
    2. VkSurfaceKHR (ANativeWindow -> Vulkan)
    3. Physical device + graphics/present queue lookup
    4. Event loop — clean exit on Back button

  The window stays black because nothing is drawn. To add a real
  renderer, drop your code into the [RENDER HERE] block inside on_frame.

  Build:
      Linux         :  ./build-android-vulkan.sh
      Windows       :  build-android-vulkan.bat
*/

#include "../../../../include/KernelNucleusT.hpp"
#include <iostream>



// ─── 1. Global state ────────────────────────────────────────────────────────
// Just two Vulkan objects. A real renderer would also create a logical
// device, swapchain, pipelines — we stop at "is the device usable?".

static VkInstance g_instance = VK_NULL_HANDLE;
static VkSurfaceKHR g_surface = VK_NULL_HANDLE;


// ─── 2. VkInstance creation ─────────────────────────────────────────────────
// Each platform needs its own surface extension:
//   Windows : VK_KHR_win32_surface
//   X11  ---: VK_KHR_xlib_surface
//   Wayland : VK_KHR_wayland_surface
//   Android : VK_KHR_android_surface
//
// The library knows which apply and returns them from
// RequiredInstanceExtensions(). No #ifdef in user code.

static bool create_instance() {
    uint32_t ext_count = 0;
    const char** exts = knst_window_vulkan_content::RequiredInstanceExtensions(&ext_count);

    if (!exts || ext_count == 0) {
        std::cerr << "  [!] No required instance extensions returned.\n";
        return false;
    }

    std::cout << "Required instance extensions:\n";
    for (uint32_t i = 0; i < ext_count; ++i) {
        std::cout << "  - " << exts[i] << "\n";
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Knst Vulkan Test (Android)";
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
// Walk every physical device's queue families looking for one that has
// the GRAPHICS bit AND can present to our VkSurfaceKHR.
// -*-*-*-*-
// Same code as the desktop example — once instance and surface exist,
// this is platform-independent.

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

        bool found = false;

        for (uint32_t q = 0; q < qf_count; ++q) {
            if (!(qfps[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;

            VkBool32 present = VK_FALSE;
            VkResult r = vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], q, g_surface, &present);

            if (r == VK_SUCCESS && present) {
                std::cout << "      -> queue family " << q << ": graphics + present  OK\n";
                found = true;
                break;
            }
        }

        if (!found) {
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


// ─── 5. Frame callback ──────────────────────────────────────────────────────
// Called once per loop iteration. This example only cares about two
// events: window close and the Android back button. Both call
// should_close(), and the library then triggers ANativeActivity_finish
// automatically so the activity closes cleanly.
//
// Every other event (touch, motion, keyboard, lifecycle) is ignored.

static void on_frame(knst_window& window, void* /*user_data*/) {
    for (size_t i = 0; i < window.event_count(); ++i) {
        const knst_window_event& ev = window.get_window_event_handle(i);

        switch (ev.type) {
            case KNST_WINDOW_EVENT_CLOSE:
                window.should_close();
                break;

            case KNST_WINDOW_EVENT_MOBILE_BACK:
                std::fprintf(stderr, "[APP] back button -> quit\n");
                window.should_close();
                break;

            default:
                break;
        }
    }

    window.clear_events();

    if (window.is_should_close()) return;
    if (!window.is_frame_ready()) return;

    // ── [RENDER HERE] ─────────────────────────────────────────────────
    // This example renders nothing on purpose. The window stays black.
    // A real renderer would put this sequence here:
    //
    //      1. vkAcquireNextImageKHR
    //      2. vkResetCommandBuffer + vkBeginCommandBuffer
    //      3. vkCmdBeginRenderPass
    //      4. vkCmdBindPipeline
    //      5. vkCmdDraw
    //      6. vkCmdEndRenderPass + vkEndCommandBuffer
    //      7. vkQueueSubmit
    //      8. vkQueuePresentKHR
}


// ─── 6. Entry point ─────────────────────────────────────────────────────────
// Android never calls main(). native_app_glue runs its own loop and
// calls android_main() on the activity thread. The android_app* handle
// carries:
//     app->window       — ANativeWindow (native surface)
//     app->activity     — Java-side activity (JNI access)
//     app->looper       — the app's main ALooper
//     app->onAppCmd     — lifecycle callback slot
//     app->onInputEvent — input callback slot
//
// Init(app) installs the library's own handlers into those slots — we
// never touch them.

void android_main(struct android_app* app) {
    std::cout << "*-*-.,.,.,.--*---**-*-*--*-*-*--*-*\n";
    std::cout << "  KernelNucleusT — Vulkan infrastructure\n";
    std::cout << "  (Android)\n";
    std::cout << "*-*-.,.,.,.--*---**-*-*--*-*-*--*-*\n\n";

    // ---- Step 1: platform layer ----
    // Only API difference from desktop: pass `app` so the library can
    // reach the native window, ALooper and input queue. Blocks until
    // the ANativeWindow is ready.
    KnstWindowSources::Init(app);
    if (KnstWindowSources::active_backend() == knst_backend::none) {
        std::cerr << "Failed to initialize the platform layer.\n";
        return;
    }

    // ---- Step 2: VkInstance ----
    if (!create_instance()) {
        KnstWindowSources::CleanUp();
        return;
    }

    // ---- Step 3: window ----
    // On Android the window is the activity's surface — width/height
    // are ignored.
    knst_window window(800, 600, u"Knst Vulkan Test");
    window.set_redraw_callback(on_frame);
    window.creation();

    // ---- Step 4: VkSurfaceKHR ----
    // knst_window_vulkan_content creates the platform-specific surface:
    //   Windows : vkCreateWin32SurfaceKHR
    //   X11     : vkCreateXlibSurfaceKHR
    //   Wayland : vkCreateWaylandSurfaceKHR
    //   Android : vkCreateAndroidSurfaceKHR
    // Same API on every platform.
    knst_window_vulkan_content vk;
    if (!vk.Init(&window, g_instance)) {
        std::cerr << "  [!] VkSurfaceKHR creation failed.\n";
        cleanup_vulkan();
        window.destroy();
        KnstWindowSources::CleanUp();
        return;
    }
    g_surface = vk.GetSurface();

    std::cout << "\nVkSurfaceKHR created: " << g_surface << "\n";

    // ---- Step 5: physical device check ----
    enumerate_devices();

    std::cout << "\n--- Entering event loop ----------------\n";
    std::cout << "  Back button to quit.\n\n";

    // ---- Step 6: main loop ----
    // Identical to desktop — no `&& app->destroyRequested == 0` check.
    //
    // Android can close the app on its own (Recents swipe, low-memory
    // killer, force-stop). is_should_close() checks destroyRequested
    // internally and returns true, so user code never touches the
    // Android API.
    //
    // non_block_pool_event() blocks while backgrounded — this loop uses
    // ~0% CPU when invisible.
    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        window.call_redraw_callback();

        if (!window.is_frame_ready()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        } else {
            std::this_thread::yield();
        }
    }

    std::cout << "\n--- Closed -----------------------------\n";

    // ---- Step 7: teardown in reverse order ----
    // surface -> instance -> window -> platform
    //
    // should_close() was already called by the library (via back button
    // or destroyRequested). The library also triggers
    // ANativeActivity_finish automatically — the activity closes clean.
    vk.Shutdown(); // VkSurfaceKHR
    g_surface = VK_NULL_HANDLE;

    cleanup_vulkan();  // VkInstance

    window.destroy();
    KnstWindowSources::CleanUp();

    std::cout << "Done.\n";
}