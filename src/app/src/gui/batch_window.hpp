// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Production batch window (File > Production batch…, non-modal): edits a `.fosbatch`
/// (projects with quantities, material substitutions, batch cutting settings) through
/// BatchEditor and shows the merged batch summary of the project module's batch engine, with
/// exports of the combined cut list, labels and cutting plan SVGs.
#include "fos/app/batch_editor.hpp"

#include <QMainWindow>

#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;
class QTableWidget;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class NestingSettingsForm;

class BatchWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit BatchWindow(Session& session, QWidget* parent = nullptr);

    BatchEditor& editor() noexcept { return editor_; }
    /// Re-reads every widget from the editor (after new / open / an edit).
    void reload();
    /// Recomputes the batch (loads the listed projects) and fills the Summary tab.
    void refreshSummary();
    /// Save / Discard / Cancel when modified; false = cancelled.
    bool maybeSave();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QWidget* buildProjectsTab();
    QWidget* buildSettingsTab();
    QWidget* buildSummaryTab();
    void newBatch();
    void openBatch();
    bool save();
    bool saveAs();
    void addProjects();
    void addCurrentProject();
    void removeProject();
    void entryEdited(int row, int column);
    void loadEntries();
    void loadSubstitutions();
    void applySubstitutions();
    void loadSawSettings();
    void applySawSettings();
    void edited(const core::Result<void>& result, QLabel* error);
    void updateTitle();
    const BatchEvaluation& evaluation();
    bool confirmIncomplete();
    void exportCutList();
    void exportLabels();
    void exportCuttingPlans();
    void reportWritten(const core::Result<std::vector<std::filesystem::path>>& written);

    Session& session_;
    BatchEditor editor_;
    std::optional<BatchEvaluation> evaluation_;
    bool loading_ = false;

    QTabWidget* tabs_;
    QLineEdit* name_;
    QLineEdit* date_;
    QPlainTextEdit* notes_;
    QTableWidget* entries_;
    QLabel* entriesError_;
    QTableWidget* substitutions_;
    QLabel* materials_;
    QLabel* substitutionError_;
    QDoubleSpinBox* kerf_;
    QDoubleSpinBox* trimLeft_;
    QDoubleSpinBox* trimRight_;
    QDoubleSpinBox* trimTop_;
    QDoubleSpinBox* trimBottom_;
    QComboBox* objective_;
    QCheckBox* groupCutList_;
    QLabel* sawError_;
    NestingSettingsForm* nesting_;
    QLabel* incomplete_;
    QTableWidget* summary_;
    QTableWidget* diagnostics_;
};

} // namespace fos::app::gui
