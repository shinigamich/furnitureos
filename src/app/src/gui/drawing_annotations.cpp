// SPDX-License-Identifier: AGPL-3.0-only
#include "drawing_annotations.hpp"

#include "form_widgets.hpp"
#include "fos/app/session.hpp"
#include "fos/core/units.hpp"
#include "inspector_panel.hpp"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

namespace fos::app::gui {

namespace {
constexpr int kIdRole = Qt::UserRole;

QString mm(double v) { return QString::fromStdString(core::formatLength(v)); }

QDoubleSpinBox* offsetSpin(QWidget* parent, double value) {
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(-300, 300);
    s->setDecimals(1);
    s->setSingleStep(2);
    s->setSuffix(AnnotationPanel::tr(" mm"));
    s->setValue(value);
    s->setToolTip(AnnotationPanel::tr("Paper distance of the dimension line from the measured points: "
                                      "negative = below / to the left."));
    return s;
}
} // namespace

SheetPreview::SheetPreview(QWidget* parent) : QLabel(parent) { setCursor(Qt::CrossCursor); }

void SheetPreview::mousePressEvent(QMouseEvent* event) {
    const QPixmap pm = pixmap();
    if (event->button() != Qt::LeftButton || pm.isNull()) return QLabel::mousePressEvent(event);
    const QPointF origin((width() - pm.width()) / 2.0, (height() - pm.height()) / 2.0);
    emit clicked(event->position() - origin);
}

AnnotationPanel::AnnotationPanel(Session& session, QWidget* parent) : QWidget(parent), session_(session) {
    setObjectName(QStringLiteral("annotationPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 0);
    auto* hint = new QLabel(tr("Click a dimension on the sheet to select it. Edits are stored with the project and "
                               "re-applied whenever the drawing is regenerated."),
                            this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    selectedBox_ = new QGroupBox(tr("Selected dimension"), this);
    auto* form = new QFormLayout(selectedBox_);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    selectedInfo_ = new QLabel(selectedBox_);
    selectedInfo_->setObjectName(QStringLiteral("selectedDimension"));
    selectedInfo_->setWordWrap(true);
    selectedInfo_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(selectedInfo_);
    offset_ = offsetSpin(selectedBox_, 0);
    offset_->setObjectName(QStringLiteral("dimensionOffset"));
    applyOffset_ = new QPushButton(tr("Apply"), selectedBox_);
    applyOffset_->setObjectName(QStringLiteral("applyOffset"));
    auto* offsetRow = new QHBoxLayout;
    offsetRow->addWidget(offset_, 1);
    offsetRow->addWidget(applyOffset_);
    form->addRow(tr("Offset:"), offsetRow);
    text_ = new QLineEdit(selectedBox_);
    text_->setObjectName(QStringLiteral("dimensionText"));
    text_->setPlaceholderText(tr("measured value"));
    text_->setToolTip(tr("Replaces the displayed text. The value it was made against is stored; when the model "
                         "changes, the sheet marks the text with * and a warning is listed below."));
    applyText_ = new QPushButton(tr("Apply"), selectedBox_);
    applyText_->setObjectName(QStringLiteral("applyText"));
    auto* textRow = new QHBoxLayout;
    textRow->addWidget(text_, 1);
    textRow->addWidget(applyText_);
    form->addRow(tr("Text:"), textRow);
    hide_ = new QPushButton(tr("Hide"), selectedBox_);
    hide_->setObjectName(QStringLiteral("hideDimension"));
    reset_ = new QPushButton(tr("Reset"), selectedBox_);
    reset_->setObjectName(QStringLiteral("resetDimension"));
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(hide_);
    buttons->addWidget(reset_);
    form->addRow(buttons);
    layout->addWidget(selectedBox_);

    auto* addBox = new QGroupBox(tr("Add a dimension"), this);
    auto* addForm = new QFormLayout(addBox);
    addForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    orientation_ = choiceBox(orientationChoices(), drawings::DimensionOrientation::Horizontal, addBox);
    orientation_->setObjectName(QStringLiteral("newDimensionOrientation"));
    newOffset_ = offsetSpin(addBox, 10);
    newOffset_->setObjectName(QStringLiteral("newDimensionOffset"));
    pick_ = new QPushButton(tr("Pick two corners"), addBox);
    pick_->setObjectName(QStringLiteral("pickCorners"));
    pick_->setCheckable(true);
    pick_->setToolTip(tr("Then click near two part corners in the same view. The dimension is attached to the "
                         "parts and follows the model."));
    pickStatus_ = new QLabel(addBox);
    pickStatus_->setObjectName(QStringLiteral("pickStatus"));
    pickStatus_->setWordWrap(true);
    pickStatus_->hide();
    addForm->addRow(tr("Direction:"), orientation_);
    addForm->addRow(tr("Offset:"), newOffset_);
    addForm->addRow(pick_);
    addForm->addRow(pickStatus_);
    layout->addWidget(addBox);

    error_ = makeErrorLabel(this);
    error_->setObjectName(QStringLiteral("annotationError"));
    layout->addWidget(error_);

    auto* editsBox = new QGroupBox(tr("Stored edits of this drawing"), this);
    auto* editsLayout = new QVBoxLayout(editsBox);
    edits_ = new QListWidget(editsBox);
    edits_->setObjectName(QStringLiteral("drawingEdits"));
    edits_->setWordWrap(true);
    edits_->setMinimumHeight(90);
    removeEdit_ = new QPushButton(tr("Remove edit"), editsBox);
    removeEdit_->setObjectName(QStringLiteral("removeEdit"));
    findings_ = new QListWidget(editsBox);
    findings_->setObjectName(QStringLiteral("editFindings"));
    findings_->setWordWrap(true);
    findings_->setMinimumHeight(70);
    editsLayout->addWidget(edits_);
    editsLayout->addWidget(removeEdit_);
    editsLayout->addWidget(new QLabel(tr("Findings (stale overrides, orphans):"), editsBox));
    editsLayout->addWidget(findings_);
    layout->addWidget(editsBox, 1);

    connect(applyOffset_, &QPushButton::clicked, this, [this] {
        if (const auto d = drawing_ ? findDimension(*drawing_, selected_) : std::nullopt)
            run(dimensionOffsetCommand(session_.project(), drawingId(), *d, offset_->value()));
    });
    auto applyText = [this] {
        if (const auto d = drawing_ ? findDimension(*drawing_, selected_) : std::nullopt)
            run(dimensionTextCommand(session_.project(), drawingId(), *d, text_->text()));
    };
    connect(applyText_, &QPushButton::clicked, this, applyText);
    connect(text_, &QLineEdit::returnPressed, this, applyText);
    connect(hide_, &QPushButton::clicked, this, [this] {
        if (const auto d = drawing_ ? findDimension(*drawing_, selected_) : std::nullopt)
            run(hideDimensionCommand(session_.project(), drawingId(), *d));
    });
    connect(reset_, &QPushButton::clicked, this, [this] {
        const std::string id = selected_;
        if (run(removeEditCommand(session_.project(), drawingId(), id)) && (!drawing_ || !findDimension(*drawing_, id))) {
            selected_.clear();
            refreshSelection();
        }
    });
    connect(pick_, &QPushButton::toggled, this, [this](bool on) {
        firstPick_.reset();
        setPickStatus(on ? tr("Click near the first corner.") : QString());
        emit overlayChanged();
    });
    connect(edits_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        removeEdit_->setEnabled(item != nullptr);
        if (item) selectDimension(item->data(kIdRole).toString().toStdString());
    });
    connect(removeEdit_, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem* item = edits_->currentItem())
            run(removeEditCommand(session_.project(), drawingId(), item->data(kIdRole).toString().toStdString()));
    });
    setDrawing(nullptr, std::nullopt, 0);
}

void AnnotationPanel::setPickStatus(const QString& text) {
    pickStatus_->setText(text);
    pickStatus_->setVisible(!text.isEmpty());
}

std::string AnnotationPanel::drawingId() const { return request_ ? drawingIdOf(*request_) : std::string(); }

void AnnotationPanel::setDrawing(const drawings::DrawingResult* drawing, const std::optional<DrawingRequest>& request,
                                 std::size_t sheetIndex) {
    const bool sameDrawing = request && request_ && drawingIdOf(*request) == drawingIdOf(*request_);
    drawing_ = drawing;
    request_ = request;
    sheet_ = sheetIndex;
    if (!sameDrawing) {
        selected_.clear();
        firstPick_.reset();
        pick_->setChecked(false);
    }
    // Same drawing regenerated (after an edit): keep the selection while it is still drawn.
    if (drawing_ && !findDimension(*drawing_, selected_)) selected_.clear();
    if (firstPick_ && drawing_ && firstPick_->viewIndex >= drawing_->views.size()) firstPick_.reset();
    setEnabled(drawing_ != nullptr);
    refreshSelection();
    refreshEdits();
}

bool AnnotationPanel::selectDimension(const std::string& id) {
    if (!drawing_ || !findDimension(*drawing_, id)) return false;
    selected_ = id;
    showError(error_, {});
    refreshSelection();
    return true;
}

void AnnotationPanel::refreshSelection() {
    const auto d = drawing_ && !selected_.empty() ? findDimension(*drawing_, selected_) : std::nullopt;
    for (QWidget* w : std::initializer_list<QWidget*>{offset_, applyOffset_, text_, applyText_, hide_, reset_})
        w->setEnabled(d.has_value());
    if (!d) {
        selectedInfo_->setText(tr("Nothing selected."));
        text_->clear();
        emit overlayChanged();
        return;
    }
    const auto& dim = d->dimension;
    const QString direction = dim.orientation == drawings::DimensionOrientation::Horizontal ? tr("Horizontal")
                                                                                            : tr("Vertical");
    QString info = tr("%1 dimension, measures %2 mm").arg(direction, mm(drawings::measuredValue(dim)));
    if (d->user) info += QLatin1Char('\n') + tr("User dimension attached to part corners.");
    if (dim.staleOverride) info += QLatin1Char('\n') + tr("The text override is STALE: the model value changed.");
    selectedInfo_->setText(info);
    selectedInfo_->setToolTip(QString::fromStdString(dim.id));
    offset_->setValue(dim.offset);
    text_->setText(dim.textOverride ? QString::fromStdString(*dim.textOverride) : QString());
    hide_->setEnabled(!d->user);
    reset_->setText(d->user ? tr("Delete") : tr("Reset"));
    const auto* stored = [&]() -> const drawings::DrawingEdits* {
        const auto it = session_.project().drawingEdits.find(drawingId());
        return it == session_.project().drawingEdits.end() ? nullptr : &it->second;
    }();
    reset_->setEnabled(d->user || (stored && stored->elements.count(dim.id) != 0));
    emit overlayChanged();
}

void AnnotationPanel::refreshEdits() {
    edits_->blockSignals(true);
    edits_->clear();
    findings_->clear();
    if (request_) {
        for (const auto& row : drawingEditRows(session_.project(), drawingId(), drawing_)) {
            auto* item = new QListWidgetItem(
                QStringLiteral("%1\n%2").arg(QString::fromStdString(row.id), editRowText(row)), edits_);
            item->setData(kIdRole, QString::fromStdString(row.id));
            if (row.stale || row.orphan) item->setForeground(QColor(0x9a, 0x67, 0x00));
        }
    }
    if (drawing_)
        for (const auto& f : editFindings(*drawing_)) {
            auto* item = new QListWidgetItem(QString::fromStdString(f.message), findings_);
            item->setForeground(QColor(0x9a, 0x67, 0x00));
            item->setToolTip(QString::fromStdString(f.code));
        }
    if (findings_->count() == 0) new QListWidgetItem(tr("None."), findings_);
    edits_->blockSignals(false);
    removeEdit_->setEnabled(false);
}

drawings::Rect2 AnnotationPanel::highlight() const {
    if (!drawing_ || selected_.empty() || sheet_ >= drawing_->sheets.size()) return {};
    return elementBounds(drawing_->sheets[sheet_], selected_);
}

std::vector<geometry::Vec2> AnnotationPanel::markers() const {
    if (!drawing_ || !firstPick_ || sheet_ >= drawing_->sheets.size()) return {};
    const auto& view = drawing_->views[firstPick_->viewIndex];
    for (const auto& placed : drawing_->sheets[sheet_].views)
        if (placed.viewId == view.view.id) return {placed.placement.toPaper(firstPick_->corner.viewPoint)};
    return {};
}

void AnnotationPanel::sheetClicked(geometry::Vec2 paper) {
    if (!drawing_ || !request_) return;
    showError(error_, {});
    if (!pick_->isChecked()) {
        const std::string id = dimensionAt(*drawing_, sheet_, paper);
        selected_ = id;
        refreshSelection();
        return;
    }
    const auto viewIndex = viewAt(*drawing_, sheet_, paper);
    if (!viewIndex) {
        setPickStatus(tr("Click inside a view."));
        return;
    }
    const auto& view = drawing_->views[*viewIndex];
    const drawings::PlacedView* placed = nullptr;
    for (const auto& p : drawing_->sheets[sheet_].views)
        if (p.viewId == view.view.id) placed = &p;
    const project::Resolved* resolved = session_.resolved();
    if (!placed || !resolved) return;
    const auto context = editContext(*resolved, *request_);
    const auto corner = nearestCorner(view, context, placed->placement.toView(paper));
    if (!corner) {
        setPickStatus(tr("No part corner in this view."));
        return;
    }
    if (!firstPick_ || firstPick_->viewIndex != *viewIndex) {
        firstPick_ = Pick{*viewIndex, *corner};
        setPickStatus(tr("First corner: %1, corner %2. Click near the second corner.")
                                 .arg(QString::fromStdString(corner->partName))
                                 .arg(corner->anchor.corner.value_or(0)));
        emit overlayChanged();
        return;
    }
    const Pick first = *firstPick_;
    firstPick_.reset();
    auto command = addUserDimensionCommand(session_.project(), drawingId(), view, context,
                                           valueOf<drawings::DimensionOrientation>(orientation_), first.corner.anchor,
                                           corner->anchor, newOffset_->value());
    const std::string drawing = drawingId();
    if (run(command)) {
        pick_->setChecked(false);
        // The new dimension is the last stored one; select it in the regenerated drawing.
        const auto it = session_.project().drawingEdits.find(drawing);
        if (it != session_.project().drawingEdits.end() && !it->second.dimensions.empty())
            selectDimension(it->second.dimensions.back().id);
    } else {
        setPickStatus(tr("Click near the first corner."));
        emit overlayChanged();
    }
}

bool AnnotationPanel::run(const core::Result<project::Command>& command) {
    if (!command) {
        showError(error_, QString::fromStdString(command.error().message));
        return false;
    }
    auto r = session_.execute(command.value());
    if (!r) {
        showError(error_, QString::fromStdString(r.error().message));
        return false;
    }
    return true;
}

} // namespace fos::app::gui
