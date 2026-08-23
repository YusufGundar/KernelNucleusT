#include <iostream>
#include <chrono>
#include <glm/glm.hpp> // include glm
#include <glm/gtc/matrix_transform.hpp> // include glm

#include "../../include/KernelNucleusT.hpp"


KnstDrawConfig CreateGround() {
    KnstDrawConfig ground;
    float size = 5.0f;

    ground.AddVertex3DUVN(-size, -0.5f, -size,  0,1,0,  0,0,  0.3f, 0.3f, 0.3f);
    ground.AddVertex3DUVN( size, -0.5f, -size,  0,1,0,  1,0,  0.3f, 0.3f, 0.3f);
    ground.AddVertex3DUVN( size, -0.5f,  size,  0,1,0,  1,1,  0.3f, 0.3f, 0.3f);
    ground.AddVertex3DUVN(-size, -0.5f,  size,  0,1,0,  0,1,  0.3f, 0.3f, 0.3f);

    ground.AddIndices({0,1,2, 0,2,3});
    ground.SetCoordSystemNDCVulkan();
    ground.UsePerspective3D(45.0f, 0.1f, 100.0f);
    ground.SetPosition(0.0f, 0.0f, 0.0f);
    ground.SetColor(1.0f, 1.0f, 1.0f, 1.0f);
    ground.SyncPushConstants();

    return ground;
}

KnstDrawConfig CreateSkybox(int width, int height) {
    KnstDrawConfig sky;
    sky.Set2D();
    sky.UsePixelSpace2D();
    sky.SetZLayer(0.0f);

    sky.AddVertex2D(0, 0, 0.1f, 0.2f, 0.5f);
    sky.AddVertex2D(width, 0, 0.1f, 0.2f, 0.5f);
    sky.AddVertex2D(width, height, 1.0f, 0.5f, 0.1f);
    sky.AddVertex2D(0, height, 1.0f, 0.5f, 0.1f);
    sky.AddIndices({0,1,2, 0,2,3});
    sky.SyncPushConstants();

    return sky;
}

