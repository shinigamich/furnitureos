// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/compatibility.hpp"
#include "fos/hardware/corner_joint.hpp"
#include "fos/hardware/hardware_list.hpp"
#include "test_util.hpp"

using namespace fos;
using namespace fos::hardware;
using namespace fos::hardware::testing;
using core::Severity;
using domain::Face;
using domain::HardwareKind;
using geometry::Transform;

namespace {
int count(const core::Diagnostics& ds, const std::string& code) {
    int n = 0;
    for (const auto& d : ds) n += d.code == code;
    return n;
}
bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
domain::Part panel(const char* name, double l, double w, double t) {
    return makePart(name, name, l, w, t, Transform::identity());
}
} // namespace

TEST(Catalog, EveryEntryIsGenericAndUnverified) {
    const auto cat = builtinCatalog();
    ASSERT_EQ(cat.size(), 7u);
    const char* expectedIds[] = {"hw.hinge_cup_35", "hw.confirmat_7x50", "hw.dowel_8x30", "hw.shelf_pin_5",
                                 "hw.cam_15",       "hw.handle_bar_128", "hw.foot_adjustable"};
    for (std::size_t i = 0; i < cat.size(); ++i) {
        const auto& d = cat[i];
        EXPECT_EQ(d.id.str(), expectedIds[i]);
        EXPECT_FALSE(d.dimensionsVerified) << d.id.str();
        EXPECT_TRUE(d.manufacturer.empty()) << d.id.str();
        EXPECT_TRUE(d.article.empty()) << d.id.str();
        EXPECT_TRUE(endsWith(d.name, "(generic)")) << d.name;
        EXPECT_DOUBLE_EQ(d.unitCost, 0.0);
        // Only the "unverified" info, no parameter problems.
        const auto ds = checkDefinition(d);
        ASSERT_EQ(ds.size(), 1u) << d.id.str();
        EXPECT_EQ(ds[0].code, "hardware.definition.unverified");
        EXPECT_EQ(ds[0].severity, Severity::Info);
        for (const auto& key : requiredParams(d.kind)) EXPECT_TRUE(d.params.count(key)) << d.id.str() << " " << key;
    }
}

TEST(Catalog, ParameterValues) {
    const auto hinge = def(ids::kHingeCup35);
    EXPECT_EQ(hinge.kind, HardwareKind::Hinge);
    EXPECT_DOUBLE_EQ(hinge.params.at("cup_diameter"), 35);
    EXPECT_DOUBLE_EQ(hinge.params.at("cup_depth"), 13);
    EXPECT_DOUBLE_EQ(hinge.params.at("cup_center_from_door_edge"), 21.5);
    EXPECT_DOUBLE_EQ(hinge.params.at("plate_line_from_front_edge"), 37);
    EXPECT_DOUBLE_EQ(hinge.params.at("plate_hole_spacing"), 32);
    EXPECT_DOUBLE_EQ(hinge.params.at("plate_hole_diameter"), 5);
    EXPECT_DOUBLE_EQ(hinge.params.at("plate_hole_depth"), 13);
    const auto confirmat = def(ids::kConfirmat7x50);
    EXPECT_EQ(confirmat.kind, HardwareKind::Confirmat);
    EXPECT_DOUBLE_EQ(confirmat.params.at("length"), 50);
    EXPECT_DOUBLE_EQ(confirmat.params.at("through_diameter"), 7);
    EXPECT_DOUBLE_EQ(confirmat.params.at("pilot_diameter"), 5);
    EXPECT_DOUBLE_EQ(confirmat.params.at("pilot_extra_depth"), 5);
    const auto dowel = def(ids::kDowel8x30);
    EXPECT_EQ(dowel.kind, HardwareKind::Dowel);
    EXPECT_DOUBLE_EQ(dowel.params.at("diameter"), 8);
    EXPECT_DOUBLE_EQ(dowel.params.at("length"), 30);
    EXPECT_DOUBLE_EQ(dowel.params.at("face_depth"), 12);
    EXPECT_DOUBLE_EQ(dowel.params.at("edge_depth"), 20);
    const auto pin = def(ids::kShelfPin5);
    EXPECT_EQ(pin.kind, HardwareKind::ShelfSupport);
    EXPECT_DOUBLE_EQ(pin.params.at("hole_diameter"), 5);
    EXPECT_DOUBLE_EQ(pin.params.at("hole_depth"), 13);
    const auto cam = def(ids::kCam15);
    EXPECT_EQ(cam.kind, HardwareKind::CamConnector);
    EXPECT_DOUBLE_EQ(cam.params.at("cam_diameter"), 15);
    EXPECT_DOUBLE_EQ(cam.params.at("bolt_hole_diameter"), 8);
    const auto handle = def(ids::kHandleBar128);
    EXPECT_EQ(handle.kind, HardwareKind::Handle);
    EXPECT_DOUBLE_EQ(handle.params.at("hole_spacing"), 128);
    EXPECT_DOUBLE_EQ(handle.params.at("hole_diameter"), 5);
    EXPECT_EQ(def(ids::kFootAdjustable).kind, HardwareKind::Foot);
    // The generic pin uses the same id and keys as the pin of the reference fixture.
    const auto fixturePin = domain::fixtures::basicCabinet().hardwareCatalog[0];
    EXPECT_EQ(pin.id, fixturePin.id);
    EXPECT_EQ(pin.params, fixturePin.params);
}

