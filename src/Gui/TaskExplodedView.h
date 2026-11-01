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

#include <Inventor/SbVec3f.h>

#include <App/DocumentObserver.h>
#include <Base/Matrix.h>
#include <Base/Vector3D.h>
#include <fastsignals/connection.h>

#include "TaskView/TaskDialog.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class SoTransform;

namespace Gui
{

/**
 * Exploded view for looking at an assembly: a slider moves its components apart from its
 * centre, or from its largest component, and back. Only the 3D views change: each moving
 * object's view provider is shifted, no placement is written, so a document opened only to
 * look at stays unmodified, and closing the panel puts everything back.
 */
class GuiExport TaskExplodedView: public TaskView::TaskDialog
{
    Q_OBJECT

public:
    TaskExplodedView();
    ~TaskExplodedView() override;

    QDialogButtonBox::StandardButtons getStandardButtons() const override
    {
        return QDialogButtonBox::Close;
    }
    bool reject() override;
    void closed() override;

private:
    // What moves: the first object on a part's path that only this instance shows
    struct Unit
    {
        App::SubObjectT object;
        Base::Vector3d centre;
        double size = 0.0;
        Base::Matrix4D parentInverse;  // world to the frame its transform is in
        SbVec3f original;              // its translation as the document places it
        SbVec3f written;               // its translation as last moved here
    };

    void rebuild();
    void collect();
    void move();
    void restore();
    void rebuildLater();
    SoTransform* transformOf(const Unit& unit) const;
    double factor() const;

    std::string docName;
    std::vector<Unit> units;
    Base::Vector3d centre;
    int shared = 0;
    bool pending = false;

    QComboBox* level {nullptr};
    QCheckBox* keepLargest {nullptr};
    QSlider* distance {nullptr};
    QLabel* summary {nullptr};

    std::vector<fastsignals::scoped_connection> connections;
};

}  // namespace Gui
