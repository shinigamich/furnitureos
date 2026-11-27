// SPDX-License-Identifier: AGPL-3.0-only
#include "edit_tools_controller.hpp"

#include "add_panel_dialog.hpp"
#include "fos/app/edit_tools.hpp"
#include "fos/app/item_clipboard.hpp"
#include "fos/app/panel_tools.hpp"
#include "fos/app/session.hpp"
#include "transform_dialog.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QStatusBar>

namespace fos::app::gui {

EditToolsController::EditToolsController(Session& session, QWidget* window, QList<QAction*>& allActions)
    : QObject(window), session_(session), window_(window) {
    auto make = [&](const QString& text, const QString& name, const QKeySequence& key = {}) {
        auto* a = new QAction(text, window);
        a->setObjectName(name);
        if (!key.isEmpty()) a->setShortcut(key);
        allActions.append(a);
        return a;
    };
    addPanel_ = make(tr("Add &panel…"), QStringLiteral("actionAddPanel"));
    addPanel_->setToolTip(tr("Add a free panel at the origin, next to the selection or filling the gap between two "
                             "selected items"));
    copy_ = make(tr("&Copy"), QStringLiteral("actionCopyItems"), QKeySequence::Copy);
    cut_ = make(tr("Cu&t"), QStringLiteral("actionCutItems"), QKeySequence::Cut);
    paste_ = make(tr("&Paste"), QStringLiteral("actionPasteItems"), QKeySequence::Paste);
    transform_ = make(tr("&Move / rotate…"), QStringLiteral("actionTransform"), QKeySequence(Qt::Key_G));
    rotate90_ = make(tr("&Rotate 90°"), QStringLiteral("actionRotate90"), QKeySequence(Qt::Key_R));
    rotate90_->setToolTip(tr("Rotate the selection 90° counter-clockwise about the vertical axis"));
    array_ = make(tr("&Array…"), QStringLiteral("actionArray"));
    const QString axes[3] = {tr("Mirror across X (left ↔ right)"), tr("Mirror across Y (front ↔ back)"),
                             tr("Mirror across Z (top ↔ bottom)")};
    for (int i = 0; i < 3; ++i) {
        mirror_.append(make(axes[i], QStringLiteral("actionMirror%1").arg(i)));
        connect(mirror_.back(), &QAction::triggered, this, [this, i] { mirror(i); });
    }
    const QString edges[6] = {tr("Left edges"), tr("Right edges"), tr("Front faces"),
                              tr("Back faces"), tr("Bottoms"),     tr("Tops")};
    for (int i = 0; i < 6; ++i) {
        align_.append(make(edges[i], QStringLiteral("actionAlign%1").arg(i)));
        connect(align_.back(), &QAction::triggered, this, [this, i] { align(i); });
    }
    const QString dist[3] = {tr("Along X"), tr("Along Y"), tr("Along Z")};
    for (int i = 0; i < 3; ++i) {
        distribute_.append(make(dist[i], QStringLiteral("actionDistribute%1").arg(i)));
        connect(distribute_.back(), &QAction::triggered, this, [this, i] { distribute(i); });
    }
    connect(addPanel_, &QAction::triggered, this, &EditToolsController::addPanel);
    connect(copy_, &QAction::triggered, this, &EditToolsController::copy);
    connect(cut_, &QAction::triggered, this, &EditToolsController::cut);
    connect(paste_, &QAction::triggered, this, &EditToolsController::paste);
    connect(transform_, &QAction::triggered, this, &EditToolsController::transform);
    connect(rotate90_, &QAction::triggered, this, &EditToolsController::rotateQuarter);
    connect(array_, &QAction::triggered, this, &EditToolsController::array);
    connect(&session_, &Session::selectionChanged, this, &EditToolsController::updateEnabled);
    connect(&session_, &Session::projectChanged, this, &EditToolsController::updateEnabled);
    updateEnabled();
}

void EditToolsController::populateMenu(QMenu* edit) {
    edit->addSeparator();
    edit->addAction(addPanel_);
    edit->addSeparator();
    edit->addAction(cut_);
    edit->addAction(copy_);
    edit->addAction(paste_);
    edit->addSeparator();
    edit->addAction(transform_);
    edit->addAction(rotate90_);
    QMenu* mirror = edit->addMenu(tr("M&irror copy"));
    mirror->addActions(mirror_);
    edit->addAction(array_);
    QMenu* align = edit->addMenu(tr("A&lign to first selected"));
    align->addActions(align_);
    QMenu* distribute = edit->addMenu(tr("Distribute &evenly"));
    distribute->addActions(distribute_);
}

QList<QAction*> EditToolsController::multiSelectionActions() const {
    return {transform_, rotate90_, array_, mirror_[0], align_[0], align_[4], distribute_[0], copy_, cut_};
}

void EditToolsController::updateEnabled() {
    const auto n = session_.selection().allItems().size();
    for (QAction* a : {copy_, cut_, transform_, rotate90_, array_}) a->setEnabled(n >= 1);
    for (QAction* a : mirror_) a->setEnabled(n >= 1);
    for (QAction* a : align_) a->setEnabled(n >= 2);
    for (QAction* a : distribute_) a->setEnabled(n >= 3);
}

void EditToolsController::report(const QString& title, const QString& message) {
    QMessageBox::warning(window_, title, message);
}

namespace {
QString err(const core::Error& e) { return QString::fromStdString(e.message); }
QStatusBar* statusOf(QWidget* w) {
    auto* main = qobject_cast<QMainWindow*>(w);
    return main ? main->statusBar() : nullptr;
}
} // namespace

void EditToolsController::addPanel() {
    AddPanelDialog dialog(session_, session_.selection().allItems(), window_);
    if (dialog.exec() != QDialog::Accepted) return;
    if (auto r = applyAddPanel(session_, dialog.request()); !r) report(tr("Add panel"), err(r.error()));
}

void EditToolsController::copy() {
    const auto text = copySelectedItems(session_);
    if (!text) return report(tr("Copy"), err(text.error()));
    auto* mime = new QMimeData;
    const QByteArray bytes = QByteArray::fromStdString(text.value());
    mime->setData(QString::fromLatin1(kItemsMimeType), bytes);
    mime->setText(QString::fromUtf8(bytes));
    QApplication::clipboard()->setMimeData(mime);
    if (auto* bar = statusOf(window_))
        bar->showMessage(tr("%n item(s) copied.", nullptr, static_cast<int>(session_.selection().allItems().size())),
                         5000);
}

void EditToolsController::cut() {
    const auto n = static_cast<int>(session_.selection().allItems().size());
    copy();
    if (auto r = deleteSelectedItems(session_); !r) return report(tr("Cut"), err(r.error()));
    if (auto* bar = statusOf(window_)) bar->showMessage(tr("%n item(s) cut.", nullptr, n), 5000);
}

void EditToolsController::paste() {
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime) return;
    const QString type = QString::fromLatin1(kItemsMimeType);
    const QByteArray bytes = mime->hasFormat(type) ? mime->data(type) : mime->text().toUtf8();
    const auto r = pasteItems(session_, bytes.toStdString());
    if (!r) return report(tr("Paste"), err(r.error()));
    if (auto* bar = statusOf(window_))
        bar->showMessage(tr("%n item(s) pasted.", nullptr, static_cast<int>(r.value().size())), 5000);
}

