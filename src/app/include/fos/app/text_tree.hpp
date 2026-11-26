// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// A widget-free tree of display texts (one QString per column) used by tree views whose
/// content is laid out by a view-model (edge-banding jobs, project comparison).
#include <QStringList>

#include <vector>

namespace fos::app {

struct TextNode {
    QStringList columns;            ///< Column texts; may be shorter than the view's column count.
    bool emphasis = false;          ///< Group / total row (shown bold).
    bool warning = false;           ///< Highlight (missing reference, problem).
    std::vector<TextNode> children; ///< Display order.
};

} // namespace fos::app
