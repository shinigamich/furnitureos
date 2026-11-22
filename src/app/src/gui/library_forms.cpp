// SPDX-License-Identifier: AGPL-3.0-only
// The three form pages of the library editor (materials, edge bands, hardware).
#include "fos/app/session.hpp"
#include "form_widgets.hpp"
#include "library_window.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace fos::app::gui {
namespace {

QDoubleSpinBox* lengthBox(QWidget* parent) {
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(0.0, 100000.0);
    box->setDecimals(2);
    box->setSuffix(QStringLiteral(" mm"));
    return box;
}

QDoubleSpinBox* costBox(const Session& session, QWidget* parent) {
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(0.0, 1e9);
    box->setDecimals(2);
    box->setSuffix(QStringLiteral(" ") + QString::fromStdString(session.project().metadata.currency));
    return box;
}

QLineEdit* textEdit(QWidget* parent, const char* objectName) {
    auto* edit = new QLineEdit(parent);
    edit->setObjectName(QString::fromLatin1(objectName));
    return edit;
}

std::string text(const QLineEdit* edit) { return edit->text().toStdString(); }
void setText(QLineEdit* edit, const std::string& value) { edit->setText(QString::fromStdString(value)); }

template <class T> const T* findEntry(const std::vector<T>& list, const std::string& id) {
    for (const auto& e : list)
        if (e.id.str() == id) return &e;
    return nullptr;
}

/// Swatch button that opens a colour dialog.
class ColorButton : public QPushButton {
public:
    explicit ColorButton(QWidget* parent) : QPushButton(parent) {
        setObjectName(QStringLiteral("libraryColor"));
        connect(this, &QPushButton::clicked, this, [this] {
            const QColor chosen = QColorDialog::getColor(QColor(color_.r, color_.g, color_.b), this);
            if (chosen.isValid())
                setColor({static_cast<std::uint8_t>(chosen.red()), static_cast<std::uint8_t>(chosen.green()),
                          static_cast<std::uint8_t>(chosen.blue())});
        });
    }
    void setColor(const domain::Color& c) {
        color_ = c;
        const QString hex = QString::fromStdString(colorText(c));
        setText(hex);
        setStyleSheet(QStringLiteral("QPushButton { background-color: %1; color: %2; }")
                          .arg(hex, (c.r * 299 + c.g * 587 + c.b * 114) / 1000 > 128 ? QStringLiteral("black")
                                                                                    : QStringLiteral("white")));
    }
    const domain::Color& color() const noexcept { return color_; }

private:
    domain::Color color_;
};

// ------------------------------------------------------------------ materials

class MaterialPage final : public LibraryPage {
public:
    MaterialPage(Session& session, QWidget* parent) : LibraryPage(session, LibraryCategory::Material, parent) {
        auto* form = new QWidget(this);
        auto* f = new QFormLayout(form);
        id_ = textEdit(form, "libraryId");
        name_ = textEdit(form, "libraryName");
        manufacturer_ = textEdit(form, "libraryManufacturer");
        code_ = textEdit(form, "libraryCode");
        supplier_ = textEdit(form, "librarySupplier");
        kind_ = choiceBox(LibraryText::materialKinds(), domain::MaterialKind::Particleboard, form);
        thickness_ = lengthBox(form);
        thickness_->setObjectName(QStringLiteral("libraryThickness"));
        length_ = lengthBox(form);
        width_ = lengthBox(form);
        grain_ = new QCheckBox(LibraryPage::tr("Visible grain along the sheet length"), form);
        color_ = new ColorButton(form);
        costSheet_ = costBox(session, form);
        costArea_ = costBox(session, form);
        costArea_->setObjectName(QStringLiteral("libraryCostPerSquareMeter"));
        density_ = new QDoubleSpinBox(form);
        density_->setObjectName(QStringLiteral("libraryDensity"));
        density_->setRange(0.0, 100000.0);
        density_->setDecimals(2);
        density_->setSuffix(QStringLiteral(" kg/m³"));
        density_->setToolTip(LibraryPage::tr("0 = unknown; used for package weights in packing lists (never estimated)"));
        available_ = new QCheckBox(LibraryPage::tr("Available"), form);
        f->addRow(LibraryPage::tr("Id:"), id_);
        f->addRow(LibraryPage::tr("Name:"), name_);
        f->addRow(LibraryPage::tr("Manufacturer:"), manufacturer_);
        f->addRow(LibraryPage::tr("Code:"), code_);
        f->addRow(LibraryPage::tr("Supplier:"), supplier_);
        f->addRow(LibraryPage::tr("Kind:"), kind_);
        f->addRow(LibraryPage::tr("Thickness:"), thickness_);
        f->addRow(LibraryPage::tr("Sheet length:"), length_);
        f->addRow(LibraryPage::tr("Sheet width:"), width_);
        f->addRow(LibraryPage::tr("Grain:"), grain_);
        f->addRow(LibraryPage::tr("Colour:"), color_);
        f->addRow(LibraryPage::tr("Cost per sheet:"), costSheet_);
        f->addRow(LibraryPage::tr("Cost per m²:"), costArea_);
        f->addRow(LibraryPage::tr("Density:"), density_);
        f->addRow(QString(), available_);
        costSheet_->setToolTip(LibraryPage::tr("0 = unknown"));
        costArea_->setToolTip(LibraryPage::tr("0 = unknown; used by the quote when boards are priced by area"));
        buildLayout(form, true);
    }

protected:
    void showEntry(const std::string& id) override {
        if (const auto* m = findEntry(session_.project().libraries.materials, id)) fill(*m);
    }
    void showNew() override { fill(newMaterial(session_.project())); }
    void showDuplicate(const std::string& id) override {
        if (const auto* m = findEntry(session_.project().libraries.materials, id)) fill(duplicateMaterial(session_.project(), *m));
    }
    void clearForm() override { fill(domain::Material{}); }
    std::string formId() const override { return text(id_); }
    void setIdEditable(bool editable) override { id_->setReadOnly(!editable); }
    LibraryEditResult applyForm(bool isNew) override {
        domain::Material m = base_;
        m.id = domain::MaterialId(text(id_));
        m.name = text(name_);
        m.manufacturer = text(manufacturer_);
        m.code = text(code_);
        m.supplier = text(supplier_);
        m.kind = valueOf<domain::MaterialKind>(kind_);
        m.thickness = thickness_->value();
        m.sheetLength = length_->value();
        m.sheetWidth = width_->value();
        m.hasGrain = grain_->isChecked();
        m.color = color_->color();
        m.costPerSheet = costSheet_->value();
        m.costPerSquareMeter = costArea_->value();
        m.density = density_->value();
        m.available = available_->isChecked();
        return applyMaterial(session_, m, isNew);
    }

private:
    void fill(const domain::Material& m) {
        base_ = m; // texture and custom properties are kept as they are
        setText(id_, m.id.str());
        setText(name_, m.name);
        setText(manufacturer_, m.manufacturer);
        setText(code_, m.code);
        setText(supplier_, m.supplier);
        selectData(kind_, static_cast<int>(m.kind));
        thickness_->setValue(m.thickness);
        length_->setValue(m.sheetLength);
        width_->setValue(m.sheetWidth);
        grain_->setChecked(m.hasGrain);
        color_->setColor(m.color);
        costSheet_->setValue(m.costPerSheet);
        costArea_->setValue(m.costPerSquareMeter);
        density_->setValue(m.density);
        available_->setChecked(m.available);
    }

