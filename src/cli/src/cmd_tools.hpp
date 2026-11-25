// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal: `.fostools` loading shared by `fos tools` and `fos cam --tools` (not public API).
#include "common.hpp"

#include "fos/cam/tool.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace fos::cli::detail {

/// Loads a `.fostools` file as a CAM tool library. Prints warnings to io.err. On failure prints
/// "fos <command>: ..." with every error finding, sets `exitCode` (kExitUsageOrIo when the file
/// cannot be read, kExitValidationErrors when it is not a valid tool table or lacks speeds and
/// feeds CAM needs) and returns nullopt.
std::optional<cam::ToolLibrary> loadToolLibraryOrReport(Io& io, std::string_view command, const std::string& path,
                                                        int& exitCode);

} // namespace fos::cli::detail
