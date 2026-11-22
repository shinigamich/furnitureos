// SPDX-License-Identifier: AGPL-3.0-only
#include "dialogs.hpp"

#include "fos/app/autosave.hpp"
#include "fos/app/component_browser.hpp"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QHeaderView>
#include <QLabel>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fos::app::gui {
namespace {
QDoubleSpinBox* mmBox(double value, QWidget* parent) {
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(1, 10000);
    box->setDecimals(1);
    box->setSingleStep(10);
    box->setSuffix(QCoreApplication::translate("fos::app::gui::TemplateSizeDialog", " mm"));
    box->setValue(value);
    return box;
}
QDialogButtonBox* okCancel(QDialog* dialog) {
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return buttons;
}
} // namespace

TemplateSizeDialog::TemplateSizeDialog(TemplateKind kind, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("New %1").arg(templateTitle(kind)));
    const TemplateSize d = defaultTemplateSize(kind);
    width_ = mmBox(d.width, this);
    height_ = mmBox(d.height, this);
    depth_ = mmBox(d.depth, this);
    auto* form = new QFormLayout;
    form->addRow(tr("Width:"), width_);
    form->addRow(tr("Height (including plinth):"), height_);
    form->addRow(tr("Depth:"), depth_);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    auto* note = new QLabel(tr("All other parameters keep the template defaults and can be edited in the inspector."), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    if (kind == TemplateKind::LCornerCabinet) {
        auto* legs = new QLabel(tr("Width and depth are those of leg A (along the rear wall); leg B is set in the "
                                   "Corner group of the inspector."),
                                this);
        legs->setWordWrap(true);
        layout->addWidget(legs);
    }
    layout->addWidget(okCancel(this));
}

TemplateSize TemplateSizeDialog::size() const { return {width_->value(), height_->value(), depth_->value()}; }

PreferencesDialog::PreferencesDialog(const Preferences& current, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Preferences"));
    language_ = new QComboBox(this);
    language_->addItem(tr("System default"), QString());
    language_->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language_->addItem(QString::fromUtf8("Українська"), QStringLiteral("uk"));
    language_->setCurrentIndex(std::max(0, language_->findData(current.language)));
    autosave_ = new QSpinBox(this);
    autosave_->setRange(0, kMaxAutosaveMinutes);
    autosave_->setSuffix(tr(" min"));
    autosave_->setSpecialValueText(tr("Off"));
    autosave_->setValue(current.autosaveMinutes);
    auto* form = new QFormLayout;
    form->addRow(tr("Language:"), language_);
    form->addRow(tr("Autosave every:"), autosave_);

    componentFolders_ = new QListWidget(this);
    componentFolders_->setObjectName(QStringLiteral("componentFolders"));
    componentFolders_->addItems(current.componentFolders);
    componentFolders_->setMinimumHeight(90);
    auto* addFolder = new QPushButton(tr("Add folder…"), this);
    auto* removeFolder = new QPushButton(tr("Remove"), this);
    connect(addFolder, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Component library folder"));
        if (!dir.isEmpty() && componentFolders_->findItems(dir, Qt::MatchExactly).isEmpty())
            componentFolders_->addItem(dir);
    });
    connect(removeFolder, &QPushButton::clicked, this, [this] { delete componentFolders_->currentItem(); });
    auto* folderButtons = new QHBoxLayout;
    folderButtons->addWidget(addFolder);
    folderButtons->addWidget(removeFolder);
    folderButtons->addStretch(1);
    const QString defaultFolder = QString::fromStdString(defaultComponentFolder(false).string());
    auto* folderNote = new QLabel(tr("When the list is empty, the per-user folder is used: %1").arg(defaultFolder), this);
    folderNote->setWordWrap(true);
    auto* folderBox = new QVBoxLayout;
    folderBox->addWidget(componentFolders_);
    folderBox->addLayout(folderButtons);
    folderBox->addWidget(folderNote);
    form->addRow(tr("Component folders:"), folderBox);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    auto* note = new QLabel(tr("A change of language takes effect when the application is restarted. "
                               "Messages from the engineering core (diagnostics) are always in English."),
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addWidget(okCancel(this));
}

Preferences PreferencesDialog::preferences() const {
    Preferences p;
    p.language = language_->currentData().toString();
    p.autosaveMinutes = autosave_->value();
    for (int i = 0; i < componentFolders_->count(); ++i) p.componentFolders << componentFolders_->item(i)->text();
    return p;
}

ShortcutsDialog::ShortcutsDialog(const QList<QAction*>& actions, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Keyboard shortcuts"));
    auto* table = new QTableWidget(this);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({tr("Command"), tr("Shortcut")});
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (QAction* a : actions) {
        if (a->shortcut().isEmpty()) continue;
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(a->text().remove(QLatin1Char('&'))));
        table->setItem(row, 1, new QTableWidgetItem(a->shortcut().toString(QKeySequence::NativeText)));
    }
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->resizeColumnToContents(1);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(table);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(460, 480);
}

} // namespace fos::app::gui
