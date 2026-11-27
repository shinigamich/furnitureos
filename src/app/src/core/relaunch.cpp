// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/relaunch.hpp"

#include <QFileInfo>

#include <array>

namespace fos::app {

namespace {

bool isOption(const QString& a) { return a.size() > 1 && a.startsWith(QLatin1Char('-')); }

/// Positional-argument scan shared by projectFileArgument and relaunchArguments: calls
/// `option(arg)` for options and their values and `positional(arg)` for the rest.
template <typename OptionFn, typename PositionalFn>
void scan(const QStringList& arguments, OptionFn option, PositionalFn positional) {
    bool onlyPositional = false;
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        const QString& a = arguments.at(i);
        if (onlyPositional || !isOption(a)) {
            positional(a);
            continue;
        }
        if (a == QLatin1String("--")) {
            onlyPositional = true;
            continue;
        }
        option(a);
        if (optionTakesValue(a) && i + 1 < arguments.size()) option(arguments.at(++i));
    }
}

} // namespace

bool optionTakesValue(const QString& option) {
    // QGuiApplication / QApplication options with a separate value argument (Qt 6 docs of
    // QGuiApplication::QGuiApplication and QApplication::QApplication).
    static const std::array<const char*, 14> kWithValue{
        "platform", "platformpluginpath", "platformtheme", "plugin", "qwindowgeometry", "qwindowicon",
        "qwindowtitle", "style", "stylesheet", "session", "display", "geometry", "title", "name"};
    QString name = option;
    if (name.startsWith(QLatin1String("--"))) name.remove(0, 2);
    else if (name.startsWith(QLatin1Char('-'))) name.remove(0, 1);
    else return false;
    if (name.contains(QLatin1Char('='))) return false;
    for (const char* k : kWithValue)
        if (name == QLatin1String(k)) return true;
    return false;
}

QString projectFileArgument(const QStringList& arguments) {
    QString file;
    scan(arguments, [](const QString&) {}, [&](const QString& a) {
        if (file.isEmpty()) file = a;
    });
    return file;
}

QStringList relaunchArguments(const QStringList& arguments, const QString& documentPath) {
    QStringList out;
    scan(arguments, [&](const QString& a) { out << a; }, [](const QString&) {});
    if (!documentPath.isEmpty()) {
        // A document whose name starts with '-' must not be read as an option.
        if (isOption(documentPath)) out << QStringLiteral("--");
        out << documentPath;
    }
    return out;
}

QString relaunchProgram(const QString& applicationFilePath, const QString& appImagePath) {
    if (!appImagePath.isEmpty() && QFileInfo(appImagePath).isFile()) return appImagePath;
    return applicationFilePath;
}

} // namespace fos::app
