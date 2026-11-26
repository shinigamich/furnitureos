// SPDX-License-Identifier: AGPL-3.0-only
// fos quote / fos batch quote
#include "cli_test_support.hpp"

#include "fos/formats/json.hpp"
#include "fos/project/file.hpp"

namespace fos::cli::test {
namespace {

/// Gives every library entry of the project a price and sets the pricing settings.
void priceProject(const std::filesystem::path& file) {
    auto loaded = project::loadProject(file);
    ASSERT_TRUE(loaded) << loaded.error().message;
    project::Project p = loaded.value().project;
    for (auto& m : p.libraries.materials) m.costPerSquareMeter = 12.5;
    for (auto& e : p.libraries.edgeBands) e.costPerMeter = 0.8;
    for (auto& h : p.libraries.hardware) h.unitCost = 0.1;
    production::PricingSettings s;
    s.wastePercent = 15;
    s.markupPercent = 25;
    s.taxPercent = 20;
    s.labour.perPart = 1.0;
    s.labour.perCutMetre = 0.5;
    p.settings.pricing = s;
    formats::SaveOptions so;
    so.backupCount = 0;
    ASSERT_TRUE(project::saveProject(file, p, so));
}

TEST(CliQuote, UnpricedDefaultLibrariesAreListedNotInvented) {
    TempDir dir;
    ASSERT_EQ(newBaseCabinet(dir.file("k.fosp")).code, 0);
    const auto r = fos({"quote", dir.file("k.fosp")});
    EXPECT_EQ(r.code, 1) << r.out << r.err; // generic libraries have cost 0 = unknown
    EXPECT_TRUE(contains(r.out, "UNPRICED")) << r.out;
    EXPECT_TRUE(contains(r.out, "INCOMPLETE QUOTE")) << r.out;
    EXPECT_TRUE(contains(r.out, "production.quote.unpriced")) << r.out;
    EXPECT_TRUE(contains(r.out, "TOTAL (EUR)")) << r.out;
}

TEST(CliQuote, PricedProjectTextCsvJsonAndSheets) {
    TempDir dir;
    const std::string file = dir.file("k.fosp");
    ASSERT_EQ(newBaseCabinet(file).code, 0);
    priceProject(dir.path / "k.fosp");
    // The pricing settings and the per-m2 price survive a save / load round trip.
    const std::string saved = readFile(dir.path / "k.fosp");
    EXPECT_TRUE(contains(saved, "\"costPerM2\": 12.5")) << saved;
    EXPECT_TRUE(contains(saved, "\"markupPercent\": 25")) << saved;

    // By area: cutting labour needs a cutting plan -> that one line is unpriced (exit 1).
    auto r = fos({"quote", file, "--format", "json"});
    EXPECT_EQ(r.code, 1) << r.err;
    const auto j = formats::Json::parse(r.out);
    EXPECT_EQ(j.at("currency"), "EUR");
    EXPECT_EQ(j.at("unpricedCount"), 1);
    EXPECT_FALSE(j.at("bySheets").get<bool>());
    // total = (subtotal * 1.25) * 1.20 = 1.5 * subtotal (checked on the 2-decimal strings).
    const double subtotal = std::stod(j.at("subtotal").get<std::string>());
    const double total = std::stod(j.at("total").get<std::string>());
    EXPECT_NEAR(total, subtotal * 1.5, 0.011);

    // With --sheets every line is priced: materials per sheet (2800 x 2070 = 5.796 m2 * 12.50).
    r = fos({"quote", file, "--sheets", "--format", "csv"});
    EXPECT_EQ(r.code, 0) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, "Category,Item,Quantity,Unit,Unit price,Amount,Note")) << r.out;
    EXPECT_TRUE(contains(r.out, ",sheet,72.45,")) << r.out; // 5.796 * 12.5 = 72.45
    EXPECT_FALSE(contains(r.out, "UNPRICED")) << r.out;

    EXPECT_EQ(fos({"quote", file, "--format", "xml"}).code, 2);
}

TEST(CliQuote, BatchQuoteCountsCopies) {
    TempDir dir;
    ASSERT_EQ(newBaseCabinet(dir.file("k.fosp")).code, 0);
    priceProject(dir.path / "k.fosp");
    const std::string batch = dir.file("b.fosbatch");
    ASSERT_EQ(fos({"batch", "new", batch, "--name", "B"}).code, 0);
    ASSERT_EQ(fos({"batch", "add", batch, dir.file("k.fosp")}).code, 0);
    auto one = fos({"batch", "quote", batch, "--sheets", "--format", "json"});
    ASSERT_EQ(one.code, 0) << one.err;
    ASSERT_EQ(fos({"batch", "add", batch, dir.file("k.fosp"), "--qty", "1"}).code, 0); // now x 2
    auto two = fos({"batch", "quote", batch, "--format", "json"});
    const auto j1 = formats::Json::parse(fos({"quote", dir.file("k.fosp"), "--format", "json"}).out);
    const auto j2 = formats::Json::parse(two.out);
    // By area every line scales linearly with the copies: 2 x hardware of one project.
    EXPECT_NEAR(std::stod(j2.at("hardwareSubtotal").get<std::string>()),
                2 * std::stod(j1.at("hardwareSubtotal").get<std::string>()), 0.011);
    EXPECT_TRUE(contains(two.err, "Batch 'B'")) << two.err;
}

} // namespace
} // namespace fos::cli::test

namespace fos::cli::test {

// Glass-panel frame front: the panes are bought-in items (report type boughtin, quote line by
// area), not in the cut list.
TEST(CliQuote, GlassPanesAreBoughtInItems) {
    TempDir dir;
    const std::string file = dir.file("g.fosp");
    ASSERT_EQ(newBaseCabinet(file).code, 0);
    auto loaded = project::loadProject(file);
    ASSERT_TRUE(loaded) << loaded.error().message;
    project::Project p = loaded.value().project;
    domain::Material g;
    g.id = domain::MaterialId("glass.float4");
    g.name = "Float glass 4 mm";
    g.kind = domain::MaterialKind::Glass;
    g.thickness = 4;
    g.costPerSquareMeter = 60;
    p.libraries.materials.push_back(g);
    ASSERT_FALSE(p.items.empty());
    auto* cab = p.items[0].cabinet();
    ASSERT_TRUE(cab != nullptr && cab->interior.front.has_value());
    auto& f = *cab->interior.front;
    f.style = furniture::FrontStyle::FramePanel;
    f.frame.joint = furniture::FrameJoint::Dowel;
    f.frame.panelKind = furniture::FramePanelKind::Glass;
    f.frame.panelMaterial = g.id;
    formats::SaveOptions so;
    so.backupCount = 0;
    ASSERT_TRUE(project::saveProject(file, p, so));

    const auto bought = fos({"report", file, "--type", "boughtin"});
    EXPECT_EQ(bought.code, 0) << bought.out << bought.err;
    EXPECT_TRUE(contains(bought.out, "Glass pane")) << bought.out;
    EXPECT_TRUE(contains(bought.out, "Float glass 4 mm")) << bought.out;
    const auto cut = fos({"report", file, "--type", "cutlist"});
    EXPECT_FALSE(contains(cut.out, "Float glass 4 mm")) << cut.out;
    EXPECT_TRUE(contains(cut.out, "production.bought_in.excluded")) << cut.out;
    const auto q = fos({"quote", file});
    EXPECT_TRUE(contains(q.out, "Bought-in")) << q.out;
    EXPECT_TRUE(contains(q.out, "Glass pane")) << q.out;
}

} // namespace fos::cli::test
