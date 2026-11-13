// SPDX-License-Identifier: AGPL-3.0-only
#include "settings.hpp"

#include "fos/app/autosave.hpp"

#include <QLocale>
#include <QSettings>

#include <algorithm>

namespace fos::app::gui {

Preferences Preferences::load() {
    QSettings s;
    Preferences p;
    p.language = s.value(QStringLiteral("ui/language"), QString()).toString();
    if (p.language != QLatin1String("en") && p.language != QLatin1String("uk")) p.language.clear();
    p.autosaveMinutes =
        std::clamp(s.value(QStringLiteral("files/autosaveMinutes"), kDefaultAutosaveMinutes).toInt(), 0, kMaxAutosaveMinutes);
    return p;
}

void Preferences::save() const {
    QSettings s;
    s.setValue(QStringLiteral("ui/language"), language);
    s.setValue(QStringLiteral("files/autosaveMinutes"), autosaveMinutes);
}

QString effectiveLanguage(const Preferences& preferences) {
    if (!preferences.language.isEmpty()) return preferences.language;
    return QLocale::system().language() == QLocale::Ukrainian ? QStringLiteral("uk") : QStringLiteral("en");
}

} // namespace fos::app::gui
