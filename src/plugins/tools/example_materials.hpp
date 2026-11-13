// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/formats/library.hpp"

#include <chrono>
#include <string_view>

/// Content of the example plugin examples/plugins/org.furnitureos.example-materials.
/// Shared by the generator tool (which writes the committed .foslib) and the tests
/// (which check that the committed file is byte-identical to a fresh export).
namespace fos::plugins::example {

inline constexpr std::string_view kPluginId = "org.furnitureos.example-materials";
inline constexpr std::string_view kLibraryRelativePath = "libraries/generic-boards.foslib";
inline constexpr std::string_view kAppVersion = "0.1.0";
/// Fixed `savedAt` (2026-11-12T00:00:00Z = 1762905600 + 365 * 86400 = 1794441600 s since the epoch) so that the
/// export is reproducible byte for byte.
inline constexpr std::chrono::seconds kSavedAt{1794441600};

/// Generic boards and edge bands; no manufacturer names, costs left at 0 (unknown).
formats::Library library();

/// Writes library() to `path` exactly as committed (no backups, fixed timestamp).
core::Result<formats::SaveReport> writeLibrary(const std::filesystem::path& path);

} // namespace fos::plugins::example
