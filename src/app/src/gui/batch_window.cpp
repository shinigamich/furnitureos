// SPDX-License-Identifier: AGPL-3.0-only
// Production batch window: file handling, projects tab and settings tab (the Summary tab and
// the exports are in batch_window_summary.cpp).
#include "batch_window.hpp"

#include "fos/app/cutting_rows.hpp"
#include "fos/app/session.hpp"
#include "form_widgets.hpp"
#include "inspector_panel.hpp"
#include "nesting_settings_form.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDate>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace fos::app::gui {

namespace fs = std::filesystem;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string str(const QString& s) { return s.trimmed().toStdString(); }
fs::path toPath(const QString& file) { return fs::path(file.toStdU16String()); }
QString fromPath(const fs::path& p) { return QString::fromStdU16String(p.u16string()); }

QDoubleSpinBox* mmSpin(QWidget* parent) {
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(0, 100);
    s->setDecimals(1);
    s->setSingleStep(0.5);
    s->setSuffix(BatchWindow::tr(" mm"));
    return s;
}

QTableWidgetItem* readOnly(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

enum EntryColumn { kPath = 0, kKey, kQuantity, kNote };

} // namespace

BatchWindow::BatchWindow(Session& session, QWidget* parent) : QMainWindow(parent, Qt::Window), session_(session) {
    setObjectName(QStringLiteral("batchWindow"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(1000, 700);
    editor_.newBatch(tr("Batch").toStdString(), QDate::currentDate().toString(Qt::ISODate).toStdString());

    QToolBar* bar = addToolBar(tr("Batch"));
    bar->setObjectName(QStringLiteral("batchToolBar"));
    bar->addAction(tr("New"), this, [this] { newBatch(); });
    bar->addAction(tr("Open…"), this, [this] { openBatch(); });
    bar->addAction(tr("Save"), this, [this] { save(); });
    bar->addAction(tr("Save As…"), this, [this] { saveAs(); });

    tabs_ = new QTabWidget(this);
    tabs_->addTab(buildProjectsTab(), tr("Projects"));
    tabs_->addTab(buildSettingsTab(), tr("Settings"));
    tabs_->addTab(buildSummaryTab(), tr("Summary"));
    setCentralWidget(tabs_);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 2) refreshSummary();
    });
    reload();
}

// ------------------------------------------------------------------- tabs

QWidget* BatchWindow::buildProjectsTab() {
    auto* page = new QWidget(this);
    name_ = new QLineEdit(page);
    name_->setObjectName(QStringLiteral("batchName"));
    date_ = new QLineEdit(page);
    date_->setPlaceholderText(tr("YYYY-MM-DD"));
    notes_ = new QPlainTextEdit(page);
    notes_->setMaximumHeight(60);
    auto* applyInfo = new QPushButton(tr("Apply"), page);
    auto* infoError = makeErrorLabel(page);

    entries_ = new QTableWidget(0, 4, page);
    entries_->setObjectName(QStringLiteral("batchEntries"));
    entries_->setHorizontalHeaderLabels({tr("Project file"), tr("Key"), tr("Quantity"), tr("Note")});
    entries_->horizontalHeader()->setSectionResizeMode(kPath, QHeaderView::Stretch);
    entries_->horizontalHeader()->setSectionResizeMode(kNote, QHeaderView::Stretch);
    entries_->setSelectionBehavior(QAbstractItemView::SelectRows);
    entries_->setToolTip(tr("Key, quantity and note can be edited in place. An empty key is derived from the file name."));
    entriesError_ = makeErrorLabel(page);
    auto* add = new QPushButton(tr("Add project…"), page);
    auto* addCurrent = new QPushButton(tr("Add current project"), page);
    addCurrent->setToolTip(tr("Adds the project open in the main window; it must be saved first."));
    auto* remove = new QPushButton(tr("Remove"), page);

    auto* form = new QFormLayout;
    form->addRow(tr("Name:"), name_);
    form->addRow(tr("Date:"), date_);
    form->addRow(tr("Notes:"), notes_);
    form->addRow(applyInfo);
    form->addRow(infoError);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(addCurrent);
    buttons->addWidget(remove);
    buttons->addStretch();
    auto* layout = new QVBoxLayout(page);
    layout->addLayout(form);
    layout->addWidget(entries_, 1);
    layout->addLayout(buttons);
    layout->addWidget(entriesError_);

    connect(applyInfo, &QPushButton::clicked, this, [this, infoError] {
        edited(editor_.setInfo(str(name_->text()), str(date_->text()), notes_->toPlainText().toStdString()), infoError);
    });
    connect(add, &QPushButton::clicked, this, &BatchWindow::addProjects);
    connect(addCurrent, &QPushButton::clicked, this, &BatchWindow::addCurrentProject);
    connect(remove, &QPushButton::clicked, this, &BatchWindow::removeProject);
    connect(entries_, &QTableWidget::cellChanged, this, &BatchWindow::entryEdited);
    return page;
}

