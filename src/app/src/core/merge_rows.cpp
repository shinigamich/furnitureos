// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/merge_rows.hpp"

#include "fos/project/file.hpp"

#include <QCoreApplication>

namespace fos::app {
namespace {
// Context "fos::app": lupdate files these free tr() calls under the enclosing namespace.
QString tr(const char* text) { return QCoreApplication::translate("fos::app", text); }

// The command label key of commands::mergeItems, extracted for translation.
[[maybe_unused]] const char* const kMergeKey = QT_TRANSLATE_NOOP("fos::app::CommandText", "Merge items from {0}");

QString q(const std::string& s) { return QString::fromStdString(s); }

QString categoryText(formats::LibraryCategory category) {
    switch (category) {
    case formats::LibraryCategory::Material: return tr("Material");
    case formats::LibraryCategory::EdgeBand: return tr("Edge band");
    case formats::LibraryCategory::Hardware: return tr("Hardware");
    }
    return {};
}
} // namespace

core::Result<project::Project> loadMergeSource(const std::filesystem::path& path) {
    auto loaded = project::loadProject(path);
    if (!loaded) return loaded.error();
    return std::move(loaded.value().project);
}

std::vector<MergeSourceRow> mergeSourceRows(const project::Project& source) {
    std::vector<MergeSourceRow> rows;
    for (const auto& item : source.items)
        rows.push_back({item.id(), q(item.name()), item.isCabinet() ? tr("Cabinet") : tr("Panel")});
    return rows;
}

std::vector<ConflictPolicyChoice> conflictPolicyChoices() {
    return {{formats::ConflictPolicy::Skip, tr("Keep this project's entry")},
            {formats::ConflictPolicy::Replace, tr("Replace this project's entry")},
            {formats::ConflictPolicy::RenameWithNewId, tr("Add the imported entry under a new id")}};
}

project::MergeRequest mergeRequest(const std::vector<std::string>& checked, formats::ConflictPolicy policy) {
    project::MergeRequest r;
    r.itemIds = checked;
    r.conflictPolicy = policy;
    return r;
}

bool mergeAllowed(const std::vector<std::string>& checked) noexcept { return !checked.empty(); }

QStringList mergePreviewLines(const project::MergePlan& plan) {
    QStringList lines;
    for (const auto& item : plan.items) {
        if (item.renamed())
            lines << tr("%1 “%2” → %3 (id already used)").arg(q(item.sourceId), q(item.name), q(item.newId));
        else
            lines << tr("%1 “%2”").arg(q(item.sourceId), q(item.name));
    }
    for (const auto& l : plan.library) {
        const auto& e = l.entry;
        const QString who = tr("%1 “%2” (%3)").arg(categoryText(e.category), q(e.name), q(e.incomingId));
        QString what;
        switch (e.action) {
        case formats::MergeAction::Added: what = tr("added"); break;
        case formats::MergeAction::Skipped:
            what = l.conflict ? tr("differs: this project's entry is kept") : tr("identical entry already in this project");
            break;
        case formats::MergeAction::Replaced:
            what = tr("replaces this project's entry (its other users change too)");
            break;
        case formats::MergeAction::Renamed: what = tr("differs: added as %1").arg(q(e.resultId)); break;
        }
        lines << who + QStringLiteral(": ") + what;
    }
    return lines;
}

} // namespace fos::app
