#include "astro_bot_ps4/reprise/import.hpp"
#include "astro_bot_ps4/reprise/viewer.hpp"
#include "app/library.hpp"
#include "platform/host.hpp"
#include "platform/sha256.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <cmath>
#include <cstring>

static void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
int main(int argc,char** argv) {
    try {
        const auto tmp=std::filesystem::temp_directory_path()/("beacon-assets-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(tmp);
        struct Cleanup {std::filesystem::path path; ~Cleanup(){std::filesystem::remove_all(path);}} cleanup{tmp};
        auto reject=[&](const std::vector<std::uint8_t>& bytes,bool other){
            const auto image=tmp/"bad.beacon";
            {std::ofstream out(image,std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
            platform::FileSource file(image);
            bool failed=false,not_this=false;
            try {beacon::reprise::import_assets(file,tmp/"output",{});}
            catch(const app::NotThisGame&) {failed=true;not_this=true;}
            catch(const std::runtime_error&) {failed=true;}
            require(failed && not_this==other,"Importer rejection category is wrong");
        };
        reject(std::vector<std::uint8_t>(20),true);
        const std::string magic("BEACON-ASSETS-1\n\0",16);
        std::vector<std::uint8_t> header(magic.begin(),magic.end()); header.resize(20);
        reject(header,false); // zero count
        header[16]=1; reject(header,false); // missing record
        auto oversized=header; oversized.push_back(255);oversized.push_back(255);oversized.resize(62);reject(oversized,false);
        auto traversal=header; std::string name="../outside";
        traversal.push_back(name.size());traversal.push_back(0);traversal.resize(62);traversal.insert(traversal.end(),name.begin(),name.end());reject(traversal,false);
        auto identity=header; name="sce_sys/param.sfo"; identity.push_back(name.size());identity.push_back(0);identity.resize(62);identity.insert(identity.end(),name.begin(),name.end());reject(identity,false);
        require(!std::filesystem::exists(tmp.parent_path()/"outside"),"Traversal escaped staging");
        // The shared textured path must display an original texel, not just material colour.
        ui::Model triangle;
        triangle.points={{{-1,-1,0}},{{1,-1,0}},{{0,1,0}}};
        auto tex=std::make_shared<platform::Image>(1,1);tex->pixels[0]={240,20,10};triangle.textures.push_back(tex);
        triangle.faces.push_back({{0,1,2},{255,255,255},{{0,0},{0,0},{0,0}},0,0,true});
        platform::Image image(100,100); ui::draw_model(image,0,0,100,100,triangle,0,0);
        const auto pixel=image.pixels[50*100+50];require(pixel.r==240 && pixel.g==20,"Texture sampler did not preserve colour");
        std::cout<<"Asset-free import and texture checks passed\n";
        if(argc==1) return 0;
        if(argc!=4) throw std::runtime_error("usage: beacon_assets_check [BUNDLE DATA_DIR CAPTURE_DIR]");
        app::GameEntry entry;entry.id="astro_bot_ps4";entry.title="Beacon characters";
        entry.import_into=beacon::reprise::import_assets;entry.largest_image=2ull<<30;entry.previews=beacon::reprise::previews;
        app::Library library(argv[2],{entry});
        if(!library.installed(0)) {
            const auto result=library.import(argv[1],0);
            if(!result.ok) throw std::runtime_error(result.message);
        }
        require(library.verify(0),"Imported folder failed verification");
        app::Library reopened(argv[2],{entry});require(reopened.installed(0),"Fresh library lost imported assets");
        const auto items=entry.previews(library.content_directory(0));
        std::cout<<items.size()<<" character previews\n";
        std::filesystem::create_directories(argv[3]);
        for(std::size_t n=0;n<std::min<std::size_t>(2,items.size());++n) {
            auto model=items[n].animated_model();
            std::cout<<items[n].title<<": "<<model->model().points.size()<<" vertices, "<<model->model().faces.size()<<" faces, "<<model->clips().size()<<" clips\n";
            for(std::size_t i=1;i<model->clips().size();++i) if(model->clips()[i].name=="walk" || model->clips()[i].name=="idle") {
                model->select(i); model->pose(0); const auto first=model->model().points;
                model->pose(model->clips()[i].duration*.37);
                require(first!=model->model().points,"Animation did not change the character pose");
                break;
            }
            auto start=std::chrono::steady_clock::now();
            platform::Image capture(800,600);
            for(auto& p:capture.pixels)p={20,30,34};
            ui::draw_model(capture,0,0,800,600,model->model(),-.3,.1);
            std::cout<<"render "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms\n";
            platform::save_png(capture,std::filesystem::path(argv[3])/(items[n].title+".png"));
        }
        int failures=0;
        for(std::size_t n=2;n<items.size();++n) {
            try {
                if(items[n].load_sound) {
                    auto sound=items[n].load_sound();
                    require(sound.frames()>0 && sound.rate>0,"Empty music");
                    require(std::any_of(sound.samples.begin(),sound.samples.end(),[](float v){return v!=0;}),"Silent music");
                    std::cout<<items[n].title<<": "<<sound.frames()<<" frames, "<<sound.channels<<" channels, "<<sound.rate<<" Hz\n";
                } else {auto model=items[n].animated_model(); require(!model->model().faces.empty(),"No visible faces");}
            }
            catch(const std::exception& e) { ++failures; std::cout<<"VIEWER LIMIT "<<items[n].title<<": "<<e.what()<<'\n'; }
        }
        std::cout<<"Model loading failures: "<<failures<<'\n';
        require(failures==0,"Some visible models failed to load");
        // A failed replacement must preserve the installed content.
        const auto before=library.installed_files(0);
        const auto bad=library.import(tmp/"bad.beacon",0);
        require(!bad.ok && library.installed(0) && before==library.installed_files(0),"Failed import replaced the good installation");
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
