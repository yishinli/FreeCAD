// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Gregg Jaskiewicz
// SPDX-FileNotice: Part of the FreeCAD project.

/******************************************************************************
 *                                                                            *
 *   FreeCAD is free software: you can redistribute it and/or modify          *
 *   it under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1            *
 *   of the License, or (at your option) any later version.                   *
 *                                                                            *
 *   FreeCAD is distributed in the hope that it will be useful,               *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty              *
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                  *
 *   See the GNU Lesser General Public License for more details.              *
 *                                                                            *
 *   You should have received a copy of the GNU Lesser General Public         *
 *   License along with FreeCAD. If not, see https://www.gnu.org/licenses     *
 *                                                                            *
 ******************************************************************************/

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <utility>

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>


#include <App/Application.h>
#include <App/Datums.h>
#include <App/Document.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/CommandT.h>
#include <Gui/Document.h>
#include <Gui/QuantitySpinBox.h>
#include <QSignalBlocker>

#include <numbers>

#include <Precision.hxx>

#include <Base/Console.h>
#include <Gui/View3DInventor.h>
#include <Gui/ViewParams.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/Widgets.h>
#include <App/Material.h>
#include <Mod/Part/App/FeatureSectionAnalysis.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/SectionCap.h>
#include <Mod/Part/Gui/ViewProvider.h>

#include <Base/Converter.h>
#include <Gui/Utilities.h>
#include <Gui/InputHint.h>
#include <Gui/MainWindow.h>

#include "TaskSectionAnalysis.h"
#include "ViewProviderSectionAnalysis.h"


using namespace PartGui;

// -----------------------------------------------------------------------
// SectionAnalysisWidget
// -----------------------------------------------------------------------

