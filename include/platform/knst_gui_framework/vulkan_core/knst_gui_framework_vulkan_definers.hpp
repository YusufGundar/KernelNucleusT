#pragma once


void knst_gui_framework::Init(knst_window_vulkan_content *vk_content){ //önemli kaynaklar temizleniyor
    m_vk_content = vk_content;
    m_lastBoundTexture.clear();
    m_resizePending = false;
    m_skipFrame = false;
    m_resizeRetryCount = 0;
    m_swapchain = VK_NULL_HANDLE;
    m_renderPass = VK_NULL_HANDLE;
    m_graphicsPipeline = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_commandPool = VK_NULL_HANDLE;
    m_pendingVertexDestroys.clear();
    m_pendingVertexDestroys.resize(MAX_FRAMES_IN_FLIGHT);
    m_pendingIndexDestroys.clear();
    m_pendingIndexDestroys.resize(MAX_FRAMES_IN_FLIGHT);
   

    m_depthImage = VK_NULL_HANDLE;
    m_depthImageMemory = VK_NULL_HANDLE;
    m_depthImageView = VK_NULL_HANDLE;
    m_resolveImage = VK_NULL_HANDLE;
    m_resolveImageMemory = VK_NULL_HANDLE;
    m_resolveImageView = VK_NULL_HANDLE;

    
    m_swapchainImages.clear();
    m_swapchainImageViews.clear();
    m_swapchainFramebuffers.clear();
    m_commandBuffers.clear();
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
    m_imagesInFlight.clear();
    m_inputImages.clear();
    m_inputImageMemories.clear();
    m_inputImageViews.clear();

   
    m_currentFrame = 0;
    m_swapchainReady = false;
    m_renderPass2Supported = IsRenderPass2Supported();
    m_hasInputAttachments = false;
    m_hasDepthAttachment = false;
    m_hasResolveAttachment = false;
    m_swapchainExtent = {0, 0};
    m_swapchainImageFormat = VK_FORMAT_UNDEFINED;
    m_currentCommandBuffer = VK_NULL_HANDLE;
    m_currentImageIndex = 0;
    m_renderPassAttachmentCount = 0;

    m_currentClearColor = KnstClearColor::Black();
}




bool knst_gui_framework::InitFrameworkGui(){
    if (!CreateAuxiliaryResources()) return false;
    if (!CreateFramebuffers()) return false;
    if (!CreateCommandPool()) return false;
    if (!CreateCommandBuffers()) return false;
    if (!CreateSyncObjects()) return false;
    if (!CreatePersistentBuffers()) return false;


    m_dummyTexture = knst_texture_loader::CreateEmpty(
        GetDevice(), GetPhysicalDevice(), GetCommandPool(), GetGraphicsQueue(), 1, 1);

    if (m_dummyTexture.IsValid()) {
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            UpdateDescriptorSetInternal(m_descriptorSets[i], m_dummyTexture);
        }
    }

    m_vertexInputDynamicSupported = CheckVertexInputDynamicSupport();
    m_swapchainReady = true;
    return true;
}



bool knst_gui_framework::CheckVertexInputDynamicSupport() {
    if (m_vk_content == nullptr || m_vk_content->GetDevice() == VK_NULL_HANDLE) {
        return false;
    }
    
    VkPhysicalDevice physicalDevice = m_vk_content->GetPhysicalDevice();
    
   
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    
    if (props.apiVersion >= VK_API_VERSION_1_3) {
        m_vkCmdSetVertexInputEXT = (PFN_vkCmdSetVertexInputEXT)vkGetDeviceProcAddr(
            m_vk_content->GetDevice(), "vkCmdSetVertexInputEXT");
        return m_vkCmdSetVertexInputEXT != nullptr;
    }
    
   
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    if (extensionCount == 0) return false;
    
    knst_vector<VkExtensionProperties> extensions;
    extensions.resize(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data());
    
    for (const auto& ext : extensions) {
        if (strcmp(ext.extensionName, VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME) == 0) {
            m_vkCmdSetVertexInputEXT = (PFN_vkCmdSetVertexInputEXT)vkGetDeviceProcAddr(
                m_vk_content->GetDevice(), "vkCmdSetVertexInputEXT");
            return m_vkCmdSetVertexInputEXT != nullptr;
        }
    }
    
    return false;
}


bool knst_gui_framework::AllocateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped) {
                                             
    VkDevice device = m_vk_content->GetDevice();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, buffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = m_vk_content->FindMemoryType(
        memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        return false;
    }
    
    vkBindBufferMemory(device, buffer, memory, 0);
    
    if (vkMapMemory(device, memory, 0, size, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        vkFreeMemory(device, memory, nullptr);
        return false;
    }
    
    return true;
}


bool knst_gui_framework::CreatePersistentBuffers() {
    m_vertexBuffers.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    m_vertexBufferMemories.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    m_vertexBufferMapped.resize(MAX_FRAMES_IN_FLIGHT, nullptr);
    m_vertexBufferCapacity.resize(MAX_FRAMES_IN_FLIGHT, 0);

    m_indexBuffers.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    m_indexBufferMemories.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    m_indexBufferMapped.resize(MAX_FRAMES_IN_FLIGHT, nullptr);
    m_indexBufferCapacity.resize(MAX_FRAMES_IN_FLIGHT, 0);

    const VkDeviceSize VB_SIZE = 8 * 1024 * 1024; // 8MB
    const VkDeviceSize IB_SIZE = 4 * 1024 * 1024; // 4MB

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (!AllocateHostBuffer(VB_SIZE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,m_vertexBuffers[i], m_vertexBufferMemories[i], m_vertexBufferMapped[i])) {
                                 
            return false;
        }
        m_vertexBufferCapacity[i] = VB_SIZE;

        if (!AllocateHostBuffer(IB_SIZE, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,m_indexBuffers[i], m_indexBufferMemories[i], m_indexBufferMapped[i])) {
                                 
            return false;
        }
        m_indexBufferCapacity[i] = IB_SIZE;
    }
    
    return true;
}

void knst_gui_framework::CleanupPersistentBuffers() {
    VkDevice device = m_vk_content->GetDevice();
    
    for (uint32_t i = 0; i < (uint32_t)m_vertexBuffers.size(); ++i) {
        if (m_vertexBufferMapped[i] != nullptr) {
            vkUnmapMemory(device, m_vertexBufferMemories[i]);
            m_vertexBufferMapped[i] = nullptr;
        }
        if (m_vertexBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, m_vertexBuffers[i], nullptr);
            m_vertexBuffers[i] = VK_NULL_HANDLE;
        }
        if (m_vertexBufferMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_vertexBufferMemories[i], nullptr);
            m_vertexBufferMemories[i] = VK_NULL_HANDLE;
        }
        m_vertexBufferCapacity[i] = 0;
    }
    m_vertexBuffers.clear();
    m_vertexBufferMemories.clear();
    m_vertexBufferMapped.clear();
    m_vertexBufferCapacity.clear();
    
    for (uint32_t i = 0; i < (uint32_t)m_indexBuffers.size(); ++i) {
        if (m_indexBufferMapped[i] != nullptr) {
            vkUnmapMemory(device, m_indexBufferMemories[i]);
            m_indexBufferMapped[i] = nullptr;
        }
        if (m_indexBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, m_indexBuffers[i], nullptr);
            m_indexBuffers[i] = VK_NULL_HANDLE;
        }
        if (m_indexBufferMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_indexBufferMemories[i], nullptr);
            m_indexBufferMemories[i] = VK_NULL_HANDLE;
        }
        m_indexBufferCapacity[i] = 0;
    }
    m_indexBuffers.clear();
    m_indexBufferMemories.clear();
    m_indexBufferMapped.clear();
    m_indexBufferCapacity.clear();

   
    for (auto& list : m_pendingVertexDestroys) {
        for (auto& pending : list) {
            if (pending.buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, pending.buffer, nullptr);
            }
            if (pending.memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, pending.memory, nullptr);
            }
        }
        list.clear();
    }
    m_pendingVertexDestroys.clear();
    
    for (auto& list : m_pendingIndexDestroys) {
        for (auto& pending : list) {
            if (pending.buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, pending.buffer, nullptr);
            }
            if (pending.memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, pending.memory, nullptr);
            }
        }
        list.clear();
    }
    m_pendingIndexDestroys.clear();
}

bool knst_gui_framework::IsRenderPass2Supported() const {
    if (m_vk_content == nullptr || m_vk_content->GetPhysicalDevice() == VK_NULL_HANDLE) {
        return false;
    }
    
    VkPhysicalDevice physicalDevice = m_vk_content->GetPhysicalDevice();
    
   
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    
 
    if (props.apiVersion >= VK_API_VERSION_1_2) {
        return true;
    }
    
   
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    if (extensionCount == 0) return false;
    
    knst_vector<VkExtensionProperties> extensions;
    extensions.resize(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data());
    
    for (const auto& ext : extensions) {
        if (strcmp(ext.extensionName, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME) == 0) {
            return true;
        }
    }
    
    return false;
}




