/*
  knst_window_touch_screen_basic.cpp

  Touch input on every platform the library supports:

    Windows  -> WM_TOUCH
    X11      -> XInput2 (TouchBegin/Update/End)
    Wayland  -> wl_touch
    Android  -> AInputEvent (see android/ folder for that entry point)

  Every finger draws a colored circle. If no touch hardware is found,
  the app falls back to mouse input so the same binary works on desktop.

  Things covered:
    1. Query touch hardware at runtime (supports_touch / max_touch_points)
    2. Enable the touch stream (enable_touch_screen)
    3. Read a TOUCH event:
         touch_action     -> press / move / release / cancel / ...
         pointer_count    -> active contacts
         pointer_id[i]    -> stable per-contact id
         pointer_x/y[i]   -> window-pixel position
    4. Deterministic per-finger colors from pointer ids
    5. Draw a triangle-fan circle into a streaming VBO
    6. Fall back to mouse when there's no touch hardware

  Rendering: vertex shader takes pixel coords with top-left origin and
  converts to NDC. Each circle is a triangle fan re-uploaded per frame.

  Controls:
    ESC              quit
    Touch screen     every finger -> colored circle
    Mouse fallback   hold left button -> gray circle
*/

#include "../../../../include/KernelNucleusT.hpp"
#include <iostream>


// ─── 1. Global state ────────────────────────────────────────────────────────

static knst_window_opengl_content* g_gl = nullptr;
static GLuint g_program = 0;
static GLuint g_vao = 0;
static GLuint g_vbo = 0;
static GLint g_u_res_loc = -1;

// Touch contacts — up to KNST_MAX_TOUCH_POINTS at once.
static int   g_touch_count                     = 0;
static float g_touch_x[KNST_MAX_TOUCH_POINTS]  = {0};
static float g_touch_y[KNST_MAX_TOUCH_POINTS]  = {0};
static int   g_touch_id[KNST_MAX_TOUCH_POINTS] = {0};

// Mouse fallback.
static float g_mouse_x = 0.0f;
static float g_mouse_y = 0.0f;
static bool g_mouse_down = false;


// ─── 2. Shaders ─────────────────────────────────────────────────────────────

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


// ─── 3. Shader program builder ──────────────────────────────────────────────

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


// ─── 4. Dynamic geometry upload ─────────────────────────────────────────────
// 6 floats per vertex (2 pos + 4 color), 3 verts per triangle.

static void draw_triangles(const float* verts, int triangle_count) {
    if (triangle_count <= 0) return;

    const GLsizeiptr bytes =static_cast<GLsizeiptr>(triangle_count) * 18 * sizeof(float);

    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, verts);
    glDrawArrays(GL_TRIANGLES, 0, triangle_count * 3);
}


// ─── 5. Filled circle ───────────────────────────────────────────────────────
// Triangle fan built on the stack — no index buffer needed.

