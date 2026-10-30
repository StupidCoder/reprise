#include "app/touch_controls.hpp"
#include "ui/shelf.hpp"
#include "ui/control_icons.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <map>
#include <tuple>
namespace app {
struct TouchPainter::Cache {
    using Key = std::tuple<std::string,int,int>;
    SDL_Renderer* renderer{};
    std::map<Key,SDL_Texture*> labels;
    std::map<std::pair<platform::ControlIcon,int>,SDL_Texture*> icons;
    void clear() {
        for (auto& [key,t]:labels) SDL_DestroyTexture(t);
        for (auto& [key,t]:icons) SDL_DestroyTexture(t);
        labels.clear(); icons.clear();
    }
    ~Cache() { clear(); }
};
TouchPainter::TouchPainter() : cache_(std::make_unique<Cache>()) {}
TouchPainter::~TouchPainter() = default;
void TouchPainter::draw(const platform::TouchDeck& deck, const platform::GpuFrame& frame) {
    if (!deck.active()) return;
    auto* r=frame.renderer;
    if (cache_->renderer!=r) { cache_->clear(); cache_->renderer=r; }
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    auto circle=[&](float x,float y,float radius, Uint8 alpha) {
        SDL_SetRenderDrawColor(r,235,242,250,alpha);
        for(int n=0;n<64;++n) {
            const float a=float(n)*6.2831853f/64, b=float(n+1)*6.2831853f/64;
            SDL_RenderLine(r,x+std::cos(a)*radius,y+std::sin(a)*radius,x+std::cos(b)*radius,y+std::sin(b)*radius);
        }
    };
    for(std::size_t k=0;k<deck.controls().size();++k) {
        const auto& c=deck.controls()[k]; const auto& v=deck.values()[k];
        const auto b=deck.bounds(k);
        const float cx=b.x+b.width/2,cy=b.y+b.height/2;
        SDL_SetRenderDrawColor(r,12,22,35,v.held?110:65);
        SDL_FRect box{b.x,b.y,b.width,b.height};
        if(c.kind==platform::TouchControl::Kind::stick) {
            auto [x,y]=deck.centre(k); const float radius=std::min(b.width,b.height)*.35f;
            circle(x,y,radius,100); circle(x+v.x*radius,y+v.y*radius,radius*.28f,200);
        } else {
            SDL_RenderFillRect(r,&box);
            SDL_SetRenderDrawColor(r,235,242,250,v.held?210:100); SDL_RenderRect(r,&box);
            if(c.kind==platform::TouchControl::Kind::slider) {
                const float y=b.y+(1-v.y)*b.height;
                SDL_RenderLine(r,cx,b.y,cx,b.y+b.height);
                SDL_FRect knob{b.x,y-3,b.width,6}; SDL_RenderFillRect(r,&knob);
            }
        }
        const bool has_icon=c.kind==platform::TouchControl::Kind::button && c.icon!=platform::ControlIcon::none;
        if (has_icon) {
            const int size=std::max(1,int(std::lround(std::min(b.width,b.height)*.58f)));
            const auto key=std::pair{c.icon,size};
            if (!cache_->icons.contains(key)) {
                if (cache_->icons.size()>=64) cache_->clear();
                const auto tile=ui::control_icon(c.icon,size);
                auto* texture=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,size,size);
                if (texture) {
                    SDL_UpdateTexture(texture,nullptr,tile.pixels.data(),size*int(sizeof(platform::Color)));
                    SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
                    SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_LINEAR);
                }
                cache_->icons.emplace(key,texture);
            }
            if (auto* texture=cache_->icons.at(key)) {
                SDL_SetTextureAlphaMod(texture,v.held?210:100);
                SDL_FRect at{std::round(cx-float(size)/2),std::round(cy-float(size)/2),float(size),float(size)};
                SDL_RenderTexture(r,texture,nullptr,&at);
            }
        }
        if(!has_icon && !c.label.empty()) {
            // Small native-resolution text tiles, never a full-screen CPU overlay.
            const int size=std::max(12,int(std::min(frame.width,frame.height)*.024f));
            const int width=std::max(1,int(b.width));
            const Cache::Key key{c.label,width,size};
            if (!cache_->labels.contains(key)) {
                // Bound cache growth during continuous window resizing.
                if (cache_->labels.size()>=64) cache_->clear();
                platform::Image label(width,size*2);
                for(auto& p:label.pixels) p={0,0,0,0};
                ui::shelf::text(label,c.label,4,2,size,{235,242,250,180});
                auto* texture=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,width,size*2);
                if (texture) {
                    SDL_UpdateTexture(texture,nullptr,label.pixels.data(),width*int(sizeof(platform::Color)));
                    SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
                }
                cache_->labels.emplace(key,texture);
            }
            if (auto* texture=cache_->labels.at(key)) {
                SDL_FRect at{b.x,cy-float(size),float(width),float(size*2)};
                if(c.kind==platform::TouchControl::Kind::stick) at.y=b.y;
                SDL_RenderTexture(r,texture,nullptr,&at);
            }
        }
    }
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
}
}
