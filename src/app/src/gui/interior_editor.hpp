// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Interior tab of the cabinet inspector: the section tree (fos::app::sectionRows) with the
/// Split vertically / Split horizontally / Remove split / Set size / Shelves actions, the
/// clothes rail, the inner drawers and the front of the selected section. Every action is one project command (interior_rows.hpp); a
/// refused edit shows the furniture module's message and changes nothing.
#include "fos/app/interior_rows.hpp"

#include <QWidget>

#include <string>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class InnerDrawersBox;

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
    void showRail(const SectionRow* row);

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
    QComboBox* drawerBox_;    ///< None / box (drawer stack only).
    QComboBox* slide_;        ///< DrawerSlide library entries.
    QComboBox* drawerBottom_; ///< DrawerBottomMount.
    QComboBox* nicheVent_;          ///< NicheVent (appliance niche only).
    QDoubleSpinBox* ventSize_;      ///< ApplianceNicheSpec::ventSize, mm.
    QDoubleSpinBox* ventEdge_;      ///< ApplianceNicheSpec::ventEdgeDistance, mm.
    QDoubleSpinBox* ventEndMargin_; ///< ApplianceNicheSpec::ventEndMargin, mm.
    QComboBox* frontStyle_;          ///< FrontStyle (door / pair / flap / drawer stack).
    QDoubleSpinBox* stileWidth_;     ///< FramePanelSpec, mm.
    QDoubleSpinBox* railWidth_;
    QDoubleSpinBox* panelThickness_;
    QDoubleSpinBox* grooveDepth_;
    QDoubleSpinBox* panelClearance_;
    QComboBox* frameJoint_;          ///< FrameJoint.
    QComboBox* panelMaterial_;       ///< "" = same as front, else a material id.
    QPushButton* applyFront_;
    QCheckBox* railEnabled_;      ///< Clothes rail in the (leaf) section.
    QDoubleSpinBox* railHeight_;  ///< HangingRailSpec::heightFromTop, mm.
    QComboBox* railHardware_;     ///< Clothes rail library entries.
    QComboBox* railSupports_;     ///< Rail support library entries.
    QPushButton* applyRail_;
    InnerDrawersBox* innerDrawers_; ///< Inner drawers of the (leaf) section behind a door.
    QLabel* selectedTitle_;
    QLabel* error_;
};

} // namespace fos::app::gui
