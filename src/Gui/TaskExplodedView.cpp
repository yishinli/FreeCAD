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
#include <map>

#include <Inventor/nodes/SoTransform.h>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/GeoFeature.h>
#include <App/Link.h>
#include <Base/BoundBox.h>
#include <Base/Parameter.h>

#include "Application.h"
#include "BitmapFactory.h"
#include "Document.h"
#include "MainWindow.h"
#include "TaskExplodedView.h"
#include "TaskView/TaskView.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewProviderDocumentObject.h"
#include "VisibleParts.h"

using namespace Gui;

namespace
{

ParameterGrp::handle settingsGroup()
{
    return App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/View/ExplodedView"
    );
}

enum Level
{
    Components,
    AllParts
};

// The objects along a subname: root, then the one each step leads to
std::vector<std::pair<App::DocumentObject*, std::string>> pathOf(
    App::DocumentObject* root,
    const std::string& sub
)
{
    std::vector<std::pair<App::DocumentObject*, std::string>> path {{root, std::string()}};
    std::string prefix;
    std::size_t start = 0;
    for (std::size_t dot = sub.find('.'); dot != std::string::npos; dot = sub.find('.', start)) {
        prefix = sub.substr(0, dot + 1);
        start = dot + 1;
        App::DocumentObject* obj = root->getSubObject(prefix.c_str());
        if (!obj) {
            break;
        }
        path.emplace_back(obj, prefix);
    }
    return path;
}

}  // namespace

TaskExplodedView::TaskExplodedView()
{
    if (App::Document* doc = App::GetApplication().getActiveDocument()) {
        docName = doc->getName();
    }

    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    auto* form = new QFormLayout();

    level = new QComboBox();
    level->addItem(tr("Components"));
    level->addItem(tr("All parts"));
    level->setToolTip(
        tr("Components keep sub-assemblies and their screws together; all parts move every part")
    );
    level->setCurrentIndex(
        settingsGroup()->GetInt("Level", Components) == AllParts ? AllParts : Components
    );
    form->addRow(tr("Move apart"), level);

    distance = new QSlider(Qt::Horizontal);
    distance->setRange(0, 100);
    distance->setValue(static_cast<int>(settingsGroup()->GetInt("Distance", 50)));
    form->addRow(tr("Distance"), distance);
    layout->addLayout(form);

    keepLargest = new QCheckBox(tr("Keep the largest part in place"));
    keepLargest->setChecked(settingsGroup()->GetBool("KeepLargest", true));
    layout->addWidget(keepLargest);

    summary = new QLabel();
    summary->setWordWrap(true);
    layout->addWidget(summary);

    auto* box = new TaskView::TaskBox(
        BitmapFactory().pixmap("Std_ExplodedView"),
        tr("Exploded View"),
        true,
        nullptr
    );
    box->groupLayout()->addWidget(widget);
    Content.push_back(box);
    setAutoCloseOnDeletedDocument(true);

    connect(level, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        settingsGroup()->SetInt("Level", index);
        rebuild();
    });
    connect(keepLargest, &QCheckBox::toggled, this, [this](bool on) {
        settingsGroup()->SetBool("KeepLargest", on);
        rebuild();
    });
    connect(distance, &QSlider::valueChanged, this, [this](int value) {
        settingsGroup()->SetInt("Distance", value);
        move();
    });

    // While open the document can still change (an undo, a script, a recompute): collect again
    // from the real places, where restore() leaves what really moved
    if (App::Document* doc = App::GetApplication().getDocument(docName.c_str())) {
        auto later = [this](auto&&...) {
            rebuildLater();
        };
        connections.emplace_back(doc->signalRecomputed.connect(later));
        connections.emplace_back(doc->signalChangedObject.connect(later));
        connections.emplace_back(doc->signalNewObject.connect(later));
        connections.emplace_back(doc->signalDeletedObject.connect(later));
    }

    rebuild();
}

TaskExplodedView::~TaskExplodedView()
{
    restore();
}

bool TaskExplodedView::reject()
{
    restore();
    return true;
}

// Also when closed without the button, by Gui.Control.closeDialog() or with the document
void TaskExplodedView::closed()
{
    restore();
}

double TaskExplodedView::factor() const
{
    constexpr double farthest = 2.0;  // twice a component's distance from the centre
    return farthest * distance->value() / distance->maximum();
}

SoTransform* TaskExplodedView::transformOf(const Unit& unit) const
{
    App::DocumentObject* obj = unit.object.getSubObject();
    Gui::Document* doc = Application::Instance->getDocument(docName.c_str());
    if (!obj || !doc || obj->getDocument() != doc->getDocument()) {
        return nullptr;
    }
    ViewProvider* vp = doc->getViewProvider(obj);
    return vp ? vp->getTransformNode() : nullptr;
}

void TaskExplodedView::rebuild()
{
    restore();
    collect();
    move();
}