namespace
{
/// Smallest length or cosine still worth dividing by.
///
/// Deliberately not Precision::Confusion(): that is a modelling tolerance in
/// millimetres, and these quantities are dimensionless - a unit vector's length
/// and the cosine of an angle. Borrowing a length tolerance for them reads as a
/// geometric claim that is not being made.
constexpr double minUnitMagnitude = 1e-10;

/// How far the plane may be from the one the panel last set and still be it.
constexpr double lastPanelTolerance = 1e-9;

/// True when `normal` points along `axis`, either way round.
///
/// Spelled out because the alternative - three chained comparisons of each
/// component against a tolerance - says how the test is done rather than what it
/// asks, and reads the same for all three axes.
bool isAlignedWith(const Base::Vector3d& normal, const Base::Vector3d& axis)
{
    constexpr double alignmentTolerance = 1e-6;
    return std::abs(std::abs(normal * axis) - 1.0) < alignmentTolerance;
}

/// Rodrigues' rotation of v about the unit axis k by theta.
Base::Vector3d rotateAbout(const Base::Vector3d& v, const Base::Vector3d& k, double theta)
{
    const double ct = std::cos(theta);
    const double st = std::sin(theta);
    return v * ct + k.Cross(v) * st + k * (k * v) * (1.0 - ct);
}

/// The two world axes the tilt boxes turn a base normal about.
void tiltAxes(const Base::Vector3d& baseNormal, Base::Vector3d& angle1Axis, Base::Vector3d& angle2Axis)
{
    // Rotate about world axes lying in the plane, not about an arbitrary frame:
    // the angles are meant to read as "tilt about X", and the boxes are labelled
    // that way.
    // Whichever world axis the normal leans on most is the one there is no
    // point turning about; the other two are the tilt axes. Picking the largest
    // component rather than testing against a threshold keeps this right for the
    // presets that are not axis aligned - view direction, or none at all - where a
    // threshold could fall through and hand back an axis nearly parallel to the
    // normal, which is a degenerate pivot.
    const double alongX = std::abs(baseNormal.x);
    const double alongY = std::abs(baseNormal.y);
    const double alongZ = std::abs(baseNormal.z);
    if (alongZ >= alongX && alongZ >= alongY) {
        angle1Axis = Base::Vector3d::UnitX;
        angle2Axis = Base::Vector3d::UnitY;
    }
    else if (alongY >= alongX) {
        angle1Axis = Base::Vector3d::UnitX;
        angle2Axis = Base::Vector3d::UnitZ;
    }
    else {
        angle1Axis = Base::Vector3d::UnitY;
        angle2Axis = Base::Vector3d::UnitZ;
    }
}

/// The signed world axis a normal leans on most, ties broken as tiltAxes()
/// breaks them. From it, any normal is reached by tilts of at most 90 degrees.
Base::Vector3d nearestAxis(const Base::Vector3d& normal)
{
    const double alongX = std::abs(normal.x);
    const double alongY = std::abs(normal.y);
    const double alongZ = std::abs(normal.z);
    if (alongZ >= alongX && alongZ >= alongY) {
        return normal.z < 0.0 ? -Base::Vector3d::UnitZ : Base::Vector3d::UnitZ;
    }
    if (alongY >= alongX) {
        return normal.y < 0.0 ? -Base::Vector3d::UnitY : Base::Vector3d::UnitY;
    }
    return normal.x < 0.0 ? -Base::Vector3d::UnitX : Base::Vector3d::UnitX;
}

/// The turns about the tilt axes that take baseNormal to normal, the inverse of
/// SectionAnalysisWidget::applyAngles(), or nothing if no turns within
/// maxAngle do.
///
/// Turning about the second axis leaves the part along it alone, so the first
/// turn alone has to bring the normal's part along it: a cos t + b sin t = c,
/// with two roots. The second turn is then whatever is left, about its own axis.
/// Two roots can both hold; the smaller tilt is the one to show.
std::optional<std::pair<double, double>> solveTilt(
    const Base::Vector3d& baseNormal,
    const Base::Vector3d& normal,
    double maxAngle
)
{
    constexpr double reachTolerance = 1e-7;
    Base::Vector3d angle1Axis;
    Base::Vector3d angle2Axis;
    tiltAxes(baseNormal, angle1Axis, angle2Axis);

    const double a = baseNormal * angle2Axis;
    const double b = angle1Axis.Cross(baseNormal) * angle2Axis;
    const double c = normal * angle2Axis;
    const double r = std::hypot(a, b);
    if (r <= minUnitMagnitude) {
        return std::nullopt;
    }
    const double limit = maxAngle + reachTolerance;
    const double phase = std::atan2(b, a);
    const double spread = std::acos(std::clamp(c / r, -1.0, 1.0));
    std::optional<std::pair<double, double>> best;
    for (const double root : {phase + spread, phase - spread}) {
        const double turn1 = std::remainder(root, 2.0 * std::numbers::pi);
        const Base::Vector3d turned = rotateAbout(baseNormal, angle1Axis, turn1);
        const Base::Vector3d from = turned - angle2Axis * (turned * angle2Axis);
        const Base::Vector3d to = normal - angle2Axis * (normal * angle2Axis);
        const double turn2 = std::atan2(angle2Axis * from.Cross(to), from * to);
        const Base::Vector3d reached = rotateAbout(turned, angle2Axis, turn2);
        if (std::abs(turn1) > limit || std::abs(turn2) > limit
            || (reached - normal).Length() > reachTolerance) {
            continue;
        }
        if (!best
            || std::abs(turn1) + std::abs(turn2) < std::abs(best->first) + std::abs(best->second)) {
            best = std::pair {turn1, turn2};
        }
    }
    return best;
}

/// Work out which way the camera is looking
Base::Vector3d cameraViewDirection()
{
    auto* mdiView = qobject_cast<Gui::View3DInventor*>(Gui::Application::Instance->activeView());
    if (!mdiView) {
        return -Base::Vector3d::UnitZ;
    }
    float vx = 0.0F;
    float vy = 0.0F;
    float vz = 0.0F;
    mdiView->getViewer()->getViewDirection().getValue(vx, vy, vz);
    return Base::Vector3d(vx, vy, vz);
}


/// The preset combo row for a base orientation: the axis plane it is square to,
/// either way round, or -1 (blank) for none.
int presetIndexFor(const Base::Vector3d& base)
{
    using Preset = SectionAnalysisWidget::Preset;
    if (isAlignedWith(base, Base::Vector3d::UnitZ)) {
        return static_cast<int>(Preset::XY);
    }
    if (isAlignedWith(base, Base::Vector3d::UnitY)) {
        return static_cast<int>(Preset::XZ);
    }
    if (isAlignedWith(base, Base::Vector3d::UnitX)) {
        return static_cast<int>(Preset::YZ);
    }
    return -1;
}

/// Helps in picking initial preset.
/// Returns a normal vector that is aligned with axis and points in the same
/// direction as the camera's view vector, unless current is already aligned
/// with axis, in which case it is returned (possibly negated to match the
/// camera's view direction).
Base::Vector3d presetNormal(const Base::Vector3d& axis, const Base::Vector3d& current)
{
    if (isAlignedWith(current, axis)) {
        return (current * axis < 0.0) ? -axis : axis;
    }
    return (cameraViewDirection() * axis < 0.0) ? axis : -axis;
}
}  // namespace


bool SectionAnalysisWidget::AlignGate::allow(
    App::Document* /*doc*/,
    App::DocumentObject* obj,
    const char* subname
)
{
    if (panel->planeOfSelection(obj, subname, std::nullopt)) {
        return true;
    }
    notAllowedReason = QT_TRANSLATE_NOOP("QObject", "Not a flat face, a circular or straight edge, or a datum plane or line");
    return false;
}

SectionAnalysisWidget::SectionAnalysisWidget(
    Part::SectionAnalysis* feat,
    ViewProviderSectionAnalysis* vp,
    QWidget* parent
)
    : QWidget(parent)
    // Attached only while waiting for a pick, and unresolved, so a face inside
    // a Part or a link comes with the full path its placement is taken along.
    , Gui::SelectionObserver(false, Gui::ResolveMode::NoResolve)
    , feature(feat)
    , viewProvider(vp)
{
    setupUi();
    setupConnections();

    // Angles and offset come from the plane, not from defaults, so a reopened
    // section shows the tilt it was saved with.
    refreshFromFeature();

    // The dragger writes the feature, not these boxes, so the panel has to read
    // the plane back or it shows numbers the plane no longer has.
    featureConn = feat->getDocument()->signalChangedObject.connect(
        [this](const App::DocumentObject& obj, const App::Property& prop) {
            if (&obj != feature) {
                return;
            }
            // AngleBase too: undo can put it back after the plane, and the
            // angles read against the old base until it does.
            if (&prop == &feature->PlaneNormal || &prop == &feature->PlaneOffset
                || &prop == &feature->AngleBase) {
                refreshFromFeature();
            }
        }
    );
}