KnstDrawConfig CreateCube() {
    KnstDrawConfig cube;
    float size = 0.5f;

    cube.AddVertex3DUVN(-size, -size,  size,  0,0,1,  0,0,  1.0f, 0.2f, 0.2f);
    cube.AddVertex3DUVN( size, -size,  size,  0,0,1,  1,0,  1.0f, 0.2f, 0.2f);
    cube.AddVertex3DUVN( size,  size,  size,  0,0,1,  1,1,  1.0f, 0.2f, 0.2f);
    cube.AddVertex3DUVN(-size,  size,  size,  0,0,1,  0,1,  1.0f, 0.2f, 0.2f);

    cube.AddVertex3DUVN(-size, -size, -size,  0,0,-1,  0,0,  0.8f, 0.2f, 1.0f);
    cube.AddVertex3DUVN(-size,  size, -size,  0,0,-1,  1,0,  0.8f, 0.2f, 1.0f);
    cube.AddVertex3DUVN( size,  size, -size,  0,0,-1,  1,1,  0.8f, 0.2f, 1.0f);
    cube.AddVertex3DUVN( size, -size, -size,  0,0,-1,  0,1,  0.8f, 0.2f, 1.0f);

    cube.AddVertex3DUVN(-size, -size, -size, -1,0,0,  0,0,  1.0f, 0.8f, 0.2f);
    cube.AddVertex3DUVN(-size, -size,  size, -1,0,0,  1,0,  1.0f, 0.8f, 0.2f);
    cube.AddVertex3DUVN(-size,  size,  size, -1,0,0,  1,1,  1.0f, 0.8f, 0.2f);
    cube.AddVertex3DUVN(-size,  size, -size, -1,0,0,  0,1,  1.0f, 0.8f, 0.2f);

    cube.AddVertex3DUVN( size, -size, -size,  1,0,0,  0,0,  0.2f, 1.0f, 0.2f);
    cube.AddVertex3DUVN( size,  size, -size,  1,0,0,  1,0,  0.2f, 1.0f, 0.2f);
    cube.AddVertex3DUVN( size,  size,  size,  1,0,0,  1,1,  0.2f, 1.0f, 0.2f);
    cube.AddVertex3DUVN( size, -size,  size,  1,0,0,  0,1,  0.2f, 1.0f, 0.2f);

    cube.AddVertex3DUVN(-size,  size, -size,  0,1,0,  0,0,  0.2f, 0.4f, 1.0f);
    cube.AddVertex3DUVN(-size,  size,  size,  0,1,0,  1,0,  0.2f, 0.4f, 1.0f);
    cube.AddVertex3DUVN( size,  size,  size,  0,1,0,  1,1,  0.2f, 0.4f, 1.0f);
    cube.AddVertex3DUVN( size,  size, -size,  0,1,0,  0,1,  0.2f, 0.4f, 1.0f);

    cube.AddVertex3DUVN(-size, -size, -size,  0,-1,0,  0,0,  0.0f, 1.0f, 1.0f);
    cube.AddVertex3DUVN( size, -size, -size,  0,-1,0,  1,0,  0.0f, 1.0f, 1.0f);
    cube.AddVertex3DUVN( size, -size,  size,  0,-1,0,  1,1,  0.0f, 1.0f, 1.0f);
    cube.AddVertex3DUVN(-size, -size,  size,  0,-1,0,  0,1,  0.0f, 1.0f, 1.0f);

    cube.AddIndices({
        0,1,2, 0,2,3,
        4,5,6, 4,6,7,
        8,9,10, 8,10,11,
        12,13,14, 12,14,15,
        16,17,18, 16,18,19,
        20,21,22, 20,22,23
    });

    cube.SetCoordSystemNDCVulkan();
    cube.UsePerspective3D(45.0f, 0.1f, 100.0f);
    cube.SetPosition(0.0f, 0.5f, 0.0f);
    cube.SetColor(1.0f, 1.0f, 1.0f, 1.0f);
    cube.SetCamera(0.0f, 0.5f, 3.5f);
    cube.SetCameraTarget(0.0f, 0.0f, 0.0f);
    cube.SetLightPosition(3.0f, 5.0f, 2.0f);
    cube.SetLightIntensity(2.0f);
    cube.SetLightColor(1.0f, 0.9f, 0.7f);
    cube.SyncPushConstants();

    return cube;
}

KnstDrawConfig CreateButton(float x, float y, float w, float h, float r, float g, float b) {
    KnstDrawConfig btn;
    btn.Set2D();
    btn.UsePixelSpace2D();
    btn.SetZLayer(0.8f);

    btn.AddVertex2D(x, y, r*0.8f, g*0.8f, b*0.8f);
    btn.AddVertex2D(x+w, y, r*0.8f, g*0.8f, b*0.8f);
    btn.AddVertex2D(x+w, y+h, r, g, b);
    btn.AddVertex2D(x, y+h, r, g, b);
    btn.AddIndices({0,1,2, 0,2,3});
    btn.SyncPushConstants();

    return btn;
}

KnstDrawConfig CreateHUD(int width) {
    KnstDrawConfig hud;
    hud.Set2D();
    hud.UsePixelSpace2D();
    hud.SetZLayer(0.7f);

    hud.AddVertex2D(0, 0, 0.0f, 0.0f, 0.0f, 0.5f);
    hud.AddVertex2D(width, 0, 0.0f, 0.0f, 0.0f, 0.5f);
    hud.AddVertex2D(width, 50, 0.0f, 0.0f, 0.0f, 0.5f);
    hud.AddVertex2D(0, 50, 0.0f, 0.0f, 0.0f, 0.5f);
    hud.AddIndices({0,1,2, 0,2,3});
    hud.SyncPushConstants();

    return hud;
}


struct KeyboardState {
    bool held_W = false;
    bool held_A = false;
    bool held_S = false;
    bool held_D = false;
};


struct VulkanRenderState {
    knst_window_vulkan_content* vk_content;
    knst_gui_framework* gui;
    KnstSwapchainConfig* swapConfig;

