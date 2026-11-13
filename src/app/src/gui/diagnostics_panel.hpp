// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Diagnostics dock: generation findings and model validation; clicking a row selects its
/// part (or item).
#include "fos/app/diagnostics_rows.hpp"

#include <QTreeWidget>

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class DiagnosticsPanel : public QTreeWidget {
    Q_OBJECT
public:
    explicit DiagnosticsPanel(Session& session, QWidget* parent = nullptr);
    const DiagnosticSummary& summary() const noexcept { return summary_; }

signals:
    void summaryChanged();

private:
    void rebuild();
    Session& session_;
    DiagnosticSummary summary_;
};

} // namespace fos::app::gui
