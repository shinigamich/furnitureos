// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/formats/atomic_file.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fos::formats {
namespace {

namespace fs = std::filesystem;
using core::ErrorCode;
using core::makeError;

std::string randomSuffix() {
    std::random_device device;
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 4; ++i) {
        const unsigned value = device();
        for (int shift = 0; shift < 16; shift += 4) out.push_back(kHex[(value >> shift) & 0xf]);
    }
    return out;
}

fs::path withSuffix(const fs::path& path, const std::string& suffix) {
    fs::path out = path;
    out += suffix;
    return out;
}

core::Error ioError(const fs::path& file, const std::string& what, int err = 0) {
    std::string message = what;
    if (err != 0) message += std::string(": ") + std::strerror(err);
    message += ".";
    return makeError(ErrorCode::IoError, message, file.string());
}

void removeQuietly(const fs::path& file) {
    std::error_code ec;
    fs::remove(file, ec);
}

/// Best effort: make the directory entry changes durable (POSIX only).
void syncDirectory(const fs::path& directory) {
#ifndef _WIN32
    const int fd = ::open(directory.c_str(), O_RDONLY);
    if (fd >= 0) {
        (void)::fsync(fd);
        ::close(fd);
    }
#else
    (void)directory; // Directory handles cannot be flushed portably on Windows.
#endif
}

/// Indices N (1 <= N <= maxIndex) of the existing backup files `<target name>.bakN`, found by
/// ONE listing of the target's directory (not by probing every index, which costs
/// `backupCount` file-system calls per save). Names with leading zeros or other suffixes are
/// not backups. Ascending.
std::vector<int> existingBackups(const fs::path& target, int maxIndex) {
    std::vector<int> out;
    const fs::path directory = target.parent_path().empty() ? fs::path(".") : target.parent_path();
    const std::string prefix = target.filename().string() + ".bak";
    std::error_code ec;
    fs::directory_iterator it(directory, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0) continue;
        const std::string digits = name.substr(prefix.size());
        if (digits.size() > 9 || digits[0] == '0' ||
            digits.find_first_not_of("0123456789") != std::string::npos)
            continue;
        const int index = std::stoi(digits);
        if (index >= 1 && index <= maxIndex) out.push_back(index);
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// State of a backup rotation, able to undo itself.
class BackupRotation {
public:
    BackupRotation(fs::path target, int count) : target_(std::move(target)), count_(count) {}

    core::Result<void> perform() {
        std::error_code ec;
        const std::vector<int> existing = existingBackups(target_, count_);
        // The oldest backup is parked, not deleted, until the save has succeeded.
        const fs::path oldest = backupPath(target_, count_);
        if (!existing.empty() && existing.back() == count_) {
            parked_ = withSuffix(oldest, ".discard-" + randomSuffix());
            fs::rename(oldest, parked_, ec);
            if (ec) {
                parked_.clear();
                return ioError(oldest, "Cannot rotate backup '" + oldest.string() + "': " + ec.message());
            }
        }
        // Newest last in `existing`: move .bak(count-1) .. .bak1 up by one, highest first.
        for (auto it = existing.rbegin(); it != existing.rend(); ++it) {
            const int i = *it;
            if (i >= count_) continue;
            const fs::path from = backupPath(target_, i);
            const fs::path to = backupPath(target_, i + 1);
            fs::rename(from, to, ec);
            if (ec) return ioError(from, "Cannot rotate backup '" + from.string() + "': " + ec.message());
            moved_.emplace_back(from, to);
        }
        // The current file becomes .bak1 through a hard link (or a copy where links are
        // unavailable), so the target itself never disappears.
        const fs::path first = backupPath(target_, 1);
        fs::create_hard_link(target_, first, ec);
        if (ec) {
            ec.clear();
            fs::copy_file(target_, first, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                removeQuietly(first);
                return ioError(first, "Cannot create backup '" + first.string() + "': " + ec.message());
            }
        }
        firstCreated_ = true;
        return {};
    }

    /// Restores the backup files to their state before perform().
    void undo() noexcept {
        std::error_code ec;
        if (firstCreated_) fs::remove(backupPath(target_, 1), ec);
        for (auto it = moved_.rbegin(); it != moved_.rend(); ++it) fs::rename(it->second, it->first, ec);
        if (!parked_.empty()) fs::rename(parked_, backupPath(target_, count_), ec);
        moved_.clear();
        parked_.clear();
        firstCreated_ = false;
    }

    /// Finalises the rotation after the target was replaced.
    void commit() noexcept {
        if (!parked_.empty()) removeQuietly(parked_);
    }

private:
    fs::path target_;
    int count_;
    fs::path parked_;
    std::vector<std::pair<fs::path, fs::path>> moved_;
    bool firstCreated_ = false;
};

core::Result<void> runHook(const SaveOptions& options, SaveStage stage) {
    if (!options.beforeStage) return {};
    return options.beforeStage(stage);
}

Document stamp(Document document, const SaveOptions& options) {
    if (document.savedAt.empty())
        document.savedAt =
            formatIso8601Utc(options.clock ? options.clock() : std::chrono::system_clock::now());
    return document;
}

} // namespace

