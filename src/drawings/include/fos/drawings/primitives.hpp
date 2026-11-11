// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/drawings/geom2d.hpp"

#include <string>
#include <string_view>
#include <variant>
#include <vector>

/// Renderer-independent drawing primitives.
///
/// PAPER SPACE: millimetres on the sheet, origin at the bottom-left corner of the
/// paper, +X to the right, +Y up. Angles are degrees, counter-clockwise from +X.
/// Writers (SVG, PDF) flip Y themselves. Line weights are millimetres on paper.
///
/// Every primitive carries two optional ids:
///  * `sourceId`  - id of the model entity it depicts (part id, operation id, edge-band id);
///  * `elementId` - stable id of the drawing element it belongs to (a dimension, a note,
///                  a title-block field, ...), the key used by `DrawingEdits`.
namespace fos::drawings {

enum class LineStyle {
    Visible,   ///< Continuous, thick: visible outlines.
    Hidden,    ///< Dashed: hidden outlines.
    Center,    ///< Long-dash/short-dash: centre lines and axes.
    Dimension, ///< Continuous, thin: dimension and extension lines, leaders.
    Thin,      ///< Continuous, thin: hatching, auxiliary lines.
    Border     ///< Continuous, extra thick: sheet frame.
};

/// Default line weight in mm: Visible 0.5, Hidden 0.25, Center 0.18, Dimension 0.18,
/// Thin 0.18, Border 0.7.
double defaultLineWeight(LineStyle s) noexcept;
/// Dash pattern in paper mm as alternating dash/gap lengths; empty = continuous.
/// Hidden: {2, 1}; Center: {8, 1.5, 1, 1.5}.
std::vector<double> dashPattern(LineStyle s);
const char* toString(LineStyle s) noexcept;

struct Line {
    Vec2 a, b;
    LineStyle style = LineStyle::Visible;
    double weight = 0.5;
    std::string sourceId;
    std::string elementId;
};

struct Polyline {
    std::vector<Vec2> points;
    bool closed = false;
    LineStyle style = LineStyle::Visible;
    double weight = 0.5;
    std::string sourceId;
    std::string elementId;
};

struct Circle {
    Vec2 center;
    double radius = 0;
    LineStyle style = LineStyle::Visible;
    double weight = 0.5;
    std::string sourceId;
    std::string elementId;
};

/// Circular arc drawn counter-clockwise from `startDeg` to `endDeg`
/// (endDeg <= startDeg wraps around through 360).
struct Arc {
    Vec2 center;
    double radius = 0;
    double startDeg = 0;
    double endDeg = 90;
    LineStyle style = LineStyle::Visible;
    double weight = 0.5;
    std::string sourceId;
    std::string elementId;
};

enum class HAlign { Left, Center, Right };
/// Vertical anchor: `Baseline` = `position` lies on the text baseline; `Middle` = on the
/// middle of the capital letters (0.35 height above the baseline); `Top` = on the cap
/// line (0.7 height above the baseline).
enum class VAlign { Baseline, Middle, Top };

/// Single-line text. `height` is the font em size in mm (capital letters are about
/// 0.7 x height tall). `rotationDeg` rotates counter-clockwise about `position`.
struct Text {
    Vec2 position;
    double height = 2.5;
    HAlign hAlign = HAlign::Left;
    VAlign vAlign = VAlign::Baseline;
    double rotationDeg = 0;
    std::string text; ///< UTF-8.
    std::string sourceId;
    std::string elementId;
};

enum class MarkerKind {
    Arrow, ///< Filled triangle: tip at `position`, pointing along `angleDeg`, length = size, width = size / 3.
    Tick,  ///< Oblique stroke of length `size` centred at `position`, at angleDeg + 45 degrees.
    Dot    ///< Filled disc of diameter `size`.
};

struct Marker {
    MarkerKind kind = MarkerKind::Arrow;
    Vec2 position;
    double angleDeg = 0;
    double size = 2.5;
    std::string sourceId;
    std::string elementId;
};

using Primitive = std::variant<Line, Polyline, Circle, Arc, Text, Marker>;

/// Outline of a marker in paper space: Arrow -> 3 points (tip first, filled polygon),
/// Tick -> 2 points (stroke), Dot -> empty (use position and size / 2).
std::vector<Vec2> markerOutline(const Marker& m);

/// HEURISTIC text width in mm: 0.6 x height per Unicode code point. No font metrics
/// are available in the engineering core; layout code adds margins on top of this.
double estimateTextWidth(std::string_view utf8, double height) noexcept;
/// Number of Unicode code points in a UTF-8 string (malformed bytes count as one each).
std::size_t codePointCount(std::string_view utf8) noexcept;

/// Axis-aligned paper-space box around a text, using estimateTextWidth() and an
/// ascent of 0.75 / descent of 0.2 x height, honouring anchor and rotation.
Rect2 textBox(const Text& t) noexcept;

Rect2 boundsOf(const Primitive& p) noexcept;
Rect2 boundsOf(const std::vector<Primitive>& ps) noexcept;
const std::string& sourceIdOf(const Primitive& p) noexcept;
const std::string& elementIdOf(const Primitive& p) noexcept;
/// Moves a primitive by `d` (paper mm).
void translate(Primitive& p, Vec2 d) noexcept;

} // namespace fos::drawings