    domain::Material base_;
    QLineEdit *id_, *name_, *manufacturer_, *code_, *supplier_;
    QComboBox* kind_;
    QDoubleSpinBox *thickness_, *length_, *width_, *costSheet_, *costArea_, *density_;
    QCheckBox *grain_, *available_;
    ColorButton* color_;
};

// ------------------------------------------------------------------ edge bands

class EdgeBandPage final : public LibraryPage {
public:
    EdgeBandPage(Session& session, QWidget* parent) : LibraryPage(session, LibraryCategory::EdgeBand, parent) {
        auto* form = new QWidget(this);
        auto* f = new QFormLayout(form);
        id_ = textEdit(form, "libraryId");
        name_ = textEdit(form, "libraryName");
        manufacturer_ = textEdit(form, "libraryManufacturer");
        code_ = textEdit(form, "libraryCode");
        kind_ = choiceBox(LibraryText::edgeBandKinds(), domain::EdgeBandKind::Abs, form);
        thickness_ = lengthBox(form);
        thickness_->setObjectName(QStringLiteral("libraryThickness"));
        width_ = lengthBox(form);
        width_->setObjectName(QStringLiteral("libraryWidth"));
        color_ = new ColorButton(form);
        cost_ = costBox(session, form);
        f->addRow(LibraryPage::tr("Id:"), id_);
        f->addRow(LibraryPage::tr("Name:"), name_);
        f->addRow(LibraryPage::tr("Manufacturer:"), manufacturer_);
        f->addRow(LibraryPage::tr("Code:"), code_);
        f->addRow(LibraryPage::tr("Kind:"), kind_);
        f->addRow(LibraryPage::tr("Thickness:"), thickness_);
        f->addRow(LibraryPage::tr("Tape width:"), width_);
        f->addRow(LibraryPage::tr("Colour:"), color_);
        f->addRow(LibraryPage::tr("Cost per metre:"), cost_);
        thickness_->setToolTip(LibraryPage::tr("Added to the panel edge"));
        width_->setToolTip(LibraryPage::tr("Must cover the board thickness"));
        cost_->setToolTip(LibraryPage::tr("0 = unknown"));
        buildLayout(form, true);
    }

protected:
    void showEntry(const std::string& id) override {
        if (const auto* e = findEntry(session_.project().libraries.edgeBands, id)) fill(*e);
    }
    void showNew() override { fill(newEdgeBand(session_.project())); }
    void showDuplicate(const std::string& id) override {
        if (const auto* e = findEntry(session_.project().libraries.edgeBands, id)) fill(duplicateEdgeBand(session_.project(), *e));
    }
    void clearForm() override { fill(domain::EdgeBand{}); }
    std::string formId() const override { return text(id_); }
    void setIdEditable(bool editable) override { id_->setReadOnly(!editable); }
    LibraryEditResult applyForm(bool isNew) override {
        domain::EdgeBand e = base_;
        e.id = domain::EdgeBandId(text(id_));
        e.name = text(name_);
        e.manufacturer = text(manufacturer_);
        e.code = text(code_);
        e.kind = valueOf<domain::EdgeBandKind>(kind_);
        e.thickness = thickness_->value();
        e.width = width_->value();
        e.color = color_->color();
        e.costPerMeter = cost_->value();
        return applyEdgeBand(session_, e, isNew);
    }

private:
    void fill(const domain::EdgeBand& e) {
        base_ = e;
        setText(id_, e.id.str());
        setText(name_, e.name);
        setText(manufacturer_, e.manufacturer);
        setText(code_, e.code);
        selectData(kind_, static_cast<int>(e.kind));
        thickness_->setValue(e.thickness);
        width_->setValue(e.width);
        color_->setColor(e.color);
        cost_->setValue(e.costPerMeter);
    }

