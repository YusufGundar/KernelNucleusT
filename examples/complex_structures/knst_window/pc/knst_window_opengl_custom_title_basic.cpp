/*
  knst_window_opengl_custom_title_basic.cpp

  Custom title bar (CSD — Client-Side Decoration) drawn with OpenGL,
  plus working minimize / maximize / close buttons.

  Things this example teaches:
    1. How to tell the library we're drawing our own title bar
       (set_title_bar_height + set_csd_hit_test).
    2. How to describe interactive regions to the library:
         - draggable area  (title bar)
         - resize edges    (all four sides + corners)
         - normal area     (buttons, content — clicks reach the app)
       The library maps those regions to native actions per platform
       (X11 _NET_WM_MOVERESIZE, Wayland toplevel move/resize, Win
       WM_NCHITTEST).
    3. How to draw a pixel-coordinate 2D UI on top of a GL context —
       a small shader turns pixel coords into NDC.
    4. How to keep the content area separated from the title bar with
       glScissor, even at tiny window sizes.
    5. Hover state from motion events, clicks routed to buttons.
    6. Correct teardown order for GL resources.

  Controls:
    ESC              quit
    Title bar        drag the window
    Title bar dbl-cl maximize / restore
    Edges / corners  resize
    Right buttons    minimize / maximize-restore / close
*/

#include "../../../../include/KernelNucleusT.hpp"
#include <iostream>


// ─── 1. Layout constants ────────────────────────────────────────────────────
// Named constants make the layout self-documenting and keep the
// render + hit-test code in sync.

static constexpr int TITLE_BAR_HEIGHT = 40;   // custom title bar height
static constexpr int BUTTON_WIDTH = 46;   // each system button
static constexpr int RESIZE_BORDER = 8;    // hit-test border

enum class ButtonId : int {
    NONE = -1,
    MINIMIZE = 0,
    MAXIMIZE = 1,
    CLOSE = 2,
};


// ─── 2. Global state ────────────────────────────────────────────────────────
// A real app would wrap this in a struct; statics keep the flow readable
// for a tutorial.

static knst_window_opengl_content* g_gl = nullptr;
static GLuint g_program = 0;
static GLuint g_vao = 0;
static GLuint g_vbo = 0;
static ButtonId g_hover = ButtonId::NONE;


// ─── 3. Shaders ─────────────────────────────────────────────────────────────
// Standard 2D UI shader. Vertex receives pixel coords (top-left origin)
// and converts them to NDC (bottom-left origin, range -1..1):
//
//     ndc_x = (px_x / width)  * 2 - 1
//     ndc_y = (px_y / height) * 2 - 1, then flipped (y points down)
//
// Fragment just forwards the interpolated color.

static const char* VERTEX_SHADER_SRC = KNST_GLSL_HEADER R"(
layout(location = 0) in vec2 a_position;   // pixel coords, top-left origin
layout(location = 1) in vec4 a_color;      // rgba, non-premultiplied

uniform vec2 u_resolution;

out vec4 v_color;

