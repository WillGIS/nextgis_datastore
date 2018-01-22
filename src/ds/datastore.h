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
              const CPLString & name = "",
              const CPLString & path = "");
    virtual ~DataStore();

    // static
public:
    static bool create(const char* path);
    static const char* extension();

    // Dataset interface
public:
    virtual bool open(unsigned int openFlags,
                      const Options &options = Options()) override;
    virtual void startBatchOperation() override { enableJournal(false); }
    virtual void stopBatchOperation() override { enableJournal(true); }
    virtual bool isBatchOperation() const override {
        return m_disableJournalCounter > 0;
    }

    virtual FeatureClass* createFeatureClass(const CPLString& name,
                                             enum ngsCatalogObjectType objectType,
                                             OGRFeatureDefn * const definition,
                                             OGRSpatialReference* spatialRef,
                                             OGRwkbGeometryType type,
                                             const Options& options = Options(),
                                             const Progress& progress = Progress()) override;
    virtual Table* createTable(const CPLString& name,
                               enum ngsCatalogObjectType objectType,
                               OGRFeatureDefn * const definition,
                               const Options& options = Options(),
                               const Progress& progress = Progress()) override;


    virtual bool setProperty(const char* key, const char* value) override;
    virtual CPLString property(const char* key, const char* defaultValue) const override;
    virtual std::map<CPLString, CPLString> properties(
            const char* table, const char* domain) const override;
    // Dataset interface
protected:
    virtual OGRLayer* createAttachmentsTable(const char* name) override;
    virtual OGRLayer* createEditHistoryTable(const char* name) override;

    // ObjectContainer interface
public:
    virtual bool canCreate(const enum ngsCatalogObjectType type) const override;
    virtual bool create(const enum ngsCatalogObjectType type, const CPLString& name,
                        const Options& options) override;

protected:
    virtual bool isNameValid(const char* name) const override;
    virtual CPLString normalizeFieldName(const CPLString& name) const override;
    virtual void fillFeatureClasses() override;

protected:
    void enableJournal(bool enable);
    bool upgrade(int oldVersion);

protected:
    unsigned char m_disableJournalCounter;

};

} // namespace ngs

#endif // NGSDATASTORE_H
