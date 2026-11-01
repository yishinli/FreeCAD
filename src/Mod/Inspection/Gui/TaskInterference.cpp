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
#include <cmath>
#include <map>

#include <QCheckBox>
#include <QLocale>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>

#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoMarkerSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoPickStyle.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShapeHints.h>

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Quantity.h>
#include <Base/UnitsApi.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/Document.h>
#include <Gui/Inventor/MarkerBitmaps.h>
#include <Gui/QuantitySpinBox.h>
#include <Gui/Selection/Selection.h>
#include <Gui/TaskView/TaskView.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/WaitCursor.h>
#include <Mod/Part/App/TopoShape.h>

#include "ColumnUnit.h"
#include "TaskInterference.h"

using namespace InspectionGui;
using Inspection::InterferenceCheck;
using Inspection::InterferenceKind;

namespace
{

ParameterGrp::handle settingsGroup()
{
    return App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Inspection/Interference"
    );
}

// Colors of #28165: clash red, contact yellow, clearance cyan
SbColor kindColor(InterferenceKind kind)
{
    switch (kind) {
        case InterferenceKind::Clash:
            return {0.9F, 0.1F, 0.08F};
        case InterferenceKind::Contact:
            return {1.0F, 0.78F, 0.08F};
        case InterferenceKind::Clearance:
            return {0.0F, 0.7F, 0.85F};
        default:
            return {0.6F, 0.6F, 0.6F};
    }
}

QString kindText(InterferenceKind kind)
{
    switch (kind) {
        case InterferenceKind::Clash:
            return QObject::tr("Clash");
        case InterferenceKind::Contact:
            return QObject::tr("Contact");
        case InterferenceKind::Clearance:
            return QObject::tr("Clearance");
        default:
            return QObject::tr("Not checked");
    }
}

SoSeparator* markers(const std::vector<SbVec3f>& points, InterferenceKind kind)
{
    auto* sep = new SoSeparator();
    auto* material = new SoMaterial();
    material->diffuseColor = kindColor(kind);
    sep->addChild(material);
    auto* coords = new SoCoordinate3();
    coords->point.setValues(0, static_cast<int>(points.size()), points.data());
    sep->addChild(coords);
    auto* set = new SoMarkerSet();
    set->markerIndex = Gui::Inventor::MarkerBitmaps::getMarkerIndex("CIRCLE_FILLED", 9);
    sep->addChild(set);
    return sep;
}

SoSeparator* line(const SbVec3f& from, const SbVec3f& to, InterferenceKind kind)
{
    auto* sep = new SoSeparator();
    auto* material = new SoMaterial();
    material->diffuseColor = kindColor(kind);
    sep->addChild(material);
    auto* style = new SoDrawStyle();
    style->lineWidth = 2.0F;
    sep->addChild(style);
    auto* coords = new SoCoordinate3();
    coords->point.set1Value(0, from);
    coords->point.set1Value(1, to);
    sep->addChild(coords);
    sep->addChild(new SoLineSet());
    return sep;
}

// The shared volume of a clash, drawn see-through
SoSeparator* solid(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    const double size = box.IsVoid() ? 1.0 : std::sqrt(box.SquareExtent());
    std::vector<Base::Vector3d> points;
    std::vector<Data::ComplexGeoData::Facet> facets;
    Part::TopoShape(shape).getFaces(points, facets, std::max(size * 0.005, 1e-3));

    auto* sep = new SoSeparator();
    auto* hints = new SoShapeHints();
    hints->vertexOrdering = SoShapeHints::COUNTERCLOCKWISE;
    hints->shapeType = SoShapeHints::UNKNOWN_SHAPE_TYPE;
    sep->addChild(hints);
    auto* material = new SoMaterial();
    material->diffuseColor = kindColor(InterferenceKind::Clash);
    material->emissiveColor = SbColor(0.45F, 0.05F, 0.04F);
    material->transparency = 0.25F;
    sep->addChild(material);
    auto* coords = new SoCoordinate3();
    coords->point.setNum(static_cast<int>(points.size()));
    SbVec3f* values = coords->point.startEditing();
    for (std::size_t i = 0; i < points.size(); ++i) {
        values[i].setValue(
            static_cast<float>(points[i].x),
            static_cast<float>(points[i].y),
            static_cast<float>(points[i].z)
        );
    }
    coords->point.finishEditing();
    sep->addChild(coords);
    auto* faces = new SoIndexedFaceSet();
    faces->coordIndex.setNum(static_cast<int>(facets.size() * 4));
    int32_t* index = faces->coordIndex.startEditing();
    for (const auto& facet : facets) {
        *index++ = static_cast<int32_t>(facet.I1);
        *index++ = static_cast<int32_t>(facet.I2);
        *index++ = static_cast<int32_t>(facet.I3);
        *index++ = SO_END_FACE_INDEX;
    }
    faces->coordIndex.finishEditing();
    sep->addChild(faces);
    return sep;
}

