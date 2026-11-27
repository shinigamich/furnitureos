// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Sample projects bundled as Qt resources (resources/samples.qrc, prefix ":/samples/"),
/// offered by the Welcome dialog and opened as untitled copies (Session::openUntitledCopy).
#include <QByteArray>
#include <QString>

#include <vector>

namespace fos::app {

struct SampleProject {
    QString key;          ///< Stable id, e.g. "kitchen_run".
    QString title;        ///< Translated display name.
    QString description;  ///< Translated one-line description.
    QString resourcePath; ///< ":/samples/<key>.fosp".
};

/// The bundled samples in display order (kitchen run, wardrobe, desk). Registers the
/// resource file on first use (static library).
std::vector<SampleProject> sampleProjects();

/// The project text of a sample (empty when the resource is missing).
QByteArray sampleProjectText(const SampleProject& sample);

} // namespace fos::app
