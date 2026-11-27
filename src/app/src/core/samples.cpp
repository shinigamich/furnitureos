// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/samples.hpp"

#include <QCoreApplication>
#include <QFile>

// Q_INIT_RESOURCE must be used outside a namespace; referencing it from this file also keeps
// the resource object linked from the static library.
static void initSampleResources() { Q_INIT_RESOURCE(samples); }

namespace fos::app {

std::vector<SampleProject> sampleProjects() {
    static const bool registered = [] {
        initSampleResources();
        return true;
    }();
    (void)registered;
    const auto tr = [](const char* text) { return QCoreApplication::translate("fos::app::Samples", text); };
    const auto make = [&](const char* key, const char* title, const char* description) {
        const QString k = QString::fromLatin1(key);
        return SampleProject{k, tr(title), tr(description), QStringLiteral(":/samples/") + k + QStringLiteral(".fosp")};
    };
    return {
        make("kitchen_run", QT_TRANSLATE_NOOP("fos::app::Samples", "Kitchen run with worktop"),
             QT_TRANSLATE_NOOP("fos::app::Samples",
                               "Base cabinet, drawer unit, dishwasher front and sink cabinet under one worktop.")),
        make("wardrobe_sliding_doors", QT_TRANSLATE_NOOP("fos::app::Samples", "Wardrobe with sliding doors"),
             QT_TRANSLATE_NOOP("fos::app::Samples", "1800 mm wardrobe: shelves, hanging rail, two sliding doors.")),
        make("desk", QT_TRANSLATE_NOOP("fos::app::Samples", "Desk"),
             QT_TRANSLATE_NOOP("fos::app::Samples", "1400 mm desk with modesty panel and a three-drawer pedestal.")),
    };
}

QByteArray sampleProjectText(const SampleProject& sample) {
    QFile f(sample.resourcePath);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

} // namespace fos::app
