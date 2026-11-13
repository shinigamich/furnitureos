// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Production workspace: cut list, parts, materials, edge banding, hardware and machining
/// tables (production::Table, captions in the UI language) with CSV export.
#include "fos/production/common.hpp"

#include <QWidget>

#include <vector>

class QTabWidget;
class QTableWidget;
class QLabel;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class ProductionWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit ProductionWorkspace(Session& session, QWidget* parent = nullptr);
    /// Rows of the cut-list table currently shown (tests).
    int cutListRows() const;
    void exportCurrentCsv();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void refresh();
    Session& session_;
    QTabWidget* tabs_;
    QLabel* note_;
    std::vector<QTableWidget*> views_;
    std::vector<production::Table> tables_;
    bool stale_ = true;
};

/// Fills `view` with `table` (read-only cells).
void fillTable(QTableWidget* view, const production::Table& table);

} // namespace fos::app::gui
