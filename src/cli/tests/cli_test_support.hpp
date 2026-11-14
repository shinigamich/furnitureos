// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/cli/cli.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fos::cli::test {

struct RunResult {
    int code = -1;
    std::string out, err;
};

/// In-process `fos <args...>`.
inline RunResult fos(const std::vector<std::string>& args) {
    std::ostringstream out, err;
    RunResult r;
    r.code = run(args, out, err);
    r.out = out.str();
    r.err = err.str();
    return r;
}

inline bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

/// Path of a UTF-8 string (char8_t: UTF-8 with every standard library, unlike std::string
/// on MSVC, which uses the ANSI code page).
inline std::filesystem::path u8path(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

inline std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

/// Fresh temporary directory removed on destruction.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        std::random_device rd;
        path = std::filesystem::temp_directory_path() /
               ("fos_cli_test_" + std::to_string(rd()) + "_" + std::to_string(rd()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    /// UTF-8 path of `name` (UTF-8) inside the directory, as a command-line argument.
    std::string file(const std::string& name) const {
        const auto u8 = (path / std::filesystem::path(std::u8string(name.begin(), name.end()))).u8string();
        return std::string(u8.begin(), u8.end());
    }
};

inline constexpr const char* kTestUuid = "0b6f6a52-6a4e-4c8e-9a3b-6f1d2c3e4f50";
inline constexpr const char* kTestDate = "2026-11-13T09:00:00Z";

/// `fos new <file> --template base --width 800 --height 720` with a fixed id and date.
inline RunResult newBaseCabinet(const std::string& file) {
    return fos({"new", file, "--name", "Kitchen", "--author", "Test", "--template", "base", "--width", "800",
                "--height", "720", "--id", kTestUuid, "--date", kTestDate});
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

} // namespace fos::cli::test
