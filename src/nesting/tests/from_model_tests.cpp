// SPDX-License-Identifier: AGPL-3.0-only
#include "phase12_model.hpp"
#include "test_support.hpp"

#include "fos/nesting/from_model.hpp"

#include <cmath>
#include <numbers>

using namespace fos::nesting;
using namespace fos::nesting::test;
namespace domain = fos::domain;
namespace geometry = fos::geometry;

namespace {

constexpr double kPi = std::numbers::pi;

// Exact areas, computed by hand:
//  rounded 600 x 400, R50: 600*400 - 4*(50^2 - pi*50^2/4) = 240000 - (4 - pi)*2500 = 237853.98...
const double kRoundedArea = 240000.0 - (4.0 - kPi) * 2500.0;
//  arched side: 800 x 500 rectangle + circular segment of the bulge-0.25 arc over the 800 chord:
//  theta = 4 atan(0.25), R = 400 / sin(theta/2) = 850, segment = R^2/2 (theta - sin theta).
const double kTheta = 4.0 * std::atan(0.25);
const double kArchedArea = 400000.0 + 850.0 * 850.0 / 2.0 * (kTheta - std::sin(kTheta));
//  worktop: 1200*600 outline; net = 720000 - 500*400 - pi*17.5^2.
const double kWorktopArea = 720000.0;
const double kWorktopNet = 720000.0 - 200000.0 - kPi * 17.5 * 17.5;

/// Tessellation is inscribed: the polygon loses at most chordTolerance * perimeter of area
/// (each circular segment between chord and arc is thinner than the tolerance).
void expectTessellatedArea(const Part& p, double exact, double perimeter, double tol) {
    const double a = geometry::area(p.outline);
    EXPECT_LE(a, exact + 1e-6) << p.id;
    EXPECT_GE(a, exact - tol * perimeter) << p.id;
}

} // namespace