    domain::EdgeBand base_;
    QLineEdit *id_, *name_, *manufacturer_, *code_;
    QComboBox* kind_;
    QDoubleSpinBox *thickness_, *width_, *cost_;
    ColorButton* color_;
};

// ------------------------------------------------------------------ hardware

class HardwarePage final : public LibraryPage {
public:
    HardwarePage(Session& session, QWidget* parent) : LibraryPage(session, LibraryCategory::Hardware, parent) {
        auto* form = new QWidget(this);
        auto* f = new QFormLayout(form);
        id_ = textEdit(form, "libraryId");
        name_ = textEdit(form, "libraryName");
        manufacturer_ = textEdit(form, "libraryManufacturer");
        code_ = textEdit(form, "libraryCode");
        cost_ = costBox(session, form);
        cost_->setObjectName(QStringLiteral("libraryUnitCost"));
        kind_ = new QLabel(form);
        kind_->setObjectName(QStringLiteral("libraryHardwareKind"));
        verified_ = new QLabel(form);
        verified_->setObjectName(QStringLiteral("libraryVerified"));
        verified_->setWordWrap(true);
        auto* note = new QLabel(LibraryPage::tr("Kind and drilling geometry come from the catalogue and are not edited here. "
                                   "To add a fitting, duplicate a definition of the same kind."),
                                form);
        note->setWordWrap(true);
        f->addRow(LibraryPage::tr("Id:"), id_);
        f->addRow(LibraryPage::tr("Name:"), name_);
        f->addRow(LibraryPage::tr("Manufacturer:"), manufacturer_);
        f->addRow(LibraryPage::tr("Code:"), code_);
        f->addRow(LibraryPage::tr("Unit cost:"), cost_);
        f->addRow(LibraryPage::tr("Kind:"), kind_);
        f->addRow(LibraryPage::tr("Dimensions:"), verified_);
        f->addRow(note);
        cost_->setToolTip(LibraryPage::tr("0 = unknown"));
        buildLayout(form, false);
    }

protected:
    void showEntry(const std::string& id) override {
        if (const auto* h = findEntry(session_.project().libraries.hardware, id)) fill(*h);
    }
    void showNew() override {} // New is not offered: a definition needs catalogue geometry.
    void showDuplicate(const std::string& id) override {
        if (const auto* h = findEntry(session_.project().libraries.hardware, id)) fill(duplicateHardware(session_.project(), *h));
    }
    void clearForm() override { fill(domain::HardwareDefinition{}); }
    std::string formId() const override { return text(id_); }
    void setIdEditable(bool editable) override { id_->setReadOnly(!editable); }
    LibraryEditResult applyForm(bool isNew) override {
        domain::HardwareDefinition h =
            applyHardwareInput(base_, HardwareInput{text(name_), text(manufacturer_), text(code_), cost_->value()});
        h.id = domain::HardwareId(text(id_));
        return applyHardware(session_, h, isNew);
    }

private:
    void fill(const domain::HardwareDefinition& h) {
        base_ = h; // kind, params, verification and custom data are kept
        setText(id_, h.id.str());
        setText(name_, h.name);
        setText(manufacturer_, h.manufacturer);
        setText(code_, h.article);
        cost_->setValue(h.unitCost);
        kind_->setText(LibraryText::hardwareKind(h.kind));
        verified_->setText(h.dimensionsVerified ? LibraryPage::tr("Verified against the manufacturer's data sheet")
                                                : LibraryPage::tr("Not verified (generic or unchecked dimensions)"));
    }

    domain::HardwareDefinition base_;
    QLineEdit *id_, *name_, *manufacturer_, *code_;
    QDoubleSpinBox* cost_;
    QLabel *kind_, *verified_;
};

} // namespace

LibraryPage* makeMaterialPage(Session& session, QWidget* parent) { return new MaterialPage(session, parent); }
LibraryPage* makeEdgeBandPage(Session& session, QWidget* parent) { return new EdgeBandPage(session, parent); }
LibraryPage* makeHardwarePage(Session& session, QWidget* parent) { return new HardwarePage(session, parent); }

} // namespace fos::app::gui
