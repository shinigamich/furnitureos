// SPDX-License-Identifier: AGPL-3.0-only
#include <QGuiApplication>

#include <QtGlobal>

#include <gtest/gtest.h>

// The printing tests need a QGuiApplication (fonts, QPdfWriter, QPrinter). They run
// headless on the "offscreen" platform unless the caller chose another one.
int main(int argc, char** argv) {
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    // X11/Wayland systems: headless offscreen platform. On Windows and macOS the native
    // platform is always available (and Qt's offscreen platform has no fonts there).
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
