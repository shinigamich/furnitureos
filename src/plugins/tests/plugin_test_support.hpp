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
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#include <cstring>
#endif

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

#ifdef _WIN32
/// Creates a directory junction (IO_REPARSE_TAG_MOUNT_POINT) `link` -> `target`. Unlike a
/// symbolic link this needs no privilege on Windows, which is exactly why the product code
/// must treat junctions as links. Returns "" on success or the reason it failed.
inline std::string createJunction(const fs::path& target, const fs::path& link) {
    std::error_code ec;
    if (!fs::create_directory(link, ec)) return "cannot create the junction directory: " + ec.message();
    const std::wstring substitute = L"\\??\\" + fs::absolute(target).wstring();
    const std::wstring print = fs::absolute(target).wstring();
    // REPARSE_DATA_BUFFER.MountPointReparseBuffer, laid out by hand (the struct is DDK-only).
    const std::size_t pathBytes = (substitute.size() + 1 + print.size() + 1) * sizeof(wchar_t);
    std::vector<char> buffer(8 + 8 + pathBytes, 0);
    auto put16 = [&](std::size_t at, std::size_t v) {
        const auto w = static_cast<WORD>(v);
        std::memcpy(buffer.data() + at, &w, sizeof w);
    };
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    std::memcpy(buffer.data(), &tag, sizeof tag);
    put16(4, 8 + pathBytes);                                    // ReparseDataLength
    put16(8, 0);                                                // SubstituteNameOffset
    put16(10, substitute.size() * sizeof(wchar_t));             // SubstituteNameLength
    put16(12, (substitute.size() + 1) * sizeof(wchar_t));       // PrintNameOffset
    put16(14, print.size() * sizeof(wchar_t));                  // PrintNameLength
    std::memcpy(buffer.data() + 16, substitute.c_str(), (substitute.size() + 1) * sizeof(wchar_t));
    std::memcpy(buffer.data() + 16 + (substitute.size() + 1) * sizeof(wchar_t), print.c_str(),
                (print.size() + 1) * sizeof(wchar_t));
    HANDLE h = ::CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "cannot open the junction directory (error " + std::to_string(::GetLastError()) + ")";
    DWORD returned = 0;
    const BOOL ok = ::DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr, 0,
                                      &returned, nullptr);
    const DWORD error = ok ? 0 : ::GetLastError();
    ::CloseHandle(h);
    if (!ok) {
        fs::remove(link, ec);
        return "FSCTL_SET_REPARSE_POINT failed (error " + std::to_string(error) +
               (error == ERROR_NOT_SUPPORTED ? ": not supported; Wine 9 implements no reparse points" : "") + ")";
    }
    return {};
}
#endif

/// Creates a link `link` -> `target` that the product code must treat as a link. Returns ""
/// on success, otherwise why the platform cannot create one here (the caller then skips the
/// link-specific checks with GTEST_SKIP and that reason; the product checks themselves do
/// not depend on the test being able to create links).
///  * POSIX: a symbolic link.
///  * Windows: CreateSymbolicLinkW (libstdc++'s std::filesystem::create_symlink is not
///    implemented on MinGW-w64 and always fails with ENOSYS). Without Developer Mode or
///    SeCreateSymbolicLinkPrivilege that fails; a directory then falls back to a junction,
///    which needs no privilege. The result is verified, because Wine 9 reports success from
///    CreateSymbolicLinkW without creating anything (a Wine limitation, not Windows
///    behaviour).
inline std::string tryCreateLink(const fs::path& target, const fs::path& link, bool directory) {
#ifdef _WIN32
    DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (directory) flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    std::string why;
    if (::CreateSymbolicLinkW(link.c_str(), target.c_str(), flags)) {
        const DWORD attributes = ::GetFileAttributesW(link.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return {};
        why = "CreateSymbolicLinkW reported success but no link exists (Wine 9 stubs symbolic links)";
    } else {
        why = "CreateSymbolicLinkW failed with error " + std::to_string(::GetLastError()) +
              " (symbolic links need Developer Mode or SeCreateSymbolicLinkPrivilege)";
    }
    if (directory) {
        const std::string junction = createJunction(target, link);
        if (junction.empty()) return {};
        why += "; junction fallback: " + junction;
    }
    return "cannot create a " + std::string(directory ? "directory" : "file") + " link on this system: " + why;
#else
    std::error_code ec;
    if (directory) fs::create_directory_symlink(target, link, ec);
    else fs::create_symlink(target, link, ec);
    if (ec) return "cannot create a symbolic link on this system: " + ec.message();
    return {};
#endif
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