QWidget* BatchWindow::buildSettingsTab() {
    auto* inner = new QWidget;
    // Material substitutions.
    auto* subBox = new QGroupBox(tr("Material substitutions"), inner);
    substitutions_ = new QTableWidget(0, 2, subBox);
    substitutions_->setObjectName(QStringLiteral("batchSubstitutions"));
    substitutions_->setHorizontalHeaderLabels({tr("Material id"), tr("Replaced by (stock material id)")});
    substitutions_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    substitutions_->setMinimumHeight(120);
    substitutions_->setToolTip(tr("Every part of the first material is cut from the second one. Each change is "
                                  "reported in the summary; a missing target or another thickness is refused."));
    materials_ = new QLabel(subBox);
    materials_->setWordWrap(true);
    materials_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    substitutionError_ = makeErrorLabel(subBox);
    auto* addSub = new QPushButton(tr("Add row"), subBox);
    auto* removeSub = new QPushButton(tr("Remove row"), subBox);
    auto* applySub = new QPushButton(tr("Apply substitutions"), subBox);
    auto* subButtons = new QHBoxLayout;
    subButtons->addWidget(addSub);
    subButtons->addWidget(removeSub);
    subButtons->addWidget(applySub);
    subButtons->addStretch();
    auto* subLayout = new QVBoxLayout(subBox);
    subLayout->addWidget(substitutions_);
    subLayout->addLayout(subButtons);
    subLayout->addWidget(materials_);
    subLayout->addWidget(substitutionError_);
    connect(addSub, &QPushButton::clicked, this, [this] { substitutions_->insertRow(substitutions_->rowCount()); });
    connect(removeSub, &QPushButton::clicked, this, [this] {
        if (substitutions_->currentRow() >= 0) substitutions_->removeRow(substitutions_->currentRow());
    });
    connect(applySub, &QPushButton::clicked, this, &BatchWindow::applySubstitutions);

    // Saw (guillotine) settings of the batch: defaults of all materials, as the Cutting workspace.
    auto* sawBox = new QGroupBox(tr("Panel saw settings (all materials)"), inner);
    kerf_ = mmSpin(sawBox);
    trimLeft_ = mmSpin(sawBox);
    trimRight_ = mmSpin(sawBox);
    trimTop_ = mmSpin(sawBox);
    trimBottom_ = mmSpin(sawBox);
    objective_ = choiceBox(objectiveChoices(), cutting::Objective::MinimizeSheets, sawBox);
    objective_->setToolTip(tr("What the heuristic optimiser compares candidate layouts by."));
    groupCutList_ = new QCheckBox(tr("Group identical parts across items in the cut list"), sawBox);
    sawError_ = makeErrorLabel(sawBox);
    auto* applySaw = new QPushButton(tr("Apply"), sawBox);
    auto* sawForm = new QFormLayout(sawBox);
    sawForm->addRow(tr("Kerf (blade width):"), kerf_);
    sawForm->addRow(tr("Trim left:"), trimLeft_);
    sawForm->addRow(tr("Trim right:"), trimRight_);
    sawForm->addRow(tr("Trim top:"), trimTop_);
    sawForm->addRow(tr("Trim bottom:"), trimBottom_);
    sawForm->addRow(tr("Objective:"), objective_);
    sawForm->addRow(groupCutList_);
    sawForm->addRow(applySaw);
    sawForm->addRow(sawError_);
    connect(applySaw, &QPushButton::clicked, this, &BatchWindow::applySawSettings);

    // Nesting settings: the project editor, pointed at the batch settings.
    nesting_ = new NestingSettingsForm([this] { return editor_.settingsProject(); },
                                       [this](const project::Command& command) {
                                           auto r = editor_.executeSettingsCommand(command);
                                           if (r) {
                                               evaluation_.reset();
                                               updateTitle();
                                           }
                                           return r;
                                       },
                                       inner);

    auto* layout = new QVBoxLayout(inner);
    layout->addWidget(subBox);
    layout->addWidget(sawBox);
    layout->addWidget(nesting_);
    layout->addStretch();
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(inner);
    return scroll;
}

// ------------------------------------------------------------------- state