static void fill_circle(float cx, float cy, float radius, float r, float g, float b, float a = 1.0f) {

    constexpr int SEGMENTS = 32;
    constexpr float TAU = 6.28318530718f;

    float verts[SEGMENTS * 18];
    int idx = 0;

    for (int i = 0; i < SEGMENTS; ++i) {
        const float a0 = (TAU * i) / SEGMENTS;
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


// ─── 6. Per-finger color ────────────────────────────────────────────────────
// Cheap hash: id * 47 → hue, split into 6 sectors. Not real HSV, but
// fast and visually distinct for each finger.

static void finger_color(int id, float& r, float& g, float& b) {
    const int hue = (id * 47) % 360;
    const int sector = hue / 60;
    const float frac = (hue % 60) / 60.0f;

    constexpr float P = 0.3f;
    constexpr float Q = 1.0f;
    const float q_val = Q - frac * (Q - P);

    switch (sector) {
        case 0:  r = 1.0f;  g = q_val; b = P; break;
        case 1:  r = q_val; g = 1.0f;  b = P; break;
        case 2:  r = P;     g = 1.0f;  b = q_val; break;
        case 3:  r = P;     g = q_val; b = 1.0f; break;
        case 4:  r = q_val; g = P;     b = 1.0f; break;
        default: r = 1.0f;  g = P;     b = q_val; break;
    }
}


// ─── 7. Touch action name (log only) ────────────────────────────────────────

static const char* touch_action_name(int action) {
    switch (action) {
        case KNST_WINDOW_TOUCH_ACTION_PRESS: return "PRESS";
        case KNST_WINDOW_TOUCH_ACTION_RELEASE: return "RELEASE";
        case KNST_WINDOW_TOUCH_ACTION_MOVE: return "MOVE";
        case KNST_WINDOW_TOUCH_ACTION_CANCEL: return "CANCEL";
        case KNST_WINDOW_TOUCH_ACTION_OUTSIDE:return "OUTSIDE";
        case KNST_WINDOW_TOUCH_ACTION_POINTER_PRESS: return "PTR_PRESS";
        case KNST_WINDOW_TOUCH_ACTION_POINTER_RELEASE: return "PTR_RELEASE";
        default: return "UNKNOWN";
    }
}


// ─── 8. Touch handler ───────────────────────────────────────────────────────
// A touch event carries a snapshot of every active pointer with stable
// ids, so multi-touch works out of the box.

static void handle_touch_event(const knst_window_event& ev) {
    g_touch_count = ev.pointer_count;

    std::fprintf(stderr, "[TOUCH] action=%-11s  pointers=%d  ", touch_action_name(ev.touch_action),ev.pointer_count);

    for (int i = 0; i < ev.pointer_count &&
                    i < KNST_MAX_TOUCH_POINTS; ++i) {
        g_touch_x [i] = ev.pointer_x [i];
        g_touch_y [i] = ev.pointer_y [i];
        g_touch_id[i] = ev.pointer_id[i];

        std::fprintf(stderr, "[id=%d (%.0f,%.0f)] ", ev.pointer_id[i], ev.pointer_x[i], ev.pointer_y[i]);
    }
    std::fprintf(stderr, "\n");

    // All fingers up / cancelled — clear so nothing lingers.
    if (ev.touch_action == KNST_WINDOW_TOUCH_ACTION_RELEASE ||
        ev.touch_action == KNST_WINDOW_TOUCH_ACTION_CANCEL) {
        g_touch_count = 0;
    }
}


// ─── 9. Mouse fallback ──────────────────────────────────────────────────────

static void handle_motion_event(const knst_window_event& ev) {
    g_mouse_x = static_cast<float>(ev.mouse_x);
    g_mouse_y = static_cast<float>(ev.mouse_y);
}

static void handle_mouse_button_event(const knst_window_event& ev) {
    if (ev.mouse_button != KNST_WINDOW_MOUSE_BUTTON_LEFT) return;

    g_mouse_x    = static_cast<float>(ev.mouse_x);
    g_mouse_y    = static_cast<float>(ev.mouse_y);
    g_mouse_down = (ev.mouse_action == KNST_WINDOW_MOUSE_ACTION_PRESS);

    std::fprintf(stderr, "[MOUSE] %-8s at (%.0f,%.0f)\n", g_mouse_down ? "press" : "release", g_mouse_x, g_mouse_y);
}


// ─── 10. Frame callback ─────────────────────────────────────────────────────

static void on_frame(knst_window& window, void* /*user_data*/) {
    for (size_t i = 0; i < window.event_count(); ++i) {
        const knst_window_event& ev = window.get_window_event_handle(i);

        switch (ev.type) {
            case KNST_WINDOW_EVENT_CLOSE:
                window.should_close();
                break;

            case KNST_WINDOW_EVENT_KEYBOARD:
                if (ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS &&
                    ev.key_code   == KNST_WINDOW_KEY_CODE_ESCAPE) {
                    window.should_close();
                }
                break;

            case KNST_WINDOW_EVENT_RESIZE:
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

            default:
                break;
        }
    }

    window.clear_events();

    if (window.is_should_close()) return;
    if (!window.is_frame_ready()) return;

    g_gl->MakeCurrent();

    const int w = g_gl->GetWidth();
    const int h = g_gl->GetHeight();

    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.10f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_program);
    glUniform2f(g_u_res_loc, static_cast<float>(w), static_cast<float>(h));

    // Mouse fallback: 3 nested circles read as a soft glow.
    if (g_touch_count == 0 && g_mouse_down) {
        fill_circle(g_mouse_x, g_mouse_y, 34.0f, 0.9f, 0.9f, 0.9f, 0.35f);
        fill_circle(g_mouse_x, g_mouse_y, 20.0f, 0.9f, 0.9f, 0.9f, 1.00f);
        fill_circle(g_mouse_x, g_mouse_y,  5.0f, 1.0f, 1.0f, 1.0f, 1.00f);
    }

    // Each finger: halo + solid core + white dot.
    for (int i = 0; i < g_touch_count && i < KNST_MAX_TOUCH_POINTS; ++i) {
        float r, g, b;
        finger_color(g_touch_id[i], r, g, b);
        fill_circle(g_touch_x[i], g_touch_y[i], 36.0f, r * 0.5f, g * 0.5f, b * 0.5f, 0.55f);
        fill_circle(g_touch_x[i], g_touch_y[i], 22.0f,  r, g, b, 1.0f);
        fill_circle(g_touch_x[i], g_touch_y[i],  5.0f, 1.0f, 1.0f, 1.0f, 1.0f);
                   
    }

    window.request_frame_callback();
    g_gl->SwapBuffers();
}


// ─── 11. Startup info ───────────────────────────────────────────────────────

static void print_startup_info(const knst_window& window) {
    std::cout << "--- Hardware -----------------------------\n";
    std::cout << "  Touch supported  : " << (window.supports_touch()    ? "YES" : "NO") << "\n";
    std::cout << "  Max touch points : "  << window.max_touch_points()                  << "\n";
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
    std::cout << "  ESC              quit\n";
    std::cout << "\n--- Event log ---------------------------\n";
}


// ─── 12. Main ───────────────────────────────────────────────────────────────

int main() {
    std::cout << "========================================\n";
    std::cout << "  KernelNucleusT — Touch Screen Test\n";
    std::cout << "========================================\n\n";

    KnstWindowSources::Init();
    if (KnstWindowSources::active_backend() == knst_backend::none) {
        std::cerr << "Failed to initialize the platform layer.\n";
        return 1;
    }

    knst_opengl_global::Options gl_options;
    gl_options.gl_major     = 3;
    gl_options.gl_minor     = 3;
    gl_options.core_profile = true;

    if (!knst_opengl_global::Init(gl_options)) {
        std::cerr << "Failed to initialize OpenGL global state.\n";
        KnstWindowSources::CleanUp();
        return 1;
    }

    knst_window window(800, 600, u"Touch Screen Test");
    window.set_redraw_callback(on_frame);
    window.creation();

    window.enable_touch_screen(true);
    print_startup_info(window);

    knst_window_opengl_content gl;
    if (!gl.Init(&window)) {
        std::cerr << "Failed to create the OpenGL context.\n";
        knst_opengl_global::CleanUp();
        KnstWindowSources::CleanUp();
        return 1;
    }
    g_gl = &gl;

    gl.MakeCurrent();
    if (!gladLoadGLLoader(
            reinterpret_cast<GLADloadproc>(
                knst_window_opengl_content::LoadGLProc))) {
        std::cerr << "Failed to load OpenGL function pointers.\n";
        gl.Shutdown();
        knst_opengl_global::CleanUp();
        KnstWindowSources::CleanUp();
        return 1;
    }

    g_program   = build_shader_program();
    g_u_res_loc = glGetUniformLocation(g_program, "u_resolution");

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);

    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);

    // Pre-allocate 64 KB — the buffer is re-uploaded every frame, so
    // this is just the worst-case size.
    glBufferData(GL_ARRAY_BUFFER, 64 * 1024, nullptr, GL_STREAM_DRAW);

    const GLsizei stride = 6 * sizeof(float);

    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1, 4, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Draw the first frame before showing — kills the empty-window flash.
    on_frame(window, nullptr);
    window.show();

    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        window.call_redraw_callback();
        std::this_thread::yield();
    }

    std::cout << "\n--- Closed ------------------------------\n";

    gl.MakeCurrent();

    if (g_vbo) { glDeleteBuffers(1, &g_vbo); g_vbo = 0; }
    if (g_vao) { glDeleteVertexArrays(1, &g_vao); g_vao  = 0; }
    if (g_program) { glDeleteProgram(g_program); g_program = 0; }

    g_gl = nullptr;

    gl.Shutdown();
    knst_opengl_global::CleanUp();
    window.destroy();
    KnstWindowSources::CleanUp();

    return 0;
}