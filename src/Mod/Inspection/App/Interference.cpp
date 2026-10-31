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

#include <algorithm>
#include <chrono>
#include <set>
#include <utility>

#include <BRepBndLib.hxx>
#include <BRep_Tool.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Precision.hxx>
#include <ShapeAnalysis_ShapeTolerance.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/GroupExtension.h>
#include <App/Link.h>
#include <Base/Exception.h>
#include <Base/Sequencer.h>
#include <Mod/Mesh/App/MeshFeature.h>
#include <Mod/Part/App/FCBRepAlgoAPI_Common.h>
#include <Mod/Part/App/PartFeature.h>

#include "Interference.h"

using namespace Inspection;

namespace
{

// Deeper than any real assembly; it stops a link that leads back to itself
constexpr int maxDepth = 64;

class PartCollector
{
public:
    std::vector<InterferencePart> parts;
    std::vector<std::string>* unchecked = nullptr;
    std::set<std::string> keys;

    // obj is root itself or the object at sub, a path from root ending in '.'
    void collect(App::DocumentObject* root, const std::string& sub, const std::string& path, int depth)
    {
        App::DocumentObject* obj = sub.empty() ? root : root->getSubObject(sub.c_str());
        if (!obj || depth > maxDepth) {
            return;
        }
        App::DocumentObject* linked = obj->getLinkedObject(true);
        if (!linked) {
            linked = obj;
        }
        const std::string name = path.empty() ? obj->Label.getStrValue()
                                              : path + " / " + obj->Label.getStrValue();

        // A shape feature is one part, whatever it is made of
        if (linked->isDerivedFrom<Part::Feature>()) {
            const TopoDS_Shape shape = Part::Feature::getShape(
                root,
                Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform,
                sub.empty() ? nullptr : sub.c_str()
            );
            std::string key = std::string(root->getNameInDocument()) + "." + sub;
            if (!shape.IsNull() && TopExp_Explorer(shape, TopAbs_SOLID).More()
                && keys.insert(key).second) {
                parts.push_back({App::SubObjectT(root, sub.c_str()), name, shape, std::move(key)});
            }
            return;
        }
        if (linked->isDerivedFrom<Mesh::Feature>()) {
            if (unchecked) {
                unchecked->push_back(name);
            }
            return;
        }

        // Open groups, App::Part, assemblies and links; anything else (joints, datums,
        // annotations) holds no part of its own
        const bool container = linked->hasExtension(App::GroupExtension::getExtensionClassTypeId())
            || obj->hasExtension(App::LinkBaseExtension::getExtensionClassTypeId());
        if (!container) {
            return;
        }
        for (const std::string& child : obj->getSubObjects(App::DocumentObject::GS_SELECT)) {
            App::DocumentObject* childObj = obj->getSubObject(child.c_str());
            if (!childObj) {
                continue;
            }
            std::string element = child;
            if (!element.empty() && element.back() == '.') {
                element.pop_back();
            }
            const int visible = obj->isElementVisible(element.c_str());
            if (visible == 0 || (visible < 0 && !childObj->Visibility.getValue())) {
                continue;
            }
            collect(root, sub + child, name, depth + 1);
        }
    }

    // Where root is not the document's top, the labels of the containers above the path
    static std::string pathLabels(App::DocumentObject* root, const std::string& sub)
    {
        std::string labels;
        const auto objects = root->getSubObjectList(sub.c_str());
        for (std::size_t i = 0; i + 1 < objects.size(); ++i) {
            if (objects[i]) {
                labels += (labels.empty() ? "" : " / ") + objects[i]->Label.getStrValue();
            }
        }
        return labels;
    }
};

Bnd_Box boxOf(const TopoDS_Shape& shape, double grow)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    box.Enlarge(grow);
    return box;
}

// Whether a solid of inner lies inside a solid of outer. The distance between two solids
// is 0 when one holds the other, but not between compounds of solids.
bool holds(const TopoDS_Shape& outer, const TopoDS_Shape& inner)
{
    for (TopExp_Explorer o(outer, TopAbs_SOLID); o.More(); o.Next()) {
        BRepClass3d_SolidClassifier classifier(o.Current());
        for (TopExp_Explorer i(inner, TopAbs_SOLID); i.More(); i.Next()) {
            TopExp_Explorer vertex(i.Current(), TopAbs_VERTEX);
            if (!vertex.More()) {
                continue;
            }
            classifier.Perform(BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current())), Precision::Confusion());
            if (classifier.State() == TopAbs_IN) {
                return true;
            }
        }
    }
    return false;
}

double shapeTolerance(const TopoDS_Shape& shape)
{
    // A sloppy edge tolerance must not hide a real gap
    constexpr double largest = 0.01;  // mm
    return std::min(ShapeAnalysis_ShapeTolerance().Tolerance(shape, 1), largest);
}

InterferenceResult unchecked(std::size_t first, std::size_t second, const std::string& reason)
{
    InterferenceResult result;
    result.first = first;
    result.second = second;
    result.reason = reason;
    return result;
}

