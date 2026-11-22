// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Edit > Add worktop…: the form of a project::WorktopRequest with a live preview from
/// project::planWorktop (view-model fos/app/worktop_form.hpp). OK is enabled only while the
/// engine accepts the request; the refusal message is shown otherwise.
#include "fos/app/worktop_form.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QTableWidget;

namespace fos::app {
struct Selection;
}

namespace fos::app::gui {

class WorktopDialog : public QDialog {
    Q_OBJECT
public:
    WorktopDialog(const project::Project& project, const Selection& selection, QWidget* parent = nullptr);

    /// The request built from the current form values.
    project::WorktopRequest request() const;
    /// Preview of request() (updated on every change).
    const WorktopPreview& preview() const noexcept { return preview_; }

private:
    void addCutoutRow(const project::WorktopCutout& cutout);
    void updatePreview();

    const project::Project& project_;
    project::WorktopRequest base_; ///< id, name and rules (not edited here).
    WorktopPreview preview_;

    QListWidget* cabinets_;
    QComboBox* material_;
    QComboBox* band_;
    QCheckBox* bandFront_;
    QCheckBox* bandBack_;
    QCheckBox* bandLeft_;
    QCheckBox* bandRight_;
    QDoubleSpinBox* frontOverhang_;
    QDoubleSpinBox* leftOverhang_;
    QDoubleSpinBox* rightOverhang_;
    QDoubleSpinBox* backGap_;
    QDoubleSpinBox* maxPiece_; ///< Minimum (0) = usable stock length.
    QTableWidget* cutouts_;
    QPlainTextEdit* previewText_;
    QLabel* errorLabel_;
    QDialogButtonBox* buttons_;
};

} // namespace fos::app::gui
