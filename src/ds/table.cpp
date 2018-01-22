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
#include "table.h"

#include "api_priv.h"
#include "dataset.h"
#include "catalog/file.h"
#include "catalog/folder.h"
#include "ngstore/api.h"
#include "util/error.h"
#include "util/notify.h"

namespace ngs {


//------------------------------------------------------------------------------
// FieldMapPtr
//-------------------------------------------------

FieldMapPtr::FieldMapPtr(unsigned long size) :
    shared_ptr(static_cast<int*>(CPLMalloc(sizeof(int) * size)), CPLFree)
{

}

int &FieldMapPtr::operator[](int key)
{
    return get()[key];
}

const int &FieldMapPtr::operator[](int key) const
{
    return get()[key];
}

//------------------------------------------------------------------------------
// FeaturePtr
//------------------------------------------------------------------------------

FeaturePtr::FeaturePtr(OGRFeature* feature, Table* table) :
    shared_ptr( feature, OGRFeature::DestroyFeature ),
    m_table(table)
{

}

FeaturePtr::FeaturePtr(OGRFeature* feature, const Table* table) :
    shared_ptr( feature, OGRFeature::DestroyFeature ),
    m_table(const_cast<Table*>(table))
{

}

FeaturePtr:: FeaturePtr() :
    shared_ptr( nullptr, OGRFeature::DestroyFeature ),
    m_table(nullptr)
{

}

FeaturePtr& FeaturePtr::operator=(OGRFeature* feature) {
    reset(feature);
    return *this;
}

//------------------------------------------------------------------------------
// Table
//------------------------------------------------------------------------------

Table::Table(OGRLayer *layer,
             ObjectContainer * const parent,
             const enum ngsCatalogObjectType type,
             const CPLString &name) :
    Object(parent, type, name, ""),
    m_layer(layer),
    m_attTable(nullptr),
    m_editHistoryTable(nullptr),
    m_saveEditHistory(NOT_FOUND),
    m_featureMutex(CPLCreateMutex())
{
    CPLReleaseMutex(m_featureMutex);
}

Table::~Table()
{
    CPLDebug("ngstore", "CPLDestroyMutex(m_featureMutex)");
    CPLDestroyMutex(m_featureMutex);

    if(m_type == CAT_QUERY_RESULT || m_type == CAT_QUERY_RESULT_FC) {
        Dataset* const dataset = dynamic_cast<Dataset*>(m_parent);
        if(nullptr != dataset) {
            GDALDataset * const DS = dataset->getGDALDataset();
            if(nullptr != DS && nullptr != m_layer) {
                DS->ReleaseResultSet(m_layer);
            }
        }
    }
}

FeaturePtr Table::createFeature() const
{
    if(nullptr == m_layer)
        return FeaturePtr();

    OGRFeature* pFeature = OGRFeature::CreateFeature(m_layer->GetLayerDefn());
    if (nullptr == pFeature)
        return FeaturePtr();

    return FeaturePtr(pFeature, this);
}

FeaturePtr Table::getFeature(GIntBig id) const
{
    if(nullptr == m_layer)
        return FeaturePtr();
    CPLMutexHolder holder(m_featureMutex);
    OGRFeature* pFeature = m_layer->GetFeature(id);
    if (nullptr == pFeature)
        return FeaturePtr();

    return FeaturePtr(pFeature, this);
}

bool Table::insertFeature(const FeaturePtr &feature, bool logEdits)
{
    if(nullptr == m_layer)
        return false;

    CPLErrorReset();
    Dataset* dataset = dynamic_cast<Dataset*>(m_parent);
    DatasetExecuteSQLLockHolder holder(dataset);
    if(m_layer->CreateFeature(feature) == OGRERR_NONE) {
        if(logEdits) {
            FeaturePtr opFeature = logEditFeature(feature, FeaturePtr(),
                                                  CC_CREATE_FEATURE);
            logEditOperation(opFeature);
        }
        if(dataset && !dataset->isBatchOperation()) {
            Notify::instance().onNotify(CPLSPrintf("%s#" CPL_FRMT_GIB,
                                                   fullName().c_str(),
                                                   feature->GetFID()),
                                    ngsChangeCode::CC_CREATE_FEATURE);
        }
        return true;
    }

    return errorMessage(CPLGetLastErrorMsg());
}

bool Table::updateFeature(const FeaturePtr &feature, bool logEdits)
{
    if(nullptr == m_layer)
        return false;

    CPLErrorReset();
    Dataset* dataset = dynamic_cast<Dataset*>(m_parent);
    DatasetExecuteSQLLockHolder holder(dataset);
    if(m_layer->SetFeature(feature) == OGRERR_NONE) {
        if(logEdits) {
            FeaturePtr opFeature = logEditFeature(feature, FeaturePtr(),
                                                  CC_CHANGE_FEATURE);
            logEditOperation(opFeature);
        }
        if(dataset && !dataset->isBatchOperation()) {
            Notify::instance().onNotify(CPLSPrintf("%s#" CPL_FRMT_GIB,
                                               fullName().c_str(),
                                               feature->GetFID()),
                                    ngsChangeCode::CC_CHANGE_FEATURE);
        }
        return true;
    }

    return errorMessage(CPLGetLastErrorMsg());
}

bool Table::deleteFeature(GIntBig id, bool logEdits)
{
    if(nullptr == m_layer) {
        return false;
    }

    FeaturePtr logFeature;
    if(saveEditHistory() && logEdits) {
        FeaturePtr feature = m_layer->GetFeature(id);
        logFeature = logEditFeature(feature, FeaturePtr(), CC_DELETE_FEATURE);
    }

    CPLErrorReset();
    DatasetExecuteSQLLockHolder holder(dynamic_cast<Dataset*>(m_parent));
    if(m_layer->DeleteFeature(id) == OGRERR_NONE) {
        deleteAttachments(id, logEdits);

        if(logEdits) {
            logEditOperation(logFeature);
        }
        Notify::instance().onNotify(CPLSPrintf("%s#" CPL_FRMT_GIB,
                                               fullName().c_str(),
                                               id),
                                    ngsChangeCode::CC_DELETE_FEATURE);
        return true;
    }

    return errorMessage(CPLGetLastErrorMsg());
}

bool Table::deleteFeatures(bool logEdits)
{
    if(nullptr == m_layer) {
        return false;
    }

    CPLErrorReset();
    Dataset* const dataset = dynamic_cast<Dataset*>(m_parent);
    if(nullptr == dataset) {
        return false;
    }

    if(dataset->deleteFeatures(name())) {
        if(logEdits) {
            FeaturePtr logFeature = logEditFeature(FeaturePtr(), FeaturePtr(),
                                                   CC_DELETEALL_FEATURES);
            logEditOperation(logFeature);
        }
        Notify::instance().onNotify(fullName(),
                                    ngsChangeCode::CC_DELETEALL_FEATURES);
        dataset->destroyAttachmentsTable(name()); // Attachments table maybe not exists
        Folder::rmDir(getAttachmentsPath());
        return true;
    }

    return false;
}

GIntBig Table::featureCount(bool force) const
{
    if(nullptr == m_layer)
        return 0;

    CPLMutexHolder holder(m_featureMutex);
    return m_layer->GetFeatureCount(force ? TRUE : FALSE);
}

void Table::reset() const
{
    if(nullptr != m_layer) {
        CPLMutexHolder holder(m_featureMutex);
        m_layer->ResetReading();
    }
}

FeaturePtr Table::nextFeature() const
{
    if(nullptr == m_layer)
        return FeaturePtr();
    CPLMutexHolder holder(m_featureMutex);
    return FeaturePtr(m_layer->GetNextFeature(), this);
}

int Table::copyRows(const TablePtr srcTable, const FieldMapPtr fieldMap,
                     const Progress& progress)
{
    if(!srcTable) {
        return errorMessage(COD_COPY_FAILED, _("Source table is invalid"));
    }

    progress.onProgress(COD_IN_PROCESS, 0.0,
                       _("Start copy records from '%s' to '%s'"),
                       srcTable->name().c_str(), m_name.c_str());

    DatasetBatchOperationHolder holder(dynamic_cast<Dataset*>(m_parent));

    GIntBig featureCount = srcTable->featureCount();
    double counter = 0;
    srcTable->reset();
    FeaturePtr feature;
    while((feature = srcTable->nextFeature ())) {
        double complete = counter / featureCount;
        if(!progress.onProgress(COD_IN_PROCESS, complete,
                           _("Copy in process ..."))) {
            return  COD_CANCELED;
        }

        FeaturePtr dstFeature = createFeature();
        dstFeature->SetFieldsFrom(feature, fieldMap.get());

        if(!insertFeature(dstFeature)) {
            if(!progress.onProgress(COD_WARNING, complete,
                               _("Create feature failed. Source feature FID:" CPL_FRMT_GIB),
                               feature->GetFID ())) {
               return  COD_CANCELED;
            }
        }
        counter++;
    }

    progress.onProgress(COD_FINISHED, 1.0, _("Done. Copied %d rows"),
                       int(counter));

    return COD_SUCCESS;
}

const char* Table::fidColumn() const
{
    if(nullptr == m_layer)
        return "";
    return m_layer->GetFIDColumn();
}

char** Table::metadata(const char* domain) const
{
    if(nullptr == m_layer)
        return nullptr;
    DatasetExecuteSQLLockHolder holder(dynamic_cast<Dataset*>(m_parent));
    return m_layer->GetMetadata(domain);
}

bool Table::destroy()
{
    Dataset* const dataset = dynamic_cast<Dataset*>(m_parent);
    if(nullptr == dataset) {
        return errorMessage(_("Parent is not dataset"));
    }

    CPLString fullNameStr = fullName();
    CPLString name = m_name;
    CPLString attPath = getAttachmentsPath();
    m_layer->SetAttributeFilter(nullptr);
    reset();
    if(dataset->destroyTable(this)) {
        Notify::instance().onNotify(fullNameStr, ngsChangeCode::CC_DELETE_OBJECT);

        dataset->destroyAttachmentsTable(name); // Attachments table maybe not exists
        Folder::rmDir(attPath);
        dataset->destroyEditHistoryTable(name); // Log edit table may be not exists
        return true;
    }
    return false;
}

void Table::setAttributeFilter(const char* filter)
{
    if(nullptr != m_layer) {
        m_layer->SetAttributeFilter(filter);
    }
}

OGRFeatureDefn*Table::definition() const
{
    if(nullptr == m_layer)
        return nullptr;
    return m_layer->GetLayerDefn();
}

bool Table::initAttachmentsTable() const
{
    if(m_attTable) {
        return true;
    }

    Dataset* parentDS = dynamic_cast<Dataset*>(m_parent);
    if(nullptr == parentDS) {
        return false;
    }

    m_attTable = parentDS->getAttachmentsTable(name());
    if(nullptr == m_attTable) {
        m_attTable = parentDS->createAttachmentsTable(name());
    }

    return m_attTable != nullptr;
}

bool Table::initEditHistoryTable() const
{
    if(m_editHistoryTable) {
        return true;
    }

    Dataset* parentDS = dynamic_cast<Dataset*>(m_parent);
    if(nullptr == parentDS) {
        return false;
    }

    m_editHistoryTable = parentDS->getEditHistoryTable(name());
    if(nullptr == m_editHistoryTable) {
        m_editHistoryTable = parentDS->createEditHistoryTable(name());
    }

    return m_editHistoryTable != nullptr;
}

CPLString Table::getAttachmentsPath() const
{
    const char* dstRootPath = CPLResetExtension(
                m_parent->path(), Dataset::attachmentsFolderExtension());
    return CPLFormFilename(dstRootPath, name(), nullptr);
}

void Table::fillFields() const
{
    m_fields.clear();
    if(nullptr != m_layer) {
        Dataset* parentDataset = dynamic_cast<Dataset*>(m_parent);
        parentDataset->lockExecuteSql(true);
        OGRFeatureDefn* defn = m_layer->GetLayerDefn();
        parentDataset->lockExecuteSql(false);
        if(nullptr == defn || nullptr == parentDataset) {
            return;
        }

        auto propertyList = properties(NG_ADDITIONS_KEY);

        parentDataset->lockExecuteSql(true);
        for(int i = 0; i < defn->GetFieldCount(); ++i) {
            OGRFieldDefn* fieldDefn = defn->GetFieldDefn(i);
            Field fieldDesc;
            fieldDesc.m_type = fieldDefn->GetType();
            CPLStrlcpy(fieldDesc.m_name, fieldDefn->GetNameRef(), 255);
            //fieldDesc.m_name = fieldDefn->GetNameRef();

            CPLString alias = propertyList[CPLSPrintf("FIELD_%d_ALIAS", i)];
            if(alias.empty()) {
                CPLStrlcpy(fieldDesc.m_alias, fieldDesc.m_name, 255);
            }
            else {
                CPLStrlcpy(fieldDesc.m_alias, alias.c_str(), 1024);
                //fieldDesc.m_alias = properties[CPLSPrintf("FIELD_%d_ALIAS", i)];
            }

            CPLString originalName = propertyList[CPLSPrintf("FIELD_%d_NAME", i)];
            if(originalName.empty()) {
                CPLStrlcpy(fieldDesc.m_originalName, fieldDefn->GetNameRef(), 255);
            }
            else {
                CPLStrlcpy(fieldDesc.m_originalName, originalName.c_str(), 255);
                //fieldDesc.m_originalName = properties[CPLSPrintf("FIELD_%d_NAME", i)];
            }
            m_fields.push_back(fieldDesc);
        }        
        parentDataset->lockExecuteSql(false);

        // Fill metadata
        propertyList = properties(USER_KEY);
        for(auto it = propertyList.begin(); it != propertyList.end(); ++it) {
            if(m_layer->GetMetadataItem(it->first, USER_KEY) == nullptr) {
                m_layer->SetMetadataItem(it->first, it->second, USER_KEY);
            }
        }
    }
}

GIntBig Table::addAttachment(GIntBig fid, const char* fileName,
                             const char* description, const char* filePath,
                             char** options, bool logEdits)
{
    if(!initAttachmentsTable()) {
        return NOT_FOUND;
    }
    bool move = CPLFetchBool(options, "MOVE", false);

    FeaturePtr newAttachment = OGRFeature::CreateFeature(
                m_attTable->GetLayerDefn());

    newAttachment->SetField(ATTACH_FEATURE_ID_FIELD, fid);
    newAttachment->SetField(ATTACH_FILE_NAME_FIELD, fileName);
    newAttachment->SetField(ATTACH_DESCRIPTION_FIELD, description);

    if(m_attTable->CreateFeature(newAttachment) == OGRERR_NONE) {
        CPLString dstTablePath = getAttachmentsPath();
        if(!Folder::isExists(dstTablePath)) {
            Folder::mkDir(dstTablePath);
        }
        CPLString dstFeaturePath = CPLFormFilename(dstTablePath,
                                                   CPLSPrintf(CPL_FRMT_GIB, fid),
                                                   nullptr);
        if(!Folder::isExists(dstFeaturePath)) {
            Folder::mkDir(dstFeaturePath);
        }

        CPLString dstPath = CPLFormFilename(dstFeaturePath,
                                            CPLSPrintf(CPL_FRMT_GIB,
                                                       newAttachment->GetFID()),
                                            nullptr);
        if(Folder::isExists(filePath)) {
            if(move) {
                File::moveFile(filePath, dstPath);
            }
            else {
                File::copyFile(filePath, dstPath);
            }
        }

        if(logEdits) {
            FeaturePtr feature = m_layer->GetFeature(fid);
            FeaturePtr logFeauture = logEditFeature(feature, newAttachment,
                                                    CC_CREATE_ATTACHMENT);

            logEditOperation(logFeauture);
        }
        return newAttachment->GetFID();
    }

    return NOT_FOUND;
}

bool Table::deleteAttachment(GIntBig aid, bool logEdits)
{
    if(!initAttachmentsTable()) {
        return false;
    }

    FeaturePtr attFeature = m_attTable->GetFeature(aid);
    bool result = m_attTable->DeleteFeature(aid) == OGRERR_NONE;
    if(result) {
        GIntBig fid = attFeature->GetFieldAsInteger64(ATTACH_FEATURE_ID_FIELD);
        CPLString attFeaturePath = CPLFormFilename(getAttachmentsPath(),
                                                   CPLSPrintf(CPL_FRMT_GIB, fid),
                                                   nullptr);
        CPLString attPath = CPLFormFilename(attFeaturePath,
                                                   CPLSPrintf(CPL_FRMT_GIB, aid),
                                                   nullptr);

        result = File::deleteFile(attPath);

        if(logEdits) {
            FeaturePtr feature = m_layer->GetFeature(fid);
            FeaturePtr logFeauture = logEditFeature(feature, attFeature,
                                                    CC_DELETE_ATTACHMENT);
            logEditOperation(logFeauture);
        }
    }

    return result;
}

bool Table::deleteAttachments(GIntBig fid, bool logEdits)
{
    Dataset* dataset = dynamic_cast<Dataset*>(m_parent);
    if(nullptr == dataset) {
        return false;
    }

    dataset->lockExecuteSql(true);
    dataset->executeSQL(CPLSPrintf("DELETE FROM %s_%s WHERE %s = " CPL_FRMT_GIB,
                                   m_name.c_str(), Dataset::attachmentsFolderExtension(),
                                   ATTACH_FEATURE_ID_FIELD, fid));
    dataset->lockExecuteSql(false);

    CPLString attFeaturePath = CPLFormFilename(getAttachmentsPath(),
                                               CPLSPrintf(CPL_FRMT_GIB, fid),
                                               nullptr);
    Folder::rmDir(attFeaturePath);

    if(logEdits) {
        FeaturePtr feature = m_layer->GetFeature(fid);
        FeaturePtr logFeauture = logEditFeature(feature, FeaturePtr(),
                                                CC_DELETEALL_ATTACHMENTS);
        logEditOperation(logFeauture);
    }
    return true;
}

bool Table::updateAttachment(GIntBig aid, const char* fileName,
                             const char* description, bool logEdits)
{
    if(!initAttachmentsTable()) {
        return false;
    }

    FeaturePtr attFeature = m_attTable->GetFeature(aid);
    if(!attFeature) {
        return false;
    }

    if(fileName) {
        attFeature->SetField(ATTACH_FILE_NAME_FIELD, fileName);
    }
    if(description) {
        attFeature->SetField(ATTACH_DESCRIPTION_FIELD, description);
    }

    DatasetExecuteSQLLockHolder holder(dynamic_cast<Dataset*>(m_parent));
    if(m_attTable->SetFeature(attFeature) == OGRERR_NONE) {
        if(logEdits) {
            GIntBig fid = attFeature->GetFieldAsInteger64(ATTACH_FEATURE_ID_FIELD);
            FeaturePtr feature = m_layer->GetFeature(fid);
            FeaturePtr logFeauture = logEditFeature(feature, attFeature,
                                                    CC_CHANGE_ATTACHMENT);
            logEditOperation(logFeauture);
        }
        return true;
    }

    return false;
}

std::vector<Table::AttachmentInfo> Table::attachments(GIntBig fid) const
{
    std::vector<AttachmentInfo> out;

    if(!initAttachmentsTable()) {
        return out;
    }

    DatasetExecuteSQLLockHolder holder(dynamic_cast<Dataset*>(m_parent));
    m_attTable->SetAttributeFilter(CPLSPrintf("%s = " CPL_FRMT_GIB,
                                              ATTACH_FEATURE_ID_FIELD, fid));
    //m_attTable->ResetReading();
    FeaturePtr attFeature;
    while((attFeature = m_attTable->GetNextFeature())) {
        AttachmentInfo info;
        info.name = attFeature->GetFieldAsString(ATTACH_FILE_NAME_FIELD);
        info.description = attFeature->GetFieldAsString(ATTACH_DESCRIPTION_FIELD);
        info.id = attFeature->GetFID();

        CPLString attFeaturePath = CPLFormFilename(getAttachmentsPath(),
                                                   CPLSPrintf(CPL_FRMT_GIB, fid),
                                                   nullptr);
        info.path = CPLFormFilename(attFeaturePath,
                                    CPLSPrintf(CPL_FRMT_GIB, info.id),
                                    nullptr);

        info.size = File::fileSize(info.path);

        out.push_back(info);
    }

    return out;
}


bool Table::canDestroy() const
{
    Dataset* const dataSet= dynamic_cast<Dataset*>(m_parent);
    if(nullptr == dataSet)
        return false;
    return !dataSet->isReadOnly();
}

void Table::checkSetProperty(const char* key, const char* value, const char* domain)
{
    if(EQUAL(key, LOG_EDIT_HISTORY_KEY) && EQUAL(domain, NG_ADDITIONS_KEY)) {
        char prevValue = m_saveEditHistory;
        m_saveEditHistory = EQUAL(value, "ON") ? 1 : 0;
        if(prevValue != m_saveEditHistory && prevValue == 1) {
            // Clear history table
            Dataset* parentDataset = dynamic_cast<Dataset*>(m_parent);
            if(nullptr != parentDataset) {
                parentDataset->clearEditHistoryTable(m_name);
            }
        }
    }
}

bool Table::saveEditHistory()
{
    if(m_saveEditHistory == NOT_FOUND) {
        m_saveEditHistory = EQUAL(property(LOG_EDIT_HISTORY_KEY, "OFF",
                                           NG_ADDITIONS_KEY), "ON") ? 1 : 0;
    }
    return m_saveEditHistory == 1;
}

bool Table::setProperty(const char* key, const char* value, const char* domain)
{
    Dataset* parentDataset = dynamic_cast<Dataset*>(m_parent);
    if(nullptr == parentDataset) {
        return false;
    }

    CPLString name = m_name;
    if(nullptr != domain) {
        name += CPLString(".") + domain;
    }

    name += CPLString(".") + key;

    checkSetProperty(key, value, domain);

    return parentDataset->setProperty(name, value);
}

CPLString Table::property(const char* key, const char* defaultValue,
                          const char* domain) const
{
    Dataset* parentDataset = dynamic_cast<Dataset*>(m_parent);
    if(nullptr == parentDataset) {
        return "";
    }

    CPLString name = m_name;
    if(nullptr != domain) {
        name += CPLString(".") + domain;
    }

    name += CPLString(".") + key;

    return parentDataset->property(name, defaultValue);

}

std::map<CPLString, CPLString> Table::properties(const char* domain) const
{
    std::map<CPLString, CPLString> out;
    Dataset* parentDataset = dynamic_cast<Dataset*>(m_parent);
    if(nullptr == parentDataset) {
        return out;
    }

    return parentDataset->properties(m_name, domain);
}

void Table::deleteProperties()
{
    Dataset* parentDataset = dynamic_cast<Dataset*>(m_parent);
    if(nullptr == parentDataset) {
        return;
    }

    return parentDataset->deleteProperties(m_name);
}

const std::vector<Field>& Table::fields() const
{
    if(m_fields.empty())
        fillFields();
    return m_fields;
}

void Table::logEditOperation(FeaturePtr opFeature)
{
    if(!opFeature) {
        return;
    }

    GIntBig fid = opFeature->GetFieldAsInteger64(FEATURE_ID_FIELD);
    GIntBig aid = opFeature->GetFieldAsInteger64(ATTACH_FEATURE_ID_FIELD);
    enum ngsChangeCode code =
            static_cast<enum ngsChangeCode>(opFeature->GetFieldAsInteger64(
                                                OPERATION_FIELD));

    Dataset* parentDataset = dynamic_cast<Dataset*>(m_parent);
    if(nullptr == parentDataset) {
        return;
    }

    DatasetExecuteSQLLockHolder holder(parentDataset);
    if(code == CC_DELETEALL_FEATURES) {
        parentDataset->clearEditHistoryTable(name());

        if(m_editHistoryTable->CreateFeature(opFeature) != OGRERR_NONE) {
            CPLDebug("ngstore", "Log operation %d failed", code);
        }

        return;
    }

    GDALDataset* addsDS = parentDataset->m_addsDS;
    if(code == CC_DELETEALL_ATTACHMENTS) {
        if(fid == NOT_FOUND) {
            return;
        }
        addsDS->ExecuteSQL(CPLSPrintf("DELETE FROM %s WHERE %s = " CPL_FRMT_GIB " AND %s <> -1",
                                       parentDataset->historyTableName(m_name),
                                       FEATURE_ID_FIELD, fid, ATTACH_FEATURE_ID_FIELD),
                           nullptr, nullptr);
        if(m_editHistoryTable->CreateFeature(opFeature) != OGRERR_NONE) {
            CPLDebug("ngstore", "Log operation %d failed", code);
        }

        return;
    }

    // Check delete all
    addsDS->ExecuteSQL(CPLSPrintf("DELETE FROM %s WHERE %s = %d",
                                   parentDataset->historyTableName(m_name),
                                   OPERATION_FIELD, CC_DELETEALL_FEATURES),
                       nullptr, nullptr);

    if(code == CC_CREATE_ATTACHMENT || code == CC_CHANGE_ATTACHMENT) {
        if(fid == NOT_FOUND) {
            return;
        }
        addsDS->ExecuteSQL(CPLSPrintf("DELETE FROM %s WHERE %s = %d AND %s = " CPL_FRMT_GIB,
                                       parentDataset->historyTableName(m_name),
                                       OPERATION_FIELD, CC_DELETEALL_ATTACHMENTS,
                                       FEATURE_ID_FIELD, fid),
                           nullptr, nullptr);
    }

    if(code == CC_CREATE_FEATURE || code == CC_CREATE_ATTACHMENT) {
        if(fid == NOT_FOUND) {
            return;
        }

        if(m_editHistoryTable->CreateFeature(opFeature) != OGRERR_NONE) {
            CPLDebug("ngstore", "Log operation %d failed", code);
        }

        return;
    }

    m_editHistoryTable->SetAttributeFilter(CPLSPrintf("%s = " CPL_FRMT_GIB, FEATURE_ID_FIELD, fid));
    std::vector<FeaturePtr> features;
    FeaturePtr f;
    while((f = m_editHistoryTable->GetNextFeature())) {
        features.push_back(f);
    }
    m_editHistoryTable->SetAttributeFilter(nullptr);

    if(code == CC_DELETE_FEATURE) {
        if(fid == NOT_FOUND) {
            return;
        }

        if(!features.empty()) {
            addsDS->ExecuteSQL(CPLSPrintf("DELETE FROM %s WHERE %s = " CPL_FRMT_GIB,
                                       parentDataset->historyTableName(m_name),
                                       FEATURE_ID_FIELD, fid),
                           nullptr, nullptr);
        }

        // If feature created and than deleted - nop
        for(auto feature : features) {
            enum ngsChangeCode testCode = static_cast<enum ngsChangeCode>(
                        feature->GetFieldAsInteger64(OPERATION_FIELD));

            if(testCode == CC_CREATE_FEATURE) {
                return;
            }
        }

        // Add new operation
        if(m_editHistoryTable->CreateFeature(opFeature) != OGRERR_NONE) {
            CPLDebug("ngstore", "Log operation %d failed", code);
        }

        return;
    }

    if(code == CC_DELETE_ATTACHMENT) {
        if(fid == NOT_FOUND || aid == NOT_FOUND) {
            return;
        }
        FeaturePtr attFeature;
        for(auto feature : features) {
            GIntBig testAid = feature->GetFieldAsInteger64(ATTACH_FEATURE_ID_FIELD);
            if(testAid == aid) {
                attFeature = feature;
                enum ngsChangeCode testCode = static_cast<enum ngsChangeCode>(
                            feature->GetFieldAsInteger64(OPERATION_FIELD));

                if(testCode == CC_CREATE_ATTACHMENT) {
                    if(m_editHistoryTable->DeleteFeature(feature->GetFID()) !=
                            OGRERR_NONE) {
                        CPLDebug("ngstore", "Failed delete log item");
                    }
                    return;
                }
                break;
            }
        }

        if(attFeature) {
            attFeature->SetField(OPERATION_FIELD, code);
            if(m_editHistoryTable->SetFeature(attFeature) != OGRERR_NONE) {
                CPLDebug("ngstore", "Failed update log item");
            }
            return;
        }

        if(m_editHistoryTable->CreateFeature(opFeature) != OGRERR_NONE) {
            CPLDebug("ngstore", "Log operation %d failed", code);
        }

        return;
    }

    if(code == CC_CHANGE_FEATURE) {
        if(fid == NOT_FOUND) {
            return;
        }
        // Check if feature deleted - skip
        // Check if feature added. If added - skip
        // Check if feature changed. If changed - skip
        if(!features.empty()) {
            return;
        }
        // Add new operation
        if(m_editHistoryTable->CreateFeature(opFeature) != OGRERR_NONE) {
            CPLDebug("ngstore", "Log operation %d failed", code);
        }

        return;
    }

    if(code == CC_CHANGE_ATTACHMENT) {
        if(fid == NOT_FOUND || aid == NOT_FOUND) {
            return;
        }
        // Check if attach deleted - skip
        // Check if attach added. If added - skip
        // Check if attach changed. If changed - skip
        for(auto feature : features) {
            GIntBig testAid = feature->GetFieldAsInteger64(ATTACH_FEATURE_ID_FIELD);
            if(testAid == aid) {
                return;
            }
        }

        // Add new operation
        if(m_editHistoryTable->CreateFeature(opFeature) != OGRERR_NONE) {
            CPLDebug("ngstore", "Log operation %d failed", code);
        }

        return;
    }

}

void Table::deleteEditOperation(const ngsEditOperation& op)
{
    Dataset* parentDataset = dynamic_cast<Dataset*>(m_parent);
    if(nullptr == parentDataset) {
        return;
    }

    DatasetExecuteSQLLockHolder holder(parentDataset);

    GDALDataset* addsDS = parentDataset->m_addsDS;
    addsDS->ExecuteSQL(CPLSPrintf("DELETE FROM %s WHERE %s = " CPL_FRMT_GIB " AND %s = " CPL_FRMT_GIB /*" AND %s = %d"*/,
                                  parentDataset->historyTableName(m_name),
                                  FEATURE_ID_FIELD, op.fid,
                                  ATTACH_FEATURE_ID_FIELD, op.aid/*,
                                  OPERATION_FIELD, op.code*/),
                           nullptr, nullptr);
}

std::vector<ngsEditOperation> Table::editOperations() const
{
    if(nullptr == m_editHistoryTable) {
        initEditHistoryTable();
    }

    std::vector<ngsEditOperation> out;
    if(nullptr == m_editHistoryTable) {
        return out;
    }

    DatasetExecuteSQLLockHolder holder(dynamic_cast<Dataset*>(m_parent));
    FeaturePtr feature;
    m_editHistoryTable->ResetReading();
    while((feature = m_editHistoryTable->GetNextFeature())) {
        ngsEditOperation op;
        op.fid = feature->GetFieldAsInteger64(FEATURE_ID_FIELD);
        op.aid = feature->GetFieldAsInteger64(ATTACH_FEATURE_ID_FIELD);
        op.code = static_cast<enum ngsChangeCode>(feature->GetFieldAsInteger64(
                                                      OPERATION_FIELD));
        op.rid = NOT_FOUND;
        op.arid = NOT_FOUND;
        out.push_back(op);
    }
    return out;
}

FeaturePtr Table::logEditFeature(FeaturePtr feature, FeaturePtr attachFeature,
                                      enum ngsChangeCode code)
{
    if(!saveEditHistory()) {
        return FeaturePtr();
    }

    if(nullptr == m_editHistoryTable) {
        initEditHistoryTable();
    }

    if(nullptr == m_editHistoryTable) {
        return FeaturePtr();
    }

    FeaturePtr newOp = OGRFeature::CreateFeature(m_editHistoryTable->GetLayerDefn());

    if(feature) {
        newOp->SetField(FEATURE_ID_FIELD, feature->GetFID());
    }
    else {
        newOp->SetField(FEATURE_ID_FIELD, NOT_FOUND);
    }

    if(attachFeature) {
        newOp->SetField(ATTACH_FEATURE_ID_FIELD, attachFeature->GetFID());
    }
    else {
        newOp->SetField(ATTACH_FEATURE_ID_FIELD, NOT_FOUND);
    }
    newOp->SetField(OPERATION_FIELD, code);

    return newOp;
}

} // namespace ngs
