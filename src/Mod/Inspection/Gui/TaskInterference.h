// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Yishin Li
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

#include <string>
#include <vector>

#include <QPointer>

#include <Gui/TaskView/TaskDialog.h>
#include <Mod/Inspection/App/Interference.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTreeWidget;
class SoSeparator;

namespace Gui
{
class QuantitySpinBox;
class View3DInventor;
}  // namespace Gui

namespace InspectionGui
{

/**
 * Interference check: which visible parts share volume, touch, or come closer than a
 * clearance. The results are drawn over the 3D views while the panel is open (clashes red,
 * contacts yellow, clearances cyan) and nothing is added to the document, so a file opened
 * only to be looked at stays unmodified.
 */
class TaskInterference: public Gui::TaskView::TaskDialog
{
    Q_OBJECT

public:
    TaskInterference();
    ~TaskInterference() override;

    QDialogButtonBox::StandardButtons getStandardButtons() const override
    {
        return QDialogButtonBox::Close;
    }
    bool reject() override;

private:
    enum Scope
    {
        AllParts,
        SelectionAgainstRest,
        WithinSelection
    };

    void check();
    void collectAndCheck(App::Document& doc, const std::vector<App::SubObjectT>& picked);
    void fillList();
    void draw();
    void clearDrawing();
    void showRow();

    std::string docName;
    std::vector<Inspection::InterferencePart> parts;
    Inspection::InterferenceReport report;
    std::vector<std::string> meshes;

    QComboBox* scope {nullptr};
    Gui::QuantitySpinBox* clearance {nullptr};
    QCheckBox* contacts {nullptr};
    QPushButton* run {nullptr};
    QLabel* summary {nullptr};
    QTreeWidget* list {nullptr};

    SoSeparator* drawing {nullptr};
    std::vector<QPointer<Gui::View3DInventor>> views;
};

}  // namespace InspectionGui
