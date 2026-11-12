// SPDX-License-Identifier: AGPL-3.0-only
#include <QGuiApplication>

#include <gtest/gtest.h>

// The printing tests need a QGuiApplication (fonts, QPdfWriter, QPrinter). They run
// headless on the "offscreen" platform unless the caller chose another one.
int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