SectionAnalysisWidget::~SectionAnalysisWidget()
{
    disarmAlign();
    // The hint belongs to the panel, so it has to go when the panel does
}

ViewProviderSectionAnalysis* SectionAnalysisWidget::getViewProvider() const
{
    return viewProvider;
}


Part::SectionAnalysis* SectionAnalysisWidget::getObject() const
{
    return feature;
}

void SectionAnalysisWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    // --- Cutting Plane group ---
    auto* planeGroup = new QGroupBox(tr("Cutting Plane"), this);
    auto* planeLayout = new QGridLayout(planeGroup);

    // Preset combo
    planeLayout->addWidget(new QLabel(tr("Preset:"), this), 0, 0);
    presetCombo = new QComboBox(this);
    presetCombo->addItem(tr("XY Plane (Z normal)"));
    presetCombo->addItem(tr("XZ Plane (Y normal)"));
    presetCombo->addItem(tr("YZ Plane (X normal)"));
    presetCombo->addItem(tr("View Direction"));
    // Shown when the plane matches no preset, so the box reads as "none of
    // these" rather than as an empty control that failed to populate.
    presetCombo->setPlaceholderText(tr("(no preset)"));
    planeLayout->addWidget(presetCombo, 0, 1);

    // Seed the base the first time only. After that it is what the preset left,
    // which is what lets a reopened section still know its preset and angles.
    const Base::Vector3d n = feature->PlaneNormal.getValue();
    if (feature->AngleBase.getValue().Length() <= minUnitMagnitude) {
        const double openLen = n.Length();
        feature->AngleBase.setValue((openLen > minUnitMagnitude) ? n / openLen : Base::Vector3d::UnitZ);
    }

    // The preset comes from the base, not from the plane: a tilt bakes itself
    // into PlaneNormal, so asking the plane would say "none" for anything tilted.
    presetCombo->setCurrentIndex(presetIndexFor(feature->AngleBase.getValue()));

    // Angle adjustments (tilt the plane from the preset orientation)
    angleLabel1 = new QLabel(tr("X Angle:"), this);
    planeLayout->addWidget(angleLabel1, 1, 0);
    angle1Spin = new Gui::QuantitySpinBox(this);
    angle1Spin->setUnit(Base::Unit::Angle);
    angle1Spin->setRange(-90.0, 90.0);
    angle1Spin->setSingleStep(0.1);
    angle1Spin->setValue(0.0);
    planeLayout->addWidget(angle1Spin, 1, 1);

    angleLabel2 = new QLabel(tr("Z Angle:"), this);
    planeLayout->addWidget(angleLabel2, 2, 0);
    angle2Spin = new Gui::QuantitySpinBox(this);
    angle2Spin->setUnit(Base::Unit::Angle);
    angle2Spin->setRange(-90.0, 90.0);
    angle2Spin->setSingleStep(0.1);
    angle2Spin->setValue(0.0);
    planeLayout->addWidget(angle2Spin, 2, 1);

    applyPresetAngleLabels(static_cast<Preset>(presetCombo->currentIndex()));

    // Offset along the normal.
    auto* offsetLabel = new QLabel(tr("Offset:"), this);
    planeLayout->addWidget(offsetLabel, 3, 0);
    offsetSpin = new Gui::QuantitySpinBox(this);
    offsetSpin->setUnit(Base::Unit::Length);
    offsetSpin->setRange(-1.0e7, 1.0e7);
    offsetSpin->setSingleStep(1.0);
    offsetSpin->setValue(feature->PlaneOffset.getValue());
    planeLayout->addWidget(offsetSpin, 3, 1);

    flipCheck = new QCheckBox(tr("Flip Direction"), this);
    flipCheck->setChecked(feature->FlipCut.getValue());
    planeLayout->addWidget(flipCheck, 4, 0, 1, 2);

    alignButton = new QPushButton(tr("Align to Face or Edge"), this);
    alignButton->setCheckable(true);
    alignButton->setToolTip(
        tr("Put the plane on a flat face, in the plane of a circular edge,\n"
           "or square to a straight edge. Select one in the 3D view first,\n"
           "or press this and then click one.")
    );
    planeLayout->addWidget(alignButton, 5, 0, 1, 2);
    alignHint = new QLabel(this);
    alignHint->setWordWrap(true);
    alignHint->hide();
    planeLayout->addWidget(alignHint, 6, 0, 1, 2);

    mainLayout->addWidget(planeGroup);

    // --- Appearance group ---
    auto* appearGroup = new QGroupBox(tr("Appearance"), this);
    auto* appearLayout = new QGridLayout(appearGroup);

    appearLayout->addWidget(new QLabel(tr("Section Color:"), this), 0, 0);
    sectionColorBtn = new Gui::ColorButton(this);
    {
        // Reflect the section's actual colour (each new section gets a distinct
        // default colour assigned at creation) rather than a fixed swatch.
        const App::Material& curMat = viewProvider->ShapeAppearance[0];
        sectionColorBtn->setColor(
            QColor::fromRgbF(curMat.diffuseColor.r, curMat.diffuseColor.g, curMat.diffuseColor.b)
        );
    }
    appearLayout->addWidget(sectionColorBtn, 0, 1);

    hatchCheck = new QCheckBox(tr("Show Hatching"), this);
    hatchCheck->setChecked(viewProvider->ShowHatching.getValue());
    appearLayout->addWidget(hatchCheck, 1, 0, 1, 2);

    autoHideHatchCheck = new QCheckBox(tr("Fade Hatching When Zoomed Out"), this);
    autoHideHatchCheck->setToolTip(
        tr("Fade the hatching out, then stop drawing it, once the lines are too\n"
           "close together on screen to be told apart")
    );
    autoHideHatchCheck->setChecked(viewProvider->AutoHideHatching.getValue());
    appearLayout->addWidget(autoHideHatchCheck, 4, 0, 1, 2);

    perSolidColorCheck = new QCheckBox(tr("Per-Body Colors"), this);
    perSolidColorCheck->setChecked(viewProvider->PerBodyColors.getValue());
    // Nothing to tell apart below two bodies, and while it is on the single
    // colour picker would overwrite the per-body colours it assigns
    // Counted from the source recursion rather than read off SourceParts, which
    // Display mode leaves empty by design - so this was permanently greyed out
    // in the default mode. It still showed ticked, because PerBodyColors is a
    // separate view property the cap reads directly, so setting it from the
    // property editor worked while the panel insisted it could not be changed.
    perSolidColorCheck->setEnabled(
        Part::SectionAnalysis::distinctSourceParts(feature->Source.getValues(), feature).size() > 1
    );
    if (!perSolidColorCheck->isEnabled()) {
        perSolidColorCheck->setToolTip(tr("The section only comes from a single body"));
    }
    sectionColorBtn->setEnabled(!perSolidColorCheck->isChecked());
    appearLayout->addWidget(perSolidColorCheck, 2, 0, 1, 2);

    ghostCheck = new QCheckBox(tr("Show Removed Material"), this);
    ghostCheck->setToolTip(
        tr("Draw the material the section removes faintly, so the\n"
           "cross-sections have something to sit in")
    );
    ghostCheck->setChecked(viewProvider->ShowRemovedMaterial.getValue());
    // Row and span given, like every other row here. The one argument overload
    // is QLayout's, and on a grid it drops the widget in a cell of its own
    // choosing spanning a single column - which is neither where this reads as
    // going nor the full width its siblings get.
    appearLayout->addWidget(ghostCheck, 5, 0, 1, 2);

    showPlaneCheck = new QCheckBox(tr("Show Cutting Plane"), this);
    showPlaneCheck->setChecked(true);
    appearLayout->addWidget(showPlaneCheck, 3, 0, 1, 2);

    mainLayout->addWidget(appearGroup);

    // Update View checkbox
    updateViewCheck = new QCheckBox(tr("Update View"), this);
    updateViewCheck->setChecked(true);
    mainLayout->addWidget(updateViewCheck);

    mainLayout->addStretch();
}


