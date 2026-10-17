/*
  knst_window_all_basic.cpp

  KernelNucleusT — Comprehensive Test Suite

  One file, tests the whole public API.

  Categories:
    A. Window lifecycle     -> F1..F7
    B. Size / position      -> N, B, S, E, W, Q, T
    C. Opacity              -> 1, 2, 3
    D. Cursor               -> C, V, X, Z, H, J, G, P, O, F
    E. Clipboard / Primary  -> K, L, M, U, Y
    F. Attributes           -> A, D, F8..F11
    G. CSD (custom decor)   -> Ctrl+T, Ctrl+X, Ctrl+S, Ctrl+A
    H. Behaviour            -> R (raw mouse), I (idle)
    I. Touch                -> Ctrl+Y, Ctrl+U
    J. Icon                 -> Shift+I
    K. Drag & drop          -> Shift+D + drop files
    L. Debug / verbose      -> ` (motion), ; (events), ? (help)
    M. Info                 -> 0, 8, 9, F12
    N. OpenGL               -> colored triangle in the background

  Console output is prefixed by tag:
    [TEST]  test action       [WIN]   window event
    [KEY]   keyboard          [MOUSE] mouse
    [TEXT]  text input        [CLIP]  clipboard
    [CSD]   client-side deco  [DROP]  file drag & drop
    [TOUCH] touch             [ERROR] error
*/

#include <KernelNucleusT.hpp> // yes < > Now it finally feels like a real library ::))
#include <iostream>


// -----------------------------------------------------------------------------
//  1) Shaders — interleaved position + color triangle
// -----------------------------------------------------------------------------
static const char* VERTEX_SHADER_SRC = KNST_GLSL_HEADER R"(
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_color;
out vec3 v_color;
void main() {
    gl_Position = vec4(a_position, 1.0);
    v_color     = a_color;
}
)";

static const char* FRAGMENT_SHADER_SRC = KNST_GLSL_HEADER R"(
in  vec3 v_color;
out vec4 frag_color;
void main() {
    frag_color = vec4(v_color, 1.0);
}
)";


// -----------------------------------------------------------------------------
//  2) Global state
// -----------------------------------------------------------------------------
static knst_window_opengl_content* g_gl = nullptr;
static knst_window* g_window = nullptr;
static GLuint g_program = 0;
static GLuint g_vao = 0;
static GLuint g_vbo = 0;


// Verbose toggles — flipped at runtime from the keyboard.
static bool g_verbose_motion = false;
static bool g_verbose_all_event = true;
static bool g_verbose_key = true;
static bool g_verbose_mouse = true;

// Mirrors of the current test state. Keeping these around saves a
// get_attribute() round-trip for toggles.
static bool g_waiting_clipboard = false;
static bool g_dnd_enabled = true;
static bool g_idle_inhibit = false;
static bool g_always_on_top = false;
static bool g_click_through = false;
static bool g_raw_mouse = false;
static bool g_csd_enabled = false;


static int  g_title_counter = 0;


// -----------------------------------------------------------------------------
//  3) Console helpers — plain, log-friendly, no colors
// -----------------------------------------------------------------------------

static void hr(const char* title) {
    std::cout << "\n";
    std::cout << "============================================================\n";
    if (title) std::cout << "  " << title << "\n";
    std::cout << "============================================================\n";
}

static void section(const char* title) {
    std::cout << "\n--- " << title << " ---\n";
}

static void ok(const char* msg)   { std::cout << "  [OK]   " << msg << "\n"; }
[[maybe_unused]] static void fail(const char* msg) { std::cout << "  [FAIL] " << msg << "\n"; }
static void info(const char* msg) { std::cout << "  [INFO] " << msg << "\n"; }
static void test(const char* msg) { std::cout << "  >>>    " << msg << "\n"; }


// -----------------------------------------------------------------------------
//  4) Info printers
// -----------------------------------------------------------------------------

