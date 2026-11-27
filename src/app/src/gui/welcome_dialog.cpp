// SPDX-License-Identifier: AGPL-3.0-only
#include "welcome_dialog.hpp"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

namespace fos::app::gui {

WelcomeDialog::WelcomeDialog(const QString& initialLanguage, bool liveLanguage, QWidget* parent)
    : QDialog(parent),
      initialLanguage_(initialLanguage == QLatin1String("uk") ? initialLanguage : QStringLiteral("en")),
      liveLanguage_(liveLanguage) {
    setObjectName(QStringLiteral("welcomeDialog"));
    heading_ = new QLabel(this);
    heading_->setWordWrap(true);
    languageLabel_ = new QLabel(this);
    languageBox_ = new QComboBox(this);
    languageBox_->setObjectName(QStringLiteral("welcomeLanguage"));
    // Language names in their own language: readable whatever the current language is.
    languageBox_->addItem(QStringLiteral("English"), QStringLiteral("en"));
    languageBox_->addItem(QString::fromUtf8("Українська"), QStringLiteral("uk"));
    languageBox_->setCurrentIndex(std::max(0, languageBox_->findData(initialLanguage_)));
    languageLabel_->setBuddy(languageBox_);
    restartNote_ = new QLabel(this);
    restartNote_->setWordWrap(true);
    restartNote_->setVisible(false);
    unitsNote_ = new QLabel(this);
    unitsNote_->setWordWrap(true);

    templateMenu_ = new QMenu(this);
    sampleMenu_ = new QMenu(this);
    newButton_ = new QPushButton(this);
    newButton_->setObjectName(QStringLiteral("welcomeNewFromTemplate"));
    newButton_->setMenu(templateMenu_);
    sampleButton_ = new QPushButton(this);
    sampleButton_->setObjectName(QStringLiteral("welcomeOpenSample"));
    sampleButton_->setMenu(sampleMenu_);
    openButton_ = new QPushButton(this);
    openButton_->setObjectName(QStringLiteral("welcomeOpenFile"));
    closeButton_ = new QPushButton(this);
    closeButton_->setDefault(true);
    connect(openButton_, &QPushButton::clicked, this, [this] { choose({WelcomeChoice::Action::OpenFile, {}, {}}); });
    connect(closeButton_, &QPushButton::clicked, this, &QDialog::reject);
    connect(languageBox_, &QComboBox::currentIndexChanged, this, [this] {
        const QString lang = language();
        if (liveLanguage_) emit languageChanged(lang);
        else restartNote_->setVisible(lang != initialLanguage_);
    });

    auto* languageRow = new QHBoxLayout;
    languageRow->addWidget(languageLabel_);
    languageRow->addWidget(languageBox_);
    languageRow->addStretch(1);
    auto* actions = new QHBoxLayout;
    actions->addWidget(newButton_);
    actions->addWidget(sampleButton_);
    actions->addWidget(openButton_);
    auto* bottom = new QHBoxLayout;
    bottom->addStretch(1);
    bottom->addWidget(closeButton_);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(heading_);
    layout->addLayout(languageRow);
    layout->addWidget(restartNote_);
    layout->addWidget(unitsNote_);
    layout->addSpacing(8);
    layout->addLayout(actions);
    layout->addStretch(1);
    layout->addLayout(bottom);
    retranslate();
    resize(560, sizeHint().height());
}

QString WelcomeDialog::language() const { return languageBox_->currentData().toString(); }

void WelcomeDialog::choose(WelcomeChoice choice) {
    choice_ = std::move(choice);
    accept();
}

void WelcomeDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) retranslate();
    QDialog::changeEvent(event);
}

void WelcomeDialog::retranslate() {
    setWindowTitle(tr("Welcome to FurnitureOS"));
    heading_->setText(tr("<h3>Welcome to FurnitureOS</h3><p>Parametric furniture design, drawings, cut lists "
                         "and production documents.</p>"));
    languageLabel_->setText(tr("&Language:"));
    restartNote_->setText(tr("The main window uses the new language after a restart; you will be asked "
                             "whether to restart now."));
    unitsNote_->setText(tr("All lengths are in millimetres (mm). Change the language later in "
                           "Edit ▸ Preferences; open this dialog again from Help ▸ Welcome."));
    newButton_->setText(tr("New from template"));
    sampleButton_->setText(tr("Open sample project"));
    openButton_->setText(tr("Open file…"));
    closeButton_->setText(tr("Close"));

    templateMenu_->clear();
    for (TemplateKind kind : allTemplates())
        templateMenu_->addAction(templateTitle(kind) + QStringLiteral("…"), this,
                                 [this, kind] { choose({WelcomeChoice::Action::NewFromTemplate, kind, {}}); });
    sampleMenu_->clear();
    for (const SampleProject& sample : sampleProjects()) {
        QAction* a = sampleMenu_->addAction(sample.title, this, [this, key = sample.key] {
            choose({WelcomeChoice::Action::OpenSample, TemplateKind::BaseCabinet, key});
        });
        a->setToolTip(sample.description);
        a->setStatusTip(sample.description);
    }
    sampleMenu_->setToolTipsVisible(true);
}

} // namespace fos::app::gui
