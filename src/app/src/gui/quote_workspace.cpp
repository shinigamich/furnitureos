// SPDX-License-Identifier: AGPL-3.0-only
#include "quote_workspace.hpp"

#include "fos/app/session.hpp"
#include "fos/production/csv.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fos::app::gui {
namespace {

QDoubleSpinBox* percentBox(double value, QWidget* parent) {
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(0.0, 1000.0); // production::checkPricingSettings limit
    box->setDecimals(2);
    box->setSuffix(QStringLiteral(" %"));
    box->setValue(value);
    return box;
}

} // namespace

// ------------------------------------------------------------------ PricingSettingsDialog

PricingSettingsDialog::PricingSettingsDialog(const PricingInput& current, const QString& currency, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("pricingSettingsDialog"));
    setWindowTitle(tr("Pricing settings"));
    auto* form = new QFormLayout;
    form->addRow(tr("Currency:"), new QLabel(currency.isEmpty() ? tr("(none)") : currency, this));
    waste_ = percentBox(current.wastePercent, this);
    waste_->setToolTip(tr("Extra board added to the cut area of materials priced by area (not used with cutting plan sheets)"));
    markup_ = percentBox(current.markupPercent, this);
    tax_ = percentBox(current.taxPercent, this);
    form->addRow(tr("Waste:"), waste_);
    form->addRow(tr("Markup:"), markup_);
    form->addRow(tr("Tax:"), tax_);

    const std::array<std::optional<double>, 4> rates{current.labour.perCutMetre, current.labour.perEdgeMetre,
                                                      current.labour.perHole, current.labour.perPart};
    const std::array<QString, 4> names{tr("Labour per cut metre"), tr("Labour per edge banding metre"),
                                       tr("Labour per drilled hole"), tr("Labour per part")};
    const QString suffix = currency.isEmpty() ? QString() : QStringLiteral(" ") + currency;
    for (std::size_t i = 0; i < 4; ++i) {
        rateSet_[i] = new QCheckBox(names[i], this);
        rateSet_[i]->setChecked(rates[i].has_value());
        rate_[i] = new QDoubleSpinBox(this);
        rate_[i]->setRange(0.0, 1e9);
        rate_[i]->setDecimals(2);
        rate_[i]->setSuffix(suffix);
        rate_[i]->setValue(rates[i].value_or(0.0));
        rate_[i]->setEnabled(rates[i].has_value());
        connect(rateSet_[i], &QCheckBox::toggled, rate_[i], &QWidget::setEnabled);
        form->addRow(rateSet_[i], rate_[i]);
    }
    auto* note = new QLabel(tr("Unchecked labour rates produce no labour line. Material, edge band and hardware "
                               "prices come from the project libraries."),
                            this);
    note->setWordWrap(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
}

PricingInput PricingSettingsDialog::input() const {
    PricingInput in;
    in.wastePercent = waste_->value();
    in.markupPercent = markup_->value();
    in.taxPercent = tax_->value();
    auto rate = [this](std::size_t i) -> std::optional<double> {
        return rateSet_[i]->isChecked() ? std::optional<double>(rate_[i]->value()) : std::nullopt;
    };
    in.labour.perCutMetre = rate(0);
    in.labour.perEdgeMetre = rate(1);
    in.labour.perHole = rate(2);
    in.labour.perPart = rate(3);
    return in;
}

// ------------------------------------------------------------------ QuoteWorkspace

QuoteWorkspace::QuoteWorkspace(Session& session, QWidget* parent) : QWidget(parent), session_(session) {
    setObjectName(QStringLiteral("quoteWorkspace"));
    useSheets_ = new QCheckBox(tr("Use cutting plan sheets"), this);
    useSheets_->setObjectName(QStringLiteral("quoteUseSheets"));
    useSheets_->setToolTip(tr("Price materials per sheet from the cutting plans of the Cutting workspace (heuristic "
                              "optimiser) instead of by cut area plus waste; also gives the cut length for labour"));
    auto* settings = new QPushButton(tr("Pricing settings…"), this);
    auto* exportButton = new QPushButton(tr("Export CSV…"), this);
    banner_ = new QLabel(this);
    banner_->setObjectName(QStringLiteral("quoteIncompleteBanner"));
    banner_->setWordWrap(true);
    banner_->setStyleSheet(QStringLiteral("QLabel { background: #c62828; color: white; font-weight: bold; padding: 6px; }"));
    banner_->hide();
    info_ = new QLabel(this);
    info_->setWordWrap(true);
    view_ = new QTableWidget(this);
    view_->setObjectName(QStringLiteral("quoteTable"));
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->verticalHeader()->hide();
    view_->setSortingEnabled(false);

    auto* bar = new QHBoxLayout;
    bar->addWidget(useSheets_);
    bar->addStretch(1);
    bar->addWidget(settings);
    bar->addWidget(exportButton);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addWidget(banner_);
    layout->addWidget(info_);
    layout->addWidget(view_, 1);

    connect(useSheets_, &QCheckBox::toggled, this, [this] { refresh(); });
    connect(settings, &QPushButton::clicked, this, &QuoteWorkspace::editPricingSettings);
    connect(exportButton, &QPushButton::clicked, this, &QuoteWorkspace::exportCsv);
    connect(&session_, &Session::projectChanged, this, [this] {
        stale_ = true;
        if (isVisible()) refresh();
    });
}

