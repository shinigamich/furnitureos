// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Free-modelling actions of the main window (Edit menu): Add panel…, Copy / Cut / Paste of
/// items (system clipboard, fos::app::item_clipboard.hpp), Move / rotate…, Rotate 90°, Mirror,
/// Array…, Align and Distribute. Every action runs ONE command (one undo step) built by
/// fos::app (edit_tools.hpp / panel_tools.hpp); errors are shown with the core's message.
#include <QList>
#include <QObject>

class QAction;
class QMenu;
class QWidget;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class EditToolsController : public QObject {
    Q_OBJECT
public:
    /// Creates the actions (owned by `window`) and appends them to `allActions` (shortcut list).
    EditToolsController(Session& session, QWidget* window, QList<QAction*>& allActions);

    /// Adds the actions and submenus to the Edit menu.
    void populateMenu(QMenu* edit);
    /// Actions offered for a multi-selection (inspector "N items selected" box).
    QList<QAction*> multiSelectionActions() const;

    QAction* addPanelAction() const noexcept { return addPanel_; }
    QAction* copyAction() const noexcept { return copy_; }
    QAction* cutAction() const noexcept { return cut_; }
    QAction* pasteAction() const noexcept { return paste_; }
    QAction* transformAction() const noexcept { return transform_; }

    /// Runs the tools (also used by tests).
    void addPanel();
    void copy();
    void cut();
    void paste();
    void transform();
    void rotateQuarter();
    void mirror(int axis);
    void array();
    void align(int edge);
    void distribute(int axis);

private:
    void updateEnabled();
    void report(const QString& title, const QString& message);

    Session& session_;
    QWidget* window_;
    QAction *addPanel_ = nullptr, *copy_ = nullptr, *cut_ = nullptr, *paste_ = nullptr;
    QAction *transform_ = nullptr, *rotate90_ = nullptr, *array_ = nullptr;
    QList<QAction*> mirror_, align_, distribute_;
};

} // namespace fos::app::gui
