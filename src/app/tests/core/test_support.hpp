// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/app/session.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <random>

namespace fos::app::test {

/// Fresh temporary directory removed on destruction.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        std::random_device rd;
        path = std::filesystem::temp_directory_path() /
               ("fos_app_test_" + std::to_string(rd()) + std::to_string(rd()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

inline const domain::Part* part(const Session& s, const std::string& id) { return s.findPart(id); }

} // namespace fos::app::test
