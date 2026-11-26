// SPDX-License-Identifier: AGPL-3.0-only
#include "edge_banding_view.hpp"

#include "fos/app/session.hpp"
#include "fos/production/csv.hpp"
#include "production_workspace.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace fos::app::gui {

EdgeBandingView::EdgeBandingView(Session& session, QWidget* parent) : QWidget(parent), session_(session) {
    setObjectName(QStringLiteral("edgeBandingJobs"));
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    auto* note = new QLabel(EdgeBandingText::orderNote(), this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    auto* csv = new QPushButton(tr("Export CSV…"), this);
    auto* svg = new QPushButton(tr("Export SVG sheets…"), this);
    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(static_cast<int>(EdgeBandingText::columns().size()));
    tree_->setHeaderLabels(EdgeBandingText::columns());
    tree_->setAlternatingRowColors(true);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setStretchLastSection(false);
    warnings_ = new QLabel(this);
    warnings_->setWordWrap(true);
    warnings_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* bar = new QHBoxLayout;
    bar->addWidget(summary_, 1);
    bar->addWidget(csv);
    bar->addWidget(svg);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addWidget(note);
    layout->addWidget(tree_, 1);
    layout->addWidget(warnings_);
    connect(csv, &QPushButton::clicked, this, &EdgeBandingView::exportCsv);
    connect(svg, &QPushButton::clicked, this, &EdgeBandingView::exportSvg);
}

void EdgeBandingView::refresh() {
    if (const project::Resolved* r = session_.resolved())
        jobs_ = projectEdgeBandingJobs(session_.project(), r->model);
    else
        jobs_.reset();
    const EdgeBandingJobRows rows = jobs_ ? EdgeBandingText::rows(*jobs_) : EdgeBandingJobRows{};
    summary_->setText(jobs_ ? rows.summary : tr("The model could not be generated."));
    std::vector<TextNode> nodes = rows.bands;
    if (!rows.contour.empty()) {
        TextNode contour;
        contour.emphasis = true;
        contour.columns = {tr("Shaped parts: %1").arg(EdgeBandingText::contourNote())};
        contour.children = rows.contour;
        nodes.push_back(std::move(contour));
    }
    fillTree(tree_, nodes);
    tree_->expandToDepth(0);
    for (int c = 1; c < tree_->columnCount(); ++c) tree_->resizeColumnToContents(c);
    warnings_->setText(rows.warnings.join(QLatin1Char('\n')));
    warnings_->setVisible(!rows.warnings.isEmpty());
}

QString EdgeBandingView::exportBaseName() const {
    const QString file = QString::fromStdString(session_.filePath().string());
    const QString stem = file.isEmpty() ? tr("untitled") : QFileInfo(file).completeBaseName();
    return stem + tr("-edge-banding");
}

void EdgeBandingView::exportCsv() {
    if (!jobs_) refresh();
    if (!jobs_) return;
    const QString file = QFileDialog::getSaveFileName(this, tr("Export edge-banding jobs as CSV"),
                                                      exportBaseName() + QStringLiteral(".csv"), tr("CSV files (*.csv)"));
    if (file.isEmpty()) return;
    auto r = production::writeTextFile(file.toStdString(), edgeBandingJobsCsv(*jobs_));
    if (!r) QMessageBox::critical(this, tr("Export failed"), QString::fromStdString(r.error().message));
}

void EdgeBandingView::exportSvg() {
    if (!jobs_) refresh();
    if (!jobs_) return;
    const QString file = QFileDialog::getSaveFileName(this, tr("Export edge-banding job sheets as SVG"),
                                                      exportBaseName() + QStringLiteral(".svg"), tr("SVG files (*.svg)"));
    if (file.isEmpty()) return;
    auto r = exportEdgeBandingSheets(*jobs_, file.toStdString());
    if (!r) {
        QMessageBox::critical(this, tr("Export failed"), QString::fromStdString(r.error().message));
        return;
    }
    QMessageBox::information(this, tr("Export"), tr("%n file(s) written.", nullptr, static_cast<int>(r.value().size())));
}

} // namespace fos::app::gui
