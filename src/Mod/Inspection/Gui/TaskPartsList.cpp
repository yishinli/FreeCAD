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

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <Standard_Failure.hxx>

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Stream.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/FileDialog.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection/Selection.h>
#include <Gui/TaskView/TaskView.h>
#include <Gui/WaitCursor.h>

#include "ColumnUnit.h"
#include "TaskPartsList.h"

using namespace InspectionGui;
using Inspection::InterferenceCheck;
using Inspection::PartsList;

namespace
{

ParameterGrp::handle settingsGroup()
{
    return App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Inspection/PartsList"
    );
}

enum Column
{
    IndexColumn,
    NameColumn,
    QuantityColumn,
    SizeColumn,
    VolumeColumn
};

// Item rows hold their item's index; piece rows hold -1 - the part's index
constexpr int RowRole = Qt::UserRole;

}  // namespace

TaskPartsList::TaskPartsList()
{
    if (App::Document* doc = App::GetApplication().getActiveDocument()) {
        docName = doc->getName();
    }

    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    auto* form = new QFormLayout();

    scope = new QComboBox();
    scope->addItem(tr("All parts"));
    scope->addItem(tr("Selected parts"));
    const bool selected = !Gui::Selection().getSelectionT(docName.c_str()).empty();
    scope->setCurrentIndex(selected ? SelectedParts : AllParts);
    form->addRow(tr("List"), scope);
    layout->addLayout(form);

    hidden = new QCheckBox(tr("Include hidden parts"));
    hidden->setChecked(settingsGroup()->GetBool("IncludeHidden", true));
    layout->addWidget(hidden);

    summary = new QLabel();
    summary->setWordWrap(true);
    layout->addWidget(summary);

    list = new QTreeWidget();
    list->setHeaderLabels({tr("Index"), tr("Name"), tr("Quantity"), tr("Size"), tr("Volume")});
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setMinimumHeight(250);
    list->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(list);

    auto* buttons = new QHBoxLayout();
    copies = new QPushButton(tr("Find Copies"));
    copies->setToolTip(
        tr("Goes to the line of the part selected in the 3D view, and selects all its copies")
    );
    save = new QPushButton(tr("Save as CSV..."));
    buttons->addWidget(copies);
    buttons->addWidget(save);
    layout->addLayout(buttons);

    auto* box = new Gui::TaskView::TaskBox(
        Gui::BitmapFactory().pixmap("Inspection_PartsList"),
        tr("Parts List"),
        true,
        nullptr
    );
    box->groupLayout()->addWidget(widget);
    Content.push_back(box);
    setAutoCloseOnDeletedDocument(true);

    connect(scope, qOverload<int>(&QComboBox::currentIndexChanged), this, &TaskPartsList::refresh);
    connect(hidden, &QCheckBox::toggled, this, [this](bool on) {
        settingsGroup()->SetBool("IncludeHidden", on);
        refresh();
    });
    connect(list, &QTreeWidget::itemSelectionChanged, this, &TaskPartsList::showRow);
    connect(copies, &QPushButton::clicked, this, &TaskPartsList::findCopies);
    connect(save, &QPushButton::clicked, this, &TaskPartsList::exportCsv);

    refresh();
}

