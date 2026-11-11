#include "platform/scene3d.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace platform::scene3d {
namespace {
Vec3 camera_point(Vec3 p, const View& v) {
    p=p-v.eye;
    return {dot(p,v.right),dot(p,v.up),dot(p,v.forward)};
}
PixelPoint screen(Vec3 p,const View& v,int w,int h) {
    return {(p.x/p.z-v.left)/(v.right_edge-v.left)*w,
            (v.top-p.y/p.z)/(v.top-v.bottom)*h,p.z};
}
std::vector<Vec3> clipped(const std::vector<Vec3>& polygon,double plane,bool near) {
    std::vector<Vec3> out;
    for(std::size_t i=0;i<polygon.size();++i) {
        const auto a=polygon[i], b=polygon[(i+1)%polygon.size()];
        const bool ina=near ? a.z>=plane : a.z<=plane;
        const bool inb=near ? b.z>=plane : b.z<=plane;
        if(ina) out.push_back(a);
        if(ina!=inb) out.push_back(a+(b-a)*((plane-a.z)/(b.z-a.z)));
    }
    return out;
}
double edge(PixelPoint a,PixelPoint b,double x,double y) { return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x); }
}
PixelPoint project(Vec3 point,const View& view,int width,int height) {
    return screen(camera_point(point,view),view,width,height);
}
Image reference_image(const Mesh& mesh,const View& view,int width,int height,Color background) {
    if(width<1 || height<1 || width>8192 || height>8192 || view.near_z<=0 ||
       view.far_z<=view.near_z || view.left>=view.right_edge || view.bottom>=view.top)
        throw std::runtime_error("Invalid perspective view");
    Image image(width,height);
    std::fill(image.pixels.begin(),image.pixels.end(),background);
    std::vector<double> depth(image.pixels.size(),std::numeric_limits<double>::infinity());
    for(const auto& triangle:mesh.triangles) {
        std::vector<Vec3> polygon;
        for(const auto index:triangle.vertices) polygon.push_back(camera_point(mesh.vertices.at(index).position,view));
        polygon=clipped(clipped(polygon,view.near_z,true),view.far_z,false);
        for(std::size_t i=1;i+1<polygon.size();++i) {
            const auto a=screen(polygon[0],view,width,height), b=screen(polygon[i],view,width,height),
                       c=screen(polygon[i+1],view,width,height);
            const double area=edge(a,b,c.x,c.y);
            if(std::abs(area)<1e-12) continue;
            const int x0=int(std::clamp(std::floor(std::min({a.x,b.x,c.x})),0.,double(width)));
            const int x1=int(std::clamp(std::ceil(std::max({a.x,b.x,c.x})),0.,double(width)));
            const int y0=int(std::clamp(std::floor(std::min({a.y,b.y,c.y})),0.,double(height)));
            const int y1=int(std::clamp(std::ceil(std::max({a.y,b.y,c.y})),0.,double(height)));
            for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) {
                const double wa=edge(b,c,x+.5,y+.5)/area, wb=edge(c,a,x+.5,y+.5)/area, wc=1-wa-wb;
                if(wa<0 || wb<0 || wc<0) continue;
                const double z=1/(wa/a.z+wb/b.z+wc/c.z);
                const auto at=std::size_t(y*width+x);
                if(z<depth[at]) { depth[at]=z; image.pixels[at]=mesh.materials.at(triangle.material).colour; }
            }
        }
    }
    return image;
}
} // namespace platform::scene3d
