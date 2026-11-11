#include "platform/zip_archive.hpp"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace platform {
namespace {
void need(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
std::uint64_t number(std::span<const std::uint8_t> b, std::size_t p, int n) {
    need(p<=b.size() && std::size_t(n)<=b.size()-p,"Truncated ZIP directory.");
    std::uint64_t v=0;for(int i=0;i<n;++i)v|=std::uint64_t(b[p+i])<<(i*8);return v;
}
std::vector<std::uint8_t> read(FileSource& s,std::uint64_t p,std::size_t n) {
    need(p<=s.size() && n<=s.size()-p,"ZIP entry lies outside the archive.");
    std::vector<std::uint8_t> b(n);s.read(p,b);return b;
}
}
bool ZipArchive::signature(FileSource& s) {
    if(s.size()<4)return false;
    const auto b=read(s,0,4);const auto v=number(b,0,4);
    return v==0x04034b50 || v==0x06054b50;
}
ZipArchive::ZipArchive(FileSource& s):source_(s) {
    const auto tail=read(s,s.size()-std::min<std::uint64_t>(s.size(),65557),std::size_t(std::min<std::uint64_t>(s.size(),65557)));
    std::size_t end=tail.size();
    for(std::size_t p=tail.size();p-- > 0;)if(p+22<=tail.size() && number(tail,p,4)==0x06054b50 && p+22+number(tail,p+20,2)==tail.size()){end=p;break;}
    need(end<tail.size(),"ZIP end record is missing or damaged.");
    need(number(tail,end+4,2)==0 && number(tail,end+6,2)==0,"Split ZIP archives are not supported.");
    auto count=number(tail,end+10,2), bytes=number(tail,end+12,4), offset=number(tail,end+16,4);
    need(number(tail,end+8,2)==count,"Split ZIP archives are not supported.");
    const auto end_offset=s.size()-tail.size()+end;
    if(count==65535 || bytes==0xffffffff || offset==0xffffffff) {
        need(end_offset>=20,"Missing ZIP64 locator.");
        auto loc=read(s,end_offset-20,20);
        need(number(loc,0,4)==0x07064b50 && number(loc,4,4)==0 && number(loc,16,4)==1,"Invalid or split ZIP64 archive.");
        auto z=read(s,number(loc,8,8),56);
        need(number(z,0,4)==0x06064b50 && number(z,4,8)>=44 && number(z,16,4)==0 && number(z,20,4)==0,"Invalid ZIP64 record.");
        count=number(z,32,8);bytes=number(z,40,8);offset=number(z,48,8);
        need(number(z,24,8)==count,"Split ZIP64 archives are not supported.");
    }
    need(count<=100000 && bytes<=64*1024*1024,"ZIP directory exceeds the supported size.");
    need(offset<=end_offset && bytes<=end_offset-offset,"Invalid ZIP directory bounds.");
    const auto directory=read(s,offset,std::size_t(bytes));std::size_t p=0;
    for(std::uint64_t i=0;i<count;++i) {
        need(p+46<=directory.size() && number(directory,p,4)==0x02014b50,"Invalid ZIP directory entry.");
        Entry e; e.flags=number(directory,p+8,2);e.method=number(directory,p+10,2);e.crc=number(directory,p+16,4);
        e.compressed=number(directory,p+20,4);e.size=number(directory,p+24,4);e.offset=number(directory,p+42,4);
        const auto names=number(directory,p+28,2),extras=number(directory,p+30,2),comments=number(directory,p+32,2);
        need(number(directory,p+34,2)==0,"Split ZIP entries are not supported.");
        need(names+extras+comments<=directory.size()-p-46,"Truncated ZIP filename.");
        e.name.assign(reinterpret_cast<const char*>(directory.data()+p+46),std::size_t(names));
        need(e.name.find('\0')==std::string::npos,"Invalid ZIP filename.");
        bool zip64=false;
        for(std::size_t q=p+46+names,stop=q+extras;q<stop;) {
            need(q+4<=stop,"Invalid ZIP extra field.");const auto type=number(directory,q,2),len=number(directory,q+2,2);q+=4;
            need(len<=stop-q,"Truncated ZIP extra field.");
            if(type==1){zip64=true;std::size_t at=q;auto next=[&]{need(at+8<=q+len,"Missing ZIP64 field.");auto v=number(directory,at,8);at+=8;return v;};
                if(e.size==0xffffffff)e.size=next();if(e.compressed==0xffffffff)e.compressed=next();if(e.offset==0xffffffff)e.offset=next();}
            q+=len;
        }
        need(zip64 || (e.size!=0xffffffff && e.compressed!=0xffffffff && e.offset!=0xffffffff),"Missing ZIP64 entry sizes.");
        entries_.push_back(std::move(e));p+=46+names+extras+comments;
    }
}
void ZipArchive::extract(const Entry& e,const std::filesystem::path& target,std::uint64_t limit,const std::function<void(double)>& progress) {
    need(e.size<=limit,"Archived game image is too large.");
    need(!(e.flags & (1|64)),"Encrypted ZIP entries are not supported; unpack them first.");
    need(e.method==0 || e.method==8,"ZIP compression method is not supported; use stored or deflate compression.");
    const auto h=read(source_,e.offset,30);
    need(number(h,0,4)==0x04034b50 && number(h,8,2)==e.method && number(h,6,2)==e.flags,"ZIP local header disagrees with directory.");
    const auto name_size=number(h,26,2), extra_size=number(h,28,2);
    const auto name=read(source_,e.offset+30,std::size_t(name_size));
    need(std::string(name.begin(),name.end())==e.name,"ZIP filenames disagree.");
    auto pos=e.offset+30+name_size+extra_size;
    need(pos<=source_.size() && e.compressed<=source_.size()-pos,"Truncated ZIP data.");
    std::ofstream out(target,std::ios::binary);need(bool(out),"Could not create temporary game image.");
    z_stream z{};bool inflated=false;
    struct End {z_stream& z;bool& active;~End(){if(active)inflateEnd(&z);}} end{z,inflated};
    if(e.method==8){need(inflateInit2(&z,-MAX_WBITS)==Z_OK,"Could not start ZIP decompression.");inflated=true;}
    std::array<std::uint8_t,128*1024> input{},output{};
    std::uint64_t remaining=e.compressed,written=0;uLong crc=crc32(0,nullptr,0);int status=Z_OK;
    auto write=[&](const std::uint8_t* data,std::size_t n){
        need(n<=e.size-written,"ZIP entry expands beyond its declared size.");out.write(reinterpret_cast<const char*>(data),std::streamsize(n));need(bool(out),"Writing temporary game image failed (check free space).");written+=n;crc=crc32(crc,data,uInt(n));};
    while(remaining) {
        const auto n=std::size_t(std::min<std::uint64_t>(input.size(),remaining));source_.read(pos,std::span(input).first(n));pos+=n;remaining-=n;
        if(e.method==0)write(input.data(),n);
        else {
            need(status!=Z_STREAM_END,"Unexpected data after ZIP stream.");z.next_in=input.data();z.avail_in=uInt(n);
            do {z.next_out=output.data();z.avail_out=uInt(output.size());status=inflate(&z,Z_NO_FLUSH);
                if(status==Z_BUF_ERROR && !z.avail_in && z.avail_out==output.size())break;
                need(status==Z_OK || status==Z_STREAM_END,"Damaged ZIP compressed data.");write(output.data(),output.size()-z.avail_out);
                if(status==Z_STREAM_END){need(!z.avail_in && !remaining,"Unexpected data after ZIP stream.");break;}
            } while(z.avail_in || z.avail_out==0);
        }
        if(progress)progress(e.compressed?double(e.compressed-remaining)/double(e.compressed):1);
    }
    need(e.method==0 || status==Z_STREAM_END,"Incomplete ZIP compressed stream.");
    need(written==e.size && crc==e.crc,"ZIP checksum or size mismatch.");out.close();need(bool(out),"Closing temporary game image failed.");
}
}
