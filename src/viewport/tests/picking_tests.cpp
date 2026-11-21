// SPDX-License-Identifier: AGPL-3.0-only
// Widget-level picking without a GL context (the widget is never shown).
#include "fos/viewport/camera.hpp"
#include "fos/viewport/viewport_widget.hpp"

#include "fos/domain/fixtures.hpp"

#include <QCoreApplication>
#include <QKeyEvent>
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

// Section plane Y = 560.5 keeping y >= 560.5 (flip): the shelf (y 20..560) lies entirely on the
// removed side and is never picked; the centre ray reaches the back (y 560..563), which is cut.
// With Y = 300 (keep y >= 300) the shelf is cut and the ray hits its cap at y = 300.
TEST(Picking, SectionPlaneIgnoresRemovedParts) {
    for (auto proj : {Projection::Perspective, Projection::Orthographic}) {
        Fixture f;
        f.w.setProjection(proj);
        f.w.setViewPreset(ViewPreset::Front);
        f.w.setSectionPlane(viewport::SectionPlane{viewport::SectionAxis::Y, 560.5, true});
        EXPECT_EQ(f.w.partAt(centre(f.w)), "cab1/back");
        f.w.setSectionPlane(viewport::SectionPlane{viewport::SectionAxis::Y, 300, true});
        EXPECT_EQ(f.w.partAt(centre(f.w)), "cab1/shelf.1");
        // Keep y <= 10: everything the centre ray could hit is removed.
        f.w.setSectionPlane(viewport::SectionPlane{viewport::SectionAxis::Y, 10, false});
        EXPECT_EQ(f.w.partAt(centre(f.w)), "");
        // A click on the removed shelf selects nothing.
        f.w.setSectionPlane(viewport::SectionPlane{viewport::SectionAxis::Y, 560.5, true});
        f.w.setHiddenParts({"cab1/back"});
        QString clicked = QStringLiteral("unset");
        QObject::connect(&f.w, &ViewportWidget::partClicked, [&](const QString& id, bool) { clicked = id; });
        sendMouse(f.w, QEvent::MouseButtonPress, centre(f.w), Qt::LeftButton);
        sendMouse(f.w, QEvent::MouseButtonRelease, centre(f.w), Qt::LeftButton);
        EXPECT_EQ(clicked, QString());
        f.w.setSectionPlane(std::nullopt);
        EXPECT_EQ(f.w.partAt(centre(f.w)), "cab1/shelf.1");
    }
}

// Measure mode: two clicks on snapped corners give the distance and no selection request.
// Front ortho view: shelf front corners (18, 20, 369) and (782, 20, 369): distance 764.
TEST(Picking, MeasureTwoCornersAndEscape) {
    Fixture f;
    f.w.setProjection(Projection::Orthographic);
    f.w.setViewPreset(ViewPreset::Front);
    f.w.setHiddenParts({"cab1/side.left", "cab1/side.right", "cab1/top", "cab1/bottom", "cab1/back"});
    int clicks = 0;
    QObject::connect(&f.w, &ViewportWidget::partClicked, [&](const QString&, bool) { ++clicks; });
    f.w.setMeasureMode(true);
    for (geometry::Vec3 p : {geometry::Vec3{18, 20, 369}, geometry::Vec3{782, 20, 369}}) {
        const auto px = f.w.camera().projectToPixel(p);
        ASSERT_TRUE(px);
        const QPoint q(int(px->x) + 2, int(px->y) + 1);
        sendMouse(f.w, QEvent::MouseButtonPress, q, Qt::LeftButton);
        sendMouse(f.w, QEvent::MouseButtonRelease, q, Qt::LeftButton);
    }
    EXPECT_EQ(clicks, 0);
    const auto m = f.w.measurement();
    ASSERT_TRUE(m);
    EXPECT_NEAR(m->distance(), 764.0, 1e-6);
    EXPECT_NEAR(m->delta().x, 764.0, 1e-6);
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&f.w, &esc);
    EXPECT_FALSE(f.w.measurement());
    EXPECT_TRUE(f.w.measureMode());
    QCoreApplication::sendEvent(&f.w, &esc);
    EXPECT_FALSE(f.w.measureMode());
}
