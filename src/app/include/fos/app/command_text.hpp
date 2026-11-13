// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// User-interface text of undoable commands (project::CommandLabel).
#include "fos/project/commands.hpp"

#include <QString>

namespace fos::app {

/// Translates `label.key` (context "fos::app::CommandText") and substitutes {0}, {1}, ...
/// by the arguments. An argument that is the id of an item of `project` is replaced by that
/// item's name (labels carry ids, see project README §5). Unknown keys stay English.
/// Example: {"Set {1} of {0} to {2}", {"cab1", "width", "900"}} with item cab1 named
/// "Base cabinet" -> "Set width of Base cabinet to 900".
QString commandText(const project::CommandLabel& label, const project::Project& project);

} // namespace fos::app
