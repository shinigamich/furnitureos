// SPDX-License-Identifier: AGPL-3.0-only
// Bounded reading of regular files (readFile) and loadDocument on top of it.
#include "fos/formats/container.hpp"

#include <cerrno>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <fstream>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fos::formats {
namespace {

namespace fs = std::filesystem;
using core::ErrorCode;
using core::makeError;

std::string kindOf(fs::file_type type) {
    switch (type) {
    case fs::file_type::fifo: return "a FIFO (named pipe)";
    case fs::file_type::character: return "a character device";
    case fs::file_type::block: return "a block device";
    case fs::file_type::socket: return "a socket";
    default: return "not a regular file";
    }
}

core::Error notRegular(const std::string& name, const std::string& kind) {
    return makeError(ErrorCode::IoError,
                     "'" + name + "' is " + kind + ", not a regular file; only regular files are read.", name);
}

core::Error tooLarge(const std::string& name, std::uintmax_t size, std::uintmax_t limit, bool whileReading) {
    return makeError(ErrorCode::ConstraintViolation,
                     whileReading ? "File '" + name + "' grew beyond the limit of " + std::to_string(limit) +
                                        " bytes while it was read; it was not loaded."
                                  : "File '" + name + "' is " + std::to_string(size) + " bytes; the limit is " +
                                        std::to_string(limit) + " bytes. It was not read.",
                     name);
}

constexpr std::size_t kChunk = 64 * 1024;

} // namespace

core::Result<std::string> readFile(const std::filesystem::path& path, const ReadLimits& limits) {
    const std::string name = path.string();
    std::error_code ec;
    const auto status = fs::status(path, ec);
    if (ec || !fs::exists(status)) return makeError(ErrorCode::IoError, "File '" + name + "' does not exist.", name);
    if (fs::is_directory(status))
        return makeError(ErrorCode::IoError, "'" + name + "' is a directory, not a file.", name);
    // Refuse special files before opening them: opening a FIFO for reading blocks until a
    // writer appears, and a device such as /dev/zero never ends.
    if (!fs::is_regular_file(status)) return notRegular(name, kindOf(status.type()));
    const std::uintmax_t limit = limits.maxBytes;

#ifdef _WIN32
    const auto size = fs::file_size(path, ec);
    if (ec) return makeError(ErrorCode::IoError, "Cannot determine the size of '" + name + "': " + ec.message() + ".", name);
    if (size > limit) return tooLarge(name, size, limit, false);
    std::ifstream in(path, std::ios::binary);
    if (!in) return makeError(ErrorCode::IoError, "File '" + name + "' cannot be opened for reading.", name);
    std::string data;
    std::string chunk(kChunk, '\0');
    while (in) {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        if (data.size() + got > limit) return tooLarge(name, 0, limit, true);
        data.append(chunk.data(), got);
    }
    if (in.bad()) return makeError(ErrorCode::IoError, "File '" + name + "' could not be read completely.", name);
    return data;
#else
    // O_NONBLOCK: if the path was swapped for a FIFO after the check above, open() still
    // returns at once and fstat() below refuses it. It has no effect on regular files.
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        const int err = errno;
        if (err == ENOENT) return makeError(ErrorCode::IoError, "File '" + name + "' does not exist.", name);
        return makeError(ErrorCode::IoError,
                         "File '" + name + "' cannot be opened for reading: " + std::strerror(err) + ".", name);
    }
    struct Closer {
        int fd;
        ~Closer() { ::close(fd); }
    } closer{fd};
    struct stat st {};
    if (::fstat(fd, &st) != 0)
        return makeError(ErrorCode::IoError, "File '" + name + "' cannot be examined: " + std::strerror(errno) + ".", name);
    if (S_ISDIR(st.st_mode)) return makeError(ErrorCode::IoError, "'" + name + "' is a directory, not a file.", name);
    if (!S_ISREG(st.st_mode)) {
        const fs::file_type type = S_ISFIFO(st.st_mode)  ? fs::file_type::fifo
                                   : S_ISCHR(st.st_mode) ? fs::file_type::character
                                   : S_ISBLK(st.st_mode) ? fs::file_type::block
                                   : S_ISSOCK(st.st_mode) ? fs::file_type::socket
                                                          : fs::file_type::unknown;
        return notRegular(name, kindOf(type));
    }
    const auto size = static_cast<std::uintmax_t>(st.st_size < 0 ? 0 : st.st_size);
    if (size > limit) return tooLarge(name, size, limit, false);
    std::string data;
    data.reserve(static_cast<std::size_t>(size));
    std::string chunk(kChunk, '\0');
    for (;;) {
        const ssize_t got = ::read(fd, chunk.data(), chunk.size());
        if (got < 0) {
            if (errno == EINTR) continue;
            return makeError(ErrorCode::IoError,
                             "File '" + name + "' could not be read completely: " + std::strerror(errno) + ".", name);
        }
        if (got == 0) break;
        if (data.size() + static_cast<std::size_t>(got) > limit) return tooLarge(name, 0, limit, true);
        data.append(chunk.data(), static_cast<std::size_t>(got));
    }
    return data;
#endif
}

core::Result<LoadedDocument> loadDocument(const std::filesystem::path& path, const FormatSpec& spec,
                                          const LoadOptions& options) {
    auto text = readFile(path, ReadLimits{options.maxFileBytes});
    if (!text) return text.error();
    return parseDocument(text.value(), spec, options, path.filename().string());
}

} // namespace fos::formats
