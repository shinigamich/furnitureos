// SPDX-License-Identifier: AGPL-3.0-only
// Cabinet sections of the inspector: tabs, construction options, materials by role, hardware,
// and the per-part override editor.
#include "enum_text.hpp"
#include "form_widgets.hpp"
#include "fos/app/cabinet_parts.hpp"
#include "fos/app/session.hpp"
#include "fos/core/units.hpp"
#include "inspector_panel.hpp"
#include "interior_editor.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace fos::app::gui {
namespace {
/// Tab widget as tall as its CURRENT page (QTabWidget's own size hint is that of the tallest
/// page, which would leave the short tabs with a large empty block in the scrolling inspector).
class CurrentPageTabs : public QTabWidget {
public:
    using QTabWidget::QTabWidget;
    QSize sizeHint() const override {
        QSize s = QTabWidget::sizeHint();
        if (const QWidget* page = currentWidget())
            s.setHeight(tabBar()->sizeHint().height() + page->sizeHint().height() + 8);
        return s;
    }
    // Word-wrapped labels make the pages height-for-width; QTabWidget would again take the
    // tallest page.
    int heightForWidth(int width) const override {
        const QWidget* page = currentWidget();
        if (!page) return QTabWidget::heightForWidth(width);
        const int h = page->hasHeightForWidth() ? page->heightForWidth(width) : page->sizeHint().height();
        return tabBar()->sizeHint().height() + h + 8;
    }
};
} // namespace

void InspectorPanel::buildCabinet(QVBoxLayout* layout, const std::string& itemId) {
    buildNameEditor(layout, itemId);
    const auto* cabinet = session_.project().findItem(itemId)->cabinet();
    auto* tabs = new CurrentPageTabs(layout->parentWidget());
    tabs->setObjectName(QStringLiteral("cabinetTabs"));
    tabs->setDocumentMode(true);
    auto page = [tabs](const QString& title) {
        auto* w = new QWidget(tabs);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(0, 6, 0, 0);
        tabs->addTab(w, title);
        return l;
    };
    QVBoxLayout* parameters = page(tr("Parameters"));
    QVBoxLayout* construction = page(tr("Construction"));
    QVBoxLayout* interior = page(tr("Interior"));
    buildParameters(parameters, itemId, parameterGroups(*cabinet), true);
    buildConstruction(construction, itemId);
    buildMaterials(construction, itemId);
    buildHardware(construction, itemId);
    if (interiorItem_ != itemId) {
        interiorItem_ = itemId;
        interiorPath_.clear();
    }
    interior_ = new InteriorEditor(session_, itemId, interiorPath_, tabs);
    interior->addWidget(interior_);
    for (QVBoxLayout* l : {parameters, construction, interior}) l->addStretch(1);
    // Hidden pages get the Ignored policy so that their minimum height does not count either.
    auto sizeToCurrent = [tabs](int current) {
        for (int i = 0; i < tabs->count(); ++i)
            tabs->widget(i)->setSizePolicy(QSizePolicy::Preferred,
                                           i == current ? QSizePolicy::Preferred : QSizePolicy::Ignored);
        tabs->updateGeometry();
    };
    tabs->setCurrentIndex(std::clamp(cabinetTab_, 0, tabs->count() - 1));
    sizeToCurrent(tabs->currentIndex());
    connect(tabs, &QTabWidget::currentChanged, this, [this, sizeToCurrent](int index) {
        cabinetTab_ = index;
        sizeToCurrent(index);
    });
    cabinetTabs_ = tabs;
    layout->addWidget(tabs);
}

void InspectorPanel::buildHardware(QVBoxLayout* layout, const std::string& itemId) {
    const auto& project = session_.project();
    const furniture::HardwareSelection current = project.findItem(itemId)->cabinet()->hardware;
    auto* box = new QGroupBox(tr("Hardware"), layout->parentWidget());
    auto* form = new QFormLayout(box);
    auto* error = makeErrorLabel(box);
    for (HardwareRole role : kHardwareRoles) {
        auto* combo = narrowCombo(box);
        combo->setObjectName(QStringLiteral("hardware:%1").arg(static_cast<int>(role)));
        const std::string selectedId = selected(current, role).str();
        for (const auto& choice : hardwareChoices(project.libraries, role, selectedId)) {
            combo->addItem(choice.missing ? tr("%1 (not in the library)").arg(QString::fromStdString(choice.id))
                                          : QString::fromStdString(choice.name),
                           QString::fromStdString(choice.id));
            combo->setItemData(combo->count() - 1, QString::fromStdString(choice.id), Qt::ToolTipRole);
        }
        selectData(combo, QString::fromStdString(selectedId));
        form->addRow(hardwareRoleTitle(role), combo);
        connect(combo, qOverload<int>(&QComboBox::activated), this, [this, itemId, role, combo, error, selectedId] {
            const std::string id = combo->currentData().toString().toStdString();
            const project::Item* item = session_.project().findItem(itemId);
            if (id == selectedId || !item || !item->cabinet()) return;
            if (!run(hardwareCommand(*item->cabinet(), role, id), error)) selectData(combo, QString::fromStdString(selectedId));
        });
    }
    form->addRow(error);
    auto* note = new QLabel(tr("Only fittings of the matching kind from the project's hardware library are offered."), box);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    form->addRow(note);
    layout->addWidget(box);
}

