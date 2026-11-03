#include "astro_bot_ps4/reprise/import.hpp"
#include "app/library.hpp"
#include "platform/sha256.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <map>
#include <algorithm>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::map<std::string,std::string> inventory(const std::filesystem::path& root) {
    std::map<std::string,std::string> result;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root))if(entry.is_regular_file()) {
        const auto path=std::filesystem::relative(entry.path(),root).generic_string();
        if(!path.starts_with("data/") && path!="sce_sys/param.sfo")continue;
        std::ifstream in(entry.path(),std::ios::binary);std::vector<std::uint8_t> bytes(std::filesystem::file_size(entry.path()));
        in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());check(bool(in),"Cannot read comparison asset");
        result[path]=platform::sha256(bytes);
    }
    return result;
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::runtime_error("usage: beacon_package_check PACKAGE REFERENCE_CONTENT NEW_LIBRARY");
        app::GameEntry entry;entry.id="astro_bot_ps4";entry.title="Astro Bot";entry.import_into=beacon::reprise::import_assets;entry.largest_image=8ull<<30;
        app::Library library(argv[3],{entry});check(!library.installed(0),"Use a fresh test library");
        const auto start=std::chrono::steady_clock::now();
        const auto imported=library.import(argv[1],0);check(imported.ok,imported.message.c_str());
        const auto expected=inventory(argv[2]),actual=inventory(library.content_directory(0));
        check(!expected.empty() && actual==expected,"Package assets differ from validated bundle import");
        app::Library reopened(argv[3],{entry});check(reopened.installed(0) && reopened.verify(0),"Fresh process inventory invalid");
        std::cout<<actual.size()<<" assets match the existing extraction byte-for-byte; "
                 <<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" seconds including comparison\n";
        // Valid content ID and full advertised size, but damaged body: checksum rejects
        // before nested parsing and Library preserves the previous installation.
        const auto damaged=std::filesystem::path(argv[3])/"damaged.pkg";
        {
            std::ifstream original(argv[1],std::ios::binary);std::array<char,4096> header{};original.read(header.data(),header.size());check(bool(original),"Cannot read package header");
            std::ofstream out(damaged,std::ios::binary);out.write(header.data(),header.size());
            out.seekp(std::filesystem::file_size(argv[1])-1);out.put(0);out.close();check(bool(out),"Cannot create sparse damaged fixture");
        }
        const auto bad=library.import(damaged,0);std::filesystem::remove(damaged);
        check(!bad.ok && bad.message.find("checksum")!=std::string::npos,"Damaged package was not rejected by checksum");
        check(library.installed(0) && library.verify(0),"Failed import changed installed assets");
        std::cout<<"Damaged full-size package rejected; existing installation preserved\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