void TaskPartsList::refresh()
{
    App::Document* doc = App::GetApplication().getDocument(docName.c_str());
    if (!doc) {
        return;
    }
    const bool visibleOnly = !hidden->isChecked();
    const auto picked = Gui::Selection().getSelectionT(docName.c_str(), Gui::ResolveMode::NoResolve);
    if (scope->currentIndex() == SelectedParts && picked.empty()) {
        parts.clear();
        items.clear();
        list->clear();
        summary->setText(tr("Select the parts to list first."));
        return;
    }

    Gui::WaitCursor wait;
    meshes.clear();
    QString error;
    // A broken link or placement must not escape the slot
    try {
        parts = scope->currentIndex() == SelectedParts
            ? InterferenceCheck::collectParts(picked, &meshes, visibleOnly)
            : InterferenceCheck::collectParts(doc, &meshes, visibleOnly);
        items = PartsList::group(parts);
    }
    catch (const Base::AbortException&) {
        error = tr("stopped");
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
    if (!error.isEmpty()) {
        parts.clear();
        items.clear();
        list->clear();
        summary->setText(tr("The list failed: %1").arg(error));
        return;
    }
    fillList();
}

void TaskPartsList::fillList()
{
    QSignalBlocker blocker(list);
    list->clear();

    double smallestSize = 0.0;
    double largestSize = 0.0;
    double smallestVolume = 0.0;
    double largestVolume = 0.0;
    for (const auto& item : items) {
        for (int i = 0; i < 3; ++i) {
            const double size = item.size.Coord(i + 1);
            if (size > 0.0) {
                smallestSize = largestSize == 0.0 ? size : std::min(smallestSize, size);
                largestSize = std::max(largestSize, size);
            }
        }
        if (item.volume > 0.0) {
            smallestVolume = largestVolume == 0.0 ? item.volume
                                                  : std::min(smallestVolume, item.volume);
            largestVolume = std::max(largestVolume, item.volume);
        }
    }
    const ColumnUnit sizeUnit(smallestSize, largestSize, Base::Unit::Length);
    const ColumnUnit volumeUnit = ColumnUnit::cubed(sizeUnit, smallestVolume);

    for (std::size_t k = 0; k < items.size(); ++k) {
        const auto& item = items[k];
        auto* row = new QTreeWidgetItem(list);
        row->setData(IndexColumn, RowRole, static_cast<int>(k));
        row->setText(IndexColumn, QString::number(k + 1));
        row->setText(NameColumn, QString::fromStdString(item.name));
        row->setToolTip(NameColumn, QString::fromStdString(item.description));
        row->setText(QuantityColumn, QString::number(item.instances.size()));
        row->setTextAlignment(QuantityColumn, Qt::AlignRight | Qt::AlignVCenter);
        row->setText(
            SizeColumn,
            QStringLiteral("%1 × %2 × %3 %4")
                .arg(
                    sizeUnit.number(item.size.X()),
                    sizeUnit.number(item.size.Y()),
                    sizeUnit.number(item.size.Z()),
                    sizeUnit.unitName()
                )
        );
        row->setText(VolumeColumn, volumeUnit.format(item.volume));
        for (std::size_t i : item.instances) {
            auto* piece = new QTreeWidgetItem(row);
            piece->setData(IndexColumn, RowRole, -1 - static_cast<int>(i));
            piece->setText(NameColumn, QString::fromStdString(parts[i].name));
        }
    }

    std::size_t pieces = 0;
    for (const auto& item : items) {
        pieces += item.instances.size();
    }
    QString text = tr("%n part(s)", "", static_cast<int>(pieces)) + QStringLiteral(", ")
        + tr("%n different", "", static_cast<int>(items.size())) + QStringLiteral(".");
    if (!meshes.empty()) {
        text += QLatin1Char(' ')
            + tr("%n mesh object(s) not listed.", "", static_cast<int>(meshes.size()));
    }
    if (std::ranges::any_of(items, [](const auto& item) { return item.mirrored; })) {
        text += QLatin1Char(' ') + tr("Mirror images are listed apart.");
    }
    summary->setText(text);
    save->setEnabled(!items.empty());
}

void TaskPartsList::showRow()
{
    const auto selected = list->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    const int row = selected.front()->data(IndexColumn, RowRole).toInt();
    std::vector<std::size_t> pieces;
    if (row >= 0 && static_cast<std::size_t>(row) < items.size()) {
        pieces = items[row].instances;
    }
    else if (row < 0 && static_cast<std::size_t>(-1 - row) < parts.size()) {
        pieces.push_back(static_cast<std::size_t>(-1 - row));
    }
    if (pieces.empty()) {
        return;
    }
    // Select the pieces and fit them in the view
    Gui::Selection().clearSelection();
    for (std::size_t i : pieces) {
        const auto& object = parts[i].object;
        Gui::Selection().addSelection(
            docName.c_str(),
            object.getObjectName().c_str(),
            object.getSubNameNoElement().c_str()
        );
    }
    Gui::Application::Instance->commandManager().runCommandByName("Std_ViewSelection");
}

void TaskPartsList::findCopies()
{
    const auto picked = Gui::Selection().getSelectionT(docName.c_str(), Gui::ResolveMode::NoResolve);
    if (picked.empty()) {
        summary->setText(tr("Select a part in the 3D view first."));
        return;
    }
    std::vector<Inspection::InterferencePart> found;
    try {
        found = InterferenceCheck::collectParts({picked.front()}, nullptr, false);
    }
    catch (const Base::Exception&) {
    }
    if (found.empty()) {
        summary->setText(tr("The selection is not a solid part."));
        return;
    }
    std::map<std::string, std::size_t> itemOf;
    for (std::size_t k = 0; k < items.size(); ++k) {
        for (std::size_t i : items[k].instances) {
            itemOf.emplace(parts[i].key, k);
        }
    }
    auto it = itemOf.find(found.front().key);
    if (it == itemOf.end()) {
        summary->setText(tr("The selected part is not in this list."));
        return;
    }
    QTreeWidgetItem* row = list->topLevelItem(static_cast<int>(it->second));
    list->scrollToItem(row);
    list->setCurrentItem(row);  // selects all its pieces
}

void TaskPartsList::exportCsv()
{
    const QString name = Gui::FileDialog::getSaveFileName(
        Gui::getMainWindow(),
        tr("Save Parts List"),
        QString(),
        Gui::FileDialog::FilterList {{tr("CSV files"), {QStringLiteral("*.csv")}}}
    );
    if (name.isEmpty()) {
        return;
    }
    Base::FileInfo file(name.toUtf8().toStdString());
    Base::ofstream out(file, std::ios::out | std::ios::binary);
    out << PartsList::csv(items, parts);
    out.close();
    summary->setText(out ? tr("Saved to %1.").arg(name) : tr("Could not save %1.").arg(name));
}

#include "moc_TaskPartsList.cpp"
