// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QColor>
#include <QPointF>
#include <QString>

class QPainter;

namespace fos::printing::detail {

/// Draws single-line text in a painter whose units are mm (Y down). `anchor` lies on
/// the baseline; `hAlign` -1 = text starts at the anchor, 0 = centred, +1 = ends there.
/// `height` is the font em size in mm; `rotationCcwDeg` rotates about the anchor.
void drawTextMm(QPainter& painter, QPointF anchor, double height, int hAlign, double rotationCcwDeg,
                const QString& text, const QString& family, const QColor& color);

/// Scales the painter so that its units become millimetres (see PaintOptions::unitsPerMm).
void scaleToMm(QPainter& painter, double unitsPerMm);

} // namespace fos::printing::detail
