// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/drawings/view.hpp"

#include <optional>
#include <string>
#include <vector>

/// Annotation entities attached to a view. Anchor points are VIEW coordinates
/// (model mm, see view.hpp); sizes and offsets that must not change with the drawing
/// scale (text heights, dimension-line offsets, text shifts) are PAPER millimetres.
/// Every entity has a stable `id` that `DrawingEdits` refers to.
namespace fos::drawings {

enum class DimensionOrientation {
    Horizontal, ///< Measures |u2 - u1|; the dimension line is parallel to view `right`.
    Vertical    ///< Measures |v2 - v1|; the dimension line is parallel to view `up`.
};

/// Linear dimension between two anchor points. The measured value is ALWAYS computed
/// from the anchors (measuredValue()); it cannot be typed in. `textOverride` replaces
/// only the displayed text.
struct Dimension {
    std::string id;
    DimensionOrientation orientation = DimensionOrientation::Horizontal;
    Vec2 p1, p2; ///< Anchors, view coordinates.
    /// Signed paper distance (mm) of the dimension line from the anchor that lies
    /// farthest in the offset direction: Horizontal -> along +v (negative = below),
    /// Vertical -> along +u (negative = to the left).
    double offset = 10.0;
    std::optional<std::string> textOverride;
    /// Row of the text: 0 = directly at the dimension line, k = k rows farther away
    /// (set by automatic layout to keep texts of narrow chain dimensions apart).
    int textRow = 0;
    Vec2 textShift{}; ///< Extra paper shift of the text (user edit).
    bool hidden = false;
    bool userAuthored = false;
    /// Set by edit application when `textOverride` was made against a different measured
    /// value than the current one (drawings.edits.stale_override). The text is then drawn
    /// with kStaleOverrideMarker appended so the sheet visibly flags it.
    bool staleOverride = false;
};

/// Appended to a stale text override on the sheet ("800" -> "800 *").
inline constexpr const char* kStaleOverrideMarker = " *";

/// Distance between the anchors along the measured direction, model mm.
double measuredValue(const Dimension& d) noexcept;
/// `textOverride` (+ kStaleOverrideMarker when staleOverride) if set, otherwise
/// core::formatLength(measuredValue(d), decimals).
std::string dimensionText(const Dimension& d, int decimals = core::kDisplayDecimals);

/// Text anchored to a view point.
struct Note {
    std::string id;
    Vec2 anchor;        ///< View coordinates.
    Vec2 paperOffset{}; ///< Paper mm added after scaling.
    std::string text;   ///< UTF-8, single line.
    double height = 2.5;
    HAlign hAlign = HAlign::Left;
    VAlign vAlign = VAlign::Baseline;
    double rotationDeg = 0;
    Vec2 textShift{};
    bool hidden = false;
    bool userAuthored = false;
    std::string sourceId;
};

/// Arrow from a text to a view point.
struct LeaderNote {
    std::string id;
    Vec2 target;       ///< View coordinates; the arrow tip.
    Vec2 textOffset{}; ///< Paper mm from the target to the start of the text shoulder.
    std::string text;
    double height = 2.5;
    Vec2 textShift{};
    bool hidden = false;
    bool userAuthored = false;
};

/// Circle with a label marking a region shown elsewhere as a detail.
struct DetailMarker {
    std::string id;
    Vec2 center;       ///< View coordinates.
    double radius = 0; ///< Model mm.
    std::string label;
    Vec2 textShift{};
    bool hidden = false;
    bool userAuthored = false;
};

/// Edge-banding mark: a thick line parallel to a banded edge, drawn just inside the
/// outline, plus a legend code ("E1").
struct EdgeBandMark {
    std::string id;
    Vec2 a, b;        ///< The banded edge, view coordinates.
    Vec2 inward;      ///< Unit vector (view axes) pointing into the part.
    std::string code; ///< Legend code.
    std::string sourceId; ///< Edge-band id.
    Vec2 textShift{};
    bool hidden = false;
};

/// Double-headed arrow showing the grain direction.
struct GrainArrow {
    std::string id;
    Vec2 center;    ///< View coordinates.
    Vec2 direction; ///< Unit vector in view axes.
    Vec2 textShift{}; ///< Paper shift of the whole arrow (user edit).
    bool hidden = false;
};

struct ViewAnnotations {
    std::vector<Dimension> dimensions;
    std::vector<Note> notes;
    std::vector<LeaderNote> leaders;
    std::vector<DetailMarker> detailMarkers;
    std::vector<EdgeBandMark> edgeBandMarks;
    std::vector<GrainArrow> grainArrows;
};

enum class DimensionTerminator { Arrow, Tick };

/// All values in paper mm.
struct DimensionStyle {
    double textHeight = 2.5;
    double textGap = 0.8;            ///< Between dimension line and text baseline, and between text rows.
    double arrowSize = 2.5;
    double extensionGap = 1.0;       ///< Gap between the anchor and the start of the extension line.
    double extensionOvershoot = 1.5; ///< Extension line beyond the dimension line.
    DimensionTerminator terminator = DimensionTerminator::Arrow;
    double firstGap = 6.0;           ///< Clear distance between the view outline and the first stacked dimension.
    double stackGap = 3.5;           ///< Clear distance between consecutive stacked dimensions.
    int decimals = core::kDisplayDecimals;
};

/// Paper-space geometry of one dimension (also useful for hit-testing in a UI).
struct DimensionGeometry {
    Segment2 line;                 ///< Dimension line between the two extension lines.
    std::optional<Segment2> ext1;  ///< Extension lines (absent when shorter than the gap).
    std::optional<Segment2> ext2;
    Text text;
    Rect2 textBounds;              ///< textBox(text).
    bool terminatorsOutside = false; ///< Arrows flipped outside because the span is too short.
};

DimensionGeometry layoutDimension(const Dimension& d, const ViewPlacement& placement, const DimensionStyle& style);

/// Deterministic stacking rule shared by all automatic dimensions. Entry i is one
/// dimension line (or one chain) whose texts occupy `rows[i]` text rows. Dimension
/// texts sit above horizontal lines and to the left of vertical lines, so with
/// row depth D_i = rows[i] * (textHeight + textGap) the distances from the outline are
///   text on the view side of its line (horizontal below / vertical right of the view):
///       line_i = cursor + D_i,  cursor' = line_i + stackGap
///   text on the far side (horizontal above / vertical left of the view):
///       line_i = cursor,        cursor' = line_i + D_i + stackGap
/// starting with cursor = firstGap. `side` is -1 (below / left) or +1 (above / right);
/// the returned offsets carry that sign.
std::vector<double> stackDimensionOffsets(const std::vector<int>& rows, DimensionOrientation orientation, int side,
                                          const DimensionStyle& style);

struct AutoDimensionOptions {
    bool overall = true; ///< Overall extents of the view (width/height/depth).
    bool chains = true;  ///< Chains through the edge-on panels (shelves, partitions, sides).
};

/// Automatic dimensions of an assembly view, laid out for paper scale `scale`:
///  * horizontal dimensions below the view, vertical dimensions to its left;
///  * nearest to the view a CHAIN whose boundaries are the view extents plus both
///    faces of every panel seen edge-on across that direction (vertical chain:
///    PanelAspect::EdgeOnHorizontal parts such as shelves, top, bottom; horizontal
///    chain: EdgeOnVertical parts such as sides and partitions); a chain with a single
///    segment is omitted because it would repeat the overall dimension;
///  * outside it the OVERALL dimension.
/// Ids: "<view>/dim.overall.<width|depth|height>" (the assembly axis measured) and
/// "<view>/dim.chain.<h|v>[<from>..<to>]" where a boundary is "env:min", "env:max",
/// "<part-id>:min" or "<part-id>:max" (first part in assembly order at that position).
std::vector<Dimension> autoDimensionView(const View& view, double scale, const DimensionStyle& style,
                                         const AutoDimensionOptions& options = {});

} // namespace fos::drawings