bool knst_gui_framework::CreateRenderPass1(const KnstRenderPassConfig& config) {
   
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_vk_content->GetDevice(), m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    m_renderPassConfig = config;
    m_renderPassAttachmentCount = static_cast<uint32_t>(config.attachments.size());

   
    
    knst_vector<VkAttachmentDescription> attachments;

    for (const auto& att : config.attachments) {
        VkAttachmentDescription attachment = {};
        attachment.format = (att.format != VK_FORMAT_UNDEFINED)? att.format: m_swapchainImageFormat;
                           
                           
        attachment.samples = att.samples;
        attachment.loadOp = KnstToVulkanLoadOp(att.loadOp);
        attachment.storeOp = KnstToVulkanStoreOp(att.storeOp);
        attachment.stencilLoadOp = KnstToVulkanLoadOp(att.stencilLoadOp);
        attachment.stencilStoreOp = KnstToVulkanStoreOp(att.stencilStoreOp);
        attachment.initialLayout = KnstToVulkanImageLayout(att.initialLayout);
        attachment.finalLayout = KnstToVulkanImageLayout(att.finalLayout);
        attachments.push_back(attachment);
    }

    knst_vector<VkSubpassDescription> subpasses;
    knst_vector<knst_vector<VkAttachmentReference>> colorRefs;
    knst_vector<knst_vector<VkAttachmentReference>> inputRefs;
    knst_vector<knst_vector<VkAttachmentReference>> resolveRefs;
    knst_vector<VkAttachmentReference> depthRefs;
    knst_vector<knst_vector<uint32_t>> preserveRefs;

    for (const auto& sp : config.subpasses) {
        knst_vector<VkAttachmentReference> colorRef;
        for (const auto& ref : sp.colorAttachments) {
            VkAttachmentReference vkRef = {};
            vkRef.attachment = ref.attachment;
            vkRef.layout = KnstToVulkanImageLayout(ref.layout);
            colorRef.push_back(vkRef);
        }
        colorRefs.push_back(colorRef);

        knst_vector<VkAttachmentReference> inputRef;
        for (const auto& ref : sp.inputAttachments) {
            VkAttachmentReference vkRef = {};
            vkRef.attachment = ref.attachment;
            vkRef.layout = KnstToVulkanImageLayout(ref.layout);
            inputRef.push_back(vkRef);
        }
        inputRefs.push_back(inputRef);

        knst_vector<VkAttachmentReference> resolveRef;
        for (const auto& ref : sp.resolveAttachments) {
            VkAttachmentReference vkRef = {};
            vkRef.attachment = ref.attachment;
            vkRef.layout = KnstToVulkanImageLayout(ref.layout);
            resolveRef.push_back(vkRef);
        }
        resolveRefs.push_back(resolveRef);

        VkAttachmentReference depthRef = {};
        if (sp.hasDepthStencil) {
            depthRef.attachment = sp.depthStencilAttachment.attachment;
            depthRef.layout = KnstToVulkanImageLayout(sp.depthStencilAttachment.layout);
        }
        depthRefs.push_back(depthRef);

        preserveRefs.push_back(sp.preserveAttachments);
    }

    for (size_t i = 0; i < config.subpasses.size(); ++i) {
        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = config.subpasses[i].bindPoint;
        subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs[i].size());
        subpass.pColorAttachments = colorRefs[i].data();

        if (!inputRefs[i].empty()) {
            subpass.inputAttachmentCount = static_cast<uint32_t>(inputRefs[i].size());
            subpass.pInputAttachments = inputRefs[i].data();
        }

        if (!resolveRefs[i].empty()) {
            subpass.pResolveAttachments = resolveRefs[i].data();
        }

        if (config.subpasses[i].hasDepthStencil) {
            subpass.pDepthStencilAttachment = &depthRefs[i];
        }

        if (!preserveRefs[i].empty()) {
            subpass.preserveAttachmentCount = static_cast<uint32_t>(preserveRefs[i].size());
            subpass.pPreserveAttachments = preserveRefs[i].data();
        }

        subpasses.push_back(subpass);
    }

    knst_vector<VkSubpassDependency> dependencies;
    for (const auto& dep : config.dependencies) {
        VkSubpassDependency dependency = {};
        dependency.srcSubpass = dep.srcSubpass;
        dependency.dstSubpass = dep.dstSubpass;
        dependency.srcStageMask = dep.srcStageMask;
        dependency.dstStageMask = dep.dstStageMask;
        dependency.srcAccessMask = dep.srcAccessMask;
        dependency.dstAccessMask = dep.dstAccessMask;
        dependency.dependencyFlags = dep.byRegion ? VK_DEPENDENCY_BY_REGION_BIT : 0;
        dependencies.push_back(dependency);
    }

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
    renderPassInfo.pSubpasses = subpasses.data();
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(m_vk_content->GetDevice(), &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        return false;
    }

    return true;
}



bool knst_gui_framework::SetKnstGuiConfig(const KnstSwapchainConfig& swapConfig,
                                           const KnstGuiConfig& guiConfig) {

    KnstGuiSurfacePropConfig surfaceConfig = KnstGuiSurfacePropConfig::Default();
    if (!QuerySurfaceStaticProperties(surfaceConfig)) return false;

    if (!CreateSwapchain(swapConfig)) return false;

    KnstRenderPassConfig renderConfig = KnstRenderPassConfig::Default();
    if (!CreateRenderPass(renderConfig)) return false;


    if (!CreateDescriptorPool()) return false;
    if (!CreateDescriptorSetLayout()) return false;
    if (!AllocateDescriptorSet()) return false;

    KnstGraphicsPipelineConfig pipelineConfig = KnstGraphicsPipelineConfig::Default();
    pipelineConfig.rasterization.cullMode = guiConfig.cullMode;

    if (!guiConfig.enableBlend) {
        for (auto& att : pipelineConfig.colorBlend.attachments) {
            att.blendEnable = false;
        }
    }

    pipelineConfig.vertexInput.bindings.clear();
    pipelineConfig.vertexInput.attributes.clear();
    
    pipelineConfig.dynamicStates.push_back(KnstDynamicState::VERTEX_INPUT_EXT);

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushData);
    pipelineConfig.pushConstantRanges.push_back(pushRange);

   
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        pipelineConfig.descriptorSetLayouts.push_back(m_descriptorSetLayout);
    }

    pipelineConfig.vertexShaderCode = ReadShaderFile(guiConfig.vertexShaderPath);
    pipelineConfig.fragmentShaderCode = ReadShaderFile(guiConfig.fragmentShaderPath);

    if (pipelineConfig.vertexShaderCode.empty() || pipelineConfig.fragmentShaderCode.empty()) {
        return false;
    }

    if (!CreateGraphicsPipeline(pipelineConfig)) return false;

   
    if (!InitFrameworkGui()) return false;

    return true;
}

