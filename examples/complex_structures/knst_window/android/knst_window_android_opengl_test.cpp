/*
  knst_window_android_opengl_test.cpp

  Android twin of the desktop touch-screen example. Same shaders, same
  event loop. Only three things differ, all NDK rules:

    1. Entry point   -> android_main(app), not main()
    2. Platform init -> KnstWindowSources::Init(app)
    3. GPU upload    -> glBufferData (orphaning), not glBufferSubData
                        — tile-based mobile GPUs need this

  Everything else (density, lifecycle, back, IME, volume keys, surface
  recreation) is handled by the library.

  std::cout / std::cerr auto-redirect to logcat under "KNST".
  knst_c16string prints directly via operator<<. Direct severity logging:
  KNST_LOG_INFO / ERROR / WARN / DEBUG / VERBOSE. Watch: adb logcat -s KNST

  Build:
      Linux         :  ./build-android-opengl.sh
      Windows       :  build-android-opengl.bat
*/

#include "../../../../include/KernelNucleusT.hpp"
#include <iostream>


// ─── 1. Global state ────────────────────────────────────────────────────────
// Static callback can't capture, so everything lives at file scope.
// Alternative: bundle in a struct and pass via set_user_data().

static knst_window_opengl_content* g_gl = nullptr;

static GLuint g_program = 0;   // shader program
static GLuint g_vao = 0;   // vertex array — remembers attrib layout
static GLuint g_vbo = 0;   // vertex buffer
static GLint  g_u_res_loc = -1;  // uniform location for u_resolution

// Touch state. Refreshed from each event batch; zeroed when fingers lift.
static int g_touch_count = 0;
static float g_touch_x[KNST_MAX_TOUCH_POINTS] = {0};
static float g_touch_y[KNST_MAX_TOUCH_POINTS] = {0};
static int g_touch_id[KNST_MAX_TOUCH_POINTS] = {0};

// Mouse fallback for emulators / non-touch devices.
static float g_mouse_x = 0.0f;
static float g_mouse_y = 0.0f;
static bool g_mouse_down = false;

// Density scale from APP_CONFIG_CHANGED (1.0 = mdpi, 2.0 = xhdpi, ...).
// Multiply pixel sizes by this or the UI looks tiny on modern phones.
static float g_ui_scale = 1.0f;

static knst_c16string g_typed_text;


// UTF-16 gotcha: emoji live in surrogate pairs. Dropping just the last
// unit leaves a broken half-char, so drop both.
static void remove_last_codepoint(knst_c16string& s) {
    const uint32_t len = s.length();
    if (len == 0) return;

    uint32_t remove_count = 1;

    if (len >= 2) {
        const uint16_t last = static_cast<uint16_t>(s[len - 1]);
        if (last >= 0xDC00 && last <= 0xDFFF) {
            const uint16_t prev = static_cast<uint16_t>(s[len - 2]);
            if (prev >= 0xD800 && prev <= 0xDBFF) {
                remove_count = 2;
            }
        }
    }

    const uint32_t new_len = len - remove_count;
    knst_c16string fresh;
    for (uint32_t i = 0; i < new_len; ++i) {
        fresh += s[i];
    }
    s = fresh;
}


// ─── 2. Shaders ─────────────────────────────────────────────────────────────
// KNST_GLSL_HEADER expands to "300 es + precision" on Android,
// "330 core" on desktop. Same source, both platforms.
//
// Vertex: 2 floats position (pixels, top-left origin) + 4 floats color.
// Shader converts pixels → NDC and flips Y (GL is Y-up, screen is Y-down).

static const char* VERTEX_SHADER_SRC = KNST_GLSL_HEADER R"(
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec4 a_color;

uniform vec2 u_resolution;

out vec4 v_color;

void main() {
    vec2 ndc = a_position / u_resolution;
    ndc      = ndc * 2.0 - 1.0;
    ndc.y    = -ndc.y;

    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color     = a_color;
}
)";

static const char* FRAGMENT_SHADER_SRC = KNST_GLSL_HEADER R"(
in  vec4 v_color;
out vec4 frag_color;

void main() {
    frag_color = v_color;
}
)";


static GLuint build_shader_program() {
    auto make_shader = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource (s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };

    GLuint vs = make_shader(GL_VERTEX_SHADER,   VERTEX_SHADER_SRC);
    GLuint fs = make_shader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER_SRC);

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram (program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    return program;
}


