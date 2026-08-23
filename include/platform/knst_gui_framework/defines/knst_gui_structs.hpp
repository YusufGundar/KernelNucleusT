#ifndef KNST_GUI_STRUCTS_HPP
#define KNST_GUI_STRUCTS_HPP
#pragma once


struct KnstVertex2D {
    float x, y, z;
    float r, g, b, a;
    float u, v;
    
    static KnstVertex2D Make(float x, float y, float z, float r=1, float g=1, float b=1, float a=1,float u=0, float v=0) {
                         
                         
    KnstVertex2D vertex;
    vertex.x = x; 
    vertex.y = y; 
    vertex.z = z;
    vertex.r = r; 
    vertex.g = g; 
    vertex.b = b; 
    vertex.a = a;
    vertex.u = u; 
    vertex.v = v;
    return vertex;
}
};




struct PushData {
   
    float width = 0.0f;
    float height = 0.0f;
    float time = 0.0f;
    
    
    float useUV = 0.0f;
    float useNormal = 0.0f;
    float useTangent = 0.0f;
    float useBones = 0.0f;
    float useCustom = 0.0f;
    
    
    float is2D = 0.0f;
    
    
    float rotX = 0.0f;
    float rotY = 0.0f;
    float rotZ = 0.0f;
    
    
    float translateX = 0.0f;
    float translateY = 0.0f;
    float translateZ = 0.0f;
    
   
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float scaleZ = 1.0f;
    
    
    float camX = 0.0f;
    float camY = 0.0f;
    float camZ = 500.0f;
    float camTargetX = 0.0f;
    float camTargetY = 0.0f;
    float camTargetZ = 0.0f;
    float camUpX = 0.0f;
    float camUpY = 1.0f;
    float camUpZ = 0.0f;
    
    
    float lightX = 300.0f;
    float lightY = -300.0f;
    float lightZ = 400.0f;
    float lightIntensity = 1.0f;
    float lightColorR = 1.0f;
    float lightColorG = 1.0f;
    float lightColorB = 1.0f;
    
    
    float colorR = 1.0f;
    float colorG = 1.0f;
    float colorB = 1.0f;
    float colorA = 1.0f;
    
  
    float projectionType = 0.0f;
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float orthoSize = 5.0f;
    
   
    float coordSystem = 0.0f;
};


struct KnstDrawConfig {
    
    enum class RenderMode {
        MODE_2D,
        MODE_3D
    };

    RenderMode renderMode = RenderMode::MODE_3D;
    bool frontFaceCW = true;
  
    knst_vector<KnstVertex2D> vertices2D;
    knst_vector<KnstVertex3D> vertices3D;
    knst_vector<uint32_t> indices;
    
    const knst_texture* texture = nullptr;
    
   
    void SetTexture(const knst_texture* tex) {
        texture = tex;
        if (tex != nullptr) {
            pushData.useUV = 1.0f;
        }
    }
    
    bool HasTexture() const {
        return texture != nullptr && texture->IsValid();
    }

    PushData pushData;
    knst_vector<uint8_t> pushConstants;
    
    
    float zLayer = 0.5f;
    
    
    VkShaderStageFlags pushConstantStage = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uint32_t pushConstantOffset = 0;
    VkPipelineLayout pushConstantLayout = VK_NULL_HANDLE;
    
    
    //2d vertex yapısı eklemek içindir
    void AddVertex2D(float x, float y,float r=1, float g=1, float b=1, float a=1,float u=0, float v=0) { 
        renderMode = RenderMode::MODE_2D;
        vertices2D.push_back(KnstVertex2D::Make(x, y, zLayer, r, g, b, a, u, v));
    }
    
    //2d vertex yapısı eklemek içindir z kordinatiyla bu widgetlarda z eksenini kontrol etmek isteyebilirsiniz hangisi önde hangisi arkada
    void AddVertex2DZ(float x, float y, float z,float r=1, float g=1, float b=1, float a=1,float u=0, float v=0) {
                      
        renderMode = RenderMode::MODE_2D;
        vertices2D.push_back(KnstVertex2D::Make(x, y, z, r, g, b, a, u, v));
    }
    
   //3d vertex yapısı eklemek içindir
    void AddVertex3D(float x, float y, float z,
                     float r=1, float g=1, float b=1, float a=1,
                     float u=0, float v=0,
                     float nx=0, float ny=0, float nz=1) {
        renderMode = RenderMode::MODE_3D;
        vertices3D.push_back(KnstVertex3D::Make(x, y, z, r, g, b, a, u, v, nx, ny, nz));
    }
    
    // 3d Vertex Ekle UV + Normal
    void AddVertex3DUVN(float x, float y, float z,
                        float nx, float ny, float nz,
                        float u, float v,
                        float r=1, float g=1, float b=1, float a=1) {
        renderMode = RenderMode::MODE_3D;
        vertices3D.push_back(KnstVertex3D::Make(x, y, z, r, g, b, a, u, v, nx, ny, nz));
        pushData.useUV = 1.0f;
        pushData.useNormal = 1.0f;
    }
    
