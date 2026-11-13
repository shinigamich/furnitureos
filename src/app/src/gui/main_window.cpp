// SPDX-License-Identifier: AGPL-3.0-only
#include "main_window.hpp"

#include "cutting_workspace.hpp"
#include "diagnostics_panel.hpp"
#include "drawings_workspace.hpp"
#include "enum_text.hpp"
#include "fos/app/autosave.hpp"
#include "fos/app/command_text.hpp"
#include "fos/app/window_title.hpp"
#include "fos/core/units.hpp"
#include "fos/viewport/viewport_widget.hpp"
#include "inspector_panel.hpp"
#include "production_workspace.hpp"
#include "project_browser.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>

namespace fos::app::gui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), preferences_(Preferences::load()) {
    setObjectName(QStringLiteral("mainWindow"));
    {
        QSettings settings;
        recent_.load(settings);
    }
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    session_.setUntitledAutosaveDirectory((QDir(dataDir).filePath(QStringLiteral("autosave"))).toStdString());

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("workspaces"));
    tabs_->setDocumentMode(true);
    viewport_ = new viewport::ViewportWidget(tabs_);
    viewport_->setObjectName(QStringLiteral("viewport"));
    drawings_ = new DrawingsWorkspace(session_, tabs_);
    production_ = new ProductionWorkspace(session_, tabs_);
    cutting_ = new CuttingWorkspace(session_, tabs_);
    tabs_->addTab(viewport_, tr("3D"));
    tabs_->addTab(drawings_, tr("Drawings"));
    tabs_->addTab(production_, tr("Production"));
    tabs_->addTab(cutting_, tr("Cutting"));
    setCentralWidget(tabs_);

    buildDocks();
    buildActions();
    buildMenus();
    buildStatusBar();

    connect(&session_, &Session::documentReplaced, this, [this] { fitOnNextModel_ = true; });
    connect(&session_, &Session::projectChanged, this, [this] {
        if (const project::Resolved* r = session_.resolved()) {
            viewport_->setModel(r->model, fitOnNextModel_);
            fitOnNextModel_ = false;
        } else {
            viewport_->setModel(domain::Model{}, false);
        }
        syncViewportSelection();
        updateUndoRedo();
        updateStatus();
    });
    connect(&session_, &Session::selectionChanged, this, [this] {
        syncViewportSelection();
        updateStatus();
        updateUndoRedo();
    });
    connect(&session_, &Session::savedStateChanged, this, &MainWindow::updateTitle);
    connect(viewport_, &viewport::ViewportWidget::partClicked, this, [this](const QString& id, bool additive) {
        if (id.isEmpty()) {
            if (!additive) session_.setSelection({});
            return;
        }
        Selection sel = additive ? session_.selection() : Selection{};
        auto& parts = sel.partIds;
        const std::string pid = id.toStdString();
        const auto it = std::find(parts.begin(), parts.end(), pid);
        if (it != parts.end()) parts.erase(it);
        else parts.push_back(pid);
        if (!additive || session_.itemOfPart(pid) != sel.itemId) sel.itemId.clear();
        session_.setSelection(sel);
    });
    connect(viewport_, &viewport::ViewportWidget::partDoubleClicked, this,
            [this](const QString& id) { session_.selectItem(session_.itemOfPart(id.toStdString())); });
    connect(diagnostics_, &DiagnosticsPanel::summaryChanged, this, &MainWindow::updateStatus);

    autosaveTimer_ = new QTimer(this);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::autosaveTick);
    applyPreferences();

    emit session_.projectChanged(); // initial fill of every view
    updateTitle();
    resize(1440, 900);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildDocks() {
    auto dock = [this](const QString& title, const QString& name, QWidget* content, Qt::DockWidgetArea area) {
        auto* d = new QDockWidget(title, this);
        d->setObjectName(name);
        d->setWidget(content);
        addDockWidget(area, d);
        return d;
    };
    browser_ = new ProjectBrowser(session_, this);
    browser_->setMinimumWidth(270);
    inspector_ = new InspectorPanel(session_, this);
    inspector_->setMinimumWidth(390);
    diagnostics_ = new DiagnosticsPanel(session_, this);
    dock(tr("Project browser"), QStringLiteral("browserDock"), browser_, Qt::LeftDockWidgetArea);
    auto* insp = dock(tr("Properties"), QStringLiteral("inspectorDock"), inspector_, Qt::RightDockWidgetArea);
    auto* diag = dock(tr("Diagnostics"), QStringLiteral("diagnosticsDock"), diagnostics_, Qt::BottomDockWidgetArea);
    resizeDocks({insp}, {420}, Qt::Horizontal);
    resizeDocks({diag}, {160}, Qt::Vertical);
}

