// SPDX-License-Identifier: AGPL-3.0-only
#include "library_window.hpp"

#include "fos/app/session.hpp"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace fos::app::gui {

// ------------------------------------------------------------------ LibraryPage

LibraryPage::LibraryPage(Session& session, LibraryCategory category, QWidget* parent)
    : QWidget(parent), session_(session), category_(category) {}

void LibraryPage::buildLayout(QWidget* form, bool allowNew) {
    form_ = form;
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("libraryList"));
    list_->setMinimumWidth(260);
    connect(list_, &QListWidget::currentRowChanged, this, &LibraryPage::rowChanged);

    auto* newButton = new QPushButton(tr("&New"), this);
    newButton->setObjectName(QStringLiteral("libraryNew"));
    newButton->setVisible(allowNew);
    duplicate_ = new QPushButton(tr("D&uplicate"), this);
    duplicate_->setObjectName(QStringLiteral("libraryDuplicate"));
    delete_ = new QPushButton(tr("&Delete"), this);
    delete_->setObjectName(QStringLiteral("libraryDelete"));
    apply_ = new QPushButton(tr("&Apply"), this);
    apply_->setObjectName(QStringLiteral("libraryApply"));
    apply_->setDefault(true);
    connect(newButton, &QPushButton::clicked, this, &LibraryPage::startNew);
    connect(duplicate_, &QPushButton::clicked, this, &LibraryPage::duplicateCurrent);
    connect(delete_, &QPushButton::clicked, this, &LibraryPage::deleteCurrent);
    connect(apply_, &QPushButton::clicked, this, &LibraryPage::apply);

    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("libraryMessage"));
    message_->setWordWrap(true);
    message_->setStyleSheet(QStringLiteral("color: #b00020;"));
    message_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(newButton);
    buttons->addWidget(duplicate_);
    buttons->addWidget(delete_);
    buttons->addStretch();
    buttons->addWidget(apply_);

    auto* right = new QVBoxLayout;
    right->addWidget(form_);
    right->addWidget(message_);
    right->addStretch();
    right->addLayout(buttons);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(list_, 2);
    layout->addLayout(right, 3);
    reload();
}

void LibraryPage::reload() {
    const auto rows = libraryRows(session_.project(), category_);
    {
        const QSignalBlocker block(list_);
        list_->clear();
        for (const auto& row : rows) {
            auto* item = new QListWidgetItem(LibraryText::rowCaption(row), list_);
            item->setData(Qt::UserRole, QString::fromStdString(row.id));
            if (!row.usedBy.empty()) {
                QStringList users;
                for (const auto& u : row.usedBy)
                    if (const auto* it = session_.project().findItem(u)) users << QString::fromStdString(it->name());
                item->setToolTip(tr("Used by: %1").arg(users.join(QStringLiteral(", "))));
            }
        }
    }
    if (editingNew_) return; // keep the unsaved New / Duplicate form
    const auto found = std::find_if(rows.begin(), rows.end(), [&](const LibraryRow& r) { return r.id == currentId_; });
    if (found == rows.end()) {
        currentId_.clear();
        if (!rows.empty()) {
            list_->setCurrentRow(0); // emits rowChanged
            return;
        }
        clearForm();
        setEditing(false);
        return;
    }
    {
        const QSignalBlocker block(list_);
        list_->setCurrentRow(static_cast<int>(found - rows.begin()));
    }
    showEntry(currentId_);
    setEditing(false);
}

void LibraryPage::selectEntry(const std::string& id) {
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(Qt::UserRole).toString().toStdString() == id) list_->setCurrentRow(i);
}

void LibraryPage::rowChanged(int row) {
    if (row < 0) return;
    currentId_ = list_->item(row)->data(Qt::UserRole).toString().toStdString();
    showMessages({});
    showEntry(currentId_);
    setEditing(false);
}

void LibraryPage::setEditing(bool editingNew) {
    editingNew_ = editingNew;
    setIdEditable(editingNew);
    const bool hasEntry = editingNew || !currentId_.empty();
    form_->setEnabled(hasEntry);
    apply_->setEnabled(hasEntry);
    duplicate_->setEnabled(!editingNew && !currentId_.empty());
    delete_->setEnabled(!editingNew && !currentId_.empty());
}

void LibraryPage::showMessages(const QStringList& messages) {
    message_->setText(messages.join(QLatin1Char('\n')));
}

void LibraryPage::startNew() {
    {
        const QSignalBlocker block(list_);
        list_->clearSelection();
        list_->setCurrentRow(-1);
    }
    showMessages({});
    showNew();
    setEditing(true);
}

void LibraryPage::duplicateCurrent() {
    if (currentId_.empty() || editingNew_) return;
    const std::string source = currentId_;
    {
        const QSignalBlocker block(list_);
        list_->clearSelection();
        list_->setCurrentRow(-1);
    }
    showMessages({});
    showDuplicate(source);
    setEditing(true);
}

void LibraryPage::deleteCurrent() {
    if (currentId_.empty() || editingNew_) return;
    showMessages(removeLibraryEntry(session_, category_, currentId_));
}

void LibraryPage::apply() {
    const bool isNew = editingNew_;
    const std::string previous = currentId_;
    // The project-changed notification arrives inside execute: select the applied entry then.
    currentId_ = formId();
    editingNew_ = false;
    const LibraryEditResult result = applyForm(isNew);
    if (!result.isEmpty()) {
        currentId_ = previous;
        editingNew_ = isNew;
    }
    showMessages(result);
}

