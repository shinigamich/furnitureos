// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Command-line handling for start-up and for restarting the application (e.g. after the
/// interface language was changed): which argument is the project file, which arguments a
/// relaunched process gets, and which executable to start. Widget-free and testable.
#include <QString>
#include <QStringList>

namespace fos::app {

/// True for the Qt command-line options that consume the following argument as their value
/// ("-platform offscreen", "-style fusion", "--qwindowtitle T", ...). The `=value` spelling
/// ("-style=fusion") is one argument and returns false.
bool optionTakesValue(const QString& option);

/// The first positional argument after argv[0] (the project file given on the command line),
/// skipping options ("-x", "--x") and the values of the options of optionTakesValue();
/// everything after "--" is positional. Empty when there is none.
/// Example: {"furnitureos", "-style", "fusion", "a.fosp"} -> "a.fosp".
QString projectFileArgument(const QStringList& arguments);

/// Arguments for relaunching the application: `arguments` (as QCoreApplication::arguments(),
/// argv[0] first) without argv[0] and without the positional (project file) arguments, so
/// options such as "-platform xcb" are kept in their order; then `documentPath` when it is
/// non-empty (the saved document to reopen; pass an absolute path, it is appended as given).
/// A "--" of `arguments` is dropped; one is written before `documentPath` only when that
/// starts with '-'.
/// Example: ({"furnitureos", "-style", "fusion", "old.fosp"}, "/p/new.fosp")
///          -> {"-style", "fusion", "/p/new.fosp"}.
QStringList relaunchArguments(const QStringList& arguments, const QString& documentPath);

/// The executable to start for a relaunch: `appImagePath` (the APPIMAGE environment variable
/// of an AppImage, whose mounted applicationFilePath() disappears when the process exits)
/// when it is non-empty and names an existing file, otherwise `applicationFilePath`.
QString relaunchProgram(const QString& applicationFilePath, const QString& appImagePath);

} // namespace fos::app
