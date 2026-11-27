// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal helpers shared by edit_tools.cpp, edit_layout.cpp and panel_tools.cpp.
#include "fos/app/edit_tools.hpp"

namespace fos::app::detail {
/// Non-empty list of existing, distinct item ids.
core::Result<void> checkItemIds(const project::Project& project, const std::vector<std::string>& ids);
/// "Verb {0}" with the item id for one item, else "Verb {0} items" with the count.
project::CommandLabel itemsLabel(const std::string& verb, const std::vector<std::string>& ids);
double axisValue(geometry::Vec3 v, Axis a);
geometry::Vec3 axisVector(Axis a, double value);
} // namespace fos::app::detail
