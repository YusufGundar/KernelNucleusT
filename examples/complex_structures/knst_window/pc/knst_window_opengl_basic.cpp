/*
  knst_window_opengl_basic.cpp

  A minimal OpenGL 3.3 Core example — draws a colored triangle and
  closes on ESC.

  Steps covered:
    1. Initialize the platform layer
    2. Initialize the shared GL context
    3. Create the window WITHOUT showing it
    4. Attach a GL context to the window
    5. Load GL function pointers via GLAD
    6. Build a shader program + VBO/VAO
    7. Draw the first frame BEFORE showing the window — this kills the
       "flash of uninitialized content" that compositors like Cinnamon /
       Muffin and KWin produce on mapped windows during fade-in
    8. Run the event/render loop
    9. Shut down in the right order

  Controls: ESC closes the window.
*/

#include "../../../../include/KernelNucleusT.hpp"
#include <iostream>


// ─── 1. Shaders ─────────────────────────────────────────────────────────────
// Vertex: 3D position + 3D color per vertex.
// Fragment: outputs the interpolated color.

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


// ─── 2. Global state ────────────────────────────────────────────────────────
// Kept minimal on purpose. A real app would wrap this in a struct, but
// statics are easier to follow for a first example.

static knst_window_opengl_content* g_gl = nullptr;
static GLuint g_program = 0;
static GLuint g_vao = 0;
static GLuint g_vbo = 0;


// ─── 3. Shader program builder ──────────────────────────────────────────────
// Compiles vertex + fragment, links, returns the program handle.
// No error checks here for brevity — a real app would query
// glGetShaderiv / glGetProgramiv.

static GLuint build_shader_program() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource (vs, 1, &VERTEX_SHADER_SRC, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource (fs, 1, &FRAGMENT_SHADER_SRC, nullptr);
    glCompileShader(fs);

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram (program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    return program;
}


// ─── 4. Geometry setup ──────────────────────────────────────────────────────
// One triangle, position + color interleaved:
//   location 0  →  3 floats position
//   location 1  →  3 floats color

static void setup_triangle() {
    const float vertices[] = {
        //  x      y      z       r     g     b
        -0.5f, -0.5f,  0.0f,   1.0f, 0.2f, 0.2f,   // bottom-left  → red
         0.5f, -0.5f,  0.0f,   0.2f, 1.0f, 0.2f,   // bottom-right → green
         0.0f,  0.5f,  0.0f,   0.2f, 0.2f, 1.0f,   // top          → blue
    };

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);

    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    const GLsizei stride = 6 * sizeof(float);

    // Position (vec3) at offset 0.
    glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(0);

    // Color (vec3) at offset 3 floats.
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
}


// ─── 5. Render one frame ────────────────────────────────────────────────────
// Called from the main loop, and once before the window is shown.

static void render_frame() {
    const int width  = g_gl->GetWidth();
    const int height = g_gl->GetHeight();

    glViewport(0, 0, width, height);

    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_program);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}


// ─── 6. Event handler ───────────────────────────────────────────────────────
// Runs once per frame from the redraw callback. Consume every queued
// event, then clear the ring.

static void on_frame(knst_window& window, void* /*user_data*/) {
    for (size_t i = 0; i < window.event_count(); ++i) {
        const knst_window_event& ev = window.get_window_event_handle(i);

        switch (ev.type) {
            // WM / OS asked us to close.
            case KNST_WINDOW_EVENT_CLOSE:
                window.should_close();
                break;

            // ESC also closes.
            case KNST_WINDOW_EVENT_KEYBOARD:
                if (ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS &&
                    ev.key_code   == KNST_WINDOW_KEY_CODE_ESCAPE) {
                    window.should_close();
                }
                break;

            // Window resized — tell the GL surface.
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

    // Skip the frame if the compositor isn't ready (Wayland frame
    // gating). On X11 / Windows this always returns true.
    if (!window.is_frame_ready()) return;

    g_gl->MakeCurrent();
    render_frame();
    g_gl->SwapBuffers();
}


// ─── 7. Main ────────────────────────────────────────────────────────────────

int main() {
    std::cout << "KernelNucleusT — OpenGL 3.3 Core example\n";
    std::cout << "Press ESC to close the window.\n\n";

    // STEP 1 — platform layer.
    // Opens the X11 display (or Wayland connection), interns atoms,
    // enumerates monitors. Call once before creating any window.
    KnstWindowSources::Init();

    if (KnstWindowSources::active_backend() == knst_backend::none) {
        std::cerr << "Failed to initialize the platform layer.\n";
        return 1;
    }

    // STEP 2 — shared GL context config.
    // Holds the EGL display and pixel format — no per-window surface
    // yet, that comes in STEP 4.
    knst_opengl_global::Options gl_options;
    gl_options.gl_major     = 3;
    gl_options.gl_minor     = 3;
    gl_options.core_profile = true;

    if (!knst_opengl_global::Init(gl_options)) {
        std::cerr << "Failed to initialize the OpenGL global state.\n";
        KnstWindowSources::CleanUp();
        return 1;
    }

    // STEP 3 — create the window but keep it hidden.
    // creation()  builds the native window, unmapped
    // show()      maps it, making it visible
    //
    // By deferring show() until after the first frame is drawn, compositors
    // that fade windows in never show an empty/black flash.
    knst_window window(800, 600, u"Knst OpenGL Basic");
    window.set_redraw_callback(on_frame);
    window.creation();

    // STEP 4 — attach a GL context to this window.
    knst_window_opengl_content gl;
    if (!gl.Init(&window)) {
        std::cerr << "Failed to create the OpenGL context.\n";
        knst_opengl_global::CleanUp();
        KnstWindowSources::CleanUp();
        return 1;
    }
    g_gl = &gl;

    // STEP 5 — load GL function pointers with GLAD.
    // Must run AFTER MakeCurrent() — the loader needs an active context
    // to query the driver for function addresses.
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

    // glad's #defines route every gl* call to the loaded symbols from
    // here on.
    std::cout << "GPU        : " << gl.GetRendererString()    << "\n";
    std::cout << "GL version : " << gl.GetVersionString()     << "\n";
    std::cout << "GLSL       : " << gl.GetGLSLVersionString() << "\n\n";

    // STEP 6 — build the render pipeline.
    g_program = build_shader_program();
    setup_triangle();

    // STEP 7 — draw the first frame BEFORE showing the window.
    // This is the trick that avoids the "flash of uninitialized content"
    // on compositors that animate window mapping.
    render_frame();
    gl.SwapBuffers();

    // STEP 8 — now (and only now) make the window visible.
    window.show();

    // STEP 9 — main loop: poll → render → yield.
    // yield() lets the OS schedule other threads while keeping the loop
    // responsive and resize smooth.
    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        window.call_redraw_callback();
        std::this_thread::yield();
    }

    std::cout << "Closing...\n";

    // STEP 10 — teardown in reverse order:
    // GL resources → GL context → global GL → window → platform.
    gl.MakeCurrent();

    if (g_vbo) { glDeleteBuffers(1, &g_vbo); g_vbo = 0;     }
    if (g_vao) { glDeleteVertexArrays(1, &g_vao); g_vao = 0;     }
    if (g_program) { glDeleteProgram(g_program); g_program = 0; }

    g_gl = nullptr;

    gl.Shutdown();
    knst_opengl_global::CleanUp();
    window.destroy();
    KnstWindowSources::CleanUp();

    return 0;
}