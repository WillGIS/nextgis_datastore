/******************************************************************************
 * Project:  libngstore
 * Purpose:  NextGIS store and visualisation support library
 * Author: Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
 ******************************************************************************
 *   Copyright (c) 2016 NextGIS, <info@nextgis.com>
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
#ifndef DATASETCONTAINER_H
#define DATASETCONTAINER_H

#include "dataset.h"

namespace ngs {

void LoadingThread(void * store);

typedef struct _loadData {
    DatasetPtr ds;
    // ? CPLString subDatasetName;
    ngsProgressFunc progressFunc;
    void *progressArguments;
} LoadData;

class DatasetContainer : public Dataset
{
    friend void LoadingThread(void * store);
public:
    DatasetContainer();
    virtual ~DatasetContainer();
    virtual int datasetCount() const;
    virtual int rasterCount() const;
    virtual DatasetWPtr getDataset(const CPLString& name);
    virtual DatasetWPtr getDataset(int index);
    int loadDataset(const CPLString& path, const CPLString& subDatasetName,
                            bool move, ngsProgressFunc progressFunc,
                            void* progressArguments = nullptr);
protected:
    bool isNameValid(const CPLString& name) const;

protected:
    map<string, DatasetPtr> m_datasets;
    /**
     * Load Dataset
     */
    CPLJoinableThread* m_hLoadThread;
    bool m_cancelLoad;
    vector<LoadData> m_loadData;
};

}

#endif // DATASETCONTAINER_H
