#include "system/script.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace picarat {
ScriptReader::ScriptReader(std::span<const std::uint8_t> file) {
    auto length = read32(file, 0);
    if (length != file.size() - 4) {
        throw std::runtime_error("GDS body size mismatch");
    }
    body_.assign(file.begin() + 4, file.end());
}

// ov2:0207ccd8 (+14458). Reading a token updates only the corresponding slot;
// integer, float, string and last command are distinct persistent values.
ScriptToken ScriptReader::next() {
    ScriptToken token;
    token.offset = cursor_;
    token.tag = read16(body_, cursor_);
    std::size_t length = 0, payload = cursor_ + 2;
    switch (token.tag) {
    case 0:
        length = 2;
        break; // Command - Reads the 16-bit opcode.
    case 1:
        length = 4;
        break; // Integer - Reads a signed 32-bit argument.
    case 2:
        length = 4;
        break; // Float - Reads binary32 bits without conversion.
    case 3:
        length = read16(body_, payload);
        payload += 2;
        break; // String - Reads a length-prefixed byte string.
    case 4:
        length = read16(body_, payload);
        payload += 2;
        break; // AlternateString - Reads the second string value slot.
    case 5:
        break; // Tag5 - Carries no payload; no reader uses it.
    case 6:
        length = 4;
        break; // Jump - Reads the jump-target slot (first adventure's flow control).
    case 7:
        length = 4;
        break; // Jump7 - Reads the same slot; no reader of either game uses it.
    case 8:
        break; // Not - Carries no payload; negates conditions (first adventure).
    case 9:
        break; // And - Carries no payload; "and" in condition chains (first adventure).
    case 10:
        break; // Or - Carries no payload; "or" in condition chains (first adventure).
    case 11:
        break; // Break - Carries no payload; leaves a loop (first adventure).
    case 12:
        break; // End - Terminates the executable token stream.
    default:
        throw std::runtime_error(
            "unknown GDS token tag"); // UnknownTag - Rejects an unsupported token encoding.
    }
    if (payload > body_.size() || length > body_.size() - payload) {
        throw std::runtime_error("truncated GDS token");
    }
    token.payload.assign(body_.begin() + payload, body_.begin() + payload + length);
    token.next = payload + length;
    switch (token.tag) {
    case 0:
        values_.command = read16(token.payload, 0);
        break; // Command - Reads the 16-bit opcode.
    case 1:
        values_.integerBits = read32(token.payload, 0);
        break; // Integer - Reads a signed 32-bit argument.
    case 2:
        values_.floatBits = read32(token.payload, 0);
        break; // Float - Reads binary32 bits without conversion.
    case 3:
        values_.string = token.payload;
        break; // String - Reads a length-prefixed byte string.
    case 4:
        values_.alternateString = token.payload;
        break; // AlternateString - Reads the second string value slot.
    case 6:
        values_.addressBits = read32(token.payload, 0);
        break; // Jump - Stores the jump target.
    case 7:
        values_.addressBits = read32(token.payload, 0);
        break; // Jump7 - Stores into the same slot (ARM9 02017e7c, ov2 0207ccd8).
    default:
        break; // NoValue - Leaves persistent argument slots unchanged.
    }
    cursor_ = token.next;
    if (onToken) {
        onToken(token, values_);
    }
    return token;
}

void ScriptReader::seek(std::size_t offset) {
    if (offset > body_.size()) {
        throw std::runtime_error("GDS jump outside the script");
    }
    cursor_ = offset;
}

ScriptToken ScriptArguments::argument(std::uint16_t expectedTag) {
    auto token = reader_.next();
    if (token.tag != expectedTag) {
        throw std::runtime_error("unexpected GDS argument type");
    }
    return token;
}

std::int32_t ScriptArguments::integer() {
    return std::bit_cast<std::int32_t>(read32(argument(1).payload, 0));
}

std::uint32_t ScriptArguments::floatingBits() {
    return read32(argument(2).payload, 0);
}

float ScriptArguments::real() {
    return std::bit_cast<float>(floatingBits());
}

std::string ScriptArguments::string() {
    auto bytes = argument(3).payload;
    auto nul = std::find(bytes.begin(), bytes.end(), 0);
    if (nul == bytes.end()) {
        throw std::runtime_error("unterminated GDS string argument");
    }
    return std::string(bytes.begin(), nul);
}