void SectionAnalysisWidget::setupConnections()
{
    connect(
        presetCombo,
        qOverload<int>(&QComboBox::activated),
        this,
        &SectionAnalysisWidget::onPresetChanged
    );
    connect(
        angle1Spin,
        qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
        this,
        &SectionAnalysisWidget::onAngle1Changed
    );
    connect(
        angle2Spin,
        qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
        this,
        &SectionAnalysisWidget::onAngle2Changed
    );
    connect(offsetSpin, qOverload<double>(&Gui::QuantitySpinBox::valueChanged), this, [this](double value) {
        const PanelWrite writing(this);
        feature->PlaneOffset.setValue(value);
        recompute();
    });
    connect(flipCheck, &QCheckBox::toggled, this, &SectionAnalysisWidget::onFlipToggled);
    connect(alignButton, &QPushButton::toggled, this, &SectionAnalysisWidget::onAlignToggled);
    connect(sectionColorBtn, &Gui::ColorButton::changed, this, [this]() {
        onSectionColorChanged(sectionColorBtn->color());
    });
    connect(hatchCheck, &QCheckBox::toggled, this, &SectionAnalysisWidget::onHatchToggled);
    connect(autoHideHatchCheck, &QCheckBox::toggled, this, [this](bool on) {
        viewProvider->AutoHideHatching.setValue(on);
    });
    connect(perSolidColorCheck, &QCheckBox::toggled, this, &SectionAnalysisWidget::onPerSolidColorToggled);
    connect(ghostCheck, &QCheckBox::toggled, this, [this](bool on) {
        viewProvider->ShowRemovedMaterial.setValue(on);
    });
    connect(showPlaneCheck, &QCheckBox::toggled, this, &SectionAnalysisWidget::onShowPlaneToggled);
    connect(updateViewCheck, &QCheckBox::toggled, this, &SectionAnalysisWidget::onUpdateViewToggled);
}

