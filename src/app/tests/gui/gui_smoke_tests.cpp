// SPDX-License-Identifier: AGPL-3.0-only
// GUI smoke test (label `gui`): needs a display (CTest runs it under xvfb-run); exits with 77
// (skipped) without one. Optional: FOS_APP_SHOT_DIR=<dir> writes 1440x900 screenshots of the
// four workspaces in English and Ukrainian.
#include "cutting_workspace.hpp"
#include "drawings_workspace.hpp"
#include "inspector_panel.hpp"
#include "main_window.hpp"
#include "production_workspace.hpp"
#include "project_browser.hpp"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

#include <gtest/gtest.h>

namespace fos::app::gui {
namespace {

double width(Session& s) { return s.project().items.at(0).cabinet()->parameters.value("width").valueOr(-1); }

void settle(int ms = 200) {
    QApplication::processEvents();
    QTest::qWait(ms);
}

/// True when the image contains something other than white paper.
bool hasInk(const QImage& image) {
    for (int y = 0; y < image.height(); y += 3)
        for (int x = 0; x < image.width(); x += 3)
            if (qGray(image.pixel(x, y)) < 128) return true;
    return false;
}

TEST(GuiSmoke, TemplateWidthDrawingsSaveReopen) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    MainWindow w;
    w.show();
    ASSERT_TRUE(w.newFromTemplate(TemplateKind::BaseCabinet, std::nullopt));
    settle();
    // 2 sides, bottom, 2 rails, plinth, back, shelf, 2 doors.
    EXPECT_EQ(w.browser()->partCount(), 10);
    EXPECT_EQ(w.session().selection().itemId, "cab1");

    // Width 900 through the inspector editor (as typed + Enter).
    ASSERT_TRUE(w.inspector()->submitParameter(QStringLiteral("width"), QStringLiteral("900")));
    settle();
    EXPECT_DOUBLE_EQ(width(w.session()), 900);
    EXPECT_DOUBLE_EQ(w.session().findPart("cab1/shelf.1")->length, 862); // 900 - 2*18 - 2*1
    EXPECT_TRUE(w.windowTitle().contains(QLatin1Char('*')));

    // Invalid input: inline engine message, nothing changes.
    EXPECT_FALSE(w.inspector()->submitParameter(QStringLiteral("width"), QStringLiteral("width +")));
    EXPECT_FALSE(w.inspector()->parameterError(QStringLiteral("width")).isEmpty());
    EXPECT_DOUBLE_EQ(width(w.session()), 900);

    // Drawings workspace renders a non-empty preview of the assembly drawing.
    w.workspaces()->setCurrentWidget(w.drawingsWorkspace());
    settle();
    ASSERT_NE(w.drawingsWorkspace()->drawing(), nullptr);
    ASSERT_FALSE(w.drawingsWorkspace()->preview().isNull());
    EXPECT_TRUE(hasInk(w.drawingsWorkspace()->preview()));
    w.workspaces()->setCurrentWidget(w.productionWorkspace());
    settle();
    EXPECT_GT(w.productionWorkspace()->cutListRows(), 0);
    w.workspaces()->setCurrentWidget(w.cuttingWorkspace());
    settle();
    EXPECT_EQ(w.cuttingWorkspace()->materialCount(), 2);

    const QString file = dir.filePath(QStringLiteral("smoke.fosp"));
    ASSERT_TRUE(w.saveToFile(file));
    EXPECT_FALSE(w.windowTitle().contains(QLatin1Char('*')));

    MainWindow reopened;
    ASSERT_TRUE(reopened.openFile(file, false));
    settle();
    EXPECT_DOUBLE_EQ(width(reopened.session()), 900);
    EXPECT_EQ(reopened.browser()->partCount(), 10);
    EXPECT_FALSE(reopened.session().isDirty());
}

void screenshots(const QString& language) {
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
        ASSERT_TRUE(w.newFromTemplate(TemplateKind::BaseCabinet, std::nullopt));
        ASSERT_TRUE(w.inspector()->submitParameter(QStringLiteral("width"), QStringLiteral("900")));
        settle(300);
        if (language == QLatin1String("uk")) {
            EXPECT_EQ(w.workspaces()->tabText(1), QString::fromUtf8("Креслення"));
            EXPECT_EQ(w.workspaces()->tabText(3), QString::fromUtf8("Розкрій"));
        }
        const char* names[] = {"3d", "drawings", "production", "cutting"};
        for (int i = 0; i < 4; ++i) {
            w.workspaces()->setCurrentIndex(i);
            settle(500);
            const QImage shot = w.grab().toImage();
            EXPECT_EQ(shot.width(), 1440);
            if (!shotDir.isEmpty()) {
                QDir().mkpath(shotDir);
                EXPECT_TRUE(shot.save(QDir(shotDir).filePath(QStringLiteral("%1-%2.png").arg(language, QLatin1String(names[i])))));
            }
        }
        // A part selected: the inspector shows its read-only facts.
        w.session().selectPart("cab1/shelf.1");
        w.workspaces()->setCurrentIndex(0);
        settle(300);
        if (!shotDir.isEmpty())
            w.grab().toImage().save(QDir(shotDir).filePath(QStringLiteral("%1-part.png").arg(language)));
    }
    if (language != QLatin1String("en")) QApplication::removeTranslator(&translator);
}

TEST(GuiSmoke, ScreenshotsEnglish) { screenshots(QStringLiteral("en")); }
TEST(GuiSmoke, ScreenshotsUkrainian) { screenshots(QStringLiteral("uk")); }

} // namespace
} // namespace fos::app::gui

int main(int argc, char** argv) {
    const bool offscreen = qEnvironmentVariable("QT_QPA_PLATFORM") == QLatin1String("offscreen");
    if (!offscreen && qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        std::fprintf(stderr, "No display: GUI smoke test skipped.\n");
        return 77;
    }
    QApplication app(argc, argv);
    // Isolated settings so the test never touches the user's preferences or recent files.
    QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QStandardPaths::setTestModeEnabled(true);
    QApplication::setOrganizationName(QStringLiteral("FurnitureOS-test"));
    QApplication::setApplicationName(QStringLiteral("furnitureos-gui-test"));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
