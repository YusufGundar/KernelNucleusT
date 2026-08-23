//------Example------
#define KNST_USING_PLATFORM_ANDROID
#define KNST_PLATFORM_ANDROID_OPENGL
//-----_------_------


#include "../../../include/KernelNucleusT.hpp"

static const char* vertexShaderSource = R"(
    #version 300 es
    layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec3 aColor;
    out vec3 vColor;
    void main() {
        gl_Position = vec4(aPos, 1.0);
        vColor = aColor;
    }
)";

static const char* fragmentShaderSource = R"(
    #version 300 es
    precision mediump float;
    in vec3 vColor;
    out vec4 FragColor;
    void main() {
        FragColor = vec4(vColor, 1.0);
    }
)";

static GLuint CompileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
    }
    return shader;
}

static GLuint CreateShaderProgram(const char* vertex, const char* fragment) {
    GLuint v = CompileShader(GL_VERTEX_SHADER, vertex);
    GLuint f = CompileShader(GL_FRAGMENT_SHADER, fragment);
    
    GLuint program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
    }
    
    glDeleteShader(v);
    glDeleteShader(f);
    return program;
}

struct RenderState {
    knst_window_opengl_content* content;
    GLuint shaderProgram;
    GLuint VAO, VBO;
    
    bool keyW, keyA, keyS, keyD;
    float posX, posY;
    std::chrono::steady_clock::time_point lastFrameTime;
};

static void render_frame(knst_window& window, void* user_data) {
    RenderState* rs = static_cast<RenderState*>(user_data);
    
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - rs->lastFrameTime).count();
    rs->lastFrameTime = now;
    dt = std::min(dt, 0.05f);
    
    const float MOVE_SPEED = 2.0f;
    
    if (rs->keyW) rs->posY += MOVE_SPEED * dt;
    if (rs->keyS) rs->posY -= MOVE_SPEED * dt;
    if (rs->keyA) rs->posX -= MOVE_SPEED * dt;
    if (rs->keyD) rs->posX += MOVE_SPEED * dt;
    
    rs->content->MakeCurrent();
    glClearColor(0.2f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glUseProgram(rs->shaderProgram);
    glBindVertexArray(rs->VAO);
    
    GLint posLoc = glGetUniformLocation(rs->shaderProgram, "uPos");
    if (posLoc != -1) {
        glUniform2f(posLoc, rs->posX, rs->posY);
    }
    
    glDrawArrays(GL_TRIANGLES, 0, 3);
    
    rs->content->SwapBuffers();
}

