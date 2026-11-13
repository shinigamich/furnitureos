// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Build identity of the desktop application (shown in Help > About).

namespace fos::app {

/// Application version, from the CMake project version (e.g. "0.1.0").
const char* applicationVersion() noexcept;
/// Where the corresponding source code is published (AGPL-3.0 section 13).
inline constexpr const char* kSourceUrl = "https://github.com/furnitureos/furnitureos";
inline constexpr const char* kLicense = "AGPL-3.0-only";
inline constexpr const char* kOrganization = "FurnitureOS";
inline constexpr const char* kApplicationName = "FurnitureOS";

} // namespace fos::app
