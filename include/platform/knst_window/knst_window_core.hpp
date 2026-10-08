// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0



/*
----------------------------
knst_window_core.hpp
----------------------------

    The core structure of the window library is here—structures, classes, and the like...

*/



#pragma once




#include <chrono>
#include <memory>
#if KNST_USING_LINUX_PLATFORM_X11
    #include <xcb/sync.h>
#endif

#include "knst_display.hpp"



#if defined(KNST_USING_PLATFORM_ANDROID)

    class KnstWindowSources;    
    #include <mutex>

    class knst_mobile_keyboard { // android keyborad
    private:
        friend KnstWindowSources;
        static inline JavaVM* s_javaVM = nullptr;
        static inline jobject s_activity = nullptr;
        static inline jclass s_activityClass = nullptr;
        static inline jclass s_immClass = nullptr;
        static inline jmethodID s_getSystemService = nullptr;
        static inline jmethodID s_showSoftInput = nullptr;
        static inline jmethodID s_hideSoftInput = nullptr;
        static inline jmethodID s_getWindow = nullptr;
        static inline jmethodID s_getDecorView = nullptr;
        static inline jmethodID s_getWindowToken = nullptr;
        static inline bool s_initialized = false;
        static inline bool s_visible = false;
        static inline std::mutex s_mutex;

        struct jni_scope {
            JNIEnv* env = nullptr;
            bool attached = false;
            JavaVM* vm = nullptr;

            jni_scope(JavaVM* vm);
            ~jni_scope();

            JNIEnv* get() const { return env; }
            bool valid() const { return env != nullptr; }
        };
        static void Init(struct android_app* app);
        static void shutdown();
    public:
        
        static bool show();
        static bool hide();
        static void toggle();
        static bool is_visible();
        
    };


#endif


#if KNST_USING_LINUX_PLATFORM_WAYLAND
        
        

        enum class resize_edge
        {
            none,

            left,
            right,
            top,
            bottom,
            top_left,
            top_right,
            bottom_left,
            bottom_right
        };

        #include "../linux/wayland/knst_window_wayland_event_manager.hpp"

#endif




class knst_window;

class KnstWindowSources{

        public:
        friend struct knst_window_event_system;
        friend class knst_window;
        
        #if KNST_USING_PLATFORM_WINDOWS

            static inline HINSTANCE m_hInstance;
            static inline WNDCLASSEXW m_wc = {0};

        #elif KNST_USING_LINUX_PLATFORM_X11
            friend KNST_FORCE_INLINE void load_native_to_knst_event(knst_window& window,xcb_generic_event_t* ev) noexcept;
            friend class knst_display;
            static inline xcb_connection_t* m_connection = nullptr;
            static inline Display* m_xlib_display = nullptr; 

            static inline xcb_window_t m_root = 0;
            static inline xcb_atom_t m_wmDelete = 0;
            static inline xcb_key_symbols_t* m_keysyms = nullptr; 
            static inline xcb_atom_t m_netMoveResizeWindow = 0;
            
            static inline xcb_atom_t m_NET_WM_STATE = 0;
            static inline xcb_atom_t m_NET_WM_STATE_FULLSCREEN = 0;
            static inline xcb_atom_t m_NET_WM_STATE_MAXIMIZED_VERT = 0;
            static inline xcb_atom_t m_NET_WM_STATE_MAXIMIZED_HORZ = 0;
            static inline xcb_atom_t m_NET_WM_STATE_HIDDEN = 0;
            static inline xcb_atom_t m_wmChangeState = 0;
            static inline xcb_atom_t m_NET_ACTIVE_WINDOW = 0;
            
            static inline xcb_atom_t m_CLIPBOARD = 0;
            static inline xcb_atom_t m_UTF8_STRING = 0;
            static inline xcb_atom_t m_TARGETS = 0;
            static inline xcb_atom_t m_ATOM = 0;
            static inline xcb_atom_t m_PRIMARY = 0;
            static inline xcb_atom_t m_XdndAware = 0;
            static inline xcb_atom_t m_NET_WM_SYNC_REQUEST = 0;
            static inline xcb_atom_t m_NET_WM_SYNC_REQUEST_COUNTER = 0;
            static inline int m_xsync_event_base = 0;
            static inline int m_xsync_error_base = 0;

            static inline xcb_atom_t m_XdndEnter = 0;
            static inline xcb_atom_t m_XdndPosition = 0;
            static inline xcb_atom_t m_XdndStatus = 0;
            static inline xcb_atom_t m_XdndDrop = 0;
            static inline xcb_atom_t m_XdndFinished = 0;
            static inline xcb_atom_t m_XdndSelection = 0;
            static inline xcb_atom_t m_XdndActionCopy = 0;
            static inline xcb_atom_t m_XdndTypeList = 0;
            static inline xcb_atom_t m_textUriList = 0;
            static inline xcb_atom_t m_XdndLeave = 0;
            static inline xcb_atom_t m_MOTIF_WM_HINTS = 0;
            static inline xcb_atom_t m_NET_WM_MOVERESIZE = 0;

            static inline xcb_atom_t m_wmSyncRequest = XCB_NONE;
            static inline xcb_atom_t m_wmSyncRequestCounter = XCB_NONE;
            #elif KNST_USING_LINUX_PLATFORM_WAYLAND

            static inline struct zwlr_layer_shell_v1* layer_shell = nullptr;
            static inline uint32_t drag_enter_serial = 0;
            static inline struct wl_data_device_manager* data_device_manager = nullptr;
            static inline struct wl_data_device* data_device = nullptr;

           
            static inline struct wl_data_source* clipboard_source = nullptr;
            static inline knst_c16string clipboard_pending_text;

           
            static inline struct wl_data_offer* selection_offer = nullptr;

           
            static inline struct wl_data_offer* drag_offer = nullptr;
            static inline bool drag_offer_has_uri_list = false;
            static inline struct wl_data_offer* pending_offer = nullptr;  
            static inline bool pending_offer_has_uri_list = false;
            static inline knst_window* drag_target_window = nullptr;
            static inline uint32_t keyboard_serial = 0;
            friend class knst_window_wayland_funcs;
            friend inline void knst_display::refresh_screens() noexcept;
            
