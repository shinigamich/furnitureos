// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// "Edge-banding jobs" page of the Production workspace: production::edgeBandingJobs per band
/// (parts, edges, strip lengths, suggested step, totals), the contour-banding list and the
/// job-list warnings, with CSV and SVG (printable sheets) export. Layout: app/edge_banding_rows.hpp.
#include "fos/app/edge_banding_rows.hpp"

#include <QWidget>

#include <optional>

class QLabel;
class QTreeWidget;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class EdgeBandingView : public QWidget {
    Q_OBJECT
public:
    explicit EdgeBandingView(Session& session, QWidget* parent = nullptr);
    void exportCsv();
    void exportSvg();
    /// Rebuilds from the session (called by the Production workspace).
    void refresh();
    // Tests.
    const QTreeWidget* tree() const noexcept { return tree_; }

private:
    QString exportBaseName() const;
    Session& session_;
    QLabel* summary_;
    QTreeWidget* tree_;
    QLabel* warnings_;
    std::optional<production::EdgeBandingJobs> jobs_;
};

} // namespace fos::app::gui
