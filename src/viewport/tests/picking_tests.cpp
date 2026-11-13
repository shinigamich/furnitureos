// SPDX-License-Identifier: AGPL-3.0-only
// Widget-level picking without a GL context (the widget is never shown).
#include "fos/viewport/camera.hpp"
#include "fos/viewport/viewport_widget.hpp"

#include "fos/domain/fixtures.hpp"

#include <QCoreApplication>
#include <QMouseEvent>

#include <gtest/gtest.h>

using namespace fos;
using viewport::Projection;
using viewport::ViewportWidget;
using viewport::ViewPreset;

namespace {
struct Fixture {
    ViewportWidget w;
    Fixture() {
        w.resize(800, 600);
        w.setModel(domain::fixtures::basicCabinet());
    }
};
QPoint centre(const QWidget& w) { return {w.width() / 2, w.height() / 2}; }

void sendMouse(QWidget& w, QEvent::Type type, QPoint p, Qt::MouseButton b, Qt::KeyboardModifiers m = {}) {
    const Qt::MouseButtons held = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons(b);
    QMouseEvent e(type, QPointF(p), w.mapToGlobal(QPointF(p)), b, held, m);
    QCoreApplication::sendEvent(&w, &e);
}
} // namespace

// Front preset + fitAll -> camera target = centre of the cabinet bounds (400, 281.5, 360) and
// the centre ray runs along +Y through x = 400, z = 360. Parts whose x AND z ranges contain
// that line: shelf.1 (x 18..782, z 351..369, y 20..560) and back (x 2..798, z 2..718,
// y 560..563). Sides (x 0..18 / 782..800), bottom (z 0..18) and top (z 702..720) miss.
// The shelf front (y = 20) comes before the back (y = 560): the shelf is hit.
TEST(Picking, FrontCentreHitsShelfThenBack) {
    for (auto proj : {Projection::Perspective, Projection::Orthographic}) {
        Fixture f;
        f.w.setProjection(proj);
        f.w.setViewPreset(ViewPreset::Front);
        EXPECT_EQ(f.w.partAt(centre(f.w)), "cab1/shelf.1");
        f.w.setHiddenParts({"cab1/shelf.1"});
        EXPECT_EQ(f.w.partAt(centre(f.w)), "cab1/back");
        // Through the open front with shelf and back hidden: nothing.
        f.w.setHiddenParts({"cab1/shelf.1", "cab1/back"});
        EXPECT_EQ(f.w.partAt(centre(f.w)), "");
    }
}

TEST(Picking, OutsideCabinetIsEmptyAndOtherPresets) {
    Fixture f;
    f.w.setViewPreset(ViewPreset::Front);
    // Front, fitted: half height at the target 378 mm (360 * 1.05) over 300 px, so the cabinet
    // silhouette ends >= 15 px from the top/bottom edges and ~100 px from the sides; the
    // corner pixels are outside it.
    EXPECT_EQ(f.w.partAt({3, 3}), "");
    EXPECT_EQ(f.w.partAt({797, 597}), "");
    // Top: centre ray goes down at (400, 281.5): first the top panel (z 702..720).
    f.w.setViewPreset(ViewPreset::Top);
    EXPECT_EQ(f.w.partAt(centre(f.w)), "cab1/top");
    // Left (observer at -X): first the left side (x 0..18).
    f.w.setViewPreset(ViewPreset::Left);
    EXPECT_EQ(f.w.partAt(centre(f.w)), "cab1/side.left");
    // Back (observer at +Y): the back panel at y 560..563.
    f.w.setViewPreset(ViewPreset::Back);
    EXPECT_EQ(f.w.partAt(centre(f.w)), "cab1/back");
}

TEST(Picking, IsolationLimitsPickableParts) {
    Fixture f;
    f.w.setViewPreset(ViewPreset::Front);
    f.w.setIsolatedParts({"cab1/back"});
    EXPECT_EQ(f.w.partAt(centre(f.w)), "cab1/back");
    f.w.setIsolatedParts({"cab1/side.left"});
    EXPECT_EQ(f.w.partAt(centre(f.w)), "");
    f.w.setIsolatedParts({});
    EXPECT_EQ(f.w.partAt(centre(f.w)), "cab1/shelf.1");
}