namespace {
// ov2 02078ab4..02078aec (second adventure), ARM9 02016258 (first): integers
// use nonzero; strings compare with "true" ignoring ASCII case (0206b234,
// 0200c08c); other token types are false.
bool truthOf(const ScriptToken& t) {
    if (t.tag == 1) {
        return read32(t.payload, 0) != 0;
    }
    if (t.tag != 3) {
        return false;
    }
    const auto end = std::find(t.payload.begin(), t.payload.end(), 0);
    if (end == t.payload.end()) {
        throw std::runtime_error("unterminated GDS truth string");
    }
    if (end - t.payload.begin() != 4) {
        return false;
    }
    constexpr std::string_view expected = "true";
    for (unsigned i = 0; i < 4; ++i) {
        auto c = t.payload[i];
        if (c >= 'A' && c <= 'Z') {
            c += 32;
        }
        if (c != expected[i]) {
            return false;
        }
    }
    return true;
}
} // namespace

bool ScriptArguments::truth() {
    return truthOf(reader_.next());
}

bool CommandArguments::truth() {
    return take<bool>();
}

std::int32_t CommandArguments::integer() {
    return take<std::int32_t>();
}

std::uint32_t CommandArguments::floatingBits() {
    return take<FloatBits>().bits;
}

float CommandArguments::real() {
    return std::bit_cast<float>(floatingBits());
}

std::string CommandArguments::string() {
    return take<std::string>();
}

void ScriptContext::bind(std::uint16_t command, Handler handler) {
    if (command == 0 || command >= handlers_.size() || !handler || handlers_[command] ||
        conditions_[command]) {
        throw std::runtime_error("invalid or duplicate GDS context handler");
    }
    handlers_[command] = std::move(handler);
}

void ScriptContext::rebind(std::uint16_t command, Handler handler) {
    if (command == 0 || command >= handlers_.size() || !handler || conditions_[command]) {
        throw std::runtime_error("invalid GDS context handler");
    }
    handlers_[command] = std::move(handler);
}

void ScriptContext::bindCondition(std::uint16_t command, Condition condition) {
    if (command == 0 || command >= conditions_.size() || !condition || handlers_[command] ||
        conditions_[command]) {
        throw std::runtime_error("invalid or duplicate GDS context condition");
    }
    conditions_[command] = std::move(condition);
}

const ScriptCommandInfo* ScriptDialect::find(std::uint16_t command) const {
    for (const auto& info : commands) {
        if (info.command == command) {
            return &info;
        }
    }
    return nullptr;
}

