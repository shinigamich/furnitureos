// SPDX-License-Identifier: AGPL-3.0-only
// GUI smoke test of the Shape section (contoured parts) through the widgets. main() is in
// gui_smoke_tests.cpp. With FOS_APP_SHOT_DIR=<dir> it writes shape-editor-en.png / -uk.png.
#include "cutting_workspace.hpp"
#include "drawings_workspace.hpp"
#include "fos/app/contour_rows.hpp"
#include "inspector_panel.hpp"
#include "main_window.hpp"
#include "shape_editor.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QTranslator>

#include <gtest/gtest.h>

#include <numbers>

namespace fos::app::gui {
namespace {

void wait(int ms = 150) {
    QApplication::processEvents();
    QTest::qWait(ms);
}

ShapeEditor* editor(MainWindow& w) {
    wait();
    return w.inspector()->shapeEditor();
}

template <class W> W* child(QWidget* parent, const char* name) {
    return parent->findChild<W*>(QString::fromLatin1(name));
}

void selectKind(ShapeEditor* e, furniture::ContourKind kind) {
    auto* combo = child<QComboBox>(e, "shapeKind");
    ASSERT_NE(combo, nullptr);
    combo->setCurrentIndex(combo->findData(static_cast<int>(kind)));
}

double outlineArea(Session& s, const std::string& partId) {
    const domain::Part* p = s.findPart(partId);
    return p ? shapeFacts(*p).outlineArea : -1;
}

/// Open shelving 800 x 1800 x 300; shelf.1.1 is 762 long (800 - 2*18 - 2*1) by its width W
/// (read from the part). R 30 on the two front corners removes 2 * (900 - 225 pi) mm2.
TEST(ShapeGui, RoundedShelfCutoutRefusalUndo) {
    MainWindow w;
    w.show();
    ASSERT_TRUE(w.newFromTemplate(TemplateKind::OpenShelving, std::nullopt));
    Session& s = w.session();
    const std::string shelf = "cab1/shelf.1.1";
    s.selectPart(shelf);
    const domain::Part* rect = s.findPart(shelf);
    ASSERT_NE(rect, nullptr);
    ASSERT_DOUBLE_EQ(rect->length, 762);
    const double L = rect->length, W = rect->width;

    ShapeEditor* e = editor(w);
    ASSERT_NE(e, nullptr);
    selectKind(e, furniture::ContourKind::RoundedCorners);
    child<QLineEdit>(e, "shapeRadius:0")->setText(QStringLiteral("30"));
    child<QLineEdit>(e, "shapeRadius:1")->setText(QStringLiteral("30"));
    child<QPushButton>(e, "shapeApply")->click();
    wait();
    EXPECT_NEAR(outlineArea(s, shelf), L * W - 2 * (900 - 225 * std::numbers::pi), 1e-6);
    auto* summary = w.inspector()->findChild<QLabel*>(QStringLiteral("partShape"));
    ASSERT_NE(summary, nullptr);
    EXPECT_EQ(summary->text(), QStringLiteral("shaped part (routed)"));
    ASSERT_NE(w.inspector()->findChild<QLabel*>(QStringLiteral("partOutlineArea")), nullptr);

    // The editor shows the stored spec after the rebuild; an invalid radius is refused inline.
    e = editor(w);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(child<QLineEdit>(e, "shapeRadius:0")->text(), QStringLiteral("30"));
    const auto revision = s.revision();
    child<QLineEdit>(e, "shapeRadius:0")->setText(QStringLiteral("5000"));
    EXPECT_FALSE(e->apply());
    EXPECT_TRUE(e->errorText().contains(QStringLiteral("furniture.contour.invalid"))) << e->errorText().toStdString();
    EXPECT_EQ(s.revision(), revision);
    EXPECT_NEAR(outlineArea(s, shelf), L * W - 2 * (900 - 225 * std::numbers::pi), 1e-6);
    // Reset brings back the stored value.
    child<QPushButton>(e, "shapeReset")->click();
    EXPECT_EQ(child<QLineEdit>(e, "shapeRadius:0")->text(), QStringLiteral("30"));
    EXPECT_TRUE(e->errorText().isEmpty());

    // 3D / drawings show the shaped shelf (the drawing preview is not empty).
    w.workspaces()->setCurrentWidget(w.drawingsWorkspace());
    wait(300);
    EXPECT_FALSE(w.drawingsWorkspace()->preview().isNull());

    // A free panel with a circular cut-out (centre 300, 150; R 20): 600*300 - 400 pi.
    project::Item panel{project::PanelItem{
        domain::AssemblyId("p1"), geometry::Transform{},
        furniture::makePanel(domain::PartId("p1/panel"), "Filler",
                             domain::MaterialId(project::defaults::kParticleboard18))}};
    ASSERT_TRUE(s.execute(project::commands::addItem(panel)).ok());
    s.selectItem("p1");
    e = editor(w);
    ASSERT_NE(e, nullptr);
    child<QPushButton>(e, "shapeAddCircleCutout")->click();
    auto* cutouts = child<QTableWidget>(e, "shapeCutouts");
    ASSERT_EQ(cutouts->rowCount(), 1);
    cutouts->item(0, 1)->setText(QStringLiteral("300"));
    cutouts->item(0, 2)->setText(QStringLiteral("150"));
    cutouts->item(0, 5)->setText(QStringLiteral("20"));
    ASSERT_TRUE(e->apply());
    wait();
    EXPECT_NEAR(outlineArea(s, "p1/panel"), 600.0 * 300.0 - 400.0 * std::numbers::pi, 1e-6);

    // Cutting lists both shaped parts as routed (not in the saw layouts).
    w.workspaces()->setCurrentWidget(w.cuttingWorkspace());
    wait(300);
    EXPECT_EQ(w.cuttingWorkspace()->routedPartCount(), 2);

    // Undo: panel cut-out, panel, shelf shape.
    ASSERT_TRUE(s.undo().ok());
    EXPECT_FALSE(s.findPart("p1/panel")->shape.has_value());
    ASSERT_TRUE(s.undo().ok());
    ASSERT_TRUE(s.undo().ok());
    EXPECT_FALSE(s.findPart(shelf)->shape.has_value());
    wait(300);
    EXPECT_EQ(w.cuttingWorkspace()->routedPartCount(), 0);
}

void shapeScreenshot(const QString& language) {
    const QString shotDir = qEnvironmentVariable("FOS_APP_SHOT_DIR");
    QTranslator translator;
    if (language != QLatin1String("en")) {
        ASSERT_TRUE(translator.load(QStringLiteral("furnitureos_") + language, QStringLiteral(FOS_APP_QM_DIR)));
        QApplication::installTranslator(&translator);
    }
    {
        MainWindow w;
        w.resize(1440, 900);
        w.show();
        ASSERT_TRUE(w.newFromTemplate(TemplateKind::OpenShelving, std::nullopt));
        Session& s = w.session();
        const auto target = contourTarget(s.project(), "cab1", "cab1/shelf.1.1");
        ContourInput in;
        in.kind = furniture::ContourKind::RoundedCorners;
        in.radii = {QStringLiteral("30"), QStringLiteral("30"), {}, {}};
        in.cutouts.push_back({furniture::CutoutKind::Circle, QStringLiteral("contour.length / 2"),
                              QStringLiteral("150"), {}, {}, QStringLiteral("17.5")});
        ASSERT_TRUE(applyContourInput(s, *target, in).ok());
        s.selectPart("cab1/shelf.1.1");
        ShapeEditor* e = editor(w);
        ASSERT_NE(e, nullptr);
        w.inspector()->ensureWidgetVisible(e, 0, 40);
        wait(500);
        const QImage shot = w.grab().toImage();
        EXPECT_EQ(shot.size(), w.size());
        if (!shotDir.isEmpty()) {
            QDir().mkpath(shotDir);
            EXPECT_TRUE(shot.save(QDir(shotDir).filePath(QStringLiteral("shape-editor-%1.png").arg(language))));
        }
    }
    if (language != QLatin1String("en")) QApplication::removeTranslator(&translator);
}

TEST(ShapeGui, ScreenshotEnglish) { shapeScreenshot(QStringLiteral("en")); }
TEST(ShapeGui, ScreenshotUkrainian) { shapeScreenshot(QStringLiteral("uk")); }

} // namespace
} // namespace fos::app::gui
