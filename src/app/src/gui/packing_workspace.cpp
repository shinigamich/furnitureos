// SPDX-License-Identifier: AGPL-3.0-only
#include "packing_workspace.hpp"

#include "fos/app/session.hpp"
#include "fos/production/csv.hpp"

#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace fos::app::gui {
namespace {

QDoubleSpinBox* limitBox(double max, const QString& suffix, const QString& noLimit, QWidget* parent) {
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(0.0, max);
    box->setDecimals(1);
    box->setSuffix(suffix);
    if (!noLimit.isEmpty()) box->setSpecialValueText(noLimit); // shown for 0
    return box;
}

QTableWidget* makeTable(const char* objectName, QWidget* parent) {
    auto* t = new QTableWidget(parent);
    t->setObjectName(QString::fromLatin1(objectName));
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->verticalHeader()->hide();
    t->setSortingEnabled(false);
    t->setWordWrap(true);
    return t;
}

QTableWidgetItem* cell(const QString& text, bool highlight = false) {
    auto* item = new QTableWidgetItem(text);
    if (highlight) {
        item->setBackground(QColor(255, 205, 210));
        item->setForeground(Qt::black);
        QFont f = item->font();
        f.setBold(true);
        item->setFont(f);
    }
    return item;
}

std::filesystem::path toPath(const QString& s) { return std::filesystem::path(s.toStdString()); }

} // namespace

PackingWorkspace::PackingWorkspace(Session& session, QWidget* parent) : QWidget(parent), session_(session) {
    setObjectName(QStringLiteral("packingWorkspace"));
    pages_ = new QTabWidget(this);
    pages_->setObjectName(QStringLiteral("packingPages"));
    pages_->addTab(buildPackingPage(), tr("Packing list"));
    pages_->addTab(buildAssemblyPage(), tr("Assembly"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(pages_);
    setInput(PackingInput{});

    connect(&session_, &Session::projectChanged, this, [this] {
        stale_ = true;
        if (isVisible()) refresh();
    });
}

QWidget* PackingWorkspace::buildPackingPage() {
    auto* page = new QWidget(this);
    const QString mmSuffix = tr(" mm");
    maxWeight_ = limitBox(10000.0, tr(" kg"), QString(), page);
    maxWeight_->setObjectName(QStringLiteral("packingMaxWeight"));
    maxWeight_->setMinimum(0.1); // production::packingList requires a positive weight limit
    maxLength_ = limitBox(100000.0, mmSuffix, tr("no limit"), page);
    maxWidth_ = limitBox(100000.0, mmSuffix, tr("no limit"), page);
    maxHeight_ = limitBox(100000.0, mmSuffix, tr("no limit"), page);
    padding_ = limitBox(1000.0, mmSuffix, QString(), page);
    maxLength_->setToolTip(tr("Maximum outer package length including padding; 0 = no limit"));
    maxWidth_->setToolTip(tr("Maximum outer package width including padding; 0 = no limit"));
    maxHeight_->setToolTip(tr("Maximum outer package height including padding; 0 = no limit"));
    padding_->setToolTip(tr("Packaging allowance added on each side of the panel stack"));
    maxWeight_->setToolTip(tr("Maximum panel weight per package (packaging and hardware not included)"));

    auto* form = new QFormLayout;
    form->addRow(tr("Max. weight:"), maxWeight_);
    form->addRow(tr("Padding:"), padding_);
    auto* form2 = new QFormLayout;
    form2->addRow(tr("Max. length:"), maxLength_);
    form2->addRow(tr("Max. width:"), maxWidth_);
    form2->addRow(tr("Max. height:"), maxHeight_);
    auto* csv = new QPushButton(tr("Export CSV…"), page);
    auto* labels = new QPushButton(tr("Export package labels…"), page);
    auto* buttons = new QVBoxLayout;
    buttons->addWidget(csv);
    buttons->addWidget(labels);
    buttons->addStretch(1);
    auto* bar = new QHBoxLayout;
    bar->addLayout(form);
    bar->addLayout(form2);
    bar->addStretch(1);
    bar->addLayout(buttons);

    banner_ = new QLabel(page);
    banner_->setObjectName(QStringLiteral("packingUnknownWeightBanner"));
    banner_->setWordWrap(true);
    banner_->setStyleSheet(QStringLiteral("QLabel { background: #c62828; color: white; font-weight: bold; padding: 6px; }"));
    banner_->hide();
    info_ = new QLabel(page);
    info_->setWordWrap(true);
    info_->setText(tr("Packing rule: a simple heuristic (largest panels first, first package that fits), not an "
                      "optimiser. Weights are panels only: finished area × thickness × material density."));
    packages_ = makeTable("packingPackages", page);
    bags_ = makeTable("packingBags", page);
    warnings_ = new QLabel(page);
    warnings_->setObjectName(QStringLiteral("packingWarnings"));
    warnings_->setWordWrap(true);
    warnings_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* split = new QSplitter(Qt::Vertical, page);
    auto* top = new QWidget(split);
    auto* topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->addWidget(new QLabel(tr("<b>Packages</b>"), top));
    topLayout->addWidget(packages_);
    auto* bottom = new QWidget(split);
    auto* bottomLayout = new QVBoxLayout(bottom);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->addWidget(new QLabel(tr("<b>Hardware bags</b>"), bottom));
    bottomLayout->addWidget(bags_);
    split->addWidget(top);
    split->addWidget(bottom);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    auto* layout = new QVBoxLayout(page);
    layout->addLayout(bar);
    layout->addWidget(banner_);
    layout->addWidget(info_);
    layout->addWidget(split, 1);
    layout->addWidget(warnings_);

    for (QDoubleSpinBox* box : {maxWeight_, maxLength_, maxWidth_, maxHeight_, padding_})
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] {
            if (!updating_) refresh();
        });
    connect(csv, &QPushButton::clicked, this, &PackingWorkspace::exportCsv);
    connect(labels, &QPushButton::clicked, this, &PackingWorkspace::exportLabels);
    return page;
}

