// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Interior editor view-model: the section tree of a cabinet as rows, the size syntax of the
/// editor and the project commands of the Split / Remove split / Set size / Shelves / Front
/// actions. Every edit is a project::commands call (furniture/edit.hpp is atomic: a refused
/// edit leaves the project unchanged and its message is shown verbatim). Nothing is
/// computed here: sizes in mm are resolved by the furniture generator.
#include "fos/core/result.hpp"
#include "fos/project/commands.hpp"

#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace fos::app {

/// One node of the section tree (depth-first, parents before children, children in path order).
struct SectionRow {
    std::string path;     ///< "" = root, "1", "2.1", ... (furniture::Section paths).
    int depth = 0;        ///< 0 = root.
    int index = 0;        ///< 1-based position within the parent (0 for the root).
    int siblings = 0;     ///< Number of children of the parent (0 for the root).
    furniture::SplitDirection parentSplit = furniture::SplitDirection::None;
    furniture::SectionSize size; ///< Own size within the parent (unused for the root).
    furniture::SplitDirection split = furniture::SplitDirection::None;
    int childCount = 0;
    int shelfCount = 0;   ///< Leaf only.
    std::optional<furniture::FrontSpec> front;
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
/// Translated content column, e.g. "split vertically into 2", "shelves: 2", "empty",
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
project::Command splitCommand(const std::string& itemId, const std::string& path, furniture::SplitDirection direction,
                              int parts);
/// commands::replaceSection making the split section at `path` one empty leaf again (no
/// shelves; its front is kept; its size within the parent is kept by the command).
/// InvalidArgument when the section does not exist or is not split.
core::Result<project::Command> removeSplitCommand(const furniture::CabinetDefinition& cabinet, const std::string& itemId,
                                                  const std::string& path);
/// Parses `text` (parseSectionSize) into commands::setSectionSize.
core::Result<project::Command> sectionSizeCommand(const std::string& itemId, const std::string& path,
                                                  const QString& text);
/// commands::setInterior with an empty leaf (no shelves, no front).
project::Command clearInteriorCommand(const std::string& itemId);

/// What the front editor shows / submits.
struct FrontInput {
    enum class Choice { None, Door, DoorPair, DrawerStack };
    Choice choice = Choice::None;
    furniture::OverlayKind overlay = furniture::OverlayKind::Full;
    furniture::HingeSide hingeSide = furniture::HingeSide::Left;
    bool hinges = true;
    furniture::HandlePosition handle = furniture::HandlePosition::Center;
    QString drawerHeights = QStringLiteral("1*; 1*; 1*"); ///< parseSizeList syntax, bottom to top.
};
/// Editor state of an existing front (None for std::nullopt).
FrontInput frontInput(const std::optional<furniture::FrontSpec>& front);
/// commands::setFront for the input (Choice::None removes the front). Drawer heights are
/// parsed only for a drawer stack; a parse error is returned without a command.
core::Result<project::Command> frontCommand(const std::string& itemId, const std::string& path, const FrontInput& input);

} // namespace fos::app