void BatchWindow::reload() {
    loading_ = true;
    const project::Batch& b = editor_.batch();
    name_->setText(qs(b.name));
    date_->setText(qs(b.date));
    notes_->setPlainText(qs(b.notes));
    loading_ = false;
    loadEntries();
    loadSubstitutions();
    loadSawSettings();
    nesting_->load();
    showError(entriesError_, {});
    showError(substitutionError_, {});
    showError(sawError_, {});
    evaluation_.reset();
    if (tabs_->currentIndex() == 2) refreshSummary();
    updateTitle();
}

void BatchWindow::loadEntries() {
    loading_ = true;
    const auto& entries = editor_.batch().entries;
    entries_->setRowCount(static_cast<int>(entries.size()));
    for (int row = 0; row < entries_->rowCount(); ++row) {
        const project::BatchEntry& e = entries[static_cast<std::size_t>(row)];
        entries_->setItem(row, kPath, readOnly(qs(e.path)));
        auto* key = new QTableWidgetItem(qs(e.key));
        key->setToolTip(tr("Effective key: %1").arg(qs(project::batchKey(e))));
        entries_->setItem(row, kKey, key);
        auto* qty = new QTableWidgetItem;
        qty->setData(Qt::EditRole, e.quantity);
        entries_->setItem(row, kQuantity, qty);
        entries_->setItem(row, kNote, new QTableWidgetItem(qs(e.note)));
    }
    loading_ = false;
}

void BatchWindow::loadSubstitutions() {
    const auto& subs = editor_.batch().materialSubstitutions;
    substitutions_->setRowCount(0);
    for (const auto& [from, to] : subs) {
        const int row = substitutions_->rowCount();
        substitutions_->insertRow(row);
        substitutions_->setItem(row, 0, new QTableWidgetItem(qs(from)));
        substitutions_->setItem(row, 1, new QTableWidgetItem(qs(to)));
    }
}

void BatchWindow::loadSawSettings() {
    const CuttingSettingsInput s = cuttingSettingsInput(editor_.settingsProject(), std::nullopt);
    kerf_->setValue(s.kerf);
    trimLeft_->setValue(s.trim.left);
    trimRight_->setValue(s.trim.right);
    trimTop_->setValue(s.trim.top);
    trimBottom_->setValue(s.trim.bottom);
    selectData(objective_, static_cast<int>(s.objective));
    groupCutList_->setChecked(editor_.batch().cutListGroupAcrossAssemblies);
}

void BatchWindow::edited(const core::Result<void>& result, QLabel* error) {
    if (!result) {
        showError(error, qs(result.error().message));
        return;
    }
    showError(error, {});
    evaluation_.reset();
    if (tabs_->currentIndex() == 2) refreshSummary();
    updateTitle();
}

void BatchWindow::updateTitle() {
    const QString file = editor_.filePath().empty() ? tr("untitled") : fromPath(editor_.filePath().filename());
    setWindowTitle(tr("Production batch %1 — %2%3")
                       .arg(qs(editor_.batch().name), file, editor_.isModified() ? QStringLiteral("*") : QString()));
}

// ------------------------------------------------------------------- edits

void BatchWindow::entryEdited(int row, int column) {
    if (loading_ || row < 0) return;
    const auto index = static_cast<std::size_t>(row);
    QTableWidgetItem* item = entries_->item(row, column);
    if (!item) return;
    core::Result<void> r;
    if (column == kKey) r = editor_.setKey(index, str(item->text()));
    else if (column == kNote) r = editor_.setNote(index, item->text().toStdString());
    else if (column == kQuantity) r = editor_.setQuantity(index, item->data(Qt::EditRole).toInt());
    else return;
    edited(r, entriesError_);
    if (!r) {
        const QString message = entriesError_->text();
        loadEntries(); // back to the stored values
        showError(entriesError_, message);
    } else if (column == kKey) {
        loadEntries();
    }
}

void BatchWindow::addProjects() {
    const QString dir = editor_.filePath().empty() ? QDir::homePath() : fromPath(editor_.baseDir());
    const QStringList files =
        QFileDialog::getOpenFileNames(this, tr("Add projects to the batch"), dir, tr("FurnitureOS projects (*.fosp)"));
    if (files.isEmpty()) return;
    for (const QString& file : files) {
        auto r = editor_.addProject(toPath(file));
        if (!r) {
            edited(r.error(), entriesError_);
            loadEntries();
            return;
        }
    }
    loadEntries();
    edited({}, entriesError_);
}

