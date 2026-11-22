// SPDX-License-Identifier: AGPL-3.0-only
#include "component_browser.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace fos::app::gui {
namespace {
constexpr int kIdRole = Qt::UserRole + 1;
QString q(const std::string& s) { return QString::fromStdString(s); }
QString esc(const QString& s) { return s.toHtmlEscaped(); }
} // namespace

// ------------------------------------------------------------------ ComponentBrowser

ComponentBrowser::ComponentBrowser(Session& session, QWidget* parent) : QWidget(parent), session_(session) {
    setObjectName(QStringLiteral("componentBrowser"));
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("componentSearch"));
    search_->setPlaceholderText(tr("Search components"));
    search_->setClearButtonEnabled(true);
    tag_ = new QComboBox(this);
    tag_->setObjectName(QStringLiteral("componentTag"));
    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("componentTree"));
    tree_->setHeaderHidden(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    details_ = new QTextBrowser(this);
    details_->setObjectName(QStringLiteral("componentDetails"));
    details_->setOpenLinks(false);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    insert_ = new QPushButton(tr("Insert"), this);
    insert_->setObjectName(QStringLiteral("componentInsert"));
    auto* refreshButton = new QPushButton(tr("Refresh"), this);

    auto* filters = new QHBoxLayout;
    filters->addWidget(search_, 2);
    filters->addWidget(tag_, 1);
    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(tree_);
    splitter->addWidget(details_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(insert_);
    buttons->addWidget(refreshButton);
    buttons->addStretch(1);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(filters);
    layout->addWidget(splitter, 1);
    layout->addWidget(status_);
    layout->addLayout(buttons);

    connect(search_, &QLineEdit::textChanged, this, &ComponentBrowser::rebuildTree);
    connect(tag_, &QComboBox::currentIndexChanged, this, &ComponentBrowser::rebuildTree);
    connect(tree_, &QTreeWidget::currentItemChanged, this, &ComponentBrowser::updateDetails);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (item && !item->data(0, kIdRole).toString().isEmpty()) insertCurrent();
    });
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &ComponentBrowser::showContextMenu);
    connect(insert_, &QPushButton::clicked, this, &ComponentBrowser::insertCurrent);
    connect(refreshButton, &QPushButton::clicked, this, &ComponentBrowser::refresh);
    rebuildTags();
    rebuildTree();
}

void ComponentBrowser::setFolders(std::vector<std::filesystem::path> folders) {
    const QString keep = currentEntry() ? q(currentEntry()->component.meta.id) : QString();
    const core::Diagnostics notes = model_.rescan(folders);
    QStringList messages;
    for (const auto& d : notes) messages << q(d.message);
    status_->setText(notes.empty() ? tr("%n component(s)", nullptr, int(model_.library().entries().size()))
                                   : tr("%n component(s), %1 problem(s) while reading the folders", nullptr,
                                        int(model_.library().entries().size()))
                                         .arg(notes.size()));
    status_->setToolTip(messages.join(QLatin1Char('\n')));
    rebuildTags();
    rebuildTree();
    if (!keep.isEmpty()) selectComponent(keep);
}

void ComponentBrowser::refresh() { setFolders(model_.folders()); }

const project::ComponentEntry* ComponentBrowser::currentEntry() const {
    const QTreeWidgetItem* item = tree_->currentItem();
    if (!item) return nullptr;
    const QString id = item->data(0, kIdRole).toString();
    return id.isEmpty() ? nullptr : model_.library().find(id.toStdString());
}

bool ComponentBrowser::selectComponent(const QString& id) {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* cat = tree_->topLevelItem(i);
        for (int j = 0; j < cat->childCount(); ++j)
            if (cat->child(j)->data(0, kIdRole).toString() == id) {
                tree_->setCurrentItem(cat->child(j));
                return true;
            }
    }
    return false;
}

void ComponentBrowser::rebuildTags() {
    const QString keep = tag_->currentData().toString();
    const QSignalBlocker block(tag_);
    tag_->clear();
    tag_->addItem(tr("All tags"), QString());
    for (const auto& t : model_.library().tags()) tag_->addItem(q(t), q(t));
    tag_->setCurrentIndex(std::max(0, tag_->findData(keep)));
}