TEST(Catalog, LookupAndRequireParam) {
    const auto missing = builtinDefinition("hw.does_not_exist");
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, core::ErrorCode::NotFound);
    EXPECT_NE(missing.error().message.find("hw.does_not_exist"), std::string::npos);

    auto d = def(ids::kConfirmat7x50);
    EXPECT_DOUBLE_EQ(requireParam(d, "length").value(), 50);
    auto r = requireParam(d, "no_such_key");
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("'no_such_key'"), std::string::npos);
    EXPECT_NE(r.error().message.find("hw.confirmat_7x50"), std::string::npos);
    d.params["pilot_extra_depth"] = 0;
    EXPECT_FALSE(requireParam(d, "pilot_extra_depth").ok());
    EXPECT_TRUE(requireParam(d, "pilot_extra_depth", true).ok());
    d.params["length"] = -1;
    EXPECT_FALSE(requireParam(d, "length", true).ok());

    // checkDefinition: zero extra depth is allowed, a negative length and a missing key are not.
    d.params.erase("pilot_diameter");
    const auto ds = checkDefinition(d);
    EXPECT_EQ(count(ds, "hardware.definition.invalid_param"), 1);
    EXPECT_EQ(count(ds, "hardware.definition.missing_param"), 1);
    EXPECT_EQ(count(ds, "hardware.definition.unverified"), 1);
    // A verified definition (user library entry) gets no info.
    auto verified = def(ids::kDowel8x30);
    verified.dimensionsVerified = true;
    EXPECT_TRUE(checkDefinition(verified).empty());
}

