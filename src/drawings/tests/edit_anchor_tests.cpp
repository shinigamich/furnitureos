// SPDX-License-Identifier: AGPL-3.0-only
// Edits that must stay truthful when the model changes: stale text overrides and user
// dimensions attached to model points (ModelAnchor).
#include "test_util.hpp"

using namespace fos;
using namespace fos::drawings;
using namespace fos::drawings::testing;

namespace {

const std::string kFront = "cab1/view.front";
const std::string kWidthId = kFront + "/dim.overall.width";

const GeneratedView* viewOf(const DrawingResult& r, const std::string& id) {
    for (const GeneratedView& v : r.views)
        if (v.view.id == id) return &v;
    return nullptr;
}

/// Texts drawn on the sheets for one element id.
std::vector<std::string> textsOf(const DrawingResult& r, const std::string& elementId) {
    std::vector<std::string> out;
    for (const Sheet& s : r.sheets)
        for (const Primitive& p : s.primitives)
            if (const Text* t = std::get_if<Text>(&p); t && t->elementId == elementId) out.push_back(t->text);
    return out;
}

std::vector<core::Diagnostic> withCode(const core::Diagnostics& ds, const std::string& code) {
    std::vector<core::Diagnostic> out;
    for (const auto& d : ds)
        if (d.code == code) out.push_back(d);
    return out;
}

/// Widens basicCabinet from 800 to 1203.5 mm (+403.5): the right side moves right and the
/// horizontal panels and the back get 403.5 mm longer.
void widen(domain::Model& m) {
    domain::Assembly& a = m.assemblies.front();
    a.findPart(domain::PartId("cab1/side.right"))->placement.origin.x += 403.5; // 800 -> 1203.5
    for (const char* id : {"cab1/bottom", "cab1/top", "cab1/shelf.1", "cab1/back"})
        a.findPart(domain::PartId(id))->length += 403.5;
}

/// Fit-to-page (default): the scale may differ between runs, which does not matter here.
AssemblyDrawingOptions options() { return {}; }

/// Front view: left side local corner 4 = (0,0,T) -> assembly (18 - 18, 0, 0) = (0,0,0)
/// (thickness runs along -X); right side local corner 0 = (0,0,0) -> (800,0,0).
/// Front view (u, v) = (x, z): u = 0 and u = 800, so the dimension measures 800.
UserDimension cornerToCorner() {
    UserDimension u;
    u.id = "user-width";
    u.viewId = kFront;
    u.orientation = DimensionOrientation::Horizontal;
    u.offset = -40.0;
    u.anchor1 = ModelAnchor{"cab1/side.left", 4, {}};
    u.anchor2 = ModelAnchor{"cab1/side.right", 0, {}};
    return u;
}

} // namespace

TEST(StaleOverride, OverrideIsFlaggedWhenTheMeasuredValueChanges) {
    domain::Model m = domain::fixtures::basicCabinet();
    const auto r0 = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), options());
    ASSERT_TRUE(r0.ok()) << r0.error().message;
    const Dimension* w0 = findDimension(viewOf(r0.value(), kFront)->annotations.dimensions, kWidthId);
    ASSERT_TRUE(w0);

    DrawingEdits edits;
    setTextOverride(edits.elements[kWidthId], *w0, "800");
    ASSERT_TRUE(edits.elements[kWidthId].measuredAtEdit);
    EXPECT_DOUBLE_EQ(*edits.elements[kWidthId].measuredAtEdit, 800.0); // carcass width

    // Unchanged model: override shown as typed, no finding.
    const auto r1 = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), options(), edits);
    ASSERT_TRUE(r1.ok());
    EXPECT_TRUE(withCode(r1.value().diagnostics, "drawings.edits.stale_override").empty());
    EXPECT_EQ(textsOf(r1.value(), kWidthId), std::vector<std::string>{"800"});

    // Width 800 -> 1203.5: the override no longer matches the geometry.
    widen(m);
    const auto r2 = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), options(), edits);
    ASSERT_TRUE(r2.ok());
    const auto stale = withCode(r2.value().diagnostics, "drawings.edits.stale_override");
    ASSERT_EQ(stale.size(), 1u);
    EXPECT_EQ(stale[0].severity, core::Severity::Warning);
    EXPECT_EQ(stale[0].entityId, kWidthId);
    EXPECT_NE(stale[0].message.find("'" + kWidthId + "'"), std::string::npos) << stale[0].message;
    EXPECT_NE(stale[0].message.find("measured 800 mm"), std::string::npos) << stale[0].message;
    EXPECT_NE(stale[0].message.find("now measures 1203.5 mm"), std::string::npos) << stale[0].message;
    const Dimension* w2 = findDimension(viewOf(r2.value(), kFront)->annotations.dimensions, kWidthId);
    ASSERT_TRUE(w2);
    EXPECT_DOUBLE_EQ(measuredValue(*w2), 1203.5);
    EXPECT_TRUE(w2->staleOverride);
    EXPECT_EQ(dimensionText(*w2), "800 *");
    EXPECT_EQ(textsOf(r2.value(), kWidthId), std::vector<std::string>{"800 *"}) << "flag is visible on the sheet";
    EXPECT_FALSE(core::hasErrors(r2.value().diagnostics));
}

