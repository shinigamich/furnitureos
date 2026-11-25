// SPDX-License-Identifier: AGPL-3.0-only
/// Check 8 (engineering heuristics) with hand-computed numbers. g = 9.81 N/kg throughout.
#include "test_support.hpp"

namespace fos::validation::test {
namespace {
using domain::HardwareDefinition;
using domain::HardwareId;
using domain::HardwareInstance;
using domain::HardwareInstanceId;
using domain::HardwareKind;
using domain::PartRole;
using geometry::Transform;
using geometry::Vec3;

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

/// Only the engineering check enabled.
ValidationOptions engineeringOnly() {
    ValidationOptions o;
    o.checkParts = o.checkEdgeBanding = o.checkPartInterference = o.checkAssemblyInterference = false;
    o.checkMachining = o.checkHardware = o.checkCompleteness = false;
    o.checkEngineering = true;
    return o;
}

void expectNoErrors(const ValidationReport& r) {
    for (const auto& f : r.diagnostics) EXPECT_NE(f.severity, core::Severity::Error) << toText(r);
}

domain::Part withRole(domain::Part p, PartRole role) {
    p.role = role;
    return p;
}

// Local X -> assembly Z (upright panel, length = height), local Y -> assembly X.
Transform uprightAcross(Vec3 origin) { return Transform::fromAxes({0, 0, 1}, {1, 0, 0}, origin); }
// Local X -> Z, local Y -> Y (cabinet side: length = height, width = depth).
Transform uprightDepth(Vec3 origin) { return Transform::fromAxes({0, 0, 1}, {0, 1, 0}, origin); }

HardwareDefinition definition(const std::string& id, HardwareKind kind) {
    HardwareDefinition d;
    d.id = HardwareId(id);
    d.kind = kind;
    d.name = id;
    return d;
}

HardwareInstance instance(const std::string& id, const std::string& def, int qty, std::vector<std::string> parts,
                          std::string note = {}) {
    HardwareInstance h;
    h.id = HardwareInstanceId(id);
    h.definition = HardwareId(def);
    h.quantity = qty;
    for (const auto& p : parts) h.parts.push_back(domain::PartId(p));
    h.note = std::move(note);
    return h;
}

TEST(Engineering, OffByDefaultAndFixtureCleanWhenOn) {
    const domain::Model m = domain::fixtures::basicCabinet();
    EXPECT_EQ(validateModel(m).stats.checksRun, 7u);
    ValidationOptions o;
    o.checkEngineering = true;
    const ValidationReport r = validateModel(m, o);
    EXPECT_EQ(r.stats.checksRun, 8u);
    // Fixture shelf 764 x 540 x 18 particleboard: self weight 0.764*0.54*0.018*650 = 4.827 kg,
    // W = 19.827 kg * 9.81 = 194.5 N, I = 540*18^3/12 = 262440 mm^4,
    // delta = 5*194.5*764^3 / (384*2500*262440) = 1.72 mm < min(764/300, 2) = 2 mm -> no warning.
    EXPECT_EQ(r.counts.warnings, 0u) << toText(r);
    EXPECT_EQ(availableChecks().back().name, "engineering");
}

TEST(Engineering, ShelfSagHandComputed) {
    domain::Model m = libraryModel({withRole(board("s", 800, 300), PartRole::Shelf)});
    ValidationOptions o = engineeringOnly();
    o.engineering.shelfSelfWeight = false;
    // W = 15 * 9.81 = 147.15 N, I = 300 * 18^3 / 12 = 145800 mm^4, E = 2500 MPa (particleboard)
    // delta = 5 * 147.15 * 800^3 / (384 * 2500 * 145800) = 3.76704e11 / 1.39968e11 = 2.69 mm
    // limit = min(800 / 300 = 2.67, 2) = 2 mm -> warning.
    ValidationReport r = validateModel(m, o);
    Finding f = only(r, "validation.engineering.shelf_sag");
    EXPECT_EQ(f.severity, core::Severity::Warning);
    EXPECT_TRUE(has(f.message, "sag 2.69 mm")) << f.message;
    EXPECT_TRUE(has(f.message, "limit of 2.00 mm")) << f.message;
    EXPECT_TRUE(has(f.message, "E = 2500 MPa")) << f.message;
    EXPECT_EQ(f.partId, "s");

    // Self weight on: 0.8 * 0.3 * 0.018 * 650 = 2.808 kg -> 2.69 * 17.808 / 15 = 3.20 mm.
    o.engineering.shelfSelfWeight = true;
    f = only(validateModel(m, o), "validation.engineering.shelf_sag");
    EXPECT_TRUE(has(f.message, "sag 3.20 mm")) << f.message;
    EXPECT_TRUE(has(f.message, "2.8 kg self weight")) << f.message;
    o.engineering.shelfSelfWeight = false;

    // Absolute limit 3 mm only: 2.69 < 3 -> clean. Ratio only (span/250 = 3.2): clean.
    o.engineering.sagLimitSpanRatio = 0;
    o.engineering.sagLimitMm = 3;
    EXPECT_EQ(count(validateModel(m, o), "validation.engineering.shelf_sag"), 0u);
    o.engineering.sagLimitSpanRatio = 250;
    o.engineering.sagLimitMm = 0;
    EXPECT_EQ(count(validateModel(m, o), "validation.engineering.shelf_sag"), 0u);
    o.engineering = {};
    o.engineering.shelfSelfWeight = false;

    // Material override E = 4000 MPa: 2.69 * 2500 / 4000 = 1.68 mm < 2 -> clean.
    m.materials[0].custom["elastic_modulus_mpa"] = "4000";
    ASSERT_EQ(m.materials[0].id.str(), "mat.pb18");
    EXPECT_EQ(count(validateModel(m, o), "validation.engineering.shelf_sag"), 0u);
    // Override E = 2000: 2.69 * 2500 / 2000 = 3.36 mm.
    m.materials[0].custom["elastic_modulus_mpa"] = "2000";
    f = only(validateModel(m, o), "validation.engineering.shelf_sag");
    EXPECT_TRUE(has(f.message, "sag 3.36 mm")) << f.message;
    EXPECT_TRUE(has(f.message, "custom elastic_modulus_mpa")) << f.message;

    // Rule switched off.
    o.engineering.shelfSag = false;
    EXPECT_TRUE(validateModel(m, o).diagnostics.empty());
}

TEST(Engineering, ShelfSagUnknownOrInvalidModulusIsInfo) {
    domain::Model m = libraryModel({withRole(board("s", 800, 300), PartRole::Shelf)});
    m.materials[0].custom["elastic_modulus_mpa"] = "stiff";
    Finding f = only(validateModel(m, engineeringOnly()), "validation.engineering.invalid_custom_value");
    EXPECT_EQ(f.severity, core::Severity::Info);
    m.materials[0].custom.clear();
    m.materials[0].kind = domain::MaterialKind::Other;
    f = only(validateModel(m, engineeringOnly()), "validation.engineering.unknown_modulus");
    EXPECT_EQ(f.severity, core::Severity::Info);
}

TEST(Engineering, HingeCountByHeight) {
    domain::Model m = libraryModel({withRole(board("door", 2000, 600, uprightAcross({0, -18, 0})), PartRole::Door),
                                    withRole(board("side", 2000, 500, uprightDepth({18, 0, 0})), PartRole::Side)});
    m.hardwareCatalog.push_back(definition("hw.hinge", HardwareKind::Hinge));
    m.assemblies[0].hardware.push_back(instance("h1", "hw.hinge", 3, {"door", "side"}));
    ValidationOptions o = engineeringOnly();
    o.engineering.tipping = false;
    // Height 2000 -> table row <= 2000: 4 hinges. Weight 2.0*0.6*0.018*650 = 14.04 kg -> ceil(14.04/5) = 3.
    // Required max(4, 3) = 4 > 3 present -> warning.
    Finding f = only(validateModel(m, o), "validation.engineering.too_few_hinges");
    EXPECT_EQ(f.severity, core::Severity::Warning);
    EXPECT_TRUE(has(f.message, "hangs on 3 hinge(s) but about 4")) << f.message;
    EXPECT_TRUE(has(f.message, "14.0 kg")) << f.message;
    EXPECT_TRUE(has(f.message, "Rule of thumb")) << f.message;
    m.assemblies[0].hardware[0].quantity = 4;
    EXPECT_EQ(count(validateModel(m, o), "validation.engineering.too_few_hinges"), 0u);
    // Two instances add up (2 + 2 = 4): clean.
    m.assemblies[0].hardware[0].quantity = 2;
    m.assemblies[0].hardware.push_back(instance("h2", "hw.hinge", 2, {"door", "side"}));
    EXPECT_EQ(count(validateModel(m, o), "validation.engineering.too_few_hinges"), 0u);
    // A door without hinges (sliding) is not judged.
    m.assemblies[0].hardware.clear();
    EXPECT_TRUE(validateModel(m, o).diagnostics.empty());
}

TEST(Engineering, HingeCountByWeightAndTableEnd) {
    domain::Material mdf = domain::fixtures::basicCabinet().materials[0];
    mdf.id = domain::MaterialId("mat.mdf18");
    mdf.kind = domain::MaterialKind::Mdf;
    mdf.density = 0;
    domain::Model m = libraryModel(
        {withRole(board("door", 1000, 1000, uprightAcross({0, -18, 0}), "mat.mdf18"), PartRole::Door),
         withRole(board("side", 1000, 500, uprightDepth({18, 0, 0})), PartRole::Side)});
    m.materials.push_back(mdf);
    m.hardwareCatalog.push_back(definition("hw.hinge", HardwareKind::Hinge));
    m.assemblies[0].hardware.push_back(instance("h1", "hw.hinge", 3, {"door", "side"}));
    ValidationOptions o = engineeringOnly();
    o.engineering.tipping = false;
    // Height 1000 -> 3; weight 1*1*0.018*750 = 13.5 kg: at 5 kg/hinge ceil(2.7) = 3 -> clean.
    EXPECT_EQ(count(validateModel(m, o), "validation.engineering.too_few_hinges"), 0u);
    // At 4 kg/hinge ceil(3.375) = 4 > 3 -> warning (weight governs).
    o.engineering.doorKgPerHinge = 4;
    Finding f = only(validateModel(m, o), "validation.engineering.too_few_hinges");
    EXPECT_TRUE(has(f.message, "13.5 kg (750 kg/m3, generic Mdf density)")) << f.message;
    EXPECT_TRUE(has(f.message, "about 4")) << f.message;
    o.engineering.hingeCount = false;
    EXPECT_EQ(count(validateModel(m, o), "validation.engineering.too_few_hinges"), 0u);
    // Taller than the table (2600 > 2400): info, weight term still applies.
    o.engineering = {};
    o.engineering.tipping = false;
    part(m, "door").length = 2600;
    const ValidationReport r = validateModel(m, o);
    EXPECT_EQ(only(r, "validation.engineering.hinge_table_exceeded").severity, core::Severity::Info);
    expectNoErrors(r);
}

TEST(Engineering, TopHingedFlapUsesWidth) {
    // Flap 1000 (X) x 400, carried by a horizontal top panel (identity placement, normal Z).
    domain::Model m = libraryModel({withRole(board("flap", 1000, 400, Transform::translation({0, 0, 500})),
                                             PartRole::Door),
                                    withRole(board("top", 1000, 500, Transform::translation({0, 0, 600})),
                                             PartRole::Top)});
    m.hardwareCatalog.push_back(definition("hw.hinge", HardwareKind::Hinge));
    m.assemblies[0].hardware.push_back(instance("h1", "hw.hinge", 2, {"flap", "top"}));
    // Width 1000 -> row <= 1600: 3; weight 1*0.4*0.018*650 = 4.68 kg -> 2. Required 3 > 2.
    Finding f = only(validateModel(m, engineeringOnly()), "validation.engineering.too_few_hinges");
    EXPECT_TRUE(has(f.message, "width (top-hinged flap) 1000 mm")) << f.message;
}

domain::Model drawerCabinet(double drawerLength, const std::string& slideNote) {
    // Side y 0..560; back panel front face at y = 550 -> interior depth 550.
    domain::Model m = libraryModel(
        {withRole(board("side", 720, 560, uprightDepth({18, 0, 0})), PartRole::Side),
         withRole(board("back", 800, 700, Transform::fromAxes({1, 0, 0}, {0, 0, 1}, {0, 553, 0}), "mat.hdf3", 3),
                  PartRole::Back),
         withRole(board("dside", drawerLength, 150, Transform::fromAxes({0, 1, 0}, {0, 0, 1}, {30, 20, 100}), "mat.pb18",
                        16),
                  PartRole::DrawerSide)});
    m.hardwareCatalog.push_back(definition("hw.slide", HardwareKind::DrawerSlide));
    m.assemblies[0].hardware.push_back(instance("sl1", "hw.slide", 1, {"side", "dside"}, slideNote));
    return m;
}

TEST(Engineering, DrawerDepthAndSlide) {
    ValidationOptions o = engineeringOnly();
    // Box 500, slide 500, interior 550: clean.
    EXPECT_TRUE(validateModel(drawerCabinet(500, "slide length 500.0 mm"), o).diagnostics.empty());
    // Slide 600 > interior 550 -> slide_too_long (box 500 <= 600, so not longer than the slide).
    ValidationReport r = validateModel(drawerCabinet(500, "slide length 600.0 mm"), o);
    Finding f = only(r, "validation.engineering.slide_too_long");
    EXPECT_EQ(f.severity, core::Severity::Warning);
    EXPECT_TRUE(has(f.message, "600 mm long, more than the interior depth 550 mm")) << f.message;
    EXPECT_TRUE(has(f.message, "the back panel")) << f.message;
    EXPECT_EQ(r.diagnostics.size(), 1u) << toText(r);
    // Box 560 > interior 550 and > slide 500.
    r = validateModel(drawerCabinet(560, "slide length 500 mm"), o);
    EXPECT_TRUE(has(only(r, "validation.engineering.drawer_too_deep").message, "560 mm long"));
    EXPECT_TRUE(has(only(r, "validation.engineering.drawer_longer_than_slide").message, "(500 mm)"));
    // Rear clearance 60: 500 + 60 > 550 -> drawer_too_deep.
    o.engineering.drawerMinRearClearance = 60;
    f = only(validateModel(drawerCabinet(500, "slide length 500 mm"), o), "validation.engineering.drawer_too_deep");
    EXPECT_TRUE(has(f.message, "leaves only 490 mm")) << f.message;
    o.engineering.drawerMinRearClearance = 0;
    // Fixed-length definition param wins over the note: 450 < box 500.
    domain::Model m = drawerCabinet(500, "");
    m.hardwareCatalog.back().params["length"] = 450;
    EXPECT_EQ(count(validateModel(m, o), "validation.engineering.drawer_longer_than_slide"), 1u);
    // Unknown slide length: slide rules skipped. Rule switched off: nothing.
    EXPECT_TRUE(validateModel(drawerCabinet(500, "no length here"), o).diagnostics.empty());
    o.engineering.drawerDepth = false;
    EXPECT_TRUE(validateModel(drawerCabinet(560, "slide length 500 mm"), o).diagnostics.empty());
}

TEST(Engineering, TallUnitTipping) {
    // 2000 tall, 500 deep: 2000 / 500 = 4.00 > 2 and >= 1000 mm -> warning.
    domain::Model m = libraryModel({withRole(board("side", 2000, 500, uprightDepth({18, 0, 0})), PartRole::Side)});
    ValidationOptions o = engineeringOnly();
    Finding f = only(validateModel(m, o), "validation.engineering.tipping_risk");
    EXPECT_EQ(f.severity, core::Severity::Warning);
    EXPECT_EQ(f.entityId, "a1");
    EXPECT_TRUE(has(f.message, "2000 mm tall and 500 mm deep (height/depth 4.00 > 2.00)")) << f.message;
    // A cabinet hanger (Suspension) counts as wall fixing.
    m.hardwareCatalog.push_back(definition("hw.hanger", HardwareKind::Suspension));
    m.assemblies[0].hardware.push_back(instance("hg", "hw.hanger", 2, {"side"}));
    EXPECT_TRUE(validateModel(m, o).diagnostics.empty());
    // So does any definition marked custom wall_fixing=true.
    m.hardwareCatalog.back().kind = HardwareKind::Other;
    EXPECT_EQ(count(validateModel(m, o), "validation.engineering.tipping_risk"), 1u);
    m.hardwareCatalog.back().custom["wall_fixing"] = "true";
    EXPECT_TRUE(validateModel(m, o).diagnostics.empty());
    // Low unit (900 tall, 300 deep, ratio 3) below the 1000 mm minimum height: clean.
    domain::Model low = libraryModel({withRole(board("side", 900, 300, uprightDepth({18, 0, 0})), PartRole::Side)});
    EXPECT_TRUE(validateModel(low, o).diagnostics.empty());
    o.engineering.tippingMinHeight = 800;
    EXPECT_EQ(count(validateModel(low, o), "validation.engineering.tipping_risk"), 1u);
    o.engineering.tipping = false;
    EXPECT_TRUE(validateModel(low, o).diagnostics.empty());
}

} // namespace
} // namespace fos::validation::test