void ComponentBrowser::rebuildTree() {
    const QString keep = currentEntry() ? q(currentEntry()->component.meta.id) : QString();
    {
        const QSignalBlocker block(tree_);
        tree_->clear();
        for (const auto& row : model_.rows(tag_->currentData().toString().toStdString(), search_->text())) {
            auto* cat = new QTreeWidgetItem(tree_, {row.category.empty() ? tr("Uncategorised") : q(row.category)});
            for (const auto* entry : row.entries) {
                auto* item = new QTreeWidgetItem(cat, {q(entry->component.meta.name)});
                item->setData(0, kIdRole, q(entry->component.meta.id));
                item->setToolTip(0, q(entry->source));
            }
        }
        tree_->expandAll();
    }
    if (!keep.isEmpty()) selectComponent(keep);
    updateDetails();
}

void ComponentBrowser::updateDetails() {
    const project::ComponentEntry* entry = currentEntry();
    insert_->setEnabled(entry != nullptr);
    if (!entry) {
        details_->setHtml(model_.library().entries().empty()
                              ? esc(tr("No components found. Use \"Save as component…\" to add the selected item, or "
                                       "add library folders in Preferences."))
                              : QString());
        return;
    }
    const ComponentDetails d = componentDetails(*entry);
    auto line = [](const QString& label, const QString& value) {
        return value.isEmpty() ? QString() : QStringLiteral("<b>%1</b> %2<br>").arg(esc(label), esc(value));
    };
    auto list = [](const QString& label, const QStringList& values) {
        return values.isEmpty() ? QString()
                                : QStringLiteral("<b>%1</b><br>&nbsp;&nbsp;%2<br>")
                                      .arg(esc(label), esc(values.join(QStringLiteral("\n"))).replace(
                                                           QLatin1Char('\n'), QStringLiteral("<br>&nbsp;&nbsp;")));
    };
    QString html = QStringLiteral("<h3>%1</h3>").arg(esc(d.name));
    if (!d.description.isEmpty()) html += QStringLiteral("<p>%1</p>").arg(esc(d.description));
    html += line(tr("Size (W × H × D):"), d.size);
    html += line(tr("Category:"), d.category);
    html += line(tr("Tags:"), d.tags.join(QStringLiteral(", ")));
    html += line(tr("Author:"), d.author);
    html += list(tr("Materials:"), d.materials);
    html += list(tr("Edge bands:"), d.edgeBands);
    html += list(tr("Hardware:"), d.hardware);
    html += line(tr("File:"), d.source);
    details_->setHtml(html);
}

bool ComponentBrowser::insertCurrent() {
    const project::ComponentEntry* entry = currentEntry();
    if (!entry) return false;
    const project::Component& component = entry->component;
    auto conflicts = componentConflicts(session_.project(), component);
    if (!conflicts) {
        QMessageBox::warning(this, tr("Insert component"), q(conflicts.error().message));
        return false;
    }
    formats::ConflictPolicy policy = formats::ConflictPolicy::Skip;
    if (!conflicts.value().isEmpty()) {
        ComponentConflictDialog dialog(q(component.meta.name), conflicts.value(), this);
        if (dialog.exec() != QDialog::Accepted) return false;
        policy = dialog.policy();
    }
    auto id = insertComponentIntoSession(session_, component, policy);
    if (!id) {
        QMessageBox::warning(this, tr("Insert component"), q(id.error().message));
        return false;
    }
    return true;
}

void ComponentBrowser::showContextMenu(const QPoint& pos) {
    QMenu menu(this);
    QAction* insert = menu.addAction(tr("Insert"));
    insert->setEnabled(currentEntry() != nullptr);
    QAction* save = menu.addAction(tr("Save selected item as component…"));
    save->setEnabled(!session_.selection().itemId.empty());
    menu.addSeparator();
    QAction* refreshAction = menu.addAction(tr("Refresh"));
    QAction* chosen = menu.exec(tree_->viewport()->mapToGlobal(pos));
    if (chosen == insert) insertCurrent();
    else if (chosen == save) emit saveAsComponentRequested();
    else if (chosen == refreshAction) refresh();
}