void knst_gui_framework::SetVertexInputFor(bool is2D) {
    if (m_vkCmdSetVertexInputEXT == nullptr) return;
    
        VkVertexInputBindingDescription2EXT binding{};
    binding.sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT;
    binding.binding = 0;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    binding.stride = is2D ? sizeof(KnstVertex2D) : sizeof(KnstVertex3D);
    binding.divisor = 1;

    VkVertexInputAttributeDescription2EXT attrs[8];
    for (auto& a : attrs) {
        a.sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT;
        a.pNext = nullptr;
    }

    if (is2D) {
        attrs[0].location = 0; attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(KnstVertex2D, x);

        attrs[1].location = 1; attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[1].offset = offsetof(KnstVertex2D, r);

        attrs[2].location = 2; attrs[2].binding = 0;
        attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[2].offset = offsetof(KnstVertex2D, u);

       
        attrs[3].location = 3; attrs[3].binding = 0;
        attrs[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[3].offset = offsetof(KnstVertex2D, x);

        attrs[4].location = 4; attrs[4].binding = 0;
        attrs[4].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[4].offset = offsetof(KnstVertex2D, x);

        attrs[5].location = 5; attrs[5].binding = 0;
        attrs[5].format = VK_FORMAT_R32G32B32A32_UINT;
        attrs[5].offset = offsetof(KnstVertex2D, x);

        attrs[6].location = 6; attrs[6].binding = 0;
        attrs[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[6].offset = offsetof(KnstVertex2D, x);

        attrs[7].location = 7; attrs[7].binding = 0;
        attrs[7].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[7].offset = offsetof(KnstVertex2D, x);

        m_vkCmdSetVertexInputEXT(m_currentCommandBuffer, 1, &binding, 8, attrs);
    } else {
       
        attrs[0].location = 0; attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(KnstVertex3D, x);

        attrs[1].location = 1; attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[1].offset = offsetof(KnstVertex3D, r);

        attrs[2].location = 2; attrs[2].binding = 0;
        attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[2].offset = offsetof(KnstVertex3D, u);

        attrs[3].location = 3; attrs[3].binding = 0;
        attrs[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[3].offset = offsetof(KnstVertex3D, nx);

        attrs[4].location = 4; attrs[4].binding = 0;
        attrs[4].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[4].offset = offsetof(KnstVertex3D, tx);

        attrs[5].location = 5; attrs[5].binding = 0;
        attrs[5].format = VK_FORMAT_R32G32B32A32_UINT;
        attrs[5].offset = offsetof(KnstVertex3D, boneIndices);

        attrs[6].location = 6; attrs[6].binding = 0;
        attrs[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[6].offset = offsetof(KnstVertex3D, boneWeights);

        attrs[7].location = 7; attrs[7].binding = 0;
        attrs[7].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[7].offset = offsetof(KnstVertex3D, customData);

        m_vkCmdSetVertexInputEXT(m_currentCommandBuffer, 1, &binding, 8, attrs);
    }
}


bool knst_gui_framework::CreateRenderPass2(const KnstRenderPassConfig& config) {
   
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_vk_content->GetDevice(), m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    m_renderPassConfig = config;
    m_renderPassAttachmentCount = static_cast<uint32_t>(config.attachments.size());

    
    knst_vector<VkAttachmentDescription2> attachments2;
    attachments2.reserve(config.attachments.size());

    for (const auto& att : config.attachments) {
        VkAttachmentDescription2 attachment2 = {};
        attachment2.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
        attachment2.pNext = nullptr;
        attachment2.flags = 0;
        attachment2.format = (att.format != VK_FORMAT_UNDEFINED) ? att.format : m_swapchainImageFormat;
        attachment2.samples = att.samples;
        attachment2.loadOp = KnstToVulkanLoadOp(att.loadOp);
        attachment2.storeOp = KnstToVulkanStoreOp(att.storeOp);
        attachment2.stencilLoadOp = KnstToVulkanLoadOp(att.stencilLoadOp);
        attachment2.stencilStoreOp = KnstToVulkanStoreOp(att.stencilStoreOp);
        attachment2.initialLayout = KnstToVulkanImageLayout(att.initialLayout);
        attachment2.finalLayout = KnstToVulkanImageLayout(att.finalLayout);
        attachments2.push_back(attachment2);
    }

    
    knst_vector<knst_vector<VkAttachmentReference2>> colorRefs2;
    knst_vector<knst_vector<VkAttachmentReference2>> inputRefs2;
    knst_vector<knst_vector<VkAttachmentReference2>> resolveRefs2;
    knst_vector<VkAttachmentReference2> depthRefs2;
    knst_vector<knst_vector<uint32_t>> preserveRefs;
    
    knst_vector<VkSubpassDescription2> subpasses2;
    subpasses2.reserve(config.subpasses.size());

    for (const auto& sp : config.subpasses) {
        
        knst_vector<VkAttachmentReference2> colorRef;
        for (const auto& ref : sp.colorAttachments) {
            VkAttachmentReference2 vkRef = {};
            vkRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
            vkRef.pNext = nullptr;
            vkRef.attachment = ref.attachment;
            vkRef.layout = KnstToVulkanImageLayout(ref.layout);
            vkRef.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            colorRef.push_back(vkRef);
        }
        colorRefs2.push_back(colorRef);

        
        knst_vector<VkAttachmentReference2> inputRef;
        for (const auto& ref : sp.inputAttachments) {
            VkAttachmentReference2 vkRef = {};
            vkRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
            vkRef.pNext = nullptr;
            vkRef.attachment = ref.attachment;
            vkRef.layout = KnstToVulkanImageLayout(ref.layout);
            vkRef.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            inputRef.push_back(vkRef);
        }
        inputRefs2.push_back(inputRef);

        
        knst_vector<VkAttachmentReference2> resolveRef;
        for (const auto& ref : sp.resolveAttachments) {
            VkAttachmentReference2 vkRef = {};
            vkRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
            vkRef.pNext = nullptr;
            vkRef.attachment = ref.attachment;
            vkRef.layout = KnstToVulkanImageLayout(ref.layout);
            vkRef.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            resolveRef.push_back(vkRef);
        }
        resolveRefs2.push_back(resolveRef);

        VkAttachmentReference2 depthRef = {};
        if (sp.hasDepthStencil) {
            depthRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
            depthRef.pNext = nullptr;
            depthRef.attachment = sp.depthStencilAttachment.attachment;
            depthRef.layout = KnstToVulkanImageLayout(sp.depthStencilAttachment.layout);
            depthRef.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        depthRefs2.push_back(depthRef);

        preserveRefs.push_back(sp.preserveAttachments);
    }

    for (size_t i = 0; i < config.subpasses.size(); ++i) {
        const auto& sp = config.subpasses[i];
        
        VkSubpassDescription2 subpass2 = {};
        subpass2.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
        subpass2.pNext = nullptr;
        subpass2.flags = 0;
        subpass2.pipelineBindPoint = sp.bindPoint;
        subpass2.viewMask = 0;
        
   
        subpass2.colorAttachmentCount = static_cast<uint32_t>(colorRefs2[i].size());
        subpass2.pColorAttachments = colorRefs2[i].data();
        
        
        if (!inputRefs2[i].empty()) {
            subpass2.inputAttachmentCount = static_cast<uint32_t>(inputRefs2[i].size());
            subpass2.pInputAttachments = inputRefs2[i].data();
        }
        
       
        if (!resolveRefs2[i].empty()) {
            subpass2.pResolveAttachments = resolveRefs2[i].data();
        }
        
        
        if (sp.hasDepthStencil) {
            subpass2.pDepthStencilAttachment = &depthRefs2[i];
        }
        
        
        if (!preserveRefs[i].empty()) {
            subpass2.preserveAttachmentCount = static_cast<uint32_t>(preserveRefs[i].size());
            subpass2.pPreserveAttachments = preserveRefs[i].data();
        }
        
        subpasses2.push_back(subpass2);
    }

    
    knst_vector<VkSubpassDependency2> dependencies2;
    dependencies2.reserve(config.dependencies.size());

    for (const auto& dep : config.dependencies) {
        VkSubpassDependency2 dependency2 = {};
        dependency2.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
        dependency2.pNext = nullptr;
        dependency2.srcSubpass = dep.srcSubpass;
        dependency2.dstSubpass = dep.dstSubpass;
        dependency2.srcStageMask = dep.srcStageMask;
        dependency2.dstStageMask = dep.dstStageMask;
        dependency2.srcAccessMask = dep.srcAccessMask;
        dependency2.dstAccessMask = dep.dstAccessMask;
        dependency2.dependencyFlags = dep.byRegion ? VK_DEPENDENCY_BY_REGION_BIT : 0;
        dependency2.viewOffset = 0;
        dependencies2.push_back(dependency2);
    }


    VkRenderPassCreateInfo2 renderPassInfo2 = {};
    renderPassInfo2.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
    renderPassInfo2.pNext = nullptr;
    renderPassInfo2.flags = 0;
    renderPassInfo2.attachmentCount = static_cast<uint32_t>(attachments2.size());
    renderPassInfo2.pAttachments = attachments2.data();
    renderPassInfo2.subpassCount = static_cast<uint32_t>(subpasses2.size());
    renderPassInfo2.pSubpasses = subpasses2.data();
    renderPassInfo2.dependencyCount = static_cast<uint32_t>(dependencies2.size());
    renderPassInfo2.pDependencies = dependencies2.data();
    renderPassInfo2.correlatedViewMaskCount = 0;
    renderPassInfo2.pCorrelatedViewMasks = nullptr;

  
    auto vkCreateRenderPass2 = (PFN_vkCreateRenderPass2)vkGetDeviceProcAddr(
        m_vk_content->GetDevice(),
        "vkCreateRenderPass2"
    );

    if (vkCreateRenderPass2 == nullptr) {
        return CreateRenderPass1(config);
    }

    if (vkCreateRenderPass2(m_vk_content->GetDevice(), &renderPassInfo2, nullptr, &m_renderPass) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool knst_gui_framework::QuerySurfaceStaticProperties(const KnstGuiSurfacePropConfig& config) {
    VkPhysicalDevice physicalDevice = m_vk_content->GetPhysicalDevice();
    VkSurfaceKHR surface = m_vk_content->GetSurface();

    uint32_t formatCount = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr) != VK_SUCCESS ||
        formatCount == 0) {
        return false;
    }

    knst_vector<VkSurfaceFormatKHR> formats;
    formats.resize(formatCount);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data()) != VK_SUCCESS) {
        return false;
    }

    VkFormat desiredFormat = KnstToVulkanFormat(config.formatType);
    VkColorSpaceKHR desiredColorSpace = KnstToVulkanColorSpace(config.colorSpace);

    m_cachedSurfaceFormat = formats[0];
    bool formatFound = false;

    for (const auto& format : formats) {
        if (format.format == desiredFormat &&
            format.colorSpace == desiredColorSpace) {
            m_cachedSurfaceFormat = format;
            formatFound = true;
            break;
        }
    }

    if (!formatFound) {
        VkFormat defaultFormat = VK_FORMAT_B8G8R8A8_SRGB;
        VkColorSpaceKHR defaultColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

        for (const auto& format : formats) {
            if (format.format == defaultFormat &&
                format.colorSpace == defaultColorSpace) {
                m_cachedSurfaceFormat = format;
                formatFound = true;
                break;
            }
        }

        if (!formatFound) {
            for (const auto& format : formats) {
                if (format.format == VK_FORMAT_B8G8R8A8_UNORM) {
                    m_cachedSurfaceFormat = format;
                    formatFound = true;
                    break;
                }
            }
        }

        if (!formatFound) {
            m_cachedSurfaceFormat = formats[0];
        }
    }

    uint32_t presentModeCount = 0;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr) != VK_SUCCESS ||
        presentModeCount == 0) {
        return false;
    }

    knst_vector<VkPresentModeKHR> presentModes;
    presentModes.resize(presentModeCount);
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data()) != VK_SUCCESS) {
        return false;
    }

    VkPresentModeKHR desiredVsyncMode = KnstToVulkanPresentMode(config.presentModeVsync);
    m_cachedVsyncPresentMode = VK_PRESENT_MODE_FIFO_KHR;

    for (auto mode : presentModes) {
        if (mode == desiredVsyncMode) {
            m_cachedVsyncPresentMode = mode;
            break;
        }
    }

    VkPresentModeKHR desiredImmediateMode = KnstToVulkanPresentMode(config.presentMode);
    m_cachedImmediatePresentMode = VK_PRESENT_MODE_FIFO_KHR;

    for (auto mode : presentModes) {
        if (mode == desiredImmediateMode) {
            m_cachedImmediatePresentMode = mode;
            break;
        }
    }

    return true;
}

bool knst_gui_framework::CreateSwapchain(const KnstSwapchainConfig& config) {
    VkDevice device = m_vk_content->GetDevice();
    VkSurfaceKHR surface = m_vk_content->GetSurface();
    VkPhysicalDevice physicalDevice = m_vk_content->GetPhysicalDevice();

    VkPresentModeKHR presentMode = config.vsync ? m_cachedVsyncPresentMode : m_cachedImmediatePresentMode;

    VkSurfaceCapabilitiesKHR capabilities{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities) != VK_SUCCESS) {
        return false;
    }

    if (config.width == 0 || config.height == 0)
        return false;

    uint32_t imageCount;
    if (config.customImageCount > 0) {
        imageCount = config.customImageCount;
    } else if (config.buffering == KnstSwapchainBuffering::AUTO) {
        imageCount = capabilities.minImageCount + 2;
    } else {
        imageCount = static_cast<uint32_t>(config.buffering);
    }

    if (imageCount < capabilities.minImageCount) {
        imageCount = capabilities.minImageCount;
    }
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
   
    if (imageCount > MAX_SWAPCHAIN_IMAGES) {
        imageCount = MAX_SWAPCHAIN_IMAGES;
    }

    VkExtent2D extent{};
    if (capabilities.currentExtent.width != UINT32_MAX) {
        extent = capabilities.currentExtent;
    } else {
        extent.width = std::max(capabilities.minImageExtent.width,std::min(capabilities.maxImageExtent.width,static_cast<uint32_t>(config.width)));
        extent.height = std::max(capabilities.minImageExtent.height,std::min(capabilities.maxImageExtent.height,static_cast<uint32_t>(config.height)));
                                          
    }

    if (extent.width == 0 || extent.height == 0)
        return false;

    VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (config.allowTransferSrc) imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (config.allowTransferDst) imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (config.allowStorage)     imageUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (config.allowSampled)     imageUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;

    VkCompositeAlphaFlagBitsKHR compositeAlpha = config.transparentWindow
        ? VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
        : VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    VkSurfaceTransformFlagBitsKHR preTransform = config.allowRotation
        ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
        : capabilities.currentTransform;

    VkSwapchainKHR oldSwapchain = m_swapchain;

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = m_cachedSurfaceFormat.format;
    swapchainInfo.imageColorSpace = m_cachedSurfaceFormat.colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = imageUsage;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = preTransform;
    swapchainInfo.compositeAlpha = compositeAlpha;
    swapchainInfo.presentMode = presentMode;
    swapchainInfo.clipped = config.clipped ? VK_TRUE : VK_FALSE;
    swapchainInfo.oldSwapchain = oldSwapchain;

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &newSwapchain) != VK_SUCCESS) {
        return false;
    }

   
    VkExtent2D oldExtent = m_swapchainExtent;
    VkFormat oldFormat = m_swapchainImageFormat;

    m_swapchain = newSwapchain;
    m_swapchainImageFormat = m_cachedSurfaceFormat.format;
    m_swapchainExtent = extent;

    uint32_t newImageCount = 0;
    if (vkGetSwapchainImagesKHR(device, m_swapchain, &newImageCount, nullptr) != VK_SUCCESS || newImageCount == 0) {
        vkDestroySwapchainKHR(device, m_swapchain, nullptr);
        m_swapchain = oldSwapchain;
        m_swapchainExtent = oldExtent;
        m_swapchainImageFormat = oldFormat;
        return false;
    }

    m_swapchainImages.clear();
    m_swapchainImages.resize(newImageCount);

    if (vkGetSwapchainImagesKHR(device, m_swapchain, &newImageCount, m_swapchainImages.data()) != VK_SUCCESS) {
        vkDestroySwapchainKHR(device, m_swapchain, nullptr);
        m_swapchain = oldSwapchain;
        m_swapchainExtent = oldExtent;
        m_swapchainImageFormat = oldFormat;
        return false;
    }

    m_swapchainImageViews.clear();
    m_swapchainImageViews.resize(newImageCount);

    for (uint32_t i = 0; i < newImageCount; ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_swapchainImageFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS) {
            for (auto view : m_swapchainImageViews) {
                if (view != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, view, nullptr);
                }
            }
            m_swapchainImageViews.clear();
            vkDestroySwapchainKHR(device, m_swapchain, nullptr);
            m_swapchain = oldSwapchain;
            m_swapchainExtent = oldExtent;
            m_swapchainImageFormat = oldFormat;
            return false;
        }
    }

    return true;
}