    //3D Vertex Ekle (UV + Normal + Tangent) 
    void AddVertexPBR(float x, float y, float z,float nx, float ny, float nz,
                      float tx, float ty, float tz,
                      float u, float v,
                      float r=1, float g=1, float b=1, float a=1) {
        renderMode = RenderMode::MODE_3D;
        KnstVertex3D vert = KnstVertex3D::Make(x, y, z, r, g, b, a, u, v, nx, ny, nz);
        vert.tx = tx; vert.ty = ty; vert.tz = tz;
        vertices3D.push_back(vert);
        pushData.useUV = 1.0f;
        pushData.useNormal = 1.0f;
        pushData.useTangent = 1.0f;
    }
    
   
    void Set2D() { renderMode = RenderMode::MODE_2D; }
    void Set3D() { renderMode = RenderMode::MODE_3D; }
    
    bool Is2D() const { return renderMode == RenderMode::MODE_2D; }
    bool Is3D() const { return renderMode == RenderMode::MODE_3D; }
   
    void SetZLayer(float z) {
        zLayer = z;
        for (auto& v : vertices2D) {
            v.z = z;
        }
    }
    
   
    void SetLayerBackground() { SetZLayer(0.0f); }
    void SetLayerBack()       { SetZLayer(0.2f); }
    void SetLayerMiddle()     { SetZLayer(0.5f); }
    void SetLayerFront()      { SetZLayer(0.8f); }
    void SetLayerOverlay()    { SetZLayer(0.99f); }
    void SetLayerTop()        { SetZLayer(1.0f); }
    
    
    void SetRotation(float x, float y, float z) {
        pushData.rotX = x;
        pushData.rotY = y;
        pushData.rotZ = z;
    }
    
    void SetPosition(float x, float y, float z = 0.0f) {
        pushData.translateX = x;
        pushData.translateY = y;
        pushData.translateZ = z;
    }
    
    void SetScale(float x, float y, float z = 1.0f) {
        pushData.scaleX = x;
        pushData.scaleY = y;
        pushData.scaleZ = z;
    }
    
    void SetColor(float r, float g, float b, float a = 1.0f) {
        pushData.colorR = r;
        pushData.colorG = g;
        pushData.colorB = b;
        pushData.colorA = a;
    }
    
    void SetCamera(float x, float y, float z) {
        pushData.camX = x;
        pushData.camY = y;
        pushData.camZ = z;
    }
    
    void SetCameraTarget(float x, float y, float z) {
        pushData.camTargetX = x;
        pushData.camTargetY = y;
        pushData.camTargetZ = z;
    }
    
    void SetCameraUp(float x, float y, float z) {
        pushData.camUpX = x;
        pushData.camUpY = y;
        pushData.camUpZ = z;
    }
    
    void SetLightPosition(float x, float y, float z) {
        pushData.lightX = x;
        pushData.lightY = y;
        pushData.lightZ = z;
    }
    
    void SetLightIntensity(float intensity) {
        pushData.lightIntensity = intensity;
    }
    
    void SetLightColor(float r, float g, float b) {
        pushData.lightColorR = r;
        pushData.lightColorG = g;
        pushData.lightColorB = b;
    }
    
    void UsePerspective3D(float fovDeg, float nearP, float farP) {
        pushData.projectionType = 1.0f;
        pushData.fov = fovDeg;
        pushData.nearPlane = nearP;
        pushData.farPlane = farP;
    }
    
    void UseOrthographic3D(float size, float nearP, float farP) {
        pushData.projectionType = 2.0f;
        pushData.orthoSize = size;
        pushData.nearPlane = nearP;
        pushData.farPlane = farP;
    }
    
    void UsePixelSpace2D() {
        pushData.coordSystem = 0.0f;
        pushData.projectionType = 0.0f;
        pushData.is2D = 1.0f;
    }
    
    void UseNDC() {
        pushData.coordSystem = 2.0f;
        pushData.is2D = 0.0f;
    }
    
    void SetCoordSystemNDCVulkan() {
        pushData.coordSystem = 2.0f;
    }
   
    void AddIndex(uint32_t index) {
        indices.push_back(index);
    }
    
   
    void AddIndices(std::initializer_list<uint32_t> list) {
        for (uint32_t idx : list) {
            indices.push_back(idx);
        }
    }
    
   
    void AddIndices(const knst_vector<uint32_t>& idxList) {
        for (uint32_t idx : idxList) {
            indices.push_back(idx);
        }
    }
    
    
    void AddIndices(const uint32_t* data, uint32_t count) {
        for (uint32_t i = 0; i < count; i++) {
            indices.push_back(data[i]);
        }
    }
    
    // template ilede verebilirsiniz tabi çeviriyor bunu burda
    template<typename... Args>
    void AddIndices(Args... args) {
        (indices.push_back(static_cast<uint32_t>(args)), ...);
    }
    
    
    void SyncPushConstants() { // PushData yı shadera göndermeye hazırlar bunun amacı şudur bu ölçekleme dönme işlemlerini yapıyorsanız eğer bunu gpu ya gönderir örneğin  küpün rotX değeri değişti işte artık böyle gibisinden 
        pushConstants.resize(sizeof(PushData));
        memcpy(pushConstants.data(), &pushData, sizeof(PushData));
    }
    
   
    bool Empty() const {
        return vertices2D.empty() && vertices3D.empty();
    }
    