            static inline wl_registry * registery = nullptr;
            static inline wl_compositor * compositor = nullptr;
            static inline struct xdg_wm_base* wmBase = nullptr;
            static inline struct wl_shm* shm = nullptr;
            static inline struct wl_seat* seat = nullptr;
            static inline wl_pointer* pointer = nullptr;
            static inline knst_window* active_window = nullptr;
            static inline wl_keyboard* keyboard = nullptr;                 
            static inline knst_window* keyboard_focus_window = nullptr;    
            static inline xkb_context* xkb_ctx = nullptr;                   
            static inline xkb_keymap*  xkb_map = nullptr;                 
            static inline xkb_state*   xkb_st  = nullptr;                  
            static inline struct zwp_pointer_constraints_v1* pointer_constraints = nullptr;
            static inline struct zwp_relative_pointer_manager_v1* relative_pointer_manager = nullptr;
            static inline struct zwp_locked_pointer_v1* locked_pointer = nullptr;
            static inline struct zwp_confined_pointer_v1* confined_pointer = nullptr;

            

            static inline wl_cursor_theme* cursor_theme = nullptr;

            static inline wl_surface* cursor_surface = nullptr;
            static inline wl_cursor* cursor_default = nullptr;
            static inline wl_cursor* cursor_ew = nullptr;
            static inline wl_cursor* cursor_ns = nullptr;
            static inline wl_cursor* cursor_nwse = nullptr;
            static inline wl_cursor* cursor_nesw = nullptr;
            static inline void RegistryAdd(void* data,wl_registry* registry,uint32_t name, const char* interface,uint32_t version){
    

            if (strcmp(interface, wl_compositor_interface.name) == 0)
            {
                compositor = (wl_compositor*)wl_registry_bind(
                    registry,
                    name,
                    &wl_compositor_interface,
                    4);
            }
            else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
            {
                wmBase = (xdg_wm_base*)wl_registry_bind(
                    registry,
                    name,
                    &xdg_wm_base_interface,
                    1);
            }
            else if (strcmp(interface, wl_shm_interface.name) == 0)
            {
                shm = (wl_shm*)wl_registry_bind(
                    registry,
                    name,
                    &wl_shm_interface,
                    1);
            }

            else if (strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0) {
                pointer_constraints = (struct zwp_pointer_constraints_v1*)wl_registry_bind(
                    registry, name, &zwp_pointer_constraints_v1_interface, 1);
            }
            else if (strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0) {
                relative_pointer_manager = (struct zwp_relative_pointer_manager_v1*)wl_registry_bind(
                    registry, name, &zwp_relative_pointer_manager_v1_interface, 1);
            }
            else if (strcmp(interface, wl_seat_interface.name) == 0)
            {
                uint32_t bind_ver = (version > 7) ? 7 : version;

                seat = (wl_seat*)wl_registry_bind(
                    registry,
                    name,
                    &wl_seat_interface,
                    bind_ver);
            }
            else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
                data_device_manager = (struct wl_data_device_manager*)wl_registry_bind(
                    registry, name, &wl_data_device_manager_interface, 3);
            }
            else if (strcmp(interface, wl_output_interface.name) == 0) {
               
                knst_monitor mon;
                mon.output_id = name;
                
                uint32_t bind_ver = (version > 3) ? 3 : version;
                mon.output = (wl_output*)wl_registry_bind(registry, name, &wl_output_interface, bind_ver);
                
                knst_display::m_monitor_list.push_back(mon);
                
               
                if (mon.output) {
                    wl_output_add_listener(
                        knst_display::m_monitor_list.back().output,
                        &knst_display::wayland_output_listener,
                        &knst_display::m_monitor_list.back()
                    );
                }
            }
            
        }

        static inline const wl_registry_listener registryListener =
        {
            RegistryAdd,
            nullptr
        };
        #elif defined(KNST_USING_PLATFORM_ANDROID)
            
            friend KNST_FORCE_INLINE void handle_android_cmd(int32_t cmd);
            friend KNST_FORCE_INLINE int32_t handle_android_input(AInputEvent* event);

            friend class knst_display;
            static inline struct android_app* m_app;
           
        #endif

       
        

    public:


        KNST_FORCE_INLINE static uint32_t get_current_time_ms() noexcept {
            using namespace std::chrono;
            return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        }

        #if KNST_USING_PLATFORM_WINDOWS

            KNST_FORCE_INLINE static HINSTANCE& get_windows_native_instance_handle() noexcept{
                return m_hInstance;
            }

        #elif KNST_USING_LINUX_PLATFORM_X11

            KNST_FORCE_INLINE static xcb_window_t& get_native_x11_root_handle() noexcept{

                return m_root;
            }

            KNST_FORCE_INLINE static xcb_connection_t* get_native_x11_connection_handle() noexcept{

                return m_connection;
            }

            KNST_FORCE_INLINE static Display* get_native_x11_display() noexcept{

                return m_xlib_display;
            }

            

        #elif KNST_USING_LINUX_PLATFORM_WAYLAND

            static inline wl_display* wayland_display = nullptr;
        
        #elif defined(KNST_USING_PLATFORM_ANDROID)
            KNST_FORCE_INLINE static android_app* get_android_app() noexcept{

                return m_app;
            }
        
        #else
            
        #endif
        
        #if defined(KNST_USING_PLATFORM_ANDROID)
            KNST_FORCE_INLINE static void Init(struct android_app* app) noexcept {
        #else
            KNST_FORCE_INLINE static void Init() noexcept {
        #endif
            #if KNST_USING_PLATFORM_WINDOWS

                m_hInstance = GetModuleHandleW(nullptr);
                m_wc.cbSize = sizeof(WNDCLASSEXW);
                m_wc.lpfnWndProc = load_native_to_knst_event;
                m_wc.hInstance = m_hInstance;
                m_wc.lpszClassName = L"KnstWindowClass";         
                m_wc.hCursor = LoadCursor(NULL, IDC_ARROW);
                m_wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
                m_wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_OWNDC;
                RegisterClassExW(&m_wc);    
            

            #elif KNST_USING_LINUX_PLATFORM_X11

                m_xlib_display = XOpenDisplay(nullptr);

                Bool supported = False;
                XkbSetDetectableAutoRepeat(m_xlib_display, True, &supported);

                XSetEventQueueOwner(m_xlib_display, XCBOwnsEventQueue);
                m_connection = XGetXCBConnection(m_xlib_display);
                if (xcb_connection_has_error(m_connection)) {
                    return ;
                }
                const xcb_setup_t* setup = xcb_get_setup(m_connection);
                xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
                xcb_screen_t* screen = iter.data;
                m_root = screen->root;


                auto intern = [](const char* name) -> xcb_atom_t {
                    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(m_connection, 0, (uint16_t)strlen(name), name);
                    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(m_connection, cookie, nullptr);
                    xcb_atom_t atom = 0;
                    if (reply) {
                        atom = reply->atom;
                        free(reply);
                    }
                    return atom;
                };

                auto c1 = xcb_intern_atom(m_connection, 0, strlen("_NET_WM_SYNC_REQUEST"), "_NET_WM_SYNC_REQUEST");
                auto r1 = xcb_intern_atom_reply(m_connection, c1, nullptr);
                if (r1) { m_wmSyncRequest = r1->atom; free(r1); }

                auto c2 = xcb_intern_atom(m_connection, 0, strlen("_NET_WM_SYNC_REQUEST_COUNTER"), "_NET_WM_SYNC_REQUEST_COUNTER");
                auto r2 = xcb_intern_atom_reply(m_connection, c2, nullptr);
                if (r2) { m_wmSyncRequestCounter = r2->atom; free(r2); }


                m_wmDelete = intern("WM_DELETE_WINDOW");
                m_netMoveResizeWindow = intern("_NET_MOVERESIZE_WINDOW");
                m_NET_WM_STATE  = intern("_NET_WM_STATE");
                m_NET_WM_STATE_FULLSCREEN = intern("_NET_WM_STATE_FULLSCREEN");
                m_NET_WM_STATE_MAXIMIZED_VERT = intern("_NET_WM_STATE_MAXIMIZED_VERT");
                m_NET_WM_STATE_MAXIMIZED_HORZ = intern("_NET_WM_STATE_MAXIMIZED_HORZ");
                m_NET_WM_STATE_HIDDEN  = intern("_NET_WM_STATE_HIDDEN");
                m_wmChangeState = intern("WM_CHANGE_STATE");
                m_NET_ACTIVE_WINDOW = intern("_NET_ACTIVE_WINDOW");
                m_MOTIF_WM_HINTS = intern("_MOTIF_WM_HINTS");
                m_CLIPBOARD = intern("CLIPBOARD");
                m_UTF8_STRING  = intern("UTF8_STRING");
                m_TARGETS = intern("TARGETS");
                m_ATOM = intern("ATOM");
                m_PRIMARY = intern("PRIMARY");
                m_NET_WM_MOVERESIZE = intern("_NET_WM_MOVERESIZE");
                m_XdndAware = intern("XdndAware");
                m_XdndEnter= intern("XdndEnter");
                m_XdndPosition = intern("XdndPosition");
                m_XdndStatus = intern("XdndStatus");
                m_XdndDrop = intern("XdndDrop");
                m_XdndFinished  = intern("XdndFinished");
                m_XdndSelection  = intern("XdndSelection");
                m_XdndActionCopy = intern("XdndActionCopy");
                m_XdndTypeList  = intern("XdndTypeList");
                m_textUriList = intern("text/uri-list");
                m_XdndLeave = intern("XdndLeave");


                

                m_keysyms = xcb_key_symbols_alloc(m_connection);


           #elif KNST_USING_LINUX_PLATFORM_WAYLAND

                wayland_display = wl_display_connect(nullptr);

                if (!wayland_display)
                {
                    return;
                }

                registery = wl_display_get_registry(wayland_display);
                if (!registery)
                {
                    wl_display_disconnect(wayland_display);
                    wayland_display = nullptr;
                    return;
                }

                wl_registry_add_listener(
                    registery,
                    &registryListener,
                    nullptr
                );

                wl_display_roundtrip(wayland_display);

                
                if (!compositor || !wmBase)
                {
                    return;
                }
                xdg_wm_base_add_listener(wmBase, &knst_window_wayland_funcs::wmBaseListener, nullptr);
                
                if (seat)
                {
                    wl_seat_add_listener(
                        seat,
                        &knst_window_wayland_funcs::seatListener,
                        nullptr
                    );
                }

                xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

            
                
                wl_display_roundtrip(wayland_display);


                if (data_device_manager && seat) {
                    data_device = wl_data_device_manager_get_data_device(data_device_manager, seat);
                    if (data_device) {
                        wl_data_device_add_listener(data_device, &knst_window_wayland_funcs::dataDeviceListener, nullptr);
                    }
                }

                wl_display_roundtrip(wayland_display);
                wl_display_roundtrip(wayland_display);

                if (shm)
                {
                    cursor_theme =
                        wl_cursor_theme_load(
                            nullptr,
                            24,
                            shm
                        );
                }

                if (cursor_theme)
                {
                    cursor_default =
                        wl_cursor_theme_get_cursor(
                            cursor_theme,
                            "default"
                        );

                    cursor_ew =
                        wl_cursor_theme_get_cursor(
                            cursor_theme,
                            "ew-resize"
                        );

                    cursor_ns =
                        wl_cursor_theme_get_cursor(
                            cursor_theme,
                            "ns-resize"
                        );

                    cursor_nwse =
                        wl_cursor_theme_get_cursor(
                            cursor_theme,
                            "nwse-resize"
                        );

                    cursor_nesw =
                        wl_cursor_theme_get_cursor(
                            cursor_theme,
                            "nesw-resize"
                        );

                    cursor_surface =
                        wl_compositor_create_surface(
                            compositor
                        );
                }
           #elif defined(KNST_USING_PLATFORM_ANDROID)


    
    
                while (app->window == nullptr) {
                
                    
                    int events;
                    struct android_poll_source* source = nullptr;
                    
                
                    int pollResult = ALooper_pollOnce(0, nullptr, &events, (void**)&source);
                    
                    if (pollResult >= 0) {
                    
                        
                        if (source) {
                        
                            source->process(app, source);
                            
                        } 
                    }
                    
                
                    if (app->destroyRequested != 0) {
                    
                        return;
                    }
                    
                    
                }
            
                
                m_app = app;
                int width = ANativeWindow_getWidth(app->window);
                int height = ANativeWindow_getHeight(app->window);
                knst_mobile_keyboard::Init(app);
           
            #endif

                knst_display::refresh_screens();
                
            }

        KNST_FORCE_INLINE static void CleanUp() noexcept {

            #if KNST_USING_PLATFORM_WINDOWS

                if(m_hInstance){
                    m_hInstance = nullptr;
                }


            #elif KNST_USING_LINUX_PLATFORM_X11

                if (m_keysyms) {
                    xcb_key_symbols_free(m_keysyms);
                    m_keysyms = nullptr;
                }
            
                if (m_xlib_display) {
                    XCloseDisplay(m_xlib_display);
                    m_xlib_display = nullptr;
                }
                
                m_connection = nullptr;
                
                m_root = 0;
                m_wmDelete = 0;
                m_netMoveResizeWindow = 0;
                m_NET_WM_STATE = 0;
                m_NET_WM_STATE_FULLSCREEN = 0;
                m_NET_WM_STATE_MAXIMIZED_VERT = 0;
                m_NET_WM_STATE_MAXIMIZED_HORZ = 0;
                m_NET_WM_STATE_HIDDEN = 0;
                m_wmChangeState = 0;
                m_NET_ACTIVE_WINDOW = 0;
                
                m_CLIPBOARD = 0;
                m_UTF8_STRING = 0;
                m_TARGETS = 0;
                m_ATOM = 0;
                m_PRIMARY = 0;
                

                m_NET_WM_MOVERESIZE = 0;
                m_XdndAware = 0;
                m_XdndEnter = 0;
                m_XdndPosition = 0;
                m_XdndStatus = 0;
                m_XdndDrop = 0;
                m_XdndFinished = 0;
                m_XdndSelection = 0;
                m_XdndActionCopy = 0;
                m_XdndTypeList = 0;
                m_textUriList = 0;
                m_XdndLeave = 0;


            #elif KNST_USING_LINUX_PLATFORM_WAYLAND
    

                for (auto& mon : knst_display::m_monitor_list) {
                    if (mon.output) {
                        wl_output_destroy(mon.output);
                        mon.output = nullptr;
                    }
                }
                knst_display::m_monitor_list.clear();


                if (pointer)
                {
                    wl_pointer_destroy(pointer);
                    pointer = nullptr;
                }

                active_window = nullptr;

                if (keyboard) { wl_keyboard_destroy(keyboard); keyboard = nullptr; }
                if (xkb_st)  { xkb_state_unref(xkb_st);   xkb_st  = nullptr; }
                if (xkb_map) { xkb_keymap_unref(xkb_map); xkb_map = nullptr; }
                if (xkb_ctx) { xkb_context_unref(xkb_ctx); xkb_ctx = nullptr; }
                keyboard_focus_window = nullptr;

      
                if (locked_pointer) {
                    zwp_locked_pointer_v1_destroy(locked_pointer);
                    locked_pointer = nullptr;
                }
                if (confined_pointer) {
                    zwp_confined_pointer_v1_destroy(confined_pointer);
                    confined_pointer = nullptr;
                }
                if (pointer_constraints) {
                    zwp_pointer_constraints_v1_destroy(pointer_constraints);
                    pointer_constraints = nullptr;
                }
                if (relative_pointer_manager) {
                    zwp_relative_pointer_manager_v1_destroy(relative_pointer_manager);
                    relative_pointer_manager = nullptr;
                }


                if (cursor_surface)
                {
                    wl_surface_destroy(cursor_surface);
                    cursor_surface = nullptr;
                }


               

                if (cursor_theme)
                {
                    wl_cursor_theme_destroy(cursor_theme);
                    cursor_theme = nullptr;
                }

                cursor_default = nullptr;
                cursor_ew = nullptr;
                cursor_ns = nullptr;
                cursor_nwse = nullptr;
                cursor_nesw = nullptr;


              

                if (seat)
                {
                    wl_seat_destroy(seat);
                    seat = nullptr;
                }



                if (shm)
                {
                    wl_shm_destroy(shm);
                    shm = nullptr;
                }


               

                if (wmBase)
                {
                    xdg_wm_base_destroy(wmBase);
                    wmBase = nullptr;
                }



                if (compositor)
                {
                    wl_compositor_destroy(compositor);
                    compositor = nullptr;
                }


                if (registery)
                {
                    wl_registry_destroy(registery);
                    registery = nullptr;
                }

                if (clipboard_source) { wl_data_source_destroy(clipboard_source); clipboard_source = nullptr; }
                if (selection_offer) { wl_data_offer_destroy(selection_offer);   selection_offer = nullptr; }
                if (drag_offer){ wl_data_offer_destroy(drag_offer);  drag_offer = nullptr; }
                if (data_device) { wl_data_device_release(data_device);  data_device = nullptr; }
                if (data_device_manager) { wl_data_device_manager_destroy(data_device_manager); data_device_manager = nullptr; }

                if (wayland_display)
                {
                    wl_display_flush(
                        wayland_display
                    );

                    wl_display_disconnect(
                        wayland_display
                    );

                    wayland_display = nullptr;
                }

                pending_offer = nullptr;
               
            #elif defined(KNST_USING_PLATFORM_ANDROID)
                m_app = nullptr;
                knst_mobile_keyboard::shutdown();
            #endif

        }



};






