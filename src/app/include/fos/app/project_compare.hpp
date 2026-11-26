// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// View-model of File > Compare with…: the comparison itself is project::diffProjects
/// (fos/project/diff.hpp, with the model-level diff); this file loads the other file, lays the
/// ProjectDiff out as a translated, grouped tree and produces the export texts.
#include "fos/app/text_tree.hpp"
#include "fos/core/result.hpp"
#include "fos/project/diff.hpp"

#include <QCoreApplication>
#include <QString>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fos::app {

/// Loads `other` (project::loadProject) and returns
/// project::diffProjects(current, other, {model = true}): A = the open project, B = the file.
/// Error = the load / resolve error of the project module.
core::Result<project::ProjectDiff> compareWithFile(const project::Project& current, const std::filesystem::path& other);

class CompareText {
    Q_DECLARE_TR_FUNCTIONS(CompareText)
public:
    /// Tree columns: Change, Current project (A), Other file (B).
    static QStringList columns();
    /// Groups in this order, each only when non-empty: Metadata, Libraries, Items, Settings,
    /// Other, Production model (parts / hardware). A single "No differences" node when empty.
    static std::vector<TextNode> tree(const project::ProjectDiff& diff);
    /// Compact display of a JSON value: strings without quotes, absent = "—".
    static QString value(const std::optional<formats::Json>& v);
};

/// project::diffText (English, the CLI's report).
std::string compareText(const project::ProjectDiff& diff);
/// project::diffToJson, pretty-printed (2 spaces), trailing newline.
std::string compareJson(const project::ProjectDiff& diff);

} // namespace fos::app