    size_t VertexCount() const {
        return Is2D() ? vertices2D.size() : vertices3D.size();
    }
    
    size_t VertexSize() const {
        return Is2D() ? sizeof(KnstVertex2D) : sizeof(KnstVertex3D);
    }
    
    const void* VertexData() const {
        if (Is2D()) {
            return static_cast<const void*>(vertices2D.data());
        } else {
            return static_cast<const void*>(vertices3D.data());
        }
    }
    
    
    const KnstVertex2D* VertexData2D() const { return vertices2D.data(); }
    const KnstVertex3D* VertexData3D() const { return vertices3D.data(); }
   
    void Clear() {
        vertices2D.clear();
        vertices3D.clear();
        indices.clear();
        pushConstants.clear();
        pushData = PushData();
        renderMode = RenderMode::MODE_3D;
        zLayer = 0.5f;
    }
};


struct KnstVertex {
    float x, y, z;
    float r, g, b, a;
    float u, v;
    float nx, ny, nz;
    float tx, ty, tz;
    uint32_t boneIndices[4];
    float boneWeights[4];
    float customData[4];
    
    KnstVertex() {
        x = y = z = 0;
        r = g = b = 1.0f; a = 1.0f;
        u = v = 0;
        nx = ny = nz = 0;
        tx = ty = tz = 0;
        boneIndices[0] = boneIndices[1] = boneIndices[2] = boneIndices[3] = 0;
        boneWeights[0] = 1.0f; boneWeights[1] = boneWeights[2] = boneWeights[3] = 0;
        customData[0] = customData[1] = customData[2] = customData[3] = 0;
    }
    
    static KnstVertex Make2D(float x, float y, float r = 1, float g = 1, float b = 1) {
        KnstVertex v;
        v.x = x; v.y = y; v.z = 0;
        v.r = r; v.g = g; v.b = b; v.a = 1;
        return v;
    }
    
    static KnstVertex Make3D(float x, float y, float z, 
                             float r = 1, float g = 1, float b = 1, float a = 1) {
        KnstVertex v;
        v.x = x; v.y = y; v.z = z;
        v.r = r; v.g = g; v.b = b; v.a = a;
        return v;
    }
    
    static KnstVertex Make3DUVN(float x, float y, float z,
                                float nx, float ny, float nz,
                                float u, float _v,
                                float r = 1, float g = 1, float b = 1, float a = 1) {
        KnstVertex v;
        v.x = x; v.y = y; v.z = z;
        v.nx = nx; v.ny = ny; v.nz = nz;
        v.u = u; v.v = _v;
        v.r = r; v.g = g; v.b = b; v.a = a;
        return v;
    }
};


class KnstVertexLayout {
public:
    static knst_vector<VkVertexInputAttributeDescription> CreateLayout(
        bool usePosition = true,
        bool useColor = true,
        bool useUV = false,
        bool useNormal = false,
        bool useTangent = false,
        bool useBones = false,
        bool useCustom = false
    ) {
        knst_vector<VkVertexInputAttributeDescription> attributes;
        uint32_t location = 0;
        
       
        if (usePosition) {
            VkVertexInputAttributeDescription attr{};
            attr.location = location++;
            attr.binding = 0;
            attr.format = VK_FORMAT_R32G32B32_SFLOAT;
            attr.offset = offsetof(KnstVertex, x);
            attributes.push_back(attr);
        }
        
    
        if (useColor) {
            VkVertexInputAttributeDescription attr{};
            attr.location = location++;
            attr.binding = 0;
            attr.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attr.offset = offsetof(KnstVertex, r);
            attributes.push_back(attr);
        }
        
        
        if (useUV) {
            VkVertexInputAttributeDescription attr{};
            attr.location = location++;
            attr.binding = 0;
            attr.format = VK_FORMAT_R32G32_SFLOAT;
            attr.offset = offsetof(KnstVertex, u);
            attributes.push_back(attr);
        }
        
      
        if (useNormal) {
            VkVertexInputAttributeDescription attr{};
            attr.location = location++;
            attr.binding = 0;
            attr.format = VK_FORMAT_R32G32B32_SFLOAT;
            attr.offset = offsetof(KnstVertex, nx);
            attributes.push_back(attr);
        }
        
      
        if (useTangent) {
            VkVertexInputAttributeDescription attr{};
            attr.location = location++;
            attr.binding = 0;
            attr.format = VK_FORMAT_R32G32B32_SFLOAT;
            attr.offset = offsetof(KnstVertex, tx);
            attributes.push_back(attr);
        }
        

        if (useBones) {
            VkVertexInputAttributeDescription attr{};
            attr.location = location++;
            attr.binding = 0;
            attr.format = VK_FORMAT_R32G32B32A32_UINT;
            attr.offset = offsetof(KnstVertex, boneIndices);
            attributes.push_back(attr);
        }
        
       
        if (useBones) {
            VkVertexInputAttributeDescription attr{};
            attr.location = location++;
            attr.binding = 0;
            attr.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attr.offset = offsetof(KnstVertex, boneWeights);
            attributes.push_back(attr);
        }
        
       
        if (useCustom) {
            VkVertexInputAttributeDescription attr{};
            attr.location = location++;
            attr.binding = 0;
            attr.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attr.offset = offsetof(KnstVertex, customData);
            attributes.push_back(attr);
        }
        
        return attributes;
    }
};

