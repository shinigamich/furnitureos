// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Start-up crash-recovery dialog: lists the recovery copies of the RecoveryStore (project,
/// original file, autosave time) with Restore / Discard / Keep for later.
#include "fos/app/recovery_store.hpp"

#include <QDialog>

#include <optional>
#include <vector>

class QPushButton;
class QTreeWidget;

namespace fos::app::gui {

class RecoveryDialog : public QDialog {
    Q_OBJECT
public:
    /// `store` is where Discard deletes; `entries` = store.list().
    RecoveryDialog(RecoveryStore store, std::vector<RecoveryEntry> entries, QWidget* parent = nullptr);
    /// The entry chosen with Restore (the dialog was accepted); nullopt otherwise.
    const std::optional<RecoveryEntry>& chosen() const noexcept { return chosen_; }

private:
    void fillTree();
    void updateButtons();
    void restoreSelected();
    void discardSelected();
    std::vector<int> selectedRows() const;

    RecoveryStore store_;
    std::vector<RecoveryEntry> entries_;
    std::optional<RecoveryEntry> chosen_;
    QTreeWidget* tree_;
    QPushButton* restore_;
    QPushButton* discard_;
};

} // namespace fos::app::gui
