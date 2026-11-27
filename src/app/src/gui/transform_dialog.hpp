// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Dialogs of the free-modelling tools: Move / rotate (absolute position, relative offset,
/// rotation about an axis) and Array (linear copies). They only collect numbers (mm, degrees);
/// the commands are built by fos::app (edit_tools.hpp).
#include "fos/app/edit_tools.hpp"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QTabWidget;

namespace fos::app::gui {

class TransformDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { Absolute, Relative, Rotate };
    /// `minCorner`: current minimum corner of the selection's box (prefills the absolute
    /// position); `panelsOnly`: rotation about X / Y offered (only free panels selected).
    TransformDialog(geometry::Vec3 minCorner, bool panelsOnly, int itemCount, QWidget* parent = nullptr);

    Mode mode() const;
    geometry::Vec3 position() const;  ///< Absolute minimum corner, mm.
    geometry::Vec3 offset() const;    ///< Relative translation, mm.
    Axis rotationAxis() const;
    double angle() const;             ///< Degrees, counter-clockwise looking against the axis.
    /// Opens on the given tab.
    void setMode(Mode mode);

private:
    QTabWidget* tabs_ = nullptr;
    QDoubleSpinBox* abs_[3] = {};
    QDoubleSpinBox* rel_[3] = {};
    QComboBox* axis_ = nullptr;
    QComboBox* angle_ = nullptr;
};

class ArrayDialog : public QDialog {
    Q_OBJECT
public:
    /// `size`: extents of the selection's box (the default step is its X size, side by side).
    explicit ArrayDialog(geometry::Vec3 size, QWidget* parent = nullptr);
    int count() const;
    geometry::Vec3 step() const;

private:
    QSpinBox* count_ = nullptr;
    QDoubleSpinBox* step_[3] = {};
};

/// Spin box for a length in mm in [-100 m, 100 m], 1 decimal.
QDoubleSpinBox* lengthSpin(QWidget* parent, double value);

} // namespace fos::app::gui
