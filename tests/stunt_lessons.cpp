// Run without imports to check packaged prose; pass a content directory to
// exercise actual imported diagrams, scrolling, resize and touch navigation.
#include "stunt_car_racer_amiga/lessons/technical_article.hpp"
#include "platform/resources.hpp"
#include <SDL3/SDL.h>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <set>
#include <sstream>
void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
bool same(const platform::Image& a,const platform::Image& b) {
    return a.width==b.width && a.height==b.height &&
        std::memcmp(a.pixels.data(),b.pixels.data(),a.pixels.size()*sizeof(platform::Color))==0;
}
int main(int argc,char** argv) {
    try {
        const std::set<std::string> figures={"race","angles","projection","road","profile","surface","springs","damping","opponent_marks","opponent_choices","opponent_contact"};
        for(const char* name:{"projection","physics","opponent"}) {
            auto bytes=platform::resource(std::string("articles/stunt_car_racer_")+name+".txt");
            std::string text(bytes.begin(),bytes.end());
            check(text.starts_with("# "),"Article needs a title");
            check(text.find("## Evidence and scope")!=std::string::npos,"Article needs evidence scope");
            std::istringstream in(text); std::string line; int count=0;
            while(std::getline(in,line)) if(line.starts_with("!figure ")) {
                check(figures.contains(line.substr(8)),"Unknown figure reference");++count;
            }
            check(count==4,"Each article has four explanatory figures");
        }
        if(argc>1) for(int chapter=0;chapter<3;++chapter) {
            stuntcar::lessons::TechnicalArticle lesson(argv[1],chapter);
            for(auto size:{std::pair{960,640},std::pair{640,1138},std::pair{1422,640}}) {
                lesson.set_canvas(size.first,size.second);
                platform::Controls in; in.wheel=-10000; lesson.update(in,0);
                check(lesson.scroll()==0,"Top clamp");
                const auto first=lesson.draw();
                check(first.width==size.first && first.height==size.second,"Responsive dimensions");
                in={};in.wheel_precise=.5F;lesson.update(in,0);
                check(lesson.scroll()==28,"Fractional wheel scrolling");
                in={};in.nav_down=true;lesson.update(in,0);
                check(lesson.scroll()==72,"Directional scrolling");
                in={};in.click=true;in.mouse_held=true;in.mouse_x=size.first/2;in.mouse_y=300;
                in.ui_pointer=std::array<int,2>{size.first/2,300};lesson.update(in,0);
                in.click=false;in.ui_pointer=std::array<int,2>{size.first/2,200};lesson.update(in,0);
                check(lesson.scroll()==172,"Touch/mouse drag");
                in={};in.wheel=-10000;lesson.update(in,0);
                int capture=0;
                while(true) {
                    if(argc>2 && size.first!=1422) {
                        std::filesystem::create_directories(argv[2]);
                        platform::save_png(lesson.draw(),std::filesystem::path(argv[2])/
                            (std::to_string(chapter)+"-"+std::to_string(size.first)+"-"+std::to_string(capture++)+".png"));
                    }
                    if(lesson.scroll()==lesson.scroll_limit()) break;
                    in={};in.page_down=true;lesson.update(in,0);
                }
                check(!same(first,lesson.draw()),"Article actually scrolls");
                auto end=lesson.draw();in={};in.wheel=10000;lesson.update(in,0);
                check(same(end,lesson.draw()),"Bottom clamp / final paragraph reachable");
            }
            // Resizing a finished article retains its relative reading position.
            lesson.set_canvas(640,1138);
            check(lesson.scroll()==lesson.scroll_limit(),"Resize preserves end position");
        }
        std::cout<<"Technical articles OK"<<(argc>1?" (imported figures, three aspect ratios, wheel, keys, drag, bounds)":" (packaged text)")<<"\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