namespace {
// The second adventure's script commands this reconstruction understands
// (see ScriptCommandInfo). Each scene binds handlers for the commands it
// supports (ScriptContext); a script using any other command is rejected.
constexpr ScriptCommandInfo kCommands[] = {
    {0x02, "", "FadeIn", "Fades both screens in."},
    {0x03, "", "FadeOut", "Fades both screens out."},
    {0x04, "i", "Dialogue", "Plays an event dialogue record."},
    {0x05, "i", "SetPlace", "Moves the story to a place and copies its p_%u.txt label (02077da8)."},
    {0x06, "s", "RequestScene", "Selects the next scene kind."},
    {0x07, "s", "FollowingScene", "Selects the subsequent scene kind."},
    {0x08, "i", "SelectMovie", "Sets the movie ID."},
    {0x09, "i", "SelectEvent", "Sets the next event ID."},
    {0x0b, "i", "SelectPuzzle", "Sets the puzzle ID."},
    {0x0c, "iii", "DrawingColor", "Sets the drawing palette from three color components."},
    {0x0d, "i", "LineLimit", "Maximum simultaneous lines (ov37 +0x34)."},
    {0x10, "ii", "GridOrigin", "Lower-screen origin of the line grid."},
    {0x11, "ii", "GridSize", "Line grid columns and rows."},
    {0x12, "i", "GridCell", "Pixel size of one line-grid cell."},
    {0x14, "iisbi", "SelectionChoice", "Adds a sprite choice, required selection and sound ID."},
    {0x18, "iifb", "CircleRegion",
     "Adds a center, float parameter and answer flag to the active region group."},
    {0x19, "ii", "TraceSeed", "Flood-fill start point (ov54)."},
    {0x1a, "ii", "TraceInside", "Point that must be enclosed (ov34/ov54)."},
    {0x1b, "ii", "TraceOutside", "Point that must stay outside (ov34/ov54)."},
    {0x21, "si", "MainBackground", "Loads a lower-screen background layer."},
    {0x22, "si", "SubBackground", "Loads an upper-screen background layer."},
    {0x24, "iii", "LineColor", "Path/line colour from three components."},
    {0x25, "iii", "PreviewColor", "Preview colour from three components."},
    {0x26, "iiiii", "GridRectangle", "Fills grid cells with a kind (0 open, 1 area, 2 wall)."},
    {0x27, "", "FreeDirection", "Lines may join points in any direction."},
    {0x28, "ii", "GridPoint", "Marks a connectable grid point."},
    {0x29, "iiii", "LineAnswer", "Adds a required answer segment."},
    {0x2a, "i", "ShowActor", "Makes an actor visible."},
    {0x2b, "i", "HideActor", "Hides an actor."},
    {0x2c, "if", "ActorFade",
     "Shows an actor with an alpha speed, waits 32/|speed|+4 frames (0207936c)."},
    {0x2d, "i", "ChapterCard", "Displays a chapter title and waits for dismissal."},
    {0x2e, "iisii", "CyclingSprite", "Adds a sprite with initial and required animation indices."},
    {0x30, "ii", "ActorPosition",
     "Moves an actor slot to a stage position, then one frame (02079718 -> ov8 020f0de4)."},
    {0x31, "i", "WaitFrames", "Waits the specified frame count."},
    {0x32, "", "MainFadeIn", "Fades the lower screen in."},
    {0x33, "", "MainFadeOut", "Fades the lower screen out."},
    {0x35, "ii", "AddCounter", "Adds a byte to save counter [index] (0207988c -> 020826b0)."},
    {0x36, "ii", "OrCounter", "ORs a byte mask into save counter [index] (020798e8 -> 020826d0)."},
    {0x37, "iiii", "Blend", "Configures the screen blend transition."},
    {0x39, "iisi", "SlotTile", "Tile at its home slot with sprite and animation (ov39)."},
    {0x3a, "ii", "SlotTarget", "Labelled target slot (ov39)."},
    {0x3b, "i", "SlotAnswers", "Number of accepted arrangements (ov39)."},
    {0x3c, "ii", "SlotAnswer", "Next accepted slot label of a tile (ov39)."},
    {0x3e, "", "NewRegionGroup", "Starts another drawing-picture region group."},
    {0x3f, "is", "ActorAnimation", "Selects an actor animation by name."},
    {0x40, "i", "PancakeCount", "Sets the number of pancakes on the first plate."},
    {0x41, "iiii", "AnswerSlot", "Sets an answer slot's digit count (x/y unused by ov42)."},
    {0x42, "is", "AnswerText", "Sets the expected characters of an answer slot."},
    {0x43, "s", "AnswerBackground", "Selects the nazo/drawinput answer-screen picture."},
    {0x44, "iiiiiii", "KnightBoard", "Sets origin, cell size, dimensions and initial square."},
    {0x49, "fi", "MusicVolumeFrames",
     "BGM volume over frames/60 s, stops the unique effect (0207a15c -> ov8 020f0e5c)."},
    {0x4a, "iiiiiiiiii", "ToggleGrid",
     "Origin, size, cell size, RGB555 components and alpha (ov45)."},
    {0x4b, "iiii", "ToggleAnswer", "Cells that must be on (ov45)."},
    {0x4c, "iiii", "RoseBoard", "Origin, columns and rows of 24-pixel cells (ov46)."},
    {0x4d, "iiii", "RoseWall", "Axis-aligned wall between two grid corners (ov46)."},
    {0x4e, "iiiiii", "SlideBoard", "Origin, columns, rows and cell size (ov47)."},
    {0x4f, "iiii", "SlideWall", "Blocked cell rectangle (ov47)."},
    {0x50, "iii", "SlideGoal", "Required cell of a piece (ov47)."},
    {0x51, "s", "SlideSprite", "Loads a nazo/slide sprite (ov47)."},
    {0x52, "issii", "SlidePiece", "Sprite, held/idle animations and start cell (ov47)."},
    {0x53, "iiii", "SlideRect", "Adds a cell rectangle to the last piece (ov47)."},
    {0x54, "s", "TileResource", "Loads a nonrotating tile sprite resource."},
    {0x55, "ii", "TileSlot", "Adds a tile destination at x and y."},
    {0x57, "iiisssi", "TilePiece", "Defines a piece, its animations, anchor and starting slot."},
    {0x58, "iiisssii", "RotatingTilePiece", "Defines a tile and its initial orientation."},
    {0x59, "iiii", "TileHitRect", "Adds an inclusive hit rectangle to the last piece."},
    {0x5a, "ii", "TileAnswer", "Requires a piece to occupy a specific slot."},
    {0x5b, "iii", "RotatingTileAnswer", "Requires a piece slot and orientation."},
    {0x5c, "i", "SelectVoice", "Sets the next dialogue voice ID."},
    {0x5d, "i", "StreamEffect", "Plays streamed effect ST_%03d.SAD on stream route 0."},
    {0x5e, "i", "SoundEffect", "Plays a sequenced sound effect."},
    {0x5f, "fi", "MusicVolumeFramesOnly",
     "BGM volume over frames/60 s, keeping the unique effect (0207ac90 -> ov8 020f0e7c)."},
    {0x60, "", "TileSwap", "Enables swapping with occupied slots."},
    {0x61, "", "EndToTitle",
     "BGM fade, screen fade, wait, scene 0 (new game -> title) (ov8 020f0f5c)."},
    {0x62, "ifi", "SequenceMusic", "Requests sequenced music with volume and routing."},
    {0x63, "iiiiiiii", "SkateBoard", "Origin, size, start and exit cells (ov49)."},
    {0x64, "iiii", "SkateWall", "Wall on a grid line (ov49)."},
    {0x65, "iiiii", "PegRectangle", "Set occupied/empty cells in a board rectangle."},
    {0x66, "iii", "PairRow", "Item count, slot count and row x (ov51)."},
    {0x67, "iiii", "LampSetup", "Required lamps and street colour components (ov52)."},
    {0x68, "iiii", "LampStreet", "Street between two grid points (ov52)."},
    {0x69, "", "WaitRelease", "Waits indefinitely for a touch release."},
    {0x6a, "i", "ShakeMain", "Main background shake for N frames (0207b154 -> layer +0x8c)."},
    {0x6b, "i", "ShakeSub", "Sub background shake for N frames (0207b190 -> layer +0x90)."},
    {0x6c, "i", "WaitOrRelease", "Waits until timeout or touch release."},
    {0x6d, "iiii", "TileDragRect", "Defines the drag area within a rotatable piece."},
    {0x6e, "iiii", "ToggleDisabled", "Cells excluded from the toggle grid (ov45)."},
    {0x6f, "iiii", "LampDisabled", "Grid points without a lamp position (ov52)."},
    {0x70, "i", "MysteryFound", "Records a discovered mystery."},
    {0x71, "i", "MysteryMenu", "Opens the mystery menu."},
    {0x72, "i", "FadeOutFrames", "Fades both screens out over a supplied duration."},
    {0x73, "ii", "TeaRequest",
     "Scene 25 (herb tea) with following drama event; +0x8233/+0x8234 (0207b4c0)."},
    {0x74, "", "PuzzleItemReward", "Handles the completed puzzle’s item reward."},
    {0x75, "", "PuzzleIndexNotice", "Shows the puzzle-index notification."},
    {0x76, "i", "NazobaMemo",
     "Lists unsolved puzzles of a group passed on (bit 6) (0207b59c -> ov8 020f25e8)."},
    {0x77, "i", "GiveItem", "Adds an item and displays its notification."},
    {0x78, "i", "SetReward",
     "Reward (state +0x8200) for 0x74; run only by the ov8 pre-pass 0207cff0, skipped by the main "
     "dispatcher (0207b634)."},
    {0x79, "i", "FeaturePopup",
     "New-feature window by kind, jingle stream 6 (0207b694 -> ov8 020f21bc)."},
    {0x7a, "i", "RemoveItem", "Clears a story item flag (0207b6e0 -> 02082e44 with 0)."},
    {0x7b, "i", "SavePrompt",
     "Yes/no save window; argument is the resume event, 0 the completed game (0207b730 -> ov8 "
     "020f29fc)."},
    {0x7c, "i", "WaitFramesNoTouch", "Waits frames without input (0207b77c)."},
    {0x7d, "i", "MysterySolved",
     "Mystery menu at index n+9 (solved entry) (0207b7ec -> ov8 020f2d18)."},
    {0x7e, "ii", "ActorShake",
     "Actor slot shakes for N drawn frames (0207b838 -> ov8 020f37d4, actor +0xc)."},
    {0x7f, "i", "MainFadeOutFrames",
     "Main screen to black over N frames, float steps (0207b894 -> 0207205c)."},
    {0x80, "i", "FadeInFrames", "Fades both screens in over n frames."},
    {0x81, "i", "MainFadeInFrames", "Main screen from black over N frames (0207b91c -> 02071974)."},
    {0x82, "", "MainWhiteFadeIn",
     "Main brightness +16 down to 0, one step per frame (ov8 020f37e8)."},
    {0x83, "ii", "CounterJump",
     "Drama event as current/following scene when counter[a]==b (0207b994)."},
    {0x84, "i", "PhotoPiece", "Popup for an obtained photo piece n (ov8 020f382c)."},
    {0x86, "i", "MaximumCenters", "Sets the allowed region count inside a drawing."},
    {0x87, "i", "SubFadeOutFrames", "Fades the upper screen out."},
    {0x88, "i", "SubFadeInFrames", "Fades the upper screen in."},
    {0x89, "", "StopAmbience", "Stops the unique room effect (ov8 020f3adc)."},
    {0x8a, "fi", "MusicVolumeTimed",
     "BGM volume over a time-table duration, stops the unique effect."},
    {0x8b, "fi", "MusicVolumeTimedOnly", "BGM volume over a time-table duration."},
    {0x8c, "ifi", "MusicTimed", "BGM track, volume, time-table fade (ov8 020f0fd0)."},
    {0x8e, "i", "WaitDuration", "Waits for a duration."},
    {0x90, "i", "ClearViewedBit", "state +0x3d8 = n, EventViewed bit n cleared (0207bdc8)."},
    {0x91, "i", "SetViewedBit", "state +0x3d8 = -1, EventViewed bit n set (0207be24)."},
    {0x92, "", "RoomTutorial", "Requests the room tutorial on entry."},
    {0x94, "", "HamsterNamed", "tx 0xd7 with the hamster name from save +0x2f7 (ov8 020f2da0)."},
    {0x95, "", "TornPhoto", "Torn-photo popup tx_216 (0207bf4c -> ov8 020f2ee4)."},
    {0x96, "i", "CompanionJoin",
     "Sets companion bits in save +0x31e, window tx 0xd9 unless bit 2 (ov8 020f30ac)."},
    {0x97, "i", "CompanionLeave",
     "Clears companion bits, window tx 0xda unless bit 2 (ov8 020f32cc)."},
    {0x98, "ii", "SequencedEffect",
     "Sequenced effect; mode 1 also starts it as the tracked effect (0207c018)."},
    {0x99, "iffi", "StreamAudio", "Requests streamed audio with volume, fade and route."},
    {0x9b, "", "AdventureResult",
     "Solved count and rank in the text window, tx 1100..1103 (ov8 020f0bdc)."},
    {0x9c, "iii", "CountJump",
     "With a 0 first argument, solved >= b and event c unviewed: drama event c (ov8 020f34f4)."},
    {0x9d, "", "PlacePrompt", "Yes/no window tx 0xdd; yes stores place 0x23 (ov8 020f356c)."},
    {0x9e, "ii", "TextNotice", "Jingle 5 window with tx n (mode 1: hamster name) (ov8 020f23ec)."},
    {0x9f, "i", "UniqueEffect", "Starts the unique room effect id (ov8 020f3ab4)."},
    {0xa0, "if", "StopEffect", "Stops a sequenced effect over seconds (0207c334 -> 0207f918)."},
    {0xa1, "", "ClearNotice",
     "Unless the game was completed: jingle 5 window tx 1200 (ov8 020f1000)."},
    {0xa2, "iff", "MovieSubtitle", "Schedules a subtitle between two timestamps."},
};

} // namespace

