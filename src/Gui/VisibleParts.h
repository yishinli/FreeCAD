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

#include <App/DocumentObserver.h>

namespace App
{
class Document;
class DocumentObject;
}  // namespace App

namespace Gui::VisibleParts
{

/// An object with faces, itself or through a link: a Part feature, a PartDesign body. Sketches,
/// datums and meshes have none.
bool isPart(App::DocumentObject* obj);

/// An object that opens into children: a group, an App::Part, a link to one, a link array
bool isContainer(App::DocumentObject* obj);

/// The document's top objects as the tree shows them, the visible ones
std::vector<App::DocumentObject*> roots(App::Document* doc);

/// The visible children of the object at sub from root, as subnames from root
std::vector<std::string> children(App::DocumentObject* root, const std::string& sub);

/// The visible parts at and below sub from root, opening containers
void collect(App::DocumentObject* root, const std::string& sub, std::vector<App::SubObjectT>& parts);

}  // namespace Gui::VisibleParts
