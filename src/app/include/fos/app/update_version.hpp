// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Semantic versions (https://semver.org, 2.0.0) for the in-app updater: parsing release
/// tags such as "v0.2.0-beta.1" and ordering them, including pre-release precedence.
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fos::app {

struct SemVer {
    unsigned long major = 0, minor = 0, patch = 0;
    /// Dot-separated pre-release identifiers ("beta.1" -> {"beta", "1"}); empty = final release.
    std::vector<std::string> prerelease;

    bool isPrerelease() const noexcept { return !prerelease.empty(); }
    /// Canonical text without a leading "v" and without build metadata, e.g. "0.2.0-beta.1".
    std::string toString() const;
};

/// Parses "X.Y.Z[-pre][+build]" with an optional leading 'v' or 'V'. Build metadata is
/// accepted and ignored (it has no precedence). Leading zeros in numeric parts, empty
/// identifiers and characters outside [0-9A-Za-z-] are rejected (std::nullopt).
std::optional<SemVer> parseSemVer(std::string_view text);

/// Precedence per semver 2.0 rule 11: negative if a < b, 0 if equal, positive if a > b.
/// 1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-alpha.beta < 1.0.0-beta < 1.0.0-beta.2 < 1.0.0-beta.11
/// < 1.0.0-rc.1 < 1.0.0.
int compareSemVer(const SemVer& a, const SemVer& b);

inline bool operator<(const SemVer& a, const SemVer& b) { return compareSemVer(a, b) < 0; }
inline bool operator==(const SemVer& a, const SemVer& b) { return compareSemVer(a, b) == 0; }

} // namespace fos::app
