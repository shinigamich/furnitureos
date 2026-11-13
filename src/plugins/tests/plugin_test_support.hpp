// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/formats/library.hpp"
#include "fos/plugins/manifest.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>

namespace fos::plugins::test {

namespace fs = std::filesystem;
using formats::Json;

/// Unique temporary directory, removed recursively on destruction.
class TempDir {
public:
    TempDir() {
        std::random_device device;
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = fs::temp_directory_path() /
                                   ("fos-plugins-test-" + std::to_string(device()) + "-" + std::to_string(device()));
            if (fs::create_directory(candidate)) {
                path_ = fs::canonical(candidate);
                return;
            }
        }
        throw std::runtime_error("cannot create a temporary directory");
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const fs::path& path() const { return path_; }
    fs::path operator/(const std::string& name) const { return path_ / name; }

private:
    fs::path path_;
};

inline void writeText(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

inline std::string readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// A valid manifest for `id` contributing `libraries`.
inline Json manifestJson(const std::string& id, const std::string& version = "1.0.0",
                         std::vector<std::string> libraries = {"lib/main.foslib"}) {
    Json m = Json::object();
    m["manifestVersion"] = 1;
    m["id"] = id;
    m["name"] = "Test plugin " + id;
    m["version"] = version;
    m["description"] = "Plugin used by the unit tests.";
    m["author"] = "Test author";
    m["license"] = "CC0-1.0";
    m["compatibility"] = Json{{"minAppVersion", "0.1.0"}};
    m["permissions"] = Json::array({"data"});
    m["contributes"] = Json{{"libraries", libraries}};
    return m;
}

inline domain::Material material(const std::string& id, const std::string& name = "Board", double thickness = 18.0) {
    domain::Material m;
    m.id = domain::MaterialId(id);
    m.name = name;
    m.thickness = thickness;
    return m;
}

inline domain::EdgeBand edgeBand(const std::string& id, const std::string& name = "Edge") {
    domain::EdgeBand e;
    e.id = domain::EdgeBandId(id);
    e.name = name;
    return e;
}

/// Library with one material "<id>:board" and one edge band "<id>:edge".
inline formats::Library namespacedLibrary(const std::string& pluginId, const std::string& suffix = "") {
    formats::Library lib;
    lib.materials.push_back(material(pluginId + ":board" + suffix, "Board " + pluginId));
    lib.edgeBands.push_back(edgeBand(pluginId + ":edge" + suffix, "Edge " + pluginId));
    return lib;
}

inline void writeLibrary(const fs::path& file, const formats::Library& lib) {
    fs::create_directories(file.parent_path());
    formats::SaveOptions o;
    o.backupCount = 0;
    auto r = formats::exportLibrary(file, lib, formats::AppInfo{"FurnitureOS", "0.1.0"}, o);
    ASSERT_TRUE(r.ok()) << r.error().message;
}

/// Creates <parent>/<id>/ with plugin.json (`manifest`) and lib/main.foslib.
inline fs::path makePlugin(const fs::path& parent, const std::string& id, const std::string& version = "1.0.0",
                           const formats::Library* lib = nullptr) {
    const fs::path dir = parent / id;
    writeText(dir / "plugin.json", manifestJson(id, version).dump(2));
    writeLibrary(dir / "lib/main.foslib", lib ? *lib : namespacedLibrary(id));
    return dir;
}

inline bool hasCode(const core::Diagnostics& ds, const std::string& code) {
    for (const auto& d : ds)
        if (d.code == code) return true;
    return false;
}

inline std::string allMessages(const core::Diagnostics& ds) {
    std::string s;
    for (const auto& d : ds) s += "[" + d.code + "] " + d.message + "\n";
    return s;
}

} // namespace fos::plugins::test