void main() {
    vec2 ndc = a_position / u_resolution;
    ndc      = ndc * 2.0 - 1.0;
    ndc.y    = -ndc.y;                     // top-left origin -> flip

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


// ─── 4. Shader program builder ──────────────────────────────────────────────

static GLuint build_shader_program() {
    auto make_shader = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource (s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };

    GLuint vs = make_shader(GL_VERTEX_SHADER, VERTEX_SHADER_SRC);
    GLuint fs = make_shader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER_SRC);

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram (program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    return program;
}


// ─── 5. Filled rectangle ────────────────────────────────────────────────────
// All UI in this example is composed from rectangles, so this is the
// workhorse. Coordinates are pixels, origin at the top-left of the
// window, y growing downward — same convention most UI toolkits use.

static void fill_rect(float x, float y, float w, float h,
                      float r, float g, float b, float a = 1.0f) {
    const float verts[] = {
        // first triangle
        x,     y,     r, g, b, a,
        x + w, y,     r, g, b, a,
        x,     y + h, r, g, b, a,
        // second triangle
        x + w, y,     r, g, b, a,
        x + w, y + h, r, g, b, a,
        x,     y + h, r, g, b, a,
    };

    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}


// ─── 6. Title bar ───────────────────────────────────────────────────────────
// Layout (right-aligned buttons):
//
//     +-----------------------------------------------+---+---+---+
//     |               draggable area                  | - | o | x |
//     +-----------------------------------------------+---+---+---+
//
// The maximize button swaps to a "restore" glyph (two overlapping
// squares) when the window is maximized — matching native title bars.

static void draw_title_bar(int window_width, bool is_maximized) {
    const float bw = static_cast<float>(BUTTON_WIDTH);
    const float bh = static_cast<float>(TITLE_BAR_HEIGHT);

    // Background.
    fill_rect(0, 0, static_cast<float>(window_width), bh,0.15f, 0.15f, 0.18f);

    // Button backgrounds — brighter on hover.
    const bool hover_min = (g_hover == ButtonId::MINIMIZE);
    const bool hover_max = (g_hover == ButtonId::MAXIMIZE);
    const bool hover_cls = (g_hover == ButtonId::CLOSE);

    // Close (rightmost) turns red on hover.
    fill_rect(window_width - bw, 0, bw, bh,
              hover_cls ? 0.85f : 0.20f,
              hover_cls ? 0.20f : 0.20f,
              hover_cls ? 0.20f : 0.20f);

    fill_rect(window_width - bw * 2, 0, bw, bh,
              hover_max ? 0.35f : 0.20f,
              hover_max ? 0.35f : 0.20f,
              hover_max ? 0.35f : 0.20f);

    fill_rect(window_width - bw * 3, 0, bw, bh,
              hover_min ? 0.35f : 0.20f,
              hover_min ? 0.35f : 0.20f,
              hover_min ? 0.35f : 0.20f);

    // Icon glyphs — simple lines and squares.
    constexpr float FG = 0.90f;
    constexpr float BG = 0.15f;

    // Close: an X built from two crossed bars.
    {
        const float cx = window_width - bw * 0.5f;
        const float cy = bh * 0.5f;
        fill_rect(cx - 5, cy - 1, 10, 2, FG, FG, FG);   // horizontal
        fill_rect(cx - 1, cy - 5,  2, 10, FG, FG, FG);  // vertical
    }

    // Maximize / restore glyph.
    {
        const float cx = window_width - bw * 1.5f;
        const float cy = bh * 0.5f;

        if (is_maximized) {
            // Restore: two overlapping squares.
            fill_rect(cx - 6, cy - 6, 8, 8, FG, FG, FG);
            fill_rect(cx - 4, cy - 4, 4, 4, BG, BG, BG);
            fill_rect(cx - 2, cy - 2, 8, 8, FG, FG, FG);
            fill_rect(cx,     cy,     4, 4, BG, BG, BG);
        } else {
            // Maximize: single outlined square.
            fill_rect(cx - 5, cy - 5, 10, 10, FG, FG, FG);
            fill_rect(cx - 3, cy - 3,  6,  6, BG, BG, BG);
        }
    }

    // Minimize: a horizontal bar.
    {
        const float cx = window_width - bw * 2.5f;
        const float cy = bh * 0.5f;
        fill_rect(cx - 5, cy - 1, 10, 2, FG, FG, FG);
    }
}


// ─── 7. Content ─────────────────────────────────────────────────────────────
// Draws a colored triangle centered in the region BELOW the title bar.
// The caller enables glScissor so the triangle can never bleed up into
// the title bar.

static void draw_content(int window_width, int window_height) {
    const float cx = window_width * 0.5f;
    const float cy = TITLE_BAR_HEIGHT
                   + (window_height - TITLE_BAR_HEIGHT) * 0.5f;
    const float s  = 120.0f;

    const float verts[] = {
        cx,     cy - s,  1.0f, 0.3f, 0.3f, 1.0f,   // top
        cx + s, cy + s,  0.3f, 1.0f, 0.3f, 1.0f,   // bottom-right
        cx - s, cy + s,  0.3f, 0.3f, 1.0f, 1.0f,   // bottom-left
    };

    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}


// ─── 8. Hit test ────────────────────────────────────────────────────────────
// Which button is under this pixel? Only used for hover highlights —
// actual click handling lives in handle_click().

static ButtonId button_at(int window_width, int x, int y) {
    if (y < 0 || y >= TITLE_BAR_HEIGHT) return ButtonId::NONE;
    if (x < window_width - BUTTON_WIDTH * 3) return ButtonId::NONE;

    if (x >= window_width - BUTTON_WIDTH) return ButtonId::CLOSE;
    if (x >= window_width - BUTTON_WIDTH * 2) return ButtonId::MAXIMIZE;
    return ButtonId::MINIMIZE;
}


// ─── 9. Input helpers ───────────────────────────────────────────────────────

static void update_hover(knst_window& window, int x, int y) {
    g_hover = button_at(window.get_width(), x, y);
}

static void clear_hover() {
    g_hover = ButtonId::NONE;
}

static void handle_click(knst_window& window, int x, int y) {
    switch (button_at(window.get_width(), x, y)) {
        case ButtonId::CLOSE:
            window.should_close();
            break;

        case ButtonId::MAXIMIZE:
            if (window.is_maximized()) window.restore();
            else window.set_maximized();
            break;

        case ButtonId::MINIMIZE:
            window.set_minimized();
            break;

        case ButtonId::NONE:
        default:
            break;
    }
}


// ─── 10. CSD hit-test callback ──────────────────────────────────────────────
// The library calls this on every left-click press. We return one of:
//
//     draggable   -> library starts a native window move
//     resize_*    -> library starts a native window resize
//     normal      -> library does nothing; the click becomes a regular
//                    KNST_WINDOW_EVENT_MOUSE that the app handles
//
// Pure function, no side effects — easy to reason about.

static knst_window::csd_hit csd_hit_test(const knst_window& window,  int x, int y) {
    const int w = window.get_width();
    const int h = window.get_height();

    // While maximized or fullscreen the WM owns the edges — skip the
    // resize hit-test so we don't fight it.
    const bool locked = window.is_maximized() || window.is_fullscreen();

    if (!locked) {
        const bool near_l = x < RESIZE_BORDER;
        const bool near_r = x >= w - RESIZE_BORDER;
        const bool near_t = y < RESIZE_BORDER;
        const bool near_b = y >= h - RESIZE_BORDER;

        // Corners take priority — they overlap two edges.
        if (near_t && near_l) return knst_window::csd_hit::resize_top_left;
        if (near_t && near_r) return knst_window::csd_hit::resize_top_right;
        if (near_b && near_l) return knst_window::csd_hit::resize_bottom_left;
        if (near_b && near_r) return knst_window::csd_hit::resize_bottom_right;

        if (near_l) return knst_window::csd_hit::resize_left;
        if (near_r) return knst_window::csd_hit::resize_right;
        if (near_t) return knst_window::csd_hit::resize_top;
        if (near_b) return knst_window::csd_hit::resize_bottom;
    }

    // Title bar: buttons are "normal" (app handles them), the rest is
    // draggable.
    if (y < TITLE_BAR_HEIGHT) {
        if (x >= w - BUTTON_WIDTH * 3) return knst_window::csd_hit::normal;
        return knst_window::csd_hit::draggable;
    }

    return knst_window::csd_hit::normal;
}


// ─── 11. Frame callback ─────────────────────────────────────────────────────
// Runs once per loop iteration:
//   1. Consume every queued event
//   2. Clear the event ring
//   3. Skip the frame if closing or the compositor isn't ready
//   4. Render: background -> clipped content -> title bar

static void on_frame(knst_window& window, void* /*user_data*/) {
    // --- Step 1: consume events ---
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

            case KNST_WINDOW_EVENT_MOTION:
                update_hover(window, ev.mouse_x, ev.mouse_y);
                break;

            case KNST_WINDOW_EVENT_LEAVE:
                clear_hover();
                break;

            case KNST_WINDOW_EVENT_MOUSE:
                if (ev.mouse_action == KNST_WINDOW_MOUSE_ACTION_PRESS &&
                    ev.mouse_button == KNST_WINDOW_MOUSE_BUTTON_LEFT) {
                    handle_click(window, ev.mouse_x, ev.mouse_y);
                }
                break;

            case KNST_WINDOW_EVENT_RESIZE:
                if (g_gl && g_gl->IsValid()) {
                    g_gl->OnResize(ev.window_width, ev.window_height);
                }
                break;

            default:
                break;
        }
    }

    window.clear_events();

    if (window.is_should_close()) return;
    if (!window.is_frame_ready()) return;

    // --- Step 2: render ---
    g_gl->MakeCurrent();

    const int w = g_gl->GetWidth();
    const int h = g_gl->GetHeight();

    glViewport(0, 0, w, h);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_program);
    glUniform2f(glGetUniformLocation(g_program, "u_resolution"),  static_cast<float>(w), static_cast<float>(h));

    // Content is clipped to exclude the title bar.
    // GL scissor origin is bottom-left, so the content area is
    // [0, 0, w, h - TITLE_BAR_HEIGHT].
    const int content_h = (h > TITLE_BAR_HEIGHT) ? (h - TITLE_BAR_HEIGHT) : 0;

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, w, content_h);

    draw_content(w, h);

    glDisable(GL_SCISSOR_TEST);

    // Title bar on top — always drawn last.
    draw_title_bar(w, window.is_maximized());

    window.request_frame_callback();
    g_gl->SwapBuffers();
}


