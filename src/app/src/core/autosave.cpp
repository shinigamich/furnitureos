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