void SectionAnalysisWidget::applyPresetAngleLabels(Preset preset)
{
    switch (preset) {
        case Preset::XY:
            angleLabel1->setText(tr("X Angle:"));
            angleLabel2->setText(tr("Y Angle:"));
            break;
        case Preset::XZ:
            angleLabel1->setText(tr("X Angle:"));
            angleLabel2->setText(tr("Z Angle:"));
            break;
        case Preset::YZ:
            angleLabel1->setText(tr("Y Angle:"));
            angleLabel2->setText(tr("Z Angle:"));
            break;
        default:
            // View direction, or no preset at all: the tilt axes are derived
            // from the current normal, so they have no standing names.
            angleLabel1->setText(tr("Angle 1:"));
            angleLabel2->setText(tr("Angle 2:"));
            break;
    }
}

void SectionAnalysisWidget::refreshFromFeature()
{
    if (pushingToFeature) {
        return;
    }

    const QSignalBlocker blockOffset(offsetSpin);
    const QSignalBlocker blockAngle1(angle1Spin);
    const QSignalBlocker blockAngle2(angle2Spin);

    offsetSpin->setValue(feature->PlaneOffset.getValue());

    Base::Vector3d normal = feature->PlaneNormal.getValue();
    const double len = normal.Length();
    if (len <= minUnitMagnitude) {
        return;
    }
    normal /= len;

    // Both boxes have the same range.
    constexpr double toDegrees = 180.0 / std::numbers::pi;
    const double maxAngle = angle1Spin->maximum() / toDegrees;

    // The base is only what the panel last left. A plane set elsewhere - the
    // property editor, a script, the dragger's turn handles - may be one no tilt
    // of the base reaches, or one only a 90 degree tilt reaches, and the boxes
    // then showed a preset and angles that read nothing like the plane. Such a
    // plane starts again from the axis it leans on most, from which every plane
    // is in range. A plane the panel set itself keeps the base it was set from,
    // so typing 90 into a box is not undone by the next offset drag.
    const bool setElsewhere = (normal - lastPanelNormal).Length() > lastPanelTolerance;
    lastPanelNormal = normal;
    Base::Vector3d baseNormal = feature->AngleBase.getValue();
    auto tilt = solveTilt(baseNormal, normal, maxAngle);
    const auto atLimit = [maxAngle](double turn) {
        return std::abs(turn) >= maxAngle - lastPanelTolerance;
    };
    if (!tilt || (setElsewhere && (atLimit(tilt->first) || atLimit(tilt->second)))) {
        baseNormal = nearestAxis(normal);
        tilt = solveTilt(baseNormal, normal, maxAngle);
        if (!tilt) {
            baseNormal = normal;
            tilt = std::pair {0.0, 0.0};
        }
        // Undo and redo put AngleBase back themselves, maybe after this.
        //
        // The write comes back here through the AngleBase listener. That inner
        // pass finds the plane it was handed and a tilt in range, so it writes
        // nothing and shows what this pass goes on to show.
        if (!feature->getDocument()->isPerformingTransaction()) {
            feature->AngleBase.setValue(baseNormal);
        }
    }

    // The combo follows the base, also one undo put back. View Direction has no
    // axis of its own, so it stays while the base is not on one.
    const int basePreset = presetIndexFor(baseNormal);
    const bool keepsViewDirection = basePreset < 0
        && presetCombo->currentIndex() == static_cast<int>(Preset::ViewDirection);
    if (presetCombo->currentIndex() != basePreset && !keepsViewDirection) {
        {
            const QSignalBlocker blockPreset(presetCombo);
            presetCombo->setCurrentIndex(basePreset);
        }
        applyPresetAngleLabels(static_cast<Preset>(presetCombo->currentIndex()));
    }

    // applyAngles() turns the first axis the other way from its box. What would
    // show as "-0.00" is 0.
    const auto shown = [](double degrees) {
        return std::abs(degrees) < 0.005 ? 0.0 : degrees;
    };
    angle1Spin->setValue(shown(-tilt->first * toDegrees));
    angle2Spin->setValue(shown(tilt->second * toDegrees));
}

Base::Vector3d SectionAnalysisWidget::normalForPreset(Preset preset) const
{
    const Base::Vector3d current = feature->PlaneNormal.getValue();
    switch (preset) {
        case Preset::XY:
            return presetNormal(Base::Vector3d::UnitZ, current);
        case Preset::XZ:
            return presetNormal(Base::Vector3d::UnitY, current);
        case Preset::YZ:
            return presetNormal(Base::Vector3d::UnitX, current);
        case Preset::ViewDirection:
            return -cameraViewDirection();  // face toward camera
        default:
            return current;
    }
}

void SectionAnalysisWidget::resetTiltAngles()
{
    const QSignalBlocker blockAngle1(angle1Spin);
    const QSignalBlocker blockAngle2(angle2Spin);
    angle1Spin->setValue(0.0);
    angle2Spin->setValue(0.0);
}

