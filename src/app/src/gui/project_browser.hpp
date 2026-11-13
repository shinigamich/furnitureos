// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Project browser dock content: items -> assembly -> parts (fos::app::buildBrowserTree),
/// selection synchronised with the Session.
#include <QTreeWidget>

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class ProjectBrowser : public QTreeWidget {
    Q_OBJECT
public:
    explicit ProjectBrowser(Session& session, QWidget* parent = nullptr);

    /// Number of part rows currently shown (for tests and the status bar).
    int partCount() const;

private:
    void rebuild();
    void syncFromSession();
    void pushToSession();

    Session& session_;
    bool syncing_ = false;
};

} // namespace fos::app::gui