void MainWindow::buildStatusBar() {
    selectionLabel_ = new QLabel(this);
    validationLabel_ = new QLabel(this);
    validationLabel_->setObjectName(QStringLiteral("validationSummary"));
    statusBar()->addWidget(selectionLabel_, 1);
    statusBar()->addPermanentWidget(validationLabel_);
}

void MainWindow::updateTitle() {
    setWindowTitle(fos::app::windowTitle(QString::fromStdString(session_.filePath().string()), session_.isDirty()));
}

void MainWindow::updateStatus() {
    const Selection& sel = session_.selection();
    QString text;
    if (sel.partIds.size() == 1) {
        if (const domain::Part* p = session_.findPart(sel.partIds[0])) {
            const project::Item* item = session_.project().findItem(sel.itemId);
            text = tr("%1 — %2 (%3): %4 × %5 × %6 mm")
                       .arg(item ? QString::fromStdString(item->name()) : QString(), QString::fromStdString(p->name),
                            roleText(p->role), QString::fromStdString(core::formatLength(p->length)),
                            QString::fromStdString(core::formatLength(p->width)),
                            QString::fromStdString(core::formatLength(p->thickness)));
        }
    } else if (sel.partIds.size() > 1) {
        text = tr("%1 parts selected").arg(sel.partIds.size());
    } else if (const project::Item* item = session_.project().findItem(sel.itemId)) {
        const domain::Assembly* a =
            session_.resolved() ? session_.resolved()->model.findAssembly(domain::AssemblyId(sel.itemId)) : nullptr;
        text = a ? tr("%1: %2 parts").arg(QString::fromStdString(item->name())).arg(a->parts.size())
                 : tr("%1: not generated").arg(QString::fromStdString(item->name()));
    } else {
        text = tr("%1 item(s) in the project").arg(session_.project().items.size());
    }
    selectionLabel_->setText(text);
    const auto& s = diagnostics_->summary();
    validationLabel_->setText(tr("Errors: %1   Warnings: %2").arg(s.errors).arg(s.warnings));
    validationLabel_->setStyleSheet(s.errors ? QStringLiteral("color: #b00020; font-weight: bold;")
                                             : (s.warnings ? QStringLiteral("color: #9a6700;") : QString()));
}

void MainWindow::updateUndoRedo() {
    const auto& doc = session_.document();
    const auto undo = doc.undoLabel();
    const auto redo = doc.redoLabel();
    a_.undo->setEnabled(undo.has_value());
    a_.redo->setEnabled(redo.has_value());
    a_.undo->setText(undo ? tr("&Undo %1").arg(commandText(*undo, session_.project())) : tr("&Undo"));
    a_.redo->setText(redo ? tr("&Redo %1").arg(commandText(*redo, session_.project())) : tr("&Redo"));
    const bool item = !session_.selection().itemId.empty();
    a_.deleteItem->setEnabled(item);
    a_.duplicateItem->setEnabled(item);
}

void MainWindow::syncViewportSelection() {
    const Selection& sel = session_.selection();
    std::vector<std::string> parts = sel.partIds;
    if (parts.empty() && !sel.itemId.empty() && session_.resolved())
        if (const auto* a = session_.resolved()->model.findAssembly(domain::AssemblyId(sel.itemId)))
            for (const auto& p : a->parts) parts.push_back(p.id.str());
    viewport_->setSelection(parts);
}

void MainWindow::showError(const QString& title, const core::Error& error) {
    QMessageBox::critical(this, title, QString::fromStdString(error.message));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    // Leaving deliberately: the autosave of this document is no longer needed.
    session_.discardAutosave();
    event->accept();
}

} // namespace fos::app::gui
