// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Main-window title text.
#include <QString>

namespace fos::app {

/// "<document> — FurnitureOS" where <document> is the file name of `filePath` (empty path:
/// the translated word "Untitled"), followed by "*" when `dirty`.
/// Example: windowTitle("/home/a/kitchen.fosp", true) == "kitchen.fosp* — FurnitureOS".
QString windowTitle(const QString& filePath, bool dirty);

} // namespace fos::app