const char* toString(SaveStage stage) noexcept {
    switch (stage) {
    case SaveStage::WriteTemp: return "WriteTemp";
    case SaveStage::Verify: return "Verify";
    case SaveStage::RotateBackups: return "RotateBackups";
    case SaveStage::Replace: return "Replace";
    }
    return "Unknown";
}

std::filesystem::path backupPath(const std::filesystem::path& path, int index) {
    return withSuffix(path, ".bak" + std::to_string(index));
}

std::filesystem::path autosavePath(const std::filesystem::path& path) { return withSuffix(path, ".autosave"); }

core::Result<void> writeFileDurably(const std::filesystem::path& file, std::string_view bytes) {
#ifdef _WIN32
    std::FILE* handle = ::_wfopen(file.c_str(), L"wb");
#else
    std::FILE* handle = std::fopen(file.c_str(), "wb");
#endif
    if (handle == nullptr) return ioError(file, "Cannot create '" + file.string() + "'", errno);
    bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), handle) == bytes.size();
    ok = ok && std::fflush(handle) == 0;
#ifdef _WIN32
    ok = ok && ::_commit(::_fileno(handle)) == 0;
#else
    ok = ok && ::fsync(::fileno(handle)) == 0;
#endif
    const int writeErrno = ok ? 0 : errno;
    const bool closed = std::fclose(handle) == 0;
    if (!ok) return ioError(file, "Cannot write " + std::to_string(bytes.size()) + " bytes to '" + file.string() + "'", writeErrno);
    if (!closed) return ioError(file, "Cannot finish writing '" + file.string() + "'", errno);
    return {};
}

