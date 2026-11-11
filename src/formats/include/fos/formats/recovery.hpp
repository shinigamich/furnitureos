// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/formats/atomic_file.hpp"

namespace fos::formats {

enum class RecoveryKind { Autosave, Backup };

/// A file from which a damaged or lost document may be restored.
struct RecoveryCandidate {
    std::filesystem::path path;
    RecoveryKind kind = RecoveryKind::Backup;
    int backupIndex = 0;                          ///< 1.. for backups, 0 for the autosave.
    std::filesystem::file_time_type modified{};   ///< Last write time.
};

/// Autosave and backup files that exist for `path` (the document itself is not
/// included), newest first by modification time; ties: autosave before backups, then
/// lower backup index first. Returns an empty list when there are none.
std::vector<RecoveryCandidate> listRecoveryCandidates(const std::filesystem::path& path);

struct SkippedCandidate {
    RecoveryCandidate candidate;
    core::Error error; ///< Why it could not be loaded.
};

struct RecoveredDocument {
    LoadedDocument loaded;
    RecoveryCandidate source;               ///< File the document came from.
    std::vector<SkippedCandidate> skipped;  ///< Newer candidates that were invalid.
};

/// Loads the newest candidate that passes the full load pipeline (including the
/// checksum, unless options.allowChecksumMismatch). NotFound when there is no candidate
/// or none is valid; the message lists why each one was rejected.
core::Result<RecoveredDocument> loadNewestValid(const std::filesystem::path& path, const FormatSpec& spec,
                                                const LoadOptions& options = {});

} // namespace fos::formats