void QuoteWorkspace::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (stale_) refresh();
}

void QuoteWorkspace::setUseCuttingPlans(bool on) {
    if (useSheets_->isChecked() == on) refresh();
    else useSheets_->setChecked(on); // toggled() refreshes
}

void QuoteWorkspace::refresh() {
    stale_ = false;
    quote_.reset();
    rows_ = {};
    view_->clear();
    view_->setRowCount(0);
    banner_->hide();
    const project::Resolved* resolved = session_.resolved();
    if (!resolved) {
        info_->setText(tr("No quote: the project could not be generated (%1).")
                           .arg(QString::fromStdString(session_.resolveError())));
        return;
    }
    const project::CuttingPlans* plans = useSheets_->isChecked() ? &session_.cuttingPlans() : nullptr;
    auto q = projectQuote(session_.project(), resolved->model, plans);
    if (!q) {
        info_->setText(tr("No quote: %1").arg(QString::fromStdString(q.error().message)));
        return;
    }
    quote_ = std::move(q.value());
    rows_ = QuoteText::rows(*quote_);

    QStringList info;
    info << (quote_->bySheets ? tr("Materials priced by sheets from the cutting plans (heuristic).")
                              : tr("Materials priced by cut area plus waste."));
    info << tr("Currency %1 (label only, no conversion).").arg(QString::fromStdString(quote_->currency));
    if (!session_.project().settings.pricing) info << tr("Pricing settings are not configured (no markup, tax or labour).");
    for (const auto& d : quote_->diagnostics)
        if (d.severity != core::Severity::Info && d.code != "production.quote.unpriced")
            info << QString::fromStdString(d.message);
    info_->setText(info.join(QLatin1Char('\n')));
    const QString banner = QuoteText::incompleteBanner(*quote_);
    banner_->setText(banner);
    banner_->setVisible(!banner.isEmpty());

    const auto& t = rows_.table;
    view_->setColumnCount(static_cast<int>(t.header.size()));
    view_->setRowCount(static_cast<int>(t.rows.size()));
    QStringList header;
    for (const auto& h : t.header) header << QString::fromStdString(h);
    view_->setHorizontalHeaderLabels(header);
    const QColor unpricedColor(255, 205, 210);
    for (std::size_t r = 0; r < t.rows.size(); ++r) {
        const bool unpriced = rows_.unpriced[r];
        const bool summary = r >= rows_.summaryStart;
        const bool total = r + 1 == t.rows.size();
        for (std::size_t c = 0; c < t.rows[r].size(); ++c) {
            auto* item = new QTableWidgetItem(QString::fromStdString(t.rows[r][c]));
            if (c == 2 || c == 4 || c == 5) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (unpriced) {
                item->setBackground(unpricedColor);
                item->setForeground(Qt::black);
            }
            if (total || (summary && c == 1 && r == rows_.summaryStart + 4)) {
                QFont f = item->font();
                f.setBold(true);
                item->setFont(f);
            }
            view_->setItem(static_cast<int>(r), static_cast<int>(c), item);
        }
    }
    view_->resizeColumnsToContents();
}

void QuoteWorkspace::editPricingSettings() {
    PricingSettingsDialog dialog(pricingInput(session_.project()), QString::fromStdString(session_.project().metadata.currency),
                                 this);
    if (dialog.exec() != QDialog::Accepted) return;
    const PricingInput in = dialog.input();
    if (session_.project().settings.pricing && in == pricingInput(session_.project())) return; // nothing changed
    if (auto r = session_.execute(pricingSettingsCommand(in)); !r)
        QMessageBox::warning(this, tr("Pricing settings"), QString::fromStdString(r.error().message));
}

void QuoteWorkspace::exportCsv() {
    if (stale_) refresh();
    if (!quote_) {
        QMessageBox::information(this, tr("Export quote"), info_->text());
        return;
    }
    const QString file = QFileDialog::getSaveFileName(this, tr("Export quote as CSV"), tr("quote") + QStringLiteral(".csv"),
                                                      tr("CSV files (*.csv)"));
    if (file.isEmpty()) return;
    production::CsvOptions options;
    options.utf8Bom = true; // spreadsheet programs detect UTF-8 (Ukrainian captions)
    auto r = production::writeTextFile(file.toStdString(), production::toCsv(rows_.table, options));
    if (!r) QMessageBox::critical(this, tr("Export failed"), QString::fromStdString(r.error().message));
}

int QuoteWorkspace::lineRows() const { return quote_ ? static_cast<int>(quote_->lines.size()) : 0; }

bool QuoteWorkspace::incompleteShown() const { return !banner_->isHidden(); }

} // namespace fos::app::gui