const ScriptDialect& secondAdventureScripts() {
    // Overlay 2, 0207cee0: command 1 ends the script; arguments are typed.
    static const ScriptDialect dialect{kCommands, 1, false};
    return dialect;
}

// Overlay 2, 0207cee0: the one dispatch loop for every script. It reads
// tokens; a command token looks up the scene's handler, reads the command's
// arguments as the table above describes, and calls the handler. Commands a
// scene does not handle are an error here, whereas the original silently
// runs its default (often empty) handler.
void ScriptInterpreter::run(const ScriptContext& context) {
    if (dialect_.structured) {
        runStructured(context);
        return;
    }
    ScriptArguments source(reader);
    while (reader.position() < reader.size()) {
        auto token = reader.next();
        if (token.tag == 12) {
            return; // End - Stops at the terminal token.
        }
        if (token.tag != 0) {
            continue;
        }
        auto command = reader.values().command;
        if (command == dialect_.stop) {
            reader.finish();
            return;
        } // Stop - Ends execution and skips the remaining body.
        if (onCommand) {
            onCommand({command, token.offset, reader.position(), true});
        }
        if (command >= context.handlers_.size() || !context.handlers_[command]) {
            throw std::runtime_error("GDS command unavailable in this context: " +
                                     std::to_string(command));
        }
        const auto* info = dialect_.find(command);
        if (!info) {
            throw std::runtime_error("unreconstructed GDS command " + std::to_string(command));
        }
        auto layout = info->arguments;
        // Original virtual +8 precedes argument reads. Resolve support first.
        if (context.beforeCommand) {
            context.beforeCommand();
        }
        CommandArguments args;
        for (char type : layout) {
            switch (type) {
            case 'i':
                args.values_.emplace_back(source.integer());
                break; // Integer - Decodes a signed 32-bit argument.
            case 'f':
                args.values_.emplace_back(CommandArguments::FloatBits{source.floatingBits()});
                break; // Float - Preserves the exact binary32 argument bits.
            case 'b':
                args.values_.emplace_back(source.truth());
                break; // Truth - Accepts a nonzero integer or case-insensitive true string.
            case 's':
                args.values_.emplace_back(source.string());
                break; // String - Decodes a NUL-terminated byte string.
            default:
                throw std::runtime_error(
                    "invalid internal GDS argument layout"); // InvalidLayout - Rejects a programmer
                                                             // error.
            }
        }
        context.handlers_[command](args);
        if (!args.consumed()) {
            throw std::runtime_error("GDS context left arguments unread");
        }
        if (onCommand) {
            onCommand({command, token.offset, reader.position(), false});
        }
    }
}

