// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/cabinet_run_form.hpp"

#include "fos/app/session.hpp"
#include "fos/core/units.hpp"

#include <QCoreApplication>

namespace fos::app {
namespace {
// Context "fos::app": lupdate files these free tr() calls under the enclosing namespace.
QString tr(const char* text) { return QCoreApplication::translate("fos::app", text); }
QString qs(const std::string& s) { return QString::fromStdString(s); }

bool idInUse(const project::Project& project, const std::string& id) {
    const std::string prefix = id + ".";
    for (const auto& item : project.items)
        if (item.id() == id || item.id().rfind(prefix, 0) == 0) return true;
    return false;
}

QString severityText(core::Severity severity) {
    switch (severity) {
    case core::Severity::Error: return tr("Error");
    case core::Severity::Warning: return tr("Warning");
    case core::Severity::Info: break;
    }
    return tr("Note");
}
} // namespace

std::vector<CabinetRunRow> cabinetRunRows(const project::Project& project, const Selection& selection) {
    const project::Item* selected = project.findItem(selection.itemId);
    const bool selectedCabinet = selected && selected->isCabinet();
    std::vector<CabinetRunRow> rows;
    for (const auto& item : project.items) {
        if (!item.isCabinet()) continue;
        rows.push_back({item.id(), QStringLiteral("%1 (%2)").arg(qs(item.name()), qs(item.id())),
                        selectedCabinet ? item.id() == selection.itemId : true});
    }
    return rows;
}

QString materialChoiceText(const domain::Material& material) {
    return tr("%1 — %2 mm (%3)")
        .arg(qs(material.name), QString::fromStdString(core::formatLength(material.thickness)), qs(material.id.str()));
}

std::string nextRunItemId(const project::Project& project, const std::string& base) {
    for (int n = 1;; ++n) {
        std::string id = base + std::to_string(n);
        if (!idInUse(project, id)) return id;
    }
}

QStringList diagnosticLines(const core::Diagnostics& notes) {
    QStringList lines;
    for (const auto& d : notes) lines << QStringLiteral("%1: %2").arg(severityText(d.severity), qs(d.message));
    return lines;
}

} // namespace fos::app
