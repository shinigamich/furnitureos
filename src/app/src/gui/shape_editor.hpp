// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// "Shape" section of the part inspector: edits the parametric outline (ContourSpec) of a
/// cabinet part slot or of a free panel through the contour_rows view-model. Apply runs
/// commands::setPartContour / setPanelContour; a refused spec shows the core's message inline
/// and changes nothing; Reset reloads the stored spec.
#include "fos/app/contour_rows.hpp"

#include <QGroupBox>

class QComboBox;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QTableWidget;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class ShapeEditor : public QGroupBox {
    Q_OBJECT
public:
    ShapeEditor(Session& session, ContourTarget target, QWidget* parent = nullptr);

    /// The rows as currently entered in the widgets.
    ContourInput input() const;
    /// Shows `input` in the widgets (not applied).
    void setInput(const ContourInput& input);
    /// Same as pressing Apply; false when refused (the message is shown inline).
    bool apply();
    /// Inline error currently shown (empty = none).
    QString errorText() const;

private:
    void load(const ContourInput& input);
    void fillVertices();
    void fillCutouts();
    void kindChanged();
    void addVertex();
    void removeVertex();
    void moveVertex(int delta);
    void addCutout(furniture::CutoutKind kind);
    void removeCutout();
    bool run(std::optional<furniture::ContourSpec> spec);

    Session& session_;
    ContourTarget target_;
    /// Rows of the tables are kept here and synchronised with the table widgets.
    mutable ContourInput rows_;
    QComboBox* kind_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QLineEdit* radii_[4] = {};
    QComboBox* arcEdge_ = nullptr;
    QLineEdit* arcRise_ = nullptr;
    QComboBox* arcDirection_ = nullptr;
    QTableWidget* vertices_ = nullptr;
    QTableWidget* cutouts_ = nullptr;
    QLabel* error_ = nullptr;
};

} // namespace fos::app::gui
