// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/recent_files.hpp"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace fos::app {
namespace {
QString normalise(const QString& path) {
    if (path.isEmpty()) return {};
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
} // namespace

RecentFiles::RecentFiles(int maxEntries) : max_(std::max(1, maxEntries)) {}

void RecentFiles::load(const QSettings& settings) {
    const QStringList stored = settings.value(QString::fromLatin1(kSettingsKey)).toStringList();
    entries_.clear();
    // Stored order is newest first; adding in reverse keeps it and removes duplicates.
    for (auto it = stored.crbegin(); it != stored.crend(); ++it) add(*it);
}

void RecentFiles::save(QSettings& settings) const { settings.setValue(QString::fromLatin1(kSettingsKey), entries_); }

void RecentFiles::add(const QString& path) {
    const QString p = normalise(path);
    if (p.isEmpty()) return;
    entries_.removeAll(p);
    entries_.prepend(p);
    while (entries_.size() > max_) entries_.removeLast();
}

void RecentFiles::remove(const QString& path) { entries_.removeAll(normalise(path)); }

void RecentFiles::clear() { entries_.clear(); }

int RecentFiles::pruneMissing() {
    const auto before = entries_.size();
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [](const QString& p) { return !QFileInfo::exists(p); }),
                   entries_.end());
    return static_cast<int>(before - entries_.size());
}

} // namespace fos::app
