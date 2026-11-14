// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

namespace fos::formats::test {

/// Unique temporary directory, removed (recursively) on destruction.
class TempDir {
public:
    TempDir() {
        std::random_device device;
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = std::filesystem::temp_directory_path() /
                                   ("fos-formats-test-" + std::to_string(device()) + "-" + std::to_string(device()));
            if (std::filesystem::create_directory(candidate)) {
                path_ = candidate;
                return;
            }
        }
        throw std::runtime_error("cannot create a temporary directory");
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }
    std::filesystem::path operator/(const std::string& name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

inline void writeText(const std::filesystem::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

inline std::string readText(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// "" when `dir` can hold a file with a Cyrillic name, otherwise why not. Always "" on Linux
/// (names are bytes) and on real Windows (NTFS stores UTF-16). Wine maps Windows names to
/// Unix names through the Unix locale's character set, so with LANG unset (POSIX/ASCII) it
/// cannot create such a file at all: a limitation of that test environment (run Wine with
/// LANG=C.UTF-8), not of the product, so tests skip with this reason instead of failing.
inline std::string nonAsciiFileNameSupport(const std::filesystem::path& dir) {
    const std::filesystem::path probe = dir / std::filesystem::path(u8"\u043f\u0440\u043e\u0431\u0430.txt"); // "проба.txt"
    {
        std::ofstream out(probe, std::ios::binary);
        if (!out)
            return "this file system cannot store non-ASCII file names (Wine with a non-UTF-8 Unix locale? run it with "
                   "LANG=C.UTF-8)";
    }
    std::error_code ec;
    const bool exists = std::filesystem::exists(probe, ec);
    std::filesystem::remove(probe, ec);
    return exists ? std::string() : "a file with a non-ASCII name was created but cannot be found again";
}

} // namespace fos::formats::test