static void print_help() {
    hr("HELP — all keyboard shortcuts");
    std::cout
    << "\n"
    << "+-- GENERAL ------------------------------------------------+\n"
    << "|  ESC       Quit                                           |\n"
    << "|  ?  /      Show this help                                 |\n"
    << "|  `         Toggle mouse-motion logging                    |\n"
    << "|  ;         Toggle all event logging                       |\n"
    << "|  -         Toggle keyboard logging only                   |\n"
    << "|  =         Toggle mouse logging only                      |\n"
    << "+-----------------------------------------------------------+\n"
    << "\n"
    << "+-- A) WINDOW LIFECYCLE ------------------------------------+\n"
    << "|  F1        Toggle fullscreen                              |\n"
    << "|  F2        Maximize                                       |\n"
    << "|  F3        Minimize                                       |\n"
    << "|  F4        Restore                                        |\n"
    << "|  F5        Hide (F6 to bring back)                        |\n"
    << "|  F6        Show                                           |\n"
    << "|  F7        Focus                                          |\n"
    << "+-----------------------------------------------------------+\n"
    << "\n"
    << "+-- B) SIZE / POSITION -------------------------------------+\n"
    << "|  N         move(100, 100)                                 |\n"
    << "|  B         resize(400, 300)                               |\n"
    << "|  S         resize(800, 600)                               |\n"
    << "|  E         set_minimum_size(400, 300)                     |\n"
    << "|  W         set_maximum_size(1200, 900)                    |\n"
    << "|  Q         Reset min/max limits                           |\n"
    << "|  T         Change title (Test #N)                         |\n"
    << "+-----------------------------------------------------------+\n"
    << "\n"
    << "+-- C) OPACITY ---------------------------------------------+\n"
    << "|  1         Opacity = 0.25 (very transparent)              |\n"
    << "|  2         Opacity = 0.50                                 |\n"
    << "|  3         Opacity = 1.00 (normal)                        |\n"
    << "+-----------------------------------------------------------+\n"
    << "\n"
    << "+-- D) CURSOR ----------------------------------------------+\n"
    << "|  C         ARROW                                          |\n"
    << "|  V         HAND                                           |\n"
    << "|  X         IBEAM (text)                                   |\n"
    << "|  Z         CROSSHAIR                                      |\n"
    << "|  H         HIDDEN (no grab)                               |\n"
    << "|  J         DISABLED (grab — exit with C/V/X/G)            |\n"
    << "|  G         reset_cursor (system default)                  |\n"
    << "|  F         Bitmap crosshair cursor (32x32)                |\n"
    << "|  P         set_cursor_pos_on_window(200, 200)             |\n"
    << "|  O         set_cursor_pos_global(500, 500)                |\n"
    << "+-----------------------------------------------------------+\n"
    << "\n"
    << "+-- E) CLIPBOARD / PRIMARY SELECTION -----------------------+\n"
    << "|  K         set_clipboard                                  |\n"
    << "|  L         request_clipboard (result on [CLIP] line)      |\n"
    << "|  M         clear_clipboard                                |\n"
    << "|  U         set_primary_selection                          |\n"
    << "|  Y         request_primary_selection                      |\n"
    << "+-----------------------------------------------------------+\n"
    << "\n"
    << "+-- F) WINDOW ATTRIBUTES -----------------------------------+\n"
    << "|  A         Toggle ALWAYS_ON_TOP                           |\n"
    << "|  D         Toggle CLICK_THROUGH (mouse passes through!)   |\n"
    << "|  F8        Toggle ALWAYS_ON_TOP (alt)                     |\n"
    << "|  F9        Toggle DECORATED                               |\n"
    << "|  F10       Toggle RESIZABLE                               |\n"
    << "|  F11       Reset min/max limits                           |\n"
    << "+-----------------------------------------------------------+\n"
    << "\n"
    << "+-- G) CSD (Custom Decoration) -----------------------------+\n"
    << "|  Ctrl+T    Enable CSD  (drag top 32px, edges for resize)  |\n"
    << "|  Ctrl+X    Disable CSD                                    |\n"
    << "|  Ctrl+S    Print CSD state                                |\n"
    << "|  Ctrl+A    Toggle CSD auto-cursor                         |\n"
    << "+-----------------------------------------------------------+\n"
    << "\n"
    << "+-- H) BEHAVIOUR -------------------------------------------+\n"
    << "|  R         Raw mouse mode (for FPS cameras)               |\n"
    << "|  I         Idle inhibit (keep screen awake)               |\n"
    << "+-----------------------------------------------------------+\n"
    << "\n"
    << "+-- I) TOUCH -----------------------------------------------+\n"
    << "|  Ctrl+Y    Toggle touch                                   |\n"
    << "|  Ctrl+U    Print touch info                               |\n"
    << "+-----------------------------------------------------------+\n"
    << "\n"
    << "+-- J) ICON ------------------------------------------------+\n"
    << "|  Shift+I   Apply 64x64 test icon                          |\n"
    << "+-----------------------------------------------------------+\n"
    << "\n"
    << "+-- K) DRAG & DROP -----------------------------------------+\n"
    << "|  Shift+D   Toggle drag-drop                               |\n"
    << "|  (drag a file onto the window to test)                    |\n"
    << "+-----------------------------------------------------------+\n"
    << "\n"
    << "+-- M) INFO ------------------------------------------------+\n"
    << "|  0         Caps/Num lock state                            |\n"
    << "|  8         Clipboard contents                             |\n"
    << "|  9         Monitor list                                   |\n"
    << "|  .         Full window state                              |\n"
    << "|  F12       Platform capabilities                          |\n"
    << "+-----------------------------------------------------------+\n";
}

// cool syntax
static void print_capabilities() {
    hr("Platform capabilities");
    auto c = knst_window::capabilities();

    section("Window management");
    std::cout << "  move               : " << (c.move ? "yes" : "no") << "\n";
    std::cout << "  resize_by_user     : " << (c.resize_by_user ? "yes" : "no") << "\n";
    std::cout << "  force_focus        : " << (c.force_focus ? "yes" : "no") << "\n";
    std::cout << "  cursor_position    : " << (c.cursor_position ? "yes" : "no") << "\n";
    std::cout << "  always_on_top      : " << (c.always_on_top ? "yes" : "no") << "\n";
    std::cout << "  multiple_windows   : " << (c.multiple_windows ? "yes" : "no") << "\n";

    section("Visual");
    std::cout << "  opacity            : " << (c.opacity ? "yes" : "no") << "\n";
    std::cout << "  input_transparent  : " << (c.input_transparent ? "yes" : "no") << "\n";
    std::cout << "  set_icon           : " << (c.set_icon ? "yes" : "no") << "\n";
    std::cout << "  fractional_scale   : " << (c.fractional_scale ? "yes" : "no") << "\n";

    section("Input");
    std::cout << "  text_input         : " << (c.text_input ? "yes" : "no") << "\n";
    std::cout << "  ime                : " << (c.ime ? "yes" : "no") << "\n";
    std::cout << "  raw_mouse          : " << (c.raw_mouse ? "yes" : "no") << "\n";
    std::cout << "  touch_screen       : " << (c.touch_screen ? "yes" : "no") << "\n";

    section("Data");
    std::cout << "  clipboard          : " << (c.clipboard ? "yes" : "no") << "\n";
    std::cout << "  primary_selection  : " << (c.primary_selection ? "yes" : "no") << "\n";
    std::cout << "  drag_drop          : " << (c.drag_drop ? "yes" : "no") << "\n";

    section("System");
    std::cout << "  vsync_control      : " << (c.vsync_control ? "yes" : "no") << "\n";
    std::cout << "  idle_inhibit       : " << (c.idle_inhibit ? "yes" : "no") << "\n";
}

