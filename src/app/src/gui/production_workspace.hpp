// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Production workspace: cut list, parts, materials, edge banding, hardware, machining and
/// bought-in items tables (production::Table, captions in the UI language) with CSV export,
/// and the edge-banding job list (EdgeBandingView).
#include "fos/app/text_tree.hpp"
#include "fos/production/common.hpp"

#include <QString>
#include <QWidget>

#include <vector>

class QTabWidget;
class QTableWidget;
class QLabel;
class QTreeWidget;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class EdgeBandingView;

class ProductionWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit ProductionWorkspace(Session& session, QWidget* parent = nullptr);
    /// Rows of the cut-list table currently shown (tests).
    int cutListRows() const;
    void exportCurrentCsv();
    EdgeBandingView* edgeBandingView() const noexcept { return banding_; }

protected:
    void showEvent(QShowEvent* event) override;

private:
    void refresh();
    void updateNote();
    Session& session_;
    QTabWidget* tabs_;
    QLabel* note_;
    std::vector<QTableWidget*> views_;
    std::vector<production::Table> tables_;
    EdgeBandingView* banding_;
    QString boughtInSummary_;
    bool stale_ = true;
};

/// Fills `view` with `table` (read-only cells).
void fillTable(QTableWidget* view, const production::Table& table);
/// Replaces the content of `view` with `nodes` (emphasis = bold, warning = dark red text).
void fillTree(QTreeWidget* view, const std::vector<TextNode>& nodes);

} // namespace fos::app::gui
