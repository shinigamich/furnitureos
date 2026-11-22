// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/csv.hpp"
#include "fos/production/packing.hpp"
#include "test_models.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace fos::production {
namespace {

bool contains(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

bool hasCode(const core::Diagnostics& ds, const std::string& code) {
    for (const auto& d : ds)
        if (d.code == code) return true;
    return false;
}

std::vector<int> numbers(const Package& p) {
    std::vector<int> out;
    for (const auto& part : p.parts) out.push_back(part.partNumber);
    return out;
}

/// basicCabinet with densities: particleboard 650 kg/m3, HDF 800 kg/m3.
/// Panel weights (area m2 x thickness m x density):
///   #1/#2 side   0.720*0.560 = 0.4032   * 0.018 * 650 = 4.71744 kg
///   #3/#4 bottom/top 0.764*0.560 = 0.42784 * 0.018 * 650 = 5.005728 kg
///   #5 shelf     0.764*0.540 = 0.41256  * 0.018 * 650 = 4.826952 kg
///   #6 back      0.796*0.716 = 0.569936 * 0.003 * 800 = 1.3678464 kg
///   total 25.6411344 kg
domain::Model weighedCabinet() {
    auto m = domain::fixtures::basicCabinet();
    m.materials[0].density = 650;
    m.materials[1].density = 800;
    return m;
}

TEST(Packing, FirstFitByWeightWithHandComputedSizesAndWeights) {
    const auto r = packingList(weighedCabinet()); // 25 kg, padding 10
    ASSERT_TRUE(r) << r.error().message;
    const auto& list = r.value();
    ASSERT_EQ(list.assemblies.size(), 1u);
    const auto& a = list.assemblies[0];
    // Order by long side: back 796, bottom/top 764x560, shelf 764x540, sides 720.
    // Running sum: 1.368 + 5.006 + 5.006 + 4.827 + 4.717 = 20.924; + 4.717 = 25.64 > 25 -> new package.
    ASSERT_EQ(a.packages.size(), 2u);
    EXPECT_EQ(numbers(a.packages[0]), (std::vector<int>{6, 3, 4, 5, 1}));
    EXPECT_EQ(numbers(a.packages[1]), (std::vector<int>{2}));
    ASSERT_TRUE(a.packages[0].weightKg);
    EXPECT_NEAR(*a.packages[0].weightKg, 25.6411344 - 4.71744, 1e-9);
    EXPECT_NEAR(*a.packages[1].weightKg, 4.71744, 1e-9);
    // P1: 796 + 20 = 816, 716 + 20 = 736, 3 + 4*18 + 20 = 95.   P2: 740 x 580 x 38.
    EXPECT_DOUBLE_EQ(a.packages[0].length, 816);
    EXPECT_DOUBLE_EQ(a.packages[0].width, 736);
    EXPECT_DOUBLE_EQ(a.packages[0].height, 95);
    EXPECT_DOUBLE_EQ(a.packages[1].length, 740);
    EXPECT_DOUBLE_EQ(a.packages[1].width, 580);
    EXPECT_DOUBLE_EQ(a.packages[1].height, 38);
    EXPECT_EQ(a.packages[0].id, "cab1-P1");
    EXPECT_EQ(a.packages[1].number, 2);
    EXPECT_FALSE(hasCode(list.diagnostics, "production.packing.density_unknown"));
    // Hardware bag: the 4 shelf pins are a fitting, no fasteners.
    ASSERT_EQ(a.bag.fittings.size(), 1u);
    EXPECT_EQ(a.bag.fittings[0].quantity, 4);
    EXPECT_TRUE(a.bag.fasteners.empty());
    EXPECT_EQ(a.bag.id, "cab1-H");

    const std::string text = packingListText(list);
    EXPECT_TRUE(contains(text, "Package 1/2  cab1-P1  816 x 736 x 95 mm  20.92 kg")) << text;
    EXPECT_TRUE(contains(text, "4 x Shelf support pin 5 mm (generic)")) << text;
}

TEST(Packing, LowerWeightLimit) {
    PackingOptions o;
    o.maxPackageWeightKg = 10;
    o.padding = 0;
    const auto r = packingList(weighedCabinet(), o);
    ASSERT_TRUE(r);
    const auto& p = r.value().assemblies[0].packages;
    // back 1.368 + bottom 5.006 = 6.374; top: 11.38 > 10 -> P2 (5.006); shelf: P1 11.2 no, P2 9.833 ok;
    // left: P1 11.09 no, P2 14.55 no -> P3; right: P3 9.43 ok.
    ASSERT_EQ(p.size(), 3u);
    EXPECT_EQ(numbers(p[0]), (std::vector<int>{6, 3}));
    EXPECT_EQ(numbers(p[1]), (std::vector<int>{4, 5}));
    EXPECT_EQ(numbers(p[2]), (std::vector<int>{1, 2}));
    EXPECT_NEAR(*p[1].weightKg, 5.005728 + 4.826952, 1e-9);
    EXPECT_DOUBLE_EQ(p[2].height, 36); // no padding: 2 x 18
}

TEST(Packing, UnknownDensityIsNeverGuessed) {
    auto m = weighedCabinet();
    m.materials[1].density = 0; // HDF unknown
    const auto r = packingList(m);
    ASSERT_TRUE(r);
    const auto& p = r.value().assemblies[0].packages;
    // Known class: 5.006 + 5.006 + 4.827 + 4.717 + 4.717 = 24.27 <= 25 -> one package; the back alone after it.
    ASSERT_EQ(p.size(), 2u);
    EXPECT_EQ(numbers(p[0]), (std::vector<int>{3, 4, 5, 1, 2}));
    EXPECT_NEAR(*p[0].weightKg, 25.6411344 - 1.3678464, 1e-9);
    EXPECT_EQ(numbers(p[1]), (std::vector<int>{6}));
    EXPECT_FALSE(p[1].weightKg.has_value());
    EXPECT_FALSE(p[1].parts[0].weightKg.has_value());
    EXPECT_TRUE(hasCode(r.value().diagnostics, "production.packing.density_unknown"));
    EXPECT_TRUE(contains(packingListText(r.value()), "weight unknown"));
    EXPECT_TRUE(contains(toCsv(packingTable(r.value())), ",unknown,"));

    // Missing material: weight unknown + material diagnostic.
    m.assemblies[0].parts[0].material = domain::MaterialId("nope");
    const auto r2 = packingList(m);
    ASSERT_TRUE(r2);
    EXPECT_TRUE(hasCode(r2.value().diagnostics, "production.material.missing"));
}

TEST(Packing, OversizeAndOverweightPanelsArePackedAloneWithWarnings) {
    PackingOptions o;
    o.maxPackageLength = 800; // back: 796 + 2*10 = 816 > 800
    auto r = packingList(weighedCabinet(), o);
    ASSERT_TRUE(r);
    auto p = r.value().assemblies[0].packages;
    ASSERT_GE(p.size(), 2u);
    EXPECT_EQ(numbers(p[0]), (std::vector<int>{6}));
    EXPECT_TRUE(p[0].oversize);
    EXPECT_TRUE(hasCode(r.value().diagnostics, "production.packing.oversize"));

    o = {};
    o.maxPackageWeightKg = 4; // every 18 mm panel weighs > 4 kg
    r = packingList(weighedCabinet(), o);
    ASSERT_TRUE(r);
    p = r.value().assemblies[0].packages;
    ASSERT_EQ(p.size(), 6u);
    for (std::size_t i = 1; i < 6; ++i) EXPECT_TRUE(p[i].overweight) << i;
    EXPECT_FALSE(p[0].overweight); // the back (1.37 kg)
    EXPECT_TRUE(hasCode(r.value().diagnostics, "production.packing.overweight"));

    o = {};
    o.maxPackageHeight = 60; // 20 padding + 2 x 18 = 56 fits, + 18 = 74 does not
    r = packingList(weighedCabinet(), o);
    ASSERT_TRUE(r);
    for (const auto& pkg : r.value().assemblies[0].packages) EXPECT_LE(pkg.height, 60.0);
}

TEST(Packing, FastenersAndUnknownHardwareInTheBag) {
    auto m = weighedCabinet();
    domain::HardwareDefinition conf;
    conf.id = domain::HardwareId("hw.confirmat");
    conf.kind = domain::HardwareKind::Confirmat;
    conf.name = "Confirmat 7x50";
    m.hardwareCatalog.push_back(conf);
    domain::HardwareInstance c;
    c.id = domain::HardwareInstanceId("cab1/conf");
    c.definition = conf.id;
    c.quantity = 8;
    m.assemblies[0].hardware.push_back(c);
    domain::HardwareInstance u;
    u.id = domain::HardwareInstanceId("cab1/x");
    u.definition = domain::HardwareId("hw.missing");
    u.quantity = 2;
    m.assemblies[0].hardware.push_back(u);
    const auto r = packingList(m);
    ASSERT_TRUE(r);
    const auto& bag = r.value().assemblies[0].bag;
    ASSERT_EQ(bag.fasteners.size(), 1u);
    EXPECT_EQ(bag.fasteners[0].quantity, 8);
    ASSERT_EQ(bag.fittings.size(), 2u); // shelf pins, then the unknown id
    EXPECT_TRUE(bag.fittings[1].unknownDefinition);
    EXPECT_EQ(bag.fittings[1].name, "hw.missing");
    EXPECT_EQ(bag.totalQuantity(), 4 + 8 + 2);
    EXPECT_TRUE(hasCode(r.value().diagnostics, "production.hardware.unknown_definition"));
}

TEST(Packing, InvalidOptionsAndEmptyInputs) {
    PackingOptions o;
    o.maxPackageWeightKg = 0;
    EXPECT_FALSE(packingList(weighedCabinet(), o));
    o = {};
    o.padding = -1;
    EXPECT_FALSE(packingList(weighedCabinet(), o));
    o = {};
    o.maxPackageHeight = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(packingList(weighedCabinet(), o));

    const auto empty = packingList(domain::Model{});
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty.value().assemblies.empty());
    EXPECT_TRUE(contains(packingListText(empty.value()), "Nothing to pack."));

    // A zero-size panel is skipped with a diagnostic; unknown scope -> diagnostic.
    auto m = weighedCabinet();
    m.assemblies[0].parts[0].length = 0;
    const auto r = packingList(m);
    ASSERT_TRUE(r);
    EXPECT_TRUE(hasCode(r.value().diagnostics, "production.packing.invalid_size"));
    Scope s;
    s.assembly = domain::AssemblyId("zzz");
    const auto r2 = packingList(m, {}, s);
    ASSERT_TRUE(r2);
    EXPECT_TRUE(r2.value().assemblies.empty());
    EXPECT_TRUE(hasCode(r2.value().diagnostics, "production.scope.unknown_assembly"));
}

TEST(Packing, PackageLabelsSvg) {
    auto m = weighedCabinet();
    m.materials[1].density = 0;
    const auto list = packingList(m).value();
    const auto pages = renderPackageLabelsSvg(list, "Kitchen <A>");
    ASSERT_TRUE(pages);
    ASSERT_EQ(pages.value().size(), 1u); // 2 packages + 1 bag = 3 labels, 24 per A4 page
    const std::string& svg = pages.value()[0];
    EXPECT_TRUE(contains(svg, "Package 1/2"));
    EXPECT_TRUE(contains(svg, "Package 2/2"));
    EXPECT_TRUE(contains(svg, "Hardware bag"));
    EXPECT_TRUE(contains(svg, "WEIGHT UNKNOWN"));
    EXPECT_TRUE(contains(svg, "24.27 kg"));
    EXPECT_TRUE(contains(svg, "Kitchen &lt;A&gt;"));
    EXPECT_TRUE(contains(svg, "Parts: 3, 4, 5, 1, 2"));
    LabelSheetOptions bad;
    bad.labelWidth = 0;
    EXPECT_FALSE(renderPackageLabelsSvg(list, "", bad));
}

} // namespace
} // namespace fos::production