static void print_monitors() {
    hr("Monitors");
    const auto& list = knst_display::get_monitor_list();
    std::cout << "  Count: " << list.size() << "\n";

    for (size_t i = 0; i < list.size(); ++i) {
        const auto& m = list[i];
        if (!m) continue;

        std::cout << "\n  [" << i << "] " << m->name << "\n";
        std::cout << "      Size      : " << m->width << " x " << m->height << "\n";
        std::cout << "      Position  : (" << m->root_x << ", " << m->root_y << ")\n";
        std::cout << "      DPI       : " << m->dpi_scale << "\n";
        std::cout << "      Refresh   : " << m->refresh_rate << " Hz\n";
        std::cout << "      Physical  : " << m->physical_width
                  << " x " << m->physical_height << " mm\n";
        std::cout << "      Primary   : " << (m->is_primary ? "yes" : "-") << "\n";
    }
}

static void print_window_state() {
    if (!g_window) return;
    hr("Window state");

    std::cout << "  Size          : " << g_window->get_width()  << " x " << g_window->get_height() << "\n";
    std::cout << "  Position      : (" << g_window->get_root_x() << ", " << g_window->get_root_y() << ")\n";
    std::cout << "  Title         : \"" << g_window->get_title() << "\"\n";
    std::cout << "  Opacity       : " << g_window->get_opacity() << "\n";
    std::cout << "  Fullscreen    : " << (g_window->is_fullscreen() ? "yes" : "-") << "\n";
    std::cout << "  Maximized     : " << (g_window->is_maximized()  ? "yes" : "-") << "\n";
    std::cout << "  Minimized     : " << (g_window->is_minimized()  ? "yes" : "-") << "\n";
    std::cout << "  Focused       : " << (g_window->is_focused()    ? "yes" : "-") << "\n";
    std::cout << "  CSD enabled   : " << (g_window->is_csd_enabled() ? "yes" : "-") << "\n";
    std::cout << "  Content off.  : " << g_window->get_content_y_offset() << "\n";
    std::cout << "  Content h.    : " << g_window->get_content_height() << "\n";
    std::cout << "  Touch support : " << (g_window->supports_touch() ? "yes" : "-") << "\n";
    std::cout << "  Touch enabled : " << (g_window->is_touch_enabled() ? "yes" : "-") << "\n";
    std::cout << "  Max touch pts : " << g_window->max_touch_points() << "\n";
}


// -----------------------------------------------------------------------------
//  5) Bitmap generators
// -----------------------------------------------------------------------------

// 32x32 RGBA crosshair cursor, hotspot at the center.
static knst_byte_string make_crosshair_cursor() {
    constexpr int W = 32, H = 32;
    knst_byte_string out;
    out.resize(W * H * 4, 0);

    const int cx = W / 2, cy = H / 2;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const int dx = (x >= cx) ? (x - cx) : (cx - x);
            const int dy = (y >= cy) ? (y - cy) : (cy - y);
            uint8_t r = 0, g = 0, b = 0, a = 0;

            if (dx <= 1 && dy <= 8) { r = 255; a = 255; }   // vertical bar
            if (dy <= 1 && dx <= 8) { r = 255; a = 255; }   // horizontal bar
            if (dx == 0 && dy == 0) { r = 255; g = 255; a = 255; } // center dot

            const size_t i = ((size_t)y * W + x) * 4;
            out[i+0] = r; out[i+1] = g; out[i+2] = b; out[i+3] = a;
        }
    }
    return out;
}

// 64x64 RGBA test icon — orange checkered square with blue border.
static knst_byte_string make_test_icon() {
    constexpr int W = 64, H = 64;
    knst_byte_string out;
    out.resize(W * H * 4, 0);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            uint8_t r, g, b, a = 255;
            if (x < 4 || y < 4 || x >= W - 4 || y >= H - 4) {
                r = 20; g = 30; b = 100;        // blue border
            } else if (((x + y) / 8) % 2 == 0) {
                r = 255; g = 180; b = 60;       // light orange
            } else {
                r = 240; g = 100; b = 40;       // dark orange
            }
            const size_t i = ((size_t)y * W + x) * 4;
            out[i+0] = r; out[i+1] = g; out[i+2] = b; out[i+3] = a;
        }
    }
    return out;
}


// -----------------------------------------------------------------------------
//  6) OpenGL setup
// -----------------------------------------------------------------------------

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
        std::cerr << "[ERROR] Shader compile failed:\n" << log << "\n";
    }
    return s;
}

static GLuint build_shader_program() {
    GLuint vs = compile_shader(GL_VERTEX_SHADER,   VERTEX_SHADER_SRC);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER_SRC);

    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetProgramInfoLog(p, sizeof(log) - 1, nullptr, log);
        std::cerr << "[ERROR] Program link failed:\n" << log << "\n";
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

static void setup_triangle() {
    const float verts[] = {
        // x      y      z      r     g     b
        -0.6f, -0.6f,  0.0f,  1.0f, 0.2f, 0.2f,
         0.6f, -0.6f,  0.0f,  0.2f, 1.0f, 0.2f,
         0.0f,  0.6f,  0.0f,  0.2f, 0.2f, 1.0f,
    };

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);

    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    // Interleaved: 3 floats position + 3 floats color = 6 per vertex.
    const GLsizei stride = 6 * sizeof(float);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,  reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

static void render_frame() {
    const int w = g_gl->GetFramebufferWidth();
    const int h = g_gl->GetFramebufferHeight();

    glViewport(0, 0, w, h);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_program);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}


// -----------------------------------------------------------------------------
//  7) CSD hit-test — user-supplied callback
//
//  8-pixel resize border all around, 32-pixel draggable title zone at
//  the top. Anything else is normal content.
// -----------------------------------------------------------------------------