// ─── 3. Upload + draw ───────────────────────────────────────────────────────
// Android: orphan the buffer (glBufferData) — tile-based GPUs stall on
// in-place updates.
// Desktop: reuse allocation (glBufferSubData) — no allocation churn.
//
// bytes = triangles * 3 verts * 6 floats * sizeof(float).

static void draw_triangles(const float* verts, int triangle_count) {
    if (triangle_count <= 0) return;

    const GLsizeiptr bytes = static_cast<GLsizeiptr>(triangle_count) * 18 * sizeof(float);

    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
#if defined(KNST_USING_PLATFORM_ANDROID)
    glBufferData(GL_ARRAY_BUFFER, bytes, verts, GL_STREAM_DRAW);
#else
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, verts);
#endif
    glDrawArrays(GL_TRIANGLES, 0, triangle_count * 3);
}


// ─── 4. Filled circle ───────────────────────────────────────────────────────
// Triangle fan built on the stack — no index buffer needed.
// 18 floats per segment (3 verts × 6 floats).

static void fill_circle(float cx, float cy, float radius,float r, float g, float b, float a = 1.0f) {

    constexpr int SEGMENTS = 32;
    constexpr float TAU = 6.28318530718f;

    float verts[SEGMENTS * 18];
    int idx = 0;

    for (int i = 0; i < SEGMENTS; ++i) {
        const float a0 = (TAU * i)/ SEGMENTS;
        const float a1 = (TAU * (i + 1)) / SEGMENTS;

        const float x0 = cx + std::cos(a0) * radius;
        const float y0 = cy + std::sin(a0) * radius;
        const float x1 = cx + std::cos(a1) * radius;
        const float y1 = cy + std::sin(a1) * radius;


        verts[idx++] = cx; verts[idx++] = cy;


        verts[idx++] = r;  verts[idx++] = g;  verts[idx++] = b; verts[idx++] = a;

        verts[idx++] = x0; verts[idx++] = y0;
        verts[idx++] = r;  verts[idx++] = g;  verts[idx++] = b; verts[idx++] = a;

        verts[idx++] = x1; verts[idx++] = y1;
        verts[idx++] = r;  verts[idx++] = g;  verts[idx++] = b; verts[idx++] = a;
    }

    draw_triangles(verts, SEGMENTS);
}


// ─── 5. Per-finger color ────────────────────────────────────────────────────
// Cheap hash: id × 47 → hue, split into 6 sectors. Not real HSV but
// fast and visually distinct.

static void finger_color(int id, float& r, float& g, float& b) {
    const int hue = (id * 47) % 360;
    const int sector = hue / 60;
    const float frac = (hue % 60) / 60.0f;

    constexpr float P = 0.3f;
    constexpr float Q = 1.0f;
    const float q_val = Q - frac * (Q - P);

    switch (sector) {
        case 0: r = 1.0f; g = q_val; b = P; break;
        case 1: r = q_val; g = 1.0f; b = P; break;
        case 2: r = P; g = 1.0f; b = q_val; break;
        case 3: r = P; g = q_val; b = 1.0f;  break;
        case 4: r = q_val; g = P; b = 1.0f;  break;
        default: r = 1.0f; g = P; b = q_val; break;
    }
}


// ─── 6. Touch action → printable name ───────────────────────────────────────

static const char* touch_action_name(int action) {
    switch (action) {
        case KNST_WINDOW_TOUCH_ACTION_PRESS: return "PRESS";
        case KNST_WINDOW_TOUCH_ACTION_RELEASE: return "RELEASE";
        case KNST_WINDOW_TOUCH_ACTION_MOVE: return "MOVE";
        case KNST_WINDOW_TOUCH_ACTION_CANCEL: return "CANCEL";
        case KNST_WINDOW_TOUCH_ACTION_OUTSIDE: return "OUTSIDE";
        case KNST_WINDOW_TOUCH_ACTION_POINTER_PRESS: return "PTR_PRESS";
        case KNST_WINDOW_TOUCH_ACTION_POINTER_RELEASE: return "PTR_RELEASE";
        default: return "UNKNOWN";
    }
}