// Clash, contact, clearance, or nothing to report (kind Unchecked with no reason)
InterferenceResult measure(
    const InterferencePart& a,
    const InterferencePart& b,
    std::size_t first,
    std::size_t second,
    const InterferenceSettings& settings,
    double touching
)
{
    InterferenceResult result;
    result.first = first;
    result.second = second;

    BRepExtrema_DistShapeShape distance(a.shape, b.shape);
    if (!distance.IsDone() || distance.NbSolution() < 1) {
        return unchecked(first, second, "the distance could not be measured");
    }
    const double gap = distance.Value();
    result.distance = gap;
    result.point1 = distance.PointOnShape1(1);
    result.point2 = distance.PointOnShape2(1);
    const bool compound = a.shape.ShapeType() != TopAbs_SOLID || b.shape.ShapeType() != TopAbs_SOLID;
    if (gap > touching && !(compound && (holds(a.shape, b.shape) || holds(b.shape, a.shape)))) {
        if (gap <= settings.clearance) {
            result.kind = InterferenceKind::Clearance;
        }
        return result;
    }

    // Touching, or one inside the other: only now is it worth intersecting them
    FCBRepAlgoAPI_Common common(a.shape, b.shape);
    if (!common.IsDone()) {
        return unchecked(first, second, "the parts could not be intersected");
    }
    const TopoDS_Shape shared = common.Shape();
    GProp_GProps props;
    BRepGProp::VolumeProperties(shared, props);
    if (props.Mass() > settings.minVolume) {
        result.kind = InterferenceKind::Clash;
        result.volume = props.Mass();
        result.distance = 0.0;
        result.point1 = result.point2 = props.CentreOfMass();
        result.shared = shared;
    }
    else {
        result.kind = InterferenceKind::Contact;
        result.volume = std::max(0.0, props.Mass());
    }
    return result;
}

}  // namespace

std::vector<InterferencePart> InterferenceCheck::collectParts(
    const std::vector<App::SubObjectT>& objects,
    std::vector<std::string>* unchecked
)
{
    PartCollector collector;
    collector.unchecked = unchecked;
    for (const auto& objT : objects) {
        App::DocumentObject* root = objT.getObject();
        if (!root) {
            continue;
        }
        const std::string sub = objT.getSubNameNoElement();
        collector.collect(root, sub, PartCollector::pathLabels(root, sub), 0);
    }
    return collector.parts;
}

std::vector<InterferencePart> InterferenceCheck::collectParts(
    const App::Document* doc,
    std::vector<std::string>* unchecked
)
{
    std::vector<App::SubObjectT> roots;
    if (doc) {
        // As the tree shows them: an object a link points to is still drawn itself
        for (auto* obj : doc->getRootObjectsIgnoreLinks()) {
            if (obj->Visibility.getValue()) {
                roots.emplace_back(obj, "");
            }
        }
    }
    return collectParts(roots, unchecked);
}

InterferenceReport InterferenceCheck::check(
    const std::vector<InterferencePart>& parts,
    const std::vector<std::size_t>& group,
    const std::vector<std::size_t>& others,
    const InterferenceSettings& settings
)
{
    InterferenceReport report;

    // The pairs asked for, each once
    std::set<std::pair<std::size_t, std::size_t>> pairs;
    auto add = [&](std::size_t i, std::size_t j) {
        if (i != j && i < parts.size() && j < parts.size()) {
            pairs.emplace(std::min(i, j), std::max(i, j));
        }
    };
    if (others.empty()) {
        for (std::size_t i = 0; i < group.size(); ++i) {
            for (std::size_t j = i + 1; j < group.size(); ++j) {
                add(group[i], group[j]);
            }
        }
    }
    else {
        for (std::size_t i : group) {
            for (std::size_t j : others) {
                add(i, j);
            }
        }
    }
    report.pairs = pairs.size();

    // Boxes grown by half the clearance each meet for any pair closer than the clearance
    const double grow = settings.clearance / 2.0 + settings.tolerance;
    std::vector<Bnd_Box> boxes(parts.size());
    std::vector<bool> boxed(parts.size(), false);
    auto box = [&](std::size_t i) -> const Bnd_Box& {
        if (!boxed[i]) {
            boxes[i] = boxOf(parts[i].shape, grow);
            boxed[i] = true;
        }
        return boxes[i];
    };
    // Mated faces of imported parts are apart by up to the parts' own tolerances
    std::vector<double> tolerances(parts.size(), -1.0);
    auto tolerance = [&](std::size_t i) {
        if (tolerances[i] < 0.0) {
            tolerances[i] = shapeTolerance(parts[i].shape);
        }
        return tolerances[i];
    };
    std::vector<std::pair<std::size_t, std::size_t>> candidates;
    for (const auto& [i, j] : pairs) {
        if (box(i).IsOut(box(j))) {
            ++report.pruned;
        }
        else {
            candidates.emplace_back(i, j);
        }
    }

    const auto start = std::chrono::steady_clock::now();
    const auto budget = std::chrono::duration<double>(settings.budget);
    std::string stopped;
    Base::SequencerLauncher progress("Checking interference...", candidates.size());
    for (const auto& [i, j] : candidates) {
        if (stopped.empty() && std::chrono::steady_clock::now() - start > budget) {
            stopped = "the time budget ran out";
        }
        if (stopped.empty()) {
            try {
                progress.next(true);
            }
            catch (const Base::AbortException&) {
                stopped = "the check was stopped";
            }
        }
        if (!stopped.empty()) {
            report.results.push_back(unchecked(i, j, stopped));
            continue;
        }

        InterferenceResult result;
        try {
            const double touching = std::max(settings.tolerance, tolerance(i) + tolerance(j));
            result = measure(parts[i], parts[j], i, j, settings, touching);
        }
        catch (const Standard_Failure& e) {
            result = unchecked(i, j, e.GetMessageString() ? e.GetMessageString() : "OCC error");
        }
        catch (const Base::Exception& e) {
            result = unchecked(i, j, e.what());
        }
        ++report.checked;
        if (result.kind != InterferenceKind::Unchecked || !result.reason.empty()) {
            report.results.push_back(std::move(result));
        }
    }
    return report;
}
