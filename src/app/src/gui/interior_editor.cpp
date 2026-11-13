// SPDX-License-Identifier: AGPL-3.0-only
#include "interior_editor.hpp"

#include "form_widgets.hpp"
#include "fos/app/session.hpp"
#include "inspector_panel.hpp"

#include <QCheckBox>
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

namespace fos::app::gui {
namespace {
constexpr int kPathRole = Qt::UserRole;

QLabel* hintLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color: palette(mid);"));
    return l;
}
} // namespace

InteriorEditor::InteriorEditor(Session& session, std::string itemId, std::string& selectedPath, QWidget* parent)
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
    tree_->setMinimumHeight(150);
    tree_->setMaximumHeight(230);
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
    sectionForm->addRow(hintLabel(tr("Sizes: 300 = millimetres, 1* = share of the rest, or an expression."), sectionBox));
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
    drawerHeights_->setToolTip(tr("One size per drawer front, bottom to top, separated by “;” (e.g. 140; 1*; 1*)."));
    applyFront_ = new QPushButton(tr("Apply front"), frontBox);
    applyFront_->setObjectName(QStringLiteral("applyFront"));
    frontForm->addRow(tr("Type:"), frontKind_);
    frontForm->addRow(tr("Overlay:"), overlay_);
    frontForm->addRow(tr("Single door:"), hingeSide_);
    frontForm->addRow(hinges_);
    frontForm->addRow(tr("Handle:"), handle_);
    frontForm->addRow(tr("Drawer heights:"), drawerHeights_);
    frontForm->addRow(applyFront_);
    layout->addWidget(frontBox);

    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (!item) return;
        selectedPath_ = item->data(0, kPathRole).toString().toStdString();
        showError(error_, {});
        showSelected();
    });
    connect(splitVertical_, &QPushButton::clicked, this, [this] {
        run(splitCommand(itemId_, selectedPath_, furniture::SplitDirection::Vertical, splitParts_->value()));
    });
    connect(splitHorizontal_, &QPushButton::clicked, this, [this] {
        run(splitCommand(itemId_, selectedPath_, furniture::SplitDirection::Horizontal, splitParts_->value()));
    });
    connect(removeSplit_, &QPushButton::clicked, this, [this] {
        const project::Item* item = session_.project().findItem(itemId_);
        if (item && item->cabinet()) run(removeSplitCommand(*item->cabinet(), itemId_, selectedPath_));
    });
    connect(reset, &QPushButton::clicked, this, [this] { run(clearInteriorCommand(itemId_)); });
    auto commitSize = [this] { run(sectionSizeCommand(itemId_, selectedPath_, size_->text())); };
    connect(setSize_, &QPushButton::clicked, this, commitSize);
    connect(size_, &QLineEdit::returnPressed, this, commitSize);
    connect(shelves_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int count) {
        const SectionRow* row = selectedRow();
        if (!row || count == row->shelfCount) return;
        if (!run(project::commands::setShelfCount(itemId_, selectedPath_, count))) showSelected();
    });
    auto updateFrontFields = [this] {
        const auto choice = valueOf<FrontInput::Choice>(frontKind_);
        const bool any = choice != FrontInput::Choice::None;
        const bool door = choice == FrontInput::Choice::Door || choice == FrontInput::Choice::DoorPair;
        overlay_->setEnabled(any);
        handle_->setEnabled(any);
        hinges_->setEnabled(door);
        hingeSide_->setEnabled(choice == FrontInput::Choice::Door);
        drawerHeights_->setEnabled(choice == FrontInput::Choice::DrawerStack);
    };
    connect(frontKind_, qOverload<int>(&QComboBox::currentIndexChanged), this, updateFrontFields);
    connect(applyFront_, &QPushButton::clicked, this, [this] {
        FrontInput in;
        in.choice = valueOf<FrontInput::Choice>(frontKind_);
        in.overlay = valueOf<furniture::OverlayKind>(overlay_);
        in.hingeSide = valueOf<furniture::HingeSide>(hingeSide_);
        in.hinges = hinges_->isChecked();
        in.handle = valueOf<furniture::HandlePosition>(handle_);
        in.drawerHeights = drawerHeights_->text();
        run(frontCommand(itemId_, selectedPath_, in));
    });

    fill();
    updateFrontFields();
}

void InteriorEditor::fill() {
    const project::Item* item = session_.project().findItem(itemId_);
    rows_ = item && item->cabinet() ? sectionRows(item->cabinet()->interior) : std::vector<SectionRow>{};
    if (std::none_of(rows_.begin(), rows_.end(), [this](const SectionRow& r) { return r.path == selectedPath_; }))
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
        if (row.path == selectedPath_) current = node;
    }
    tree_->expandAll();
    if (current) tree_->setCurrentItem(current);
    tree_->blockSignals(false);
    showSelected();
}

const SectionRow* InteriorEditor::selectedRow() const {
    for (const auto& r : rows_)
        if (r.path == selectedPath_) return &r;
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
    size_->setPlaceholderText(have && row->isRoot() ? tr("the whole interior has no size of its own") : QString());
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
