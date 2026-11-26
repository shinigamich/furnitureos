// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// File > Compare with…: shows project::diffProjects(current project, other file, model) as a
/// grouped tree (app/project_compare.hpp) with export as text or JSON.
#include "fos/project/diff.hpp"

#include <QDialog>

class QTreeWidget;

namespace fos::app::gui {

class CompareDialog : public QDialog {
    Q_OBJECT
public:
    CompareDialog(project::ProjectDiff diff, const QString& otherFile, QWidget* parent = nullptr);
    const QTreeWidget* tree() const noexcept { return tree_; }
    void exportText();
    void exportJson();

private:
    void save(const QString& caption, const QString& suffix, const QString& filter, const std::string& content);
    project::ProjectDiff diff_;
    QString otherFile_;
    QTreeWidget* tree_;
};

} // namespace fos::app::gui
