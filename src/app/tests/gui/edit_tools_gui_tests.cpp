// SPDX-License-Identifier: AGPL-3.0-only
// GUI step: free modelling — Add panel dialog, multi-selection (inspector box), copy / paste
// through the clipboard, plan-view arrow nudge and multi-item delete as one undo step.
#include "add_panel_dialog.hpp"
#include "edit_tools_controller.hpp"
#include "fos/app/edit_tools.hpp"
#include "inspector_panel.hpp"
#include "main_window.hpp"
#include "plan_canvas.hpp"
#include "plan_workspace.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QTabWidget>
#include <QTest>

#include <gtest/gtest.h>

namespace fos::app::gui {
namespace {

void settle(int ms = 100) {
    QApplication::processEvents();
    QTest::qWait(ms);
}

TEST(EditToolsGui, AddPanelMultiSelectCopyPasteNudgeDelete) {
    MainWindow w;
    w.resize(1440, 900);
    w.show();
    Session& s = w.session();
    s.newProject();
    settle();

    // Add panel dialog without a selection: origin placement, live preview.
    {
        AddPanelDialog dialog(s, {}, &w);
        dialog.findChild<QDoubleSpinBox*>(QStringLiteral("panelLength"))->setValue(800);
        const auto r = dialog.request();
        EXPECT_EQ(r.placement, PanelPlacement::Origin);
        EXPECT_DOUBLE_EQ(r.length, 800);
        EXPECT_TRUE(dialog.findChild<QLabel*>(QStringLiteral("panelPreview"))->text().contains(QStringLiteral("800")));
        ASSERT_TRUE(applyAddPanel(s, r).ok());
    }
    // A second panel to the right of the first (dialog defaults to "On top" with a selection).
    {
        AddPanelDialog dialog(s, s.selection().allItems(), &w);
        EXPECT_EQ(dialog.request().placement, PanelPlacement::OnTop);
        dialog.setPlacement(PanelPlacement::RightOf);
        ASSERT_TRUE(applyAddPanel(s, dialog.request()).ok());
    }
    ASSERT_EQ(s.project().items.size(), 2u);
    settle();

    // Multi-selection: inspector shows the common actions.
    s.selectItems({"panel1", "panel2"});
    settle();
    auto* box = w.inspector()->findChild<QGroupBox*>(QStringLiteral("multiSelection"));
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(box->title().contains(QStringLiteral("2")));

    // Copy + paste: two new items beside the originals, selected, one undo step.
    const auto steps = s.document().undoCount();
    w.editTools()->copy();
    w.editTools()->paste();
    settle();
    EXPECT_EQ(s.project().items.size(), 4u);
    EXPECT_EQ(s.document().undoCount(), steps + 1);
    EXPECT_EQ(s.selection().allItems().size(), 2u);

    // Arrow key in the plan view nudges the selection by 10 mm with Shift.
    w.workspaces()->setCurrentWidget(w.planWorkspace());
    settle();
    auto* canvas = w.planWorkspace()->findChild<PlanCanvas*>();
    ASSERT_NE(canvas, nullptr);
    const auto before = itemBounds(*s.resolved(), s.selection().itemId);
    QTest::keyClick(canvas, Qt::Key_Right, Qt::ShiftModifier);
    settle();
    const auto after = itemBounds(*s.resolved(), s.selection().itemId);
    ASSERT_TRUE(before && after);
    EXPECT_NEAR(after->min.x - before->min.x, 10.0, 1e-9);

    // Delete removes every selected item in one step.
    const auto beforeDelete = s.document().undoCount();
    for (QAction* a : w.findChildren<QAction*>())
        if (a->shortcut() == QKeySequence(QKeySequence::Delete)) a->trigger();
    settle();
    EXPECT_EQ(s.project().items.size(), 2u);
    EXPECT_EQ(s.document().undoCount(), beforeDelete + 1);
    ASSERT_TRUE(s.undo().ok());
    EXPECT_EQ(s.project().items.size(), 4u);
}

} // namespace
} // namespace fos::app::gui
