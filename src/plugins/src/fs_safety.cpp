// SPDX-License-Identifier: AGPL-3.0-only
#include "fs_safety.hpp"

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
#endif

namespace fos::plugins::detail {
namespace fs = std::filesystem;

#ifdef _WIN32
namespace {

std::error_code lastError() { return {static_cast<int>(::GetLastError()), std::system_category()}; }

bool isMissing(DWORD error) { return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND; }

/// Owns a HANDLE from CreateFileW.
class Handle {
public:
    explicit Handle(HANDLE h) : h_(h) {}
    ~Handle() {
        if (h_ != INVALID_HANDLE_VALUE) ::CloseHandle(h_);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return h_; }
    bool valid() const { return h_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE h_;
};

/// Opens `p` for attribute queries only (no read/write access, all sharing modes, so it never
/// conflicts with other users of the file). FILE_FLAG_BACKUP_SEMANTICS is required for
/// directories; `noFollow` opens a reparse point itself instead of its target.
HANDLE openForQuery(const fs::path& p, bool noFollow) {
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
    if (noFollow) flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    return ::CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, flags, nullptr);
}

bool equalNoCase(const fs::path& a, const fs::path& b) {
    const std::wstring& x = a.native();
    const std::wstring& y = b.native();
    return ::CompareStringOrdinal(x.c_str(), static_cast<int>(x.size()), y.c_str(), static_cast<int>(y.size()), TRUE) ==
           CSTR_EQUAL;
}

} // namespace

bool isLink(const fs::path& p, std::error_code& ec) {
    ec.clear();
    const DWORD attributes = ::GetFileAttributesW(p.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = ::GetLastError();
        if (!isMissing(error)) ec = {static_cast<int>(error), std::system_category()};
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) return false;
    Handle h(openForQuery(p, true));
    if (!h.valid()) {
        ec = lastError();
        return false;
    }
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (!::GetFileInformationByHandleEx(h.get(), FileAttributeTagInfo, &info, sizeof info)) {
        ec = lastError();
        return false;
    }
    return IsReparseTagNameSurrogate(info.ReparseTag);
}

fs::path resolvePath(const fs::path& p, std::error_code& ec) {
    ec.clear();
    Handle h(openForQuery(p, false));
    if (!h.valid()) {
        ec = lastError();
        return {};
    }
    std::wstring buffer(MAX_PATH, L'\0');
    for (int attempt = 0; attempt < 2; ++attempt) {
        const DWORD n = ::GetFinalPathNameByHandleW(h.get(), buffer.data(), static_cast<DWORD>(buffer.size()),
                                                    FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (n == 0) {
            ec = lastError();
            return {};
        }
        if (n < buffer.size()) {
            buffer.resize(n);
            break;
        }
        buffer.assign(n, L'\0'); // n includes the terminating NUL when the buffer was too small
    }
    static const std::wstring kUnc = L"\\\\?\\UNC\\";
    static const std::wstring kLong = L"\\\\?\\";
    if (buffer.compare(0, kUnc.size(), kUnc) == 0) return fs::path(L"\\\\" + buffer.substr(kUnc.size()));
    if (buffer.compare(0, kLong.size(), kLong) == 0) return fs::path(buffer.substr(kLong.size()));
    return fs::path(buffer);
}

bool samePath(const fs::path& a, const fs::path& b) {
    auto i = a.begin();
    auto j = b.begin();
    for (; i != a.end() && j != b.end(); ++i, ++j)
        if (!equalNoCase(*i, *j)) return false;
    return i == a.end() && j == b.end();
}

bool isStrictlyInside(const fs::path& outer, const fs::path& inner) {
    auto o = outer.begin();
    auto i = inner.begin();
    for (; o != outer.end(); ++o, ++i)
        if (i == inner.end() || !equalNoCase(*o, *i)) return false;
    return i != inner.end();
}

std::uintmax_t removeTreeNoFollow(const fs::path& p, std::error_code& ec) {
    ec.clear();
    const DWORD attributes = ::GetFileAttributesW(p.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = ::GetLastError();
        if (!isMissing(error)) ec = {static_cast<int>(error), std::system_category()};
        return 0;
    }
    const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const bool link = isLink(p, ec);
    if (ec) return 0;
    std::uintmax_t count = 0;
    if (directory && !link) {
        // Collect first, then delete: never modify a directory while iterating it.
        std::vector<fs::path> children;
        for (fs::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec)) children.push_back(it->path());
        if (ec) return count;
        for (const auto& child : children) {
            count += removeTreeNoFollow(child, ec);
            if (ec) return count;
        }
    }
    if (attributes & FILE_ATTRIBUTE_READONLY)
        ::SetFileAttributesW(p.c_str(), attributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
    // A directory link (junction or directory symlink) is removed with RemoveDirectoryW,
    // which deletes the link and not the target's contents.
    const BOOL removed = directory ? ::RemoveDirectoryW(p.c_str()) : ::DeleteFileW(p.c_str());
    if (!removed) {
        ec = lastError();
        return count;
    }
    return count + 1;
}

#else // POSIX

bool isLink(const fs::path& p, std::error_code& ec) {
    const auto st = fs::symlink_status(p, ec);
    if (ec == std::errc::no_such_file_or_directory) ec.clear();
    return !ec && fs::is_symlink(st);
}

fs::path resolvePath(const fs::path& p, std::error_code& ec) { return fs::canonical(p, ec); }

bool samePath(const fs::path& a, const fs::path& b) { return a == b; }

bool isStrictlyInside(const fs::path& outer, const fs::path& inner) {
    auto o = outer.begin();
    auto i = inner.begin();
    for (; o != outer.end(); ++o, ++i)
        if (i == inner.end() || *o != *i) return false;
    return i != inner.end();
}

std::uintmax_t removeTreeNoFollow(const fs::path& p, std::error_code& ec) {
    // POSIX remove_all() removes symbolic links without following them.
    const auto n = fs::remove_all(p, ec);
    return n == static_cast<std::uintmax_t>(-1) ? 0 : n;
}

#endif

} // namespace fos::plugins::detail
