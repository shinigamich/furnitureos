// SPDX-License-Identifier: AGPL-3.0-only
// File lifecycle (new / open / save / autosave / recovery), exports and small dialogs.
#include "dialogs.hpp"
#include "fos/app/autosave.hpp"
#include "fos/app/production_text.hpp"
#include "fos/app/version.hpp"
#include "fos/formats/obj_export.hpp"
#include "fos/production/csv.hpp"
#include "main_window.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>

namespace fos::app::gui {

namespace fs = std::filesystem;

namespace {
QString projectFilter() { return MainWindow::tr("FurnitureOS projects (*.fosp)"); }

QString timeText(fs::file_time_type t) {
    const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(sys.time_since_epoch()).count();
    return QLocale().toString(QDateTime::fromMSecsSinceEpoch(ms), QLocale::ShortFormat);
}

fs::path toPath(const QString& s) { return fs::path(s.toStdString()); }
} // namespace

bool MainWindow::maybeSave() {
    if (!session_.isDirty()) return true;
    const auto answer = QMessageBox::warning(
        this, tr("Unsaved changes"), tr("The project has unsaved changes. Save them?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return save();
    session_.discardAutosave();
    return true;
}

void MainWindow::newProject() {
    if (!maybeSave()) return;
    session_.newProject();
}

bool MainWindow::newFromTemplate(TemplateKind kind, const std::optional<TemplateSize>& size) {
    auto r = session_.newFromTemplate(kind, size);
    if (!r) {
        showError(tr("New from template"), r.error());
        return false;
    }
    return true;
}

void MainWindow::newFromTemplateDialog(TemplateKind kind) {
    if (!maybeSave()) return;
    TemplateSizeDialog dialog(kind, this);
    if (dialog.exec() != QDialog::Accepted) return;
    newFromTemplate(kind, dialog.size());
}

void MainWindow::addFromTemplateDialog(TemplateKind kind) {
    TemplateSizeDialog dialog(kind, this);
    if (dialog.exec() != QDialog::Accepted) return;
    auto r = session_.addFromTemplate(kind, dialog.size());
    if (!r) showError(tr("Add cabinet"), r.error());
}

void MainWindow::openDialog() {
    if (!maybeSave()) return;
    const QString start = recent_.entries().isEmpty() ? QDir::homePath() : QFileInfo(recent_.entries().front()).path();
    const QString file = QFileDialog::getOpenFileName(this, tr("Open project"), start, projectFilter());
    if (!file.isEmpty()) openFile(file);
}

bool MainWindow::openFile(const QString& path, bool offerRecovery) {
    const fs::path p = toPath(path);
    core::Result<core::Diagnostics> result = core::Diagnostics{};
    bool recovered = false;
    if (offerRecovery) {
        if (const auto offer = recoveryOffer(p)) {
            const QString kind = offer->candidate.kind == formats::RecoveryKind::Autosave ? tr("an autosave")
                                                                                         : tr("a backup");
            const QString question =
                offer->documentMissing
                    ? tr("The file %1 does not exist, but %2 of it from %3 was found. Open the recovered state?")
                          .arg(path, kind, timeText(offer->candidate.modified))
                    : tr("%1 of %2 from %3 is newer than the saved file. Recover it? "
                         "(The recovered state can be undone to return to the saved file.)")
                          .arg(kind, QFileInfo(path).fileName(), timeText(offer->candidate.modified));
            const auto answer = QMessageBox::question(this, tr("Recover project"), question,
                                                      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (answer == QMessageBox::Cancel) return false;
            if (answer == QMessageBox::Yes) {
                result = session_.openRecovered(p);
                recovered = true;
            }
        }
    }
    if (!recovered) result = session_.open(p);
    if (!result) {
        QMessageBox::critical(this, tr("Cannot open project"),
                              tr("%1\n\n%2").arg(path, QString::fromStdString(result.error().message)));
        if (!QFileInfo::exists(path)) {
            recent_.remove(path);
            QSettings settings;
            recent_.save(settings);
            rebuildRecentMenu();
        }
        return false;
    }
    recent_.add(path);
    QSettings settings;
    recent_.save(settings);
    rebuildRecentMenu();
    const auto& diagnostics = result.value();
    if (!diagnostics.empty())
        statusBar()->showMessage(tr("Opened with %1 finding(s); see Diagnostics.").arg(diagnostics.size()), 8000);
    else
        statusBar()->showMessage(tr("Opened %1").arg(path), 5000);
    return true;
}

bool MainWindow::save() {
    if (session_.filePath().empty()) return saveAs();
    auto r = session_.save();
    if (!r) {
        showError(tr("Save failed"), r.error());
        return false;
    }
    statusBar()->showMessage(tr("Saved %1").arg(QString::fromStdString(session_.filePath().string())), 5000);
    return true;
}

bool MainWindow::saveAs() {
    QString suggestion = session_.filePath().empty()
                             ? QDir(QDir::homePath()).filePath(QString::fromStdString(session_.project().metadata.name) +
                                                               QStringLiteral(".fosp"))
                             : QString::fromStdString(session_.filePath().string());
    QString file = QFileDialog::getSaveFileName(this, tr("Save project as"), suggestion, projectFilter());
    if (file.isEmpty()) return false;
    if (!file.endsWith(QLatin1String(".fosp"), Qt::CaseInsensitive)) file += QStringLiteral(".fosp");
    return saveToFile(file);
}

bool MainWindow::saveToFile(const QString& path) {
    auto r = session_.saveAs(toPath(path));
    if (!r) {
        showError(tr("Save failed"), r.error());
        return false;
    }
    recent_.add(path);
    QSettings settings;
    recent_.save(settings);
    rebuildRecentMenu();
    statusBar()->showMessage(tr("Saved %1").arg(path), 5000);
    return true;
}

void MainWindow::autosaveTick() {
    AutosaveState state{preferences_.autosaveMinutes, session_.isDirty(), session_.revision(),
                        session_.lastAutosavedRevision()};
    if (!autosaveDue(state)) return;
    auto r = session_.autosave();
    if (r) statusBar()->showMessage(tr("Autosaved"), 3000);
    else statusBar()->showMessage(tr("Autosave failed: %1").arg(QString::fromStdString(r.error().message)), 10000);
}

void MainWindow::offerUntitledRecovery() {
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("autosave"));
    const auto base = newestUntitledAutosave(toPath(dir));
    if (!base) return;
    const auto answer = QMessageBox::question(
        this, tr("Recover project"),
        tr("An autosave of a project that was never saved was found (%1). Open it?")
            .arg(timeText(fs::last_write_time(formats::autosavePath(*base)))),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Discard);
    if (answer == QMessageBox::Discard) {
        (void)formats::discardAutosave(*base);
        return;
    }
    if (answer != QMessageBox::Yes) return;
    auto loaded = project::recoverProject(*base);
    if (!loaded) {
        showError(tr("Recover project"), loaded.error());
        return;
    }
    // Opened as an untitled, modified project (one undoable step on top of an empty one);
    // it keeps the recovered project id, so later autosaves overwrite the same file.
    session_.newProject();
    project::Command cmd;
    cmd.label = {"Recover {0} from autosave or backup", {base->filename().string()}};
    cmd.apply = [p = loaded.value().project](project::Project& target) -> core::Result<project::Affected> {
        target = p;
        project::Affected a;
        for (const auto& item : target.items) a.items.push_back(item.id());
        a.libraries = a.settings = true;
        return a;
    };
    if (auto r = session_.execute(cmd); !r) showError(tr("Recover project"), r.error());
}

void MainWindow::applyPreferences() {
    const int ms = autosaveIntervalMs(preferences_.autosaveMinutes);
    if (ms > 0) autosaveTimer_->start(ms);
    else autosaveTimer_->stop();
}

void MainWindow::showPreferences() {
    PreferencesDialog dialog(preferences_, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString oldLanguage = preferences_.language;
    preferences_ = dialog.preferences();
    preferences_.save();
    applyPreferences();
    if (preferences_.language != oldLanguage)
        QMessageBox::information(this, tr("Preferences"), tr("The new language is used after a restart."));
}

void MainWindow::showAbout() {
    QMessageBox::about(
        this, tr("About FurnitureOS"),
        tr("<h3>FurnitureOS %1</h3>"
           "<p>Free and open-source parametric furniture CAD/CAM.</p>"
           "<p>License: GNU Affero General Public License v3.0 only (%2). This program comes with ABSOLUTELY NO "
           "WARRANTY.</p>"
           "<p>Source code: <a href=\"%3\">%3</a></p>"
           "<p>Uses Qt %4 (LGPL-3.0).</p>")
            .arg(QString::fromLatin1(applicationVersion()), QString::fromLatin1(kLicense),
                 QString::fromLatin1(sourceUrl()), QString::fromLatin1(qVersion())));
}

void MainWindow::deleteSelectedItem() {
    const std::string id = session_.selection().itemId;
    if (id.empty()) return;
    auto r = session_.deleteItem(id);
    if (!r) showError(tr("Delete item"), r.error());
}

void MainWindow::duplicateSelectedItem() {
    const std::string id = session_.selection().itemId;
    if (id.empty()) return;
    auto r = session_.duplicateItem(id);
    if (!r) showError(tr("Duplicate item"), r.error());
}

// --- exports ---------------------------------------------------------------------------

namespace {
QString baseName(const Session& s) {
    const QString name = s.filePath().empty() ? QString::fromStdString(s.project().metadata.name)
                                              : QString::fromStdString(s.filePath().stem().string());
    return QDir(QDir::homePath()).filePath(name);
}
} // namespace

void MainWindow::exportCutListCsv() {
    const QString file = QFileDialog::getSaveFileName(this, tr("Export cut list"), baseName(session_) + tr("-cut-list.csv"),
                                                      tr("CSV files (*.csv)"));
    if (file.isEmpty()) return;
    const auto table = production::cutListTable(session_.productionReports().cutList, ProductionText::localisation());
    production::CsvOptions options;
    options.utf8Bom = true;
    auto r = production::writeTextFile(toPath(file), production::toCsv(table, options));
    if (!r) showError(tr("Export failed"), r.error());
}

void MainWindow::exportProductionHtml() {
    const project::Resolved* resolved = session_.resolved();
    if (!resolved || resolved->model.assemblies.empty()) {
        QMessageBox::information(this, tr("Production sheet"), tr("There is no generated item to document."));
        return;
    }
    // One sheet per assembly: the selected item, otherwise the first one.
    std::string id = session_.selection().itemId;
    if (!resolved->model.findAssembly(domain::AssemblyId(id))) id = resolved->model.assemblies.front().id.str();
    const auto* assembly = resolved->model.findAssembly(domain::AssemblyId(id));
    const QString file = QFileDialog::getSaveFileName(
        this, tr("Export production sheet of %1").arg(QString::fromStdString(assembly->name)),
        baseName(session_) + QStringLiteral("-") + QString::fromStdString(id) + QStringLiteral(".html"),
        tr("HTML files (*.html)"));
    if (file.isEmpty()) return;
    production::HtmlSheetOptions options;
    options.projectName = session_.project().metadata.name;
    options.text = ProductionText::htmlSheetText();
    options.localisation = ProductionText::localisation();
    const auto po = project::productionOptions(session_.project());
    options.edgeBanding = po.edgeBanding;
    options.cutList = po.cutList;
    auto html = production::productionSheetHtml(resolved->model, domain::AssemblyId(id), options);
    if (!html) return showError(tr("Export failed"), html.error());
    auto r = production::writeTextFile(toPath(file), html.value());
    if (!r) showError(tr("Export failed"), r.error());
}

void MainWindow::exportLabelsSvg() {
    const project::Resolved* resolved = session_.resolved();
    if (!resolved) return;
    production::LabelOptions lo;
    lo.projectName = session_.project().metadata.name;
    lo.cutList = project::productionOptions(session_.project()).cutList;
    const auto set = production::labels(resolved->model, lo);
    if (set.labels.empty()) {
        QMessageBox::information(this, tr("Part labels"), tr("There are no parts to label."));
        return;
    }
    const QString file = QFileDialog::getSaveFileName(this, tr("Export part labels (A4, 70 × 37 mm)"),
                                                      baseName(session_) + tr("-labels.svg"), tr("SVG files (*.svg)"));
    if (file.isEmpty()) return;
    production::LabelSheetOptions options;
    options.text = ProductionText::labelText();
    auto pages = production::renderLabelSheetsSvg(set.labels, options);
    if (!pages) return showError(tr("Export failed"), pages.error());
    const fs::path base = toPath(file);
    QStringList written;
    for (std::size_t i = 0; i < pages.value().size(); ++i) {
        fs::path out = base;
        if (pages.value().size() > 1)
            out.replace_filename(base.stem().string() + "-" + std::to_string(i + 1) + base.extension().string());
        if (auto r = production::writeTextFile(out, pages.value()[i]); !r) return showError(tr("Export failed"), r.error());
        written << QString::fromStdString(out.string());
    }
    statusBar()->showMessage(tr("Written: %1").arg(written.join(QStringLiteral(", "))), 8000);
}

void MainWindow::exportModelObj() {
    const project::Resolved* resolved = session_.resolved();
    if (!resolved) return;
    const QString file = QFileDialog::getSaveFileName(this, tr("Export 3D model"), baseName(session_) + QStringLiteral(".obj"),
                                                      tr("Wavefront OBJ (*.obj)"));
    if (file.isEmpty()) return;
    auto r = formats::writeObjFiles(toPath(file), resolved->model);
    if (!r) showError(tr("Export failed"), r.error());
    else
        statusBar()->showMessage(tr("Written %1 and its .mtl file (millimetres, Z up; parts as boxes).").arg(file), 8000);
}

} // namespace fos::app::gui
