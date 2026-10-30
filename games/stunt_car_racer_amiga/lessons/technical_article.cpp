#include "stunt_car_racer_amiga/lessons/technical_article.hpp"
#include "stunt_car_racer_amiga/data/scenery.hpp"
#include "stunt_car_racer_amiga/render/race_view.hpp"
#include "platform/resources.hpp"
#include "ui/shelf.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace stuntcar::lessons {
namespace s = ui::shelf;
namespace {
using platform::Image;
using platform::Color;
constexpr Color orange{185, 83, 39};
void line(Image& im, int x, int y, int xx, int yy, Color ink, int thick = 2) {
    const int n = std::max(std::abs(xx-x), std::abs(yy-y));
    for (int i=0; i<=n; ++i) {
        const double t = n ? double(i)/n : 0;
        ui::rect(im, int(std::lround(x+(xx-x)*t)), int(std::lround(y+(yy-y)*t)), thick, thick, ink);
    }
}
void dot(Image& im, int x, int y, Color ink, int radius = 6) {
    ui::ring(im,x,y,radius,radius,ink,0,0,im.width,im.height);
}
void arrow(Image& im,int x,int y,int xx,int yy,Color ink) {
    line(im,x,y,xx,yy,ink);
    const double a=std::atan2(yy-y,xx-x);
    for(double d:{-.5,.5}) line(im,xx,yy,xx-int(10*std::cos(a+d)),yy-int(10*std::sin(a+d)),ink);
}
void label(Image& im,const std::string& text,int x,int y,Color c=s::muted) {
    s::text(im,text,x,y,15,c);
}
std::string number(double value) {
    return std::to_string(int(std::lround(value)));
}
struct Block { std::string text; int size{}, y{}, height{}; bool figure{}; };
}

TechnicalArticle::TechnicalArticle(const std::filesystem::path& content, int chapter) : chapter_(chapter) {
    if(chapter<0 || chapter>1) throw std::runtime_error("Unknown Stunt Car Racer article.");
    const auto assets=data::load_assets(content);
    circuits_=data::load_circuits(assets.program);
    if(circuits_.tracks.size()<3) throw std::runtime_error("The article needs the imported circuits.");
    // Hump Back makes both the elevation profile and track sampling readable.
    const auto& track=circuits_.tracks[2];
    road_=data::road(circuits_,track);
    if(road_.size()<2) throw std::runtime_error("No road samples available for the article.");
    auto camera=render::start_camera(circuits_,track);
    camera.eye+=600; camera.pitch=1800; // elevated explanatory view, not a gameplay capture
    race_=render::to_image(render::draw_race_view(circuits_,data::load_scenery(assets.program),track,
        track.car_start_segment,camera),render::race_palette(assets.program));
    const auto bytes=platform::resource(chapter==0 ? "articles/stunt_car_racer_projection.txt" :
                                                   "articles/stunt_car_racer_physics.txt");
    article_.assign(bytes.begin(),bytes.end());
    if(article_.empty()) throw std::runtime_error("The technical article is missing.");
    compose();
}

void TechnicalArticle::set_canvas(int width,int height) {
    width=std::max(480,width); height=std::max(480,height);
    if(width==width_ && height==height_) return;
    const double fraction=scroll_limit()>0 ? scroll_/scroll_limit() : 0;
    width_=width; height_=height;
    compose();
    scroll_=fraction*scroll_limit();
    drag_y_.reset();
}

void TechnicalArticle::compose() {
    const int w=std::min(880,width_-64), inner=w-48;
    std::vector<Block> blocks;
    std::istringstream input(article_);
    std::string text;
    int y=28;
    Image measure(1,1);
    while(std::getline(input,text)) {
        if(text.empty()) continue;
        Block b; b.text=text; b.y=y; b.size=20;
        if(text.starts_with("!figure ")) {
            b.figure=true; b.text=text.substr(8); b.height=figure(b.text,inner).height;
        } else {
            if(text.starts_with("# ")) { b.text=text.substr(2); b.size=34; }
            else if(text.starts_with("## ")) { b.text=text.substr(3); b.size=25; y+=16; b.y=y; }
            b.height=s::prose(measure,b.text,0,0,inner,b.size,s::ink);
        }
        y+=b.height+24;
        blocks.push_back(std::move(b));
    }
    page_=Image(w,y+12);
    ui::fill(page_,s::card);
    for(const auto& b:blocks) {
        if(b.figure) {
            const auto im=figure(b.text,inner);
            for(int py=0;py<im.height;++py)
                for(int px=0;px<im.width;++px)
                    page_.put(24+px,b.y+py,im.pixels[std::size_t(py*im.width+px)]);
        } else s::prose(page_,b.text,24,b.y,inner,b.size,b.size==20?s::ink:s::cobalt);
    }
}

