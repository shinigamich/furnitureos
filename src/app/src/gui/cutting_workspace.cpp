// SPDX-License-Identifier: AGPL-3.0-only
#include "cutting_workspace.hpp"

#include "fos/app/cutting_rows.hpp"
#include "fos/app/session.hpp"
#include "fos/core/units.hpp"
#include "fos/cutting/svg.hpp"
#include "inspector_panel.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSvgRenderer>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fos::app::gui {

SvgView::SvgView(QWidget* parent) : QWidget(parent), renderer_(new QSvgRenderer(this)) {
    setMinimumSize(200, 150);
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, Qt::white);
    setPalette(p);
}

bool SvgView::setSvg(const QByteArray& svg) {
    const bool ok = svg.isEmpty() ? renderer_->load(QByteArray()) : renderer_->load(svg);
    update();
    return ok && renderer_->isValid();
}

bool SvgView::hasDocument() const { return renderer_->isValid(); }

void SvgView::paintEvent(QPaintEvent*) {
    if (!renderer_->isValid()) return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QSizeF doc = renderer_->viewBoxF().size();
    if (doc.isEmpty()) return;
    const QRectF area = QRectF(rect()).adjusted(8, 8, -8, -8);
    const double k = std::min(area.width() / doc.width(), area.height() / doc.height());
    const QSizeF size = doc * k;
    renderer_->render(&painter, QRectF(area.center() - QPointF(size.width() / 2, size.height() / 2), size));
}

namespace {
QDoubleSpinBox* mmSpin(QWidget* parent) {
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(0, 100);
    s->setDecimals(1);
    s->setSingleStep(0.5);
    s->setSuffix(CuttingWorkspace::tr(" mm"));
    return s;
}
QString num(double v, int decimals = 1) { return QString::fromStdString(core::formatLength(v, decimals)); }
} // namespace

CuttingWorkspace::CuttingWorkspace(Session& session, QWidget* parent) : QWidget(parent), session_(session) {
    setObjectName(QStringLiteral("cuttingWorkspace"));
    auto* heuristic = new QLabel(
        tr("<b>Heuristic result.</b> Layouts come from a heuristic guillotine optimiser and are in general not optimal. "
           "Utilisation = placed part area / sheet area. “Lower bound” is the area-based minimum sheet count."),
        this);
    heuristic->setWordWrap(true);

    summary_ = new QTableWidget(this);
    summary_->setColumnCount(7);
    summary_->setHorizontalHeaderLabels({tr("Material"), tr("Pieces"), tr("Sheets"), tr("Lower bound"),
                                         tr("Utilisation (heuristic)"), tr("Unplaced"), tr("Layout check")});
    summary_->verticalHeader()->hide();
    summary_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    summary_->setSelectionBehavior(QAbstractItemView::SelectRows);
    summary_->setSelectionMode(QAbstractItemView::SingleSelection);
    summary_->horizontalHeader()->setStretchLastSection(true);

    sheet_ = new QComboBox(this);
    sheetInfo_ = new QLabel(this);
    sheetInfo_->setWordWrap(true);
    view_ = new SvgView(this);
    view_->setObjectName(QStringLiteral("cuttingSheetView"));

    // Settings: project defaults or one material; kerf and edge trim.
    scope_ = new QComboBox(this);
    kerf_ = mmSpin(this);
    trimLeft_ = mmSpin(this);
    trimRight_ = mmSpin(this);
    trimTop_ = mmSpin(this);
    trimBottom_ = mmSpin(this);
    error_ = makeErrorLabel(this);
    auto* apply = new QPushButton(tr("Apply and re-optimise"), this);
    auto* settings = new QGroupBox(tr("Saw settings"), this);
    auto* form = new QFormLayout(settings);
    form->addRow(tr("Applies to:"), scope_);
    form->addRow(tr("Kerf (blade width):"), kerf_);
    form->addRow(tr("Trim left:"), trimLeft_);
    form->addRow(tr("Trim right:"), trimRight_);
    form->addRow(tr("Trim top:"), trimTop_);
    form->addRow(tr("Trim bottom:"), trimBottom_);
    form->addRow(apply);
    form->addRow(error_);

    // Summary across the full width; below it the saw settings and the selected sheet.
    summary_->setMaximumHeight(170);
    settings->setMaximumWidth(380);
    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    auto* sheetBar = new QHBoxLayout;
    sheetBar->addWidget(new QLabel(tr("Sheet:"), right));
    sheetBar->addWidget(sheet_);
    sheetBar->addStretch(1);
    rightLayout->addLayout(sheetBar);
    rightLayout->addWidget(sheetInfo_);
    rightLayout->addWidget(view_, 1);
    auto* lower = new QHBoxLayout;
    auto* settingsColumn = new QVBoxLayout;
    settingsColumn->addWidget(settings);
    settingsColumn->addStretch(1);
    lower->addLayout(settingsColumn);
    lower->addWidget(right, 1);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(heuristic);
    layout->addWidget(summary_);
    layout->addLayout(lower, 1);

    connect(&session_, &Session::projectChanged, this, [this] {
        stale_ = true;
        if (isVisible()) refresh();
    });
    connect(summary_, &QTableWidget::itemSelectionChanged, this, &CuttingWorkspace::showPlan);
    connect(sheet_, qOverload<int>(&QComboBox::currentIndexChanged), this, &CuttingWorkspace::showSheet);
    connect(scope_, qOverload<int>(&QComboBox::activated), this, &CuttingWorkspace::loadSettings);
    connect(apply, &QPushButton::clicked, this, &CuttingWorkspace::applySettings);
}

