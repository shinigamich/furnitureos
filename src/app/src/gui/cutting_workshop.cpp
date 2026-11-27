// SPDX-License-Identifier: AGPL-3.0-only
// Workshop offcut stock in the cutting workspace (`.fosremnants`, project/remnant_use.hpp):
// "Use workshop offcuts from…" and "Add offcuts to workshop stock".
#include "cutting_workspace.hpp"
#include "fos/app/session.hpp"
#include "fos/app/workshop_offcuts.hpp"
#include "inspector_panel.hpp"

#include <QCheckBox>
#include <QDate>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fos::app::gui {
namespace {
std::filesystem::path toPath(const QString& s) { return std::filesystem::path(s.toStdU16String()); }
QString storeFilter() { return CuttingWorkspace::tr("Workshop offcut stock (*.fosremnants)"); }
} // namespace

const project::CuttingPlans& CuttingWorkspace::shownPlans() {
    return workshopRun_ ? workshopRun_->plans : session_.cuttingPlans();
}

void CuttingWorkspace::buildWorkshopBox(QVBoxLayout* layout) {
    auto* box = new QGroupBox(tr("Workshop offcuts"), this);
    box->setObjectName(QStringLiteral("workshopOffcuts"));
    auto* v = new QVBoxLayout(box);
    useWorkshop_ = new QCheckBox(tr("Use workshop offcuts from:"), box);
    useWorkshop_->setObjectName(QStringLiteral("useWorkshopOffcuts"));
    useWorkshop_->setToolTip(tr("Offcuts of the same material and thickness from the stock file are offered before "
                                "full sheets (free ones and those reserved by this project's name)."));
    workshopFile_ = new QLabel(box);
    workshopFile_->setWordWrap(true);
    workshopFile_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* choose = new QPushButton(tr("Choose…"), box);
    choose->setObjectName(QStringLiteral("chooseWorkshopStore"));
    auto* row = new QHBoxLayout;
    row->addWidget(useWorkshop_, 1);
    row->addWidget(choose);
    v->addLayout(row);
    v->addWidget(workshopFile_);
    workshopSummary_ = new QLabel(box);
    workshopSummary_->setWordWrap(true);
    v->addWidget(workshopSummary_);
    workshopTable_ = new QTableWidget(0, 4, box);
    workshopTable_->setObjectName(QStringLiteral("workshopTable"));
    workshopTable_->setHorizontalHeaderLabels({tr("Offcut"), tr("Material"), tr("Size"), tr("Status")});
    workshopTable_->horizontalHeader()->setStretchLastSection(true);
    workshopTable_->verticalHeader()->hide();
    workshopTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    workshopTable_->setMinimumHeight(90);
    workshopTable_->setMaximumHeight(160);
    v->addWidget(workshopTable_);
    auto* add = new QPushButton(tr("Add offcuts to workshop stock…"), box);
    add->setObjectName(QStringLiteral("addOffcutsToStock"));
    add->setToolTip(tr("After cutting: removes the used offcuts from the stock file and adds the usable offcuts of "
                       "these layouts (you confirm the list first)."));
    v->addWidget(add);
    workshopError_ = makeErrorLabel(box);
    workshopError_->setObjectName(QStringLiteral("workshopError"));
    v->addWidget(workshopError_);
    layout->addWidget(box);

    connect(choose, &QPushButton::clicked, this, &CuttingWorkspace::chooseWorkshopStore);
    connect(useWorkshop_, &QCheckBox::toggled, this, [this](bool on) { setWorkshopStore(workshopPath_, on); });
    connect(add, &QPushButton::clicked, this, &CuttingWorkspace::addOffcutsToStock);

    QSettings settings;
    workshopPath_ = settings.value(QLatin1String(kWorkshopStoreFileKey)).toString();
    const bool use = settings.value(QLatin1String(kUseWorkshopStoreKey), false).toBool();
    useWorkshop_->blockSignals(true);
    useWorkshop_->setChecked(use && !workshopPath_.isEmpty());
    useWorkshop_->blockSignals(false);
    if (useWorkshop_->isChecked()) {
        auto store = loadWorkshopStore(toPath(workshopPath_), false);
        if (store) workshopStore_ = std::move(store).value();
        else showError(workshopError_, QString::fromStdString(store.error().message));
    }
    workshopFile_->setText(workshopPath_.isEmpty() ? tr("No stock file chosen.") : workshopPath_);
}

