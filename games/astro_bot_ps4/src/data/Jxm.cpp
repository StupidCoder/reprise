#include "data/Jxm.hpp"

#include <cstring>

namespace beacon::data {

namespace {

constexpr std::uint16_t kStructCode = 0x600;

std::uint32_t sizeOfCode(std::uint16_t code) {
    switch (code & ~0xfu) {
    case 0x20:
        return 1;
    case 0x40:
        return 2;
    case 0x80:
        return 4;
    case 0x100:
        return 8;
    default:
        return 0;
    }
}

}   // namespace

const SchemaField* Schema::find(std::uint32_t nameHash) const {
    for (const auto& f : fields) {
        if (f.nameHash == nameHash) {
            return &f;
        }
    }
    return nullptr;
}

std::uint32_t Schema::layOut(std::uint32_t (*structSize)(std::uint32_t, const void*), const void* context) {
    std::uint32_t pos = 0;
    for (auto& f : fields) {
        std::uint32_t align;
        if (f.typeCode == kStructCode) {
            f.size = structSize ? structSize(f.typeHash, context) : 0;
            align = 8;   // the embedded common block holds a 64-bit field
        } else {
            f.size = sizeOfCode(f.typeCode);
            align = f.size ? f.size : 1;
        }
        pos = (pos + align - 1) / align * align;
        f.offset = pos;
        pos += f.size * f.count;
    }
    return pos;
}

double readNumber(ByteView row, const SchemaField& f, std::uint32_t element) {
    std::size_t at = f.offset + std::size_t(element) * f.size;
    switch (f.typeCode & 0xf) {
    case 1:
        return f.size == 4 ? read<float>(row, at) : 0.0;
    case 3:
        return read<std::uint8_t>(row, at);
    case 4:
        return read<std::uint16_t>(row, at);
    case 5:
        return read<std::uint32_t>(row, at);
    case 6:
        return static_cast<double>(read<std::uint64_t>(row, at));
    case 7:
        return read<std::int8_t>(row, at);
    case 8:
        return read<std::int16_t>(row, at);
    case 9:
        return read<std::int32_t>(row, at);
    default:
        return 0.0;
    }
}

double Table::number(std::uint32_t index, std::uint32_t nameHash, double fallback, std::uint32_t element) const {
    const SchemaField* f = schema.find(nameHash);
    if (!f || index >= rowCount || element >= f->count) {
        return fallback;
    }
    return readNumber(row(index), *f, element);
}

std::int64_t Table::integer(std::uint32_t index, std::uint32_t nameHash, std::int64_t fallback) const {
    const SchemaField* f = schema.find(nameHash);
    if (!f || index >= rowCount) {
        return fallback;
    }
    if ((f->typeCode & 0xf) == 6) {
        return static_cast<std::int64_t>(read<std::uint64_t>(row(index), f->offset));
    }
    return static_cast<std::int64_t>(readNumber(row(index), *f));
}

float Table::real(std::uint32_t index, std::uint32_t nameHash, float fallback) const {
    return static_cast<float>(number(index, nameHash, fallback));
}

std::optional<JxmFile> JxmFile::parse(ByteView file, std::string* error) {
    auto fail = [&](const char* message) -> std::optional<JxmFile> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    if (file.size() < 16 || std::memcmp(file.data(), "JXMD", 4) != 0) {
        return fail("not a JXMD model");
    }
    JxmFile out;
    std::size_t offset = 0;
    while (offset + 32 <= file.size()) {
        std::string tag(reinterpret_cast<const char*>(file.data() + offset), 4);
        std::uint32_t size = u32(file, offset + 4), headerSize = u32(file, offset + 12);
        if (size < 32 || offset + size > file.size() || headerSize > size) {
            return fail("corrupt chunk header");
        }
        std::uint32_t fieldCount = u32(file, offset + 20), stride = u32(file, offset + 24),
                      count = u32(file, offset + 28);
        if (headerSize <= 0x20) {
            // Blob length is rows x stride (stride 1 for byte blobs, 64 for the 4x4 matrices of
            // POSE and BIND).
            std::size_t length = std::size_t(count) * std::max<std::uint32_t>(stride, 1);
            out.blobs_.push_back(
                {tag, file.subspan(offset + headerSize, std::min<std::size_t>(length, size - headerSize))});
        } else {
            Table t;
            t.tag = tag;
            t.schema.typeHash = u32(file, offset + 16);
            for (std::uint32_t i = 0; i < fieldCount; ++i) {
                std::size_t at = offset + 32 + std::size_t(i) * 16;
                t.schema.fields.push_back({u32(file, at), u32(file, at + 4), u16(file, at + 8), u32(file, at + 12)});
            }
            std::uint32_t end = t.schema.layOut();
            if ((end + 3) / 4 * 4 > stride && end > stride) {
                return fail("table schema does not fit its row stride");
            }
            t.rowCount = count;
            t.rowStride = stride;
            if (std::size_t(count) * stride > size - headerSize) {
                return fail("table rows truncated");
            }
            t.rows = file.subspan(offset + headerSize, std::size_t(count) * stride);
            out.tables.push_back(std::move(t));
        }
        offset += size;
    }
    return out;
}

const Table* JxmFile::table(std::string_view tag) const {
    for (const auto& t : tables) {
        if (t.tag == tag) {
            return &t;
        }
    }
    return nullptr;
}

ByteView JxmFile::blob(std::string_view tag) const {
    for (const auto& b : blobs_) {
        if (b.tag == tag) {
            return b.bytes;
        }
    }
    return {};
}

std::string_view JxmFile::string(std::int64_t index) const {
    const Table* stbl = table("STBL");
    ByteView text = blob("STBD");
    if (!stbl || index < 0 || index >= stbl->rowCount) {
        return {};
    }
    auto offset = static_cast<std::size_t>(stbl->integer(std::uint32_t(index), fnv1a("m_offset"), 0));
    auto length = static_cast<std::size_t>(stbl->integer(std::uint32_t(index), fnv1a("m_stringLength"), 0));
    if (offset + length > text.size()) {
        return {};
    }
    return {reinterpret_cast<const char*>(text.data() + offset), length};
}

}   // namespace data
