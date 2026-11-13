// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/plugins/semver.hpp"

#include <limits>

namespace fos::plugins {
namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isIdentChar(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}

core::Error fail(std::string_view text, const std::string& reason) {
    return core::makeError(core::ErrorCode::InvalidArgument,
                           "\"" + std::string(text) + "\" is not a valid semantic version (MAJOR.MINOR.PATCH): " +
                               reason + ".");
}

/// Splits on '.', keeping empty parts so that they can be reported.
std::vector<std::string_view> split(std::string_view s) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (;;) {
        const auto dot = s.find('.', start);
        if (dot == std::string_view::npos) {
            parts.push_back(s.substr(start));
            return parts;
        }
        parts.push_back(s.substr(start, dot - start));
        start = dot + 1;
    }
}

bool allDigits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!isDigit(c)) return false;
    return true;
}

/// Parses a numeric identifier without leading zeros; false on overflow.
bool toNumber(std::string_view s, std::uint64_t& out) {
    std::uint64_t v = 0;
    for (char c : s) {
        const auto d = static_cast<std::uint64_t>(c - '0');
        if (v > (std::numeric_limits<std::uint64_t>::max() - d) / 10) return false;
        v = v * 10 + d;
    }
    out = v;
    return true;
}

} // namespace

std::string SemVer::toString() const {
    std::string s = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    for (std::size_t i = 0; i < prerelease.size(); ++i) s += (i == 0 ? "-" : ".") + prerelease[i].text;
    for (std::size_t i = 0; i < build.size(); ++i) s += (i == 0 ? "+" : ".") + build[i];
    return s;
}

core::Result<SemVer> parseSemVer(std::string_view text) {
    if (text.empty()) return fail(text, "the text is empty");
    for (char c : text)
        if (!isIdentChar(c) && c != '.' && c != '+')
            return fail(text, std::string("character '") + (c == ' ' ? std::string("space") : std::string(1, c)) +
                                  "' is not allowed");
    std::string_view rest = text;
    std::string_view buildPart;
    bool hasBuild = false;
    if (const auto plus = rest.find('+'); plus != std::string_view::npos) {
        buildPart = rest.substr(plus + 1);
        rest = rest.substr(0, plus);
        hasBuild = true;
        if (buildPart.find('+') != std::string_view::npos) return fail(text, "'+' may appear only once");
    }
    std::string_view prePart;
    bool hasPre = false;
    if (const auto dash = rest.find('-'); dash != std::string_view::npos) {
        prePart = rest.substr(dash + 1);
        rest = rest.substr(0, dash);
        hasPre = true;
    }
    const auto core = split(rest);
    if (core.size() != 3) return fail(text, "expected exactly three numeric parts MAJOR.MINOR.PATCH");
    SemVer v;
    std::uint64_t* targets[3] = {&v.major, &v.minor, &v.patch};
    const char* names[3] = {"major", "minor", "patch"};
    for (int i = 0; i < 3; ++i) {
        const auto part = core[static_cast<std::size_t>(i)];
        if (!allDigits(part)) return fail(text, std::string("the ") + names[i] + " part must be a non-negative integer");
        if (part.size() > 1 && part[0] == '0')
            return fail(text, std::string("the ") + names[i] + " part has a leading zero");
        if (!toNumber(part, *targets[i])) return fail(text, std::string("the ") + names[i] + " part is too large");
    }
    if (hasPre) {
        for (auto id : split(prePart)) {
            if (id.empty()) return fail(text, "a pre-release identifier is empty");
            PrereleaseIdentifier p;
            p.text = std::string(id);
            if (allDigits(id)) {
                if (id.size() > 1 && id[0] == '0')
                    return fail(text, "numeric pre-release identifier \"" + p.text + "\" has a leading zero");
                if (!toNumber(id, p.number))
                    return fail(text, "numeric pre-release identifier \"" + p.text + "\" is too large");
                p.numeric = true;
            }
            v.prerelease.push_back(std::move(p));
        }
    }
    if (hasBuild) {
        for (auto id : split(buildPart)) {
            if (id.empty()) return fail(text, "a build metadata identifier is empty");
            v.build.emplace_back(id);
        }
    }
    return v;
}

std::strong_ordering compare(const SemVer& a, const SemVer& b) noexcept {
    if (auto c = a.major <=> b.major; c != 0) return c;
    if (auto c = a.minor <=> b.minor; c != 0) return c;
    if (auto c = a.patch <=> b.patch; c != 0) return c;
    // A release has higher precedence than any of its pre-releases.
    if (a.prerelease.empty() && b.prerelease.empty()) return std::strong_ordering::equal;
    if (a.prerelease.empty()) return std::strong_ordering::greater;
    if (b.prerelease.empty()) return std::strong_ordering::less;
    const std::size_t n = std::min(a.prerelease.size(), b.prerelease.size());
    for (std::size_t i = 0; i < n; ++i) {
        const auto& x = a.prerelease[i];
        const auto& y = b.prerelease[i];
        if (x.numeric && y.numeric) {
            if (auto c = x.number <=> y.number; c != 0) return c;
        } else if (x.numeric != y.numeric) {
            return x.numeric ? std::strong_ordering::less : std::strong_ordering::greater;
        } else if (auto c = x.text.compare(y.text); c != 0) {
            return c < 0 ? std::strong_ordering::less : std::strong_ordering::greater;
        }
    }
    return a.prerelease.size() <=> b.prerelease.size();
}

} // namespace fos::plugins
