// SPDX-License-Identifier: AGPL-3.0-only
// fos_app_core tests need a QGuiApplication (QSettings, fonts for PDF export); they run with
// QT_QPA_PLATFORM=offscreen (set by CTest).
#include <QGuiApplication>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