// ─── 7. Touch event handler ─────────────────────────────────────────────────
// Each event carries a snapshot of ALL active pointers (stable IDs),
// not just the one that changed — pinch/zoom works out of the box.
// MOVE fires at 60-120 Hz, skip logging it.

static void handle_touch_event(const knst_window_event& ev) {
    g_touch_count = ev.pointer_count;
    if (g_touch_count > KNST_MAX_TOUCH_POINTS)
        g_touch_count = KNST_MAX_TOUCH_POINTS;

    if (ev.touch_action != KNST_WINDOW_TOUCH_ACTION_MOVE) {
        KNST_LOG_INFO("[TOUCH] action=%-11s  pointers=%d",touch_action_name(ev.touch_action),ev.pointer_count);
    }

    for (int i = 0; i < g_touch_count; ++i) {
        g_touch_x [i] = ev.pointer_x [i];
        g_touch_y [i] = ev.pointer_y [i];
        g_touch_id[i] = ev.pointer_id[i];

        if (ev.touch_action != KNST_WINDOW_TOUCH_ACTION_MOVE) {
            KNST_LOG_INFO("        [id=%d (%.0f,%.0f)]",ev.pointer_id[i],ev.pointer_x[i], ev.pointer_y[i]);
        }
    }

    // All fingers up / cancelled — clear so no ghost dots linger.
    if (ev.touch_action == KNST_WINDOW_TOUCH_ACTION_RELEASE ||
        ev.touch_action == KNST_WINDOW_TOUCH_ACTION_CANCEL) {
        g_touch_count = 0;
    }
}


// ─── 8. Mouse fallback ──────────────────────────────────────────────────────
// Tablets, ChromeOS, emulators — no touch, but mouse works.

static void handle_motion_event(const knst_window_event& ev) {
    g_mouse_x = static_cast<float>(ev.mouse_x);
    g_mouse_y = static_cast<float>(ev.mouse_y);
}

static void handle_mouse_button_event(const knst_window_event& ev) {
    if (ev.mouse_button != KNST_WINDOW_MOUSE_BUTTON_LEFT) return;

    g_mouse_x = static_cast<float>(ev.mouse_x);
    g_mouse_y = static_cast<float>(ev.mouse_y);
    g_mouse_down = (ev.mouse_action == KNST_WINDOW_MOUSE_ACTION_PRESS);

    KNST_LOG_INFO("[MOUSE] %-8s at (%.0f,%.0f)", g_mouse_down ? "press" : "release", g_mouse_x, g_mouse_y);
}

// Wipe input state on pause / focus loss — otherwise a finger that was
// "down" when we paused stays drawn forever.
static void clear_input_state() {
    g_touch_count = 0;
    g_mouse_down  = false;
}


// ─── 9. Frame callback ──────────────────────────────────────────────────────
// Three phases: walk events → clear ring → render.
// The library only queues events, never calls user code from a handler,
// so no reentrancy headaches.

