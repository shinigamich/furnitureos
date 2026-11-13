// SPDX-License-Identifier: AGPL-3.0-only
// FurnitureOS desktop application entry point.
#include "fos/app/version.hpp"
#include "main_window.hpp"
#include "settings.hpp"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTimer>
#include <QTranslator>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QString::fromLatin1(fos::app::kOrganization));
    QApplication::setApplicationName(QString::fromLatin1(fos::app::kApplicationName));
    QApplication::setApplicationVersion(QString::fromLatin1(fos::app::applicationVersion()));

    // Language: Preferences, otherwise the system locale (uk / en). Applied at start-up.
    const QString language = fos::app::gui::effectiveLanguage(fos::app::gui::Preferences::load());
    QTranslator appTranslator, qtTranslator;
    if (language != QLatin1String("en")) {
        if (appTranslator.load(QStringLiteral(":/i18n/furnitureos_") + language)) app.installTranslator(&appTranslator);
        if (qtTranslator.load(QStringLiteral("qtbase_") + language, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            app.installTranslator(&qtTranslator);
        QLocale::setDefault(QLocale(language));
    }

    fos::app::gui::MainWindow window;
    window.show();
    const QStringList args = app.arguments();
    if (args.size() > 1) window.openFile(args.at(1));
    else QTimer::singleShot(0, &window, &fos::app::gui::MainWindow::offerUntitledRecovery);
    return app.exec();
}
