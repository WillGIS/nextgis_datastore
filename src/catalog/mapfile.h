/******************************************************************************
 * Project: libngstore
 * Purpose: NextGIS store and visualization support library
 * Author:  Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
 ******************************************************************************
 *   Copyright (c) 2016-2018 NextGIS, <info@nextgis.com>
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/
#ifndef NGSMAPFILE_H
#define NGSMAPFILE_H

#include "file.h"
#include "map/mapview.h"

namespace ngs {

class MapFile : public File
{
public:
    explicit MapFile(ObjectContainer * const parent = nullptr,
                     const std::string &name = "",
                     const std::string &path = "");
    MapViewPtr map() const;
    bool open();
    bool save(MapViewPtr mapView);

    // static
public:
    static std::string extension();

    // Object interface
public:
    virtual bool destroy() override;

protected:
    MapViewPtr m_mapView;
};

}

#endif // NGSMAPFILE_H