QWidget* PackingWorkspace::buildAssemblyPage() {
    auto* page = new QWidget(this);
    assemblyNote_ = new QLabel(PackingText::assemblyOrderNote(), page);
    assemblyNote_->setObjectName(QStringLiteral("assemblyOrderNote"));
    assemblyNote_->setWordWrap(true);
    assemblyNote_->setStyleSheet(
        QStringLiteral("QLabel { background: #fff3cd; color: black; border: 1px solid #e0a800; font-weight: bold; padding: 6px; }"));
    assembly_ = new QTextBrowser(page);
    assembly_->setObjectName(QStringLiteral("assemblySheets"));
    auto* exportButton = new QPushButton(tr("Export SVG…"), page);
    auto* bar = new QHBoxLayout;
    bar->addWidget(assemblyNote_, 1);
    bar->addWidget(exportButton, 0, Qt::AlignTop);
    auto* layout = new QVBoxLayout(page);
    layout->addLayout(bar);
    layout->addWidget(assembly_, 1);
    connect(exportButton, &QPushButton::clicked, this, &PackingWorkspace::exportAssemblySvg);
    return page;
}

void PackingWorkspace::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (stale_) refresh();
}

PackingInput PackingWorkspace::input() const {
    PackingInput in;
    in.maxWeightKg = maxWeight_->value();
    in.maxLength = maxLength_->value();
    in.maxWidth = maxWidth_->value();
    in.maxHeight = maxHeight_->value();
    in.padding = padding_->value();
    return in;
}

void PackingWorkspace::setInput(const PackingInput& in) {
    updating_ = true;
    maxWeight_->setValue(in.maxWeightKg);
    maxLength_->setValue(in.maxLength);
    maxWidth_->setValue(in.maxWidth);
    maxHeight_->setValue(in.maxHeight);
    padding_->setValue(in.padding);
    updating_ = false;
    stale_ = true;
    if (isVisible()) refresh();
}

void PackingWorkspace::refresh() {
    stale_ = false;
    list_.reset();
    sheets_.reset();
    rows_ = {};
    banner_->hide();
    warnings_->clear();
    const project::Resolved* resolved = session_.resolved();
    if (!resolved) {
        const QString msg = tr("No packing list: the project could not be generated (%1).")
                                .arg(QString::fromStdString(session_.resolveError()));
        warnings_->setText(msg);
        assembly_->setPlainText(msg);
        fillTables();
        return;
    }
    sheets_ = production::assemblySheets(resolved->model);
    assembly_->setHtml(PackingText::assemblyHtml(*sheets_));
    auto list = projectPackingList(resolved->model, input());
    if (!list) {
        warnings_->setText(tr("No packing list: %1").arg(QString::fromStdString(list.error().message)));
        fillTables();
        return;
    }
    list_ = std::move(list.value());
    rows_ = PackingText::rows(*list_);
    const QString banner = PackingText::unknownWeightBanner(*list_);
    banner_->setText(banner);
    banner_->setVisible(!banner.isEmpty());
    warnings_->setText(rows_.warnings.join(QLatin1Char('\n')));
    fillTables();
}

