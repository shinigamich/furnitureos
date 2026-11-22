// SPDX-License-Identifier: AGPL-3.0-only
// Production batch window: Summary tab (project::batchSummary of the merged batch) and the
// exports (combined cut list, labels, cutting plan SVGs).
#include "batch_window.hpp"

#include "enum_text.hpp"
#include "fos/core/units.hpp"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fos::app::gui {

namespace fs = std::filesystem;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
fs::path toPath(const QString& file) { return fs::path(file.toStdU16String()); }
QString fromPath(const fs::path& p) { return QString::fromStdU16String(p.u16string()); }
QString metres(double v) { return QString::fromStdString(core::formatLength(v, 2)); }

QColor severityColor(core::Severity s) {
    switch (s) {
    case core::Severity::Error: return QColor(0xb0, 0x00, 0x20);
    case core::Severity::Warning: return QColor(0x9a, 0x67, 0x00);
    case core::Severity::Info: break;
    }
    return QColor(0x40, 0x40, 0x40);
}

QTableWidgetItem* cell(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QTableWidget* readOnlyTable(int columns, QWidget* parent) {
    auto* t = new QTableWidget(0, columns, parent);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->verticalHeader()->hide();
    return t;
}

} // namespace

QWidget* BatchWindow::buildSummaryTab() {
    auto* page = new QWidget(this);
    auto* refresh = new QPushButton(tr("Recalculate"), page);
    refresh->setToolTip(tr("Reloads every listed project from disk and merges the batch again."));
    incomplete_ = new QLabel(page);
    incomplete_->setObjectName(QStringLiteral("batchIncomplete"));
    incomplete_->setWordWrap(true);
    incomplete_->setStyleSheet(QStringLiteral("color: #b00020; font-weight: bold;"));
    incomplete_->hide();

    summary_ = readOnlyTable(9, page);
    summary_->setObjectName(QStringLiteral("batchSummary"));
    summary_->setHorizontalHeaderLabels({tr("Key"), tr("Project"), tr("Qty"), tr("Included"), tr("Parts"),
                                         tr("Sheets (lower bound)"), tr("Edge banding applied (m)"),
                                         tr("Edge banding purchase (m)"), tr("Hardware")});
    summary_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    auto* note = new QLabel(tr("Sheets are area lower bounds, not a cutting plan; the total is computed on the merged "
                               "batch, so orders sharing sheets can need fewer than the sum."),
                            page);
    note->setWordWrap(true);

    diagnostics_ = readOnlyTable(4, page);
    diagnostics_->setObjectName(QStringLiteral("batchDiagnostics"));
    diagnostics_->setHorizontalHeaderLabels({tr("Severity"), tr("Project"), tr("Message"), tr("Code")});
    diagnostics_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    diagnostics_->setWordWrap(true);

    auto* exportCsv = new QPushButton(tr("Export cut list (CSV)…"), page);
    auto* exportLabels = new QPushButton(tr("Export labels (SVG)…"), page);
    auto* exportPlans = new QPushButton(tr("Export cutting plans (SVG)…"), page);

    auto* top = new QHBoxLayout;
    top->addWidget(refresh);
    top->addStretch();
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(exportCsv);
    buttons->addWidget(exportLabels);
    buttons->addWidget(exportPlans);
    buttons->addStretch();
    auto* split = new QSplitter(Qt::Vertical, page);
    auto* upper = new QWidget(split);
    auto* upperLayout = new QVBoxLayout(upper);
    upperLayout->setContentsMargins(0, 0, 0, 0);
    upperLayout->addWidget(summary_);
    upperLayout->addWidget(note);
    split->addWidget(upper);
    split->addWidget(diagnostics_);
    auto* layout = new QVBoxLayout(page);
    layout->addLayout(top);
    layout->addWidget(incomplete_);
    layout->addWidget(split, 1);
    layout->addLayout(buttons);

    connect(refresh, &QPushButton::clicked, this, [this] {
        evaluation_.reset();
        refreshSummary();
    });
    connect(exportCsv, &QPushButton::clicked, this, &BatchWindow::exportCutList);
    connect(exportLabels, &QPushButton::clicked, this, &BatchWindow::exportLabels);
    connect(exportPlans, &QPushButton::clicked, this, &BatchWindow::exportCuttingPlans);
    return page;
}

const BatchEvaluation& BatchWindow::evaluation() {
    if (!evaluation_) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        evaluation_ = editor_.evaluate();
        QApplication::restoreOverrideCursor();
    }
    return *evaluation_;
}

