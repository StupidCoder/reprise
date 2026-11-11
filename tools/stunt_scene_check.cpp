#include "stunt_car_racer_amiga/render/scene.hpp"
#include "stunt_car_racer_amiga/render/race_screen.hpp"
#include "stunt_car_racer_amiga/game/module.hpp"
#include "stunt_car_racer_amiga/race/race.hpp"
#include "platform/host.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <stdexcept>

namespace s=platform::scene3d;
namespace {
void check(bool ok,const char* what) { if(!ok) throw std::runtime_error(what); }
s::Vec3 unit(s::Vec3 v) {
    const auto length=std::sqrt(s::dot(v,v));
    check(length>1e-9,"degenerate diagnostic camera basis");
    return v*(1/length);
}
s::Vec3 cross(s::Vec3 a,s::Vec3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
void synthetic() {
    s::Mesh mesh;
    mesh.materials={{{255,0,0,255}},{{0,255,0,255}}};
    mesh.vertices={{{-1,-1,3}},{{1,-1,3}},{{0,1,3}},{{-1,-1,2}},{{1,-1,2}},{{0,1,2}}};
    mesh.triangles={{{0,1,2},0},{{3,4,5},1}};
    s::View view;
    auto a=s::reference_image(mesh,view,64,64,{0,0,0});
    std::reverse(mesh.triangles.begin(),mesh.triangles.end());
    auto b=s::reference_image(mesh,view,64,64,{0,0,0});
    check(a.pixels[32*64+32].g==255,"depth test must select near surface");
    for(std::size_t i=0;i<a.pixels.size();++i)
        check(a.pixels[i].g==b.pixels[i].g && a.pixels[i].r==b.pixels[i].r,"triangle order changed depth result");
    const s::Vec3 convergence{0,0,2};
    double center=0;
    for(int eye=0;eye<2;++eye) {
        view.eye.x=eye ? .032 : -.032;
        view.left=(-.75-view.eye.x)/2; view.right_edge=(.75-view.eye.x)/2;
        const auto pixel=s::project(convergence,view,640,400);
        if(!eye) center=pixel.x;
        else check(std::abs(pixel.x-center)<1e-9,"off-axis convergence plane disparity");
        check(std::abs(pixel.x-320)<1e-9,"convergence point must be centered");
    }
    view={}; view.near_z=2.5;
    auto clipped=s::reference_image(mesh,view,64,64,{0,0,0});
    check(clipped.pixels[32*64+32].r==255,"near-plane clipping must remove near surface");
    const auto normalized=stuntcar::render::scene_point(256,1024,256);
    check(normalized.x==1 && normalized.y==1 && normalized.z==1,"track height normalization");
    stuntcar::render::Camera previous,current;previous.yaw=65530;current.yaw=6;
    const auto middle=stuntcar::render::interpolate_camera(previous,current,.5);
    check((middle.yaw&65535)==0,"camera interpolation must take shortest wrapped angle");
    for(int roll:{-4096,4096}) {
        stuntcar::render::Camera bank;bank.x=bank.z=bank.eye=0;
        bank.yaw=bank.pitch=0;bank.roll=roll;
        const auto bank_view=stuntcar::render::perspective_camera(bank);
        const auto right_point=s::project({1,0,10},bank_view,640,320);
        // Original angular projector: screen-down = -sin(roll)*across
        // for a point on the unpitched horizon to the right of the camera.
        check((right_point.y-160)*roll<0,"bank direction disagrees with original projector");
    }
    stuntcar::data::Track loop;loop.segments.resize(2);
    loop.segments[0].steps=loop.segments[1].steps=10;
    stuntcar::render::Opponent before{1,9,100,{0,0,0,0}},after{0,1,120,{10,10,10,10}};
    const auto halfway=stuntcar::render::interpolate_opponent(loop,before,after,.5);
    check(halfway.segment==0 && halfway.step==0 && halfway.across==110 && halfway.lift[0]==5,
          "opponent smoothing must cross the lap seam along the short path");
    std::puts("PASS depth order, near clipping, off-axis stereo convergence, height normalization");
}
void module_check(const std::filesystem::path& content,bool league=false) {
    auto baseline=stuntcar::game::start(content), sampled=stuntcar::game::start(content);
    sampled->set_scene_surface_supported(true);sampled->set_option(1,1);sampled->set_option(2,1);
    baseline->set_scene_surface_supported(true);baseline->set_option(1,1);
    unsigned scenes=0,continuous=0,snaps=0,start_continuous=0,start_snaps=0;
    std::optional<s::Vec3> prior_camera;
    for(int tick=0;tick<500;++tick) {
        app::GameInput input;
        input.fire=tick==0 || tick==20 || tick==40 || tick==80 || tick==100 || tick==120 || tick==140 || tick==200;

        input.down=league && tick==60;
        if(tick>210) { input.up=true;input.right=tick>300; }
        if(tick==250) sampled->set_option(3,0);
        if(tick==350) sampled->set_option(3,1);
        baseline->tick(input);sampled->tick(input);
        const auto unsmoothed=baseline->scene_surface();
        for(double fraction:{0.,.5,.9}) {
            baseline->set_frame_fraction(fraction);sampled->set_frame_fraction(fraction);
            const auto before=sampled->picture();
            const auto scene=sampled->scene_surface(),again=sampled->scene_surface();
            check(bool(scene)==bool(again),"scene availability changed during draw");
            if(scene) {
                ++scenes;
                if(fraction==0 && unsmoothed && prior_camera) {
                    const auto current=unsmoothed->camera.eye;
                    if(s::dot(current-*prior_camera,current-*prior_camera)>1e-12) {
                        const auto delta=scene->camera.eye-*prior_camera;
                        if(s::dot(delta,delta)<1e-12) {++continuous;if(tick<200) ++start_continuous;}
                        else {++snaps;if(tick<200) ++start_snaps;}
                    }
                }
                check(scene->mesh==again->mesh,"scene extraction rebuilt immutable geometry");
                check(scene->camera.eye.x==again->camera.eye.x && scene->camera.eye.y==again->camera.eye.y &&
                      scene->camera.eye.z==again->camera.eye.z,"repeated sample changed camera");
                check(scene->overlay.pixels.size()==again->overlay.pixels.size() &&
                      !std::memcmp(scene->overlay.pixels.data(),again->overlay.pixels.data(),scene->overlay.pixels.size()*sizeof(platform::Color)),
                      "repeated sample changed cockpit");
            }
            const auto after=sampled->picture(),flat=baseline->picture();
            check(before.pixels.size()==after.pixels.size() && flat.pixels.size()==after.pixels.size(),"module image dimensions");
            check(!std::memcmp(before.pixels.data(),after.pixels.data(),after.pixels.size()*sizeof(platform::Color)),"scene changed flat picture");
            check(!std::memcmp(flat.pixels.data(),after.pixels.data(),after.pixels.size()*sizeof(platform::Color)),"scene changed future simulation picture");
            check(baseline->step_seconds()==sampled->step_seconds(),"scene changed simulation clock");
        }
        prior_camera=unsmoothed ? std::optional{unsmoothed->camera.eye} : std::nullopt;
    }
    std::printf("Camera continuity: %u interpolated moving ticks, %u discontinuities\n",continuous,snaps);
    check(start_continuous>20 && start_snaps==0,"camera history resets during the drop/start sequence");
    check(scenes>500,"module test did not enter practice race");
    sampled->set_option(1,0);check(!sampled->scene_surface(),"flat mode must stop scene output");
    std::printf("PASS %s: 500 module ticks, %u repeated scene samples; flat/future pictures and timing unchanged\n",league ? "league" : "practice",scenes);
}
platform::Image downsample(const platform::Image& image) {
    platform::Image out(image.width/2,image.height/2);
    for(int y=0;y<out.height;++y) for(int x=0;x<out.width;++x) {
        unsigned r=0,g=0,b=0;
        for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
            const auto c=image.pixels[std::size_t((y*2+dy)*image.width+x*2+dx)];
            r+=c.r;g+=c.g;b+=c.b;
        }
        out.put(x,y,{std::uint8_t(r/4),std::uint8_t(g/4),std::uint8_t(b/4),255});
    }
    return out;
}
}
int main(int argc,char** argv) try {
    synthetic();
    if(argc==1) return 0;
    if(argc==3 && std::string(argv[1])=="--module") { module_check(argv[2]);module_check(argv[2],true);return 0; }
    if(argc<3 || argc>4) throw std::runtime_error("Usage: stunt_scene_check [content output-directory [track-index]]");
    const auto assets=stuntcar::data::load_assets(argv[1]);
    const auto circuits=stuntcar::data::load_circuits(assets.program);
    {
        stuntcar::race::Race race(assets.program,circuits,0,false,stuntcar::race::Driver{9,0});
        for(int tick=0;tick<200;++tick) race.step({});
        const auto& o=race.state().opponent;
        check(std::abs(o.body[0]-o.road[0])<=1 && std::abs(o.body[1]-o.road[1])<=1,
              "live suspension does not settle at road height");
        check(std::abs(o.lift[0]-o.road[0]-0x50)<=1 && std::abs(o.lift[1]-o.road[1]-0x50)<=1,
              "live drawing reference differs from road plus drawing offset");
        std::puts("PASS live suspension equilibrium: body at road, drawing reference +80; preload is not ride height");
    }
    const auto palette=stuntcar::render::race_palette(assets.program);
    const auto scenery=stuntcar::data::load_scenery(assets.program);
    for(const auto& track:circuits.tracks) {
        const auto camera=stuntcar::render::start_camera(circuits,track);
        const auto before=stuntcar::render::draw_race_view(circuits,scenery,track,track.car_start_segment,camera);
        const auto mesh=stuntcar::render::track_scene(circuits,track,palette);
        const auto textured=stuntcar::render::track_scene(circuits,track,palette,true);
        check(textured.vertices.size()==mesh.vertices.size() && textured.triangles.size()==mesh.triangles.size(),"materials changed topology");
        for(std::size_t i=0;i<mesh.vertices.size();++i) {
            const auto a=mesh.vertices[i].position,b=textured.vertices[i].position;
            check(a.x==b.x && a.y==b.y && a.z==b.z,"materials moved track geometry");
            check(std::isfinite(textured.vertices[i].u) && std::isfinite(textured.vertices[i].v),"invalid texture coordinate");
        }
        check(!textured.materials[1].texture.empty() && !textured.materials[13].texture.empty(),"missing road/grass materials");
        const auto background=stuntcar::render::moving_scene(circuits,track,scenery,camera,{});
        check(!background.triangles.empty(),"missing horizon scenery");
        for(std::size_t segment=0;segment<track.segments.size();++segment) {
            const stuntcar::render::Opponent actor{int(segment),double(track.segments[segment].steps)-.25,128,{15,15,15,15}};
            const auto moving=stuntcar::render::moving_scene(circuits,track,scenery,camera,actor);
            check(moving.triangles.size()==background.triangles.size()+60,"opponent missing at segment join");
            for(const auto& v:moving.vertices) check(std::isfinite(v.position.x)&&std::isfinite(v.position.y)&&std::isfinite(v.position.z),"invalid dynamic scene point");
        }
        // Locate a flat interval and prove the mesh's tyre bottoms touch
        // the road at suspension rest, while a hop still raises the car.
        const auto sections=stuntcar::data::road(circuits,track);
        int row=0;bool checked_contact=false;
        for(std::size_t i=1;i<sections.size();++i) {
            const auto& a=sections[i-1];const auto& b=sections[i];
            if(a.segment!=b.segment) {row=0;continue;}
            if(!checked_contact && a.left_height==a.right_height && a.left_height==b.left_height && a.left_height==b.right_height) {
                const double rest=0x50; // drawing lift; spring preload is not ride height
                stuntcar::render::Opponent actor{a.segment,row+.1,128,{rest,rest,rest,rest}};
                const auto tyre_bottom=[&](const auto& actor) {
                    const auto moving=stuntcar::render::moving_scene(circuits,track,scenery,camera,actor);
                    double bottom=1e9;
                    for(std::size_t j=moving.vertices.size()-96;j<moving.vertices.size();++j)
                        bottom=std::min(bottom,moving.vertices[j].position.y);
                    return bottom;
                };
                const double grounded=tyre_bottom(actor);
                check(std::abs(grounded-a.left_height/1024.)<1e-8,"resting opponent tyres float above road");
                for(auto& lift:actor.lift) lift+=64;
                check(std::abs(tyre_bottom(actor)-grounded-64./1024.)<1e-8,"grounding erased opponent jump height");
                checked_contact=true;
            }
            ++row;
        }
        check(checked_contact,"no flat interval exercised for tyre contact");
        std::size_t intervals=0;
        const auto road=stuntcar::data::road(circuits,track);
        for(std::size_t i=1;i<road.size();++i) if(road[i-1].segment==road[i].segment) ++intervals;
        check(mesh.triangles.size()==intervals*10+4,"missing road intervals or duplicate joins");
        for(const auto& vertex:mesh.vertices) check(std::isfinite(vertex.position.y),"nonfinite geometry");
        const auto after=stuntcar::render::draw_race_view(circuits,scenery,track,track.car_start_segment,camera);
        check(before.pixels==after.pixels,"scene extraction changed original rendering");
        std::printf("PASS %s: %zu road intervals, %zu triangles, original view unchanged\n",
                    track.name.c_str(),intervals,mesh.triangles.size());
    }
    const int selected=argc==4 ? std::stoi(argv[3]) : 0;
    const auto& track=circuits.tracks.at(std::size_t(selected));
    const auto mesh=stuntcar::render::track_scene(circuits,track,palette);
    auto view=stuntcar::render::perspective_camera(stuntcar::render::start_camera(circuits,track));
    const std::filesystem::path out=argv[2]; std::filesystem::create_directories(out);
    const auto sky=palette[7];
    const auto art=stuntcar::data::load_cockpit_art(assets.program);
    stuntcar::render::View zero,one; zero.pixels.fill(0);one.pixels.fill(1);
    const auto border=stuntcar::render::draw_race_screen(art,zero,palette,{});
    const auto mask=stuntcar::render::draw_race_screen(art,one,palette,{});
    // Frozen stereo study: 1.5m x .9375m panel, 2m away, nominal IPD 64mm.
    // Scale is an explicit presentation hypothesis, not recovered physics.
    constexpr double metres_per_unit=10, distance=2, ipd=.064;
    for(int eye=0;eye<2;++eye) {
        auto v=view;
        const double offset=(eye ? .5 : -.5)*ipd;
        v.eye=v.eye+v.right*(offset/metres_per_unit);
        v.left=(-.6-offset)/distance; v.right_edge=(.6-offset)/distance;
        v.top=.39375/distance; v.bottom=-.20625/distance;
        const auto image=downsample(s::reference_image(mesh,v,2048,1024,{sky.r,sky.g,sky.b,255}));
        platform::Image composed(1280,800);
        for(int y=0;y<800;++y) for(int x=0;x<1280;++x) {
            const auto at=std::size_t((y/4)*320+x/4);
            const auto a=border.pixels[at],b=mask.pixels[at];
            const bool hole=a.r!=b.r || a.g!=b.g || a.b!=b.b;
            check(!hole || (x>=128 && x<1152 && y>=64 && y<576),"cockpit aperture outside expected view");
            composed.put(x,y,hole ? image.pixels[std::size_t((y-64)*1024+x-128)] : a);
        }
        platform::save_png(composed,(out/(eye ? "right.png" : "left.png")).string());
    }
    // Two full-circuit diagnostics, independent of the driver's visibility walk.
    for(int side=0;side<2;++side) {
        auto inspected=mesh;
        if(side) std::erase_if(inspected.triangles,[](const auto& triangle){return triangle.material==13;}); // remove ground for underside inspection
        s::View overview;
        overview.eye=side ? s::Vec3{75,-90,64} : s::Vec3{155,100,-20};
        overview.forward=unit(s::Vec3{64,4,64}-overview.eye);
        overview.right=unit(cross({0,1,0},overview.forward));
        overview.up=cross(overview.forward,overview.right);
        overview.bottom=-.65;overview.top=.65;
        platform::save_png(s::reference_image(inspected,overview,960,624,{sky.r,sky.g,sky.b,255}),
                           (out/(side ? "below.png" : "overview.png")).string());
    }
    {
        const auto camera=stuntcar::render::start_camera(circuits,track);
        const double rest=0x50; // drawing lift; spring preload is not ride height
        const stuntcar::render::Opponent actor{track.car_start_segment,
            track.segments[track.car_start_segment].steps*.5+1.5,128,{rest,rest,rest,rest}};
        auto combined=mesh;
        const auto moving=stuntcar::render::moving_scene(circuits,track,scenery,camera,actor);
        const unsigned offset=unsigned(combined.vertices.size());
        combined.vertices.insert(combined.vertices.end(),moving.vertices.begin(),moving.vertices.end());
        for(auto triangle:moving.triangles) { for(auto& v:triangle.vertices) v+=offset; combined.triangles.push_back(triangle); }
        platform::save_png(s::reference_image(combined,view,1280,640,{sky.r,sky.g,sky.b,255}),(out/"opponent.png").string());
    }
    std::ofstream manifest(out/"stereo.txt");
    manifest<<"REPRISE_STEREO_STUDY_V1 1280 800 1.5 0.9375 2 0.064\n"<<track.name<<'\n';
    std::printf("Frozen stereo study exported for %s; no simulation or opponent. Scale: 10m per scene unit.\n",track.name.c_str());
    return 0;
} catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
