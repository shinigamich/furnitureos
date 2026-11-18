// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Cutting workspace: per-material sheet layouts from project::cuttingPlans — saw (guillotine)
/// layouts of rectangular parts and router nesting layouts of shaped parts, both heuristics —
/// drawn from the cutting / nesting modules' SVG; saw and nesting settings as commands.
#include "fos/app/nesting_rows.hpp"

#include <QWidget>

#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSvgRenderer;
class QTableWidget;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class NestingSettingsForm;

/// Paints an SVG document scaled to fit, keeping its aspect ratio.
class SvgView : public QWidget {
    Q_OBJECT
public:
    explicit SvgView(QWidget* parent = nullptr);
    /// Returns false when the document is not valid SVG.
    bool setSvg(const QByteArray& svg);
    bool hasDocument() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QSvgRenderer* renderer_;
};

class CuttingWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit CuttingWorkspace(Session& session, QWidget* parent = nullptr);
    int materialCount() const;
    /// Shaped parts left out of the saw layouts (routed / nested separately).
    int routedPartCount() const noexcept { return routedCount_; }
    /// Scope combo, objective combo and remnant table of the settings editor (for tests).
    QComboBox* settingsScope() const noexcept { return scope_; }
    QComboBox* objective() const noexcept { return objective_; }
    QTableWidget* remnants() const noexcept { return remnants_; }
    /// Summary table (one row per material and method), sheet selector (saw and nest sheets of
    /// the selected material) and the sheet preview (for tests).
    QTableWidget* summary() const noexcept { return summary_; }
    QComboBox* sheetSelector() const noexcept { return sheet_; }
    SvgView* sheetView() const noexcept { return view_; }
    /// Kind of the sheet shown (nullopt when none).
    std::optional<PlanKind> shownSheetKind() const;
    NestingSettingsForm* nestingSettings() const noexcept { return nesting_; }
    /// Same as pressing "Apply and re-optimise".
    void apply() { applySettings(); }

protected:
    void showEvent(QShowEvent* event) override;

private:
    void refresh();
    void showPlan();
    void showSheet();
    void loadSettings();
    void applySettings();
    void addRemnant();
    void removeRemnant();

    Session& session_;
    std::vector<CuttingTableRow> rows_;
    QTableWidget* summary_;
    QComboBox* sheet_;
    QLabel* sheetInfo_;
    SvgView* view_;
    QComboBox* scope_;
    QDoubleSpinBox* kerf_;
    QDoubleSpinBox* trimLeft_;
    QDoubleSpinBox* trimRight_;
    QDoubleSpinBox* trimTop_;
    QDoubleSpinBox* trimBottom_;
    QComboBox* objective_;
    QTableWidget* remnants_;
    QPushButton* addRemnant_;
    QPushButton* removeRemnant_;
    QPushButton* useDefaults_;
    QLabel* remnantHint_;
    QLabel* error_;
    QLabel* routed_;
    NestingSettingsForm* nesting_;
    int routedCount_ = 0;
    bool stale_ = true;
};

} // namespace fos::app::gui
