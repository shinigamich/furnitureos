// SPDX-License-Identifier: AGPL-3.0-only
// Scenario C - production drawings: generated views and dimensions, a stored user edit (moved
// dimension line, text override with measuredAtEdit), exact-scale SVG export (sheet = paper
// size in mm, 100 mm calibration line = exactly 100 user units), then a model change that makes
// the override stale (warning + flagged text). Base-cabinet template 600 x 820 x 560.
#include "scenario_support.hpp"

#include "fos/drawings/svg.hpp"

#include <cmath>
#include <regex>

namespace fos::scenario {
namespace {

const std::string kWidthDim = "cab1/view.front/dim.overall.width";

project::Document makeDoc() {
    project::Document doc(project::newProject(meta("Scenario C")));
    EXPECT_TRUE(doc.execute(project::commands::addItem(templateItem(&furniture::templates::baseCabinet, "cab1"))).ok());
    return doc;
}

drawings::DrawingResult draw(const project::Project& p, drawings::PaperFormat format = drawings::PaperFormat::A3) {
    drawings::AssemblyDrawingOptions options;
    options.sheet.format = format;
    options.sheet.orientation = drawings::PaperOrientation::Landscape;
    auto d = project::assemblyDrawing(p, resolved(p), "cab1", options);
    EXPECT_TRUE(d.ok()) << (d.ok() ? "" : d.error().message);
    return d.ok() ? d.value() : drawings::DrawingResult{};
}

bool hasCode(const core::Diagnostics& ds, const std::string& code) {
    for (const auto& d : ds)
        if (d.code == code) return true;
    return false;
}

/// x1/x2 of the SVG <line> carrying data-element="sheet/calibration.line".
std::pair<double, double> calibrationLineX(const std::string& svg) {
    const std::regex re("<line x1=\"([-0-9.]+)\" y1=\"([-0-9.]+)\" x2=\"([-0-9.]+)\" y2=\"([-0-9.]+)\"[^>]*"
                        "data-element=\"sheet/calibration.line\"");
    std::smatch m;
    if (!std::regex_search(svg, m, re)) return {NAN, NAN};
    EXPECT_EQ(m[2].str(), m[4].str()); // horizontal
    return {std::stod(m[1].str()), std::stod(m[3].str())};
}

TEST(ScenarioC, ViewsAndAutomaticDimensions) {
    auto doc = makeDoc();
    const auto d = draw(doc.project());
    ASSERT_EQ(d.sheets.size(), 1u);
    // Front, top and (first-angle) left view on one sheet.
    ASSERT_EQ(d.views.size(), 3u);
    EXPECT_EQ(d.views[0].view.id, "cab1/view.front");
    const auto* w = findDimension(d, kWidthDim);
    const auto* h = findDimension(d, "cab1/view.front/dim.overall.height");
    ASSERT_NE(w, nullptr);
    ASSERT_NE(h, nullptr);
    EXPECT_NEAR(drawings::measuredValue(*w), 600.0, 1e-9); // template width
    EXPECT_NEAR(drawings::measuredValue(*h), 820.0, 1e-9); // template height incl. plinth
    EXPECT_TRUE(d.diagnostics.empty());
}

TEST(ScenarioC, EditExportAndStaleOverride) {
    auto doc = makeDoc();
    const auto first = draw(doc.project());
    const auto* dim = findDimension(first, kWidthDim);
    ASSERT_NE(dim, nullptr);

    // The user drags the dimension line to offset -30 mm (paper) and overrides its text.
    drawings::ElementEdit edit;
    edit.offset = -30.0;
    drawings::setTextOverride(edit, *dim, "600 (check on site)");
    ASSERT_TRUE(edit.measuredAtEdit.has_value());
    EXPECT_DOUBLE_EQ(*edit.measuredAtEdit, 600.0);
    ASSERT_TRUE(doc.execute(project::commands::setElementEdit(project::assemblyDrawingId("cab1"), kWidthDim, edit)).ok());

    const auto edited = draw(doc.project());
    const auto* e = findDimension(edited, kWidthDim);
    ASSERT_NE(e, nullptr);
    EXPECT_DOUBLE_EQ(e->offset, -30.0);
    EXPECT_EQ(e->textOverride, std::optional<std::string>("600 (check on site)"));
    EXPECT_FALSE(e->staleOverride);
    EXPECT_FALSE(hasCode(edited.diagnostics, "drawings.edits.stale_override"));

    // SVG at exact scale: A3 landscape = 420 x 297 mm; 1 user unit = 1 mm.
    const auto& sheet = edited.sheets[0];
    EXPECT_DOUBLE_EQ(sheet.width, 420.0);
    EXPECT_DOUBLE_EQ(sheet.height, 297.0);
    const std::string svg = drawings::toSvg(sheet);
    EXPECT_TRUE(contains(svg, "width=\"420mm\" height=\"297mm\" viewBox=\"0 0 420 297\""));
    EXPECT_TRUE(contains(svg, ">600 (check on site)</text>"));
    const auto [x1, x2] = calibrationLineX(svg);
    EXPECT_DOUBLE_EQ(std::abs(x2 - x1), 100.0); // the 100 mm calibration line is 100 user units
    // The same check on the sheet primitives (paper mm).
    bool foundLine = false;
    for (const auto& prim : sheet.primitives)
        if (const auto* line = std::get_if<drawings::Line>(&prim); line && line->elementId == "sheet/calibration.line") {
            foundLine = true;
            EXPECT_NEAR(std::hypot(line->b.x - line->a.x, line->b.y - line->a.y), 100.0, 1e-12);
        }
    EXPECT_TRUE(foundLine);

    // The edit is stored in the project and survives save/reopen.
    TempDir dir;
    ASSERT_TRUE(doc.save(dir.path / "c.fosp", fixedClock()).ok());
    auto loaded = project::loadProject(dir.path / "c.fosp");
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    const auto reopened = draw(loaded.value().project);
    EXPECT_DOUBLE_EQ(findDimension(reopened, kWidthDim)->offset, -30.0);
    EXPECT_EQ(drawings::toSvg(reopened.sheets[0]), svg);

    // The model changes (600 -> 700): the override was made against 600 -> stale warning naming
    // both values and the text is flagged with " *" on the sheet.
    ASSERT_TRUE(doc.execute(project::commands::setParameter("cab1", "width", 700)).ok());
    const auto changed = draw(doc.project());
    const auto* s = findDimension(changed, kWidthDim);
    ASSERT_NE(s, nullptr);
    EXPECT_NEAR(drawings::measuredValue(*s), 700.0, 1e-9);
    EXPECT_TRUE(s->staleOverride);
    ASSERT_TRUE(hasCode(changed.diagnostics, "drawings.edits.stale_override"));
    for (const auto& diag : changed.diagnostics)
        if (diag.code == "drawings.edits.stale_override") {
            EXPECT_EQ(diag.severity, core::Severity::Warning);
            EXPECT_TRUE(contains(diag.message, "600")) << diag.message;
            EXPECT_TRUE(contains(diag.message, "700")) << diag.message;
        }
    EXPECT_TRUE(contains(drawings::toSvg(changed.sheets[0]), ">600 (check on site) *</text>"));
}

TEST(ScenarioC, PaperSizesAreExact) {
    auto doc = makeDoc();
    // ISO 216 landscape: A4 297 x 210, A2 594 x 420.
    const auto a4 = draw(doc.project(), drawings::PaperFormat::A4);
    ASSERT_FALSE(a4.sheets.empty());
    EXPECT_TRUE(contains(drawings::toSvg(a4.sheets[0]), "width=\"297mm\" height=\"210mm\" viewBox=\"0 0 297 210\""));
    const auto a2 = draw(doc.project(), drawings::PaperFormat::A2);
    ASSERT_FALSE(a2.sheets.empty());
    EXPECT_TRUE(contains(drawings::toSvg(a2.sheets[0]), "width=\"594mm\" height=\"420mm\" viewBox=\"0 0 594 420\""));
    const auto [x1, x2] = calibrationLineX(drawings::toSvg(a4.sheets[0]));
    EXPECT_DOUBLE_EQ(std::abs(x2 - x1), 100.0);
}

} // namespace
} // namespace fos::scenario