bool knst_gui_framework::CreateRenderPass(const KnstRenderPassConfig& config) {

    if (config.useRenderPass2 && m_renderPass2Supported) {
        
        return CreateRenderPass2(config);
    } else {
        
        return CreateRenderPass1(config);
    }
}

bool knst_gui_framework::CreateDepthResources() {
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;

    for (const auto& att : m_renderPassConfig.attachments) {
        if (att.type == KnstAttachmentType::DEPTH ||
            att.type == KnstAttachmentType::DEPTH_STENCIL) {
            if (att.format != VK_FORMAT_UNDEFINED) {
                depthFormat = att.format;
            }
            break;
        }
    }

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_swapchainExtent.width;
    imageInfo.extent.height = m_swapchainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_vk_content->GetDevice(), &imageInfo, nullptr, &m_depthImage) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_vk_content->GetDevice(), m_depthImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_vk_content->FindMemoryType(memRequirements.memoryTypeBits,
                                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_vk_content->GetDevice(), &allocInfo, nullptr, &m_depthImageMemory) != VK_SUCCESS) {
        return false;
    }

    vkBindImageMemory(m_vk_content->GetDevice(), m_depthImage, m_depthImageMemory, 0);

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;

    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        depthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
        depthFormat == VK_FORMAT_D16_UNORM_S8_UINT) {
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_vk_content->GetDevice(), &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
        return false;
    }

    return true;
}

void knst_gui_framework::CleanupDepthResources() {
    if (m_depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_vk_content->GetDevice(), m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_vk_content->GetDevice(), m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
    }
    if (m_depthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_vk_content->GetDevice(), m_depthImageMemory, nullptr);
        m_depthImageMemory = VK_NULL_HANDLE;
    }
}

bool knst_gui_framework::CreateGraphicsPipeline(const KnstGraphicsPipelineConfig& config) {
    if (m_graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_vk_content->GetDevice(), m_graphicsPipeline, nullptr);
        m_graphicsPipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_vk_content->GetDevice(), m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    knst_vector<VkPipelineShaderStageCreateInfo> shaderStages;
    knst_vector<VkShaderModule> shaderModules;

    if (!config.vertexShaderCode.empty()) {
        VkShaderModule vertModule = CreateShaderModule(config.vertexShaderCode);
        if (vertModule == VK_NULL_HANDLE) {
            for (auto& m : shaderModules) { if (m != VK_NULL_HANDLE) vkDestroyShaderModule(m_vk_content->GetDevice(), m, nullptr); }
            return false;
        }
        VkPipelineShaderStageCreateInfo vertStage = {};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "main";
        shaderStages.push_back(vertStage);
        shaderModules.push_back(vertModule);
    }

    if (!config.fragmentShaderCode.empty()) {
        VkShaderModule fragModule = CreateShaderModule(config.fragmentShaderCode);
        if (fragModule == VK_NULL_HANDLE) {
            for (auto& m : shaderModules) { if (m != VK_NULL_HANDLE) vkDestroyShaderModule(m_vk_content->GetDevice(), m, nullptr); }
            return false;
        }
        VkPipelineShaderStageCreateInfo fragStage = {};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;
        fragStage.pName = "main";
        shaderStages.push_back(fragStage);
        shaderModules.push_back(fragModule);
    }

    if (!config.geometryShaderCode.empty()) {
        VkShaderModule geomModule = CreateShaderModule(config.geometryShaderCode);
        if (geomModule == VK_NULL_HANDLE) {
            for (auto& m : shaderModules) { if (m != VK_NULL_HANDLE) vkDestroyShaderModule(m_vk_content->GetDevice(), m, nullptr); }
            return false;
        }
        VkPipelineShaderStageCreateInfo geomStage = {};
        geomStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        geomStage.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        geomStage.module = geomModule;
        geomStage.pName = "main";
        shaderStages.push_back(geomStage);
        shaderModules.push_back(geomModule);
    }

    if (!config.tessellationControlShaderCode.empty()) {
        VkShaderModule tessCtrlModule = CreateShaderModule(config.tessellationControlShaderCode);
        if (tessCtrlModule == VK_NULL_HANDLE) {
            for (auto& m : shaderModules) { if (m != VK_NULL_HANDLE) vkDestroyShaderModule(m_vk_content->GetDevice(), m, nullptr); }
            return false;
        }
        VkPipelineShaderStageCreateInfo tessCtrlStage = {};
        tessCtrlStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        tessCtrlStage.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        tessCtrlStage.module = tessCtrlModule;
        tessCtrlStage.pName = "main";
        shaderStages.push_back(tessCtrlStage);
        shaderModules.push_back(tessCtrlModule);
    }

    if (!config.tessellationEvaluationShaderCode.empty()) {
        VkShaderModule tessEvalModule = CreateShaderModule(config.tessellationEvaluationShaderCode);
        if (tessEvalModule == VK_NULL_HANDLE) {
            for (auto& m : shaderModules) { if (m != VK_NULL_HANDLE) vkDestroyShaderModule(m_vk_content->GetDevice(), m, nullptr); }
            return false;
        }
        VkPipelineShaderStageCreateInfo tessEvalStage = {};
        tessEvalStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        tessEvalStage.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        tessEvalStage.module = tessEvalModule;
        tessEvalStage.pName = "main";
        shaderStages.push_back(tessEvalStage);
        shaderModules.push_back(tessEvalModule);
    }

    if (shaderStages.empty()) {
        for (auto& m : shaderModules) { if (m != VK_NULL_HANDLE) vkDestroyShaderModule(m_vk_content->GetDevice(), m, nullptr); }
        return false;
    }


    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = static_cast<VkPrimitiveTopology>(config.topology);
    inputAssembly.primitiveRestartEnable = config.primitiveRestartEnable;

    VkPipelineTessellationStateCreateInfo tessellation = {};
    bool hasTessellation = config.patchControlPoints > 0;
    if (hasTessellation) {
        tessellation.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
        tessellation.patchControlPoints = config.patchControlPoints;
    }

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkViewport viewport = config.viewport;
    VkRect2D scissor = config.scissor;

    if (!config.dynamicViewport) {
        viewportState.pViewports = &viewport;
    }
    if (!config.dynamicScissor) {
        viewportState.pScissors = &scissor;
    }
    // kullanıcıya özelleştirilebilir imkan sunmak önceliğim , ben belki tamamen full vulkan bilemeyebilirim ancak bilen kullanıcı istediğini değiştirebilmeli kütüphanede , esneklik ön planda ayrıca performans
    

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = config.rasterization.depthClampEnable;
    rasterizer.rasterizerDiscardEnable = config.rasterization.rasterizerDiscardEnable;
    rasterizer.polygonMode = static_cast<VkPolygonMode>(config.rasterization.polygonMode);
    rasterizer.cullMode = static_cast<VkCullModeFlags>(config.rasterization.cullMode);
    rasterizer.frontFace = static_cast<VkFrontFace>(config.rasterization.frontFace);
    rasterizer.depthBiasEnable = config.rasterization.depthBiasEnable;
    rasterizer.depthBiasConstantFactor = config.rasterization.depthBiasConstantFactor;
    rasterizer.depthBiasClamp = config.rasterization.depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = config.rasterization.depthBiasSlopeFactor;
    rasterizer.lineWidth = config.rasterization.lineWidth;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = config.multisample.rasterizationSamples;
    multisampling.sampleShadingEnable = config.multisample.sampleShadingEnable;
    multisampling.minSampleShading = config.multisample.minSampleShading;
    multisampling.pSampleMask = static_cast<const VkSampleMask*>(config.multisample.pSampleMask);
    multisampling.alphaToCoverageEnable = config.multisample.alphaToCoverageEnable;
    multisampling.alphaToOneEnable = config.multisample.alphaToOneEnable;

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = config.depthStencil.depthTestEnable;
    depthStencil.depthWriteEnable = config.depthStencil.depthWriteEnable;
    depthStencil.depthCompareOp = static_cast<VkCompareOp>(config.depthStencil.depthCompareOp);
    depthStencil.depthBoundsTestEnable = config.depthStencil.depthBoundsTestEnable;
    depthStencil.stencilTestEnable = config.depthStencil.stencilTestEnable;
    depthStencil.minDepthBounds = config.depthStencil.minDepthBounds;
    depthStencil.maxDepthBounds = config.depthStencil.maxDepthBounds;
    depthStencil.front = {};
    depthStencil.back = {};
    depthStencil.front.failOp = static_cast<VkStencilOp>(config.depthStencil.front.failOp);
    depthStencil.front.passOp = static_cast<VkStencilOp>(config.depthStencil.front.passOp);
    depthStencil.front.depthFailOp = static_cast<VkStencilOp>(config.depthStencil.front.depthFailOp);
    depthStencil.front.compareOp = static_cast<VkCompareOp>(config.depthStencil.front.compareOp);
    depthStencil.front.compareMask = config.depthStencil.front.compareMask;
    depthStencil.front.writeMask = config.depthStencil.front.writeMask;
    depthStencil.front.reference = config.depthStencil.front.reference;
    depthStencil.back.failOp = static_cast<VkStencilOp>(config.depthStencil.back.failOp);
    depthStencil.back.passOp = static_cast<VkStencilOp>(config.depthStencil.back.passOp);
    depthStencil.back.depthFailOp = static_cast<VkStencilOp>(config.depthStencil.back.depthFailOp);
    depthStencil.back.compareOp = static_cast<VkCompareOp>(config.depthStencil.back.compareOp);
    depthStencil.back.compareMask = config.depthStencil.back.compareMask;
    depthStencil.back.writeMask = config.depthStencil.back.writeMask;
    depthStencil.back.reference = config.depthStencil.back.reference;

    knst_vector<VkPipelineColorBlendAttachmentState> blendAttachments;
    for (const auto& att : config.colorBlend.attachments) {





        VkPipelineColorBlendAttachmentState blendAtt = {};
        blendAtt.blendEnable = att.blendEnable;
        blendAtt.srcColorBlendFactor = static_cast<VkBlendFactor>(att.srcColorBlendFactor);
        blendAtt.dstColorBlendFactor = static_cast<VkBlendFactor>(att.dstColorBlendFactor);
        blendAtt.colorBlendOp = static_cast<VkBlendOp>(att.colorBlendOp);
        blendAtt.srcAlphaBlendFactor = static_cast<VkBlendFactor>(att.srcAlphaBlendFactor);
        blendAtt.dstAlphaBlendFactor = static_cast<VkBlendFactor>(att.dstAlphaBlendFactor);
        blendAtt.alphaBlendOp = static_cast<VkBlendOp>(att.alphaBlendOp);
        blendAtt.colorWriteMask = att.colorWriteMask;
        blendAttachments.push_back(blendAtt);




    }



    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = config.colorBlend.logicOpEnable;
    colorBlending.logicOp = config.colorBlend.logicOp;
    colorBlending.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    colorBlending.pAttachments = blendAttachments.data();
    colorBlending.blendConstants[0] = config.colorBlend.blendConstants[0];
    colorBlending.blendConstants[1] = config.colorBlend.blendConstants[1];
    colorBlending.blendConstants[2] = config.colorBlend.blendConstants[2];
    colorBlending.blendConstants[3] = config.colorBlend.blendConstants[3];

    knst_vector<VkDynamicState> dynamicStates;
    for (const auto& state : config.dynamicStates) {
        dynamicStates.push_back(static_cast<VkDynamicState>(state));
    }

    VkPipelineDynamicStateCreateInfo dynamicState = {};
    if (!dynamicStates.empty()) {
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(config.descriptorSetLayouts.size());

    pipelineLayoutInfo.pSetLayouts = config.descriptorSetLayouts.data();

    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(config.pushConstantRanges.size());


    pipelineLayoutInfo.pPushConstantRanges = config.pushConstantRanges.data();

    if (vkCreatePipelineLayout(m_vk_content->GetDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        for (auto& m : shaderModules) { if (m != VK_NULL_HANDLE) vkDestroyShaderModule(m_vk_content->GetDevice(), m, nullptr); }
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pTessellationState = hasTessellation ? &tessellation : nullptr;
    pipelineInfo.pViewportState = &viewportState;

    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;

    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = !dynamicStates.empty() ? &dynamicState : nullptr;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = config.subpassIndex;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkResult result = vkCreateGraphicsPipelines(
        m_vk_content->GetDevice(),
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &m_graphicsPipeline
    );

    for (auto& m : shaderModules) {
        if (m != VK_NULL_HANDLE) vkDestroyShaderModule(m_vk_content->GetDevice(), m, nullptr);
    }

    if (result != VK_SUCCESS) {
        if (m_pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_vk_content->GetDevice(), m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
        }
        return false;
    }

    return true;
}

bool knst_gui_framework::CreateFramebuffers() {
    if (m_swapchainImageViews.empty()) return false;

    for (auto fb : m_swapchainFramebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_vk_content->GetDevice(), fb, nullptr);
        }
    }
    m_swapchainFramebuffers.clear();
    m_swapchainFramebuffers.resize(m_swapchainImageViews.size());

        for (uint32_t i = 0; i < m_swapchainImageViews.size(); ++i) {
        VkImageView attachments[8];
        uint32_t attachmentCount = 0;
        bool firstColorUsed = false;

       
        for (const auto& att : m_renderPassConfig.attachments) {
            if (attachmentCount >= 8) break;
            if (att.type == KnstAttachmentType::COLOR) {
                if (!firstColorUsed) {
                    attachments[attachmentCount++] = m_swapchainImageViews[i];
                    firstColorUsed = true;
                }
            } else if (att.type == KnstAttachmentType::DEPTH ||att.type == KnstAttachmentType::DEPTH_STENCIL) {
                if (m_depthImageView != VK_NULL_HANDLE) {
                    attachments[attachmentCount++] = m_depthImageView;
                }
            } else if (att.type == KnstAttachmentType::RESOLVE) {
                if (m_resolveImageView != VK_NULL_HANDLE) {
                    attachments[attachmentCount++] = m_resolveImageView;
                }
            } else if (att.type == KnstAttachmentType::INPUT) {
                for (uint32_t ii = 0; ii < m_inputImageViews.size() && attachmentCount < 8; ii++) {
                    attachments[attachmentCount++] = m_inputImageViews[ii];
                }
            }
        }

        if (!firstColorUsed && attachmentCount < 8) {
            for (uint32_t k = attachmentCount; k > 0; k--) attachments[k] = attachments[k-1];
            attachments[0] = m_swapchainImageViews[i];
            attachmentCount++;
        }

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = attachmentCount;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapchainExtent.width;
        framebufferInfo.height = m_swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_vk_content->GetDevice(), &framebufferInfo, nullptr, &m_swapchainFramebuffers[i]) != VK_SUCCESS) {
                                 
            return false;
        }
    }
    return true;
}