Image TechnicalArticle::figure(const std::string& kind,int w) const {
    const int h=kind=="race" ? w/2+68 : kind=="surface" ? 360 : 340;
    Image im(w,h); ui::fill(im,s::pale);
    const int left=32, right=w-32;
    if(kind=="race") {
        ui::picture(im,race_,0,0,w,h-68);
        label(im,"HUMP BACK / imported track, elevated reconstructed view",16,h-48);
        label(im,"Original picture window: 256 x 128 pixels",16,h-26);
    } else if(kind=="angles") {
        label(im,"TOP VIEW / equally spaced viewing directions",18,16);
        const int cx=w/2, cy=272;
        for(int a:{-20,-10,0,10,20}) {
            const double r=a*std::numbers::pi/180;
            const int x=cx+int(std::sin(r)*195), y=cy-int(std::cos(r)*195);
            line(im,cx,cy,x,y,a==0?s::ink:s::cobalt);
            dot(im,x,y,s::cobalt,4);
            if(a==0 || std::abs(a)==20) label(im,std::to_string(a)+" deg",x-22,48);
        }
        dot(im,cx,cy,s::ink);
        label(im,"eye",cx+12,cy-6);
        label(im,"Each ray is 10 degrees apart / about 57 screen pixels",18,300,s::ink);
    } else if(kind=="projection") {
        label(im,"ANGLE TO SCREEN / same scale at the centre",18,16);
        const int x0=54,x1=w-24,y0=242,y1=58;
        line(im,x0,y0,x1,y0,s::muted); line(im,x0,y0,x0,y1,s::muted);
        const auto point=[&](double deg,bool perspective) {
            const double a=deg*std::numbers::pi/180;
            const double pixels=2048/(2*std::numbers::pi)*(perspective?std::tan(a):a);
            return std::array<int,2>{x0+int(deg/45*(x1-x0)),y0-int(pixels/340*(y0-y1))};
        };
        for(bool perspective:{false,true}) for(int deg=1;deg<=45;++deg) {
            auto a=point(deg-1,perspective),b=point(deg,perspective);
            line(im,a[0],a[1],b[0],b[1],perspective?orange:s::cobalt,3);
        }
        for(int deg:{0,15,30,45}) { auto p=point(deg,false); label(im,std::to_string(deg),p[0]-8,250); }
        label(im,"pixels",8,36); label(im,"340",8,62); label(im,"bearing (degrees)",w/2-60,276);
        label(im,"Angle projection",18,307,s::cobalt);
        label(im,"Perspective",w/2,307,orange);
        // The view ends at 22.5 degrees from centre; the remainder is explanatory.
        auto edge=point(22.5,false);
        line(im,edge[0],y0,edge[0],y1,s::line);
        label(im,"view edge",edge[0]-38,38);
    } else if(kind=="road" || kind=="profile") {
        label(im,kind=="road"?"HUMP BACK / decoded road cross-sections":"HUMP BACK / height along the complete circuit",18,16);
        if(kind=="road") {
            const auto plot=[&](std::size_t begin,std::size_t end,int x,int y,int width,int height) {
                int minx=100000,maxx=-100000,minz=minx,maxz=maxx;
                for(std::size_t i=begin;i<end;++i) for(auto p:{road_[i].left,road_[i].right}) {
                    minx=std::min(minx,p.x); maxx=std::max(maxx,p.x);
                    minz=std::min(minz,p.z); maxz=std::max(maxz,p.z);
                }
                const double scale=std::min(double(width)/std::max(1,maxx-minx),double(height)/std::max(1,maxz-minz));
                const int ox=x+int((width-(maxx-minx)*scale)/2),oy=y+int((height-(maxz-minz)*scale)/2);
                auto pt=[&](data::PlanPoint p) {return std::array<int,2>{ox+int((p.x-minx)*scale),oy+int((p.z-minz)*scale)};};
                for(std::size_t i=begin;i<end;++i) {
                    auto a=pt(road_[i].left),b=pt(road_[i].right);
                    line(im,a[0],a[1],b[0],b[1],s::muted,1);
                    if(i>begin) {
                        auto c=pt(road_[i-1].left),d=pt(road_[i-1].right);
                        line(im,a[0],a[1],c[0],c[1],s::cobalt);
                        line(im,b[0],b[1],d[0],d[1],orange);
                    }
                }
            };
            plot(0,road_.size(),20,66,w*3/5-30,218);
            std::size_t end=1;
            while(end<road_.size() && road_[end].segment==road_[0].segment) ++end;
            label(im,"One segment",w*3/5+10,48);
            label(im,"enlarged",w*3/5+10,69);
            plot(0,end,w*3/5+10,104,w*2/5-30,180);
            label(im,std::to_string(circuits_.tracks[2].segments.size())+" segments / "+std::to_string(road_.size())+" decoded pairs",18,307);
        } else {
            std::vector<double> distance(road_.size());
            int low=100000,high=-100000;
            for(std::size_t i=0;i<road_.size();++i) {
                auto& a=road_[i]; low=std::min({low,a.left_height,a.right_height}); high=std::max({high,a.left_height,a.right_height});
                if(i) {auto& b=road_[i-1]; distance[i]=distance[i-1]+std::hypot(
                    (a.left.x+a.right.x-b.left.x-b.right.x)/2.0,(a.left.z+a.right.z-b.left.z-b.right.z)/2.0);}
            }
            auto pt=[&](std::size_t i,bool right_edge){return std::array<int,2>{left+int(distance[i]/std::max(1.0,distance.back())*(right-left)),
                260-int(double((right_edge?road_[i].right_height:road_[i].left_height)-low)/std::max(1,high-low)*190)};};
            for(bool edge:{false,true}) for(std::size_t i=1;i<road_.size();++i) {
                auto a=pt(i-1,edge),b=pt(i,edge);line(im,a[0],a[1],b[0],b[1],edge?orange:s::cobalt);
            }
            label(im,"Height range: "+number(low/4.0)+" to "+number(high/4.0)+" plan units",18,39);
            label(im,"start",left,270); label(im,"one circuit",right-80,270);
            label(im,"Left edge",18,307,s::cobalt); label(im,"Right edge",w/2,307,orange);
        }
    } else if(kind=="surface") {
        // Select a non-degenerate patch with a real rise. Do not invent terrain values.
        std::size_t at=0;
        for(std::size_t i=0;i+1<road_.size();++i)
            if(road_[i].segment==road_[i+1].segment && road_[i].left_height!=road_[i+1].left_height) {at=i;break;}
        const auto& a=road_[at]; const auto& b=road_[at+1];
        label(im,"ONE IMPORTED ROAD PATCH / stored height units",18,16);
        const std::array<std::array<int,2>,4> p={{{w/4,98},{3*w/4,98},{w/4,244},{3*w/4,244}}};
        line(im,p[0][0],98,p[1][0],98,s::ink);line(im,p[2][0],244,p[3][0],244,s::ink);
        line(im,p[0][0],98,p[2][0],244,s::cobalt);line(im,p[1][0],98,p[3][0],244,orange);
        const int values[]={a.left_height,a.right_height,b.left_height,b.right_height};
        for(int i=0;i<4;++i){dot(im,p[i][0],p[i][1],s::ink);label(im,std::to_string(values[i]),p[i][0]-24,p[i][1]+(i<2?-30:14));}
        const double centre=(a.left_height+a.right_height+b.left_height+b.right_height)/4.0;
        dot(im,w/2,171,s::green); label(im,"centre = "+number(centre),w/2-52,190,s::green);
        label(im,"Each corner contributes 25% at the centre.",18,300);
        label(im,"Diagram unfolded for clarity; heights are not drawn to scale.",18,326);
    } else if(kind=="springs") {
        label(im,"THREE SUPPORTS / schematic weighting",18,16);
        const int x=w/2;
        line(im,x-100,100,x+100,100,s::muted);line(im,x-100,100,x,242,s::muted);line(im,x+100,100,x,242,s::muted);
        dot(im,x-100,100,s::cobalt,12);dot(im,x+100,100,s::cobalt,12);dot(im,x,242,orange,16);
        label(im,"A / 25%",x-132,63,s::cobalt);label(im,"B / 25%",x+65,63,s::cobalt);
        label(im,"C / 50%",x+25,235,orange);
        arrow(im,x,170,x,125,s::green);label(im,"lift",x+12,149,s::green);
        label(im,"Lift = (average of A and B + C) / 2",18,290,s::ink);
        label(im,"Unequal support forces also turn the body.",18,315);
    } else if(kind=="damping") {
        label(im,"SAME COMPRESSION / different recent movement",18,16);
        const char* names[]={"Steady","Compressing","Extending"};
        const int previous[]={400,200,600};
        for(int i=0;i<3;++i) {
            const int y=72+i*66;
            const int force=400+int(std::floor((400-previous[i])*276.0/256));
            label(im,names[i],18,y,s::ink);
            const int start=150,barw=int(double(force)/700*(w-start-76));
            ui::rect(im,start,y,barw,22,i==1?orange:s::cobalt);
            label(im,std::to_string(force),start+barw+8,y);
            label(im,"previous "+std::to_string(previous[i])+" / now 400",start,y+25);
        }
        label(im,"Force = compression + 1.078 x change",18,288,s::ink);
        label(im,"Illustrative values, before limits; not a landing replay.",18,315);
    } else throw std::runtime_error("Unknown technical article figure: "+kind);
    return im;
}

