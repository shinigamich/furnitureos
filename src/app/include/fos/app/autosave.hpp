// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Autosave scheduling decisions and recovery offers (the files themselves are written by
/// project::Document::autosave / formats, see src/formats/README.md).
#include "fos/formats/recovery.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace fos::app {

/// Allowed autosave interval in minutes; 0 disables autosave.
inline constexpr int kMaxAutosaveMinutes = 120;
inline constexpr int kDefaultAutosaveMinutes = 5;

/// Interval clamped to [0, kMaxAutosaveMinutes], in milliseconds (0 = off).
int autosaveIntervalMs(int minutes) noexcept;

/// Inputs of one autosave timer tick.
struct AutosaveState {
    int intervalMinutes = kDefaultAutosaveMinutes;
    bool dirty = false;
    std::uint64_t revision = 0; ///< Session revision (incremented by every change).
    std::optional<std::uint64_t> lastAutosavedRevision; ///< Revision written by the last autosave.
};

/// True when autosave is enabled, the document has unsaved changes and the current revision
/// has not been autosaved yet (no rewriting of an identical autosave every tick).
bool autosaveDue(const AutosaveState& state) noexcept;

/// File base used for autosaving a document that has never been saved:
/// `<directory>/untitled-<projectId>.fosp` (the autosave itself is "<base>.autosave").
std::filesystem::path untitledAutosaveBase(const std::filesystem::path& directory, const std::string& projectId);

/// Newest "untitled-*.fosp.autosave" in `directory`, returned as its base path (without the
/// ".autosave" suffix, i.e. the path to pass to project::recoverProject); nullopt when none.
std::optional<std::filesystem::path> newestUntitledAutosave(const std::filesystem::path& directory);

/// A recovery file that is newer than the document.
struct RecoveryOffer {
    formats::RecoveryCandidate candidate;
    bool documentMissing = false; ///< The document itself does not exist.
};

/// Offer when the newest autosave/backup of `document` (formats::listRecoveryCandidates) was
/// modified after the document itself, or the document is missing but a candidate exists.
std::optional<RecoveryOffer> recoveryOffer(const std::filesystem::path& document);

} // namespace fos::app
