// SPDX-License-Identifier: AGPL-3.0-only
#include "nesting_settings_form.hpp"

#include "fos/app/nesting_rows.hpp"
#include "fos/app/session.hpp"
#include "inspector_panel.hpp"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

namespace fos::app::gui {

namespace {
/// Wider than the accepted ranges on purpose: the command checks them and its message is shown.
QDoubleSpinBox* spin(QWidget* parent, const char* name, double max, int decimals) {
    auto* s = new QDoubleSpinBox(parent);
    s->setObjectName(QString::fromLatin1(name));
    s->setRange(0, max);
    s->setDecimals(decimals);
    s->setSingleStep(decimals > 1 ? 0.05 : 0.5);
    s->setSuffix(NestingSettingsForm::tr(" mm"));
    return s;
}
} // namespace

NestingSettingsForm::NestingSettingsForm(Session& session, QWidget* parent)
    : QGroupBox(tr("Router nesting settings (all materials)"), parent), session_(session) {
    setObjectName(QStringLiteral("nestingSettings"));
    tool_ = spin(this, "nestTool", 1000, 1);
    tool_->setToolTip(tr("Diameter of the router bit that cuts the shaped parts out; parts are spaced by it."));
    clearance_ = spin(this, "nestClearance", 1000, 1);
    clearance_->setToolTip(tr("Extra distance between parts beyond one tool diameter."));
    chord_ = spin(this, "nestChord", 100, 2);
    chord_->setToolTip(tr("Largest deviation of the straight segments that approximate arcs in the nesting."));
    marginLeft_ = spin(this, "nestMarginLeft", 10000, 1);
    marginRight_ = spin(this, "nestMarginRight", 10000, 1);
    marginBottom_ = spin(this, "nestMarginBottom", 10000, 1);
    marginTop_ = spin(this, "nestMarginTop", 10000, 1);
    iterations_ = new QSpinBox(this);
    iterations_->setObjectName(QStringLiteral("nestIterations"));
    iterations_->setRange(0, 100000);
    iterations_->setToolTip(tr("Improvement passes of the heuristic (more can find fewer sheets, takes longer)."));
    seed_ = new QLineEdit(this);
    seed_->setObjectName(QStringLiteral("nestSeed"));
    seed_->setToolTip(tr("Start value of the heuristic's random choices; the same seed gives the same layout."));
    error_ = makeErrorLabel(this);
    error_->setObjectName(QStringLiteral("nestingError"));
    auto* applyButton = new QPushButton(tr("Apply and re-nest"), this);
    applyButton->setObjectName(QStringLiteral("applyNesting"));

    auto* form = new QFormLayout(this);
    form->addRow(tr("Tool diameter:"), tool_);
    form->addRow(tr("Clearance:"), clearance_);
    form->addRow(tr("Chord tolerance:"), chord_);
    form->addRow(tr("Margin left:"), marginLeft_);
    form->addRow(tr("Margin right:"), marginRight_);
    form->addRow(tr("Margin bottom:"), marginBottom_);
    form->addRow(tr("Margin top:"), marginTop_);
    form->addRow(tr("Iterations:"), iterations_);
    form->addRow(tr("Seed:"), seed_);
    form->addRow(applyButton);
    form->addRow(error_);
    connect(applyButton, &QPushButton::clicked, this, [this] { apply(); });
}

void NestingSettingsForm::load() {
    const project::NestingSettings s = nestingSettingsInput(session_.project());
    tool_->setValue(s.toolDiameter);
    clearance_->setValue(s.clearance);
    chord_->setValue(s.chordTolerance);
    marginLeft_->setValue(s.margins.left);
    marginRight_->setValue(s.margins.right);
    marginBottom_->setValue(s.margins.bottom);
    marginTop_->setValue(s.margins.top);
    iterations_->setValue(s.iterations);
    seed_->setText(QString::number(s.seed));
    showError(error_, {});
}

bool NestingSettingsForm::apply() {
    project::NestingSettings s = nestingSettingsInput(session_.project()); // keeps minRemnantLength
    const auto seed = parseNestingSeed(seed_->text().toStdString());
    if (!seed) {
        showError(error_, tr("Seed must be a whole number from 0 to 4294967295, got “%1”.").arg(seed_->text()));
        return false;
    }
    s.toolDiameter = tool_->value();
    s.clearance = clearance_->value();
    s.chordTolerance = chord_->value();
    s.margins = {marginLeft_->value(), marginRight_->value(), marginBottom_->value(), marginTop_->value()};
    s.iterations = iterations_->value();
    s.seed = *seed;
    auto r = session_.execute(nestingSettingsCommand(s));
    if (!r) {
        showError(error_, QString::fromStdString(r.error().message));
        return false;
    }
    showError(error_, {});
    return true;
}

QString NestingSettingsForm::errorText() const { return error_->text(); }

} // namespace fos::app::gui
