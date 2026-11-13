// SPDX-License-Identifier: AGPL-3.0-only
#include "inspector_panel.hpp"

#include "enum_text.hpp"
#include "fos/app/session.hpp"
#include "fos/core/units.hpp"
#include "interior_editor.hpp"

#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace fos::app::gui {

QLabel* makeErrorLabel(QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color: #b00020;"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->hide();
    return label;
}

void showError(QLabel* label, const QString& message) {
    if (!label) return;
    label->setText(message);
    label->setVisible(!message.isEmpty());
}

namespace {
QString mm(double v) { return QString::fromStdString(core::formatLength(v)); }
} // namespace

InspectorPanel::InspectorPanel(Session& session, QWidget* parent) : QScrollArea(parent), session_(session) {
    setObjectName(QStringLiteral("inspector"));
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(&session_, &Session::projectChanged, this, &InspectorPanel::rebuild);
    connect(&session_, &Session::selectionChanged, this, &InspectorPanel::rebuild);
    rebuild();
}

void InspectorPanel::rebuild() {
    // The editor that triggered a change may still be inside its signal: delete later.
    if (QWidget* old = takeWidget()) old->deleteLater();
    editors_.clear();
    cabinetTabs_ = nullptr;
    interior_ = nullptr;
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    const Selection& sel = session_.selection();
    if (sel.partIds.size() == 1) {
        buildPart(layout, sel.partIds.front());
    } else if (sel.partIds.size() > 1) {
        layout->addWidget(new QLabel(tr("%1 parts selected.").arg(sel.partIds.size()), content));
    } else if (const project::Item* item = session_.project().findItem(sel.itemId)) {
        if (item->isCabinet()) buildCabinet(layout, sel.itemId);
        else buildPanelItem(layout, sel.itemId);
    } else {
        auto* hint = new QLabel(tr("Select a cabinet or a part in the project browser or the 3D view."), content);
        hint->setWordWrap(true);
        layout->addWidget(hint);
    }
    layout->addStretch(1);
    setWidget(content);
}

bool InspectorPanel::run(const project::Command& command, QLabel* errorLabel) {
    auto r = session_.execute(command);
    if (!r) {
        showError(errorLabel, QString::fromStdString(r.error().message));
        return false;
    }
    return true;
}

void InspectorPanel::buildNameEditor(QVBoxLayout* layout, const std::string& itemId) {
    const project::Item* item = session_.project().findItem(itemId);
    auto* box = new QGroupBox(item->isCabinet() ? tr("Cabinet") : tr("Panel"), layout->parentWidget());
    auto* form = new QFormLayout(box);
    auto* name = new QLineEdit(QString::fromStdString(item->name()), box);
    name->setObjectName(QStringLiteral("itemName"));
    auto* error = makeErrorLabel(box);
    form->addRow(tr("Name:"), name);
    form->addRow(error);
    form->addRow(tr("Id:"), new QLabel(QString::fromStdString(itemId), box));
    connect(name, &QLineEdit::editingFinished, this, [this, itemId, name, error] {
        const std::string text = name->text().trimmed().toStdString();
        const project::Item* current = session_.project().findItem(itemId);
        if (!current || text == current->name()) return;
        run(project::commands::renameItem(itemId, text), error);
    });
    layout->addWidget(box);
}

void InspectorPanel::buildParameters(QVBoxLayout* layout, const std::string& itemId,
                                     const std::vector<ParameterGroup>& groups, bool cabinet) {
    QWidget* parent = layout->parentWidget();
    auto* filter = new QLineEdit(filter_, parent);
    filter->setPlaceholderText(tr("Filter parameters"));
    filter->setClearButtonEnabled(true);
    layout->addWidget(filter);
    auto* note = new QLabel(tr("Enter a number or an expression (e.g. “width / 2”) and press Enter."), parent);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(note);

    std::vector<std::pair<QWidget*, QString>> rowsForFilter;
    for (const auto& group : groups) {
        auto* box = new QGroupBox(kindTitle(group.kind), parent);
        auto* form = new QFormLayout(box);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (const auto& row : group.rows) {
            const QString name = QString::fromStdString(row.name);
            auto* field = new QWidget(box);
            auto* v = new QVBoxLayout(field);
            v->setContentsMargins(0, 0, 0, 0);
            v->setSpacing(1);
            auto* line = new QHBoxLayout;
            auto* edit = new QLineEdit(editText(row), field);
            edit->setObjectName(QStringLiteral("param:") + name);
            line->addWidget(edit, 1);
            const QString unit = unitSuffix(row.kind);
            if (!row.expression.empty()) line->addWidget(new QLabel(QStringLiteral("= ") + valueText(row), field));
            else if (!unit.isEmpty()) line->addWidget(new QLabel(unit, field));
            if (cabinet && row.pinned) {
                auto* reset = new QPushButton(tr("Reset"), field);
                reset->setToolTip(tr("Pinned: overrides the standard formula. Reset restores it."));
                line->addWidget(reset);
                connect(reset, &QPushButton::clicked, this, [this, itemId, n = row.name] {
                    auto r = session_.execute(project::commands::resetParameter(itemId, n));
                    if (!r) showError(editors_[n].error, QString::fromStdString(r.error().message));
                });
            }
            v->addLayout(line);
            auto* error = makeErrorLabel(field);
            v->addWidget(error);
            QString caption = name;
            if (row.derived) caption += row.pinned ? tr(" (pinned)") : tr(" (derived)");
            auto* label = new QLabel(caption, box);
            QString tip = QString::fromStdString(row.description);
            if (row.derived && !row.pinned) tip += (tip.isEmpty() ? QString() : QStringLiteral("\n")) +
                                                   tr("Derived from other parameters; entering a value pins it.");
            label->setToolTip(tip);
            edit->setToolTip(tip);
            if (row.derived) {
                QFont f = label->font();
                f.setItalic(true);
                label->setFont(f);
            }
            form->addRow(label, field);
            editors_[row.name] = {edit, error};
            connect(edit, &QLineEdit::returnPressed, this, [this, itemId, n = row.name, edit] {
                commitParameter(itemId, n, edit);
            });
            rowsForFilter.emplace_back(field, name);
            rowsForFilter.emplace_back(label, name);
        }
        layout->addWidget(box);
    }
    auto apply = [this, rowsForFilter](const QString& text) {
        filter_ = text;
        for (const auto& [w, n] : rowsForFilter) w->setVisible(text.isEmpty() || n.contains(text, Qt::CaseInsensitive));
    };
    apply(filter_);
    connect(filter, &QLineEdit::textChanged, this, apply);
}

