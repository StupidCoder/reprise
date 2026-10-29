// Readers for the first adventure's own containers. See a1_formats.hpp.
#include "import/a1_formats.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace picarat::import::adventure1 {

namespace {

bool tagIs(std::span<const std::uint8_t> data, std::string_view tag) {
    return data.size() >= tag.size() && std::equal(tag.begin(), tag.end(), data.begin());
}

std::string folded(std::string text) {
    for (char& c : text) {
        c = char(std::tolower(std::uint8_t(c)));
    }
    return text;
}

} // namespace

std::vector<LpckMember> lpckMembers(std::span<const std::uint8_t> data) {
    ByteReader r(data);
    const std::uint32_t headerSize = r.u32(), total = r.u32(), count = r.u32();
    check(tagIs(r.take(4), "LPCK"), "missing LPCK signature");
    check(headerSize == 16 && total == data.size(), "invalid LPCK extent");
    std::vector<LpckMember> members;
    std::set<std::string> names;
    while (r.position() < total) {
        const std::size_t start = r.position();
        const std::uint32_t head = r.u32(), extent = r.u32(), zero = r.u32(), size = r.u32();
        check(head > 16 && head <= extent && extent <= total - start && zero == 0,
              "invalid LPCK member extent");
        check(size <= extent - head, "LPCK payload exceeds member");
        const auto nameRegion = r.take(head - 16);
        const auto end = std::find(nameRegion.begin(), nameRegion.end(), 0);
        check(end != nameRegion.end(), "unterminated LPCK name");
        check(std::all_of(nameRegion.begin(), end, [](std::uint8_t c) { return c < 128; }),
              "non-ASCII LPCK name");
        std::string name = safeName(std::string(nameRegion.begin(), end));
        check(names.insert(folded(name)).second, "duplicate/case-colliding LPCK name");
        const auto payload = r.take(size);
        r.take(extent - head - size);
        members.push_back({std::move(name), payload});
    }
    check(members.size() == count, "LPCK member count mismatch");
    return members;
}

SoundArchive parseSoundArchive(std::span<const std::uint8_t> data) {
    ByteReader r(data);
    check(tagIs(r.take(4), "SDAT"), "missing SDAT signature");
    const std::uint16_t bom = r.u16(), version = r.u16();
    const std::uint32_t size = r.u32();
    const std::uint16_t headerSize = r.u16(), blockCount = r.u16();
    check(bom == 0xfeff && version == 0x100 && size == data.size() && headerSize >= 0x40 &&
              blockCount == 4,
          "unsupported SDAT header");

    struct Block {
        std::uint32_t offset, size;
    };

    Block blocks[4];
    const char* const tags[4] = {"SYMB", "INFO", "FAT ", "FILE"};
    for (int i = 0; i < 4; ++i) {
        blocks[i] = {r.u32(), r.u32()};
        const Block& b = blocks[i];
        check(b.offset >= headerSize && b.offset <= size && b.size <= size - b.offset &&
                  tagIs(data.subspan(b.offset), tags[i]),
              "invalid SDAT block");
    }
    const Block& fileBlock = blocks[3];
    ByteReader fat(data.subspan(blocks[2].offset, blocks[2].size), 8);
    const std::uint32_t count = fat.u32();
    check(count <= 65536, "excessive SDAT file count");
    SoundArchive archive;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t offset = fat.u32(), length = fat.u32();
        fat.u32();
        fat.u32();
        check(offset >= fileBlock.offset && offset <= fileBlock.offset + fileBlock.size &&
                  length >= 16 && length <= fileBlock.offset + fileBlock.size - offset,
              "SDAT file outside its FILE block");
        const auto file = data.subspan(offset, length);
        // Each member is itself a Nitro file whose header states its size.
        check(std::uint32_t(file[8] | file[9] << 8 | file[10] << 16 | file[11] << 24) <= length,
              "SDAT member larger than its FAT entry");
        if (tagIs(file, "SSEQ")) {
            ++archive.sequences;
        } else if (tagIs(file, "SSAR")) {
            ++archive.sequenceArchives;
        } else if (tagIs(file, "SBNK")) {
            ++archive.banks;
        } else if (tagIs(file, "SWAR")) {
            ++archive.waveArchives;
        } else {
            throw FormatError("SDAT file of an unknown kind");
        }
    }
    return archive;
}

} // namespace picarat::import::adventure1
