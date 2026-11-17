// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// View-model of the part Shape editor (README "Inspector"): the rows the editor shows for a
/// furniture::ContourSpec (kind, corner radii, arc edge, custom vertices, cut-outs) and their
/// conversion back to a spec. Every value except the custom vertices' bulge is an EXPRESSION
/// text passed on unchanged (plain numbers are normalised, "12,5 mm" -> "12.5"), so the
/// furniture module judges it; the app checks nothing geometric. All lengths are mm in the
/// PART frame (x along the length 0..L, y along the width 0..W), see contour_spec.hpp.
/// Changes go through commands::setPartContour (cabinet part slots) and
/// commands::setPanelContour (free panels); a refused spec changes nothing.
#include "fos/project/commands.hpp"

#include <QString>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fos::app {

class Session;

/// One row of the Custom vertex table.
struct VertexRow {
    QString x, y;   ///< Expressions (mm).
    QString bulge;  ///< Plain number; empty = 0 (straight segment).
    std::optional<domain::EdgeSide> bandFrom; ///< std::nullopt = automatic (box side or raw).
    bool operator==(const VertexRow&) const = default;
};

/// One row of the cut-out table. Rectangle: x / y = minimum corner, length, width, corner
/// radius (empty = sharp). Circle: x / y = centre, radius; length / width unused.
struct CutoutRow {
    furniture::CutoutKind kind = furniture::CutoutKind::Rectangle;
    QString x, y, length, width, radius;
    bool operator==(const CutoutRow&) const = default;
};

/// Everything the Shape editor holds. Fields of kinds other than `kind` are kept while
/// editing (switching the kind back restores them) but are not written to the spec.
struct ContourInput {
    furniture::ContourKind kind = furniture::ContourKind::Rectangle;
    /// Corner radii (0,0), (L,0), (L,W), (0,W) = L1/W1, L1/W2, L2/W2, L2/W1; empty = sharp.
    std::array<QString, 4> radii{};
    domain::EdgeSide arcEdge = domain::EdgeSide::L2;
    QString arcRise;
    furniture::ArcDirection arcDirection = furniture::ArcDirection::Convex;
    std::vector<VertexRow> vertices;
    std::vector<CutoutRow> cutouts;
    bool operator==(const ContourInput&) const = default;
};

/// Editor rows of a stored contour (std::nullopt = plain rectangle without cut-outs).
ContourInput contourInput(const std::optional<furniture::ContourSpec>& spec);

/// The spec for the editor rows: only the fields of `kind` (plus the cut-outs) are written.
/// Kind Rectangle without cut-outs yields std::nullopt (= remove the shape). Plain numbers
/// are normalised ("12,5" -> "12.5", "30 mm" -> "30"), other texts are trimmed and kept as
/// expressions. Errors (InvalidArgument, translated, naming the row): a bulge that is not a
/// plain number. Everything else (empty or bad expressions, radii that do not fit, an outline
/// that does not span the part) is left to the furniture module.
core::Result<std::optional<furniture::ContourSpec>> contourSpec(const ContourInput& input);

/// Four vertices of the full rectangle (0, 0), (contour.length, 0),
/// (contour.length, contour.width), (0, contour.width): the starting rows of a Custom outline.
std::vector<VertexRow> rectangleVertexRows();

/// Moves element `index` by `delta` positions (clamped to the vector); returns the new index,
/// or std::nullopt when `index` is out of range. Used for "Move up / Move down".
template <class T> std::optional<std::size_t> moveRow(std::vector<T>& rows, std::size_t index, int delta) {
    if (index >= rows.size()) return std::nullopt;
    long target = static_cast<long>(index) + delta;
    if (target < 0) target = 0;
    if (target >= static_cast<long>(rows.size())) target = static_cast<long>(rows.size()) - 1;
    const auto to = static_cast<std::size_t>(target);
    T row = std::move(rows[index]);
    rows.erase(rows.begin() + static_cast<long>(index));
    rows.insert(rows.begin() + static_cast<long>(to), std::move(row));
    return to;
}

/// What a contour edit applies to: a cabinet part slot, or a free panel item (empty slot).
struct ContourTarget {
    std::string itemId;
    std::string slot; ///< Cabinet slot id ("shelf.1.1"); empty = the panel item itself.
    bool operator==(const ContourTarget&) const = default;
};

/// Target of the Shape editor for item `itemId` and (optionally) one of its generated parts:
/// a cabinet needs a part of its assembly (its slot); a panel item needs no part (or its own
/// part). std::nullopt when nothing can be shaped (unknown item, cabinet without a part).
std::optional<ContourTarget> contourTarget(const project::Project& project, const std::string& itemId,
                                           const std::string& partId);
/// The stored spec of the target (PartOverride::contour / PanelDefinition::contour).
std::optional<furniture::ContourSpec> currentContour(const project::Project& project, const ContourTarget& target);
/// commands::setPartContour or commands::setPanelContour.
project::Command contourCommand(const ContourTarget& target, std::optional<furniture::ContourSpec> spec);
/// contourSpec + contourCommand + Session::execute. On any error the project is unchanged and
/// the error carries the app's or the core's message unchanged.
core::Result<void> applyContourInput(Session& session, const ContourTarget& target, const ContourInput& input);

/// Read-only facts of a generated part's outline (inspector, cutting list).
struct ShapeFacts {
    bool shaped = false;  ///< domain::hasShape.
    bool routed = false;  ///< !domain::isRectangularOutline: cut by routing / nesting, not the saw.
    double outlineArea = 0;     ///< mm2: geometry::signedArea of the finished outline minus the cut-outs.
    std::size_t segments = 0;   ///< Segments of the finished outer outline.
    std::size_t cutouts = 0;
    std::size_t bandedSegments = 0; ///< Outer segments with an edge band (PartShape::outerEdges).
};
ShapeFacts shapeFacts(const domain::Part& part);

/// Translated one-line description: "shaped part (routed)" / "shaped part (rectangular
/// outline, sawn)" / "rectangle".
QString shapeSummary(const ShapeFacts& facts);

} // namespace fos::app
