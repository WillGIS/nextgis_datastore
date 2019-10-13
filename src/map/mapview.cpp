/******************************************************************************
 * Project:  libngstore
 * Purpose:  NextGIS store and visualisation support library
 * Author: Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
 * Author: NikitaFeodonit, nfeodonit@yandex.com
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
#include "mapview.h"

#include <algorithm>

#include "api_priv.h"
#include "catalog/folder.h"
#include "catalog/mapfile.h"
#include "ngstore/util/constants.h"
#include "util/versionutil.h"

namespace ngs {

constexpr const char *MAP_EXTENT_KEY = "extent";
constexpr const char *MAP_ROTATE_X_KEY = "rotate_x";
constexpr const char *MAP_ROTATE_Y_KEY = "rotate_y";
constexpr const char *MAP_ROTATE_Z_KEY = "rotate_z";
constexpr const char *MAP_X_LOOP_KEY = "x_looped";
constexpr const char *MAP_OVR_VISIBLE_KEY = "overlay_visible_mask";
constexpr const char *MAP_ICONS_KEY = "icon_sets";
constexpr const char *NAME_KEY = "name";
constexpr const char *PATH_KEY = "path";


MapView::MapView() : Map(),
    MapTransform(480, 640)
{
}

MapView::MapView(const std::string &name, const std::string &description,
                 unsigned short epsg, const Envelope &bounds) :
    Map(name, description, epsg, bounds),
    MapTransform(480, 640)
{
}

bool MapView::draw(ngsDrawState state, const Progress &progress)
{
    clearBackground();

    if(m_layers.empty()) {
        progress.onProgress(COD_FINISHED, 1.0,
                            _("No layers. Nothing to render."));
        return true;
    }

    // float level = 0.0f;
    // double done = 0.0;
    // for(auto it = m_layers.rbegin(); it != m_layers.rend(); ++it) {
        // LayerPtr layer = *it;
        // IRenderLayer * const renderLayer = ngsDynamicCast(IRenderLayer, layer);
        // done += renderLayer->draw(state, this, level++, progress);
    // }

    // for (auto it = m_overlays.rbegin(); it != m_overlays.rend(); ++it) {
        // OverlayPtr overlay = *it;
        // IOverlay * const iOverlay = ngsDynamicCast(IOverlay, overlay);
        // iOverlay->draw(state, this, level++, progress);
    // }

    // size_t size = m_layers.size();
    // if (isEqual(done, size)) {
        // progress.onProgress(COD_FINISHED, 1.0, _("Map render finished."));
    // } else {
        // progress.onProgress(COD_IN_PROCESS, done / (size), _("Rendering ..."));
    // }

    return true;
}

bool MapView::openInternal(const CPLJSONObject &root, MapFile * const mapFile)
{
    if(!Map::openInternal(root, mapFile))
        return false;

    setRotate(DIR_X, root.GetDouble(MAP_ROTATE_X_KEY, 0));
    setRotate(DIR_Y, root.GetDouble(MAP_ROTATE_Y_KEY, 0));
    setRotate(DIR_Z, root.GetDouble(MAP_ROTATE_Z_KEY, 0));

    Envelope env;
    env.load(root.GetObj(MAP_EXTENT_KEY), DEFAULT_BOUNDS);
    setExtent(env);

    m_XAxisLooped = root.GetBool(MAP_X_LOOP_KEY, true);

    int overlayVisibleMask = root.GetInteger(MAP_OVR_VISIBLE_KEY, 0);

    setOverlayVisible(overlayVisibleMask, true);

    // TODO: Need to load some default iconset from library share folder.
    // One or more 256 x 256 rasters with 32 x 32 or 16  x 16 icons (markers).

    CPLJSONArray iconSets = root.GetArray(MAP_ICONS_KEY);
    for(int i = 0; i < iconSets.Size(); ++i) {
        CPLJSONObject iconSetJsonItem = iconSets[i];
        std::string path = iconSetJsonItem.GetString(PATH_KEY, "");

        if(startsWith(path, "/resources/icons/")) {
            CPLString mapPath("/vsizip/");
            mapPath += mapFile->path();
            mapPath += path;
            if(Folder::isExists(mapPath)) {
                m_iconSets.push_back({iconSetJsonItem.GetString(NAME_KEY, "untitled"), mapPath, true});
            }
        }
        else {
            if(Folder::isExists(path)) {
                m_iconSets.push_back({iconSetJsonItem.GetString(NAME_KEY, "untitled"), path, false});
            }
        }
    }

    return true;
}

bool MapView::saveInternal(CPLJSONObject &root, MapFile * const mapFile)
{
    if(!Map::saveInternal(root, mapFile))
        return false;

    root.Add(MAP_EXTENT_KEY, getExtent().save());
    root.Add(MAP_ROTATE_X_KEY, getRotate(DIR_X));
    root.Add(MAP_ROTATE_Y_KEY, getRotate(DIR_Y));
    root.Add(MAP_ROTATE_Z_KEY, getRotate(DIR_Z));

    root.Add(MAP_X_LOOP_KEY, m_XAxisLooped);

    root.Add(MAP_OVR_VISIBLE_KEY, overlayVisibleMask());

    std::string tmpPath = mapFile->path();
    bool copyFromOrigin = false;
    if(Folder::isExists(mapFile->path())) {
        tmpPath = mapFile->path() + "~.zip";
        if(!File::moveFile(mapFile->path(), tmpPath)) {
            return false;
        }
        copyFromOrigin = true;
    }

    CPLJSONArray iconSets;
    for(const IconSetItem& item : m_iconSets) {
        CPLJSONObject iconSetJson;
        iconSetJson.Add(NAME_KEY, item.name);
        if(item.ownByMap) {
            if(!startsWith(item.path, "/vsizip/")) {
                // Copy to map document
                std::string iconSetPath("/resources/icons/");
                iconSetPath += item.name;
                iconSetPath += "." + File::getExtension(item.path);
                std::string mapPath("/vsizip/");
                mapPath += mapFile->path();
                mapPath += iconSetPath;
                File::copyFile(item.path, mapPath);
                iconSetJson.Add(PATH_KEY, iconSetPath);
            }
            else {
                std::string iconSetPath("/resources/icons/");
                iconSetPath += File::getFileName(item.path);
                iconSetJson.Add(PATH_KEY, iconSetPath);
            }
        }
        else {
            iconSetJson.Add(PATH_KEY, item.path);
        }

        iconSets.Add(iconSetJson);
    }

    if(copyFromOrigin) {
//        CPLString memo("/vsizip/");
//        memo += CPLFormFilename(mapFile->path(), "memo", nullptr);
//        VSILFILE *fp = VSIFOpenL( memo, "wt" );
//        const char* memoData = CPLSPrintf("Create by NextGIS map library %s", getVersionString("self"));
//        VSIFWriteL(memoData, 1, strlen(memoData), fp);
//        VSIFCloseL(fp);

        std::string icons = "/vsizip/" + tmpPath + "/resources/icons/";
        std::string newIcons = "/vsizip/" + mapFile->path() + "/resources/icons/";
        Folder::copyDir(icons, newIcons);
        File::deleteFile(tmpPath);
    }

    root.Add(MAP_ICONS_KEY, iconSets);

    return true;
}

size_t MapView::overlayIndexForType(enum ngsMapOverlayType type) const
{
    // Overlays stored in reverse order
    switch(type) {
        case MOT_FIGURES:
            return 0;
        case MOT_EDIT:
            return 1;
        case MOT_TRACK:
            return 2;
        case MOT_LOCATION:
            return 3;
        default:
            return m_overlays.size();
    }
}

OverlayPtr MapView::getOverlay(enum ngsMapOverlayType type) const
{
    size_t index = overlayIndexForType(type);
    if (m_overlays.size() == index)
        return OverlayPtr();

    return m_overlays[index];
}

void MapView::setOverlayVisible(int typeMask, bool visible)
{
    OverlayPtr overlay;

    if (MOT_LOCATION & typeMask) {
        overlay = getOverlay(MOT_LOCATION);
        if(overlay) {
            overlay->setVisible(visible);
        }
    }
    if (MOT_TRACK & typeMask) {
        overlay = getOverlay(MOT_TRACK);
        if(overlay) {
            overlay->setVisible(visible);
        }
    }
    if (MOT_EDIT & typeMask) {
        overlay = getOverlay(MOT_EDIT);
        if(overlay) {
            overlay->setVisible(visible);
        }
    }
    if (MOT_FIGURES & typeMask) {
        overlay = getOverlay(MOT_FIGURES);
        if(overlay) {
            overlay->setVisible(visible);
        }
    }
}

int MapView::overlayVisibleMask() const
{
    int mask = 0;
    OverlayPtr overlay = getOverlay(MOT_LOCATION);
    if(overlay && overlay->visible()) {
        mask |= MOT_LOCATION;
    }

    overlay = getOverlay(MOT_EDIT);
    if(overlay && overlay->visible()) {
        mask |= MOT_EDIT;
    }

    overlay = getOverlay(MOT_FIGURES);
    if(overlay && overlay->visible()) {
        mask |= MOT_FIGURES;
    }

    overlay = getOverlay(MOT_TRACK);
    if(overlay && overlay->visible()) {
        mask |= MOT_TRACK;
    }

    return mask;
}

bool MapView::setOptions(const Options &options)
{
    double reduceFactor = options.asDouble("VIEWPORT_REDUCE_FACTOR", 1.0);
    setReduceFactor(reduceFactor);

    char zoomIncrement = static_cast<char>(options.asInt("ZOOM_INCREMENT", 0));
    setZoomIncrement(zoomIncrement);
    return true;
}

bool MapView::addIconSet(const std::string &name, const std::string &path,
                         bool ownByMap)
{
    if(hasIconSet(name)) {
        return false;
    }
    m_iconSets.push_back({name, path, ownByMap});
    return true;
}

bool MapView::removeIconSet(const std::string &name)
{
    IconSetItem item = {name, "", false};
    auto it = std::find(m_iconSets.begin(), m_iconSets.end(), item);
    if(it == m_iconSets.end()) {
        return false;
    }

    if((*it).ownByMap) {
        if(!File::deleteFile((*it).path)) {
            return false;
        }
    }
    m_iconSets.erase(it);
    return true;
}

ImageData MapView::iconSet(const std::string &name) const
{
    IconSetItem item = {name, "", false};
    auto it = std::find(m_iconSets.begin(), m_iconSets.end(), item);
    if(it == m_iconSets.end()) {
        return {nullptr, 0, 0};
    }
    return iconSetData((*it).path);
}

ImageData MapView::iconSetData(const std::string &path) const
{
    GDALDataset *dataset = static_cast<GDALDataset*>(GDALOpen(path.c_str(),
                                                              GA_ReadOnly));
    if(nullptr == dataset) {
        return {nullptr, 0, 0};
    }
    int xSize = dataset->GetRasterXSize();
    int ySize = dataset->GetRasterYSize();
    size_t bufferSize = static_cast<size_t>(xSize * ySize * 4);
    ImageData out;
    out.height = xSize;
    out.width = ySize;
    out.buffer = static_cast<unsigned char*>(CPLMalloc(bufferSize));

    int badndList[4] = {1,2,3,4};
    if(dataset->RasterIO(GF_Read, 0, 0, xSize, ySize, out.buffer, xSize, ySize,
                         GDT_Byte, 4, badndList, 4, xSize * 4, 1) != CE_None) {
        CPLFree(out.buffer);
        return {nullptr, 0, 0};
    }

    return out;
}

bool MapView::hasIconSet(const std::string &name) const
{
    IconSetItem item = {name, "", false};
    return std::find(m_iconSets.begin(), m_iconSets.end(), item) != m_iconSets.end();
}

} // namespace ngs
