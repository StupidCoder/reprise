#include "scene_textures.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <cstdio>

namespace {
void check(VkResult r,const char* what) {
    if(r!=VK_SUCCESS) throw std::runtime_error(std::string(what)+" Vk "+std::to_string(r));
}
float linear(unsigned char c) { const float v=c/255.f;return v<=.04045f ? v/12.92f : std::pow((v+.055f)/1.055f,2.4f); }
unsigned char srgb(float v) {return static_cast<unsigned char>(std::clamp(std::lround(255*(v<=.0031308f ? 12.92f*v : 1.055f*std::pow(v,1/2.4f)-.055f)),0l,255l));}
}
struct SceneTextures::Impl {
    VkDevice device{};VkPhysicalDevice physical{};
    VkImage image{};VkDeviceMemory memory{},staging_memory{};VkBuffer staging{};
    VkImageView view{};VkSampler sampler{};VkDescriptorPool pool{};VkDescriptorSet set{};
    std::vector<VkBufferImageCopy> copies;
    unsigned layers{};bool pending=true;
    static constexpr unsigned size=512,levels=10;
    uint32_t type(uint32_t bits,VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties p{};vkGetPhysicalDeviceMemoryProperties(physical,&p);
        for(unsigned i=0;i<p.memoryTypeCount;++i) if((bits&(1u<<i))&&(p.memoryTypes[i].propertyFlags&flags)==flags) return i;
        throw std::runtime_error("Scene texture memory unavailable");
    }
    ~Impl() {
        if(pool) vkDestroyDescriptorPool(device,pool,nullptr);
        if(sampler) vkDestroySampler(device,sampler,nullptr);
        if(view) vkDestroyImageView(device,view,nullptr);
        if(image) vkDestroyImage(device,image,nullptr);
        if(memory) vkFreeMemory(device,memory,nullptr);
        if(staging) vkDestroyBuffer(device,staging,nullptr);
        if(staging_memory) vkFreeMemory(device,staging_memory,nullptr);
    }
};
SceneTextures::SceneTextures():impl_(std::make_unique<Impl>()) {}
SceneTextures::~SceneTextures()=default;
VkDescriptorSet SceneTextures::descriptor() const {return impl_->set;}
void SceneTextures::open(VkPhysicalDevice physical,VkDevice device,VkDescriptorSetLayout layout,
                         const std::filesystem::path& root,const std::vector<platform::scene3d::Material>& materials) {
    auto& p=*impl_;p.physical=physical;p.device=device;p.layers=unsigned(materials.size());
    std::vector<platform::Color> pixels;
    for(unsigned layer=0;layer<p.layers;++layer) {
        std::vector<platform::Color> level(p.size*p.size,{255,255,255,255});
        if(!materials[layer].texture.empty()) {
            const auto path=root/materials[layer].texture;
            using Surface=std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)>;
            Surface source(SDL_LoadPNG(path.c_str()),SDL_DestroySurface);
            Surface scaled(SDL_CreateSurface(p.size,p.size,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
            if(!source || !scaled || !SDL_BlitSurfaceScaled(source.get(),nullptr,scaled.get(),nullptr,SDL_SCALEMODE_LINEAR))
                throw std::runtime_error("Load scene material "+path.string()+": "+SDL_GetError());
            for(unsigned y=0;y<p.size;++y)
                std::memcpy(level.data()+y*p.size,static_cast<char*>(scaled->pixels)+y*scaled->pitch,p.size*4);
        }
        for(unsigned mip=0,side=p.size;mip<p.levels;++mip,side/=2) {
            VkBufferImageCopy copy{};copy.bufferOffset=pixels.size()*4;
            copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,mip,layer,1};copy.imageExtent={side,side,1};
            p.copies.push_back(copy);pixels.insert(pixels.end(),level.begin(),level.end());
            if(side==1) break;
            std::vector<platform::Color> next(side*side/4);
            for(unsigned y=0;y<side/2;++y) for(unsigned x=0;x<side/2;++x) {
                float r=0,g=0,b=0;
                for(unsigned dy=0;dy<2;++dy) for(unsigned dx=0;dx<2;++dx) {
                    const auto c=level[(2*y+dy)*side+2*x+dx];r+=linear(c.r);g+=linear(c.g);b+=linear(c.b);
                }
                next[y*side/2+x]={srgb(r/4),srgb(g/4),srgb(b/4),255};
            }
            level=std::move(next);
        }
    }
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=pixels.size()*4;buffer.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    check(vkCreateBuffer(device,&buffer,nullptr,&p.staging),"texture staging");
    VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,p.staging,&req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;
    alloc.memoryTypeIndex=p.type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device,&alloc,nullptr,&p.staging_memory),"texture staging memory");
    check(vkBindBufferMemory(device,p.staging,p.staging_memory,0),"texture staging bind");
    void* mapped{};check(vkMapMemory(device,p.staging_memory,0,VK_WHOLE_SIZE,0,&mapped),"texture map");
    std::memcpy(mapped,pixels.data(),pixels.size()*4);vkUnmapMemory(device,p.staging_memory);
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.imageType=VK_IMAGE_TYPE_2D;image.format=VK_FORMAT_R8G8B8A8_SRGB;
    image.extent={p.size,p.size,1};image.mipLevels=p.levels;image.arrayLayers=p.layers;image.samples=VK_SAMPLE_COUNT_1_BIT;
    image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    check(vkCreateImage(device,&image,nullptr,&p.image),"material image");vkGetImageMemoryRequirements(device,p.image,&req);
    alloc.allocationSize=req.size;alloc.memoryTypeIndex=p.type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device,&alloc,nullptr,&p.memory),"material memory");check(vkBindImageMemory(device,p.image,p.memory,0),"material bind");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=p.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    view.format=image.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,p.levels,0,p.layers};
    check(vkCreateImageView(device,&view,nullptr,&p.view),"material view");
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;
    sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;sampler.maxLod=float(p.levels-1);
    // Mirrored repetition guarantees edge continuity even for generated sources
    // whose opposite boundaries are not pixel-identical.
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    check(vkCreateSampler(device,&sampler,nullptr,&p.sampler),"material sampler");
    const VkDescriptorPoolSize count{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=1;pool.poolSizeCount=1;pool.pPoolSizes=&count;
    check(vkCreateDescriptorPool(device,&pool,nullptr,&p.pool),"material descriptor pool");
    VkDescriptorSetAllocateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};set.descriptorPool=p.pool;set.descriptorSetCount=1;set.pSetLayouts=&layout;
    check(vkAllocateDescriptorSets(device,&set,&p.set),"material descriptor");
    const VkDescriptorImageInfo info{p.sampler,p.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=p.set;write.descriptorCount=1;
    write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&info;vkUpdateDescriptorSets(device,1,&write,0,nullptr);
    std::printf("Scene materials: %u layers, 512px, 10 linear-light mips, trilinear mirrored repeat\n",p.layers);
}
void SceneTextures::upload(VkCommandBuffer command) {
    auto& p=*impl_;if(!p.pending) return;
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=p.image;
    barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,p.levels,0,p.layers};
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    vkCmdCopyBufferToImage(command,p.staging,p.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,unsigned(p.copies.size()),p.copies.data());
    barrier.oldLayout=barrier.newLayout;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    p.pending=false;
}
