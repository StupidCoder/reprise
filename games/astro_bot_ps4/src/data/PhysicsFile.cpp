#include "data/PhysicsFile.hpp"

#include "data/CollisionMesh.hpp"

#include <cstring>

namespace beacon::data {

namespace {

constexpr std::size_t kBodyNameSize = 0x100;

std::string fixedString(ByteView b, std::size_t at, std::size_t size) {
    std::size_t n = 0;
    while (n < size && at + n < b.size() && b[at + n] != 0) {
        ++n;
    }
    return std::string(reinterpret_cast<const char*>(b.data()) + at, n);
}

math::Vec3 vec3(ByteView b, std::size_t at) {
    return {read<float>(b, at), read<float>(b, at + 4), read<float>(b, at + 8)};
}

math::Quat quat(ByteView b, std::size_t at) {
    return {read<float>(b, at), read<float>(b, at + 4), read<float>(b, at + 8), read<float>(b, at + 12)};
}

struct Block {
    std::uint32_t kind = 0, elementSize = 0, count = 0;
    ByteView data;
};

bool readBlock(ByteView b, std::size_t& at, Block& out) {
    if (at + 16 > b.size()) {
        return false;
    }
    out.kind = u32(b, at);
    out.elementSize = u32(b, at + 4);
    out.count = u32(b, at + 8);
    const std::size_t bytes = std::size_t(out.elementSize) * out.count;
    if (at + 16 + bytes > b.size()) {
        return false;
    }
    out.data = b.subspan(at + 16, bytes);
    at += 16 + bytes;
    return true;
}

// Vertex block (3: floats, three per vertex) and index block (4: u32, three per triangle).
bool readTriangleBlocks(ByteView b, std::size_t& at, TriangleSoup& out) {
    Block v, i;
    if (!readBlock(b, at, v) || v.kind != 3 || v.elementSize != 4 || !readBlock(b, at, i) || i.kind != 4 ||
        i.elementSize != 4) {
        return false;
    }
    for (std::uint32_t k = 0; k + 2 < v.count; k += 3) {
        out.vertices.push_back(vec3(v.data, k * 4));
    }
    for (std::uint32_t k = 0; k < i.count; ++k) {
        std::uint32_t idx = u32(i.data, k * 4);
        if (idx >= out.vertices.size()) {
            return false;
        }
        out.indices.push_back(idx);
    }
    out.indices.resize(out.indices.size() / 3 * 3);
    return true;
}

}   // namespace

std::optional<PhysicsFile> loadPhysicsFile(ByteView b, std::string* error) {
    auto fail = [&](const std::string& m) -> std::optional<PhysicsFile> {
        if (error) {
            *error = m;
        }
        return std::nullopt;
    };
    if (b.size() < 16 || u32(b, 0) != 1) {
        return fail("not a physics file (version 1)");
    }
    PhysicsFile file;
    std::size_t at = 16;
    while (at < b.size()) {
        if (at + 16 > b.size()) {
            return fail("truncated chunk header");
        }
        const std::uint32_t type = u32(b, at), stride = u32(b, at + 4), count = u32(b, at + 8);
        at += 16;
        if (type != 1) {
            if (type == 2) {
                file.jointCount += count;
            }
            at += std::size_t(stride) * count;
            continue;
        }
        for (std::uint32_t n = 0; n < count; ++n) {
            const std::size_t body = at;
            if (body + stride > b.size() || stride < 0x280) {
                return fail("truncated body");
            }
            PhysicsBody pb;
            pb.name = fixedString(b, body, kBodyNameSize);
            pb.bodyName = fixedString(b, body + 0x100, kBodyNameSize);
            pb.motionType = u32(b, body + 0x204);
            pb.nodePosition = vec3(b, body + 0x208);
            pb.nodeRotation = quat(b, body + 0x214);
            pb.nodeScale = vec3(b, body + 0x224);
            pb.bodyPosition = vec3(b, body + 0x230);
            pb.bodyRotation = quat(b, body + 0x23c);
            pb.mass = read<float>(b, body + 0x24c);
            pb.restitution = read<float>(b, body + 0x254);
            pb.friction = read<float>(b, body + 0x258);
            pb.maxLinearVelocity = read<float>(b, body + 0x264);
            pb.maxAngularVelocity = read<float>(b, body + 0x268);
            pb.linearDamping = read<float>(b, body + 0x26c);
            pb.angularDamping = read<float>(b, body + 0x274);
            const std::uint32_t shapeStride = u32(b, body + 0x278), shapeCount = u32(b, body + 0x27c);
            at += stride;
            for (std::uint32_t s = 0; s < shapeCount; ++s) {
                if (at + shapeStride > b.size() || shapeStride < 0x38) {
                    return fail("truncated shape");
                }
                PhysicsShape shape;
                shape.rawKind = u32(b, at);
                shape.position = vec3(b, at + 4);
                shape.rotation = quat(b, at + 0x10);
                const std::size_t data = at + 0x20;
                at += shapeStride;
                switch (shape.rawKind) {
                case 0:
                    shape.kind = ShapeKind::Sphere;
                    shape.radius = read<float>(b, at - shapeStride + 0x34);
                    break;
                case 1:
                    shape.kind = ShapeKind::Box;
                    shape.halfExtents = vec3(b, data);
                    break;
                case 2:
                case 3:
                    shape.kind = shape.rawKind == 2 ? ShapeKind::Capsule : ShapeKind::Cylinder;
                    // +0x2c half length, +0x30 radius: a cylinder whose definition is 1.01 x 0.5 x
                    // 1.0 stores 0.25 / 0.5 (capsules: same field order assumed).
                    shape.halfLength = read<float>(b, data + 0x0c);
                    shape.radius = read<float>(b, data + 0x10);
                    break;
                case 4:
                case 6:
                    shape.kind = shape.rawKind == 4 ? ShapeKind::Convex : ShapeKind::Mesh;
                    if (!readTriangleBlocks(b, at, shape.geometry)) {
                        return fail("bad triangle blocks in " + pb.name);
                    }
                    break;
                case 7: {
                    shape.kind = ShapeKind::Mesh;
                    Block blob;
                    std::string err;
                    auto mesh = std::make_shared<LargeMesh>();
                    if (!readBlock(b, at, blob) || blob.kind != 6 ||
                        !decodeCollisionMesh(blob.data, shape.geometry, &err) ||
                        !decodeLargeMesh(blob.data, *mesh, &err)) {
                        return fail("bad serialized mesh in " + pb.name + ": " + err);
                    }
                    shape.largeMesh = std::move(mesh);
                    shape.blobOffset = std::size_t(blob.data.data() - b.data());
                    shape.blobSize = blob.data.size();
                    break;
                }
                default:
                    return fail("unknown shape kind " + std::to_string(shape.rawKind));
                }
                pb.shapes.push_back(std::move(shape));
            }
            file.bodies.push_back(std::move(pb));
        }
    }
    if (at != b.size()) {
        return fail("trailing bytes");
    }
    return file;
}

}   // namespace data
