//This is a simple example of drawing a triangle using `knst window` and OpenGL; I recommend that you follow this usage pattern.

#include <iostream>
#include <chrono>

// Windows automatic detection
//#define KNST_LINUX_PLATFORM_WAYLAND     If you are using Linux Wayland
//#define KNST_LINUX_PLATFORM_X11    If you are using Linux X11

//#define KNST_USING_VULKAN    If you're going to use Vulkan
//#define KNST_USING_OPENGL     If you're going to use Opengl
// |
// |
//  -------->   #define KNST_OPENGL_USING_EGL     If you are using Linux X11, you need to specify that additionally.
//  -------->   #define KNST_OPENGL_USING_GLX     If you are using Linux X11, you need to specify that additionally.



#include "../../glad_3_3/include/glad/glad.h" // glad path
#include "../../include/KernelNucleusT.hpp"




const char* vertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec3 aColor;
    out vec3 vColor;
    void main() {
        gl_Position = vec4(aPos, 1.0);
        vColor = aColor;
    }
)";

const char* fragmentShaderSource = R"(
    #version 330 core
    in vec3 vColor;
    out vec4 FragColor;
    void main() {
        FragColor = vec4(vColor, 1.0);
    }
)";


GLuint CompileShader(GLenum type, const char* source) {
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

GLuint CreateShaderProgram(const char* vertex, const char* fragment) {
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


void render_frame(knst_window& window, void* user_data) {
    RenderState* rs = static_cast<RenderState*>(user_data);
    
    rs->content->BeginFrame();
    
    
    glClearColor(0.2f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
   
    glUseProgram(rs->shaderProgram);
    glBindVertexArray(rs->VAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    
    rs->content->SwapBuffers();
}

int main() {
    
    
    KnstWindowSources::Init();


    for (size_t i = 0; i < knst_display::get_monitor_list().size(); i++) {
        const auto& mon = knst_display::get_monitor_list()[i];
        std::cout << "\n--- Monitor " << (i + 1) << " ---" << std::endl;
        std::cout << "Name: " << mon.name << std::endl;
        std::cout << "Primary: " << (mon.is_primary ? "Yes" : "No") << std::endl;
        std::cout << "Position: (" << mon.root_x << ", " << mon.root_y << ")" << std::endl;
        std::cout << "Resolution: " << mon.width << "x" << mon.height << std::endl;
        std::cout << "Physical size: " << mon.physical_width << "x" << mon.physical_height << " mm" << std::endl;
        std::cout << "Refresh rate: " << mon.refresh_rate << " Hz" << std::endl;
        std::cout << "DPI: " << mon.dpi_scale << std::endl;
    }

   
    knst_window window(800, 600, u"Trangle Test");
    window.creation();
    

    window.set_drag_drop_status(true);
    
    //int width,height; //  image (bmp) loader example for change cursor
    //knst_byte_string bmp_data = knst_image_loader::load_bmp("/home/knst_user/Desktop/KernelNucleusT/icon_example/cpp_logo.bmp",&width,&height,KNST_BITMAP_64_64 | KNST_BITMAP_OUTPUT_RGBA);
    //window.set_bmp_cursor(bmp_data,width,height);

    window.show();
    
    knst_window_opengl_content content;
    if (!content.Init(&window, false)) {
        std::cout << "OpenGL init failed" << std::endl;
        return -1;
    }
    
    if (!gladLoadGLLoader((GLADloadproc)knst_window_opengl_content::GLADLoaderWrapper)) {
        std::cout << "GLAD init failed" << std::endl;
        return -1;
    }
    
    std::cout << "Opengl Version :" << glGetString(GL_VERSION) <<":"<< std::endl;
    
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
        
        const auto& handle = window.get_window_event_handle();
        




        if(handle.type == KNST_KEYBOARD_EVENT){

            if(handle.key_action == KNST_KEY_PRESS && handle.key_code == KNST_KEY_C && (handle.mods & KNST_MOD_CONTROL)) {
                window.set_clipboard(u"What's up Boss");
            }
            else if(handle.key_action == KNST_KEY_PRESS && handle.key_code == KNST_KEY_V && (handle.mods & KNST_MOD_CONTROL)) {
                window.request_clipboard();
                std::cout << "copied text : " << window.get_clipboard() << std::endl;
            }
            else if(handle.key_code == KNST_KEY_W){
                std::cout << ".The 'W' key was triggered." << std::endl;
            }
            else if (handle.key_code == KNST_KEY_A){
                std::cout << ".The 'A' key was triggered." << std::endl;
            }
            else if (handle.key_code == KNST_KEY_D){
                std::cout << ".The 'D' key was triggered." << std::endl;
            }
            else if (handle.key_code == KNST_KEY_S){
                std::cout << ".The 'S' key was triggered." << std::endl;
            }
            else if (handle.key_code == KNST_KEY_SPACE){
                std::cout << ".The 'SPACE' key was triggered." << std::endl;
            }

        }
        else if(handle.type == KNST_MOUSE_EVENT){

            if(handle.mouse_button == KNST_MOUSE_BUTTON_LEFT){
                std::cout << ".'Left' mouse button triggered."<< std::endl;
                std::cout << "mouse x :" << handle.mouse_x << " mouse y:" << handle.mouse_y << std::endl;
            }
            else if(handle.mouse_button == KNST_MOUSE_BUTTON_RIGHT) {
                std::cout << ".'Right' mouse button triggered."<< std::endl;
                std::cout << "mouse x :" << handle.mouse_x << " mouse y:" << handle.mouse_y << std::endl;
            }

        }
        else if(handle.type == KNST_CLOSE_WINDOW || handle.type == KNST_DISCONNECT){
            content.Shutdown();
            window.destroy();
            window.should_close();
        }
        else if(handle.type == KNST_FILE_DROP){
            std::cout << "file drop"<<std::endl;
            for(auto file : window.get_window_event_handle().drop_files){
                std::cout << file << std::endl;
            }
        }
        else if(handle.type == KNST_FILE_DROP_LEAVE){
            std::cout << "file leave"<<std::endl;
        }
        

        window.clear_temporary_events();
    }
    
   
    glDeleteProgram(shaderProgram);
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    
    
    KnstWindowSources::CleanUp();
    
    std::cout << "Cleaning all sources..." << std::endl;
    return 0;
}
