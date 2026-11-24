// SPDX-License-Identifier: AGPL-3.0-only
#include "interior_editor.hpp"

#include "form_widgets.hpp"
#include "fos/app/session.hpp"
#include "inspector_panel.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <initializer_list>
#include <optional>

namespace fos::app::gui {
namespace {
constexpr int kPathRole = Qt::UserRole;

QLabel* hintLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color: palette(mid);"));
    return l;
}

/// Millimetre field of the niche vent. 0 is accepted here so that the furniture module judges
/// the value and its message is shown (it requires > 0).
QDoubleSpinBox* ventSpin(const char* objectName, QWidget* parent) {
    auto* s = new QDoubleSpinBox(parent);
    s->setObjectName(QLatin1String(objectName));
    s->setRange(0, 5000);
    s->setDecimals(1);
    s->setSuffix(QCoreApplication::translate("fos::app::gui::InteriorEditor", " mm"));
    return s;
}
} // namespace

InteriorEditor::InteriorEditor(Session& session, std::string itemId, std::string& selectedPath,
                               QWidget* parent)
    : QWidget(parent), session_(session), itemId_(std::move(itemId)), selectedPath_(selectedPath) {
    setObjectName(QStringLiteral("interiorEditor"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("interiorTree"));
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({tr("Section"), tr("Size"), tr("Content")});
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->header()->setStretchLastSection(true);
    tree_->setWordWrap(true);
    layout->addWidget(tree_);

    splitVertical_ = new QPushButton(tr("Split vertically"), this);
    splitVertical_->setObjectName(QStringLiteral("splitVertical"));
    splitVertical_->setToolTip(tr("Side-by-side sections separated by partitions (left to right)."));
    splitHorizontal_ = new QPushButton(tr("Split horizontally"), this);
    splitHorizontal_->setObjectName(QStringLiteral("splitHorizontal"));
    splitHorizontal_->setToolTip(tr("Stacked sections separated by fixed shelves (bottom to top)."));
    splitParts_ = new QSpinBox(this);
    splitParts_->setObjectName(QStringLiteral("splitParts"));
    splitParts_->setRange(2, 9);
    removeSplit_ = new QPushButton(tr("Remove split"), this);
    removeSplit_->setObjectName(QStringLiteral("removeSplit"));
    removeSplit_->setToolTip(tr("The section becomes one empty section again; its front is kept."));
    auto* reset = new QPushButton(tr("Empty interior"), this);
    reset->setObjectName(QStringLiteral("clearInterior"));
    reset->setToolTip(tr("Replaces the whole interior by one empty section without shelves or front."));
    auto* actions = new QGridLayout;
    actions->addWidget(splitVertical_, 0, 0);
    actions->addWidget(splitHorizontal_, 0, 1);
    auto* parts = new QHBoxLayout;
    parts->addWidget(new QLabel(tr("Into sections:"), this));
    parts->addWidget(splitParts_);
    parts->addStretch(1);
    actions->addLayout(parts, 1, 0);
    actions->addWidget(removeSplit_, 1, 1);
    actions->addWidget(reset, 2, 1);
    actions->setColumnStretch(0, 1);
    actions->setColumnStretch(1, 1);
    layout->addLayout(actions);
    error_ = makeErrorLabel(this);
    error_->setObjectName(QStringLiteral("interiorError"));
    layout->addWidget(error_);

    auto* sectionBox = new QGroupBox(this);
    auto* sectionForm = new QFormLayout(sectionBox);
    sectionForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    selectedTitle_ = new QLabel(sectionBox);
    QFont bold = selectedTitle_->font();
    bold.setBold(true);
    selectedTitle_->setFont(bold);
    sectionForm->addRow(selectedTitle_);
    size_ = new QLineEdit(sectionBox);
    size_->setObjectName(QStringLiteral("sectionSize"));
    size_->setToolTip(tr("300 = fixed size in mm, 1* or 2* = share of the remaining space, "
                         "anything else = expression over the parameters (e.g. width / 3)."));
    setSize_ = new QPushButton(tr("Set size"), sectionBox);
    setSize_->setObjectName(QStringLiteral("setSectionSize"));
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(size_, 1);
    sizeRow->addWidget(setSize_);
    sectionForm->addRow(tr("Size:"), sizeRow);
    shelves_ = new QSpinBox(sectionBox);
    shelves_->setObjectName(QStringLiteral("shelfCount"));
    shelves_->setRange(0, 20);
    shelves_->setKeyboardTracking(false);
    shelves_->setToolTip(tr("Adjustable shelves, evenly distributed (sections that are not split)."));
    sectionForm->addRow(tr("Shelves:"), shelves_);
    sectionForm->addRow(
        hintLabel(tr("Sizes: 300 = millimetres, 1* = share of the rest, or an expression."), sectionBox));
    layout->addWidget(sectionBox);

    auto* frontBox = new QGroupBox(tr("Front of the section"), this);
    auto* frontForm = new QFormLayout(frontBox);
    frontForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    frontKind_ = choiceBox(frontChoices(), FrontInput::Choice::None, frontBox);
    frontKind_->setObjectName(QStringLiteral("frontKind"));
    overlay_ = choiceBox(overlayChoices(), furniture::OverlayKind::Full, frontBox);
    overlay_->setObjectName(QStringLiteral("frontOverlay"));
    hingeSide_ = choiceBox(hingeSideChoices(), furniture::HingeSide::Left, frontBox);
    hingeSide_->setObjectName(QStringLiteral("hingeSide"));
    hinges_ = new QCheckBox(tr("Cup hinges"), frontBox);
    hinges_->setObjectName(QStringLiteral("frontHinges"));
    handle_ = choiceBox(handleChoices(), furniture::HandlePosition::Center, frontBox);
    handle_->setObjectName(QStringLiteral("handlePosition"));
    drawerHeights_ = new QLineEdit(frontBox);
    drawerHeights_->setObjectName(QStringLiteral("drawerHeights"));
    drawerHeights_->setToolTip(
        tr("One size per drawer front, bottom to top, separated by “;” (e.g. 140; 1*; 1*)."));
    // Sliding door count: found again by object name (the editor's header is not changed).
    auto* slidingDoors = new QLineEdit(frontBox);
    slidingDoors->setObjectName(QStringLiteral("slidingDoors"));
    slidingDoors->setToolTip(tr("Number of sliding doors (2 to 6) or an expression over the cabinet "
                                "parameters, e.g. if(width > 2000, 3, 2)."));
    drawerBox_ = narrowCombo(frontBox);
    drawerBox_->setObjectName(QStringLiteral("drawerBox"));
    drawerBox_->addItem(tr("None (fronts only)"), false);
    drawerBox_->addItem(tr("Box on slides"), true);
    drawerBox_->setToolTip(tr("Generates a drawer box (sides, inner front, back, bottom) and two slides "
                              "behind every drawer front."));
    slide_ = narrowCombo(frontBox);
    slide_->setObjectName(QStringLiteral("drawerSlide"));
    drawerBottom_ = choiceBox(drawerBottomChoices(), furniture::DrawerBottomMount::Grooved, frontBox);
    drawerBottom_->setObjectName(QStringLiteral("drawerBottom"));
    nicheVent_ = choiceBox(nicheVentChoices(), furniture::NicheVent::ShelfSlot, frontBox);
    nicheVent_->setObjectName(QStringLiteral("nicheVent"));
    nicheVent_->setToolTip(tr("Ventilation opening of the open appliance niche: a slot at the rear of the panel the "
                              "appliance stands on, or a cut-out at the top of the back panel. Generic sizes: check "
                              "the appliance's installation sheet."));
    ventSize_ = ventSpin("nicheVentSize", frontBox);
    ventSize_->setToolTip(tr("Size of the opening across the air flow: depth of a shelf slot, height of a back "
                             "cut-out."));
    ventEdge_ = ventSpin("nicheVentEdgeDistance", frontBox);
    ventEdge_->setToolTip(tr("Distance of the opening from the back panel (shelf slot) or from the top of the niche "
                             "(back cut-out)."));
    ventEndMargin_ = ventSpin("nicheVentEndMargin", frontBox);
    ventEndMargin_->setToolTip(tr("Distance of both ends of the opening from the panels left and right of the niche."));
    frontStyle_ = choiceBox(frontStyleChoices(), furniture::FrontStyle::Slab, frontBox);
    frontStyle_->setObjectName(QStringLiteral("frontStyle"));
    frontStyle_->setToolTip(tr("Slab = one board; frame and panel = two stiles, two rails and a panel in a groove "
                               "(doors, flaps and drawer fronts)."));
    stileWidth_ = ventSpin("frameStileWidth", frontBox);
    stileWidth_->setToolTip(tr("Visible width of the two vertical stiles."));
    railWidth_ = ventSpin("frameRailWidth", frontBox);
    railWidth_->setToolTip(tr("Visible width of the two horizontal rails between the stiles."));
    panelThickness_ = ventSpin("framePanelThickness", frontBox);
    grooveDepth_ = ventSpin("frameGrooveDepth", frontBox);
    grooveDepth_->setToolTip(tr("Depth of the panel groove in the inner edges of the stiles and rails."));
    panelClearance_ = ventSpin("framePanelClearance", frontBox);
    panelClearance_->setToolTip(tr("Gap between the panel edge and the bottom of the groove."));
    frameJoint_ = choiceBox(frameJointChoices(), furniture::FrameJoint::StubTenon, frontBox);
    frameJoint_->setObjectName(QStringLiteral("frameJoint"));
    panelMaterial_ = narrowCombo(frontBox);
    panelMaterial_->setObjectName(QStringLiteral("framePanelMaterial"));
    applyFront_ = new QPushButton(tr("Apply front"), frontBox);
    applyFront_->setObjectName(QStringLiteral("applyFront"));
    frontForm->addRow(tr("Type:"), frontKind_);
    frontForm->addRow(tr("Overlay:"), overlay_);
    frontForm->addRow(tr("Single door:"), hingeSide_);
    frontForm->addRow(hinges_);
    frontForm->addRow(tr("Handle:"), handle_);
    frontForm->addRow(tr("Style:"), frontStyle_);
    frontForm->addRow(tr("Stile width:"), stileWidth_);
    frontForm->addRow(tr("Rail width:"), railWidth_);
    frontForm->addRow(tr("Panel thickness:"), panelThickness_);
    frontForm->addRow(tr("Groove depth:"), grooveDepth_);
    frontForm->addRow(tr("Panel clearance:"), panelClearance_);
    frontForm->addRow(tr("Frame joint:"), frameJoint_);
    frontForm->addRow(tr("Panel material:"), panelMaterial_);
    frontForm->addRow(tr("Sliding doors:"), slidingDoors);
    frontForm->addRow(tr("Drawer heights:"), drawerHeights_);
    frontForm->addRow(tr("Drawer boxes:"), drawerBox_);
    frontForm->addRow(tr("Slides:"), slide_);
    frontForm->addRow(tr("Drawer bottom:"), drawerBottom_);
    frontForm->addRow(tr("Niche ventilation:"), nicheVent_);
    frontForm->addRow(tr("Opening size:"), ventSize_);
    frontForm->addRow(tr("Distance from back / top:"), ventEdge_);
    frontForm->addRow(tr("End margin:"), ventEndMargin_);
    frontForm->addRow(applyFront_);
    layout->addWidget(frontBox);

    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (!item)
            return;
        selectedPath_ = item->data(0, kPathRole).toString().toStdString();
        showError(error_, {});
        showSelected();
    });
    connect(splitVertical_, &QPushButton::clicked, this, [this] {
        run(splitCommand(itemId_, selectedPath_, furniture::SplitDirection::Vertical, splitParts_->value()));
    });
    connect(splitHorizontal_, &QPushButton::clicked, this, [this] {
        run(splitCommand(itemId_, selectedPath_, furniture::SplitDirection::Horizontal,
                         splitParts_->value()));
    });
    connect(removeSplit_, &QPushButton::clicked, this, [this] {
        const project::Item* item = session_.project().findItem(itemId_);
        if (item && item->cabinet())
            run(removeSplitCommand(*item->cabinet(), itemId_, selectedPath_));
    });
    connect(reset, &QPushButton::clicked, this, [this] { run(clearInteriorCommand(itemId_)); });
    auto commitSize = [this] { run(sectionSizeCommand(itemId_, selectedPath_, size_->text())); };
    connect(setSize_, &QPushButton::clicked, this, commitSize);
    connect(size_, &QLineEdit::returnPressed, this, commitSize);
    connect(shelves_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int count) {
        const SectionRow* row = selectedRow();
        if (!row || count == row->shelfCount)
            return;
        if (!run(project::commands::setShelfCount(itemId_, selectedPath_, count)))
            showSelected();
    });
    auto updateFrontFields = [this] {
        const auto choice = valueOf<FrontInput::Choice>(frontKind_);
        const bool any = choice != FrontInput::Choice::None;
        const bool door = choice == FrontInput::Choice::Door || choice == FrontInput::Choice::DoorPair ||
                          choice == FrontInput::Choice::Flap;
        overlay_->setEnabled(any);
        handle_->setEnabled(any);
        hinges_->setEnabled(door);
        hingeSide_->setEnabled(choice == FrontInput::Choice::Door);
        const bool niche = choice == FrontInput::Choice::ApplianceNiche;
        overlay_->setEnabled(any && !niche && choice != FrontInput::Choice::SlidingDoors);
        handle_->setEnabled(any && !niche);
        if (auto* sliding = findChild<QLineEdit*>(QStringLiteral("slidingDoors")))
            sliding->setEnabled(choice == FrontInput::Choice::SlidingDoors);
        const bool drawers = choice == FrontInput::Choice::DrawerStack;
        drawerHeights_->setEnabled(drawers);
        drawerBox_->setEnabled(drawers);
        const bool box = drawers && drawerBox_->currentData().toBool();
        slide_->setEnabled(box);
        drawerBottom_->setEnabled(box);
        nicheVent_->setEnabled(niche);
        const bool vent = niche && valueOf<furniture::NicheVent>(nicheVent_) != furniture::NicheVent::None;
        ventSize_->setEnabled(vent);
        ventEdge_->setEnabled(vent);
        ventEndMargin_->setEnabled(vent);
        const bool styled = frontStyleApplies(choice);
        frontStyle_->setEnabled(styled);
        const bool frame = styled && valueOf<furniture::FrontStyle>(frontStyle_) == furniture::FrontStyle::FramePanel;
        for (QWidget* w : std::initializer_list<QWidget*>{stileWidth_, railWidth_, panelThickness_, grooveDepth_,
                                                          panelClearance_, frameJoint_, panelMaterial_})
            w->setEnabled(frame);
    };
    connect(frontKind_, qOverload<int>(&QComboBox::currentIndexChanged), this, updateFrontFields);
    connect(drawerBox_, qOverload<int>(&QComboBox::currentIndexChanged), this, updateFrontFields);
    connect(nicheVent_, qOverload<int>(&QComboBox::currentIndexChanged), this, updateFrontFields);
    connect(frontStyle_, qOverload<int>(&QComboBox::currentIndexChanged), this, updateFrontFields);
    connect(applyFront_, &QPushButton::clicked, this, [this] {
        FrontInput in;
        in.choice = valueOf<FrontInput::Choice>(frontKind_);
        in.overlay = valueOf<furniture::OverlayKind>(overlay_);
        in.hingeSide = valueOf<furniture::HingeSide>(hingeSide_);
        in.hinges = hinges_->isChecked();
        in.handle = valueOf<furniture::HandlePosition>(handle_);
        in.drawerHeights = drawerHeights_->text();
        if (const auto* sliding = findChild<QLineEdit*>(QStringLiteral("slidingDoors")))
            in.slidingDoors = sliding->text();
        in.drawerBox = drawerBox_->currentData().toBool();
        in.slide = slide_->currentData().toString().toStdString();
        in.drawerBottom = valueOf<furniture::DrawerBottomMount>(drawerBottom_);
        in.niche.vent = valueOf<furniture::NicheVent>(nicheVent_);
        in.niche.ventSize = ventSize_->value();
        in.niche.ventEdgeDistance = ventEdge_->value();
        in.niche.ventEndMargin = ventEndMargin_->value();
        in.style = valueOf<furniture::FrontStyle>(frontStyle_);
        in.frame.stileWidth = stileWidth_->value();
        in.frame.railWidth = railWidth_->value();
        in.frame.panelThickness = panelThickness_->value();
        in.frame.grooveDepth = grooveDepth_->value();
        in.frame.panelClearance = panelClearance_->value();
        in.frame.joint = valueOf<furniture::FrameJoint>(frameJoint_);
        const std::string material = panelMaterial_->currentData().toString().toStdString();
        in.frame.panelMaterial =
            material.empty() ? std::nullopt : std::optional<domain::MaterialId>(domain::MaterialId(material));
        run(frontCommand(itemId_, selectedPath_, in));
    });

    fill();
    updateFrontFields();
}