    KnstDrawConfig* skybox;
    KnstDrawConfig* ground;
    KnstDrawConfig* cube;
    KnstDrawConfig* mesh;
    KnstDrawConfig* hud;
    KnstDrawConfig* button1;
    KnstDrawConfig* button2;
    KnstDrawConfig* button3;

    float* rota;
    float* rotb;
    float* rotc;

    KeyboardState* keys;

    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastFrameTime;

    static constexpr float ROTATION_SPEED = 2.0f;
};


void vulkan_render_frame(knst_window& window, void* user_data) {
    VulkanRenderState* rs = static_cast<VulkanRenderState*>(user_data);

    int w = window.get_window_event_handle().window_width;
    int h = window.get_window_event_handle().window_height;

    if (w <= 0 || h <= 0) return;

    rs->swapConfig->width = w;
    rs->swapConfig->height = h;

    auto now = std::chrono::steady_clock::now();
    float time = std::chrono::duration<float>(now - rs->startTime).count();

    float dt = std::chrono::duration<float>(now - rs->lastFrameTime).count();
    rs->lastFrameTime = now;

    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.0f) dt = 0.0f;

    const KeyboardState& keys = *rs->keys;
    bool meshRotationChanged = false;

    if (keys.held_W) { *rs->rota += VulkanRenderState::ROTATION_SPEED * dt; meshRotationChanged = true; }
    if (keys.held_S) { *rs->rota -= VulkanRenderState::ROTATION_SPEED * dt; meshRotationChanged = true; }
    if (keys.held_D) { *rs->rotb += VulkanRenderState::ROTATION_SPEED * dt; meshRotationChanged = true; }
    if (keys.held_A) { *rs->rotb -= VulkanRenderState::ROTATION_SPEED * dt; meshRotationChanged = true; }

    if (meshRotationChanged) {
        rs->mesh->SetRotation(*rs->rota, *rs->rotb, *rs->rotc);
    }

    rs->cube->SetRotation(time * 0.7f, time * 0.9f, time * 0.5f);
    rs->cube->SetPosition(0.0f, 0.5f + sin(time * 0.5f) * 0.3f, 0.0f);
    rs->cube->SetLightPosition(
        3.0f * cos(time * 0.3f),
        5.0f + sin(time * 0.5f) * 2.0f,
        3.0f * sin(time * 0.3f)
    );
    rs->cube->SyncPushConstants();

    rs->skybox->SyncPushConstants();
    rs->ground->SyncPushConstants();
    rs->mesh->SyncPushConstants();
    rs->button1->SyncPushConstants();
    rs->button2->SyncPushConstants();
    rs->button3->SyncPushConstants();
    rs->hud->SyncPushConstants();

    rs->gui->BeginFrame(*rs->swapConfig, KnstClearColor::Dark());
    rs->gui->Draw(*rs->skybox);
    rs->gui->Draw(*rs->ground);
    rs->gui->Draw(*rs->mesh);
    rs->gui->Draw(*rs->hud);
    rs->gui->Draw(*rs->button1);
    rs->gui->Draw(*rs->button2);
    rs->gui->Draw(*rs->button3);
    rs->gui->EndFrame();
}


