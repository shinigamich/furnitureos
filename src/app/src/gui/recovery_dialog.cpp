// SPDX-License-Identifier: AGPL-3.0-only
#include "recovery_dialog.hpp"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace fos::app::gui {

RecoveryDialog::RecoveryDialog(RecoveryStore store, std::vector<RecoveryEntry> entries, QWidget* parent)
    : QDialog(parent), store_(std::move(store)), entries_(std::move(entries)) {
    setObjectName(QStringLiteral("recoveryDialog"));
    setWindowTitle(tr("Recover unsaved work"));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("FurnitureOS was not closed normally. Unsaved changes of these projects were "
                                "autosaved. Restore opens a copy as a modified project; Save then writes it back "
                                "to the original file."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("recoveryList"));
    tree_->setRootIsDecorated(false);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setHeaderLabels({tr("Project"), tr("Original file"), tr("Autosaved")});
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(tree_);

    auto* buttons = new QDialogButtonBox(this);
    restore_ = buttons->addButton(tr("Restore"), QDialogButtonBox::AcceptRole);
    discard_ = buttons->addButton(tr("Discard"), QDialogButtonBox::DestructiveRole);
    QPushButton* keep = buttons->addButton(tr("Keep for later"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    connect(restore_, &QPushButton::clicked, this, &RecoveryDialog::restoreSelected);
    connect(discard_, &QPushButton::clicked, this, &RecoveryDialog::discardSelected);
    connect(keep, &QPushButton::clicked, this, &QDialog::reject);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &RecoveryDialog::updateButtons);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, &RecoveryDialog::restoreSelected);

    fillTree();
    resize(760, 320);
}

void RecoveryDialog::fillTree() {
    tree_->clear();
    for (const auto& e : entries_) {
        const QString project = e.projectName.isEmpty() ? tr("(unknown project)") : e.projectName;
        const QString original = !e.sidecarValid ? tr("(unknown: the recovery information is damaged)")
                                 : e.untitled()  ? tr("(never saved)")
                                                 : e.originalPath;
        auto* item = new QTreeWidgetItem(
            tree_, {project, original, QLocale().toString(e.savedAt.toLocalTime(), QLocale::ShortFormat)});
        item->setToolTip(1, original);
    }
    tree_->resizeColumnToContents(0);
    tree_->resizeColumnToContents(2);
    if (tree_->topLevelItemCount() > 0) tree_->setCurrentItem(tree_->topLevelItem(0));
    updateButtons();
}

std::vector<int> RecoveryDialog::selectedRows() const {
    std::vector<int> rows;
    for (auto* item : tree_->selectedItems()) rows.push_back(tree_->indexOfTopLevelItem(item));
    std::sort(rows.begin(), rows.end());
    return rows;
}

void RecoveryDialog::updateButtons() {
    const auto rows = selectedRows();
    restore_->setEnabled(rows.size() == 1);
    discard_->setEnabled(!rows.empty());
}

void RecoveryDialog::restoreSelected() {
    const auto rows = selectedRows();
    if (rows.size() != 1) return;
    chosen_ = entries_[static_cast<std::size_t>(rows.front())];
    accept();
}

void RecoveryDialog::discardSelected() {
    const auto rows = selectedRows();
    if (rows.empty()) return;
    const auto answer = QMessageBox::warning(
        this, tr("Discard recovered work"),
        tr("Delete %n recovery copy(ies)? The unsaved changes in them are lost.", nullptr, static_cast<int>(rows.size())),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Discard) return;
    QStringList failed;
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
        const auto index = static_cast<std::size_t>(*it);
        if (store_.remove(entries_[index].key))
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
        else
            failed << QString::fromStdU16String(entries_[index].file.u16string());
    }
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Discard recovered work"),
                             tr("These recovery files could not be deleted:\n%1").arg(failed.join(QLatin1Char('\n'))));
    if (entries_.empty()) {
        reject();
        return;
    }
    fillTree();
}

} // namespace fos::app::gui
