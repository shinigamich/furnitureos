// SPDX-License-Identifier: AGPL-3.0-only
#include "cutting_workspace.hpp"

#include "enum_text.hpp"
#include "form_widgets.hpp"
#include "fos/app/cutting_rows.hpp"
#include "fos/app/session.hpp"
#include "fos/app/workshop_offcuts.hpp"
#include "fos/core/units.hpp"
#include "fos/domain/model.hpp"
#include "fos/cutting/svg.hpp"
#include "fos/nesting/svg.hpp"
#include "inspector_panel.hpp"
#include "nesting_settings_form.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyle>
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
/// Sheet selector item data: plan kind and sheet index.
constexpr int kSheetKindRole = Qt::UserRole;
constexpr int kSheetIndexRole = Qt::UserRole + 1;
} // namespace

CuttingWorkspace::CuttingWorkspace(Session& session, QWidget* parent) : QWidget(parent), session_(session) {
    setObjectName(QStringLiteral("cuttingWorkspace"));
    auto* heuristic = new QLabel(
        tr("<b>Heuristic results.</b> Saw layouts come from a heuristic guillotine optimiser, router layouts of "
           "shaped parts from a heuristic true-shape nesting; both are in general not optimal. "
           "Utilisation = placed part area / sheet area. “Lower bound” is the area-based minimum sheet count."),
        this);
    heuristic->setWordWrap(true);

    summary_ = new QTableWidget(this);
    summary_->setObjectName(QStringLiteral("cuttingSummary"));
    summary_->setColumnCount(8);
    summary_->setHorizontalHeaderLabels({tr("Material"), tr("Method"), tr("Pieces"), tr("Sheets"), tr("Lower bound"),
                                         tr("Utilisation (heuristic)"), tr("Unplaced"), tr("Layout check")});
    summary_->verticalHeader()->hide();
    summary_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    summary_->setSelectionBehavior(QAbstractItemView::SelectRows);
    summary_->setSelectionMode(QAbstractItemView::SingleSelection);
    summary_->horizontalHeader()->setStretchLastSection(true);

    routed_ = new QLabel(this);
    routed_->setObjectName(QStringLiteral("routedParts"));
    routed_->setWordWrap(true);
    routed_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    routed_->hide();

    sheet_ = new QComboBox(this);
    sheet_->setObjectName(QStringLiteral("cuttingSheet"));
    sheet_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    sheetInfo_ = new QLabel(this);
    sheetInfo_->setWordWrap(true);
    view_ = new SvgView(this);
    view_->setObjectName(QStringLiteral("cuttingSheetView"));

    // Settings: project defaults or one material; kerf and edge trim.
    scope_ = narrowCombo(this, 18);
    kerf_ = mmSpin(this);
    trimLeft_ = mmSpin(this);
    trimRight_ = mmSpin(this);
    trimTop_ = mmSpin(this);
    trimBottom_ = mmSpin(this);
    objective_ = choiceBox(objectiveChoices(), cutting::Objective::MinimizeSheets, this);
    objective_->setObjectName(QStringLiteral("cuttingObjective"));
    objective_->setToolTip(tr("What the heuristic optimiser compares candidate layouts by."));
    remnants_ = new QTableWidget(0, 4, this);
    remnants_->setObjectName(QStringLiteral("remnantTable"));
    remnants_->setHorizontalHeaderLabels({tr("Label"), tr("Length"), tr("Width"), tr("Qty")});
    remnants_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    remnants_->setSelectionBehavior(QAbstractItemView::SelectRows);
    remnants_->setMinimumHeight(110);
    remnants_->setMaximumHeight(150);
    remnants_->setToolTip(tr("Leftover boards of this material, used before full sheets. Length along the grain, mm."));
    addRemnant_ = new QPushButton(tr("Add remnant"), this);
    addRemnant_->setObjectName(QStringLiteral("addRemnant"));
    removeRemnant_ = new QPushButton(tr("Remove remnant"), this);
    removeRemnant_->setObjectName(QStringLiteral("removeRemnant"));
    remnantHint_ = new QLabel(this);
    remnantHint_->setWordWrap(true);
    remnantHint_->setStyleSheet(QStringLiteral("color: palette(mid);"));
    useDefaults_ = new QPushButton(tr("Use project defaults for this material"), this);
    useDefaults_->setObjectName(QStringLiteral("useDefaultCutting"));
    error_ = makeErrorLabel(this);
    error_->setObjectName(QStringLiteral("cuttingError"));
    auto* apply = new QPushButton(tr("Apply and re-optimise"), this);
    apply->setObjectName(QStringLiteral("applyCutting"));
    auto* settings = new QGroupBox(tr("Saw settings"), this);
    auto* form = new QFormLayout(settings);
    form->addRow(tr("Applies to:"), scope_);
    form->addRow(tr("Kerf (blade width):"), kerf_);
    form->addRow(tr("Trim left:"), trimLeft_);
    form->addRow(tr("Trim right:"), trimRight_);
    form->addRow(tr("Trim top:"), trimTop_);
    form->addRow(tr("Trim bottom:"), trimBottom_);
    form->addRow(tr("Objective:"), objective_);
    form->addRow(new QLabel(tr("Remnants (extra stock):"), settings));
    form->addRow(remnants_);
    auto* remnantButtons = new QHBoxLayout;
    remnantButtons->addWidget(addRemnant_);
    remnantButtons->addWidget(removeRemnant_);
    form->addRow(remnantButtons);
    form->addRow(remnantHint_);
    form->addRow(apply);
    form->addRow(useDefaults_);
    form->addRow(error_);

    nesting_ = new NestingSettingsForm(session_, this);
    auto* allSettings = new QWidget(this);
    auto* allSettingsLayout = new QVBoxLayout(allSettings);
    allSettingsLayout->setContentsMargins(0, 0, 0, 0);
    allSettingsLayout->addWidget(settings);
    allSettingsLayout->addWidget(nesting_);
    buildWorkshopBox(allSettingsLayout);
    allSettingsLayout->addStretch(1);

    // Summary across the full width; below it the saw / nesting settings and the selected sheet.
    auto* settingsScroll = new QScrollArea(this);
    settingsScroll->setWidget(allSettings);
    settingsScroll->setWidgetResizable(true);
    settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    settingsScroll->setFrameShape(QFrame::NoFrame);
    // Wide enough for the (translated) form without a horizontal scroll bar.
    settingsScroll->setFixedWidth(std::max(390, allSettings->sizeHint().width() +
                                                    style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 6));
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
    lower->addWidget(settingsScroll);
    lower->addWidget(right, 1);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(heuristic);
    layout->addWidget(summary_);
    layout->addWidget(routed_);
    layout->addLayout(lower, 1);

    connect(&session_, &Session::projectChanged, this, [this] {
        stale_ = true;
        if (isVisible()) refresh();
    });
    connect(summary_, &QTableWidget::itemSelectionChanged, this, &CuttingWorkspace::showPlan);
    connect(sheet_, qOverload<int>(&QComboBox::currentIndexChanged), this, &CuttingWorkspace::showSheet);
    connect(scope_, qOverload<int>(&QComboBox::activated), this, &CuttingWorkspace::loadSettings);
    connect(apply, &QPushButton::clicked, this, &CuttingWorkspace::applySettings);
    connect(addRemnant_, &QPushButton::clicked, this, &CuttingWorkspace::addRemnant);
    connect(removeRemnant_, &QPushButton::clicked, this, &CuttingWorkspace::removeRemnant);
    connect(useDefaults_, &QPushButton::clicked, this, [this] {
        const std::string id = scope_->currentData().toString().toStdString();
        if (id.empty()) return;
        auto r = session_.execute(useDefaultCuttingSettingsCommand(id));
        if (!r) showError(error_, QString::fromStdString(r.error().message));
    });
}