// ------------------------------------------------------------------ LibraryWindow

LibraryWindow::LibraryWindow(Session& session, QWidget* parent) : QMainWindow(parent), session_(session) {
    setObjectName(QStringLiteral("libraryWindow"));
    setWindowTitle(tr("Project libraries"));
    setWindowFlag(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose, false);
    tabs_ = new QTabWidget(this);
    pages_[0] = makeMaterialPage(session_, tabs_);
    pages_[1] = makeEdgeBandPage(session_, tabs_);
    pages_[2] = makeHardwarePage(session_, tabs_);
    tabs_->addTab(pages_[0], tr("&Materials"));
    tabs_->addTab(pages_[1], tr("&Edge bands"));
    tabs_->addTab(pages_[2], tr("&Hardware"));
    setCentralWidget(tabs_);

    QMenu* file = menuBar()->addMenu(tr("&Library"));
    QAction* importAction = file->addAction(tr("&Import library file…"), this, &LibraryWindow::importDialog);
    importAction->setObjectName(QStringLiteral("actionImportLibrary"));
    QAction* exportAction = file->addAction(tr("&Export library file…"), this, &LibraryWindow::exportDialog);
    exportAction->setObjectName(QStringLiteral("actionExportLibrary"));
    file->addSeparator();
    file->addAction(tr("&Close"), QKeySequence::Close, this, &QWidget::close);
    statusBar();

    connect(&session_, &Session::projectChanged, this, &LibraryWindow::reloadAll);
    connect(&session_, &Session::documentReplaced, this, &LibraryWindow::reloadAll);
    resize(900, 560);
}

LibraryPage* LibraryWindow::page(LibraryCategory category) const {
    switch (category) {
    case LibraryCategory::Material: return pages_[0];
    case LibraryCategory::EdgeBand: return pages_[1];
    case LibraryCategory::Hardware: return pages_[2];
    }
    return pages_[0];
}

void LibraryWindow::reloadAll() {
    for (LibraryPage* p : pages_) p->reload();
}

void LibraryWindow::importDialog() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Import library file"), {},
                                                      tr("FurnitureOS library (*.foslib);;All files (*)"));
    if (!path.isEmpty()) importLibraryFile(path);
}

void LibraryWindow::exportDialog() {
    QString path = QFileDialog::getSaveFileName(this, tr("Export library file"), tr("library.foslib"),
                                                tr("FurnitureOS library (*.foslib)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".foslib");
    exportLibraryFile(path);
}

void LibraryWindow::exportLibraryFile(const QString& path) {
    if (auto r = exportProjectLibrary(session_.project(), path.toStdString()); !r) {
        QMessageBox::warning(this, tr("Export library file"), QString::fromStdString(r.error().message));
        return;
    }
    statusBar()->showMessage(tr("Library written to %1").arg(QDir::toNativeSeparators(path)), 8000);
}

void LibraryWindow::importLibraryFile(const QString& path) {
    core::Diagnostics warnings;
    auto loaded = formats::importLibrary(path.toStdString(), {}, &warnings);
    if (!loaded) {
        QMessageBox::warning(this, tr("Import library file"), QString::fromStdString(loaded.error().message));
        return;
    }
    // Entries whose id exists: ask whether to replace, keep or add them as copies.
    auto policy = formats::ConflictPolicy::Skip;
    auto probe = previewLibraryImport(session_.project(), loaded.value(), formats::ConflictPolicy::Skip);
    if (!probe) {
        QMessageBox::warning(this, tr("Import library file"), QString::fromStdString(probe.error().message));
        return;
    }
    if (const auto conflicts = probe.value().count(formats::MergeAction::Skipped); conflicts > 0) {
        QMessageBox box(QMessageBox::Question, tr("Import library file"),
                        tr("%n entry(ies) of the file have an id that already exists in the project library.", nullptr,
                           static_cast<int>(conflicts)),
                        QMessageBox::NoButton, this);
        QPushButton* replace = box.addButton(tr("Replace existing"), QMessageBox::AcceptRole);
        QPushButton* keep = box.addButton(tr("Keep existing"), QMessageBox::AcceptRole);
        QPushButton* copies = box.addButton(tr("Add as copies"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == replace) policy = formats::ConflictPolicy::Replace;
        else if (box.clickedButton() == keep) policy = formats::ConflictPolicy::Skip;
        else if (box.clickedButton() == copies) policy = formats::ConflictPolicy::RenameWithNewId;
        else return;
    }
    auto report = previewLibraryImport(session_.project(), loaded.value(), policy);
    const QString name = QFileInfo(path).fileName();
    if (auto r = session_.execute(importLibraryCommand(loaded.value(), policy, name.toStdString())); !r) {
        QMessageBox::warning(this, tr("Import library file"), QString::fromStdString(r.error().message));
        return;
    }
    QString summary = report ? LibraryText::mergeSummary(report.value()) : QString();
    for (const auto& w : warnings) summary += QStringLiteral("\n") + QString::fromStdString(w.message);
    statusBar()->showMessage(tr("Imported %1: %2").arg(name, summary.section(QLatin1Char('\n'), 0, 0)), 10000);
    if (!warnings.empty()) QMessageBox::information(this, tr("Import library file"), summary);
}

} // namespace fos::app::gui
