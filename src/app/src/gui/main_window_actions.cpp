// SPDX-License-Identifier: AGPL-3.0-only
// Menus, toolbar and actions of the main window.
#include "component_browser.hpp"
#include "dialogs.hpp"
#include "drawings_workspace.hpp"
#include "fos/viewport/viewport_widget.hpp"
#include "main_window.hpp"
#include "view_tools.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>

namespace fos::app::gui {

void MainWindow::buildActions() {
    auto make = [this](const QString& text, const QKeySequence& key = {}, QStyle::StandardPixmap icon = QStyle::SP_CustomBase) {
        auto* a = new QAction(text, this);
        if (!key.isEmpty()) a->setShortcut(key);
        if (icon != QStyle::SP_CustomBase) a->setIcon(style()->standardIcon(icon));
        allActions_.append(a);
        return a;
    };
    a_.newProject = make(tr("&New"), QKeySequence::New, QStyle::SP_FileIcon);
    a_.open = make(tr("&Open…"), QKeySequence::Open, QStyle::SP_DialogOpenButton);
    a_.save = make(tr("&Save"), QKeySequence::Save, QStyle::SP_DialogSaveButton);
    a_.saveAs = make(tr("Save &As…"), QKeySequence::SaveAs);
    a_.print = make(tr("&Print drawing…"), QKeySequence::Print);
    a_.quit = make(tr("&Quit"), QKeySequence::Quit);
    a_.exportCsv = make(tr("Cut list (CSV)…"));
    a_.exportHtml = make(tr("Production sheet (HTML)…"));
    a_.exportLabels = make(tr("Part labels (SVG)…"));
    a_.exportPdf = make(tr("Drawing (PDF)…"));
    a_.exportSvg = make(tr("Drawing (SVG)…"));
    a_.exportDxf = make(tr("Drawing views (DXF)…"));
    a_.exportObj = make(tr("3D model (OBJ)…"));
    a_.importDxf = make(tr("&Import DXF outline…"));
    a_.importDxf->setObjectName(QStringLiteral("actionImportDxf"));
    a_.undo = make(tr("&Undo"), QKeySequence::Undo, QStyle::SP_ArrowBack);
    a_.redo = make(tr("&Redo"), QKeySequence::Redo, QStyle::SP_ArrowForward);
    a_.deleteItem = make(tr("&Delete item"), QKeySequence::Delete);
    a_.duplicateItem = make(tr("D&uplicate item"), QKeySequence(Qt::CTRL | Qt::Key_D));
    a_.saveAsComponent = make(tr("Save as &component…"));
    a_.saveAsComponent->setObjectName(QStringLiteral("actionSaveAsComponent"));
    a_.preferences = make(tr("&Preferences…"), QKeySequence::Preferences);
    a_.fitAll = make(tr("&Fit all"), QKeySequence(Qt::Key_F));
    a_.perspective = make(tr("&Perspective"), QKeySequence(Qt::Key_P));
    a_.orthographic = make(tr("&Orthographic"), QKeySequence(Qt::Key_O));
    a_.shaded = make(tr("&Shaded"), QKeySequence(Qt::Key_1));
    a_.shadedEdges = make(tr("Shaded with &edges"), QKeySequence(Qt::Key_2));
    a_.wireframe = make(tr("&Wireframe"), QKeySequence(Qt::Key_3));
    a_.realistic = make(tr("&Realistic materials"), QKeySequence(Qt::Key_4));
    a_.realistic->setObjectName(QStringLiteral("actionRealistic"));
    a_.realistic->setToolTip(tr("Show material colours, wood grain, textures and edge bands"));
    a_.measure = make(tr("&Measure"), QKeySequence(Qt::Key_M));
    a_.measure->setObjectName(QStringLiteral("actionMeasure"));
    a_.measure->setToolTip(tr("Measure the distance between two snapped points (display only)"));
    a_.section = make(tr("Sect&ion plane"), QKeySequence(Qt::Key_S));
    a_.section->setObjectName(QStringLiteral("actionSection"));
    a_.section->setToolTip(tr("Cut the 3D view with an axis-aligned plane (display only)"));
    a_.about = make(tr("&About FurnitureOS"));
    a_.shortcuts = make(tr("&Keyboard shortcuts"), QKeySequence::HelpContents);

    for (QAction* a : {a_.perspective, a_.orthographic, a_.shaded, a_.shadedEdges, a_.wireframe, a_.realistic, a_.measure,
                       a_.section})
        a->setCheckable(true);
    auto* projection = new QActionGroup(this);
    projection->addAction(a_.perspective);
    projection->addAction(a_.orthographic);
    auto* display = new QActionGroup(this);
    display->addAction(a_.shaded);
    display->addAction(a_.shadedEdges);
    display->addAction(a_.wireframe);
    display->addAction(a_.realistic);
    (viewport_->projection() == viewport::Projection::Perspective ? a_.perspective : a_.orthographic)->setChecked(true);
    switch (viewport_->displayMode()) {
    case viewport::DisplayMode::Shaded: a_.shaded->setChecked(true); break;
    case viewport::DisplayMode::ShadedWithEdges: a_.shadedEdges->setChecked(true); break;
    case viewport::DisplayMode::Wireframe: a_.wireframe->setChecked(true); break;
    case viewport::DisplayMode::Realistic: a_.realistic->setChecked(true); break;
    }

    connect(a_.newProject, &QAction::triggered, this, &MainWindow::newProject);
    connect(a_.open, &QAction::triggered, this, &MainWindow::openDialog);
    connect(a_.save, &QAction::triggered, this, [this] { save(); });
    connect(a_.saveAs, &QAction::triggered, this, [this] { saveAs(); });
    connect(a_.print, &QAction::triggered, drawings_, &DrawingsWorkspace::print);
    connect(a_.quit, &QAction::triggered, this, &QWidget::close);
    connect(a_.exportCsv, &QAction::triggered, this, &MainWindow::exportCutListCsv);
    connect(a_.exportHtml, &QAction::triggered, this, &MainWindow::exportProductionHtml);
    connect(a_.exportLabels, &QAction::triggered, this, &MainWindow::exportLabelsSvg);
    connect(a_.exportPdf, &QAction::triggered, drawings_, &DrawingsWorkspace::exportPdf);
    connect(a_.exportSvg, &QAction::triggered, drawings_, &DrawingsWorkspace::exportSvg);
    connect(a_.exportDxf, &QAction::triggered, drawings_, &DrawingsWorkspace::exportDxf);
    connect(a_.exportObj, &QAction::triggered, this, &MainWindow::exportModelObj);
    connect(a_.importDxf, &QAction::triggered, this, &MainWindow::importDxfOutline);
    connect(a_.undo, &QAction::triggered, this, [this] { (void)session_.undo(); });
    connect(a_.redo, &QAction::triggered, this, [this] { (void)session_.redo(); });
    connect(a_.deleteItem, &QAction::triggered, this, &MainWindow::deleteSelectedItem);
    connect(a_.duplicateItem, &QAction::triggered, this, &MainWindow::duplicateSelectedItem);
    connect(a_.saveAsComponent, &QAction::triggered, this, &MainWindow::saveSelectedAsComponent);
    connect(components_, &ComponentBrowser::saveAsComponentRequested, this, &MainWindow::saveSelectedAsComponent);
    connect(a_.preferences, &QAction::triggered, this, &MainWindow::showPreferences);
    connect(a_.fitAll, &QAction::triggered, viewport_, &viewport::ViewportWidget::fitAll);
    connect(a_.perspective, &QAction::triggered, this, [this] { viewport_->setProjection(viewport::Projection::Perspective); });
    connect(a_.orthographic, &QAction::triggered, this, [this] { viewport_->setProjection(viewport::Projection::Orthographic); });
    connect(a_.shaded, &QAction::triggered, this, [this] { viewport_->setDisplayMode(viewport::DisplayMode::Shaded); });
    connect(a_.shadedEdges, &QAction::triggered, this,
            [this] { viewport_->setDisplayMode(viewport::DisplayMode::ShadedWithEdges); });
    connect(a_.wireframe, &QAction::triggered, this, [this] { viewport_->setDisplayMode(viewport::DisplayMode::Wireframe); });
    connect(a_.realistic, &QAction::triggered, this, [this] { viewport_->setDisplayMode(viewport::DisplayMode::Realistic); });
    connect(a_.about, &QAction::triggered, this, &MainWindow::showAbout);
    connect(a_.shortcuts, &QAction::triggered, this, [this] { ShortcutsDialog(allActions_, this).exec(); });
}

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(a_.newProject);
    QMenu* fromTemplate = file->addMenu(tr("New from &template"));
    for (TemplateKind kind : allTemplates())
        fromTemplate->addAction(templateTitle(kind) + QStringLiteral("…"), this, [this, kind] { newFromTemplateDialog(kind); });
    file->addAction(a_.open);
    recentMenu_ = file->addMenu(tr("Open &recent"));
    file->addSeparator();
    file->addAction(a_.save);
    file->addAction(a_.saveAs);
    file->addSeparator();
    file->addAction(a_.importDxf);
    QMenu* exportMenu = file->addMenu(tr("&Export"));
    for (QAction* a : {a_.exportCsv, a_.exportHtml, a_.exportLabels}) exportMenu->addAction(a);
    exportMenu->addSeparator();
    for (QAction* a : {a_.exportPdf, a_.exportSvg, a_.exportDxf}) exportMenu->addAction(a);
    exportMenu->addSeparator();
    exportMenu->addAction(a_.exportObj);
    file->addAction(a_.print);
    file->addSeparator();
    file->addAction(a_.quit);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(a_.undo);
    edit->addAction(a_.redo);
    edit->addSeparator();
    QMenu* add = edit->addMenu(tr("&Add cabinet"));
    for (TemplateKind kind : allTemplates())
        add->addAction(templateTitle(kind) + QStringLiteral("…"), this, [this, kind] { addFromTemplateDialog(kind); });
    edit->addAction(a_.duplicateItem);
    edit->addAction(a_.deleteItem);
    edit->addAction(a_.saveAsComponent);
    edit->addSeparator();
    edit->addAction(a_.preferences);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    QMenu* presets = view->addMenu(tr("View &presets"));
    const std::pair<QString, viewport::ViewPreset> list[] = {
        {tr("Front"), viewport::ViewPreset::Front},   {tr("Back"), viewport::ViewPreset::Back},
        {tr("Left"), viewport::ViewPreset::Left},     {tr("Right"), viewport::ViewPreset::Right},
        {tr("Top"), viewport::ViewPreset::Top},       {tr("Bottom"), viewport::ViewPreset::Bottom},
        {tr("Isometric"), viewport::ViewPreset::Isometric}};
    for (const auto& [text, preset] : list) {
        QAction* a = presets->addAction(text, this, [this, p = preset] {
            tabs_->setCurrentWidget(viewport_);
            viewport_->setViewPreset(p);
        });
        allActions_.append(a);
    }
    view->addSeparator();
    view->addAction(a_.perspective);
    view->addAction(a_.orthographic);
    view->addSeparator();
    view->addAction(a_.shaded);
    view->addAction(a_.shadedEdges);
    view->addAction(a_.wireframe);
    view->addAction(a_.realistic);
    view->addSeparator();
    view->addAction(a_.fitAll);
    view->addSeparator();
    view->addAction(a_.measure);
    view->addAction(a_.section);
    view->addSeparator();
    for (QDockWidget* dock : findChildren<QDockWidget*>()) view->addAction(dock->toggleViewAction());

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(a_.shortcuts);
    help->addAction(a_.about);