TEST(Compatibility, Confirmat) {
    const auto confirmat = def(ids::kConfirmat7x50);
    const auto side = panel("Side", 720, 560, 18);
    const auto bottom = panel("Bottom", 764, 560, 18);
    EXPECT_TRUE(checkConfirmatFit(confirmat, side, bottom, Face::W1).empty());
    // 50 mm screw through a 50 mm panel never reaches the other part.
    auto ds = checkConfirmatFit(confirmat, panel("Block", 720, 560, 50), bottom, Face::W1);
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "hardware.confirmat.too_short");
    EXPECT_EQ(ds[0].severity, Severity::Error);
    EXPECT_EQ(ds[0].entityId, "Block");
    // Pilot depth 50 - 18 + 5 = 37 mm into the L1 edge of a 30 mm wide strip: too deep.
    ds = checkConfirmatFit(confirmat, side, panel("Strip", 764, 30, 18), Face::L1);
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "hardware.confirmat.pilot_too_deep");
    EXPECT_NE(ds[0].message.find("37 mm deep pilot hole"), std::string::npos) << ds[0].message;
    EXPECT_NE(ds[0].message.find("30 mm behind edge L1"), std::string::npos) << ds[0].message;
    // ... but along its length (W1, 764 mm behind the edge) it fits.
    EXPECT_TRUE(checkConfirmatFit(confirmat, side, panel("Strip", 764, 30, 18), Face::W1).empty());
    // 5 mm pilot + 2 x 3 mm walls = 11 mm > 10 mm panel.
    ds = checkConfirmatFit(confirmat, side, panel("Thin", 764, 560, 10), Face::W1);
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "hardware.confirmat.panel_too_thin");
    EXPECT_TRUE(checkConfirmatFit(confirmat, side, panel("Thin", 764, 560, 11), Face::W1).empty());
    // Wrong kind.
    EXPECT_EQ(count(checkConfirmatFit(def(ids::kDowel8x30), side, bottom, Face::W1), "hardware.definition.wrong_kind"), 1);
}

TEST(Compatibility, DowelHingeShelfPin) {
    const auto dowel = def(ids::kDowel8x30);
    const auto side = panel("Side", 720, 560, 18);
    const auto bottom = panel("Bottom", 764, 560, 18);
    EXPECT_TRUE(checkDowelFit(dowel, side, bottom, Face::W1).empty());
    // 12 mm deep hole in a 14 mm panel leaves exactly 2 mm: not MORE than 2 -> error; 14.5 is fine.
    EXPECT_EQ(count(checkDowelFit(dowel, panel("S14", 720, 560, 14), bottom, Face::W1), "hardware.dowel.face_hole_too_deep"), 1);
    EXPECT_TRUE(checkDowelFit(dowel, panel("S14", 720, 560, 14.5), bottom, Face::W1).empty());
    // 8 mm + 2 x 3 mm = 14 mm > 12 mm edge panel.
    EXPECT_EQ(count(checkDowelFit(dowel, side, panel("B12", 764, 560, 12), Face::W1), "hardware.dowel.panel_too_thin"), 1);
    // 20 mm edge hole into a 15 mm wide strip.
    EXPECT_EQ(count(checkDowelFit(dowel, side, panel("Strip", 764, 15, 18), Face::L2), "hardware.dowel.edge_hole_too_deep"), 1);
    // Holes 5 + 20 = 25 mm for a 30 mm dowel.
    auto shallow = dowel;
    shallow.params["face_depth"] = 5;
    const auto ds = checkDowelFit(shallow, side, bottom, Face::W1);
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "hardware.dowel.holes_too_shallow");
    EXPECT_NE(ds[0].message.find("only 25 mm"), std::string::npos) << ds[0].message;

    const auto hinge = def(ids::kHingeCup35);
    EXPECT_TRUE(checkHingeFit(hinge, panel("Door", 716, 396, 18), side).empty());
    EXPECT_TRUE(checkHingeFit(hinge, panel("Door", 716, 396, 16), side).empty()); // 13 < 16 - 2
    EXPECT_EQ(count(checkHingeFit(hinge, panel("Door", 716, 396, 15), side), "hardware.hinge.cup_too_deep"), 1); // 13 >= 13
    EXPECT_EQ(count(checkHingeFit(hinge, panel("Door", 716, 396, 18), panel("S12", 720, 560, 12)),
                    "hardware.hinge.plate_hole_too_deep"), 1); // 13 >= 12 - 2
    FitOptions o;
    o.minRemainingMaterial = 6; // 13 >= 18 - 6 = 12
    EXPECT_EQ(count(checkHingeFit(hinge, panel("Door", 716, 396, 18), side, o), "hardware.hinge.cup_too_deep"), 1);

    const auto pin = def(ids::kShelfPin5);
    EXPECT_TRUE(checkShelfPinFit(pin, side).empty());
    EXPECT_TRUE(checkShelfPinFit(pin, panel("S16", 720, 560, 16)).empty());                              // 13 < 14
    EXPECT_EQ(count(checkShelfPinFit(pin, panel("S15", 720, 560, 15)), "hardware.shelf_pin.hole_too_deep"), 1); // 13 >= 13
}

