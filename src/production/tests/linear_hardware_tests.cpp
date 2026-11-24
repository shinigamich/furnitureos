// SPDX-License-Identifier: AGPL-3.0-only
// Linear hardware (HardwareInstance::length): lengths per definition, per-metre pricing in the
// hardware report, cost estimate and quote, and the length shown in tables and packing text.
#include "fos/production/cost.hpp"
#include "fos/production/csv.hpp"
#include "fos/production/packing.hpp"
#include "fos/production/quote.hpp"
#include "fos/production/tables.hpp"
#include "test_models.hpp"

#include <gtest/gtest.h>

#include <limits>

using namespace fos;
using namespace fos::production;

namespace {

domain::HardwareInstance inst(const std::string& id, const std::string& def, int quantity, double length) {
    domain::HardwareInstance h;
    h.id = domain::HardwareInstanceId(id);
    h.definition = domain::HardwareId(def);
    h.quantity = quantity;
    h.length = length;
    return h;
}

/// Wall rail sold per metre (12.50 / m) used as 600 mm + 2 x 900 mm; a sliding track kit priced
/// per piece (30.00 / pc) with two 1764 mm tracks; a pin (0.50 / pc) without length.
domain::Model linearModel() {
    domain::Model m = production::testing::smallModel();
    domain::HardwareDefinition rail;
    rail.id = domain::HardwareId("rail");
    rail.name = "Wall rail";
    rail.unitCost = 12.5;
    rail.custom["unit"] = "m";
    domain::HardwareDefinition track;
    track.id = domain::HardwareId("track");
    track.name = "Sliding track";
    track.unitCost = 30.0;
    domain::HardwareDefinition pin;
    pin.id = domain::HardwareId("pin");
    pin.name = "Pin";
    pin.unitCost = 0.5;
    m.hardwareCatalog = {rail, track, pin};
    m.assemblies[0].hardware = {inst("r1", "rail", 1, 600), inst("r2", "rail", 2, 900),
                                inst("t1", "track", 1, 1764), inst("t2", "track", 1, 1764),
                                inst("p1", "pin", 4, 0)};
    return m;
}

const HardwareRow* row(const HardwareReport& r, const std::string& id) {
    for (const auto& w : r.rows)
        if (w.definitionId.str() == id) return &w;
    return nullptr;
}

const QuoteLine* line(const Quote& q, const std::string& id) {
    for (const auto& l : q.lines)
        if (l.id == id) return &l;
    return nullptr;
}

bool hasCode(const core::Diagnostics& ds, const std::string& code) {
    for (const auto& d : ds)
        if (d.code == code) return true;
    return false;
}

} // namespace

TEST(LinearHardware, IsPricedPerMetreReadsCustomUnit) {
    domain::HardwareDefinition d;
    EXPECT_FALSE(isPricedPerMetre(d));
    d.custom["unit"] = "pc";
    EXPECT_FALSE(isPricedPerMetre(d));
    d.custom["unit"] = "m";
    EXPECT_TRUE(isPricedPerMetre(d));
}

TEST(LinearHardware, ReportSumsLengthsAndPricesPerMetre) {
    const auto r = hardwareReport(linearModel());
    EXPECT_TRUE(r.diagnostics.empty());
    const auto* rail = row(r, "rail");
    ASSERT_NE(rail, nullptr);
    EXPECT_TRUE(rail->perMetre);
    EXPECT_EQ(rail->quantity, 3);
    EXPECT_EQ(rail->linearPieces, 3);
    // 600 + 2 * 900 = 2400 mm = 2.4 m; 2.4 * 12.50 = 30.00.
    EXPECT_NEAR(rail->totalLengthM, 2.4, 1e-12);
    EXPECT_TRUE(rail->costKnown);
    EXPECT_NEAR(rail->totalCost, 30.0, 1e-9);
    const auto* track = row(r, "track");
    ASSERT_NE(track, nullptr);
    EXPECT_FALSE(track->perMetre);
    EXPECT_EQ(track->linearPieces, 2);
    EXPECT_NEAR(track->totalLengthM, 3.528, 1e-12); // 2 * 1764 mm
    EXPECT_NEAR(track->totalCost, 60.0, 1e-9);      // per piece: 2 * 30.00
    const auto* pin = row(r, "pin");
    ASSERT_NE(pin, nullptr);
    EXPECT_EQ(pin->linearPieces, 0);
    EXPECT_EQ(pin->totalLengthM, 0.0);
    EXPECT_NEAR(pin->totalCost, 2.0, 1e-12);
}

