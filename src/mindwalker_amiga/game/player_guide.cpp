#include "mindwalker_amiga/game/player_guide.hpp"
#include "ui/shelf.hpp"
#include <algorithm>
#include <array>
#include <string>

namespace mindwalker::game {
namespace {
namespace s = ui::shelf;
struct Page {
    const char* name;
    const char* goal;
    std::array<const char*, 3> steps;
    const char* heading;
    const char* explanation;
    const char* tip;
};
// Evidence and limits: ../PLAYER_GUIDE.md. These are explanations of recovered
// mechanics, not original manual text; illustrations are labelled schematics.
constexpr Page pages[] = {
    {"The whole journey", "Restore the 42-piece picture, seven pieces at a time.",
     {"LANDSCAPE\nConnect a trail to a beacon and take its open ramp.", "TUNNEL + ARENA\nCatch an opening, retrieve the target, return to the exit.", "PSYCHE\nPlace seven picture tiles. Repeat until all 42 are home."},
     "Four sequences, one repeating journey",
     "An arena success unlocks the next seven picture tiles. Finishing that puzzle returns you to the landscape with the way on closed again. Six successful rounds complete the picture. The world seed determines the landscape, maze and tile shuffle.",
     "A failed tunnel or an arena exit without the target returns you to the landscape. It is a retry, not a completed round."},
    {"1 / The landscape", "Build a connected trail to a beacon, then use the ramp it opens.",
     {"CONNECT\nExtend the marked trail on terrain suited to your form.", "ACTIVATE\nReach the beacon from a neighbouring trail cell.", "LEAVE\nStep onto a ramp in the newly marked block."},
     "Why the screen sometimes refuses to scroll",
     "From the second round, kite-like objects can hold you in the current view. Touch EVERY one in that view to release it; just reaching the edge does not work. These are separate from the beacon that opens the way to the tunnel.",
     "Move with the directional controls; fire zaps creatures. Linger on a pyramid to cycle through the four forms. The compass turns the view; the emblem opens the map."},
    {"When the trail stops", "Check the form, the connection and the local map boundary.",
     {"RIGHT SURFACE\nOnly the terrain matching your current form gains trail marks.", "CONNECTED\nA neighbouring marked cell or the map centre must connect it.", "LOCAL WINDOW\nNew marks must fit inside a 64 x 64 cell region."},
     "A spatial limit, not a length allowance",
     "The trail region runs from 32 cells before its centre to 31 after it on each world axis. Walking farther can still be possible, but new trail marks stop there. Touching a re-centring landmark moves this region and clears the old trail. It is not a fixed number of steps from a crystal.",
     "Backtrack to the last mark. Check your form and surface, then the map. Special cells do not become ordinary trail. Being able to walk onto a cell does not mean it can extend the trail."},
    {"2 / The tunnel", "Steer into a growing rectangular opening to reach the arena.",
     {"STEER\nDirectional controls move the view and the openings.", "CATCH\nKeep an opening across the middle as it grows towards you.", "RETRY\nIf the tunnel runs out, you return to the landscape."},
     "The rectangles are opportunities, not enemies",
     "The tunnel speeds up as it runs. A catch is registered when an opening stretches across the central horizontal span of the screen; it is not a collision with a tiny point in the centre. A successful catch awards points and takes you into the maze.",
     "Use the directional controls rather than clicking the scenery. A mouse press can briefly freeze the action; the P box is the game's pause control."},
    {"3 / The arena", "Find the target, then return to the central exit carrying it.",
     {"SEARCH\nCross room edges. Use the temperature gauge to home in.", "RETRIEVE\nTouch the target: +500 points and a life, up to six.", "RETURN\nThe gauge now points you back towards the exit."},
     "Walls are passable, but not always safe",
     "You can walk through walls. Any wall is lethal during the active yellow pulse, so cross between pulses. Creatures drain strength on contact. Hold fire and steer left or right to rotate your aim; the beam is bent by the wind. Release fire to move again.",
     "Taking the target walls in the exit area. Time your crossing, then touch the centre of the exit. Leaving empty-handed or dying sends you back without new tiles, unless it was your last life."},
    {"4 / The psyche", "Put the seven offered tiles into their correct places in the moving picture.",
     {"PICK UP\nClick a tile in a slot or in the picture grid.", "PLACE\nClick an empty position; misplaced tiles can be picked up again.", "COMPLETE\nAll seven home: return to the landscape for the next round."},
     "The picture moves; the tile homes do not",
     "The full grid has seven columns and six rows. Each round adds seven tiles while earlier correct tiles stay in place. Match the moving patterns across their edges. At 42 correct tiles the final picture and completion bonus appear.",
     "A hint button sends a line towards its slot. It must reach the colour bar while that slot is red to send the tile home. A successful hint costs 1,000 self-development bonus points."},
};
bool hit(const platform::Controls& in, int x, int y, int w, int h) {
    return in.click && in.mouse_x >= x && in.mouse_x < x+w && in.mouse_y >= y && in.mouse_y < y+h;
}
}
PlayerGuide::PlayerGuide(int page) : page_(std::clamp(page,0,5)) {}
void PlayerGuide::update(const platform::Controls& in, double) {
    page_ = std::clamp(page_ + int(in.nav_right || in.page_down) - int(in.nav_left || in.page_up) + in.wheel,0,5);
    for(int i=0;i<6;++i) if(hit(in,24,108+i*56,218,46)) page_=i;
    if(hit(in,716,572,100,36)) page_=std::max(0,page_-1);
    if(hit(in,828,572,108,36)) page_=std::min(5,page_+1);
}
platform::Image PlayerGuide::draw() const {
    platform::Image im(960,640); ui::fill(im,s::paper);
    const auto& p=pages[page_];
    s::button(im,"<  Guide",24,20,104,34);
    s::text(im,"Mind Walker / How to play",150,24,24,s::ink,true);
    s::text(im,"THE PLAYER'S COMPANION",24,79,12,s::cobalt,true);
    for(int i=0;i<6;++i) s::button(im,pages[i].name,24,108+i*56,218,46,i==page_);
    s::wrap(im,"Based on the reconstructed rules. Choose any sequence here, or read through in order.",24,466,210,5,14,s::muted);
    ui::rounded(im,264,78,672,476,10,s::card);
    s::text(im,p.name,286,96,27,s::ink,true);
    s::wrap(im,p.goal,286,134,620,2,19,s::ink);
    for(int i=0;i<3;++i) {
        const int x=286+i*210;
        ui::rounded(im,x,196,198,130,6,s::pale);
        const std::string step=p.steps[std::size_t(i)];const auto split=step.find('\n');
        s::text(im,step.substr(0,split),x+12,210,14,s::cobalt,true);
        s::wrap(im,step.substr(split+1),x+12,238,174,4,16,s::ink);
    }
    s::text(im,p.heading,286,347,21,s::ink,true);
    s::wrap(im,p.explanation,286,383,620,7,18,s::ink);
    s::wrap(im,p.tip,264,566,434,4,14,s::muted);
    s::button(im,"< Previous",716,572,100,36);
    s::button(im,"Next >",828,572,108,36);
    s::right(im,std::to_string(page_+1)+" / 6   |   Left / Right: pages   Esc: guide",936,616,11,s::muted);
    return im;
}
}
