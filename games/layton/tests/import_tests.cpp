// Importer codecs (all six adventures) on hand-made inputs, and (when given the local research
// recipe file) the embedded puzzle list against it. Whole-ROM imports are
// checked with picarat_import.
#include "import/a1_formats.hpp"
#include "import/a3_formats.hpp"
#include "import/a4_formats.hpp"
#include "import/a56_formats.hpp"
#include "import/asset_codecs.hpp"
#include "import/a2_puzzle_library.hpp"
#include "import/sha256.hpp"
#include "system/a1_language.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
using namespace picarat;
using namespace picarat::import;

static void expect(bool yes, const char* why) {
    if (!yes) {
        throw std::runtime_error(why);
    }
}

// Appends little-endian halfwords and words.
static void put16(Bytes& out, std::initializer_list<unsigned> values) {
    for (unsigned v : values) {
        out.insert(out.end(), {std::uint8_t(v), std::uint8_t(v >> 8)});
    }
}

static void put32(Bytes& out, std::initializer_list<unsigned> values) {
    for (unsigned v : values) {
        put16(out, {v & 0xffff, v >> 16});
    }
}

template <class F>
static bool rejects(F&& f) {
    try {
        f();
    } catch (const FormatError&) {
        return true;
    }
    return false;
}

int main(int argc, char** argv) try {
    // LZ10: literal 'a', then a 4-byte copy at distance 1 -> "aaaaa".
    expect(
        (decompress(Bytes{0x10, 5, 0, 0, 0x40, 'a', 0x10, 0x00}) == Bytes{'a', 'a', 'a', 'a', 'a'}),
        "LZ10 overlapping copy");
    // RL: a run of three 'x' then the literal "yz".
    expect((decompress(Bytes{0x30, 5, 0, 0, 0x80, 'x', 0x01, 'y', 'z'}) ==
            Bytes{'x', 'x', 'x', 'y', 'z'}),
           "RL runs and literals");
    expect(rejects([] { decompress(Bytes{0x10, 4, 0, 0, 0x80, 0x00, 0x05}); }),
           "LZ10 reference before output");
    // Code compression, decoded from the end: flags 0x10 give literals C, B, A
    // and then a 15-byte copy at distance 3. Footer: span 14 with an 8-byte
    // header, output 4 bytes longer. The stored prefix 'Z' stays in place.
    const Bytes code{'Z',  0x00, 0xc0, 'A',  'B',  'C',  0x10, 0x0e,
                     0x00, 0x00, 0x08, 0x04, 0x00, 0x00, 0x00};
    Bytes expected{'Z'};
    for (int i = 0; i < 6; ++i) {
        expected.insert(expected.end(), {'A', 'B', 'C'});
    }
    expect(decompressCode(code) == expected, "code compression back-reference");
    expect(rejects([] { decompressCode(Bytes{1, 2, 3}); }), "short code stream");
    expect(safeName("data_lt2/bg/a.arc") == "data_lt2/bg/a.arc", "plain relative name");
    for (const char* bad : {"/abs", "a/../b", "a//b", "c:d", "a\\b"}) {
        expect(rejects([&] { safeName(bad); }), "unsafe name accepted");
    }
    // GDS: size word, opcode 0x2e, string "x.arc\0", end.
    const auto tokens = scriptTokens(Bytes{12, 0, 0, 0, 0, 0, 0x2e, 0, 3, 0, 2, 0, 'x', 0, 12, 0});
    expect(tokens.size() == 3 && tokens[0].opcode == 0x2e && tokens[1].text() == "x" &&
               tokens[2].tag == 12,
           "GDS tokens");
    // The first adventure's extra GDS tags: 6 with a jump offset, 9 bare,
    // 4 a string like 3.
    const auto flow =
        scriptTokens(Bytes{16, 0, 0, 0, 6, 0, 8, 0, 0, 0, 9, 0, 4, 0, 2, 0, 'y', 0, 12, 0});
    expect(flow.size() == 4 && flow[0].tag == 6 && flow[0].integer == 8 && flow[1].tag == 9 &&
               flow[2].text() == "y",
           "GDS control-flow tokens");
    // LPCK: one member "a.txt" holding "hi" in a 32-byte record header.
    Bytes lpck{16,  0, 0,  0, 0, 0, 0, 0, 1, 0, 0, 0, 'L', 'P', 'C', 'K', 32,  0,
               0,   0, 36, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0,   0,   'a', '.', 't', 'x',
               't', 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 'h', 'i', 0,   0};
    lpck[4] = std::uint8_t(lpck.size());
    const auto lpckFiles = import::adventure1::lpckMembers(lpck);
    expect(lpckFiles.size() == 1 && lpckFiles[0].name == "a.txt" && lpckFiles[0].data.size() == 2 &&
               lpckFiles[0].data[0] == 'h',
           "LPCK member");
    lpck[8] = 2;
    expect(rejects([&] { import::adventure1::lpckMembers(lpck); }), "LPCK count mismatch accepted");
    // The first adventure's language rule (ARM9 020199b8).
    using picarat::adventure1::FirmwareLanguage;
    expect(picarat::adventure1::folderIndex(FirmwareLanguage::German) == 5 &&
               picarat::adventure1::folderIndex(FirmwareLanguage::Spanish) == 2 &&
               picarat::adventure1::folderIndex(FirmwareLanguage::Japanese) == 1 &&
               picarat::adventure1::folderIndex(FirmwareLanguage::Korean) == 1,
           "firmware language mapping");
    expect(picarat::adventure1::firmwareLanguage("de_AT") == FirmwareLanguage::German &&
               picarat::adventure1::firmwareLanguage("pt") == FirmwareLanguage::English,
           "host language mapping");
    expect(rejects([] { scriptTokens(Bytes{2, 0, 0, 0, 0, 0}); }), "GDS without end token");
    // Type 0 (third adventure) keeps its bytes as they are.
    expect((decompress(Bytes{0, 2, 0, 0, 'h', 'i', 0, 0}) == Bytes{'h', 'i'}), "stored wrapper");
    expect(rejects([] { decompress(Bytes{0, 9, 0, 0, 'h'}); }), "short stored wrapper");
    // LPC2 with one member "a" holding "xy".
    Bytes lpc2{'L', 'P', 'C', '2'};
    for (std::uint32_t word : {1u, 0x30u, 0x32u, 0x20u, 0x2cu, 0x30u, 0u, 0u, 0u, 2u}) {
        for (int i = 0; i < 4; ++i) {
            lpc2.push_back(std::uint8_t(word >> (8 * i)));
        }
    }
    lpc2.insert(lpc2.end(), {'a', 0, 0, 0, 'x', 'y'});
    const auto members = adventure3::lpc2Members(lpc2);
    expect(members.size() == 1 && members[0].name == "a" && members[0].data.size() == 2 &&
               members[0].data[1] == 'y',
           "LPC2 member");
    lpc2[12] = 0x33; // total size no longer matches
    expect(rejects([&] { adventure3::lpc2Members(lpc2); }), "LPC2 size mismatch");
    // LSCR: command 1000 with an integer 7 and the string "s".
    Bytes lscr{'L', 'S', 'C', 'R', 1, 0, 16, 0, 24, 0, 0, 0, 34, 0, 0, 0, 0xe8, 3,
               2,   0,   0,   0,   0, 0, 0,  7, 0,  0, 0, 2, 0,  0, 0, 0, 's',  0};
    const auto script = adventure3::parseLscr(lscr);
    expect(script.commands.size() == 1 && script.commands[0].opcode == 1000 &&
               script.commands[0].operands[0].value == 7 &&
               script.text(script.commands[0].operands[1]) == "s",
           "LSCR command");
    lscr[29] = 9; // unknown operand type
    expect(rejects([&] { adventure3::parseLscr(lscr); }), "LSCR operand type");
    // LIMG: an 8x16 picture, 4 bits per pixel, two colours, one tile used
    // in the top cell and an empty bottom cell.
    Bytes limg{'L', 'I', 'M', 'G', 0x30, 0, 0, 0};
    for (std::uint16_t half : {0x34, 0, 0x34, 0, 0x34, 2, 0x38, 1, 0, 2, 8, 16, 8, 16, 0, 0}) {
        limg.push_back(std::uint8_t(half));
        limg.push_back(std::uint8_t(half >> 8));
    }
    limg.insert(limg.end(), {0, 1, 0, 0, 0, 0, 0, 0}); // area shifts 0 and 1, padding
    limg.insert(limg.end(), {0xe0, 0x03, 0x1f, 0x00}); // green, red
    limg.insert(limg.end(), {0, 0, 0xff, 0xff});       // map: tile 0, empty
    limg.insert(limg.end(), {0x10});                   // pixels 0, 1
    limg.insert(limg.end(), 31, 0);
    const auto picture = adventure3::parseLimg(limg);
    expect(picture.pixel(0, 0).alpha == 0 && picture.pixel(1, 0).index == 1 &&
               picture.pixel(1, 0).alpha == 31 && picture.pixel(1, 8).alpha == 0,
           "LIMG pixels");
    limg[0x3c] = 0x20; // colour 2 does not exist
    expect(rejects([&] { adventure3::parseLimg(limg); }), "LIMG palette bound");
    // Fourth adventure. The name hash is CRC-16/X.25 (its standard check value).
    expect(adventure4::nameHash("123456789") == 0x906e, "GFSA name hash");
    // Method word: the method in the low three bits, the size above them.
    expect((decompressMethodWord(Bytes{2 << 3, 0, 0, 0, 'h', 'i'}) == Bytes{'h', 'i'}),
           "stored method");
    expect((decompressMethodWord(Bytes{5 << 3 | 1, 0, 0, 0, 0x40, 'a', 0x10, 0x00}) ==
            Bytes{'a', 'a', 'a', 'a', 'a'}),
           "LZ method");
    expect(rejects([] { decompressMethodWord(Bytes{5 << 3 | 5, 0, 0, 0, 0}); }), "method 5");
    // GFSP with one member "a" holding "xy".
    Bytes gfsp{'G', 'F', 'S', 'P', 1, 0};
    put16(gfsp, {0x14 / 4, 0x1c / 4, 0x20 / 4, 1, 1});
    put32(gfsp, {0});
    put16(gfsp, {adventure4::nameHash("a"), 0, 2, 0});
    gfsp.insert(gfsp.end(), {'a', 0, 0, 0, 'x', 'y'});
    const auto packed = adventure4::gfspMembers(gfsp);
    expect(packed.size() == 1 && packed[0].name == "a" && packed[0].data.size() == 2 &&
               packed[0].data[1] == 'y',
           "GFSP member");
    gfsp[0x14] ^= 1; // the name no longer matches its hash
    expect(rejects([&] { adventure4::gfspMembers(gfsp); }), "GFSP hash mismatch");
    // GSD1: command 1000 with an integer 7 and the text "s".
    Bytes gsd1{'G', 'S', 'D', '1'};
    put16(gsd1, {1, 0x10 / 4, 2, 0x18 / 4, 0x24 / 4, 0, 1000, 0, 2, 0});
    gsd1.insert(gsd1.end(), {1, 7, 0, 0, 0, 0x18, 0x24, 0, 0, 0, 0, 0, 's', 0});
    const auto list = adventure4::parseGsd1(gsd1);
    expect(list.commands.size() == 1 && list.commands[0].opcode == 1000 &&
               list.operands[0].value == 7 && list.text(list.operands[1]) == "s",
           "GSD1 command");
    gsd1[0x1d] = 0x08; // a text kind without its offset flag
    expect(rejects([&] { adventure4::parseGsd1(gsd1); }), "GSD1 operand type");
    // GTXT: an 8x8 picture, 4 bits per pixel, one tile.
    Bytes gtxt{'G', 'T', 'X', 'T', 2, 0, 0, 0};
    put16(gtxt, {8, 8, 8, 8, 0x24, 0, 0x24, 0, 0x24, 1});
    put32(gtxt, {0x28});
    put16(gtxt, {1, 0, 0, 0});
    gtxt.insert(gtxt.end(), {0x21});
    gtxt.insert(gtxt.end(), 31, 0);
    const auto tile = adventure4::parseGtxt(gtxt);
    expect(tile.pixel(0, 0) == 1 && tile.pixel(1, 0) == 2 && tile.pixel(7, 7) == 0, "GTXT pixels");
    gtxt[0x20] = 2; // two tiles announced, one stored
    expect(rejects([&] { adventure4::parseGtxt(gtxt); }), "GTXT tile bound");
    // SHA-256 in pieces equals the one-shot digest (FIPS 180 "abc" vector).
    Sha256 pieces;
    pieces.update(Bytes{'a'});
    pieces.update(Bytes{'b', 'c'});
    expect(pieces.finish() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           "incremental SHA-256");
    expect(decompressMethodWord(Bytes{0, 0, 0, 0}).empty(), "empty method-word stream");
    // Fifth and sixth adventures: CRC-32 names, an XPCK with "a" = "xy".
    expect(adventure56::nameHash("123456789") == 0xcbf43926, "CRC-32 name hash");
    Bytes xpck{'X', 'P', 'C', 'K', 1, 0};
    put16(xpck, {0x14 / 4, 0x20 / 4, 0x28 / 4, 0, 0, 0, 0});
    put32(xpck, {adventure56::nameHash("a")});
    put16(xpck, {0, 0, 2, 0});
    xpck.insert(xpck.end(), {2 << 3, 0, 0, 0, 'a', 0, 0, 0, 'x', 'y'});
    const auto xmembers = adventure56::packMembers(xpck);
    expect(xmembers.size() == 1 && xmembers[0].name == "a" && xmembers[0].data[1] == 'y',
           "XPCK member");
    xpck[0x14] ^= 1;
    expect(rejects([&] { adventure56::packMembers(xpck); }), "XPCK hash mismatch");
    // XSCR: command 6000 with an integer 7 and the text "s" (stored streams).
    Bytes xscr{'X', 'S', 'C', 'R'};
    put16(xscr, {1, 0x14 / 4});
    put32(xscr, {2, 0x20 / 4, 0x34 / 4});
    xscr.insert(xscr.end(), {8 << 3, 0, 0, 0});
    put16(xscr, {6000, 2, 0, 0});
    xscr.insert(xscr.end(), {16 << 3, 0, 0, 0});
    put32(xscr, {1, 7, 0x18, 0});
    xscr.insert(xscr.end(), {2 << 3, 0, 0, 0, 's', 0});
    const auto xlist = adventure56::parseXscr(xscr);
    expect(xlist.commands.size() == 1 && xlist.commands[0].opcode == 6000 &&
               xlist.operands[0].value == 7 && xlist.strings[0] == 's',
           "XSCR command");
    xscr[0x24] = 9; // unknown operand type
    expect(rejects([&] { adventure56::parseXscr(xscr); }), "XSCR operand type");
    // IMGC: one 8x8 RGBA tile; bytes per pixel are alpha, blue, green, red,
    // and pixels follow the Z order with y on the even bits.
    Bytes imgc{'I', 'M', 'G', 'C', '0', '0', 0, 0, 0x30, 0, 0, 1, 1, 32, 0, 1};
    put16(imgc, {8, 8});
    imgc.resize(0x38, 0);
    put32(imgc, {8, 260, 0, 0});
    imgc.insert(imgc.end(), {2 << 3, 0, 0, 0, 0, 0, 0, 0});
    imgc.insert(imgc.end(), {0x00, 0x08, 0, 0});
    for (int i = 0; i < 64; ++i) {
        imgc.insert(imgc.end(), {255, std::uint8_t(i), 0, std::uint8_t(i)});
    }
    const auto xpicture = adventure56::parseImgc(imgc);
    expect(xpicture.rgba(0, 1)[0] == 1 && xpicture.rgba(1, 0)[0] == 2 &&
               xpicture.rgba(1, 0)[2] == 2 && xpicture.rgba(0, 0)[3] == 255,
           "IMGC pixels");
    imgc[0x0d] = 24; // bits per pixel do not fit format 0
    expect(rejects([&] { adventure56::parseImgc(imgc); }), "IMGC format");
    if (argc == 2) {
        std::ifstream file(argv[1]);
        const std::string json((std::istreambuf_iterator<char>(file)), {});
        const auto list = json.substr(json.find("\"internal_ids\""));
        std::vector<unsigned> ids;
        const std::regex number("[0-9]+");
        for (auto it = std::sregex_iterator(list.begin(),
                                            list.begin() + std::ptrdiff_t(list.find(']')), number);
             it != std::sregex_iterator(); ++it) {
            ids.push_back(unsigned(std::stoul(it->str())));
        }
        expect(std::equal(ids.begin(), ids.end(), kPuzzleLibrary.begin(), kPuzzleLibrary.end()),
               "embedded puzzle list differs from research/puzzle-library.json");
    }
    std::cout << "import tests passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    return 1;
}
