// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// "Components" dock: the component libraries of the configured folders as a tree by
/// category, tag filter and search, details of the selected component and "Insert"
/// (fos/app/component_browser.hpp does the work; this is only the widget).
#include "fos/app/component_browser.hpp"

#include <QDialog>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QTextBrowser;
class QTreeWidget;

namespace fos::app::gui {

class ComponentBrowser : public QWidget {
    Q_OBJECT
public:
    ComponentBrowser(Session& session, QWidget* parent = nullptr);

    /// Sets the library folders and rescans them.
    void setFolders(std::vector<std::filesystem::path> folders);
    /// Rescans the current folders (keeps the selected component when it still exists).
    void refresh();
    const ComponentBrowserModel& model() const noexcept { return model_; }
    /// The selected component, null when a category or nothing is selected.
    const project::ComponentEntry* currentEntry() const;
    /// Selects the component with this id (false when it is not shown).
    bool selectComponent(const QString& id);
    /// Inserts the selected component; asks for the conflict policy when the project has
    /// different library entries with the same ids. Returns true when inserted.
    bool insertCurrent();

signals:
    /// "Save as component…" chosen in the context menu.
    void saveAsComponentRequested();

private:
    void rebuildTags();
    void rebuildTree();
    void updateDetails();
    void showContextMenu(const QPoint& pos);

    Session& session_;
    ComponentBrowserModel model_;
    QLineEdit* search_;
    QComboBox* tag_;
    QTreeWidget* tree_;
    QTextBrowser* details_;
    QLabel* status_;
    QPushButton* insert_;
};

/// Asks how to merge conflicting library entries: keep existing / replace / rename.
class ComponentConflictDialog : public QDialog {
    Q_OBJECT
public:
    ComponentConflictDialog(const QString& componentName, const QStringList& conflicts, QWidget* parent = nullptr);
    formats::ConflictPolicy policy() const;

private:
    QRadioButton* keep_;
    QRadioButton* replace_;
    QRadioButton* rename_;
};

/// Name, category, tags, description, author and target folder of "Save as component…".
class SaveComponentDialog : public QDialog {
    Q_OBJECT
public:
    SaveComponentDialog(const QString& itemName, const std::vector<std::string>& categories,
                        const std::vector<std::filesystem::path>& folders, QWidget* parent = nullptr);
    formats::ComponentMeta meta() const;
    std::filesystem::path folder() const;

private:
    QLineEdit* name_;
    QComboBox* category_;
    QLineEdit* tags_;
    QPlainTextEdit* description_;
    QLineEdit* author_;
    QComboBox* folder_;
};

} // namespace fos::app::gui
