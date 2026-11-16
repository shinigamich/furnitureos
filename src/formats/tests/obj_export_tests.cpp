// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/domain/fixtures.hpp"
#include "fos/formats/obj_export.hpp"
#include "obj_parse.hpp"
#include "test_support.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <clocale>
#include <map>
#include <sstream>

using namespace fos;
using namespace fos::formats;
using geometry::Vec3;
using ::testing::HasSubstr;

using namespace fos::formats::test;

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

TEST(ObjExport, NumbersIgnoreTheCLocale) {
    // The GUI sets LC_NUMERIC from the environment; a decimal-comma locale must not leak
    // into the files ("Kd 0,8706" is not valid MTL).
    const std::string previous = std::setlocale(LC_NUMERIC, nullptr);
    const char* chosen = nullptr;
    for (const char* name : {"uk_UA.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8", "de_DE", "German_Germany.1252", "German", "Ukrainian"})
        if (std::setlocale(LC_NUMERIC, name) && std::localeconv()->decimal_point[0] == ',') {
            chosen = name;
            break;
        }
    if (!chosen) {
        std::setlocale(LC_NUMERIC, previous.c_str());
        GTEST_SKIP() << "no decimal-comma locale is installed on this system";
    }
    auto exported = exportObj(domain::fixtures::basicCabinet(), {});
    std::setlocale(LC_NUMERIC, previous.c_str());
    ASSERT_TRUE(exported.ok());
    EXPECT_THAT(exported.value().mtl, HasSubstr("Kd 0.8706 0.8000 0.6667\n")) << "locale " << chosen;
    for (const std::string* text : {&exported.value().obj, &exported.value().mtl}) {
        std::istringstream lines(*text);
        for (std::string line; std::getline(lines, line);)
            if (line.rfind("v ", 0) == 0 || line.rfind("vn ", 0) == 0 || line.rfind("Kd ", 0) == 0) {
                EXPECT_EQ(line.find(','), std::string::npos) << line;
            }
    }
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
