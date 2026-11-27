// SPDX-License-Identifier: AGPL-3.0-only
#include "update_controller.hpp"

#include "settings.hpp"
#include "update_dialog.hpp"

#include "fos/app/update_zip.hpp"
#include "fos/app/version.hpp"

#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>

#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace fos::app::gui {
namespace {

constexpr int kApiTimeoutMs = 30000;
constexpr int kDownloadStallMs = 60000;
const char* kSkippedKey = "updates/skippedVersion";
const char* kResultFile = "update-result.txt";

QString fromStd(const std::string& s) { return QString::fromStdString(s); }

QNetworkRequest makeRequest(const QUrl& url, bool api, int timeoutMs) {
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUpdateUserAgent));
    if (api) {
        r.setRawHeader("Accept", "application/vnd.github+json");
        r.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    }
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    r.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    r.setTransferTimeout(timeoutMs);
    return r;
}

SemVer currentVersion() { return parseSemVer(applicationVersion()).value_or(SemVer{}); }

} // namespace

UpdateController::UpdateController(QMainWindow* window)
    : QObject(window), window_(window), network_(new QNetworkAccessManager(this)) {}

QString UpdateController::cacheFolder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("updates"));
}

void UpdateController::checkNow() { startCheck(true); }

void UpdateController::startupCheck() {
    showPreviousOutcome();
    Preferences prefs = Preferences::load();
    if (shouldAskUpdateConsent(prefs.updateConsent)) {
        const auto answer = QMessageBox::question(
            window_, tr("Check for updates"),
            tr("Should FurnitureOS check for a new version each time it starts?\n\n"
               "It asks GitHub for the list of published FurnitureOS releases; no personal data and no usage "
               "data are sent. Nothing is downloaded or installed without your confirmation, and FurnitureOS "
               "works fully offline either way. You can change this later in Preferences."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        prefs = Preferences::load(); // re-read: keep anything changed meanwhile
        prefs.updateConsent = answer == QMessageBox::Yes ? UpdateConsent::Allowed : UpdateConsent::Declined;
        prefs.save();
    }
    if (shouldCheckAtStartup(prefs.updateConsent)) startCheck(false);
}

void UpdateController::showPreviousOutcome() {
    const QString path = QDir(cacheFolder()).filePath(QLatin1String(kResultFile));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const auto outcome = parseUpdateOutcome(QString::fromUtf8(f.readAll()));
    f.close();
    QFile::remove(path);
    QFile::remove(QDir(cacheFolder()).filePath(QStringLiteral("apply-update.cmd")));
    if (!outcome) return;
    if (outcome->ok)
        QMessageBox::information(window_, tr("Update"), tr("FurnitureOS was updated to version %1.").arg(outcome->detail));
    else
        QMessageBox::warning(window_, tr("Update"),
                             tr("The last update could not be installed:\n%1").arg(outcome->detail));
}

void UpdateController::startCheck(bool interactive) {
    if (busy_) {
        if (interactive) QMessageBox::information(window_, tr("Check for updates"), tr("An update check is already running."));
        return;
    }
    busy_ = true;
    interactive_ = interactive;
    if (window_) window_->statusBar()->showMessage(tr("Checking for updates…"), 5000);
    const bool pre = Preferences::load().includePrereleases;
    fetch(releasesApiUrl(pre), true, 4 * 1024 * 1024, [this, pre](const QByteArray& body) {
        const auto releases = parseGitHubReleases(body);
        if (!releases) return fail(tr("The release list could not be read."), fromStd(releases.error().message));
        const auto release = selectUpdate(releases.value(), currentVersion(), pre);
        if (!release) {
            finishCheck();
            if (interactive_)
                QMessageBox::information(window_, tr("Check for updates"),
                                         tr("FurnitureOS %1 is the newest version.").arg(QString::fromLatin1(applicationVersion())));
            return;
        }
        const QString version = fromStd(release->version.toString());
        if (!interactive_ && QSettings().value(QLatin1String(kSkippedKey)).toString() == version) return finishCheck();
        const ReleaseAsset* manifestAsset = release->findAsset(QLatin1String(kUpdateManifestName));
        const ReleaseInfo rel = *release;
        fetch(manifestAsset->downloadUrl, false, 1024 * 1024, [this, rel](const QByteArray& json) {
            const auto manifest = parseUpdateManifest(json, rel);
            if (!manifest) return fail(tr("The update description (update.json) is invalid."), fromStd(manifest.error().message));
            finishCheck();
            offer(rel, manifest.value());
        });
    });
}

void UpdateController::fetch(const QUrl& url, bool api, qint64 maxBytes, BodyHandler onBody) {
    QNetworkReply* reply = network_->get(makeRequest(url, api, kApiTimeoutMs));
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes](qint64 received, qint64) {
        if (received > maxBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, maxBytes, onBody = std::move(onBody)] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return fail(tr("Could not reach %1.").arg(url.host()), replyError(reply));
        const QByteArray body = reply->readAll();
        if (body.size() > maxBytes) return fail(tr("Could not reach %1.").arg(url.host()), tr("The answer is unexpectedly large."));
        onBody(body);
    });
}

