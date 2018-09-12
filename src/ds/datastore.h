/******************************************************************************
 * Project:  libngstore
 * Purpose:  NextGIS store and visualisation support library
 * Author: Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
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
#ifndef NGSDATASTORE_H
#define NGSDATASTORE_H

#include "dataset.h"

namespace ngs {


constexpr const char* REMOTE_ID_KEY = "rid";
constexpr const char* ATTACHMENT_REMOTE_ID_KEY = "arid";

/**
 * @brief The geodata storage and manipulation class for raster, vector geodata
 * and attachments
 */
class DataStore : public Dataset, public SpatialDataset
{
public:
    explicit DataStore(ObjectContainer * const parent = nullptr,
              const std::string &name = "",
              const std::string &path = "");
    virtual ~DataStore() override;

    // static
public:
    static bool create(const std::string &path);
    static std::string extension();

    // Dataset interface
public:
    virtual bool open(unsigned int openFlags = GDAL_OF_SHARED|GDAL_OF_UPDATE|GDAL_OF_VERBOSE_ERROR,
                      const Options &options = Options()) override;
    virtual void startBatchOperation() override { enableJournal(false); }
    virtual void stopBatchOperation() override { enableJournal(true); }
    virtual bool isBatchOperation() const override;

    virtual FeatureClass *createFeatureClass(const std::string &name,
                                             enum ngsCatalogObjectType objectType,
                                             OGRFeatureDefn * const definition,
                                             OGRSpatialReference *spatialRef,
                                             OGRwkbGeometryType type,
                                             const Options &options = Options(),
                                             const Progress &progress = Progress()) override;
    virtual Table *createTable(const std::string& name,
                               enum ngsCatalogObjectType objectType,
                               OGRFeatureDefn * const definition,
                               const Options &options = Options(),
                               const Progress &progress = Progress()) override;

    // Dataset interface
protected:
    virtual OGRLayer *createAttachmentsTable(const std::string &name) override;
    virtual OGRLayer *createEditHistoryTable(const std::string &name) override;

    // Object interface
public:
    virtual Properties properties(const std::string &domain = NG_ADDITIONS_KEY) const override;
    virtual std::string property(const std::string &key,
                                 const std::string &defaultValue,
                                 const std::string &domain = NG_ADDITIONS_KEY) const override;
    virtual bool setProperty(const std::string &key, const std::string &value,
                             const std::string &domain = NG_ADDITIONS_KEY) override;

    // ObjectContainer interface
public:
    virtual bool canCreate(const enum ngsCatalogObjectType type) const override;
    virtual bool create(const enum ngsCatalogObjectType type,
                        const std::string& name,
                        const Options &options) override;

protected:
    virtual bool isNameValid(const std::string &name) const override;
    virtual std::string normalizeFieldName(const std::string &name) const override;
    virtual void fillFeatureClasses() const override;

protected:
    void enableJournal(bool enable);
    bool upgrade(int oldVersion);

protected:
    unsigned char m_disableJournalCounter;

};

} // namespace ngs

#endif // NGSDATASTORE_H