int main() {
    KnstWindowSources::Init();

    int winWidth = 1280;
    int winHeight = 720;

    knst_window window(winWidth, winHeight, "3D Ortam + 2D UI");
    window.creation();

    knst_window_vulkan_content vk_content;
    vk_content.Init(window);

    knst_gui_framework gui;
    gui.Init(&vk_content);

    KnstSwapchainConfig swapConfig = KnstSwapchainConfig::Default();
    swapConfig.width = winWidth;
    swapConfig.height = winHeight;

    KnstGuiConfig guiConfig = KnstGuiConfig::Default(
        "/home/knst_tester/Desktop/KernelNucleusT/include/platform/knst_gui_framework/shader/spv/vert.spv",
        "/home/knst_tester/Desktop/KernelNucleusT/include/platform/knst_gui_framework/shader/spv/frag.spv"
    );

    gui.SetKnstGuiConfig(swapConfig, guiConfig);

    KnstDrawConfig skybox = CreateSkybox(winWidth, winHeight);
    KnstDrawConfig ground = CreateGround();
    KnstDrawConfig cube = CreateCube();
    KnstDrawConfig button1 = CreateButton(50, 70, 200, 50, 0.2f, 0.4f, 0.8f);
    KnstDrawConfig button2 = CreateButton(280, 70, 200, 50, 0.2f, 0.8f, 0.4f);
    KnstDrawConfig button3 = CreateButton(510, 70, 200, 50, 0.8f, 0.2f, 0.4f);
    KnstDrawConfig hud = CreateHUD(winWidth);

    knst_texture laptop_texture = knst_texture_loader::LoadFromFile(
        gui.GetDevice(), gui.GetPhysicalDevice(), gui.GetCommandPool(), gui.GetGraphicsQueue(),
        "/home/knst_tester/Desktop/KernelNucleusT/Models/laptop_texture.png"
    );

    knst_obj_loader::MeshData laptop_data;
    knst_obj_loader::Load("/home/knst_tester/Desktop/KernelNucleusT/Models/laptop.obj", laptop_data);
    KnstDrawConfig mesh;
    mesh.vertices3D = laptop_data.vertices;
    mesh.indices = laptop_data.indices;
    mesh.SetTexture(&laptop_texture);
    mesh.UsePerspective3D(45.0f, 0.1f, 100.0f);
    mesh.SetCamera(0.0f, 0.0f, 3.0f);
    mesh.SetLightPosition(5.0f, 5.0f, 5.0f);
    mesh.SetLightIntensity(1.5f);
    mesh.frontFaceCW = true;
    mesh.pushData.useUV = 1.0f;
    mesh.SyncPushConstants();

    float rota = 0.0f, rotb = 0.0f, rotc = 0.0f;

    KeyboardState keys;

    VulkanRenderState rs;
    rs.vk_content = &vk_content;
    rs.gui = &gui;
    rs.swapConfig = &swapConfig;
    rs.skybox = &skybox;
    rs.ground = &ground;
    rs.cube = &cube;
    rs.mesh = &mesh;
    rs.hud = &hud;
    rs.button1 = &button1;
    rs.button2 = &button2;
    rs.button3 = &button3;
    rs.rota = &rota;
    rs.rotb = &rotb;
    rs.rotc = &rotc;
    rs.keys = &keys;
    rs.startTime = std::chrono::steady_clock::now();
    rs.lastFrameTime = rs.startTime;

    window.set_user_data(&rs);
    window.set_redraw_callback(vulkan_render_frame);

    window.show();

    while (true) {
        knst_window_event_system::non_block_pool_event();

        for (size_t i = 0; i < window.get_keyboard_event_count(); i++) {
            const auto& handle = window.get_keyboard_event(i);

            bool is_down = (handle.key_action == KNST_KEY_PRESS || handle.key_action == KNST_KEY_REPEAT);
            bool is_up   = (handle.key_action == KNST_KEY_RELEASE);

            if (handle.key_code == KNST_KEY_D) {
                if (is_down) keys.held_D = true;
                if (is_up)   keys.held_D = false;
            }
            else if (handle.key_code == KNST_KEY_A) {
                if (is_down) keys.held_A = true;
                if (is_up)   keys.held_A = false;
            }
            else if (handle.key_code == KNST_KEY_W) {
                if (is_down) keys.held_W = true;
                if (is_up)   keys.held_W = false;
            }
            else if (handle.key_code == KNST_KEY_S) {
                if (is_down) keys.held_S = true;
                if (is_up)   keys.held_S = false;
            }
        }

        window.call_redraw_callback();

        if (window.is_should_close()) {
            break;
        }

        window.clear_temporary_events();
    }

    gui.Destroy();
    vk_content.Destroy();
    window.destroy();
    KnstWindowSources::CleanUp();

    return 0;
}