// SPDX-License-Identifier: AGPL-3.0-only
#include "project_browser.hpp"

#include "fos/app/browser_tree.hpp"
#include "fos/app/session.hpp"

#include <QHeaderView>

namespace fos::app::gui {
namespace {
constexpr int kKindRole = Qt::UserRole;
constexpr int kIdRole = Qt::UserRole + 1;

void addNode(QTreeWidgetItem* parent, QTreeWidget* tree, const BrowserNode& node) {
    auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    item->setData(0, kKindRole, static_cast<int>(node.kind));
    item->setData(0, kIdRole, QString::fromStdString(node.id));
    item->setText(0, QString::fromStdString(node.label));
    item->setText(1, QString::fromStdString(node.detail));
    switch (node.kind) {
    case BrowserNode::Kind::Item:
        item->setToolTip(0, node.isCabinet ? ProjectBrowser::tr("Cabinet %1").arg(QString::fromStdString(node.id))
                                           : ProjectBrowser::tr("Panel %1").arg(QString::fromStdString(node.id)));
        if (!node.generated) {
            item->setText(1, ProjectBrowser::tr("not generated - see Diagnostics"));
            item->setForeground(1, Qt::red);
        }
        break;
    case BrowserNode::Kind::Assembly:
        item->setText(0, ProjectBrowser::tr("Assembly (%1 parts)").arg(node.children.size()));
        item->setToolTip(1, ProjectBrowser::tr("Envelope W × D × H, mm"));
        break;
    case BrowserNode::Kind::Part:
        item->setToolTip(0, QString::fromStdString(node.id));
        item->setToolTip(1, ProjectBrowser::tr("Finished size L × W × T, mm"));
        break;
    }
    for (const auto& child : node.children) addNode(item, tree, child);
}
} // namespace

ProjectBrowser::ProjectBrowser(Session& session, QWidget* parent) : QTreeWidget(parent), session_(session) {
    setObjectName(QStringLiteral("projectBrowser"));
    setColumnCount(2);
    setHeaderLabels({tr("Name"), tr("Size (mm)")});
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header()->setStretchLastSection(false);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(&session_, &Session::projectChanged, this, &ProjectBrowser::rebuild);
    connect(&session_, &Session::selectionChanged, this, &ProjectBrowser::syncFromSession);
    connect(this, &QTreeWidget::itemSelectionChanged, this, &ProjectBrowser::pushToSession);
    rebuild();
}

int ProjectBrowser::partCount() const {
    int n = 0;
    for (QTreeWidgetItemIterator it(const_cast<ProjectBrowser*>(this)); *it; ++it)
        if ((*it)->data(0, kKindRole).toInt() == static_cast<int>(BrowserNode::Kind::Part)) ++n;
    return n;
}

void ProjectBrowser::rebuild() {
    syncing_ = true;
    clear();
    for (const auto& node : buildBrowserTree(session_.project(), session_.resolved())) addNode(nullptr, this, node);
    expandToDepth(1);
    syncing_ = false;
    syncFromSession();
}

void ProjectBrowser::syncFromSession() {
    syncing_ = true;
    clearSelection();
    const Selection& sel = session_.selection();
    QTreeWidgetItem* current = nullptr;
    for (QTreeWidgetItemIterator it(this); *it; ++it) {
        const auto kind = static_cast<BrowserNode::Kind>((*it)->data(0, kKindRole).toInt());
        const std::string id = (*it)->data(0, kIdRole).toString().toStdString();
        const bool selected =
            (kind == BrowserNode::Kind::Part &&
             std::find(sel.partIds.begin(), sel.partIds.end(), id) != sel.partIds.end()) ||
            (kind == BrowserNode::Kind::Item && sel.partIds.empty() && id == sel.itemId) ||
            (kind == BrowserNode::Kind::Item &&
             std::find(sel.extraItemIds.begin(), sel.extraItemIds.end(), id) != sel.extraItemIds.end());
        if (selected) {
            (*it)->setSelected(true);
            if (!current) current = *it;
        }
    }
    if (current) scrollToItem(current);
    syncing_ = false;
}

void ProjectBrowser::pushToSession() {
    if (syncing_) return;
    Selection sel;
    std::vector<std::string> items; // Ctrl+clicked item / assembly rows, in tree order
    for (QTreeWidgetItem* item : selectedItems()) {
        const auto kind = static_cast<BrowserNode::Kind>(item->data(0, kKindRole).toInt());
        const std::string id = item->data(0, kIdRole).toString().toStdString();
        if (kind == BrowserNode::Kind::Part) sel.partIds.push_back(id);
        else if (std::find(items.begin(), items.end(), id) == items.end()) items.push_back(id); // item or assembly
    }
    if (!sel.partIds.empty()) {
        // Parts determine the primary item; whole item rows of other items join the selection.
        sel.itemId = session_.itemOfPart(sel.partIds.front());
        for (const auto& id : items)
            if (id != sel.itemId) sel.extraItemIds.push_back(id);
    } else if (!items.empty()) {
        sel.itemId = items.front();
        sel.extraItemIds.assign(items.begin() + 1, items.end());
    }
    session_.setSelection(sel);
}

} // namespace fos::app::gui
