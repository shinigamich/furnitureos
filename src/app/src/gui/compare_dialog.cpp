// SPDX-License-Identifier: AGPL-3.0-only
#include "compare_dialog.hpp"

#include "fos/app/project_compare.hpp"
#include "fos/production/csv.hpp"
#include "production_workspace.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace fos::app::gui {

CompareDialog::CompareDialog(project::ProjectDiff diff, const QString& otherFile, QWidget* parent)
    : QDialog(parent), diff_(std::move(diff)), otherFile_(otherFile) {
    setObjectName(QStringLiteral("compareDialog"));
    setWindowTitle(tr("Compare with %1").arg(QFileInfo(otherFile).fileName()));
    auto* intro = new QLabel(tr("Changes from the current project (left) to %1 (right), including the generated "
                                "parts and hardware.")
                                 .arg(QDir::toNativeSeparators(otherFile)),
                             this);
    intro->setWordWrap(true);
    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(static_cast<int>(CompareText::columns().size()));
    tree_->setHeaderLabels(CompareText::columns());
    tree_->setAlternatingRowColors(true);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    fillTree(tree_, CompareText::tree(diff_));
    tree_->expandToDepth(1);
    tree_->resizeColumnToContents(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* text = buttons->addButton(tr("Export as text…"), QDialogButtonBox::ActionRole);
    auto* json = buttons->addButton(tr("Export as JSON…"), QDialogButtonBox::ActionRole);
    connect(text, &QPushButton::clicked, this, &CompareDialog::exportText);
    connect(json, &QPushButton::clicked, this, &CompareDialog::exportJson);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(tree_, 1);
    layout->addWidget(buttons);
    resize(900, 600);
}

void CompareDialog::save(const QString& caption, const QString& suffix, const QString& filter, const std::string& content) {
    const QString name = QFileInfo(otherFile_).completeBaseName() + tr("-comparison") + suffix;
    const QString file = QFileDialog::getSaveFileName(this, caption, name, filter);
    if (file.isEmpty()) return;
    auto r = production::writeTextFile(file.toStdString(), content);
    if (!r) QMessageBox::critical(this, tr("Export failed"), QString::fromStdString(r.error().message));
}

void CompareDialog::exportText() {
    save(tr("Export comparison as text"), QStringLiteral(".txt"), tr("Text files (*.txt)"), compareText(diff_));
}

void CompareDialog::exportJson() {
    save(tr("Export comparison as JSON"), QStringLiteral(".json"), tr("JSON files (*.json)"), compareJson(diff_));
}

} // namespace fos::app::gui
