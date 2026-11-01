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
#include <array>
#include <cctype>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <format>
#include <map>
#include <mutex>
#include <string_view>
#include <thread>
#include <tuple>

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GProp_PrincipalProps.hxx>
#include <BRep_Tool.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <App/DocumentObject.h>
#include <Base/Exception.h>
#include <Base/Sequencer.h>

#include "PartsList.h"

using namespace Inspection;

namespace
{

/// What tells one shape from another, wherever it is placed
struct ShapeTraits
{
    std::array<int, 4> counts {};  ///< solids, faces, edges, vertices
    double volume = 0.0;
    double area = 0.0;
    std::array<double, 3> moments {};  ///< principal moments of inertia, smallest first
    int hand = 0;                      ///< +1 or -1; 0 for a shape that is its own mirror image
    gp_XYZ size;
};

bool close(double a, double b, double scale)
{
    constexpr double relative = 1e-6;
    return std::abs(a - b) <= relative * std::max({std::abs(a), std::abs(b), scale, 1e-9});
}

// Same shape, the hand aside
bool congruent(const ShapeTraits& a, const ShapeTraits& b)
{
    const double moment = std::max(a.moments[2], b.moments[2]);
    return a.counts == b.counts && close(a.volume, b.volume, 0.0) && close(a.area, b.area, 0.0)
        && close(a.moments[0], b.moments[0], moment) && close(a.moments[1], b.moments[1], moment)
        && close(a.moments[2], b.moments[2], moment);
}

// Which hand a shape is, from its B-rep vertices about the centre of mass in its principal
// axes, smallest moment first. Each axis is pointed to the side where the vertices reach
// further (the sign of their third moment), and the axes make a right- or a left-handed
// frame. Where an axis has no such side, as for a part with a half turn symmetry about a
// principal axis, the sign of the sum of x·y·z in a right-handed principal frame tells:
// flipping two axes leaves it, a mirror image turns it over. A mirror image turns the hand
// over; turning or moving the shape does not. Shapes with a principal mirror plane, and
// shapes with equal moments (the axes are not fixed), get 0 and are not told from their
// mirror images.
int handOf(const GProp_GProps& props, const TopoDS_Shape& shape)
{
    const GProp_PrincipalProps principal = props.PrincipalProperties();
    std::array<std::pair<double, gp_Vec>, 3> byMoment;
    principal.Moments(byMoment[0].first, byMoment[1].first, byMoment[2].first);
    byMoment[0].second = principal.FirstAxisOfInertia();
    byMoment[1].second = principal.SecondAxisOfInertia();
    byMoment[2].second = principal.ThirdAxisOfInertia();
    std::ranges::sort(byMoment, {}, &std::pair<double, gp_Vec>::first);
    const double largest = byMoment[2].first;
    if (largest <= 0.0 || close(byMoment[0].first, byMoment[1].first, largest)
        || close(byMoment[1].first, byMoment[2].first, largest)) {
        return 0;
    }
    std::array<gp_Vec, 3> axes {byMoment[0].second, byMoment[1].second, byMoment[2].second};

    const gp_Pnt centre = props.CentreOfMass();
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, TopAbs_VERTEX, map);
    std::vector<gp_Vec> offsets;
    offsets.reserve(map.Extent());
    for (int i = 1; i <= map.Extent(); ++i) {
        offsets.emplace_back(centre, BRep_Tool::Pnt(TopoDS::Vertex(map(i))));
    }
    constexpr double negligible = 1e-6;

    bool sided = true;
    for (gp_Vec& axis : axes) {
        double third = 0.0;
        double scale = 0.0;
        for (const gp_Vec& offset : offsets) {
            const double along = offset.Dot(axis);
            third += along * along * along;
            scale += std::abs(along * along * along);
        }
        if (std::abs(third) <= negligible * scale) {
            sided = false;
            break;
        }
        if (third < 0.0) {
            axis.Reverse();
        }
    }
    if (sided) {
        return axes[0].Crossed(axes[1]).Dot(axes[2]) > 0.0 ? 1 : -1;
    }

    const gp_Vec first = byMoment[0].second;
    const gp_Vec second = byMoment[1].second;
    const gp_Vec third = first.Crossed(second);
    double product = 0.0;
    double scale = 0.0;
    for (const gp_Vec& offset : offsets) {
        const double xyz = offset.Dot(first) * offset.Dot(second) * offset.Dot(third);
        product += xyz;
        scale += std::abs(xyz);
    }
    if (std::abs(product) <= negligible * scale) {
        return 0;
    }
    return product > 0.0 ? 1 : -1;
}

