// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/formats/recovery.hpp"

#include <algorithm>
#include <charconv>

namespace fos::formats {

namespace fs = std::filesystem;

std::vector<RecoveryCandidate> listRecoveryCandidates(const std::filesystem::path& path) {
    std::vector<RecoveryCandidate> out;
    std::error_code ec;
    const fs::path directory = path.parent_path().empty() ? fs::path(".") : path.parent_path();
    const std::string base = path.filename().string();
    if (base.empty()) return out;

    // Scan the folder instead of probing .bak1, .bak2, ... so that gaps do not hide backups.
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.size() <= base.size() || name.compare(0, base.size(), base) != 0) continue;
        const std::string_view suffix = std::string_view(name).substr(base.size());
        RecoveryCandidate candidate;
        if (suffix == ".autosave") {
            candidate.kind = RecoveryKind::Autosave;
        } else if (suffix.size() > 4 && suffix.substr(0, 4) == ".bak") {
            const std::string_view digits = suffix.substr(4);
            int index = 0;
            const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), index);
            if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || index < 1) continue;
            candidate.kind = RecoveryKind::Backup;
            candidate.backupIndex = index;
        } else {
            continue;
        }
        std::error_code fileEc;
        if (!fs::is_regular_file(it->path(), fileEc)) continue;
        candidate.path = it->path();
        candidate.modified = fs::last_write_time(it->path(), fileEc);
        if (fileEc) continue;
        out.push_back(std::move(candidate));
    }
    std::sort(out.begin(), out.end(), [](const RecoveryCandidate& a, const RecoveryCandidate& b) {
        if (a.modified != b.modified) return a.modified > b.modified;
        if (a.kind != b.kind) return a.kind == RecoveryKind::Autosave;
        return a.backupIndex < b.backupIndex;
    });
    return out;
}

core::Result<RecoveredDocument> loadNewestValid(const std::filesystem::path& path, const FormatSpec& spec,
                                                const LoadOptions& options) {
    const auto candidates = listRecoveryCandidates(path);
    if (candidates.empty())
        return core::makeError(core::ErrorCode::NotFound,
                               "No autosave or backup files exist for '" + path.string() + "'.", path.string());
    std::vector<SkippedCandidate> skipped;
    for (const RecoveryCandidate& candidate : candidates) {
        auto loaded = loadDocument(candidate.path, spec, options);
        if (loaded) return RecoveredDocument{std::move(loaded.value()), candidate, std::move(skipped)};
        skipped.push_back({candidate, loaded.error()});
    }
    std::string message = "None of the " + std::to_string(candidates.size()) +
                          " autosave/backup files of '" + path.string() + "' can be loaded:";
    for (const SkippedCandidate& s : skipped) message += "\n  " + s.error.message;
    return core::makeError(core::ErrorCode::NotFound, message, path.string());
}

} // namespace fos::formats