TEST(StaleOverride, ToleranceAndLegacyOverrides) {
    domain::Model m = domain::fixtures::basicCabinet();
    DrawingEdits edits;
    edits.elements[kWidthId].textOverride = "approx. 800";
    edits.elements[kWidthId].measuredAtEdit = 800.0 + 0.5 * kStaleOverrideTolerance; // within tolerance
    const auto r1 = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), options(), edits);
    ASSERT_TRUE(r1.ok());
    EXPECT_TRUE(withCode(r1.value().diagnostics, "drawings.edits.stale_override").empty());

    edits.elements[kWidthId].measuredAtEdit = 799.9; // 0.1 mm off -> stale
    const auto r2 = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), options(), edits);
    ASSERT_TRUE(r2.ok());
    EXPECT_EQ(withCode(r2.value().diagnostics, "drawings.edits.stale_override").size(), 1u);

    // Legacy override without measuredAtEdit: cannot be checked (documented), drawn as typed.
    edits.elements[kWidthId].measuredAtEdit.reset();
    widen(m);
    const auto r3 = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), options(), edits);
    ASSERT_TRUE(r3.ok());
    EXPECT_TRUE(withCode(r3.value().diagnostics, "drawings.edits.stale_override").empty());
    EXPECT_EQ(textsOf(r3.value(), kWidthId), std::vector<std::string>{"approx. 800"});

    // measuredAtEdit on a note is meaningless and reported.
    const View v = generateAssemblyView(cabinet(m), ViewKind::Front);
    ViewAnnotations ann;
    Note n;
    n.id = v.id + "/note.x";
    ann.notes.push_back(n);
    DrawingEdits noteEdit;
    noteEdit.elements[n.id].measuredAtEdit = 1.0;
    EditTracker tracker;
    applyEditsToView(v, EditModelContext{&cabinet(m), nullptr}, ann, noteEdit, tracker);
    ASSERT_EQ(tracker.diagnostics.size(), 1u);
    EXPECT_EQ(tracker.diagnostics[0].code, "drawings.edits.not_applicable");
}