enum class KnstPresentMode {
    IMMEDIATE,
    VSYNC,
    VSYNC_RELAXED,
    MAILBOX,
    FIFO_LATEST,
    SHARED_DEMAND,
    SHARED_CONTINUOUS,
};

enum class KnstSwapchainBuffering {
    SINGLE_BUFFER = 1,
    DOUBLE_BUFFER = 2,
    TRIPLE_BUFFER = 3,
    QUAD_BUFFER = 4,
    AUTO = 0
};

enum class KnstColorSpace {
    SRGB_NONLINEAR,
    DISPLAY_P3_NONLINEAR,
    EXTENDED_SRGB_LINEAR,
    PASS_THROUGH,
    DEFAULT = SRGB_NONLINEAR
};

enum class KnstSurfaceFormatType {
    AUTO = 0,
    B8G8R8A8_SRGB,
    R8G8B8A8_SRGB,
    B8G8R8A8_UNORM,
    R8G8B8A8_UNORM,
    R16G16B16A16_SFLOAT,
    R32G32B32A32_SFLOAT,
    R8_UNORM,
    R8_SRGB,
    R16_UNORM,
    R16_SFLOAT,
    R32_SFLOAT
};


inline VkFormat KnstToVulkanFormat(KnstSurfaceFormatType formatType) {
    switch (formatType) {
        case KnstSurfaceFormatType::B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
        case KnstSurfaceFormatType::R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
        case KnstSurfaceFormatType::B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
        case KnstSurfaceFormatType::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case KnstSurfaceFormatType::R16G16B16A16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case KnstSurfaceFormatType::R32G32B32A32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case KnstSurfaceFormatType::R8_UNORM: return VK_FORMAT_R8_UNORM;
        case KnstSurfaceFormatType::R8_SRGB: return VK_FORMAT_R8_SRGB;
        case KnstSurfaceFormatType::R16_UNORM: return VK_FORMAT_R16_UNORM;
        case KnstSurfaceFormatType::R16_SFLOAT: return VK_FORMAT_R16_SFLOAT;
        case KnstSurfaceFormatType::R32_SFLOAT: return VK_FORMAT_R32_SFLOAT;
        default: return VK_FORMAT_UNDEFINED;
    }
}

inline VkPresentModeKHR KnstToVulkanPresentMode(const KnstPresentMode& mode) {
    switch (mode) {
        case KnstPresentMode::IMMEDIATE: return VK_PRESENT_MODE_IMMEDIATE_KHR;
        case KnstPresentMode::VSYNC: return VK_PRESENT_MODE_FIFO_KHR;
        case KnstPresentMode::VSYNC_RELAXED: return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        case KnstPresentMode::MAILBOX: return VK_PRESENT_MODE_MAILBOX_KHR;
        case KnstPresentMode::FIFO_LATEST: return VK_PRESENT_MODE_FIFO_LATEST_READY_KHR;
        case KnstPresentMode::SHARED_DEMAND: return VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR;
        case KnstPresentMode::SHARED_CONTINUOUS: return VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR;
        default: return VK_PRESENT_MODE_FIFO_KHR;
    }
}

inline VkColorSpaceKHR KnstToVulkanColorSpace(KnstColorSpace cs) {
    switch (cs) {
        case KnstColorSpace::SRGB_NONLINEAR: return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        case KnstColorSpace::DISPLAY_P3_NONLINEAR: return VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT;
        case KnstColorSpace::EXTENDED_SRGB_LINEAR: return VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
        default: return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    }
}


struct KnstGuiSurfacePropConfig {
    KnstSurfaceFormatType formatType = KnstSurfaceFormatType::B8G8R8A8_SRGB;
    KnstColorSpace colorSpace = KnstColorSpace::DEFAULT;
    KnstPresentMode presentModeVsync = KnstPresentMode::MAILBOX;
    KnstPresentMode presentMode = KnstPresentMode::IMMEDIATE;
   
    static KnstGuiSurfacePropConfig Default() {
        KnstGuiSurfacePropConfig config;
        config.formatType = KnstSurfaceFormatType::B8G8R8A8_SRGB;
        config.colorSpace = KnstColorSpace::DEFAULT;
        config.presentModeVsync = KnstPresentMode::MAILBOX;
        config.presentMode = KnstPresentMode::IMMEDIATE;
        return config;
    }
};

struct KnstSwapchainConfig {
    bool vsync = false;
    KnstSwapchainBuffering buffering = KnstSwapchainBuffering::AUTO;
    uint32_t customImageCount = 0;
    bool allowTransferSrc = false;
    bool allowTransferDst = true;
    bool allowStorage = false;
    bool allowSampled = false;
    bool transparentWindow = false;
    bool clipped = true;
    bool allowRotation = false;
    int width = 800;
    int height = 800;
    
    static KnstSwapchainConfig Default() {
        KnstSwapchainConfig config;
        config.vsync = false;
        config.buffering = KnstSwapchainBuffering::AUTO;
        config.customImageCount = 0;
        config.allowTransferSrc = false;
        config.allowTransferDst = true;
        config.allowStorage = false;
        config.allowSampled = false;
        config.transparentWindow = false;
        config.clipped = true;
        config.allowRotation = false;
        config.width = 800;
        config.height = 800;
        return config;
    }
};


