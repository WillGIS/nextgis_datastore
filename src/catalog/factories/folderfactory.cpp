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
#include "folderfactory.h"

// gdal
#include "cpl_vsi_virtual.h"

#include "catalog/archive.h"
#include "catalog/file.h"
#include "catalog/folder.h"
#include "ngstore/common.h"
#include "util/stringutil.h"

namespace ngs {

FolderFactory::FolderFactory() : ObjectFactory()
{
    m_zipSupported = VSIFileManager::GetHandler(
                Archive::pathPrefix(CAT_CONTAINER_ARCHIVE_ZIP).c_str()) != nullptr;
}

std::string FolderFactory::name() const
{
    return _("Folders and archives");
}

void FolderFactory::createObjects(ObjectContainer * const container,
                                       std::vector<std::string> &names)
{
    auto it = names.begin();
    bool deleted;
    while(it != names.end()) {
        deleted = false;
        std::string path = File::formFileName(container->path(), *it);
        if(Folder::isDir(path)) {
            if(container->type() == CAT_CONTAINER_ARCHIVE_DIR) { // Check if this is archive folder
                if(m_zipSupported) {
                    std::string vsiPath = Archive::pathPrefix(
                                CAT_CONTAINER_ARCHIVE_ZIP);
                    vsiPath += path;
                    addChild(container,
                             ObjectPtr(new ArchiveFolder(container, *it, vsiPath)));
                    it = names.erase(it);
                    deleted = true;
                }
            }
            else {
                addChild(container, ObjectPtr(new Folder(container, *it, path)));
                it = names.erase(it);
                deleted = true;
            }
        }
        else if(m_zipSupported) {
            if(compare(File::getExtension(*it),
                     Archive::extension(CAT_CONTAINER_ARCHIVE_ZIP))) { // Check if this is archive file
                CPLString vsiPath = Archive::pathPrefix(CAT_CONTAINER_ARCHIVE_ZIP);
                vsiPath += path;
                addChild(container,
                         ObjectPtr(new Archive(container,
                                               CAT_CONTAINER_ARCHIVE_ZIP,
                                               *it, vsiPath)));
                it = names.erase(it);
                deleted = true;
            }
        }

        if(!deleted) {
            ++it;
        }
    }
}

}