// ---------------------------------------------------------------------------
// Structured scripts (first adventure). The original's handler for each
// command reads its own arguments and returns where the script continues;
// the flow commands below read tokens themselves. Addresses are ARM9.

// ARM9 02017e7c reads a token wherever the cursor is. One parameter script
// (puzzle 114) ends three tokens before its last command's arguments do, so
// the original reads past the end of the script data there; here every read
// at the end yields the end token without moving, which leaves the value
// slots unchanged.
ScriptToken ScriptInterpreter::nextToken() {
    if (reader.position() >= reader.size()) {
        ScriptToken end;
        end.offset = end.next = reader.size();
        end.tag = 12;
        return end;
    }
    return reader.next();
}

// Each argument is the next token, whatever its kind; the handler takes the
// value from the slot of the kind it expects, so a token of another kind
// leaves an older value in place (e.g. the integer arguments some events
// give to float parameters).
CommandArguments ScriptInterpreter::readArguments(std::string_view layout) {
    CommandArguments args;
    const auto stringOf = [](const Bytes& bytes) {
        return std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0));
    };
    for (char type : layout) {
        const auto token = nextToken();
        const auto& v = reader.values();
        switch (type) {
        case 'i':
            args.values_.emplace_back(std::bit_cast<std::int32_t>(v.integerBits));
            break; // Integer slot (state +8).
        case 'f':
            args.values_.emplace_back(CommandArguments::FloatBits{v.floatBits});
            break; // Float slot (state +4).
        case 's':
            args.values_.emplace_back(stringOf(v.string));
            break; // String buffer (0220d48c).
        case 't':
            args.values_.emplace_back(stringOf(v.alternateString));
            break; // Second string buffer (0220dc8c).
        case 'b':
            args.values_.emplace_back(truthOf(token));
            break; // Truth: an integer token != 0, or a string token "true" (any case).
        case 'x':
            break; // A token the handler reads but does not use.
        default:
            throw std::runtime_error("invalid internal GDS argument layout");
        }
    }
    return args;
}