static knst_window::csd_hit csd_hit_test(const knst_window& w, int x, int y) {
    const int W = w.get_width();
    const int H = w.get_height();
    constexpr int BORDER  = 8;
    constexpr int TITLE_H = 32;

    if (y < BORDER && x < BORDER)             return knst_window::csd_hit::resize_top_left;
    if (y < BORDER && x >= W - BORDER)        return knst_window::csd_hit::resize_top_right;
    if (y >= H - BORDER && x < BORDER)        return knst_window::csd_hit::resize_bottom_left;
    if (y >= H - BORDER && x >= W - BORDER)   return knst_window::csd_hit::resize_bottom_right;
    if (y < BORDER)                            return knst_window::csd_hit::resize_top;
    if (y >= H - BORDER)                       return knst_window::csd_hit::resize_bottom;
    if (x < BORDER)                            return knst_window::csd_hit::resize_left;
    if (x >= W - BORDER)                       return knst_window::csd_hit::resize_right;
    if (y < TITLE_H)                           return knst_window::csd_hit::draggable;

    return knst_window::csd_hit::normal;
}


// -----------------------------------------------------------------------------
//  8) Key handler
// -----------------------------------------------------------------------------

static void handle_key(knst_window& window, const knst_window_event& ev) {
    const bool ctrl = (ev.mods & KNST_WINDOW_MOD_CONTROL) != 0;
    const bool shift = (ev.mods & KNST_WINDOW_MOD_SHIFT) != 0;

    // --- Ctrl combos ---
    if (ctrl) {
        switch (ev.key_code) {
            case KNST_WINDOW_KEY_CODE_T:
                if (!g_csd_enabled) {
                    window.set_csd_hit_test(csd_hit_test);
                    window.set_attribute(KNST_WINDOW_ATTR_DECORATED, false);
                    g_csd_enabled = true;
                    test("CSD enabled — drag top 32px, resize at edges");
                } else {
                    info("CSD already on");
                }
                break;

            case KNST_WINDOW_KEY_CODE_X:
                if (g_csd_enabled) {
                    window.clear_csd_hit_test();
                    window.set_attribute(KNST_WINDOW_ATTR_DECORATED, true);
                    g_csd_enabled = false;
                    test("CSD off — native title bar is back");
                }
                break;

            case KNST_WINDOW_KEY_CODE_S:
                section("CSD state");
                std::cout << "  enabled     : " << (window.is_csd_enabled() ? "on" : "off") << "\n";
                std::cout << "  auto_cursor : " << (window.is_csd_auto_cursor() ? "on" : "off") << "\n";
                break;

            case KNST_WINDOW_KEY_CODE_A:
                window.set_csd_auto_cursor(!window.is_csd_auto_cursor());
                test(window.is_csd_auto_cursor() ? "CSD auto-cursor on": "CSD auto-cursor off");
                break;

            case KNST_WINDOW_KEY_CODE_Y:
                window.enable_touch_screen(!window.is_touch_enabled());
                test(window.is_touch_enabled() ? "Touch on" : "Touch off");
                break;

            case KNST_WINDOW_KEY_CODE_U:
                section("Touch info");
                std::cout << "  supported  : " << (window.supports_touch() ? "yes" : "no") << "\n";
                std::cout << "  enabled    : " << (window.is_touch_enabled() ? "yes" : "no") << "\n";
                std::cout << "  max_points : " << window.max_touch_points() << "\n";
                break;
        }
        return;
    }

    // --- Shift combos ---
    if (shift) {
        switch (ev.key_code) {
            case KNST_WINDOW_KEY_CODE_D:
                g_dnd_enabled = !g_dnd_enabled;
                window.set_drag_drop_status(g_dnd_enabled);
                test(g_dnd_enabled ? "Drag-drop on" : "Drag-drop off");
                break;

            case KNST_WINDOW_KEY_CODE_I: {
                auto icon = make_test_icon();
                window.apply_bmp_icon(icon, 64, 64);
                test("64x64 test icon applied");
                break;
            }
        }
        return;
    }

    // --- Plain keys ---
    switch (ev.key_code) {

        // A) window lifecycle
        case KNST_WINDOW_KEY_CODE_ESCAPE:
            test("ESC — quit");
            window.should_close();
            break;
        case KNST_WINDOW_KEY_CODE_F1:
            test("F1 — toggle fullscreen");
            window.toggle_fullscreen(!window.is_fullscreen());
            break;
        case KNST_WINDOW_KEY_CODE_F2:
            test("F2 — maximize");
            window.set_maximized();
            break;
        case KNST_WINDOW_KEY_CODE_F3:
            test("F3 — minimize (F4 to restore)");
            window.set_minimized();
            break;
        case KNST_WINDOW_KEY_CODE_F4:
            test("F4 — restore");
            window.restore();
            break;
        case KNST_WINDOW_KEY_CODE_F5:
            test("F5 — hide (F6 to show)");
            window.hide();
            break;
        case KNST_WINDOW_KEY_CODE_F6:
            test("F6 — show");
            window.show();
            break;
        case KNST_WINDOW_KEY_CODE_F7:
            test("F7 — focus");
            window.focus();
            break;

        // B) size / position
        case KNST_WINDOW_KEY_CODE_N:
            test("N — move(100, 100)");
            window.move(100, 100);
            break;
        case KNST_WINDOW_KEY_CODE_B:
            test("B — resize(400, 300)");
            window.resize(400, 300);
            break;
        case KNST_WINDOW_KEY_CODE_S:
            test("S — resize(800, 600)");
            window.resize(800, 600);
            break;
        case KNST_WINDOW_KEY_CODE_E:
            test("E — set_minimum_size(400, 300)");
            window.set_minimum_size(400, 300);
            break;
        case KNST_WINDOW_KEY_CODE_W:
            test("W — set_maximum_size(1200, 900)");
            window.set_maximum_size(1200, 900);
            break;
        case KNST_WINDOW_KEY_CODE_Q:
            test("Q — reset min/max limits");
            window.set_minimum_size(KNST_WINDOW_DEFAULT, KNST_WINDOW_DEFAULT);
            window.set_maximum_size(KNST_WINDOW_DEFAULT, KNST_WINDOW_DEFAULT);
            break;
        case KNST_WINDOW_KEY_CODE_T: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Knst Test #%d", ++g_title_counter);
            window.set_title(knst_c16string(buf));
            test(buf);
            break;
        }

        // C) opacity
        case KNST_WINDOW_KEY_CODE_1:
            window.set_opacity(0.25f);
            test("Opacity = 0.25");
            break;
        case KNST_WINDOW_KEY_CODE_2:
            window.set_opacity(0.5f);
            test("Opacity = 0.50");
            break;
        case KNST_WINDOW_KEY_CODE_3:
            window.set_opacity(1.0f);
            test("Opacity = 1.00");
            break;

        // D) cursor
        case KNST_WINDOW_KEY_CODE_C:
            window.set_cursor(KNST_WINDOW_CURSOR_ARROW);
            test("Cursor = ARROW");
            break;
        case KNST_WINDOW_KEY_CODE_V:
            window.set_cursor(KNST_WINDOW_CURSOR_HAND);
            test("Cursor = HAND");
            break;
        case KNST_WINDOW_KEY_CODE_X:
            window.set_cursor(KNST_WINDOW_CURSOR_IBEAM);
            test("Cursor = IBEAM");
            break;
        case KNST_WINDOW_KEY_CODE_Z:
            window.set_cursor(KNST_WINDOW_CURSOR_CROSSHAIR);
            test("Cursor = CROSSHAIR");
            break;
        case KNST_WINDOW_KEY_CODE_H:
            window.set_cursor_mode(KNST_WINDOW_CURSOR_HIDDEN);
            test("Cursor = HIDDEN (recover with C/V/X/G)");
            break;
        case KNST_WINDOW_KEY_CODE_J:
            window.set_cursor_mode(KNST_WINDOW_CURSOR_DISABLED);
            test("Cursor = DISABLED (mouse locked — C/V/X/G to escape)");
            break;
        case KNST_WINDOW_KEY_CODE_G:
            window.reset_cursor();
            test("Cursor = RESET");
            break;
        case KNST_WINDOW_KEY_CODE_P:
            window.set_cursor_pos_on_window(200, 200);
            test("set_cursor_pos_on_window(200, 200)");
            break;
        case KNST_WINDOW_KEY_CODE_O:
            window.set_cursor_pos_global(500, 500);
            test("set_cursor_pos_global(500, 500)");
            break;
        case KNST_WINDOW_KEY_CODE_F: {
            auto cur = make_crosshair_cursor();
            window.set_bmp_cursor(cur, 32, 32, 16, 16);
            test("Bitmap crosshair cursor 32x32 (G to reset)");
            break;
        }

        // E) clipboard / primary
        case KNST_WINDOW_KEY_CODE_K:
            window.set_clipboard(u"Hello! This is a KernelNucleusT test string.");
            test("Wrote to clipboard — paste into another app to verify");
            break;
        case KNST_WINDOW_KEY_CODE_L:
            window.request_clipboard();
            g_waiting_clipboard = true;
            test("Clipboard read requested...");
            break;
        case KNST_WINDOW_KEY_CODE_M:
            window.clear_clipboard();
            test("Clipboard cleared");
            break;
        case KNST_WINDOW_KEY_CODE_U:
            window.set_primary_selection(u"Knst primary selection test");
            test("Wrote to primary selection — middle-click to verify");
            break;
        case KNST_WINDOW_KEY_CODE_Y:
            window.request_primary_selection();
            g_waiting_clipboard = true;
            test("Primary selection read requested...");
            break;

        // F) attributes
        case KNST_WINDOW_KEY_CODE_A:
        case KNST_WINDOW_KEY_CODE_F8:
            g_always_on_top = !g_always_on_top;
            window.set_attribute(KNST_WINDOW_ATTR_ALWAYS_ON_TOP, g_always_on_top);
            test(g_always_on_top ? "Always-on-top on" : "Always-on-top off");
            break;
        case KNST_WINDOW_KEY_CODE_D:
            g_click_through = !g_click_through;
            window.set_attribute(KNST_WINDOW_ATTR_CLICK_THROUGH, g_click_through);
            test(g_click_through
                 ? "Click-through ON (mouse passes through — press D to undo)"
                 : "Click-through off");
            break;
        case KNST_WINDOW_KEY_CODE_F9: {
            bool cur = window.get_attribute(KNST_WINDOW_ATTR_DECORATED);
            window.set_attribute(KNST_WINDOW_ATTR_DECORATED, !cur);
            test(!cur ? "DECORATED on" : "DECORATED off");
            break;
        }
        case KNST_WINDOW_KEY_CODE_F10: {
            bool cur = window.get_attribute(KNST_WINDOW_ATTR_RESIZABLE);
            window.set_attribute(KNST_WINDOW_ATTR_RESIZABLE, !cur);
            test(!cur ? "RESIZABLE on" : "RESIZABLE off");
            break;
        }
        case KNST_WINDOW_KEY_CODE_F11:
            window.set_minimum_size(KNST_WINDOW_DEFAULT, KNST_WINDOW_DEFAULT);
            window.set_maximum_size(KNST_WINDOW_DEFAULT, KNST_WINDOW_DEFAULT);
            test("Min/max limits reset");
            break;
        case KNST_WINDOW_KEY_CODE_F12:
            print_capabilities();
            break;

        // H) behaviour
        case KNST_WINDOW_KEY_CODE_R:
            g_raw_mouse = !g_raw_mouse;
            window.set_raw_mouse_mode(g_raw_mouse);
            test(g_raw_mouse ? "Raw mouse on (cursor hidden, FPS style)" : "Raw mouse off");
                 
            break;
        case KNST_WINDOW_KEY_CODE_I:
            g_idle_inhibit = !g_idle_inhibit;
            window.set_idle_inhibit(g_idle_inhibit);
            test(g_idle_inhibit ? "Idle-inhibit on" : "Idle-inhibit off");
            break;

        // M) info
        case KNST_WINDOW_KEY_CODE_0:
            section("Lock state");
            std::cout << "  CapsLock : " << (window.is_caps_lock_on() ? "on" : "off") << "\n";
            std::cout << "  NumLock  : " << (window.is_num_lock_on()  ? "on" : "off") << "\n";
            break;
        case KNST_WINDOW_KEY_CODE_8:
            section("Clipboard contents");
            std::cout << "  \"" << window.get_clipboard() << "\"\n";
            break;
        case KNST_WINDOW_KEY_CODE_9:
            print_monitors();
            break;

        // Verbose toggles
        case KNST_WINDOW_KEY_CODE_GRAVE:
            g_verbose_motion = !g_verbose_motion;
            test(g_verbose_motion ? "Mouse-motion log on" : "Mouse-motion log off");
                                  
            break;
        case KNST_WINDOW_KEY_CODE_SEMICOLON:
            g_verbose_all_event = !g_verbose_all_event;
            test(g_verbose_all_event ? "All-event log on"  : "All-event log off");
                                    
            break;
        case KNST_WINDOW_KEY_CODE_MINUS:
            g_verbose_key = !g_verbose_key;
            test(g_verbose_key ? "Keyboard log on" : "Keyboard log off");
            break;
        case KNST_WINDOW_KEY_CODE_EQUAL:
            g_verbose_mouse = !g_verbose_mouse;
            test(g_verbose_mouse ? "Mouse-event log on" : "Mouse-event log off");
            break;
        case KNST_WINDOW_KEY_CODE_SLASH:
            print_help();
            break;

        case KNST_WINDOW_KEY_CODE_PERIOD:
            print_window_state();
            break;
    }
}