SbVec3f toSb(const gp_Pnt& p)
{
    return {static_cast<float>(p.X()), static_cast<float>(p.Y()), static_cast<float>(p.Z())};
}

}  // namespace

TaskInterference::TaskInterference()
{
    if (App::Document* doc = App::GetApplication().getActiveDocument()) {
        docName = doc->getName();
    }

    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    auto* form = new QFormLayout();

    scope = new QComboBox();
    scope->addItem(tr("All visible parts"));
    scope->addItem(tr("Selected parts against all others"));
    scope->addItem(tr("Between the selected parts"));
    const bool selected = !Gui::Selection().getSelectionT(docName.c_str()).empty();
    scope->setCurrentIndex(selected ? SelectionAgainstRest : AllParts);
    form->addRow(tr("Check"), scope);

    clearance = new Gui::QuantitySpinBox();
    clearance->setUnit(Base::Unit::Length);
    clearance->setMinimum(0.0);
    clearance->setValue(settingsGroup()->GetFloat("Clearance", 0.0));
    clearance->setToolTip(tr("Also list parts closer than this; 0 lists clashes only"));
    form->addRow(tr("Clearance"), clearance);
    layout->addLayout(form);

    contacts = new QCheckBox(tr("List touching parts"));
    contacts->setChecked(settingsGroup()->GetBool("ListContacts", false));
    layout->addWidget(contacts);

    run = new QPushButton(tr("Check"));
    layout->addWidget(run);

    summary = new QLabel();
    summary->setWordWrap(true);
    layout->addWidget(summary);

    list = new QTreeWidget();
    list->setHeaderLabels({tr("Result"), tr("Part"), tr("Part"), tr("Amount")});
    list->setRootIsDecorated(false);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setMinimumHeight(200);
    list->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(list);

    auto* box = new Gui::TaskView::TaskBox(
        Gui::BitmapFactory().pixmap("Inspection_Interference"),
        tr("Interference"),
        true,
        nullptr
    );
    box->groupLayout()->addWidget(widget);
    Content.push_back(box);
    setAutoCloseOnDeletedDocument(true);

    connect(run, &QPushButton::clicked, this, &TaskInterference::check);
    connect(contacts, &QCheckBox::toggled, this, [this](bool on) {
        settingsGroup()->SetBool("ListContacts", on);
        fillList();
        draw();
    });
    connect(list, &QTreeWidget::itemSelectionChanged, this, &TaskInterference::showRow);
}

TaskInterference::~TaskInterference()
{
    clearDrawing();
}

bool TaskInterference::reject()
{
    clearDrawing();
    return true;
}

void TaskInterference::check()
{
    App::Document* doc = App::GetApplication().getDocument(docName.c_str());
    if (!doc) {
        return;
    }
    settingsGroup()->SetFloat("Clearance", clearance->rawValue());

    const auto picked = Gui::Selection().getSelectionT(docName.c_str(), Gui::ResolveMode::NoResolve);
    if (scope->currentIndex() != AllParts && picked.empty()) {
        summary->setText(tr("Select the parts to check first."));
        return;
    }

    Gui::WaitCursor wait;
    meshes.clear();
    parts.clear();
    report = {};
    // A broken link or placement must not escape the button's slot
    QString error;
    try {
        collectAndCheck(*doc, picked);
    }
    catch (const Base::Exception& e) {
        error = QString::fromUtf8(e.what());
    }
    catch (const Standard_Failure& e) {
        error = QString::fromUtf8(e.GetMessageString() ? e.GetMessageString() : "OCC error");
    }
    catch (const std::exception& e) {
        error = QString::fromUtf8(e.what());
    }
    if (error.isEmpty()) {
        fillList();
    }
    else {
        report = {};
        list->clear();
        summary->setText(tr("The check failed: %1").arg(error));
    }
    draw();
}