    QToolBar* bar = addToolBar(tr("Main toolbar"));
    bar->setObjectName(QStringLiteral("mainToolbar"));
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->addAction(a_.newProject);
    bar->addAction(a_.open);
    bar->addAction(a_.save);
    bar->addSeparator();
    bar->addAction(a_.undo);
    bar->addAction(a_.redo);
    bar->addSeparator();
    bar->addAction(a_.fitAll);
    bar->addAction(a_.measure);
    bar->addAction(a_.section);
    // Undo/redo texts carry the command label; keep the toolbar compact with fixed texts.
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    rebuildRecentMenu();
}

void MainWindow::buildViewTools() {
    sectionBar_ = new SectionToolBar(viewport_, this);
    addToolBar(Qt::TopToolBarArea, sectionBar_);
    sectionBar_->setSectionEnabled(false);
    measureLabel_ = new QLabel(this);
    measureLabel_->setObjectName(QStringLiteral("measureStatus"));
    measureLabel_->setVisible(false);
    statusBar()->addWidget(measureLabel_, 2);
    viewport_->setLengthSuffix(tr(" mm"));

    connect(a_.measure, &QAction::toggled, this, [this](bool on) {
        if (on) {
            tabs_->setCurrentWidget(viewport_);
            viewport_->setFocus();
        }
        viewport_->setMeasureMode(on);
    });
    connect(viewport_, &viewport::ViewportWidget::measureModeChanged, this, [this](bool on) {
        a_.measure->setChecked(on);
        measureLabel_->setVisible(on);
    });
    connect(viewport_, &viewport::ViewportWidget::measurementChanged, this,
            [this] { measureLabel_->setText(measureStatusText(*viewport_)); });
    connect(a_.section, &QAction::toggled, this, [this](bool on) {
        if (on) tabs_->setCurrentWidget(viewport_);
        sectionBar_->setSectionEnabled(on);
    });
    connect(sectionBar_, &SectionToolBar::offRequested, this, [this] { a_.section->setChecked(false); });
}

void MainWindow::rebuildRecentMenu() {
    recentMenu_->clear();
    for (const QString& path : recent_.entries())
        recentMenu_->addAction(QFileInfo(path).fileName() + QStringLiteral("  —  ") + path, this,
                               [this, path] { if (maybeSave()) openFile(path); });
    recentMenu_->setEnabled(!recent_.entries().isEmpty());
    if (!recent_.entries().isEmpty()) {
        recentMenu_->addSeparator();
        recentMenu_->addAction(tr("Clear list"), this, [this] {
            recent_.clear();
            QSettings settings;
            recent_.save(settings);
            rebuildRecentMenu();
        });
    }
}

} // namespace fos::app::gui
