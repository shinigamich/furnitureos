// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/command_text.hpp"

#include <QCoreApplication>

namespace fos::app {
namespace {

// Every label key of project/src/commands.cpp, listed so that lupdate extracts them for
// translation (context "fos::app::CommandText"). Keys not listed stay English.
[[maybe_unused]] const char* const kKnownKeys[] = {
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Set {1} of {0} to {2}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Reset {1} of {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Change construction of {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Change materials of {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Change hardware of {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Change part {1} of {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Change interior of {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Change section {1} of {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Set shelf count of section {1} of {0} to {2}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Set front of section {1} of {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Remove front of section {1} of {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Split section {1} of {0} into {2}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Resize section {1} of {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Change panel {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Add {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Remove {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Duplicate {0} as {1}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Move {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Rename {0} to {1}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Edit drawing {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Reset drawing {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Edit {1} in drawing {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Reset {1} in drawing {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Change cutting settings of {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Edit project properties"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Change project settings"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Edit custom project data"),
    // Commands built by the application itself (session.cpp).
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Recover {0} from autosave or backup"),
};

} // namespace

QString commandText(const project::CommandLabel& label, const project::Project& project) {
    QString text = QCoreApplication::translate("fos::app::CommandText", label.key.c_str());
    for (std::size_t i = 0; i < label.args.size(); ++i) {
        const std::string& arg = label.args[i];
        const project::Item* item = project.findItem(arg);
        const QString value = QString::fromStdString(item ? item->name() : arg);
        text.replace(QStringLiteral("{%1}").arg(i), value);
    }
    return text;
}

} // namespace fos::app