TEST(ModelAnchors, UserDimensionBetweenCornersFollowsTheModel) {
    domain::Model m = domain::fixtures::basicCabinet();
    DrawingEdits edits;
    UserDimension anchored = cornerToCorner();
    anchored.textOverride = "800";
    anchored.measuredAtEdit = 800.0;
    edits.dimensions.push_back(anchored);
    // Legacy fixed points at the same place (u = 0 .. 800, v = 0): they do NOT follow the model.
    edits.dimensions.push_back({"user-fixed", kFront, DimensionOrientation::Horizontal, {0, 0}, {800, 0}, -60.0, {}});

    const auto r1 = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), options(), edits);
    ASSERT_TRUE(r1.ok()) << r1.error().message;
    const auto& d1 = viewOf(r1.value(), kFront)->annotations.dimensions;
    const Dimension* a1 = findDimension(d1, "user-width");
    ASSERT_TRUE(a1);
    EXPECT_TRUE(samePoint(a1->p1, {0, 0}));
    EXPECT_TRUE(samePoint(a1->p2, {800, 0}));
    EXPECT_TRUE(a1->userAuthored);
    EXPECT_FALSE(a1->staleOverride);
    EXPECT_TRUE(r1.value().edits.orphanIds.empty());
    EXPECT_TRUE(r1.value().diagnostics.empty());

    widen(m); // right side corner 0 -> (1203.5, 0, 0)
    const auto r2 = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), options(), edits);
    ASSERT_TRUE(r2.ok());
    const auto& d2 = viewOf(r2.value(), kFront)->annotations.dimensions;
    const Dimension* a2 = findDimension(d2, "user-width");
    ASSERT_TRUE(a2);
    EXPECT_DOUBLE_EQ(measuredValue(*a2), 1203.5) << "the anchored dimension follows the model";
    EXPECT_TRUE(a2->staleOverride);
    EXPECT_EQ(textsOf(r2.value(), "user-width"), std::vector<std::string>{"800 *"});
    const auto stale = withCode(r2.value().diagnostics, "drawings.edits.stale_override");
    ASSERT_EQ(stale.size(), 1u);
    EXPECT_EQ(stale[0].entityId, "user-width");
    // The fixed-point dimension still measures 800 - this is the documented limitation.
    EXPECT_DOUBLE_EQ(measuredValue(*findDimension(d2, "user-fixed")), 800.0);

    // Without the text override the anchored dimension simply prints the new value.
    edits.dimensions[0].textOverride.reset();
    const auto r3 = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), options(), edits);
    ASSERT_TRUE(r3.ok());
    EXPECT_EQ(textsOf(r3.value(), "user-width"), std::vector<std::string>{"1203.5"});
}

TEST(ModelAnchors, ExplicitPointsVerticalAndMixedAnchors) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const View v = generateAssemblyView(cabinet(m), ViewKind::Front);
    const EditModelContext ctx{&cabinet(m), nullptr};
    // Shelf: translation (18, 20, 351); local point (100, 0, 18) -> (118, 20, 369) -> front (u,v) = (118, 369).
    const AnchorResolution r = resolveAnchor({"cab1/shelf.1", std::nullopt, {100, 0, 18}}, v, ctx);
    ASSERT_TRUE(r.point);
    EXPECT_TRUE(samePoint(*r.point, {118, 369}));
    // Top: translation (18, 0, 702); corner 0 -> (18, 0, 702) -> (18, 702).
    UserDimension u;
    u.id = "gap";
    u.viewId = v.id;
    u.orientation = DimensionOrientation::Vertical;
    u.anchor1 = ModelAnchor{"cab1/shelf.1", std::nullopt, {100, 0, 18}};
    u.anchor2 = ModelAnchor{"cab1/top", 0, {}};
    std::string problem;
    const auto d = resolveUserDimension(u, v, ctx, problem);
    ASSERT_TRUE(d) << problem;
    EXPECT_DOUBLE_EQ(measuredValue(*d), 333.0); // 702 - 369
    // Mixed: fixed p1 (u = 50) and anchored p2 (left side corner 4 -> u = 0): horizontal 50.
    UserDimension mixed;
    mixed.id = "mixed";
    mixed.viewId = v.id;
    mixed.p1 = {50, 0};
    mixed.anchor2 = ModelAnchor{"cab1/side.left", 4, {}};
    const auto dm = resolveUserDimension(mixed, v, ctx, problem);
    ASSERT_TRUE(dm);
    EXPECT_DOUBLE_EQ(measuredValue(*dm), 50.0);
}

