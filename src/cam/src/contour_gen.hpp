// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal: contour routing of shaped parts (see CONTOUR ROUTING in generate.hpp).
#include "builder.hpp"

namespace fos::cam::detail {

/// Selects the routing tool and appends the motions for every contour target (cut-outs, then
/// outline). Adds warnings to res.diagnostics and the tool to res.toolsUsed. No-op when
/// `targets` is empty.
core::Result<void> routeContours(Builder& b, const std::vector<ContourTarget>& targets, const domain::Part& part,
                                 const SetupFrame& frame, const MachineProfile& machine, const ToolLibrary& tools,
                                 const CamOptions& o, CamResult& res);

} // namespace fos::cam::detail