void SectionAnalysisWidget::centreOffsetOnSources(const Base::Vector3d& normal)
{
    // The same box the plane quad and the handles are built from, so the three agree.
    Base::Vector3d centre;
    double diagonal = 0.0;
    if (viewProvider && viewProvider->sourceBounds(centre, diagonal)) {
        feature->PlaneOffset.setValue(centre * normal);
    }

    const QSignalBlocker blockOffset(offsetSpin);
    offsetSpin->setValue(feature->PlaneOffset.getValue());
}

void SectionAnalysisWidget::onAlignToggled(bool on)
{
    if (!on) {
        disarmAlign();
        return;
    }

    // Something already selected is the element meant, if it stands for a plane.
    for (const auto& sel : Gui::Selection().getSelectionEx(
             feature->getDocument()->getName(),
             App::DocumentObject::getClassTypeId(),
             Gui::ResolveMode::NoResolve
         )) {
        std::vector<std::string> subnames = sel.getSubNames();
        if (subnames.empty()) {
            // A whole object: a datum plane or line, or a shape that is a single
            // face (a Part::Plane, a face made from a sketch), which then
            // aligns to that face.
            subnames.emplace_back();
        }
        const auto& points = sel.getPickedPoints();
        for (std::size_t i = 0; i < subnames.size(); ++i) {
            std::optional<Base::Vector3d> picked;
            if (i < points.size()) {
                picked = points[i];
            }
            if (auto plane = planeOfSelection(sel.getObject(), subnames[i].c_str(), picked)) {
                {
                    const QSignalBlocker blockAlign(alignButton);
                    alignButton->setChecked(false);
                }
                Gui::Selection().clearSelection();
                alignTo(*plane);
                return;
            }
        }
    }

    alignHint->setText(
        tr("Click a flat face, a circular or straight edge, or a datum plane or line in the 3D view.")
    );
    alignHint->show();
    alignArmed = true;
    attachSelection();
    // Clicks have to reach the faces behind the section's own plane and caps.
    // (View3DInventorPy::getObjectInfo does not honour this, real picks do.)
    if (viewProvider) {
        viewProvider->setPickable(false);
    }
    // Only what the plane can align to highlights under the cursor.
    Gui::Selection().addSelectionGate(new AlignGate(this), Gui::ResolveMode::NoResolve);
    alignGate = Gui::Selection().getSelectionGate(feature->getDocument());
}

void SectionAnalysisWidget::disarmAlign()
{
    // Nothing to undo unless armed. The destructor comes here too, and may run
    // after the document has dropped the view provider.
    if (!alignArmed) {
        return;
    }
    alignArmed = false;
    alignPending = false;
    detachSelection();
    // The gate is the selection's, not ours, once installed: remove it only if
    // nobody has put another in its place.
    if (alignGate && Gui::Selection().getSelectionGate(feature->getDocument()) == alignGate) {
        Gui::Selection().rmvSelectionGate();
    }
    alignGate = nullptr;
    if (viewProvider) {
        viewProvider->setPickable(true);
    }
    if (alignHint) {
        alignHint->hide();
    }
}

void SectionAnalysisWidget::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    // A box or multiple selection delivers several picks at once: the first one counts.
    if (msg.Type != Gui::SelectionChanges::AddSelection || alignPending) {
        return;
    }
    App::Document* doc = feature->getDocument();
    if (!msg.pDocName || std::strcmp(msg.pDocName, doc->getName()) != 0) {
        return;
    }
    std::optional<Base::Vector3d> picked;
    if (msg.hasPickedPoint) {
        picked = Base::Vector3d(msg.x, msg.y, msg.z);
    }
    const auto plane = planeOfSelection(doc->getObject(msg.pObjectName), msg.pSubName, picked);
    if (!plane) {
        // Only reached by selections that bypass the gate.
        alignHint->setText(
            tr("Not a flat face, a circular or straight edge, or a datum plane or line.")
        );
        return;
    }

    // Not from inside the selection's own notification: the pick is still being
    // delivered to the other observers, and moving the plane recomputes.
    alignPending = true;
    QTimer::singleShot(0, this, [this, plane = *plane]() {
        if (!alignPending) {
            return;  // disarmed by hand in the meantime
        }
        alignButton->setChecked(false);  // disarms, before the selection is cleared
        Gui::Selection().clearSelection();
        alignTo(plane);
    });
}

