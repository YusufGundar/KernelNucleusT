#ifndef KNST_TEXTURE_HPP
#define KNST_TEXTURE_HPP
#pragma once


class knst_texture{

private:



public:


VkImage image = VK_NULL_HANDLE;

VkDeviceMemory image_memory = VK_NULL_HANDLE;

VkImageView imageView = VK_NULL_HANDLE;

VkSampler sampler = VK_NULL_HANDLE;

uint32_t width = 0;
uint32_t height = 0;
uint32_t channels = 0;
VkFormat format = VK_FORMAT_UNDEFINED;

bool isLoaded = false;
knst_c16string file_path;

void* userData = nullptr;


knst_texture() = default;
    

knst_texture(const knst_texture&) = delete;
knst_texture& operator=(const knst_texture&) = delete;


    knst_texture(knst_texture&& other) noexcept {
        *this = std::move(other);
    }
    
    knst_texture& operator=(knst_texture&& other) noexcept {
        if (this != &other) {
            
            if (isLoaded) {
                //bunu çağıran temizlemeli
            }
            
            // Kaynakları taşı
            image = other.image;
            image_memory = other.image_memory;
            imageView = other.imageView;
            sampler = other.sampler;
            width = other.width;
            height = other.height;
            channels = other.channels;
            format = other.format;
            isLoaded = other.isLoaded;
            file_path = std::move(other.file_path);
            
            
            other.image = VK_NULL_HANDLE;
            other.image_memory = VK_NULL_HANDLE;
            other.imageView = VK_NULL_HANDLE;
            other.sampler = VK_NULL_HANDLE;
            other.isLoaded = false;
        }
        return *this;
    }
    
    
    

    void Destroy(VkDevice device) {
       
        if (sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, sampler, nullptr);
            sampler = VK_NULL_HANDLE;
        }
        

        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, imageView, nullptr);
            imageView = VK_NULL_HANDLE;
        }
        
       
        if (image != VK_NULL_HANDLE) {
            vkDestroyImage(device, image, nullptr);
            image = VK_NULL_HANDLE;
        }
        
       
        if (image_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, image_memory, nullptr);
            image_memory = VK_NULL_HANDLE;
        }
        
        isLoaded = false;
        width = 0;
        height = 0;
        channels = 0;
        format = VK_FORMAT_UNDEFINED;
    }
    
    
    bool IsValid() const {
        return isLoaded && image != VK_NULL_HANDLE &&imageView != VK_NULL_HANDLE &&sampler != VK_NULL_HANDLE &&width > 0 && height > 0;
               
               
    }
    
    
    VkExtent2D GetSize() const {
        return {width, height};
    }
    
    
    VkFormat GetFormat() const {
        return format;
    }




};




class knst_texture_loader {