bool CuttingWorkspace::setWorkshopStore(const QString& path, bool use) {
    workshopPath_ = path;
    QSettings settings;
    settings.setValue(QLatin1String(kWorkshopStoreFileKey), path);
    workshopFile_->setText(path.isEmpty() ? tr("No stock file chosen.") : path);
    workshopStore_.reset();
    showError(workshopError_, {});
    bool ok = true;
    if (use && !path.isEmpty()) {
        auto store = loadWorkshopStore(toPath(path), false);
        if (store) {
            workshopStore_ = std::move(store).value();
        } else {
            showError(workshopError_, QString::fromStdString(store.error().message));
            ok = false;
        }
    }
    const bool on = use && workshopStore_.has_value();
    useWorkshop_->blockSignals(true);
    useWorkshop_->setChecked(on);
    useWorkshop_->blockSignals(false);
    settings.setValue(QLatin1String(kUseWorkshopStoreKey), on);
    refresh();
    return ok;
}

void CuttingWorkspace::chooseWorkshopStore() {
    // An existing file is offered; a new name creates the stock on the first "Add offcuts".
    QFileDialog dialog(this, tr("Workshop offcut stock"), workshopPath_, storeFilter());
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setDefaultSuffix(QStringLiteral("fosremnants"));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    const QString file = dialog.selectedFiles().constFirst();
    setWorkshopStore(file, QFileInfo::exists(file));
}

void CuttingWorkspace::showWorkshopRun() {
    workshopTable_->setRowCount(0);
    if (!workshopRun_ || !workshopStore_) {
        workshopSummary_->setText(useWorkshop_->isChecked() ? QString() : tr("Only the project's sheets and remnants are used."));
        return;
    }
    workshopSummary_->setText(workshopRunSummary(*workshopRun_));
    const auto rows = workshopRemnantRows(*workshopStore_, *workshopRun_);
    workshopTable_->setRowCount(static_cast<int>(rows.size()));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const int r = static_cast<int>(i);
        workshopTable_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(rows[i].id)));
        workshopTable_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(rows[i].materialId)));
        workshopTable_->setItem(r, 2, new QTableWidgetItem(rows[i].size));
        auto* status = new QTableWidgetItem(rows[i].detail);
        status->setToolTip(rows[i].detail);
        workshopTable_->setItem(r, 3, status);
    }
    workshopTable_->resizeColumnsToContents();
}

std::optional<project::StoreCommit> CuttingWorkspace::plannedStoreUpdate() {
    showError(workshopError_, {});
    if (workshopPath_.isEmpty()) {
        showError(workshopError_, tr("Choose the workshop stock file first."));
        return std::nullopt;
    }
    const project::Resolved* resolved = session_.resolved();
    if (!resolved) {
        showError(workshopError_, tr("The project cannot be generated; there are no layouts to take offcuts from."));
        return std::nullopt;
    }
    // The stock as it is on disk now (another job may have changed it); missing = new file.
    auto store = loadWorkshopStore(toPath(workshopPath_), true);
    if (!store) {
        showError(workshopError_, QString::fromStdString(store.error().message));
        return std::nullopt;
    }
    const project::StoreCuttingRun run = workshopRun_ ? *workshopRun_ : plainCuttingRun(session_.cuttingPlans());
    auto commit = project::commitStoreUse(store.value(), run, resolved->model,
                                          offcutImportOptions(session_.project(), session_.filePath(),
                                                              QDate::currentDate()));
    if (!commit) {
        showError(workshopError_, QString::fromStdString(commit.error().message));
        return std::nullopt;
    }
    return std::move(commit).value();
}

void CuttingWorkspace::addOffcutsToStock() {
    auto commit = plannedStoreUpdate();
    if (!commit) return;
    const QStringList lines = storeChangeLines(*commit);
    if (lines.isEmpty()) {
        QMessageBox::information(this, tr("Workshop offcuts"),
                                 tr("These layouts leave no usable offcuts and use no stock offcuts; the stock "
                                    "file is unchanged."));
        return;
    }
    QMessageBox box(QMessageBox::Question, tr("Workshop offcuts"),
                    tr("Change the workshop stock %1?\n%2 offcuts removed (used), %3 added.")
                        .arg(QFileInfo(workshopPath_).fileName())
                        .arg(commit->removed.size())
                        .arg(commit->added.size()),
                    QMessageBox::Save | QMessageBox::Cancel, this);
    box.setDetailedText(lines.join(QLatin1Char('\n')));
    box.setInformativeText(lines.mid(0, 12).join(QLatin1Char('\n')) +
                           (lines.size() > 12 ? QStringLiteral("\n…") : QString()));
    if (box.exec() != QMessageBox::Save) return;
    auto saved = project::saveRemnantStore(toPath(workshopPath_), commit->store);
    if (!saved) {
        showError(workshopError_, QString::fromStdString(saved.error().message));
        return;
    }
    // The used offcuts are gone from the stock: show the run against the new content.
    setWorkshopStore(workshopPath_, useWorkshop_->isChecked());
}

} // namespace fos::app::gui