// ARM9 02018050, the main loop: skips tokens up to the next command, stops at
// the end token or the stop command (0x4f), otherwise calls the command's
// handler. (Its other exit, 0200bd3c, always answers "go on".)
void ScriptInterpreter::runStructured(const ScriptContext& context) {
    while (reader.position() < reader.size()) {
        const auto token = nextToken();
        if (token.tag == 12) {
            return;
        }
        if (token.tag != 0) {
            continue;
        }
        const auto command = reader.values().command;
        if (command == dialect_.stop) {
            reader.finish();
            return;
        }
        dispatch(context, command, token.offset);
    }
}

// The loop of the engine command (02011610 after creating the engine): every
// command up to the end token goes to the new object; the stop command is an
// ordinary (empty) command here.
void ScriptInterpreter::runRest(const ScriptContext& context) {
    for (;;) {
        const auto token = nextToken();
        if (token.tag == 12) {
            return;
        }
        if (token.tag == 0) {
            dispatch(context, reader.values().command, token.offset);
        }
    }
}

// One command: the built-in flow commands, then conditions and ordinary
// commands of the context. Every handler of the original starts with the
// object's virtual +8 (`beforeCommand`).
void ScriptInterpreter::dispatch(const ScriptContext& context, std::uint16_t command,
                                 std::size_t offset) {
    if (onCommand) {
        onCommand({command, offset, reader.position(), true});
    }
    if (context.beforeCommand) {
        context.beforeCommand();
    }
    if (command == dialect_.ifCommand) {
        ifCommand(context);
    } else if (command == dialect_.elseIfCommand || command == dialect_.elseCommand) {
        skipBranch();
    } else if (command == dialect_.repeatCommand) {
        repeatCommand(context);
    } else if (command == dialect_.whileCommand) {
        whileCommand(context);
    } else if (command == dialect_.trueCommand) {
        condition_ = true; // 020115b8
    } else if (command == dialect_.falseCommand) {
        condition_ = false; // 020115e4
    } else if (command == dialect_.stop) {
        // 02012d1c: only the virtual +8 (reached outside the main loop).
    } else {
        const auto* info = dialect_.find(command);
        if (!info) {
            throw std::runtime_error("unreconstructed GDS command " + std::to_string(command));
        }
        auto args = readArguments(info->arguments);
        if (context.conditions_[command]) {
            condition_ = context.conditions_[command](args);
        } else if (context.handlers_[command]) {
            context.handlers_[command](args);
        } else {
            throw std::runtime_error("GDS command unavailable in this context: " +
                                     std::to_string(command));
        }
        if (!args.consumed()) {
            throw std::runtime_error("GDS context left arguments unread");
        }
    }
    if (onCommand) {
        onCommand({command, offset, reader.position(), false});
    }
}

