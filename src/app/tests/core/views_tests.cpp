// SPDX-License-Identifier: AGPL-3.0-only
// Browser tree, diagnostics rows, cutting summary + settings command, drawings, production text.
#include "fos/app/browser_tree.hpp"
#include "fos/app/cutting_rows.hpp"
#include "fos/app/diagnostics_rows.hpp"
#include "fos/app/drawing_request.hpp"
#include "fos/app/production_text.hpp"
#include "test_support.hpp"

#include <fstream>
#include <sstream>

namespace fos::app::test {
namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

TEST(BrowserTree, ItemsAssembliesParts) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    const auto tree = buildBrowserTree(s.project(), s.resolved());
    ASSERT_EQ(tree.size(), 1u);
    const auto& item = tree[0];
    EXPECT_EQ(item.kind, BrowserNode::Kind::Item);
    EXPECT_EQ(item.id, "cab1");
    EXPECT_EQ(item.label, "Base cabinet");
    EXPECT_TRUE(item.isCabinet);
    EXPECT_TRUE(item.generated);
    ASSERT_EQ(item.children.size(), 1u);
    const auto& assembly = item.children[0];
    EXPECT_EQ(assembly.kind, BrowserNode::Kind::Assembly);
    const auto& model = s.resolved()->model.assemblies[0];
    ASSERT_EQ(assembly.children.size(), model.parts.size());
    // 2 sides, bottom, 2 rails, plinth, back, shelf, 2 doors = 10 parts (project scenario test).
    EXPECT_EQ(assembly.children.size(), 10u);
    for (std::size_t i = 0; i < model.parts.size(); ++i) EXPECT_EQ(assembly.children[i].id, model.parts[i].id.str());
    // Side: finished 820 x 560 x 18.
    for (const auto& p : assembly.children) {
        if (p.id == "cab1/side.left") {
            EXPECT_EQ(p.detail, "820 \u00D7 560 \u00D7 18");
        }
    }

    // Without a resolved model only the items are listed.
    const auto bare = buildBrowserTree(s.project(), nullptr);
    ASSERT_EQ(bare.size(), 1u);
    EXPECT_FALSE(bare[0].generated);
    EXPECT_TRUE(bare[0].children.empty());
}

TEST(DiagnosticsRows, GenerationThenValidationWithPartLinks) {
    project::Resolved r;
    domain::Assembly a;
    a.id = domain::AssemblyId("cab1");
    domain::Part p;
    p.id = domain::PartId("cab1/side.left");
    a.parts.push_back(p);
    r.model.assemblies.push_back(a);
    r.items.push_back({"cab1", true, {}});
    r.diagnostics = {{core::Severity::Warning, "furniture.x", "w", "cab1/side.left"},
                     {core::Severity::Error, "project.item.generation_failed", "e", "cab1"},
                     {core::Severity::Info, "x.y", "i", ""}};
    validation::ValidationReport v;
    validation::Finding f;
    f.severity = core::Severity::Error;
    f.code = "validation.part.material_missing";
    f.message = "m";
    f.entityId = "cab1/side.left";
    f.assemblyId = "cab1";
    f.partId = "cab1/side.left";
    v.diagnostics.push_back(f);

    const auto rows = diagnosticRows(r, v);
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[0].source, DiagnosticSource::Generation);
    EXPECT_EQ(rows[0].partId, "cab1/side.left");
    EXPECT_EQ(rows[0].itemId, "cab1");
    EXPECT_TRUE(rows[1].partId.empty());
    EXPECT_EQ(rows[1].itemId, "cab1");
    EXPECT_TRUE(rows[2].itemId.empty());
    EXPECT_EQ(rows[3].source, DiagnosticSource::Validation);
    EXPECT_EQ(rows[3].partId, "cab1/side.left");
    const auto sum = summarize(rows);
    EXPECT_EQ(sum.errors, 2u);
    EXPECT_EQ(sum.warnings, 1u);
    EXPECT_EQ(sum.infos, 1u);
}

