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

#include <gp_Pnt.hxx>
#include <TopoDS_Shape.hxx>

#include <App/DocumentObserver.h>
#include <Mod/Inspection/InspectionGlobal.h>

namespace App
{
class Document;
}

namespace Inspection
{

/// One part to check: a solid shape where it stands in the document
struct InspectionExport InterferencePart
{
    App::SubObjectT object;  ///< the top-level object and the path to the part
    std::string name;        ///< the labels along that path, for people
    TopoDS_Shape shape;      ///< in world placement
    std::string key;         ///< the same for the same part however it was picked
};

enum class InterferenceKind
{
    Clash,      ///< the parts share volume
    Contact,    ///< they touch, or share less than the smallest volume
    Clearance,  ///< they are apart, but closer than the clearance
    Unchecked   ///< the pair, or the part, could not be checked
};

/// What one pair of parts came to
struct InspectionExport InterferenceResult
{
    InterferenceKind kind = InterferenceKind::Unchecked;
    std::size_t first = 0;
    std::size_t second = 0;  ///< the same as first for a part that is not checked at all
    double volume = 0.0;     ///< shared volume of a clash (mm³)
    double distance = 0.0;   ///< gap of a contact or a clearance (mm)
    gp_Pnt point1;           ///< the closest points, or the center of the shared volume
    gp_Pnt point2;
    TopoDS_Shape shared;  ///< the shared volume of a clash
    std::string reason;   ///< why a pair is unchecked
};

struct InspectionExport InterferenceSettings
{
    double clearance = 0.0;   ///< report parts closer than this (mm); 0: clashes and contacts only
    double minVolume = 0.01;  ///< a smaller shared volume is a contact (mm³)
    double tolerance = 1e-6;  ///< a smaller gap is a contact (mm), as are gaps within the parts'
                              ///< own tolerances
    double budget = 240.0;  ///< seconds; pairs not reached by then are unchecked (one slow pair can
                            ///< overrun it)
};

struct InspectionExport InterferenceReport
{
    std::vector<InterferenceResult> results;
    std::size_t pairs = 0;    ///< pairs asked for
    std::size_t checked = 0;  ///< pairs whose boxes met and that were measured
    std::size_t pruned = 0;   ///< pairs ruled out by their boxes
};

/**
 * Interference between solid parts: which share volume, which touch and which come closer
 * than a clearance. Pairs are first ruled out by their bounding boxes, grown by the
 * clearance; the rest are measured on the exact shapes, and only touching ones intersected.
 * Nothing is added to the document.
 */
class InspectionExport InterferenceCheck
{
public:
    /**
     * The visible solid parts under objects, each with the path to it from its top-level
     * object: groups, App::Part, assemblies and links are opened, while a shape feature
     * (a PartDesign Body, a Part feature) is one part. A path may end inside a container.
     * Mesh objects are not checked; their names go to unchecked. A part reached twice is
     * listed once.
     */
    static std::vector<InterferencePart> collectParts(
        const std::vector<App::SubObjectT>& objects,
        std::vector<std::string>* unchecked = nullptr
    );

    /// The visible solid parts of a document. An object a link points to is a part of its
    /// own when it is visible as well, as both are drawn.
    static std::vector<InterferencePart> collectParts(
        const App::Document* doc,
        std::vector<std::string>* unchecked = nullptr
    );

    /**
     * Check the parts with indices in group against each other, or, when others is not
     * empty, each of group against each of others. Pairs are measured on several threads
     * and listed in order. The progress bar can stop the check; pairs not reached are then
     * unchecked.
     */
    static InterferenceReport check(
        const std::vector<InterferencePart>& parts,
        const std::vector<std::size_t>& group,
        const std::vector<std::size_t>& others,
        const InterferenceSettings& settings
    );
};

}  // namespace Inspection
