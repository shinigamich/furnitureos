// SPDX-License-Identifier: AGPL-3.0-only
// Drawing annotation editor: picking a dimension by its primitives, offset / hide / text
// override (with the measured value recorded, so a model change flags it as stale), user
// dimensions between part corners that follow the model, orphans, refusals, undo / redo.
#include "fos/app/drawing_edits.hpp"
#include "fos/app/interior_rows.hpp"
#include "test_support.hpp"

namespace fos::app::test {
namespace {

using geometry::Vec2;

constexpr const char* kWidthDim = "cab1/view.front/dim.overall.width";

struct DrawingFixture : ::testing::Test {
    Session s;
    DrawingRequest request;
    drawings::DrawingResult drawing;
    std::string drawingId;

    void SetUp() override {
        ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
        request.id = "cab1";
        request.date = "2026-11-13";
        drawingId = drawingIdOf(request);
        regenerate();
    }
    void regenerate() {
        auto r = generateDrawing(s.project(), *s.resolved(), request);
        ASSERT_TRUE(r.ok()) << r.error().message;
        drawing = std::move(r).value();
    }
    DimensionRef dim(const std::string& id) {
        auto d = findDimension(drawing, id);
        EXPECT_TRUE(d.has_value()) << id;
        return d.value_or(DimensionRef{});
    }
    void run(const project::Command& c) {
        auto r = s.execute(c);
        ASSERT_TRUE(r.ok()) << r.error().message;
        regenerate();
    }
    void run(const core::Result<project::Command>& c) {
        ASSERT_TRUE(c.ok()) << c.error().message;
        run(c.value());
    }
};

TEST_F(DrawingFixture, IdsAndContext) {
    EXPECT_EQ(drawingId, "cab1/drawing.assembly");
    DrawingRequest part;
    part.kind = DrawingRequest::Kind::Part;
    part.id = "cab1/shelf.1";
    EXPECT_EQ(drawingIdOf(part), "cab1/shelf.1/drawing.part");
    const auto ctx = editContext(*s.resolved(), part);
    ASSERT_NE(ctx.part, nullptr);
    EXPECT_EQ(ctx.part->id.str(), "cab1/shelf.1");
    EXPECT_EQ(editContext(*s.resolved(), request).assembly, &s.resolved()->model.assemblies[0]);
}

TEST_F(DrawingFixture, HitTestFindsTheDimensionUnderTheCursor) {
    const auto width = dim(kWidthDim);
    EXPECT_DOUBLE_EQ(drawings::measuredValue(width.dimension), 600);
    EXPECT_FALSE(width.user);
    // Midpoint of one of its drawn lines (the dimension line or an extension line).
    const auto& sheet = drawing.sheets[drawing.views[width.viewIndex].sheetIndex];
    // The longest line of the element is the dimension line itself (extension lines can
    // coincide with those of the chain dimension next to it).
    std::optional<Vec2> onLine;
    double longest = 0;
    for (const auto& p : sheet.primitives)
        if (const auto* l = std::get_if<drawings::Line>(&p); l && l->elementId == kWidthDim) {
            const double len = geometry::length(l->b - l->a);
            if (len > longest) {
                longest = len;
                onLine = (l->a + l->b) * 0.5;
            }
        }
    ASSERT_TRUE(onLine.has_value());
    EXPECT_EQ(dimensionAt(drawing, 0, *onLine), kWidthDim);
    EXPECT_EQ(dimensionAt(drawing, 0, *onLine + Vec2{0.5, 0.5}), kWidthDim); // within 1.5 mm
    EXPECT_EQ(dimensionAt(drawing, 0, Vec2{-50, -50}), "");                   // off the sheet
    EXPECT_EQ(dimensionAt(drawing, 7, *onLine), "");                          // no such sheet
    const auto box = elementBounds(sheet, kWidthDim);
    EXPECT_TRUE(box.contains(*onLine));
    // The click is inside the front view's annotated box.
    const auto view = viewAt(drawing, 0, *onLine);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(drawing.views[*view].view.id, "cab1/view.front");
}

TEST_F(DrawingFixture, OffsetHideTextAndUndo) {
    const double original = dim(kWidthDim).dimension.offset;
    run(dimensionOffsetCommand(s.project(), drawingId, dim(kWidthDim), -40));
    EXPECT_DOUBLE_EQ(dim(kWidthDim).dimension.offset, -40);

    // Text override: the measured value 600 is recorded with it.
    run(dimensionTextCommand(s.project(), drawingId, dim(kWidthDim), QStringLiteral("approx. 600")));
    const auto& stored = s.project().drawingEdits.at(drawingId).elements.at(kWidthDim);
    EXPECT_EQ(stored.textOverride, std::optional<std::string>("approx. 600"));
    EXPECT_EQ(stored.measuredAtEdit, std::optional<double>(600));
    EXPECT_DOUBLE_EQ(*stored.offset, -40); // the offset edit is kept
    EXPECT_EQ(drawings::dimensionText(dim(kWidthDim).dimension), "approx. 600");
    EXPECT_TRUE(editFindings(drawing).empty());

    // Width 700: the override is now stale (flagged on the sheet and in the findings).
    ASSERT_TRUE(s.execute(project::commands::setParameter("cab1", "width", 700)).ok());
    regenerate();
    EXPECT_TRUE(dim(kWidthDim).dimension.staleOverride);
    EXPECT_EQ(drawings::dimensionText(dim(kWidthDim).dimension), "approx. 600 *");
    const auto findings = editFindings(drawing);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].code, "drawings.edits.stale_override");
    auto rows = drawingEditRows(s.project(), drawingId, &drawing);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_TRUE(rows[0].stale);
    EXPECT_FALSE(rows[0].orphan);
    EXPECT_EQ(editRowText(rows[0]), QStringLiteral("offset -40 mm, text “approx. 600”, STALE: the model value changed"));
    // Re-entering the text records the new value 700: no longer stale.
    run(dimensionTextCommand(s.project(), drawingId, dim(kWidthDim), QStringLiteral("approx. 700")));
    EXPECT_FALSE(dim(kWidthDim).dimension.staleOverride);
    EXPECT_EQ(s.project().drawingEdits.at(drawingId).elements.at(kWidthDim).measuredAtEdit, std::optional<double>(700));

