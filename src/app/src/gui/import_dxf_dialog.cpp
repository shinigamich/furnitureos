// SPDX-License-Identifier: AGPL-3.0-only
#include "import_dxf_dialog.hpp"

#include "fos/core/units.hpp"
#include "main_window.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>

namespace fos::app::gui {

ImportDxfDialog::ImportDxfDialog(const furniture::Libraries& libraries, const QString& fileName, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Import DXF outline"));
    auto* form = new QFormLayout(this);
    form->addRow(new QLabel(tr("File: %1").arg(fileName), this));
    name_ = new QLineEdit(QFileInfo(fileName).completeBaseName(), this);
    name_->setObjectName(QStringLiteral("dxfName"));
    material_ = new QComboBox(this);
    material_->setObjectName(QStringLiteral("dxfMaterial"));
    material_->addItem(tr("First material of the thickness below"), QString());
    for (const auto& m : libraries.materials)
        material_->addItem(tr("%1 (%2 mm)").arg(QString::fromStdString(m.name),
                                                 QString::fromStdString(core::formatLength(m.thickness))),
                           QString::fromStdString(m.id.str()));
    if (material_->count() > 1) material_->setCurrentIndex(1);
    thickness_ = new QDoubleSpinBox(this);
    thickness_->setObjectName(QStringLiteral("dxfThickness"));
    thickness_->setRange(0, 200);
    thickness_->setDecimals(1);
    thickness_->setSuffix(tr(" mm"));
    thickness_->setSpecialValueText(tr("from the material"));
    layer_ = new QLineEdit(this);
    layer_->setObjectName(QStringLiteral("dxfLayer"));
    layer_->setPlaceholderText(tr("all layers"));
    units_ = new QComboBox(this);
    units_->setObjectName(QStringLiteral("dxfUnits"));
    units_->addItem(tr("From the file ($INSUNITS; none = mm)"), QString());
    for (const char* u : {"mm", "cm", "m", "in", "ft"}) units_->addItem(QString::fromLatin1(u), QString::fromLatin1(u));
    skipCutouts_ = new QCheckBox(tr("Skip unsupported cut-outs (only circles and rectangles are imported)"), this);
    skipCutouts_->setObjectName(QStringLiteral("dxfSkipCutouts"));
    error_ = new QLabel(this);
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: #b00020;"));
    error_->hide();
    form->addRow(tr("Name:"), name_);
    form->addRow(tr("Material:"), material_);
    form->addRow(tr("Thickness:"), thickness_);
    form->addRow(tr("Layer:"), layer_);
    form->addRow(tr("Units:"), units_);
    form->addRow(skipCutouts_);
    form->addRow(error_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Import"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

project::DxfPanelRequest ImportDxfDialog::request() const {
    project::DxfPanelRequest r;
    r.skipOptionLabel = "\"Skip unsupported cut-outs\"";
    r.name = name_->text().trimmed().toStdString();
    r.materialId = material_->currentData().toString().toStdString();
    if (thickness_->value() > 0) r.thickness = thickness_->value();
    r.skipUnsupportedCutouts = skipCutouts_->isChecked();
    return r;
}

formats::DxfImportOptions ImportDxfDialog::options() const {
    formats::DxfImportOptions o;
    o.layer = layer_->text().trimmed().toStdString();
    const QString u = units_->currentData().toString();
    if (!u.isEmpty()) o.units = formats::dxfUnitFromString(u.toStdString());
    return o;
}

void ImportDxfDialog::showRefusal(const QString& message) {
    error_->setText(message);
    error_->show();
}

namespace {
std::filesystem::path toPath(const QString& s) { return std::filesystem::path(s.toStdString()); }

QString diagnosticsText(const core::Diagnostics& diagnostics) {
    QStringList lines;
    for (const auto& d : diagnostics) {
        const QString severity = d.severity == core::Severity::Error     ? MainWindow::tr("Error")
                                 : d.severity == core::Severity::Warning ? MainWindow::tr("Warning")
                                                                         : MainWindow::tr("Info");
        lines << QStringLiteral("%1: %2").arg(severity, QString::fromStdString(d.message));
    }
    return lines.join(QLatin1Char('\n'));
}
} // namespace

void MainWindow::importDxfOutline() {
    const QString file = QFileDialog::getOpenFileName(this, tr("Import DXF outline"), QString(),
                                                      tr("DXF drawings (*.dxf *.DXF)"));
    if (file.isEmpty()) return;
    ImportDxfDialog dialog(session_.project().libraries, QFileInfo(file).fileName(), this);
    while (dialog.exec() == QDialog::Accepted) {
        auto dxf = formats::importDxfFile(toPath(file), dialog.options());
        if (!dxf) {
            dialog.showRefusal(tr("Cannot read the DXF file: %1").arg(QString::fromStdString(dxf.error().message)));
            continue;
        }
        const project::Project& p = session_.project();
        const project::DxfPanelRequest req = dialog.request();
        const std::string id = project::dxfPanelItemId(p, req.name);
        core::Diagnostics found;
        auto built = project::buildDxfPanelItem(p, dxf.value(), req, id,
                                       geometry::Transform::translation({session_.rightEdge(), 0, 0}), &found);
        QString message = built ? QString() : QString::fromStdString(built.error().message);
        if (built) {
            found = built.value().diagnostics;
            if (auto r = session_.execute(project::commands::addItem(std::move(built.value().item))); !r)
                message = QString::fromStdString(r.error().message);
        }
        if (!message.isEmpty()) {
            QString text = tr("Refused: %1").arg(message);
            if (!found.empty()) text += QStringLiteral("\n\n") + tr("DXF diagnostics:") + QLatin1Char('\n') + diagnosticsText(found);
            dialog.showRefusal(text);
            continue;
        }
        session_.selectItem(id);
        const project::DxfPanelImport& s = built.value();
        statusBar()->showMessage(tr("Imported “%1”: %2 × %3 mm, %4 cut-out(s).")
                                     .arg(QString::fromStdString(req.name),
                                          QString::fromStdString(core::formatLength(s.length)),
                                          QString::fromStdString(core::formatLength(s.width)))
                                     .arg(s.circles + s.rectangles),
                                 8000);
        if (!found.empty())
            QMessageBox::information(this, tr("Import DXF outline"),
                                     tr("The panel was imported. Notes from the DXF import:") + QStringLiteral("\n\n") +
                                         diagnosticsText(found));
        return;
    }
}

} // namespace fos::app::gui
