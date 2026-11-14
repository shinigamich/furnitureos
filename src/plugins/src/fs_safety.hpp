// SPDX-License-Identifier: AGPL-3.0-only
// Link-aware file-system primitives used by the plugin package checks (internal).
//
// Why not plain std::filesystem: libstdc++ on Windows (MinGW-w64) implements symlink_status()
// with a stat() that follows links, never reports is_symlink(), and canonical() does not
// resolve symbolic links, directory junctions or mount points. A junction can be created by
// any user without privileges ("mklink /J"), so relying on those functions would let a
// plugin directory point outside itself and would let remove_all() descend into, and delete,
// the junction's target. These helpers use the Win32 API on Windows and std::filesystem
// elsewhere.
#pragma once

#include <cstdint>
#include <filesystem>
#include <system_error>

namespace fos::plugins::detail {

/// True when `p` itself (not what it points to) is a link: a symbolic link on POSIX; on
/// Windows any name-surrogate reparse point (symbolic link, directory junction, mount point,
/// WSL symlink). Placeholder reparse points such as cloud files are not links. False with
/// `ec` set when `p` cannot be inspected; false with `ec` clear when it does not exist.
bool isLink(const std::filesystem::path& p, std::error_code& ec);

/// Absolute path of `p` with every link resolved, for containment checks. POSIX:
/// std::filesystem::canonical. Windows: GetFinalPathNameByHandleW (the "\\?\" prefix is
/// removed, "\\?\UNC\server\share" becomes "\\server\share"). Empty with `ec` set on failure.
std::filesystem::path resolvePath(const std::filesystem::path& p, std::error_code& ec);

/// Component-wise equality of two resolved paths (case-insensitive on Windows, as NTFS is).
bool samePath(const std::filesystem::path& a, const std::filesystem::path& b);

/// True when `inner` is strictly below `outer` (both resolved), compared component-wise,
/// case-insensitively on Windows.
bool isStrictlyInside(const std::filesystem::path& outer, const std::filesystem::path& inner);

/// Removes `p` and, if it is a real directory, everything below it. Links are removed as
/// links and never followed (on every platform, including Windows junctions). Returns the
/// number of entries removed; a missing `p` removes nothing and is not an error.
std::uintmax_t removeTreeNoFollow(const std::filesystem::path& p, std::error_code& ec);

} // namespace fos::plugins::detail
