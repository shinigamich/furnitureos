// SPDX-License-Identifier: AGPL-3.0-only
// OpenGL smoke tests. They need a windowing Qt platform (e.g. xcb under xvfb-run) and an
// OpenGL 3.3 core context; otherwise every test is SKIPPED with the reason.
#include "fos/viewport/camera.hpp"
#include "fos/viewport/viewport_widget.hpp"

#include "fos/domain/fixtures.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace fos;
using viewport::Projection;
using viewport::ViewportWidget;
using viewport::ViewPreset;

namespace {

/// Empty when GL rendering through QOpenGLWidget is possible, otherwise the reason.
std::string glUnavailableReason() {
    const QString platform = QGuiApplication::platformName();
    if (platform == "offscreen" || platform == "minimal")
        return "Qt platform '" + platform.toStdString() +
               "' cannot render QOpenGLWidget; run under xvfb-run or a real display.";
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext ctx;
    ctx.setFormat(fmt);
    if (!ctx.create()) return "Could not create an OpenGL context.";
    QOffscreenSurface surface;
    surface.setFormat(ctx.format());
    surface.create();
    if (!ctx.makeCurrent(&surface)) return "Could not make an OpenGL context current.";
    const auto v = ctx.format().version();
    ctx.doneCurrent();
    if (ctx.isOpenGLES() || v < qMakePair(3, 3))
        return "OpenGL " + std::to_string(v.first) + "." + std::to_string(v.second) + " < 3.3 core.";
    return {};
}

#define REQUIRE_GL()                                                                                   \
    do {                                                                                               \
        static const std::string reason = glUnavailableReason();                                       \
        if (!reason.empty()) { GTEST_SKIP() << reason; }                                               \
    } while (0)

QImage render(ViewportWidget& w) {
    if (!w.isVisible()) {
        w.show();
        for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();
    }
    return w.grabFramebuffer().convertToFormat(QImage::Format_RGB32);
}

QColor centrePixel(const QImage& img) { return img.pixelColor(img.width() / 2, img.height() / 2); }

int colourDistance(QColor a, QColor b) {
    return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
}

const QColor kBackground(236, 238, 242); // detail::kBackground*

} // namespace

TEST(ViewportGl, RendersCabinetAndSelectionHighlight) {
    REQUIRE_GL();
    ViewportWidget w;
    w.resize(800, 600);
    w.setModel(domain::fixtures::basicCabinet());
    w.setProjection(Projection::Orthographic);
    w.setViewPreset(ViewPreset::Front);
    ASSERT_EQ(w.partAt({400, 300}), "cab1/shelf.1"); // what the CPU picker sees at the centre

    QImage img = render(w);
    ASSERT_FALSE(img.isNull());
    // A corner far outside the cabinet shows the background (or a grid line, which is grey).
    EXPECT_LT(colourDistance(img.pixelColor(img.width() - 3, 3), kBackground), 12);
    // The shelf front face (beige 222/204/170, lit) at the projected cabinet centre.
    const QColor shelf = centrePixel(img);
    EXPECT_GT(colourDistance(shelf, kBackground), 40) << shelf.name().toStdString();
    EXPECT_GT(shelf.red(), shelf.blue() + 20) << shelf.name().toStdString(); // warm beige, not grey

    // Selected: highlight colour (255, 140, 0) scaled by the lighting factor 0.85..1.0
    // -> red 217..255, green 119..140, blue 0 (+ a little MSAA tolerance).
    w.setSelection({"cab1/shelf.1"});
    img = render(w);
    const QColor sel = centrePixel(img);
    EXPECT_GE(sel.red(), 210) << sel.name().toStdString();
    EXPECT_GE(sel.green(), 112);
    EXPECT_LE(sel.green(), 146);
    EXPECT_LE(sel.blue(), 15);

    // Hidden shelf: the (unselected) HDF back is visible behind it instead.
    w.setSelection({});
    w.setHiddenParts({"cab1/shelf.1"});
    img = render(w);
    const QColor back = centrePixel(img);
    EXPECT_GT(colourDistance(back, sel), 100);
    EXPECT_LT(std::abs(back.red() - back.blue()), 20) << back.name().toStdString(); // near-white HDF
}

TEST(ViewportGl, IsometricScreenshotAndFrameTime) {
    REQUIRE_GL();
    ViewportWidget w;
    w.resize(1280, 800);
    w.setModel(domain::fixtures::basicCabinet());
    w.setViewPreset(ViewPreset::Isometric);
    w.setSelection({"cab1/shelf.1"});
    QImage img = render(w);
    ASSERT_EQ(img.width(), 1280 * int(w.devicePixelRatioF()));
    if (const char* path = std::getenv("FOS_VIEWPORT_SCREENSHOT")) {
        EXPECT_TRUE(img.save(path));
    }
    // Centre of the top panel's upper face renders as a part, not background.
    const auto px = w.camera().projectToPixel({400, 280, 720});
    ASSERT_TRUE(px);
    EXPECT_GT(colourDistance(img.pixelColor(int(px->x), int(px->y)), kBackground), 30);

    // Performance: 334 cabinets in a grid = 2004 parts.
    domain::Model big = domain::fixtures::basicCabinet();
    const auto proto = big.assemblies[0];
    big.assemblies.clear();
    for (int i = 0; i < 334; ++i) {
        auto a = proto;
        a.id = domain::AssemblyId("cab" + std::to_string(i));
        for (auto& p : a.parts) p.id = domain::PartId(a.id.str() + "/" + p.id.str());
        a.placement = geometry::Transform::translation({(i % 20) * 1000.0, (i / 20) * 1000.0, 0});
        big.assemblies.push_back(a);
    }
    QElapsedTimer t;
    t.start();
    w.setModel(big, true);
    img = render(w); // includes the GPU upload
    const double firstMs = double(t.nsecsElapsed()) / 1e6;
    constexpr int kFrames = 20;
    t.restart();
    for (int i = 0; i < kFrames; ++i) {
        viewport::Camera c = w.camera();
        c.orbit(5, 0);
        w.setCamera(c);
        img = render(w);
    }
    const double frameMs = double(t.nsecsElapsed()) / 1e6 / kFrames;
    std::printf("[ PERF     ] 2004 parts, 1280x800: setModel+first frame %.1f ms, "
                "orbit frame incl. readback %.1f ms\n",
                firstMs, frameMs);
    EXPECT_FALSE(img.isNull());
}
