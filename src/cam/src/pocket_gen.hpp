// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal: pocket clearing (see generate.hpp, POCKETS).
#include "builder.hpp"

#include "fos/cam/pocket.hpp"

namespace fos::cam::detail {

/// Tool for the pocket: options.pocketToolNumber or automatic (generate.hpp). Error names the
/// needed diameter.
core::Result<const Tool*> selectPocketTool(const PocketTarget& t, const domain::Part& part, const ToolLibrary& tools,
                                           const CamOptions& o);

/// Appends roughing (all levels) and the finishing wall passes. Errors: invalid finish allowance for
/// the tool, no possible entry for a non-centre-cutting tool.
core::Result<void> millPocket(Builder& b, const PocketTarget& t, const Tool& tool, const domain::Part& part,
                              const CamOptions& o);

} // namespace fos::cam::detail