core::Result<void> atomicWriteFile(const std::filesystem::path& target, std::string_view bytes,
                                   const SaveOptions& options, const ContentVerifier& verify) {
    const std::string name = target.string();
    if (options.backupCount < 0 || options.backupCount > kMaxBackupCount)
        return makeError(ErrorCode::InvalidArgument,
                         "backupCount must be between 0 and " + std::to_string(kMaxBackupCount) + ", got " +
                             std::to_string(options.backupCount) + ".",
                         name);
    std::error_code ec;
    if (target.filename().empty() || fs::is_directory(target, ec))
        return makeError(ErrorCode::IoError, "Cannot save to '" + name + "': it is not a file name.", name);
    const fs::path directory = target.parent_path().empty() ? fs::path(".") : target.parent_path();
    if (!fs::is_directory(directory, ec))
        return makeError(ErrorCode::IoError,
                         "Cannot save '" + name + "': the folder '" + directory.string() + "' does not exist.", name);

    const fs::path temp = withSuffix(target, ".tmp-" + randomSuffix());
    const auto fail = [&](core::Error error, SaveStage stage) {
        removeQuietly(temp);
        error.message = "Saving '" + name + "' failed (" + toString(stage) +
                        " stage); the existing file was left unchanged. " + error.message;
        if (error.context.empty()) error.context = name;
        return error;
    };

    // 1. temporary file
    if (auto r = runHook(options, SaveStage::WriteTemp); !r) return fail(r.error(), SaveStage::WriteTemp);
    if (auto r = options.writer ? options.writer(temp, bytes) : writeFileDurably(temp, bytes); !r)
        return fail(r.error(), SaveStage::WriteTemp);

    // 2. read back and verify
    if (auto r = runHook(options, SaveStage::Verify); !r) return fail(r.error(), SaveStage::Verify);
    // Bounded like every read; the limit only has to admit the bytes just written.
    auto readBack = readFile(temp, ReadLimits{std::uintmax_t{bytes.size()} + 4096});
    if (!readBack) return fail(readBack.error(), SaveStage::Verify);
    if (readBack.value() != bytes)
        return fail(makeError(ErrorCode::CorruptData,
                              "The temporary file holds " + std::to_string(readBack.value().size()) +
                                  " bytes that differ from the " + std::to_string(bytes.size()) +
                                  " bytes written (disk full or faulty storage?)."),
                    SaveStage::Verify);
    if (verify)
        if (auto r = verify(readBack.value()); !r) return fail(r.error(), SaveStage::Verify);

    // 3. backups
    BackupRotation rotation(target, options.backupCount);
    if (auto r = runHook(options, SaveStage::RotateBackups); !r) return fail(r.error(), SaveStage::RotateBackups);
    if (options.backupCount > 0 && fs::exists(target, ec)) {
        if (auto r = rotation.perform(); !r) {
            rotation.undo();
            return fail(r.error(), SaveStage::RotateBackups);
        }
    }

    // 4. replace
    if (auto r = runHook(options, SaveStage::Replace); !r) {
        rotation.undo();
        return fail(r.error(), SaveStage::Replace);
    }
    fs::rename(temp, target, ec);
    if (ec) {
        rotation.undo();
        return fail(ioError(target, "Cannot replace '" + name + "': " + ec.message()), SaveStage::Replace);
    }
    rotation.commit();
    syncDirectory(directory);
    return {};
}

core::Result<SaveReport> saveDocument(const std::filesystem::path& path, Document document,
                                      const SaveOptions& options) {
    const Document doc = stamp(std::move(document), options);
    auto text = serializeDocument(doc);
    if (!text) return text.error();
    auto checksum = payloadChecksum(doc.payload);
    if (!checksum) return checksum.error();

    // Re-parse what actually reached the disk and compare its checksum before replacing.
    FormatSpec selfSpec;
    selfSpec.kind = doc.format;
    selfSpec.currentVersion = doc.formatVersion;
    const std::string fileName = path.filename().string();
    const ContentVerifier verify = [&](std::string_view bytes) -> core::Result<void> {
        auto reread = parseDocument(bytes, selfSpec, {}, fileName);
        if (!reread) return reread.error();
        auto rereadChecksum = payloadChecksum(reread.value().document.payload);
        if (!rereadChecksum) return rereadChecksum.error();
        if (rereadChecksum.value() != checksum.value())
            return makeError(ErrorCode::CorruptData,
                             "The written file hashes to " + rereadChecksum.value() + " instead of " +
                                 checksum.value() + ".");
        return {};
    };
    if (auto r = atomicWriteFile(path, text.value(), options, verify); !r) return r.error();
    return SaveReport{path, doc.savedAt, checksum.value(), text.value().size()};
}

core::Result<SaveReport> writeAutosave(const std::filesystem::path& path, Document document,
                                       const SaveOptions& options) {
    SaveOptions autosaveOptions = options;
    autosaveOptions.backupCount = 0;
    return saveDocument(autosavePath(path), std::move(document), autosaveOptions);
}

core::Result<void> discardAutosave(const std::filesystem::path& path) {
    const fs::path file = autosavePath(path);
    std::error_code ec;
    fs::remove(file, ec);
    if (ec) return ioError(file, "Cannot delete autosave '" + file.string() + "': " + ec.message());
    return {};
}

} // namespace fos::formats
