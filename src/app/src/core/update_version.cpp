// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/update_version.hpp"

#include <algorithm>
#include <charconv>

namespace fos::app {
namespace {

bool allDigits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool validIdentifierChars(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-';
    });
}

std::optional<unsigned long> numericPart(std::string_view s) {
    if (!allDigits(s) || (s.size() > 1 && s.front() == '0')) return std::nullopt;
    unsigned long value = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc() || ptr != s.data() + s.size()) return std::nullopt;
    return value;
}

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = s.find(sep, start);
        out.push_back(s.substr(start, pos == std::string_view::npos ? std::string_view::npos : pos - start));
        if (pos == std::string_view::npos) break;
        start = pos + 1;
    }
    return out;
}

int compareIdentifier(const std::string& a, const std::string& b) {
    const bool an = allDigits(a), bn = allDigits(b);
    if (an && bn) {
        // Numeric identifiers have no leading zeros, so a longer one is larger.
        if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
        return a.compare(b) < 0 ? -1 : (a == b ? 0 : 1);
    }
    if (an != bn) return an ? -1 : 1; // numeric identifiers have lower precedence
    const int c = a.compare(b);       // ASCII order
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

} // namespace

std::string SemVer::toString() const {
    std::string s = std::to_string(major) + '.' + std::to_string(minor) + '.' + std::to_string(patch);
    for (std::size_t i = 0; i < prerelease.size(); ++i) {
        s += i == 0 ? '-' : '.';
        s += prerelease[i];
    }
    return s;
}

std::optional<SemVer> parseSemVer(std::string_view text) {
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.remove_prefix(1);
    if (const std::size_t plus = text.find('+'); plus != std::string_view::npos) {
        for (std::string_view id : split(text.substr(plus + 1), '.'))
            if (!validIdentifierChars(id)) return std::nullopt;
        text = text.substr(0, plus);
    }
    std::string_view pre;
    if (const std::size_t dash = text.find('-'); dash != std::string_view::npos) {
        pre = text.substr(dash + 1);
        if (pre.empty()) return std::nullopt;
        text = text.substr(0, dash);
    }
    const auto core = split(text, '.');
    if (core.size() != 3) return std::nullopt;
    SemVer v;
    const auto ma = numericPart(core[0]), mi = numericPart(core[1]), pa = numericPart(core[2]);
    if (!ma || !mi || !pa) return std::nullopt;
    v.major = *ma;
    v.minor = *mi;
    v.patch = *pa;
    if (!pre.empty()) {
        for (std::string_view id : split(pre, '.')) {
            if (!validIdentifierChars(id)) return std::nullopt;
            if (allDigits(id) && id.size() > 1 && id.front() == '0') return std::nullopt;
            v.prerelease.emplace_back(id);
        }
    }
    return v;
}

int compareSemVer(const SemVer& a, const SemVer& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    // A final release has higher precedence than any of its pre-releases.
    if (a.prerelease.empty() || b.prerelease.empty()) {
        if (a.prerelease.empty() && b.prerelease.empty()) return 0;
        return a.prerelease.empty() ? 1 : -1;
    }
    const std::size_t n = std::min(a.prerelease.size(), b.prerelease.size());
    for (std::size_t i = 0; i < n; ++i)
        if (const int c = compareIdentifier(a.prerelease[i], b.prerelease[i]); c != 0) return c;
    if (a.prerelease.size() == b.prerelease.size()) return 0;
    return a.prerelease.size() < b.prerelease.size() ? -1 : 1;
}

} // namespace fos::app
