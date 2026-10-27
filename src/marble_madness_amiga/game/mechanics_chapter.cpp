#include "marble_madness_amiga/game/mechanics_chapter.hpp"
#include "ui/shelf.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <set>

namespace marble_madness::game {
namespace {
namespace s=ui::shelf;
using platform::Image;
using platform::Color;
bool hit(const platform::Controls& c,int x,int y,int w,int h) {
    return c.click && c.mouse_x>=x && c.mouse_x<x+w && c.mouse_y>=y && c.mouse_y<y+h;
}
void line(Image& im,int x,int y,int xx,int yy,Color c) {
    const int n=std::max(std::abs(xx-x),std::abs(yy-y));
    for(int i=0;i<=n;++i) im.put(x+(xx-x)*i/std::max(1,n),y+(yy-y)*i/std::max(1,n),c);
}
void dot(Image& im,int x,int y,Color c,int r=4) {ui::rounded(im,x-r,y-r,r*2+1,r*2+1,r,c);}
std::string number(double v) {std::ostringstream o;o<<std::fixed<<std::setprecision(2)<<v;return o.str();}
const char* mode(const Marble& m) {
    if(!m.status)return "Gone";
    if(m.air==2)return "Falling";
    switch(m.mode) {
    case Marble::patrolling:return "Patrol";case Marble::hunting:return "Hunt";
    case Marble::chasing:return "Chase";case Marble::returning:return "Return";
    case Marble::knocked:return "Knocked";case Marble::breaking:return "Broken";
    case Marble::respawning:return "Respawning";case Marble::landing:return "Landing";
    case Marble::bumped:return "Bumped";default:return "Rolling";
    }
}
struct Page {const char* title;const char* body;const char* takeaway;};
constexpr Page physics[] = {
    {"Two worlds, one convincing course",
     "The picture is a tiled bitmap. The marble does not collide with its colored pixels: it moves through a separate model with X, Y and height. An isometric projection puts that moving point back onto the picture.\n\nThe wireframe beside this text samples the imported terrain around the marble. Each diamond is one physical cell, eight world units across. It is an illustration of the collision surface, not the renderer used by the original game.",
     "A painted wall and a physical wall agree because the course's artwork and terrain data were authored to fit."},
    {"Four corners, two triangles",
     "Inside a cell, the ground height comes from one of two triangles. The marble's position within the cell chooses the triangle; its corner heights give the local slopes.\n\nA grid vertex holds four heights, one for each neighboring cell. Those heights need not match. The same shared corner can be high on one side and low on the other: a wall or a drop, rather than a smooth hill.",
     "The colored samples use the actual terrain probe. The diagonal marks where the selected triangle changes."},
    {"Momentum meets the ground",
     "A push changes velocity, rather than teleporting the marble. Slope forces pull it downhill; surface-dependent friction and a speed limit shape the motion. In the air, gravity changes vertical velocity.\n\nEach update advances the position, examines side and corner heights, handles walls, obstacles and other marbles, then resolves ground contact or falling. A wall reverses the velocity component facing it. Landing severity can make the marble bounce or shatter.",
     "This is a purpose-built 3D motion model with authored terrain and special obstacles, not a general-purpose rigid-body engine."},
    {"Cut the sprite, reveal the wall",
     "The marble sprite would normally sit in front of the landscape. To hide it behind a wall, the game selects cover masks for the eight-pixel screen columns it touches.\n\nTerrain heights and wall-face flags choose covers, with authored lists for special cases. The selected masks cut pixels from the sprite. Entirely covered sprites disappear. Moving obstacles can supply additional masks: the drawbridge, for example, uses a silhouette of the bridge itself.",
     "These enlarged images use the real sprite renderer and imported masks. Red pixels show what the wall covers remove."},
};
constexpr Page enemy[] = {
    {"A route is a graph, not a rail",
     "The Track data supplies waypoints, each with up to four neighbor links. Patrol follows the first link. Hunt evaluates the current waypoint's neighbors and chooses the one closest to an eligible player. It is a local choice, not a search of the whole course.\n\nAt close range the steelie can leave the graph and steer straight at the player. The route diagram reads the imported links; the highlighted node is its current waypoint.",
     "The black marble uses the same rolling and collision rules as the player. A waypoint is a steering target, not a constraint on its position."},
    {"The decision behind a ram",
     "A target must be racing in the steelie's zone, in a steerable state, and not falling. A grounded steelie can chase when the distance estimate is below 40 world units and its chase allowance permits it. Otherwise it may hunt along the graph or patrol.\n\nDistance is approximated by the longer axis plus three eighths of the shorter. The detection boundary is roughly octagonal, not circular. Leaving chase sends the steelie back toward its nearest waypoint; a collision can knock it before it recovers.",
     "In two-player races, when both players qualify, the one further down the course is selected. The nearest player is not always the target."},
    {"Why baiting can work",
     "Chase steering aims toward the player's current position. That decision does not first find a safe route across the floor. The marble also carries momentum: changing its target cannot instantly reverse its motion.\n\nA collision exchanges horizontal velocities and adds a separating push. Falling too far or landing hard can remove a steelie and award a bonus. These rules make a lure or a knock off the course possible, but a nearby wall or a change of mode can save it.",
     "Try Chase, steer away, and watch the target, velocity and mode. The laboratory is a constructed scenario using the real rules, not proof that every ledge is a successful trap."},
};
constexpr Page silly_pages[] = {
    {"Uphill is the new downhill",
     "Silly Race runs from the bottom of the course toward the top. Its most mischievous change is in the slope force: the ordinary downhill response changes sign, so the terrain pulls a grounded marble uphill.\n\nThis does not reverse all physics. Airborne gravity still pulls down; momentum, walls and falling still matter. The comparison beside this text runs the ordinary and Silly slope-force branches on the same imported slope, starting with zero horizontal velocity.",
     "The joke is a targeted rule change. The height field stays meaningful; the response to its slope is reversed."},
    {"Now you are the hunter",
     "Nine critters wander inside a pen. When a racing marble enters their zone at the pen's floor height, they flee. Their three kinds use different movement patterns; the pen's wall bitmap and other critters limit where they can move.\n\nCatch one and the course adds three seconds to the clock and 500 points. The diagram reads the actual 16 by 16 pen bitmap and plots the current critter positions. In the lab, Catch sample places the marble on one critter so a single Step exposes the reward.",
     "This reverses the usual relationship with enemies: approaching these creatures is a way to earn time."},
    {"Surprises with their own rules",
     "A special funnel lifts the marble upward and releases it at one of two exits. Its walls still constrain motion, but its vertical push is a course-specific behavior.\n\nLater, birds drop into five columns and walk in alternating short, slow runs and longer, faster runs. A wall can squash a bird; contact with the player's marble can trigger an eating animation and a respawn. These are dedicated creature rules using imported animation lists, not ordinary black marbles wearing different artwork.",
     "The course combines native special-case mechanics with data for terrain, pictures and animation. It does not replace the underlying simulation."},
};
void paragraph(Image& im,const std::string& body,int x,int y,int w,int size) {
    std::size_t begin=0;
    while(begin<body.size()) {
        const auto end=body.find("\n\n",begin);
        const auto text=body.substr(begin,end==std::string::npos?end:end-begin);
        y+=s::wrap(im,text,x,y,w,16,size,s::ink)*(size+5)+18;
        if(end==std::string::npos)break;
        begin=end+2;
    }
}
}

MechanicsChapter::MechanicsChapter(const std::filesystem::path& p,int topic)
    :files_(p),assets_(data::load_assets(p)),enemies_(topic==1),silly_(topic==2) {reset(topic==1?0:2);}

void MechanicsChapter::place(Marble& m,int x,int y) {
    m.x=x*65536;m.y=y*65536;m.vx=m.vy=m.vz=0;m.air=0;
    Probe probe;probe.locate(race_->terrain(),m.x,m.y,m.z);m.z=probe.ground_height();
    m.fall_start_z=m.z;m.mode=Marble::rolling;m.status=1;
    race_->project(m,true);race_->check_visible(m);
}
void MechanicsChapter::reset(int experiment) {
    experiment_=experiment;running_=false;steps_=0;credit_=0;last_=-1;
    race_=std::make_unique<Race>(files_,silly_?data::Course::silly:enemies_?data::Course::beginner:data::Course::practice);
    race_->cheats.stop_timer=true; // A visible lab convenience, not an original rule.
    if(silly_ && experiment==1) {
        race_->spawn_critters();
        const auto& c=race_->critters[0];
        place(race_->marbles[0],c.x>>16,c.y>>16);
        race_->marbles[0].feature=1;
        const int sy=std::int16_t(race_->marbles[0].screen[0]);
        const int desired=std::clamp(int(race_->scroll_)+140-sy,0,int(race_->scroll_limit_));
        race_->view_+=desired-race_->scroll_;race_->scroll_=std::int16_t(desired);
        race_->regions_.view_scroll=std::int16_t(race_->view_-race_->first_view_);race_->regions_.view=race_->view_;
        race_->project(race_->marbles[0],true);race_->check_visible(race_->marbles[0]);
        for(auto& critter:race_->critters)if(critter.status)race_->critter_place(critter);
    } else if(enemies_) {
        const auto& t=files_.track(race_->course());
        const auto table=t.u32(files_.track_header(race_->course())+0x18);
        const auto entry=t.u32(table);
        const int band=t.s8(entry);
        // Invoke the actual spawn rule using the first imported spawn band.
        for(auto& e:race_->steelies)e.body.status=0;
        race_->spawn_steelies(band-1,band);
        auto& e=race_->steelies[0].body.status?race_->steelies[0]:race_->steelies[1];
        auto& p=race_->marbles[0];
        const int ex=e.body.x>>16,ey=e.body.y>>16;
        // Pick a genuine nearby ground point, not invented floor. Prefer
        // the same height so the close-range case is easy to inspect.
        int bx=ex+12,by=ey;int best=1000000;
        const int distance=experiment==1?56:16;
        for(int dx=-64;dx<=64;dx+=4)for(int dy=-64;dy<=64;dy+=4) {
            const int d=std::max(std::abs(dx),std::abs(dy));
            if(d<12)continue;
            Probe q;q.locate(race_->terrain(),(ex+dx)*65536,(ey+dy)*65536,e.body.z);
            const int h=q.ground_height()>>16;if(!h)continue;
            const int cost=std::abs(d-distance)*20+std::abs(h-(e.body.z>>16))*5+std::abs(dy);
            if(cost<best){best=cost;bx=ex+dx;by=ey+dy;}
        }
        place(p,bx,by);p.feature=experiment==0?std::int8_t(e.body.feature+1):e.body.feature;
        // Zone controls intentionally override only the experiment's eligibility;
        // the live run subsequently derives the player's zone from the course.
        e.chase_time=-1;
        race_->steer(e);race_->project(e.body,true);race_->check_visible(e.body);
        const int sy=std::int16_t(e.body.screen[0]);
        const int shift=std::clamp(140-sy,0,int(race_->scroll_limit_));
        race_->scroll_=std::int16_t(shift);race_->view_+=shift;
        race_->regions_.view_scroll=std::int16_t(race_->view_-race_->first_view_);
        race_->regions_.view=race_->view_;
        race_->project(p,true);race_->check_visible(p);
        race_->project(e.body,true);race_->check_visible(e.body);
    } else if(experiment==1 || experiment==2) {
        view_=std::make_unique<View>(files_,assets_,*race_);
        // Find an actual partially covered marble position in the first view.
        const auto initial=race_->marbles[0];bool found=false;
        const int radius=silly_?256:56;
        for(int dx=-radius;dx<=radius&&!found;dx+=2)for(int dy=-radius;dy<=radius&&!found;dy+=2) {
            auto& m=race_->marbles[0];m=initial;place(m,(initial.x>>16)+dx,(initial.y>>16)+dy);
            const int sx=std::int16_t(std::uint32_t(m.screen[0])>>16),sy=std::int16_t(m.screen[0]);
            if(m.z<=0 || !m.sprite_flag || sx<=20 || sx>=268 || (!silly_ && (sy<=70 || sy>=230)))continue;
            if(experiment==2) {
                Probe q;q.locate(race_->terrain(),m.x,m.y,m.z);q.ground_height();
                found=(q.slope_x!=0 || q.slope_y!=0) && std::abs(q.slope_x)<32 && std::abs(q.slope_y)<32;
            } else if(m.covers[0].cell!=-1) {
                Race raw=*race_;for(auto& c:raw.marbles[0].covers)c.cell=-1;
                Image before(288,209),after(288,209);ui::fill(before,{0,0,0,0});ui::fill(after,{0,0,0,0});
                view_->draw_marble(before,raw,raw.marbles[0]);view_->draw_marble(after,*race_,m);
                int cut=0,visible=0;for(std::size_t i=0;i<before.pixels.size();++i){cut+=before.pixels[i].a&&!after.pixels[i].a;visible+=after.pixels[i].a!=0;}
                found=cut>8 && visible>12;
            }
        }
        if(!found)race_->marbles[0]=initial;
        if(silly_ && found) {
            auto& m=race_->marbles[0];const int sy=std::int16_t(m.screen[0]);
            const int desired=std::clamp(int(race_->scroll_)+140-sy,0,int(race_->scroll_limit_));
            race_->view_+=desired-race_->scroll_;race_->scroll_=std::int16_t(desired);
            race_->regions_.view_scroll=std::int16_t(race_->view_-race_->first_view_);race_->regions_.view=race_->view_;
            race_->project(m,true);race_->check_visible(m);
        }
    }
    view_=std::make_unique<View>(files_,assets_,*race_);
}
void MechanicsChapter::step(const platform::Controls& c) {
    const int x=(int(c.left)-int(c.right))*32,y=(int(c.down)-int(c.up))*32;
    const int boost=c.fire?2:1;
    const Push push{std::int16_t((y+x)*boost),std::int16_t((x-y)*boost)};
    race_->update({push,{}});view_->update(*race_);race_->take_sounds();++steps_;
}
void MechanicsChapter::update(const platform::Controls& c,double now) {
    const int last_page=(enemies_||silly_)?3:4;
    int delta=int(c.page_down)-int(c.page_up);
    if(hit(c,810,22,54,34))--delta;
    if(hit(c,876,22,60,34))++delta;
    if(page_!=last_page)delta+=int(c.nav_right)-int(c.nav_left)+c.wheel;
    if(delta){page_=std::clamp(page_+delta,0,last_page);running_=false;credit_=0;
        if(!enemies_ && !silly_ && page_==3)reset(1);
        if(silly_ && page_==1)reset(1);
        if(silly_ && page_==0)reset(2);last_=now;return;}
    if(page_==last_page) {
        if(c.accept||hit(c,24,76,100,32)){running_=!running_;credit_=0;last_=now;}
        if(hit(c,136,76,90,32)){running_=false;step(c);}
        if(hit(c,238,76,90,32))reset(experiment_);
        if(enemies_)for(int i=0;i<3;++i)if(hit(c,350+i*105,76,96,32))reset(i);
        if(silly_)for(int i=0;i<3;++i)if(hit(c,350+i*125,76,116,32))reset(i);
        if(!enemies_ && !silly_ && hit(c,350,76,160,32))reset(experiment_==1?0:1);
        if(running_ && last_>=0){credit_+=std::clamp(now-last_,0.0,0.2);
            while(credit_>=.04){step(c);credit_-=.04;}}
    }
    last_=now;
}

void MechanicsChapter::terrain_figure(Image& im,int ox,int oy,bool triangle) const {
    const auto& m=race_->marbles[0];const int cx=m.x>>19,cy=m.y>>19;
    if(triangle) {
        std::array<int,64> h{};int lo=32767,hi=-32768;
        for(int y=0;y<8;++y)for(int x=0;x<8;++x){Probe q;q.locate(race_->terrain(),(cx*8+x)*65536,(cy*8+y)*65536,m.z);
            const int z=q.ground_height()>>16;h[y*8+x]=z;lo=std::min(lo,z);hi=std::max(hi,z);}
        for(int y=0;y<8;++y)for(int x=0;x<8;++x){const int a=(h[y*8+x]-lo)*210/std::max(1,hi-lo);
            ui::rect(im,ox+40+x*32,oy+30+y*32,31,31,ui::mix(s::pale,s::cobalt,a));}
        line(im,ox+40,oy+30,ox+296,oy+286,s::red);
        dot(im,ox+56+((m.x>>16)&7)*32,oy+46+((m.y>>16)&7)*32,s::red,6);
        s::text(im,"Actual cell: "+std::to_string(cx)+", "+std::to_string(cy),ox+20,oy+308,16,s::ink,true);
        s::text(im,"Height range "+std::to_string(lo)+" to "+std::to_string(hi),ox+20,oy+336,14,s::muted);
        return;
    }
    const int base=m.z>>16;
    auto point=[&](int x,int y,int z){return std::pair{ox+205+(y-x)*18,oy+120+(x+y)*10-(z-base)};};
    for(int y=-3;y<4;++y)for(int x=-3;x<4;++x){
        const int vx=cx+x,vy=cy+y;
        const int a=race_->terrain().vertex(vx,vy).height[1];
        const int b=race_->terrain().vertex(vx+1,vy).height[0];
        const int c=race_->terrain().vertex(vx+1,vy+1).height[3];
        const int d=race_->terrain().vertex(vx,vy+1).height[2];
        if(!a||!b||!c||!d)continue;
        auto p=point(x,y,a),q=point(x+1,y,b),r=point(x+1,y+1,c),t=point(x,y+1,d);
        if(std::min({p.second,q.second,r.second,t.second})<oy || std::max({p.second,q.second,r.second,t.second})>oy+280)continue;
        const auto color=x==0&&y==0?s::red:s::cobalt;
        line(im,p.first,p.second,q.first,q.second,color);line(im,q.first,q.second,r.first,r.second,color);
        line(im,r.first,r.second,t.first,t.second,color);line(im,t.first,t.second,p.first,p.second,color);
        line(im,p.first,p.second,r.first,r.second,s::line);
    }
    s::text(im,"Imported ground, enlarged",ox+20,oy+304,17,s::ink,true);
    s::wrap(im,"Red: the marble's cell. Height changes bend the surface; missing ground leaves gaps.",ox+20,oy+334,350,3,15);
}

void MechanicsChapter::route_figure(Image& im,int ox,int oy) const {
    const auto& e=race_->steelies[0].body.status?race_->steelies[0]:race_->steelies[1];
    const auto& t=files_.track(race_->course());
    if(!e.path){s::text(im,"No active route",ox,oy);return;}
    struct Node {int x,y;std::array<int,4> next;};std::vector<Node> nodes;
    int minx=127,miny=127,maxx=-128,maxy=-128;
    for(int n=0;n<128;++n){const auto at=e.path+std::uint32_t(n*6);
        if(!t.contains(at,6)||t.s8(at)==-1||t.s8(at+1)==-1)break;
        Node v{t.s8(at),t.s8(at+1),{}};for(int k=0;k<4;++k)v.next[k]=t.s8(at+2+k);
        minx=std::min(minx,v.x);maxx=std::max(maxx,v.x);miny=std::min(miny,v.y);maxy=std::max(maxy,v.y);nodes.push_back(v);}
    auto pos=[&](double x,double y){return std::pair{ox+30+int((x-minx)*320/std::max(1,maxx-minx)),oy+20+int((y-miny)*240/std::max(1,maxy-miny))};};
    for(std::size_t i=0;i<nodes.size();++i){auto a=pos(nodes[i].x,nodes[i].y);
        for(int k=3;k>=0;--k){int n=nodes[i].next[k];if(n<0||n>=int(nodes.size()))continue;auto b=pos(nodes[n].x,nodes[n].y);
            line(im,a.first,a.second,b.first,b.second,k==0?s::cobalt:s::line);}
    }
    for(std::size_t i=0;i<nodes.size();++i){auto a=pos(nodes[i].x,nodes[i].y);dot(im,a.first,a.second,e.waypoint==e.path+i*6?s::red:s::ink);
        if(e.waypoint==e.path+i*6)s::text(im,"Target "+std::to_string(i+1),a.first-22,a.second+10,12,s::red,true);}
    s::text(im,"Imported waypoint graph",ox+20,oy+295,18,s::ink,true);
    s::wrap(im,"Blue: first neighbor (patrol). Gray: other choices. Red: current target waypoint. Links may overlap.",ox+20,oy+327,360,3,15);
}

void MechanicsChapter::covers_figure(Image& im,int ox,int oy) const {
    Race raw=*race_;auto& m=raw.marbles[0];m.sprite_flag=1;for(auto& c:m.covers)c.cell=-1;
    Image before(View::width,View::picture_height),after(View::width,View::picture_height);
    ui::fill(before,{0,0,0,0});ui::fill(after,{0,0,0,0});
    view_->draw_marble(before,raw,m);view_->draw_marble(after,*race_,race_->marbles[0]);
    int minx=288,miny=209,maxx=0,maxy=0;
    for(int y=0;y<209;++y)for(int x=0;x<288;++x)if(before.pixels[y*288+x].a){minx=std::min(minx,x);maxx=std::max(maxx,x);miny=std::min(miny,y);maxy=std::max(maxy,y);}
    if(minx>maxx){s::wrap(im,"The marble is outside the view. Reset the lab to inspect a cover sample.",ox+20,oy+40,340,4,17);return;}
    const int scale=6;
    for(int k=0;k<3;++k){const int x0=ox+20+(k%2)*192,y0=oy+25+(k/2)*145;
        ui::checker(im,x0,y0,160,108,12,s::paper,s::line);
        for(int y=miny;y<=maxy;++y)for(int x=minx;x<=maxx;++x){const auto a=before.pixels[y*288+x],b=after.pixels[y*288+x];
            Color c=k==0?a:k==1?b:Color{0,0,0,0};if(k==2&&a.a&&!b.a)c=s::red;
            if(c.a)ui::rect(im,x0+(x-minx)*scale,y0+(y-miny)*scale,scale,scale,c);}
        s::text(im,k==0?"Without wall covers":k==1?"With wall covers":"Removed pixels",x0,y0+113,13,s::ink,true);
    }
    int strip=0;
    for(const auto& cover:race_->marbles[0].covers) {
        if(cover.cell==-1 || strip==2)break;
        const auto& bank=assets_.wall_covers[std::size_t(race_->course())];
        const auto index=std::size_t(cover.cell&0xFFFF);if(index>=bank.cells.size())continue;
        const auto& cell=bank.cells[index];Image mask(cell.width,cell.height);
        for(std::size_t i=0;i<mask.pixels.size();++i)mask.pixels[i]=cell.pixels[i]?s::ink:s::paper;
        ui::picture(im,mask,ox+212+strip*78,oy+170,72,108);++strip;
    }
    s::text(im,"Imported mask patterns",ox+212,oy+283,13,s::ink,true);
    int count=0;for(const auto& c:race_->marbles[0].covers){if(c.cell==-1)break;++count;}
    s::wrap(im,std::to_string(count)+" selected cover strips. Other obstacle masks remain active in both images.",ox+20,oy+328,360,3,15);
}
void MechanicsChapter::decision_figure(Image& im,int ox,int oy) const {
    const int width=ox>600?298:370;
    const auto& e=race_->steelies[0].body.status?race_->steelies[0]:race_->steelies[1];
    const char* names[]={"Patrol","Hunt","Chase","Return","Knocked"};
    const char* hints[]={"No eligible target: follow first link","Eligible target: choose a neighbor","Close + grounded + chase allowed","Recover the nearest waypoint","Collision: recover from the impact"};
    for(int i=0;i<5;++i){const int y=oy+i*62;const bool active=std::string(mode(e.body))==names[i];
        ui::rounded(im,ox+12,y,width,52,5,active?s::pale:s::paper);
        s::text(im,names[i],ox+24,y+5,18,active?s::cobalt:s::ink,true);
        s::clipped(im,hints[i],ox+24,y+31,width-24,12,s::muted);
        if(i<4)line(im,ox+8,y+26,ox+8,y+88,s::line);
    }
    s::wrap(im,ox>600?"Presets set target eligibility. Running resumes normal course zone rules.":"These are alternative states, not a fixed sequence. Inspect them live on the final page.",ox+12,oy+320,width,3,14);
}

void MechanicsChapter::silly_figure(Image& im,int ox,int oy) const {
    if(page_==0) {
        Race ordinary=*race_,silly=*race_;
        auto force=[](Race& r,bool reverse){auto& m=r.marbles[0];m.vx=m.vy=0;
            r.probe_.invalidate();r.locate(m);r.probe_.ground_height();
            r.course_=reverse?data::Course::silly:data::Course::practice;r.slope_force(m);
            return std::pair{m.vx/65536.,m.vy/65536.};};
        const auto a=force(ordinary,false),b=force(silly,true);
        auto panel=[&](int y,const char* title,std::pair<double,double> v,Color color){
            s::text(im,title,ox+20,y,21,color,true);
            s::text(im,"Velocity change on this slope",ox+20,y+35,14);
            s::text(im,"X  "+number(v.first)+"    Y  "+number(v.second),ox+20,y+63,19,color,true);
            const double length=std::max(.01,std::max(std::abs(v.first),std::abs(v.second)));
            const int x0=ox+290,y0=y+65,x1=x0+int((v.second-v.first)/length*28),y1=y0+int((v.first+v.second)/length*14);
            line(im,x0,y0,x1,y1,color);dot(im,x1,y1,color,5);dot(im,x0,y0,s::ink,3);
        };
        panel(oy+25,"Ordinary slope response",a,s::muted);panel(oy+175,"Silly slope response",b,s::cobalt);
        s::wrap(im,"Same imported cell, same starting velocity. Only the slope-force rule changes.",ox+20,oy+325,360,3,15);
    } else if(page_==1) {
        // The same authored pen bitmap used by critter_blocked; read as data.
        for(int y=0;y<16;++y){const auto bits=files_.program().u16(0x11D478+std::uint32_t(y*2));
            for(int x=0;x<16;++x)ui::rect(im,ox+64+x*16,oy+15+y*16,15,15,(bits&(1<<x))?s::ink:s::pale);}
        for(const auto& c:race_->critters)if(c.status){const int x=int(c.x/65536.)-89*8,y=int(c.y/65536.)-90*8;
            dot(im,ox+64+x*2,oy+15+y*2,c.state==Critter::caught?s::red:s::cobalt,4);}
        s::text(im,"The imported pen map",ox+20,oy+295,20,s::ink,true);
        s::wrap(im,"Dark: blocked cells. Blue dots: current critters. A catch adds 3 clock seconds and 500 points.",ox+20,oy+330,360,3,15);
    } else {
        const char* labels[]={"Slopes","Pen critters","Funnel","Birds","Still shared"};
        const char* values[]={"Pull uphill instead of downhill","Flee; a catch earns time + points","Lift upward, then choose an exit","Walk, meet walls, or eat a marble","Momentum, terrain and collision"};
        for(int i=0;i<5;++i){int y=oy+15+i*67;ui::rect(im,ox+12,y+57,375,1,s::line);
            s::text(im,labels[i],ox+20,y,19,s::cobalt,true);s::text(im,values[i],ox+20,y+30,15,s::ink);}
    }
}

void MechanicsChapter::lab(Image& im) const {
    s::button(im,running_?"Pause":"Run",24,76,100,32,running_);s::button(im,"Step",136,76,90,32);s::button(im,"Reset",238,76,90,32);
    if(enemies_)for(int i=0;i<3;++i)s::button(im,i==0?"Patrol":i==1?"Hunt":"Chase",350+i*105,76,96,32,experiment_==i);
    else if(silly_)for(int i=0;i<3;++i)s::button(im,i==0?"Start":i==1?"Catch sample":"Slope",350+i*125,76,116,32,experiment_==i);
    else s::button(im,experiment_==1?"Start position":"Cover sample",350,76,160,32);
    auto picture=view_->draw(*race_);ui::picture(im,picture,24,122,576,418);
    const auto& m=enemies_?(race_->steelies[0].body.status?race_->steelies[0].body:race_->steelies[1].body):race_->marbles[0];
    // Use the same projection as the game; the overlay represents a point,
    // rather than guessing the sprite's changing animation bounding box.
    auto project=[&](double x,double y,double z){return std::pair{24+int((y-x+136)*2),122+int((264-(race_->view()+z+108-(x+y)/2))*2)};};
    const double x=m.x/65536.,y=m.y/65536.,z=m.z/65536.;const auto p=project(x,y,z);
    if(p.first>30&&p.first<590&&p.second>145&&p.second<515){
        dot(im,p.first,p.second,s::red,4);
        const auto v=project(x+m.vx/65536.*8,y+m.vy/65536.*8,z);
        line(im,p.first,p.second,v.first,v.second,s::red);
        s::text(im,mode(m),p.first-12,p.second-28,14,s::white,true);
    }
    if(enemies_){
        const auto& target=race_->marbles[0];
        const auto& e=race_->steelies[0].body.status?race_->steelies[0]:race_->steelies[1];
        const auto& track=files_.track(race_->course());
        const bool direct=m.mode==Marble::chasing && target.status==1 && target.feature==m.feature &&
            (target.mode==Marble::rolling || target.mode==Marble::landing || target.mode==Marble::bumped);
        const auto q=direct?project(target.x/65536.,target.y/65536.,z):
            project(track.s8(e.waypoint)*8+4,track.s8(e.waypoint+1)*8+4,z);
        // Clip the diagnostic line to the game panel, never over the prose.
        Image overlay(576,418);ui::fill(overlay,{0,0,0,0});
        line(overlay,p.first-24,p.second-122,q.first-24,q.second-122,s::green);
        const double diagonal=40./1.375;
        const std::array<std::pair<double,double>,8> edge{{{40,0},{diagonal,diagonal},{0,40},{-diagonal,diagonal},{-40,0},{-diagonal,-diagonal},{0,-40},{diagonal,-diagonal}}};
        for(int i=0;i<8;++i){auto a=project(x+edge[i].first,y+edge[i].second,z);auto b=project(x+edge[(i+1)%8].first,y+edge[(i+1)%8].second,z);
            line(overlay,a.first-24,a.second-122,b.first-24,b.second-122,m.mode==Marble::chasing?s::red:s::cobalt);}

        for(int yy=0;yy<418;++yy)for(int xx=0;xx<576;++xx)if(overlay.pixels[yy*576+xx].a)im.put(xx+24,yy+122,overlay.pixels[yy*576+xx]);
        decision_figure(im,612,125);
        const auto ax=std::abs(target.x-m.x)>>12,ay=std::abs(target.y-m.y)>>12;
        const double distance=(std::max(ax,ay)+3*(std::min(ax,ay)>>3))/16.;
        s::text(im,"Distance "+number(distance)+" / 40",624,520,16,s::ink,true);
        
    } else if(silly_) {
        s::text(im,"Silly Race laboratory",624,137,21,s::ink,true);
        s::text(im,"Score  "+std::to_string(m.score),624,185,20,s::cobalt,true);
        s::text(im,"Clock  "+std::to_string(m.time),624,220,20,s::cobalt,true);
        int caught=0,fleeing=0;for(const auto& c:race_->critters){caught+=c.status && c.state==Critter::caught;fleeing+=c.status && c.state==Critter::fleeing;}
        s::text(im,"Caught  "+std::to_string(caught),624,265,17);
        s::text(im,"Fleeing  "+std::to_string(fleeing),624,295,17);
        s::text(im,std::string("Marble: ")+mode(m),624,330,17);
        s::wrap(im,"Catch sample is deliberately placed on a critter. Step once: the real collision rule awards time and points.",624,375,290,5,16);
        s::wrap(im,"Slope: release the controls and watch the reversed ground force. Airborne gravity remains normal.",624,465,290,3,14);
    } else {
        Probe q;q.locate(race_->terrain(),m.x,m.y,m.z);const double ground=q.ground_height()/65536.;q.analyse_sides();
        s::text(im,"Ground contact",624,137,22,s::ink,true);
        s::text(im,std::string("State: ")+mode(m),624,181,17,s::cobalt,true);
        s::text(im,"X / Y  "+number(x)+" / "+number(y),624,216,16);
        s::text(im,"Height  "+number(z),624,246,16);
        s::text(im,"Ground  "+number(ground),624,276,16);
        s::text(im,"Gap  "+number(z-ground),624,306,16);
        s::text(im,"Velocity X / Y",624,350,17,s::ink,true);
        s::text(im,number(m.vx/65536.)+" / "+number(m.vy/65536.),624,380,17);
        s::text(im,"Vertical  "+number(m.vz/65536.),624,410,17);
        s::wrap(im,"Red line: eight updates of current horizontal velocity, not a predicted path.",624,455,290,3,15);
    }
    s::text(im,"Arrows / WASD: steer   Space: stronger push   Enter: run/pause",24,554,15,s::ink);
    s::text(im,"Step "+std::to_string(steps_)+"   /   Timer frozen   /   "+(enemies_?"Green: steering target. Outline: chase range.":"Disposable experiment"),24,581,13,s::muted);
}

Image MechanicsChapter::draw() const {
    Image im(960,640);ui::fill(im,s::paper);
    s::button(im,"<  Lessons",24,20,104,34);
    const int count=(enemies_||silly_)?4:5;
    s::text(im,silly_?"How Silly Race bends the rules":enemies_?"Inside the black marble":"A marble in a painted world",148,24,23,s::ink,true);
    s::right(im,std::to_string(page_+1)+" / "+std::to_string(count),790,30,14,s::muted);
    s::button(im,"<",810,22,54,34);s::button(im,">",876,22,60,34);
    if(page_==count-1)lab(im);
    else {
        const auto& p=silly_?silly_pages[page_]:enemies_?enemy[page_]:physics[page_];
        s::text(im,p.title,24,86,28,s::ink,true);
        paragraph(im,p.body,24,145,455,18);
        ui::rounded(im,512,137,424,405,8,s::card);
        if(silly_)silly_figure(im,520,145);
        else if(enemies_){if(page_==0)route_figure(im,520,145);else decision_figure(im,520,155);}
        else if(page_==3)covers_figure(im,520,145);
        else if(page_==2) {
            const auto& m=race_->marbles[0];Probe q;q.locate(race_->terrain(),m.x,m.y,m.z);
            q.ground_height();q.analyse_sides();
            const char* stages[]={"Push + momentum","Advance X, Y and height","Walls, objects, other marbles","Ground contact / falling"};
            for(int i=0;i<4;++i){const int y=155+i*55;
                ui::rounded(im,540,y,370,38,5,i==2?s::pale:s::paper);
                s::text(im,stages[i],552,y+8,17,s::ink,true);
                if(i<3){line(im,724,y+38,724,y+53,s::cobalt);line(im,720,y+49,724,y+53,s::cobalt);line(im,728,y+49,724,y+53,s::cobalt);}
            }
            s::text(im,"Current cell's boundary tests",540,390,17,s::ink,true);
            for(int i=0;i<4;++i){const int kind=q.side_kind[i];
                s::text(im,std::string(i==0?"-X":i==1?"+Y":i==2?"+X":"-Y")+": "+(kind==0?"continuous":kind<=2?"wall above":"drop below"),540+(i%2)*180,425+(i/2)*30,15);}
            s::text(im,"Measured from the imported terrain",540,500,13,s::muted);
        } else terrain_figure(im,520,145,page_==1);
        s::wrap(im,p.takeaway,24,554,910,2,16,s::ink);
    }
    s::text(im,"Amiga reconstruction · imported course data · Page Up/Down: pages · Esc: lessons",24,614,12,s::muted);
    return im;
}
}
