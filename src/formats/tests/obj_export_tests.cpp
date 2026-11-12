// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/domain/fixtures.hpp"
#include "fos/formats/obj_export.hpp"
#include "test_support.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>
#include <sstream>

using namespace fos;
using namespace fos::formats;
using geometry::Vec3;
using ::testing::HasSubstr;

namespace {

struct ObjFace {
    std::vector<int> vertices; // 0-based global indices
    int normal = -1;
};

struct ObjObject {
    std::string name, group, material;
    std::vector<ObjFace> faces;
};

/// Minimal independent OBJ reader for the subset the exporter writes.
struct ParsedObj {
    std::vector<Vec3> vertices, normals;
    std::vector<ObjObject> objects;
    std::string mtllib;
    std::vector<std::string> comments;
};

ParsedObj parseObj(const std::string& text) {
    ParsedObj out;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') {
            out.comments.push_back(line);
            continue;
        }
        std::istringstream in(line);
        std::string tag;
        in >> tag;
        if (tag == "v" || tag == "vn") {
            Vec3 v;
            in >> v.x >> v.y >> v.z;
            EXPECT_FALSE(in.fail()) << line;
            (tag == "v" ? out.vertices : out.normals).push_back(v);
        } else if (tag == "o") {
            out.objects.emplace_back();
            in >> out.objects.back().name;
        } else if (tag == "g") {
            in >> out.objects.back().group;
        } else if (tag == "usemtl") {
            in >> out.objects.back().material;
        } else if (tag == "mtllib") {
            in >> out.mtllib;
        } else if (tag == "f") {
            ObjFace face;
            std::string corner;
            while (in >> corner) {
                const auto slashes = corner.find("//");
                EXPECT_NE(slashes, std::string::npos) << line;
                face.vertices.push_back(std::stoi(corner.substr(0, slashes)) - 1);
                face.normal = std::stoi(corner.substr(slashes + 2)) - 1;
            }
            out.objects.back().faces.push_back(face);
        } else {
            ADD_FAILURE() << "unexpected OBJ statement: " << line;
        }
    }
    return out;
}

/// Signed volume by the divergence theorem: sum over fan triangles of v0 . (v1 x v2) / 6.
/// Positive exactly when the faces wind counter-clockwise seen from outside.
double signedVolume(const ParsedObj& obj, const ObjObject& object) {
    double six = 0;
    for (const ObjFace& face : object.faces)
        for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i)
            six += dot(obj.vertices[face.vertices[0]], cross(obj.vertices[face.vertices[i]], obj.vertices[face.vertices[i + 1]]));
    return six / 6.0;
}

const ObjObject& find(const ParsedObj& obj, const std::string& name) {
    for (const auto& o : obj.objects)
        if (o.name == name) return o;
    ADD_FAILURE() << "object " << name << " not found";
    return obj.objects.front();
}

geometry::Aabb boundsOf(const ParsedObj& obj, const ObjObject& object) {
    geometry::Aabb box;
    for (const ObjFace& face : object.faces)
        for (const int v : face.vertices) box.expand(obj.vertices[v]);
    return box;
}

} // namespace

TEST(ObjExport, OneClosedOutwardBoxPerPartWithTheHandComputedVolume) {
    auto exported = exportObj(domain::fixtures::basicCabinet());
    ASSERT_TRUE(exported.ok()) << exported.error().message;
    EXPECT_TRUE(exported.value().diagnostics.empty());
    const ParsedObj obj = parseObj(exported.value().obj);

    ASSERT_EQ(obj.objects.size(), 6u);
    EXPECT_EQ(obj.vertices.size(), 6u * 8u);
    EXPECT_EQ(obj.normals.size(), 6u * 6u);
    // Finished L x W x T in mm^3:
    //   sides        720 * 560 * 18 = 7 257 600
    //   bottom, top  764 * 560 * 18 = 7 701 120
    //   shelf        764 * 540 * 18 = 7 426 080
    //   back         796 * 716 *  3 = 1 709 808
    const std::map<std::string, double> expected{
        {"cab1/side.left", 7257600.0}, {"cab1/side.right", 7257600.0}, {"cab1/bottom", 7701120.0},
        {"cab1/top", 7701120.0},       {"cab1/shelf.1", 7426080.0},    {"cab1/back", 1709808.0}};
    for (const auto& [name, volume] : expected) {
        const ObjObject& object = find(obj, name);
        EXPECT_EQ(object.group, name);
        ASSERT_EQ(object.faces.size(), 6u) << name;
        const double actual = signedVolume(obj, object);
        EXPECT_GT(actual, 0.0) << name << ": faces wind inwards";
        EXPECT_NEAR(actual, volume, 1e-6) << name;
    }
}

