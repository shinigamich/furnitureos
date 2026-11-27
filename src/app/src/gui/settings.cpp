// SPDX-License-Identifier: AGPL-3.0-only
#include "settings.hpp"

#include "fos/app/autosave.hpp"
#include "fos/app/component_browser.hpp"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QPointer>
#include <QSettings>
#include <QTranslator>

#include <algorithm>

namespace fos::app::gui {

Preferences Preferences::load() {
    QSettings s;
    Preferences p;
    p.language = s.value(QStringLiteral("ui/language"), QString()).toString();
    if (p.language != QLatin1String("en") && p.language != QLatin1String("uk")) p.language.clear();
    p.autosaveMinutes =
        std::clamp(s.value(QStringLiteral("files/autosaveMinutes"), kDefaultAutosaveMinutes).toInt(), 0, kMaxAutosaveMinutes);
    p.componentFolders = s.value(QStringLiteral("components/folders"), QStringList()).toStringList();
    p.componentFolders.removeAll(QString());
    return p;
}

void Preferences::save() const {
    QSettings s;
    s.setValue(QStringLiteral("ui/language"), language);
    s.setValue(QStringLiteral("files/autosaveMinutes"), autosaveMinutes);
    s.setValue(QStringLiteral("components/folders"), componentFolders);
}

LabelBarcodeSettings loadLabelBarcodeSettings() {
    QSettings s;
    return labelBarcodeSettings(s.value(QStringLiteral("export/labelBarcode")).toString(),
                                s.value(QStringLiteral("export/labelBarcodeContent")).toString());
}

void saveLabelBarcodeSettings(const LabelBarcodeSettings& settings) {
    QSettings s;
    s.setValue(QStringLiteral("export/labelBarcode"), labelBarcodeKey(settings.barcode));
    s.setValue(QStringLiteral("export/labelBarcodeContent"), labelBarcodeContentKey(settings.content));
}

QString effectiveLanguage(const Preferences& preferences) {
    if (!preferences.language.isEmpty()) return preferences.language;
    return QLocale::system().language() == QLocale::Ukrainian ? QStringLiteral("uk") : QStringLiteral("en");
}

void applyLanguage(const QString& language) {
    // Owned by the application object; replaced on every call.
    static QPointer<QTranslator> appTranslator, qtTranslator;
    for (QPointer<QTranslator>* t : {&appTranslator, &qtTranslator}) {
        if (*t) {
            QCoreApplication::removeTranslator(*t);
            delete t->data();
        }
    }
    if (language == QLatin1String("en")) {
        QLocale::setDefault(QLocale::system());
        return;
    }
    QLocale::setDefault(QLocale(language));
    auto* app = new QTranslator(QCoreApplication::instance());
    if (app->load(QStringLiteral(":/i18n/furnitureos_") + language)) {
        QCoreApplication::installTranslator(app);
        appTranslator = app;
    } else {
        delete app;
    }
    auto* qt = new QTranslator(QCoreApplication::instance());
    if (qt->load(QStringLiteral("qtbase_") + language, QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(qt);
        qtTranslator = qt;
    } else {
        delete qt;
    }
}

bool welcomeShown() { return QSettings().value(QStringLiteral("ui/welcomeShown"), false).toBool(); }

void setWelcomeShown() { QSettings().setValue(QStringLiteral("ui/welcomeShown"), true); }

std::vector<std::filesystem::path> effectiveComponentFolders(const Preferences& preferences) {
    std::vector<std::filesystem::path> out;
    for (const QString& f : preferences.componentFolders) out.emplace_back(f.toStdString());
    if (out.empty())
        if (auto d = defaultComponentFolder(true); !d.empty()) out.push_back(std::move(d));
    return out;
}

} // namespace fos::app::gui
