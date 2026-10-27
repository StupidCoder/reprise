// Interaction checks against the real catalog, without importing or changing any
// user data. A fake module isolates menu navigation from game rules.
#include "app/catalog.hpp"
#include "marble_madness_amiga/game/mechanics_chapter.hpp"
#include "bruce_lee_c64/game/room_chapter.hpp"
#include "bruce_lee_c64/game/background_article.hpp"
#include "app/launcher.hpp"
#include "app/pause_menu.hpp"
#include "platform/type.hpp"
#include "ui/shelf.hpp"
#include "platform/resources.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <set>
#include <cstring>

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Module : app::GameModule {
    std::vector<app::GameOption> settings;
    int edits{};
    Module() {
        for (int i = 0; i < 12; ++i) settings.push_back({"Setting " + std::to_string(i), {"Off", "On"}, 0, "An option description."});
        settings.push_back({"Grouped setting", {"Off", "On"}, 0, "A grouped option.", "Cheats"});
    }
    double step_seconds() const override { return .02; }
    void tick(const app::GameInput&) override {}
    platform::Image picture() const override { return {1, 1}; }
    double pixel_aspect() const override { return 1; }
    std::vector<app::GameOption> options() const override { return settings; }
    void set_option(std::size_t i, int value) override { settings.at(i).value = value; ++edits; }
};
platform::Controls click(int x, int y) {
    platform::Controls in; in.click = true; in.mouse_x = x; in.mouse_y = y; return in;
}
int main(int argc, char** argv) {
    try {
        if(argc==4 && std::string(argv[1])=="--marble") {
            using namespace marble_madness::game;
            std::filesystem::create_directories(argv[3]);
            app::Library library(std::filesystem::path(argv[2]).parent_path().parent_path(),app::catalog());
            app::Launcher browser(library);browser.select(4);browser.open_details();browser.update(click(50,490),0);
            const auto list=browser.draw(0);
            platform::save_png(list,std::string(argv[3])+"/browser.png");
            for(int i=0;i<3;++i){browser.update(click(450,210+i*106),0);
                const auto page=browser.draw(0);MechanicsChapter expected(argv[2],i);const auto actual=expected.draw();
                check(std::memcmp(page.pixels.data(),actual.pixels.data(),page.pixels.size()*sizeof(platform::Color))==0,"Browser must open the selected Marble lesson");
                platform::Controls back;back.back=true;browser.update(back,1);
            }
            for(int topic=0;topic<3;++topic) {
                const bool enemies=topic==1,silly=topic==2;
                MechanicsChapter lesson(argv[2],topic);
                const std::string prefix=std::string(argv[3])+(silly?"/silly":enemies?"/steelie":"/physics");
                const auto start=lesson.race().marbles[0].x;
                lesson.update({},0);lesson.update({},1);
                check(lesson.steps()==0 && lesson.race().marbles[0].x==start,"Lesson starts paused");
                platform::Controls next;next.page_down=true;
                for(int page=0;page<((enemies||silly)?4:5);++page) {
                    const auto illustration=lesson.draw();
                    platform::save_png(illustration,prefix+"-"+std::to_string(page)+".png");
                    if(topic==0 && page==1) {
                        Probe probe;const auto& m=lesson.race().marbles[0];
                        probe.locate(lesson.race().terrain(),m.x,m.y,m.z);probe.ground_height();
                        check(probe.slope_x || probe.slope_y,"Triangle illustration must use a real sloped cell");
                    }
                    if(topic==0 && page==3) {
                        int removed=0;
                        for(int y=315;y<423;++y)for(int x=540;x<700;++x){const auto c=illustration.pixels[y*960+x];
                            removed+=c.r==ui::shelf::red.r && c.g==ui::shelf::red.g && c.b==ui::shelf::red.b;}
                        check(removed>0,"Occlusion illustration must demonstrate actual removed pixels");
                    }
                    if(page<((enemies||silly)?3:4))lesson.update(next,2+page);
                }
                lesson.update(click(175,90),10);
                check(lesson.steps()==1,"Step must advance exactly one world update");
                lesson.update(click(280,90),11);
                check(lesson.steps()==0,"Reset must discard experiment progress");
                if(enemies) {
                    const int expected[]={Marble::patrolling,Marble::hunting,Marble::chasing};
                    for(int i=0;i<3;++i) {
                        lesson.update(click(380+i*105,90),12+i);
                        const auto& race=lesson.race();
                        const auto& e=race.steelies[0].body.status?race.steelies[0]:race.steelies[1];
                        std::cout<<"Scenario "<<i<<" mode="<<int(e.body.mode)<<" position="<<(e.body.x>>16)<<","<<(e.body.y>>16)<<" player="<<(race.marbles[0].x>>16)<<","<<(race.marbles[0].y>>16)<<"\n";
                        check(e.body.sprite_flag!=0,"Scenario steelie must be visible in the course view");
                        check(e.body.status && e.body.mode==expected[i],"Imported scenario must enter intended decision branch");
                        platform::save_png(lesson.draw(),prefix+"-scenario-"+std::to_string(i)+".png");
                    }
                }
                if(silly) {
                    lesson.update(click(510,90),16);
                    const int score=lesson.race().marbles[0].score,time=lesson.race().marbles[0].time;
                    lesson.update(click(175,90),17);
                    check(lesson.race().marbles[0].score==score+500,"Silly catch must award 500 points through the real collision rule");
                    check(lesson.race().marbles[0].time==time+3,"Silly catch must add three clock seconds");
                    platform::save_png(lesson.draw(),prefix+"-caught.png");
                    lesson.update(click(660,90),18);
                    Probe probe;const auto& m=lesson.race().marbles[0];probe.locate(lesson.race().terrain(),m.x,m.y,m.z);probe.ground_height();
                    check(probe.slope_x || probe.slope_y,"Silly comparison must use a sloped cell");
                }
                lesson.update(click(65,90),20);
                platform::Controls steer;steer.right=true;
                for(int i=1;i<=80;++i)lesson.update(steer,20+i*.04);
                check(lesson.steps()>=79,"Live laboratory must advance at its fixed rate");
                platform::save_png(lesson.draw(),prefix+"-running.png");
            }
            std::cout<<"Marble lesson scenarios, pause, stepping and reset passed.\n";
            return 0;
        }
        if (argc == 3) {
            using namespace bruce_lee::game;
            const auto assets=bruce_lee::data::load_assets(argv[1]);
            auto decision=[&](bool yamo,int gap,int height,bool facing) {
                Actors actors;
                actors.slots[0].x=100; actors.slots[0].y=160;
                auto& a=actors.slots[1]; a.flags=yamo?0x81:0x41;
                a.x=std::uint8_t(100-gap); a.y=std::uint8_t(160+height);
                a.facing=facing?1:0; a.terrain.feet=terrain::ground;
                a.move=move::enemy_think; a.step=20; a.d1=36;
                ActorWorld world{assets,0,false,false,0,0,{},false,false}; actors.update(1,world);
                check(actors.decisions[1].sampled,"decision must be sampled by actual actor update");
                return actors.decisions[1].outcome;
            };
            check(decision(false,4,0,true)=="Reposition","Ninja too close");
            check(decision(false,5,0,true)=="Low strike","Ninja inner threshold");
            check(decision(false,10,0,true)=="Low strike","Ninja outer threshold");
            check(decision(false,11,0,true)=="Walk on","Ninja outside range");
            check(decision(true,11,0,true)=="Chop","Yamo chop limit");
            check(decision(true,12,0,true)=="Flying kick","Yamo kick begins");
            check(decision(true,18,0,true)=="Flying kick","Yamo kick limit");
            check(decision(true,19,0,true)=="Walk on","Yamo outside range");
            check(decision(false,8,16,true)=="Face target and wait","vertical gate");
            check(decision(false,33,0,false)=="Turn and walk","facing gate");
            bruce_lee::game::EnemyChapter chapter(argv[1]);
            auto initial=chapter.passes(); chapter.update({},0); chapter.update({},1);
            check(chapter.passes()==initial,"paused chapter must not advance");
            chapter.update(click(160,96),1.1);
            check(chapter.passes()==initial+1,"step advances exactly one pass");
            platform::save_png(chapter.draw(), std::string(argv[2])+"-initial.png");
            auto run=click(60,96); chapter.update(run,0);
            for(int i=1;i<=120;++i) chapter.update({},i*.06);
            platform::save_png(chapter.draw(), std::string(argv[2])+"-running.png");
            chapter.update(click(260,96),8);
            chapter.update(click(240,140),8.1);
            platform::save_png(chapter.draw(), std::string(argv[2])+"-close.png");
            chapter.update(click(500,96),8.2);
            for(int i=0;i<20;++i) chapter.update(click(160,96),9+i*.06);
            platform::save_png(chapter.draw(), std::string(argv[2])+"-yamo.png");
            chapter.update(click(450,140),11);
            platform::save_png(chapter.draw(), std::string(argv[2])+"-height.png");
            app::Library installed(std::filesystem::path(argv[1]).parent_path().parent_path(),app::catalog());
            app::Launcher launcher(installed); launcher.select(1); launcher.open_details();
            launcher.update(click(50,490),12);
            platform::save_png(launcher.draw(12),std::string(argv[2])+"-lessons.png");
            launcher.update(click(450,210),12);
            platform::save_png(launcher.draw(12),std::string(argv[2])+"-entry.png");
            platform::Controls back; back.back=true; launcher.update(back,13);
            platform::save_png(launcher.draw(13),std::string(argv[2])+"-exit.png");
            {
                // Remove asks first; Cancel, Esc and Enter (on Cancel) keep the files.
                app::Launcher remover(installed); remover.select(1); remover.open_details();
                const auto before=remover.draw(14);
                remover.update(click(50,535),14);
                check(installed.installed(1),"Remove must ask before deleting");
                const auto dialog=remover.draw(14);
                platform::save_png(dialog,std::string(argv[2])+"-remove-confirm.png");
                check(std::memcmp(before.pixels.data(),dialog.pixels.data(),before.pixels.size()*sizeof(platform::Color))!=0,"Remove must show the confirmation");
                platform::Controls enter; enter.accept=true; remover.update(enter,15);
                check(installed.installed(1),"Enter on Cancel must keep the files");
                remover.open_details(); remover.update(click(50,535),16); remover.update(back,16);
                check(installed.installed(1),"Esc must cancel the removal");
                remover.open_details(); remover.update(click(50,535),17); remover.update(click(500,375),17);
                check(installed.installed(1)&&std::memcmp(before.pixels.data(),remover.draw(17).pixels.data(),before.pixels.size()*sizeof(platform::Color))==0,"Cancel must close the confirmation");
            }
            bruce_lee::game::RoomChapter room(argv[1]);
            for(int i=0;i<10;++i)room.update(click(180,100),20+i*.1);
            check(room.stage()==0,"Missing prerequisite must hold the floor closed");
            platform::save_png(room.draw(),std::string(argv[2])+"-floor-closed.png");
            room.update(click(700,240),22); // Clear room 3.
            for(int i=0;i<8;++i)room.update(click(180,100),23+i*.1);
            check(room.stage()==4,"Four alternate-pass stages must complete");
            check(room.changed_cells()>0,"Opening must change real playfield cells");
            platform::save_png(room.draw(),std::string(argv[2])+"-floor-open.png");
            room.update(click(850,40),24);
            platform::save_png(room.draw(),std::string(argv[2])+"-article-top.png");
            platform::Controls scroll;scroll.page_down=true;room.update(scroll,24.1);room.update(scroll,24.2);
            platform::save_png(room.draw(),std::string(argv[2])+"-article-bottom.png");
            room.update(click(700,40),25);room.update(click(700,290),25.1); // Room 7 blocks opening.
            for(int i=0;i<10;++i)room.update(click(180,100),26+i*.1);
            check(room.stage()==0,"Clearing room 7 must block this opening");
            platform::Controls down;down.nav_down=true;platform::Controls enter;enter.accept=true;
            launcher.update(down,28);launcher.update(enter,28.1);
            platform::save_png(launcher.draw(28.2),std::string(argv[2])+"-floor-entry.png");
            const auto image=bruce_lee::data::load_game_image(argv[1]);
            for(const auto& r:assets.rooms){
                int encoded=1,expanded=0;
                for(const auto& run:r.runs){
                    check(run.first==expanded,"Map records must retain stream order");
                    expanded+=run.count;encoded+=run.literal?1+run.count:2;
                    if(!run.literal) for(int i=run.first;i<std::min(440,run.first+run.count);++i)
                        check(r.cells[std::size_t(i)]==run.repeated,"Repeat record must match decoded map");
                }
                check(encoded==r.encoded_bytes,"Measured stream size must include payload/control/end bytes");
                check(expanded==440+r.spill,"Decoded overflow must be accounted for");
                check(image.byte(std::uint16_t(r.map_address+r.encoded_bytes-1))==0x80,"Measured stream must end at actual end marker");
            }
            bruce_lee::game::BackgroundArticle article(argv[1]);
            platform::Controls next;next.page_down=true;
            for(int i=0;i<8;++i){
                platform::save_png(article.draw(),std::string(argv[2])+"-background-"+std::to_string(i)+".png");
                article.update(next,i);
            }
            launcher.update(back,30);launcher.update(down,31);launcher.update(enter,32);
            platform::save_png(launcher.draw(32),std::string(argv[2])+"-background-entry.png");
            std::cout << "Room storage and article checks passed; room 1: " << assets.rooms[0].encoded_bytes << " bytes.\n";
            return 0;
        }
        const auto root = std::filesystem::temp_directory_path() / ("joyport-shell-test-" + std::to_string(SDL_GetTicksNS()));
        app::Library library(root, app::catalog()); // read-only, deliberately nonexistent
        app::Launcher shell(library);
        using Request = app::Launcher::Request;
        platform::Controls right, left, down, up, enter, back, info, pgdown;
        right.nav_right = true; left.nav_left = true; down.nav_down = true; up.nav_up = true;
        enter.accept = true; back.back = true; info.info = true; pgdown.page_down = true;
        check(shell.update(enter, 1) == Request::choose_image, "Uninstalled supported game must request import");
        shell.update(right, 1); check(shell.selected() == 1, "Right must select Bruce Lee");
        shell.update(up, 1); check(shell.selected() == 1, "Top boundary must preserve column");
        shell.update(down, 1); check(shell.selected() == 3, "Down must select Mindwalker in chronological order");
        shell.update(down, 1); shell.update(down, 1); check(shell.selected() == 8, "Later releases must be reachable");
        shell.draw(1); // includes the last row's covers, with selection scrolled into view
        check(shell.update(enter, 1) == Request::choose_image, "Wings of Fury (importable) must request import");
        shell.draw(1); shell.update(back, 1);
        shell.select(0); shell.update(left, 1); shell.update(down, 1); shell.update(down, 1);
        shell.update(right, 1); check(shell.selected() == 4, "Keyboard filter must select first Amiga entry");
        shell.update(left, 1); shell.update(down, 1); shell.update(right, 1);
        check(shell.selected() == 6, "Planned filter must select Rock 'n Roll (the first planned game)");
        check(shell.update(enter, 1) == Request::none, "Planned game must not start or import");
        shell.draw(1); shell.update(back, 1);
        shell.update(click(40, 125), 1); // All games
        shell.select(0); shell.update(click(915, 80), 1);
        check(shell.selected() == 2, "Pointer paging must follow chronological order");
        shell.update(click(600, 400), 1); check(shell.selected() == 8, "Pointer hit testing must follow scrolling");
        shell.update(info, 1); shell.draw(1); shell.update(back, 1);
        for (std::size_t i = 0; i < library.games().size(); ++i) {
            shell.select(i); const auto image = shell.draw(1);
            check(image.width == 960 && image.height == 640, "All cards must render in logical canvas");
            const auto& game=library.games()[i];
            check(game.release_year >= 1983 && game.release_year <= 1993, "Every release needs its platform year");
            check(!game.description.empty() && !game.history.empty() && !game.history_sources.empty(), "Every game needs sourced editorial content");
            shell.open_details(); const auto about=shell.draw(1);
            shell.update(right,1); const auto assets=shell.draw(1);
            check(std::memcmp(about.pixels.data(), assets.pixels.data(), about.pixels.size()*sizeof(platform::Color)) != 0, "Right must open assets from About");
            shell.update(left,1);
            check(std::memcmp(shell.draw(1).pixels.data(), about.pixels.data(), about.pixels.size()*sizeof(platform::Color)) == 0, "Left from the file list must restore About");
            shell.update(back, 1);
        }
        shell.select(0); shell.open_details();
        check(shell.update(click(50, 440), 1) == Request::choose_image, "Detail import button must work");
        shell.update(back, 1);
        check(shell.update(click(820, 35), 1) == Request::choose_image, "Global import button must work");
        Module module;
        app::PauseMenu menu;
        using Action = app::PauseMenu::Action;
        menu.show(0); menu.show_options();
        menu.update(right, module, 1); check(module.settings[0].value == 1, "Option value must change");
        menu.update(pgdown, module, 1); menu.update(right, module, 1);
        check(module.settings[8].value == 1, "Options past first screen must remain reachable");
        // Selection 8 scrolls by one; the first drawn row is option 1.
        menu.update(click(200, 195), module, 1);
        check(module.settings[1].value == 1, "Scrolled option pointer mapping must match drawing");
        menu.update(info, module, 1); const int edits = module.edits;
        menu.update(right, module, 1); check(module.edits == edits, "Description view must not edit settings");
        menu.draw(module, "Fort Apocalypse", 1, "fort_apocalypse_c64");
        menu.update(back, module, 1);
        // Focus Back without activating it: this previously indexed options[-1].
        menu.show(0); menu.show_options(); menu.update(up, module, 1);
        menu.update({}, module, 1); menu.draw(module, "Fort Apocalypse", 1, "fort_apocalypse_c64");
        menu.update(up, module, 1); menu.update(enter, module, 1); // Cheats
        menu.update(right, module, 1); check(module.settings.back().value == 1, "Grouped option must work");
        menu.update(back, module, 1); menu.update(back, module, 1);
        check(menu.update(back, module, 1) == Action::resume, "Back must return from group, then options, then resume");
        menu.show(0);
        check(menu.update(click(720, 570), module, 1) == Action::resume, "Resume pointer button must work");
        Module empty; empty.settings.clear(); menu.show(0); menu.show_options();
        menu.update({}, empty, 1); menu.update(right, empty, 1); menu.update(enter, empty, 1);
        check(menu.update(back, empty, 1) == Action::resume, "Empty options must have a safe Back row");
        // Count the disconnected light contact centres, independently of the
        // port drawing's pin positions. A decorative extra contact fails this.
        platform::Image logo(64, 40);
        ui::fill(logo, ui::shelf::paper); ui::shelf::port(logo, 4, 4);
        std::set<int> contacts;
        for (int i = 0; i < int(logo.pixels.size()); ++i) {
            const auto c = logo.pixels[std::size_t(i)];
            if (c.r > 230 && c.g > 230 && c.b > 220) contacts.insert(i);
        }
        int components = 0;
        while (!contacts.empty()) {
            ++components;
            std::vector<int> todo{*contacts.begin()}; contacts.erase(todo.back());
            while (!todo.empty()) {
                const int at = todo.back(); todo.pop_back();
                for (const int offset : {-1, 1, -64, 64}) if (contacts.erase(at + offset)) todo.push_back(at + offset);
            }
        }
        check(components == 9, "The Joyport D-sub logo must have exactly nine distinct contacts");
        const auto font = platform::typeset("Joyport – café", 18, true);
        check(font.advance > 80 && !font.coverage.pixels.empty(), "Bundled font and UTF-8 must render");
        check(!std::filesystem::exists(root), "Tests must not create or change imported data");
        std::cout << "Shell navigation, all nine covers, details, portable fonts, scrolling options and description checks passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
