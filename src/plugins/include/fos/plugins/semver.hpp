// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/// Semantic Versioning 2.0.0 (https://semver.org) parsing and precedence, used for plugin
/// versions and for the application-version compatibility range of plugin manifests.
namespace fos::plugins {

/// One dot-separated pre-release identifier: numeric (no leading zeros) or alphanumeric.
struct PrereleaseIdentifier {
    bool numeric = false;
    std::uint64_t number = 0; ///< Value when numeric.
    std::string text;         ///< Original text (always set).
    bool operator==(const PrereleaseIdentifier&) const = default;
};

/// A parsed version MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD].
struct SemVer {
    std::uint64_t major = 0;
    std::uint64_t minor = 0;
    std::uint64_t patch = 0;
    std::vector<PrereleaseIdentifier> prerelease; ///< Empty = a normal (release) version.
    std::vector<std::string> build;               ///< Build metadata; ignored for precedence.

    /// Canonical text, e.g. "1.2.3-rc.1+build.5".
    std::string toString() const;
    bool isPrerelease() const noexcept { return !prerelease.empty(); }
};

/// Strict SemVer 2.0.0 parser. Rejects leading zeros in numeric parts ("01.0.0", "1.0.0-01"),
/// empty identifiers ("1.0.0-", "1.0.0-a..b"), characters outside [0-9A-Za-z-], a leading
/// "v", whitespace, missing parts ("1.0") and numbers that do not fit 64 bits.
/// Error: InvalidArgument with a message quoting the text and the reason.
core::Result<SemVer> parseSemVer(std::string_view text);

/// SemVer precedence (spec section 11): major, minor, patch numerically; a pre-release
/// version is lower than the release; pre-release identifiers compare left to right
/// (numeric < alphanumeric, numeric numerically, alphanumeric by ASCII), a shorter set of
/// identifiers is lower when all preceding ones are equal. Build metadata is ignored, so
/// 1.0.0+a and 1.0.0+b compare equal.
std::strong_ordering compare(const SemVer& a, const SemVer& b) noexcept;

inline bool operator<(const SemVer& a, const SemVer& b) noexcept { return compare(a, b) < 0; }
inline bool operator>(const SemVer& a, const SemVer& b) noexcept { return compare(a, b) > 0; }
inline bool operator<=(const SemVer& a, const SemVer& b) noexcept { return compare(a, b) <= 0; }
inline bool operator>=(const SemVer& a, const SemVer& b) noexcept { return compare(a, b) >= 0; }
/// Equal precedence (build metadata ignored).
inline bool samePrecedence(const SemVer& a, const SemVer& b) noexcept { return compare(a, b) == 0; }

} // namespace fos::plugins