    private: 



static uint32_t FindMemoryType(

    VkPhysicalDevice physicalDevice,

    uint32_t typeFilter,


    VkMemoryPropertyFlags properties)


{
    VkPhysicalDeviceMemoryProperties memProperties;


    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);


    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {


        if ((typeFilter & (1 << i)) &&


            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {


            return i;

        }
    }


    return 0;

}



public:



static knst_texture LoadFromFile(VkDevice device,VkPhysicalDevice physical_device,VkCommandPool commandPool,VkQueue grafikqueue,const knst_c16string file_path){


int width , height;

knst_byte_string imageData = knst_image_loader::load_png(file_path,&width,&height,KNST_BITMAP_OUTPUT_RGBA);


    if(imageData.empty()){
        return knst_texture();
        
    }

    uint32_t channels = 4;  // sabit zaten r g b a
// veriler var artık datayı okuyacaz
    knst_texture texture = CreateFromData(device,physical_device,commandPool,grafikqueue,imageData,(uint32_t)width,(uint32_t)height,channels);


    texture.file_path = file_path;
    texture.isLoaded = true;

    return texture;
}


static knst_texture CreateFromData(VkDevice device,VkPhysicalDevice physical_device,VkCommandPool commandPool,VkQueue graphisc_queue,
knst_byte_string data,uint32_t width,uint32_t height,uint32_t channels){



   knst_texture texture;

    VkFormat format;
    VkDeviceSize imageSize;

    switch (channels) {
        case 1:
            format = VK_FORMAT_R8_UNORM;           // Gri tonlama
            imageSize = width * height * 1;
            break;
        case 2:
            format = VK_FORMAT_R8G8_UNORM;         // Gri + Alpha
            imageSize = width * height * 2;
            break;
        case 3:
            format = VK_FORMAT_R8G8B8_SRGB;        // RGB
            imageSize = width * height * 3;
            break;
        case 4:
        default:
            format = VK_FORMAT_R8G8B8A8_SRGB;      // RGBA  tabi şimdilik sadece rgba varsayıyıoz sabit 4 dedik çünki ilerde zaten eklerim çoklu kanal destekli gibi
            imageSize = width * height * 4; // hesap yapıyoz nekadar diye
            channels = 4;
            break;
    }


VkBuffer staggingBuffer = VK_NULL_HANDLE; // bunuda zaten cpu dan gpu ya kopyalıcaz geçiçi buffer bu
VkDeviceMemory stagging_memory = VK_NULL_HANDLE; // buda stagging bufferın belleği işte

void* staggingMapped = nullptr;

VkBufferCreateInfo bufferInfo{};

bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
bufferInfo.size = imageSize; 
bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

if(vkCreateBuffer(device,&bufferInfo,nullptr,&staggingBuffer) != VK_SUCCESS){
    return texture;
}


VkMemoryRequirements mememoryREquestment;


vkGetBufferMemoryRequirements(device,staggingBuffer,&mememoryREquestment);


uint32_t memoryTypeIndex = FindMemoryType(
    physical_device,
    mememoryREquestment.memoryTypeBits,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
);

VkMemoryAllocateInfo allocInfo{};
allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
allocInfo.allocationSize = mememoryREquestment.size;
allocInfo.memoryTypeIndex = memoryTypeIndex;

if (vkAllocateMemory(device, &allocInfo, nullptr, &stagging_memory) != VK_SUCCESS) {
    vkDestroyBuffer(device, staggingBuffer, nullptr);
    return texture;
}

vkBindBufferMemory(device, staggingBuffer, stagging_memory, 0);


vkMapMemory(device, stagging_memory, 0, imageSize, 0, &staggingMapped);
memcpy(staggingMapped, data.data(), (size_t)imageSize);
vkUnmapMemory(device, stagging_memory);


VkImageCreateInfo imageInfo{};
imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
imageInfo.imageType = VK_IMAGE_TYPE_2D;
imageInfo.extent.width = width;
imageInfo.extent.height = height;
imageInfo.extent.depth = 1;
imageInfo.mipLevels = 1;
imageInfo.arrayLayers = 1;
imageInfo.format = format;
imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

if (vkCreateImage(device, &imageInfo, nullptr, &texture.image) != VK_SUCCESS) {
    vkDestroyBuffer(device, staggingBuffer, nullptr);
    vkFreeMemory(device, stagging_memory, nullptr);
    return texture;
}


VkMemoryRequirements imgMemReq;
vkGetImageMemoryRequirements(device, texture.image, &imgMemReq);

uint32_t imgMemoryTypeIndex = FindMemoryType(
    physical_device,
    imgMemReq.memoryTypeBits,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
);

VkMemoryAllocateInfo imgAllocInfo{};
imgAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
imgAllocInfo.allocationSize = imgMemReq.size;
imgAllocInfo.memoryTypeIndex = imgMemoryTypeIndex;

if (vkAllocateMemory(device, &imgAllocInfo, nullptr, &texture.image_memory) != VK_SUCCESS) {
    vkDestroyImage(device, texture.image, nullptr);
    vkDestroyBuffer(device, staggingBuffer, nullptr);
    vkFreeMemory(device, stagging_memory, nullptr);
    return texture;
}

vkBindImageMemory(device, texture.image, texture.image_memory, 0);


TransitionImageLayout(
    commandPool,
    graphisc_queue,
    device,
    texture.image,
    format,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
);


CopyBufferToImage(
    commandPool,
    graphisc_queue,
    device,
    staggingBuffer,
    texture.image,
    width,
    height
);


TransitionImageLayout(
    commandPool,
    graphisc_queue,
    device,
    texture.image,
    format,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
);


vkDestroyBuffer(device, staggingBuffer, nullptr);
vkFreeMemory(device, stagging_memory, nullptr);


VkImageViewCreateInfo viewInfo{};
viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
viewInfo.image = texture.image;
viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
viewInfo.format = format;
viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
viewInfo.subresourceRange.baseMipLevel = 0;
viewInfo.subresourceRange.levelCount = 1;
viewInfo.subresourceRange.baseArrayLayer = 0;
viewInfo.subresourceRange.layerCount = 1;

if (vkCreateImageView(device, &viewInfo, nullptr, &texture.imageView) != VK_SUCCESS) {
    vkDestroyImage(device, texture.image, nullptr);
    vkFreeMemory(device, texture.image_memory, nullptr);
    return texture;
}


VkSamplerCreateInfo samplerInfo{};
samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
samplerInfo.magFilter = VK_FILTER_LINEAR;
samplerInfo.minFilter = VK_FILTER_LINEAR;
samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
samplerInfo.anisotropyEnable = VK_FALSE;
samplerInfo.maxAnisotropy = 1.0f;
samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
samplerInfo.unnormalizedCoordinates = VK_FALSE;
samplerInfo.compareEnable = VK_FALSE;
samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
samplerInfo.mipLodBias = 0.0f;
samplerInfo.minLod = 0.0f;
samplerInfo.maxLod = 0.0f;

if (vkCreateSampler(device, &samplerInfo, nullptr, &texture.sampler) != VK_SUCCESS) {
    vkDestroyImageView(device, texture.imageView, nullptr);
    vkDestroyImage(device, texture.image, nullptr);
    vkFreeMemory(device, texture.image_memory, nullptr);
    return texture;
}


texture.width = width;
texture.height = height;
texture.channels = channels;
texture.format = format;
texture.isLoaded = true;

return texture;
}



static void TransitionImageLayout(
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VkDevice device,
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout)
{
    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && 
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && 
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        return;
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}


static void CopyBufferToImage(
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VkDevice device,
    VkBuffer buffer,
    VkImage image,
    uint32_t width,
    uint32_t height)
{
    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}
static knst_texture CreateEmpty(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue queue, 
    uint32_t width,
    uint32_t height,
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB
) {
    knst_byte_string pixelData;
    pixelData.resize(width * height * 4);
    for (uint32_t i = 0; i < width * height * 4; i++) {
        pixelData[i] = 255;
    }
    return CreateFromData(device, physicalDevice, commandPool, queue, pixelData, width, height, 4);
}



    static VkFormat DetermineFormat(uint32_t channels) {
        switch (channels) {
            case 1:  return VK_FORMAT_R8_UNORM;           // Gri tonlama
            case 2:  return VK_FORMAT_R8G8_UNORM;         // Gri + Alpha
            case 3:  return VK_FORMAT_R8G8B8_SRGB;        // RGB
            case 4:  return VK_FORMAT_R8G8B8A8_SRGB;      // RGBA
            default: return VK_FORMAT_R8G8B8A8_SRGB;      // Varsayılan
        } // geliştirmeye devam edicem .. dostum galiba delirmeye başladım, vulkan işte.. yapı doldur ver ama birçok yapı
    }
    
    
    static uint32_t RoundToPowerOfTwo(uint32_t value) {
        uint32_t power = 1;
        while (power < value) power <<= 1;
        return power;
    }
};











#endif