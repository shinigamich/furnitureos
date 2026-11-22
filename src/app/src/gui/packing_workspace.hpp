// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Packing workspace: packing list (packages and hardware bags per assembly, from
/// production::packingList via app/packing_rows.hpp) and the generic assembly sheets.
#include "fos/app/packing_rows.hpp"

#include <QWidget>

#include <optional>

class QDoubleSpinBox;
class QLabel;
class QTableWidget;
class QTabWidget;
class QTextBrowser;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class PackingWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit PackingWorkspace(Session& session, QWidget* parent = nullptr);
    void exportCsv();
    void exportLabels();
    void exportAssemblySvg();
    void setInput(const PackingInput& input);
    PackingInput input() const;
    // Tests.
    int packageRows() const;
    bool unknownWeightShown() const;
    const QTableWidget* packagesTable() const noexcept { return packages_; }
    const QTextBrowser* assemblyView() const noexcept { return assembly_; }

protected:
    void showEvent(QShowEvent* event) override;

private:
    QWidget* buildPackingPage();
    QWidget* buildAssemblyPage();
    void refresh();
    void fillTables();
    QString exportBaseName() const;

    Session& session_;
    QDoubleSpinBox *maxWeight_, *maxLength_, *maxWidth_, *maxHeight_, *padding_;
    QLabel* banner_;
    QLabel* info_;
    QTableWidget* packages_;
    QTableWidget* bags_;
    QLabel* warnings_;
    QLabel* assemblyNote_;
    QTextBrowser* assembly_;
    QTabWidget* pages_;
    std::optional<production::PackingList> list_;
    std::optional<production::AssemblySheets> sheets_;
    PackingRows rows_;
    bool stale_ = true;
    bool updating_ = false;
};

} // namespace fos::app::gui
