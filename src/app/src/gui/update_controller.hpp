// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// In-app updates (Help > Check for updates…, optional check at startup). Opt-in, never
/// installs without the user's confirmation, sends no personal data: one anonymous request
/// to the GitHub Releases API with User-Agent "FurnitureOS-Updater". Decisions live in
/// fos/app/update_*.hpp; this class only does the networking, dialogs and launching.
#include "fos/app/update_plan.hpp"
#include "fos/app/update_release.hpp"

#include <QObject>
#include <QPointer>

#include <functional>
#include <map>

class QMainWindow;
class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

namespace fos::app::gui {

class UpdateController : public QObject {
    Q_OBJECT
public:
    explicit UpdateController(QMainWindow* window);

    /// Help menu: always checks (whatever the startup setting) and reports "up to date"
    /// and errors; offers even a version the user chose to skip.
    void checkNow();
    /// Called once after start-up: reports the result of a previous update attempt, asks
    /// once whether to check automatically, and checks silently only if the user agreed.
    void startupCheck();

    /// Folder for downloads, generated scripts and the result file.
    static QString cacheFolder();

private:
    using BodyHandler = std::function<void(const QByteArray&)>;
    void startCheck(bool interactive);
    void fetch(const QUrl& url, bool api, qint64 maxBytes, BodyHandler onBody);
    void finishCheck();
    void fail(const QString& what, const QString& detail);
    void offer(const ReleaseInfo& release, const UpdateManifest& manifest);
    void downloadAndApply(const ReleaseInfo& release, const UpdateManifest& manifest, const UpdateChoice& choice);
    void download(const UpdateAsset& asset, const std::map<QString, QString>& sums, std::function<void(const QString&)> onVerified);
    void applyPortable(const UpdateManifest& manifest, const QString& zipPath);
    void applyInstaller(const UpdateManifest& manifest, const QString& setupPath);
    bool launchScriptAndQuit(const QString& scriptText, const std::function<void()>& onAbort);
    void showPreviousOutcome();
    QString replyError(QNetworkReply* reply) const;

    QPointer<QMainWindow> window_;
    QNetworkAccessManager* network_;
    bool busy_ = false;
    bool interactive_ = false;
};

} // namespace fos::app::gui
