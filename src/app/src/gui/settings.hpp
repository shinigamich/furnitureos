// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Persistent user preferences (QSettings, organisation/application "FurnitureOS").
#include <QString>

namespace fos::app::gui {

struct Preferences {
    QString language;   ///< "" = follow the system locale, otherwise "en" or "uk".
    int autosaveMinutes = 5; ///< 0 = autosave off.

    static Preferences load();
    void save() const;
};

/// Language actually used: the stored choice, otherwise "uk" for a Ukrainian system locale
/// and "en" for everything else.
QString effectiveLanguage(const Preferences& preferences);

} // namespace fos::app::gui