void InteriorEditor::fill() {
    const project::Item* item = session_.project().findItem(itemId_);
    rows_ = item && item->cabinet() ? sectionRows(item->cabinet()->interior) : std::vector<SectionRow>{};
    if (std::none_of(rows_.begin(), rows_.end(),
                     [this](const SectionRow& r) { return r.path == selectedPath_; }))
        selectedPath_.clear();
    tree_->blockSignals(true);
    tree_->clear();
    std::vector<QTreeWidgetItem*> stack; // last item per depth
    QTreeWidgetItem* current = nullptr;
    for (const auto& row : rows_) {
        QTreeWidgetItem* parent = row.depth > 0 && static_cast<std::size_t>(row.depth) <= stack.size()
                                      ? stack[static_cast<std::size_t>(row.depth - 1)]
                                      : nullptr;
        auto* node = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
        node->setData(0, kPathRole, QString::fromStdString(row.path));
        node->setText(0, sectionCaption(row));
        node->setText(1, row.isRoot() ? QString() : sizeDescription(row.size));
        node->setText(2, contentText(row));
        node->setToolTip(2, contentText(row));
        stack.resize(static_cast<std::size_t>(row.depth));
        stack.push_back(node);
        if (row.path == selectedPath_)
            current = node;
    }
    tree_->expandAll();
    // Height for the rows (at least 4, at most 10), no empty block below a short tree.
    const int rowHeight = std::max(tree_->sizeHintForRow(0), fontMetrics().height() + 4);
    const int visibleRows = std::clamp(static_cast<int>(rows_.size()), 4, 10);
    tree_->setFixedHeight(tree_->header()->sizeHint().height() + visibleRows * rowHeight +
                          2 * tree_->frameWidth() + 4);
    if (current)
        tree_->setCurrentItem(current);
    tree_->blockSignals(false);
    showSelected();
}