void InspectorPanel::buildPartOverride(QVBoxLayout* layout, const std::string& itemId, const std::string& partId) {
    const auto& project = session_.project();
    const project::Item* item = project.findItem(itemId);
    const auto slot = partSlot(itemId, partId);
    if (!item || !item->cabinet() || !slot) return;
    const furniture::CabinetDefinition& cabinet = *item->cabinet();
    const furniture::PartOverride current = currentOverride(cabinet, *slot);
    auto* box = new QGroupBox(tr("Override for this part"), layout->parentWidget());
    box->setObjectName(QStringLiteral("partOverride"));
    auto* form = new QFormLayout(box);
    auto* error = makeErrorLabel(box);
    error->setObjectName(QStringLiteral("partOverrideError"));
    auto* material = narrowCombo(box);
    material->setObjectName(QStringLiteral("overrideMaterial"));
    material->addItem(tr("Cabinet material"), QString());
    for (const auto& m : project.libraries.materials)
        material->addItem(tr("%1 (%2 mm)").arg(QString::fromStdString(m.name),
                                               QString::fromStdString(core::formatLength(m.thickness))),
                          QString::fromStdString(m.id.str()));
    selectData(material, current.material ? QString::fromStdString(current.material->str()) : QString());
    form->addRow(tr("Material:"), material);
    connect(material, qOverload<int>(&QComboBox::activated), this, [this, itemId, slot, material, error] {
        const project::Item* it = session_.project().findItem(itemId);
        if (!it || !it->cabinet()) return;
        const QString id = material->currentData().toString();
        run(partMaterialCommand(*it->cabinet(), *slot,
                                id.isEmpty() ? std::nullopt : std::optional<std::string>(id.toStdString())),
            error);
    });
    // Edge entries: "" = cabinet rule, "-" = raw, otherwise the edge band id.
    const QString raw = QStringLiteral("-");
    for (domain::EdgeSide side : domain::kAllEdges) {
        auto* edge = narrowCombo(box);
        edge->setObjectName(QStringLiteral("overrideEdge:%1").arg(QString::fromLatin1(domain::toString(side))));
        edge->addItem(tr("Cabinet rule"), QString());
        edge->addItem(tr("Raw (no edge band)"), raw);
        for (const auto& eb : project.libraries.edgeBands)
            edge->addItem(QString::fromStdString(eb.name), QString::fromStdString(eb.id.str()));
        const EdgeOverrideChoice choice = edgeChoice(current, side);
        selectData(edge, choice.kind == EdgeOverrideChoice::Kind::CabinetRule ? QString()
                         : choice.kind == EdgeOverrideChoice::Kind::Raw       ? raw
                                                                              : QString::fromStdString(choice.bandId));
        form->addRow(tr("Edge %1:").arg(QString::fromLatin1(domain::toString(side))), edge);
        connect(edge, qOverload<int>(&QComboBox::activated), this, [this, itemId, slot, side, edge, error, raw] {
            const project::Item* it = session_.project().findItem(itemId);
            if (!it || !it->cabinet()) return;
            const QString value = edge->currentData().toString();
            EdgeOverrideChoice c;
            if (value == raw) c.kind = EdgeOverrideChoice::Kind::Raw;
            else if (!value.isEmpty()) c = {EdgeOverrideChoice::Kind::Band, value.toStdString()};
            run(partEdgeCommand(*it->cabinet(), *slot, side, c), error);
        });
    }
    auto* clear = new QPushButton(tr("Remove override"), box);
    clear->setObjectName(QStringLiteral("clearOverride"));
    clear->setEnabled(current.material || !current.edges.empty());
    connect(clear, &QPushButton::clicked, this, [this, itemId, slot, error] {
        const project::Item* it = session_.project().findItem(itemId);
        if (it && it->cabinet()) run(clearPartOverrideCommand(*it->cabinet(), *slot), error);
    });
    form->addRow(clear);
    form->addRow(error);
    layout->addWidget(box);
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