void TechnicalArticle::update(const platform::Controls& in,double) {
    const double wheel=in.wheel_precise!=0?in.wheel_precise:double(in.wheel);
    scroll_+=wheel*56+(int(in.nav_down)-int(in.nav_up))*44+
        (int(in.page_down)-int(in.page_up))*(height_-164);
    const int x=(width_-page_.width)/2;
    if(in.click && in.mouse_x>=x && in.mouse_x<x+page_.width && in.mouse_y>=84 && in.mouse_y<height_-44)
        drag_y_=in.mouse_y;
    if(drag_y_ && in.ui_pointer && in.mouse_held) {
        const int y=(*in.ui_pointer)[1]; scroll_+=*drag_y_-y; drag_y_=y;
    }
    if(!in.mouse_held) drag_y_.reset();
    scroll_=std::clamp(scroll_,0.0,double(scroll_limit()));
}

platform::Image TechnicalArticle::draw() const {
    Image im(width_,height_);ui::fill(im,s::paper);
    s::button(im,"< Lessons",24,20,104,34);
    s::text(im,"Stunt Car Racer",148,22,23,s::ink,true);
    s::text(im,chapter_==0?"01 / THE ANGULAR CAMERA":"02 / THE SPRUNG CAR",24,62,13,s::muted);
    const int x=(width_-page_.width)/2, visible=height_-128;
    for(int y=0;y<visible && y+int(scroll_)<page_.height;++y)
        for(int px=0;px<page_.width;++px)
            im.put(x+px,84+y,page_.pixels[std::size_t((y+int(scroll_))*page_.width+px)]);
    if(scroll_limit()>0) {
        const int thumb=std::max(24,visible*visible/page_.height);
        ui::rect(im,x+page_.width+8,84,4,visible,s::line);
        ui::rect(im,x+page_.width+8,84+int((visible-thumb)*scroll_/scroll_limit()),4,thumb,s::muted);
    }
    label(im,"Illustrated article / swipe, scroll or use direction and page keys",24,height_-28);
    return im;
}
} // namespace stuntcar::lessons
