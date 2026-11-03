// JXMD container: the engine's model file (.jxm), written by its art pipeline from Maya scenes.
//
// A flat sequence of chunks. Chunk header (16 bytes): tag[4], u32 chunk size, u32 0, u32 header
// size. A table chunk then holds a schema and rows; a blob chunk (header size 0x20) raw bytes:
//   +0x10  u32 typeHash, u32 fieldCount, u32 rowStride, u32 rowCount   (blob: rowCount = bytes)
//   +0x20  fieldCount x { u32 nameHash, u32 typeHash, u16 typeCode, u16 flags, u32 arrayCount }
//   +headerSize  rowCount rows of rowStride bytes
// Hashes are FNV-1a of the source names (ModelSubMeshData, m_vertexFormat, uint16_t, ...).
// typeCode: size class in bits 4..8 (0x20 = 1 byte, 0x40 = 2, 0x80 = 4, 0x100 = 8) and the kind
// in bits 0..3 (1 float, 3 uint8, 4 uint16, 5 uint32, 6 uint64, 7 int8, 8 int16, 9 int32);
// 0x600 marks an embedded structure described by another schema (material templates).
// Fields are laid out in order with natural alignment.
#pragma once

#include "data/Bytes.hpp"
#include "data/Hash.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beacon::data {

struct SchemaField {
    std::uint32_t nameHash = 0;
    std::uint32_t typeHash = 0;
    std::uint16_t typeCode = 0;
    std::uint32_t count = 1;
    std::uint32_t offset = 0;   // within a row
    std::uint32_t size = 0;     // bytes per element (0 for an embedded structure)
};

// Field layout shared by table chunks and material templates.
struct Schema {
    std::uint32_t typeHash = 0;
    std::vector<SchemaField> fields;

    const SchemaField* find(std::uint32_t nameHash) const;
    // Assigns offsets in order with natural alignment. `structSize` resolves embedded structures
    // (type code 0x600) by their type hash; returns the unpadded end offset.
    std::uint32_t layOut(std::uint32_t (*structSize)(std::uint32_t, const void*) = nullptr,
                         const void* context = nullptr);
};

// A field value as a number, whatever its stored type.
double readNumber(ByteView row, const SchemaField& field, std::uint32_t element = 0);

class Table {
public:
    std::string tag;
    Schema schema;
    std::uint32_t rowCount = 0, rowStride = 0;
    ByteView rows;

    ByteView row(std::uint32_t index) const { return rows.subspan(std::size_t(index) * rowStride, rowStride); }
    bool has(std::uint32_t nameHash) const { return schema.find(nameHash) != nullptr; }
    // Typed access by field name hash; returns `fallback` if the field is absent.
    double number(std::uint32_t index, std::uint32_t nameHash, double fallback = 0, std::uint32_t element = 0) const;
    std::int64_t integer(std::uint32_t index, std::uint32_t nameHash, std::int64_t fallback = -1) const;
    float real(std::uint32_t index, std::uint32_t nameHash, float fallback = 0) const;
};

class JxmFile {
public:
    // Parses the chunk list; the file bytes must outlive the object (tables are views).
    static std::optional<JxmFile> parse(ByteView file, std::string* error = nullptr);

    const Table* table(std::string_view tag) const;
    ByteView blob(std::string_view tag) const;
    // Entry of the string table (STBL offsets into the STBD blob); empty if out of range.
    std::string_view string(std::int64_t index) const;

    std::vector<Table> tables;

private:
    struct Blob {
        std::string tag;
        ByteView bytes;
    };
    std::vector<Blob> blobs_;
};

}   // namespace data
