// Exercise the player-facing opening module, including input/audio, presentation and lifecycle.
#include "captain_toad_3ds/reprise/module.hpp"
#include "platform/host.hpp"
#include "ui/vhs.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("usage: captain_toad_module_probe IMPORT OUTPUT");
    platform::Host host("Reprise opening module check");
    auto game=captain_toad::reprise::opening(argv[1]);game->attach_gpu(&host);
    std::filesystem::create_directories(argv[2]);
    auto initial=game->picture();
    for(const auto& p:initial.pixels)if(p.a!=255)throw std::runtime_error("Host frame is not opaque");
    std::vector<float> audio(1600);double energy=0;
    for(int frame=0;frame<1200;++frame){
        app::GameInput in;
        if(frame>=600&&frame<720)in.right=true;
        if(frame>=720&&frame<810)in.camera_stick=std::array<float,2>{.6f,0};
        game->tick(in);game->audio(audio,48000);for(float s:audio){if(!std::isfinite(s)||std::abs(s)>1.001)throw std::runtime_error("Invalid audio sample");energy+=s*s;}
        if(frame==599||frame==719||frame==1199){auto image=game->picture();for(const auto& p:image.pixels)if(p.a!=255)throw std::runtime_error("Scene effect leaked alpha into host frame");platform::save_png(image,std::filesystem::path(argv[2])/(std::to_string(frame+1)+".png"));}
        // Repeated display draws neither consume simulation time nor generate sound.
        if(frame==719){auto first=game->picture();auto second=game->picture();if(first.pixels.size()!=second.pixels.size()||std::memcmp(first.pixels.data(),second.pixels.data(),first.pixels.size()*sizeof(platform::Color)))throw std::runtime_error("Repeated capture changed scene");}
    }
    if(energy<.001)throw std::runtime_error("Opening audio is silent");
    game->set_option(1,1);game->set_option(2,1);game->set_canvas(1280,720);game->set_frame_fraction(.5);
    auto size=*game->gpu_frame_size();auto draw=[&](const platform::GpuFrame& f){game->draw_gpu(f);};
    for(int i=0;i<30;++i){host.poll();host.present_game_gpu(size[0],size[1],1,draw);}
    platform::save_png(game->picture(),std::filesystem::path(argv[2])/"wide.png");
    game->set_option(0,2);auto effect=app::screen_effect(*game,1200);host.capture_next_frame(std::filesystem::path(argv[2])/"filtered.png");host.present_game_gpu(size[0],size[1],1,draw,nullptr,false,nullptr,1,0,&*effect);
    if(host.screen_effect_path().find("GPU shader")==std::string::npos)throw std::runtime_error("No GPU screen filter");
    game->set_input_capabilities({true,true,false,false});if(game->touch_controls().size()<4)throw std::runtime_error("No touch controls");
    game.reset();
    game=captain_toad::reprise::opening(argv[1]);game->attach_gpu(&host);game->picture();game.reset();
    std::printf("PASS opening module: 1200 input/audio ticks, captures, repeated render, resize, filter, restart; audio energy %.5f\n",energy);
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