TEST(NestingFromModel, ShapedPartsOfPhase12ModelGoToOneJobWithRawOutlines) {
    const domain::Model model = phase12Model();
    fos::core::Diagnostics ds;
    ModelSettings s; // tool 8, clearance 0, chord tolerance 0.1
    const auto jobs = nestingJobsFromModel(model, s, ds);
    EXPECT_TRUE(ds.empty()) << codes(ds);
    ASSERT_EQ(jobs.size(), 1u);
    const MaterialNestJob& mj = jobs[0];
    EXPECT_EQ(mj.materialId, "mat.pb18");
    EXPECT_DOUBLE_EQ(mj.thickness, 18.0);
    // Only the three shaped parts (the cabinet's rectangles stay with guillotine cutting).
    ASSERT_EQ(mj.job.parts.size(), 3u);
    ASSERT_EQ(mj.frames.size(), 3u);
    EXPECT_EQ(mj.job.parts[0].id, "shaped/rounded");
    EXPECT_EQ(mj.job.parts[1].id, "shaped/arched");
    EXPECT_EQ(mj.job.parts[2].id, "shaped/worktop");
    // Spacing: 8 + 0 + 2 * 0.1 = 8.2; edge spacing 8 + 0.1 = 8.1.
    EXPECT_NEAR(mj.job.partSpacing, 8.2, 1e-12);
    EXPECT_NEAR(mj.job.edgeSpacing, 8.1, 1e-12);
    ASSERT_EQ(mj.job.sheets.size(), 1u);
    EXPECT_EQ(mj.job.sheets[0].id, "mat.pb18/sheet");
    EXPECT_DOUBLE_EQ(mj.job.sheets[0].length, 2800.0);
    EXPECT_DOUBLE_EQ(mj.job.sheets[0].width, 2070.0);
    EXPECT_DOUBLE_EQ(mj.job.sheets[0].margins.left, 10.0);

    // Rotation rules: rounded/worktop have no grain -> Quarter; arched side grain AlongLength on
    // grained particleboard -> Half (0/180 keeps the grain), not pre-rotated.
    EXPECT_EQ(mj.job.parts[0].rotation, RotationRule::Quarter);
    EXPECT_EQ(mj.job.parts[1].rotation, RotationRule::Half);
    EXPECT_EQ(mj.job.parts[2].rotation, RotationRule::Quarter);
    for (const auto& f : mj.frames) EXPECT_DOUBLE_EQ(f.preRotationDeg, 0.0);
    for (const auto& p : mj.job.parts) EXPECT_FALSE(p.mirrorAllowed);

    // Exact areas from the contours.
    EXPECT_NEAR(mj.frames[0].exactArea, kRoundedArea, 1e-6);
    EXPECT_NEAR(mj.frames[1].exactArea, kArchedArea, 1e-6);
    EXPECT_NEAR(mj.frames[2].exactArea, kWorktopArea, 1e-6);
    EXPECT_NEAR(mj.frames[2].exactNetArea, kWorktopNet, 1e-6);
    // Tessellated areas within the chord tolerance bound. Perimeters: rounded
    // 2*(500+300) + 2*pi*50; arched 800 + 2*500 + R*theta = 1800 + 850*theta; worktop 3600.
    expectTessellatedArea(mj.job.parts[0], kRoundedArea, 1600.0 + 100.0 * kPi, 0.1);
    expectTessellatedArea(mj.job.parts[1], kArchedArea, 1800.0 + 850.0 * kTheta, 0.1);
    EXPECT_NEAR(geometry::area(mj.job.parts[2].outline), kWorktopArea, 1e-6);
    ASSERT_EQ(mj.job.parts[2].holes.size(), 2u);
    EXPECT_NEAR(geometry::area(mj.job.parts[2].holes[0]), 200000.0, 1e-6);
    const double tap = geometry::area(mj.job.parts[2].holes[1]);
    EXPECT_LE(tap, kPi * 17.5 * 17.5);
    EXPECT_GE(tap, kPi * 17.5 * 17.5 - 0.1 * 2 * kPi * 17.5);
    // Every arc vertex lies on the exact arc: the rounded panel stays inside [0,600]x[0,400].
    const auto b = geometry::bounds(mj.job.parts[0].outline);
    EXPECT_NEAR(b.min.x, 0.0, 1e-9);
    EXPECT_NEAR(b.max.x, 600.0, 1e-9);
    EXPECT_NEAR(b.max.y, 400.0, 1e-9);

    // Nest them and validate independently.
    const NestResult r = nestValid(mj.job);
    EXPECT_EQ(r.placedCount, 3);
    EXPECT_TRUE(r.unplaced.empty());
    ASSERT_EQ(r.sheets.size(), 1u);
    // Every pair of outlines at least one tool diameter + tessellation allowance apart.
    EXPECT_GE(minPairDistance(r.sheets[0]), 8.2 - 1e-6);
    // Net area = outline areas minus hole areas (validated by validateNest as well).
    double outlines = 0, holes = 0;
    for (const auto& p : mj.job.parts) {
        outlines += geometry::area(p.outline);
        for (const auto& h : p.holes) holes += geometry::area(h);
    }
    EXPECT_NEAR(r.totalPartArea, outlines, 1e-6);
    EXPECT_NEAR(r.totalNetPartArea, outlines - holes, 1e-6);
}

TEST(NestingFromModel, BandedOutlineUsesTheRawContour) {
    domain::Model model = phase12Model();
    model.assemblies.resize(1); // cabinet only
    domain::Part p = roundedPanel();
    // 8 segments (4 lines + 4 arcs), all banded with 2 mm ABS.
    p.shape->outerEdges.assign(p.shape->outer.size(), domain::EdgeBandId("eb.abs2"));
    domain::Assembly a;
    a.id = domain::AssemblyId("shaped");
    a.parts = {p};
    model.assemblies.push_back(a);
    fos::core::Diagnostics ds;
    const auto jobs = nestingJobsFromModel(model, ModelSettings{}, ds);
    EXPECT_TRUE(ds.empty()) << codes(ds);
    ASSERT_EQ(jobs.size(), 1u);
    ASSERT_EQ(jobs[0].job.parts.size(), 1u);
    // Raw = 596 x 396 with R48 corners: 596*396 - (4 - pi)*48^2.
    const double exact = 596.0 * 396.0 - (4.0 - kPi) * 48.0 * 48.0;
    EXPECT_NEAR(jobs[0].frames[0].exactArea, exact, 1e-6);
    expectTessellatedArea(jobs[0].job.parts[0], exact, 2 * (500.0 + 300.0) + 2 * kPi * 48.0, 0.1);
    const auto b = geometry::bounds(jobs[0].job.parts[0].outline);
    EXPECT_NEAR(b.min.x, 2.0, 1e-9);
    EXPECT_NEAR(b.max.x, 598.0, 1e-9);
    EXPECT_NEAR(b.min.y, 2.0, 1e-9);
    EXPECT_NEAR(b.max.y, 398.0, 1e-9);
}

