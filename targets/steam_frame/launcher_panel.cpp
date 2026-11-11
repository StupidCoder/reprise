#include "launcher_panel.hpp"
#include "app/catalog.hpp"
#include <algorithm>
#include <stdexcept>
#include <cstdlib>
#include <cstdio>
#include <set>
#include "app/import_screen.hpp"

LauncherPanel::LauncherPanel(const std::filesystem::path& data)
    : library_(data, app::catalog(), app::Library::StartupValidation::defer_large_imports), launcher_(library_) {
    launcher_.set_canvas(960,640);
}
void LauncherPanel::select(const std::string& id) {
    const auto& games=library_.games();
    const auto found=std::find_if(games.begin(),games.end(),[&](const auto& game){return game.id==id;});
    if(found==games.end()) throw std::runtime_error("Catalog changed; regenerate the XR card atlas");
    launcher_.select(size_t(found-games.begin()));
    launcher_.open_details();
}
app::Launcher::Request LauncherPanel::update(const platform::Controls& controls,double now,bool focused) {
    if(gameplay_.active()) {
        auto input=controls; input.back=false; // B is a game button; View/Menu pause.
        gameplay_.update(input,now,focused);
        if(!gameplay_.active()) {
            launcher_.refresh_save();
            if(auto message=gameplay_.take_message(); !message.empty()) launcher_.show_error(message,now);
        }
        return app::Launcher::Request::none;
    }
    if(auto message=gameplay_.take_message(); !message.empty()) { launcher_.refresh_save(); launcher_.show_error(message,now); }
    if(!focused) return app::Launcher::Request::none;
    if(import_work_.valid()) {
        if(import_work_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            const auto index=launcher_.selected();
            try {
                const auto result=import_work_.get();
                library_=app::Library(library_.data_directory(),app::catalog(),app::Library::StartupValidation::defer_large_imports);
                launcher_.select(index); launcher_.open_details();
                launcher_.show_message(result.message,result.ok,now);
                std::printf("VR import result: %s\n",result.message.c_str());
                for(const auto& detail:result.details) std::printf("%s\n",detail.c_str());
            } catch(const std::exception& e) { launcher_.show_error(e.what(),now); }
        }
        return app::Launcher::Request::none;
    }
    if(choosing_) {
        if(controls.back || (controls.click && controls.mouse_y<85)) { choosing_=false; return app::Launcher::Request::none; }
        first_=std::clamp(first_+controls.wheel,0,std::max(0,int(choices_.size())-7));
        if(controls.click && controls.mouse_x>=36 && controls.mouse_x<924 && controls.mouse_y>=124 && controls.mouse_y<544) {
            const int index=first_+(controls.mouse_y-124)/60;
            if(index<int(choices_.size())) begin_import(choices_[index]);
        }
        return app::Launcher::Request::none;
    }
    const auto request=launcher_.update(controls,now);
    if(request==app::Launcher::Request::choose_image) { choose_import(now); return app::Launcher::Request::none; }
    if(request==app::Launcher::Request::play || request==app::Launcher::Request::continue_game) {
        gameplay_.start(selected(),content(),request==app::Launcher::Request::continue_game);
        return app::Launcher::Request::none;
    }
    return request;
}
platform::Image LauncherPanel::draw(double now,bool omit_cover) const {
    if(gameplay_.active()) return gameplay_.draw(now);
    if(import_work_.valid()) {
        std::lock_guard lock(progress_mutex_);
        return app::import_screen(960,640,phase_,fraction_,now,0,1);
    }
    if(choosing_) {
        namespace s=ui::shelf;
        platform::Image image(960,640); ui::fill(image,s::paper);
        s::text(image,"< Back",36,30,22,s::ink,true);
        s::text(image,"Import from Downloads",210,30,26,s::ink,true);
        s::text(image,"Choose an image or ZIP. Original files stay in Downloads.",36,84,18,s::muted);
        for(int row=0;row<7 && first_+row<int(choices_.size());++row) {
            const int y=124+row*60;
            ui::rounded(image,36,y,888,52,7,s::card);
            s::clipped(image,choices_[first_+row].filename().string(),52,y+16,852,20);
        }
        s::text(image,"Point and trigger to import. Thumbstick scrolls. Back cancels selection.",36,584,17,s::muted);
        return image;
    }
    return launcher_.draw_surface(now,omit_cover);
}

void LauncherPanel::choose_import(double now) {
    choices_.clear(); first_=0;
    try {
        const char* home=std::getenv("HOME");
        if(!home) throw std::runtime_error("Cannot locate Downloads");
        const std::set<std::string> allowed{".zip",".tap",".t64",".d64",".g64",".prg",".adf",".ipa",".nds",".cci",".3ds",".gg",".gba"};
        for(const auto& file:std::filesystem::directory_iterator(std::filesystem::path(home)/"Downloads")) {
            auto ext=file.path().extension().string();
            std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(file.is_regular_file() && allowed.contains(ext)) choices_.push_back(file.path());
        }
        std::sort(choices_.begin(),choices_.end());
        if(choices_.empty()) throw std::runtime_error("No supported images or ZIPs in Downloads.");
        choosing_=true;
    } catch(const std::exception& e) { launcher_.show_error(e.what(),now); }
}
void LauncherPanel::begin_import(const std::filesystem::path& path) {
    choosing_=false;
    { std::lock_guard lock(progress_mutex_); phase_="Opening "+path.filename().string(); fraction_=-1; }
    const auto data=library_.data_directory();
    const auto preferred=launcher_.selected();
    std::printf("VR importing %s\n",path.filename().c_str());
    import_work_=std::async(std::launch::async,[this,data,path,preferred] {
        app::Library worker(data,app::catalog(),app::Library::StartupValidation::defer_large_imports);
        return worker.import(path,preferred,[this](const std::string& phase,double fraction) {
            std::lock_guard lock(progress_mutex_); phase_=phase; fraction_=fraction;
        });
    });
}
