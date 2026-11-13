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
    std::string file(const std::string& name) const { return (path / name).string(); }
};

inline constexpr const char* kTestUuid = "0b6f6a52-6a4e-4c8e-9a3b-6f1d2c3e4f50";
inline constexpr const char* kTestDate = "2026-11-13T09:00:00Z";

/// `fos new <file> --template base --width 800 --height 720` with a fixed id and date.
inline RunResult newBaseCabinet(const std::string& file) {
    return fos({"new", file, "--name", "Kitchen", "--author", "Test", "--template", "base", "--width", "800",
                "--height", "720", "--id", kTestUuid, "--date", kTestDate});
}

} // namespace fos::cli::test
