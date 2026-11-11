#include "gameplay.hpp"
#include "app/catalog.hpp"
#include "app/library.hpp"
#include "app/pause_menu.hpp"
#include "app/quick_save.hpp"
#include "app/saved_options.hpp"
#include "app/startup_screen.hpp"
#include "platform/files.hpp"
#include <SDL3/SDL.h>
#include <future>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <utility>

namespace {
void require(bool ok,const char* what) { if(!ok) throw std::runtime_error(std::string(what)+": "+SDL_GetError()); }
}
struct Gameplay::Impl : platform::GpuSurface {
    SDL_Renderer* renderer{};
    SDL_Texture* capture_target{};
    int capture_width{},capture_height{};
    float pending_dx{},pending_dy{};
    SDL_AudioStream* audio{};
    std::unique_ptr<app::GameModule> game;
    bool world_supported{};
    std::optional<app::WorldSurface> world;
    app::GameEntry entry;
    std::filesystem::path content;
    app::PauseMenu menu;
    double previous{}, accumulated{}, sample_credit{};
    long ticks{};
    bool focused{}, resume{}, pointer_down{}, input_ready{};
    std::optional<std::array<int,2>> pointer, cancelled_pointer;
    std::vector<platform::PointerEvent> pending;
    std::array<int,2> logical{960,640};
    std::array<float,4> viewport{0,0,960,640};
    std::string message, written_options;
    std::future<std::unique_ptr<app::GameModule>> loading;
    ~Impl() {
        if(loading.valid()) loading.wait();
        game.reset();
        if(audio) SDL_DestroyAudioStream(audio);
        if(capture_target) SDL_DestroyTexture(capture_target);
        if(renderer) SDL_DestroyRenderer(renderer);
        SDL_QuitSubSystem(SDL_INIT_VIDEO|SDL_INIT_AUDIO);
    }
    void reset_input() {
        if(pointer_down && pointer) cancelled_pointer=pointer;
        pending.clear(); pointer.reset(); pointer_down=false; pending_dx=pending_dy=0;
        accumulated=sample_credit=0; input_ready=false;
        if(audio) SDL_ClearAudioStream(audio);
    }
    void remember_options() {
        const auto text=app::options_text(*game);
        if(text==written_options) return;
        const auto folder=content.parent_path().parent_path()/"options";
        std::filesystem::create_directories(folder);
        platform::write_text(folder/(entry.id+".txt"),text);
        written_options=text;
    }
    void attach(std::unique_ptr<app::GameModule> next) {
        if(!renderer) {
            require(SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_AUDIO),"SDL game services");
            renderer=SDL_CreateGPURenderer(nullptr,nullptr);
            require(renderer,"offscreen game renderer");
            const SDL_AudioSpec spec{SDL_AUDIO_F32,2,48000};
            audio=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
            require(audio,"game audio stream");
            require(SDL_ResumeAudioStreamDevice(audio),"resume audio");
        }
        next->set_canvas(960,640); next->attach_gpu(this);
        next->set_input_capabilities({false,false,true,false});
        next->set_world_surface_supported(world_supported);
        app::restore_options(*next,content.parent_path().parent_path()/"options"/(entry.id+".txt"));
        if(resume && !next->load()) throw std::runtime_error(next->save_error());
        game=std::move(next); written_options=app::options_text(*game);
        menu.hide(); reset_input(); cancelled_pointer.reset(); previous=0;
        std::printf("Gameplay started %s (%s), step %.3f ms\n",entry.id.c_str(),resume?"Continue":"Start",game->step_seconds()*1000);
    }
    platform::Image capture(int width,int height,const std::function<void(const platform::GpuFrame&)>& draw) override {
        if(!capture_target || width!=capture_width || height!=capture_height) {
            if(capture_target) SDL_DestroyTexture(capture_target);
            capture_target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,width,height);
            require(capture_target,"game capture target"); capture_width=width; capture_height=height;
        }
        SDL_Texture* target=capture_target;
        SDL_Texture* old=SDL_GetRenderTarget(renderer);
        struct Cleanup { SDL_Renderer* r; SDL_Texture* t; SDL_Texture* old; ~Cleanup(){SDL_SetRenderTarget(r,old);} } cleanup{renderer,target,old};
        require(SDL_SetRenderTarget(renderer,target),"set capture target");
        require(SDL_SetRenderDrawColor(renderer,0,0,0,255),"clear color");
        require(SDL_RenderClear(renderer),"clear target");
        draw({renderer,target,width,height});
        SDL_Surface* read=SDL_RenderReadPixels(renderer,nullptr); require(read,"game readback");
        SDL_Surface* rgba=SDL_ConvertSurface(read,SDL_PIXELFORMAT_RGBA32); SDL_DestroySurface(read); require(rgba,"RGBA conversion");
        platform::Image image(width,height);
        for(int y=0;y<height;++y) std::memcpy(image.pixels.data()+size_t(y)*width,static_cast<char*>(rgba->pixels)+y*rgba->pitch,size_t(width)*4);
        SDL_DestroySurface(rgba); return image;
    }
};
Gameplay::Gameplay():impl_(std::make_unique<Impl>()) {}
Gameplay::~Gameplay()=default;
bool Gameplay::active() const { return impl_->game || impl_->loading.valid(); }
std::string Gameplay::take_message() { return std::exchange(impl_->message,{}); }
void Gameplay::start(const app::GameEntry& entry,const std::filesystem::path& content,bool resume) {
    auto& p=*impl_; p.entry=entry; p.content=content; p.resume=resume; p.message.clear();
    p.loading=std::async(std::launch::async,[entry,content] {
        app::Library library(content.parent_path().parent_path(),app::catalog(),app::Library::StartupValidation::defer_large_imports);
        const auto& entries=library.games();
        const auto it=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.id==entry.id;});
        if(it==entries.end() || !library.verify(size_t(it-entries.begin()))) throw std::runtime_error("Imported files are missing or changed. Import this game again.");
        return app::with_quick_save(entry,content);
    });
}
void Gameplay::update(const platform::Controls& controls,double now,bool focused) {
    auto& p=*impl_;
    try {
        if(p.loading.valid()) {
            if(p.loading.wait_for(std::chrono::seconds(0))==std::future_status::ready) p.attach(p.loading.get());
            return;
        }
        if(!p.game) return;
        SDL_PumpEvents();
        if(!focused) {
            if(p.focused) { p.menu.show(now); std::printf("Gameplay paused on focus/tracking loss\n"); }
            p.focused=false; p.previous=now; p.reset_input(); return;
        }
        p.focused=true;
        const double dt=p.previous ? std::clamp(now-p.previous,0.,4*p.game->step_seconds()) : 0;
        p.previous=now;
        if(p.menu.open()) {
            auto navigation=controls; navigation.back |= controls.menu;
            const auto action=p.menu.update(navigation,*p.game,now);
            switch(action) {
            case app::PauseMenu::Action::quit:
                p.remember_options(); p.game.reset(); p.reset_input(); std::printf("Gameplay returned to details\n"); return;
            case app::PauseMenu::Action::restart:
                p.remember_options(); p.game.reset(); p.reset_input(); start(p.entry,p.content,false); return;
            case app::PauseMenu::Action::save: {
                const bool ok=p.game->save();
                p.menu.set_message(ok ? (p.game->saved_message().empty()?(app::persistent_saves(p.entry.id)?"Saved state":"Saved for this session only"):p.game->saved_message()) : (p.game->save_error().empty()?"Quick save is not available here":p.game->save_error()),now);
                std::printf("Gameplay save %s: %s\n",p.entry.id.c_str(),ok?"success":"failed"); break;
            }
            case app::PauseMenu::Action::load: {
                const bool ok=p.game->load(); p.menu.set_message(ok?"Loaded state":(p.game->save_error().empty()?"No quick save yet":p.game->save_error()),now);
                std::printf("Gameplay load %s: %s\n",p.entry.id.c_str(),ok?"success":"failed"); break;
            }
            default: break;
            }
            p.remember_options(); p.reset_input(); return;
        }
        if(controls.menu || controls.back) { p.menu.show(now); p.reset_input(); std::printf("Gameplay pause menu\n"); return; }
        // A launch/resume/focus change consumes held controls until released.
        if(!p.input_ready) {
            p.input_ready=!(controls.mouse_held || controls.fire || controls.button2 || controls.start || controls.face_x || controls.face_y);
            return;
        }
        app::GameInput input;
        input.up=controls.up; input.down=controls.down; input.left=controls.left; input.right=controls.right;
        input.fire=controls.fire; input.button2=controls.button2; input.start=controls.start;
        input.f1=controls.start; input.f3=controls.face_x; input.f5=controls.face_y; input.space=controls.button2;
        input.analog_stick=controls.analog_stick; input.camera_stick=controls.camera_stick;
        input.face_x=controls.face_x; input.face_y=controls.face_y;
        input.shoulder_left=controls.shoulder_left; input.shoulder_right=controls.shoulder_right;
        input.trigger_left=controls.trigger_left; input.trigger_right=controls.trigger_right;
        input.mouse_right=controls.shoulder_left;
        std::optional<std::array<int,2>> pointer;
        if(controls.ui_pointer) {
            const float x=(*controls.ui_pointer)[0]-p.viewport[0], y=(*controls.ui_pointer)[1]-p.viewport[1];
            if(x>=0 && y>=0 && x<p.viewport[2] && y<p.viewport[3])
                pointer=std::array{int(x*p.logical[0]/p.viewport[2]),int(y*p.logical[1]/p.viewport[3])};
        }
        const bool held=pointer && controls.mouse_held;
        if(pointer) {
            input.pointer=true; input.pointer_x=(*pointer)[0]; input.pointer_y=(*pointer)[1];
            if(p.pointer) { p.pending_dx+=float((*pointer)[0]-(*p.pointer)[0]); p.pending_dy+=float((*pointer)[1]-(*p.pointer)[1]); }
            if(held || p.pointer_down) p.pending.push_back({held && !p.pointer_down ? platform::PointerEvent::Kind::down : !held ? platform::PointerEvent::Kind::up : platform::PointerEvent::Kind::move,float((*pointer)[0]),float((*pointer)[1]),false});
        } else if(p.pointer_down && p.pointer) p.pending.push_back({platform::PointerEvent::Kind::up,float((*p.pointer)[0]),float((*p.pointer)[1]),false});
        p.pointer=pointer; p.pointer_down=held; input.mouse_button=held;
        p.accumulated+=dt;
        while(p.accumulated>=p.game->step_seconds() && p.game->stopped().empty() && !p.game->finished()) {
            if(p.cancelled_pointer) {
                p.pending.insert(p.pending.begin(),{platform::PointerEvent::Kind::up,float((*p.cancelled_pointer)[0]),float((*p.cancelled_pointer)[1]),false});
                p.cancelled_pointer.reset();
            }
            input.pointer_events=std::move(p.pending); p.pending.clear();
            input.mouse_dx=p.pending_dx; input.mouse_dy=p.pending_dy; p.pending_dx=p.pending_dy=0;
            p.game->tick(input);
            p.accumulated-=p.game->step_seconds(); ++p.ticks;
            p.sample_credit+=p.game->step_seconds()*48000;
            const int samples=int(p.sample_credit); p.sample_credit-=samples;
            std::vector<float> audio(size_t(samples)*2); p.game->audio(audio,48000);
            if(!p.menu.muted()) {
                if(SDL_GetAudioStreamQueued(p.audio)>48000*2*4/4) SDL_ClearAudioStream(p.audio);
                require(SDL_PutAudioStreamData(p.audio,audio.data(),int(audio.size()*sizeof(float))),"queue game audio");
            }
        }
        if(!p.game->stopped().empty() || p.game->finished()) {
            p.message=p.game->stopped(); p.remember_options(); p.game.reset(); p.reset_input(); return;
        }
        if(p.game->take_menu_request()) { p.menu.show(now); p.reset_input(); }
        p.game->set_frame_fraction(p.accumulated/p.game->step_seconds());
        p.game->set_display_pointer(pointer);
    } catch(const std::exception& e) {
        p.message=std::string("The game could not continue: ")+e.what();
        std::printf("Gameplay error: %s\n",p.message.c_str()); p.game.reset(); p.reset_input();
    }
}
platform::Image Gameplay::draw(double now) {
    auto& p=*impl_;
    p.world.reset();
    try {
    if(p.loading.valid()) return app::startup_screen(960,640,"Preparing "+p.entry.title,now);
    if(!p.game) return {960,640};
    if(p.menu.open()) return p.menu.draw(*p.game,p.entry.title,now,p.entry.id);
    p.world=p.game->world_surface();
    if(p.world) {
        platform::Image image(960,640); ui::fill(image,{0,0,0,0});
        const auto& hud=p.world->hud;
        const int height=std::min(220,int(960.0*hud.height/(hud.width*p.world->pixel_aspect)));
        ui::picture(image,hud,0,420,960,height);
        return image;
    }
    platform::Image source(1,1);
    if(const auto size=p.game->gpu_frame_size()) source=p.capture((*size)[0],(*size)[1],[&](const auto& frame){p.game->draw_gpu(frame);});
    else source=p.game->picture();
    p.logical={source.width,source.height};
    if(p.game->vhs_level()>0) source=ui::vhs(source,p.ticks,p.game->vhs_level(),p.game->pixel_aspect());
    const float aspect=float(p.logical[0]*p.game->pixel_aspect()/std::max(1,p.logical[1]));
    const float width=std::min(960.f,640.f*aspect), height=width/aspect;
    p.viewport={(960-width)/2,(640-height)/2,width,height};
    platform::Image image(960,640); ui::fill(image,{0,0,0});
    ui::picture(image,source,int(p.viewport[0]),int(p.viewport[1]),int(width),int(height));
    return image;
    } catch(const std::exception& e) {
        p.message=std::string("Game rendering failed: ")+e.what();
        std::printf("%s\n",p.message.c_str()); p.game.reset(); p.reset_input();
        return app::startup_screen(960,640,p.message,now);
    }
}
void Gameplay::set_world_surface_supported(bool supported) {
    impl_->world_supported=supported;
    if(impl_->game) impl_->game->set_world_surface_supported(supported);
}
const std::optional<app::WorldSurface>& Gameplay::world_surface() const { return impl_->world; }