bool InspectorPanel::commitParameter(const std::string& itemId, const std::string& name, QLineEdit* editor) {
    auto r = applyParameterInput(session_, itemId, name, editor->text());
    if (!r) {
        // The project is unchanged; keep the typed text so the user can correct it.
        showError(editors_[name].error, QString::fromStdString(r.error().message));
        return false;
    }
    return true;
}

bool InspectorPanel::submitParameter(const QString& name, const QString& text) {
    const auto it = editors_.find(name.toStdString());
    if (it == editors_.end() || !it->second.edit) return false;
    it->second.edit->setText(text);
    const std::string itemId = session_.selection().itemId;
    return commitParameter(itemId, name.toStdString(), it->second.edit);
}

InteriorEditor* InspectorPanel::interiorEditor() const { return interior_; }
QTabWidget* InspectorPanel::cabinetTabs() const { return cabinetTabs_; }

QString InspectorPanel::parameterError(const QString& name) const {
    const auto it = editors_.find(name.toStdString());
    if (it == editors_.end() || !it->second.error || it->second.error->isHidden()) return {};
    return it->second.error->text();
}

void InspectorPanel::buildPanelItem(QVBoxLayout* layout, const std::string& itemId) {
    buildNameEditor(layout, itemId);
    const project::Item* item = session_.project().findItem(itemId);
    buildParameters(layout, itemId, parameterGroups(item->panel()->panel), false);
}

void InspectorPanel::buildPart(QVBoxLayout* layout, const std::string& partId) {
    const domain::Part* part = session_.findPart(partId);
    const project::Resolved* resolved = session_.resolved();
    if (!part || !resolved) return;
    const auto& model = resolved->model;
    auto* box = new QGroupBox(tr("Part (read-only, generated)"), layout->parentWidget());
    auto* form = new QFormLayout(box);
    auto label = [box](const QString& text) {
        auto* l = new QLabel(text, box);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setWordWrap(true);
        return l;
    };
    form->addRow(tr("Name:"), label(QString::fromStdString(part->name)));
    form->addRow(tr("Id:"), label(QString::fromStdString(part->id.str())));
    form->addRow(tr("Role:"), label(roleText(part->role)));
    form->addRow(tr("Finished size:"),
                 label(tr("%1 × %2 × %3 mm").arg(mm(part->length), mm(part->width), mm(part->thickness))));
    const auto cut = domain::cutSize(*part, model);
    form->addRow(tr("Cut size:"), label(tr("%1 × %2 mm").arg(mm(cut.length), mm(cut.width))));
    const domain::Material* material = model.findMaterial(part->material);
    form->addRow(tr("Material:"), label(material ? QString::fromStdString(material->name)
                                                 : tr("%1 (missing)").arg(QString::fromStdString(part->material.str()))));
    form->addRow(tr("Grain:"), label(grainText(part->grain)));
    for (domain::EdgeSide side : {domain::EdgeSide::L1, domain::EdgeSide::L2, domain::EdgeSide::W1, domain::EdgeSide::W2}) {
        const auto& edge = part->edge(side);
        QString text = tr("raw");
        if (edge) {
            const domain::EdgeBand* band = model.findEdgeBand(*edge);
            text = band ? tr("%1 (%2 mm)").arg(QString::fromStdString(band->name), mm(band->thickness))
                        : tr("%1 (missing)").arg(QString::fromStdString(edge->str()));
        }
        form->addRow(tr("Edge %1:").arg(QString::fromLatin1(domain::toString(side))), label(text));
    }
    form->addRow(tr("Operations:"), label(QString::number(part->operations.size())));
    layout->addWidget(box);
    auto* hint = new QLabel(tr("Part sizes follow from the cabinet parameters; select the cabinet to edit them."),
                            layout->parentWidget());
    hint->setWordWrap(true);
    layout->addWidget(hint);
    buildPartOverride(layout, session_.itemOfPart(partId), partId);
}

} // namespace fos::app::gui
