// SPDX-License-Identifier: AGPL-3.0-only
#include "plan_workspace.hpp"

#include "fos/app/edit_tools.hpp"
#include "fos/app/plan_view.hpp"
#include "fos/app/session.hpp"
#include "plan_canvas.hpp"

#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace fos::app::gui {
namespace {

QDoubleSpinBox* mmBox(double value, double min, QWidget* parent) {
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(min, 100000.0);
    box->setDecimals(1);
    box->setSingleStep(10);
    box->setSuffix(QStringLiteral(" mm"));
    box->setValue(value);
    box->setKeyboardTracking(false); // typed values apply on Enter / focus out: one undo step per edit
    return box;
}

} // namespace

PlanWorkspace::PlanWorkspace(Session& session, QWidget* parent) : QWidget(parent), session_(session) {
    setObjectName(QStringLiteral("planWorkspace"));
    canvas_ = new PlanCanvas(this);

    auto button = [this](const QString& text, const QString& tip, const QString& name) {
        auto* b = new QPushButton(text, this);
        b->setToolTip(tip);
        b->setObjectName(name);
        return b;
    };
    auto* fit = button(tr("Fit"), tr("Show all items and the room"), QStringLiteral("planFit"));
    auto* zoomIn = button(tr("Zoom in"), tr("Zoom in (also: mouse wheel)"), QStringLiteral("planZoomIn"));
    auto* zoomOut = button(tr("Zoom out"), tr("Zoom out (also: mouse wheel)"), QStringLiteral("planZoomOut"));
    rotate_ = button(tr("Rotate 90°"), tr("Rotate the selected item 90° counter-clockwise about its centre"),
                     QStringLiteral("planRotate"));
    auto* svg = button(tr("Export SVG…"), tr("Save the plan as an SVG drawing (1 unit = 1 mm)"),
                       QStringLiteral("planExportSvg"));
    connect(fit, &QPushButton::clicked, canvas_, &PlanCanvas::fitToView);
    connect(zoomIn, &QPushButton::clicked, this, [this] { canvas_->zoomBy(1.25); });
    connect(zoomOut, &QPushButton::clicked, this, [this] { canvas_->zoomBy(0.8); });
    connect(rotate_, &QPushButton::clicked, this, &PlanWorkspace::rotateSelected);
    connect(svg, &QPushButton::clicked, this, &PlanWorkspace::exportSvg);

    addRoom_ = button(tr("Add room"), tr("Add a rectangular room to the project (saved with it, undoable)"),
                      QStringLiteral("planAddRoom"));
    removeRoom_ = button(tr("Remove room"), tr("Remove the room from the project (undoable)"),
                         QStringLiteral("planRemoveRoom"));
    roomWidth_ = mmBox(3000, 1, this);
    roomDepth_ = mmBox(2000, 1, this);
    roomHeight_ = mmBox(0, 0, this);
    roomHeight_->setSpecialValueText(tr("not set"));
    roomWall_ = mmBox(100, 0, this);
    roomX_ = mmBox(0, -100000, this);
    roomY_ = mmBox(0, -100000, this);
    roomWidth_->setToolTip(tr("Inner room width along X"));
    roomDepth_->setToolTip(tr("Inner room depth along Y"));
    roomHeight_->setToolTip(tr("Floor to ceiling height (0 = not set)"));
    roomWall_->setToolTip(tr("Wall thickness (drawing only)"));
    roomX_->setToolTip(tr("X of the inner left wall"));
    roomY_->setToolTip(tr("Y of the inner front wall"));
    for (QDoubleSpinBox* box : {roomWidth_, roomDepth_, roomHeight_, roomWall_, roomX_, roomY_})
        connect(box, &QDoubleSpinBox::valueChanged, this, &PlanWorkspace::applyRoomFields);
    connect(addRoom_, &QPushButton::clicked, this, &PlanWorkspace::applyRoomFields);
    connect(removeRoom_, &QPushButton::clicked, this, [this] {
        const auto r = applyPlanRoom(session_, std::nullopt);
        showMessage(r ? QString() : tr("Room refused: %1").arg(QString::fromStdString(r.error().message)));
    });

    auto* tools = new QHBoxLayout;
    for (QWidget* w : std::initializer_list<QWidget*>{fit, zoomIn, zoomOut, rotate_, svg}) tools->addWidget(w);
    tools->addStretch(1);
    auto* room = new QHBoxLayout;
    room->addWidget(addRoom_);
    room->addWidget(removeRoom_);
    room->addWidget(new QLabel(tr("W"), this));
    room->addWidget(roomWidth_);
    room->addWidget(new QLabel(tr("D"), this));
    room->addWidget(roomDepth_);
    room->addWidget(new QLabel(tr("H"), this));
    room->addWidget(roomHeight_);
    room->addWidget(new QLabel(tr("Wall"), this));
    room->addWidget(roomWall_);
    room->addWidget(new QLabel(tr("X"), this));
    room->addWidget(roomX_);
    room->addWidget(new QLabel(tr("Y"), this));
    room->addWidget(roomY_);
    room->addStretch(1);

    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("planMessage"));
    message_->setWordWrap(true);
    auto* hint = new QLabel(tr("Drag an item to move it (snaps to item edges and walls); wheel zooms, middle or "
                               "right button pans. Ctrl+click selects several items; arrow keys move the selection by 1 mm (Shift: "
                               "10 mm). Each move or rotation is one undo step."),
                            this);
    hint->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(tools);
    layout->addLayout(room);
    layout->addWidget(canvas_, 1);
    layout->addWidget(message_);
    layout->addWidget(hint);

    connect(canvas_, &PlanCanvas::itemClicked, this, [this](const QString& id) {
        session_.selectItem(id.toStdString());
    });
    connect(canvas_, &PlanCanvas::itemToggled, this,
            [this](const QString& id) { session_.toggleItemSelected(id.toStdString()); });
    connect(canvas_, &PlanCanvas::nudgeRequested, this, [this](double dx, double dy) {
        if (session_.selection().itemId.empty()) return;
        const auto r = nudgeSelectedItems(session_, {dx, dy, 0});
        showMessage(r ? QString() : tr("Move refused: %1").arg(QString::fromStdString(r.error().message)));
    });
    connect(canvas_, &PlanCanvas::moveRequested, this, [this](const QString& id, double dx, double dy) {
        // Dragging an item of a multi-selection moves the whole selection (one undo step).
        const auto all = session_.selection().allItems();
        const bool group = all.size() > 1 && std::find(all.begin(), all.end(), id.toStdString()) != all.end();
        const auto r = group ? nudgeSelectedItems(session_, {dx, dy, 0}) : applyPlanMove(session_, id.toStdString(), {dx, dy});
        showMessage(r ? QString() : tr("Move refused: %1").arg(QString::fromStdString(r.error().message)));
    });
    connect(&session_, &Session::projectChanged, this, [this] {
        syncRoom();
        stale_ = true;
        if (isVisible()) refresh();
    });
    connect(&session_, &Session::selectionChanged, this, [this] {
        canvas_->setSelectedItem(session_.selection().itemId);
        canvas_->setExtraSelectedItems(session_.selection().extraItemIds);
        updateButtons();
    });
    syncRoom();
    updateButtons();
}

