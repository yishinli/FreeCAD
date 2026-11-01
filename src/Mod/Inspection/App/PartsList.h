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

#include <cstddef>
#include <string>
#include <vector>

#include <gp_XYZ.hxx>

#include <Mod/Inspection/App/Interference.h>

namespace Inspection
{

/// One line of a parts list: parts of the same shape, wherever they are placed
struct InspectionExport PartsListItem
{
    std::string name;                    ///< the instances' label without a copy number
    std::string description;             ///< Label2 of the first instance
    std::vector<std::size_t> instances;  ///< indices into the parts, in their order
    double volume = 0.0;                 ///< of one piece (mm³)
    double area = 0.0;                   ///< of one piece (mm²)
    gp_XYZ size;                         ///< bounding box in the first piece's own axes (mm)
    bool mirrored = false;               ///< the mirror image of an earlier item of the same shape
};

/**
 * A parts list from the parts InterferenceCheck::collectParts() finds, as the Assembly
 * BOM counts links to one object, but by shape: parts of the same shape count as one
 * item even when they are separate objects, as in an imported STEP assembly. A mirror
 * image is an item of its own, as in the Assembly BOM. Nothing is added to the document.
 */
class InspectionExport PartsList
{
public:
    /// Items in the order their first pieces come in parts. The progress bar can stop it.
    static std::vector<PartsListItem> group(const std::vector<InterferencePart>& parts);

    /// The items as CSV for a spreadsheet: UTF-8, lengths in mm, one row per item
    static std::string csv(
        const std::vector<PartsListItem>& items,
        const std::vector<InterferencePart>& parts
    );
};

}  // namespace Inspection