// ------------------------------------------------------------------ ComponentConflictDialog

ComponentConflictDialog::ComponentConflictDialog(const QString& componentName, const QStringList& conflicts,
                                                 QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Library conflicts"));
    auto* text = new QLabel(tr("The component \"%1\" contains library entries whose ids are already used by "
                               "different entries of this project:")
                                .arg(componentName),
                            this);
    text->setWordWrap(true);
    auto* ids = new QListWidget(this);
    ids->setObjectName(QStringLiteral("conflictIds"));
    ids->addItems(conflicts);
    ids->setMaximumHeight(120);
    keep_ = new QRadioButton(tr("Keep the project's entries (the component uses them)"), this);
    replace_ = new QRadioButton(tr("Replace the project's entries (affects every item that uses them)"), this);
    rename_ = new QRadioButton(tr("Add the component's entries under new ids"), this);
    keep_->setChecked(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(text);
    layout->addWidget(ids);
    layout->addWidget(keep_);
    layout->addWidget(replace_);
    layout->addWidget(rename_);
    layout->addWidget(buttons);
}

formats::ConflictPolicy ComponentConflictDialog::policy() const {
    if (replace_->isChecked()) return formats::ConflictPolicy::Replace;
    if (rename_->isChecked()) return formats::ConflictPolicy::RenameWithNewId;
    return formats::ConflictPolicy::Skip;
}

// ------------------------------------------------------------------ SaveComponentDialog

SaveComponentDialog::SaveComponentDialog(const QString& itemName, const std::vector<std::string>& categories,
                                         const std::vector<std::filesystem::path>& folders, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Save as component"));
    name_ = new QLineEdit(itemName, this);
    category_ = new QComboBox(this);
    category_->setEditable(true);
    category_->addItem(QString());
    for (const auto& c : categories) category_->addItem(q(c));
    tags_ = new QLineEdit(this);
    tags_->setPlaceholderText(tr("comma-separated, e.g. kitchen, base"));
    description_ = new QPlainTextEdit(this);
    description_->setMaximumHeight(80);
    author_ = new QLineEdit(this);
    folder_ = new QComboBox(this);
    for (const auto& f : folders) folder_->addItem(q(f.string()), q(f.string()));
    auto* browse = new QPushButton(tr("Other folder…"), this);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Component library folder"));
        if (dir.isEmpty()) return;
        if (folder_->findData(dir) < 0) folder_->addItem(dir, dir);
        folder_->setCurrentIndex(folder_->findData(dir));
    });
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(browse);
    auto* form = new QFormLayout;
    form->addRow(tr("Name:"), name_);
    form->addRow(tr("Category:"), category_);
    form->addRow(tr("Tags:"), tags_);
    form->addRow(tr("Description:"), description_);
    form->addRow(tr("Author:"), author_);
    form->addRow(tr("Library folder:"), folderRow);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto updateOk = [this, buttons] {
        buttons->button(QDialogButtonBox::Ok)
            ->setEnabled(!name_->text().trimmed().isEmpty() && !folder_->currentData().toString().isEmpty());
    };
    connect(name_, &QLineEdit::textChanged, this, updateOk);
    connect(folder_, &QComboBox::currentIndexChanged, this, updateOk);
    updateOk();
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    resize(480, sizeHint().height());
}

formats::ComponentMeta SaveComponentDialog::meta() const {
    formats::ComponentMeta m;
    m.name = name_->text().trimmed().toStdString();
    m.category = category_->currentText().trimmed().toStdString();
    m.tags = parseTagList(tags_->text());
    m.description = description_->toPlainText().trimmed().toStdString();
    m.author = author_->text().trimmed().toStdString();
    return m;
}

std::filesystem::path SaveComponentDialog::folder() const {
    return std::filesystem::path(folder_->currentData().toString().toStdString());
}

} // namespace fos::app::gui