void EditToolsController::transform() {
    const auto ids = session_.selection().allItems();
    if (ids.empty() || !session_.resolved()) return;
    const auto box = itemsBounds(*session_.resolved(), ids);
    if (!box) return report(tr("Move / rotate"), err(box.error()));
    bool panelsOnly = true;
    for (const auto& id : ids) panelsOnly = panelsOnly && !session_.project().findItem(id)->isCabinet();
    TransformDialog dialog(box.value().min, panelsOnly, static_cast<int>(ids.size()), window_);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto& p = session_.project();
    const auto& rs = *session_.resolved();
    core::Result<project::Command> command = core::makeError(core::ErrorCode::InvalidArgument, "");
    switch (dialog.mode()) {
    case TransformDialog::Mode::Absolute: command = moveItemsToCommand(p, rs, ids, dialog.position()); break;
    case TransformDialog::Mode::Relative: command = translateItemsCommand(p, ids, dialog.offset()); break;
    case TransformDialog::Mode::Rotate:
        command = rotateItemsCommand(p, rs, ids, dialog.rotationAxis(), dialog.angle());
        break;
    }
    if (auto r = applyItemsCommand(session_, command); !r) report(tr("Move / rotate"), err(r.error()));
}

void EditToolsController::rotateQuarter() {
    const auto ids = session_.selection().allItems();
    if (ids.empty() || !session_.resolved()) return;
    if (auto r = applyItemsCommand(session_, rotateItemsCommand(session_.project(), *session_.resolved(), ids, Axis::Z, 90));
        !r)
        report(tr("Rotate"), err(r.error()));
}

void EditToolsController::mirror(int axis) {
    const auto ids = session_.selection().allItems();
    if (ids.empty() || !session_.resolved()) return;
    const auto plan = mirrorItemsCommand(session_.project(), *session_.resolved(), ids, static_cast<Axis>(axis));
    if (auto r = applyCopyPlan(session_, plan); !r) return report(tr("Mirror copy"), err(r.error()));
    if (!plan.value().notReflected.empty())
        if (auto* bar = statusOf(window_))
            bar->showMessage(tr("%n copy(ies) placed mirrored but not reflected (cabinets and shaped panels keep "
                                "their own hand).",
                                nullptr, static_cast<int>(plan.value().notReflected.size())),
                             10000);
}

void EditToolsController::array() {
    const auto ids = session_.selection().allItems();
    if (ids.empty() || !session_.resolved()) return;
    const auto box = itemsBounds(*session_.resolved(), ids);
    if (!box) return report(tr("Array"), err(box.error()));
    ArrayDialog dialog(box.value().size(), window_);
    if (dialog.exec() != QDialog::Accepted) return;
    if (auto r = applyCopyPlan(session_, arrayItemsCommand(session_.project(), ids, dialog.count(), dialog.step())); !r)
        report(tr("Array"), err(r.error()));
}

void EditToolsController::align(int edge) {
    const auto ids = session_.selection().allItems();
    if (!session_.resolved()) return;
    if (auto r = applyItemsCommand(session_, alignItemsCommand(session_.project(), *session_.resolved(), ids,
                                                               static_cast<AlignEdge>(edge)));
        !r)
        report(tr("Align"), err(r.error()));
}

void EditToolsController::distribute(int axis) {
    const auto ids = session_.selection().allItems();
    if (!session_.resolved()) return;
    if (auto r = applyItemsCommand(session_, distributeItemsCommand(session_.project(), *session_.resolved(), ids,
                                                                    static_cast<Axis>(axis)));
        !r)
        report(tr("Distribute"), err(r.error()));
}

} // namespace fos::app::gui
