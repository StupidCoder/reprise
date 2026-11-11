#include "ring.hpp"
#include "room_math.hpp"
#include "xr_texture.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#ifdef REPRISE_FRAME_LAUNCHER
#include "launcher_panel.hpp"
#endif

using namespace frame_room;
struct Ring::Impl {
    XrTexture texture, pointer_texture;
    XrSpace space{};
    int card_width{}, card_height{}, columns{}, atlas_width{};
    std::vector<std::string> ids;
    std::vector<float> emphasis;
    std::vector<XrCompositionLayerQuad> quads;
    XrVector3f center{};
    float bearing{};
    bool anchored{};
    int focused = -1;
    double previous{};
    double hover_time{};
#ifdef REPRISE_FRAME_LAUNCHER
    std::unique_ptr<LauncherPanel> panel;
    XrTexture panel_texture, fade_texture;
    std::vector<unsigned char> fade_pixels;
    int atlas_height{}, selected=-1;
    float progress{}, scroll_remainder{};
    bool closing{};
    XrPosef selected_pose{}, panel_pose{}, cover_from{};
    double last_draw=-100;
    std::optional<platform::Image> panel_image;
#endif
};
Ring::Ring() : impl_(std::make_unique<Impl>()) {}
Ring::~Ring() = default;
void Ring::recenter() { suspend(impl_->previous); impl_->anchored=false; impl_->focused=-1; }
const std::vector<XrCompositionLayerQuad>& Ring::layers() const { return impl_->quads; }
void Ring::open(XrSession session, XrSpace space, VkPhysicalDevice physical, VkDevice device,
                VkQueue queue, unsigned family, const std::filesystem::path& catalog, const char* data) {
    auto& p=*impl_;
    p.space=space;
    std::ifstream manifest(catalog/"catalog.tsv");
    std::string version;
    if (!(manifest>>version>>p.card_width>>p.card_height>>p.columns) || version!="REPRISE_XR_CATALOG_V1" ||
        p.card_width!=512 || p.card_height!=352 || p.columns!=4)
        throw std::runtime_error("Invalid generated XR catalog manifest");
    std::string line;
    std::getline(manifest,line);
    while (std::getline(manifest,line)) {
        const auto tab=line.find('\t');
        if (tab==std::string::npos || tab==0) throw std::runtime_error("Invalid XR catalog entry");
        p.ids.push_back(line.substr(0,tab));
    }
    if (p.ids.empty() || p.ids.size()>64) throw std::runtime_error("XR catalog size out of range");
    SDL_Surface* source=SDL_LoadPNG((catalog/"cards.png").c_str());
    if (!source) throw std::runtime_error(SDL_GetError());
    SDL_Surface* rgba=SDL_ConvertSurface(source,SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(source);
    if (!rgba) throw std::runtime_error(SDL_GetError());
    p.atlas_width=rgba->w;
    const int height=int((p.ids.size()+p.columns-1)/p.columns)*p.card_height;
    if (rgba->w!=p.columns*p.card_width || rgba->h!=height) {
        SDL_DestroySurface(rgba); throw std::runtime_error("XR atlas dimensions do not match catalog");
    }
    // Pack normal and highlighted variants once. Focus/hover require no uploads.
    const int width=rgba->w*2;
    std::vector<unsigned char> pixels(size_t(width)*height*4);
    for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
        const auto* src=static_cast<unsigned char*>(rgba->pixels)+y*rgba->pitch+(x%rgba->w)*4;
        auto* dst=pixels.data()+(size_t(y)*width+x)*4;
        std::copy_n(src,4,dst);
        const int cx=x%p.card_width, cy=y%p.card_height;
        const int edge=std::min({cx,cy,p.card_width-1-cx,p.card_height-1-cy});
        if (x>=rgba->w && edge<6) {
            const float glow=1.f-float(edge)/7.f;
            dst[0]=static_cast<unsigned char>(dst[0]*(1-glow)+255*glow);
            dst[1]=static_cast<unsigned char>(dst[1]*(1-glow)+173*glow);
            dst[2]=static_cast<unsigned char>(dst[2]*(1-glow)+53*glow);
        }
    }
    SDL_DestroySurface(rgba);
    p.texture.open(session,physical,device,queue,family,width,height);
    p.texture.upload(pixels.data(),width*4);
#ifdef REPRISE_FRAME_LAUNCHER
    if (data) {
        p.panel=std::make_unique<LauncherPanel>(data);
        p.panel_texture.open(session,physical,device,queue,family,960,640);
        p.fade_texture.open(session,physical,device,queue,family,p.atlas_width,height);
        p.atlas_height=height;
        p.fade_pixels.resize(size_t(p.atlas_width)*height*4);
        for(int y=0;y<height;++y)
            std::copy_n(pixels.data()+size_t(y)*width*4,p.atlas_width*4,p.fade_pixels.data()+size_t(y)*p.atlas_width*4);
        std::printf("Live landscape launcher enabled; shared Library/Launcher services\n");
    }
#else
    if(data) throw std::runtime_error("Build --launcher to use the live details window");
#endif
    // Four cached styles (each hand, released/pressed). Beam and contact glow
    // are compositor layers, so the UI cannot occlude its own pointing feedback.
    constexpr int pointer_width=1024, pointer_height=256;
    std::vector<unsigned char> glow(pointer_width*pointer_height*4);
    for(int style=0;style<4;++style) for(int y=0;y<256;++y) for(int x=0;x<256;++x) {
        const bool dot=x>=128;
        if(dot && y>=128) continue;
        const float dx=((x%128)+.5f)/64.f-1;
        const float dy=(y+.5f)/64.f-1;
        const float d2=dot ? dx*dx+dy*dy : dx*dx;
        const float core=std::exp(-d2*180);
        const float halo=std::exp(-d2*10);
        const float end=dot ? 1.f : std::min(1.f,std::min(y+.5f,255.5f-y)/8.f);
        const std::array<float,3> color=style>=2 ? std::array{1.f,1.f,1.f} :
            style==0 ? std::array{.08f,.9f,.4f} : std::array{.72f,.3f,1.f};
        const auto at=(size_t(y)*pointer_width+style*256+x)*4;
        for(int c=0;c<3;++c) glow[at+c]=static_cast<unsigned char>(255*(color[c]*(1-core)+core));
        glow[at+3]=static_cast<unsigned char>(255*std::min(1.f,.65f*halo+core)*end);
    }
    p.pointer_texture.open(session,physical,device,queue,family,pointer_width,pointer_height);
    p.pointer_texture.upload(glow.data(),pointer_width*4);
    p.emphasis.resize(p.ids.size());
    p.quads.reserve(p.ids.size());
    std::printf("Catalog ring loaded: %zu real cards; cached %dx%d atlas\n",p.ids.size(),width,height);
}
void Ring::update(double time, const XrPosef& head, const std::array<std::optional<XrPosef>,2>& aim,
                  const std::array<bool,2>& clicked, bool reduced, unsigned budget,
                  bool has_focus, bool back, float scroll, const platform::Controls& game_controls,
                  const std::array<bool,2>& held) {
    auto& p=*impl_;
    if (!p.anchored) {
        if(!has_focus) { p.quads.clear(); return; }
        const auto forward=rotate(head.orientation,{0,0,-1});
        const auto old_center=p.center;
        const float old_bearing=p.bearing;
        p.center=head.position;
        p.bearing=std::atan2(forward.x,-forward.z);
#ifdef REPRISE_FRAME_LAUNCHER
        if(p.panel && p.selected>=0) {
            // Recenter deliberately brings the open window in front of the user.
            p.bearing-=float(p.selected)*2*std::numbers::pi_v<float>/p.ids.size();
            const float half=-(p.bearing-old_bearing)/2;
            const XrQuaternionf yaw{0,std::sin(half),0,std::cos(half)};
            for(auto* pose : {&p.selected_pose,&p.panel_pose,&p.cover_from}) {
                pose->position=add(p.center,rotate(yaw,sub(pose->position,old_center)));
                const auto q=pose->orientation;
                pose->orientation={0,yaw.w*q.y+yaw.y*q.w,0,yaw.w*q.w-yaw.y*q.y};
            }
            p.scroll_remainder=0;
        }
#else
        (void)old_center; (void)old_bearing;
#endif
        p.anchored=true;
        std::printf("Ring anchored at eye height %.3f; forward %.3f radians\n",p.center.y,p.bearing);
    }
    const float dt=p.previous ? float(std::clamp(time-p.previous,0.0,.05)) : 0;
    p.previous=time;
#ifdef REPRISE_FRAME_LAUNCHER
    if(p.panel) reduced = reduced || p.panel->reduced_motion();
    if(p.selected<0) p.hover_time+=dt;
#else
    (void)back; (void)scroll; (void)game_controls; (void)held;
    p.hover_time+=dt;
#endif
    const float radius=std::max(3.f,float(p.ids.size())*.74f/(2*std::numbers::pi_v<float>));
    std::vector<XrPosef> poses(p.ids.size());
    int focused=-1, clicked_card=-1;
    float nearest=1000, nearest_clicked=1000;
    for (size_t i=0;i<p.ids.size();++i) {
        const float angle=p.bearing+float(i)*2*std::numbers::pi_v<float>/p.ids.size();
        auto& pose=poses[i];
        pose.orientation={0,std::sin(-angle/2),0,std::cos(-angle/2)};
        const float hover=reduced ? 0 : .01f*std::sin(float(p.hover_time)*.55f+float(i)*1.618f);
        pose.position=add(p.center,{std::sin(angle)*radius,hover,-std::cos(angle)*radius});
        const float w=.65f*(1+.05f*p.emphasis[i]);
        for (size_t hand=0;hand<2;++hand) if (aim[hand]) {
            if (const auto hit=frame_room::hit(*aim[hand],pose,w,w*p.card_height/p.card_width)) {
                if (hit->distance<nearest) { nearest=hit->distance; focused=int(i); }
                if (clicked[hand] && hit->distance<nearest_clicked) { nearest_clicked=hit->distance; clicked_card=int(i); }
            }
        }
    }
    if (clicked_card>=0) focused=clicked_card;
#ifdef REPRISE_FRAME_LAUNCHER
    if(p.panel && p.selected<0 && clicked_card>=0 && has_focus) {
        p.selected=clicked_card; p.selected_pose=poses[clicked_card]; p.closing=false; p.progress=0;
        p.panel_pose=p.selected_pose;
        p.panel_pose.position=add(p.center,mul(sub(p.selected_pose.position,p.center),2.f/radius));
        p.cover_from=p.selected_pose;
        p.cover_from.position=add(p.cover_from.position,{0,.65f*(176-147)/512.f,0});
        p.panel->select(p.ids[clicked_card]); p.last_draw=-100;
        std::printf("Opening live details %s\n",p.ids[clicked_card].c_str());
    }
    if(p.panel && p.selected>=0) {
        // Transition input is consumed here, never forwarded to Start/Delete.
        if(back && p.progress<1) p.closing=true;
        p.progress=std::clamp(p.progress+(p.closing ? -1.f : 1.f)*dt/(reduced ? .12f : .4f),0.f,1.f);
        const auto ease=[](float v){return v*v*(3-2*v);};
        const float opacity=ease(p.progress);
        const float t=reduced ? 1.f : opacity;
        if(!has_focus) p.scroll_remainder=0;
        bool dirty=false;
        if(p.progress==1 && !p.closing) {
            platform::Controls controls=game_controls;
            controls.back=back; controls.wheel_precise=-scroll*dt*8;
            p.scroll_remainder += float(controls.wheel_precise);
            controls.wheel=int(p.scroll_remainder);
            p.scroll_remainder -= controls.wheel;
            float nearest_panel=1000;
            for(size_t hand=0;hand<2;++hand) if(aim[hand]) {
                if(const auto hit=frame_room::hit(*aim[hand],p.panel_pose,1.5f,1.f); hit && hit->distance-(clicked[hand]?2000.f:held[hand]?1000.f:0.f)<nearest_panel) {
                    nearest_panel=hit->distance-(clicked[hand]?2000.f:held[hand]?1000.f:0.f);
                    controls.mouse_x=std::clamp(int(hit->u*960),0,959);
                    controls.mouse_y=std::clamp(int(hit->v*640),0,639);
                    controls.ui_pointer=std::array{controls.mouse_x,controls.mouse_y};
                    controls.click=clicked[hand];
                    controls.mouse_held=held[hand];
                }
            }
            const auto request=p.panel->update(controls,time,has_focus);
            if(request!=app::Launcher::Request::none)
                p.panel->launcher().show_error("This XR build currently tests details and documentation. Game launch controls are the next integration step.",time);
            if(!p.panel->open()) { p.closing=true; std::printf("Closing live details\n"); }
            dirty=controls.click || back || scroll!=0;
        }
        p.quads.clear();
        if(p.progress==0 && p.closing) {
            p.selected=-1; p.focused=-1; p.closing=false; p.scroll_remainder=0;
            return;
        }
        if(p.progress<1 && !reduced) {
            const auto alpha=static_cast<unsigned char>(255*(1-ease(std::clamp(p.progress*4.f/3,0.f,1.f))));
            for(size_t i=3;i<p.fade_pixels.size();i+=4) p.fade_pixels[i]=alpha;
            p.fade_texture.upload(p.fade_pixels.data(),p.atlas_width*4);
            const auto forward=rotate(head.orientation,{0,0,-1});
            for(size_t i=0;i<poses.size() && p.quads.size()+2<budget;++i) {
                if(int(i)==p.selected || dot(sub(poses[i].position,head.position),forward)<-.4f) continue;
                XrCompositionLayerQuad q{XR_TYPE_COMPOSITION_LAYER_QUAD};
                q.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                q.space=p.space; q.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
                q.subImage.swapchain=p.fade_texture.swapchain();
                q.subImage.imageRect={{int(i%p.columns)*p.card_width,int(i/p.columns)*p.card_height},{p.card_width,p.card_height}};
                q.pose=poses[i];
                q.pose.position=add(p.center,mul(sub(q.pose.position,p.center),1+t*.25f/radius));
                q.pose.position.y+=t*.25f;
                q.size={.65f,.65f*p.card_height/p.card_width};
                p.quads.push_back(q);
            }
        }
        if(dirty || p.progress<1 || p.panel->playing() || time-p.last_draw>1./30) {
            if(!p.closing || !p.panel_image) p.panel_image=p.panel->draw(time,true);
            auto image=*p.panel_image;
            for(auto& pixel:image.pixels) pixel.a=static_cast<unsigned char>(pixel.a*opacity);
            p.panel_texture.upload(image.pixels.data(),image.width*4);
            p.last_draw=time;
        }
        XrCompositionLayerQuad panel{XR_TYPE_COMPOSITION_LAYER_QUAD};
        panel.space=p.space; panel.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        panel.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        panel.subImage.swapchain=p.panel_texture.swapchain();
        panel.subImage.imageRect.extent={960,640};
        panel.pose=p.panel_pose;
        panel.pose.position=add(mul(p.selected_pose.position,1-t),mul(p.panel_pose.position,t));
        panel.size={.65f+(.85f*t),.65f*p.card_height/p.card_width+(1-.65f*p.card_height/p.card_width)*t};
        p.quads.push_back(panel);
        constexpr auto rect=app::Launcher::landscape_cover_rect;
        const XrVector3f offset{((rect[0]+rect[2]/2.f)/960-.5f)*1.5f,(.5f-(rect[1]+rect[3]/2.f)/640),.002f};
        XrCompositionLayerQuad cover{XR_TYPE_COMPOSITION_LAYER_QUAD};
        cover.space=p.space; cover.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        cover.subImage.swapchain=p.texture.swapchain();
        cover.subImage.imageRect={{(p.selected%p.columns)*p.card_width+6,(p.selected/p.columns)*p.card_height+6},{500,282}};
        cover.pose=p.selected_pose;
        cover.pose.position=add(mul(p.cover_from.position,1-t),mul(add(p.panel_pose.position,rotate(p.panel_pose.orientation,offset)),t));
        cover.size={.65f*500/512*(1-t)+rect[2]/960.f*1.5f*t,.65f*282/512*(1-t)+rect[3]/640.f*t};
        if(p.panel->separate_cover()) p.quads.push_back(cover);
        return;
    }
#endif
    if (focused!=p.focused) {
        p.focused=focused;
        std::printf("Ring focus %s\n",focused<0 ? "none" : p.ids[focused].c_str());
    }
    if (clicked_card>=0) std::printf("Ring click %s (details integration pending)\n",p.ids[clicked_card].c_str());
    const auto forward=rotate(head.orientation,{0,0,-1});
    std::vector<std::pair<float,size_t>> visible;
    for (size_t i=0;i<p.ids.size();++i) {
        const float target=int(i)==focused ? 1.f : 0.f;
        p.emphasis[i]=reduced ? target : p.emphasis[i]+(target-p.emphasis[i])*(1-std::exp(-dt/0.12f));
        const float facing=dot(sub(poses[i].position,head.position),forward);
        if (facing>-.4f) visible.emplace_back(facing,i);
    }
    std::sort(visible.begin(),visible.end(),[](auto a,auto b){return a.first>b.first;});
    if (visible.size()>budget) visible.resize(budget);
    p.quads.clear();
    for (const auto& item:visible) {
        const auto i=item.second;
        XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
        quad.space=p.space; quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain=p.texture.swapchain();
        quad.subImage.imageRect.offset={int(i%p.columns)*p.card_width+(int(i)==focused ? p.atlas_width : 0),
                                       int(i/p.columns)*p.card_height};
        quad.subImage.imageRect.extent={p.card_width,p.card_height};
        quad.pose=poses[i];
        quad.size.width=.65f*(1+.05f*p.emphasis[i]);
        quad.size.height=quad.size.width*p.card_height/p.card_width;
        p.quads.push_back(quad);
    }
}