struct knst_window_event {

    #ifndef KNST_MAX_HELD_KEYS
    #define KNST_MAX_HELD_KEYS 8
    #endif


    struct knst_held_key {
        int32_t key_code = 0;
        int32_t scancode = 0;
        uint32_t last_key_time = 0;
        uint32_t last_repeat_time = 0;
        bool repeat_initialized = false;
        bool active = false;
    };


    #if KNST_USING_PLATFORM_WINDOWS
        friend KNST_FORCE_INLINE LRESULT CALLBACK load_native_to_knst_event(
            HWND, UINT, WPARAM, LPARAM) noexcept;
    #elif KNST_USING_LINUX_PLATFORM_X11
        friend KNST_FORCE_INLINE void load_native_to_knst_event(
            knst_window&, xcb_generic_event_t*) noexcept;
    #elif KNST_USING_LINUX_PLATFORM_WAYLAND
        friend class knst_window_wayland_funcs;
        friend class knst_window;
    #elif defined(KNST_USING_PLATFORM_ANDROID)
        friend KNST_FORCE_INLINE int32_t input_callback(
            struct android_app*, AInputEvent*);
    #endif



    friend class knst_window_event_system;
    friend class knst_window;

    #ifndef KNST_WINDOW_KEY_REPEAT_DELAY
        #define KNST_WINDOW_KEY_REPEAT_DELAY 100
    #endif
    #ifndef KNST_WINDOW_KEY_REPEAT_INTERVAL
        #define KNST_WINDOW_KEY_REPEAT_INTERVAL 23
    #endif

