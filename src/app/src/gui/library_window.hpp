// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Project library editor (Project > Libraries…, non-modal): tabs Materials / Edge bands /
/// Hardware, each a list plus a form with New / Duplicate / Delete / Apply. Logic and
/// validation live in fos/app/library_editor.hpp; every Apply / Delete / Import is one
/// undoable project command. Hardware drilling geometry is not editable here.
#include "fos/app/library_editor.hpp"

#include <QMainWindow>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QTabWidget;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

/// One tab: entry list + form. Subclasses (library_forms.cpp) own the form widgets.
class LibraryPage : public QWidget {
    Q_OBJECT
public:
    LibraryPage(Session& session, LibraryCategory category, QWidget* parent);
    /// Re-reads the list from the project and re-selects the current entry (keeps an unsaved
    /// New / Duplicate form as it is).
    void reload();
    QListWidget* list() const noexcept { return list_; }
    LibraryCategory category() const noexcept { return category_; }
    /// The messages under the form (validation problems or a refused command).
    QLabel* messageLabel() const noexcept { return message_; }
    void selectEntry(const std::string& id);
    void startNew();
    void duplicateCurrent();
    void deleteCurrent();
    void apply();

protected:
    /// Lays out the list, `form` and the buttons; `allowNew` = show New.
    void buildLayout(QWidget* form, bool allowNew);
    virtual void showEntry(const std::string& id) = 0;
    virtual void showNew() = 0;
    virtual void showDuplicate(const std::string& id) = 0;
    virtual void clearForm() = 0;
    /// Id in the form (the id field for a new entry).
    virtual std::string formId() const = 0;
    virtual LibraryEditResult applyForm(bool isNew) = 0;
    /// The id field is editable only for New / Duplicate.
    virtual void setIdEditable(bool editable) = 0;

    Session& session_;

private:
    void rowChanged(int row);
    void setEditing(bool editingNew);
    void showMessages(const QStringList& messages);

    LibraryCategory category_;
    std::string currentId_;
    bool editingNew_ = false;
    QListWidget* list_ = nullptr;
    QWidget* form_ = nullptr;
    QLabel* message_ = nullptr;
    QPushButton* duplicate_ = nullptr;
    QPushButton* delete_ = nullptr;
    QPushButton* apply_ = nullptr;
};

LibraryPage* makeMaterialPage(Session& session, QWidget* parent);
LibraryPage* makeEdgeBandPage(Session& session, QWidget* parent);
LibraryPage* makeHardwarePage(Session& session, QWidget* parent);

class LibraryWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit LibraryWindow(Session& session, QWidget* parent = nullptr);
    LibraryPage* page(LibraryCategory category) const;
    QTabWidget* tabs() const noexcept { return tabs_; }
    /// Imports a `.foslib` (asks how to treat entries whose id already exists); one undo step.
    void importLibraryFile(const QString& path);
    void exportLibraryFile(const QString& path);

private:
    void reloadAll();
    void importDialog();
    void exportDialog();

    Session& session_;
    QTabWidget* tabs_ = nullptr;
    LibraryPage* pages_[3] = {};
};

} // namespace fos::app::gui
