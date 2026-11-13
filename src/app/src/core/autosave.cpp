// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/autosave.hpp"

#include <algorithm>
#include <system_error>

namespace fos::app {

namespace fs = std::filesystem;

int autosaveIntervalMs(int minutes) noexcept { return std::clamp(minutes, 0, kMaxAutosaveMinutes) * 60 * 1000; }

bool autosaveDue(const AutosaveState& state) noexcept {
    if (autosaveIntervalMs(state.intervalMinutes) == 0 || !state.dirty) return false;
    return !state.lastAutosavedRevision || *state.lastAutosavedRevision != state.revision;
}

fs::path untitledAutosaveBase(const fs::path& directory, const std::string& projectId) {
    return directory / ("untitled-" + projectId + ".fosp");
}

std::optional<fs::path> newestUntitledAutosave(const fs::path& directory) {
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) return std::nullopt;
    std::optional<fs::path> best;
    fs::file_time_type bestTime{};
    const std::string prefix = "untitled-", suffix = ".fosp.autosave";
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.size() <= prefix.size() + suffix.size() || name.rfind(prefix, 0) != 0 ||
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        std::error_code tec;
        const auto t = fs::last_write_time(entry.path(), tec);
        if (tec) continue;
        // Newest first; ties broken by name so the choice is deterministic.
        if (!best || t > bestTime || (t == bestTime && entry.path() < *best)) {
            best = entry.path();
            bestTime = t;
        }
    }
    if (!best) return std::nullopt;
    fs::path base = *best;
    base.replace_extension(); // drop ".autosave"
    return base;
}

std::optional<RecoveryOffer> recoveryOffer(const fs::path& document) {
    const auto candidates = formats::listRecoveryCandidates(document);
    if (candidates.empty()) return std::nullopt;
    std::error_code ec;
    if (!fs::exists(document, ec)) return RecoveryOffer{candidates.front(), true};
    const auto docTime = fs::last_write_time(document, ec);
    if (ec) return std::nullopt;
    if (candidates.front().modified > docTime) return RecoveryOffer{candidates.front(), false};
    return std::nullopt;
}

} // namespace fos::app
