// SPDX-License-Identifier: AGPL-3.0-only
// FurnitureOS desktop application entry point.
#include "fos/app/relaunch.hpp"
#include "fos/app/version.hpp"
#include "main_window.hpp"
#include "settings.hpp"
#include "welcome_dialog.hpp"

#include <QApplication>
#include <QTimer>

#include <optional>

int main(int argc, char** argv) {
    using namespace fos::app::gui;
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QString::fromLatin1(fos::app::kOrganization));
    QApplication::setApplicationName(QString::fromLatin1(fos::app::kApplicationName));
    QApplication::setApplicationVersion(QString::fromLatin1(fos::app::applicationVersion()));

    // Language: Preferences, otherwise the system locale (Ukrainian -> uk, anything else -> en).
    Preferences preferences = Preferences::load();
    applyLanguage(effectiveLanguage(preferences));

    // Project file given on the command line (options such as "-style fusion" are skipped).
    const QString file = fos::app::projectFileArgument(QApplication::arguments());

    // First start (and not opening a file): the Welcome dialog before the main window, so a
    // language chosen there applies to everything at once (live retranslation of the dialog).
    std::optional<WelcomeChoice> welcomeChoice;
    if (!welcomeShown() && file.isEmpty()) {
        WelcomeDialog welcome(effectiveLanguage(preferences), /*liveLanguage=*/true);
        QObject::connect(&welcome, &WelcomeDialog::languageChanged, &welcome,
                         [](const QString& language) { applyLanguage(language); });
        welcome.exec();
        preferences.language = welcome.language(); // the language is now an explicit choice
        preferences.save();
        setWelcomeShown();
        welcomeChoice = welcome.choice();
    }

    MainWindow window; // reads the Preferences saved above
    window.show();
    if (!file.isEmpty()) window.openFile(file);
    QTimer::singleShot(0, &window, [&window, welcomeChoice] {
        window.offerStartupRecovery();
        if (welcomeChoice) window.runWelcomeAction(*welcomeChoice);
        // Opt-in update check: asks once, never checks without consent, offline use unaffected.
        QTimer::singleShot(1500, &window, &MainWindow::startupUpdateCheck);
    });
    return app.exec();
}