TEST(Picking, ProjectedPartCentreIsPickedThroughWidgetCamera) {
    Fixture f;
    f.w.setViewPreset(ViewPreset::Isometric);
    // Centre of the top panel's upper face: (18 + 382, 280, 720) = (400, 280, 720).
    const auto px = f.w.camera().projectToPixel({400, 280, 720});
    ASSERT_TRUE(px);
    EXPECT_EQ(f.w.partAt(QPoint(int(px->x), int(px->y))), "cab1/top");
}

TEST(Picking, ClickEmitsPartAndDragDoesNot) {
    Fixture f;
    f.w.setViewPreset(ViewPreset::Front);
    std::vector<std::pair<QString, bool>> clicks;
    std::vector<QString> doubles;
    QObject::connect(&f.w, &ViewportWidget::partClicked,
                     [&](const QString& id, bool additive) { clicks.emplace_back(id, additive); });
    QObject::connect(&f.w, &ViewportWidget::partDoubleClicked, [&](const QString& id) { doubles.push_back(id); });
    const QPoint c = centre(f.w);

    sendMouse(f.w, QEvent::MouseButtonPress, c, Qt::LeftButton);
    sendMouse(f.w, QEvent::MouseMove, c + QPoint(2, 1), Qt::LeftButton); // below the 4 px threshold
    sendMouse(f.w, QEvent::MouseButtonRelease, c + QPoint(2, 1), Qt::LeftButton);
    ASSERT_EQ(clicks.size(), 1u);
    EXPECT_EQ(clicks[0].first, "cab1/shelf.1");
    EXPECT_FALSE(clicks[0].second);

    sendMouse(f.w, QEvent::MouseButtonPress, {3, 3}, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(f.w, QEvent::MouseButtonRelease, {3, 3}, Qt::LeftButton, Qt::ControlModifier);
    ASSERT_EQ(clicks.size(), 2u);
    EXPECT_EQ(clicks[1].first, "");
    EXPECT_TRUE(clicks[1].second);

    // A drag orbits the camera and emits nothing.
    const double yawBefore = f.w.camera().yaw();
    sendMouse(f.w, QEvent::MouseButtonPress, c, Qt::LeftButton);
    sendMouse(f.w, QEvent::MouseMove, c + QPoint(50, 0), Qt::LeftButton);
    sendMouse(f.w, QEvent::MouseButtonRelease, c + QPoint(50, 0), Qt::LeftButton);
    EXPECT_EQ(clicks.size(), 2u);
    EXPECT_NEAR(f.w.camera().yaw(), yawBefore - 50 * 0.4, 1e-9);

    // Double click: one partDoubleClicked, and the trailing release emits no extra click.
    f.w.setViewPreset(ViewPreset::Front);
    sendMouse(f.w, QEvent::MouseButtonPress, c, Qt::LeftButton);
    sendMouse(f.w, QEvent::MouseButtonRelease, c, Qt::LeftButton);
    sendMouse(f.w, QEvent::MouseButtonDblClick, c, Qt::LeftButton);
    sendMouse(f.w, QEvent::MouseButtonRelease, c, Qt::LeftButton);
    EXPECT_EQ(clicks.size(), 3u);
    ASSERT_EQ(doubles.size(), 1u);
    EXPECT_EQ(doubles[0], "cab1/shelf.1");
}

TEST(Picking, ProjectionSwitchKeepsFraming) {
    Fixture f;
    f.w.setViewPreset(ViewPreset::Front);
    const double hh = f.w.camera().halfHeightAtTarget();
    f.w.setProjection(Projection::Orthographic);
    EXPECT_EQ(f.w.projection(), Projection::Orthographic);
    EXPECT_DOUBLE_EQ(f.w.camera().halfHeightAtTarget(), hh);
    f.w.setDisplayMode(viewport::DisplayMode::Wireframe);
    EXPECT_EQ(f.w.displayMode(), viewport::DisplayMode::Wireframe);
}
