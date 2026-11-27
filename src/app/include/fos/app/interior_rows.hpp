// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Interior editor view-model: the section tree of a cabinet as rows, the size syntax of the
/// editor and the project commands of the Split / Remove split / Set size / Shelves / Front
/// actions. Every edit is a project::commands call (furniture/edit.hpp is atomic: a refused
/// edit leaves the project unchanged and its message is shown verbatim). Nothing is
/// computed here: sizes in mm are resolved by the furniture generator.
#include "fos/app/cabinet_parts.hpp"
#include "fos/core/result.hpp"
#include "fos/project/commands.hpp"

#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace fos::app {

/// One node of the section tree (depth-first, parents before children, children in path order).
struct SectionRow {
    std::string path; ///< "" = root, "1", "2.1", ... (furniture::Section paths).
    int depth = 0;    ///< 0 = root.
    int index = 0;    ///< 1-based position within the parent (0 for the root).
    int siblings = 0; ///< Number of children of the parent (0 for the root).
    furniture::SplitDirection parentSplit = furniture::SplitDirection::None;
    furniture::SectionSize size; ///< Own size within the parent (unused for the root).
    furniture::SplitDirection split = furniture::SplitDirection::None;
    int childCount = 0;
    int shelfCount = 0; ///< Leaf only.
    std::optional<furniture::FrontSpec> front;
    std::optional<furniture::HangingRailSpec> hangingRail; ///< Leaf only: clothes rail.
    std::optional<furniture::InnerDrawerSpec> innerDrawers; ///< Leaf only: drawers behind the door.
    bool isRoot() const noexcept { return depth == 0; }
    bool isLeaf() const noexcept { return split == furniture::SplitDirection::None; }
};

std::vector<SectionRow> sectionRows(const furniture::Section& root);

/// "Whole interior" for the root; otherwise "Section 2.1" plus the position inside a
/// two-or-more split: "(left)" / "(right)" for vertical splits, "(bottom)" / "(top)" for
/// horizontal ones (first / last child only). Translated.
QString sectionCaption(const SectionRow& row);
/// Translated size column: "300 mm (fixed)", "share 1", "= width / 2" (root: empty).
QString sizeDescription(const furniture::SectionSize& size);
/// Translated content column, e.g. "2 side by side" (vertical split), "3 stacked"
/// (horizontal split), "shelves: 2", "clothes rail" (a leaf with a hanging rail; after the
/// shelves when both are set), "inner drawers: 2" (innerDrawersText), "empty",
/// followed by the front ("; door pair", "; door, hinged left", "; drawers: 3").
QString contentText(const SectionRow& row);

/// Editor text of a size (inverse of parseSectionSize): Fixed "300", Weight "1*" / "2.5*",
/// Expression its text.
QString sizeText(const furniture::SectionSize& size);
/// Size syntax of the editor:
///  * a plain decimal number (optional "mm"; one decimal comma accepted) -> Fixed millimetres;
///  * "*" or "<number>*" -> Weight (share of the remaining space; "*" = 1);
///  * anything else -> Expression over the cabinet parameters (judged by the engine).
/// Empty input -> InvalidArgument. Positivity is checked by the furniture module.
core::Result<furniture::SectionSize> parseSectionSize(const QString& text);
/// ";"-separated list of sizes (drawer heights bottom to top), e.g. "140; 1*; 1*".
core::Result<std::vector<furniture::SectionSize>> parseSizeList(const QString& text);
QString sizeListText(const std::vector<furniture::SectionSize>& sizes);

/// commands::splitSection into `parts` equal shares (Weight 1 each).
project::Command splitCommand(const std::string& itemId, const std::string& path,
                              furniture::SplitDirection direction, int parts);
/// commands::replaceSection making the split section at `path` one empty leaf again (no
/// shelves; its front is kept; its size within the parent is kept by the command).
/// InvalidArgument when the section does not exist or is not split.
core::Result<project::Command> removeSplitCommand(const furniture::CabinetDefinition& cabinet,
                                                  const std::string& itemId, const std::string& path);
/// Parses `text` (parseSectionSize) into commands::setSectionSize.
core::Result<project::Command> sectionSizeCommand(const std::string& itemId, const std::string& path,
                                                  const QString& text);
/// commands::setInterior with an empty leaf (no shelves, no front).
project::Command clearInteriorCommand(const std::string& itemId);