enum class KnstAttachmentLoadOp {
    LOAD = VK_ATTACHMENT_LOAD_OP_LOAD,
    CLEAR = VK_ATTACHMENT_LOAD_OP_CLEAR,
    DONT_CARE = VK_ATTACHMENT_LOAD_OP_DONT_CARE
};

enum class KnstAttachmentStoreOp {
    STORE = VK_ATTACHMENT_STORE_OP_STORE,
    DONT_CARE = VK_ATTACHMENT_STORE_OP_DONT_CARE
};

enum class KnstImageLayout {
    UNDEFINED = VK_IMAGE_LAYOUT_UNDEFINED,
    GENERAL = VK_IMAGE_LAYOUT_GENERAL,
    COLOR_ATTACHMENT_OPTIMAL = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    DEPTH_STENCIL_ATTACHMENT_OPTIMAL = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    DEPTH_STENCIL_READ_ONLY_OPTIMAL = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
    SHADER_READ_ONLY_OPTIMAL = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    TRANSFER_SRC_OPTIMAL = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    TRANSFER_DST_OPTIMAL = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    PREINITIALIZED = VK_IMAGE_LAYOUT_PREINITIALIZED,
    PRESENT_SRC_KHR = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
};

enum class KnstAttachmentType {
    COLOR = 0,
    DEPTH = 1,
    STENCIL = 2,
    DEPTH_STENCIL = 3,
    RESOLVE = 4,
    INPUT = 5,
    PRESERVE = 6
};


struct KnstAttachmentConfig {
    KnstAttachmentType type = KnstAttachmentType::COLOR;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    KnstAttachmentLoadOp loadOp = KnstAttachmentLoadOp::CLEAR;
    KnstAttachmentStoreOp storeOp = KnstAttachmentStoreOp::STORE;
    KnstAttachmentLoadOp stencilLoadOp = KnstAttachmentLoadOp::DONT_CARE;
    KnstAttachmentStoreOp stencilStoreOp = KnstAttachmentStoreOp::DONT_CARE;
    KnstImageLayout initialLayout = KnstImageLayout::UNDEFINED;
    KnstImageLayout finalLayout = KnstImageLayout::PRESENT_SRC_KHR;
};

struct KnstAttachmentReference {
    uint32_t attachment = 0;
    KnstImageLayout layout = KnstImageLayout::COLOR_ATTACHMENT_OPTIMAL;
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
};

struct KnstSubpassConfig {
    knst_vector<KnstAttachmentReference> colorAttachments;
    knst_vector<KnstAttachmentReference> inputAttachments;
    knst_vector<KnstAttachmentReference> resolveAttachments;
    KnstAttachmentReference depthStencilAttachment = {};
    bool hasDepthStencil = false;
    knst_vector<uint32_t> preserveAttachments;
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
};

struct KnstSubpassDependency {
    uint32_t srcSubpass = VK_SUBPASS_EXTERNAL;
    uint32_t dstSubpass = 0;
    VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags srcAccessMask = 0;
    VkAccessFlags dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    bool byRegion = false;
};

struct KnstRenderPassConfig {
    knst_vector<KnstAttachmentConfig> attachments;
    knst_vector<KnstSubpassConfig> subpasses;
    knst_vector<KnstSubpassDependency> dependencies;
    bool useRenderPass2 = false;
    
        static KnstRenderPassConfig Default() {
        KnstRenderPassConfig config;
        

        KnstAttachmentConfig color;
        color.type = KnstAttachmentType::COLOR;
        color.format = VK_FORMAT_UNDEFINED;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = KnstAttachmentLoadOp::CLEAR;
        color.storeOp = KnstAttachmentStoreOp::STORE;
        color.stencilLoadOp = KnstAttachmentLoadOp::DONT_CARE;
        color.stencilStoreOp = KnstAttachmentStoreOp::DONT_CARE;
        color.initialLayout = KnstImageLayout::UNDEFINED;
        color.finalLayout = KnstImageLayout::PRESENT_SRC_KHR;
        config.attachments.push_back(color);
        
        
        KnstAttachmentConfig depth;
        depth.type = KnstAttachmentType::DEPTH;
        depth.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = KnstAttachmentLoadOp::CLEAR;
        depth.storeOp = KnstAttachmentStoreOp::DONT_CARE;
        depth.stencilLoadOp = KnstAttachmentLoadOp::DONT_CARE;
        depth.stencilStoreOp = KnstAttachmentStoreOp::DONT_CARE;
        depth.initialLayout = KnstImageLayout::UNDEFINED;
        depth.finalLayout = KnstImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        config.attachments.push_back(depth);
        
      
        KnstSubpassConfig subpass;
        
        KnstAttachmentReference colorRef;
        colorRef.attachment = 0;
        colorRef.layout = KnstImageLayout::COLOR_ATTACHMENT_OPTIMAL;
        colorRef.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subpass.colorAttachments.push_back(colorRef);
        
        subpass.hasDepthStencil = true;
        subpass.depthStencilAttachment.attachment = 1;
        subpass.depthStencilAttachment.layout = KnstImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        subpass.depthStencilAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        
        config.subpasses.push_back(subpass);
        
       
        KnstSubpassDependency dep;
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        config.dependencies.push_back(dep);
        
        return config;
    }
};