// ─── 12. Main ───────────────────────────────────────────────────────────────

int main() {
    std::cout << "KernelNucleusT — CSD + OpenGL example\n";
    std::cout << "ESC = quit | drag the title bar | resize from edges\n\n";

    // STEP 1 — platform layer.
    KnstWindowSources::Init();
    if (KnstWindowSources::active_backend() == knst_backend::none) {
        std::cerr << "Failed to initialize the platform layer.\n";
        return 1;
    }

    // STEP 2 — shared GL state.
    knst_opengl_global::Options gl_options;
    gl_options.gl_major = 3;
    gl_options.gl_minor = 3;
    gl_options.core_profile = true;

    if (!knst_opengl_global::Init(gl_options)) {
        std::cerr << "Failed to initialize OpenGL global state.\n";
        KnstWindowSources::CleanUp();
        return 1;
    }

    // STEP 3 — window with CSD, still invisible.
    //
    // Both calls must happen BEFORE show():
    //   set_title_bar_height → tells the library how tall the custom
    //                          title bar is, so hit-test and content
    //                          layout match the drawing code.
    //   set_csd_hit_test     → installs the interactive-region
    //                          callback. Non-null callback = CSD mode,
    //                          native title bar removed.
    knst_window window(900, 600, u"CSD + OpenGL");
    window.set_redraw_callback(on_frame);
    window.set_title_bar_height(TITLE_BAR_HEIGHT);
    window.set_csd_hit_test(csd_hit_test);
    window.creation();

    // STEP 4 — GL context on this window.
    knst_window_opengl_content gl;
    if (!gl.Init(&window)) {
        std::cerr << "Failed to create the OpenGL context.\n";
        knst_opengl_global::CleanUp();
        KnstWindowSources::CleanUp();
        return 1;
    }
    g_gl = &gl;

    // STEP 5 — GLAD must run after MakeCurrent().
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

    std::cout << "GPU        : " << gl.GetRendererString()    << "\n";
    std::cout << "GL version : " << gl.GetVersionString()     << "\n";
    std::cout << "GLSL       : " << gl.GetGLSLVersionString() << "\n\n";

    // STEP 6 — build the render pipeline.
    //
    // The VBO starts empty. Every fill_rect / draw_content call uploads
    // 6 floats per vertex via glBufferSubData — a simple dynamic VBO
    // pattern that's fine for a small UI.
    //
    // Vertex layout (6 floats):
    //   attribute 0 → vec2 pixel position
    //   attribute 1 → vec4 rgba color
    g_program = build_shader_program();

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);

    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, 36 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    const GLsizei stride = 6 * sizeof(float);

    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1, 4, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // STEP 7 — draw the first frame before showing the window. Kills
    // the empty-window flash on compositors that animate mapping.
    on_frame(window, nullptr);
    window.show();

    // STEP 8 — main loop.
    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        window.call_redraw_callback();
        std::this_thread::yield();
    }

    std::cout << "Closing...\n";

    // STEP 9 — teardown, reverse order.
    gl.MakeCurrent();

    if (g_vbo) { glDeleteBuffers(1, &g_vbo); g_vbo = 0;}
    if (g_vao) { glDeleteVertexArrays(1, &g_vao); g_vao = 0;}
    if (g_program) { glDeleteProgram(g_program); g_program = 0;}

    g_gl = nullptr;

    gl.Shutdown();
    knst_opengl_global::CleanUp();
    window.destroy();
    KnstWindowSources::CleanUp();

    return 0;
}