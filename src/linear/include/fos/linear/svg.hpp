// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// SVG rendering of a linear cutting plan.
#include "fos/linear/types.hpp"

#include <string>

namespace fos::linear {

/// One SVG with every bar as a horizontal strip, 1 user unit = 1 mm along the bar
/// (viewBox width = longest bar length; each strip is 40 mm high, 30 mm apart, with a label
/// line above it). CSS classes: fos-bar, fos-trim, fos-piece, fos-kerf, fos-offcut, fos-waste,
/// fos-label. Text is XML-escaped.
std::string renderLinearSvg(const LinearJob& job, const LinearResult& result);

} // namespace fos::linear
