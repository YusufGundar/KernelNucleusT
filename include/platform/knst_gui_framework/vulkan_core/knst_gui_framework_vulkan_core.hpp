#ifndef KNST_GUI_FRAMEWORK_VULKAN_CORE_HPP
#define KNST_GUI_FRAMEWORK_VULKAN_CORE_HPP
#pragma once



#include <fstream>

class knst_gui_framework {

private:

    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    
    knst_vector<VkDescriptorSet> m_descriptorSets;         
knst_vector<VkDescriptorSet> m_textureDescriptorSets;   
knst_vector<const knst_texture*> m_lastBoundTexture;    
knst_texture m_dummyTexture;                             

void UpdateDescriptorSetInternal(VkDescriptorSet set, const knst_texture& texture);

   
    bool CreateDescriptorPool();
    bool CreateDescriptorSetLayout();
    bool AllocateDescriptorSet();
    void UpdateDescriptorSet(const knst_texture& texture, uint32_t frameIndex);

    struct PendingBufferDestroy {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };
    knst_vector<knst_vector<PendingBufferDestroy>> m_pendingVertexDestroys;
    knst_vector<knst_vector<PendingBufferDestroy>> m_pendingIndexDestroys;

    knst_window_vulkan_content* m_vk_content = nullptr;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;


    knst_vector<VkImage> m_swapchainImages;
    knst_vector<VkImageView> m_swapchainImageViews;
    knst_vector<VkFramebuffer> m_swapchainFramebuffers;
    VkFormat m_swapchainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapchainExtent{0, 0};

    knst_vector<VkCommandBuffer> m_commandBuffers;
    VkCommandBuffer m_currentCommandBuffer = VK_NULL_HANDLE;

   

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 4;
    static constexpr uint32_t MAX_SWAPCHAIN_IMAGES = 8;
    knst_vector<VkSemaphore> m_imageAvailableSemaphores;
    knst_vector<VkSemaphore> m_renderFinishedSemaphores;
    knst_vector<VkFence> m_inFlightFences;
    std::vector<VkFence> m_imagesInFlight;
    uint32_t m_currentFrame = 0;
    uint32_t m_currentImageIndex = 0;

  
    bool m_surfacePropsCached = false;
    VkSurfaceFormatKHR m_cachedSurfaceFormat{};
    VkPresentModeKHR m_cachedVsyncPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkPresentModeKHR m_cachedImmediatePresentMode = VK_PRESENT_MODE_FIFO_KHR;

   
    bool m_swapchainReady = false;

   
    uint32_t m_lastRequestedWidth = 0;
    uint32_t m_lastRequestedHeight = 0;
    std::chrono::steady_clock::time_point m_lastSizeChangeTime;

    
    knst_vector<VkBuffer> m_vertexBuffers;
    knst_vector<VkDeviceMemory> m_vertexBufferMemories;
    knst_vector<void*> m_vertexBufferMapped;
    knst_vector<VkDeviceSize> m_vertexBufferCapacity;
    VkDeviceSize                                     m_vertexWriteOffset = 0;  // galiba delirmeye başladım. bu boşluk bu kodun burasını okuyan herkese gelsin saygılarımla...

    knst_vector<VkBuffer>       m_indexBuffers;
    knst_vector<VkDeviceMemory> m_indexBufferMemories;
    knst_vector<void*>          m_indexBufferMapped;
    knst_vector<VkDeviceSize>   m_indexBufferCapacity;
    VkDeviceSize                m_indexWriteOffset = 0;

    
    PFN_vkCmdSetVertexInputEXT m_vkCmdSetVertexInputEXT = nullptr;
    bool m_vertexInputDynamicSupported = false;

    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;
    bool m_hasDepthAttachment = false;

    VkImage m_resolveImage = VK_NULL_HANDLE;
    VkDeviceMemory m_resolveImageMemory = VK_NULL_HANDLE;
    VkImageView m_resolveImageView = VK_NULL_HANDLE;
    bool m_hasResolveAttachment = false;

