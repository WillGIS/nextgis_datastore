/******************************************************************************
 * Project: libngstore
 * Purpose: NextGIS store and visualization support library
 * Author:  Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
 ******************************************************************************
 *   Copyright (c) 2016-2017 NextGIS, <info@nextgis.com>
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
#ifndef NGSFILE_H
#define NGSFILE_H

#include "objectcontainer.h"

namespace ngs {

class File : public Object
{
public:
    File(ObjectContainer * const parent = nullptr,
         const ngsCatalogObjectType type = ngsCatalogObjectType::CAT_FILE_ANY,
         const CPLString & name = "",
         const CPLString & path = "");

public:
    static bool deleteFile(const char* path);

    // Object interface
public:
    virtual bool destroy() override;
    virtual bool canDestroy() const override;
};

}

#endif // NGSFILE_H
