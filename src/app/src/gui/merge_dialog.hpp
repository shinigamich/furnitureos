// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// File > Import items from project…: pick a `.fosp`, check the items to import, choose how
/// differing library entries are handled and see project::planMerge()'s preview (renamed ids,
/// library actions) before the merge runs as ONE undoable step (commands::mergeItems).
#include "fos/project/merge.hpp"

#include <QDialog>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class MergeDialog : public QDialog {
    Q_OBJECT
public:
    MergeDialog(Session& session, project::Project source, const QString& sourceFile, QWidget* parent = nullptr);
    QListWidget* items() const noexcept { return items_; }
    QComboBox* policy() const noexcept { return policy_; }
    QPlainTextEdit* preview() const noexcept { return preview_; }
    /// Runs the merge (the Import button); false with the message shown when it is refused.
    bool importChecked();

private:
    std::vector<std::string> checkedIds() const;
    void updatePreview();

    Session& session_;
    project::Project source_;
    QListWidget* items_;
    QComboBox* policy_;
    QPlainTextEdit* preview_;
    QLabel* error_;
    QDialogButtonBox* buttons_;
};

/// Asks for a project file, loads it and runs the dialog (errors in a message box).
void importItemsFromProject(Session& session, QWidget* parent);

} // namespace fos::app::gui
