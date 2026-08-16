

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
};

static void render_frame(knst_window& window, void* user_data) {
    RenderState* rs = static_cast<RenderState*>(user_data);
    
    rs->content->MakeCurrent();
    glClearColor(0.2f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glUseProgram(rs->shaderProgram);
    glBindVertexArray(rs->VAO);
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
    if (!content.Init(&window, false)) { // vsync false
        return;
    }
    
   
    GLuint shaderProgram = CreateShaderProgram(vertexShaderSource, fragmentShaderSource);
    
  
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
    
    window.set_user_data(&rs);
    window.set_redraw_callback(render_frame);
   
    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        
        window.call_redraw_callback();
        const auto& handle = window.get_window_event_handle();
        

        if(handle.type == KNST_MOBILE_TOUCH_EVENT){ 

            if(handle.touch_action == KNST_MOBILE_TOUCH_ACTION_PRESS){
                KNST_LOG_INFO("Touch Press triggered");
            }
            else if(handle.touch_action == KNST_MOBILE_TOUCH_ACTION_RELEASE){
                KNST_LOG_INFO("Touch Release triggered");
            }

        }
        else if(handle.type == KNST_MOBILE_VOLUME_UP){
            knst_mobile_keyboard::toggle();
            KNST_LOG_INFO("Keyboard enabled");
        }
        else if(handle.type == KNST_MOBILE_VOLUME_DOWN){
            knst_mobile_keyboard::hide();
            KNST_LOG_INFO("Keyboard disabled");
        }
        else if(handle.type == KNST_CLOSE_WINDOW || handle.type == KNST_DISCONNECT){ // This is not triggered when the operating system forcibly terminates it.
            content.Shutdown();
            window.destroy();
            window.should_close();
            KNST_LOG_INFO("Closing App");
            break;
        }
        else if (handle.type == KNST_SAVE_STATE) { // This event always fires; perform the necessary operations here when closing the application.
            //Perform the necessary operations here—such as writing to a file or other critical tasks; if the OS kills the current application, this event will be the final one received.
            // It would be beneficial to fill this section out as well; I just wanted to let you know.
            KNST_LOG_INFO("Save State");
        }
        else if (handle.type == KNST_CONTENT_RECT_CHANGED) { 
            // It detects changes occurring on the screen—for example, when you open or close the keyboard.
            // If the back button is pressed while the keyboard is open, this event is triggered; we need to reset the keyboard state, so we manually handle the hiding process here using `hide`.
            if (knst_mobile_keyboard::is_visible()) {
                if (handle.window_height > 0 && handle.content_bottom - handle.content_top >= handle.window_height * 0.7f) {
                  
                    knst_mobile_keyboard::hide();
                    KNST_LOG_INFO("The back button was pressed while the keyboard was open.");
                    
                }
            }
        }
        else if (handle.type == KNST_KEYBOARD_EVENT) { 
           

            if(handle.key_code == KNST_KEY_W){
                KNST_LOG_INFO(".The 'W' key was triggered.");
            }
            else if(handle.key_code == KNST_KEY_A){
                KNST_LOG_INFO(".The 'A' key was triggered.");
            }
            else if(handle.key_code == KNST_KEY_D){
                KNST_LOG_INFO(".The 'D' key was triggered.");
            }
            else if(handle.key_code == KNST_KEY_S){
                KNST_LOG_INFO(".The 'S' key was triggered.");
            }
            else if(handle.key_code == KNST_KEY_S){
                KNST_LOG_INFO(".The 'S' key was triggered.");
            }
            else if(handle.key_code == KNST_KEY_C){
                
                window.set_clipboard(u"What's up Boss");
            }
            else if(handle.key_code == KNST_KEY_V){
                
                
                window.request_clipboard();
                knst_byte_string utf8_copied_text(window.get_clipboard());
               
                KNST_LOG_INFO("Copied Text: %s", reinterpret_cast<const char*>(utf8_copied_text.data()));
            }
            else if(handle.key_code == KNST_KEY_H){
                
                
               window.set_opacity(0.2f);
            }
            

        }
        else if (handle.type == KNST_WINDOW_LOST || handle.type == KNST_APP_STOPPED) {
            // We are clearing the resources when the application is moved to the background.
            content.Shutdown();
            if (knst_mobile_keyboard::is_visible()) {
                knst_mobile_keyboard::hide();
            }
            KNST_LOG_INFO("App went to background, OpenGL closed");
        }
        else if (handle.type == KNST_EXPOSE || handle.type == KNST_APP_RESUMED) {
            
            
                if (content.Init(&window, true)) {
                    shaderProgram = CreateShaderProgram(vertexShaderSource, fragmentShaderSource);
                    
                    glGenVertexArrays(1, &VAO);
                    glGenBuffers(1, &VBO);
                    
                    glBindVertexArray(VAO);
                    glBindBuffer(GL_ARRAY_BUFFER, VBO);
                    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
                    
                    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
                    glEnableVertexAttribArray(1);
                    
                    // We are updating the render state.
                    rs.content = &content;
                    rs.shaderProgram = shaderProgram;
                    rs.VAO = VAO;
                    rs.VBO = VBO;
                    window.set_user_data(&rs);
                    
                    KNST_LOG_INFO("App resumed, OpenGL reinitialized");
                }
            
        }
        else if (handle.type == KNST_CONFIG_CHANGED) {
            
            content.Shutdown();
            if (knst_mobile_keyboard::is_visible()) {
                knst_mobile_keyboard::hide();
            }
            if (content.Init(&window, true)) {
                    shaderProgram = CreateShaderProgram(vertexShaderSource, fragmentShaderSource);
                    
                    glGenVertexArrays(1, &VAO);
                    glGenBuffers(1, &VBO);
                    
                    glBindVertexArray(VAO);
                    glBindBuffer(GL_ARRAY_BUFFER, VBO);
                    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
                    
                    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
                    glEnableVertexAttribArray(1);
                    
                    // We are updating the render state.
                    rs.content = &content;
                    rs.shaderProgram = shaderProgram;
                    rs.VAO = VAO;
                    rs.VBO = VBO;
                    window.set_user_data(&rs);
                    
                  
            }
              KNST_LOG_INFO("App resumed, OpenGL reinitialized");

        }
        
        window.clear_temporary_events();
        
    }
    
    
    glDeleteProgram(shaderProgram);
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    
    
    KnstWindowSources::CleanUp();
    KNST_LOG_INFO("Cleaned Manually");
}