void CuttingWorkspace::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (stale_) refresh();
}

int CuttingWorkspace::materialCount() const { return summary_->rowCount(); }

void CuttingWorkspace::refresh() {
    stale_ = false;
    workshopRun_.reset();
    if (useWorkshop_->isChecked() && workshopStore_ && session_.resolved())
        workshopRun_ = workshopCuttingRun(*session_.resolved(), session_.project(), *workshopStore_);
    showWorkshopRun();
    const int keep = std::max(0, summary_->currentRow());
    const auto& plans = shownPlans();
    const auto saw = cuttingSummary(plans);
    const auto nest = nestingSummary(plans);
    rows_ = cuttingTableRows(plans);
    summary_->setRowCount(static_cast<int>(rows_.size()));
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const int row = static_cast<int>(i);
        auto set = [&](int col, const QString& text) { summary_->setItem(row, col, new QTableWidgetItem(text)); };
        if (rows_[i].kind == PlanKind::Nesting) {
            const auto& r = nest[rows_[i].planIndex];
            set(0, tr("%1 (%2 mm)").arg(QString::fromStdString(r.materialName), num(r.thickness)));
            set(1, tr("Router nesting (heuristic)"));
            set(2, QString::number(r.parts));
            if (!r.nested) {
                set(3, tr("failed"));
                for (int c = 4; c < 8; ++c) set(c, QStringLiteral("—"));
                continue;
            }
            set(3, QString::number(r.sheets));
            set(4, QStringLiteral("—")); // the nesting reports no lower bound
            set(5, QStringLiteral("%1 %").arg(num(r.utilisationPct)));
            set(6, QString::number(r.unplaced));
            set(7, r.problems == 0 ? tr("valid") : tr("problems: %1").arg(r.problems));
            continue;
        }
        const auto& r = saw[rows_[i].planIndex];
        set(0, tr("%1 (%2 mm)").arg(QString::fromStdString(r.materialName), num(r.thickness)));
        set(1, tr("Saw (guillotine)"));
        set(2, QString::number(r.pieces));
        if (!r.optimised) {
            set(3, tr("failed"));
            for (int c = 4; c < 8; ++c) set(c, QStringLiteral("—"));
            continue;
        }
        set(3, QString::number(r.sheets));
        set(4, r.sheetCountProvenMinimal ? tr("%1 (count is minimal)").arg(r.sheetLowerBound)
                                         : QString::number(r.sheetLowerBound));
        set(5, QStringLiteral("%1 %").arg(num(r.utilisationPct)));
        set(6, QString::number(r.unplaced));
        set(7, r.layoutValid ? tr("valid") : tr("ERRORS"));
    }
    summary_->resizeColumnsToContents();
    // As tall as its rows (no empty block below two materials), at most 190 px.
    // Room for a horizontal scroll bar too (long translated column titles).
    int tableHeight = summary_->horizontalHeader()->height() + 2 * summary_->frameWidth() + 2 +
                      style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    for (int r = 0; r < summary_->rowCount(); ++r) tableHeight += summary_->rowHeight(r);
    summary_->setFixedHeight(std::min(std::max(tableHeight, 60), 190));

    // Shaped parts whose outline is not a plain rectangle are not in the saw jobs
    // (cutting::cuttingFromModel uses the same domain::isRectangularOutline rule).
    QStringList routed;
    if (const project::Resolved* resolved = session_.resolved()) {
        const auto& model = resolved->model;
        for (const auto& assembly : model.assemblies)
            for (const auto& part : assembly.parts) {
                if (domain::isRectangularOutline(part)) continue;
                const auto blank = domain::cutSize(part, model);
                const domain::Material* m = model.findMaterial(part.material);
                routed << tr("%1 (%2; blank %3 × %4 mm)")
                              .arg(QString::fromStdString(part.name.empty() ? part.id.str() : part.name),
                                   m ? QString::fromStdString(m->name) : QString::fromStdString(part.material.str()),
                                   num(blank.length), num(blank.width));
            }
    }
    routedCount_ = static_cast<int>(routed.size());
    routed_->setText(tr("<b>Routed parts</b> (shaped outlines are cut on the CNC router, not on the saw; their "
                        "layouts are the “Router nesting” rows): %1")
                         .arg(routed.join(QStringLiteral("; ")).toHtmlEscaped()));
    routed_->setVisible(!routed.isEmpty());

    // Settings scope: defaults + materials of the library.
    const QString scope = scope_->currentData().toString();
    scope_->clear();
    scope_->addItem(tr("Project defaults"), QString());
    for (const auto& m : session_.project().libraries.materials)
        scope_->addItem(QString::fromStdString(m.name), QString::fromStdString(m.id.str()));
    scope_->setCurrentIndex(std::max(0, scope_->findData(scope)));
    loadSettings();
    nesting_->load();
    if (!rows_.empty()) summary_->selectRow(std::min(keep, static_cast<int>(rows_.size()) - 1));
    else showPlan();
}