TEST(HardwareList, FixtureAndAggregation) {
    Cabinet c;
    // Fixture: 4 shelf pins at 0.05 each = 0.20.
    auto rows = hardwareList(c.model);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].definition.str(), "hw.shelf_pin_5");
    EXPECT_TRUE(rows[0].known);
    EXPECT_EQ(rows[0].name, "Shelf support pin 5 mm (generic)");
    EXPECT_EQ(rows[0].kind, HardwareKind::ShelfSupport);
    EXPECT_FALSE(rows[0].dimensionsVerified);
    EXPECT_EQ(rows[0].quantity, 4);
    EXPECT_DOUBLE_EQ(rows[0].unitCost, 0.05);
    EXPECT_NEAR(rows[0].totalCost, 0.20, 1e-12);
    EXPECT_NEAR(totalHardwareCost(rows), 0.20, 1e-12);

    // Add two confirmat joints (3 screws each) at 0.04, a second assembly with 8 more pins,
    // an instance of an unknown definition and one with quantity 0 (ignored).
    auto confirmat = def(ids::kConfirmat7x50);
    confirmat.unitCost = 0.04;
    c.model.hardwareCatalog.push_back(confirmat);
    for (const char* id : {"cab1/hw.c.bl", "cab1/hw.c.tr"}) {
        CornerJointConfig cfg;
        cfg.instanceId = domain::HardwareInstanceId(id);
        const bool bl = std::string(id) == "cab1/hw.c.bl";
        const auto r = connectCorner(bl ? c.left : c.right, bl ? c.bottom : c.top, confirmat, cfg);
        ASSERT_TRUE(r.ok());
        ASSERT_TRUE(applyPlacement(c.a, r.value()).ok());
    }
    domain::Assembly second;
    second.id = domain::AssemblyId("cab2");
    domain::HardwareInstance morePins;
    morePins.id = domain::HardwareInstanceId("cab2/hw.pins");
    morePins.definition = domain::HardwareId("hw.shelf_pin_5");
    morePins.quantity = 8;
    domain::HardwareInstance unknown;
    unknown.id = domain::HardwareInstanceId("cab2/hw.x");
    unknown.definition = domain::HardwareId("hw.unknown");
    unknown.quantity = 2;
    domain::HardwareInstance zero = unknown;
    zero.definition = domain::HardwareId("hw.aaa_zero");
    zero.quantity = 0;
    second.hardware = {unknown, morePins, zero};
    c.model.assemblies.push_back(second);

    rows = hardwareList(c.model);
    ASSERT_EQ(rows.size(), 3u); // sorted by definition id
    EXPECT_EQ(rows[0].definition.str(), "hw.confirmat_7x50");
    EXPECT_EQ(rows[0].quantity, 6);
    EXPECT_NEAR(rows[0].totalCost, 0.24, 1e-12); // 6 * 0.04
    EXPECT_EQ(rows[1].definition.str(), "hw.shelf_pin_5");
    EXPECT_EQ(rows[1].quantity, 12);
    EXPECT_NEAR(rows[1].totalCost, 0.60, 1e-12); // 12 * 0.05
    EXPECT_EQ(rows[2].definition.str(), "hw.unknown");
    EXPECT_FALSE(rows[2].known);
    EXPECT_EQ(rows[2].quantity, 2);
    EXPECT_DOUBLE_EQ(rows[2].totalCost, 0.0);
    EXPECT_NEAR(totalHardwareCost(rows), 0.84, 1e-12);
    EXPECT_TRUE(hardwareList(domain::Model{}).empty());
}