    // Hide: not drawn, not pickable, listed as hidden.
    run(hideDimensionCommand(s.project(), drawingId, dim(kWidthDim)));
    EXPECT_TRUE(dim(kWidthDim).dimension.hidden);
    EXPECT_TRUE(elementBounds(drawing.sheets[0], kWidthDim).empty());
    rows = drawingEditRows(s.project(), drawingId, &drawing);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_TRUE(rows[0].hidden);

    // Empty text removes only the override.
    run(dimensionTextCommand(s.project(), drawingId, dim(kWidthDim), QString()));
    EXPECT_FALSE(s.project().drawingEdits.at(drawingId).elements.at(kWidthDim).textOverride.has_value());

    // Remove the edit -> the drawing has no stored edits at all.
    run(removeEditCommand(s.project(), drawingId, kWidthDim));
    EXPECT_EQ(s.project().drawingEdits.count(drawingId), 0u);
    EXPECT_DOUBLE_EQ(dim(kWidthDim).dimension.offset, original);
    EXPECT_FALSE(removeEditCommand(s.project(), drawingId, kWidthDim).ok()); // nothing left to remove

    // Undo the removal and the text removal: hidden + text again.
    ASSERT_TRUE(s.undo().ok());
    ASSERT_TRUE(s.undo().ok());
    regenerate();
    EXPECT_TRUE(dim(kWidthDim).dimension.hidden);
    EXPECT_EQ(dim(kWidthDim).dimension.textOverride, std::optional<std::string>("approx. 700"));
    ASSERT_TRUE(s.redo().ok());
    regenerate();
    EXPECT_FALSE(dim(kWidthDim).dimension.textOverride.has_value());
}

TEST_F(DrawingFixture, UserDimensionBetweenCornersFollowsTheModel) {
    const auto& front = drawing.views[dim(kWidthDim).viewIndex];
    const auto ctx = editContext(*s.resolved(), request);
    // Front view (u, v) = (x, z): the cabinet spans x 0..600, z 0..820 (sides to the floor).
    const auto a = nearestCorner(front, ctx, Vec2{-3, -2});
    const auto b = nearestCorner(front, ctx, Vec2{604, 830});
    ASSERT_TRUE(a && b);
    EXPECT_EQ(a->anchor.partId, "cab1/side.left");
    EXPECT_EQ(b->anchor.partId, "cab1/side.right");
    EXPECT_EQ(a->viewPoint, (Vec2{0, 0}));
    EXPECT_EQ(b->viewPoint, (Vec2{600, 820}));
    EXPECT_DOUBLE_EQ(a->distance, std::hypot(3.0, 2.0));
    EXPECT_EQ(a->partName, "Left side");

    run(addUserDimensionCommand(s.project(), drawingId, front, ctx, drawings::DimensionOrientation::Vertical, a->anchor,
                                b->anchor, 15));
    auto u = dim("user.dim.1");
    EXPECT_TRUE(u.user);
    EXPECT_DOUBLE_EQ(drawings::measuredValue(u.dimension), 820);
    EXPECT_DOUBLE_EQ(u.dimension.offset, 15);
    // It is pickable like the generated ones and listed as a user dimension.
    EXPECT_FALSE(elementBounds(drawing.sheets[0], "user.dim.1").empty());
    auto rows = drawingEditRows(s.project(), drawingId, &drawing);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].kind, DrawingEditRow::Kind::UserDimension);

    // Offset and text work on the stored user dimension; a user dimension cannot be hidden.
    run(dimensionOffsetCommand(s.project(), drawingId, dim("user.dim.1"), 25));
    EXPECT_DOUBLE_EQ(dim("user.dim.1").dimension.offset, 25);
    run(dimensionTextCommand(s.project(), drawingId, dim("user.dim.1"), QStringLiteral("820 total")));
    EXPECT_EQ(s.project().drawingEdits.at(drawingId).dimensions.at(0).measuredAtEdit, std::optional<double>(820));
    EXPECT_FALSE(hideDimensionCommand(s.project(), drawingId, dim("user.dim.1")).ok());

    // Height 900: the anchors follow the model, the override is stale.
    ASSERT_TRUE(s.execute(project::commands::setParameter("cab1", "height", 900)).ok());
    regenerate();
    EXPECT_DOUBLE_EQ(drawings::measuredValue(dim("user.dim.1").dimension), 900);
    EXPECT_TRUE(dim("user.dim.1").dimension.staleOverride);
    rows = drawingEditRows(s.project(), drawingId, &drawing);
    EXPECT_TRUE(rows.at(0).stale);

    // A second user dimension gets the next id; the horizontal 0..600 between the same corners.
    const auto& front2 = drawing.views[dim(kWidthDim).viewIndex];
    const auto ctx2 = editContext(*s.resolved(), request);
    run(addUserDimensionCommand(s.project(), drawingId, front2, ctx2, drawings::DimensionOrientation::Horizontal,
                                a->anchor, b->anchor, 10));
    EXPECT_DOUBLE_EQ(drawings::measuredValue(dim("user.dim.2").dimension), 600);

    // Removing it deletes only that user dimension.
    run(removeEditCommand(s.project(), drawingId, "user.dim.2"));
    EXPECT_FALSE(findDimension(drawing, "user.dim.2").has_value());
    EXPECT_TRUE(findDimension(drawing, "user.dim.1").has_value());
}