void TaskExplodedView::collect()
{
    units.clear();
    shared = 0;
    App::Document* doc = App::GetApplication().getDocument(docName.c_str());
    Gui::Document* guiDoc = Application::Instance->getDocument(docName.c_str());
    if (!doc || !guiDoc) {
        return;
    }

    // Components: the top objects, opened while a level holds a single container, as an
    // imported assembly nests one App::Part in another. All parts: every part.
    std::vector<App::SubObjectT> candidates;
    if (level->currentIndex() == AllParts) {
        for (App::DocumentObject* root : VisibleParts::roots(doc)) {
            VisibleParts::collect(root, std::string(), candidates);
        }
    }
    else {
        for (App::DocumentObject* root : VisibleParts::roots(doc)) {
            candidates.emplace_back(root, "");
        }
        while (candidates.size() == 1
               && VisibleParts::isContainer(candidates.front().getSubObject())) {
            App::DocumentObject* root = candidates.front().getObject();
            const auto children = VisibleParts::children(root, candidates.front().getSubName());
            if (children.empty()) {
                break;
            }
            candidates.clear();
            for (const auto& child : children) {
                candidates.emplace_back(root, child.c_str());
            }
        }
        std::erase_if(candidates, [](const App::SubObjectT& candidate) {
            App::DocumentObject* obj = candidate.getSubObject();
            return !obj || !(VisibleParts::isPart(obj) || VisibleParts::isContainer(obj));
        });
    }

    // Boxes as this document's own view draws them, whichever view is active
    const View3DInventorViewer* viewer = nullptr;
    const auto views = guiDoc->getMDIViewsOfType(View3DInventor::getClassTypeId());
    if (!views.empty()) {
        viewer = static_cast<View3DInventor*>(views.front())->getViewer();
    }

    // A part inside something a link shows is drawn once for every such link: it cannot move
    // on its own, so the first link on its path moves instead, with whatever else it holds
    std::map<std::string, std::size_t> byKey;
    for (const auto& candidate : candidates) {
        App::DocumentObject* root = candidate.getObject();
        if (!root) {
            continue;
        }
        const auto path = pathOf(root, candidate.getSubName());
        std::size_t moving = path.size() - 1;
        for (std::size_t i = 0; i + 1 < path.size(); ++i) {
            if (path[i].first->hasExtension(App::LinkBaseExtension::getExtensionClassTypeId())) {
                moving = i;
                break;
            }
        }
        const std::string& sub = path[moving].second;
        const std::string key = std::string(root->getNameInDocument()) + "." + sub;
        if (byKey.contains(key)) {
            continue;
        }
        if (moving + 1 != path.size()) {
            ++shared;
        }

        Unit unit;
        unit.object = App::SubObjectT(root, sub.c_str());
        auto* rootView = freecad_cast<ViewProviderDocumentObject*>(guiDoc->getViewProvider(root));
        if (!rootView) {
            continue;
        }
        const Base::BoundBox3d box
            = rootView->getBoundingBox(sub.empty() ? nullptr : sub.c_str(), nullptr, true, viewer);
        if (!box.IsValid()) {
            continue;
        }
        unit.centre = box.GetCenter();
        unit.size = box.CalcDiagonalLength();

        // Its transform is in its parent's frame: the root's parent is the world
        Base::Matrix4D parent;
        if (moving > 0) {
            const std::string& parentSub = path[moving - 1].second;
            if (!parentSub.empty()) {
                root->getSubObject(parentSub.c_str(), nullptr, &parent, true);
            }
            else if (auto* geo = freecad_cast<App::GeoFeature*>(root)) {
                parent = geo->Placement.getValue().toMatrix();
            }
        }
        unit.parentInverse = parent;
        unit.parentInverse.inverseGauss();

        SoTransform* transform = transformOf(unit);
        if (!transform) {
            continue;
        }
        unit.original = unit.written = transform->translation.getValue();
        byKey.emplace(key, units.size());
        units.push_back(std::move(unit));
    }

    // From the largest one, which stays, or from the middle of them all
    centre = Base::Vector3d();
    if (!units.empty()) {
        if (keepLargest->isChecked()) {
            centre = std::ranges::max_element(units, {}, &Unit::size)->centre;
        }
        else {
            for (const auto& unit : units) {
                centre += unit.centre;
            }
            centre /= static_cast<double>(units.size());
        }
    }

    if (units.size() < 2) {
        summary->setText(tr("Nothing to move apart: the document shows a single part."));
    }
    else {
        QString text = tr("%n component(s) move apart.", "", static_cast<int>(units.size()));
        if (shared > 0) {
            text += QLatin1Char(' ') + tr("%n linked sub-assembly(s) move as a whole.", "", shared);
        }
        summary->setText(text);
    }
}

void TaskExplodedView::move()
{
    const double f = factor();
    for (auto& unit : units) {
        SoTransform* transform = transformOf(unit);
        if (!transform) {
            continue;
        }
        // Someone else placed it since: that is its place now
        if (transform->translation.getValue() != unit.written) {
            unit.original = transform->translation.getValue();
        }
        const Base::Vector3d offset = (unit.centre - centre) * f;
        const Base::Vector3d local = unit.parentInverse * offset
            - unit.parentInverse * Base::Vector3d();
        unit.written = unit.original
            + SbVec3f(static_cast<float>(local.x),
                      static_cast<float>(local.y),
                      static_cast<float>(local.z));
        transform->translation.setValue(unit.written);
    }
}

void TaskExplodedView::rebuildLater()
{
    if (pending) {
        return;
    }
    pending = true;
    QTimer::singleShot(0, this, [this]() {
        pending = false;
        rebuild();
    });
}

void TaskExplodedView::restore()
{
    for (const auto& unit : units) {
        SoTransform* transform = transformOf(unit);
        // Left where a real placement change put it
        if (transform && transform->translation.getValue() == unit.written) {
            transform->translation.setValue(unit.original);
        }
    }
    units.clear();
}

#include "moc_TaskExplodedView.cpp"
