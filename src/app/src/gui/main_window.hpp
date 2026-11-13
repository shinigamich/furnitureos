// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Main window: menus, toolbar, status bar, docks (browser, inspector, diagnostics) and the
/// workspaces (3D, Drawings, Production, Cutting). Owns the Session.
#include "fos/app/recent_files.hpp"
#include "fos/app/session.hpp"
#include "settings.hpp"

#include <QMainWindow>

class QAction;
class QLabel;
class QMenu;
class QTabWidget;
class QTimer;

namespace fos::viewport {
class ViewportWidget;
}

namespace fos::app::gui {

class CuttingWorkspace;
class DiagnosticsPanel;
class DrawingsWorkspace;
class InspectorPanel;
class ProductionWorkspace;
class ProjectBrowser;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    Session& session() noexcept { return session_; }
    ProjectBrowser* browser() const noexcept { return browser_; }
    InspectorPanel* inspector() const noexcept { return inspector_; }
    DiagnosticsPanel* diagnostics() const noexcept { return diagnostics_; }
    DrawingsWorkspace* drawingsWorkspace() const noexcept { return drawings_; }
    ProductionWorkspace* productionWorkspace() const noexcept { return production_; }
    CuttingWorkspace* cuttingWorkspace() const noexcept { return cutting_; }
    QTabWidget* workspaces() const noexcept { return tabs_; }

    /// Opens a project file (with recovery offer when a newer autosave/backup exists);
    /// errors are shown with the core's message. Returns true when the file was opened.
    bool openFile(const QString& path, bool offerRecovery = true);
    /// Saves to `path` (Save As without the dialog). Errors are shown; returns success.
    bool saveToFile(const QString& path);
    /// "New from template" without the size dialog (nullopt = template size).
    bool newFromTemplate(TemplateKind kind, const std::optional<TemplateSize>& size);
    /// Offers to open the newest autosave of an unsaved project (called once at start-up).
    void offerUntitledRecovery();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    // main_window.cpp
    void buildDocks();
    void buildStatusBar();
    void updateTitle();
    void updateStatus();
    void updateUndoRedo();
    void syncViewportSelection();
    void showError(const QString& title, const core::Error& error);
    // main_window_actions.cpp
    void buildActions();
    void buildMenus();
    void rebuildRecentMenu();
    // main_window_files.cpp
    bool maybeSave();
    void newProject();
    void newFromTemplateDialog(TemplateKind kind);
    void addFromTemplateDialog(TemplateKind kind);
    void openDialog();
    bool save();
    bool saveAs();
    void exportCutListCsv();
    void exportProductionHtml();
    void exportLabelsSvg();
    void exportModelObj();
    void autosaveTick();
    void applyPreferences();
    void showPreferences();
    void showAbout();
    void deleteSelectedItem();
    void duplicateSelectedItem();

    Session session_;
    Preferences preferences_;
    RecentFiles recent_;
    QTimer* autosaveTimer_ = nullptr;

    QTabWidget* tabs_ = nullptr;
    viewport::ViewportWidget* viewport_ = nullptr;
    DrawingsWorkspace* drawings_ = nullptr;
    ProductionWorkspace* production_ = nullptr;
    CuttingWorkspace* cutting_ = nullptr;
    ProjectBrowser* browser_ = nullptr;
    InspectorPanel* inspector_ = nullptr;
    DiagnosticsPanel* diagnostics_ = nullptr;
    QLabel* selectionLabel_ = nullptr;
    QLabel* validationLabel_ = nullptr;
    bool fitOnNextModel_ = true;

    struct Actions {
        QAction *newProject, *open, *save, *saveAs, *print, *quit;
        QAction *exportCsv, *exportHtml, *exportLabels, *exportPdf, *exportSvg, *exportDxf, *exportObj;
        QAction *undo, *redo, *deleteItem, *duplicateItem, *preferences;
        QAction *fitAll, *perspective, *orthographic, *shaded, *shadedEdges, *wireframe;
        QAction *about, *shortcuts;
    } a_{};
    QMenu* recentMenu_ = nullptr;
    QList<QAction*> allActions_;
};

} // namespace fos::app::gui