QString UpdateController::replyError(QNetworkReply* reply) const {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if ((status == 403 || status == 429) && reply->rawHeader("X-RateLimit-Remaining") == "0")
        return tr("GitHub's limit for anonymous requests from your network was reached. Try again in an hour.");
    if (status == 404) return tr("Nothing is published there (HTTP 404).");
    if (reply->error() == QNetworkReply::OperationCanceledError) return tr("The request was cancelled or timed out.");
    if (status >= 400) return tr("The server answered HTTP %1 (%2).").arg(status).arg(reply->errorString());
    return reply->errorString();
}

void UpdateController::finishCheck() { busy_ = false; }

void UpdateController::fail(const QString& what, const QString& detail) {
    busy_ = false;
    if (interactive_) {
        QMessageBox::warning(window_, tr("Check for updates"),
                             what + QStringLiteral("\n\n") + detail + QStringLiteral("\n\n") +
                                 tr("FurnitureOS keeps working offline; you can try again later."));
    } else if (window_) {
        // An automatic check never interrupts work with a dialog.
        window_->statusBar()->showMessage(tr("Update check failed: %1").arg(what), 10000);
    }
}

void UpdateController::offer(const ReleaseInfo& release, const UpdateManifest& manifest) {
    const InstallKind kind = detectInstallKind(QCoreApplication::applicationDirPath(),
#ifdef Q_OS_WIN
                                               true
#else
                                               false
#endif
    );
    const UpdateChoice choice = chooseUpdate(kind, manifest);
    UpdateDialog dialog(release, manifest, choice, QString::fromLatin1(applicationVersion()), window_);
    dialog.exec();
    const QString version = fromStd(manifest.version.toString());
    switch (dialog.choice()) {
    case UpdateDialog::Choice::Later: break;
    case UpdateDialog::Choice::Skip: QSettings().setValue(QLatin1String(kSkippedKey), version); break;
    case UpdateDialog::Choice::OpenReleasePage: QDesktopServices::openUrl(QUrl(release.htmlUrl)); break;
    case UpdateDialog::Choice::UpdateNow: downloadAndApply(release, manifest, choice); break;
    }
}

void UpdateController::downloadAndApply(const ReleaseInfo& release, const UpdateManifest& manifest,
                                        const UpdateChoice& choice) {
    if (!choice.asset) return;
    busy_ = true;
    interactive_ = true;
    const ReleaseAsset* sumsAsset = release.findAsset(QLatin1String(kUpdateChecksumsName));
    const UpdateAsset asset = *choice.asset;
    const UpdateMethod method = choice.method;
    fetch(sumsAsset->downloadUrl, false, 1024 * 1024, [this, asset, method, manifest](const QByteArray& text) {
        const auto sums = parseSha256Sums(text);
        if (!sums) return fail(tr("The checksum list (SHA256SUMS.txt) is invalid."), fromStd(sums.error().message));
        // Refuse before downloading when the two published checksums disagree.
        if (auto agree = checkChecksumsAgree(asset, sums.value()); !agree)
            return fail(tr("The published checksums are inconsistent."), fromStd(agree.error().message));
        download(asset, sums.value(), [this, method, manifest](const QString& path) {
            busy_ = false;
            if (method == UpdateMethod::SwapPortable) applyPortable(manifest, path);
            else applyInstaller(manifest, path);
        });
    });
}

