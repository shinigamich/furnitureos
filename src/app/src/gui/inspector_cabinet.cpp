// SPDX-License-Identifier: AGPL-3.0-only
// Cabinet sections of the inspector: construction options and materials by role.
#include "enum_text.hpp"
#include "fos/app/session.hpp"
#include "fos/core/units.hpp"
#include "inspector_panel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

namespace fos::app::gui {
namespace {

/// Combo box that does not force the inspector wider than the dock (long translated texts).
QComboBox* narrowCombo(QWidget* parent) {
    auto* box = new QComboBox(parent);
    box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    box->setMinimumContentsLength(12);
    return box;
}

template <class E>
QComboBox* choiceBox(const Choices<E>& choices, E current, QWidget* parent) {
    auto* box = narrowCombo(parent);
    for (const auto& [text, value] : choices) {
        box->addItem(text, static_cast<int>(value));
        if (value == current) box->setCurrentIndex(box->count() - 1);
    }
    return box;
}

template <class E> E valueOf(const QComboBox* box) { return static_cast<E>(box->currentData().toInt()); }

} // namespace

void InspectorPanel::buildCabinet(QVBoxLayout* layout, const std::string& itemId) {
    buildNameEditor(layout, itemId);
    const auto* cabinet = session_.project().findItem(itemId)->cabinet();
    buildParameters(layout, itemId, parameterGroups(*cabinet), true);
    buildConstruction(layout, itemId);
    buildMaterials(layout, itemId);
}

void InspectorPanel::buildConstruction(QVBoxLayout* layout, const std::string& itemId) {
    const furniture::ConstructionOptions o = session_.project().findItem(itemId)->cabinet()->construction;
    auto* box = new QGroupBox(tr("Construction"), layout->parentWidget());
    auto* form = new QFormLayout(box);
    auto* error = makeErrorLabel(box);
    auto* topJoint = choiceBox(jointChoices(), o.topJoint, box);
    auto* bottomJoint = choiceBox(jointChoices(), o.bottomJoint, box);
    auto* top = choiceBox(topChoices(), o.top, box);
    auto* back = choiceBox(backChoices(), o.back, box);
    auto* plinth = choiceBox(plinthChoices(), o.plinth, box);
    auto* topFastener = choiceBox(fastenerChoices(), o.topFastener, box);
    auto* bottomFastener = choiceBox(fastenerChoices(), o.bottomFastener, box);
    auto* dividerFastener = choiceBox(fastenerChoices(), o.dividerFastener, box);
    auto* pins = new QCheckBox(tr("Drill shelf-pin holes"), box);
    pins->setChecked(o.shelfSupports);
    form->addRow(tr("Top joint:"), topJoint);
    form->addRow(tr("Bottom joint:"), bottomJoint);
    form->addRow(tr("Top:"), top);
    form->addRow(tr("Back panel:"), back);
    form->addRow(tr("Plinth:"), plinth);
    form->addRow(tr("Top fastener:"), topFastener);
    form->addRow(tr("Bottom fastener:"), bottomFastener);
    form->addRow(tr("Divider fastener:"), dividerFastener);
    form->addRow(pins);
    form->addRow(error);
    auto commit = [=, this] {
        furniture::ConstructionOptions n;
        n.topJoint = valueOf<furniture::JointKind>(topJoint);
        n.bottomJoint = valueOf<furniture::JointKind>(bottomJoint);
        n.top = valueOf<furniture::TopKind>(top);
        n.back = valueOf<furniture::BackMount>(back);
        n.plinth = valueOf<furniture::PlinthKind>(plinth);
        n.topFastener = valueOf<furniture::JointFastener>(topFastener);
        n.bottomFastener = valueOf<furniture::JointFastener>(bottomFastener);
        n.dividerFastener = valueOf<furniture::JointFastener>(dividerFastener);
        n.shelfSupports = pins->isChecked();
        if (!run(project::commands::setConstruction(itemId, n), error)) {
            // Refused: the project is unchanged, so the editors go back to its values.
            auto reset = [](QComboBox* c, int v) { c->setCurrentIndex(std::max(0, c->findData(v))); };
            reset(topJoint, static_cast<int>(o.topJoint));
            reset(bottomJoint, static_cast<int>(o.bottomJoint));
            reset(top, static_cast<int>(o.top));
            reset(back, static_cast<int>(o.back));
            reset(plinth, static_cast<int>(o.plinth));
            reset(topFastener, static_cast<int>(o.topFastener));
            reset(bottomFastener, static_cast<int>(o.bottomFastener));
            reset(dividerFastener, static_cast<int>(o.dividerFastener));
            pins->setChecked(o.shelfSupports);
        }
    };
    for (QComboBox* c : {topJoint, bottomJoint, top, back, plinth, topFastener, bottomFastener, dividerFastener})
        connect(c, qOverload<int>(&QComboBox::activated), this, commit);
    connect(pins, &QCheckBox::clicked, this, commit);
    layout->addWidget(box);
}

void InspectorPanel::buildMaterials(QVBoxLayout* layout, const std::string& itemId) {
    const auto& project = session_.project();
    const furniture::MaterialAssignment m = project.findItem(itemId)->cabinet()->materials;
    auto* box = new QGroupBox(tr("Materials"), layout->parentWidget());
    auto* form = new QFormLayout(box);
    auto* error = makeErrorLabel(box);

    auto materialBox = [&](const std::string& current, const QString& emptyText) {
        auto* c = narrowCombo(box);
        if (!emptyText.isEmpty()) c->addItem(emptyText, QString());
        for (const auto& mat : project.libraries.materials) {
            c->addItem(tr("%1 (%2 mm)").arg(QString::fromStdString(mat.name),
                                            QString::fromStdString(core::formatLength(mat.thickness))),
                       QString::fromStdString(mat.id.str()));
        }
        c->setCurrentIndex(std::max(0, c->findData(QString::fromStdString(current))));
        return c;
    };
    // Edge boxes: first entry = "" (meaning given by `emptyText`), then the library.
    auto edgeBox = [&](const std::optional<std::string>& current, const QString& emptyText) {
        auto* c = narrowCombo(box);
        c->addItem(emptyText, QString());
        for (const auto& eb : project.libraries.edgeBands)
            c->addItem(QString::fromStdString(eb.name), QString::fromStdString(eb.id.str()));
        c->setCurrentIndex(current ? std::max(0, c->findData(QString::fromStdString(*current))) : 0);
        return c;
    };
    auto* carcass = materialBox(m.carcass.str(), {});
    auto* backMat = materialBox(m.back.str(), tr("None"));
    auto* front = materialBox(m.front.str(), tr("Same as carcass"));
    auto* visible = edgeBox(m.visibleEdge.str().empty() ? std::nullopt : std::optional<std::string>(m.visibleEdge.str()),
                            tr("Raw (no edge band)"));
    auto* hidden = edgeBox(m.hiddenEdge ? std::optional<std::string>(m.hiddenEdge->str()) : std::nullopt,
                           tr("Raw (no edge band)"));
    auto* frontEdge = edgeBox(m.frontEdge ? std::optional<std::string>(m.frontEdge->str()) : std::nullopt,
                              tr("Same as visible edges"));
    form->addRow(tr("Carcass:"), carcass);
    form->addRow(tr("Back panel:"), backMat);
    form->addRow(tr("Fronts:"), front);
    form->addRow(tr("Visible edges:"), visible);
    form->addRow(tr("Hidden end edges:"), hidden);
    form->addRow(tr("Front edges:"), frontEdge);
    form->addRow(error);
    auto commit = [=, this] {
        furniture::MaterialAssignment n = m; // keeps per-part overrides
        n.carcass = domain::MaterialId(carcass->currentData().toString().toStdString());
        n.back = domain::MaterialId(backMat->currentData().toString().toStdString());
        n.front = domain::MaterialId(front->currentData().toString().toStdString());
        n.visibleEdge = domain::EdgeBandId(visible->currentData().toString().toStdString());
        const std::string h = hidden->currentData().toString().toStdString();
        n.hiddenEdge = h.empty() ? std::nullopt : std::optional<domain::EdgeBandId>(domain::EdgeBandId(h));
        const std::string f = frontEdge->currentData().toString().toStdString();
        n.frontEdge = f.empty() ? std::nullopt : std::optional<domain::EdgeBandId>(domain::EdgeBandId(f));
        if (!run(project::commands::setMaterials(itemId, n), error)) {
            // Refused: show the project's (unchanged) assignment again.
            auto reset = [](QComboBox* c, const std::string& v) {
                c->setCurrentIndex(std::max(0, c->findData(QString::fromStdString(v))));
            };
            reset(carcass, m.carcass.str());
            reset(backMat, m.back.str());
            reset(front, m.front.str());
            reset(visible, m.visibleEdge.str());
            reset(hidden, m.hiddenEdge ? m.hiddenEdge->str() : std::string());
            reset(frontEdge, m.frontEdge ? m.frontEdge->str() : std::string());
        }
    };
    for (QComboBox* c : {carcass, backMat, front, visible, hidden, frontEdge})
        connect(c, qOverload<int>(&QComboBox::activated), this, commit);
    layout->addWidget(box);
}

} // namespace fos::app::gui