bool knst_gui_framework::CreateCommandPool() {
    if (m_commandPool != VK_NULL_HANDLE) {

        vkDestroyCommandPool(m_vk_content->GetDevice(), m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;

    }

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;

    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    poolInfo.queueFamilyIndex = m_vk_content->GetGraphicsFamilyIndex();

    return vkCreateCommandPool(m_vk_content->GetDevice(), &poolInfo, nullptr, &m_commandPool) == VK_SUCCESS;
}

bool knst_gui_framework::CreateCommandBuffers() {
    if (m_commandPool == VK_NULL_HANDLE) return false;

    m_commandBuffers.clear();
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();

    return vkAllocateCommandBuffers(m_vk_content->GetDevice(), &allocInfo, m_commandBuffers.data()) == VK_SUCCESS;
}


bool knst_gui_framework::CreateSyncObjects() {
    VkDevice device = m_vk_content->GetDevice();

    for (auto sem : m_imageAvailableSemaphores) if (sem) vkDestroySemaphore(device, sem, nullptr);
    for (auto sem : m_renderFinishedSemaphores) if (sem) vkDestroySemaphore(device, sem, nullptr);

    for (auto fence : m_inFlightFences) if (fence) vkDestroyFence(device, fence, nullptr);

    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
    m_imagesInFlight.clear();

    
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    
    
    m_renderFinishedSemaphores.resize(MAX_SWAPCHAIN_IMAGES);
    
   
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

 
    uint32_t imageCount = (uint32_t)m_swapchainImages.size();
    if (imageCount == 0) imageCount = 4;
    m_imagesInFlight.resize(imageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

   
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            return false;
        }
    }

    
    for (uint32_t i = 0; i < MAX_SWAPCHAIN_IMAGES; ++i) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

void knst_gui_framework::BeginFrame(const KnstSwapchainConfig& config, const KnstClearColor& clearColor) {
    auto sendSyncAckIfPending = [this]() {
        #if KNST_USING_LINUX_PLATFORM_X11
        if (m_vk_content && m_vk_content->m_window && m_vk_content->m_window->m_syncHasPendingValue) {
            xcb_sync_int64_t value = m_vk_content->m_window->m_syncPendingValue;
            xcb_sync_set_counter(
                KnstWindowSources::get_native_x11_connection_handle(),
                m_vk_content->m_window->m_syncCounter, value);
            if (m_vk_content->m_window->m_syncRequestReceived) {
                xcb_flush(KnstWindowSources::get_native_x11_connection_handle());
                m_vk_content->m_window->m_syncRequestReceived = false;
            }
            m_vk_content->m_window->m_syncHasPendingValue = false;
            m_vk_content->m_window->m_syncPendingValue = value;
        }
        #endif
    };

    if (m_vk_content == nullptr || m_vk_content->GetDevice() == VK_NULL_HANDLE || m_renderPass == VK_NULL_HANDLE || m_graphicsPipeline == VK_NULL_HANDLE ||
        config.width == 0 || config.height == 0) {
       
        sendSyncAckIfPending();
        m_currentCommandBuffer = VK_NULL_HANDLE;
        return;
    }

    m_currentClearColor = clearColor;

    VkSurfaceCapabilitiesKHR liveCaps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        m_vk_content->GetPhysicalDevice(), m_vk_content->GetSurface(), &liveCaps);

    VkExtent2D liveExtent = liveCaps.currentExtent;
    if (liveExtent.width == UINT32_MAX) {
        liveExtent.width = (uint32_t)config.width;
        liveExtent.height = (uint32_t)config.height;
    }

    bool needsRecreate = !m_swapchainReady || m_swapchain == VK_NULL_HANDLE ||m_swapchainExtent.width != liveExtent.width ||m_swapchainExtent.height != liveExtent.height;
    if (needsRecreate) {
       
        m_pendingResizeConfig = config;
        if (!m_resizePending) {
            m_resizePending = true;
            m_resizeRetryCount = 0;
        }
    }

    if (m_resizePending) {
        bool allFencesReady = true;
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (m_inFlightFences[i] != VK_NULL_HANDLE) {
                VkResult fenceStatus = vkGetFenceStatus(m_vk_content->GetDevice(), m_inFlightFences[i]);
                if (fenceStatus == VK_NOT_READY) {
                    allFencesReady = false;
                    break;
                }
            }
        }

        if (!allFencesReady) {
           
            m_skipFrame = true;
            m_currentCommandBuffer = VK_NULL_HANDLE;
            return;
        }

        if (!RecreateSwapchainZeroWait(m_pendingResizeConfig)) {
            if (m_resizeRetryCount < MAX_RESIZE_RETRIES) {
                m_resizeRetryCount++;
            } else {
                m_resizeRetryCount = 0;
            }
           
            m_skipFrame = true;
            m_currentFrame++;
            m_currentCommandBuffer = VK_NULL_HANDLE;
            return;
        }

      
        m_resizePending = false;
        m_resizeRetryCount = 0;
       
        m_skipFrame = false;
    }

    if (m_skipFrame) {
        m_skipFrame = false;
        m_currentFrame++;
       
        m_currentCommandBuffer = VK_NULL_HANDLE;
        return;
    }

    if (m_swapchainFramebuffers.empty() || m_currentImageIndex >= m_swapchainFramebuffers.size()) {
       
        m_currentCommandBuffer = VK_NULL_HANDLE;
        return;
    }

    uint32_t frameIndex = m_currentFrame % MAX_FRAMES_IN_FLIGHT;
    VkFence currentFence = m_inFlightFences[frameIndex];
    VkResult waitResult = vkWaitForFences(m_vk_content->GetDevice(), 1, &currentFence, VK_TRUE, 16'000'000ULL);
    if (waitResult == VK_TIMEOUT) {
       
        m_currentCommandBuffer = VK_NULL_HANDLE;
        return;
    }
    if (waitResult != VK_SUCCESS) {
        m_currentCommandBuffer = VK_NULL_HANDLE;
        return;
    }

    VkResult result = vkAcquireNextImageKHR(
        m_vk_content->GetDevice(),
        m_swapchain,
        UINT64_MAX,
        m_imageAvailableSemaphores[frameIndex],
        VK_NULL_HANDLE,
        &m_currentImageIndex
    );

    if (result == VK_TIMEOUT || result == VK_NOT_READY) {
        m_currentCommandBuffer = VK_NULL_HANDLE;
        return;
    }

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        m_resizePending = true;
        m_skipFrame = true;
        m_currentCommandBuffer = VK_NULL_HANDLE;
        return;
    }
    if (result == VK_SUBOPTIMAL_KHR) {
        m_resizePending = true;
        m_resizeRetryCount = 0;
    }

    if ((result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) ||
        m_currentImageIndex >= m_swapchainFramebuffers.size()) {
        sendSyncAckIfPending();
        m_currentCommandBuffer = VK_NULL_HANDLE;
        return;
    }

    
    if (m_currentImageIndex < m_imagesInFlight.size() &&
        m_imagesInFlight[m_currentImageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(m_vk_content->GetDevice(), 1, &m_imagesInFlight[m_currentImageIndex], VK_TRUE, UINT64_MAX);
                       
    }

    for (auto& pending : m_pendingVertexDestroys[frameIndex]) {
        if (pending.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_vk_content->GetDevice(), pending.buffer, nullptr);
        }
        if (pending.memory != VK_NULL_HANDLE) {
            vkFreeMemory(m_vk_content->GetDevice(), pending.memory, nullptr);
        }
    }
    m_pendingVertexDestroys[frameIndex].clear();

    for (auto& pending : m_pendingIndexDestroys[frameIndex]) {
        if (pending.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_vk_content->GetDevice(), pending.buffer, nullptr);
        }
        if (pending.memory != VK_NULL_HANDLE) {
            vkFreeMemory(m_vk_content->GetDevice(), pending.memory, nullptr);
        }
    }
    m_pendingIndexDestroys[frameIndex].clear();

    vkResetFences(m_vk_content->GetDevice(), 1, &currentFence);
    m_imagesInFlight[m_currentImageIndex] = currentFence;

    VkCommandBuffer commandBuffer = m_commandBuffers[frameIndex];
    m_currentCommandBuffer = commandBuffer;

    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapchainFramebuffers[m_currentImageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchainExtent;

    VkClearValue clearValues[8];
    uint32_t clearCount = 0;

    for (const auto& att : m_renderPassConfig.attachments) {
        if (att.loadOp == KnstAttachmentLoadOp::CLEAR && clearCount < 8) {
            if (att.type == KnstAttachmentType::DEPTH ||att.type == KnstAttachmentType::DEPTH_STENCIL) {
                clearValues[clearCount].depthStencil = {1.0f, 0};
            } else {
                clearValues[clearCount].color = {{
                    m_currentClearColor.r,
                    m_currentClearColor.g,
                    m_currentClearColor.b,
                    m_currentClearColor.a
                }};
            }
            clearCount++;
        }
    }

    if (clearCount == 0) {
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clearCount = 1;
    }

    renderPassInfo.clearValueCount = clearCount;
    renderPassInfo.pClearValues = clearValues;

    m_vertexWriteOffset = 0;
    m_indexWriteOffset = 0;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

    VkViewport viewportState{};
    viewportState.x = m_viewportConfig.x;
    viewportState.y = m_viewportConfig.y;
    viewportState.minDepth = m_viewportConfig.minDepth;
    viewportState.maxDepth = m_viewportConfig.maxDepth;

    float viewportWidth, viewportHeight;
    if (m_viewportConfig.autoSize) {
        viewportWidth = (float)m_swapchainExtent.width;
        viewportHeight = (float)m_swapchainExtent.height;
    } else {
        viewportWidth = m_viewportConfig.width;
        viewportHeight = m_viewportConfig.height;
    }

    if (m_viewportConfig.preserveAspectRatio && m_viewportConfig.aspectRatio > 0.0f) {
        float currentAspect = viewportWidth / viewportHeight;
        float targetAspect = m_viewportConfig.aspectRatio;
        if (currentAspect > targetAspect) {
            float newWidth = viewportHeight * targetAspect;
            float offsetX = (viewportWidth - newWidth) * 0.5f;
            viewportState.x += offsetX;
            viewportState.width = newWidth;
            viewportState.height = viewportHeight;
        } else {
            float newHeight = viewportWidth / targetAspect;
            float offsetY = (viewportHeight - newHeight) * 0.5f;
            viewportState.y += offsetY;
            viewportState.width = viewportWidth;
            viewportState.height = newHeight;
        }
    } else {
        viewportState.width = viewportWidth;
        viewportState.height = viewportHeight;
    }

    vkCmdSetViewport(commandBuffer, 0, 1, &viewportState);

    VkRect2D scissor{};
    if (m_viewportConfig.autoScissor) {
        scissor.offset = {0, 0};
        scissor.extent = m_swapchainExtent;
    } else {
        if (m_viewportConfig.preserveAspectRatio && m_viewportConfig.aspectRatio > 0.0f) {
            scissor.offset.x = (int32_t)viewportState.x;
            scissor.offset.y = (int32_t)viewportState.y;
            scissor.extent.width = (uint32_t)viewportState.width;
            scissor.extent.height = (uint32_t)viewportState.height;
        } else {
            scissor.offset = {m_viewportConfig.scissorX, m_viewportConfig.scissorY};
            scissor.extent = {m_viewportConfig.scissorWidth, m_viewportConfig.scissorHeight};
        }
    }
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void knst_gui_framework::Draw(const KnstDrawConfig& config) { // vertex ve index verilerini gpu ya kopyalar vk cmd draw çağrılır tabi önce begin frame ile başlatmanız lazım bunu begin ve end frame arasında çağırmanız gerekir
    if (config.Empty() || m_currentCommandBuffer == VK_NULL_HANDLE) return;

    uint32_t frameIndex = m_currentFrame % MAX_FRAMES_IN_FLIGHT;

    bool is2D = (config.renderMode == KnstDrawConfig::RenderMode::MODE_2D);
    VkDeviceSize vertexSize = config.VertexSize();
    size_t vertexCount = config.VertexCount();
    const void* vertexData = config.VertexData();

    PushData pushData = config.pushData;
    pushData.is2D = is2D ? 1.0f : 0.0f;
    pushData.width = (float)m_swapchainExtent.width;
    pushData.height = (float)m_swapchainExtent.height;
    
    knst_vector<uint8_t> pushConstants;
    pushConstants.resize(sizeof(PushData));
    memcpy(pushConstants.data(), &pushData, sizeof(PushData));

    if (m_vertexInputDynamicSupported) {
        SetVertexInputFor(is2D);
    }

    VkDeviceSize alignment = 4;
    VkDeviceSize vOffset = (m_vertexWriteOffset + alignment - 1) & ~(alignment - 1);
    VkDeviceSize vRequired = vertexSize * vertexCount;

    if (vOffset + vRequired > m_vertexBufferCapacity[frameIndex]) {
        
        VkDeviceSize newSize = (vOffset + vRequired) + ((vOffset + vRequired) / 4); // %25 fazla burdan ihtiyacınıza göre ayarlarsınız % 50 gibi
        newSize = (newSize + 4095) & ~4095; // 4KB hizalama / aligment 
        
        VkBuffer newBuffer = VK_NULL_HANDLE;
        VkDeviceMemory newMemory = VK_NULL_HANDLE;
        void* newMapped = nullptr;
        
        if (!AllocateHostBuffer(newSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,newBuffer, newMemory, newMapped)) {
                                
            return;
        }
        
       
        if (m_vertexBufferMapped[frameIndex] != nullptr && m_vertexWriteOffset > 0) {
            memcpy(newMapped, m_vertexBufferMapped[frameIndex], (size_t)m_vertexWriteOffset);
        }
        
      
        if (m_vertexBuffers[frameIndex] != VK_NULL_HANDLE) {
            PendingBufferDestroy pending;
            pending.buffer = m_vertexBuffers[frameIndex];
            pending.memory = m_vertexBufferMemories[frameIndex];
            m_pendingVertexDestroys[frameIndex].push_back(pending);
            
           
            if (m_vertexBufferMapped[frameIndex] != nullptr) {
                vkUnmapMemory(m_vk_content->GetDevice(), m_vertexBufferMemories[frameIndex]);
            }
        }
        
      
        m_vertexBuffers[frameIndex] = newBuffer;
        m_vertexBufferMemories[frameIndex] = newMemory;
        m_vertexBufferMapped[frameIndex] = newMapped;
        m_vertexBufferCapacity[frameIndex] = newSize;
        
        
        vOffset = (m_vertexWriteOffset + alignment - 1) & ~(alignment - 1);
    }

    memcpy((uint8_t*)m_vertexBufferMapped[frameIndex] + vOffset, vertexData, (size_t)vRequired);

    VkBuffer vertexBuffers[] = { m_vertexBuffers[frameIndex] };
    VkDeviceSize offsets[] = { vOffset };
    vkCmdBindVertexBuffers(m_currentCommandBuffer, 0, 1, vertexBuffers, offsets);

    m_vertexWriteOffset = vOffset + vRequired;

    
    bool hasIndices = !config.indices.empty();
    if (hasIndices) {
        VkDeviceSize iOffset = (m_indexWriteOffset + alignment - 1) & ~(alignment - 1);
        VkDeviceSize iRequired = sizeof(uint32_t) * config.indices.size();

        if (iOffset + iRequired > m_indexBufferCapacity[frameIndex]) {
            VkDeviceSize newSize = (iOffset + iRequired) + ((iOffset + iRequired) / 4); // %25 fazla / aynı şekilde bunuda ayarlayabilirsiniz ihtiyacnıza göre ama bana sorarsanız  kalsın böyle gerçi yazıyorum böyle burayada kim okuyacakki sanki :p
            newSize = (newSize + 4095) & ~4095; // 4KB hizalama
            
            VkBuffer newBuffer = VK_NULL_HANDLE;


            VkDeviceMemory newMemory = VK_NULL_HANDLE;
            void* newMapped = nullptr;
            
            if (!AllocateHostBuffer(newSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,newBuffer, newMemory, newMapped)) {
                hasIndices = false;
            } else {
                
                if (m_indexBufferMapped[frameIndex] != nullptr && m_indexWriteOffset > 0) {
                    memcpy(newMapped, m_indexBufferMapped[frameIndex], (size_t)m_indexWriteOffset);
                }
                
               
                if (m_indexBuffers[frameIndex] != VK_NULL_HANDLE) {
                    PendingBufferDestroy pending;
                    pending.buffer = m_indexBuffers[frameIndex];
                    pending.memory = m_indexBufferMemories[frameIndex];
                    m_pendingIndexDestroys[frameIndex].push_back(pending);
                    
                    
                    if (m_indexBufferMapped[frameIndex] != nullptr) {
                        vkUnmapMemory(m_vk_content->GetDevice(), m_indexBufferMemories[frameIndex]);
                    }
                }
                
                
                m_indexBuffers[frameIndex] = newBuffer;
                m_indexBufferMemories[frameIndex] = newMemory;
                m_indexBufferMapped[frameIndex] = newMapped;
                m_indexBufferCapacity[frameIndex] = newSize;
                
                
                iOffset = (m_indexWriteOffset + alignment - 1) & ~(alignment - 1);
            }
        }
        
        if (hasIndices) {
            memcpy((uint8_t*)m_indexBufferMapped[frameIndex] + iOffset,config.indices.data(), (size_t)iRequired);
                   
            vkCmdBindIndexBuffer(m_currentCommandBuffer, m_indexBuffers[frameIndex],iOffset, VK_INDEX_TYPE_UINT32);
                                  
            m_indexWriteOffset = iOffset + iRequired;
        }
    }

  
      
    // burası önemli vulkanın 1.3 sürümüyle gelen bi özellik dinamik olarak vertexleri farklı olarak 2d vertex 3d vertex diye ayrı ayrı verebiliyorum tek pipelinede bu sayede hem 2d hem 3d görüntüyü farklı vertexlerle oluşturabiliyorum bu kontrolü shader tarafındada yapabilirdim veya kalsik 2 pipelinede yapabilirdim ancak böyle olmasını daha mantıklı buldum
    // saat gece yarısı 3 ve ben depth buffer ı attachmentte aktif etmeyi unuttuğum için 3 saatim gitti , bende neden obj loaderim düzgün çalışmıyor diyordum
    // dostum cmdSetCullModeyi NONE de unuttuğum için bütün kodu tekrar tekrar inceledim .. bende diyorum neden 3d modellerde sıkıntı var  , ayrıca depth bufferıda de başlatmayı unutmuşum yani derinlik bilgisini anlamıyordu program , objloader ile yüklediğim modellerde sorun var zannediyordum hatta bu laptop modelini ben sıfırdan tasarladım sırf modellerde hata var diye normalde bu laptop modeli yerine internette bulduğum modeli koycaktım ama sırf hata o modellerde diye gittim kendim blenderdan modelledim , hatta obj loaderı düzeltmek için gittim yapay zekaya deepseeke dedim bana yardım et , bağırarak sorunun ne olduğunu söylemesini istedim hatta şuandaki bu options yapısıda bu yüzden var , yüzey düzeltme vs , ortaya karışık bi yapı oldu aynı zamanda image loaderda zlib yerine kendi algoritmamı yazmaya çalıştım sıkıştırılmış png ler yerine ,, dostum manyak bir olay bu , ve umarımki buraya kadar okumanız gerekmez bile , bende niye yazdığımı bilmiyorum büyük bir proje diye başladım bu işe gerçekten piyasının ihtiyacı var diye düşündüm , hatta biliyormusunuz bu proje string sınıfı olarak başladı ilk başta sadece string sınıfını kodlamıştım sonrasında knst_window sınıfı knstbytestring vector gibi onlarıda bu sınıfa özel kodladım o yüzden döküman yazmadım kütüphanenin ihtiyacı olan şeyleri ekliyorum ama hemen hemen knst_c16string ile aynı mantık gibi byte_string de burada dökümanı okuyun knst_c16string de birçok özellik var onun üzerine çok fazla durdum herşeyi ayarlayabiliyorsunuz......, evet son nokta saat akşam gece yarısı 3 , ve ben çok zaman kaybettim böyle mesajlar yazmak benim tarzım değil ancak ilerde belki buna bakıp gülerim, normalde kısa kısa ingilizce not yazardım ama vulkan tarafına geçince artık benim kendimin unutmaması için türkçe ingilizce yazmaya başladım sonra bu yorum satırı işleri iyice karıştı kafayı toparlamam gerekti.. hatta şuan bunu githuba attığımda event sistemi yapısını değiştirmiş olcam kökünden event ezilmesi durumu vardı artık farklı bi yöntem bulmaya çalışıyorum ne glfw ninki gibi callbackli olcak nede event queue gibi bi sistem callback olmasını istemiyorum bence yazılımcıyı kısıtlıyor , ah evet dimi fökümanda set redraw callback atın diyorum diyorumda bunun nedeni windowsun event işleme sistemi bu yüzden çapraz platforma uyum sağlasın diye x11 ve waylandda sorun yok tabi resize yaparken de render alıyoruz ancak windowsta callback mantığı farklı işte resize içindede wm paint ve wm resizing içindede render almanız gerek e onuda işte yazmak zorunda kalmayın diye böyle bi yol öneriyorum umarım beni anlıyorsunuzdur tabi bu özelliğide makro ile kapatabiliyorsunuz sıkıntı değil :) istediğinizi yapın özgürsünüz , aslında şuan aklıma geldide bende glm kütüphanesindeki gibi happy rabbit lisansı gibi birşeymi koysam mesela şey gibi bu yazılımı kullanmak için önce bir türk kebabı yemeniz gerekir gibi yabancılar için , neyse cidden yoruldum umarım ilerde cv me koyabilirim , dostum cidden render moturu geliştirmek bambaşka bi işmiş şuan birçok özellik ekledim internetten öğrendim ancak shaderlarda boş boş duruyor yani benim doldurmam burdan göndermem gerekli ancak mağlum zamanım yok neyse umarım ilerde herşeyi daha iyi hale getirebilirim , iyi geceler sizlere :))
    if (is2D) {
        vkCmdSetDepthTestEnable(m_currentCommandBuffer, VK_FALSE);
        vkCmdSetDepthWriteEnable(m_currentCommandBuffer, VK_FALSE);
        vkCmdSetCullMode(m_currentCommandBuffer, VK_CULL_MODE_NONE);
        vkCmdSetDepthCompareOp(m_currentCommandBuffer, VK_COMPARE_OP_ALWAYS);
        vkCmdSetDepthBounds(m_currentCommandBuffer, 0.0f, 1.0f);
    } else {
        vkCmdSetDepthTestEnable(m_currentCommandBuffer, VK_TRUE);
        vkCmdSetDepthWriteEnable(m_currentCommandBuffer, VK_TRUE);
        vkCmdSetCullMode(m_currentCommandBuffer, VK_CULL_MODE_BACK_BIT);
        vkCmdSetDepthCompareOp(m_currentCommandBuffer, VK_COMPARE_OP_LESS);
        vkCmdSetDepthBounds(m_currentCommandBuffer, 0.0f, 1.0f);
    }

    vkCmdSetFrontFace(m_currentCommandBuffer,config.frontFaceCW ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE);
            

if (config.HasTexture()) {
    UpdateDescriptorSet(*config.texture, frameIndex);
    vkCmdBindDescriptorSets(
        m_currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
        0, 1, &m_textureDescriptorSets[frameIndex], 0, nullptr);
} else {
    vkCmdBindDescriptorSets(
        m_currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
        0, 1, &m_descriptorSets[frameIndex], 0, nullptr);
}

    if (!pushConstants.empty()) {
        vkCmdPushConstants(m_currentCommandBuffer, m_pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,0, (uint32_t)pushConstants.size(), pushConstants.data());
                          
                          
    }


    if (hasIndices) {
        vkCmdDrawIndexed(m_currentCommandBuffer, (uint32_t)config.indices.size(), 1, 0, 0, 0);
    } else {
        vkCmdDraw(m_currentCommandBuffer, (uint32_t)vertexCount, 1, 0, 0);
    }
}



bool knst_gui_framework::CreateDescriptorPool() {
    VkDevice device = m_vk_content->GetDevice();

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = MAX_FRAMES_IN_FLIGHT * 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT * 2;
    poolInfo.flags = 0;

    return vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) == VK_SUCCESS;
}



