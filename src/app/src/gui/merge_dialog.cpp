// SPDX-License-Identifier: AGPL-3.0-only
#include "merge_dialog.hpp"

#include "fos/app/merge_rows.hpp"
#include "fos/app/session.hpp"
#include "inspector_panel.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace fos::app::gui {
namespace {
constexpr int kIdRole = Qt::UserRole;
}

MergeDialog::MergeDialog(Session& session, project::Project source, const QString& sourceFile, QWidget* parent)
    : QDialog(parent), session_(session), source_(std::move(source)) {
    setObjectName(QStringLiteral("mergeDialog"));
    setWindowTitle(tr("Import items from %1").arg(QFileInfo(sourceFile).fileName()));
    items_ = new QListWidget(this);
    items_->setObjectName(QStringLiteral("mergeItems"));
    for (const auto& row : mergeSourceRows(source_)) {
        auto* item = new QListWidgetItem(tr("%1 — %2 (%3)").arg(row.name, QString::fromStdString(row.id), row.kind), items_);
        item->setData(kIdRole, QString::fromStdString(row.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    policy_ = new QComboBox(this);
    policy_->setObjectName(QStringLiteral("mergePolicy"));
    for (const auto& c : conflictPolicyChoices()) policy_->addItem(c.text, static_cast<int>(c.policy));
    policy_->setToolTip(tr("What happens to a material, edge band or hardware entry whose id exists in this project "
                           "with a different content. Identical entries are never duplicated."));
    preview_ = new QPlainTextEdit(this);
    preview_->setObjectName(QStringLiteral("mergePreview"));
    preview_->setReadOnly(true);
    error_ = makeErrorLabel(this);
    error_->setObjectName(QStringLiteral("mergeError"));
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setText(tr("Import"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Items to import (they keep their position from the other project):"), this));
    layout->addWidget(items_, 1);
    auto* form = new QFormLayout;
    form->addRow(tr("Differing library entries:"), policy_);
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("Preview:"), this));
    layout->addWidget(preview_, 1);
    layout->addWidget(error_);
    layout->addWidget(buttons_);
    resize(560, 520);

    connect(items_, &QListWidget::itemChanged, this, &MergeDialog::updatePreview);
    connect(policy_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MergeDialog::updatePreview);
    connect(buttons_, &QDialogButtonBox::accepted, this, [this] {
        if (importChecked()) accept();
    });
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updatePreview();
}

std::vector<std::string> MergeDialog::checkedIds() const {
    std::vector<std::string> ids;
    for (int i = 0; i < items_->count(); ++i)
        if (items_->item(i)->checkState() == Qt::Checked)
            ids.push_back(items_->item(i)->data(kIdRole).toString().toStdString());
    return ids;
}

void MergeDialog::updatePreview() {
    const auto ids = checkedIds();
    QPushButton* ok = buttons_->button(QDialogButtonBox::Ok);
    if (!mergeAllowed(ids)) {
        preview_->clear();
        showError(error_, tr("Check at least one item to import."));
        ok->setEnabled(false);
        return;
    }
    const auto policy = static_cast<formats::ConflictPolicy>(policy_->currentData().toInt());
    auto plan = project::planMerge(session_.project(), source_, mergeRequest(ids, policy));
    if (!plan) {
        preview_->clear();
        showError(error_, QString::fromStdString(plan.error().message));
        ok->setEnabled(false);
        return;
    }
    preview_->setPlainText(mergePreviewLines(plan.value()).join(QLatin1Char('\n')));
    showError(error_, {});
    ok->setEnabled(true);
}

bool MergeDialog::importChecked() {
    const auto ids = checkedIds();
    if (!mergeAllowed(ids)) return false;
    const auto policy = static_cast<formats::ConflictPolicy>(policy_->currentData().toInt());
    auto r = session_.execute(project::commands::mergeItems(source_, mergeRequest(ids, policy)));
    if (!r) {
        showError(error_, QString::fromStdString(r.error().message));
        return false;
    }
    return true;
}

void importItemsFromProject(Session& session, QWidget* parent) {
    const QString file = QFileDialog::getOpenFileName(parent, MergeDialog::tr("Import items from project"), QString(),
                                                      MergeDialog::tr("FurnitureOS projects (*.fosp)"));
    if (file.isEmpty()) return;
    auto source = loadMergeSource(std::filesystem::path(file.toStdU16String()));
    if (!source) {
        QMessageBox::warning(parent, MergeDialog::tr("Import items from project"),
                             QString::fromStdString(source.error().message));
        return;
    }
    MergeDialog dialog(session, std::move(source).value(), file, parent);
    dialog.exec();
}

} // namespace fos::app::gui