std::optional<Part::SectionAnalysis::ElementPlane> SectionAnalysisWidget::planeOfSelection(
    const App::DocumentObject* obj,
    const char* subname,
    const std::optional<Base::Vector3d>& picked
) const
{
    if (!obj) {
        return std::nullopt;
    }
    if (!subname) {
        subname = "";
    }
    Base::Matrix4D placement;
    const App::DocumentObject* target = obj->getSubObject(subname, nullptr, &placement);
    // The section's own plane and caps are not what anyone means to align to.
    if (!target || target == feature || obj == feature) {
        return std::nullopt;
    }

    std::optional<Part::SectionAnalysis::ElementPlane> plane;
    if (const auto* datum = dynamic_cast<const App::DatumElement*>(target);
        datum && (datum->isDerivedFrom<App::Plane>() || datum->isDerivedFrom<App::Line>())) {
        // A datum has no shape to ask, but its placement says it all.
        const Base::Vector3d origin = placement * Base::Vector3d();
        Base::Vector3d direction = placement * datum->getBaseDirection() - origin;
        if (direction.Length() <= minUnitMagnitude) {
            return std::nullopt;
        }
        direction.Normalize();
        Base::Vector3d through = origin;
        if (picked && datum->isDerivedFrom<App::Line>()) {
            through = origin + direction * ((*picked - origin) * direction);
        }
        plane = Part::SectionAnalysis::ElementPlane {direction, direction * through, false};
    }
    else {
        // Same lookup as Std_AlignToSelection (Part::Feature::getCameraAlignmentDirection).
        const TopoDS_Shape element = Part::Feature::getShape(
            obj,
            Part::ShapeOption::NeedSubElement | Part::ShapeOption::ResolveLink
                | Part::ShapeOption::Transform,
            subname
        );
        plane = Part::SectionAnalysis::planeOfElement(element, picked);
        // A face only knows its outside when it bounds a solid; a sheet's face
        // can face either way.
        if (plane && plane->outward
            && !Part::SectionCap::isClosedSolid(Part::Feature::getShape(
                obj,
                Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform,
                subname
            ))) {
            plane->outward = false;
        }
    }

    if (plane && !plane->outward && plane->normal * cameraViewDirection() > 0.0) {
        // Nothing says which side is out, so cut away the side nearer the
        // camera, as the view-direction preset does.
        plane->normal = -plane->normal;
        plane->offset = -plane->offset;
    }
    return plane;
}

void SectionAnalysisWidget::alignTo(const Part::SectionAnalysis::ElementPlane& plane)
{
    const PanelWrite writing(this);

    // Stored as the element gives it, so PlaneNormal reads plainly: flipping
    // would turn it into the material.
    if (feature->FlipCut.getValue()) {
        feature->FlipCut.setValue(false);
        const QSignalBlocker blockFlip(flipCheck);
        flipCheck->setChecked(false);
    }

    resetTiltAngles();
    feature->PlaneNormal.setValue(plane.normal);
    feature->AngleBase.setValue(plane.normal);
    lastPanelNormal = plane.normal;
    feature->PlaneOffset.setValue(plane.offset);
    {
        const QSignalBlocker blockPreset(presetCombo);
        presetCombo->setCurrentIndex(presetIndexFor(plane.normal));
    }
    applyPresetAngleLabels(static_cast<Preset>(presetCombo->currentIndex()));
    {
        const QSignalBlocker blockOffset(offsetSpin);
        offsetSpin->setValue(plane.offset);
    }
    alignHint->hide();

    recompute();
}

void SectionAnalysisWidget::onPresetChanged(int index)
{
    const PanelWrite writing(this);
    const auto preset = static_cast<Preset>(index);

    applyPresetAngleLabels(preset);

    // Blank combo: nothing to apply, the plane keeps the pose it has.
    if (preset != Preset::XY && preset != Preset::XZ && preset != Preset::YZ
        && preset != Preset::ViewDirection) {
        return;
    }

    const Base::Vector3d normal = normalForPreset(preset);
    resetTiltAngles();
    feature->PlaneNormal.setValue(normal);
    feature->AngleBase.setValue(normal);
    lastPanelNormal = normal;
    centreOffsetOnSources(normal);

    recompute();
}

void SectionAnalysisWidget::onAngle1Changed(double /*val*/)
{
    applyAngles();
}

void SectionAnalysisWidget::onAngle2Changed(double /*val*/)
{
    applyAngles();
}

void SectionAnalysisWidget::angleReferenceFrame(
    Base::Vector3d& baseNormal,
    Base::Vector3d& angle1Axis,
    Base::Vector3d& angle2Axis
) const
{
    // The orientation captured when the panel opened or a preset was picked
    // but never used again when changes were made
    baseNormal = feature->AngleBase.getValue();
    tiltAxes(baseNormal, angle1Axis, angle2Axis);
}


void SectionAnalysisWidget::applyAngles()
{
    const PanelWrite writing(this);

    // Negated so a positive angle tilts the way the box reads
    double a1 = -angle1Spin->value().getValue() * std::numbers::pi / 180.0;
    double a2 = angle2Spin->value().getValue() * std::numbers::pi / 180.0;

    // Base normal and tilt axes both come from presetFrame, which is the same
    // frame the tilt handles are placed in. A second copy of the preset switch
    // lived here, and a handle turning about a different axis than the box it
    // drives is worse than no handle at all.
    Base::Vector3d baseNormal;
    Base::Vector3d angle1Axis;
    Base::Vector3d angle2Axis;
    angleReferenceFrame(baseNormal, angle1Axis, angle2Axis);

    // Rotate baseNormal around angle1Axis by a1, then around angle2Axis by a2
    Base::Vector3d n = rotateAbout(baseNormal, angle1Axis, a1);
    n = rotateAbout(n, angle2Axis, a2);

    double len = n.Length();
    if (len > minUnitMagnitude) {
        n = n / len;
    }

    // Keep the plane passing through the same geometric point as it tilts.
    //
    // Pivot on the geometry, not on the plane's closest approach to the world
    // origin: a building 20 m away swings out of the model on a small tilt.
    // Same anchor the handles orbit, so the plane turns about what they show.
    Base::Vector3d oldN = feature->PlaneNormal.getValue();
    const double oldD = feature->PlaneOffset.getValue();
    const double oldLen = oldN.Length();

    Base::Vector3d pivot(0, 0, 0);
    if (oldLen > minUnitMagnitude) {
        const Base::Vector3d unitOld = oldN / oldLen;

        Base::Vector3d hint = unitOld * oldD;
        double diagonal = 0.0;
        if (viewProvider) {
            viewProvider->sourceBounds(hint, diagonal);
        }
        pivot = Part::SectionAnalysis::draggerAnchor(unitOld, oldD, hint);
    }
    const double newOffset = pivot * n;

    feature->PlaneNormal.setValue(n);
    lastPanelNormal = n;
    feature->PlaneOffset.setValue(newOffset);

    // Tilting moves the offset too: same point, different distance along the
    // new normal, so the box has to follow.
    {
        const QSignalBlocker blockOffset(offsetSpin);
        offsetSpin->setValue(newOffset);
    }

    recompute();
}

