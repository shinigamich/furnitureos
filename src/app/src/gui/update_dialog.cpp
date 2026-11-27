// SPDX-License-Identifier: AGPL-3.0-only
#include "update_dialog.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace fos::app::gui {

UpdateDialog::UpdateDialog(const ReleaseInfo& release, const UpdateManifest& manifest, const UpdateChoice& choice,
                           const QString& currentVersion, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Update available"));
    setObjectName(QStringLiteral("updateDialog"));
    const QString version = QString::fromStdString(manifest.version.toString());
    auto* heading = new QLabel(QStringLiteral("<b>%1</b>").arg(tr("FurnitureOS %1 is available.").arg(version.toHtmlEscaped())), this);
    QString facts = tr("You have version %1.").arg(currentVersion);
    if (!manifest.published.isEmpty()) facts += QLatin1Char(' ') + tr("Published %1.").arg(manifest.published);
    if (choice.asset) facts += QLatin1Char(' ') + tr("Download size: %1.").arg(QLocale().formattedDataSize(choice.asset->size));
    if (release.prerelease || manifest.version.isPrerelease()) facts += QLatin1Char(' ') + tr("This is a pre-release.");
    auto* factsLabel = new QLabel(facts, this);
    factsLabel->setWordWrap(true);

    auto* notes = new QTextBrowser(this);
    notes->setObjectName(QStringLiteral("updateNotes"));
    notes->setOpenExternalLinks(true);
    if (manifest.notes.trimmed().isEmpty())
        notes->setPlainText(tr("No release notes were published for this version."));
    else
        notes->setMarkdown(manifest.notes);
    notes->setMinimumSize(520, 260);

    QString how;
    switch (choice.method) {
    case UpdateMethod::RunInstaller:
        how = tr("“Update now” downloads the installer and checks its SHA-256 checksum. FurnitureOS then closes, "
                 "the installer upgrades this installation silently and FurnitureOS starts again.");
        break;
    case UpdateMethod::SwapPortable:
        how = tr("“Update now” downloads the portable package, checks its SHA-256 checksum and unpacks it next to "
                 "this folder. After FurnitureOS closes, the old folder is replaced by the new one (and restored if "
                 "anything fails) and FurnitureOS starts again. Settings and libraries are kept: they are stored in "
                 "your user profile, not in the program folder.");
        break;
    case UpdateMethod::ManualOnly:
        how = tr("FurnitureOS cannot install this update itself here: automatic installation is only available for "
                 "the Windows portable and installer builds. Open the release page to download it, or update through "
                 "the package manager or build you installed it from.");
        break;
    }
    auto* howLabel = new QLabel(how, this);
    howLabel->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(this);
    if (choice.method == UpdateMethod::ManualOnly) {
        QPushButton* open = buttons->addButton(tr("Open release page"), QDialogButtonBox::AcceptRole);
        connect(open, &QPushButton::clicked, this, [this] { choice_ = Choice::OpenReleasePage; accept(); });
    } else {
        QPushButton* now = buttons->addButton(tr("Update now"), QDialogButtonBox::AcceptRole);
        now->setObjectName(QStringLiteral("updateNow"));
        now->setDefault(true);
        connect(now, &QPushButton::clicked, this, [this] { choice_ = Choice::UpdateNow; accept(); });
    }
    QPushButton* later = buttons->addButton(tr("Later"), QDialogButtonBox::RejectRole);
    connect(later, &QPushButton::clicked, this, [this] { choice_ = Choice::Later; reject(); });
    QPushButton* skip = buttons->addButton(tr("Skip this version"), QDialogButtonBox::DestructiveRole);
    connect(skip, &QPushButton::clicked, this, [this] { choice_ = Choice::Skip; accept(); });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(heading);
    layout->addWidget(factsLabel);
    layout->addWidget(notes, 1);
    layout->addWidget(howLabel);
    layout->addWidget(buttons);
}

} // namespace fos::app::gui
