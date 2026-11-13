// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Project browser view-model: items -> generated assembly -> parts.
#include "fos/project/resolve.hpp"

#include <string>
#include <vector>

namespace fos::app {

struct BrowserNode {
    enum class Kind { Item, Assembly, Part };
    Kind kind = Kind::Item;
    std::string id;     ///< Item id, assembly id (= item id) or part id.
    std::string label;  ///< Item / assembly / part name.
    std::string detail; ///< Part: finished size "L x W x T" (mm); assembly: envelope "W x D x H".
    bool isCabinet = false;  ///< Item nodes: cabinet (true) or free panel (false).
    bool generated = true;   ///< Item nodes: false when the generator refused the item.
    std::vector<BrowserNode> children;
};

/// One node per project item (project order). A generated item has exactly one Assembly
/// child whose children are its parts in model order; an item that was not generated has
/// no children. `resolved` may be null (structural resolve failure): items only.
std::vector<BrowserNode> buildBrowserTree(const project::Project& project, const project::Resolved* resolved);

} // namespace fos::app