inline VkAttachmentLoadOp KnstToVulkanLoadOp(KnstAttachmentLoadOp op) {
    return static_cast<VkAttachmentLoadOp>(op);
}

inline VkAttachmentStoreOp KnstToVulkanStoreOp(KnstAttachmentStoreOp op) {
    return static_cast<VkAttachmentStoreOp>(op);
}

inline VkImageLayout KnstToVulkanImageLayout(KnstImageLayout layout) {
    return static_cast<VkImageLayout>(layout);
}

enum class KnstPrimitiveTopology {
    POINT_LIST = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
    LINE_LIST = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    LINE_STRIP = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
    TRIANGLE_LIST = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    TRIANGLE_STRIP = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    TRIANGLE_FAN = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
    LINE_LIST_WITH_ADJACENCY = VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY,
    LINE_STRIP_WITH_ADJACENCY = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY,
    TRIANGLE_LIST_WITH_ADJACENCY = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY,
    TRIANGLE_STRIP_WITH_ADJACENCY = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY,
    PATCH_LIST = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST
};

enum class KnstPolygonMode {
    FILL = VK_POLYGON_MODE_FILL,
    LINE = VK_POLYGON_MODE_LINE,
    POINT = VK_POLYGON_MODE_POINT
};

enum class KnstCullMode {
    NONE = VK_CULL_MODE_NONE,
    FRONT = VK_CULL_MODE_FRONT_BIT,
    BACK = VK_CULL_MODE_BACK_BIT,
    FRONT_AND_BACK = VK_CULL_MODE_FRONT_AND_BACK
};

enum class KnstFrontFace {
    COUNTER_CLOCKWISE = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    CLOCKWISE = VK_FRONT_FACE_CLOCKWISE
};

enum class KnstBlendFactor {
    ZERO = VK_BLEND_FACTOR_ZERO,
    ONE = VK_BLEND_FACTOR_ONE,
    SRC_COLOR = VK_BLEND_FACTOR_SRC_COLOR,
    ONE_MINUS_SRC_COLOR = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    DST_COLOR = VK_BLEND_FACTOR_DST_COLOR,
    ONE_MINUS_DST_COLOR = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    SRC_ALPHA = VK_BLEND_FACTOR_SRC_ALPHA,
    ONE_MINUS_SRC_ALPHA = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    DST_ALPHA = VK_BLEND_FACTOR_DST_ALPHA,
    ONE_MINUS_DST_ALPHA = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    CONSTANT_COLOR = VK_BLEND_FACTOR_CONSTANT_COLOR,
    ONE_MINUS_CONSTANT_COLOR = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
    CONSTANT_ALPHA = VK_BLEND_FACTOR_CONSTANT_ALPHA,
    ONE_MINUS_CONSTANT_ALPHA = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
    SRC_ALPHA_SATURATE = VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
    SRC1_COLOR = VK_BLEND_FACTOR_SRC1_COLOR,
    ONE_MINUS_SRC1_COLOR = VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
    SRC1_ALPHA = VK_BLEND_FACTOR_SRC1_ALPHA,
    ONE_MINUS_SRC1_ALPHA = VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA
};

enum class KnstBlendOp {
    ADD = VK_BLEND_OP_ADD,
    SUBTRACT = VK_BLEND_OP_SUBTRACT,
    REVERSE_SUBTRACT = VK_BLEND_OP_REVERSE_SUBTRACT,
    MIN = VK_BLEND_OP_MIN,
    MAX = VK_BLEND_OP_MAX
};

enum class KnstCompareOp {
    NEVER = VK_COMPARE_OP_NEVER,
    LESS = VK_COMPARE_OP_LESS,
    EQUAL = VK_COMPARE_OP_EQUAL,
    LESS_OR_EQUAL = VK_COMPARE_OP_LESS_OR_EQUAL,
    GREATER = VK_COMPARE_OP_GREATER,
    NOT_EQUAL = VK_COMPARE_OP_NOT_EQUAL,
    GREATER_OR_EQUAL = VK_COMPARE_OP_GREATER_OR_EQUAL,
    ALWAYS = VK_COMPARE_OP_ALWAYS
};

enum class KnstStencilOp {
    KEEP = VK_STENCIL_OP_KEEP,
    ZERO = VK_STENCIL_OP_ZERO,
    REPLACE = VK_STENCIL_OP_REPLACE,
    INCREMENT_AND_CLAMP = VK_STENCIL_OP_INCREMENT_AND_CLAMP,
    DECREMENT_AND_CLAMP = VK_STENCIL_OP_DECREMENT_AND_CLAMP,
    INVERT = VK_STENCIL_OP_INVERT,
    INCREMENT_AND_WRAP = VK_STENCIL_OP_INCREMENT_AND_WRAP,
    DECREMENT_AND_WRAP = VK_STENCIL_OP_DECREMENT_AND_WRAP
};

enum class KnstColorComponent {
    R = VK_COLOR_COMPONENT_R_BIT,
    G = VK_COLOR_COMPONENT_G_BIT,
    B = VK_COLOR_COMPONENT_B_BIT,
    A = VK_COLOR_COMPONENT_A_BIT
};

