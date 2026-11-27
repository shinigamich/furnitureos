// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// How a verified update is applied, decided without widgets: install kind detection, the
/// portable staging/backup plan, the generated Windows command scripts that swap the folder
/// (or run the installer) after FurnitureOS has exited, and the startup-check consent.
///
/// Why a generated .cmd script and not a helper executable: Windows cannot rename a folder
/// while an image inside it is running, so a helper would have to be copied out of the app
/// folder together with its Qt and C++ runtime DLLs. cmd.exe is always present, is not
/// subject to the PowerShell execution policy, and the script text is unit-tested here.
#include "fos/app/update_release.hpp"

#include "fos/core/result.hpp"

#include <QString>

namespace fos::app {

/// Marker written next to furnitureos.exe by the Windows installer (Inno Setup).
inline constexpr const char* kInstallerMarker = "installed.txt";
/// Silent-upgrade switches of the Inno Setup installer (it closes a running copy itself
/// but does not relaunch after a silent run, so the script relaunches).
inline constexpr const char* kInstallerSilentArgs = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART";

enum class InstallKind {
    WindowsInstalled, ///< installed by setup.exe (marker present)
    WindowsPortable,  ///< unpacked portable ZIP
    Unsupported,      ///< Linux/other: no in-place update; download/release page only
};

/// `appDir` holds the running executable; `isWindows` is the platform (a parameter for tests).
InstallKind detectInstallKind(const QString& appDir, bool isWindows);

/// What "Update now" does for the given kind and manifest.
enum class UpdateMethod { RunInstaller, SwapPortable, ManualOnly };
struct UpdateChoice {
    UpdateMethod method = UpdateMethod::ManualOnly;
    const UpdateAsset* asset = nullptr; ///< what to download; nullptr for ManualOnly
};
/// Installed copies use the setup asset (falling back to ManualOnly when the release has
/// none: swapping files of an installed copy would desynchronise the uninstaller); portable
/// copies use the ZIP; Unsupported is always ManualOnly.
UpdateChoice chooseUpdate(InstallKind kind, const UpdateManifest& manifest);

/// Folder names of a portable swap. All live in the parent of the app folder so that every
/// move is a rename on one volume (atomic per folder).
struct PortableSwapPlan {
    QString appDir;      ///< current application folder (holds the exe)
    QString stagingDir;  ///< "<parent>/<app>.update-<version>": the ZIP is extracted here
    QString backupDir;   ///< "<parent>/<app>.previous-<version>": the old folder during the swap
    QString exeRelative; ///< exe path relative to appDir, e.g. "furnitureos.exe"
};

/// Plans a swap of `appDir` (absolute) for `newVersion`; the exe is `exePath` inside appDir.
/// Chooses staging/backup names that do not exist yet (suffix -2, -3 ... when needed).
core::Result<PortableSwapPlan> planPortableSwap(const QString& appDir, const QString& exePath, const SemVer& newVersion);

/// After extraction: the folder inside `stagingDir` that becomes the new app folder, i.e.
/// the one holding `exeRelative` — `stagingDir` itself, or its single top-level folder
/// (release ZIPs contain "FurnitureOS-X.Y.Z-windows-x64/"). Error when neither holds it.
core::Result<QString> locateStagedRoot(const QString& stagingDir, const QString& exeRelative);

/// Escapes a value for `set "NAME=value"` in a .cmd file ('%' doubled). Windows paths cannot
/// contain '"', so quoting is otherwise safe.
QString cmdEscape(const QString& value);

/// Script (UTF-8, CRLF) run detached by cmd.exe: waits up to 120 s for process `pid` to exit,
/// renames appDir -> backupDir (retrying 30 s while files are locked), renames `stagedRoot`
/// -> appDir, rolls back on failure, relaunches the exe (new or, after a failure, old) and
/// writes "ok <version>" or "failed <reason>" to `resultFile`. The old folder is deleted
/// only after the new one is in place.
QString portableSwapScript(const PortableSwapPlan& plan, const QString& stagedRoot, qint64 pid,
                           const QString& newVersion, const QString& resultFile);

/// Script that waits for `pid`, runs `setupExe` with kInstallerSilentArgs, records the result
/// in `resultFile`, deletes the installer and relaunches `exePath`.
QString installerScript(const QString& setupExe, const QString& exePath, qint64 pid, const QString& newVersion,
                        const QString& resultFile);

/// Outcome of a previous update attempt read back from the result file.
struct UpdateOutcome {
    bool ok = false;
    QString detail; ///< version on success, reason on failure
};
/// Parses the result file's text; std::nullopt when it is empty or unrecognised.
std::optional<UpdateOutcome> parseUpdateOutcome(const QString& text);

/// Consent to the automatic check at startup (QSettings "updates/startupCheck").
enum class UpdateConsent { NotAsked, Allowed, Declined };
QString updateConsentKey(UpdateConsent consent);           ///< "ask" / "yes" / "no"
UpdateConsent updateConsentFromKey(const QString& key);    ///< unknown -> NotAsked
/// The question is asked once, on the first start after installation; nothing is checked
/// automatically until the user said yes.
inline bool shouldAskUpdateConsent(UpdateConsent c) { return c == UpdateConsent::NotAsked; }
inline bool shouldCheckAtStartup(UpdateConsent c) { return c == UpdateConsent::Allowed; }

} // namespace fos::app