TEST(LinearHardware, PerMetreWithoutLengthIsUnpricedAndInvalidLengthIgnored) {
    auto m = linearModel();
    m.assemblies[0].hardware.push_back(inst("r3", "rail", 1, 0));  // no length (old file)
    m.assemblies[0].hardware.push_back(inst("t3", "track", 1, -5)); // invalid
    m.assemblies[0].hardware.push_back(inst("t4", "track", 1, std::numeric_limits<double>::quiet_NaN()));
    const auto r = hardwareReport(m);
    EXPECT_TRUE(hasCode(r.diagnostics, "production.hardware.missing_length"));
    EXPECT_TRUE(hasCode(r.diagnostics, "production.hardware.invalid_length"));
    const auto* rail = row(r, "rail");
    EXPECT_EQ(rail->quantity, 4);
    EXPECT_EQ(rail->linearPieces, 3);
    EXPECT_FALSE(rail->costKnown);
    EXPECT_EQ(rail->totalCost, 0.0);
    const auto* track = row(r, "track");
    EXPECT_EQ(track->quantity, 4);
    EXPECT_EQ(track->linearPieces, 2);
    EXPECT_NEAR(track->totalLengthM, 3.528, 1e-12);

    auto q = quote(m, {});
    ASSERT_TRUE(q) << q.error().message;
    const auto* l = line(q.value(), "rail");
    ASSERT_NE(l, nullptr);
    EXPECT_FALSE(l->priced());
    EXPECT_EQ(l->unit, "m");
    EXPECT_NE(l->note.find("1 of 4 pieces have no length"), std::string::npos) << l->note;
    EXPECT_FALSE(q.value().complete);
}

TEST(LinearHardware, QuotePricesTotalMetres) {
    auto q = quote(linearModel(), {});
    ASSERT_TRUE(q) << q.error().message;
    const auto* rail = line(q.value(), "rail");
    ASSERT_NE(rail, nullptr);
    EXPECT_EQ(rail->unit, "m");
    EXPECT_NEAR(rail->quantity, 2.4, 1e-12);
    ASSERT_TRUE(rail->priced());
    EXPECT_DOUBLE_EQ(*rail->unitPrice, 12.5);
    EXPECT_NEAR(rail->amount, 30.0, 1e-9);
    EXPECT_EQ(rail->note, "3 pc, total length 2.4 m");
    const auto* track = line(q.value(), "track");
    EXPECT_EQ(track->unit, "pc");
    EXPECT_EQ(track->quantity, 2.0);
    EXPECT_NEAR(track->amount, 60.0, 1e-9);
    EXPECT_EQ(track->note, "2 pc, total length 3.528 m");
    EXPECT_EQ(line(q.value(), "pin")->note, "");
    // 30 + 60 + 2 = 92.00 hardware.
    EXPECT_NEAR(q.value().hardwareSubtotal, 92.0, 1e-9);

    // Unpriced per-metre definition keeps the unpriced rule.
    auto m = linearModel();
    m.hardwareCatalog[0].unitCost = 0;
    auto q2 = quote(m, {});
    ASSERT_TRUE(q2);
    EXPECT_FALSE(line(q2.value(), "rail")->priced());
    EXPECT_NE(line(q2.value(), "rail")->note.find("no price per metre"), std::string::npos);
}

TEST(LinearHardware, CostEstimateUsesMetres) {
    const auto c = costEstimate(linearModel());
    ASSERT_EQ(c.hardware.size(), 3u);
    EXPECT_TRUE(c.hardware[0].perMetre);
    EXPECT_NEAR(c.hardware[0].totalLengthM, 2.4, 1e-12);
    EXPECT_NEAR(c.hardware[0].cost, 30.0, 1e-9);
    EXPECT_NEAR(c.hardwareTotal, 92.0, 1e-9);
    const auto t = costTable(c);
    bool metres = false;
    for (const auto& r : t.rows) metres = metres || (r.size() > 3 && r[1] == "Wall rail" && r[2] == "2.4" && r[3] == "m");
    EXPECT_TRUE(metres) << toCsv(t);
}

TEST(LinearHardware, TableShowsLengthColumnOnlyWhenLinear) {
    const auto t = hardwareTable(hardwareReport(linearModel()));
    ASSERT_EQ(t.header.size(), 9u);
    EXPECT_EQ(t.header[4], "Total length (m)");
    ASSERT_EQ(t.rows.size(), 3u);
    EXPECT_EQ(t.rows[0][4], "2.4");
    EXPECT_EQ(t.rows[0][5], "12.5 / m");
    EXPECT_EQ(t.rows[1][4], "3.528");
    EXPECT_EQ(t.rows[2][4], ""); // pin: not linear

    auto m = linearModel();
    m.hardwareCatalog = {m.hardwareCatalog[2]};
    m.assemblies[0].hardware = {inst("p1", "pin", 4, 0)};
    const auto plain = hardwareTable(hardwareReport(m));
    EXPECT_EQ(plain.header.size(), 8u); // unchanged layout without linear fittings
}

TEST(LinearHardware, PackingTextShowsTotalLength) {
    const auto p = packingList(linearModel());
    ASSERT_TRUE(p) << p.error().message;
    const std::string text = packingListText(p.value());
    EXPECT_NE(text.find("3 x Wall rail, total length 2.4 m"), std::string::npos) << text;
    const std::string csv = toCsv(packingTable(p.value()));
    EXPECT_NE(csv.find("Sliding track (total length 3.528 m)"), std::string::npos) << csv;
}