void BatchWindow::refreshSummary() {
    const BatchEvaluation& e = evaluation();
    const project::BatchSummary& s = e.summary;
    QStringList leftOut;
    for (const auto& row : s.projects)
        if (!row.included) leftOut << qs(row.key);
    if (e.incomplete)
        incomplete_->setText(tr("INCOMPLETE BATCH: the outputs miss the projects marked LEFT OUT (%1) or are affected "
                                "by the errors listed below.")
                                 .arg(leftOut.isEmpty() ? tr("none") : leftOut.join(QStringLiteral(", "))));
    incomplete_->setVisible(e.incomplete);

    summary_->setRowCount(0);
    auto addRow = [this](const project::BatchSummaryRow& r, bool total) {
        const int row = summary_->rowCount();
        summary_->insertRow(row);
        const QString included = total ? QString() : (r.included ? tr("yes") : tr("LEFT OUT"));
        const QStringList texts{total ? tr("TOTAL") : qs(r.key),
                                total ? tr("All projects") : qs(r.name),
                                QString::number(r.quantity),
                                included,
                                QString::number(r.partCount),
                                QString::number(r.sheetsLowerBound),
                                metres(r.edgeBandingAppliedM),
                                metres(r.edgeBandingPurchaseM),
                                QString::number(r.hardwareQuantity)};
        for (int c = 0; c < texts.size(); ++c) {
            auto* item = cell(texts[c]);
            if (c >= 2) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (total) {
                QFont f = item->font();
                f.setBold(true);
                item->setFont(f);
            } else if (!r.included) {
                item->setForeground(severityColor(core::Severity::Error));
            }
            summary_->setItem(row, c, item);
        }
    };
    for (const auto& r : s.projects) addRow(r, false);
    addRow(s.total, true);
    summary_->resizeColumnsToContents();
    summary_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    const auto rows = batchDiagnosticRows(e);
    diagnostics_->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < diagnostics_->rowCount(); ++i) {
        const BatchDiagnosticRow& d = rows[static_cast<std::size_t>(i)];
        auto* severity = cell(severityText(d.severity));
        severity->setForeground(severityColor(d.severity));
        diagnostics_->setItem(i, 0, severity);
        diagnostics_->setItem(i, 1, cell(d.projectKey.empty() ? tr("(batch)") : qs(d.projectKey)));
        auto* message = cell(qs(d.message));
        message->setToolTip(qs(d.message));
        diagnostics_->setItem(i, 2, message);
        diagnostics_->setItem(i, 3, cell(qs(d.code)));
    }
    diagnostics_->resizeRowsToContents();

    // Material choices of the substitution table (merged libraries).
    QStringList choices;
    for (const auto& m : batchMaterialChoices(e))
        choices << tr("%1 (%2, %3 mm)").arg(qs(m.id), qs(m.name), qs(core::formatLength(m.thickness)));
    materials_->setText(choices.isEmpty() ? tr("No material: the batch has no loadable project.")
                                          : tr("Materials in the batch: %1").arg(choices.join(QStringLiteral("; "))));
}

bool BatchWindow::confirmIncomplete() {
    const BatchEvaluation& e = evaluation();
    if (!e.incomplete) return true;
    return QMessageBox::warning(this, tr("Incomplete batch"),
                                tr("The batch has errors: some projects are LEFT OUT or a material substitution was "
                                   "refused (see Summary). Export what can be produced anyway?"),
                                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
}

void BatchWindow::reportWritten(const core::Result<std::vector<fs::path>>& written) {
    if (!written) {
        QMessageBox::warning(this, tr("Export failed"), qs(written.error().message));
        return;
    }
    statusBar()->showMessage(tr("Written %n file(s).", nullptr, static_cast<int>(written.value().size())), 8000);
}

namespace {
QString exportBase(const BatchEditor& editor) {
    const fs::path dir = editor.filePath().empty() ? toPath(QDir::homePath()) : editor.baseDir();
    const QString stem = editor.filePath().empty() ? qs(editor.batch().name) : fromPath(editor.filePath().stem());
    return QDir(fromPath(dir)).filePath(stem);
}
QString exportDir(const BatchEditor& editor) {
    return editor.filePath().empty() ? QDir::homePath() : fromPath(editor.baseDir());
}
} // namespace

void BatchWindow::exportCutList() {
    if (!confirmIncomplete()) return;
    const QString file = QFileDialog::getSaveFileName(this, tr("Export batch cut list"),
                                                      exportBase(editor_) + tr("-cut-list.csv"), tr("CSV files (*.csv)"));
    if (file.isEmpty()) return;
    reportWritten(exportBatchCutListCsv(evaluation(), toPath(file)));
}

void BatchWindow::exportLabels() {
    if (!confirmIncomplete()) return;
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder for the batch labels"), exportDir(editor_));
    if (dir.isEmpty()) return;
    reportWritten(exportBatchLabels(evaluation(), toPath(dir)));
}

void BatchWindow::exportCuttingPlans() {
    if (!confirmIncomplete()) return;
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder for the cutting plan SVGs"), exportDir(editor_));
    if (dir.isEmpty()) return;
    reportWritten(exportBatchCuttingPlans(evaluation(), toPath(dir)));
}

} // namespace fos::app::gui
