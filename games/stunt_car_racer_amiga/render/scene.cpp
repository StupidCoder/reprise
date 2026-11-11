#include "stunt_car_racer_amiga/render/scene.hpp"
#include <cmath>
#include <algorithm>
#include <numbers>
#include <stdexcept>

namespace stuntcar::render {
namespace s=platform::scene3d;
s::Vec3 scene_point(double x,double height,double z) { return {x/256.,height/1024.,z/256.}; }
s::Mesh track_scene(const data::Circuits& circuits,const data::Track& track,const Palette& palette,bool textured) {
    s::Mesh mesh;
    for(const auto& c:palette) mesh.materials.push_back({{c.r,c.g,c.b,255}});
    for(const auto& c:palette) mesh.materials.push_back({{c.r,c.g,c.b,255}}); // untextured dynamic palette
    if(textured) {
        for(unsigned i:{1u,2u}) mesh.materials[i]={{235,235,235,255},"stunt_car_racer_amiga/materials/asphalt.png"};
        mesh.materials[2].colour={245,245,245,255};
        mesh.materials[13]={{210,220,195,255},"stunt_car_racer_amiga/materials/grass.png"};
    }
    const auto road=data::road(circuits,track);
    auto quad=[&](s::Vec3 a,s::Vec3 b,s::Vec3 c,s::Vec3 d,unsigned material,double from,double to) {
        const unsigned first=unsigned(mesh.vertices.size());
        const double width=std::sqrt(s::dot(b-a,b-a));
        // One tile per half scene unit. UVs measure arc length rather than
        // segment number, so banked slopes do not change texture density.
        const double across=textured ? width*2 : 1;
        const double scale=textured ? 2 : 1;
        mesh.vertices.insert(mesh.vertices.end(),{{a,0,from*scale},{b,across,from*scale},{c,across,to*scale},{d,0,to*scale}});
        if(textured && material==13) for(unsigned i=first;i<first+4;++i) {
            auto& v=mesh.vertices[i];v.u=v.position.x;v.v=v.position.z;
        }
        mesh.triangles.push_back({{first,first+1,first+2},material});
        mesh.triangles.push_back({{first,first+2,first+3},material});
    };
    struct Mark { s::Vec3 a,b,c,d; unsigned material; };
    std::vector<Mark> marks;
    std::vector<std::vector<data::CrossSection>> sections(track.segments.size());
    for(const auto& row:road) sections.at(row.segment).push_back(row);
    // Same profile-boundary colour decision as race_view.cpp Road::road_colour.
    const auto surface_colour=[&](int segment,int step) {
        for(std::size_t n=0;n<sections.size();++n,segment=(segment+1)%int(sections.size()),step=1) {
            const auto& rows=sections[segment];
            for(;step<int(rows.size());++step) if(rows[step].marked!=(step==int(rows.size())-1))
                return unsigned(std::abs(rows[step].left_height-rows[step-1].left_height)>=0x280 ? 0 : 1+(segment&1));
        }
        return 1u;
    };
    double distance=0;
    int step=0;
    for(std::size_t i=1;i<road.size();++i) {
        const auto& a=road[i-1]; const auto& b=road[i];
        if(a.segment!=b.segment) { step=0; continue; } // duplicated join, never bridge a discontinuity
        const auto al=scene_point(a.left.x,a.left_height,a.left.z), ar=scene_point(a.right.x,a.right_height,a.right.z);
        const auto bl=scene_point(b.left.x,b.left_height,b.left.z), br=scene_point(b.right.x,b.right_height,b.right.z);
        const auto delta=(bl+br-al-ar)*.5;
        const double next=distance+std::sqrt(s::dot(delta,delta));
        // Profile-boundary shading follows the original renderer.
        ++step;
        quad(al,ar,br,bl,surface_colour(a.segment,step),distance,next);
        // The original uses screen-space one-pixel edge lines; VR ribbons
        // have finite world width for stable stereo and antialiasing. Keep
        // the decoded per-step kerb phase, extended around the whole track.
        const unsigned stripe=((step ^ track.segments[a.segment].kerb_phase)&1) ? 3 : 9;
        const s::Vec3 lift{0,.0008,0};
        constexpr double fraction=.012;
        marks.push_back({al+lift,al+(ar-al)*fraction+lift,bl+(br-bl)*fraction+lift,bl+lift,stripe});
        marks.push_back({ar+(al-ar)*fraction+lift,ar+lift,br+lift,br+(bl-br)*fraction+lift,stripe});
        if(a.segment==track.start_line_segment && step==int(sections[a.segment].size())-1) {
            marks.push_back({bl+(al-bl)*.035+lift,br+(ar-br)*.035+lift,br+lift,bl+lift,15});
        }
        // Original side_base is height $200, not height zero (race_view.cpp).
        quad(al,bl,scene_point(b.left.x,512,b.left.z),scene_point(a.left.x,512,a.left.z),
             unsigned(a.segment&1 ? 10 : 15),distance,next);
        quad(ar,br,scene_point(b.right.x,512,b.right.z),scene_point(a.right.x,512,a.right.z),
             unsigned(a.segment&1 ? 10 : 15),distance,next);
        distance=next;
    }
    // Mirrored tiles repeat every two UV units; fit an integer number of
    // periods around the closed circuit to avoid a texture phase seam.
    if(textured && distance>0) for(auto& v:mesh.vertices) v.v*=std::max(1.,std::round(distance))/distance;
    // Extend beyond the view far plane: the old finite arena left a sky
    // strip between its edge and the distant horizon polygons.
    constexpr double extent=4096;
    quad({-extent,.5,-extent},{extent,.5,-extent},{extent,.5,extent},{-extent,.5,extent},13,0,extent);
    for(const auto& mark:marks) quad(mark.a,mark.b,mark.c,mark.d,mark.material,0,1);
    return mesh;
}
s::Mesh moving_scene(const data::Circuits& circuits,const data::Track& track,const data::Scenery& scenery,
                      const Camera& camera,const std::optional<Opponent>& opponent) {
    s::Mesh mesh;
    const auto quad=[&](s::Vec3 a,s::Vec3 b,s::Vec3 c,s::Vec3 d,unsigned material) {
        const unsigned first=unsigned(mesh.vertices.size());
        mesh.vertices.insert(mesh.vertices.end(),{{a},{b},{c},{d}});
        mesh.triangles.push_back({{first,first+1,first+2},material+16});
        mesh.triangles.push_back({{first,first+2,first+3},material+16});
    };
    // Original scenery is angular and never approaches the player. Place it
    // on a distant camera-centred dome, retaining decoded polygons/colours.
    // Ear clipping preserves concave outlines rather than assuming a fan.
    const auto eye=scene_point(camera.x,512,camera.z); // scenery bases meet ground, not eye height
    constexpr double radians=2*std::numbers::pi/65536.,radius=500;
    for(const auto& placed:scenery.placed) {
        const auto& shape=scenery.shapes.at(placed.shape);
        for(const auto& polygon:shape.polygons) {
            auto loop=polygon.corners;
            if(loop.size()>1 && loop.front()==loop.back()) loop.pop_back();
            if(loop.size()<3) continue;
            const auto cross=[&](int a,int b,int c) {
                const auto& p=shape.points.at(a);const auto& q=shape.points.at(b);const auto& r=shape.points.at(c);
                return double(q.x-p.x)*(r.y-p.y)-double(q.y-p.y)*(r.x-p.x);
            };
            double area=0;
            for(std::size_t i=0;i<loop.size();++i) {
                const auto a=shape.points.at(loop[i]),b=shape.points.at(loop[(i+1)%loop.size()]);
                area+=double(a.x)*b.y-double(b.x)*a.y;
            }
            const double sign=area<0 ? -1 : 1;
            const auto triangle=[&](int a,int b,int c) {
                const unsigned first=unsigned(mesh.vertices.size());
                for(int index:{a,b,c}) {
                    const auto point=shape.points.at(index);
                    const double bearing=(placed.bearing*256.+point.x*8.)*radians;
                    const double elevation=point.y*8.*radians;
                    mesh.vertices.push_back({eye+s::Vec3{std::sin(bearing)*radius,std::tan(elevation)*radius,std::cos(bearing)*radius}});
                }
                mesh.triangles.push_back({{first,first+1,first+2},unsigned(polygon.colour)+16});
            };
            while(loop.size()>3) {
                bool clipped=false;
                for(std::size_t i=0;i<loop.size();++i) {
                    const int a=loop[(i+loop.size()-1)%loop.size()],b=loop[i],c=loop[(i+1)%loop.size()];
                    if(sign*cross(a,b,c)<1e-8) continue;
                    bool inside=false;
                    for(int p:loop) if(p!=a && p!=b && p!=c && sign*cross(a,b,p)>1e-8 &&
                        sign*cross(b,c,p)>1e-8 && sign*cross(c,a,p)>1e-8) {inside=true;break;}
                    if(inside) continue;
                    triangle(a,b,c);loop.erase(loop.begin()+i);clipped=true;break;
                }
                if(!clipped) {
                    // Remove a redundant straight-line vertex; malformed
                    // polygons fail explicitly rather than filling across them.
                    for(std::size_t i=0;i<loop.size();++i) if(std::abs(cross(loop[(i+loop.size()-1)%loop.size()],loop[i],loop[(i+1)%loop.size()]))<1e-8) {
                        loop.erase(loop.begin()+i);clipped=true;break;
                    }
                    if(!clipped) throw std::runtime_error("Non-simple scenery polygon");
                }
            }
            if(loop.size()==3) triangle(loop[0],loop[1],loop[2]);
        }
    }
    if(!opponent) return mesh;
    std::vector<std::vector<data::CrossSection>> sections(track.segments.size());
    for(const auto& row:data::road(circuits,track)) sections.at(row.segment).push_back(row);
    // Sampling crosses segment joins, so the front axle does not disappear
    // when the rear axle is near a boundary (the old view could cull it).
    const auto point=[&](double step,double across,double lift) {
        int segment=opponent->segment;
        while(step<0) {segment=(segment+int(sections.size())-1)%int(sections.size());step+=sections[segment].size()-1;}
        while(step>=sections[segment].size()-1) {step-=sections[segment].size()-1;segment=(segment+1)%int(sections.size());}
        const auto& a=sections[segment][std::size_t(step)],b=sections[segment][std::size_t(step)+1];
        const double f=step-std::floor(step),t=across/256.;
        const auto al=scene_point(a.left.x,a.left_height,a.left.z),ar=scene_point(a.right.x,a.right_height,a.right.z);
        const auto bl=scene_point(b.left.x,b.left_height,b.left.z),br=scene_point(b.right.x,b.right_height,b.right.z);
        return (al+(ar-al)*t)*(1-f)+(bl+(br-bl)*t)*f+s::Vec3{0,lift/1024.,0};
    };
    // OpponentDriver::suspension adds $50 for the original screen-space
    // artwork. Remove only that drawing offset. $28/$7C are spring preload:
    // at rest compression = (road-body+preload)-preload = road-body,
    // so equilibrium is body == road, including on curves.
    constexpr double wheel_reference=0x50;
    std::array<s::Vec3,4> corners;
    for(int axle=0;axle<2;++axle) for(int side=0;side<2;++side)
        corners[axle*2+side]=point(opponent->step+axle*.5,opponent->across+(side ? 27 : -27),opponent->lift[axle*2+side]-wheel_reference);
    const auto unit=[](s::Vec3 v) { const double length=std::sqrt(s::dot(v,v));return length>1e-8 ? v*(1/length) : s::Vec3{0,0,1}; };
    const auto cross=[](s::Vec3 a,s::Vec3 b){return s::Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    const auto right=unit(corners[1]-corners[0]);
    const auto forward=unit(corners[2]+corners[3]-corners[0]-corners[1]);
    auto up=unit(cross(forward,right));if(up.y<0) up=up*-1;
    const double width=std::sqrt(s::dot(corners[1]-corners[0],corners[1]-corners[0]));
    // New 3D interpretation: low wedge body and four chunky wheels. Original
    // axle spacing, lateral extent, lifts and palette are retained. Vertical
    // body/wheel proportions are presentation choices, not recovered physics.
    std::array<s::Vec3,4> base,top;
    for(int i=0;i<4;++i) {
        const auto middle=(corners[(i/2)*2]+corners[(i/2)*2+1])*.5;
        const auto lateral=corners[i]-middle;
        // race_view.cpp: body base +/-U/2, rear roof +/-3U/8,
        // front roof +/-U/2; U is half the full axle span.
        base[i]=middle+lateral*.5+up*(width*.125);
        top[i]=middle+lateral*(i<2 ? .375 : .5)+up*(width*(i<2 ? .5 : .25));
    }
    quad(base[0],base[1],top[1],top[0],10);quad(base[2],base[3],top[3],top[2],12);
    quad(base[0],base[2],top[2],top[0],12);quad(base[1],base[3],top[3],top[1],12);
    quad(top[0],top[1],top[3],top[2],15);quad(base[0],base[1],base[3],base[2],9);
    for(int i=0;i<4;++i) {
        const auto middle=(corners[(i/2)*2]+corners[(i/2)*2+1])*.5;
        const auto center=middle+(corners[i]-middle)*.75+up*(width*.125);
        const auto r=right*(width*.125),u=up*(width*.125),f=forward*(width*.125);
        quad(center-r-u-f,center+r-u-f,center+r+u-f,center-r+u-f,0);
        quad(center-r-u+f,center+r-u+f,center+r+u+f,center-r+u+f,0);
        quad(center-r-u-f,center-r-u+f,center-r+u+f,center-r+u-f,0);
        quad(center+r-u-f,center+r-u+f,center+r+u+f,center+r+u-f,0);
        quad(center-r+u-f,center+r+u-f,center+r+u+f,center-r+u+f,0);
        quad(center-r-u-f,center+r-u-f,center+r-u+f,center-r-u+f,0);
    }
    return mesh;
}
s::View perspective_camera(const Camera& camera) {
    const auto radians=[](int angle) {return angle*(2*std::numbers::pi/65536.);};
    const double yaw=radians(camera.yaw), pitch=radians(camera.pitch), roll=radians(camera.roll);
    s::View view;
    view.eye=scene_point(camera.x,camera.eye,camera.z);
    view.forward={std::sin(yaw)*std::cos(pitch),-std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    const s::Vec3 right{std::cos(yaw),0,-std::sin(yaw)};
    const s::Vec3 up{std::sin(yaw)*std::sin(pitch),std::cos(pitch),std::cos(yaw)*std::sin(pitch)};
    // race_view.cpp turns screen-down coordinates: positive roll raises
    // points on the right. Our world-up basis must use the opposite sign.
    view.right=right*std::cos(roll)-up*std::sin(roll);
    view.up=up*std::cos(roll)+right*std::sin(roll);
    view.left=-.41421356237; view.right_edge=-view.left;
    view.bottom=view.left*.5; view.top=-view.bottom;
    return view;
}
Opponent interpolate_opponent(const data::Track& track,const Opponent& previous,const Opponent& current,double fraction) {
    fraction=std::clamp(fraction,0.,1.);
    double length=0;for(const auto& segment:track.segments) length+=segment.steps;
    const auto along=[&](const Opponent& o) {
        double value=o.step;for(int i=0;i<o.segment;++i) value+=track.segments.at(i).steps;
        return value;
    };
    Opponent result=current;
    double position=along(previous)+std::remainder(along(current)-along(previous),length)*fraction;
    position=std::fmod(position,length);if(position<0) position+=length;
    result.segment=0;
    while(result.segment+1<int(track.segments.size()) && position>=track.segments[result.segment].steps)
        position-=track.segments[result.segment++].steps;
    result.step=position;
    result.across=previous.across+(current.across-previous.across)*fraction;
    for(int i=0;i<4;++i) result.lift[i]=previous.lift[i]+(current.lift[i]-previous.lift[i])*fraction;
    return result;
}
Camera interpolate_camera(const Camera& previous,const Camera& current,double fraction) {
    fraction=std::clamp(fraction,0.,1.);
    const auto lerp=[&](double a,double b) { return a+(b-a)*fraction; };
    const auto angle=[&](int a,int b) { return int(std::lround(a+std::remainder(double(b)-a,65536.)*fraction)); };
    return {lerp(previous.x,current.x),lerp(previous.z,current.z),lerp(previous.eye,current.eye),
            angle(previous.yaw,current.yaw),angle(previous.pitch,current.pitch),angle(previous.roll,current.roll)};
}
} // namespace stuntcar::render
