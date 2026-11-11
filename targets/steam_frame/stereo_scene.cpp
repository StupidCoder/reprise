#include "stereo_scene.hpp"
#include "room_math.hpp"
#include <chrono>
#include <cstdio>
#include <stdexcept>

void StereoScene::open(XrSession session,XrSpace space,VkPhysicalDevice physical,VkDevice device,
                       VkQueue queue,unsigned family,const std::filesystem::path& shaders) {
    gpu_.open(physical,device,1024,512,shaders);
    for(int eye=0;eye<2;++eye) {
        textures_[eye].open(session,physical,device,queue,family,1024,512);
        auto& layer=layers_[eye];layer={XR_TYPE_COMPOSITION_LAYER_QUAD};layer.space=space;
        layer.eyeVisibility=eye ? XR_EYE_VISIBILITY_RIGHT : XR_EYE_VISIBILITY_LEFT;
        layer.subImage.swapchain=textures_[eye].swapchain();layer.subImage.imageRect.extent={1024,512};
    }
}
void StereoScene::draw(const app::SceneSurface& scene,const XrPosef& panel,const std::array<XrView,2>& eyes) {
    using namespace frame_room;
    const auto began=std::chrono::steady_clock::now();
    if(!scene.mesh || scene.metres_per_unit<=0 || scene.overlay.width<=0 || scene.overlay.height<=0)
        throw std::runtime_error("Invalid scene surface");
    gpu_.set_mesh(scene.mesh);
    gpu_.set_dynamic(scene.dynamic_mesh.get());
    const auto right=rotate(panel.orientation,{1,0,0}),up=rotate(panel.orientation,{0,1,0}),normal=rotate(panel.orientation,{0,0,1});
    const auto& a=scene.aperture;
    const double panel_height=1.5*scene.overlay.height/scene.overlay.width;
    const double cx=(a[0]+a[2]/2-.5)*1.5,cy=(.5-a[1]-a[3]/2)*panel_height;
    const double width=a[2]*1.5,height=a[3]*panel_height;
    for(int eye=0;eye<2;++eye) {
        const auto relative=sub(eyes[eye].pose.position,panel.position);
        const double x=dot(relative,right), y=dot(relative,up), distance=dot(relative,normal);
        // Near/behind the physical window: fall back rather than submit a
        // singular off-axis projection. The normal seating distance is 2m.
        if(distance<.15) throw std::runtime_error("Viewer is behind or too close to stereo window");
        auto camera=scene.camera;
        camera.eye=camera.eye+camera.right*(x/scene.metres_per_unit)+camera.up*(y/scene.metres_per_unit)+
            camera.forward*((2-distance)/scene.metres_per_unit);
        camera.left=(cx-width/2-x)/distance;camera.right_edge=(cx+width/2-x)/distance;
        camera.bottom=(cy-height/2-y)/distance;camera.top=(cy+height/2-y)/distance;
        const auto timing=textures_[eye].render([&](VkCommandBuffer command,VkImage image) {
            gpu_.draw(command,image,camera,scene.background);
        });
        fence_ms_+=timing.fence_ms;
        auto& layer=layers_[eye];layer.pose=panel;
        layer.pose.position=add(panel.position,rotate(panel.orientation,{float(cx),float(cy),0}));
        layer.size={float(width),float(height)};
    }
    wall_ms_+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
    if(++frames_%360==0)
        std::printf("Live stereo scene: %u frames, both-eye wall mean %.3f ms, fence wait mean %.3f ms; no readback\n",
                    frames_,wall_ms_/frames_,fence_ms_/frames_);
}