static void on_frame(knst_window& window, void* /*user_data*/) {
    // Pull IME events that the Java thread pushed (clipboard, commitText,
    // preedit) — they arrive as TEXT_INPUT events below.
    knst_android_jni::drain(&window);

    // GPU drivers occasionally lose the context. Library recovers it
    // silently, but every GL object is now garbage — rebuild here.
    if (window.is_gl_context_lost()) {
        KNST_LOG_WARN("[GL] Context lost — rebuilding GL resources");
    }

    // ---- Phase 1: process events ----
    for (size_t i = 0; i < window.event_count(); ++i) {
        const knst_window_event& ev = window.get_window_event_handle(i);

        switch (ev.type) {
            case KNST_WINDOW_EVENT_CLOSE:
                window.should_close();
                break;

            case KNST_WINDOW_EVENT_KEYBOARD:
                if (ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS ||
                    ev.key_action == KNST_WINDOW_KEY_ACTION_REPEAT) {

                    if (ev.key_code == KNST_WINDOW_KEY_CODE_ESCAPE) {
                        window.should_close();
                    }
                    else if (ev.key_code == KNST_WINDOW_KEY_CODE_BACKSPACE) {
                        const uint32_t before = g_typed_text.length();
                        remove_last_codepoint(g_typed_text);
                        const uint32_t after = g_typed_text.length();

                        if (after < before) {
                            KNST_LOG_INFO("[KB]  backspace -> %u chars left",(unsigned)after);
                        }
                    }
                    else if (ev.key_code == KNST_WINDOW_KEY_CODE_C) {
                        // Same API on every platform. Android: JNI to
                        // android.content.ClipboardManager.
                        window.set_clipboard(u"Merhaba from KernelNucleusT Android!");
                        KNST_LOG_INFO("[CLIP] set: \"Merhaba from KernelNucleusT Android!\"");
                    }
                    else if (ev.key_code == KNST_WINDOW_KEY_CODE_V) {
                        // Reply arrives later as CLIPBOARD_DATA.
                        KNST_LOG_INFO("[CLIP] requested...");
                        window.request_clipboard();
                    }
                }
                break;

            case KNST_WINDOW_EVENT_RESIZE:
                // Rotation, IME popup — surface size changed.
                if (g_gl && g_gl->IsValid()) {
                    g_gl->OnResize(ev.window_width, ev.window_height);
                }
                break;

            case KNST_WINDOW_EVENT_TOUCH:
                handle_touch_event(ev);
                break;

            case KNST_WINDOW_EVENT_MOTION:
                handle_motion_event(ev);
                break;

            case KNST_WINDOW_EVENT_MOUSE:
                handle_mouse_button_event(ev);
                break;

            // Async reply to request_clipboard(). knst_c16string prints
            // via operator<<; on Android stdout → logcat.
            case KNST_WINDOW_EVENT_CLIPBOARD_DATA:
                std::cout << "[CLIP] got: \"" << window.get_clipboard() << "\"" << std::endl;
                break;

            // ---- Android lifecycle (never fires on desktop) ----

            case KNST_WINDOW_EVENT_MOBILE_BACK:
                KNST_LOG_INFO("[APP] back button -> quit");
                window.should_close();
                break;

            case KNST_WINDOW_EVENT_APP_PAUSED:
                KNST_LOG_INFO("[APP] paused (background)");
                clear_input_state();
                knst_mobile_keyboard::hide();
                break;

            case KNST_WINDOW_EVENT_APP_RESUMED:
                KNST_LOG_INFO("[APP] resumed");
                break;

            case KNST_WINDOW_EVENT_APP_STOPPED:
                KNST_LOG_INFO("[APP] stopped");
                break;

            case KNST_WINDOW_EVENT_APP_SAVE_STATE:
                KNST_LOG_INFO("[APP] save-state requested");
                break;

            case KNST_WINDOW_EVENT_APP_WINDOW_LOST:
                // ANativeWindow gone. EGL / Vulkan surface already
                // released by the library.
                KNST_LOG_INFO("[APP] surface LOST");
                clear_input_state();
                break;

            case KNST_WINDOW_EVENT_EXPOSE:
                KNST_LOG_INFO("[APP] surface ready");
                break;

            case KNST_WINDOW_EVENT_APP_LOW_MEMORY:
                KNST_LOG_WARN("[APP] low memory");
                break;

            case KNST_WINDOW_EVENT_FOCUS_IN:
                KNST_LOG_INFO("[APP] focus in");
                break;

            case KNST_WINDOW_EVENT_FOCUS_OUT:
                KNST_LOG_INFO("[APP] focus out");
                clear_input_state();
                break;

            case KNST_WINDOW_EVENT_APP_CONFIG_CHANGED:
                // Density, orientation, locale, night-mode come through
                // here. We only care about density for UI scaling.
                if (ev.density > 0.0f) g_ui_scale = ev.density;
                KNST_LOG_INFO("[APP] config changed (density=%.2f)", (double)g_ui_scale);
                             
                break;

            // Volume keys are repurposed as IME toggles for the demo.
            case KNST_WINDOW_EVENT_MOBILE_VOLUME_UP:
                KNST_LOG_INFO("[KB] volume up -> show soft keyboard");
                knst_mobile_keyboard::show();
                break;

            case KNST_WINDOW_EVENT_MOBILE_VOLUME_DOWN:
                KNST_LOG_INFO("[KB] volume down -> hide soft keyboard");
                knst_mobile_keyboard::hide();
                break;

            case KNST_WINDOW_EVENT_TEXT_INPUT: {
                // Two flavors via this event type:
                //   preedit  → IME composition preview (CJK, etc.)
                //   text_input → final committed text
                if (ev.text_preedit.length() > 0) {
                    std::cout << "[IME]  preedit: \""<< ev.text_preedit << "\"" << std::endl;
                    break;
                }

                if (ev.text_input.length() > 0) {
                    // Soft keyboards don't emit KEYBOARD events — catch
                    // C/V here too or they only work on physical kbd.
                    const uint32_t n = ev.text_input.length();
                    if (n == 1) {
                        const char16_t ch = ev.text_input[0];
                        if (ch == u'c' || ch == u'C') {
                            window.set_clipboard(
                                u"Merhaba from KernelNucleusT Android!");
                            KNST_LOG_INFO("[CLIP] set: \"Merhaba from KernelNucleusT Android!\"");
                            break;
                        }
                        if (ch == u'v' || ch == u'V') {
                            KNST_LOG_INFO("[CLIP] requested...");
                            window.request_clipboard();
                            break;
                        }
                    }

                    g_typed_text += ev.text_input;
                    std::cout << "[TEXT] \"" << ev.text_input<< "\"  (len=" << ev.text_input.length()<< "  total=" << g_typed_text.length() << ")" << std::endl;
                }
                break;
            }

            default:
                break;
        }
    }

    // ---- Phase 2: ring is ours. Fixed size — not clearing drops events. ----
    window.clear_events();

    // ---- Phase 3: nothing to draw? bail. ----
    if (window.is_should_close())return;
    if (!window.is_frame_ready())return;    // backgrounded / not ready
    if (!g_gl->HasSurface()) return;   // ANativeWindow gone

    // ---- Phase 4: render ----
    g_gl->MakeCurrent();
    const int w = g_gl->GetWidth();
    const int h = g_gl->GetHeight();

    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.10f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // 2D only — no depth, no culling, plain alpha blend.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(g_program);
    glUniform2f(g_u_res_loc,static_cast<float>(w), static_cast<float>(h));
                

    const float ui = g_ui_scale;

    // Mouse fallback: 3 nested circles read as a soft glow.
    if (g_touch_count == 0 && g_mouse_down) {
        fill_circle(g_mouse_x, g_mouse_y, 34.0f * ui, 0.9f, 0.9f, 0.9f, 0.35f);
        fill_circle(g_mouse_x, g_mouse_y, 20.0f * ui, 0.9f, 0.9f, 0.9f, 1.00f);
        fill_circle(g_mouse_x, g_mouse_y,  5.0f * ui, 1.0f, 1.0f, 1.0f, 1.00f);
    }

    // Each finger: halo + solid core + white dot.
    for (int i = 0; i < g_touch_count && i < KNST_MAX_TOUCH_POINTS; ++i) {
        float r, g, b;
        finger_color(g_touch_id[i], r, g, b);

        fill_circle(g_touch_x[i], g_touch_y[i], 36.0f * ui,  r * 0.5f, g * 0.5f, b * 0.5f, 0.55f);
        fill_circle(g_touch_x[i], g_touch_y[i], 22.0f * ui, r, g, b, 1.0f);
        fill_circle(g_touch_x[i], g_touch_y[i],  5.0f * ui, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    // Request next frame BEFORE presenting — Wayland wants this order,
    // Android treats it as a no-op. Keeps the two symmetric.
    window.request_frame_callback();
    g_gl->SwapBuffers();
}


// ─── 10. Startup banner ─────────────────────────────────────────────────────

static void print_startup_info(const knst_window& window) {
    std::cout << "--- Hardware -----------------------------\n";
    std::cout << "  Touch supported  : " << (window.supports_touch()    ? "YES" : "NO") << "\n";
              
    std::cout << "  Max touch points : " << window.max_touch_points()                  << "\n";
              
    std::cout << "  Touch enabled    : "  << (window.is_touch_enabled() ? "YES" : "NO") << "\n\n";
             

    std::cout << "--- Mode --------------------------------\n";
    if (window.supports_touch()) {
        std::cout << "  Touch hardware detected.\n";
        std::cout << "  Every finger draws a colored circle.\n\n";
    } else {
        std::cout << "  No touch hardware.\n";
        std::cout << "  Mouse fallback active — hold left button.\n\n";
    }

    std::cout << "--- Controls ----------------------------\n";
    std::cout << "  Volume UP        open soft keyboard (IME)\n";
    std::cout << "  Volume DOWN      close soft keyboard\n";
    std::cout << "  C key            set clipboard (test)\n";
    std::cout << "  V key            request clipboard (test)\n";
    std::cout << "  Back button      quit\n";
    std::cout << "\n--- Event log ---------------------------\n";
}


// ─── 11. Entry point ────────────────────────────────────────────────────────
// android_main runs on the main thread and must not return while the
// Activity is alive — the UI thread syncs lifecycle with us and would
// deadlock. KnstWindowSources::CleanUp() handles that handshake.

void android_main(struct android_app* app) {
    KnstWindowSources::Init(app);

    std::cout << "========================================\n";
    std::cout << "  KernelNucleusT — Touch Screen Test\n";
    std::cout << "========================================\n\n";

    if (KnstWindowSources::active_backend() == knst_backend::none) {
        KNST_LOG_ERROR("Failed to initialize the platform layer.");
        return;
    }

    // GLES 3.3 core — gives us modern shader syntax. Lower versions
    // would need a different #version directive.
    knst_opengl_global::Options gl_options;
    gl_options.gl_major     = 3;
    gl_options.gl_minor     = 3;
    gl_options.core_profile = true;
    gl_options.vsync        = true;
    if (!knst_opengl_global::Init(gl_options)) {
        KNST_LOG_ERROR("Failed to initialize OpenGL global state.");
        KnstWindowSources::CleanUp();
        return;
    }

    knst_window window(800, 600, u"Touch Screen Test");
    window.set_redraw_callback(on_frame);
    window.creation();

    window.enable_touch_screen(true);
    print_startup_info(window);

    // Content object wraps EGL surface + context and auto-handles
    // surface recreation on Android (TERM_WINDOW / INIT_WINDOW).
    knst_window_opengl_content gl;
    if (!gl.Init(&window)) {
        KNST_LOG_ERROR("Failed to create the OpenGL context.");
        knst_opengl_global::CleanUp();
        KnstWindowSources::CleanUp();
        return;
    }
    g_gl = &gl;

    gl.MakeCurrent();

    // Now that a context is current, GLAD can resolve gl* symbols.
    // LoadGLProc → eglGetProcAddress on Android, wglGetProcAddress on Win.
    if (!gladLoadGLLoader(
            reinterpret_cast<GLADloadproc>( knst_window_opengl_content::LoadGLProc))) {
               
        KNST_LOG_ERROR("Failed to load OpenGL function pointers.");
        gl.Shutdown();
        knst_opengl_global::CleanUp();
        KnstWindowSources::CleanUp();
        return;
    }

    // ---- One-time GL setup ----
    g_program   = build_shader_program();
    g_u_res_loc = glGetUniformLocation(g_program, "u_resolution");

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);

    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);

    // Pre-allocate 64 KB. Every frame orphans it, so this is just the
    // worst-case size.
    glBufferData(GL_ARRAY_BUFFER, 64 * 1024, nullptr, GL_STREAM_DRAW);

    // Vertex layout: 6 floats (2 pos + 4 color).
    const GLsizei stride = 6 * sizeof(float);

    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1, 4, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Draw once before showing — avoids a black flash on map.
    on_frame(window, nullptr);
    window.show();

    // ---- Main loop ----
    // non_block_pool_event() drains Android looper messages (input,
    // lifecycle) without blocking. call_redraw_callback() runs on_frame().
    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        window.call_redraw_callback();

        // Back off if the compositor isn't ready. On Android this is
        // basically "is there a live ANativeWindow" — cheap check.
        if (!window.is_frame_ready()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        } else {
            std::this_thread::yield();
        }
    }

    std::cout << "\n--- Closed ------------------------------\n";

    // ---- Teardown: GL objects → context → global → window → platform ----
    gl.MakeCurrent();

    if (g_vbo) { glDeleteBuffers(1, &g_vbo);        g_vbo     = 0; }
    if (g_vao) { glDeleteVertexArrays(1, &g_vao);   g_vao     = 0; }
    if (g_program) { glDeleteProgram(g_program);        g_program = 0; }

    g_gl = nullptr;

    g_touch_count = 0;
    g_mouse_down = false;
    g_typed_text = u"";

    gl.Shutdown();
    knst_opengl_global::CleanUp();
    window.destroy();
    KnstWindowSources::CleanUp();
}