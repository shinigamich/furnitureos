// SPDX-License-Identifier: AGPL-3.0-only
#include "diagnostics_panel.hpp"

#include "enum_text.hpp"
#include "fos/app/session.hpp"

#include <QHeaderView>

namespace fos::app::gui {
namespace {
constexpr int kPartRole = Qt::UserRole;
constexpr int kItemRole = Qt::UserRole + 1;

QColor severityColor(core::Severity s) {
    switch (s) {
    case core::Severity::Error: return QColor(0xb0, 0x00, 0x20);
    case core::Severity::Warning: return QColor(0x9a, 0x67, 0x00);
    case core::Severity::Info: return QColor(0x40, 0x40, 0x40);
    }
    return {};
}
} // namespace

DiagnosticsPanel::DiagnosticsPanel(Session& session, QWidget* parent) : QTreeWidget(parent), session_(session) {
    setObjectName(QStringLiteral("diagnostics"));
    setRootIsDecorated(false);
    setColumnCount(5);
    setHeaderLabels({tr("Severity"), tr("Source"), tr("Message"), tr("Entity"), tr("Code")});
    header()->setSectionResizeMode(2, QHeaderView::Stretch);
    header()->setStretchLastSection(false);
    setWordWrap(true);
    setToolTip(tr("Messages from the engineering core are in English."));
    connect(&session_, &Session::projectChanged, this, &DiagnosticsPanel::rebuild);
    connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        const std::string part = item->data(0, kPartRole).toString().toStdString();
        const std::string owner = item->data(0, kItemRole).toString().toStdString();
        if (!part.empty()) session_.selectPart(part);
        else if (!owner.empty()) session_.selectItem(owner);
    });
    rebuild();
}

void DiagnosticsPanel::rebuild() {
    clear();
    std::vector<DiagnosticRow> rows;
    if (const project::Resolved* r = session_.resolved()) {
        rows = diagnosticRows(*r, session_.validationReport());
    } else if (!session_.resolveError().empty()) {
        rows.push_back({core::Severity::Error, DiagnosticSource::Generation, "project.structure",
                        session_.resolveError(), {}, {}, {}});
    }
    for (const auto& row : rows) {
        auto* item = new QTreeWidgetItem(this);
        item->setText(0, severityText(row.severity));
        item->setForeground(0, severityColor(row.severity));
        item->setText(1, row.source == DiagnosticSource::Generation ? tr("Generation") : tr("Validation"));
        item->setText(2, QString::fromStdString(row.message));
        item->setToolTip(2, QString::fromStdString(row.message));
        item->setText(3, QString::fromStdString(row.partId.empty() ? row.entityId : row.partId));
        item->setText(4, QString::fromStdString(row.code));
        item->setData(0, kPartRole, QString::fromStdString(row.partId));
        item->setData(0, kItemRole, QString::fromStdString(row.itemId));
    }
    resizeColumnToContents(0);
    resizeColumnToContents(1);
    summary_ = summarize(rows);
    emit summaryChanged();
}

} // namespace fos::app::gui