enum class KnstDynamicState {
    VIEWPORT = VK_DYNAMIC_STATE_VIEWPORT,
    SCISSOR = VK_DYNAMIC_STATE_SCISSOR,
    LINE_WIDTH = VK_DYNAMIC_STATE_LINE_WIDTH,
    DEPTH_BIAS = VK_DYNAMIC_STATE_DEPTH_BIAS,
    BLEND_CONSTANTS = VK_DYNAMIC_STATE_BLEND_CONSTANTS,
    DEPTH_BOUNDS = VK_DYNAMIC_STATE_DEPTH_BOUNDS,
    STENCIL_REFERENCE = VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    VIEWPORT_WITH_COUNT = VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT_EXT,
    SCISSOR_WITH_COUNT = VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT_EXT,
    VERTEX_INPUT_EXT = VK_DYNAMIC_STATE_VERTEX_INPUT_EXT,
    PATCH_CONTROL_POINTS_EXT = VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT,
    RASTERIZATION_STREAM_EXT = VK_DYNAMIC_STATE_RASTERIZATION_STREAM_EXT,

    
    CULL_MODE = VK_DYNAMIC_STATE_CULL_MODE_EXT,
    FRONT_FACE = VK_DYNAMIC_STATE_FRONT_FACE_EXT,
    DEPTH_TEST_ENABLE = VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE_EXT,
    DEPTH_WRITE_ENABLE = VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE_EXT,
    DEPTH_COMPARE_OP = VK_DYNAMIC_STATE_DEPTH_COMPARE_OP_EXT,
    DEPTH_BOUNDS_TEST_ENABLE = VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE_EXT,
};


struct KnstVertexBindingDescription {
    uint32_t binding = 0;
    uint32_t stride = 0;
    VkVertexInputRate inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
};

struct KnstVertexInputConfig {
    knst_vector<KnstVertexBindingDescription> bindings;
    knst_vector<VkVertexInputAttributeDescription> attributes; // buraya geri dön.. knst yapı hatası şimdilik vulkan yapısı kullandım unutma..
};


struct KnstStencilOpState {
    KnstStencilOp failOp = KnstStencilOp::KEEP;
    KnstStencilOp passOp = KnstStencilOp::KEEP;
    KnstStencilOp depthFailOp = KnstStencilOp::KEEP;
    KnstCompareOp compareOp = KnstCompareOp::ALWAYS;
    uint32_t compareMask = 0xFFFFFFFF;
    uint32_t writeMask = 0xFFFFFFFF;
    uint32_t reference = 0;
};

struct KnstDepthStencilConfig {
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    KnstCompareOp depthCompareOp = KnstCompareOp::LESS;
    bool depthBoundsTestEnable = false;
    bool stencilTestEnable = false;
    KnstStencilOpState front = {};
    KnstStencilOpState back = {};
    float minDepthBounds = 0.0f;
    float maxDepthBounds = 1.0f;
};


struct KnstColorBlendAttachmentConfig {
    bool blendEnable = true;
    KnstBlendFactor srcColorBlendFactor = KnstBlendFactor::SRC_ALPHA;
    KnstBlendFactor dstColorBlendFactor = KnstBlendFactor::ONE_MINUS_SRC_ALPHA;
    KnstBlendOp colorBlendOp = KnstBlendOp::ADD;
    KnstBlendFactor srcAlphaBlendFactor = KnstBlendFactor::ONE;
    KnstBlendFactor dstAlphaBlendFactor = KnstBlendFactor::ZERO;
    KnstBlendOp alphaBlendOp = KnstBlendOp::ADD;
    uint32_t colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                             
};

struct KnstColorBlendConfig {
    bool logicOpEnable = false;
    VkLogicOp logicOp = VK_LOGIC_OP_COPY;
    knst_vector<KnstColorBlendAttachmentConfig> attachments;
    float blendConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};


struct KnstRasterizationConfig {
    bool depthClampEnable = false;
    bool rasterizerDiscardEnable = false;
    KnstPolygonMode polygonMode = KnstPolygonMode::FILL;
    KnstCullMode cullMode = KnstCullMode::BACK;
    KnstFrontFace frontFace = KnstFrontFace::CLOCKWISE;
    bool depthBiasEnable = false;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
    float lineWidth = 1.0f;
};


struct KnstMultisampleConfig {
    VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    bool sampleShadingEnable = false;
    float minSampleShading = 1.0f;
    const void* pSampleMask = nullptr;
    bool alphaToCoverageEnable = false;
    bool alphaToOneEnable = false;
};


struct KnstGraphicsPipelineConfig {
    knst_byte_string vertexShaderCode;
    knst_byte_string fragmentShaderCode;
    knst_byte_string geometryShaderCode;
    knst_byte_string tessellationControlShaderCode;
    knst_byte_string tessellationEvaluationShaderCode;
    
