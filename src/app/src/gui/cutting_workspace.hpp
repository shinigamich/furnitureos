// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Cutting workspace: per-material sheet layouts from project::cuttingPlans (a heuristic
/// optimiser), drawn from the cutting module's SVG; kerf / trim settings as a command.
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QSvgRenderer;
class QTableWidget;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

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

protected:
    void showEvent(QShowEvent* event) override;

private:
    void refresh();
    void showPlan();
    void showSheet();
    void loadSettings();
    void applySettings();

    Session& session_;
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
    QLabel* error_;
    bool stale_ = true;
};

} // namespace fos::app::gui