/// What the front editor shows / submits.
struct FrontInput {
    enum class Choice { None, Door, DoorPair, DrawerStack, Flap, SlidingDoors, ApplianceNiche, ApplianceFront };
    Choice choice = Choice::None;
    furniture::OverlayKind overlay = furniture::OverlayKind::Full;
    furniture::HingeSide hingeSide = furniture::HingeSide::Left;
    bool hinges = true;
    furniture::HandlePosition handle = furniture::HandlePosition::Center;
    QString drawerHeights = QStringLiteral("1*; 1*; 1*"); ///< parseSizeList syntax, bottom to top.
    /// Drawer stack only: a drawer box behind every front (furniture::DrawerBoxSpec); false =
    /// fronts only. Ignored for the other choices.
    bool drawerBox = false;
    std::string slide = std::string(hardware::ids::kSlideBallBearing); ///< DrawerSlide hardware id.
    furniture::DrawerBottomMount drawerBottom = furniture::DrawerBottomMount::Grooved;
    /// Sliding doors only: door count, a whole number 2..6 or an expression over the
    /// cabinet parameters (furniture::SlidingDoorSpec::countExpression).
    QString slidingDoors = QStringLiteral("2");
    /// Appliance niche only: vent settings (furniture::ApplianceNicheSpec), submitted as given;
    /// the furniture module validates them (sizes > 0, opening fits the niche).
    furniture::ApplianceNicheSpec niche;
    /// Door / door pair / flap / drawer stack only (frontStyleApplies): Slab = one board,
    /// FramePanel = five-piece front built from `frame` (furniture README "Frame-and-panel
    /// fronts"). Submitted as given; the furniture module validates the frame values.
    furniture::FrontStyle style = furniture::FrontStyle::Slab;
    /// Frame settings (mm); kept while the style is Slab so switching back restores them.
    furniture::FramePanelSpec frame;
};
/// True for the choices that may be built frame-and-panel (Door, DoorPair, Flap, DrawerStack).
bool frontStyleApplies(FrontInput::Choice choice) noexcept;
/// Panel material combo of a frame-and-panel front: first "Same as front" (empty id = the
/// front material), then the library materials in library order ("Name — 18 mm (id)");
/// `current` is appended with `missing` set when it is not empty and not in the library.
struct PanelMaterialChoice {
    std::string id;
    QString text;
    bool missing = false;
};
std::vector<PanelMaterialChoice> panelMaterialChoices(const furniture::Libraries& libraries,
                                                      const std::string& current);
/// Material a frame-and-panel front of `cabinet` would use for its panel: frame.panelMaterial,
/// else the front material, else (empty front) the carcass material.
domain::MaterialId framePanelMaterialId(const furniture::CabinetDefinition& cabinet,
                                        const furniture::FramePanelSpec& frame);
/// Translated warning for a GLASS frame panel (frame.panelKind == Glass) whose material
/// (framePanelMaterialId) is not of MaterialKind::Glass or not in the library, mirroring the
/// engine warning furniture.front.glass_material; empty otherwise (also for grooved panels).
QString glassPanelMaterialWarning(const furniture::CabinetDefinition& cabinet, const furniture::Libraries& libraries,
                                  const furniture::FramePanelSpec& frame);
/// Library entries of kind DrawerSlide (id, name) in library order; `current` is appended
/// with `missing` set when it is not among them.
struct SlideChoice {
    std::string id;
    std::string name;
    bool missing = false;
};
std::vector<SlideChoice> slideChoices(const furniture::Libraries& libraries, const std::string& current);
/// The FrontSpec the editor submits (drawer heights parsed for a drawer stack; for sliding
/// doors a whole number in `slidingDoors` is the count, other text the count expression).
/// InvalidArgument for Choice::None (no spec), an unparsable height list, an empty door count
/// or a whole number outside 2..6. `style` / `frame` are copied for the choices where
/// frontStyleApplies (otherwise the spec is a slab with the default frame).
core::Result<furniture::FrontSpec> frontSpec(const FrontInput& input);
/// Editor state of an existing front (None for std::nullopt).
FrontInput frontInput(const std::optional<furniture::FrontSpec>& front);
/// commands::setFront for the input (Choice::None removes the front). Drawer heights are
/// parsed only for a drawer stack; a parse error is returned without a command.
core::Result<project::Command> frontCommand(const std::string& itemId, const std::string& path,
                                            const FrontInput& input);

/// What the clothes-rail editor of a LEAF section shows / submits (furniture::HangingRailSpec,
/// furniture README "Hanging rails"). Submitted as given: the furniture module judges the
/// height, the section depth / height, the front and the library ids, and its refusal is shown.
struct HangingRailInput {
    bool enabled = false;
    double heightFromTop = 60; ///< mm below the underside of the panel above the section.
    std::string rail = std::string(hardware::ids::kClothesRailGeneric);
    std::string supports = std::string(hardware::ids::kRailSupportGeneric);
};
/// Editor state of an existing rail (enabled = false and the generic defaults for std::nullopt).
HangingRailInput hangingRailInput(const std::optional<furniture::HangingRailSpec>& rail);
/// The spec the editor submits (std::nullopt when not enabled).
std::optional<furniture::HangingRailSpec> hangingRailSpec(const HangingRailInput& input);
/// Clothes rails in the library: kind Other entries carrying rail_height and hook_clearance (the
/// keys of hw.clothes_rail_generic), library order; `current` appended as `missing` otherwise.
std::vector<HardwareChoice> clothesRailChoices(const furniture::Libraries& libraries, const std::string& current);
/// Rail supports in the library: kind Other entries carrying body_height and screw_spacing and no
/// rail_height (the keys of hw.rail_support_generic); `current` appended as `missing` otherwise.
std::vector<HardwareChoice> railSupportChoices(const furniture::Libraries& libraries, const std::string& current);
/// commands::replaceSection of the leaf at `path` with its hanging rail set (or removed when
/// not enabled); everything else of the section (size, shelves, front) is kept. InvalidArgument
/// when the section does not exist or is split (a rail belongs to a leaf).
core::Result<project::Command> hangingRailCommand(const furniture::CabinetDefinition& cabinet,
                                                  const std::string& itemId, const std::string& path,
                                                  const HangingRailInput& input);

} // namespace fos::app
