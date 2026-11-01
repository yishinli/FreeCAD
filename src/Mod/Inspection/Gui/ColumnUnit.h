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

#include <algorithm>
#include <cmath>
#include <string>

#include <QLocale>
#include <QString>

#include <Base/Quantity.h>
#include <Base/Unit.h>
#include <Base/UnitsApi.h>

namespace InspectionGui
{

/// One unit and one number of decimals for every value of a kind in the list, so the column
/// does not mix mm and µm (or inch and mil): the unit the schema in Preferences picks for the
/// largest value, and as many decimals as Preferences say, or more to give the smallest value
/// two significant digits
class ColumnUnit
{
public:
    ColumnUnit(double smallest, double largest, const Base::Unit& unit)
    {
        const int preferred = Base::UnitsApi::getDecimals();
        const int most = std::max(preferred, 6);
        pick(largest, unit);
        decimals = std::max(preferred, needed(smallest));
        if (decimals > most) {
            // Values so far apart that the smallest would show as zeros: its unit instead, the
            // largest then has more integer digits
            pick(smallest, unit);
            decimals = std::clamp(needed(smallest), preferred, most);
        }
    }

    /// Volumes in the cube of a length column's unit, as mm³ beside mm
    static ColumnUnit cubed(const ColumnUnit& length, double smallest)
    {
        ColumnUnit unit(length);
        unit.factor = length.factor * length.factor * length.factor;
        unit.name = length.name + QChar(0x00B3);
        const int preferred = Base::UnitsApi::getDecimals();
        unit.decimals = std::clamp(unit.needed(smallest), preferred, std::max(preferred, 6));
        return unit;
    }

    QString format(double value) const
    {
        return number(value) + QLatin1Char(' ') + name;
    }

    /// The value in the column's unit, without the unit
    QString number(double value) const
    {
        return QLocale().toString(value / factor, 'f', decimals);
    }

    const QString& unitName() const
    {
        return name;
    }

private:
    void pick(double value, const Base::Unit& unit)
    {
        std::string unitName;
        Base::Quantity(value, unit).getUserString(factor, unitName);
        if (factor <= 0.0) {
            factor = 1.0;
        }
        name = QString::fromStdString(unitName);
    }

    /// Decimals giving value two significant digits in the unit picked
    int needed(double value) const
    {
        return value > 0.0 ? static_cast<int>(std::ceil(-std::log10(value / factor))) + 1 : 0;
    }

    double factor = 1.0;
    QString name;
    int decimals = 2;
};

}  // namespace InspectionGui