TEST_F(DrawingFixture, RefusedUserDimensionsAndOrphans) {
    const auto& front = drawing.views[dim(kWidthDim).viewIndex];
    const auto ctx = editContext(*s.resolved(), request);
    const drawings::ModelAnchor corner0{"cab1/side.left", 0, {}};
    const auto revision = s.revision();
    // Missing part, bad corner, same point twice: refused before reaching the document.
    auto missing = addUserDimensionCommand(s.project(), drawingId, front, ctx, drawings::DimensionOrientation::Horizontal,
                                           corner0, {"cab1/nope", 1, {}}, 10);
    ASSERT_FALSE(missing.ok());
    EXPECT_NE(missing.error().message.find("cab1/nope"), std::string::npos) << missing.error().message;
    EXPECT_FALSE(addUserDimensionCommand(s.project(), drawingId, front, ctx, drawings::DimensionOrientation::Horizontal,
                                         corner0, {"cab1/side.right", 9, {}}, 10)
                     .ok());
    EXPECT_FALSE(addUserDimensionCommand(s.project(), drawingId, front, ctx, drawings::DimensionOrientation::Horizontal,
                                         corner0, corner0, 10)
                     .ok());
    EXPECT_EQ(s.revision(), revision);
    EXPECT_EQ(s.project().drawingEdits.count(drawingId), 0u);

    // A user dimension on the shelf becomes an orphan when the shelf is removed.
    const drawings::ModelAnchor shelfA{"cab1/shelf.1", 0, {}}, shelfB{"cab1/shelf.1", 1, {}};
    run(addUserDimensionCommand(s.project(), drawingId, front, ctx, drawings::DimensionOrientation::Horizontal, shelfA,
                                shelfB, 10));
    EXPECT_DOUBLE_EQ(drawings::measuredValue(dim("user.dim.1").dimension), 562);
    // And an element edit for an id the drawing does not have.
    run(project::commands::setElementEdit(drawingId, "cab1/view.front/dim.bogus", drawings::ElementEdit{{}, {}, true, {}, {}}));
    run(project::commands::setShelfCount("cab1", "", 0));
    auto rows = drawingEditRows(s.project(), drawingId, &drawing);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].id, "cab1/view.front/dim.bogus");
    EXPECT_TRUE(rows[0].orphan);
    EXPECT_TRUE(rows[1].orphan);
    const auto findings = editFindings(drawing);
    ASSERT_EQ(findings.size(), 2u);
    EXPECT_EQ(findings[0].code, "drawings.edits.orphan");
    EXPECT_EQ(findings[1].code, "drawings.edits.orphan_annotation");
    EXPECT_EQ(editRowText(rows[0]), QStringLiteral("hidden, ORPHAN: not in the drawing any more"));
    // Removing both orphans clears the findings.
    run(removeEditCommand(s.project(), drawingId, rows[0].id));
    run(removeEditCommand(s.project(), drawingId, rows[1].id));
    EXPECT_TRUE(editFindings(drawing).empty());
    EXPECT_EQ(s.project().drawingEdits.count(drawingId), 0u);
}

} // namespace
} // namespace fos::app::test