bool knst_gui_framework::CreateDescriptorSetLayout() {
    VkDevice device = m_vk_content->GetDevice();
    
    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = 0;
    layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBinding.descriptorCount = 1;
    layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBinding.pImmutableSamplers = nullptr;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &layoutBinding;
    
    return vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) == VK_SUCCESS;
}


bool knst_gui_framework::AllocateDescriptorSet() {
    VkDevice device = m_vk_content->GetDevice();
    knst_vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_descriptorSetLayout);

    m_descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS)
        return false;

    m_textureDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, m_textureDescriptorSets.data()) != VK_SUCCESS)
        return false;

    m_lastBoundTexture.assign(MAX_FRAMES_IN_FLIGHT, nullptr);
    return true;
}

void knst_gui_framework::UpdateDescriptorSetInternal(VkDescriptorSet set, const knst_texture& texture) {
    VkDevice device = m_vk_content->GetDevice();

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = texture.imageView;
    imageInfo.sampler = texture.sampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void knst_gui_framework::UpdateDescriptorSet(const knst_texture& texture, uint32_t frameIndex) {
   
    if (m_lastBoundTexture[frameIndex] == &texture) return;

    UpdateDescriptorSetInternal(m_textureDescriptorSets[frameIndex], texture);
    m_lastBoundTexture[frameIndex] = &texture;
}



void knst_gui_framework::EndFrame() {
    if (m_currentCommandBuffer == VK_NULL_HANDLE) {
        return;
    }

    VkCommandBuffer commandBuffer = m_currentCommandBuffer;
    VkQueue queue = m_vk_content->GetGraphicsQueue();

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        m_currentCommandBuffer = VK_NULL_HANDLE;
        return;
    }

    uint32_t frameIndex = m_currentFrame % MAX_FRAMES_IN_FLIGHT;
    uint32_t imgIdx = m_currentImageIndex;

    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[frameIndex]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[imgIdx]};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult submitResult = vkQueueSubmit(queue, 1, &submitInfo, m_inFlightFences[frameIndex]);
    if (submitResult != VK_SUCCESS) {
        m_swapchainReady = false;
        m_currentCommandBuffer = VK_NULL_HANDLE;
        return;
    }

    VkSwapchainKHR swapChains[] = {m_swapchain};
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &m_currentImageIndex;

    VkResult presentResult = vkQueuePresentKHR(queue, &presentInfo);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        m_resizePending = true;
        m_resizeRetryCount = 0;
        m_skipFrame = true;
        m_swapchainReady = false;
    }

    #if KNST_USING_LINUX_PLATFORM_X11
    if (m_vk_content && m_vk_content->m_window && m_vk_content->m_window->m_syncHasPendingValue) {
        xcb_sync_int64_t value = m_vk_content->m_window->m_syncPendingValue;
        xcb_sync_set_counter(
            KnstWindowSources::get_native_x11_connection_handle(),
            m_vk_content->m_window->m_syncCounter, value);
        if (m_vk_content->m_window->m_syncRequestReceived) {
            xcb_flush(KnstWindowSources::get_native_x11_connection_handle());
            m_vk_content->m_window->m_syncRequestReceived = false;
        }
        m_vk_content->m_window->m_syncHasPendingValue = false;
        m_vk_content->m_window->m_syncPendingValue = value;
    }
    #endif

    m_currentFrame++;
    m_currentCommandBuffer = VK_NULL_HANDLE;
}

  