TEST(ModelAnchors, MissingPartIsAnOrphanAndBadCornerIsReported) {
    domain::Model m = domain::fixtures::basicCabinet();
    DrawingEdits edits;
    edits.dimensions.push_back(cornerToCorner());
    auto& parts = m.assemblies.front().parts;
    parts.erase(parts.begin() + 1); // remove the right side
    const auto r = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), options(), edits);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const DrawingResult& d = r.value();
    EXPECT_FALSE(findDimension(viewOf(d, kFront)->annotations.dimensions, "user-width"));
    EXPECT_EQ(d.edits.orphanIds, std::vector<std::string>{"user-width"});
    const auto orphan = withCode(d.diagnostics, "drawings.edits.orphan_annotation");
    ASSERT_EQ(orphan.size(), 1u);
    EXPECT_EQ(orphan[0].entityId, "user-width");
    EXPECT_NE(orphan[0].message.find("'cab1/side.right'"), std::string::npos) << orphan[0].message;
    EXPECT_TRUE(withCode(d.diagnostics, "drawings.edits.bad_anchor").empty());

    // Corner out of range on an existing part: reported, not an orphan.
    domain::Model m2 = domain::fixtures::basicCabinet();
    DrawingEdits e2;
    UserDimension bad2 = cornerToCorner();
    bad2.anchor2->corner = 9;
    e2.dimensions.push_back(bad2);
    const auto r2 = generateAssemblyDrawing(m2, domain::AssemblyId("cab1"), options(), e2);
    ASSERT_TRUE(r2.ok());
    const auto b2 = withCode(r2.value().diagnostics, "drawings.edits.bad_anchor");
    ASSERT_EQ(b2.size(), 1u);
    EXPECT_NE(b2[0].message.find("corner 9"), std::string::npos) << b2[0].message;
    EXPECT_TRUE(r2.value().edits.orphanIds.empty());
}

TEST(ModelAnchors, LegacyApiWithoutGeometryMakesAnchoredDimensionsOrphans) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const View v = generateAssemblyView(cabinet(m), ViewKind::Front);
    DrawingEdits edits;
    edits.dimensions.push_back(cornerToCorner());
    ViewAnnotations ann;
    EditTracker tracker;
    applyEditsToView(v.id, ann, edits, tracker);
    EXPECT_TRUE(ann.dimensions.empty());
    const EditReport report = finishEdits(edits, tracker);
    EXPECT_EQ(report.orphanIds, std::vector<std::string>{"user-width"});
    ASSERT_EQ(report.diagnostics.size(), 1u);
    EXPECT_NE(report.diagnostics[0].message.find("without model geometry"), std::string::npos);
}

TEST(ModelAnchors, PartDrawingResolvesInThePartFrame) {
    domain::Model m = domain::fixtures::basicCabinet();
    const auto r0 = generatePartDrawing(m, domain::PartId("cab1/shelf.1"));
    ASSERT_TRUE(r0.ok()) << r0.error().message;
    const std::string viewId = r0.value().views.front().view.id;
    DrawingEdits edits;
    UserDimension u;
    u.id = "shelf-len";
    u.viewId = viewId;
    u.anchor1 = ModelAnchor{"cab1/shelf.1", 0, {}}; // (0, 0, 0)
    u.anchor2 = ModelAnchor{"cab1/shelf.1", 7, {}}; // (L, W, T) -> face A view u = L = 764
    edits.dimensions.push_back(u);
    UserDimension other = u;
    other.id = "other-part";
    other.anchor2 = ModelAnchor{"cab1/top", 1, {}}; // not the drawn part
    edits.dimensions.push_back(other);

    m.assemblies.front().findPart(domain::PartId("cab1/shelf.1"))->length = 1000.0;
    const auto r = generatePartDrawing(m, domain::PartId("cab1/shelf.1"), {}, edits);
    ASSERT_TRUE(r.ok());
    const Dimension* d = findDimension(r.value().views.front().annotations.dimensions, "shelf-len");
    ASSERT_TRUE(d);
    EXPECT_DOUBLE_EQ(measuredValue(*d), 1000.0);
    EXPECT_EQ(r.value().edits.orphanIds, std::vector<std::string>{"other-part"});
}