    static constexpr uint32_t KEY_REPEAT_DELAY = KNST_WINDOW_KEY_REPEAT_DELAY;
    static constexpr uint32_t KEY_REPEAT_INTERVAL = KNST_WINDOW_KEY_REPEAT_INTERVAL;


    uint32_t type = 0;
    uint32_t timestamp_ms = 0;


    int32_t mods = 0;
    int32_t mouse_x = 0;
    int32_t mouse_y = 0;
    int32_t mouse_root_x = 0;
    int32_t mouse_root_y = 0;
    int32_t window_width = 0;
    int32_t window_height = 0;
    int32_t window_root_x = 0;
    int32_t window_root_y = 0;
    bool is_focused = false;
    bool mouse_on_window = false;
    bool is_full_screen = false;
    bool is_maximized = false;
    bool is_minimized = false;


    union {
        struct {
            int32_t mouse_button;
            int32_t mouse_action;
            int32_t mouse_scroll_delta;
        };
        struct {
            int32_t key_code;
            int32_t scancode;
            int32_t key_action;
        };
    };


    std::shared_ptr<knst_vector<knst_c16string>> drop_files;
    uint32_t drop_count = 0;


    #if defined(KNST_USING_PLATFORM_ANDROID)
        int pointer_count = 0;

        float pointer_x[10] = {0};
        float pointer_y[10] = {0};
        int pointer_id[10] = {0};


