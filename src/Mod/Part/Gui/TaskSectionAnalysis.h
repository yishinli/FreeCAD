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

#pragma once

#include <memory>

#include <fastsignals/signal.h>

#include <Base/Vector3D.h>

#include <Gui/Selection/Selection.h>
#include <Gui/TaskView/TaskView.h>
#include <Gui/TaskView/TaskDialog.h>

#include <Mod/Part/App/FeatureSectionAnalysis.h>

class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;

namespace Gui
{
class QuantitySpinBox;
class ColorButton;
}  // namespace Gui

namespace PartGui
{

class ViewProviderSectionAnalysis;

class SectionAnalysisWidget: public QWidget, public Gui::SelectionObserver
{
    Q_OBJECT

public:
    /// Entries of the preset combo, in order. The index is used to pick the base
    /// normal and to label the two angle boxes, so it is worth naming rather
    /// than reading `case 2:` and counting rows in the constructor. A plane
    /// matching no entry leaves the combo blank, i.e. currentIndex() == -1.
    enum class Preset
    {
        XY = 0,  //!< Z normal
        XZ = 1,  //!< Y normal
        YZ = 2,  //!< X normal
        ViewDirection = 3,
    };

    explicit SectionAnalysisWidget(
        Part::SectionAnalysis* feature,
        ViewProviderSectionAnalysis* vp,
        QWidget* parent = nullptr
    );
    ~SectionAnalysisWidget() override;

    bool accept();
    bool reject();
    Part::SectionAnalysis* getObject() const;
    ViewProviderSectionAnalysis* getViewProvider() const;

private:
    void setupUi();
    void setupConnections();

    /// Base orientation and the two axes the angle boxes turn about.
    ///
    /// What the angles mean: applyAngles rotates the base normal about these.
    void angleReferenceFrame(
        Base::Vector3d& baseNormal,
        Base::Vector3d& angle1Axis,
        Base::Vector3d& angle2Axis
    ) const;

    /// Name the two tilt boxes after the axes the given preset turns about.
    void applyPresetAngleLabels(Preset preset);

    /// The plane normal a preset asks for, keeping the side the plane is already on.
    Base::Vector3d normalForPreset(Preset preset) const;

    /// Back to zero tilt, without the spin boxes reporting it as a user edit.
    void resetTiltAngles();

    /// Put the plane through the middle of the sources, and show the offset it got.
    void centreOffsetOnSources(const Base::Vector3d& normal);

    /// Pull the plane back out of the feature into the boxes, after something
    /// other than the boxes moved it - the dragger, or a script.
    void refreshFromFeature();

    /// Put the plane on the face or edge picked in the 3D view, if it stands for one.
    void onSelectionChanged(const Gui::SelectionChanges& msg) override;

    /// Start or stop waiting for the face or edge the plane is to align to.
    void onAlignToggled(bool on);

    /// Stop waiting for a pick: detach, and drop the gate if it is still ours.
    void disarmAlign();

    /// Lets through only what the plane can align to, while waiting for a pick.
    class AlignGate: public Gui::SelectionGate
    {
    public:
        explicit AlignGate(const SectionAnalysisWidget* widget)
            : panel(widget)
        {}
        bool allow(App::Document* doc, App::DocumentObject* obj, const char* subname) override;

    private:
        const SectionAnalysisWidget* panel;
    };
    /// The gate installed while waiting, to recognise it when removing it.
    const Gui::SelectionGate* alignGate = nullptr;
    /// Waiting for a pick: the observer, the gate and the pick lock are in place.
    bool alignArmed = false;
    /// A pick has been taken and its alignment is queued; further picks are ignored.
    bool alignPending = false;

    /// The plane a selected element stands for, oriented for the cut.
    std::optional<Part::SectionAnalysis::ElementPlane> planeOfSelection(
        const App::DocumentObject* obj,
        const char* subname,
        const std::optional<Base::Vector3d>& picked
    ) const;

    /// Make `plane` the cutting plane, as no preset and without tilt, with the
    /// handles on `picked` when the pick has a point.
    void alignTo(
        const Part::SectionAnalysis::ElementPlane& plane,
        const std::optional<Base::Vector3d>& picked
    );

    void onPresetChanged(int index);
    void onAngle1Changed(double val);
    void onAngle2Changed(double val);
    void applyAngles();
    void onFlipToggled(bool on);
    void onSectionColorChanged(const QColor& color);
    void onHatchToggled(bool on);
    void onPerSolidColorToggled(bool on);
    void onShowPlaneToggled(bool on);
    void onUpdateViewToggled(bool on);
    void recompute();

    Part::SectionAnalysis* feature;
    ViewProviderSectionAnalysis* viewProvider;


    fastsignals::scoped_connection featureConn;

    /// True while the panel is the one writing the plane, so the change coming
    /// back does not overwrite the box being typed into.
    bool pushingToFeature = false;

    /// The normal the panel last wrote or showed, to tell a plane the panel set
    /// from one set elsewhere. Zero to start with, so the plane the panel opens
    /// on counts as set elsewhere.
    Base::Vector3d lastPanelNormal {0.0, 0.0, 0.0};

    /// Sets that flag for as long as it lives, early returns included.
    struct PanelWrite
    {
        explicit PanelWrite(SectionAnalysisWidget* widget)
            : panel(widget)
        {
            panel->pushingToFeature = true;
        }
        ~PanelWrite()
        {
            panel->pushingToFeature = false;
        }
        PanelWrite(const PanelWrite&) = delete;
        PanelWrite& operator=(const PanelWrite&) = delete;

    private:
        SectionAnalysisWidget* panel;
    };

    QComboBox* presetCombo = nullptr;
    QLabel* angleLabel1 = nullptr;
    QLabel* angleLabel2 = nullptr;
    Gui::QuantitySpinBox* offsetSpin = nullptr;
    Gui::QuantitySpinBox* angle1Spin = nullptr;
    Gui::QuantitySpinBox* angle2Spin = nullptr;

    QPushButton* alignButton = nullptr;
    QLabel* alignHint = nullptr;

    QCheckBox* flipCheck = nullptr;
    Gui::ColorButton* sectionColorBtn = nullptr;
    QCheckBox* hatchCheck = nullptr;
    QCheckBox* autoHideHatchCheck = nullptr;
    QCheckBox* perSolidColorCheck = nullptr;
    QCheckBox* ghostCheck = nullptr;
    QCheckBox* showPlaneCheck = nullptr;
    QCheckBox* updateViewCheck = nullptr;
};

class TaskSectionAnalysis: public Gui::TaskView::TaskDialog
{
    Q_OBJECT

public:
    explicit TaskSectionAnalysis(Part::SectionAnalysis* feature, ViewProviderSectionAnalysis* vp);
    ~TaskSectionAnalysis() override;

    bool accept() override;
    bool reject() override;
    Part::SectionAnalysis* getObject() const;

    QDialogButtonBox::StandardButtons getStandardButtons() const override
    {
        return QDialogButtonBox::Ok | QDialogButtonBox::Cancel;
    }

private:
    SectionAnalysisWidget* widget;
};

}  // namespace PartGui
