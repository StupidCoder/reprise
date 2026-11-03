// Export-time dependency discovery uses the same original-data model reader as the viewer.
#include "viewer.hpp"
#include <iostream>
int main(int argc,char** argv) {
    try {
        if(argc!=2) throw std::runtime_error("usage: beacon_asset_list EXTRACTED_GAME_DIR");
        for(const auto& path:beacon::reprise::texture_dependencies(std::filesystem::absolute(argv[1]))) std::cout<<path.generic_string()<<'\n';
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
