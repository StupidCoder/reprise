// Physics verification tool: runs Beacon's ports of the physics library's routines on inputs
// given on stdin, one case per line, and prints the results, for comparison with the original
// routines executed on the same inputs (.analysis/tools/pfxprobe.py).
//
//   convex full|single SHAPE POSE SHAPE POSE
//     SHAPE: cyl HALF_LENGTH RADIUS | tet X Y Z X Y Z X Y Z X Y Z | sph R | box HX HY HZ |
//            cap HALF_LENGTH RADIUS | hull N X Y Z ...
//     POSE:  R00 R01 R02 R10 R11 R12 R20 R21 R22 TX TY TZ (rotation by rows)
//   prints: none | DIST NX NY NZ AX AY AZ BX BY BZ
//   sort4 manifold|cache D0 D1 D2 D3 NEW_D  P0(3) P1(3) P2(3) P3(3) NEW_P(3)
//   prints: the index of the point the new one replaces
//   cylmesh FILE OFFSET SIZE SX SY SZ HALF RADIUS MESHPOSE CYLPOSE   (PELM blob at OFFSET in FILE)
//   prints: COUNT then per point | DIST SUB_A SUB_B NX NY NZ AX AY AZ BX BY BZ
#include "physics/rigid/ContactManifold.hpp"
#include "physics/rigid/ConvexSolver.hpp"
#include "physics/rigid/MeshBuilder.hpp"
#include "physics/rigid/MeshContacts.hpp"
#include "physics/rigid/PrimitiveContacts.hpp"
#include "physics/rigid/RigidWorld.hpp"
#include "physics/rigid/ShapeCasts.hpp"
#include "physics/rigid/SweptContacts.hpp"
#include "data/PhysicsFile.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace beacon;

using namespace beacon::physics::rigid;

namespace {

// Logs every support query (PHYSICS_LOG=1), for comparison with the original's.
class LoggedSupport : public SupportShape {
public:
    LoggedSupport(std::unique_ptr<SupportShape> s, char name) : shape_(std::move(s)), name_(name) {}
    math::Vec3 support(const math::Vec3& d) const override {
        const math::Vec3 p = shape_->support(d);
        std::fprintf(stderr, "sup%c dir %.9g %.9g %.9g -> %.9g %.9g %.9g\n", name_, d.x, d.y, d.z, p.x, p.y, p.z);
        return p;
    }
    math::Vec3 centre() const override { return shape_->centre(); }

private:
    std::unique_ptr<SupportShape> shape_;
    char name_;
};

std::unique_ptr<SupportShape> readShape(std::istream& in) {
    std::string kind;
    in >> kind;
    if (kind == "cyl") {
        float h = 0, r = 0;
        in >> h >> r;
        return std::make_unique<CylinderSupport>(h, r);
    }
    if (kind == "sph") {
        float r = 0;
        in >> r;
        return std::make_unique<SphereSupport>(r);
    }
    if (kind == "box") {
        math::Vec3 h;
        in >> h.x >> h.y >> h.z;
        return std::make_unique<BoxSupport>(h);
    }
    if (kind == "cap") {
        float h = 0, r = 0;
        in >> h >> r;
        return std::make_unique<CapsuleSupport>(h, r);
    }
    if (kind == "hull") {
        int n = 0;
        in >> n;
        static std::vector<math::Vec3> store[2];
        static int which = 0;
        auto& pts = store[which ^= 1];
        pts.assign(std::size_t(n), {});
        for (auto& v : pts) {
            in >> v.x >> v.y >> v.z;
        }
        return std::make_unique<HullSupport>(pts.data(), n);
    }
    math::Vec3 p[4];
    for (auto& v : p) {
        in >> v.x >> v.y >> v.z;
    }
    return std::make_unique<TetraSupport>(p);
}

Pose readPose(std::istream& in) {
    Pose p;
    for (auto& row : p.rotation.r) {
        in >> row.x >> row.y >> row.z;
    }
    in >> p.translation.x >> p.translation.y >> p.translation.z;
    return p;
}

}   // namespace

// A mesh for the mesh commands: SIZE > 0 the serialized mesh at OFFSET in PATH; SIZE == 0 PATH is a
// triangle file (`soup`) built with buildLargeMesh (as pfxrun builds it with the game's builder).
bool loadMesh(const std::string& path, long offset, long size, data::LargeMesh& mesh, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (size > 0) {
        std::vector<std::uint8_t> blob(static_cast<std::size_t>(size));
        f.seekg(offset);
        f.read(reinterpret_cast<char*>(blob.data()), size);
        return data::decodeLargeMesh(data::ByteView(blob.data(), blob.size()), mesh, error);
    }
    if (path.size() > 5 && path.compare(path.size() - 5, 5, ".dump") == 0) {
        // pfxrun's meshdump of a mesh (e.g. one the game's builder made), read back.
        mesh = {};
        std::string line;
        data::MeshIsland* is = nullptr;
        while (std::getline(f, line)) {
            std::istringstream l(line);
            std::string tag;
            l >> tag;
            std::string w;
            unsigned k, a, b, c, d, e, g;
            if (tag == "mesh") {
                l >> w >> w >> w >> mesh.centre.x >> mesh.centre.y >> mesh.centre.z >> w >> mesh.halfExtent.x >>
                    mesh.halfExtent.y >> mesh.halfExtent.z;
            } else if (tag == "island") {
                mesh.islands.emplace_back();
                is = &mesh.islands.back();
                l >> w >> w >> w >> w >> w >> w >> w >> w >> is->min.x >> is->min.y >> is->min.z >> w >> is->max.x >>
                    is->max.y >> is->max.z;
            } else if (tag == "m" || tag == "t") {
                data::MeshTreeNode t;
                l >> w >> w >> k >> w >> a >> w >> b >> w >> c >> d >> e >> w >> g;
                t.childKinds = std::uint8_t(k);
                t.left = std::uint16_t(a);
                t.right = std::uint16_t(b);
                t.minStep = {std::uint8_t(c), std::uint8_t(d), std::uint8_t(e)};
                t.maxStep[0] = std::uint8_t(g);
                l >> a >> b;
                t.maxStep[1] = std::uint8_t(a);
                t.maxStep[2] = std::uint8_t(b);
                (tag == "m" ? mesh.tree : is->tree).push_back(t);
            } else if (tag == "v") {
                math::Vec3 v;
                l >> w >> v.x >> v.y >> v.z;
                is->vertices.push_back(v);
            } else if (tag == "e") {
                data::MeshEdge me;
                l >> w >> a >> b >> c >> d;
                me.raw = {std::uint8_t(a), std::uint8_t(b), std::uint8_t(c), std::uint8_t(d)};
                is->edges.push_back(me);
            } else if (tag == "f") {
                data::MeshFacet mf;
                l >> w >> w >> mf.normal.x >> mf.normal.y >> mf.normal.z >> w >> mf.thickness >> w >> a >> b >> c >> w >>
                    d >> e >> g >> w >> mf.user;
                mf.vertex = {std::uint8_t(a), std::uint8_t(b), std::uint8_t(c)};
                mf.edge = {std::uint8_t(d), std::uint8_t(e), std::uint8_t(g)};
                is->facets.push_back(mf);
            }
        }
        return !mesh.islands.empty();
    }
    std::uint32_t n[2] = {};
    f.read(reinterpret_cast<char*>(n), 8);
    std::vector<math::Vec3> vertices(n[0]);
    std::vector<std::uint32_t> indices(std::size_t(n[1]) * 3);
    for (auto& v : vertices) {
        f.read(reinterpret_cast<char*>(&v.x), 4);
        f.read(reinterpret_cast<char*>(&v.y), 4);
        f.read(reinterpret_cast<char*>(&v.z), 4);
    }
    f.read(reinterpret_cast<char*>(indices.data()), std::streamsize(indices.size() * 4));
    if (!f) {
        if (error) *error = "short triangle file";
        return false;
    }
    return buildLargeMesh(vertices, indices, mesh, error);
}