void CuttingWorkspace::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (stale_) refresh();
}

int CuttingWorkspace::materialCount() const { return summary_->rowCount(); }

void CuttingWorkspace::refresh() {
    stale_ = false;
    const int keep = std::max(0, summary_->currentRow());
    const auto rows = cuttingSummary(session_.cuttingPlans());
    summary_->setRowCount(static_cast<int>(rows.size()));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const int row = static_cast<int>(i);
        auto set = [&](int col, const QString& text) { summary_->setItem(row, col, new QTableWidgetItem(text)); };
        set(0, tr("%1 (%2 mm)").arg(QString::fromStdString(r.materialName), num(r.thickness)));
        set(1, QString::number(r.pieces));
        if (!r.optimised) {
            set(2, tr("failed"));
            for (int c = 3; c < 7; ++c) set(c, QStringLiteral("—"));
            continue;
        }
        set(2, QString::number(r.sheets));
        set(3, r.sheetCountProvenMinimal ? tr("%1 (count is minimal)").arg(r.sheetLowerBound)
                                         : QString::number(r.sheetLowerBound));
        set(4, QStringLiteral("%1 %").arg(num(r.utilisationPct)));
        set(5, QString::number(r.unplaced));
        set(6, r.layoutValid ? tr("valid") : tr("ERRORS"));
    }
    summary_->resizeColumnsToContents();

    // Settings scope: defaults + materials of the library.
    const QString scope = scope_->currentData().toString();
    scope_->clear();
    scope_->addItem(tr("Project defaults"), QString());
    for (const auto& m : session_.project().libraries.materials)
        scope_->addItem(QString::fromStdString(m.name), QString::fromStdString(m.id.str()));
    scope_->setCurrentIndex(std::max(0, scope_->findData(scope)));
    loadSettings();
    if (!rows.empty()) summary_->selectRow(std::min(keep, static_cast<int>(rows.size()) - 1));
    else showPlan();
}

void CuttingWorkspace::showPlan() {
    const auto& plans = session_.cuttingPlans().plans;
    const int row = summary_->currentRow();
    sheet_->blockSignals(true);
    sheet_->clear();
    if (row >= 0 && row < static_cast<int>(plans.size()) && plans[static_cast<std::size_t>(row)].result) {
        const auto& sheets = plans[static_cast<std::size_t>(row)].result->sheets;
        for (std::size_t i = 0; i < sheets.size(); ++i)
            sheet_->addItem(tr("%1 of %2").arg(i + 1).arg(sheets.size()));
    }
    sheet_->blockSignals(false);
    showSheet();
}

void CuttingWorkspace::showSheet() {
    const auto& plans = session_.cuttingPlans().plans;
    const int row = summary_->currentRow();
    const int index = sheet_->currentIndex();
    if (row < 0 || row >= static_cast<int>(plans.size()) || index < 0) {
        view_->setSvg({});
        sheetInfo_->setText(plans.empty() ? tr("No parts to cut.") : QString());
        return;
    }
    const auto& plan = plans[static_cast<std::size_t>(row)];
    const auto& sheet = plan.result->sheets[static_cast<std::size_t>(index)];
    view_->setSvg(QByteArray::fromStdString(cutting::renderSheetSvg(plan.job, sheet)));
    sheetInfo_->setText(tr("Sheet %1 × %2 mm, %3 parts; utilisation %4 % of the sheet (%5 % of the usable area); "
                           "cut length %6 m.")
                            .arg(num(sheet.sheetLength), num(sheet.sheetWidth))
                            .arg(sheet.placements.size())
                            .arg(num(sheet.utilisationOfSheetPct), num(sheet.utilisationOfUsablePct),
                                 num(sheet.cutLength / 1000.0, 2)));
}

void CuttingWorkspace::loadSettings() {
    const std::string id = scope_->currentData().toString().toStdString();
    const auto& c = session_.project().cutting;
    const project::CuttingSettings& s = id.empty() ? c.defaults : c.forMaterial(id);
    kerf_->setValue(s.kerf);
    trimLeft_->setValue(s.trim.left);
    trimRight_->setValue(s.trim.right);
    trimTop_->setValue(s.trim.top);
    trimBottom_->setValue(s.trim.bottom);
    showError(error_, {});
}

void CuttingWorkspace::applySettings() {
    const std::string id = scope_->currentData().toString().toStdString();
    const std::optional<std::string> material = id.empty() ? std::nullopt : std::optional<std::string>(id);
    cutting::Trim trim{trimLeft_->value(), trimRight_->value(), trimTop_->value(), trimBottom_->value()};
    auto r = session_.execute(cuttingSettingsCommand(session_.project(), material, kerf_->value(), trim));
    if (!r) showError(error_, QString::fromStdString(r.error().message));
}

} // namespace fos::app::gui
