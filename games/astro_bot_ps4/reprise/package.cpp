// Bounded, random-access reader for the supplied merged CUSA12392 package.
// Format evidence: Beacon .analysis/tools/pkgfs.py and docs/probe.md; this
// adapter owns no host I/O beyond FileSource and Library's staging directory.
#include "package.hpp"
#include "package_keys.hpp"
#include "data/Model.hpp"
#include "platform/sha256.hpp"
#include <mbedtls/aes.h>
#include <mbedtls/bignum.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cctype>
#include <fstream>
#include <map>
#include <set>

namespace beacon::reprise {
namespace {
using Bytes=std::vector<std::uint8_t>;
using Read=std::function<Bytes(std::uint64_t,std::size_t)>;
constexpr std::uint64_t package_size=7486636032ull;
constexpr std::string_view package_hash="028c88e1dd07efcebd085d5a91ed9e4ab7b3d192d7ce4f0cc94958b568e807e2";
void require(bool ok,const char* why) {if(!ok) throw std::runtime_error(std::string("Astro Bot package: ")+why);}
void range(std::uint64_t at,std::uint64_t n,std::uint64_t size) {require(at<=size && n<=size-at,"invalid or truncated data range");}
std::uint64_t number(const Bytes& b,std::size_t at,int n,bool big=false) {
    range(at,n,b.size()); std::uint64_t v=0;
    for(int i=0;i<n;++i) v|=std::uint64_t(b[at+i])<<(8*(big?n-1-i:i));
    return v;
}
std::string hex(std::span<const std::uint8_t> b) {
    std::string s;for(auto c:b) {s+="0123456789abcdef"[c>>4];s+="0123456789abcdef"[c&15];}return s;
}
Bytes sha(const Bytes& bytes) {
    Bytes out(32);require(mbedtls_sha256(bytes.data(),bytes.size(),out.data(),0)==0,"SHA-256 failed");return out;
}
Bytes hmac(const Bytes& key,const Bytes& message) {
    Bytes out(32);require(mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),key.data(),key.size(),message.data(),message.size(),out.data())==0,"HMAC failed");return out;
}
Bytes rsa(const Bytes& encrypted,const char* modulus,const char* exponent) {
    struct Integer {mbedtls_mpi n;Integer(){mbedtls_mpi_init(&n);}~Integer(){mbedtls_mpi_free(&n);}} a,m,e,result;
    require(encrypted.size()==256,"invalid RSA block size");
    require(mbedtls_mpi_read_binary(&a.n,encrypted.data(),encrypted.size())==0 &&
        mbedtls_mpi_read_string(&m.n,16,modulus)==0 && mbedtls_mpi_read_string(&e.n,16,exponent)==0 &&
        mbedtls_mpi_cmp_mpi(&a.n,&m.n)<0 && mbedtls_mpi_exp_mod(&result.n,&a.n,&e.n,&m.n,nullptr)==0,"RSA operation failed");
    Bytes out(256);require(mbedtls_mpi_write_binary(&result.n,out.data(),out.size())==0,"RSA result failed");
    require(out[0]==0 && out[1]==2,"invalid RSA padding");
    auto end=std::find(out.begin()+2,out.end(),0);require(end>=out.begin()+10 && end!=out.end(),"invalid RSA padding length");
    return {end+1,out.end()};
}
struct Source {std::uint64_t size;Read read;};
Bytes get(const Source& src,std::uint64_t at,std::size_t n) {range(at,n,src.size);auto b=src.read(at,n);require(b.size()==n,"short read");return b;}
// Small caches bound memory independently of the 7 GB source and 14 GB filesystem.
struct Encrypted {
    Source raw;std::uint64_t start;mbedtls_aes_xts_context ctx{};std::map<std::uint64_t,Bytes> cache;
    Encrypted(Source source,const Bytes& key,std::uint64_t first):raw(std::move(source)),start(first) {
        mbedtls_aes_xts_init(&ctx);
        Bytes keys(key.begin()+16,key.end());keys.insert(keys.end(),key.begin(),key.begin()+16);
        require(mbedtls_aes_xts_setkey_dec(&ctx,keys.data(),256)==0,"invalid XTS key");
    }
    ~Encrypted(){mbedtls_aes_xts_free(&ctx);}
    Bytes read(std::uint64_t at,std::size_t n) {
        range(at,n,raw.size);Bytes out;out.reserve(n);
        while(n) {
            const auto sector=at/4096, offset=at%4096;const auto count=std::min<std::size_t>(n,4096-offset);
            auto it=cache.find(sector);
            if(it==cache.end()) {
                auto bytes=get(raw,sector*4096,4096);
                if(sector>=start) {
                    std::array<unsigned char,16> tweak{};for(int k=0;k<8;++k)tweak[k]=std::uint8_t(sector>>(8*k));
                    Bytes plain(4096);require(mbedtls_aes_crypt_xts(&ctx,MBEDTLS_AES_DECRYPT,4096,tweak.data(),bytes.data(),plain.data())==0,"XTS decode failed");bytes=std::move(plain);
                }
                if(cache.size()>=4096)cache.clear();it=cache.emplace(sector,std::move(bytes)).first;
            }
            out.insert(out.end(),it->second.begin()+offset,it->second.begin()+offset+count);at+=count;n-=count;
        }
        return out;
    }
};
struct Compressed {
    Source raw;std::uint64_t size{},bs{};std::vector<std::uint64_t> offsets;std::map<std::uint64_t,Bytes> cache;
    explicit Compressed(Source source):raw(std::move(source)) {
        auto h=get(raw,0,48);require(number(h,0,4)==0x43534650,"invalid PFSC signature");
        bs=number(h,12,4);size=number(h,40,8);
        require(bs>=4096 && bs<=1048576 && number(h,16,8)==bs && size>0 && size<=32ull<<30 && size%bs==0,"unsupported PFSC dimensions");
        const auto count=size/bs+1;require(count<=8388609,"PFSC table too large");
        const auto table=get(raw,number(h,24,8),std::size_t(count*8));
        for(std::size_t i=0;i<count;++i) {auto off=number(table,i*8,8);require(off<=raw.size && (offsets.empty() || off>=offsets.back()),"invalid PFSC offset");offsets.push_back(off);}
    }
    Bytes read(std::uint64_t at,std::size_t n) {
        range(at,n,size);Bytes out;out.reserve(n);
        while(n) {
            const auto block=at/bs,offset=at%bs;const auto count=std::min<std::size_t>(n,bs-offset);
            auto it=cache.find(block);
            if(it==cache.end()) {
                const auto length=offsets[block+1]-offsets[block];Bytes bytes(bs);
                if(length==bs)bytes=get(raw,offsets[block],bs);
                else if(length<bs) {
                    require(length>2,"empty compressed block");auto packed=get(raw,offsets[block],length);
                    z_stream z{};z.next_in=packed.data()+2;z.avail_in=uInt(length-2);z.next_out=bytes.data();z.avail_out=uInt(bs);
                    require(inflateInit2(&z,-15)==Z_OK,"cannot initialize decompressor");
                    const int result=inflate(&z,Z_FINISH);const auto written=z.total_out;inflateEnd(&z);
                    require(result==Z_STREAM_END && written==bs,"damaged compressed block");
                } // Oversized extent is a sparse/zero block in this format.
                if(cache.size()>=64)cache.clear();it=cache.emplace(block,std::move(bytes)).first;
            }
            out.insert(out.end(),it->second.begin()+offset,it->second.begin()+offset+count);at+=count;n-=count;
        }
        return out;
    }
};
struct Node {std::uint64_t size{},stored{};std::uint32_t blocks{};std::uint16_t mode{};std::array<std::uint32_t,12> direct{};std::array<std::uint32_t,5> indirect{};};
class Filesystem {
    Source src_;std::uint64_t bs_{};bool signed_{};std::vector<Node> nodes_;
    std::set<std::uint32_t> directories_;
    void walk(std::uint32_t ino,const std::string& prefix,unsigned depth) {
        require(depth<32 && directories_.insert(ino).second,"cyclic or deep filesystem directory");
        require(ino<nodes_.size(),"directory inode out of range");const auto& node=nodes_[ino];
        require((node.mode&0xf000)==0x4000 && node.blocks<=1024,"invalid directory inode");
        for(std::uint32_t block=0;block<node.blocks;++block) {
            const auto bytes=get(src_,(std::uint64_t(node.direct[0])+block)*bs_,bs_);
            for(std::size_t at=0;at+16<=bytes.size();) {
                const auto child=number(bytes,at,4),type=number(bytes,at+4,4),length=number(bytes,at+8,4),stride=number(bytes,at+12,4);
                if(!stride)break;
                require(stride>=16 && stride<=bytes.size()-at && length<=stride-16 && length<=255,"invalid directory record");
                const std::string name(bytes.begin()+at+16,bytes.begin()+at+16+length);
                if(type==2 || type==3) {
                    require(!name.empty() && name!="." && name!=".." && name.back()!='.' && name.back()!=' ',"unsafe directory name");
                    for(unsigned char c:name)require(c>=32 && c<127 && std::string_view("/\\:*?\"<>|").find(c)==std::string_view::npos,"unsafe directory character");
                    const auto path=prefix+name;require(path.size()<=512 && child<nodes_.size(),"invalid file path or inode");
                    if(type==3)walk(std::uint32_t(child),path+"/",depth+1);
                    else require(files.emplace(path,std::uint32_t(child)).second,"duplicate file name");
                }
                at+=stride;
            }
        }
    }
public:
    std::map<std::string,std::uint32_t> files;
    explicit Filesystem(Source source,const Bytes& ek={}):src_(std::move(source)) {
        const auto h=get(src_,0,0x400);require(number(h,0,8)==1 && number(h,8,8)==20130315,"invalid PFS superblock");
        const auto mode=number(h,0x1c,2);signed_=mode&1;bs_=number(h,0x20,4);
        const auto count=number(h,0x30,8),blocks=number(h,0x40,8),stride=std::uint64_t(signed_?0x2c8:0xa8);
        require(bs_>=4096 && bs_<=1048576 && bs_%4096==0 && count>0 && count<=100000 && blocks>0 && blocks<=10000 && count<=blocks*(bs_/stride),"unsupported PFS layout");
        if(mode&4) {
            require(ek.size()==32,"missing PFS key");Bytes seed(h.begin()+0x370,h.begin()+0x380),message{1,0,0,0};message.insert(message.end(),seed.begin(),seed.end());
            bool found=false;
            for(const auto& key:{ek,hmac(ek,seed)}) {
                auto crypt=std::make_shared<Encrypted>(src_,hmac(key,message),bs_/4096);
                if((number(crypt->read(bs_,2),0,2)&0xf000)==0x4000) {
                    src_={src_.size,[crypt](auto at,auto n){return crypt->read(at,n);}};found=true;break;
                }
            }
            require(found,"unsupported PFS encryption");
        }
        for(std::uint64_t b=0;b<blocks && nodes_.size()<count;++b) {
            const auto bytes=get(src_,(b+1)*bs_,bs_);
            for(std::size_t at=0;at+stride<=bytes.size() && nodes_.size()<count;at+=stride) {
                Node node;node.mode=number(bytes,at,2);node.size=number(bytes,at+8,8);node.stored=number(bytes,at+16,8);node.blocks=number(bytes,at+0x60,4);
                require(node.size<=32ull<<30 && node.stored<=32ull<<30,"file exceeds filesystem limit");
                for(int j=0;j<17;++j) {
                    const auto v=std::uint32_t(number(bytes,at+0x64+j*(signed_?36:4)+(signed_?32:0),4));
                    if(j<12)node.direct[j]=v;else node.indirect[j-12]=v;
                }
                nodes_.push_back(node);
            }
        }
        walk(0,"",0);
    }
    Source open(std::uint32_t ino) const {
        require(ino<nodes_.size(),"file inode out of range");const auto& node=nodes_[ino];
        require((node.mode&0xf000)==0x8000,"not a regular file");
        std::vector<std::uint32_t> blocks;
        if(node.blocks>1 && node.direct[1]!=0xffffffff) {
            require(signed_ && node.blocks<=524288,"unsupported indirect filesystem");
            for(std::size_t i=0;i<std::min<std::size_t>(12,node.blocks);++i)blocks.push_back(node.direct[i]);
            auto indirect=[&](std::uint32_t block) {const auto bytes=get(src_,std::uint64_t(block)*bs_,bs_);std::vector<std::uint32_t> out;for(std::size_t i=0;i<bs_/36;++i)out.push_back(number(bytes,i*36+32,4));return out;};
            if(blocks.size()<node.blocks) {auto list=indirect(node.indirect[0]);list.resize(std::min<std::size_t>(list.size(),node.blocks-blocks.size()));blocks.insert(blocks.end(),list.begin(),list.end());}
            if(blocks.size()<node.blocks) for(auto block:indirect(node.indirect[1])) {
                auto list=indirect(block);list.resize(std::min<std::size_t>(list.size(),node.blocks-blocks.size()));blocks.insert(blocks.end(),list.begin(),list.end());if(blocks.size()==node.blocks)break;
            }
            require(blocks.size()==node.blocks,"unsupported triple-indirect file");
        }
        const auto source=src_;const auto bs=bs_;const auto first=node.direct[0];
        Source raw{node.size,[source,bs,first,blocks=std::move(blocks)](std::uint64_t at,std::size_t n) {
            if(blocks.empty())return get(source,std::uint64_t(first)*bs+at,n);
            Bytes out;out.reserve(n);
            while(n) {require(at/bs<blocks.size(),"file block out of range");const auto count=std::min<std::size_t>(n,bs-at%bs);auto b=get(source,std::uint64_t(blocks[at/bs])*bs+at%bs,count);out.insert(out.end(),b.begin(),b.end());at+=count;n-=count;}return out;
        }};
        if(node.size!=node.stored) {
            auto compressed=std::make_shared<Compressed>(raw);require(compressed->size==node.stored,"inconsistent compressed file size");
            return {compressed->size,[compressed](auto at,auto n){return compressed->read(at,n);}};
        }
        return raw;
    }
};
void write(const std::filesystem::path& dest,const Bytes& bytes) {
    std::filesystem::create_directories(dest.parent_path());std::ofstream out(dest,std::ios::binary|std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));out.close();require(bool(out),"cannot write imported asset");
}
}
std::string import_package(platform::FileSource& file,const std::filesystem::path& folder,const app::ImportProgress& progress) {
    require(file.size()==package_size,"unsupported or truncated release; expected CUSA12392.v1.04.pkg");
    // Authenticate the exact supported input before parsing nested filesystem data.
    mbedtls_sha256_context context;mbedtls_sha256_init(&context);
    struct Cleanup {mbedtls_sha256_context* p;~Cleanup(){mbedtls_sha256_free(p);}} cleanup{&context};
    require(mbedtls_sha256_starts(&context,0)==0,"cannot start package verification");
    Bytes buffer(4<<20);
    for(std::uint64_t at=0;at<file.size();) {
        const auto n=std::min<std::uint64_t>(buffer.size(),file.size()-at);file.read(at,std::span(buffer).first(n));
        require(mbedtls_sha256_update(&context,buffer.data(),n)==0,"package verification failed");at+=n;
        if(progress)progress("Verifying Astro Bot package",double(at)/file.size());
    }
    std::array<std::uint8_t,32> digest{};require(mbedtls_sha256_finish(&context,digest.data())==0 && hex(digest)==package_hash,"package checksum differs from the supported copy");
    Source source{file.size(),[&file](auto at,auto n){Bytes b(n);file.read(at,b);return b;}};
    const auto h=get(source,0,4096);const auto count=number(h,0x10,4,true),table_at=number(h,0x18,4,true);
    require(count>0 && count<=4096,"invalid package table");auto table=get(source,table_at,count*32);
    struct Entry {Bytes meta;std::uint64_t at,size;};std::map<std::uint32_t,Entry> entries;
    for(std::size_t i=0;i<count;++i) {
        auto at=number(table,i*32+16,4,true),n=number(table,i*32+20,4,true);range(at,n,source.size);
        require(entries.emplace(number(table,i*32,4,true),Entry{Bytes(table.begin()+i*32,table.begin()+(i+1)*32),at,n}).second,"duplicate package entry");
    }
    auto entry=[&](std::uint32_t id){require(entries.contains(id),"missing package entry");const auto& e=entries.at(id);require(e.size<=1<<20,"oversized metadata");return get(source,e.at,e.size);};
    auto sfo=entry(0x1000);require(platform::sha256(sfo)=="b8198d82c595d41f092424cc14234bf1b26e82c0427970fe0c70ac5876af2d9c","unsupported package metadata");
    const auto keys=entry(0x10);range(1024,256,keys.size());
    auto derived=rsa(Bytes(keys.begin()+1024,keys.begin()+1280),pkg::derived_modulus,pkg::derived_exponent);
    auto encrypted=entry(0x20);auto mix=entries.at(0x20).meta;mix.insert(mix.end(),derived.begin(),derived.end());auto ivkey=sha(mix);
    require(encrypted.size()==256,"invalid image key size");Bytes decoded(256);
    mbedtls_aes_context aes;mbedtls_aes_init(&aes);
    const int set=mbedtls_aes_setkey_dec(&aes,ivkey.data()+16,128);
    const int dec=set ? set : mbedtls_aes_crypt_cbc(&aes,MBEDTLS_AES_DECRYPT,256,ivkey.data(),encrypted.data(),decoded.data());mbedtls_aes_free(&aes);require(dec==0,"image key decryption failed");
    auto ek=rsa(decoded,pkg::fake_modulus,pkg::fake_exponent);
    const auto offset=number(h,0x410,8,true),size=number(h,0x418,8,true);range(offset,size,source.size);
    if(progress)progress("Reading Astro Bot filesystem",-1);
    Filesystem outer({size,[source,offset](auto at,auto n){return get(source,offset+at,n);}},ek);
    require(outer.files.contains("uroot/pfs_image.dat"),"missing inner filesystem");
    Filesystem inner(outer.open(outer.files.at("uroot/pfs_image.dat")));
    std::map<std::string,std::uint32_t> paths;std::map<std::string,std::string> names;
    for(const auto& [raw,ino]:inner.files) {
        const auto path=raw.starts_with("uroot/") ? raw.substr(6) : raw;
        require(paths.emplace(path,ino).second,"duplicate inner path");
        if(path.starts_with("data/"))names.try_emplace(std::filesystem::path(path).filename().string(),path);
    }
    auto read_asset=[&](const std::string& name){const auto src=inner.open(paths.at(name));require(src.size<=64u<<20,"asset exceeds 64 MiB limit");return get(src,0,src.size);};
    std::set<std::string> selected;
    for(const auto& [path,ino]:paths) {
        const auto p=std::filesystem::path(path);const auto ext=p.extension().string();
        if(path.starts_with("data/") && ext==".sxd" && p.stem().string().ends_with("_bgm"))selected.insert(path);
        if((path.starts_with("data/multi_platformer/gfx/") || path.starts_with("data/multi_platformer/anim/") || path.starts_with("data/common/")) &&
            (ext==".jxm" || ext==".gnf" || ext==".skel" || ext==".anim"))selected.insert(path);
        if(p.parent_path()=="data/multi_platformer/gfx" && ext==".jxm") {
            const auto bytes=read_asset(path);auto jxm=data::JxmFile::parse(bytes);
            if(!jxm || !jxm->table("VBDT"))continue;
            auto model=data::buildModel(*jxm);require(model.has_value(),"invalid model material dependencies");
            for(const auto& material:model->materials) if(!material.base.constant && !material.base.name.empty()) {
                const auto name=material.base.name+".gnf";require(names.contains(name),"missing model texture");selected.insert(names.at(name));
            }
        }
    }
    require(selected.size()<=12000 && selected.contains("data/multi_platformer/gfx/mup_hero.jxm") && selected.contains("data/multi_platformer/anim/mup_hero.skel"),"missing character assets");
    std::uint64_t total=sfo.size(),done=0;std::set<std::string> folded;
    for(const auto& path:selected) {
        auto lower=path;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});require(folded.insert(lower).second,"case-colliding asset paths");
        const auto src=inner.open(paths.at(path));require(src.size<=64u<<20,"asset exceeds size limit");total+=src.size;require(total<=2ull<<30,"asset import exceeds 2 GiB");
    }
    write(folder/"sce_sys/param.sfo",sfo);done+=sfo.size();
    for(const auto& path:selected) {auto bytes=read_asset(path);write(folder/path,bytes);done+=bytes.size();if(progress)progress("Importing Astro Bot characters and music",double(done)/total);}
    return "CUSA12392 — package 1.04 (metadata 01.03); characters and music";
}
}
