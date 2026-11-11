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

} // namespace fos::formats::test