void SectionAnalysisWidget::onFlipToggled(bool on)
{
    const PanelWrite writing(this);
    feature->FlipCut.setValue(on);
    recompute();
}

void SectionAnalysisWidget::onSectionColorChanged(const QColor& color)
{
    // Update the section face color on the ViewProvider
    if (viewProvider) {
        App::Material mat;
        mat.diffuseColor.set(color.redF(), color.greenF(), color.blueF(), 0.0f);
        viewProvider->ShapeAppearance.setValues({mat});
    }
}

void SectionAnalysisWidget::onHatchToggled(bool on)
{
    if (viewProvider) {
        viewProvider->setHatching(on);
    }
}

void SectionAnalysisWidget::onPerSolidColorToggled(bool on)
{
    if (viewProvider) {
        viewProvider->setPerSolidColors(on);
    }
    // The single colour would be applied to every face, undoing the per-body one
    sectionColorBtn->setEnabled(!on);
}

void SectionAnalysisWidget::onShowPlaneToggled(bool on)
{
    if (viewProvider) {
        viewProvider->setShowPlane(on);
    }
}

void SectionAnalysisWidget::onUpdateViewToggled(bool on)
{
    if (on) {
        recompute();
    }
}

void SectionAnalysisWidget::recompute()
{
    if (updateViewCheck && updateViewCheck->isChecked()) {
        feature->getDocument()->recomputeFeature(feature);
    }
}

bool SectionAnalysisWidget::accept()
{
    disarmAlign();
    try {
        Base::Vector3d n = feature->PlaneNormal.getValue();
        // %.12g rather than %f: these are doubles, and a recorded macro should
        // reproduce the plane the user actually left rather than a rounding of it.
        Gui::cmdAppObjectArgs(feature, "PlaneNormal = FreeCAD.Vector(%.12g, %.12g, %.12g)", n.x, n.y, n.z);
        Gui::cmdAppObjectArgs(feature, "PlaneOffset = %.12g", feature->PlaneOffset.getValue());
        Gui::cmdAppObjectArgs(feature, "FlipCut = %s", feature->FlipCut.getValue() ? "True" : "False");

        Gui::Command::doCommand(Gui::Command::Doc, "App.ActiveDocument.recompute()");
        if (!feature->isValid()) {
            throw Base::CADKernelError(feature->getStatusString());
        }

        Gui::Command::doCommand(Gui::Command::Gui, "Gui.ActiveDocument.resetEdit()");
        // Commit the transaction opened by the command (Create Section Analysis)
        feature->getDocument()->commitTransaction();
    }
    catch (const Base::Exception& e) {
        // On error, abort and remove the object
        if (viewProvider) {
            viewProvider->hide();
        }
        feature->getDocument()->abortTransaction();
        QMessageBox::warning(
            this,
            tr("Input error"),
            QCoreApplication::translate("Exception", e.what())
        );
        return false;
    }

    return true;
}

bool SectionAnalysisWidget::reject()
{
    disarmAlign();
    // Remove clipping before undo so the source VP is restored
    if (viewProvider) {
        viewProvider->hide();
    }

    // Roll back the transaction (removes the SectionAnalysis object if newly created)
    feature->getDocument()->abortTransaction();
    Gui::Command::doCommand(Gui::Command::Gui, "Gui.ActiveDocument.resetEdit()");
    Gui::Command::updateActive();
    return true;
}


// -----------------------------------------------------------------------
// TaskSectionAnalysis
// -----------------------------------------------------------------------

TaskSectionAnalysis::TaskSectionAnalysis(Part::SectionAnalysis* feature, ViewProviderSectionAnalysis* vp)
{
    widget = new SectionAnalysisWidget(feature, vp);
    addTaskBox(Gui::BitmapFactory().pixmap("Part_SectionAnalysis"), widget);
}

TaskSectionAnalysis::~TaskSectionAnalysis() = default;

Part::SectionAnalysis* TaskSectionAnalysis::getObject() const
{
    return widget->getObject();
}

bool TaskSectionAnalysis::accept()
{
    return widget->accept();
}

bool TaskSectionAnalysis::reject()
{
    return widget->reject();
}

#include "moc_TaskSectionAnalysis.cpp"
