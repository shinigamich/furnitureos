// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// "Update available" dialog: version, size, release notes (Markdown) and the choice
/// Update now / Later / Skip this version (or "Open release page" where FurnitureOS cannot
/// install the update itself).
#include "fos/app/update_plan.hpp"

#include <QDialog>

namespace fos::app::gui {

class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    enum class Choice { Later, UpdateNow, Skip, OpenReleasePage };

    UpdateDialog(const ReleaseInfo& release, const UpdateManifest& manifest, const UpdateChoice& choice,
                 const QString& currentVersion, QWidget* parent = nullptr);
    Choice choice() const noexcept { return choice_; }

private:
    Choice choice_ = Choice::Later;
};

} // namespace fos::app::gui
