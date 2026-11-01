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

#include <Gui/TaskView/TaskDialog.h>
#include <Mod/Inspection/App/PartsList.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTreeWidget;

namespace InspectionGui
{

/**
 * Parts list: the parts of the document, or of the selection, counted by shape, so that
 * copies of one part are one line even when they are separate objects. A line selects all
 * its pieces in the 3D view; Find Copies goes to the line of the part selected there; the
 * list can be saved as CSV. Nothing is added to the document.
 */
class TaskPartsList: public Gui::TaskView::TaskDialog
{
    Q_OBJECT

public:
    TaskPartsList();

    QDialogButtonBox::StandardButtons getStandardButtons() const override
    {
        return QDialogButtonBox::Close;
    }

private:
    enum Scope
    {
        AllParts,
        SelectedParts
    };

    void refresh();
    void fillList();
    void showRow();
    void findCopies();
    void exportCsv();

    std::string docName;
    std::vector<Inspection::InterferencePart> parts;
    std::vector<Inspection::PartsListItem> items;
    std::vector<std::string> meshes;

    QComboBox* scope {nullptr};
    QCheckBox* hidden {nullptr};
    QLabel* summary {nullptr};
    QTreeWidget* list {nullptr};
    QPushButton* copies {nullptr};
    QPushButton* save {nullptr};
};

}  // namespace InspectionGui
