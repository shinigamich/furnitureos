// SPDX-License-Identifier: AGPL-3.0-only
// Scenario F - contoured parts end to end (ADR-0008): a base cabinet whose shelf has rounded
// front corners (furniture ContourSpec through the undoable project command) plus a worktop
// panel with a sink cut-out and a tap hole -> resolve -> cut list shows shaped rows -> cutting
// plans: rectangular parts in validated guillotine plans, shaped parts nested (validateNest
// clean, every shaped part placed) -> worktop part drawing carries the radius / diameter
// dimensions -> CAM program for the shaped shelf simulates clean, has G2/G3 arcs and the
// UNVERIFIED header -> save / reopen identical. FICTIONAL demo machine of the CLI.
//
// Hand-computed expectations:
//  * Cabinet width 800: shelf finished length = 800 - 2 * 18 (sides) - 2 (clearance) = 762,
//    finished width 540 (default base cabinet, as in the CLI README cut list), front edge L1
//    banded with ABS 2 mm -> cut (blank) size 762 x 538.
//  * Worktop 1200 x 600 x 18, raw edges: cut size = finished size 1200 x 600.
//    Sink 500 x 400 at (350, 100), corner radius 20; tap hole centre (1000, 450), radius 17.5.
//    Exact net area = 1200 * 600 - (500 * 400 - (4 - pi) * 20^2) - pi * 17.5^2
//                   = 720000 - 199656.637 - 962.113 = 519381.250 mm^2.
//  * Nesting defaults: tool 8, clearance 0, chord 0.1 -> partSpacing 8 + 0 + 2 * 0.1 = 8.2,
//    edgeSpacing 8 + 0.1 = 8.1. Both shaped parts are particleboard -> ONE nesting job, 2 parts.
//  * Particleboard rectangular parts: 10 parts of the cabinet minus the HDF back minus the
//    shaped shelf = 8 particleboard pieces on the saw; the HDF back is its own guillotine job.
//  * CAM blank of the shelf: allowance 10 -> (762 + 2 * 10) x (540 + 2 * 10) = 782 x 560 (cam
//    setup.hpp: the part's L x W bounding box grown per side); stock min corner (20, 20, 0).
#include "scenario_support.hpp"

#include "fos/cam/contour.hpp"
#include "fos/cam/generate.hpp"
#include "fos/cam/generic_iso.hpp"
#include "fos/cam/simulate.hpp"
#include "fos/cli/demo_cam.hpp"
#include "fos/furniture/panel.hpp"
#include "fos/nesting/validate.hpp"

#include <cmath>
#include <numbers>