void CuttingWorkspace::showPlan() {
    // All sheets of the selected row's material: saw sheets, then nest sheets; the first sheet
    // of the selected row's method is shown.
    const auto& plans = shownPlans();
    const int row = summary_->currentRow();
    sheet_->blockSignals(true);
    sheet_->clear();
    if (row >= 0 && row < static_cast<int>(rows_.size())) {
        const CuttingTableRow& selected = rows_[static_cast<std::size_t>(row)];
        auto add = [&](PlanKind kind, const QString& text, std::size_t index) {
            sheet_->addItem(text);
            sheet_->setItemData(sheet_->count() - 1, static_cast<int>(kind), kSheetKindRole);
            sheet_->setItemData(sheet_->count() - 1, static_cast<int>(index), kSheetIndexRole);
        };
        if (const auto* g = plans.guillotineFor(selected.materialId); g && g->result)
            for (std::size_t i = 0; i < g->result->sheets.size(); ++i)
                add(PlanKind::Guillotine, tr("Saw sheet %1 of %2").arg(i + 1).arg(g->result->sheets.size()), i);
        if (const auto* n = plans.nestingFor(selected.materialId); n && n->result)
            for (std::size_t i = 0; i < n->result->sheets.size(); ++i)
                add(PlanKind::Nesting, tr("Nest sheet %1 of %2").arg(i + 1).arg(n->result->sheets.size()), i);
        sheet_->setCurrentIndex(std::max(0, sheet_->findData(static_cast<int>(selected.kind), kSheetKindRole)));
    }
    sheet_->blockSignals(false);
    showSheet();
}