        int touch_action = 0;
        int content_left = 0;
        int content_top = 0;
        int content_right = 0;
        int content_bottom = 0;
        int orientation = 0;



        char language[4] = {0};
        char country[4] = {0};
        bool is_night_mode = false;
        bool is_low_memory = false;
        float density = 1.0f;
        int screen_width_dp = 0;
        int screen_height_dp = 0;
        void* saved_state = nullptr;
        size_t saved_state_size = 0;
    #endif

    KNST_FORCE_INLINE void begin(uint32_t ev_type) noexcept {
        type = ev_type;
        timestamp_ms = KnstWindowSources::get_current_time_ms();

        mouse_button = 0;
        mouse_action = 0;

        mouse_scroll_delta = 0;

        key_code = 0;
        scancode = 0;
        key_action = 0;
        drop_files.reset();
        drop_count = 0;
    }
};



template<size_t N>
struct knst_event_ring {
    static_assert((N & (N - 1)) == 0, "KNST event ring size must be a power of two");

    knst_window_event data[N];
    size_t count = 0;
    size_t head  = 0;

    KNST_FORCE_INLINE void push(const knst_window_event& ev) noexcept {
        data[head] = ev;
        head = (head + 1) & (N - 1);
        if (count < N) ++count;
    }

    KNST_FORCE_INLINE size_t size() const noexcept { return count; }

    KNST_FORCE_INLINE const knst_window_event& at(size_t i) const noexcept {
        size_t start = (head + N - count) & (N - 1);
        return data[(start + i) & (N - 1)];
    }

    KNST_FORCE_INLINE void clear() noexcept {
        count = 0;
        head  = 0;
    }
};

#if KNST_USING_PLATFORM_WINDOWS

    #include "../windows/knst_display_win32.hpp"


#elif KNST_USING_LINUX_PLATFORM_X11

    #include "../linux/x11/knst_display_x11.hpp"
    #include <xcb/randr.h>

#elif KNST_USING_LINUX_PLATFORM_WAYLAND

    #include "../linux/wayland/knst_display_wayland.hpp"



#endif




class knst_window{



private:


    


    #if KNST_USING_PLATFORM_WINDOWS