void PackingWorkspace::fillTables() {
    packages_->clear();
    packages_->setColumnCount(6);
    packages_->setHorizontalHeaderLabels(
        {tr("Assembly"), tr("Package"), tr("Id"), tr("Size (L × W × H)"), tr("Weight"), tr("Parts")});
    packages_->setRowCount(static_cast<int>(rows_.packages.size()));
    for (std::size_t i = 0; i < rows_.packages.size(); ++i) {
        const auto& r = rows_.packages[i];
        const int row = static_cast<int>(i);
        packages_->setItem(row, 0, cell(r.assembly));
        packages_->setItem(row, 1, cell(r.package));
        packages_->setItem(row, 2, cell(r.id));
        auto* size = cell(r.size, r.oversize);
        if (r.oversize) size->setToolTip(tr("A single panel is larger than a size limit"));
        packages_->setItem(row, 3, size);
        auto* weight = cell(r.overweight ? r.weight + tr(" (over limit)") : r.weight, r.weightUnknown || r.overweight);
        if (r.weightUnknown) weight->setToolTip(tr("A material of this package has no density (0 = unknown)"));
        weight->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        packages_->setItem(row, 4, weight);
        packages_->setItem(row, 5, cell(r.parts));
    }
    packages_->resizeColumnsToContents();
    packages_->horizontalHeader()->setStretchLastSection(true);

    bags_->clear();
    bags_->setColumnCount(7);
    bags_->setHorizontalHeaderLabels(
        {tr("Assembly"), tr("Bag"), tr("Group"), tr("Kind"), tr("Name"), tr("Article"), tr("Quantity")});
    bags_->setRowCount(static_cast<int>(rows_.bags.size()));
    for (std::size_t i = 0; i < rows_.bags.size(); ++i) {
        const auto& r = rows_.bags[i];
        const int row = static_cast<int>(i);
        bags_->setItem(row, 0, cell(r.assembly));
        bags_->setItem(row, 1, cell(r.bag));
        bags_->setItem(row, 2, cell(r.group));
        bags_->setItem(row, 3, cell(r.kind));
        bags_->setItem(row, 4, cell(r.name, r.unknownDefinition));
        bags_->setItem(row, 5, cell(r.article));
        auto* qty = cell(QString::number(r.quantity));
        qty->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bags_->setItem(row, 6, qty);
    }
    bags_->resizeColumnsToContents();
}

QString PackingWorkspace::exportBaseName() const {
    const QString name = session_.filePath().empty() ? QString::fromStdString(session_.project().metadata.name)
                                                     : QString::fromStdString(session_.filePath().stem().string());
    return QDir(QDir::homePath()).filePath(name);
}

void PackingWorkspace::exportCsv() {
    if (stale_) refresh();
    if (!list_) {
        QMessageBox::information(this, tr("Export packing list"), warnings_->text());
        return;
    }
    const QString file = QFileDialog::getSaveFileName(this, tr("Export packing list as CSV"),
                                                      exportBaseName() + tr("-packing.csv"), tr("CSV files (*.csv)"));
    if (file.isEmpty()) return;
    auto r = production::writeTextFile(toPath(file), packingCsv(*list_));
    if (!r) QMessageBox::critical(this, tr("Export failed"), QString::fromStdString(r.error().message));
}

void PackingWorkspace::exportLabels() {
    if (stale_) refresh();
    if (!list_ || list_->assemblies.empty()) {
        QMessageBox::information(this, tr("Package labels"), tr("There are no packages to label."));
        return;
    }
    const QString file = QFileDialog::getSaveFileName(this, tr("Export package labels (A4, 70 × 37 mm)"),
                                                      exportBaseName() + tr("-package-labels.svg"), tr("SVG files (*.svg)"));
    if (file.isEmpty()) return;
    auto r = exportPackageLabels(*list_, session_.project().metadata.name, toPath(file));
    if (!r) QMessageBox::critical(this, tr("Export failed"), QString::fromStdString(r.error().message));
}

void PackingWorkspace::exportAssemblySvg() {
    if (stale_) refresh();
    if (!sheets_ || sheets_->sheets.empty()) {
        QMessageBox::information(this, tr("Assembly sheets"), tr("There are no assemblies with parts or hardware."));
        return;
    }
    const QString file = QFileDialog::getSaveFileName(this, tr("Export assembly sheets as SVG"),
                                                      exportBaseName() + tr("-assembly.svg"), tr("SVG files (*.svg)"));
    if (file.isEmpty()) return;
    auto r = exportAssemblySheets(*sheets_, session_.project().metadata.name, toPath(file));
    if (!r) QMessageBox::critical(this, tr("Export failed"), QString::fromStdString(r.error().message));
}

int PackingWorkspace::packageRows() const { return static_cast<int>(rows_.packages.size()); }

bool PackingWorkspace::unknownWeightShown() const { return !banner_->isHidden(); }

} // namespace fos::app::gui
