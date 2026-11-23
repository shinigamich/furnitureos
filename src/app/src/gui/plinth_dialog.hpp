// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Edit > Add plinth…: the form of a project::PlinthRequest with a live preview from
/// project::planPlinth (view-model fos/app/plinth_form.hpp). OK is enabled only while the
/// engine accepts the request; the refusal message is shown otherwise.
#include "fos/app/plinth_form.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;

namespace fos::app {
struct Selection;
}

namespace fos::app::gui {

class PlinthDialog : public QDialog {
    Q_OBJECT
public:
    PlinthDialog(const project::Project& project, const Selection& selection, QWidget* parent = nullptr);

    /// The request built from the current form values.
    project::PlinthRequest request() const;
    /// Preview of request() (updated on every change).
    const PlinthPreview& preview() const noexcept { return preview_; }

private:
    void updatePreview();

    const project::Project& project_;
    project::PlinthRequest base_; ///< id and name (not edited here).
    PlinthPreview preview_;

    QListWidget* cabinets_;
    QComboBox* material_;
    QDoubleSpinBox* setback_;
    QDoubleSpinBox* height_;   ///< Minimum (0) = the cabinets' plinth height.
    QCheckBox* returnLeft_;
    QCheckBox* returnRight_;
    QDoubleSpinBox* maxPiece_; ///< Minimum (0) = usable stock length.
    QPlainTextEdit* previewText_;
    QLabel* errorLabel_;
    QDialogButtonBox* buttons_;
};

} // namespace fos::app::gui
