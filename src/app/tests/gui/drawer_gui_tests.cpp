// SPDX-License-Identifier: AGPL-3.0-only
// GUI step: drawer boxes switched on in the front editor of the drawer-unit template; the box
// parts appear in the project browser. With FOS_APP_SHOT_DIR=<dir> it writes drawer-boxes-en.png.
#include "fos/app/interior_rows.hpp"
#include "inspector_panel.hpp"
#include "interior_editor.hpp"
#include "main_window.hpp"
#include "project_browser.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QPushButton>
#include <QScrollBar>
#include <QTabWidget>
#include <QTest>

#include <gtest/gtest.h>

namespace fos::app::gui {
namespace {

void settleUi(int ms = 200) {
    QApplication::processEvents();
    QTest::qWait(ms);
}

TEST(DrawerGui, DrawerBoxesOnTheDrawerUnit) {
    MainWindow w;
    w.resize(1440, 900);
    w.show();
    ASSERT_TRUE(w.newFromTemplate(TemplateKind::DrawerUnit, std::nullopt));
    settleUi();
    Session& s = w.session();
    std::string path;
    std::size_t drawers = 0;
    for (const auto& r : sectionRows(s.project().items[0].cabinet()->interior))
        if (r.front && r.front->kind == furniture::FrontKind::DrawerStack) {
            path = r.path;
            drawers = r.front->drawerHeights.size();
        }
    ASSERT_GT(drawers, 0u);
    const int partsBefore = w.browser()->partCount();
    w.inspector()->cabinetTabs()->setCurrentIndex(2); // Interior
    settleUi(100);
    ASSERT_TRUE(w.inspector()->interiorEditor()->selectSection(QString::fromStdString(path)));
    settleUi(100);
    auto* box = w.inspector()->findChild<QComboBox*>(QStringLiteral("drawerBox"));
    auto* slide = w.inspector()->findChild<QComboBox*>(QStringLiteral("drawerSlide"));
    ASSERT_TRUE(box && slide);
    ASSERT_TRUE(box->isEnabled());
    EXPECT_FALSE(slide->isEnabled()); // fronts only: no slide to choose
    box->setCurrentIndex(box->findData(true));
    EXPECT_TRUE(slide->isEnabled());
    EXPECT_EQ(slide->count(), 2);
    auto* apply = w.inspector()->findChild<QPushButton*>(QStringLiteral("applyFront"));
    ASSERT_NE(apply, nullptr);
    apply->click();
    settleUi(300);

    // Five box parts per drawer (two sides, inner front, back, bottom) in the browser.
    EXPECT_EQ(w.browser()->partCount(), partsBefore + 5 * static_cast<int>(drawers));
    const QString bottomId = QStringLiteral("cab1/drawer.%1%2.bottom")
                                 .arg(path.empty() ? QString() : QString::fromStdString(path) + QLatin1Char('.'))
                                 .arg(1);
    EXPECT_NE(s.findPart(bottomId.toStdString()), nullptr);
    bool listed = false;
    for (QTreeWidgetItemIterator it(w.browser()); *it && !listed; ++it)
        listed = (*it)->data(0, Qt::UserRole + 1).toString() == bottomId; // browser id role
    EXPECT_TRUE(listed);

    const QString shotDir = qEnvironmentVariable("FOS_APP_SHOT_DIR");
    if (!shotDir.isEmpty()) {
        w.inspector()->cabinetTabs()->setCurrentIndex(2);
        settleUi(300);
        auto* front = w.inspector()->findChild<QPushButton*>(QStringLiteral("applyFront"));
        if (front) w.inspector()->ensureWidgetVisible(front);
        settleUi(400);
        QDir().mkpath(shotDir);
        EXPECT_TRUE(w.grab().toImage().save(QDir(shotDir).filePath(QStringLiteral("drawer-boxes-en.png"))));
    }

    // One undo removes the boxes again.
    ASSERT_TRUE(s.undo().ok());
    settleUi(200);
    EXPECT_EQ(w.browser()->partCount(), partsBefore);
}

} // namespace
} // namespace fos::app::gui