void PlanWorkspace::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (stale_) refresh();
}

void PlanWorkspace::refresh() {
    stale_ = false;
    canvas_->setFootprints(planFootprints(session_));
    canvas_->setSelectedItem(session_.selection().itemId);
    canvas_->setExtraSelectedItems(session_.selection().extraItemIds);
    if (session_.resolved() == nullptr)
        showMessage(tr("The project could not be resolved: %1").arg(QString::fromStdString(session_.resolveError())));
    updateButtons();
}

void PlanWorkspace::applyRoomFields() {
    // Editing the fields of a project without a room changes nothing until "Add room".
    if (!planRoom(session_) && sender() != addRoom_) return;
    project::RoomSettings room;
    room.width = roomWidth_->value();
    room.depth = roomDepth_->value();
    if (roomHeight_->value() > 0) room.height = roomHeight_->value();
    room.wallThickness = roomWall_->value();
    room.origin = {roomX_->value(), roomY_->value()};
    const auto r = applyPlanRoom(session_, room);
    showMessage(r ? QString() : tr("Room refused: %1").arg(QString::fromStdString(r.error().message)));
    if (!r) syncRoom(); // back to the project's values
}

void PlanWorkspace::syncRoom() {
    const auto& room = planRoom(session_);
    if (room) {
        const std::initializer_list<std::pair<QDoubleSpinBox*, double>> values{
            {roomWidth_, room->width},
            {roomDepth_, room->depth},
            {roomHeight_, room->height.value_or(0.0)},
            {roomWall_, room->wallThickness},
            {roomX_, room->origin.x},
            {roomY_, room->origin.y}};
        for (const auto& [box, value] : values) {
            const QSignalBlocker block(box);
            box->setValue(value);
        }
    }
    for (QDoubleSpinBox* box : {roomWidth_, roomDepth_, roomHeight_, roomWall_, roomX_, roomY_})
        box->setEnabled(room.has_value());
    addRoom_->setEnabled(!room);
    removeRoom_->setEnabled(room.has_value());
    canvas_->setRoom(room);
}

void PlanWorkspace::updateButtons() {
    const std::string& id = session_.selection().itemId;
    const auto& fp = canvas_->footprints();
    rotate_->setEnabled(!id.empty() && std::any_of(fp.begin(), fp.end(),
                                                   [&](const PlanFootprint& f) { return f.itemId == id; }));
}

void PlanWorkspace::showMessage(const QString& text) { message_->setText(text); }

void PlanWorkspace::rotateSelected() {
    const std::string id = session_.selection().itemId;
    if (id.empty()) return;
    const auto r = applyPlanRotate(session_, id);
    showMessage(r ? QString() : tr("Rotation refused: %1").arg(QString::fromStdString(r.error().message)));
}

void PlanWorkspace::exportSvg() {
    if (stale_) refresh();
    if (canvas_->footprints().empty()) {
        QMessageBox::information(this, tr("Export plan"), tr("There are no generated items to export."));
        return;
    }
    QString base = QString::fromStdString(session_.filePath().stem().string());
    if (base.isEmpty()) base = tr("plan");
    const QString path = QFileDialog::getSaveFileName(this, tr("Export plan as SVG"), base + QStringLiteral("-plan.svg"),
                                                      tr("SVG drawing (*.svg)"));
    if (path.isEmpty()) return;
    const QString title = QString::fromStdString(session_.project().metadata.name);
    if (!canvas_->exportSvg(path, title.isEmpty() ? tr("Plan") : title))
        QMessageBox::warning(this, tr("Export plan"), tr("Could not write %1.").arg(QDir::toNativeSeparators(path)));
}

} // namespace fos::app::gui