ShapeTraits traitsOf(const TopoDS_Shape& shape)
{
    ShapeTraits traits;
    for (TopExp_Explorer solid(shape, TopAbs_SOLID); solid.More(); solid.Next()) {
        ++traits.counts[0];
    }
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, TopAbs_FACE, map);
    traits.counts[1] = map.Extent();
    map.Clear();
    TopExp::MapShapes(shape, TopAbs_EDGE, map);
    traits.counts[2] = map.Extent();
    map.Clear();
    TopExp::MapShapes(shape, TopAbs_VERTEX, map);
    traits.counts[3] = map.Extent();

    GProp_GProps volume;
    BRepGProp::VolumeProperties(shape, volume);
    traits.volume = volume.Mass();
    GProp_GProps surface;
    BRepGProp::SurfaceProperties(shape, surface);
    traits.area = surface.Mass();
    double i1 = 0.0;
    double i2 = 0.0;
    double i3 = 0.0;
    volume.PrincipalProperties().Moments(i1, i2, i3);
    traits.moments = {i1, i2, i3};
    std::ranges::sort(traits.moments);
    traits.hand = handOf(volume, shape);

    // In the part's own axes: the placement is the shape's location
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape.Located(TopLoc_Location()), box, false, false);
    if (!box.IsVoid()) {
        double x0 = 0.0, y0 = 0.0, z0 = 0.0, x1 = 0.0, y1 = 0.0, z1 = 0.0;
        box.Get(x0, y0, z0, x1, y1, z1);
        traits.size.SetCoord(x1 - x0, y1 - y0, z1 - z0);
    }
    return traits;
}

// The traits of every part, on several threads; the progress bar stays on this one. Shapes
// are only read. No Base::Console or FC_* calls on the threads: they reach the report view
// directly, off the GUI thread.
std::vector<ShapeTraits> traitsOf(const std::vector<InterferencePart>& parts)
{
    std::vector<ShapeTraits> traits(parts.size());
    std::atomic<std::size_t> next {0};
    std::atomic<std::size_t> done {0};
    std::atomic<bool> stop {false};
    std::mutex mutex;
    std::condition_variable progressed;
    auto work = [&]() {
        while (!stop) {
            const std::size_t k = next++;
            if (k >= parts.size()) {
                break;
            }
            try {
                traits[k] = traitsOf(parts[k].shape);
            }
            catch (...) {
                traits[k] = {};  // counted as a shape of its own below, by its index
                traits[k].counts[0] = -1 - static_cast<int>(k);
            }
            {
                std::lock_guard lock(mutex);
                ++done;
            }
            progressed.notify_one();
        }
        progressed.notify_one();
    };

    std::vector<std::thread> threads;
    struct StopAndJoin
    {
        std::atomic<bool>& stop;
        std::vector<std::thread>& threads;
        ~StopAndJoin()
        {
            stop = true;
            for (auto& thread : threads) {
                if (thread.joinable()) {
                    thread.join();
                }
            }
        }
    } guard {stop, threads};
    const std::size_t count = std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1, 16);
    threads.reserve(count);
    for (std::size_t t = 0; t < count; ++t) {
        threads.emplace_back(work);
    }

    Base::SequencerLauncher progress("Listing parts...", parts.size());
    std::size_t shown = 0;
    while (shown < parts.size()) {
        {
            std::unique_lock lock(mutex);
            progressed.wait_for(lock, std::chrono::milliseconds(100), [&] { return done > shown; });
        }
        const std::size_t now = done;
        if (now == shown) {
            Base::Sequencer().checkAbort();
        }
        for (; shown < now; ++shown) {
            progress.next(true);  // AbortException leaves through the guard
        }
    }
    return traits;
}

// "Screw001" may be a copy of "Screw": FreeCAD numbers copies with three digits. Empty when
// the label has no such number.
std::string withoutCopyNumber(const std::string& label)
{
    std::size_t end = label.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(label[end - 1]))) {
        --end;
    }
    return (label.size() - end >= 3 && end > 0) ? label.substr(0, label.size() - 3) : std::string();
}

std::string csvField(const std::string& label)
{
    // A spreadsheet would read a label such as "=A1" or "-5V" as a formula
    const std::string text
        = (!label.empty() && std::string_view("=+-@").find(label.front()) != std::string_view::npos)
        ? "'" + label
        : label;
    if (text.find_first_of(",\"\r\n") == std::string::npos) {
        return text;
    }
    std::string quoted = "\"";
    for (char c : text) {
        quoted += c;
        if (c == '"') {
            quoted += '"';
        }
    }
    return quoted + "\"";
}

}  // namespace