void dumpMesh(const data::LargeMesh& m) {
    std::printf("mesh layout 2 centre %.9g %.9g %.9g half %.9g %.9g %.9g islands %zu\n", m.centre.x, m.centre.y,
                m.centre.z, m.halfExtent.x, m.halfExtent.y, m.halfExtent.z, m.islands.size());
    for (std::size_t k = 0; k < m.tree.size(); ++k) {
        const data::MeshTreeNode& t = m.tree[k];
        std::printf(" m %zu k %u l %u r %u min %u %u %u max %u %u %u\n", k, t.childKinds, t.left, t.right, t.minStep[0],
                    t.minStep[1], t.minStep[2], t.maxStep[0], t.maxStep[1], t.maxStep[2]);
    }
    for (std::size_t i = 0; i < m.islands.size(); ++i) {
        const data::MeshIsland& is = m.islands[i];
        std::printf("island %zu v %zu e %zu f %zu min %.9g %.9g %.9g max %.9g %.9g %.9g\n", i, is.vertices.size(),
                    is.edges.size(), is.facets.size(), is.min.x, is.min.y, is.min.z, is.max.x, is.max.y, is.max.z);
        for (std::size_t k = 0; k < is.vertices.size(); ++k)
            std::printf(" v %zu %.9g %.9g %.9g\n", k, is.vertices[k].x, is.vertices[k].y, is.vertices[k].z);
        for (std::size_t k = 0; k < is.edges.size(); ++k)
            std::printf(" e %zu %u %u %u %u\n", k, is.edges[k].raw[0], is.edges[k].raw[1], is.edges[k].raw[2],
                        is.edges[k].raw[3]);
        for (std::size_t k = 0; k < is.facets.size(); ++k) {
            const data::MeshFacet& f = is.facets[k];
            std::printf(" f %zu n %.9g %.9g %.9g t %.9g v %u %u %u e %u %u %u u %u\n", k, f.normal.x, f.normal.y,
                        f.normal.z, f.thickness, f.vertex[0], f.vertex[1], f.vertex[2], f.edge[0], f.edge[1], f.edge[2],
                        f.user);
        }
        for (std::size_t k = 0; k < is.tree.size(); ++k) {
            const data::MeshTreeNode& t = is.tree[k];
            std::printf(" t %zu k %u l %u r %u min %u %u %u max %u %u %u\n", k, t.childKinds, t.left, t.right,
                        t.minStep[0], t.minStep[1], t.minStep[2], t.maxStep[0], t.maxStep[1], t.maxStep[2]);
        }
    }
}

namespace {
}   // namespace

