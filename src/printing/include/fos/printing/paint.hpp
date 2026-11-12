// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/drawings/sheet.hpp"

#include <QColor>
#include <QImage>
#include <QString>

class QPainter;

/// Painting of drawings::Sheet with QPainter (screen preview, PDF, printers).
///
/// The geometry is identical to drawings::toSvg(): 1 sheet unit = 1 mm, sheet origin
/// bottom-left with Y up (each coordinate is flipped, y_device = height - y, so text is
/// never mirrored), line weights and dash patterns in mm, butt caps and mitre joins,
/// text height = font em size, Middle/Top anchors realised as a baseline shift of
/// 0.35 / 0.7 x height, rotation counter-clockwise, markers from drawings::markerOutline().
///
/// A QGuiApplication must exist (fonts).
namespace fos::printing {

struct PaintOptions {
    /// Font family for all text. The default asks the platform for its generic
    /// sans-serif face. Glyph shapes and advance widths depend on the font actually
    /// installed; positions, sizes and anchors do not.
    QString fontFamily = QStringLiteral("sans-serif");
    QColor color = Qt::black; ///< Lines, fills and text.
    QColor background;        ///< Invalid (default) = the sheet area is not filled.
    /// Painter units per millimetre.
    ///  * 0 (default): derived from the paint device, logicalDpiX()/25.4 horizontally
    ///    and logicalDpiY()/25.4 vertically - correct for QPrinter and QPdfWriter.
    ///  * > 0: used as given for both axes. Pass 1.0 when the painter's coordinate
    ///    system is already millimetres.
    double unitsPerMm = 0.0;
    bool antialiasing = true;
};

/// Paints all primitives of `sheet`. The sheet's top-left corner is placed at the
/// painter's current origin and the sheet extends `sheet.width` mm to the right and
/// `sheet.height` mm down (after the mm scaling described in PaintOptions::unitsPerMm).
/// The painter's state (transform, pen, brush, font, hints) is restored on return.
/// Primitives with non-finite coordinates are skipped; nothing is painted for a sheet
/// with a non-positive size.
void paintSheet(QPainter& painter, const drawings::Sheet& sheet, const PaintOptions& options = {});

/// Largest preview side renderPreview() will allocate, in pixels.
inline constexpr int kMaxPreviewSidePx = 16384;

/// Raster preview of a sheet: an image of ceil(width x pixelsPerMm) by
/// ceil(height x pixelsPerMm) pixels (Format_ARGB32_Premultiplied) whose pixel (0, 0)
/// is the sheet's TOP-left corner; sheet point (x, y) lies at pixel coordinates
/// (x * pixelsPerMm, (height - y) * pixelsPerMm). The paper is filled with
/// `options.background`, or white when that is invalid.
/// Returns a null QImage when the sheet size or `pixelsPerMm` is not positive and
/// finite, or when a side would exceed kMaxPreviewSidePx.
QImage renderPreview(const drawings::Sheet& sheet, double pixelsPerMm, const PaintOptions& options = {});

} // namespace fos::printing