void TaskInterference::collectAndCheck(App::Document& doc, const std::vector<App::SubObjectT>& picked)
{
    std::vector<std::size_t> group;
    std::vector<std::size_t> others;
    if (scope->currentIndex() == WithinSelection) {
        parts = InterferenceCheck::collectParts(picked, &meshes);
        for (std::size_t i = 0; i < parts.size(); ++i) {
            group.push_back(i);
        }
    }
    else {
        parts = InterferenceCheck::collectParts(&doc, &meshes);
        if (scope->currentIndex() == AllParts) {
            for (std::size_t i = 0; i < parts.size(); ++i) {
                group.push_back(i);
            }
        }
        else {
            std::map<std::string, std::size_t> index;
            for (std::size_t i = 0; i < parts.size(); ++i) {
                index.emplace(parts[i].key, i);
            }
            // A picked part that is hidden is still checked against the others
            for (auto& part : InterferenceCheck::collectParts(picked, &meshes)) {
                auto found = index.find(part.key);
                if (found == index.end()) {
                    parts.push_back(std::move(part));
                    group.push_back(parts.size() - 1);
                }
                else {
                    group.push_back(found->second);
                }
            }
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (std::ranges::find(group, i) == group.end()) {
                    others.push_back(i);
                }
            }
            if (others.empty()) {
                others.push_back(parts.size());  // no other part: nothing to pair with
            }
        }
    }
    std::ranges::sort(meshes);
    meshes.erase(std::unique(meshes.begin(), meshes.end()), meshes.end());

    Inspection::InterferenceSettings settings;
    settings.clearance = clearance->rawValue();
    report = InterferenceCheck::check(parts, group, others, settings);
}

void TaskInterference::fillList()
{
    QSignalBlocker blocker(list);
    list->clear();

    int clashes = 0;
    int touching = 0;
    int close = 0;
    int unchecked = static_cast<int>(meshes.size());
    double smallestGap = 0.0;
    double largestGap = 0.0;
    double smallestVolume = 0.0;
    double largestVolume = 0.0;
    auto widen = [](double& smallest, double& largest, double value) {
        smallest = (largest == 0.0) ? value : std::min(smallest, value);
        largest = std::max(largest, value);
    };
    for (const auto& result : report.results) {
        if (result.kind == InterferenceKind::Clearance) {
            widen(smallestGap, largestGap, result.distance);
        }
        else if (result.kind == InterferenceKind::Clash) {
            widen(smallestVolume, largestVolume, result.volume);
        }
    }
    const ColumnUnit gapUnit(smallestGap, largestGap, Base::Unit::Length);
    const ColumnUnit volumeUnit(smallestVolume, largestVolume, Base::Unit::Volume);
    for (std::size_t i = 0; i < report.results.size(); ++i) {
        const auto& result = report.results[i];
        switch (result.kind) {
            case InterferenceKind::Clash:
                ++clashes;
                break;
            case InterferenceKind::Contact:
                ++touching;
                break;
            case InterferenceKind::Clearance:
                ++close;
                break;
            default:
                ++unchecked;
                break;
        }
        if (result.kind == InterferenceKind::Contact && !contacts->isChecked()) {
            continue;
        }

        auto* item = new QTreeWidgetItem(list);
        item->setData(0, Qt::UserRole, static_cast<int>(i));
        item->setText(0, kindText(result.kind));
        const SbColor color = kindColor(result.kind);
        item->setForeground(0, QColor::fromRgbF(color[0] * 0.8F, color[1] * 0.8F, color[2] * 0.8F));
        item->setText(1, QString::fromStdString(parts[result.first].name));
        item->setText(2, QString::fromStdString(parts[result.second].name));
        switch (result.kind) {
            case InterferenceKind::Clash:
                item->setText(3, volumeUnit.format(result.volume));
                break;
            case InterferenceKind::Clearance:
                item->setText(3, gapUnit.format(result.distance));
                break;
            case InterferenceKind::Contact:
                break;
            default:
                item->setText(3, QString::fromStdString(result.reason));
                break;
        }
    }
    for (const auto& mesh : meshes) {
        auto* item = new QTreeWidgetItem(list);
        item->setData(0, Qt::UserRole, -1);
        item->setText(0, kindText(InterferenceKind::Unchecked));
        item->setText(1, QString::fromStdString(mesh));
        item->setText(3, tr("Mesh: not checked"));
    }

    if (clashes + touching + close + unchecked == 0) {
        summary->setText(tr("No interference among %n part(s).", "", static_cast<int>(parts.size())));
        return;
    }
    QString text = tr("%1 clash(es), %2 touching, %3 closer than the clearance.")
                       .arg(clashes)
                       .arg(touching)
                       .arg(close);
    text += QLatin1Char('\n')
        + tr("%1 of %2 pairs measured; the others are apart by their bounding boxes.")
              .arg(report.checked)
              .arg(report.pairs);
    if (unchecked > 0) {
        text += QLatin1Char('\n') + tr("%n not checked.", "", unchecked);
    }
    summary->setText(text);
}

