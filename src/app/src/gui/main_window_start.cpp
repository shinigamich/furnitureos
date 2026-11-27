// SPDX-License-Identifier: AGPL-3.0-only
// Welcome dialog actions, bundled sample projects and restarting the application.
#include "fos/app/relaunch.hpp"
#include "fos/app/samples.hpp"
#include "main_window.hpp"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStatusBar>

namespace fos::app::gui {

void MainWindow::showWelcome() {
    const QString oldLanguage = effectiveLanguage(preferences_);
    WelcomeDialog dialog(oldLanguage, /*liveLanguage=*/false, this);
    dialog.exec();
    if (dialog.language() != oldLanguage) {
        preferences_.language = dialog.language();
        preferences_.save();
        offerRestartForLanguage();
        if (restarting_) return;
    }
    runWelcomeAction(dialog.choice());
}

void MainWindow::runWelcomeAction(const WelcomeChoice& choice) {
    switch (choice.action) {
    case WelcomeChoice::Action::None: return;
    case WelcomeChoice::Action::NewFromTemplate: newFromTemplateDialog(choice.templateKind); return;
    case WelcomeChoice::Action::OpenFile: openDialog(); return;
    case WelcomeChoice::Action::OpenSample:
        if (maybeSave()) openSample(choice.sampleKey);
        return;
    }
}

bool MainWindow::openSample(const QString& key) {
    for (const SampleProject& sample : sampleProjects()) {
        if (sample.key != key) continue;
        const QByteArray text = sampleProjectText(sample);
        if (text.isEmpty()) break;
        auto r = session_.openUntitledCopy(std::string_view(text.constData(), static_cast<std::size_t>(text.size())),
                                           sample.resourcePath.toStdString());
        if (!r) {
            showError(tr("Open sample project"), r.error());
            return false;
        }
        statusBar()->showMessage(tr("Opened the sample \"%1\" as a new project; Save asks where to keep it.")
                                     .arg(sample.title),
                                 8000);
        return true;
    }
    QMessageBox::critical(this, tr("Open sample project"), tr("The sample project \"%1\" is missing.").arg(key));
    return false;
}

void MainWindow::offerRestartForLanguage() {
    QMessageBox box(QMessageBox::Question, tr("Language"),
                    tr("The new language is used after FurnitureOS restarts. Restart now?"), QMessageBox::NoButton,
                    this);
    QPushButton* restart = box.addButton(tr("Restart now"), QMessageBox::AcceptRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.setDefaultButton(restart);
    box.exec();
    if (box.clickedButton() == restart) restartApplication();
}

bool MainWindow::restartApplication() {
    preferences_.save();
    // The normal close flow (closeEvent: unsaved batch / project -> Save, Discard or Cancel).
    // Closing the last window must not end the event loop yet: the restart may still fail.
    const bool quitOnLast = QApplication::quitOnLastWindowClosed();
    QApplication::setQuitOnLastWindowClosed(false);
    const bool closed = close();
    QApplication::setQuitOnLastWindowClosed(quitOnLast);
    if (!closed) return false; // cancelled: stay as we are

    // Reopen the document as it is on disk now (saved, or the file whose changes were discarded).
    const QString document =
        session_.filePath().empty() ? QString()
                                    : QFileInfo(QString::fromStdU16String(session_.filePath().u16string())).absoluteFilePath();
    const QString program =
        relaunchProgram(QCoreApplication::applicationFilePath(), qEnvironmentVariable("APPIMAGE"));
    const QStringList arguments = relaunchArguments(QCoreApplication::arguments(), document);
    if (!QProcess::startDetached(program, arguments, QDir::currentPath())) {
        show();
        QMessageBox::warning(this, tr("Restart"),
                             tr("FurnitureOS could not be started again (%1). Your settings are saved; close the "
                                "application and start it again to use them.")
                                 .arg(QDir::toNativeSeparators(program)));
        return false;
    }
    restarting_ = true;
    // exit(), not quit(): the windows were already asked; no second round of close events.
    QCoreApplication::exit(0);
    return true;
}

} // namespace fos::app::gui
