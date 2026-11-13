// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/window_title.hpp"

#include <QCoreApplication>
#include <QFileInfo>

namespace fos::app {

QString windowTitle(const QString& filePath, bool dirty) {
    const QString name = filePath.isEmpty() ? QCoreApplication::translate("fos::app::WindowTitle", "Untitled")
                                            : QFileInfo(filePath).fileName();
    return QStringLiteral("%1%2 — FurnitureOS").arg(name, dirty ? QStringLiteral("*") : QString());
}

} // namespace fos::app