void TaskInterference::draw()
{
    clearDrawing();
    Gui::Document* guiDoc = Gui::Application::Instance->getDocument(docName.c_str());
    if (!guiDoc || report.results.empty()) {
        return;
    }

    drawing = new SoSeparator();
    drawing->ref();
    auto* pick = new SoPickStyle();
    pick->style = SoPickStyle::UNPICKABLE;
    drawing->addChild(pick);
    // In front of the parts, as the shared volumes are inside them
    auto* front = new SoAnnotation();
    drawing->addChild(front);

    std::vector<SbVec3f> touching;
    for (const auto& result : report.results) {
        switch (result.kind) {
            case InterferenceKind::Clash:
                if (!result.shared.IsNull()) {
                    front->addChild(solid(result.shared));
                }
                break;
            case InterferenceKind::Contact:
                if (contacts->isChecked()) {
                    touching.push_back(toSb(result.point1));
                }
                break;
            case InterferenceKind::Clearance:
                front->addChild(line(toSb(result.point1), toSb(result.point2), result.kind));
                front->addChild(markers({toSb(result.point1), toSb(result.point2)}, result.kind));
                break;
            default:
                break;
        }
    }
    if (!touching.empty()) {
        front->addChild(markers(touching, InterferenceKind::Contact));
    }

    for (auto* view : guiDoc->getMDIViewsOfType(Gui::View3DInventor::getClassTypeId())) {
        auto* view3d = static_cast<Gui::View3DInventor*>(view);
        static_cast<SoGroup*>(view3d->getViewer()->getSceneGraph())->addChild(drawing);
        views.emplace_back(view3d);
    }
}

void TaskInterference::clearDrawing()
{
    if (!drawing) {
        return;
    }
    for (const auto& view : views) {
        if (view) {
            auto* root = static_cast<SoGroup*>(view->getViewer()->getSceneGraph());
            if (root->findChild(drawing) >= 0) {
                root->removeChild(drawing);
            }
        }
    }
    views.clear();
    drawing->unref();
    drawing = nullptr;
}

void TaskInterference::showRow()
{
    const auto selected = list->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    const int row = selected.front()->data(0, Qt::UserRole).toInt();
    if (row < 0 || static_cast<std::size_t>(row) >= report.results.size()) {
        return;
    }
    // Select the two parts and fit them in the view
    const auto& result = report.results[row];
    Gui::Selection().clearSelection();
    for (std::size_t i : {result.first, result.second}) {
        const auto& object = parts[i].object;
        Gui::Selection().addSelection(
            docName.c_str(),
            object.getObjectName().c_str(),
            object.getSubNameNoElement().c_str()
        );
    }
    Gui::Application::Instance->commandManager().runCommandByName("Std_ViewSelection");
}

#include "moc_TaskInterference.cpp"
