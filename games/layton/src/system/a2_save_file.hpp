// The game's own save file: the cartridge backup memory with three save
// slots, in the original layout, so a native save is the same image a DS
// would hold. Layout (ov2 0206c3f4, 020821a8..02083924, 02083114):
//   0x000  16 bytes  identification string (0206c3f4); missing = blank card
//   0x010 200 bytes  header: checksum, used-slot mask, three 64-byte slot
//                    summaries (name, place, play time, puzzle counts, how
//                    far the game was finished)
//   0x110 + 0x370 n  800 bytes per slot: the progress block (state +0xc8),
//                    its first word a checksum of the rest
//   0xb60 144 bytes  a block shared by all slots (state +0x3e8), checksummed
// Every block's checksum is the 32-bit Fletcher sum of 0206b1b4.
#pragma once
#include "system/resources.hpp"
#include <array>
#include <filesystem>
#include <string>

namespace picarat {
class SaveFile {
public:
    static constexpr std::size_t imageSize = 0x2000; // native file size (blank cells read 0xff)
    static constexpr int slots = 3;
    static constexpr std::size_t headerSize = 200, slotSize = 800, globalSize = 144;

    // Opens (or, when missing, creates a blank) backup image at `path`.
    explicit SaveFile(std::filesystem::path path);

    // Where the app keeps the image: beside the story checkpoint file.
    static std::filesystem::path besideCheckpoint(const std::filesystem::path& checkpoint) {
        return checkpoint.parent_path() / "backup.sav";
    }

    // Empties a slot: its summary is cleared and written, its block blanked
    // (the app's save management; the game itself never erases a slot).
    // Returns false for an index out of range.
    bool erase(int slot);

    // Boot (0206c3f4, 0206bbac): formats a blank card, reads the header
    // (02082204) and checks it, each used slot and the shared block. Returns
    // the damaged parts: bits 0..2 slots (7 for a bad header), bit 3 the
    // shared block; `repair` then clears them as the corruption screen does.
    unsigned boot();
    void repair(unsigned damaged);

    // Header summaries (02082278..020823c8).
    bool used(int slot) const;             // 0208227c: mask bit
    std::string name(int slot) const;      // 020822d4: +0x08, 20 bytes
    int place(int slot) const;             // 02082308: +0x1c
    std::uint32_t seconds(int slot) const; // 0208232c: +0x34
    unsigned found(int slot) const;        // 02082360: +0x38
    unsigned solved(int slot) const;       // 02082394: +0x3a
    unsigned clearLevel(int slot) const;   // 020823c8: +0x3c (0..3)
    bool anyUsed() const;

    // Header writes (0208229c..020823dc) change the RAM copy only.
    void setUsed(int slot, bool used);
    void setName(int slot, const std::string& name);
    void setPlace(int slot, int place);
    void setSeconds(int slot, std::uint32_t seconds);
    void setFound(int slot, unsigned found);
    void setSolved(int slot, unsigned solved);
    void setClearLevel(int slot, unsigned level);
    void clearSlot(int slot); // 02083924

    // Card transfers.
    void invalidateHeader();                  // 020821a8: write the header with checksum 0
    void commitHeader();                      // 020821d0: checksum and write the header
    void writeSlot(int slot, Bytes progress); // 02083668..02083690 (sets the block checksum)
    Bytes readSlot(int slot) const;           // 020837fc (0206c4e8)

    std::array<std::uint8_t, globalSize>& shared() { return shared_; } // state +0x3e8

    void writeShared(); // 02083114 (with checksum)

    // The console's MAC address (0201fed0), from which the bonus passcodes
    // are computed. Natively a random stand-in made once per installation
    // and kept beside the image (console.id), so the codes stay the same.
    std::array<std::uint8_t, 6> consoleAddress() const;

    static std::uint32_t checksum(const std::uint8_t* data, std::size_t bytes); // 0206b1b4

private:
    std::filesystem::path path_;
    Bytes image_;
    std::array<std::uint8_t, headerSize> header_{}; // RAM copy at state +0x000
    std::array<std::uint8_t, globalSize> shared_{};

    std::uint8_t* summary(int slot) { return header_.data() + 8 + 64 * slot; }

    const std::uint8_t* summary(int slot) const { return header_.data() + 8 + 64 * slot; }

    void write(std::size_t offset, const std::uint8_t* data, std::size_t bytes);
    void flush() const;
};
} // namespace picarat