void Ring::append_pointers(const XrPosef& head,
                          const std::array<std::optional<XrPosef>,2>& aim,
                          const std::array<bool,2>& pressed) {
    auto& p=*impl_;
    const auto surfaces=p.quads.size();
    for(size_t hand=0;hand<2;++hand) {
        if(!aim[hand]) continue;
        const auto origin=aim[hand]->position;
        const auto direction=rotate(aim[hand]->orientation,{0,0,-1});
        float length=3.5f;
        bool contact=false;
        XrQuaternionf contact_rotation=head.orientation;
        for(size_t i=0;i<surfaces;++i) {
            const auto& surface=p.quads[i];
            if(const auto h=hit(*aim[hand],surface.pose,surface.size.width,surface.size.height);
               h && h->distance<length) {
                length=h->distance; contact=true; contact_rotation=surface.pose.orientation;
            }
        }
        const auto endpoint=add(origin,mul(direction,length));
        const int style=int(hand)+(pressed[hand] ? 2 : 0);
        XrCompositionLayerQuad beam{XR_TYPE_COMPOSITION_LAYER_QUAD};
        beam.space=p.space; beam.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        beam.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                        XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        beam.subImage.swapchain=p.pointer_texture.swapchain();
        beam.subImage.imageRect={{style*256,0},{128,256}};
        beam.pose.position=add(origin,mul(direction,length*.5f));
        // The ribbon's long axis follows the ray; its face follows the wearer.
        beam.pose.orientation=ribbon_orientation(direction,sub(head.position,beam.pose.position));
        beam.size={.024f,length};
        p.quads.push_back(beam);
        if(contact) {
            auto dot=beam;
            dot.subImage.imageRect={{style*256+128,0},{128,128}};
            dot.pose.orientation=contact_rotation;
            dot.pose.position=add(endpoint,rotate(contact_rotation,{0,0,.002f}));
            dot.size={.035f,.035f};
            p.quads.push_back(dot);
        }
    }
}

void Ring::suspend(double now) {
#ifdef REPRISE_FRAME_LAUNCHER
    if(impl_->panel && impl_->panel->playing()) impl_->panel->update({},now,false);
#else
    (void)now;
#endif
}