TEST(ObjExport, FaceNormalsAgreeWithWindingAndPointOutwards) {
    domain::Model model = domain::fixtures::basicCabinet();
    model.assemblies[0].placement = geometry::Transform::translation({100, 200, 300}) * geometry::Transform::rotationZ(37.0);
    const ParsedObj obj = parseObj(exportObj(model).value().obj);
    for (const ObjObject& object : obj.objects) {
        const Vec3 centre = boundsOf(obj, object).center();
        for (const ObjFace& face : object.faces) {
            ASSERT_EQ(face.vertices.size(), 4u);
            const Vec3 p0 = obj.vertices[face.vertices[0]], p1 = obj.vertices[face.vertices[1]],
                       p2 = obj.vertices[face.vertices[2]], p3 = obj.vertices[face.vertices[3]];
            const Vec3 winding = normalized(cross(p1 - p0, p2 - p0));
            const Vec3 stated = obj.normals[face.normal];
            EXPECT_NEAR(length(stated), 1.0, 1e-12);
            EXPECT_TRUE(approxEqual(winding, stated, 1e-9)) << object.name;
            // Outward: the normal points away from the box centre.
            EXPECT_GT(dot(stated, p0 - centre), 0.0) << object.name;
            // Planar quad: the fourth corner lies in the plane of the first three.
            EXPECT_NEAR(dot(stated, p3 - p0), 0.0, 1e-9) << object.name;
        }
    }
}

TEST(ObjExport, GeometryIsInTheProjectFrame) {
    // Assembly frame == project frame for the fixture. Hand-checked boxes:
    //   left side  occupies x [0, 18],   y [0, 560],   z [0, 720]
    //   back       occupies x [2, 798],  y [560, 563], z [2, 718]
    {
        const ParsedObj obj = parseObj(exportObj(domain::fixtures::basicCabinet()).value().obj);
        const auto left = boundsOf(obj, find(obj, "cab1/side.left"));
        EXPECT_EQ(left.min, (Vec3{0, 0, 0}));
        EXPECT_EQ(left.max, (Vec3{18, 560, 720}));
        const auto back = boundsOf(obj, find(obj, "cab1/back"));
        EXPECT_EQ(back.min, (Vec3{2, 560, 2}));
        EXPECT_EQ(back.max, (Vec3{798, 563, 718}));
    }
    // Rotate the cabinet 90 degrees counter-clockwise about Z and move it to (1000, 2000, 50):
    // assembly (x, y, z) -> project (1000 - y, 2000 + x, 50 + z). The left side becomes
    //   x [1000 - 560, 1000] = [440, 1000],  y [2000, 2018],  z [50, 770].
    {
        domain::Model model = domain::fixtures::basicCabinet();
        model.assemblies[0].placement = geometry::Transform::fromAxes({0, 1, 0}, {-1, 0, 0}, {1000, 2000, 50});
        const ParsedObj obj = parseObj(exportObj(model).value().obj);
        const auto left = boundsOf(obj, find(obj, "cab1/side.left"));
        EXPECT_EQ(left.min, (Vec3{440, 2000, 50}));
        EXPECT_EQ(left.max, (Vec3{1000, 2018, 770}));
        EXPECT_NEAR(signedVolume(obj, find(obj, "cab1/side.left")), 7257600.0, 1e-6);
    }
}

TEST(ObjExport, HeaderMaterialsAndMtl) {
    ObjExportOptions options;
    options.mtlFileName = "cabinet.mtl";
    auto exported = exportObj(domain::fixtures::basicCabinet(), options);
    ASSERT_TRUE(exported.ok());
    const std::string& text = exported.value().obj;
    EXPECT_THAT(text, HasSubstr("# Units: millimetres\n"));
    EXPECT_THAT(text, HasSubstr("Z up"));
    EXPECT_THAT(text, HasSubstr("# Left side: 720 x 560 x 18 mm, assembly cab1\n"));
    const ParsedObj obj = parseObj(text);
    EXPECT_EQ(obj.mtllib, "cabinet.mtl");
    EXPECT_EQ(find(obj, "cab1/side.left").material, "mat.pb18");
    EXPECT_EQ(find(obj, "cab1/back").material, "mat.hdf3");

    // Colours: particleboard (222, 204, 170) / 255 = 0.8706 0.8000 0.6667;
    //          HDF (240, 240, 235) / 255 = 0.9412 0.9412 0.9216.
    const std::string& mtl = exported.value().mtl;
    EXPECT_THAT(mtl, HasSubstr("# Particleboard 18 mm, 18 mm\nnewmtl mat.pb18\nKd 0.8706 0.8000 0.6667\n"));
    EXPECT_THAT(mtl, HasSubstr("newmtl mat.hdf3\nKd 0.9412 0.9412 0.9216\n"));
    // Deterministic output.
    EXPECT_EQ(exportObj(domain::fixtures::basicCabinet(), options).value().obj, text);
}

