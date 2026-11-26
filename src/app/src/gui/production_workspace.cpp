// SPDX-License-Identifier: AGPL-3.0-only
#include "production_workspace.hpp"

#include "edge_banding_view.hpp"
#include "fos/app/edge_banding_rows.hpp"
#include "fos/app/production_text.hpp"
#include "fos/app/session.hpp"
#include "fos/production/csv.hpp"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace fos::app::gui {

void fillTable(QTableWidget* view, const production::Table& table) {
    view->clear();
    view->setColumnCount(static_cast<int>(table.header.size()));
    view->setRowCount(static_cast<int>(table.rows.size()));
    QStringList header;
    for (const auto& h : table.header) header << QString::fromStdString(h);
    view->setHorizontalHeaderLabels(header);
    for (std::size_t r = 0; r < table.rows.size(); ++r)
        for (std::size_t c = 0; c < table.rows[r].size(); ++c)
            view->setItem(static_cast<int>(r), static_cast<int>(c),
                          new QTableWidgetItem(QString::fromStdString(table.rows[r][c])));
    view->resizeColumnsToContents();
}

namespace {
void addNodes(QTreeWidgetItem* parent, QTreeWidget* view, const std::vector<TextNode>& nodes) {
    for (const TextNode& node : nodes) {
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(view);
        for (int c = 0; c < node.columns.size(); ++c) {
            item->setText(c, node.columns[c]);
            if (node.emphasis) {
                QFont font = item->font(c);
                font.setBold(true);
                item->setFont(c, font);
            }
            if (node.warning) item->setForeground(c, QColor(0xb0, 0x00, 0x20));
        }
        if (!node.columns.isEmpty()) item->setToolTip(0, node.columns[0]);
        addNodes(item, view, node.children);
    }
}
} // namespace

void fillTree(QTreeWidget* view, const std::vector<TextNode>& nodes) {
    view->clear();
    addNodes(nullptr, view, nodes);
}

ProductionWorkspace::ProductionWorkspace(Session& session, QWidget* parent) : QWidget(parent), session_(session) {
    setObjectName(QStringLiteral("productionWorkspace"));
    tabs_ = new QTabWidget(this);
    const QStringList names{tr("Cut list"), tr("Parts"), tr("Materials"), tr("Edge banding"), tr("Hardware"),
                            tr("Machining"), tr("Bought-in items")};
    for (const QString& name : names) {
        auto* view = new QTableWidget(tabs_);
        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        view->setAlternatingRowColors(true);
        view->verticalHeader()->hide();
        view->setSortingEnabled(false);
        tabs_->addTab(view, name);
        views_.push_back(view);
    }
    banding_ = new EdgeBandingView(session_, tabs_);
    tabs_->addTab(banding_, tr("Edge-banding jobs"));
    note_ = new QLabel(this);
    note_->setWordWrap(true);
    auto* exportCsv = new QPushButton(tr("Export table as CSV…"), this);
    auto* bar = new QHBoxLayout;
    bar->addWidget(note_, 1);
    bar->addWidget(exportCsv);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addWidget(tabs_, 1);
    connect(tabs_, &QTabWidget::currentChanged, this, &ProductionWorkspace::updateNote);
    updateNote();
    connect(exportCsv, &QPushButton::clicked, this, &ProductionWorkspace::exportCurrentCsv);
    connect(&session_, &Session::projectChanged, this, [this] {
        stale_ = true;
        if (isVisible()) refresh();
    });
}

void ProductionWorkspace::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (stale_) refresh();
}

void ProductionWorkspace::refresh() {
    stale_ = false;
    const auto& reports = session_.productionReports();
    const auto loc = ProductionText::localisation();
    tables_ = {production::cutListTable(reports.cutList, loc),       production::partListTable(reports.parts, loc),
               production::materialTable(reports.materials, loc),    production::edgeBandingTable(reports.edgeBanding, loc),
               production::hardwareTable(reports.hardware, loc),     production::machiningTable(reports.machining, loc),
               BoughtInText::table(reports.boughtIn)};
    for (std::size_t i = 0; i < views_.size(); ++i) fillTable(views_[i], tables_[i]);
    boughtInSummary_ = BoughtInText::summary(reports.boughtIn);
    banding_->refresh();
    updateNote();
}

void ProductionWorkspace::updateNote() {
    const bool boughtIn = !views_.empty() && tabs_->currentWidget() == views_.back();
    note_->setText(boughtIn ? tr("Bought-in items are ordered finished to size from a supplier (glass panes); they are "
                                 "not in the cut list, the material totals or the edge banding. %1")
                                  .arg(boughtInSummary_)
                            : tr("All figures come from the production module, regenerated from the current model. "
                                 "Material sheet counts here are area lower bounds; real counts come from the Cutting workspace."));
}

int ProductionWorkspace::cutListRows() const { return views_.empty() ? 0 : views_[0]->rowCount(); }

void ProductionWorkspace::exportCurrentCsv() {
    if (stale_) refresh();
    const int index = tabs_->currentIndex();
    if (tabs_->currentWidget() == banding_) {
        banding_->exportCsv();
        return;
    }
    if (index < 0 || index >= static_cast<int>(tables_.size())) return;
    const QString file = QFileDialog::getSaveFileName(this, tr("Export table as CSV"),
                                                      tabs_->tabText(index) + QStringLiteral(".csv"), tr("CSV files (*.csv)"));
    if (file.isEmpty()) return;
    production::CsvOptions options;
    options.utf8Bom = true; // spreadsheet programs detect UTF-8 (Ukrainian captions)
    auto r = production::writeTextFile(file.toStdString(), production::toCsv(tables_[static_cast<std::size_t>(index)], options));
    if (!r) QMessageBox::critical(this, tr("Export failed"), QString::fromStdString(r.error().message));
}

} // namespace fos::app::gui