        friend KNST_FORCE_INLINE LRESULT CALLBACK load_native_to_knst_event(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
       
        friend class knst_window_event_system;

        HWND m_window;
        HCURSOR m_cursor = nullptr;          
        HCURSOR m_system_cursor = nullptr; 
        HICON m_icon_big = nullptr;
        WINDOWPLACEMENT m_prevPlacement = {};
        LONG m_prevStyle = 0;
        IDropTarget* m_drop_target = nullptr;
        friend class knst_drop_target;


        
    #elif KNST_USING_LINUX_PLATFORM_X11

        friend KNST_FORCE_INLINE void load_native_to_knst_event(knst_window& window,xcb_generic_event_t* ev) noexcept;
    
        friend struct knst_window_event_system;
        xcb_window_t m_window = 0;
        xcb_window_t m_xdnd_source = 0;
        xcb_atom_t m_xdnd_selected_type = 0;
        int m_xdnd_version = 5;

        static const int RESIZE_BORDER = 8;
        enum knst_edge_zone {
            ZONE_NONE, ZONE_TOP, ZONE_BOTTOM, ZONE_LEFT, ZONE_RIGHT,
            ZONE_TOP_LEFT, ZONE_TOP_RIGHT, ZONE_BOTTOM_LEFT, ZONE_BOTTOM_RIGHT
        };
        knst_edge_zone m_hovered_edge = ZONE_NONE;
        Cursor m_current_x11_cursor = 0;
        

        knst_edge_zone detect_edge_zone(int x, int y) const noexcept;
        uint32_t edge_to_moveresize_direction(knst_edge_zone zone) const noexcept;
        unsigned int edge_to_x11_cursor_shape(knst_edge_zone zone) const noexcept;
        void update_edge_cursor(int x, int y) noexcept;
        void start_move_or_resize(int root_x, int root_y, uint32_t direction) noexcept;
       
        


        

    #elif KNST_USING_LINUX_PLATFORM_WAYLAND
        
        friend class knst_window_wayland_funcs;
        friend struct knst_window_event_system;
        
       
        wl_surface * m_surface = nullptr;
        xdg_surface * xdgSurface = nullptr;
        xdg_toplevel * toplevel = nullptr;



        static inline knst_vector<knst_window*> s_windows;

        static knst_window* find_from_surface(
        wl_surface* surface) noexcept;

        
       

        resize_edge m_resize_edge = resize_edge::none;

        

        uint32_t m_pointer_serial = 0;
        bool m_pointer_pressed = false;


       
        knst_byte_string m_pending_cursor_data;
        int m_pending_cursor_w = 0;
        int m_pending_cursor_h = 0;
        int m_pending_cursor_hot_x = -1;
        int m_pending_cursor_hot_y = -1;
        bool m_has_pending_cursor = false;
        bool m_using_custom_cursor = false;  
        void apply_bmp_cursor_now(const knst_byte_string& data, int width, int height, int hot_x, int hot_y) noexcept;


        void apply_wayland_configure_size(
            int width,
            int height
        ) noexcept;

        resize_edge get_resize_edge(
            double x,
            double y
        ) const noexcept;

            bool m_always_on_top = false;
                struct zwlr_layer_surface_v1* m_layer_surface = nullptr;

        
    #elif defined(KNST_USING_PLATFORM_ANDROID)

       
        friend struct knst_window_event_system;
        friend KNST_FORCE_INLINE void cmd_callback(struct android_app* app, int32_t cmd);
        friend KNST_FORCE_INLINE int32_t input_callback(struct android_app* app, AInputEvent* event);
        
    #endif
                



    
    bool m_input_transparent = false;

    
    knst_window_event m_knst_event;

    knst_window_event::knst_held_key m_held_keys[KNST_MAX_HELD_KEYS];

    knst_event_ring<256> m_events;

    knst_c16string m_title;

    bool m_should_close;

    bool m_disconnected = false;

    float m_opacity;

    void* m_user_data;

    knst_function m_redraw_callback;

    knst_c16string clipboard_text;

    bool m_drag_drop_enabled = false; 

    int m_custom_title_bar_height; // -1 default

    bool m_draw_custom_title_bar;



public:
    double m_mouse_x = 0.0;
    double m_mouse_y = 0.0;


    #if KNST_USING_LINUX_PLATFORM_X11
        xcb_sync_counter_t m_syncCounter = XCB_NONE;
        xcb_sync_int64_t m_syncPendingValue{0, 0};
        bool m_syncHasPendingValue = false;
        bool m_syncRequestReceived = false;
    #endif
    
    KNST_FORCE_INLINE const knst_c16string& get_clipboard() const noexcept{
        return clipboard_text;
    }

    KNST_FORCE_INLINE void clear_clipboard() noexcept{
        clipboard_text = "";
    }

    KNST_FORCE_INLINE const float& get_opacity() const noexcept {
        return m_opacity;
    }

    KNST_FORCE_INLINE void set_title_bar_height(int height) noexcept {
        m_custom_title_bar_height = height;
    }
    
    KNST_FORCE_INLINE int get_title_bar_height() const noexcept {
        if (m_custom_title_bar_height > 0) {
            return m_custom_title_bar_height;
        }
        
        constexpr float base_height = 34.0f;
        constexpr float base_dpi = 96.0f;
        
        const knst_monitor* mon = knst_display::get_primary_monitor();
        if (!mon || mon->dpi_scale <= 0.0f) {
            return (int)base_height;
        }
        
        float ratio = mon->dpi_scale / base_dpi;
        return (int)(base_height * ratio);
    }

    #if KNST_USING_PLATFORM_WINDOWS
        
        KNST_FORCE_INLINE const HWND& get_windows_window_handle() const noexcept{
            return m_window;
        }

    #elif KNST_USING_LINUX_PLATFORM_X11

        KNST_FORCE_INLINE const xcb_window_t& get_x11_window_handle() const noexcept{
            return m_window;
        }
    #elif KNST_USING_LINUX_PLATFORM_WAYLAND

        KNST_FORCE_INLINE const wl_surface * get_wayland_surface_handle() const noexcept{
            return m_surface;
        }

    #endif


    template<typename Callback>
    KNST_FORCE_INLINE void set_redraw_callback(Callback&& callback) noexcept {
        auto* self = this;

        m_redraw_callback = [self, cb = std::forward<Callback>(callback)]() {
            cb(*self, self->m_user_data);
        };
    }

   
    KNST_FORCE_INLINE void call_redraw_callback() noexcept {
        if (m_redraw_callback) m_redraw_callback();
    }
    

    KNST_FORCE_INLINE ~knst_window() noexcept{
        destroy();
    }
    #if !defined(KNST_USING_PLATFORM_ANDROID)
    KNST_FORCE_INLINE knst_window(int width = 800, int height = 800,knst_c16string title = u"Knst_Window",int root_x = KNST_WINDOW_DEFAULT, int root_y = KNST_WINDOW_DEFAULT, 
        const knst_monitor& monitor = knst_monitor()) noexcept
        : m_title(title), 
          m_should_close(false), 
          m_opacity(1.0f),
          m_redraw_callback(),
          m_custom_title_bar_height(-1),m_draw_custom_title_bar(false) {
        

        if (monitor.physical_height > 0 && monitor.physical_width > 0) {
            if (root_x < 0 && root_y < 0) {
                root_x = monitor.root_x + (monitor.width - width) / 2;
                root_y = monitor.root_y + (monitor.height - height) / 2;
            } else {
                root_x = monitor.root_x + root_x;
                root_y = monitor.root_y + root_y;
            }
        }

        m_knst_event.window_width = width;
        m_knst_event.window_height = height;
        m_knst_event.window_root_x = root_x;
        m_knst_event.window_root_y = root_y;



    }
    #else

    KNST_FORCE_INLINE knst_window(int width = KNST_WINDOW_DEFAULT, int height = KNST_WINDOW_DEFAULT,knst_c16string title = u"Knst_Window",int root_x = KNST_WINDOW_DEFAULT, int root_y = KNST_WINDOW_DEFAULT, 
        const knst_monitor& monitor = knst_monitor())
        : m_title(title),
          m_should_close(false),
          m_opacity(1.0f),
          m_user_data(nullptr),
          m_custom_title_bar_height(-1),
          m_draw_custom_title_bar(false) {
            (void)width; (void)height; (void)root_x; (void)root_y; (void)monitor;
        }

    #endif

  
    knst_window(const knst_window&) = delete;
    knst_window& operator=(const knst_window&) = delete;

    KNST_FORCE_INLINE knst_window(knst_window&& other) noexcept;



    KNST_FORCE_INLINE knst_window& operator=(knst_window&& other) noexcept;

    
    




    KNST_FORCE_INLINE size_t event_count() const noexcept {
        return m_events.size();
    }

    KNST_FORCE_INLINE const knst_window_event& get_window_event_handle(size_t i) const noexcept {
        return m_events.at(i);
    }

    KNST_FORCE_INLINE void clear_events() noexcept {
        m_events.clear();
    }

    

    KNST_FORCE_INLINE knst_window_event::knst_held_key* 
    find_held_by_scancode(int sc) noexcept {
        for (auto& hk : m_held_keys)
            if (hk.active && hk.scancode == sc) return &hk;
        return nullptr;
    }

    KNST_FORCE_INLINE knst_window_event::knst_held_key* 
    add_held_key(int kc, int sc, uint32_t now) noexcept {
        for (auto& hk : m_held_keys) {
            if (!hk.active) {
                hk.key_code = kc;
                hk.scancode = sc;
                hk.last_key_time = now;
                hk.last_repeat_time = now;
                hk.repeat_initialized = false;
                hk.active = true;
                return &hk;
            }
        }
        return nullptr;
    }

    KNST_FORCE_INLINE void remove_held_key(int sc) noexcept {
        for (auto& hk : m_held_keys)
            if (hk.active && hk.scancode == sc) hk.active = false;
    }

    KNST_FORCE_INLINE void clear_held_keys() noexcept {
        for (auto& hk : m_held_keys) hk.active = false;
    }

    KNST_FORCE_INLINE bool is_key_held(int kc) const noexcept {
        for (const auto& hk : m_held_keys)
            if (hk.active && hk.key_code == kc) return true;
        return false;
    }

    KNST_FORCE_INLINE bool is_caps_lock_on() const noexcept {
            #if KNST_USING_PLATFORM_WINDOWS
                return (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
            #elif KNST_USING_LINUX_PLATFORM_X11
                XkbStateRec state;
                if (XkbGetState(KnstWindowSources::m_xlib_display, XkbUseCoreKbd, &state) == Success) {
                    return (state.locked_mods & LockMask) != 0;
                }
                return false;
            #elif KNST_USING_LINUX_PLATFORM_WAYLAND
                if (!KnstWindowSources::xkb_st || !KnstWindowSources::xkb_map) return false;
                xkb_mod_index_t idx = xkb_keymap_mod_get_index(KnstWindowSources::xkb_map, "Lock");
                if (idx == XKB_MOD_INVALID) return false;
                xkb_mod_mask_t locked = xkb_state_serialize_mods(
                    KnstWindowSources::xkb_st, XKB_STATE_MODS_LOCKED);
                return (locked & (1u << idx)) != 0;
            #else
                return false;
            #endif
    }

    KNST_FORCE_INLINE bool is_num_lock_on() const noexcept {
            #if KNST_USING_PLATFORM_WINDOWS
                return (GetKeyState(VK_NUMLOCK) & 0x0001) != 0;
            #elif KNST_USING_LINUX_PLATFORM_X11
                XkbStateRec state;
                if (XkbGetState(KnstWindowSources::m_xlib_display, XkbUseCoreKbd, &state) == Success) {
                    return (state.locked_mods & Mod2Mask) != 0;
                }
                return false;
            #elif KNST_USING_LINUX_PLATFORM_WAYLAND
                if (!KnstWindowSources::xkb_st || !KnstWindowSources::xkb_map) return false;
                xkb_mod_index_t idx = xkb_keymap_mod_get_index(KnstWindowSources::xkb_map, "Mod2");
                if (idx == XKB_MOD_INVALID) return false;
                xkb_mod_mask_t locked = xkb_state_serialize_mods(
                    KnstWindowSources::xkb_st, XKB_STATE_MODS_LOCKED);
                return (locked & (1u << idx)) != 0;
            #else
                return false;
            #endif
    }

    KNST_FORCE_INLINE void dispatch_event(const knst_window_event& ev) noexcept {
        if (ev.type == KNST_WINDOW_EVENT_UNKNOWN) return;
        m_events.push(ev);
    }

    KNST_FORCE_INLINE void dispatch_current_event() noexcept {
        dispatch_event(m_knst_event);
    }

    KNST_FORCE_INLINE void should_close() noexcept{
        m_should_close = true;
    }

    KNST_FORCE_INLINE const bool& is_should_close()const noexcept{
       return m_should_close;
        
    }

    KNST_FORCE_INLINE const bool& is_disconnected()const noexcept{
       return m_disconnected;
    }

    KNST_FORCE_INLINE void set_user_data(void* data)noexcept{
        m_user_data = data;
    }

    KNST_FORCE_INLINE const void* get_user_data()const noexcept{
        return m_user_data;
    }

    KNST_FORCE_INLINE const knst_c16string& get_title() const noexcept{
        return m_title;
    }



    void creation() noexcept;


    

    
   
    void show() noexcept;
    void destroy() noexcept;
    
    void set_title(const knst_c16string& title) noexcept;
    void move(int root_x, int root_y, const knst_monitor& monitor) noexcept;
    void move(int root_x, int root_y) noexcept;
    void toggle_fullscreen(bool fullscreen) noexcept;
    void set_minimized() noexcept;
    void set_maximized() noexcept;

    void restore() noexcept;
    void hide() noexcept;
    void focus() noexcept;
   

    void set_cursor(uint16_t cursor_type) noexcept;
    void set_bmp_cursor(const knst_byte_string& data,int width, int height, int hot_x = -1, int hot_y = -1) noexcept;
    
    void set_minimum_size(int width = KNST_WINDOW_DEFAULT, int height = KNST_WINDOW_DEFAULT) noexcept;
    void set_maximum_size(int width = KNST_WINDOW_DEFAULT, int height = KNST_WINDOW_DEFAULT) noexcept;
  
    void reset_cursor() noexcept;

    void resize(int width = KNST_WINDOW_DEFAULT, int height = KNST_WINDOW_DEFAULT) noexcept;
    void set_cursor_mode(int mode) noexcept;
    void set_cursor_pos_on_window(int x = KNST_WINDOW_DEFAULT, int y = KNST_WINDOW_DEFAULT) noexcept;
    void set_cursor_pos_global(int root_x = KNST_WINDOW_DEFAULT, int root_y = KNST_WINDOW_DEFAULT) noexcept;
    void set_clipboard(const knst_c16string& text) noexcept;
    void request_clipboard() noexcept;
    void set_drag_drop_status(bool enabled) noexcept;
    void set_opacity(float opacity) noexcept;
    void set_attribute(int attribute, bool value) noexcept;
    bool get_attribute(int attribute) const noexcept;

    void apply_bmp_icon(const knst_byte_string&data,int with,int height)noexcept;

    
    KNST_FORCE_INLINE void creation_and_show()noexcept{
        
        creation();
        show();

    }

};

    
    #include "knst_window_event_system.hpp"



KNST_FORCE_INLINE knst_window::knst_window(knst_window&& other) noexcept
    : m_input_transparent(other.m_input_transparent),
            m_knst_event(other.m_knst_event),
      m_events(other.m_events),
      m_title(std::move(other.m_title)),
      m_should_close(other.m_should_close), m_disconnected(other.m_disconnected),
      m_opacity(other.m_opacity), m_user_data(other.m_user_data),
      m_redraw_callback(std::move(other.m_redraw_callback)),
      clipboard_text(std::move(other.clipboard_text)),
      m_drag_drop_enabled(other.m_drag_drop_enabled),
      m_custom_title_bar_height(other.m_custom_title_bar_height),
      m_draw_custom_title_bar(other.m_draw_custom_title_bar),
      m_mouse_x(other.m_mouse_x), m_mouse_y(other.m_mouse_y)
{
        for (uint32_t i = 0; i < KNST_MAX_HELD_KEYS; ++i)
            m_held_keys[i] = other.m_held_keys[i];

        #if KNST_USING_PLATFORM_WINDOWS
        
            m_window = other.m_window; other.m_window = nullptr;
            m_cursor = other.m_cursor; other.m_cursor = nullptr;
            m_system_cursor = other.m_system_cursor; other.m_system_cursor = nullptr;
            m_icon_big = other.m_icon_big; other.m_icon_big = nullptr;
            m_prevPlacement = other.m_prevPlacement;
            m_prevStyle = other.m_prevStyle;
            m_drop_target = other.m_drop_target; other.m_drop_target = nullptr;
            if (m_window) SetWindowLongPtrW(m_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));


        #elif KNST_USING_LINUX_PLATFORM_X11

            m_window = other.m_window; other.m_window = 0;
            m_xdnd_source = other.m_xdnd_source;
            m_xdnd_selected_type = other.m_xdnd_selected_type;
            m_xdnd_version = other.m_xdnd_version;
            m_hovered_edge = other.m_hovered_edge;
            m_current_x11_cursor = other.m_current_x11_cursor; other.m_current_x11_cursor = 0;
            m_syncCounter = other.m_syncCounter; other.m_syncCounter = XCB_NONE;
            m_syncPendingValue = other.m_syncPendingValue;
            m_syncHasPendingValue = other.m_syncHasPendingValue;
            m_syncRequestReceived = other.m_syncRequestReceived;


        #elif KNST_USING_LINUX_PLATFORM_WAYLAND

            m_surface = other.m_surface; other.m_surface = nullptr;
            xdgSurface = other.xdgSurface; other.xdgSurface = nullptr;
            toplevel = other.toplevel; other.toplevel = nullptr;
            m_resize_edge = other.m_resize_edge;
            m_pointer_serial = other.m_pointer_serial;
            m_pointer_pressed = other.m_pointer_pressed;
            m_pending_cursor_data = std::move(other.m_pending_cursor_data);
            m_pending_cursor_w = other.m_pending_cursor_w;
            m_pending_cursor_h = other.m_pending_cursor_h;
            m_pending_cursor_hot_x = other.m_pending_cursor_hot_x;
            m_pending_cursor_hot_y = other.m_pending_cursor_hot_y;
            m_has_pending_cursor = other.m_has_pending_cursor;
            m_using_custom_cursor = other.m_using_custom_cursor;
            m_always_on_top = other.m_always_on_top;
            m_layer_surface = other.m_layer_surface; other.m_layer_surface = nullptr;
            for (auto*& w : s_windows) { if (w == &other) w = this; }


        #endif

        knst_window_event_system::unregister_window(&other);
        knst_window_event_system::register_window(this);
    }

