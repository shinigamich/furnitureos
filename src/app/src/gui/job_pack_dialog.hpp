// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// File > Export job pack…: section checkboxes and "open in the browser" (remembered in
/// QSettings "export/jobPackSections" / "export/jobPackOpen"), then the file dialog; the HTML is
/// fos::app::jobPackHtml of the Session (job_pack_export.hpp).
#include "fos/production/job_pack.hpp"

#include <QDialog>

#include <vector>

class QCheckBox;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class JobPackDialog : public QDialog {
    Q_OBJECT
public:
    explicit JobPackDialog(QWidget* parent = nullptr);
    std::vector<production::JobPackSection> sections() const;
    bool openAfterExport() const;
    void remember() const;

private:
    std::vector<std::pair<production::JobPackSection, QCheckBox*>> boxes_;
    QCheckBox* open_;
};

/// Runs the dialog, writes the file and optionally opens it (QDesktopServices).
void exportJobPack(QWidget* parent, Session& session);

} // namespace fos::app::gui
