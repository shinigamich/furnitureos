// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Interior tab of the cabinet inspector: the section tree (fos::app::sectionRows) with the
/// Split vertically / Split horizontally / Remove split / Set size / Shelves actions and the
/// front of the selected section. Every action is one project command (interior_rows.hpp); a
/// refused edit shows the furniture module's message and changes nothing.
#include "fos/app/interior_rows.hpp"

#include <QWidget>

#include <string>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class InteriorEditor : public QWidget {
    Q_OBJECT
public:
    /// `selectedPath` is owned by the inspector so the selection survives its rebuilds.
    InteriorEditor(Session& session, std::string itemId, std::string& selectedPath,
                   QWidget* parent = nullptr);

    /// Selects the section row with `path` ("" = whole interior). False when there is none.
    bool selectSection(const QString& path);

private:
    void fill();
    void showSelected();
    const SectionRow* selectedRow() const;
    /// Executes the command (or shows the input error); returns success.
    bool run(const core::Result<project::Command>& command);

    Session& session_;
    std::string itemId_;
    std::string& selectedPath_;
    std::vector<SectionRow> rows_;

    QTreeWidget* tree_;
    QPushButton* splitVertical_;
    QPushButton* splitHorizontal_;
    QSpinBox* splitParts_;
    QPushButton* removeSplit_;
    QLineEdit* size_;
    QPushButton* setSize_;
    QSpinBox* shelves_;
    QComboBox* frontKind_;
    QComboBox* overlay_;
    QComboBox* hingeSide_;
    QCheckBox* hinges_;
    QComboBox* handle_;
    QLineEdit* drawerHeights_;
    QPushButton* applyFront_;
    QLabel* selectedTitle_;
    QLabel* error_;
};

} // namespace fos::app::gui
