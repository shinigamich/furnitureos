// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Quote workspace (customer price estimate from production::quote, via app/quote_rows.hpp)
/// and the pricing settings dialog (Project > Pricing settings…).
#include "fos/app/quote_rows.hpp"

#include <QDialog>
#include <QWidget>

#include <array>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

/// Waste / markup / tax percentages and the four optional labour rates.
class PricingSettingsDialog : public QDialog {
    Q_OBJECT
public:
    PricingSettingsDialog(const PricingInput& current, const QString& currency, QWidget* parent = nullptr);
    PricingInput input() const;

private:
    QDoubleSpinBox* waste_;
    QDoubleSpinBox* markup_;
    QDoubleSpinBox* tax_;
    std::array<QCheckBox*, 4> rateSet_{};
    std::array<QDoubleSpinBox*, 4> rate_{};
};

class QuoteWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit QuoteWorkspace(Session& session, QWidget* parent = nullptr);
    /// Opens the pricing settings dialog and applies it as one undoable command.
    void editPricingSettings();
    void exportCsv();
    void setUseCuttingPlans(bool on);
    // Tests.
    int lineRows() const;
    bool incompleteShown() const;
    const QTableWidget* table() const noexcept { return view_; }

protected:
    void showEvent(QShowEvent* event) override;

private:
    void refresh();
    Session& session_;
    QCheckBox* useSheets_;
    QLabel* banner_;
    QLabel* info_;
    QTableWidget* view_;
    std::optional<production::Quote> quote_;
    QuoteRows rows_;
    bool stale_ = true;
};

} // namespace fos::app::gui
