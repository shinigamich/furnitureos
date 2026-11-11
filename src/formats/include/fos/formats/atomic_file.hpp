// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/formats/container.hpp"

#include <cstddef>

/// Crash-safe file replacement with rotating backups, and document saving on top of it.
/// Behaviour is specified in src/formats/README.md ("Atomic save and backups").
namespace fos::formats {

/// Phases of an atomic save, in execution order.
enum class SaveStage { WriteTemp, Verify, RotateBackups, Replace };
const char* toString(SaveStage stage) noexcept;

struct SaveOptions {
    /// Number of rotating backups `<name>.bak1` (newest) .. `<name>.bakN` kept beside the
    /// target. 0 = no backups are made (existing backup files are left untouched).
    int backupCount = 3;
    /// Clock for `savedAt` when Document::savedAt is empty. Null = system clock.
    std::function<std::chrono::system_clock::time_point()> clock;
    /// Replaces the built-in durable writer (create, write, flush, fsync, close) for the
    /// temporary file. For tests and fault injection; null = built-in.
    std::function<core::Result<void>(const std::filesystem::path& file, std::string_view bytes)> writer;
    /// Fault-injection hook called immediately before each stage; returning an error
    /// aborts the save exactly as a real failure of that stage would. Null = none.
    std::function<core::Result<void>(SaveStage)> beforeStage;
};

struct SaveReport {
    std::filesystem::path path;   ///< File that was replaced/created.
    std::string savedAt;          ///< Timestamp written into the envelope.
    std::string checksum;         ///< SHA-256 (hex) of the canonical payload.
    std::size_t bytesWritten = 0;
};

/// `<path>.bak<index>` (index >= 1; 1 is the most recent backup).
std::filesystem::path backupPath(const std::filesystem::path& path, int index);
/// `<path>.autosave`.
std::filesystem::path autosavePath(const std::filesystem::path& path);

/// Creates/overwrites `file` with `bytes`, flushes and fsyncs it (POSIX fsync,
/// Windows _commit). Not atomic by itself.
core::Result<void> writeFileDurably(const std::filesystem::path& file, std::string_view bytes);

/// Extra check on the bytes read back from the temporary file before it replaces the target.
using ContentVerifier = std::function<core::Result<void>(std::string_view bytesReadBack)>;

/// Atomically replaces `target` with `bytes`:
///  1. write a temporary file in the same directory (durably),
///  2. read it back, compare with `bytes`, run `verify`,
///  3. rotate backups (the old target becomes `.bak1`; the target itself stays in place),
///  4. rename the temporary file over the target, fsync the directory (POSIX).
/// On any failure the previous target and the previous backups are left as they were,
/// the temporary file is removed and an IoError/CorruptData error says which stage failed.
core::Result<void> atomicWriteFile(const std::filesystem::path& target, std::string_view bytes,
                                   const SaveOptions& options = {}, const ContentVerifier& verify = {});

/// Serialises `document` (filling savedAt from the clock when empty) and writes it with
/// atomicWriteFile; the verify step re-parses the temporary file and checks its checksum.
core::Result<SaveReport> saveDocument(const std::filesystem::path& path, Document document,
                                      const SaveOptions& options = {});

/// Writes `document` atomically to autosavePath(path) without backups. The file at
/// `path` is not touched. Scheduling autosaves is the application's job.
core::Result<SaveReport> writeAutosave(const std::filesystem::path& path, Document document,
                                       const SaveOptions& options = {});

/// Deletes autosavePath(path) if present (call after a successful regular save).
core::Result<void> discardAutosave(const std::filesystem::path& path);

} // namespace fos::formats