TEST(ObjExport, NamesAreSanitisedAndMadeUnique) {
    domain::Model model = domain::fixtures::basicCabinet();
    model.materials[0].id = domain::MaterialId("oak natural #3");
    model.materials[0].name = "Oak\nnewmtl injected";
    for (auto& part : model.assemblies[0].parts) {
        if (part.material.str() == "mat.pb18") part.material = model.materials[0].id;
    }
    model.assemblies[0].parts[0].name = "Left\nv 1 2 3";
    model.assemblies[0].parts[1].id = model.assemblies[0].parts[0].id; // duplicate id
    model.assemblies[0].parts[2].id = domain::PartId("my part");
    auto exported = exportObj(model);
    ASSERT_TRUE(exported.ok()) << exported.error().message;
    const ParsedObj obj = parseObj(exported.value().obj); // fails on any injected statement
    EXPECT_EQ(obj.vertices.size(), 48u);
    EXPECT_EQ(obj.objects[0].name, "cab1/side.left");
    EXPECT_EQ(obj.objects[1].name, "cab1/side.left_2");
    EXPECT_EQ(obj.objects[2].name, "my_part");
    EXPECT_EQ(obj.objects[0].material, "oak_natural__3");
    EXPECT_THAT(exported.value().mtl, HasSubstr("# Oak newmtl injected, 18 mm\nnewmtl oak_natural__3\n"));
}

TEST(ObjExport, MissingMaterialGetsAGreyMaterialAndAWarning) {
    domain::Model model = domain::fixtures::basicCabinet();
    model.assemblies[0].parts[5].material = domain::MaterialId("mat.gone");
    model.assemblies[0].parts[4].material = domain::MaterialId();
    auto exported = exportObj(model);
    ASSERT_TRUE(exported.ok());
    const ParsedObj obj = parseObj(exported.value().obj);
    EXPECT_EQ(find(obj, "cab1/back").material, "mat.gone");
    EXPECT_EQ(find(obj, "cab1/shelf.1").material, "unassigned");
    // Default display colour (200, 200, 200) / 255 = 0.7843.
    EXPECT_THAT(exported.value().mtl, HasSubstr("newmtl mat.gone\nKd 0.7843 0.7843 0.7843\n"));
    ASSERT_EQ(exported.value().diagnostics.size(), 2u);
    EXPECT_EQ(exported.value().diagnostics[0].code, "formats.obj.material_missing");
    EXPECT_EQ(exported.value().diagnostics[0].severity, core::Severity::Warning);
    EXPECT_EQ(exported.value().diagnostics[0].entityId, "cab1/shelf.1");
    EXPECT_THAT(exported.value().diagnostics[1].message, HasSubstr("refers to material \"mat.gone\""));
}

TEST(ObjExport, InvalidPartsAndOptionsAreErrors) {
    domain::Model flat = domain::fixtures::basicCabinet();
    flat.assemblies[0].parts[3].thickness = 0;
    auto r = exportObj(flat);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_THAT(r.error().message, HasSubstr("part 'Top' (cab1/top) has the size 764 x 560 x 0 mm"));

    domain::Model skewed = domain::fixtures::basicCabinet();
    skewed.assemblies[0].parts[0].placement.yAxis = {0, 2, 0};
    EXPECT_THAT(exportObj(skewed).error().message, HasSubstr("placement of part 'Left side' (cab1/side.left) is not a rigid"));

    domain::Model mirrored = domain::fixtures::basicCabinet();
    mirrored.assemblies[0].placement.zAxis = {0, 0, -1};
    EXPECT_THAT(exportObj(mirrored).error().message, HasSubstr("assembly 'Base cabinet 800' (cab1)"));

    ObjExportOptions options;
    options.mtlFileName = "my materials.mtl";
    EXPECT_EQ(exportObj(domain::fixtures::basicCabinet(), options).error().code, core::ErrorCode::InvalidArgument);
    // An empty model is a valid (empty) export.
    auto empty = exportObj(domain::Model{});
    ASSERT_TRUE(empty.ok());
    EXPECT_TRUE(parseObj(empty.value().obj).objects.empty());
}

TEST(ObjExport, WritesObjAndCompanionMtlFiles) {
    formats::test::TempDir dir;
    const auto objPath = dir / "my cabinet.obj";
    auto written = writeObjFiles(objPath, domain::fixtures::basicCabinet());
    ASSERT_TRUE(written.ok()) << written.error().message;
    EXPECT_EQ(formats::test::readText(objPath), written.value().obj);
    EXPECT_EQ(formats::test::readText(dir / "my_cabinet.mtl"), written.value().mtl);
    EXPECT_EQ(parseObj(written.value().obj).mtllib, "my_cabinet.mtl");
    EXPECT_FALSE(writeObjFiles(dir.path() / "missing" / "a.obj", domain::fixtures::basicCabinet()).ok());
}