TEST(CuttingRows, SummaryFromPlansAndKerfCommand) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    const auto rows = cuttingSummary(s.cuttingPlans());
    // pb18: 2 sides, bottom, 2 rails, plinth, shelf, 2 doors = 9 pieces; hdf3: back = 1.
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].materialId, project::defaults::kParticleboard18);
    EXPECT_EQ(rows[0].pieces, 9);
    EXPECT_EQ(rows[1].pieces, 1);
    for (const auto& r : rows) {
        EXPECT_TRUE(r.optimised);
        EXPECT_TRUE(r.layoutValid);
        EXPECT_EQ(r.unplaced, 0);
        EXPECT_GE(r.sheets, r.sheetLowerBound);
        EXPECT_GT(r.utilisationPct, 0);
        EXPECT_LE(r.utilisationPct, 100);
    }
    EXPECT_EQ(rows[1].sheets, 1); // one back (596 x 716) on a 2800 x 2070 sheet

    // Kerf / trim through a Document command; other settings untouched; undoable.
    const auto before = s.project().cutting.defaults;
    ASSERT_TRUE(s.execute(cuttingSettingsCommand(s.project(), std::nullopt, 3.2, {5, 6, 7, 8})).ok());
    const auto& after = s.project().cutting.defaults;
    EXPECT_DOUBLE_EQ(after.kerf, 3.2);
    EXPECT_DOUBLE_EQ(after.trim.left, 5);
    EXPECT_DOUBLE_EQ(after.trim.bottom, 8);
    EXPECT_DOUBLE_EQ(after.minRemnantLength, before.minRemnantLength);
    EXPECT_DOUBLE_EQ(s.cuttingPlans().plans[0].job.kerf, 3.2); // regenerated, not cached
    ASSERT_TRUE(s.execute(cuttingSettingsCommand(s.project(), std::string(project::defaults::kHdf3), 0, {0, 0, 0, 0})).ok());
    EXPECT_DOUBLE_EQ(s.project().cutting.forMaterial(project::defaults::kHdf3).kerf, 0);
    EXPECT_DOUBLE_EQ(s.project().cutting.forMaterial(project::defaults::kParticleboard18).kerf, 3.2);
    ASSERT_TRUE(s.undo().ok());
    ASSERT_TRUE(s.undo().ok());
    EXPECT_DOUBLE_EQ(s.project().cutting.defaults.kerf, before.kerf);
}

TEST(Drawings, AssemblyAndPartDrawingFollowWidthAndExport) {
    TempDir dir;
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    DrawingRequest req;
    req.id = "cab1";
    req.date = "2026-11-13";
    auto d = generateDrawing(s.project(), *s.resolved(), req);
    ASSERT_TRUE(d.ok()) << d.error().message;
    ASSERT_FALSE(d.value().sheets.empty());
    EXPECT_DOUBLE_EQ(d.value().sheets[0].width, 420); // A3 landscape
    EXPECT_DOUBLE_EQ(d.value().sheets[0].height, 297);

    auto svg = exportDrawingSvg(d.value(), dir.path / "a.svg");
    ASSERT_TRUE(svg.ok()) << svg.error().message;
    ASSERT_EQ(svg.value().size(), 1u);
    const std::string text = readFile(dir.path / "a.svg");
    EXPECT_NE(text.find("<svg"), std::string::npos);
    EXPECT_NE(text.find("2026-11-13"), std::string::npos);
    EXPECT_NE(text.find("Base cabinet"), std::string::npos);

    auto dxf = exportDrawingDxf(d.value(), dir.path / "a.dxf");
    ASSERT_TRUE(dxf.ok());
    ASSERT_EQ(dxf.value().size(), d.value().views.size());
    EXPECT_NE(readFile(dxf.value()[0]).find("ENTITIES"), std::string::npos);

    ASSERT_TRUE(exportDrawingPdf(d.value(), dir.path / "a.pdf", "Base cabinet").ok());
    EXPECT_EQ(readFile(dir.path / "a.pdf").rfind("%PDF", 0), 0u);

    req.kind = DrawingRequest::Kind::Part;
    req.id = "cab1/shelf.1";
    req.format = drawings::PaperFormat::A4;
    req.scale = drawings::Scale{1, 10}; // 1:5 needs 121 x 142 mm > the 267 x 138 mm A4 area
    auto pd = generateDrawing(s.project(), *s.resolved(), req);
    ASSERT_TRUE(pd.ok()) << (pd.ok() ? "" : pd.error().message);
    EXPECT_DOUBLE_EQ(pd.value().sheets[0].scale.denominator, 10);

    // 1:1 on A4 cannot hold a 900 mm cabinet: the drawings module refuses with its message.
    req.kind = DrawingRequest::Kind::Assembly;
    req.id = "cab1";
    req.scale = drawings::Scale{1, 1};
    auto tooBig = generateDrawing(s.project(), *s.resolved(), req);
    ASSERT_FALSE(tooBig.ok());
    EXPECT_FALSE(tooBig.error().message.empty());
    req.id = "nope";
    req.scale.reset();
    EXPECT_FALSE(generateDrawing(s.project(), *s.resolved(), req).ok());
}

TEST(ProductionText, EnglishDefaultsWithoutTranslator) {
    const auto loc = ProductionText::localisation();
    const production::Localisation defaults;
    EXPECT_EQ(loc.cutList.cutLength, defaults.cutList.cutLength);
    EXPECT_EQ(loc.partList.edgeW2, defaults.partList.edgeW2);
    EXPECT_EQ(loc.terms.areaLowerBound, defaults.terms.areaLowerBound);
    EXPECT_EQ(loc.terms.roles.at(domain::PartRole::Shelf), "Shelf");
    EXPECT_EQ(ProductionText::htmlSheetText().title, production::HtmlSheetText{}.title);
    EXPECT_EQ(DrawingText::titleLabels().scale, drawings::TitleBlockLabels{}.scale);
    EXPECT_EQ(DrawingText::calibration().noteLines, drawings::CalibrationOptions{}.noteLines);
}

} // namespace
} // namespace fos::app::test