// The condition list of an if or while (02010f2c, 0201127c): commands up to
// the jump token, each setting the condition. A "not" token negates every
// later condition of the chain (the flag is never cleared); "and" and "or"
// switch how the following results combine. Without either, a later result
// replaces the earlier one. Returns the combined result; `endTarget` is the
// jump token's target. A stop command inside the chain ends the script.
bool ScriptInterpreter::conditionChain(const ScriptContext& context, std::size_t& endTarget,
                                       bool& stopped) {
    bool negate = false, both = false, either = false, first = true, result = false;
    stopped = false;
    auto token = nextToken();
    while (token.tag != 6) {
        if (token.tag == 8) {
            negate = true;
        } else if (token.tag == 9) {
            both = true;
            either = false;
        } else if (token.tag == 10) {
            either = true;
            both = false;
        } else if (token.tag == 0) {
            const auto command = reader.values().command;
            if (command == dialect_.stop) {
                reader.finish();
                stopped = true;
                return false;
            }
            dispatch(context, command, token.offset);
            if (negate) {
                condition_ = !condition_;
            }
            if (first) {
                result = condition_;
                first = false;
            } else if (both) {
                result = result && condition_;
            } else if (either) {
                result = result || condition_;
            } else {
                result = condition_;
            }
        }
        token = nextToken();
        if (token.tag == 12) {
            break;
        }
    }
    // The target is the jump slot even when the chain ran into the end token.
    endTarget = reader.values().addressBits;
    return result;
}