TEST(NestingFromModel, GrainAlongWidthPreRotatesTheOutline) {
    domain::Model model = phase12Model();
    domain::Part arched = archedSide();
    arched.grain = domain::GrainDirection::AlongWidth;
    model.assemblies.back().parts = {arched};
    fos::core::Diagnostics ds;
    const auto jobs = nestingJobsFromModel(model, ModelSettings{}, ds);
    ASSERT_EQ(jobs.size(), 1u);
    ASSERT_EQ(jobs[0].job.parts.size(), 1u);
    EXPECT_DOUBLE_EQ(jobs[0].frames[0].preRotationDeg, 90.0);
    EXPECT_EQ(jobs[0].job.parts[0].rotation, RotationRule::Half);
    // (x, y) -> (-y, x): [0,800]x[0,600] becomes [-600,0]x[0,800]; the 600 mm width now runs along X.
    const auto b = geometry::bounds(jobs[0].job.parts[0].outline);
    EXPECT_NEAR(b.min.x, -600.0, 1e-9);
    EXPECT_NEAR(b.max.x, 0.0, 1e-9);
    EXPECT_NEAR(b.min.y, 0.0, 1e-9);
    EXPECT_NEAR(b.max.y, 800.0, 1e-9);
    nestValid(jobs[0].job);
}

TEST(NestingFromModel, ProblemsAreReportedNotDropped) {
    domain::Model model = phase12Model();
    auto& parts = model.assemblies.back().parts;
    parts[0].material = domain::MaterialId("mat.unknown");
    parts[1].thickness = 19;
    // Self-intersecting cut-out: a bow tie.
    geometry::Contour bow;
    bow.vertices = {{{100, 100}, 0}, {{200, 200}, 0}, {{200, 100}, 0}, {{100, 200}, 0}};
    parts[2].shape->cutouts.push_back(bow);
    fos::core::Diagnostics ds;
    const auto jobs = nestingJobsFromModel(model, ModelSettings{}, ds);
    ASSERT_EQ(jobs.size(), 1u);
    ASSERT_EQ(jobs[0].job.parts.size(), 1u); // only the arched side remains
    EXPECT_EQ(jobs[0].job.parts[0].id, "shaped/arched");
    ASSERT_EQ(ds.size(), 3u) << codes(ds);
    EXPECT_EQ(ds[0].code, "nesting.adapter.material_missing");
    EXPECT_EQ(ds[0].entityId, "shaped/rounded");
    EXPECT_EQ(ds[1].code, "nesting.adapter.thickness_mismatch");
    EXPECT_EQ(ds[2].code, "nesting.adapter.invalid_shape");
    EXPECT_EQ(ds[2].entityId, "shaped/worktop");
    EXPECT_NE(ds[2].message.find("cut-out 2"), std::string::npos) << ds[2].message;
}

TEST(NestingFromModel, InvalidSettingsAndVertexLimit) {
    const domain::Model model = phase12Model();
    fos::core::Diagnostics ds;
    ModelSettings bad;
    bad.toolDiameter = 0;
    EXPECT_TRUE(nestingJobsFromModel(model, bad, ds).empty());
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "nesting.adapter.invalid_settings");

    ds.clear();
    ModelSettings fine;
    // At 1e-5 mm a chord spans about 2*sqrt(2*tol/R) rad: R50 corners ~6.3 / 0.00126 = 5000
    // vertices, the R850 arc ~0.98 / 0.000307 = 3200, the R17.5 tap hole ~6.3 / 0.00214 = 2900.
    fine.chordTolerance = 1e-5;
    EXPECT_TRUE(nestingJobsFromModel(model, fine, ds).empty());
    ASSERT_EQ(ds.size(), 3u) << codes(ds);
    for (const auto& d : ds) EXPECT_EQ(d.code, "nesting.adapter.too_many_vertices");
    EXPECT_EQ(ds[2].entityId, "shaped/worktop");
}