std::optional<PlanKind> CuttingWorkspace::shownSheetKind() const {
    if (sheet_->currentIndex() < 0) return std::nullopt;
    return static_cast<PlanKind>(sheet_->currentData(kSheetKindRole).toInt());
}

void CuttingWorkspace::showSheet() {
    const auto& plans = shownPlans();
    const int row = summary_->currentRow();
    const auto kind = shownSheetKind();
    if (row < 0 || row >= static_cast<int>(rows_.size()) || !kind) {
        view_->setSvg({});
        sheetInfo_->setText(rows_.empty() ? tr("No parts to cut.") : QString());
        return;
    }
    const std::string& material = rows_[static_cast<std::size_t>(row)].materialId;
    const auto index = static_cast<std::size_t>(sheet_->currentData(kSheetIndexRole).toInt());
    if (*kind == PlanKind::Nesting) {
        const project::NestingPlan& plan = *plans.nestingFor(material);
        const nesting::SheetLayout& sheet = plan.result->sheets[index];
        view_->setSvg(QByteArray::fromStdString(nesting::renderNestSvg(plan.job.job, sheet)));
        double length = 0, width = 0;
        for (const auto& stock : plan.job.job.sheets)
            if (stock.id == sheet.sheetId) {
                length = stock.length;
                width = stock.width;
            }
        sheetInfo_->setText(tr("Nest sheet %1 × %2 mm, %3 parts; utilisation %4 % of the sheet (heuristic); "
                               "used length %5 mm.")
                                .arg(num(length), num(width))
                                .arg(sheet.placements.size())
                                .arg(num(sheet.utilisation * 100.0), num(sheet.usedLength)));
        return;
    }
    const project::CuttingPlan& plan = *plans.guillotineFor(material);
    const auto& sheet = plan.result->sheets[index];
    view_->setSvg(QByteArray::fromStdString(cutting::renderSheetSvg(plan.job, sheet)));
    sheetInfo_->setText(tr("Sheet %1 × %2 mm, %3 parts; utilisation %4 % of the sheet (%5 % of the usable area); "
                           "cut length %6 m.")
                            .arg(num(sheet.sheetLength), num(sheet.sheetWidth))
                            .arg(sheet.placements.size())
                            .arg(num(sheet.utilisationOfSheetPct), num(sheet.utilisationOfUsablePct),
                                 num(sheet.cutLength / 1000.0, 2)));
}

namespace {
/// Remnant table columns: label, length, width, quantity; the id is kept in the label's data.
constexpr int kRemnantIdRole = Qt::UserRole;

QDoubleSpinBox* remnantSpin(QWidget* parent, double value) {
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(0, 10000);
    s->setDecimals(1);
    s->setValue(value);
    s->setFrame(false);
    return s;
}
} // namespace