// 02010f2c, if: a true chain continues into the branch; otherwise the script
// goes to the jump target, which is the next else-if (whose chain is then
// tested the same way), an else (whose branch then runs) or the end.
void ScriptInterpreter::ifCommand(const ScriptContext& context) {
    std::size_t target = 0;
    bool stopped = false;
    if (conditionChain(context, target, stopped) || stopped) {
        return;
    }
    reader.seek(target);
    const auto token = nextToken();
    if (token.tag == 0 && reader.values().command == dialect_.elseIfCommand) {
        if (context.beforeCommand) {
            context.beforeCommand(); // the else-if is tested by calling 02010f2c again
        }
        ifCommand(context);
    } else if (!(token.tag == 0 && reader.values().command == dialect_.elseCommand)) {
        reader.seek(target);
    }
}

// 020114a0 (else-if) and 02011508 (else), reached at the end of a taken
// branch: skip to their jump token and go to its target. Chained else-if and
// else commands repeat this until the end of the whole if.
void ScriptInterpreter::skipBranch() {
    auto token = nextToken();
    while (token.tag != 6 && token.tag != 12) {
        token = nextToken();
    }
    reader.seek(reader.values().addressBits);
}

// The body of a loop: commands until the cursor reaches `end`. A break token
// leaves the loop (to `end`), the end token or the stop command the script.
// A command token that ends at or after `end` is not run.
ScriptInterpreter::BodyEnd ScriptInterpreter::runBody(const ScriptContext& context,
                                                      std::size_t end) {
    while (reader.position() < end) {
        const auto token = nextToken();
        if (token.tag == 11) {
            reader.seek(end);
            return BodyEnd::Break;
        }
        if (reader.position() >= end) {
            return BodyEnd::Done;
        }
        if (token.tag == 12) {
            return BodyEnd::End;
        }
        if (token.tag != 0) {
            continue;
        }
        const auto command = reader.values().command;
        if (command == dialect_.stop) {
            reader.finish();
            return BodyEnd::Stop;
        }
        dispatch(context, command, token.offset);
    }
    return BodyEnd::Done;
}

// 02011138, repeat: an integer count, then the jump token (the end of the
// body). The body runs count times. Quirk: with a count of 0 the original
// returns the start of the body, so the body still runs once, as ordinary
// commands of the enclosing loop.
void ScriptInterpreter::repeatCommand(const ScriptContext& context) {
    nextToken();
    auto count = std::bit_cast<std::int32_t>(reader.values().integerBits);
    for (auto token = nextToken(); token.tag != 6; token = nextToken()) {
        if (token.tag == 12) {
            throw std::runtime_error("GDS repeat without its jump token");
        }
    }
    const std::size_t end = reader.values().addressBits;
    const std::size_t start = reader.position();
    while (count != 0) {
        --count;
        reader.seek(start);
        if (runBody(context, end) != BodyEnd::Done) {
            return;
        }
    }
}

// 0201127c, while: the condition chain as for if, its jump token marking the
// end of the body. A false chain goes to the end; otherwise the body runs and
// the chain is tested again.
void ScriptInterpreter::whileCommand(const ScriptContext& context) {
    const std::size_t start = reader.position();
    for (;;) {
        reader.seek(start);
        std::size_t end = 0;
        bool stopped = false;
        const bool result = conditionChain(context, end, stopped);
        if (stopped) {
            return;
        }
        if (!result) {
            reader.seek(end);
            return;
        }
        if (runBody(context, end) != BodyEnd::Done) {
            return;
        }
    }
}

ScriptContext openingContext(OpeningHost& host) {
    ScriptContext context;
    // The opening's original virtual +8 target is a no-op (0206abe0).
    for (auto command : {0x21, 0x22}) {
        context.bind(command, [&host, command](CommandArguments& a) {
            auto name = a.string();
            auto layer = a.integer();
            host.background(command == 0x22, name, layer);
        }); // ov2 02078eac / 02078f18
    }
    context.bind(2, [&host](CommandArguments&) { host.fadeIn(); });  // 02077cf4
    context.bind(3, [&host](CommandArguments&) { host.fadeOut(); }); // 02077d28
    for (auto command : {0x31, 0x6c}) {
        context.bind(command, [&host, command](CommandArguments& a) {
            host.wait(a.integer(), command == 0x6c);
        });
    }
    context.bind(0x99, [&host](CommandArguments& a) {
        auto id = a.integer();
        auto first = a.floatingBits();
        auto second = a.floatingBits();
        auto route = a.integer();
        host.audioRequest(id, first, second, route);
    }); // 0207c098, retaining exact float bits and route order.
    return context;
}
} // namespace picarat