void UpdateController::download(const UpdateAsset& asset, const std::map<QString, QString>& sums,
                                std::function<void(const QString&)> onVerified) {
    if (!QDir().mkpath(cacheFolder()))
        return fail(tr("The download folder could not be created."), cacheFolder());
    const QString finalPath = QDir(cacheFolder()).filePath(asset.name);
    const QString partPath = finalPath + QStringLiteral(".part");
    auto file = std::make_shared<QFile>(partPath);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(tr("The download could not be saved."), tr("%1: %2").arg(partPath, file->errorString()));
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    auto failure = std::make_shared<QString>();

    auto* progress = new QProgressDialog(tr("Downloading %1…").arg(asset.name), tr("Cancel"), 0, 1000, window_);
    progress->setWindowTitle(tr("Update"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setValue(0);

    QNetworkReply* reply = network_->get(makeRequest(asset.downloadUrl, false, kDownloadStallMs));
    connect(progress, &QProgressDialog::canceled, reply, [reply, failure, this] {
        *failure = tr("The download was cancelled.");
        reply->abort();
    });
    connect(reply, &QNetworkReply::readyRead, reply, [reply, file, hash, failure, asset, this] {
        const QByteArray chunk = reply->readAll();
        if (file->pos() + chunk.size() > asset.size) {
            *failure = tr("The server sent more than the expected %1 bytes.").arg(asset.size);
            reply->abort();
            return;
        }
        if (file->write(chunk) != chunk.size()) {
            *failure = tr("Writing %1 failed: %2").arg(file->fileName(), file->errorString());
            reply->abort();
            return;
        }
        hash->addData(chunk);
    });
    connect(reply, &QNetworkReply::downloadProgress, progress, [progress, asset](qint64 received, qint64) {
        progress->setValue(static_cast<int>(std::min<qint64>(1000, received * 1000 / std::max<qint64>(1, asset.size))));
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file, hash, failure, progress, asset, sums, finalPath, partPath, onVerified = std::move(onVerified)] {
                reply->deleteLater();
                progress->deleteLater();
                if (failure->isEmpty() && reply->error() == QNetworkReply::NoError) {
                    const QByteArray rest = reply->readAll();
                    if (file->pos() + rest.size() <= asset.size && file->write(rest) == rest.size()) hash->addData(rest);
                    else *failure = tr("The download is larger than expected or could not be written.");
                }
                const qint64 size = file->size();
                file->close();
                if (failure->isEmpty() && reply->error() != QNetworkReply::NoError) *failure = replyError(reply);
                if (failure->isEmpty()) {
                    const auto ok = verifyDownload(size, QString::fromLatin1(hash->result().toHex()), asset, sums);
                    if (!ok) *failure = fromStd(ok.error().message);
                }
                if (!failure->isEmpty()) {
                    QFile::remove(partPath); // never keep an unverified file
                    return fail(tr("The update was not installed."), *failure);
                }
                QFile::remove(finalPath);
                if (!QFile::rename(partPath, finalPath)) {
                    QFile::remove(partPath);
                    return fail(tr("The update was not installed."), tr("The verified download could not be renamed to %1.").arg(finalPath));
                }
                onVerified(finalPath);
            });
}

void UpdateController::applyPortable(const UpdateManifest& manifest, const QString& zipPath) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const auto plan = planPortableSwap(appDir, QCoreApplication::applicationFilePath(), manifest.version);
    const auto refuse = [this, &zipPath](const QString& detail) {
        QFile::remove(zipPath);
        QMessageBox::warning(window_, tr("Update"), tr("The update was not installed; nothing was changed.") +
                                                         QStringLiteral("\n\n") + detail);
    };
    if (!plan) return refuse(fromStd(plan.error().message));
    const PortableSwapPlan& p = plan.value();
    if (!QDir().mkpath(p.stagingDir))
        return refuse(tr("The folder %1 is not writable, so the new version cannot be unpacked next to the program. "
                         "Move FurnitureOS to a folder you can write to, or download the release manually.")
                          .arg(QDir::toNativeSeparators(QFileInfo(p.stagingDir).absolutePath())));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (window_) window_->statusBar()->showMessage(tr("Unpacking the update…"));
    QCoreApplication::processEvents();
    const auto extracted = extractZip(zipPath, p.stagingDir);
    QApplication::restoreOverrideCursor();
    if (window_) window_->statusBar()->clearMessage();
    const auto removeStaging = [&p] { QDir(p.stagingDir).removeRecursively(); };
    if (!extracted) {
        removeStaging();
        return refuse(fromStd(extracted.error().message));
    }
    const auto root = locateStagedRoot(p.stagingDir, p.exeRelative);
    if (!root) {
        removeStaging();
        return refuse(fromStd(root.error().message));
    }
    QFile::remove(zipPath); // unpacked; the archive is no longer needed
    const QString resultFile = QDir(cacheFolder()).filePath(QLatin1String(kResultFile));
    const QString script = portableSwapScript(p, root.value(), QCoreApplication::applicationPid(),
                                              fromStd(manifest.version.toString()), resultFile);
    launchScriptAndQuit(script, removeStaging);
}

void UpdateController::applyInstaller(const UpdateManifest& manifest, const QString& setupPath) {
    const QString resultFile = QDir(cacheFolder()).filePath(QLatin1String(kResultFile));
    const QString script = installerScript(setupPath, QCoreApplication::applicationFilePath(),
                                           QCoreApplication::applicationPid(), fromStd(manifest.version.toString()), resultFile);
    launchScriptAndQuit(script, [setupPath] { QFile::remove(setupPath); });
}

bool UpdateController::launchScriptAndQuit(const QString& scriptText, const std::function<void()>& onAbort) {
    // Write the script first: everything that can fail is done before FurnitureOS closes.
    const QString scriptPath = QDir(cacheFolder()).filePath(QStringLiteral("apply-update.cmd"));
    QFile::remove(QDir(cacheFolder()).filePath(QLatin1String(kResultFile)));
    QFile f(scriptPath);
    const QByteArray utf8 = scriptText.toUtf8();
    const bool written = f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(utf8) == utf8.size() && f.flush();
    f.close();
    if (!written) {
        onAbort();
        QMessageBox::warning(window_, tr("Update"),
                             tr("The update helper script could not be written to %1; nothing was changed.")
                                 .arg(QDir::toNativeSeparators(scriptPath)));
        return false;
    }
    const auto ask = QMessageBox::question(window_, tr("Update"),
                                           tr("The update is ready. FurnitureOS will now close (you will be asked "
                                              "about unsaved changes), install it and start again.\n\nContinue?"),
                                           QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
    if (ask != QMessageBox::Yes || (window_ && !window_->close())) {
        onAbort();
        QFile::remove(scriptPath);
        QMessageBox::information(window_, tr("Update"), tr("The update was cancelled; nothing was changed."));
        return false;
    }
    QProcess process;
    process.setProgram(QStringLiteral("cmd.exe"));
    process.setArguments({QStringLiteral("/d"), QStringLiteral("/c"), QDir::toNativeSeparators(scriptPath)});
    process.setWorkingDirectory(cacheFolder()); // never inside the folder that is replaced
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    if (!process.startDetached()) {
        onAbort();
        QMessageBox::warning(nullptr, tr("Update"),
                             tr("The update helper could not be started (%1); nothing was changed.")
                                 .arg(QDir::toNativeSeparators(scriptPath)));
        return false;
    }
    QCoreApplication::quit();
    return true;
}

} // namespace fos::app::gui