    KNST_FORCE_INLINE knst_window& knst_window::operator=(knst_window&& other) noexcept {
        if (this == &other) return *this;

        destroy();


        m_mouse_x = other.m_mouse_x; m_mouse_y = other.m_mouse_y;
        m_input_transparent = other.m_input_transparent;
        m_knst_event = other.m_knst_event;



        for (uint32_t i = 0; i < KNST_MAX_HELD_KEYS; ++i)
            m_held_keys[i] = other.m_held_keys[i];



        m_events = other.m_events;
        m_title = std::move(other.m_title);
        m_should_close = other.m_should_close; m_disconnected = other.m_disconnected;
        m_opacity = other.m_opacity; m_user_data = other.m_user_data;
        m_redraw_callback = std::move(other.m_redraw_callback);
        clipboard_text = std::move(other.clipboard_text);
        m_drag_drop_enabled = other.m_drag_drop_enabled;
        m_custom_title_bar_height = other.m_custom_title_bar_height;
        m_draw_custom_title_bar = other.m_draw_custom_title_bar;

        #if KNST_USING_PLATFORM_WINDOWS
            m_window = other.m_window; other.m_window = nullptr;
            m_cursor = other.m_cursor; other.m_cursor = nullptr;
            m_system_cursor = other.m_system_cursor; other.m_system_cursor = nullptr;
            m_icon_big = other.m_icon_big; other.m_icon_big = nullptr;
            m_prevPlacement = other.m_prevPlacement;
            m_prevStyle = other.m_prevStyle;
            m_drop_target = other.m_drop_target; other.m_drop_target = nullptr;
            if (m_window) SetWindowLongPtrW(m_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        #elif KNST_USING_LINUX_PLATFORM_X11
            m_window = other.m_window; other.m_window = 0;
            m_xdnd_source = other.m_xdnd_source;
            m_xdnd_selected_type = other.m_xdnd_selected_type;
            m_xdnd_version = other.m_xdnd_version;
            m_hovered_edge = other.m_hovered_edge;
            m_current_x11_cursor = other.m_current_x11_cursor; other.m_current_x11_cursor = 0;
            m_syncCounter = other.m_syncCounter; other.m_syncCounter = XCB_NONE;
            m_syncPendingValue = other.m_syncPendingValue;
            m_syncHasPendingValue = other.m_syncHasPendingValue;
            m_syncRequestReceived = other.m_syncRequestReceived;
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            m_surface = other.m_surface; other.m_surface = nullptr;
            xdgSurface = other.xdgSurface; other.xdgSurface = nullptr;
            toplevel = other.toplevel; other.toplevel = nullptr;
            m_resize_edge = other.m_resize_edge;
            m_pointer_serial = other.m_pointer_serial;
            m_pointer_pressed = other.m_pointer_pressed;
            m_pending_cursor_data = std::move(other.m_pending_cursor_data);
            m_pending_cursor_w = other.m_pending_cursor_w;
            m_pending_cursor_h = other.m_pending_cursor_h;
            m_pending_cursor_hot_x = other.m_pending_cursor_hot_x;
            m_pending_cursor_hot_y = other.m_pending_cursor_hot_y;
            m_has_pending_cursor = other.m_has_pending_cursor;
            m_using_custom_cursor = other.m_using_custom_cursor;
            m_always_on_top = other.m_always_on_top;
            m_layer_surface = other.m_layer_surface; other.m_layer_surface = nullptr;
            for (auto*& w : s_windows) { if (w == &other) w = this; }
        #endif

        knst_window_event_system::unregister_window(&other);
        knst_window_event_system::register_window(this);
        return *this;
    }

    #if KNST_USING_PLATFORM_WINDOWS

        #include "../windows/knst_window_win32_manager.hpp"

    #elif KNST_USING_LINUX_PLATFORM_X11

        #include "../linux/x11/knst_window_x11_manager.hpp"

    #elif KNST_USING_LINUX_PLATFORM_WAYLAND
        
        #include "../linux/wayland/knst_window_wayland_event_helper.hpp"
        #include "../linux/wayland/knst_window_wayland_manager.hpp"

    #elif defined(KNST_USING_PLATFORM_ANDROID)

        #include "../android/knst_window_android_manager.hpp"
        #include "../android/knst_display_android.hpp"
    #endif

    
