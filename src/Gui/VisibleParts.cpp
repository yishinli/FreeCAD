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

#include <App/ComplexGeoData.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/GeoFeature.h>
#include <App/GroupExtension.h>
#include <App/Link.h>
#include <App/PropertyGeo.h>

#include "VisibleParts.h"

namespace Gui::VisibleParts
{

namespace
{

App::DocumentObject* resolved(App::DocumentObject* obj)
{
    App::DocumentObject* linked = obj->getLinkedObject(true);
    return linked ? linked : obj;
}

void collect(
    App::DocumentObject* root,
    const std::string& sub,
    int depth,
    std::vector<App::SubObjectT>& parts
)
{
    constexpr int maxDepth = 64;  // a link that leads back to itself
    App::DocumentObject* obj = sub.empty() ? root : root->getSubObject(sub.c_str());
    if (!obj || depth > maxDepth) {
        return;
    }
    if (isPart(obj)) {
        parts.emplace_back(root, sub.c_str());
        return;
    }
    if (!isContainer(obj)) {
        return;
    }
    for (const std::string& child : children(root, sub)) {
        collect(root, child, depth + 1, parts);
    }
}

}  // namespace

bool isPart(App::DocumentObject* obj)
{
    auto* geo = freecad_cast<App::GeoFeature*>(resolved(obj));
    const auto* prop = geo ? geo->getPropertyOfGeometry() : nullptr;
    const Data::ComplexGeoData* data = prop ? prop->getComplexData() : nullptr;
    return data && data->countSubElements("Face") > 0;
}

bool isContainer(App::DocumentObject* obj)
{
    if (isPart(obj)) {
        return false;
    }
    return resolved(obj)->hasExtension(App::GroupExtension::getExtensionClassTypeId())
        || obj->hasExtension(App::LinkBaseExtension::getExtensionClassTypeId());
}

std::vector<App::DocumentObject*> roots(App::Document* doc)
{
    std::vector<App::DocumentObject*> visible;
    for (App::DocumentObject* obj : doc->getRootObjectsIgnoreLinks()) {
        if (obj->Visibility.getValue()) {
            visible.push_back(obj);
        }
    }
    return visible;
}

std::vector<std::string> children(App::DocumentObject* root, const std::string& sub)
{
    std::vector<std::string> visible;
    App::DocumentObject* obj = sub.empty() ? root : root->getSubObject(sub.c_str());
    if (!obj) {
        return visible;
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
        const int shown = obj->isElementVisible(element.c_str());
        if (shown == 0 || (shown < 0 && !childObj->Visibility.getValue())) {
            continue;
        }
        visible.push_back(sub + child);
    }
    return visible;
}

void collect(App::DocumentObject* root, const std::string& sub, std::vector<App::SubObjectT>& parts)
{
    collect(root, sub, 0, parts);
}

}  // namespace Gui::VisibleParts
