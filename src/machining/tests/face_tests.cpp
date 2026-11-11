// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/domain/fixtures.hpp"
#include "fos/machining/face.hpp"
#include "fos/machining/flip.hpp"
#include "fos/machining/validation.hpp"

#include <gtest/gtest.h>

using namespace fos;
using domain::Face;
using geometry::Vec2;
using geometry::Vec3;
using machining::FaceFrame;

namespace {
domain::Part panel(double l = 720, double w = 560, double t = 18) {
    domain::Part p;
    p.id = domain::PartId("p1");
    p.name = "Panel";
    p.length = l;
    p.width = w;
    p.thickness = t;
    return p;
}
void expectVec(Vec3 a, Vec3 b) { EXPECT_TRUE(approxEqual(a, b)) << a.x << "," << a.y << "," << a.z; }
} // namespace

// Expectations below are transcribed from the table in fos/domain/operation.hpp for a
// 720 x 560 x 18 panel (L x W x T).
TEST(FaceGeometry, FramesMatchOperationHppForAllSixFaces) {
    const auto p = panel();
    struct Expect { Face f; Vec3 origin, u, v, tool; double uMax, vMax, depth; };
    const Expect table[] = {
        {Face::A, {0, 0, 18}, {1, 0, 0}, {0, 1, 0}, {0, 0, -1}, 720, 560, 18},  // z = T, u=x, v=y, tool -Z
        {Face::B, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 720, 560, 18},    // z = 0, u=x, v=y, tool +Z
        {Face::L1, {0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 720, 18, 560},   // y = 0, u=x, v=z, tool +Y
        {Face::L2, {0, 560, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}, 720, 18, 560}, // y = W, u=x, v=z, tool -Y
        {Face::W1, {0, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, 560, 18, 720},   // x = 0, u=y, v=z, tool +X
        {Face::W2, {720, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}, 560, 18, 720}, // x = L, u=y, v=z, tool -X
    };
    for (const auto& e : table) {
        SCOPED_TRACE(domain::toString(e.f));
        const FaceFrame fr = machining::faceFrame(p, e.f);
        expectVec(fr.origin, e.origin);
        expectVec(fr.uAxis, e.u);
        expectVec(fr.vAxis, e.v);
        expectVec(fr.toolDir, e.tool);
        const auto ext = machining::faceExtents(p, e.f);
        EXPECT_DOUBLE_EQ(ext.u.min, 0);
        EXPECT_DOUBLE_EQ(ext.v.min, 0);
        EXPECT_DOUBLE_EQ(ext.u.max, e.uMax);
        EXPECT_DOUBLE_EQ(ext.v.max, e.vMax);
        EXPECT_DOUBLE_EQ(ext.materialDepth, e.depth);
    }
}

TEST(FaceGeometry, FaceToPartPointsForAllSixFaces) {
    const auto p = panel();
    // (u, v, depth) -> part point, worked out by hand from the conventions:
    expectVec(machining::faceToPart(p, Face::A, 100, 50, 5), {100, 50, 13});  // z = T - depth = 18 - 5
    expectVec(machining::faceToPart(p, Face::B, 100, 50, 5), {100, 50, 5});   // z = depth
    expectVec(machining::faceToPart(p, Face::L1, 100, 9, 5), {100, 5, 9});    // y = depth, z = v
    expectVec(machining::faceToPart(p, Face::L2, 100, 9, 5), {100, 555, 9});  // y = W - depth = 560 - 5
    expectVec(machining::faceToPart(p, Face::W1, 50, 9, 5), {5, 50, 9});      // x = depth, y = u, z = v
    expectVec(machining::faceToPart(p, Face::W2, 50, 9, 5), {715, 50, 9});    // x = L - depth = 720 - 5
}

TEST(FaceGeometry, PartToFaceIsTheInverse) {
    const auto p = panel();
    for (Face f : machining::kAllFaces) {
        const auto c = machining::partToFace(p, f, machining::faceToPart(p, f, 123.5, 7.25, 4.5));
        EXPECT_NEAR(c.u, 123.5, 1e-9);
        EXPECT_NEAR(c.v, 7.25, 1e-9);
        EXPECT_NEAR(c.depth, 4.5, 1e-9);
    }
    // A point in front of face A (outside the panel) has negative depth.
    EXPECT_NEAR(machining::partToFace(p, Face::A, {10, 20, 20}).depth, -2, 1e-9);
}

TEST(FaceGeometry, OppositeAndEdgeFaces) {
    EXPECT_EQ(machining::oppositeFace(Face::A), Face::B);
    EXPECT_EQ(machining::oppositeFace(Face::B), Face::A);
    EXPECT_EQ(machining::oppositeFace(Face::L1), Face::L2);
    EXPECT_EQ(machining::oppositeFace(Face::W2), Face::W1);
    EXPECT_FALSE(machining::isEdgeFace(Face::A));
    EXPECT_FALSE(machining::isEdgeFace(Face::B));
    EXPECT_TRUE(machining::isEdgeFace(Face::L1));
    EXPECT_TRUE(machining::isEdgeFace(Face::W2));
}

TEST(FaceGeometry, AssemblyFrameOfFixtureLeftSide) {
    const auto model = domain::fixtures::basicCabinet();
    const auto& left = *model.assemblies[0].findPart(domain::PartId("cab1/side.left"));
    // Left side placement: local X -> +Z, local Y -> +Y, local Z -> -X, origin (18, 0, 0).
    // Face B (local z = 0) is the inner face at x = 18; point (u=346, v=37):
    //   assembly = (18, 0, 0) + 346*(0,0,1) + 37*(0,1,0) = (18, 37, 346).
    expectVec(machining::faceToAssembly(left, Face::B, 346, 37), {18, 37, 346});
    // 13 mm into the material from face B: local z = 13 -> x = 18 - 13 = 5.
    expectVec(machining::faceToAssembly(left, Face::B, 346, 37, 13), {5, 37, 346});
    // Face A (local z = 18) is the outer face at x = 0.
    expectVec(machining::faceToAssembly(left, Face::A, 346, 37), {0, 37, 346});
    const FaceFrame b = machining::faceFrameInAssembly(left, Face::B);
    expectVec(b.toolDir, {-1, 0, 0}); // drilling from the inside towards the outside
    expectVec(b.uAxis, {0, 0, 1});
    expectVec(b.vAxis, {0, 1, 0});
    const auto back = machining::assemblyToFace(left, Face::B, {5, 37, 346});
    EXPECT_NEAR(back.u, 346, 1e-9);
    EXPECT_NEAR(back.v, 37, 1e-9);
    EXPECT_NEAR(back.depth, 13, 1e-9);

    // Right side: origin (800, 0, 0), same axes -> face A (local z = 18) at x = 782 is inner.
    const auto& right = *model.assemblies[0].findPart(domain::PartId("cab1/side.right"));
    expectVec(machining::faceToAssembly(right, Face::A, 100, 50), {782, 50, 100});
    expectVec(machining::faceFrameInAssembly(right, Face::A).toolDir, {1, 0, 0});
}

TEST(FaceGeometry, HoleAxis) {
    const auto model = domain::fixtures::basicCabinet();
    const auto& left = *model.assemblies[0].findPart(domain::PartId("cab1/side.left"));
    const domain::Hole blind{Face::B, 346, 37, 5, 13, false};
    const auto a = machining::holeAxisInAssembly(left, blind);
    expectVec(a.start, {18, 37, 346});
    expectVec(a.direction, {-1, 0, 0});
    EXPECT_DOUBLE_EQ(a.length, 13);
    EXPECT_DOUBLE_EQ(a.radius, 2.5);
    const domain::Hole through{Face::A, 10, 10, 7, 0, true};
    EXPECT_DOUBLE_EQ(machining::effectiveDepth(left, through), 18); // panel thickness
    const domain::Hole edgeThrough{Face::L1, 10, 9, 5, 0, true};
    EXPECT_DOUBLE_EQ(machining::effectiveDepth(left, edgeThrough), 560); // panel width behind L1
    const auto local = machining::holeAxis(left, through);
    expectVec(local.start, {10, 10, 18});
    expectVec(local.direction, {0, 0, -1});
}

// ---- flip helpers -----------------------------------------------------------------

TEST(Flip, FaceBAboutLengthAxisMatchesARigidRotation) {
    const auto p = panel(600, 400, 18);
    // Physically flipping the panel about its length axis (local X) through the panel
    // centre is the rigid motion  p' = Rx(180) p + (0, W, T):  (x, y, z) -> (x, W-y, T-z).
    const auto flip = geometry::Transform::translation({0, 400, 18}) * geometry::Transform::rotationX(180);
    // Hole on face B at (u, v) = (100, 50): part point (100, 50, 0).
    const Vec3 moved = flip.applyPoint({100, 50, 0});
    expectVec(moved, {100, 350, 18}); // now on top (z = T) at (100, 400 - 50)
    const Vec2 view = machining::flippedFaceCoordinates(p, Face::B, {100, 50});
    EXPECT_NEAR(view.x, moved.x, 1e-9);
    EXPECT_NEAR(view.y, moved.y, 1e-9);
}

TEST(Flip, FaceBAboutWidthAxisMatchesARigidRotation) {
    const auto p = panel(600, 400, 18);
    // Flip about the width axis (local Y): (x, y, z) -> (L-x, y, T-z).
    const auto flip = geometry::Transform::translation({600, 0, 18}) * geometry::Transform::rotationY(180);
    const Vec3 moved = flip.applyPoint({100, 50, 0});
    expectVec(moved, {500, 50, 18});
    const Vec2 view = machining::flippedFaceCoordinates(p, Face::B, {100, 50}, machining::FlipAxis::Width);
    EXPECT_NEAR(view.x, 500, 1e-9);
    EXPECT_NEAR(view.y, 50, 1e-9);
}

TEST(Flip, MirrorOperationsForFace) {
    auto p = panel(600, 400, 18);
    domain::Operation hb{domain::OperationId("hb"), domain::Hole{Face::B, 100, 50, 5, 13, false}, "user", ""};
    domain::Operation ha{domain::OperationId("ha"), domain::Hole{Face::A, 100, 50, 5, 13, false}, "user", ""};
    domain::Operation gb{domain::OperationId("gb"), domain::Groove{Face::B, 0, 10, 600, 10, 4, 8}, "rule", ""};
    domain::Pocket pk;
    pk.face = Face::B;
    pk.contour = {{10, 10}, {60, 10}, {60, 40}, {10, 40}}; // CCW rectangle
    pk.depth = 5;
    domain::Operation pb{domain::OperationId("pb"), pk, "user", ""};
    p.operations = {hb, ha, gb, pb};

    const auto ops = machining::mirrorOperationsForFace(p); // face B, flip about the length axis
    ASSERT_EQ(ops.size(), 3u); // the face-A hole is not included
    EXPECT_EQ(ops[0].id.str(), "hb");
    EXPECT_EQ(std::get<domain::Hole>(ops[0].shape), (domain::Hole{Face::B, 100, 350, 5, 13, false})); // v' = 400 - 50
    EXPECT_EQ(ops[1].source, "rule");
    EXPECT_EQ(std::get<domain::Groove>(ops[1].shape), (domain::Groove{Face::B, 0, 390, 600, 390, 4, 8})); // 400 - 10
    const auto& c = std::get<domain::Pocket>(ops[2].shape).contour;
    // Mirrored points: (10,390) (60,390) (60,360) (10,360); reversed after the first to stay CCW.
    ASSERT_EQ(c.size(), 4u);
    EXPECT_EQ(c[0], (Vec2{10, 390}));
    EXPECT_EQ(c[1], (Vec2{10, 360}));
    EXPECT_EQ(c[2], (Vec2{60, 360}));
    EXPECT_EQ(c[3], (Vec2{60, 390}));
    EXPECT_GT(machining::signedArea(c), 0);
    EXPECT_NEAR(machining::signedArea(c), 50.0 * 30.0, 1e-9);
    // The part itself is untouched.
    EXPECT_EQ(std::get<domain::Hole>(p.operations[0].shape).v, 50);

    const auto w = machining::mirrorOperationsForFace(p, Face::B, machining::FlipAxis::Width);
    EXPECT_EQ(std::get<domain::Hole>(w[0].shape), (domain::Hole{Face::B, 500, 50, 5, 13, false})); // u' = 600 - 100
}