bool knst_gui_framework::RecreateSwapchainZeroWait(const KnstSwapchainConfig& config) {
    if (config.width == 0 || config.height == 0) return false;

    VkDevice device = m_vk_content->GetDevice();

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (m_inFlightFences[i] == VK_NULL_HANDLE) continue;
        VkResult fenceWait = vkWaitForFences(device, 1, &m_inFlightFences[i], VK_TRUE, 30'000'000ULL);
        if (fenceWait != VK_SUCCESS) {
            return false;
        }
    }

  
    VkSwapchainKHR oldSwapchain = m_swapchain;
    knst_vector<VkFramebuffer> oldFramebuffers = m_swapchainFramebuffers;
    knst_vector<VkImageView> oldImageViews = m_swapchainImageViews;

  
    knst_vector<VkImage> oldImages = m_swapchainImages;
    if (!CreateSwapchain(config)) {
        m_swapchain = oldSwapchain;
        m_swapchainFramebuffers = oldFramebuffers;
        m_swapchainImageViews = oldImageViews;
        m_swapchainImages = oldImages;
        return false;
    }

   
    for (auto fb : oldFramebuffers) {
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device, fb, nullptr);
    }
   
    m_swapchainFramebuffers.clear();

    for (auto imageView : oldImageViews) {
        if (imageView != VK_NULL_HANDLE) vkDestroyImageView(device, imageView, nullptr);
    }
    

    if (oldSwapchain != VK_NULL_HANDLE && oldSwapchain != m_swapchain) {
        vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
    }

    

   
    if (m_hasDepthAttachment || m_hasResolveAttachment || m_hasInputAttachments) {
        CleanupAuxiliaryResources();
        if (!CreateAuxiliaryResources()) {
            m_swapchainReady = false;
            return false;
        }
    }

   
    if (!CreateFramebuffers()) {
        m_swapchainReady = false;
        return false;
    }

 
    m_imagesInFlight.assign(m_swapchainImages.size(), VK_NULL_HANDLE);
    m_currentImageIndex = 0;

    m_swapchainReady = true;
    return true;
}