void BatchWindow::addCurrentProject() {
    if (session_.filePath().empty() || session_.isDirty()) {
        showError(entriesError_, tr("Save the current project first: the batch reads the project file, so unsaved "
                                    "changes would not be produced."));
        return;
    }
    auto r = editor_.addProject(session_.filePath());
    loadEntries();
    edited(r ? core::Result<void>{} : core::Result<void>{r.error()}, entriesError_);
    if (r) entries_->selectRow(static_cast<int>(r.value()));
}

void BatchWindow::removeProject() {
    const int row = entries_->currentRow();
    if (row < 0) return;
    auto r = editor_.removeEntry(static_cast<std::size_t>(row));
    loadEntries();
    edited(r, entriesError_);
}

void BatchWindow::applySubstitutions() {
    std::map<std::string, std::string> subs;
    for (int row = 0; row < substitutions_->rowCount(); ++row) {
        const QString from = substitutions_->item(row, 0) ? substitutions_->item(row, 0)->text().trimmed() : QString();
        const QString to = substitutions_->item(row, 1) ? substitutions_->item(row, 1)->text().trimmed() : QString();
        if (from.isEmpty() && to.isEmpty()) continue; // blank row
        if (subs.count(str(from))) {
            showError(substitutionError_, tr("Material “%1” is substituted twice (again in row %2); keep one row.")
                                              .arg(from)
                                              .arg(row + 1));
            return;
        }
        subs[str(from)] = str(to);
    }
    edited(editor_.setMaterialSubstitutions(std::move(subs)), substitutionError_);
    if (substitutionError_->isHidden()) loadSubstitutions();
}

void BatchWindow::applySawSettings() {
    CuttingSettingsInput in = cuttingSettingsInput(editor_.settingsProject(), std::nullopt); // keeps remnants
    in.kerf = kerf_->value();
    in.trim = {trimLeft_->value(), trimRight_->value(), trimTop_->value(), trimBottom_->value()};
    in.objective = valueOf<cutting::Objective>(objective_);
    auto r = editor_.executeSettingsCommand(cuttingSettingsCommand(editor_.settingsProject(), std::nullopt, in));
    if (r && groupCutList_->isChecked() != editor_.batch().cutListGroupAcrossAssemblies)
        r = editor_.setCutListGroupAcrossAssemblies(groupCutList_->isChecked());
    edited(r, sawError_);
}

// ------------------------------------------------------------------- files

bool BatchWindow::maybeSave() {
    if (!editor_.isModified()) return true;
    const auto answer = QMessageBox::question(
        this, tr("Production batch"), tr("The batch “%1” has unsaved changes. Save them?").arg(qs(editor_.batch().name)),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return save();
    return true;
}

void BatchWindow::closeEvent(QCloseEvent* event) {
    if (maybeSave()) event->accept();
    else event->ignore();
}

void BatchWindow::newBatch() {
    if (!maybeSave()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New production batch"), tr("Batch name:"), QLineEdit::Normal,
                                               tr("Batch"), &ok);
    if (!ok) return;
    editor_.newBatch(str(name), QDate::currentDate().toString(Qt::ISODate).toStdString());
    reload();
}

void BatchWindow::openBatch() {
    if (!maybeSave()) return;
    const QString file = QFileDialog::getOpenFileName(this, tr("Open production batch"), QDir::homePath(),
                                                      tr("Production batches (*.fosbatch)"));
    if (file.isEmpty()) return;
    if (auto r = editor_.open(toPath(file)); !r) {
        QMessageBox::warning(this, tr("Open failed"), qs(r.error().message));
        return;
    }
    reload();
}

bool BatchWindow::save() {
    if (editor_.filePath().empty()) return saveAs();
    if (auto r = editor_.save(); !r) {
        QMessageBox::warning(this, tr("Save failed"), qs(r.error().message));
        return false;
    }
    updateTitle();
    return true;
}

bool BatchWindow::saveAs() {
    const QString suggested =
        editor_.filePath().empty() ? QDir(QDir::homePath()).filePath(qs(editor_.batch().name) + QStringLiteral(".fosbatch"))
                                   : fromPath(editor_.filePath());
    const QString file =
        QFileDialog::getSaveFileName(this, tr("Save production batch"), suggested, tr("Production batches (*.fosbatch)"));
    if (file.isEmpty()) return false;
    if (auto r = editor_.saveAs(toPath(file)); !r) {
        QMessageBox::warning(this, tr("Save failed"), qs(r.error().message));
        return false;
    }
    loadEntries(); // the paths are now relative to the new folder
    updateTitle();
    return true;
}

} // namespace fos::app::gui
