#include "viewer.hpp"
#include "anim/Pose.hpp"
#include "audio/MusicCatalog.hpp"
#include "data/Animation.hpp"
#include "data/Gnf.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "third_party/bcdec.h"

namespace beacon::reprise {
namespace {
using Index = std::map<std::string, std::filesystem::path>;
data::Bytes read(const std::filesystem::path& p) {
    if (std::filesystem::is_symlink(p) || std::filesystem::file_size(p) > (64u << 20)) throw std::runtime_error("Invalid viewer asset: " + p.filename().string());
    data::Bytes bytes;
    if (!data::loadFile(p, bytes)) throw std::runtime_error("Cannot read " + p.filename().string());
    return bytes;
}
std::shared_ptr<const platform::Image> texture(const data::TextureRef& ref, const Index& files) {
    if (ref.constant) {
        auto im = std::make_shared<platform::Image>(1,1);
        im->pixels[0] = {std::uint8_t(ref.colorRgba), std::uint8_t(ref.colorRgba>>8), std::uint8_t(ref.colorRgba>>16), std::uint8_t(ref.colorRgba>>24)};
        return im;
    }
    if (ref.name.empty()) return {};
    const auto it = files.find(ref.name + ".gnf");
    if (it == files.end()) throw std::runtime_error("Missing character texture: " + ref.name);
    std::string error;
    auto tex = data::loadGnf(read(it->second), &error);
    if (!tex || tex->kind != data::TextureKind::Tex2D || tex->levels.empty()) throw std::runtime_error("Cannot preview texture " + ref.name + ": " + error);
    // Decode a supplied mip, keeping viewer memory bounded; no generated replacement textures.
    std::size_t mip=0;
    while (mip+1 < tex->levels.size() && std::max(tex->levels[mip].width,tex->levels[mip].height)>512) ++mip;
    const auto& lv=tex->levels[mip];
    if (!lv.width || !lv.height || lv.width>4096 || lv.height>4096) throw std::runtime_error("Texture dimensions exceed viewer limit");
    auto im=std::make_shared<platform::Image>(lv.width,lv.height);
    if (tex->format==data::TextureFormat::Rgba8) {
        if (lv.data.size()<im->pixels.size()*4) throw std::runtime_error("Truncated texture pixels");
        std::memcpy(im->pixels.data(),lv.data.data(),im->pixels.size()*4); return im;
    }
    const auto bw=(lv.width+3)/4, bh=(lv.height+3)/4, stride=data::bytesPerElement(tex->format);
    if (!stride || lv.data.size()<std::size_t(bw)*bh*stride) throw std::runtime_error("Truncated compressed texture");
    for (unsigned y=0;y<bh;++y) for (unsigned x=0;x<bw;++x) {
        alignas(16) std::uint8_t block[16]{}; // bcdec's scalar reads require aligned input.
        std::memcpy(block,lv.data.data()+(std::size_t(y)*bw+x)*stride,stride);
        std::uint8_t rgba[64]{};
        switch(tex->format) {
        case data::TextureFormat::Bc1: bcdec_bc1(block,rgba,16); break;
        case data::TextureFormat::Bc2: bcdec_bc2(block,rgba,16); break;
        case data::TextureFormat::Bc3: bcdec_bc3(block,rgba,16); break;
        case data::TextureFormat::Bc7: bcdec_bc7(block,rgba,16); break;
        default: throw std::runtime_error("Unsupported base-colour texture format: " + std::string(data::formatName(tex->format)));
        }
        for(unsigned j=0;j<4;++j) for(unsigned i=0;i<4;++i) if(x*4+i<lv.width && y*4+j<lv.height) {
            const auto* c=rgba+(j*4+i)*4; im->put(x*4+i,y*4+j,{c[0],c[1],c[2],c[3]});
        }
    }
    return im;
}
class Character final : public ui::AnimatedModel {
    std::shared_ptr<const Index> files_;
    data::Model source_;
    data::Skeleton skeleton_;
    std::unique_ptr<anim::ModelPoser> poser_;
    std::optional<data::AnimationClip> clip_;
    std::vector<ui::ModelClip> clips_{{"Bind pose",0,30}};
    std::vector<std::filesystem::path> paths_{{}};
    ui::Model view_;
    std::vector<int> materials_;
public:
    Character(std::shared_ptr<const Index> files, const std::string& stem) : files_(std::move(files)) {
        std::string error;
        const auto bytes=read(files_->at(stem+".jxm"));
        auto jxm=data::JxmFile::parse(bytes,&error);
        auto model=jxm ? data::buildModel(*jxm,&error) : std::nullopt;
        auto skel=data::loadSkeleton(read(files_->at(stem+".skel")),&error);
        if (!model || !skel) throw std::runtime_error("Cannot preview " + stem + ": " + error);
        source_=std::move(*model); skeleton_=std::move(*skel);
        if(source_.vertices.size()>500000 || source_.indices.size()>3000000) throw std::runtime_error("Character exceeds viewer geometry limits");
        poser_=std::make_unique<anim::ModelPoser>(skeleton_,source_);
        for(const auto& [name,path]:*files_) if (name.starts_with(stem+"_anim_") && path.extension()==".anim") {
            const auto b=read(path);
            // Metadata only here; full decode happens on clip selection, including unsupported errors.
            if(b.size()<24 || std::memcmp(b.data(),"80AE",4)) continue;
            const double rate=data::read<float>(b,8); const auto frames=data::u16(b,16);
            if(!std::isfinite(rate) || rate<=0 || rate>1000) continue;
            const auto label=path.stem().string().substr(stem.size()+6);
            clips_.push_back({label,frames>1 ? (frames-1)/rate : 0,rate}); paths_.push_back(path);
        }
        view_.points.resize(source_.vertices.size());
        view_.fit_bounds=std::array<float,6>{source_.boundsMin[0],source_.boundsMin[1],source_.boundsMin[2],source_.boundsMax[0],source_.boundsMax[1],source_.boundsMax[2]};
        for(const auto& mat:source_.materials) view_.textures.push_back(texture(mat.base,*files_));
        for(const auto& draw:source_.draws) {
            if(draw.category==data::fnv1a("Shadowmap") || draw.category==data::fnv1a("ZPrepass")) continue;
            if(draw.material<0 || std::size_t(draw.material)>=source_.materials.size()) continue;
            const auto& mat=source_.materials[draw.material];
            if(std::uint64_t(draw.firstIndex)+draw.indexCount>source_.indices.size()) throw std::runtime_error("Invalid model draw range");
            for(std::uint32_t i=0;i+2<draw.indexCount;i+=3) {
                ui::Model::Face face;
                for(int k=0;k<3;++k) {
                    const std::int64_t n=std::int64_t(source_.indices[draw.firstIndex+i+k])+draw.vertexOffset;
                    if(n<0 || std::uint64_t(n)>=source_.vertices.size()) throw std::runtime_error("Invalid model vertex index");
                    face.corners.push_back(int(n)); face.uv.push_back({});
                }
                auto channel=[](float c){return std::uint8_t(std::clamp(c,0.f,1.f)*255);};
                face.colour={channel(mat.color[0]*mat.gameColor[0]),channel(mat.color[1]*mat.gameColor[1]),channel(mat.color[2]*mat.gameColor[2]),channel(mat.alphaScale)};
                face.texture=draw.material; face.alpha_threshold=mat.alphaThreshold; face.unlit=mat.unlit;
                view_.faces.push_back(std::move(face)); materials_.push_back(draw.material);
            }
        }
        pose(0);
    }
    const std::vector<ui::ModelClip>& clips() const override {return clips_;}
    void select(std::size_t index) override {
        if(index>=paths_.size()) throw std::runtime_error("Invalid animation selection");
        if(index==0) {clip_.reset(); return;}
        std::string error; auto clip=data::loadAnimation(read(paths_[index]),&error);
        if(!clip) throw std::runtime_error(error);
        if(clip->joints.size()!=skeleton_.jointCount()) throw std::runtime_error("Animation and skeleton joint counts differ");
        clip_=std::move(clip);
    }
    void pose(double seconds) override {
        std::vector<data::JointPose> locals=skeleton_.basePose;
        if(clip_) clip_->sample(skeleton_,float(seconds),locals);
        std::vector<math::Mat4> matrices;
        poser_->palette(locals,matrices);
        math::Vec3 offset{};
        if(skeleton_.locomotionJoint<locals.size()) {
            std::vector<math::Mat4> world,base; poser_->worldMatrices(locals,world); poser_->worldMatrices(skeleton_.basePose,base);
            const auto j=skeleton_.locomotionJoint;
            offset={world[j][12]-base[j][12],world[j][13]-base[j][13],world[j][14]-base[j][14]};
        }
        for(std::size_t i=0;i<source_.vertices.size();++i) {
            const auto& v=source_.vertices[i]; std::array<float,3> p{}; float weight=0;
            for(int k=0;k<4;++k) if(v.weights[k]>0 && v.joints[k]<matrices.size()) {
                const auto& m=matrices[v.joints[k]];
                for(int a=0;a<3;++a) p[a]+=v.weights[k]*(m[a]*v.position[0]+m[4+a]*v.position[1]+m[8+a]*v.position[2]+m[12+a]);
                weight+=v.weights[k];
            }
            if(weight==0) p={v.position[0],v.position[1],v.position[2]};
            view_.points[i]={p[0]-offset.x,p[1]-offset.y,p[2]-offset.z};
        }
        std::vector<std::array<float,2>> offsets(source_.materials.size());
        for(std::size_t i=0;i<source_.materials.size();++i) {
            const auto& mat=source_.materials[i];
            float uv[2]={mat.uvOffset[0],mat.uvOffset[1]};
            if(clip_) for(std::size_t c=0;c<skeleton_.userChannelName.size();++c) {
                if(skeleton_.userChannelNode[c]!=mat.nameHash && skeleton_.userChannelNode[c]!=mat.groupHash) continue;
                const auto name=skeleton_.userChannelName[c];
                const int axis=name==data::fnv1a("uvOffset0X") ? 0 : name==data::fnv1a("uvOffset0Y") ? 1 : -1;
                float value; if(axis>=0 && clip_->sampleUser(c,float(seconds),value)) uv[axis]=value;
            }
            offsets[i]={uv[0],uv[1]};
        }
        for(std::size_t i=0;i<view_.faces.size();++i) {
            auto& face=view_.faces[i]; const auto& mat=source_.materials[materials_[i]];
            const auto& uv=offsets[materials_[i]];
            for(std::size_t k=0;k<face.corners.size();++k) {
                const auto& v=source_.vertices[face.corners[k]];
                face.uv[k]={v.uv[0]*mat.uvScale[0]+uv[0],v.uv[1]*mat.uvScale[1]+uv[1]};
            }
        }
    }
    const ui::Model& model() const override {return view_;}
};
}
std::vector<std::filesystem::path> texture_dependencies(const std::filesystem::path& game) {
    Index files;
    for(const auto& e:std::filesystem::recursive_directory_iterator(game/"data")) if(e.is_regular_file() && !e.is_symlink()) {
        const auto name=e.path().filename().string();auto it=files.find(name);
        if(it==files.end() || e.path()<it->second) files[name]=e.path();
    }
    std::set<std::filesystem::path> paths;
    for(const auto& [name,path]:files) if(path.extension()==".jxm" && path.parent_path()==game/"data/multi_platformer/gfx") {
        const auto bytes=read(path);std::string error;
        const auto file=data::JxmFile::parse(bytes,&error);
        if(!file || !file->table("VBDT")) continue;
        const auto model=data::buildModel(*file,&error);
        if(!model) throw std::runtime_error("Cannot export model dependencies: "+error);
        for(const auto& material:model->materials) {
            const auto& ref=material.base;
            if(ref.constant || ref.name.empty()) continue;
            const auto it=files.find(ref.name+".gnf");
            if(it==files.end()) throw std::runtime_error("Source texture missing: "+ref.name);
            paths.insert(std::filesystem::relative(it->second,game));
        }
    }
    return {paths.begin(),paths.end()};
}
std::vector<app::Preview> previews(const std::filesystem::path& content) {
    auto files=std::make_shared<Index>();
    for(const auto& e:std::filesystem::recursive_directory_iterator(content/"data")) {
        if(e.is_symlink()) throw std::runtime_error("Symlinks are not supported in character assets");
        if(e.is_regular_file()) {
            const auto name=e.path().filename().string(); auto it=files->find(name);
            if(it==files->end() || e.path()<it->second) (*files)[name]=e.path();
        }
    }
    std::vector<std::string> stems;
    for(const auto& [name,path]:*files) if(path.extension()==".jxm" && path.parent_path().filename()=="gfx" &&
        path.parent_path().parent_path().filename()=="multi_platformer" && files->contains(path.stem().string()+".skel")) {
        const auto bytes=read(path);const auto model=data::JxmFile::parse(bytes);
        if(model && model->table("VBDT")) stems.push_back(path.stem().string());
    }
    std::stable_sort(stems.begin(),stems.end(),[](const auto& a,const auto& b){
        auto priority=[](const auto& s){return s=="mup_hero" ? 0 : s=="mup_arbot" ? 1 : 2;};
        return priority(a)<priority(b);
    });
    std::vector<app::Preview> out;
    for(const auto& stem:stems) {
        app::Preview preview{stem,platform::Image(0,0)};
        preview.animated_model=[files,stem]{return std::make_unique<Character>(files,stem);};
        out.push_back(std::move(preview));
    }
    if(out.empty()) throw std::runtime_error("No character models with skeletons found in the imported assets");
    std::vector<std::filesystem::path> banks;
    for (const auto& [name,path]:*files) if(path.extension()==".sxd") banks.push_back(path);
    auto music=std::make_shared<audio::MusicCatalog>();
    music->scan(banks);
    std::vector<app::Preview> tracks;
    for(const auto& track:music->tracks()) {
        app::Preview preview{"Music: " + track.title,platform::Image(0,0)};
        preview.load_sound=[music,track] {
            audio::Pcm pcm; std::size_t start{},end{}; std::string error;
            if(!music->decode(track,pcm,start,end,&error)) throw std::runtime_error("Cannot decode music: "+error);
            if((pcm.channels!=1 && pcm.channels!=2) || !pcm.sampleRate || pcm.samples.empty())
                throw std::runtime_error("Unsupported music format");
            app::PreviewSound sound{{},double(pcm.sampleRate),pcm.channels};
            sound.samples.reserve(pcm.samples.size());
            for(auto sample:pcm.samples) sound.samples.push_back(float(sample)/32768.f);
            return sound;
        };
        tracks.push_back(std::move(preview));
    }
    // Keep the two lead characters first, with music immediately accessible afterwards.
    out.insert(out.begin()+std::min<std::size_t>(2,out.size()),std::make_move_iterator(tracks.begin()),std::make_move_iterator(tracks.end()));
    return out;
}
}
