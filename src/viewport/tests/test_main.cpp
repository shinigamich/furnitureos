// SPDX-License-Identifier: AGPL-3.0-only
// Test entry point: widgets need a QApplication. Without any display the offscreen platform
// is used (enough for everything except QOpenGLWidget rendering, which then skips).
#include <QApplication>
#include <QtGlobal>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
#ifndef Q_OS_WIN
    // X11/Wayland only: Windows (and macOS) always have a window system available.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && qEnvironmentVariableIsEmpty("DISPLAY") &&
        qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
