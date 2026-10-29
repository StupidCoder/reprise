// Interaction checks against the real catalog, without importing or changing any
// user data. A fake module isolates menu navigation from game rules.
#include "app/catalog.hpp"
#include "mindwalker_amiga/game/player_guide.hpp"
#include "marble_madness_amiga/game/mechanics_chapter.hpp"
#include "bruce_lee_c64/game/room_chapter.hpp"
#include "bruce_lee_c64/game/background_article.hpp"
#include "app/launcher.hpp"
#include "app/pause_menu.hpp"
#include "platform/type.hpp"
#include "ui/shelf.hpp"
#include "platform/resources.hpp"
#include <algorithm>
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <set>
#include <cstring>
#include <fstream>

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
        if (argc >= 2 && std::string(argv[1]) == "--motion") {
            const auto root = std::filesystem::temp_directory_path() / ("joyport-motion-" + std::to_string(SDL_GetTicksNS()));
            struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{root};
            app::Library library(root, app::catalog());
            app::Launcher shell(library);
            const auto different = [](const auto& a, const auto& b) {
                return std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size() * sizeof(platform::Color)) != 0;
            };
            platform::Controls enter, back, left, down;
            enter.accept = true; back.back = true; left.nav_left = true; down.nav_down = true;
            shell.select(0); shell.update({}, 0); shell.update({}, .2);
            const auto shelf = shell.draw(.2);
            platform::Controls hover;
            hover.ui_pointer = std::array<int, 2>{650, 160};
            shell.update(hover, .3); shell.update(hover, .5);
            check(shell.selected() == 0, "Mouse hover must not steal controller selection");
            check(different(shelf, shell.draw(.5)), "Hover lifts a card without activating it");
            shell.update({}, .7); shell.update({}, .9);
            shell.update(enter, 1);
            const auto opening = shell.draw(1.08);
            const auto details = shell.draw(1.3);
            check(different(opening, details), "Cover must move into details before settling");
            shell.update(back, 2);
            shell.update(enter, 2.04); // Reverse before the closing motion has finished.
            check(shell.update(enter, 2.05) == app::Launcher::Request::choose_image,
                  "Interrupted animation must not delay the active detail action");
            shell.draw(2.4);
            shell.update(click(80, 450), 3); shell.draw(3.3);
            shell.update(click(80, 435), 4);
            const auto doc_mid = shell.draw(4.06), doc_end = shell.draw(4.3);
            check(different(doc_mid, doc_end), "Documentation content must transition");
            for (int y = 106; y < 360; ++y) for (int x = 24; x < 264; ++x) {
                const auto at = std::size_t(y * 960 + x);
                check(std::memcmp(&doc_mid.pixels[at], &doc_end.pixels[at], sizeof(platform::Color)) == 0,
                      "Documentation keeps its cover and identity still");
            }
            shell.update(back, 5); shell.update(back, 5.3);
            shell.select(0);
            shell.update(down, 6); shell.update(down, 6.3);
            const auto scroll_start = shell.draw(6.3), scroll_mid = shell.draw(6.4), scroll_end = shell.draw(6.6);
            check(different(scroll_start, scroll_mid) && different(scroll_mid, scroll_end),
                  "Rows must travel continuously instead of flipping pages");
            shell.select(0); shell.update(down, 7); shell.update(down, 7.3);
            shell.update(click(650, 180), 7.3);
            check(shell.selected() == 1, "During scroll, a click selects the visible card, not the destination row");
            // Reduced Motion remains available as a saved preference, without a front-page control.
            std::filesystem::create_directories(root / "options");
            { std::ofstream out(root / "options" / "launcher.txt"); out << "Reduced motion = On\n"; }
            app::Launcher restored(library);
            check(restored.reduced_motion(), "Reduced Motion survives restarting the launcher");
            restored.select(0); restored.update(enter, 8);
            const auto still = restored.draw(8), later = restored.draw(9);
            check(!different(still, later), "Reduced Motion removes all transition and press animation");
            if (argc == 3) {
                std::filesystem::create_directories(argv[2]);
                platform::save_png(scroll_mid, std::string(argv[2]) + "/scroll-moving.png");
                platform::save_png(shelf, std::string(argv[2]) + "/shelf.png");
                platform::save_png(opening, std::string(argv[2]) + "/cover-moving.png");
                platform::save_png(details, std::string(argv[2]) + "/details.png");
                platform::save_png(doc_mid, std::string(argv[2]) + "/document-moving.png");
            }
            // Exercise import and save-preview animation using generated fixtures only.
            struct MotionGame : Module {
                bool changed{};
                void tick(const app::GameInput&) override { changed = true; }
                platform::Image picture() const override {
                    platform::Image image(160, 100);
                    ui::fill(image, changed ? ui::shelf::green : ui::shelf::cobalt);
                    return image;
                }
                std::vector<std::uint8_t> write_state() override { return {1}; }
            };
            auto entries = app::catalog();
            entries[0].import = [](std::span<const std::uint8_t>) {
                app::ImportResult result; result.release = "Generated motion fixture";
                result.files.push_back({"fixture.bin", {1, 2, 3}}); return result;
            };
            entries[0].previews = [](const std::filesystem::path&) {
                platform::Image a(160, 100), b(160, 100);
                ui::fill(a, ui::shelf::cobalt); ui::fill(b, ui::shelf::green);
                // A sound (a square wave) and a model (a cube) for the viewer's players.
                std::vector<std::int8_t> wave(4000);
                for (std::size_t i = 0; i < wave.size(); ++i) wave[i] = (i / 20) % 2 ? 100 : -100;
                auto cube = std::make_shared<ui::Model>();
                for (int i = 0; i < 8; ++i) cube->points.push_back({float(i & 1) * 2 - 1, float(i >> 1 & 1) * 2 - 1, float(i >> 2) * 2 - 1});
                for (const auto& f : std::vector<std::vector<int>>{{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}})
                    cube->faces.push_back({f, ui::shelf::cobalt});
                cube->lines.push_back({0, 7, ui::shelf::green});
                std::vector<app::Preview> result{{"First asset", std::move(a)}, {"Second asset", std::move(b)}};
                result.push_back({"A sound", platform::Image(160, 100), app::PreviewSound::signed8(wave, 8000)});
                result.push_back({"A model", platform::Image(1, 1), std::nullopt, cube});
                return result;
            };
            entries[0].start = [](const std::filesystem::path&) { return std::make_unique<MotionGame>(); };
            {
                auto recent_entries = entries;
                recent_entries[1].import = recent_entries[0].import;
                recent_entries[1].start = recent_entries[0].start;
                app::Library recent(root / "recency", recent_entries);
                const auto source = root / "recency-source.bin";
                { std::ofstream out(source); out << "synthetic import"; }
                check(recent.import(source, 0).ok && recent.import(source, 1).ok, "Recency fixtures import");
                auto first = app::with_quick_save(recent.games()[0], recent.content_directory(0));
                auto second = app::with_quick_save(recent.games()[1], recent.content_directory(1));
                check(first->save(), "Older fixture save succeeds");
                SDL_Delay(1100); // Save schema records whole UTC seconds.
                check(second->save(), "Newer fixture save succeeds");
                const auto old = app::save_preview(recent.content_directory(0), recent.games()[0].id);
                const auto latest = app::save_preview(recent.content_directory(1), recent.games()[1].id);
                check(old.saved_at < latest.saved_at, "Validated save previews expose chronological timestamps");
                app::Launcher shelf(recent);
                shelf.filter(1);
                check(shelf.selected() == 1, "Continue starts with newer save, not older release year");
                platform::Controls right; right.nav_right = true;
                shelf.update(right, 1);
                check(shelf.selected() == 0, "Older save follows the newest save");
            }
            app::Library fixture(root / "fixture", entries);
            app::Launcher importing(fixture);
            const auto source = root / "generated.bin";
            { std::ofstream out(source); out << "test fixture"; }
            importing.import({source}, 10);
            check(fixture.installed(0), "Fixture imports through the normal library path");
            check(different(importing.draw(10.05), importing.draw(11)), "Imported card settles and Ready light flashes once");
            auto game = app::with_quick_save(fixture.games()[0], fixture.content_directory(0));
            check(game->save(), "Fixture saves a screenshot");
            importing.open_details(); importing.draw(12); importing.draw(13);
            game->tick({}); check(game->save(), "Fixture updates its screenshot");
            importing.refresh_save();
            const auto old_save = importing.draw(14), new_save = importing.draw(14.3);
            check(different(old_save, new_save), "A changed save screenshot fades when returning to Details");
            check(!different(new_save, importing.draw(15)), "Screenshot settles without idle animation");
            importing.set_canvas(640, 960);
            check(importing.draw(15.5).width == 640, "Saved screenshot details fit portrait without landscape fading bounds");
            importing.open_assets(3);
            importing.draw(15.6);
            importing.update(click(300, 450), 15.7);
            std::vector<float> portrait_audio(200);
            importing.preview_audio(portrait_audio, 48000);
            check(std::any_of(portrait_audio.begin(), portrait_audio.end(), [](float v) { return v != 0; }), "Portrait waveform tap starts audio");
            importing.open_assets(4); importing.draw(15.8);
            importing.update(click(300, 450), 15.9);
            platform::Controls portrait_drag; portrait_drag.mouse_held = true;
            portrait_drag.ui_pointer = std::array<int,2>{420,500};
            importing.update(portrait_drag, 15.95);
            check(importing.draw(15.96).width == 640, "Portrait model drag renders safely");
            importing.set_canvas(960, 640);
            importing.open_assets(1); importing.draw(16);
            platform::Controls right; right.nav_right = true;
            importing.update(right, 17);
            const auto asset_mid = importing.draw(17.04);
            const auto backdrop = asset_mid.pixels[162 * 960 + 306];
            check(backdrop.r == 20 && backdrop.g == 30 && backdrop.b == 34,
                  "Asset transitions preserve the dark backdrop without a paper flash");
            if (argc == 3) platform::save_png(asset_mid, std::string(argv[2]) + "/asset-moving.png");
            {
                // Sounds play on a click and stop when the viewer turns the page or is left.
                const auto loud = [&] {
                    std::vector<float> stereo(800);
                    importing.preview_audio(stereo, 48000);
                    return std::any_of(stereo.begin(), stereo.end(), [](float v) { return v != 0; });
                };
                importing.open_assets(3); importing.update({}, 20);
                check(!loud(), "A sound is silent until clicked");
                importing.update(click(600, 340), 20.1);
                check(loud(), "Clicking a sound's preview plays it");
                importing.update(click(600, 340), 20.2);
                check(!loud() && importing.take_audio_cut(), "A second click stops it");
                importing.update(click(600, 340), 20.3);
                importing.update(right, 20.4);
                check(!loud() && importing.take_audio_cut(), "Turning the page stops the sound");
                importing.open_assets(3); importing.update({}, 20.5);
                importing.update(click(600, 340), 20.6);
                platform::Controls back; back.back = true;
                for (int i = 0; i < 3; ++i) importing.update(back, 20.7 + i * .1);
                check(!loud() && importing.take_audio_cut(), "Leaving the viewer stops the sound");

                // A model turns on its own, follows a drag and keeps turning after it.
                importing.open_details(); importing.open_assets(4); importing.update({}, 30);
                const auto a = importing.draw(30.5);
                importing.update({}, 31);
                const auto b = importing.draw(31.5);
                check(different(a, b), "A model turns slowly on its own");
                if (argc == 3) platform::save_png(b, std::string(argv[2]) + "/asset-model.png");
                importing.update(click(500, 340), 32);
                platform::Controls drag; drag.mouse_held = true;
                for (int i = 1; i <= 5; ++i) {
                    drag.ui_pointer = std::array<int, 2>{500 + i * 30, 340 + i * 6};
                    importing.update(drag, 32 + i * .016);
                }
                const auto dragged = importing.draw(33);
                importing.update({}, 33.1);
                const auto after = importing.draw(33.2);
                importing.update({}, 33.2);
                check(different(dragged, after), "A released drag carries the model on");
                check(different(b, dragged), "Dragging turns the model");
            }

            std::cout << "Motion, continuous scrolling, moving-card hit testing, flash-free assets and saved preference checks passed.\n";
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--details-preview") {
            app::Library library(argv[2], app::catalog());
            std::filesystem::create_directories(argv[3]);
            for (const int game : {1, 3, 4}) {
                app::Launcher shell(library);
                shell.select(std::size_t(game));
                shell.open_details();
                const auto prefix = std::string(argv[3]) + "/details-" + std::to_string(game);
                platform::save_png(shell.draw(0), prefix + ".png");
                if (game == 1 || game == 4) {
                    platform::Controls enter;
                    enter.accept = true;
                    check(shell.update(enter, 0) == app::Launcher::Request::continue_game,
                          "Saved game defaults to Continue");
                    check(shell.update(click(50, 390), 0) == app::Launcher::Request::play,
                          "Start remains a separate action");
                }
                shell.update(click(100, 450), 0);
                platform::save_png(shell.draw(0), prefix + "-docs.png");
                shell.open_history();
                platform::save_png(shell.draw(0), prefix + "-history.png");
                shell.open_lessons();
                platform::save_png(shell.draw(0), prefix + "-lessons.png");
                shell.open_assets();
                platform::save_png(shell.draw(0), prefix + "-assets.png");
            }
            std::cout << "Saved Continue, Start and documentation previews passed.\n";
            return 0;
        }
        if(argc==4 && std::string(argv[1])=="--marble") {
            using namespace marble_madness::game;
            std::filesystem::create_directories(argv[3]);
            app::Library library(std::filesystem::path(argv[2]).parent_path().parent_path(),app::catalog());
            app::Launcher browser(library);
            browser.select(4);
            browser.open_details();
            browser.open_lessons();
            const auto list=browser.draw(1);
            platform::save_png(list,std::string(argv[3])+"/browser.png");
            for(int i=0;i<3;++i){browser.update(click(450,210+i*106),0);
                const auto page=browser.draw(1);MechanicsChapter expected(argv[2],i);const auto actual=expected.draw();
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
        if (argc == 3 && std::string(argv[1]) != "--mindwalker-guide") {
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
            launcher.open_lessons();
            platform::save_png(launcher.draw(12),std::string(argv[2])+"-lessons.png");
            launcher.update(click(450,210),12);
            platform::save_png(launcher.draw(12),std::string(argv[2])+"-entry.png");
            platform::Controls back; back.back=true; launcher.update(back,13);
            platform::save_png(launcher.draw(13),std::string(argv[2])+"-exit.png");
            {
                // Remove asks first; Cancel, Esc and Enter (on Cancel) keep the files.
                app::Launcher remover(installed); remover.select(1); remover.open_details();
                const auto before=remover.draw(14);
                remover.update(click(50, 510), 14);
                check(installed.installed(1),"Remove must ask before deleting");
                const auto dialog=remover.draw(14);
                platform::save_png(dialog,std::string(argv[2])+"-remove-confirm.png");
                check(std::memcmp(before.pixels.data(),dialog.pixels.data(),before.pixels.size()*sizeof(platform::Color))!=0,"Remove must show the confirmation");
                platform::Controls enter; enter.accept=true; remover.update(enter,15);
                check(installed.installed(1),"Enter on Cancel must keep the files");
                remover.open_details();
                remover.update(click(50, 510), 16);
                remover.update(back, 16);
                check(installed.installed(1),"Esc must cancel the removal");
                remover.open_details();
                remover.update(click(50, 510), 17);
                remover.update(click(500, 375), 17);
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
        {
            // Marble Madness's random numbers: in range, and reduced once past
            // the range as the original does (random(3): 0 half the time).
            marble_madness::game::Random random;
            std::array<int,5> counts{};
            for(int i=0;i<40000;++i){const auto v=random.next(5);check(v<5,"Marble random(5) stays in range");++counts[v];}
            check(counts[0]>9000 && counts[0]<11000 && counts[4]>4000 && counts[4]<6000,"Marble random(5) keeps the original's odds");
            int zeros=0;
            for(int i=0;i<40000;++i) zeros+=random.next(3)==0;
            check(zeros>19000 && zeros<21000,"Marble random(3) gives 0 half the time");
            marble_madness::game::Random again;
            check(again.next(0x100)==marble_madness::game::Random().next(0x100),"Marble fixed seed repeats");
        }
        const auto root = std::filesystem::temp_directory_path() / ("joyport-shell-test-" + std::to_string(SDL_GetTicksNS()));
        app::Library library(root, app::catalog()); // read-only, deliberately nonexistent
        {
            app::Launcher guide(library);
            guide.select(3);
            guide.open_details();
            guide.open_lessons();
            const bool capture = argc == 3 && std::string(argv[1]) == "--mindwalker-guide";
            if (capture)
                std::filesystem::create_directories(argv[2]);
            for (int i = 0; i < 6; ++i) {
                platform::Controls open;
                open.accept = true;
                guide.update(open, 0);
                mindwalker::game::PlayerGuide expected(i);
                const auto actual = guide.draw(1), wanted = expected.draw();
                check(actual.pixels.size() == wanted.pixels.size() &&
                          std::memcmp(actual.pixels.data(), wanted.pixels.data(),
                                      actual.pixels.size() * sizeof(platform::Color)) == 0,
                      "Mind Walker guide must open each selected section without an import");
                if (capture)
                    platform::save_png(actual, std::string(argv[2]) + "/guide-" + std::to_string(i) + ".png");
                platform::Controls back;
                back.back = true;
                guide.update(back, 0);
                platform::Controls next;
                next.nav_down = true;
                guide.update(next, 0);
            }
            mindwalker::game::PlayerGuide pages;
            pages.update(click(50, 108 + 5 * 56 + 12), 0);
            check(pages.page() == 5, "Guide sidebar reaches last page");
            platform::Controls next;
            next.page_down = true;
            pages.update(next, 0);
            check(pages.page() == 5, "Guide page boundary clamps");
            pages.update(click(750, 590), 0);
            check(pages.page() == 4, "Guide previous button works");
        }
        app::Launcher shell(library);
        using Request = app::Launcher::Request;
        platform::Controls right, left, down, up, enter, back, info, pgdown;
        right.nav_right = true; left.nav_left = true; down.nav_down = true; up.nav_up = true;
        enter.accept = true; back.back = true; info.info = true; pgdown.page_down = true;
        const auto library_picture = shell.draw(2);
        check(shell.update(info, 1) == Request::none, "I must not activate a game");
        check(std::memcmp(library_picture.pixels.data(), shell.draw(2).pixels.data(),
                          library_picture.pixels.size() * sizeof(platform::Color)) == 0,
              "I no longer opens details from the library");
        check(shell.update(enter, 1) == Request::none, "Uninstalled game must open details before import");
        check(shell.update(enter, 1) == Request::choose_image, "Import starts only from details");
        shell.update(back, 1);
        shell.update(right, 1); check(shell.selected() == 1, "Right must select Bruce Lee");
        shell.update(up, 1); check(shell.selected() == 1, "Top boundary must preserve column");
        shell.update(down, 1); check(shell.selected() == 2, "Down moves one three-card row, preserving its column");
        shell.filter(3); check(library.games()[shell.selected()].collection == app::Collection::sonic, "Sonic filter selects Sonic");
        {
            app::Launcher sonic(library); sonic.filter(3);
            check(library.games()[sonic.selected()].id == "sonic_gg", "Sonic 1 comes first");
            sonic.update(right, 1);
            check(library.games()[sonic.selected()].id == "sonic2_gg", "Sonic 2 comes second");
            sonic.update(right, 2);
            check(library.games()[sonic.selected()].id == "triple_trouble_gg", "Triple Trouble comes third");
            sonic.update(down, 3);
            check(library.games()[sonic.selected()].id == "sonic_advance_gba", "Sonic Advance is fourth");
            sonic.set_canvas(640, 960);
            check(sonic.draw(4).width == 640, "Four Sonic covers render in portrait");
        }
        shell.draw(2);
        check(shell.update(enter, 2) == Request::none && shell.update(enter, 3) == Request::none, "Preview titles cannot launch or import");
        shell.update(back, 4);
        shell.filter(4); check(library.games()[shell.selected()].collection == app::Collection::layton, "Layton filter selects Layton");
        shell.draw(5);
        shell.filter(1);
        check(shell.update(enter, 6) == Request::none, "An empty Continue list cannot open stale selection");
        shell.draw(7);
        shell.update(right, 7); // Leave sidebar, entering an empty Continue list.
        shell.update(left, 7.2); shell.update(down, 7.3);
        check(shell.filter() == 2, "An empty collection must allow keyboard return to sidebar");
        shell.update(click(40, 255), 8);
        check(shell.filter() == 2, "Pointer selects Commodore collection");
        shell.update(left, 9); shell.update(down, 10); shell.update(right, 11);
        check(shell.filter() == 3, "Controller changes collection and enters its shelf");
        shell.filter(0); shell.select(0);
        shell.update(click(720, 170), 12);
        check(shell.selected() == 4, "Third card hit testing uses three-column layout");
        shell.draw(13); shell.update(back, 14);
        const auto spy = std::find_if(library.games().begin(), library.games().end(), [](const auto& g) { return g.id == "spy_mouse_ios"; });
        check(spy != library.games().end() && spy->collection == app::Collection::none, "Spy Mouse has no collection membership");
        {
            const auto& interceptor = library.games().at(9);
            check(interceptor.machine == app::Machine::amiga && interceptor.import && interceptor.previews &&
                      interceptor.start && interceptor.release_year == 1988,
                  "F/A-18 Interceptor imports, previews and starts free flight");
            const auto& spindizzy = library.games().at(10);
            check(spindizzy.machine == app::Machine::amiga && spindizzy.import && spindizzy.previews &&
                      spindizzy.start && spindizzy.release_year == 1990,
                  "Spindizzy Worlds imports, previews and starts");
        }
        for (std::size_t i = 0; i < library.games().size(); ++i) {
            shell.select(i); const auto image = shell.draw(2);
            check(image.width == 960 && image.height == 640, "All cards must render in logical canvas");
            const auto& game=library.games()[i];
            if (!game.integration_pending) check(game.release_year >= 1983 && game.release_year <= 2015, "Every release needs its platform year");
            if (!game.integration_pending) check(!game.description.empty() && !game.history.empty() && !game.history_sources.empty(), "Every game needs sourced editorial content");
            shell.open_details();
            const auto overview = shell.draw(2);
            check(shell.update(click(200, 390), 1) == Request::none,
                  "Continue without a save must do nothing");
            shell.update(click(80, 450), 1);
            const auto docs = shell.draw(2);
            check(std::memcmp(overview.pixels.data(), docs.pixels.data(),
                              overview.pixels.size() * sizeof(platform::Color)) != 0,
                  "Documentation opens assets directly");
            shell.update(click(80, 435), 1);
            const auto history = shell.draw(2);
            check(std::memcmp(history.pixels.data(), docs.pixels.data(),
                              docs.pixels.size() * sizeof(platform::Color)) != 0,
                  "Documentation sidebar opens development notes");
            for (int y = 106; y < 360; ++y)
                for (int x = 24; x < 264; ++x) {
                    const auto at = std::size_t(y * overview.width + x);
                    check(std::memcmp(&overview.pixels[at], &history.pixels[at], sizeof(platform::Color)) == 0,
                          "Details and documentation share the same cover and identity layout");
                }
            shell.update(back, 1);
            check(std::memcmp(shell.draw(2).pixels.data(), overview.pixels.data(),
                              overview.pixels.size() * sizeof(platform::Color)) != 0,
                  "Returning to details focuses Documentation");
            shell.update(back, 1);
        }
        shell.select(0); shell.open_details();
        check(shell.update(click(50, 390), 1) == Request::choose_image, "Detail import button must work");
        shell.update(back, 1);
        check(shell.update(click(820, 35), 1) == Request::choose_image, "Global import button must work");
        Module module;
        // Portrait reflow preserves selection and uses matching input bounds.
        {
            app::Launcher portrait(library);
            portrait.set_canvas(1080, 1920);
            auto image = portrait.draw(0);
            check(image.width == 640 && image.height == 1137, "Portrait uses a tall logical canvas");
            portrait.update(click(240, 140), 1); // Layton collection in second tab row
            check(portrait.filter() == 4, "Portrait collection buttons use their drawn positions");
            portrait.update(click(100, 270), 2);
            check(portrait.draw(3).width == 640, "Portrait card tap opens portrait details");
            portrait.update(click(500, 280), 3.1);
            check(portrait.draw(3.2).width == 640, "Documentation remains portrait");
            portrait.update(click(300, 280), 3.3);
            check(portrait.draw(3.4).height == 1137, "Development notes use the reading pane");
            portrait.update(back, 3.5);
            portrait.update(back, 4);
            check(portrait.draw(5).width == 640, "Back restores portrait shelf without mismatched transition");
            auto selected = portrait.selected();
            portrait.set_canvas(1920, 1080);
            check(portrait.selected() == selected && portrait.draw(6).width == 960, "Rotation preserves selection");
            portrait.set_canvas(1080, 1920);
            portrait.filter(0);
            platform::Controls press = click(100, 400);
            press.mouse_held = true; press.ui_pointer = std::array<int,2>{100,400};
            portrait.update(press, 7);
            platform::Controls drag;
            drag.mouse_held = true; drag.ui_pointer = std::array<int,2>{100,180};
            portrait.update(drag, 7.1);
            drag.mouse_held = false;
            portrait.update(drag, 7.2);
            check(portrait.draw(8).width == 640, "Swiping a card must not open details");
            platform::save_png(portrait.draw(8), std::filesystem::temp_directory_path() / "reprise-portrait-check.png");
        }
        // The same picture encoded at two native resolutions must acquire the
        // same video-space faults. This catches game-pixel noise and smear.
        {
            platform::Image low(32, 24), doubled(64, 48);
            for (int y = 0; y < 24; ++y) for (int x = 0; x < 32; ++x) {
                const platform::Color c{std::uint8_t(x * 7), std::uint8_t(y * 9), std::uint8_t((x+y)%2 * 200), 255};
                low.put(x,y,c);
                for (int dy=0;dy<2;++dy) for (int dx=0;dx<2;++dx) doubled.put(x*2+dx,y*2+dy,c);
            }
            for (int level = 1; level < ui::vhs_levels; ++level) {
                const auto a = ui::vhs(low, 241, level), b = ui::vhs(doubled, 241, level);
                check(a.width == 720 && a.height == 480, "VHS uses a fixed virtual video raster");
                check(a.pixels.size() == b.pixels.size() && std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size()*sizeof(platform::Color)) == 0,
                      "VHS noise, scanlines and chroma must not depend on native resolution");
            }
            check(ui::vhs(low, 1, 0).width == 32, "Filter off retains native raster");
            app::ShownPicture shown{platform::Image(720,480), 320,200};
            check(std::abs(shown.display_pixel_aspect(1.2)*720/480 - 1.2*320/200) < .000001,
                  "Virtual raster retains the original displayed aspect");
            check(shown.to_picture({360,240}) == std::array<int,2>{160,100}, "Virtual raster retains pointer mapping");
        }
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
        std::cout << "Shell navigation, all catalog covers, details, portable fonts, scrolling options and description checks passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
