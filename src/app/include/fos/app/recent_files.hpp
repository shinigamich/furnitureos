// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Most-recently-used list of project files, persisted in QSettings.
#include <QSettings>
#include <QStringList>

namespace fos::app {

class RecentFiles {
public:
    /// `maxEntries` >= 1 (smaller values are treated as 1).
    explicit RecentFiles(int maxEntries = 10);

    /// Replaces the list by the one stored under kSettingsKey (missing key = empty list);
    /// duplicates are removed and the list is cut to maxEntries().
    void load(const QSettings& settings);
    void save(QSettings& settings) const;

    /// Puts the absolute, cleaned form of `path` first (moving it when already listed) and
    /// drops the oldest entries beyond maxEntries(). Empty paths are ignored.
    void add(const QString& path);
    void remove(const QString& path);
    void clear();
    /// Removes entries whose file no longer exists; returns how many were removed.
    int pruneMissing();

    const QStringList& entries() const noexcept { return entries_; }
    int maxEntries() const noexcept { return max_; }

    static constexpr const char* kSettingsKey = "files/recent";

private:
    QStringList entries_;
    int max_;
};

} // namespace fos::app
