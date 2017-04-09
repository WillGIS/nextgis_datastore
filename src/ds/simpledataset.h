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
#ifndef NGSSIMPLEDATASET_H
#define NGSSIMPLEDATASET_H

#include "dataset.h"

namespace ngs {

class SimpleDataset : public Dataset
{
public:
    SimpleDataset(ObjectContainer * const parent = nullptr,
                  const CPLString & name = "",
                  const CPLString & path = "");
    ObjectPtr getInternalObject() const;


    // ObjectContainer interface
public:
    virtual bool hasChildren() override;
    virtual bool canCreate(const ngsCatalogObjectType) const override { return false; }
    virtual bool canPaste(const ngsCatalogObjectType) override { return false; }
};

}

#endif // NGSSIMPLEDATASET_H
