// SPDX-License-Identifier: AGPL-3.0-only
// fos_app_core tests need a QGuiApplication (QSettings, fonts for PDF export); they run with
// QT_QPA_PLATFORM=offscreen (set by CTest).
#include <QGuiApplication>

#include <QtGlobal>

#include <gtest/gtest.h>

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