    KnstVertexInputConfig vertexInput = {};
    KnstPrimitiveTopology topology = KnstPrimitiveTopology::TRIANGLE_LIST;
    bool primitiveRestartEnable = false;
    uint32_t patchControlPoints = 0;
    bool dynamicViewport = true;
    bool dynamicScissor = true;
    VkViewport viewport = {};
    VkRect2D scissor = {};
    KnstRasterizationConfig rasterization = {};
    KnstMultisampleConfig multisample = {};
    KnstDepthStencilConfig depthStencil = {};
    KnstColorBlendConfig colorBlend = {};
    knst_vector<KnstDynamicState> dynamicStates;
    knst_vector<VkPushConstantRange> pushConstantRanges;
    knst_vector<VkDescriptorSetLayout> descriptorSetLayouts;
    uint32_t subpassIndex = 0;
    
    static KnstGraphicsPipelineConfig Default() {
        KnstGraphicsPipelineConfig config;
        config.vertexInput = {};
        config.topology = KnstPrimitiveTopology::TRIANGLE_LIST;
        config.primitiveRestartEnable = false;
        config.rasterization.polygonMode = KnstPolygonMode::FILL;
        config.rasterization.cullMode = KnstCullMode::BACK;
        config.rasterization.frontFace = KnstFrontFace::CLOCKWISE;
        config.rasterization.lineWidth = 1.0f;
        config.multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        config.depthStencil.depthTestEnable = true;
        config.depthStencil.depthWriteEnable = true;
        config.depthStencil.depthCompareOp = KnstCompareOp::LESS;
        
        KnstColorBlendAttachmentConfig blendAttachment;
        blendAttachment.blendEnable = true;
        blendAttachment.srcColorBlendFactor = KnstBlendFactor::SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = KnstBlendFactor::ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = KnstBlendOp::ADD;
        config.colorBlend.attachments.push_back(blendAttachment);
        
        config.dynamicStates = {
            KnstDynamicState::VIEWPORT,
            KnstDynamicState::SCISSOR,
            KnstDynamicState::DEPTH_TEST_ENABLE,
            KnstDynamicState::DEPTH_WRITE_ENABLE,
            KnstDynamicState::DEPTH_COMPARE_OP,
            KnstDynamicState::DEPTH_BOUNDS,
            KnstDynamicState::CULL_MODE,
            KnstDynamicState::FRONT_FACE
        };
        
        return config;
    }
};


struct KnstClearColor {
    float r = -1.0f;
    float g = -1.0f;
    float b = -1.0f;
    float a = -1.0f;
    
    static KnstClearColor DontClear() { return {-1.0f, -1.0f, -1.0f, -1.0f}; }
    static KnstClearColor Black() { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    static KnstClearColor White() { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    static KnstClearColor Red() { return {1.0f, 0.0f, 0.0f, 1.0f}; }
    static KnstClearColor Green() { return {0.0f, 1.0f, 0.0f, 1.0f}; }
    static KnstClearColor Blue() { return {0.0f, 0.0f, 1.0f, 1.0f}; }
    static KnstClearColor Dark() { return {0.05f, 0.05f, 0.1f, 1.0f}; }
    static KnstClearColor Light() { return {0.95f, 0.95f, 0.97f, 1.0f}; }
    static KnstClearColor Custom(float r, float g, float b, float a = 1.0f) { return {r, g, b, a}; }
    bool IsValid() const { return r >= 0.0f && g >= 0.0f && b >= 0.0f && a >= 0.0f; }
};


struct KnstViewportConfig {
    bool autoSize = true;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
    bool preserveAspectRatio = false;
    float aspectRatio = 1.777f;
    bool autoScissor = true;
    int32_t scissorX = 0;
    int32_t scissorY = 0;
    uint32_t scissorWidth = 0;
    uint32_t scissorHeight = 0;
    
    static KnstViewportConfig Fullscreen() {
        KnstViewportConfig config;
        config.autoSize = true;
        config.autoScissor = true;
        config.preserveAspectRatio = false;
        return config;
    }
    
    static KnstViewportConfig Custom(float x, float y, float w, float h) {
        KnstViewportConfig config;
        config.autoSize = false;
        config.x = x;
        config.y = y;
        config.width = w;
        config.height = h;
        config.autoScissor = false;
        config.scissorX = (int32_t)x;
        config.scissorY = (int32_t)y;
        config.scissorWidth = (uint32_t)w;
        config.scissorHeight = (uint32_t)h;
        config.preserveAspectRatio = false;
        return config;
    }
    
    static KnstViewportConfig AspectRatio(float ratio) {
        KnstViewportConfig config = Fullscreen();
        config.preserveAspectRatio = true;
        config.aspectRatio = ratio;
        return config;
    }
    
    static KnstViewportConfig AspectRatioCustom(float ratio, float x, float y, float w, float h) {
        KnstViewportConfig config = Custom(x, y, w, h);
        config.preserveAspectRatio = true;
        config.aspectRatio = ratio;
        return config;
    }
};

struct KnstGuiConfig {
    std::string vertexShaderPath;
    std::string fragmentShaderPath;
    KnstCullMode cullMode = KnstCullMode::BACK;
    bool enableBlend = true;

    static KnstGuiConfig Default(const std::string& vertPath,const std::string& fragPath) {
        KnstGuiConfig cfg;
        cfg.vertexShaderPath = vertPath;
        cfg.fragmentShaderPath = fragPath;
        return cfg;
    }
};

#endif // KNST_GUI_STRUCTS_HPP