// -----------------------------------------------------------------------------
//  9) Event handler — called once per frame from the main loop
// -----------------------------------------------------------------------------

static void on_frame(knst_window& window, void* /*user_data*/) {
    for (size_t i = 0; i < window.event_count(); ++i) {
        const knst_window_event& ev = window.get_window_event_handle(i);

        switch (ev.type) {

            // --- Keyboard ---
            case KNST_WINDOW_EVENT_KEYBOARD:
                if (ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS) {
                    handle_key(window, ev);
                } else if (ev.key_action == KNST_WINDOW_KEY_ACTION_REPEAT) {
                    if (g_verbose_key) {
                        std::cout << "[KEY] repeat 0x" << std::hex << ev.key_code  << std::dec << "\n";
                                 
                    }
                } else if (ev.key_action == KNST_WINDOW_KEY_ACTION_RELEASE) {
                    // Skip modifier releases — too noisy.
                    if (g_verbose_key &&
                        ev.key_code != KNST_WINDOW_KEY_CODE_SHIFT &&
                        ev.key_code != KNST_WINDOW_KEY_CODE_CONTROL &&
                        ev.key_code != KNST_WINDOW_KEY_CODE_ALT) {
                        std::cout << "[KEY] release 0x" << std::hex << ev.key_code << std::dec << "\n";
                                  
                    }
                }
                break;

            // --- Text input ---
            case KNST_WINDOW_EVENT_TEXT_INPUT:
                if (g_verbose_key)
                    std::cout << "[TEXT] \"" << ev.text_input << "\"\n";
                break;

            // --- IME ---
            case KNST_WINDOW_EVENT_TEXT_PREEDIT:
                std::cout << "[IME] preedit: \"" << ev.text_preedit << "\"\n";
                break;
            case KNST_WINDOW_EVENT_TEXT_DELETE_SURROUNDING:
                std::cout << "[IME] delete before=" << ev.text_delete_before
                          << " after=" << ev.text_delete_after << "\n";
                break;

            // --- Mouse ---
            case KNST_WINDOW_EVENT_MOUSE:
                if (!g_verbose_mouse) break;

                if (ev.mouse_action == KNST_WINDOW_MOUSE_ACTION_PRESS) {
                    const char* b = "?";
                    switch (ev.mouse_button) {
                        case KNST_WINDOW_MOUSE_BUTTON_LEFT:    b = "LEFT";    break;
                        case KNST_WINDOW_MOUSE_BUTTON_RIGHT:   b = "RIGHT";   break;
                        case KNST_WINDOW_MOUSE_BUTTON_MIDDLE:  b = "MIDDLE";  break;
                        case KNST_WINDOW_MOUSE_BUTTON_BACK:    b = "BACK";    break;
                        case KNST_WINDOW_MOUSE_BUTTON_FORWARD: b = "FORWARD"; break;
                    }
                    std::cout << "[MOUSE] PRESS " << b
                              << " @ (" << ev.mouse_x << "," << ev.mouse_y << ")\n";
                } else if (ev.mouse_action == KNST_WINDOW_MOUSE_ACTION_RELEASE) {
                    std::cout << "[MOUSE] RELEASE btn=" << ev.mouse_button << "\n";
                } else if (ev.mouse_action == KNST_WINDOW_MOUSE_ACTION_SCROLL) {
                    std::cout << "[MOUSE] SCROLL delta=" << ev.mouse_scroll_delta  << " (btn=" << ev.mouse_button << ")\n";
                }
                break;

            case KNST_WINDOW_EVENT_MOTION:
                if (g_verbose_motion) {
                    if (ev.is_raw) {
                        std::cout << "[MOUSE] RAW d=(" << ev.raw_delta_x  << "," << ev.raw_delta_y << ")\n";
                    } else {
                        std::cout << "[MOUSE] (" << ev.mouse_x  << "," << ev.mouse_y << ")\n";
                    }
                }
                break;

            case KNST_WINDOW_EVENT_ENTER:
                if (g_verbose_mouse) std::cout << "[MOUSE] ENTER\n";
                break;
            case KNST_WINDOW_EVENT_LEAVE:
                if (g_verbose_mouse) std::cout << "[MOUSE] LEAVE\n";
                break;

            // --- Window state ---
            case KNST_WINDOW_EVENT_RESIZE:
                if (g_verbose_all_event)
                    std::cout << "[WIN] RESIZE " << ev.window_width
                              << "x" << ev.window_height << "\n";
                if (g_gl && g_gl->IsValid()) {
                    g_gl->OnResize(ev.window_width, ev.window_height);
                }
                break;

            case KNST_WINDOW_EVENT_MOVE:
                if (g_verbose_all_event)
                    std::cout << "[WIN] MOVE -> (" << ev.window_root_x  << "," << ev.window_root_y << ")\n";
                             
                break;

            case KNST_WINDOW_EVENT_MAXIMIZE: std::cout << "[WIN] MAXIMIZE\n"; break;
            case KNST_WINDOW_EVENT_MINIMIZE: std::cout << "[WIN] MINIMIZE\n"; break;
            case KNST_WINDOW_EVENT_RESTORE: std::cout << "[WIN] RESTORE\n"; break;
            case KNST_WINDOW_EVENT_FULLSCREEN: std::cout << "[WIN] FULLSCREEN\n"; break;

            case KNST_WINDOW_EVENT_EXPOSE:
                if (g_verbose_all_event) std::cout << "[WIN] EXPOSE\n";
                break;

            case KNST_WINDOW_EVENT_SCALE_CHANGED:
                std::cout << "[WIN] SCALE_CHANGED (HiDPI)\n";
                if (g_gl && g_gl->IsValid()) g_gl->OnScaleChanged();
                break;

            case KNST_WINDOW_EVENT_CLOSE:
                std::cout << "[WIN] CLOSE\n";
                window.should_close();
                break;

            case KNST_WINDOW_EVENT_DISCONNECT:
                std::cout << "[WIN] DISCONNECT — connection lost\n";
                window.should_close();
                break;

            // --- Focus ---
            case KNST_WINDOW_EVENT_FOCUS_IN:  std::cout << "[FOCUS] IN\n";  break;
            case KNST_WINDOW_EVENT_FOCUS_OUT: std::cout << "[FOCUS] OUT\n"; break;

            // --- Clipboard ---
            case KNST_WINDOW_EVENT_CLIPBOARD_DATA:
                if (g_waiting_clipboard) {
                    std::cout << "[CLIP] contents = \"" << window.get_clipboard()
                              << "\"\n";
                    g_waiting_clipboard = false;
                }
                break;
            case KNST_WINDOW_EVENT_CLIPBOARD_UPDATE:
                std::cout << "[CLIP] updated (Windows only)\n";
                break;

            // --- File drop ---
            case KNST_WINDOW_EVENT_FILE_DROP_ENTER:
                std::cout << "[DROP] ENTER\n";
                break;
            case KNST_WINDOW_EVENT_FILE_DROP_MOVE:
                std::cout << "[DROP] MOVE @ (" << ev.mouse_x << "," << ev.mouse_y << ")\n";
                break;
            case KNST_WINDOW_EVENT_FILE_DROP_LEAVE:
                std::cout << "[DROP] LEAVE\n";
                break;
            case KNST_WINDOW_EVENT_FILE_DROP:
                std::cout << "[DROP] dropped " << ev.drop_count << " file(s):\n";
                if (ev.drop_files) {
                    for (uint32_t k = 0; k < ev.drop_count; ++k) {
                        std::cout << "        - " << (*ev.drop_files)[k] << "\n";
                    }
                }
                break;

            // --- Touch ---
            case KNST_WINDOW_EVENT_TOUCH: {
                const char* act = "?";
                switch (ev.touch_action) {
                    case KNST_WINDOW_TOUCH_ACTION_PRESS:           act = "PRESS";       break;
                    case KNST_WINDOW_TOUCH_ACTION_RELEASE:         act = "RELEASE";     break;
                    case KNST_WINDOW_TOUCH_ACTION_MOVE:            act = "MOVE";        break;
                    case KNST_WINDOW_TOUCH_ACTION_CANCEL:          act = "CANCEL";      break;
                    case KNST_WINDOW_TOUCH_ACTION_OUTSIDE:         act = "OUTSIDE";     break;
                    case KNST_WINDOW_TOUCH_ACTION_POINTER_PRESS:   act = "PTR_PRESS";   break;
                    case KNST_WINDOW_TOUCH_ACTION_POINTER_RELEASE: act = "PTR_RELEASE"; break;
                }
                std::cout << "[TOUCH] " << act
                          << " count=" << ev.pointer_count << "\n";
                for (int p = 0; p < ev.pointer_count; ++p) {
                    std::cout << "        #" << ev.pointer_id[p] << " @ (" << ev.pointer_x[p] << "," << ev.pointer_y[p] << ")\n";
                }
                break;
            }

            // --- Android ---
            case KNST_WINDOW_EVENT_MOBILE_VOLUME_UP:   std::cout << "[MOBILE] VOLUME_UP\n";   break;
            case KNST_WINDOW_EVENT_MOBILE_VOLUME_DOWN: std::cout << "[MOBILE] VOLUME_DOWN\n"; break;
            case KNST_WINDOW_EVENT_MOBILE_VOLUME_MUTE: std::cout << "[MOBILE] VOLUME_MUTE\n"; break;
            case KNST_WINDOW_EVENT_MOBILE_BACK:        std::cout << "[MOBILE] BACK\n";        break;
            case KNST_WINDOW_EVENT_MOBILE_MENU:        std::cout << "[MOBILE] MENU\n";        break;
            case KNST_WINDOW_EVENT_APP_STARTED:        std::cout << "[APP] STARTED\n";        break;
            case KNST_WINDOW_EVENT_APP_RESUMED:        std::cout << "[APP] RESUMED\n";        break;
            case KNST_WINDOW_EVENT_APP_PAUSED:         std::cout << "[APP] PAUSED\n";         break;
            case KNST_WINDOW_EVENT_APP_STOPPED:        std::cout << "[APP] STOPPED\n";        break;
            case KNST_WINDOW_EVENT_APP_LOW_MEMORY:     std::cout << "[APP] LOW MEMORY\n";     break;

            case KNST_WINDOW_EVENT_UNKNOWN:
                break;

            default:
                if (g_verbose_all_event) {
                    std::cout << "[??] unknown event type=" << ev.type << "\n";
                }
                break;
        }
    }

    window.clear_events();

    if (window.is_should_close()) return;
    if (!window.is_frame_ready()) return;

    window.request_frame_callback();
    g_gl->MakeCurrent();
    render_frame();
    g_gl->SwapBuffers();
}


