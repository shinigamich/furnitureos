// SPDX-License-Identifier: AGPL-3.0-only
// Test entry point: widgets need a QApplication. Without any display the offscreen platform
// is used (enough for everything except QOpenGLWidget rendering, which then skips).
#include <QApplication>

#include <gtest/gtest.h>

#include <cstdlib>

int main(int argc, char** argv) {
    if (!std::getenv("QT_QPA_PLATFORM") && !std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY"))
        setenv("QT_QPA_PLATFORM", "offscreen", 1);
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