void CuttingWorkspace::loadSettings() {
    const std::string id = scope_->currentData().toString().toStdString();
    const std::optional<std::string> material = id.empty() ? std::nullopt : std::optional<std::string>(id);
    const CuttingSettingsInput s = cuttingSettingsInput(session_.project(), material);
    kerf_->setValue(s.kerf);
    trimLeft_->setValue(s.trim.left);
    trimRight_->setValue(s.trim.right);
    trimTop_->setValue(s.trim.top);
    trimBottom_->setValue(s.trim.bottom);
    selectData(objective_, static_cast<int>(s.objective));
    remnants_->setRowCount(0);
    for (const auto& r : s.remnants) {
        const int row = remnants_->rowCount();
        remnants_->insertRow(row);
        auto* label = new QTableWidgetItem(QString::fromStdString(r.label));
        label->setData(kRemnantIdRole, QString::fromStdString(r.id));
        label->setToolTip(tr("Remnant id %1").arg(QString::fromStdString(r.id)));
        remnants_->setItem(row, 0, label);
        remnants_->setCellWidget(row, 1, remnantSpin(remnants_, r.length));
        remnants_->setCellWidget(row, 2, remnantSpin(remnants_, r.width));
        auto* qty = new QSpinBox(remnants_);
        qty->setRange(0, 999);
        qty->setValue(r.quantity);
        qty->setFrame(false);
        remnants_->setCellWidget(row, 3, qty);
    }
    remnants_->resizeColumnToContents(1);
    remnants_->resizeColumnToContents(2);
    // Remnants are boards of one material: edited per material, not in the project defaults.
    const bool perMaterial = material.has_value();
    remnants_->setEnabled(perMaterial);
    addRemnant_->setEnabled(perMaterial);
    removeRemnant_->setEnabled(perMaterial && remnants_->rowCount() > 0);
    remnantHint_->setText(perMaterial ? tr("Changes take effect with “Apply and re-optimise”.")
                                      : tr("Choose a material under “Applies to” to edit its remnants."));
    useDefaults_->setVisible(perMaterial);
    useDefaults_->setEnabled(perMaterial && hasOwnCuttingSettings(session_.project(), id));
    showError(error_, {});
}

void CuttingWorkspace::addRemnant() {
    std::vector<project::Remnant> existing;
    for (int row = 0; row < remnants_->rowCount(); ++row)
        existing.push_back({remnants_->item(row, 0)->data(kRemnantIdRole).toString().toStdString(), {}, 0, 0, 1});
    const int row = remnants_->rowCount();
    remnants_->insertRow(row);
    auto* label = new QTableWidgetItem(tr("Remnant"));
    label->setData(kRemnantIdRole, QString::fromStdString(nextRemnantId(existing)));
    remnants_->setItem(row, 0, label);
    remnants_->setCellWidget(row, 1, remnantSpin(remnants_, 1000));
    remnants_->setCellWidget(row, 2, remnantSpin(remnants_, 500));
    auto* qty = new QSpinBox(remnants_);
    qty->setRange(0, 999);
    qty->setValue(1);
    qty->setFrame(false);
    remnants_->setCellWidget(row, 3, qty);
    remnants_->setCurrentCell(row, 0);
    removeRemnant_->setEnabled(true);
}

void CuttingWorkspace::removeRemnant() {
    const int row = remnants_->currentRow();
    if (row >= 0) remnants_->removeRow(row);
    removeRemnant_->setEnabled(remnants_->rowCount() > 0);
}

void CuttingWorkspace::applySettings() {
    const std::string id = scope_->currentData().toString().toStdString();
    const std::optional<std::string> material = id.empty() ? std::nullopt : std::optional<std::string>(id);
    CuttingSettingsInput in = cuttingSettingsInput(session_.project(), material);
    in.kerf = kerf_->value();
    in.trim = cutting::Trim{trimLeft_->value(), trimRight_->value(), trimTop_->value(), trimBottom_->value()};
    in.objective = valueOf<cutting::Objective>(objective_);
    if (material) {
        in.remnants.clear();
        for (int row = 0; row < remnants_->rowCount(); ++row) {
            project::Remnant r;
            r.id = remnants_->item(row, 0)->data(kRemnantIdRole).toString().toStdString();
            r.label = remnants_->item(row, 0)->text().toStdString();
            r.length = static_cast<QDoubleSpinBox*>(remnants_->cellWidget(row, 1))->value();
            r.width = static_cast<QDoubleSpinBox*>(remnants_->cellWidget(row, 2))->value();
            r.quantity = static_cast<QSpinBox*>(remnants_->cellWidget(row, 3))->value();
            in.remnants.push_back(std::move(r));
        }
    }
    auto r = session_.execute(cuttingSettingsCommand(session_.project(), material, in));
    if (!r) showError(error_, QString::fromStdString(r.error().message));
}

} // namespace fos::app::gui