void android_main(struct android_app* app) {
    KnstWindowSources::Init(app);

    for (size_t i = 0; i < knst_display::get_monitor_list().size(); i++) {
        const auto& mon = knst_display::get_monitor_list()[i];
        knst_byte_string utf8_name(mon.name);
        KNST_LOG_INFO("--- Monitor %zu ---", i + 1);
        KNST_LOG_INFO("  Name: %s", reinterpret_cast<const char*>(utf8_name.data()));
        KNST_LOG_INFO("  Primary: %s", mon.is_primary ? "Yes" : "No");
        KNST_LOG_INFO("  Position: (%d, %d)", mon.root_x, mon.root_y);
        KNST_LOG_INFO("  Resolution: %dx%d", mon.width, mon.height);
        KNST_LOG_INFO("  Physical size: %dx%d mm", mon.physical_width, mon.physical_height);
        KNST_LOG_INFO("  Refresh rate: %.1f Hz", mon.refresh_rate);
        KNST_LOG_INFO("  DPI: %.1f", mon.dpi_scale);
    }
    
    knst_window window;
    window.creation_and_show();

    knst_window_opengl_content content;
    if (!content.Init(&window, false)) {
        return;
    }
    
    const char* vertexShaderSourceWithUniform = R"(
        #version 300 es
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aColor;
        out vec3 vColor;
        uniform vec2 uPos;
        void main() {
            gl_Position = vec4(aPos.x + uPos.x, aPos.y + uPos.y, aPos.z, 1.0);
            vColor = aColor;
        }
    )";
    
    const char* fragmentShaderSourceES = R"(
        #version 300 es
        precision mediump float;
        in vec3 vColor;
        out vec4 FragColor;
        void main() {
            FragColor = vec4(vColor, 1.0);
        }
    )";
    
    GLuint shaderProgram = CreateShaderProgram(vertexShaderSourceWithUniform, fragmentShaderSourceES);
    
    float vertices[] = {
        -0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,
         0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f
    };
    
    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    RenderState rs;
    rs.content = &content;
    rs.shaderProgram = shaderProgram;
    rs.VAO = VAO;
    rs.VBO = VBO;
    rs.keyW = false;
    rs.keyA = false;
    rs.keyS = false;
    rs.keyD = false;
    rs.posX = 0.0f;
    rs.posY = 0.0f;
    rs.lastFrameTime = std::chrono::steady_clock::now();
    
    window.set_user_data(&rs);
    window.set_redraw_callback(render_frame);

    auto reinit_gl = [&]() {
        if (content.Init(&window, true)) {
            shaderProgram = CreateShaderProgram(vertexShaderSourceWithUniform, fragmentShaderSourceES);
            
            glGenVertexArrays(1, &VAO);
            glGenBuffers(1, &VBO);
            
            glBindVertexArray(VAO);
            glBindBuffer(GL_ARRAY_BUFFER, VBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
            
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            
            rs.content = &content;
            rs.shaderProgram = shaderProgram;
            rs.VAO = VAO;
            rs.VBO = VBO;
            rs.lastFrameTime = std::chrono::steady_clock::now();
            window.set_user_data(&rs);
            
            KNST_LOG_INFO("App resumed, OpenGL reinitialized");
        }
    };

    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        
        window.call_redraw_callback();
        
        for (size_t i = 0; i < window.get_touch_event_count(); i++) {
            const auto& t = window.get_touch_event(i);
            
            if (t.touch_action == KNST_MOBILE_TOUCH_ACTION_PRESS) {
                KNST_LOG_INFO("Touch Press triggered");
            }
            else if (t.touch_action == KNST_MOBILE_TOUCH_ACTION_RELEASE) {
                KNST_LOG_INFO("Touch Release triggered");
            }
        }
        
        const auto& lifecycle = window.get_lifecycle_event();
        
        if (lifecycle.type == KNST_SAVE_STATE) {
            KNST_LOG_INFO("Save State");
        }
        else if (lifecycle.type == KNST_CONTENT_RECT_CHANGED) {
            if (knst_mobile_keyboard::is_visible()) {
                if (lifecycle.window_height > 0 &&
                    lifecycle.content_bottom - lifecycle.content_top >= lifecycle.window_height * 0.7f) {
                    knst_mobile_keyboard::hide();
                    KNST_LOG_INFO("The back button was pressed while the keyboard was open.");
                }
            }
        }
        else if (lifecycle.type == KNST_WINDOW_LOST || lifecycle.type == KNST_APP_STOPPED) {
            content.Shutdown();
            if (knst_mobile_keyboard::is_visible()) {
                knst_mobile_keyboard::hide();
            }
            KNST_LOG_INFO("App went to background, OpenGL closed");
        }
        else if (lifecycle.type == KNST_APP_RESUMED) {
            reinit_gl();
        }
        else if (lifecycle.type == KNST_CONFIG_CHANGED) {
            content.Shutdown();
            if (knst_mobile_keyboard::is_visible()) {
                knst_mobile_keyboard::hide();
            }
            reinit_gl();
        }
        
        const auto& misc = window.get_misc_event();
        if (misc.type == KNST_CLOSE_WINDOW || misc.type == KNST_DISCONNECT) {
            content.Shutdown();
            window.destroy();
            window.should_close();
            KNST_LOG_INFO("Closing App");
        }
        
        const auto& expose = window.get_expose_event();
        if (expose.type == KNST_EXPOSE) {
            reinit_gl();
        }
        
        for (size_t i = 0; i < window.get_keyboard_event_count(); i++) {
            const auto& handle = window.get_keyboard_event(i);
            
            bool isDown = (handle.key_action == KNST_KEY_PRESS);
            bool isUp = (handle.key_action == KNST_KEY_RELEASE);
            
            switch (handle.type) {
                case KNST_MOBILE_VOLUME_UP:
                    if (handle.key_action == KNST_KEY_PRESS) {
                        knst_mobile_keyboard::toggle();
                        KNST_LOG_INFO("Keyboard toggled");
                    }
                    break;
                    
                case KNST_MOBILE_VOLUME_DOWN:
                    if (handle.key_action == KNST_KEY_PRESS) {
                        knst_mobile_keyboard::hide();
                        KNST_LOG_INFO("Keyboard disabled");
                    }
                    break;
                    
                case KNST_MOBILE_BACK_PRESS:
                    if (handle.key_action == KNST_KEY_PRESS) {
                        KNST_LOG_INFO("Back button pressed");
                    }
                    break;
                    
                case KNST_KEYBOARD_EVENT:
                default:
                    if (handle.key_action == KNST_KEY_PRESS && handle.key_code == KNST_KEY_C) {
                        window.set_clipboard(u"What's up Boss");
                    }
                    else if (handle.key_action == KNST_KEY_PRESS && handle.key_code == KNST_KEY_V) {
                        window.request_clipboard();
                        knst_byte_string utf8_copied_text(window.get_clipboard());
                        KNST_LOG_INFO("Copied Text: %s", reinterpret_cast<const char*>(utf8_copied_text.data()));
                    }
                    else if (handle.key_action == KNST_KEY_PRESS && handle.key_code == KNST_KEY_H) {
                        window.set_opacity(0.2f);
                    }
                    else if (isDown || isUp) {
                        if (handle.key_code == KNST_KEY_W) {
                            rs.keyW = isDown;
                            if (isDown) KNST_LOG_INFO("W pressed");
                            else KNST_LOG_INFO("W released");
                        }
                        else if (handle.key_code == KNST_KEY_A) {
                            rs.keyA = isDown;
                            if (isDown) KNST_LOG_INFO("A pressed");
                            else KNST_LOG_INFO("A released");
                        }
                        else if (handle.key_code == KNST_KEY_S) {
                            rs.keyS = isDown;
                            if (isDown) KNST_LOG_INFO("S pressed");
                            else KNST_LOG_INFO("S released");
                        }
                        else if (handle.key_code == KNST_KEY_D) {
                            rs.keyD = isDown;
                            if (isDown) KNST_LOG_INFO("D pressed");
                            else KNST_LOG_INFO("D released");
                        }
                    }
                    break;
            }
        }
        
        if (window.is_should_close()) {
            break;
        }
        
        window.clear_temporary_events();
    }
    
    glDeleteProgram(shaderProgram);
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    
    KnstWindowSources::CleanUp();
    KNST_LOG_INFO("Cleaned Manually");
}