    knst_vector<VkImage> m_inputImages;
    knst_vector<VkDeviceMemory> m_inputImageMemories;
    knst_vector<VkImageView> m_inputImageViews;
    bool m_hasInputAttachments = false;

    KnstRenderPassConfig m_renderPassConfig;
    KnstViewportConfig m_viewportConfig = KnstViewportConfig::Fullscreen();
    KnstClearColor m_currentClearColor = KnstClearColor::Black();

    uint32_t m_renderPassAttachmentCount = 1;
    bool m_resizePending = false;
    KnstSwapchainConfig m_pendingResizeConfig;
    bool m_skipFrame = false;
    uint32_t m_resizeRetryCount = 0;
    static constexpr uint32_t MAX_RESIZE_RETRIES = 3;
    bool m_renderPass2Supported = false;
    bool SupportsRenderPass2() const { return m_renderPass2Supported; }

    
    bool CreateDepthResources();
    void CleanupDepthResources();
    bool CreateAuxiliaryResources();
    void CleanupAuxiliaryResources();

   
    bool CreatePersistentBuffers();
    bool AllocateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped);
                            
    void CleanupPersistentBuffers();
    void SetVertexInputFor(bool is2D);
    bool CheckVertexInputDynamicSupport();
    
public:
   bool CreateImageResources(VkImage& image, VkDeviceMemory& memory, VkImageView& imageView,VkFormat format, VkImageUsageFlags usage,VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT); 
                                              
                                              
   void SetViewportConfig(const KnstViewportConfig& config) {
        m_viewportConfig = config;
    }
    const KnstViewportConfig& GetViewportConfig() const {
        return m_viewportConfig;
    }   



   
    bool CreateRenderPass2(const KnstRenderPassConfig& config);
    
    // RenderPass1 düşük vulkan sürümü için kullanabilirsiniz
    bool CreateRenderPass1(const KnstRenderPassConfig& config);
    
    // RenderPass2 desteğini kontrol et her sistemde olmayabiliyor
    bool IsRenderPass2Supported() const;

    void Init(knst_window_vulkan_content *vk_content);
   
   
    bool QuerySurfaceStaticProperties(const KnstGuiSurfacePropConfig& config);

   
    bool InitFrameworkGui();
  
    bool CreateSwapchain(const KnstSwapchainConfig& config);

    void BeginFrame(const KnstSwapchainConfig& config, const KnstClearColor& clearColor);
    void EndFrame();
    void Draw(const KnstDrawConfig& config);
  
    bool CreateRenderPass(const KnstRenderPassConfig& config);

   
    static knst_byte_string ReadShaderFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            return knst_byte_string();
        }
        
        size_t fileSize = (size_t)file.tellg();
        unsigned char* data = new unsigned char[fileSize + 1];
        
        file.seekg(0);
        file.read(reinterpret_cast<char*>(data), fileSize);
        file.close();
        
        data[fileSize] = '\0';
        
        return knst_byte_string::take_ownership(data, static_cast<uint32_t>(fileSize));
    }


    VkShaderModule CreateShaderModule(const knst_byte_string& code) {
        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.length();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        
        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_vk_content->GetDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        return shaderModule;
    }

  
    bool CreateGraphicsPipeline(const KnstGraphicsPipelineConfig& config);

    bool CreateFramebuffers();

    bool CreateCommandPool();

    bool CreateCommandBuffers();

    bool CreateSyncObjects();

    bool RecreateSwapchainZeroWait(const KnstSwapchainConfig& config);
   
    void CleanupSwapchain();
   
    void Destroy();

    bool SetKnstGuiConfig(const KnstSwapchainConfig& swapConfig,const KnstGuiConfig& guiConfig) ;













    // getter setterlar burda


    knst_window_vulkan_content* GetVkContent() const { return m_vk_content; }
    
    VkDevice GetDevice() const { return m_vk_content ? m_vk_content->GetDevice() : VK_NULL_HANDLE; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_vk_content ? m_vk_content->GetPhysicalDevice() : VK_NULL_HANDLE; }
    VkSurfaceKHR GetSurface() const { return m_vk_content ? m_vk_content->GetSurface() : VK_NULL_HANDLE; }
    VkQueue GetGraphicsQueue() const { return m_vk_content ? m_vk_content->GetGraphicsQueue() : VK_NULL_HANDLE; }
    uint32_t GetGraphicsFamilyIndex() const { return m_vk_content ? m_vk_content->GetGraphicsFamilyIndex() : 0; }
    VkSwapchainKHR GetSwapchain() const { return m_swapchain; }
    VkFormat GetSwapchainFormat() const { return m_swapchainImageFormat; }
    VkExtent2D GetSwapchainExtent() const { return m_swapchainExtent; }
    uint32_t GetSwapchainImageCount() const { return (uint32_t)m_swapchainImages.size(); }
    VkImage GetSwapchainImage(uint32_t index) const { return index < m_swapchainImages.size() ? m_swapchainImages[index] : VK_NULL_HANDLE; }
    VkImageView GetSwapchainImageView(uint32_t index) const { return index < m_swapchainImageViews.size() ? m_swapchainImageViews[index] : VK_NULL_HANDLE; }
    VkFramebuffer GetSwapchainFramebuffer(uint32_t index) const { return index < m_swapchainFramebuffers.size() ? m_swapchainFramebuffers[index] : VK_NULL_HANDLE; }
    const knst_vector<VkImage>& GetSwapchainImages() const { return m_swapchainImages; }
    const knst_vector<VkImageView>& GetSwapchainImageViews() const { return m_swapchainImageViews; }
    const knst_vector<VkFramebuffer>& GetSwapchainFramebuffers() const { return m_swapchainFramebuffers; }
    
    bool IsSwapchainReady() const { return m_swapchainReady; }
    void SetSwapchainReady(bool ready) { m_swapchainReady = ready; }

    VkRenderPass GetRenderPass() const { return m_renderPass; }
    const KnstRenderPassConfig& GetRenderPassConfig() const { return m_renderPassConfig; }
    void SetRenderPassConfig(const KnstRenderPassConfig& config) { m_renderPassConfig = config; }
    uint32_t GetRenderPassAttachmentCount() const { return m_renderPassAttachmentCount; }

    VkPipeline GetGraphicsPipeline() const { return m_graphicsPipeline; }
    VkPipelineLayout GetPipelineLayout() const { return m_pipelineLayout; }
    void SetGraphicsPipeline(VkPipeline pipeline) { m_graphicsPipeline = pipeline; }
    void SetPipelineLayout(VkPipelineLayout layout) { m_pipelineLayout = layout; }

    VkCommandPool GetCommandPool() const { return m_commandPool; }
    VkCommandBuffer GetCurrentCommandBuffer() const { return m_currentCommandBuffer; }
    VkCommandBuffer GetCommandBuffer(uint32_t frameIndex) const { return frameIndex < m_commandBuffers.size() ? m_commandBuffers[frameIndex] : VK_NULL_HANDLE; }
    const knst_vector<VkCommandBuffer>& GetCommandBuffers() const { return m_commandBuffers; }

    uint32_t GetCurrentFrame() const { return m_currentFrame; }
    uint32_t GetCurrentImageIndex() const { return m_currentImageIndex; }
    void SetCurrentFrame(uint32_t frame) { m_currentFrame = frame; }
    
    VkSemaphore GetImageAvailableSemaphore(uint32_t frameIndex) const { return frameIndex < m_imageAvailableSemaphores.size() ? m_imageAvailableSemaphores[frameIndex] : VK_NULL_HANDLE; }
    VkSemaphore GetRenderFinishedSemaphore(uint32_t imageIndex) const { return imageIndex < m_renderFinishedSemaphores.size() ? m_renderFinishedSemaphores[imageIndex] : VK_NULL_HANDLE; }
    VkFence GetInFlightFence(uint32_t frameIndex) const { return frameIndex < m_inFlightFences.size() ? m_inFlightFences[frameIndex] : VK_NULL_HANDLE; }
    const knst_vector<VkSemaphore>& GetImageAvailableSemaphores() const { return m_imageAvailableSemaphores; }
    const knst_vector<VkSemaphore>& GetRenderFinishedSemaphores() const { return m_renderFinishedSemaphores; }
    const knst_vector<VkFence>& GetInFlightFences() const { return m_inFlightFences; }

    VkImage GetDepthImage() const { return m_depthImage; }
    VkDeviceMemory GetDepthImageMemory() const { return m_depthImageMemory; }
    VkImageView GetDepthImageView() const { return m_depthImageView; }
    bool HasDepthAttachment() const { return m_hasDepthAttachment; }
    void SetDepthImageView(VkImageView view) { m_depthImageView = view; }

   
    VkImage GetResolveImage() const { return m_resolveImage; }
    
    VkDeviceMemory GetResolveImageMemory() const { return m_resolveImageMemory; }
    
    
    VkImageView GetResolveImageView() const { return m_resolveImageView; }
    
    
    bool HasResolveAttachment() const { return m_hasResolveAttachment; }

    
    
    
    
    VkImage GetInputImage(uint32_t index) const { return index < m_inputImages.size() ? m_inputImages[index] : VK_NULL_HANDLE; }
    
    
    
    VkImageView GetInputImageView(uint32_t index) const { return index < m_inputImageViews.size() ? m_inputImageViews[index] : VK_NULL_HANDLE; }
    
    bool HasInputAttachments() const { return m_hasInputAttachments; }

    KnstClearColor GetClearColor() const { return m_currentClearColor; }



    void SetClearColor(const KnstClearColor& color) { m_currentClearColor = color; }


    
    
    VkBuffer GetVertexBuffer(uint32_t frameIndex) const { return frameIndex < m_vertexBuffers.size() ? m_vertexBuffers[frameIndex] : VK_NULL_HANDLE; }
    
    
    VkDeviceSize GetVertexBufferCapacity(uint32_t frameIndex) const { return frameIndex < m_vertexBufferCapacity.size() ? m_vertexBufferCapacity[frameIndex] : 0; }

    VkDeviceSize GetVertexWriteOffset() const { return m_vertexWriteOffset; }
    
    void SetVertexWriteOffset(VkDeviceSize offset) { m_vertexWriteOffset = offset; }

    
    VkBuffer GetIndexBuffer(uint32_t frameIndex) const { return frameIndex < m_indexBuffers.size() ? m_indexBuffers[frameIndex] : VK_NULL_HANDLE; }
    
    VkDeviceSize GetIndexBufferCapacity(uint32_t frameIndex) const { return frameIndex < m_indexBufferCapacity.size() ? m_indexBufferCapacity[frameIndex] : 0; }
    
    VkDeviceSize GetIndexWriteOffset() const { return m_indexWriteOffset; }
    
    void SetIndexWriteOffset(VkDeviceSize offset) { m_indexWriteOffset = offset; }

    bool IsVertexInputDynamicSupported() const { return m_vertexInputDynamicSupported; }
    PFN_vkCmdSetVertexInputEXT GetVertexInputFunction() const { return m_vkCmdSetVertexInputEXT; }

    bool IsResizePending() const { return m_resizePending; }
    
    void SetResizePending(bool pending) { m_resizePending = pending; }
    
    const KnstSwapchainConfig& GetPendingResizeConfig() const { return m_pendingResizeConfig; }
    
    void SetPendingResizeConfig(const KnstSwapchainConfig& config) { m_pendingResizeConfig = config; }
   
    bool IsSkipFrame() const { return m_skipFrame; }
    
    void SetSkipFrame(bool skip) { m_skipFrame = skip; }
    
    uint32_t GetResizeRetryCount() const { return m_resizeRetryCount; }
    
    void SetResizeRetryCount(uint32_t count) { m_resizeRetryCount = count; }



                                           
};


#include "knst_gui_framework_vulkan_definers.hpp"

#endif