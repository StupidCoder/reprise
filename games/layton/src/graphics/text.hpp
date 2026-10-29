#pragma once
#include "graphics/graphics.hpp"

namespace picarat {
// The game's text: byte strings in the Windows-1252 character set. Texts may
// contain escapes that stand for special characters: "<name>" in the second
// adventure, "{xy}" in the third; the table of escapes and its delimiters
// come from the game itself (escapes.bin). Texts are never
// converted to another character set.
class GameText {
public:
    explicit GameText(const std::filesystem::path& assets);
    std::string expand(std::string_view source) const;

private:
    std::map<std::string, unsigned> escapes_;
    char open_ = '<', close_ = '>';
};

int textAdvance(const Font& font, unsigned character);
void textGlyph(Surface& image, const Font& font, unsigned character, int x, int y,
               std::uint16_t color = 0x8000);
// Native glyph submission; callers own recovered layout, wrapping and timing.
void textLines(Surface& image, const Font& font, std::string_view text, int x, int y,
               int lineHeight = 16, std::uint16_t color = 0x8000, bool colorControls = false);

struct PuzzleData {
    explicit PuzzleData(std::span<const std::uint8_t> bytes, const GameText& text);
    unsigned number = 0, type = 0, tutorial = 0, background = 0, theme = 0, flags = 0,
             place = 0;   // place +0x39 (puzzle index p_%u)
    unsigned soundId = 0; // +0x3c room-sound row (rc/snd_fix), ov27 020f2650
    std::array<unsigned, 3> picarats{};
    int completionValue = 0;
    std::string title, prompt, correct, incorrect;
    std::array<std::string, 3> hints;
};
} // namespace picarat