std::vector<PartsListItem> PartsList::group(const std::vector<InterferencePart>& parts)
{
    // Labels on this thread: document objects are not for the workers
    std::vector<std::string> labels(parts.size());
    std::vector<std::string> descriptions(parts.size());
    for (std::size_t i = 0; i < parts.size(); ++i) {
        // A link goes by the name of its object, as in the Assembly BOM
        if (App::DocumentObject* obj = parts[i].object.getSubObject()) {
            App::DocumentObject* linked = obj->getLinkedObject(true);
            const App::DocumentObject* named = linked ? linked : obj;
            labels[i] = named->Label.getStrValue();
            descriptions[i] = named->Label2.getStrValue();
        }
    }
    const std::vector<ShapeTraits> traits = traitsOf(parts);

    std::vector<PartsListItem> items;
    std::vector<std::size_t> firstOf;   // the part whose traits an item has
    std::vector<std::size_t> mirrorOf;  // the item a mirrored one is the image of
    std::map<std::array<int, 4>, std::vector<std::size_t>> byCounts;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        auto& candidates = byCounts[traits[i].counts];
        auto found = std::ranges::find_if(candidates, [&](std::size_t item) {
            const ShapeTraits& other = traits[firstOf[item]];
            return other.hand == traits[i].hand && congruent(other, traits[i]);
        });
        if (found != candidates.end()) {
            items[*found].instances.push_back(i);
            continue;
        }
        PartsListItem item;
        item.instances.push_back(i);
        item.volume = traits[i].volume;
        item.area = traits[i].area;
        item.size = traits[i].size;
        item.description = descriptions[i];
        std::size_t imageOf = 0;
        if (traits[i].hand != 0) {
            auto image = std::ranges::find_if(candidates, [&](std::size_t earlier) {
                const ShapeTraits& other = traits[firstOf[earlier]];
                return other.hand == -traits[i].hand && congruent(other, traits[i]);
            });
            if (image != candidates.end()) {
                item.mirrored = true;
                imageOf = *image;
            }
        }
        candidates.push_back(items.size());
        firstOf.push_back(i);
        mirrorOf.push_back(imageOf);
        items.push_back(std::move(item));
    }

    // The name most of an item's pieces have, as their label or as the label they are numbered
    // copies of: "Screw" for "Screw", "Screw001" and "Screw002". A base counts only when it is
    // itself the label of a piece, so "Bolt2024" and "Bolt2025" do not become "Bolt2". On a
    // tie, the earlier name.
    for (auto& item : items) {
        struct Vote
        {
            std::string name;
            std::size_t count = 0;
            bool label = false;
        };
        std::vector<Vote> votes;
        auto vote = [&votes](const std::string& name, bool label) {
            auto found = std::ranges::find(votes, name, &Vote::name);
            if (found == votes.end()) {
                votes.push_back({name, 1, label});
            }
            else {
                ++found->count;
                found->label = found->label || label;
            }
        };
        for (std::size_t i : item.instances) {
            vote(labels[i], true);
            if (const std::string base = withoutCopyNumber(labels[i]); !base.empty()) {
                vote(base, false);
            }
        }
        std::size_t best = 0;
        for (const auto& candidate : votes) {
            if (candidate.label && candidate.count > best) {
                best = candidate.count;
                item.name = candidate.name;
            }
        }
    }

    // A mirror image is named after the item it is the image of, when its pieces are labelled
    // or numbered as that item, with the Assembly BOM's suffix
    for (std::size_t k = 0; k < items.size(); ++k) {
        if (!items[k].mirrored) {
            continue;
        }
        const std::string& original = items[mirrorOf[k]].name;
        const bool sameName = std::ranges::any_of(items[k].instances, [&](std::size_t i) {
            return labels[i] == original || withoutCopyNumber(labels[i]) == original;
        });
        if (sameName) {
            items[k].name = original + " (mirrored)";
        }
    }
    return items;
}

std::string PartsList::csv(
    const std::vector<PartsListItem>& items,
    const std::vector<InterferencePart>& parts
)
{
    // The byte order mark tells spreadsheet programs the file is UTF-8
    std::string text = "\xEF\xBB\xBF";
    text += "Index,Name,Description,Quantity,Size X (mm),Size Y (mm),Size Z (mm),Volume "
            "(mm\xC2\xB3),Parts\r\n";
    for (std::size_t k = 0; k < items.size(); ++k) {
        const auto& item = items[k];
        std::string where;
        for (std::size_t i : item.instances) {
            where += (where.empty() ? "" : "; ") + parts[i].name;
        }
        text += std::format(
            "{},{},{},{},{:.3f},{:.3f},{:.3f},{:.3f},{}\r\n",
            k + 1,
            csvField(item.name),
            csvField(item.description),
            item.instances.size(),
            item.size.X(),
            item.size.Y(),
            item.size.Z(),
            item.volume,
            csvField(where)
        );
    }
    return text;
}
