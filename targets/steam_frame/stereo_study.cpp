#include "stereo_study.hpp"
#include "room_math.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>

void StereoStudy::open(XrSession session,XrSpace space,VkPhysicalDevice physical,VkDevice device,
                       VkQueue queue,unsigned family,const std::filesystem::path& folder) {
    std::ifstream input(folder/"stereo.txt");
    std::string version;
    int width{},height{}; float w{},h{};
    if(!(input>>version>>width>>height>>w>>h>>distance_>>nominal_ipd_) ||
       version!="REPRISE_STEREO_STUDY_V1" || width<1 || height<1 || width>4096 || height>4096 ||
       !std::isfinite(w) || !std::isfinite(h) || !std::isfinite(distance_) || !std::isfinite(nominal_ipd_) ||
       w<=0 || h<=0 || distance_<.5f || nominal_ipd_<.04f || nominal_ipd_>.09f)
        throw std::runtime_error("Invalid frozen stereo study manifest");
    for(int eye=0;eye<2;++eye) {
        auto* loaded=SDL_LoadPNG((folder/(eye ? "right.png" : "left.png")).c_str());
        if(!loaded) throw std::runtime_error(SDL_GetError());
        auto* rgba=SDL_ConvertSurface(loaded,SDL_PIXELFORMAT_RGBA32); SDL_DestroySurface(loaded);
        if(!rgba) throw std::runtime_error(SDL_GetError());
        if(rgba->w!=width || rgba->h!=height) { SDL_DestroySurface(rgba);throw std::runtime_error("Stereo image dimensions differ"); }
        try {
            textures_[eye].open(session,physical,device,queue,family,width,height);
            textures_[eye].upload(rgba->pixels,rgba->pitch);
        } catch(...) { SDL_DestroySurface(rgba);throw; }
        SDL_DestroySurface(rgba);
        auto& layer=layers_[eye]; layer={XR_TYPE_COMPOSITION_LAYER_QUAD};
        layer.space=space; layer.eyeVisibility=eye ? XR_EYE_VISIBILITY_RIGHT : XR_EYE_VISIBILITY_LEFT;
        layer.subImage.swapchain=textures_[eye].swapchain();
        layer.subImage.imageRect.extent={width,height};layer.size={w,h};
    }
    std::printf("Frozen stereo study loaded: %dx%d, panel %.3fx%.3f at %.3fm, nominal IPD %.1fmm; no simulation\n",
                width,height,w,h,distance_,nominal_ipd_*1000);
}
void StereoStudy::update(const XrView& left,const XrView& right,bool recenter) {
    if(anchored_ && !recenter) return;
    using namespace frame_room;
    const auto center=mul(add(left.pose.position,right.pose.position),.5f);
    const auto forward=rotate(left.pose.orientation,{0,0,-1});
    const float angle=std::atan2(forward.x,-forward.z);
    XrPosef pose{{0,std::sin(-angle/2),0,std::cos(-angle/2)},
                add(center,{std::sin(angle)*distance_,0,-std::cos(angle)*distance_})};
    for(auto& layer:layers_) layer.pose=pose;
    anchored_=true;
    const auto delta=sub(left.pose.position,right.pose.position);
    std::printf("Frozen stereo anchored; runtime eye separation %.1fmm, nominal %.1fmm. Fixed images: no head-translation parallax.\n",
                std::sqrt(dot(delta,delta))*1000,nominal_ipd_*1000);
}
