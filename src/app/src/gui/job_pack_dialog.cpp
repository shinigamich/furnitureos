// SPDX-License-Identifier: AGPL-3.0-only
#include "job_pack_dialog.hpp"

#include "fos/app/job_pack_export.hpp"
#include "fos/app/session.hpp"
#include "fos/production/csv.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDate>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

namespace fos::app::gui {

namespace {
constexpr const char* kSectionsKey = "export/jobPackSections";
constexpr const char* kOpenKey = "export/jobPackOpen";
} // namespace

JobPackDialog::JobPackDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("jobPackDialog"));
    setWindowTitle(tr("Export job pack"));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("One printable HTML document for the workshop (A4 landscape, every section on a new "
                                "page). The content is in English."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* group = new QGroupBox(tr("Sections"), this);
    auto* groupLayout = new QVBoxLayout(group);
    const QSettings settings;
    const QStringList stored = settings.value(QLatin1String(kSectionsKey)).toStringList();
    for (const auto section : production::allJobPackSections()) {
        auto* box = new QCheckBox(jobPackSectionTitle(section), group);
        const QString key = QString::fromLatin1(production::toString(section));
        box->setObjectName(QStringLiteral("jobPack_") + key);
        box->setChecked(stored.isEmpty() || stored.contains(key));
        groupLayout->addWidget(box);
        boxes_.emplace_back(section, box);
    }
    layout->addWidget(group);
    auto* plansNote = new QLabel(tr("Cutting plans are HEURISTIC layouts (as in the Cutting workspace)."), this);
    plansNote->setWordWrap(true);
    layout->addWidget(plansNote);
    open_ = new QCheckBox(tr("Open in the browser after export"), this);
    open_->setObjectName(QStringLiteral("jobPackOpen"));
    open_->setChecked(settings.value(QLatin1String(kOpenKey), true).toBool());
    layout->addWidget(open_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Export…"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    const auto updateOk = [this, buttons] { buttons->button(QDialogButtonBox::Ok)->setEnabled(!sections().empty()); };
    for (auto& [section, box] : boxes_) connect(box, &QCheckBox::toggled, this, updateOk);
    updateOk();
}

std::vector<production::JobPackSection> JobPackDialog::sections() const {
    std::vector<production::JobPackSection> out;
    for (const auto& [section, box] : boxes_)
        if (box->isChecked()) out.push_back(section);
    return out;
}

bool JobPackDialog::openAfterExport() const { return open_->isChecked(); }

void JobPackDialog::remember() const {
    QStringList keys;
    for (const auto section : sections()) keys << QString::fromLatin1(production::toString(section));
    QSettings settings;
    settings.setValue(QLatin1String(kSectionsKey), keys);
    settings.setValue(QLatin1String(kOpenKey), openAfterExport());
}

void exportJobPack(QWidget* parent, Session& session) {
    if (!session.resolved() || session.resolved()->model.assemblies.empty()) {
        QMessageBox::information(parent, JobPackDialog::tr("Export job pack"),
                                 JobPackDialog::tr("There is no generated item to document."));
        return;
    }
    JobPackDialog dialog(parent);
    if (dialog.exec() != QDialog::Accepted) return;
    dialog.remember();
    const QString stem = session.filePath().empty() ? QString::fromStdString(session.project().metadata.name)
                                                    : QString::fromStdString(session.filePath().stem().string());
    const QString file = QFileDialog::getSaveFileName(
        parent, JobPackDialog::tr("Export job pack"),
        QDir(QDir::homePath()).filePath(stem + QStringLiteral("-jobpack.html")), JobPackDialog::tr("HTML files (*.html)"));
    if (file.isEmpty()) return;
    JobPackRequest request;
    request.sections = dialog.sections();
    request.date = QDate::currentDate().toString(Qt::ISODate).toStdString();
    QApplication::setOverrideCursor(Qt::WaitCursor); // the cutting plans may take a moment
    auto html = jobPackHtml(session, request);
    QApplication::restoreOverrideCursor();
    if (!html) {
        QMessageBox::critical(parent, JobPackDialog::tr("Export failed"), QString::fromStdString(html.error().message));
        return;
    }
    if (auto r = production::writeTextFile(std::filesystem::path(file.toStdU16String()), html.value()); !r) {
        QMessageBox::critical(parent, JobPackDialog::tr("Export failed"), QString::fromStdString(r.error().message));
        return;
    }
    if (dialog.openAfterExport() && !QDesktopServices::openUrl(QUrl::fromLocalFile(file)))
        QMessageBox::warning(parent, JobPackDialog::tr("Export job pack"),
                             JobPackDialog::tr("The job pack was written to %1 but no browser could be opened.").arg(file));
}

} // namespace fos::app::gui
