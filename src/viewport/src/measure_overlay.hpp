// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/viewport/camera.hpp"
#include "fos/viewport/snap.hpp"

#include <QString>

#include <optional>
#include <vector>

class QPainter;

/// QPainter overlay of the measurement tool (private to the viewport module): the line between
/// the picked points (or the first point and the hover snap), the point markers, the snap
/// marker (shape by SnapKind) and the distance label at the line midpoint.
namespace fos::viewport::detail {

void paintMeasureOverlay(QPainter& painter, const Camera& camera, const std::vector<SnapPoint>& points,
                         const std::optional<SnapPoint>& hover, const QString& lengthSuffix);

/// Distance label text, e.g. "600.0 mm" (one decimal, `suffix` appended).
QString distanceLabel(double mm, const QString& suffix);

} // namespace fos::viewport::detail