namespace fos::scenario {
namespace {

constexpr double kPi = std::numbers::pi;

project::Project shapedProject() {
    project::Document doc(project::newProject(meta("Scenario F")));
    EXPECT_TRUE(doc.execute(project::commands::addItem(templateItem(&furniture::templates::baseCabinet, "cab1"))).ok());
    EXPECT_TRUE(doc.execute(project::commands::setParameter("cab1", "width", 800)).ok());
    furniture::ContourSpec front;
    front.kind = furniture::ContourKind::RoundedCorners;
    front.radii = {"30", "30", "", ""}; // corners (0,0) and (L,0): the front edge L1
    EXPECT_TRUE(doc.execute(project::commands::setPartContour("cab1", "shelf.1", front)).ok());

    project::PanelItem top;
    top.id = domain::AssemblyId("top");
    top.panel = furniture::makePanel(domain::PartId("top/panel"), "Worktop",
                                     domain::MaterialId(project::defaults::kParticleboard18));
    EXPECT_TRUE(top.panel.parameters.setValue("length", 1200).ok());
    EXPECT_TRUE(top.panel.parameters.setValue("width", 600).ok());
    top.placement = geometry::Transform::translation({0, 0, 900});
    EXPECT_TRUE(doc.execute(project::commands::addItem(project::Item{top, formats::Json::object()})).ok());
    furniture::ContourSpec worktop;
    worktop.kind = furniture::ContourKind::Rectangle;
    worktop.cutouts = {{furniture::CutoutKind::Rectangle, "350", "100", "500", "400", "20"},
                       {furniture::CutoutKind::Circle, "1000", "450", "", "", "17.5"}};
    EXPECT_TRUE(doc.execute(project::commands::setPanelContour("top", worktop)).ok());
    return doc.project();
}

bool hasLine(const std::string& text, const std::string& prefix) {
    return text.find("\n" + prefix) != std::string::npos;
}

} // namespace

TEST(ScenarioF, ShapedPartsEndToEnd) {
    const project::Project p = shapedProject();
    const project::Resolved r = resolved(p);
    EXPECT_FALSE(core::hasErrors(r.diagnostics));
    const domain::Part* shelf = findPart(r, "cab1/shelf.1");
    const domain::Part* top = findPart(r, "top/panel");
    ASSERT_NE(shelf, nullptr);
    ASSERT_NE(top, nullptr);
    ASSERT_TRUE(shelf->shape.has_value());
    ASSERT_TRUE(top->shape.has_value());
    EXPECT_DOUBLE_EQ(shelf->length, 762);
    EXPECT_DOUBLE_EQ(shelf->width, 540);

    // --- cut list: shaped rows with the blank (cut) sizes.
    const auto reports = project::productionReports(r, project::productionOptions(p));
    const auto* shelfRow = cutRow(reports.cutList, "cab1/shelf.1");
    const auto* topRow = cutRow(reports.cutList, "top/panel");
    ASSERT_NE(shelfRow, nullptr);
    ASSERT_NE(topRow, nullptr);
    EXPECT_TRUE(shelfRow->shaped);
    EXPECT_TRUE(topRow->shaped);
    EXPECT_DOUBLE_EQ(shelfRow->cutLength, 762);
    EXPECT_DOUBLE_EQ(shelfRow->cutWidth, 538);
    EXPECT_DOUBLE_EQ(topRow->cutLength, 1200);
    EXPECT_DOUBLE_EQ(topRow->cutWidth, 600);
    EXPECT_FALSE(cutRow(reports.cutList, "cab1/bottom")->shaped);

    // --- cutting plans: both kinds, both validators clean.
    const auto plans = project::cuttingPlans(r, p.cutting);
    EXPECT_FALSE(core::hasErrors(plans.diagnostics));
    ASSERT_EQ(plans.routedParts.size(), 2u);
    const auto* saw = plans.guillotineFor(project::defaults::kParticleboard18);
    const auto* hdf = plans.guillotineFor(project::defaults::kHdf3);
    const auto* nest = plans.nestingFor(project::defaults::kParticleboard18);
    ASSERT_NE(saw, nullptr);
    ASSERT_NE(hdf, nullptr);
    ASSERT_NE(nest, nullptr);
    int sawPieces = 0;
    for (const auto& piece : saw->job.pieces) {
        sawPieces += piece.quantity;
        for (const auto& id : piece.sourceIds) EXPECT_TRUE(id != "cab1/shelf.1" && id != "top/panel") << id;
    }
    EXPECT_EQ(sawPieces, 8);
    for (const auto* g : {saw, hdf}) {
        ASSERT_TRUE(g->result.has_value());
        EXPECT_TRUE(g->validation.empty());
        EXPECT_EQ(g->result->totals.unplacedCount, 0);
    }
    ASSERT_EQ(nest->job.job.parts.size(), 2u);
    EXPECT_DOUBLE_EQ(nest->job.job.partSpacing, 8.2);
    EXPECT_DOUBLE_EQ(nest->job.job.edgeSpacing, 8.1);
    ASSERT_TRUE(nest->result.has_value());
    EXPECT_TRUE(nest->result->isHeuristic);
    EXPECT_EQ(nest->result->placedCount, 2);
    EXPECT_TRUE(nest->result->unplaced.empty());
    EXPECT_TRUE(nest->validation.empty());
    EXPECT_TRUE(nesting::validateNest(nest->job.job, *nest->result).empty()); // independent re-check
    for (std::size_t i = 0; i < nest->job.frames.size(); ++i) {
        if (nest->job.frames[i].partId == "top/panel") {
            EXPECT_NEAR(nest->job.frames[i].exactNetArea,
                        720000 - (500 * 400 - (4 - kPi) * 400) - kPi * 17.5 * 17.5, 1e-6);
        }
    }

    // --- worktop drawing: R 20 on the sink corners, diameter 35 for the tap hole.
    auto drawing = project::partDrawing(p, r, "top/panel");
    ASSERT_TRUE(drawing.ok()) << drawing.error().message;
    int sinkRadii = 0;
    bool tapDiameter = false;
    for (const auto& v : drawing.value().views)
        for (const auto& d : v.annotations.radiusDimensions) {
            if (contains(d.id, "dim.radius.cutout[0.")) {
                ++sinkRadii;
                EXPECT_NEAR(drawings::measuredValue(d), 20, 1e-9);
                EXPECT_EQ(drawings::dimensionText(d), "R 20");
            }
            if (contains(d.id, "dim.diameter.cutout[1]")) {
                tapDiameter = true;
                EXPECT_TRUE(d.diameter);
                EXPECT_NEAR(drawings::measuredValue(d), 35, 1e-9);
            }
        }
    EXPECT_EQ(sinkRadii, 4);
    EXPECT_TRUE(tapDiameter);

    // --- CAM of the shaped shelf, face A, blank allowance 10 (as `fos cam` does by default).
    const auto machine = cli::demoMachineProfile();
    const auto tools = cli::demoToolLibrary();
    cam::Setup setup{domain::Face::A, cli::demoStockOrigin()};
    setup.blankAllowance = 10;
    cam::CamOptions options;
    options.model = &r.model;
    cam::SimulationOptions sim;
    sim.model = &r.model;
    auto frame = cam::makeSetupFrame(*shelf, setup);
    ASSERT_TRUE(frame.ok());
    EXPECT_NEAR(frame.value().stock.max.x - frame.value().stock.min.x, 782, 1e-9);
    EXPECT_NEAR(frame.value().stock.max.y - frame.value().stock.min.y, 560, 1e-9);
    auto g = cam::generateProgram(*shelf, setup, machine, tools, options);
    ASSERT_TRUE(g.ok()) << g.error().message;
    const auto report = cam::simulate(g.value().program, *shelf, setup, machine, tools, sim);
    for (const auto& d : report.diagnostics)
        if (d.severity == core::Severity::Error) ADD_FAILURE() << d.code << ": " << d.message;
    const cam::GenericIsoPostprocessor post;
    auto out = post.generate(cam::PostInput{g.value().program, *shelf, setup, machine, tools}, sim);
    ASSERT_TRUE(out.ok()) << out.error().message;
    const std::string& nc = out.value().text;
    EXPECT_TRUE(contains(nc, "UNVERIFIED"));
    EXPECT_TRUE(hasLine(nc, "G2 ") || hasLine(nc, "G3 "));
    EXPECT_TRUE(contains(nc, "(OPERATION contour.outline)"));
    EXPECT_FALSE(out.value().verifiedForMachine);
    // Without a blank allowance a rectangular part is unchanged; with one it is refused.
    cam::Setup rectSetup{domain::Face::A, cli::demoStockOrigin()};
    rectSetup.blankAllowance = 10;
    EXPECT_FALSE(cam::generateProgram(*findPart(r, "cab1/bottom"), rectSetup, machine, tools, options).ok());

    // --- save / reopen identical.
    TempDir dir;
    const auto path = dir.path / "shaped.fosp";
    ASSERT_TRUE(project::saveProject(path, p, fixedClock()).ok());
    auto loaded = project::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    EXPECT_EQ(modelText(loaded.value().resolved.model), modelText(r.model));
    EXPECT_EQ(project::serializeProject(loaded.value().project, "2026-11-13T08:00:00Z").value(),
              project::serializeProject(p, "2026-11-13T08:00:00Z").value());
}

} // namespace fos::scenario