const SectionRow* InteriorEditor::selectedRow() const {
    for (const auto& r : rows_)
        if (r.path == selectedPath_)
            return &r;
    return nullptr;
}

bool InteriorEditor::selectSection(const QString& path) {
    for (QTreeWidgetItemIterator it(tree_); *it; ++it)
        if ((*it)->data(0, kPathRole).toString() == path) {
            tree_->setCurrentItem(*it);
            return true;
        }
    return false;
}

void InteriorEditor::showSelected() {
    const SectionRow* row = selectedRow();
    const bool have = row != nullptr;
    selectedTitle_->setText(have ? tr("Selected: %1").arg(sectionCaption(*row)) : QString());
    splitVertical_->setEnabled(have && row->isLeaf());
    splitHorizontal_->setEnabled(have && row->isLeaf());
    splitParts_->setEnabled(have && row->isLeaf());
    removeSplit_->setEnabled(have && !row->isLeaf());
    size_->setEnabled(have && !row->isRoot());
    setSize_->setEnabled(have && !row->isRoot());
    size_->setText(have && !row->isRoot() ? sizeText(row->size) : QString());
    size_->setPlaceholderText(have && row->isRoot() ? tr("the whole interior has no size of its own")
                                                    : QString());
    shelves_->blockSignals(true);
    shelves_->setValue(have ? row->shelfCount : 0);
    shelves_->setEnabled(have && row->isLeaf());
    shelves_->blockSignals(false);
    const FrontInput in = frontInput(have ? row->front : std::nullopt);
    selectData(frontKind_, static_cast<int>(in.choice));
    selectData(overlay_, static_cast<int>(in.overlay));
    selectData(hingeSide_, static_cast<int>(in.hingeSide));
    selectData(handle_, static_cast<int>(in.handle));
    hinges_->setChecked(in.hinges);
    drawerHeights_->setText(in.drawerHeights);
    if (auto* sliding = findChild<QLineEdit*>(QStringLiteral("slidingDoors")))
        sliding->setText(in.slidingDoors);
    drawerBox_->setCurrentIndex(in.drawerBox ? 1 : 0);
    slide_->clear();
    for (const auto& c : slideChoices(session_.project().libraries, in.slide))
        slide_->addItem(c.missing ? tr("%1 (not in the library)").arg(QString::fromStdString(c.id))
                                  : QString::fromStdString(c.name),
                        QString::fromStdString(c.id));
    selectData(slide_, QString::fromStdString(in.slide));
    selectData(drawerBottom_, static_cast<int>(in.drawerBottom));
    selectData(nicheVent_, static_cast<int>(in.niche.vent));
    ventSize_->setValue(in.niche.ventSize);
    ventEdge_->setValue(in.niche.ventEdgeDistance);
    ventEndMargin_->setValue(in.niche.ventEndMargin);
    selectData(frontStyle_, static_cast<int>(in.style));
    stileWidth_->setValue(in.frame.stileWidth);
    railWidth_->setValue(in.frame.railWidth);
    panelThickness_->setValue(in.frame.panelThickness);
    grooveDepth_->setValue(in.frame.grooveDepth);
    panelClearance_->setValue(in.frame.panelClearance);
    selectData(frameJoint_, static_cast<int>(in.frame.joint));
    const std::string panelMaterial = in.frame.panelMaterial ? in.frame.panelMaterial->str() : std::string{};
    panelMaterial_->clear();
    for (const auto& c : panelMaterialChoices(session_.project().libraries, panelMaterial))
        panelMaterial_->addItem(c.text, QString::fromStdString(c.id));
    selectData(panelMaterial_, QString::fromStdString(panelMaterial));
    applyFront_->setEnabled(have);
}

bool InteriorEditor::run(const core::Result<project::Command>& command) {
    if (!command) {
        showError(error_, QString::fromStdString(command.error().message));
        return false;
    }
    auto r = session_.execute(command.value());
    if (!r) {
        // Refused by the furniture module: the project is unchanged.
        showError(error_, QString::fromStdString(r.error().message));
        return false;
    }
    return true;
}

} // namespace fos::app::gui