void printCache(const ContactCache& out) {
    std::printf("%d", out.size());
    for (int i = 0; i < out.size(); ++i) {
        const CachedContact& c = out[i];
        std::printf(" | %.9g %llx %llx %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g", c.distance,
                    (unsigned long long)c.subA, (unsigned long long)c.subB, c.normal.x, c.normal.y, c.normal.z,
                    c.pointA.x, c.pointA.y, c.pointA.z, c.pointB.x, c.pointB.y, c.pointB.z);
    }
    std::printf("\n");
}

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        // ccast / cmeshcast QX QY QZ QW HALF <cast / meshcast arguments>: the capsule casts (the
        // ray's radius is the capsule's), as pfxrun.
        bool capsule = false, rayTable = false;
        math::Quat capsuleTurn;
        float capsuleHalf = 0;
        if (line.rfind("rcast ", 0) == 0 || line.rfind("rmeshcast ", 0) == 0) {
            line = line.substr(1);   // the ray table (pfxrun rcast / rmeshcast)
            rayTable = true;
        }
        if (line.rfind("ccast ", 0) == 0 || line.rfind("cmeshcast ", 0) == 0) {
            std::istringstream head(line);
            std::string name;
            head >> name >> capsuleTurn.x >> capsuleTurn.y >> capsuleTurn.z >> capsuleTurn.w >> capsuleHalf;
            std::string rest;
            std::getline(head, rest);
            line = (name == "ccast" ? "cast" : "meshcast") + rest;
            capsule = true;
        }
        std::istringstream in(line);
        std::string cmd, mode;
        in >> cmd >> mode;
        if (cmd == "soup") {
            // soup FILE.pfb OUT [N [SX SY SZ]]: the N-th (default 0) run-time mesh (kind 6) of the file
            // as a triangle file for `meshdump OUT 0 0` here and in pfxrun, its vertices scaled as
            // the module scales them (0xbdd350: by the placement scale, each part at least 0.1).
            std::string out;
            int which = 0;
            math::Vec3 sc{1, 1, 1};
            in >> out >> which >> sc.x >> sc.y >> sc.z;
            std::ifstream f(mode, std::ios::binary);
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            const auto file = data::loadPhysicsFile(data::ByteView(bytes.data(), bytes.size()));
            const data::TriangleSoup* soup = nullptr;
            int seen = 0;
            if (file)
                for (const auto& b : file->bodies)
                for (const auto& sh : b.shapes)
                    if (sh.kind == data::ShapeKind::Mesh && !sh.largeMesh && seen++ == which) soup = &sh.geometry;
            if (!soup) {
                std::printf("no mesh\n");
                continue;
            }
            const math::Vec3 k{std::max(sc.x, 0.1f), std::max(sc.y, 0.1f), std::max(sc.z, 0.1f)};
            std::ofstream o(out, std::ios::binary);
            const std::uint32_t n[2] = {std::uint32_t(soup->vertices.size()), std::uint32_t(soup->indices.size() / 3)};
            o.write(reinterpret_cast<const char*>(n), 8);
            for (const auto& v : soup->vertices) {
                const float p[3] = {v.x * k.x, v.y * k.y, v.z * k.z};
                o.write(reinterpret_cast<const char*>(p), 12);
            }
            o.write(reinterpret_cast<const char*>(soup->indices.data()), std::streamsize(soup->indices.size() * 4));
            std::printf("%u vertices %u triangles\n", n[0], n[1]);
            continue;
        }
        if (cmd == "meshdump") {
            // meshdump FILE OFFSET SIZE: the mesh as pfxrun's meshdump prints it
            long offset = 0, size = 0;
            in >> offset >> size;
            data::LargeMesh mesh;
            std::string error;
            if (!loadMesh(mode, offset, size, mesh, &error)) {
                std::printf("status %s\n", error.c_str());
                continue;
            }
            dumpMesh(mesh);
            continue;
        }
        if (cmd == "bodies") {
            // bodies FILE: motion type and shape kinds of every body in a .pfb
            std::ifstream f(mode, std::ios::binary);
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            const auto file = data::loadPhysicsFile(data::ByteView(bytes.data(), bytes.size()));
            if (!file) {
                continue;
            }
            for (const auto& b : file->bodies) {
                std::printf("%s (%s) %u", b.bodyName.c_str(), b.name.c_str(), b.motionType);
                for (const auto& sh : b.shapes) {
                    std::printf(" %u", sh.rawKind);
                }
                std::printf("\n");
                if (std::getenv("BEACON_BODIES_FULL")) {   // frames and dimensions
                    std::printf("  body at (%g %g %g) turn (%g %g %g %g)\n", b.bodyPosition.x, b.bodyPosition.y,
                                b.bodyPosition.z, b.bodyRotation.x, b.bodyRotation.y, b.bodyRotation.z, b.bodyRotation.w);
                    for (const auto& sh : b.shapes) {
                        std::printf("  shape %u at (%g %g %g) turn (%g %g %g %g) half (%g %g %g) r %g h %g\n",
                                    sh.rawKind, sh.position.x, sh.position.y, sh.position.z, sh.rotation.x,
                                    sh.rotation.y, sh.rotation.z, sh.rotation.w, sh.halfExtents.x, sh.halfExtents.y,
                                    sh.halfExtents.z, sh.radius, sh.halfLength);
                    }
                }
            }
            continue;
        }
        if (cmd == "drop" || cmd == "dropbox" || cmd == "dropcap") {
            // drop FILE OFFSET SIZE FRAMES HALF RADIUS MASS X Y Z  (cylinder axis along x, mesh at the origin;
            // dropbox: box of half extents HALF RADIUS RADIUS; dropcap: capsule)
            std::string path = mode;
            long offset = 0, size = 0;
            int frames = 0;
            float h, r, m, x, y, z;
            in >> offset >> size >> frames >> h >> r >> m >> x >> y >> z;
            static data::LargeMesh mesh;
            std::string error;
            if (!loadMesh(path, offset, size, mesh, &error)) {
                std::printf("error %s\n", error.c_str());
                continue;
            }
            RigidWorld world;
            Body ground;
            ground.state.motion = MotionType::Fixed;
            ground.mass.mass = 0;
            Shape ms;
            ms.type = ShapeType::Mesh;
            ms.mesh = &mesh;
            ms.offset.rotation = Mat3{{{3, 0, 0}, {0, 3, 0}, {0, 0, 3}}};   // world scale
            ground.shapes.push_back(ms);
            world.add(ground);
            Body cyl;
            cyl.state.position = {x * 3, y * 3, z * 3};
            cyl.mass.mass = m;
            const float R = r * 3, H = h * 3;
            cyl.mass.inertia = Mat3{{{m * R * R / 2, 0, 0}, {0, m * (3 * R * R + 4 * H * H) / 12, 0},
                                     {0, 0, m * (3 * R * R + 4 * H * H) / 12}}};
            cyl.mass.restitution = 0.2f;
            cyl.mass.friction = 0.6f;
            if (const char* r = std::getenv("BEACON_DROP_ROLLING")) {   // rolling friction (game default 0)
                cyl.mass.rollingFriction = std::strtof(r, nullptr);
            }
            Shape cs;
            cs.type = cmd == "dropbox" ? ShapeType::Box : cmd == "dropcap" ? ShapeType::Capsule : ShapeType::Cylinder;
            cs.halfLength = H;
            cs.radius = R;
            cs.halfExtents = {H, R, R};
            if (cs.type == ShapeType::Box) {
                cyl.mass.inertia = Mat3{{{m * (R * R + R * R) / 3, 0, 0}, {0, m * (H * H + R * R) / 3, 0},
                                         {0, 0, m * (H * H + R * R) / 3}}};
            }
            cyl.shapes.push_back(cs);
            const int id = world.add(cyl);
            for (int i = 0; i < frames; ++i) {
                world.step();
                const RigidState& st = world.body(id).state;
                const ContactManifold* mf = world.manifold(0, id);
                std::printf("%3d pos %.5f %.5f %.5f vel %.5f %.5f %.5f w %.4f %.4f %.4f contacts %d\n", i,
                            st.position.x / 3, st.position.y / 3, st.position.z / 3, st.linearVelocity.x / 3,
                            st.linearVelocity.y / 3, st.linearVelocity.z / 3, st.angularVelocity.x,
                            st.angularVelocity.y, st.angularVelocity.z, mf ? mf->size() : 0);
                if (mf && std::getenv("BEACON_DROP_POINTS")) {
                    for (int k = 0; k < mf->size(); ++k) {
                        const ContactPoint& cp = mf->point(k);
                        std::printf("      p%d d %.5f A %.4f %.4f %.4f B %.4f %.4f %.4f n %.3f %.3f %.3f imp %.4f %.4f %.4f age %d\n", k,
                                    cp.distance / 3, cp.localPointA.x / 3, cp.localPointA.y / 3, cp.localPointA.z / 3,
                                    cp.localPointB.x / 3, cp.localPointB.y / 3, cp.localPointB.z / 3, cp.normal().x,
                                    cp.normal().y, cp.normal().z, cp.rows[0].accumImpulse, cp.rows[1].accumImpulse,
                                    cp.rows[2].accumImpulse, int(cp.duration));
                    }
                }
                if (i == frames - 1 && mf) {
                    std::printf("    orientation %.4f %.4f %.4f %.4f\n", st.orientation.x, st.orientation.y,
                                st.orientation.z, st.orientation.w);
                    for (int k = 0; k < mf->size(); ++k) {
                        const ContactPoint& cp = mf->point(k);
                        std::printf("    point %d dist %.5f normal %.3f %.3f %.3f\n", k, cp.distance / 3,
                                    cp.normal().x, cp.normal().y, cp.normal().z);
                    }
                }
            }
            continue;
        }
        if (cmd == "cylfacet") {
            std::istringstream args(line.substr(8));
            float f[15];
            for (float& v : f) {
                args >> v;
            }
            unsigned flags = 0;
            args >> flags;
            ContactCache out;
            cylinderFacetContact(f[0], f[1], 7, {f[2], f[3], f[4]}, {f[5], f[6], f[7]}, {f[8], f[9], f[10]},
                                 {f[11], f[12], f[13]}, f[14], flags, out);
            std::printf("%d", out.size());
            for (int i = 0; i < out.size(); ++i) {
                const CachedContact& c = out[i];
                std::printf(" | %.9g %llx %llx %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g", c.distance,
                            (unsigned long long)c.subA, (unsigned long long)c.subB, c.normal.x, c.normal.y,
                            c.normal.z, c.pointA.x, c.pointA.y, c.pointA.z, c.pointB.x, c.pointB.y, c.pointB.z);
            }
            std::printf("\n");
            continue;
        }
        if (cmd == "sphfacet") {
            std::istringstream args(line.substr(8));
            float f[17];
            for (float& v : f) {
                args >> v;
            }
            unsigned flags = 0;
            args >> flags;
            const float tilts[3] = {f[14], f[15], f[16]};
            ContactCache out;
            sphereFacetContact(f[0], 7, {f[1], f[2], f[3]}, {f[4], f[5], f[6]}, {f[7], f[8], f[9]},
                               {f[10], f[11], f[12]}, f[13], tilts, flags, out);
            printCache(out);
            continue;
        }
        if (cmd == "cast") {
            // Same line format as pfxrun's cast.
            std::istringstream args(line.substr(4));
            float f[25];
            for (float& v : f) {
                args >> v;
            }
            const int type = int(f[0]);
            Pose pose;
            for (int r = 0; r < 3; ++r) {
                pose.rotation.r[r] = {f[5 + r * 3], f[6 + r * 3], f[7 + r * 3]};
            }
            pose.translation = {f[14], f[15], f[16]};
            CastRay ray{{f[17], f[18], f[19]}, {f[20], f[21], f[22]}, f[23]};
            CastResult res;
            res.t = f[24];
            bool hit = false;
            const CapsuleRay cray{ray.start, ray.direction, rotationMatrix(capsuleTurn), ray.radius, capsuleHalf};
            if (rayTable && type < 4) {
                hit = type == 0   ? rayCastSphere(f[1], pose, ray, res)
                      : type == 1 ? rayCastBox({f[1], f[2], f[3]}, pose, ray, res)
                      : type == 2 ? rayCastCapsule(f[1], f[2], pose, ray, res)
                                  : rayCastCylinder(f[1], f[2], pose, ray, res);
            } else if (capsule && type < 4) {
                hit = type == 0   ? capsuleCastSphere(f[1], pose, cray, res)
                      : type == 1 ? capsuleCastBox({f[1], f[2], f[3]}, pose, cray, res)
                      : type == 2 ? capsuleCastCapsule(f[1], f[2], pose, cray, res)
                                  : capsuleCastCylinder(f[1], f[2], pose, cray, res);
            } else if (type == 0) {
                hit = castSphere(f[1], pose, ray, res);
            } else if (type == 1) {
                hit = castBox({f[1], f[2], f[3]}, pose, ray, res);
            } else if (type == 2) {
                hit = castCapsule(f[1], f[2], pose, ray, res);
            } else if (type == 3) {
                hit = castCylinder(f[1], f[2], pose, ray, res);
            } else if (type == 4) {
                // N X Y Z ... then M I J K ... (triangles)
                int n = 0, m = 0;
                args >> n;
                std::vector<math::Vec3> pts(static_cast<std::size_t>(n));
                for (auto& v : pts) {
                    args >> v.x >> v.y >> v.z;
                }
                args >> m;
                std::vector<std::uint32_t> idx(static_cast<std::size_t>(m) * 3);
                for (auto& v : idx) {
                    args >> v;
                }
                const HullGeometry hull{pts.data(), n, idx.data(), int(idx.size()), false};
                hit = capsule    ? capsuleCastHull(hull, pose, cray, res)
                      : rayTable ? rayCastHull(hull, pose, ray, res)
                                 : castHull(hull, pose, ray, res);
            }
            std::printf("%d %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %llx\n", int(hit), int(res.hit), res.point.x, res.point.y,
                        res.point.z, res.normal.x, res.normal.y, res.normal.z, res.t, hit ? 1ull : 0ull);
            continue;
        }
        if (cmd == "simplex") {
            // simplex COUNT VX VY VZ W0.. : one step of the distance search's simplex.
            std::istringstream args(line.substr(7));
            int count = 0;
            args >> count;
            math::Vec3 v, w[4];
            args >> v.x >> v.y >> v.z;
            for (int i = 0; i < count; ++i) {
                args >> w[i].x >> w[i].y >> w[i].z;
            }
            float weights[4];
            int ids[4];
            const int n = separationStep(w, count, v, weights, ids);
            std::printf("%d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %d %d %d %d\n", n, v.x, v.y, v.z, weights[0], weights[1],
                        weights[2], weights[3], ids[0], ids[1], ids[2], ids[3]);
            continue;
        }
        if (cmd == "gjk") {
            std::istringstream args(line.substr(3));
            float f[30];
            for (float& v : f) {
                args >> v;
            }
            std::vector<math::Vec3> hulls[2];
            std::unique_ptr<SupportShape> shape[2];
            for (int k = 0; k < 2; ++k) {
                const int t = int(f[3 * k]);
                if (t == 4) {
                    int n = 0;
                    args >> n;
                    hulls[k].resize(std::size_t(n));
                    for (auto& v : hulls[k]) {
                        args >> v.x >> v.y >> v.z;
                    }
                }
            }
            for (int k = 0; k < 2; ++k) {
                const int t = int(f[3 * k]);
                const float d0 = f[3 * k + 1], d1 = f[3 * k + 2];
                if (t == 0) {
                    shape[k] = std::make_unique<SphereSupport>(d0);
                } else if (t == 2) {
                    shape[k] = std::make_unique<CapsuleSupport>(d0, d1);
                } else if (t == 3) {
                    shape[k] = std::make_unique<CylinderSupport>(d0, d1);
                } else {
                    shape[k] = std::make_unique<HullSupport>(hulls[k].data(), int(hulls[k].size()));
                }
            }
            std::istringstream poses(line.substr(3));
            for (int k = 0; k < 6; ++k) {
                float skip;
                poses >> skip;
            }
            const Pose pa = readPose(poses), pb = readPose(poses);
            const Separation r = separation(*shape[0], pa, *shape[1], pb);
            std::printf("%d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", r.status, r.distance, r.normal.x, r.normal.y,
                        r.normal.z, r.pointA.x, r.pointA.y, r.pointA.z, r.pointB.x, r.pointB.y, r.pointB.z);
            continue;
        }
        if (cmd == "sfsep") {
            std::istringstream args(line.substr(5));
            float f[13];
            for (float& v : f) {
                args >> v;
            }
            unsigned flags = 0;
            args >> flags;
            float d = 0;
            math::Vec3 n, pa, pb;
            sweptSphereFacet(f[0], {f[1], f[2], f[3]}, {f[4], f[5], f[6]}, {f[7], f[8], f[9]}, {f[10], f[11], f[12]}, flags, d,
                             n, pa, pb);
            std::printf("%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", d, n.x, n.y, n.z, pa.x, pa.y, pa.z, pb.x, pb.y,
                        pb.z);
            continue;
        }
        if (cmd == "meshcast") {
            // Same line format as pfxrun's meshcast (the pose unscaled; the scale multiplies its
            // columns).
            std::string path = mode;
            long offset = 0, size = 0;
            in >> offset >> size;
            float sx, sy, sz;
            in >> sx >> sy >> sz;
            Pose mp = readPose(in);
            for (auto& row : mp.rotation.r) {
                row = {row.x * sx, row.y * sy, row.z * sz};
            }
            CastRay ray;
            in >> ray.start.x >> ray.start.y >> ray.start.z >> ray.direction.x >> ray.direction.y >> ray.direction.z >>
                ray.radius;
            CastResult res;
            in >> res.t;
            static std::string loaded;
            static long loadedOffset = -1;
            static data::LargeMesh mesh;
            if (loaded != path || loadedOffset != offset) {
                std::string error;
                if (!loadMesh(path, offset, size, mesh, &error)) {
                    std::printf("error %s\n", error.c_str());
                    continue;
                }
                loaded = path;
                loadedOffset = offset;
            }
            const MeshPlacement placement{&mesh, mp, sx * sy * sz < 0};
            const bool hit = capsule ? capsuleCastMesh(placement,
                                                       {ray.start, ray.direction, rotationMatrix(capsuleTurn), ray.radius,
                                                        capsuleHalf},
                                                       res)
                                     : rayTable ? rayCastMesh(placement, ray, res)
                                                : castMesh(placement, ray, res);
            std::printf("%d %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %llx\n", int(hit), int(res.hit), res.point.x, res.point.y,
                        res.point.z, res.normal.x, res.normal.y, res.normal.z, res.t, hit ? 1ull : 0ull);
            continue;
        }
        if (cmd == "ccdmesh") {
            // Same line format as pfxrun's ccdmesh.
            std::string path = mode;
            long offset = 0, size = 0;
            in >> offset >> size;
            float sx, sy, sz, r;
            in >> sx >> sy >> sz >> r;
            const Pose mp = readPose(in), sp = readPose(in), pp = readPose(in);
            static std::string loaded;
            static long loadedOffset = -1;
            static data::LargeMesh mesh;
            if (loaded != path || loadedOffset != offset) {
                std::string error;
                if (!loadMesh(path, offset, size, mesh, &error)) {
                    std::printf("error %s\n", error.c_str());
                    continue;
                }
                loaded = path;
                loadedOffset = offset;
            }
            SweptPrimitive a, b;
            a.kind = SweptPrimitive::SweptSphere;
            a.radius = r;
            b.kind = SweptPrimitive::Mesh;
            b.mesh = &mesh;
            b.mirrored = sx * sy * sz < 0;
            std::vector<CachedContact> found, kept;
            sweptShapeContacts(a, sp, pp, 0, b, mp, mp, 0, found);
            for (const CachedContact& c : found) {
                keepSweptCandidate(kept, c);
            }
            std::printf("%zu", kept.size());
            for (const CachedContact& c : kept) {
                std::printf(" | %.9g %llx %llx %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g", c.distance,
                            (unsigned long long)c.subA, (unsigned long long)c.subB, c.normal.x, c.normal.y, c.normal.z,
                            c.pointA.x, c.pointA.y, c.pointA.z, c.pointB.x, c.pointB.y, c.pointB.z);
            }
            std::printf("\n");
            continue;
        }
        if (cmd == "ccd") {
            // Same line format as pfxrun's ccd (the function address only picks nothing here: the
            // shape types do).
            std::istringstream args(line.substr(3));
            std::string fn;
            args >> fn;
            SweptPrimitive prim[2];
            for (SweptPrimitive& p : prim) {
                float t, d0, d1, d2;
                args >> t >> d0 >> d1 >> d2;
                p.kind = SweptPrimitive::Kind(int(t));
                p.radius = p.kind == SweptPrimitive::Capsule || p.kind == SweptPrimitive::Cylinder ? d1 : d0;
                p.halfLength = d0;
                p.halfExtents = {d0, d1, d2};
            }
            Pose pose[6];
            for (Pose& p : pose) {
                p = readPose(args);
            }
            float ia = 0, ib = 0;
            args >> ia >> ib;
            // Hull points follow for hull shapes: N X Y Z ...
            std::vector<math::Vec3> hulls[2];
            for (int k = 0; k < 2; ++k) {
                if (prim[k].kind == SweptPrimitive::Hull) {
                    int n = 0;
                    args >> n;
                    hulls[k].resize(std::size_t(n));
                    for (auto& v : hulls[k]) {
                        args >> v.x >> v.y >> v.z;
                    }
                    prim[k].hull = hulls[k].data();
                    prim[k].hullCount = n;
                }
            }
            std::vector<CachedContact> out;
            sweptShapeContacts(prim[0], pose[1], pose[2], std::uint32_t(ia), prim[1], pose[4], pose[5], std::uint32_t(ib),
                               out);
            std::printf("%zu", out.size());
            for (const CachedContact& c : out) {
                const math::Vec3 pa = apply(pose[0].rotation, c.pointA) + pose[0].translation;
                const math::Vec3 pb = apply(pose[3].rotation, c.pointB) + pose[3].translation;
                std::printf(" | %.9g %llx %llx %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g", c.distance,
                            (unsigned long long)c.subA, (unsigned long long)c.subB, c.normal.x, c.normal.y, c.normal.z,
                            pa.x, pa.y, pa.z, pb.x, pb.y, pb.z);
            }
            std::printf("\n");
            continue;
        }
        if (cmd == "sweepdemo") {
            // sweepdemo SPEED HALF_THICKNESS SWEPT(0/1) FRAMES [WALL]: a sphere (r 0.3) at x = -3 fired
            // along +x at a fixed wall (half thickness along x; WALL 0 box, 1 hull of the box's
            // corners, 2 cylinder of radius 2 along x); prints the sphere's x each frame (library units).
            std::istringstream args(line.substr(9));
            float speed = 0, half = 0, swept = 0, frames = 0, kind = 0;
            args >> speed >> half >> swept >> frames;
            args >> kind;
            RigidWorld world;
            Body wall;
            wall.state.motion = MotionType::Fixed;
            wall.mass.mass = 0;
            Shape ws;
            ws.type = kind == 1 ? ShapeType::Hull : kind == 2 ? ShapeType::Cylinder : ShapeType::Box;
            ws.halfExtents = {half, 2, 2};
            ws.halfLength = half;
            ws.radius = 2;
            for (int c = 0; c < 8; ++c) {
                ws.hull.push_back({c & 1 ? half : -half, c & 2 ? 2.0f : -2.0f, c & 4 ? 2.0f : -2.0f});
            }
            wall.shapes.push_back(ws);
            world.add(wall);
            Body ball;
            ball.state.position = {-3, 0, 0};
            ball.state.linearVelocity = {speed, 0, 0};
            ball.mass.mass = 1;
            ball.mass.inertia = Mat3{{{0.036f, 0, 0}, {0, 0.036f, 0}, {0, 0, 0.036f}}};
            Shape bs;
            bs.type = ShapeType::Sphere;
            bs.radius = 0.3f;
            ball.shapes.push_back(bs);
            if (swept != 0) {
                bs.type = ShapeType::SweptSphere;
                ball.shapes.push_back(bs);
            }
            const int id = world.add(ball);
            for (int i = 0; i < int(frames); ++i) {
                world.step();
                const RigidState& st = world.body(id).state;
                std::printf("%d x %.4f vx %.3f\n", i, st.position.x, st.linearVelocity.x);
            }
            continue;
        }
        if (cmd == "swept") {
            std::istringstream args(line.substr(5));
            float dt = 0, iterations = 0;
            int n = 0;
            args >> dt >> iterations >> n;
            RigidState st[2];
            SolverBody sb[2];
            for (int b = 0; b < 2; ++b) {
                float f[24];
                for (float& v : f) {
                    args >> v;
                }
                st[b].position = {f[0], f[1], f[2]};
                st[b].orientation = {f[3], f[4], f[5], f[6]};
                st[b].linearVelocity = {f[7], f[8], f[9]};
                st[b].angularVelocity = {f[10], f[11], f[12]};
                st[b].motion = MotionType(int(f[23]));
                sb[b].orientation = st[b].orientation;
                sb[b].massInv = f[13];
                for (int r = 0; r < 3; ++r) {
                    sb[b].inertiaInv.r[r] = {f[14 + 3 * r], f[15 + 3 * r], f[16 + 3 * r]};
                }
                sb[b].motion = st[b].motion;
            }
            struct P {
                ConstraintRow row;
                math::Vec3 la, lb;
            };
            std::vector<P> pts(static_cast<std::size_t>(n));
            for (P& p : pts) {
                float f[10];
                for (float& v : f) {
                    args >> v;
                }
                p.la = {f[4], f[5], f[6]};
                p.lb = {f[7], f[8], f[9]};
                setupSweptPoint(p.row, f[0], {f[1], f[2], f[3]}, p.la, p.lb, dt, st[0], st[1], sb[0], sb[1]);
            }
            for (int it = 0; it < int(iterations); ++it) {
                for (P& p : pts) {
                    solveSweptPoint(p.row, p.la, p.lb, sb[0], sb[1]);
                }
            }
            std::printf("%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g", sb[0].deltaLinearVelocity.x,
                        sb[0].deltaLinearVelocity.y, sb[0].deltaLinearVelocity.z, sb[0].deltaAngularVelocity.x,
                        sb[0].deltaAngularVelocity.y, sb[0].deltaAngularVelocity.z, sb[1].deltaLinearVelocity.x,
                        sb[1].deltaLinearVelocity.y, sb[1].deltaLinearVelocity.z, sb[1].deltaAngularVelocity.x,
                        sb[1].deltaAngularVelocity.y, sb[1].deltaAngularVelocity.z);
            for (const P& p : pts) {
                std::printf(" | %.9g %.9g %.9g", p.row.rhs, p.row.jacDiagInv, p.row.accumImpulse);
            }
            std::printf("\n");
            continue;
        }
        if (cmd == "rolling") {
            std::istringstream args(line.substr(7));
            float head[3];
            for (float& v : head) {
                args >> v;
            }
            RigidState st[2];
            SolverBody sb[2];
            for (int b = 0; b < 2; ++b) {
                float f[24];
                for (float& v : f) {
                    args >> v;
                }
                st[b].angularVelocity = {f[10], f[11], f[12]};
                for (int r = 0; r < 3; ++r) {
                    sb[b].inertiaInv.r[r] = {f[14 + 3 * r], f[15 + 3 * r], f[16 + 3 * r]};
                }
            }
            for (int b = 0; b < 2; ++b) {
                args >> sb[b].deltaAngularVelocity.x >> sb[b].deltaAngularVelocity.y >> sb[b].deltaAngularVelocity.z;
            }
            RollingRow row;
            row.accumImpulse = head[1];
            setupRolling(row, head[0], st[0], st[1], sb[0], sb[1]);
            warmStartRolling(row, sb[0], sb[1]);
            for (int it = 0; it < int(head[2]); ++it) {
                solveRolling(row, sb[0], sb[1]);
            }
            std::printf("%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g 0 0\n", row.direction.x,
                        row.direction.y, row.direction.z, row.rhs, row.jacDiagInv, row.lowerLimit, row.upperLimit,
                        row.accumImpulse, sb[0].deltaAngularVelocity.x, sb[0].deltaAngularVelocity.y,
                        sb[0].deltaAngularVelocity.z, sb[1].deltaAngularVelocity.x, sb[1].deltaAngularVelocity.y,
                        sb[1].deltaAngularVelocity.z);
            continue;
        }
        if (cmd == "integrate") {
            std::istringstream args(line.substr(9));
            float f[15];
            for (float& v : f) {
                args >> v;
            }
            RigidState st;
            st.motion = MotionType(int(f[14]));
            st.position = {f[1], f[2], f[3]};
            st.orientation = {f[4], f[5], f[6], f[7]};
            st.linearVelocity = {f[8], f[9], f[10]};
            st.angularVelocity = {f[11], f[12], f[13]};
            integrate(st, f[0]);
            std::printf("%.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", st.position.x, st.position.y, st.position.z,
                        st.orientation.x, st.orientation.y, st.orientation.z, st.orientation.w);
            continue;
        }
        if (cmd == "manifold") {
            std::istringstream args(line.substr(8));
            ContactManifold m;
            m.reset(0, 1);
            char op;
            while (args >> op) {
                if (op == 'r') {
                    float f[14];
                    for (float& v : f) {
                        args >> v;
                    }
                    m.refresh({f[0], f[1], f[2]}, {f[3], f[4], f[5], f[6]}, {f[7], f[8], f[9]}, {f[10], f[11], f[12], f[13]});
                } else if (op == 'a') {
                    float f[10];
                    for (float& v : f) {
                        args >> v;
                    }
                    m.addPoint(f[0], {f[1], f[2], f[3]}, {f[4], f[5], f[6]}, {f[7], f[8], f[9]}, {});
                } else {
                    break;
                }
            }
            std::printf("%d", m.size());
            for (int i = 0; i < m.size(); ++i) {
                const ContactPoint& p = m.point(i);
                std::printf(" | %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g", int(p.duration), p.distance,
                            p.localPointA.x, p.localPointA.y, p.localPointA.z, p.localPointB.x, p.localPointB.y,
                            p.localPointB.z, p.normal().x, p.normal().y, p.normal().z);
            }
            std::printf("\n");
            continue;
        }
        if (cmd == "solve" || cmd == "posfix") {
            // Same line format as pfxrun: solve DT RESTITUTION FRICTION ITERATIONS N BODY_A BODY_B POINTS /
            // posfix BIAS N BODY_A BODY_B POINTS (BODY: pos quat v w massInv inertiaInv rows motion;
            // POINT: distance normal localA localB accumulated impulses).
            const bool solve = cmd == "solve";
            std::istringstream args(line.substr(cmd.size()));
            float head[4] = {};
            for (int k = 0; k < (solve ? 4 : 1); ++k) {
                args >> head[k];
            }
            int n = 0;
            args >> n;
            RigidState st[2];
            SolverBody sb[2];
            for (int b = 0; b < 2; ++b) {
                float f[24];
                for (float& v : f) {
                    args >> v;
                }
                st[b].position = {f[0], f[1], f[2]};
                st[b].orientation = {f[3], f[4], f[5], f[6]};
                st[b].linearVelocity = {f[7], f[8], f[9]};
                st[b].angularVelocity = {f[10], f[11], f[12]};
                st[b].motion = MotionType(int(f[23]));
                sb[b].orientation = st[b].orientation;
                sb[b].massInv = f[13];
                for (int r = 0; r < 3; ++r) {
                    sb[b].inertiaInv.r[r] = {f[14 + 3 * r], f[15 + 3 * r], f[16 + 3 * r]};
                }
                sb[b].motion = st[b].motion;
            }
            std::vector<ContactPoint> pts(static_cast<std::size_t>(n));
            for (ContactPoint& cp : pts) {
                float f[13];
                for (float& v : f) {
                    args >> v;
                }
                cp.distance = f[0];
                cp.rows[0].normal = {f[1], f[2], f[3]};
                cp.localPointA = {f[4], f[5], f[6]};
                cp.localPointB = {f[7], f[8], f[9]};
                for (int k = 0; k < 3; ++k) {
                    cp.rows[k].accumImpulse = f[10 + k];
                }
            }
            if (solve) {
                ContactSolverParams p;
                p.timeStep = head[0];
                for (ContactPoint& cp : pts) {
                    setupContactPoint(cp, head[1], st[0], st[1], sb[0], sb[1], p);
                }
                for (const ContactPoint& cp : pts) {
                    warmStartContactPoint(cp, sb[0], sb[1]);
                }
                for (int it = 0; it < int(head[3]); ++it) {
                    for (ContactPoint& cp : pts) {
                        solveContactPoint(cp, sb[0], sb[1], head[2]);
                    }
                }
                std::printf("%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g", sb[0].deltaLinearVelocity.x,
                            sb[0].deltaLinearVelocity.y, sb[0].deltaLinearVelocity.z, sb[0].deltaAngularVelocity.x,
                            sb[0].deltaAngularVelocity.y, sb[0].deltaAngularVelocity.z, sb[1].deltaLinearVelocity.x,
                            sb[1].deltaLinearVelocity.y, sb[1].deltaLinearVelocity.z, sb[1].deltaAngularVelocity.x,
                            sb[1].deltaAngularVelocity.y, sb[1].deltaAngularVelocity.z);
                for (const ContactPoint& cp : pts) {
                    std::printf(" | %.9g %.9g %.9g %.9g %.9g", cp.rows[0].rhs, cp.rows[0].jacDiagInv, cp.rows[0].accumImpulse,
                                cp.rows[1].accumImpulse, cp.rows[2].accumImpulse);
                }
                std::printf("\n");
            } else {
                for (const ContactPoint& cp : pts) {
                    correctContactPosition(cp, st[0], st[1], sb[0], sb[1], head[0]);
                }
                for (int b = 0; b < 2; ++b) {
                    std::printf("%s%.9g %.9g %.9g %.9g %.9g %.9g %.9g", b ? " " : "", st[b].position.x, st[b].position.y,
                                st[b].position.z, st[b].orientation.x, st[b].orientation.y, st[b].orientation.z,
                                st[b].orientation.w);
                }
                std::printf("\n");
            }
            continue;
        }
        if (cmd == "boxfacet" || cmd == "capfacet") {
            // boxfacet HX HY HZ | capfacet HALF RADIUS, then N V0 V1 V2 THICKNESS TILT(3) FLAGS
            std::istringstream args(line.substr(8));
            const int dims = cmd == "boxfacet" ? 3 : 2;
            float d[3] = {}, f[16];
            for (int k = 0; k < dims; ++k) {
                args >> d[k];
            }
            for (float& v : f) {
                args >> v;
            }
            unsigned flags = 0;
            args >> flags;
            const float tilts[3] = {f[13], f[14], f[15]};
            ContactCache out;
            if (dims == 3) {
                boxFacetContact({d[0], d[1], d[2]}, 7, {f[0], f[1], f[2]}, {f[3], f[4], f[5]}, {f[6], f[7], f[8]},
                                {f[9], f[10], f[11]}, f[12], tilts, flags, out);
            } else {
                capsuleFacetContact(d[0], d[1], 7, {f[0], f[1], f[2]}, {f[3], f[4], f[5]}, {f[6], f[7], f[8]},
                                    {f[9], f[10], f[11]}, f[12], tilts, flags, out);
            }
            printCache(out);
            continue;
        }
        if (cmd == "cylmesh" || cmd == "sphmesh" || cmd == "hullmesh" || cmd == "boxmesh" || cmd == "capmesh") {
            // cylmesh FILE OFFSET SIZE SX SY SZ HALF RADIUS MESHPOSE(12) CYLPOSE(12)
            std::string path;
            long offset = 0, size = 0;
            path = mode;
            in >> offset >> size;
            const bool sphere = cmd == "sphmesh", hull = cmd == "hullmesh";
            float sx, sy, sz, h = 0, r = 0, boxZ = 0;
            in >> sx >> sy >> sz;
            std::vector<math::Vec3> hullPoints;
            if (hull) {
                int n = 0;
                in >> n;
                hullPoints.resize(std::size_t(n));
                for (auto& v : hullPoints) {
                    in >> v.x >> v.y >> v.z;
                }
            } else if (cmd == "boxmesh") {
                in >> h >> r >> boxZ;
            } else {
                if (!sphere) {
                    in >> h;
                }
                in >> r;
            }
            const Pose mp = readPose(in), cp = readPose(in);
            static std::string loaded;
            static long loadedOffset = -1;
            static data::LargeMesh mesh;
            if (loaded != path || loadedOffset != offset) {
                std::string error;
                if (!loadMesh(path, offset, size, mesh, &error)) {
                    std::printf("error %s\n", error.c_str());
                    continue;
                }
                loaded = path;
                loadedOffset = offset;
            }
            ContactCache out;
            if (hull) {
                hullMeshContacts(MeshPlacement{&mesh, mp, sx * sy * sz < 0}, hullPoints.data(), int(hullPoints.size()), cp,
                                 out);
            } else if (cmd == "boxmesh") {
                boxMeshContacts(MeshPlacement{&mesh, mp, sx * sy * sz < 0}, {h, r, boxZ}, cp, out);
            } else if (cmd == "capmesh") {
                capsuleMeshContacts(MeshPlacement{&mesh, mp, sx * sy * sz < 0}, h, r, cp, out);
            } else if (sphere) {
                sphereMeshContacts(MeshPlacement{&mesh, mp, sx * sy * sz < 0}, r, cp, out);
            } else {
                cylinderMeshContacts(MeshPlacement{&mesh, mp, sx * sy * sz < 0}, h, r, cp, out);
            }
            printCache(out);
            continue;
        }
        if (cmd == "pair") {
            // pair sphsph|boxsph|capsph|capcap DIMS_A POSE_A DIMS_B POSE_B
            const int na = mode == "sphsph" ? 1 : mode == "boxsph" || mode == "boxcap" || mode == "boxbox" ? 3 : 2;
            const int nb = mode == "boxbox" ? 3 : mode == "capcap" || mode == "boxcap" ? 2 : 1;
            float da[3] = {}, db[3] = {};
            for (int k = 0; k < na; ++k) {
                in >> da[k];
            }
            const Pose pa = readPose(in);
            for (int k = 0; k < nb; ++k) {
                in >> db[k];
            }
            const Pose pb = readPose(in);
            ConvexContact c;
            if (mode == "sphsph") {
                c = sphereSphere(da[0], pa, db[0], pb);
            } else if (mode == "boxsph") {
                c = boxSphere({da[0], da[1], da[2]}, pa, db[0], pb);
            } else if (mode == "boxbox") {
                c = boxBox({da[0], da[1], da[2]}, pa, {db[0], db[1], db[2]}, pb);
            } else if (mode == "boxcap") {
                c = boxCapsule({da[0], da[1], da[2]}, pa, db[0], db[1], pb);
            } else if (mode == "capsph") {
                c = capsuleSphere(da[0], da[1], pa, db[0], pb);
            } else {
                c = capsuleCapsule(da[0], da[1], pa, db[0], db[1], pb);
            }
            std::printf("%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", c.distance, c.normal.x, c.normal.y,
                        c.normal.z, c.pointA.x, c.pointA.y, c.pointA.z, c.pointB.x, c.pointB.y, c.pointB.z);
            continue;
        }
        if (cmd == "sort4") {
            float d[5];
            math::Vec3 p[5];
            for (float& v : d) {
                in >> v;
            }
            for (auto& v : p) {
                in >> v.x >> v.y >> v.z;
            }
            const float(&old)[4] = reinterpret_cast<const float(&)[4]>(d);
            const math::Vec3(&pts)[4] = reinterpret_cast<const math::Vec3(&)[4]>(p);
            std::printf("%d\n", pointToReplace(old, pts, d[4], p[4]));
            continue;
        }
        if (cmd != "convex") {
            continue;
        }
        auto a = readShape(in);
        const Pose pa = readPose(in);
        auto b = readShape(in);
        if (std::getenv("PHYSICS_LOG")) {
            a = std::make_unique<LoggedSupport>(std::move(a), 'A');
            b = std::make_unique<LoggedSupport>(std::move(b), 'B');
        }
        const Pose pb = readPose(in);
        const auto c = mode == "full" ? penetrationFull(*a, pa, *b, pb) : penetrationSingle(*a, pa, *b, pb);
        if (!c) {
            std::printf("none\n");
            continue;
        }
        std::printf("%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", c->distance, c->normal.x, c->normal.y,
                    c->normal.z, c->pointA.x, c->pointA.y, c->pointA.z, c->pointB.x, c->pointB.y, c->pointB.z);
    }
}
