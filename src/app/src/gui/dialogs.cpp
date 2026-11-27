// SPDX-License-Identifier: AGPL-3.0-only
#include "dialogs.hpp"

#include "fos/app/autosave.hpp"
#include "fos/app/component_browser.hpp"

#include <QAction>
#include <QCheckBox>
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

std::optional<LabelBarcodeSettings> askLabelBarcode(QWidget* parent) {
    const LabelBarcodeSettings current = loadLabelBarcodeSettings();
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("fos::app::gui::LabelBarcodeDialog", "Part labels"));
    auto* form = new QFormLayout(&dialog);
    auto* barcode = new QComboBox(&dialog);
    barcode->setObjectName(QStringLiteral("labelBarcode"));
    for (const auto& [text, value] : labelBarcodeChoices()) {
        barcode->addItem(text, static_cast<int>(value));
        if (value == current.barcode) barcode->setCurrentIndex(barcode->count() - 1);
    }
    auto* content = new QComboBox(&dialog);
    content->setObjectName(QStringLiteral("labelBarcodeContent"));
    for (const auto& [text, value] : labelBarcodeContentChoices()) {
        content->addItem(text, static_cast<int>(value));
        if (value == current.content) content->setCurrentIndex(content->count() - 1);
    }
    const auto update = [barcode, content] {
        content->setEnabled(barcode->currentData().toInt() != static_cast<int>(production::LabelBarcode::None));
    };
    QObject::connect(barcode, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, update);
    update();
    form->addRow(QCoreApplication::translate("fos::app::gui::LabelBarcodeDialog", "Barcode:"), barcode);
    form->addRow(QCoreApplication::translate("fos::app::gui::LabelBarcodeDialog", "Barcode content:"), content);
    auto* hint = new QLabel(QCoreApplication::translate(
                                "fos::app::gui::LabelBarcodeDialog",
                                "The barcode replaces the part-id line of each label. A label too narrow for a "
                                "readable barcode keeps the text instead, and you are told which ones."),
                            &dialog);
    hint->setWordWrap(true);
    form->addRow(hint);
    form->addRow(okCancel(&dialog));
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    LabelBarcodeSettings chosen;
    chosen.barcode = static_cast<production::LabelBarcode>(barcode->currentData().toInt());
    chosen.content = static_cast<production::LabelBarcodeContent>(content->currentData().toInt());
    saveLabelBarcodeSettings(chosen);
    return chosen;
}

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
    if (kind == TemplateKind::DishwasherFront) {
        auto* gap = new QLabel(tr("The width includes the end panel (gap width + board thickness). Keep it to have the "
                                  "width follow the gap; the gap width (generic 600 mm) is the parameter "
                                  "appliance_gap_width in the inspector. Check the appliance's installation sheet."),
                               this);
        gap->setWordWrap(true);
        layout->addWidget(gap);
    }
    if (kind == TemplateKind::OvenTallHousing) {
        auto* niche = new QLabel(tr("The niche height (generic 595 mm) is the parameter niche_height in the inspector; "
                                    "check the appliance's installation sheet for the niche and ventilation sizes."),
                                 this);
        niche->setWordWrap(true);
        layout->addWidget(niche);
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

    initialConsent_ = current.updateConsent;
    checkUpdates_ = new QCheckBox(tr("Check for updates at startup"), this);
    checkUpdates_->setObjectName(QStringLiteral("checkUpdates"));
    checkUpdates_->setChecked(current.updateConsent == UpdateConsent::Allowed);
    checkUpdates_->setToolTip(tr("Asks GitHub for the list of FurnitureOS releases; no personal data is sent. "
                                 "Updates are only installed after you confirm."));
    updateChannel_ = new QComboBox(this);
    updateChannel_->setObjectName(QStringLiteral("updateChannel"));
    updateChannel_->addItem(tr("Stable releases only"), false);
    updateChannel_->addItem(tr("Include pre-releases (beta)"), true);
    updateChannel_->setCurrentIndex(current.includePrereleases ? 1 : 0);
    form->addRow(tr("Updates:"), checkUpdates_);
    form->addRow(tr("Update channel:"), updateChannel_);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    auto* note = new QLabel(tr("A new language is used after the application restarts; you are asked whether to restart now. "
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
    const bool wasChecked = initialConsent_ == UpdateConsent::Allowed;
    p.updateConsent = checkUpdates_->isChecked() == wasChecked
                          ? initialConsent_
                          : (checkUpdates_->isChecked() ? UpdateConsent::Allowed : UpdateConsent::Declined);
    p.includePrereleases = updateChannel_->currentData().toBool();
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