void knst_gui_framework::CleanupSwapchain() {
    VkDevice device = m_vk_content->GetDevice();

    for (auto framebuffer : m_swapchainFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
    }
    m_swapchainFramebuffers.clear();

    for (auto imageView : m_swapchainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, imageView, nullptr);
        }
    }
    m_swapchainImageViews.clear();
    m_swapchainImages.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }

    m_imagesInFlight.clear();
    m_swapchainReady = false;
}

void knst_gui_framework::Destroy() {
    if (m_vk_content == nullptr) return;


    VkDevice dev = m_vk_content->GetDevice();
    if (dev == VK_NULL_HANDLE) {
        m_swapchainReady = false;
        m_swapchain = VK_NULL_HANDLE;
        m_renderPass = VK_NULL_HANDLE;
        m_graphicsPipeline = VK_NULL_HANDLE;
        m_pipelineLayout = VK_NULL_HANDLE;
        m_commandPool = VK_NULL_HANDLE;
        m_descriptorPool = VK_NULL_HANDLE;
        m_descriptorSetLayout = VK_NULL_HANDLE;
        return;
    }

    vkDeviceWaitIdle(dev);


    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }

   
    m_dummyTexture.Destroy(dev);

    CleanupAuxiliaryResources();

    CleanupSwapchain();
    CleanupPersistentBuffers();

    for (auto semaphore : m_imageAvailableSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_vk_content->GetDevice(), semaphore, nullptr);
        }
    }
    m_imageAvailableSemaphores.clear();

    for (auto semaphore : m_renderFinishedSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_vk_content->GetDevice(), semaphore, nullptr);
        }
    }
    m_renderFinishedSemaphores.clear();

    for (auto fence : m_inFlightFences) {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(m_vk_content->GetDevice(), fence, nullptr);
        }
    }
    m_inFlightFences.clear();

    m_commandBuffers.clear();

    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_vk_content->GetDevice(), m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }

    if (m_graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_vk_content->GetDevice(), m_graphicsPipeline, nullptr);
        m_graphicsPipeline = VK_NULL_HANDLE;
    }

    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_vk_content->GetDevice(), m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_vk_content->GetDevice(), m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    m_swapchainReady = false;
    m_vk_content = nullptr;
}



bool knst_gui_framework::CreateAuxiliaryResources() {
    CleanupAuxiliaryResources();

    m_hasDepthAttachment = false;
    m_hasResolveAttachment = false;
    m_hasInputAttachments = false;

    for (const auto& att : m_renderPassConfig.attachments) {
        if (att.type == KnstAttachmentType::DEPTH ||
            att.type == KnstAttachmentType::DEPTH_STENCIL) {
            m_hasDepthAttachment = true;
        }
        if (att.type == KnstAttachmentType::RESOLVE) {
            m_hasResolveAttachment = true;
        }
        if (att.type == KnstAttachmentType::INPUT) {
            m_hasInputAttachments = true;
        }
    }

    if (m_hasDepthAttachment) {
        if (!CreateDepthResources()) {
            return false;
        }
    }


    if (m_hasResolveAttachment) {
        VkFormat resolveFormat = m_swapchainImageFormat;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

        for (const auto& att : m_renderPassConfig.attachments) {
            if (att.type == KnstAttachmentType::RESOLVE) {
                samples = att.samples;
                if (att.format != VK_FORMAT_UNDEFINED) {
                    resolveFormat = att.format;
                }
                break;
            }
        }

        if (!CreateImageResources(m_resolveImage, m_resolveImageMemory, m_resolveImageView,
                                   resolveFormat, usage, samples)) {
            return false;
        }
    }

    if (m_hasInputAttachments) {
        uint32_t inputCount = 0;
        for (const auto& att : m_renderPassConfig.attachments) {
            if (att.type == KnstAttachmentType::INPUT) {
                inputCount++;
            }
        }

        m_inputImages.resize(inputCount);
        m_inputImageMemories.resize(inputCount);
        m_inputImageViews.resize(inputCount);

        uint32_t idx = 0;
        for (const auto& att : m_renderPassConfig.attachments) {
            if (att.type == KnstAttachmentType::INPUT) {
                VkFormat format = (att.format != VK_FORMAT_UNDEFINED) ? att.format : m_swapchainImageFormat;
                VkImageUsageFlags usage = VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                VkSampleCountFlagBits samples = att.samples;

                if (!CreateImageResources(m_inputImages[idx], m_inputImageMemories[idx],m_inputImageViews[idx], format, usage, samples)) {
                                          
                    return false;
                }
                idx++;
            }
        }
    }

    return true;
}

bool knst_gui_framework::CreateImageResources(VkImage& image, VkDeviceMemory& memory,VkImageView& imageView, VkFormat format,VkImageUsageFlags usage,VkSampleCountFlagBits samples) {
                                              
    VkDevice device = m_vk_content->GetDevice();

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_swapchainExtent.width;
    imageInfo.extent.height = m_swapchainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = samples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_vk_content->FindMemoryType(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
        return false;
    }

    vkBindImageMemory(device, image, memory, 0);

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;

    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        vkDestroyImage(device, image, nullptr);
        vkFreeMemory(device, memory, nullptr);
        image = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

void knst_gui_framework::CleanupAuxiliaryResources() {
    VkDevice device = m_vk_content->GetDevice();
        CleanupDepthResources();
    if (m_resolveImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_resolveImageView, nullptr);
        m_resolveImageView = VK_NULL_HANDLE;
    }
    if (m_resolveImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_resolveImage, nullptr);
        m_resolveImage = VK_NULL_HANDLE;
    }
    if (m_resolveImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_resolveImageMemory, nullptr);
        m_resolveImageMemory = VK_NULL_HANDLE;
    }

    for (auto view : m_inputImageViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
        }
    }
    m_inputImageViews.clear();

    for (auto image : m_inputImages) {
        if (image != VK_NULL_HANDLE) {
            vkDestroyImage(device, image, nullptr);
        }
    }
    m_inputImages.clear();

    for (auto memory : m_inputImageMemories) {
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, memory, nullptr);
        }
    }
    m_inputImageMemories.clear();
}