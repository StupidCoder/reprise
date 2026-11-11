#include "app/library.hpp"
#include "platform/files.hpp"
#include "platform/zip_archive.hpp"
#include <zlib.h>
#include <iostream>
#include <chrono>
#include <stdexcept>
using Bytes=std::vector<std::uint8_t>;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
void put(Bytes& b,std::uint64_t x,int n){for(int i=0;i<n;++i)b.push_back(std::uint8_t(x>>(i*8)));}
struct Item {std::string name,data;bool deflate{};};
Bytes archive(const std::vector<Item>& items,bool zip64=false,bool corrupt=false) {
    Bytes out,dir;
    for(const auto& item:items){
        Bytes payload(item.data.begin(),item.data.end());
        if(item.deflate){z_stream z{};check(deflateInit2(&z,6,Z_DEFLATED,-15,8,Z_DEFAULT_STRATEGY)==Z_OK,"deflater");Bytes packed(compressBound(payload.size()));z.next_in=payload.data();z.avail_in=payload.size();z.next_out=packed.data();z.avail_out=packed.size();check(deflate(&z,Z_FINISH)==Z_STREAM_END,"compress");packed.resize(z.total_out);deflateEnd(&z);payload=std::move(packed);}
        const auto offset=out.size();const auto crc=crc32(0,reinterpret_cast<const Bytef*>(item.data.data()),item.data.size())+(corrupt?1:0);
        put(out,0x04034b50,4);put(out,20,2);put(out,0,2);put(out,item.deflate?8:0,2);put(out,0,4);put(out,crc,4);put(out,payload.size(),4);put(out,item.data.size(),4);put(out,item.name.size(),2);put(out,0,2);out.insert(out.end(),item.name.begin(),item.name.end());out.insert(out.end(),payload.begin(),payload.end());
        put(dir,0x02014b50,4);put(dir,20,2);put(dir,20,2);put(dir,0,2);put(dir,item.deflate?8:0,2);put(dir,0,4);put(dir,crc,4);put(dir,zip64?0xffffffff:payload.size(),4);put(dir,zip64?0xffffffff:item.data.size(),4);put(dir,item.name.size(),2);put(dir,zip64?28:0,2);put(dir,0,2);put(dir,0,2);put(dir,0,2);put(dir,0,4);put(dir,zip64?0xffffffff:offset,4);dir.insert(dir.end(),item.name.begin(),item.name.end());
        if(zip64){put(dir,1,2);put(dir,24,2);put(dir,item.data.size(),8);put(dir,payload.size(),8);put(dir,offset,8);}
    }
    const auto start=out.size();out.insert(out.end(),dir.begin(),dir.end());
    if(zip64){const auto at=out.size();put(out,0x06064b50,4);put(out,44,8);put(out,45,2);put(out,45,2);put(out,0,4);put(out,0,4);put(out,items.size(),8);put(out,items.size(),8);put(out,dir.size(),8);put(out,start,8);put(out,0x07064b50,4);put(out,0,4);put(out,at,8);put(out,1,4);}
    put(out,0x06054b50,4);put(out,0,4);put(out,zip64?65535:items.size(),2);put(out,zip64?65535:items.size(),2);put(out,zip64?0xffffffff:dir.size(),4);put(out,zip64?0xffffffff:start,4);put(out,0,2);return out;
}
int main(){
    const auto root=std::filesystem::temp_directory_path()/("reprise-zip-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::filesystem::remove_all(p);}}cleanup{root};
    try {
        std::filesystem::create_directories(root);int calls=0;
        app::GameEntry a;a.id="a";a.title="A";a.largest_image=2<<20;
        a.import=[&](auto bytes){++calls;std::string data(bytes.begin(),bytes.end());if(!data.starts_with("A"))throw app::NotThisGame("Unknown game");return app::ImportResult{{{"data",Bytes(bytes.begin(),bytes.end())}},"A"};};
        auto b=a;b.id="b";b.title="B";b.import=[&](auto bytes){++calls;if(bytes.size()!=2 || bytes[0]!='B')throw app::NotThisGame("Unknown game");return app::ImportResult{{{std::string("disk")+char(bytes[1]),Bytes(bytes.begin(),bytes.end())}},"B",bytes[1]-'0',2};};
        app::Library lib(root/"library",{a,b});
        auto run=[&](Bytes zip){platform::write_file(root/"document",zip);return lib.import(root/"document",{});};
        auto result=run(archive({{"folder/A.gg","A",true},{"folder/B2.adf","B2",false},{"../B1.adf","B1",true},{"__MACOSX/._noise.gg","noise",false},{"folder/.DS_Store","junk",false},{"unsupported.gg","other",true}}));
        check(result.ok && lib.installed(0) && lib.installed(1),"Collection installs games and out-of-order disks");
        check(result.details.size()==4,"Metadata ignored and unknown game reported");
        check(!std::filesystem::exists(root/"B1.adf"),"Archive paths never escape staging");
        check(result.message.find("1 file(s)")!=std::string::npos,"Partial failure summary");
        check(std::filesystem::is_empty(root/"library/archive-import"),"Temporary files cleaned");
        result=run(archive({{"large.gg","A"+std::string(300000,'x'),true}},true));check(result.ok,"ZIP64 and chunked deflate");
        result=run(archive({{"bad.gg","A",true}},false,true));check(!result.ok && lib.installed(0),"Corrupt ZIP cannot replace existing import");
        result=run(archive({{"big.gg","A"+std::string(3<<20,'x'),true}}));check(!result.ok,"Oversize entry bounded");
        auto truncated=archive({{"a.gg","A",false}});truncated.resize(truncated.size()-12);check(!run(truncated).ok,"Truncated directory rejected");
        check(!run(archive({{"notes.txt","ignored",false}})).ok,"No game images reported");
        // A ZIP-wrapped IPA reaches its game importer intact; a direct IPA does too.
        app::GameEntry ipa;ipa.id="ipa";ipa.title="IPA";ipa.import=[](auto bytes){if(bytes.size()<2 || bytes[0]!='P')throw app::NotThisGame("Not IPA");return app::ImportResult{{{"asset",{1}}},"IPA"};};
        app::Library phone(root/"phone",{ipa});const auto inner=archive({{"Payload/App/data","content",true}});platform::write_file(root/"app.ipa",inner);
        check(phone.import(root/"app.ipa",{}).ok,"Direct IPA preserved");
        const auto outer=archive({{"App.ipa",std::string(inner.begin(),inner.end()),true}});platform::write_file(root/"outer.zip",outer);
        check(phone.import(root/"outer.zip",{}).ok,"Wrapped IPA preserved");
        std::cout<<"ZIP import checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
