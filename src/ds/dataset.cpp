/******************************************************************************
 * Project:  libngstore
 * Purpose:  NextGIS store and visualization support library
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

#include "dataset.h"

// stl
#include <algorithm>
#include <array>

#include "featureclass.h"
#include "raster.h"
#include "simpledataset.h"
#include "table.h"
#include "catalog/file.h"
#include "catalog/folder.h"
#include "ngstore/api.h"
#include "ngstore/catalog/filter.h"
#include "ngstore/version.h"
#include "util/error.h"
#include "util/notify.h"
#include "util/stringutil.h"

namespace ngs {

constexpr std::array<char, 22> forbiddenChars = {{':', '@', '#', '%', '^', '&', '*',
    '!', '$', '(', ')', '+', '-', '?', '=', '/', '\\', '"', '\'', '[', ']', ','}};

constexpr std::array<const char*, 124> forbiddenSQLFieldNames {{ "ABORT", "ACTION",
    "ADD", "AFTER", "ALL", "ALTER", "ANALYZE", "AND", "AS", "ASC", "ATTACH",
    "AUTOINCREMENT", "BEFORE", "BEGIN", "BETWEEN", "BY", "CASCADE", "CASE",
    "CAST", "CHECK", "COLLATE", "COLUMN", "COMMIT", "CONFLICT", "CONSTRAINT",
    "CREATE", "CROSS", "CURRENT_DATE", "CURRENT_TIME", "CURRENT_TIMESTAMP",
    "DATABASE", "DEFAULT", "DEFERRABLE", "DEFERRED", "DELETE", "DESC", "DETACH",
    "DISTINCT", "DROP", "EACH", "ELSE", "END", "ESCAPE", "EXCEPT", "EXCLUSIVE",
    "EXISTS", "EXPLAIN", "FAIL", "FOR", "FOREIGN", "FROM", "FULL", "GLOB",
    "GROUP", "HAVING", "IF", "IGNORE", "IMMEDIATE", "IN", "INDEX", "INDEXED",
    "INITIALLY", "INNER", "INSERT", "INSTEAD", "INTERSECT", "INTO", "IS", "ISNULL",
    "JOIN", "KEY", "LEFT", "LIKE", "LIMIT", "MATCH", "NATURAL", "NO", "NOT",
    "NOTNULL", "NULL", "OF", "OFFSET", "ON", "OR", "ORDER", "OUTER", "PLAN",
    "PRAGMA", "PRIMARY", "QUERY", "RAISE", "RECURSIVE", "REFERENCES", "REGEXP",
    "REINDEX", "RELEASE", "RENAME", "REPLACE", "RESTRICT", "RIGHT", "ROLLBACK",
    "ROW", "SAVEPOINT", "SELECT", "SET", "TABLE", "TEMP", "TEMPORARY", "THEN",
    "TO", "TRANSACTION", "TRIGGER", "UNION", "UNIQUE", "UPDATE", "USING",
    "VACUUM", "VALUES", "VIEW", "VIRTUAL", "WHEN", "WHERE", "WITH", "WITHOUT"}};

constexpr unsigned short MAX_EQUAL_NAMES = 10000;

//------------------------------------------------------------------------------
// GDALDatasetPtr
//------------------------------------------------------------------------------

GDALDatasetPtr::GDALDatasetPtr(GDALDataset *ds) :
    shared_ptr(ds, GDALClose)
{
}

GDALDatasetPtr::GDALDatasetPtr() :
    shared_ptr(nullptr, GDALClose)
{

}

GDALDatasetPtr::GDALDatasetPtr(const GDALDatasetPtr &ds) : shared_ptr(ds)
{
}

GDALDatasetPtr& GDALDatasetPtr::operator=(GDALDataset* ds) {
    reset(ds);
    return *this;
}

GDALDatasetPtr::operator GDALDataset *() const
{
    return get();
}

//------------------------------------------------------------------------------
// DatasetBase
//------------------------------------------------------------------------------
DatasetBase::DatasetBase() :
    m_DS(nullptr)
{

}

DatasetBase::~DatasetBase()
{
    close();
}

void DatasetBase::close()
{
    GDALClose(m_DS);
    m_DS = nullptr;
}

const char* DatasetBase::options(const enum ngsCatalogObjectType type,
                                    ngsOptionType optionType) const
{
    GDALDriver* poDriver = Filter::getGDALDriver(type);
    switch (optionType) {
    case OT_CREATE_DATASOURCE:
        if(nullptr == poDriver)
            return "";
        return poDriver->GetMetadataItem(GDAL_DMD_CREATIONOPTIONLIST);
    case OT_CREATE_LAYER:
        if(nullptr == poDriver)
            return "";
        return poDriver->GetMetadataItem(GDAL_DS_LAYER_CREATIONOPTIONLIST);
    case OT_CREATE_LAYER_FIELD:
        if(nullptr == poDriver)
            return "";
        return poDriver->GetMetadataItem(GDAL_DMD_CREATIONFIELDDATATYPES);
    case OT_CREATE_RASTER:
        if(nullptr == poDriver)
            return "";
        return poDriver->GetMetadataItem(GDAL_DMD_CREATIONDATATYPES);
    case OT_OPEN:
        if(nullptr == poDriver)
            return "";
        return poDriver->GetMetadataItem(GDAL_DMD_OPENOPTIONLIST);
    case OT_LOAD:
        return "";
    }
    return "";
}

bool DatasetBase::open(const char* path, unsigned int openFlags,
                       const Options &options)
{
    if(nullptr == path || EQUAL(path, "")) {
        return errorMessage(_("The path is empty"));
    }

    // NOTE: VALIDATE_OPEN_OPTIONS can be set to NO to avoid warnings

    CPLErrorReset();
    auto openOptions = options.getOptions();
    m_DS = static_cast<GDALDataset*>(GDALOpenEx( path, openFlags, nullptr,
                                                 openOptions.get(), nullptr));

    if(m_DS == nullptr) {
        errorMessage(CPLGetLastErrorMsg());
        if(openFlags & GDAL_OF_UPDATE) {
            // Try to open read-only
            openFlags &= static_cast<unsigned int>(~GDAL_OF_UPDATE);
            openFlags |= GDAL_OF_READONLY;
            m_DS = static_cast<GDALDataset*>(GDALOpenEx( path, openFlags,
                                          nullptr, openOptions.get(), nullptr));
            if(nullptr == m_DS) {
                errorMessage(CPLGetLastErrorMsg());
                return false; // Error message comes from GDALOpenEx
            }            
        }
        else {
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------------
// Dataset
//------------------------------------------------------------------------------
constexpr const char* ADDS_EXT = "ngadds";

// Metadata
constexpr const char* META_KEY = "key";
constexpr unsigned short META_KEY_LIMIT = 128;
constexpr const char* META_VALUE = "value";
constexpr unsigned short META_VALUE_LIMIT = 512;

// Attachments
constexpr const char* ATTACH_SUFFIX = "attachments";

// History
constexpr const char* HISTORY_SUFFIX = "editlog";

// Overviews
constexpr const char* OVR_SUFFIX = "overviews";

constexpr const char* METHADATA_TABLE_NAME = "nga_meta";

constexpr const char* NG_PREFIX = "nga_";
constexpr int NG_PREFIX_LEN = length(NG_PREFIX);

Dataset::Dataset(ObjectContainer * const parent,
                 const enum ngsCatalogObjectType type,
                 const CPLString &name,
                 const CPLString &path) :
    ObjectContainer(parent, type, name, path),
    DatasetBase(),
    m_addsDS(nullptr),
    m_metadata(nullptr),
    m_executeSQLMutex(CPLCreateMutex())
{
    CPLReleaseMutex(m_executeSQLMutex);
}

Dataset::~Dataset()
{
    CPLDestroyMutex(m_executeSQLMutex);
    GDALClose(m_addsDS);
    m_addsDS = nullptr;
}

FeatureClass* Dataset::createFeatureClass(const CPLString& name,
                                          enum ngsCatalogObjectType objectType,
                                          OGRFeatureDefn * const definition,
                                          OGRSpatialReference* spatialRef,
                                          OGRwkbGeometryType type,
                                          const Options& options,
                                          const Progress& progress)
{
    if(nullptr == m_DS) {
        errorMessage(_("Not opened"));
        return nullptr;
    }

    OGRLayer* layer = m_DS->CreateLayer(name, spatialRef, type,
                                        options.getOptions().get());

    if(layer == nullptr) {
        errorMessage(CPLGetLastErrorMsg());
        return nullptr;
    }

    for (int i = 0; i < definition->GetFieldCount(); ++i) {
        OGRFieldDefn* srcField = definition->GetFieldDefn(i);
        OGRFieldDefn dstField(srcField);

        CPLString newFieldName = normalizeFieldName(srcField->GetNameRef());
        if(!EQUAL(newFieldName, srcField->GetNameRef())) {
            progress.onProgress(COD_WARNING, 0.0,
                                _("Field %s of source table was renamed to %s in destination table"),
                                srcField->GetNameRef(), newFieldName.c_str());
        }

        dstField.SetName(newFieldName);
        if (layer->CreateField(&dstField) != OGRERR_NONE) {
            errorMessage(CPLGetLastErrorMsg());
            return nullptr;
        }
    }

    FeatureClass* out = new FeatureClass(layer, this, objectType, name);

    if(options.boolOption("CREATE_OVERVIEWS", false) &&
            !options.stringOption("ZOOM_LEVELS", "").empty()) {
        out->createOverviews(progress, options);
    }

    if(m_parent) {
        m_parent->notifyChanges();
    }

    Notify::instance().onNotify(out->fullName(), ngsChangeCode::CC_CREATE_OBJECT);

    return out;
}

Table* Dataset::createTable(const CPLString &name,
                            enum ngsCatalogObjectType objectType,
                            OGRFeatureDefn * const definition,
                            const Options &options,
                            const Progress &progress)
{
    return static_cast<Table*>(createFeatureClass(name, objectType, definition,
                                                  nullptr, wkbNone, options,
                                                  progress));
}

bool Dataset::setProperty(const char* key, const char* value)
{
    CPLMutexHolder holder(m_executeSQLMutex);

    if(!m_addsDS) {
        createAdditionsDataset();
    }    

    if(!m_metadata) {
        m_metadata = createMetadataTable(m_addsDS);
    }
    if(!m_metadata) {
        return false;
    }

    FeaturePtr feature(OGRFeature::CreateFeature(m_metadata->GetLayerDefn()));

    feature->SetField(META_KEY, key);
    feature->SetField(META_VALUE, value);
    return m_metadata->CreateFeature(feature) != OGRERR_NONE;
}

CPLString Dataset::property(const char* key, const char* defaultValue) const
{
    if(!m_metadata)
        return defaultValue;

    CPLMutexHolder holder(m_executeSQLMutex);

    m_metadata->SetAttributeFilter(CPLSPrintf("%s LIKE \"%s\"", META_KEY, key));
    OGRFeature* feature = m_metadata->GetNextFeature();
    CPLString out;
    if(feature) {
        out = feature->GetFieldAsString(1);
        OGRFeature::DestroyFeature(feature);
    }

    m_metadata->SetAttributeFilter(nullptr);
    return out;
}

std::map<CPLString, CPLString> Dataset::properties(const char* table,
                                                      const char* domain) const
{
    std::map<CPLString, CPLString> out;
    if(nullptr == m_metadata || nullptr == table) {
        return out;
    }

    CPLString name = table;
    if(nullptr != domain) {
        name += CPLString(".") + domain;
    }

    CPLMutexHolder holder(m_executeSQLMutex);

    m_metadata->SetAttributeFilter(CPLSPrintf("%s LIKE \"%s.%%\"", META_KEY, name.c_str()));
    FeaturePtr feature;
    while((feature = m_metadata->GetNextFeature())) {
        CPLString key = feature->GetFieldAsString(0) + CPLStrnlen(table, 255) + 1;
        CPLString value = feature->GetFieldAsString(1);

        out[key] = value;
    }
    m_metadata->SetAttributeFilter(nullptr);
    return out;
}

void Dataset::deleteProperties(const char* table)
{
    if(!m_metadata)
        return;
    executeSQL(CPLSPrintf("DELETE FROM %s WHERE %s LIKE \"%s.%%\"",
                          METHADATA_TABLE_NAME, META_KEY, table));
}

void Dataset::lockExecuteSql(bool lock)
{
    if(lock) {
        CPLAcquireMutex(m_executeSQLMutex, 15.0);
    }
    else {
        CPLReleaseMutex(m_executeSQLMutex);
    }
}

bool Dataset::destroyTable(Table* table)
{
    if(destroyTable(m_DS, table->m_layer)) {
        deleteProperties(table->name());
        notifyChanges();
        return true;
    }
    return false;
}

bool Dataset::destroy()
{
    clear();
    GDALClose(m_DS);
    m_DS = nullptr;
    GDALClose(m_addsDS);
    m_addsDS = nullptr;

    if(!File::deleteFile(m_path)) {
        return false;
    }

    // Delete additions
    if(!Filter::isDatabase(type())) {
        const char* ovrPath = CPLResetExtension(m_path, ADDS_EXT);
        if(Folder::isExists(ovrPath)) {
            File::deleteFile(ovrPath);
        }
    }
    const char* attachmentsPath = CPLResetExtension(m_path, ATTACH_SUFFIX);
    if(Folder::isExists(attachmentsPath)) {
        Folder::rmDir(attachmentsPath);
    }

    // aux.xml
    CPLString auxPath = m_path + ".aux.xml";
    if(Folder::isExists(auxPath)) {
        File::deleteFile(auxPath);
    }

    CPLString name = fullName();
    if(m_parent) {
        m_parent->notifyChanges();
    }

    Notify::instance().onNotify(name, ngsChangeCode::CC_DELETE_OBJECT);

    return true;
}

char** Dataset::metadata(const char* domain) const {
    if(nullptr == m_DS)
        return nullptr;
    CPLMutexHolder holder(m_executeSQLMutex);
    return m_DS->GetMetadata(domain);
}

bool Dataset::isNameValid(const char* name) const
{
    for(const ObjectPtr& object : m_children)
        if(EQUAL(object->name(), name))
            return false;
    if(EQUAL(METHADATA_TABLE_NAME, name) && Filter::isDatabase(m_type))
        return false;
    return true;
}

bool forbiddenChar(char c)
{
    return std::find(forbiddenChars.begin(), forbiddenChars.end(), c) !=
            forbiddenChars.end();
}

CPLString Dataset::normalizeDatasetName(const CPLString &name) const
{
    // create unique name
    CPLString outName;
    if(name.empty()) {
        outName = "new_dataset";
    }
    else {
        outName = normalize(name);
        replace_if(outName.begin(), outName.end(), forbiddenChar, '_');
    }

    CPLString originName = outName;
    int nameCounter = 0;
    while(!isNameValid (outName)) {
        outName = originName + "_" + CPLSPrintf("%d", ++nameCounter);
        if(nameCounter == MAX_EQUAL_NAMES) {
            return "";
        }
    }

    return outName;
}

CPLString Dataset::normalizeFieldName(const CPLString &name) const
{
    CPLString out = normalize(name); // TODO: add lang support, i.e. ru_RU)

    replace_if(out.begin(), out.end(), forbiddenChar, '_');

    if(isdigit(out[0]))
        out = "Fld_" + out;

    if(Filter::isDatabase(m_type)) {
        // Check for sql forbidden names
        CPLString testFb = out.toupper();
        if(find(forbiddenSQLFieldNames.begin (), forbiddenSQLFieldNames.end(),
                testFb) != forbiddenSQLFieldNames.end()) {
            out += "_";
        }
    }

    return out;
}

bool Dataset::skipFillFeatureClass(OGRLayer* layer)
{
    if(EQUAL(layer->GetName(), METHADATA_TABLE_NAME)) {
        m_metadata = layer;
        return true;
    }
    if(EQUALN(layer->GetName(), NG_PREFIX, NG_PREFIX_LEN)) {
        return true;
    }
    return false;
}

void Dataset::fillFeatureClasses()
{
    for(int i = 0; i < m_DS->GetLayerCount(); ++i) {
        OGRLayer* layer = m_DS->GetLayer(i);
        if(nullptr != layer) {
            OGRwkbGeometryType geometryType = layer->GetGeomType();
            // Hide metadata, overviews, history tables
            if(skipFillFeatureClass(layer)) {
                continue;
            }

            const char* layerName = layer->GetName();
            // layer->GetLayerDefn()->GetGeomFieldCount() == 0
            if(geometryType == wkbNone) {
                m_children.push_back(ObjectPtr(new Table(layer, this,
                        CAT_TABLE_ANY, layerName)));
            }
            else {
                m_children.push_back(ObjectPtr(new FeatureClass(layer, this,
                            CAT_FC_ANY, layerName)));
            }
        }
    }
}

GDALDataset* Dataset::createAdditionsDataset()
{
    if(m_addsDS) {
        return m_addsDS;
    }

    if(Filter::isDatabase(type())) {
        m_addsDS = m_DS;
        m_addsDS->Reference();
        return m_addsDS;
    }
    else {
        CPLString ovrPath = CPLResetExtension(m_path, ADDS_EXT);
        CPLErrorReset();
        GDALDriver* poDriver = Filter::getGDALDriver(CAT_CONTAINER_SQLITE);
        if(poDriver == nullptr) {
            errorMessage(COD_CREATE_FAILED,
                                _("SQLite driver is not present"));
            return nullptr;
        }

        Options options;
        options.addOption("METADATA", "NO");
        options.addOption("SPATIALITE", "NO");
        options.addOption("INIT_WITH_EPSG", "NO");
        auto createOptionsPointer = options.getOptions();

        GDALDataset* DS = poDriver->Create(ovrPath, 0, 0, 0, GDT_Unknown,
                                           createOptionsPointer.get());
        if(DS == nullptr) {
            errorMessage(CPLGetLastErrorMsg());
            return nullptr;
        }
        m_addsDS = DS;
        return m_addsDS;
    }
}

OGRLayer* Dataset::createOverviewsTable(const char* name)
{
    if(!m_addsDS) {
        createAdditionsDataset();
    }

    if(!m_addsDS)
        return nullptr;

    return createOverviewsTable(m_addsDS, overviewsTableName(name));
}

bool Dataset::createOverviewsTableIndex(const char* name)
{
    if(!m_addsDS)
        return false;

    return createOverviewsTableIndex(m_addsDS, overviewsTableName(name));
}

bool Dataset::dropOverviewsTableIndex(const char* name)
{
    if(!m_addsDS)
        return false;

    return dropOverviewsTableIndex(m_addsDS, overviewsTableName(name));
}

const char* Dataset::overviewsTableName(const char* name) const
{
    return CPLSPrintf("%s%s_%s", NG_PREFIX, name, OVR_SUFFIX);
}

bool Dataset::createOverviewsTableIndex(GDALDataset* ds, const char* name)
{
    ds->ExecuteSQL(CPLSPrintf("CREATE INDEX IF NOT EXISTS %s_idx on %s (%s, %s, %s)", name,
                              name, OVR_X_KEY, OVR_Y_KEY, OVR_ZOOM_KEY), nullptr,
                   nullptr);
    return true;
}

bool Dataset::dropOverviewsTableIndex(GDALDataset* ds, const char* name)
{
    ds->ExecuteSQL(CPLSPrintf("DROP INDEX IF EXISTS %s_idx", name), nullptr, nullptr);
    return true;
}

bool Dataset::destroyOverviewsTable(const char* name)
{
    if(!m_addsDS)
        return false;

    OGRLayer* layer = m_addsDS->GetLayerByName(overviewsTableName(name));
    if(!layer)
        return false;
    return destroyTable(m_DS, layer);
}

bool Dataset::clearOverviewsTable(const char* name)
{
    return deleteFeatures(overviewsTableName(name));
}

OGRLayer* Dataset::getOverviewsTable(const char* name)
{
    if(!m_addsDS)
        return nullptr;

    return m_addsDS->GetLayerByName(overviewsTableName(name));
}

const char* Dataset::options(enum ngsOptionType optionType) const
{
    switch (optionType) {
    case OT_CREATE_DATASOURCE:
    case OT_CREATE_LAYER:
    case OT_CREATE_LAYER_FIELD:
    case OT_CREATE_RASTER:
    case OT_OPEN:
        return DatasetBase::options(m_type, optionType);
    case OT_LOAD:
        return "<LoadOptionList>"
               "  <Option name='MOVE' type='boolean' description='If TRUE move dataset, else copy it.' default='FALSE'/>"
               "  <Option name='NEW_NAME' type='string' description='The new name for loaded dataset'/>"
               "  <Option name='ACCEPT_GEOMETRY' type='string-select' description='Load only specific geometry types' default='ANY'>"
               "    <Value>ANY</Value>"
               "    <Value>POINT</Value>"
               "    <Value>LINESTRING</Value>"
               "    <Value>POLYGON</Value>"
               "    <Value>MULTIPOINT</Value>"
               "    <Value>MULTILINESTRING</Value>"
               "    <Value>MULTIPOLYGON</Value>"
               "  </Option>"
               "  <Option name='FORCE_GEOMETRY_TO_MULTI' type='boolean' description='Force input geometry to multi' default='NO'/>"
               "  <Option name='SKIP_EMPTY_GEOMETRY' type='boolean' description='Skip empty geometry' default='NO'/>"
               "  <Option name='SKIP_INVALID_GEOMETRY' type='boolean' description='Skip invalid geometry' default='NO'/>"
               "  <Option name='CREATE_OVERVIEWS_TABLE' type='boolean' description='Create empty overviews table' default='NO'/>"
               "  <Option name='CREATE_OVERVIEWS' type='boolean' description='Create overviews table and fill it with overviews. The level should be set by ZOOM_LEVELS option' default='NO'/>"
               "  <Option name='ZOOM_LEVELS' type='string' description='Comma separated list of zoom level' default=''/>"
               "</LoadOptionList>";
    }

    return "";
}

bool Dataset::hasChildren()
{
    if(m_childrenLoaded)
        return ObjectContainer::hasChildren();

    if(!isOpened()) {
        if(!open(GDAL_OF_SHARED|GDAL_OF_UPDATE|GDAL_OF_VERBOSE_ERROR)) {
            return false;
        }
    }

    // fill vector layers and tables
    fillFeatureClasses();

    // fill rasters
    char** subdatasetList = m_DS->GetMetadata("SUBDATASETS");
    std::vector<CPLString> siblingFiles;
    if(nullptr != subdatasetList) {
        int i = 0;
        size_t strLen = 0;
        const char* testStr = nullptr;
        char rasterPath[255];
        while((testStr = subdatasetList[i++]) != nullptr) {
            strLen = CPLStrnlen(testStr, 255);
            if(EQUAL(testStr + strLen - 4, "NAME")) {
                CPLStrlcpy(rasterPath, testStr, strLen - 4);
                CPLStringList pathPortions(CSLTokenizeString2( rasterPath, ":", 0 ));
                const char* rasterName = pathPortions[pathPortions.size() - 1];
                m_children.push_back(ObjectPtr(new Raster(siblingFiles, this,
                    CAT_RASTER_ANY, rasterName, rasterPath)));
            }
        }
        CSLDestroy(subdatasetList);
    }

    m_childrenLoaded = true;

    return ObjectContainer::hasChildren();
}

bool Dataset::isReadOnly() const
{
    if(m_DS == nullptr)
        return true;
    return m_DS->GetAccess() == GA_ReadOnly;
}

int Dataset::paste(ObjectPtr child, bool move, const Options &options,
                    const Progress &progress)
{
    CPLString newName = options.stringOption("NEW_NAME",
                                                CPLGetBasename(child->name()));
    newName = normalizeDatasetName(newName);
    if(move) {
        progress.onProgress(COD_IN_PROCESS, 0.0,
                        _("Move '%s' to '%s'"), newName.c_str (),
                            m_name.c_str ());
    }
    else {
        progress.onProgress(COD_IN_PROCESS, 0.0,
                        _("Copy '%s' to '%s'"), newName.c_str (),
                            m_name.c_str ());
    }

    if(child->type() == CAT_CONTAINER_SIMPLE) {
        SimpleDataset* const dataset = ngsDynamicCast(SimpleDataset, child);
        dataset->hasChildren();
        child = dataset->internalObject();
    }

    CPLString fullNameStr;
    if(Filter::isTable(child->type())) {
        TablePtr srcTable = std::dynamic_pointer_cast<Table>(child);
        if(!srcTable) {
            return errorMessage(move ? COD_MOVE_FAILED : COD_COPY_FAILED,
                                _("Source object '%s' report type TABLE, but it is not a table"),
                                child->name().c_str());
        }
        OGRFeatureDefn * const srcDefinition = srcTable->definition();
        std::unique_ptr<Table> dstTable(createTable(newName, CAT_TABLE_ANY,
                                                    srcDefinition,
                                                    options));
        if(nullptr == dstTable) {
            return move ? COD_MOVE_FAILED : COD_COPY_FAILED;
        }

        auto dstFields = dstTable->fields();
        // Create fields map. We expected equal count of fields
        FieldMapPtr fieldMap(dstFields.size());
        for(int i = 0; i < static_cast<int>(dstFields.size()); ++i) {
            fieldMap[i] = i;
        }

        int result = dstTable->copyRows(srcTable, fieldMap, progress);
        if(result != COD_SUCCESS) {
            return result;
        }
        fullNameStr = dstTable->fullName();
    }
    else if(Filter::isFeatureClass(child->type())) {
        FeatureClassPtr srcFClass = std::dynamic_pointer_cast<FeatureClass>(child);
        if(!srcFClass) {
            return errorMessage(move ? COD_MOVE_FAILED : COD_COPY_FAILED,
                                _("Source object '%s' report type FEATURECLASS, but it is not a feature class"),
                                child->name().c_str());
        }
        bool createOvr = options.boolOption("CREATE_OVERVIEWS", false) &&
                !options.stringOption("ZOOM_LEVELS", "").empty();
        bool toMulti = options.boolOption("FORCE_GEOMETRY_TO_MULTI", false);
        OGRFeatureDefn * const srcDefinition = srcFClass->definition();
        std::vector<OGRwkbGeometryType> geometryTypes =
                srcFClass->geometryTypes();
        OGRwkbGeometryType filterFeometryType =
                FeatureClass::geometryTypeFromName(
                    options.stringOption("ACCEPT_GEOMETRY", "ANY"));
        for(OGRwkbGeometryType geometryType : geometryTypes) {
            if(filterFeometryType != geometryType &&
                    filterFeometryType != wkbUnknown) {
                continue;
            }
            CPLString createName = newName;
            OGRwkbGeometryType newGeometryType = geometryType;
            if(geometryTypes.size () > 1 && filterFeometryType == wkbUnknown) {
                createName += "_";
                createName += FeatureClass::geometryTypeName(geometryType,
                                        FeatureClass::GeometryReportType::SIMPLE);
                if(toMulti && geometryType < wkbMultiPoint) {
                    newGeometryType =
                            static_cast<OGRwkbGeometryType>(geometryType + 3);
                }
            }

            std::unique_ptr<FeatureClass> dstFClass(createFeatureClass(createName,
                CAT_FC_ANY, srcDefinition, srcFClass->getSpatialReference(),
                newGeometryType, options));
            if(nullptr == dstFClass) {
                return move ? COD_MOVE_FAILED : COD_COPY_FAILED;
            }
            auto dstFields = dstFClass->fields();
            // Create fields map. We expected equal count of fields
            FieldMapPtr fieldMap(dstFields.size());
            for(int i = 0; i < static_cast<int>(dstFields.size()); ++i) {
                fieldMap[i] = i;
            }

            Progress progressMulti(progress);
            if(createOvr) {
                progressMulti.setTotalSteps(2);
                progressMulti.setStep(0);
            }

            int result = dstFClass->copyFeatures(srcFClass, fieldMap,
                                                 filterFeometryType,
                                                 progressMulti, options);
            if(result != COD_SUCCESS) {
                return result;
            }
            fullNameStr = dstFClass->fullName();

            if(createOvr) {
                progressMulti.setStep(1);
                dstFClass->createOverviews(progressMulti, options);
            }
        }
    }
    else {
        // TODO: raster and container support
        return errorMessage(COD_UNSUPPORTED,
                            _("'%s' has unsuported type"), child->name().c_str());
    }

    if(m_childrenLoaded) {
        notifyChanges();
        Notify::instance().onNotify(fullNameStr, ngsChangeCode::CC_CREATE_OBJECT);
    }

    if(move) {
        return child->destroy() ? COD_SUCCESS : COD_DELETE_FAILED;
    }
    return COD_SUCCESS;
}

bool Dataset::canPaste(const enum ngsCatalogObjectType type) const
{
    if(!isOpened() || isReadOnly())
        return false;
    return Filter::isFeatureClass(type) || Filter::isTable(type) ||
            type == CAT_CONTAINER_SIMPLE;
}

bool Dataset::canCreate(const enum ngsCatalogObjectType type) const
{
    if(!isOpened() || isReadOnly())
        return false;
    return Filter::isFeatureClass(type) || Filter::isTable(type);
}

const char* Dataset::additionsDatasetExtension()
{
    return ADDS_EXT;
}

const char*Dataset::attachmentsFolderExtension()
{
    return ATTACH_SUFFIX;
}

Dataset* Dataset::create(ObjectContainer* const parent,
                        const enum ngsCatalogObjectType type,
                        const CPLString& name,
                        const Options& options)
{
    GDALDriver* driver = Filter::getGDALDriver(type);
    if(nullptr == driver) {
        return nullptr;
    }

    Dataset* out;
    CPLString path = CPLFormFilename(parent->path(), name,
                                     Filter::getExtension(type));
    if(options.boolOption("OVERWRITE")) {
        auto ovr = parent->getChild(CPLFormFilename(nullptr, name,
                                 Filter::getExtension(type)));
        if(ovr && !ovr->destroy()) {
            return nullptr;
        }
    }

    if(Filter::isSimpleDataset(type)) {
        out = new Dataset(parent, CAT_CONTAINER_SIMPLE, name, path);
    }
    else {
        out = new Dataset(parent, type, name, path);
    }

    auto ptrOpt = options.getOptions();
    out->m_DS = driver->Create(path, 0, 0, 0, GDT_Unknown, ptrOpt.get());

    return out;
}

TablePtr Dataset::executeSQL(const char* statement, const char* dialect)
{
    if(nullptr == m_DS) {
        errorMessage(_("Not opened."));
        return TablePtr();
    }

    CPLMutexHolder holder(m_executeSQLMutex);

    OGRLayer* layer = m_DS->ExecuteSQL(statement, nullptr, dialect);
    if(nullptr == layer) {
        errorMessage(CPLGetLastErrorMsg());
        return TablePtr();
    }
    if(layer->GetGeomType() == wkbNone)
        return TablePtr(new Table(layer, this, CAT_QUERY_RESULT));
    else
        return TablePtr(new FeatureClass(layer, this, CAT_QUERY_RESULT_FC));
}

TablePtr Dataset::executeSQL(const char* statement,
                                    GeometryPtr spatialFilter,
                                    const char* dialect)
{
    if(nullptr == m_DS) {
        errorMessage(_("Not opened."));
        return TablePtr();
    }
    OGRGeometry* spaFilter(nullptr);
    if(spatialFilter) {
        spaFilter = spatialFilter.get()->clone();
    }
    OGRLayer* layer = m_DS->ExecuteSQL(statement, spaFilter, dialect);
    if(nullptr == layer) {
        errorMessage(CPLGetLastErrorMsg());
        return TablePtr();
    }
    if(layer->GetGeomType() == wkbNone)
        return TablePtr(new Table(layer, this, CAT_QUERY_RESULT));
    else
        return TablePtr(new FeatureClass(layer, this, CAT_QUERY_RESULT_FC));

}

bool Dataset::open(unsigned int openFlags, const Options &options)
{
    if(isOpened()) {
        return true;
    }

    bool result = DatasetBase::open(m_path, openFlags, options);
    if(result) {
        if(Filter::isDatabase(type())) {
            m_addsDS = m_DS;
            m_addsDS->Reference();
        }
        else {
            const char* ovrPath = CPLResetExtension(m_path, ADDS_EXT);
            if(Folder::isExists(ovrPath)) {
                m_addsDS = static_cast<GDALDataset*>(
                            GDALOpenEx(ovrPath, openFlags, nullptr, nullptr,
                                       nullptr));
            }
        }

        if(m_addsDS == nullptr) {
            warningMessage(CPLGetLastErrorMsg());
        }
        else {
            m_metadata = m_addsDS->GetLayerByName(METHADATA_TABLE_NAME);
        }
    }
    return result;
}

OGRLayer* Dataset::createMetadataTable(GDALDataset* ds)
{
    CPLErrorReset();
    if(nullptr == ds) {
        return nullptr;
    }

    OGRLayer* metadataLayer = ds->CreateLayer(METHADATA_TABLE_NAME, nullptr,
                                                   wkbNone, nullptr);
    if (nullptr == metadataLayer) {
        return nullptr;
    }

    OGRFieldDefn fieldKey(META_KEY, OFTString);
    fieldKey.SetWidth(META_KEY_LIMIT);
    OGRFieldDefn fieldValue(META_VALUE, OFTString);
    fieldValue.SetWidth(META_VALUE_LIMIT);

    if(metadataLayer->CreateField(&fieldKey) != OGRERR_NONE ||
       metadataLayer->CreateField(&fieldValue) != OGRERR_NONE) {
        return nullptr;
    }

    FeaturePtr feature(OGRFeature::CreateFeature(metadataLayer->GetLayerDefn()));

    // write version
    feature->SetField(META_KEY, NGS_VERSION_KEY);
    feature->SetField(META_VALUE, NGS_VERSION_NUM);
    if(metadataLayer->CreateFeature(feature) != OGRERR_NONE) {
        warningMessage(COD_WARNING, _("Failed to add version to methadata"));
    }

    if(ds->GetDriver() == Filter::getGDALDriver(CAT_CONTAINER_GPKG)) {
        ds->SetMetadataItem(NGS_VERSION_KEY, CPLSPrintf("%d", NGS_VERSION_NUM),
                            NG_ADDITIONS_KEY);
    }

    return metadataLayer;
}

bool Dataset::destroyTable(GDALDataset* ds, OGRLayer* layer)
{
    for(int i = 0; i < ds->GetLayerCount (); ++i) {
        if(ds->GetLayer(i) == layer) {
            layer->ResetReading();
            if(ds->DeleteLayer(i) == OGRERR_NONE) {
                return true;
            }
        }
    }
    return false;
}

OGRLayer* Dataset::createOverviewsTable(GDALDataset* ds, const char* name)
{
    OGRLayer* ovrLayer = ds->CreateLayer(name, nullptr, wkbNone, nullptr);
    if (nullptr == ovrLayer) {
        errorMessage(COD_CREATE_FAILED, CPLGetLastErrorMsg());
        return nullptr;
    }

    OGRFieldDefn xField(OVR_X_KEY, OFTInteger);
    OGRFieldDefn yField(OVR_Y_KEY, OFTInteger);
    OGRFieldDefn zField(OVR_ZOOM_KEY, OFTInteger);
    OGRFieldDefn tileField(OVR_TILE_KEY, OFTBinary);

    if(ovrLayer->CreateField(&xField) != OGRERR_NONE ||
       ovrLayer->CreateField(&yField) != OGRERR_NONE ||
       ovrLayer->CreateField(&zField) != OGRERR_NONE ||
       ovrLayer->CreateField(&tileField) != OGRERR_NONE) {
        errorMessage(COD_CREATE_FAILED, CPLGetLastErrorMsg());
        return nullptr;
    }

    return ovrLayer;
}

OGRLayer* Dataset::createEditHistoryTable(GDALDataset* ds, const char* name)
{
    OGRLayer* logLayer = ds->CreateLayer(name, nullptr, wkbNone, nullptr);
    if (nullptr == logLayer) {
        errorMessage(COD_CREATE_FAILED, CPLGetLastErrorMsg());
        return nullptr;
    }

    // Create table  fields
    OGRFieldDefn fidField(FEATURE_ID_FIELD, OFTInteger64);
    OGRFieldDefn afidField(ATTACH_FEATURE_ID_FIELD, OFTInteger64);
    OGRFieldDefn opField(OPERATION_FIELD, OFTInteger64);

    if(logLayer->CreateField(&fidField) != OGRERR_NONE ||
       logLayer->CreateField(&afidField) != OGRERR_NONE ||
       logLayer->CreateField(&opField) != OGRERR_NONE) {
        errorMessage(COD_CREATE_FAILED, CPLGetLastErrorMsg());
        return nullptr;
    }

    return logLayer;
}

OGRLayer* Dataset::createAttachmentsTable(GDALDataset* ds, const char* path,
                                          const char* name)
{
    OGRLayer* attLayer = ds->CreateLayer(name, nullptr, wkbNone, nullptr);
    if (nullptr == attLayer) {
        errorMessage(COD_CREATE_FAILED, CPLGetLastErrorMsg());
        return nullptr;
    }

    // Create folder for files
    if(nullptr != path) {
        CPLString attachmentsPath = CPLResetExtension(path, ATTACH_SUFFIX);
        if(!Folder::isExists(attachmentsPath)) {
            Folder::mkDir(attachmentsPath);
        }
    }

    // Create table  fields
    OGRFieldDefn fidField(ATTACH_FEATURE_ID_FIELD, OFTInteger64);
    OGRFieldDefn nameField(ATTACH_FILE_NAME_FIELD, OFTString);
    OGRFieldDefn descField(ATTACH_DESCRIPTION_FIELD, OFTString);

    if(attLayer->CreateField(&fidField) != OGRERR_NONE ||
       attLayer->CreateField(&nameField) != OGRERR_NONE ||
       attLayer->CreateField(&descField) != OGRERR_NONE) {
        errorMessage(COD_CREATE_FAILED, CPLGetLastErrorMsg());
        return nullptr;
    }

    return attLayer;
}

OGRLayer* Dataset::createAttachmentsTable(const char* name)
{
    if(!m_addsDS) {
        createAdditionsDataset();
    }

    if(!m_addsDS) {
        return nullptr;
    }

    return createAttachmentsTable(m_addsDS, m_path, attachmentsTableName(name));
}

bool Dataset::destroyAttachmentsTable(const char* name)
{
    if(!m_addsDS)
        return false;

    OGRLayer* layer = m_addsDS->GetLayerByName(attachmentsTableName(name));
    if(!layer)
        return false;
    return destroyTable(m_DS, layer);
}

OGRLayer* Dataset::getAttachmentsTable(const char* name)
{
    if(!m_addsDS)
        return nullptr;

    return m_addsDS->GetLayerByName(attachmentsTableName(name));
}

OGRLayer* Dataset::createEditHistoryTable(const char* name)
{
    if(!m_addsDS) {
        createAdditionsDataset();
    }

    if(!m_addsDS) {
        return nullptr;
    }

    return createEditHistoryTable(m_addsDS, historyTableName(name));
}

bool Dataset::destroyEditHistoryTable(const char* name)
{
    if(!m_addsDS)
        return false;
    OGRLayer* layer = m_addsDS->GetLayerByName(historyTableName(name));
    if(!layer)
        return false;
    return destroyTable(m_DS, layer);
}

OGRLayer* Dataset::getEditHistoryTable(const char* name)
{
    if(!m_addsDS)
        return nullptr;
    return m_addsDS->GetLayerByName(historyTableName(name));
}

void Dataset::clearEditHistoryTable(const char* name)
{
    deleteFeatures(historyTableName(name));
}

const char* Dataset::historyTableName(const char* name) const
{
    return CPLSPrintf("%s%s_%s", NG_PREFIX, name, HISTORY_SUFFIX);
}

const char* Dataset::attachmentsTableName(const char* name) const
{
    return CPLSPrintf("%s%s_%s", NG_PREFIX, name, ATTACH_SUFFIX);
}

bool Dataset::deleteFeatures(const char* name)
{
    GDALDataset* ds = nullptr;
    if(m_DS->GetLayerByName(name) != nullptr) {
        ds = m_DS;
    }

    if(!ds && m_addsDS->GetLayerByName(name) != nullptr) {
        ds = m_addsDS;
    }

    if(!ds)
        return false;
    CPLErrorReset();
    CPLMutexHolder holder(m_executeSQLMutex);
    ds->ExecuteSQL(CPLSPrintf("DELETE from %s", name), nullptr, nullptr);
    return CPLGetLastErrorType() < CE_Failure;
}

void Dataset::refresh()
{
    if(!m_childrenLoaded) {
        hasChildren();
        return;
    }

    std::vector<const char*> deleteNames, addNames;
    for(int i = 0; i < m_DS->GetLayerCount(); ++i) {
        OGRLayer* layer = m_DS->GetLayer(i);
        if(nullptr != layer) {
            // Hide metadata, overviews, history tables
            if(skipFillFeatureClass(layer)) {
                continue;
            }
            const char* layerName = layer->GetName();
            CPLDebug("ngstore", "refresh layer %s", layerName);
            addNames.push_back(layerName);
        }
    }

    // Fill delete names array
    for(const ObjectPtr& child : m_children) {
        CPLDebug("ngstore", "refresh del layer %s", child->name().c_str());
        deleteNames.push_back(child->name());
    }

    // Remove same names from add and delete arrays
    removeDuplicates(deleteNames, addNames);

    CPLDebug("ngstore", "Add count %ld, delete count %ld", addNames.size(),
             deleteNames.size());

    // Delete objects
    auto it = m_children.begin();
    while(it != m_children.end()) {
        const char* name = (*it)->name();
        auto itdn = std::find(deleteNames.begin(), deleteNames.end(), name);
        if(itdn != deleteNames.end()) {
            deleteNames.erase(itdn);
            it = m_children.erase(it);
        }
        else {
            ++it;
        }
    }

    // Create new objects
    for(auto layerName: addNames) {
        OGRLayer* layer = m_DS->GetLayerByName(layerName);
        if(nullptr != layer) {
            OGRwkbGeometryType geometryType = layer->GetGeomType();
            if(geometryType == wkbNone) {
                m_children.push_back(ObjectPtr(new Table(layer, this,
                        CAT_TABLE_ANY, layerName)));
            }
            else {
                m_children.push_back(ObjectPtr(new FeatureClass(layer, this,
                            CAT_FC_ANY, layerName)));
            }
        }
    }
}

} // namespace ngs