// -----------------------------------------------------------------------------
// 10) Main
// -----------------------------------------------------------------------------

int main() {
    std::cout << "============================================================\n";
    std::cout << "  KernelNucleusT — Comprehensive Test Suite\n";
    std::cout << "  (press ? inside the app for help)\n";
    std::cout << "============================================================\n";

    // ---- 1. Platform init ----
    hr("1. PLATFORM INIT");
    KnstWindowSources::Init();
    if (KnstWindowSources::active_backend() == knst_backend::none) {
        std::cerr << "[ERROR] Platform init failed\n";
        return 1;
    }

    std::cout << "  Backend: "
              << (KnstWindowSources::is_windows_active() ? "Windows" :
                  KnstWindowSources::is_x11_active()     ? "X11" :
                  KnstWindowSources::is_wayland_active() ? "Wayland" : "?")
              << "\n";
    ok("Platform layer ready");

    print_monitors();

    // ---- 2. OpenGL global init ----
    hr("2. OPENGL GLOBAL INIT");
    knst_opengl_global::Options opts;
    opts.gl_major = 3;
    opts.gl_minor = 3;
    opts.core_profile = true;
    opts.vsync = false;

    if (!knst_opengl_global::Init(opts)) {
        std::cerr << "[ERROR] OpenGL global init failed\n";
        KnstWindowSources::CleanUp();
        return 1;
    }
    ok("EGL/GL global ready");

    // ---- 3. Create window ----
    hr("3. WINDOW CREATION");
    knst_window window(800, 600, u"Knst — Comprehensive Test");
    g_window = &window;
    window.set_redraw_callback(on_frame);
    window.creation();
    ok("Native window created (not yet visible)");

    // ---- 4. GL content ----
    hr("4. OPENGL CONTENT INIT");
    knst_window_opengl_content gl;
    if (!gl.Init(&window)) {
        std::cerr << "[ERROR] GL context init failed\n";
        knst_opengl_global::CleanUp();
        KnstWindowSources::CleanUp();
        return 1;
    }
    g_gl = &gl;

    gl.MakeCurrent();
    if (!gladLoadGLLoader(
            reinterpret_cast<GLADloadproc>(
                knst_window_opengl_content::LoadGLProc))) {
        std::cerr << "[ERROR] GLAD load failed\n";
        gl.Shutdown();
        knst_opengl_global::CleanUp();
        KnstWindowSources::CleanUp();
        return 1;
    }
    std::cout << "  GPU     : " << gl.GetRendererString()    << "\n";
    std::cout << "  GL ver. : " << gl.GetVersionString()     << "\n";
    std::cout << "  GLSL    : " << gl.GetGLSLVersionString() << "\n";

    // ---- 5. GL pipeline ----
    g_program = build_shader_program();
    setup_triangle();
    ok("Shader + triangle ready");

    // ---- 6. Capabilities ----
    print_capabilities();

    // ---- 7. Help ----
    print_help();

    // ---- 8. First frame (before showing) ----
    hr("5. FIRST FRAME (before show)");
    window.set_drag_drop_status(true);
    g_dnd_enabled = true;

    gl.MakeCurrent();
    render_frame();
    gl.SwapBuffers();
    ok("First frame drawn (prevents flash on show)");

    window.show();
    ok("Window is now visible");

    // ---- 9. Main loop ----
    hr("6. MAIN LOOP — go test things");
    std::cout << " Press ? for help\n";
    std::cout << " Press ESC to quit\n\n";

    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        window.call_redraw_callback();
        std::this_thread::yield();
    }

    // ---- 10. Shutdown ----
    hr("7. SHUTDOWN");
    gl.MakeCurrent();

    if (g_vbo) { glDeleteBuffers(1, &g_vbo); g_vbo = 0; }
    if (g_vao) { glDeleteVertexArrays(1, &g_vao); g_vao = 0; }
    if (g_program) { glDeleteProgram(g_program); g_program = 0; }

    g_gl = nullptr;

    gl.Shutdown();
    knst_opengl_global::CleanUp();
    window.destroy();
    KnstWindowSources::CleanUp();

    std::cout << "\n";
    ok("Clean exit — see you next time");

    return 0;
}