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

#include <cstring>
#include <math.h>

#include "cpl_conv.h"
#include "ogr_core.h"

#include "layer.h"
#include "style.h"
#include "view.h"
#include "util/global.h"
#include "util/error.h"

namespace ngs {

constexpr unsigned char MAX_ZOOM = 18;

//------------------------------------------------------------------------------
// IGlRenderLayer
//------------------------------------------------------------------------------

GlRenderLayer::GlRenderLayer()
{
}

GlRenderLayer::~GlRenderLayer()
{
}

void GlRenderLayer::free(const GlTilePtr &tile)
{
    double lockTime = CPLAtofM(CPLGetConfigOption("HTTP_TIMEOUT", "5"));
    MutexHolder holder(m_dataMutex, lockTime);
    auto it = m_tiles.find(tile->getTile());
    if(it != m_tiles.end()) {
        if(it->second) {
            it->second->destroy();
        }
        m_tiles.erase(it);
    }

    for(const StylePtr &style : m_oldStyles) {
        style->destroy();
    }
    m_oldStyles.clear();
    // CPLDebug("ngstore", "GlRenderLayer::free: %ld GlObject in layer", m_tiles.size());
}

//------------------------------------------------------------------------------
// GlFeatureLayer
//------------------------------------------------------------------------------

GlFeatureLayer::GlFeatureLayer(Map *map, const std::string &name) :
    FeatureLayer(map, name),
    GlRenderLayer()
{
}

bool GlFeatureLayer::fill(const GlTilePtr &tile, float z, bool isLastTry)
{
    ngsUnused(isLastTry);
    double lockTime = CPLAtofM(CPLGetConfigOption("HTTP_TIMEOUT", "5"));
    if(!m_visible) {
        MutexHolder holder(m_dataMutex, lockTime);
        m_tiles[tile->getTile()] = GlObjectPtr();
        return true;
    }

    VectorGlObject *bufferArray = nullptr;
    VectorTile vtile = m_featureClass->getTile(tile->getTile(), tile->getExtent());
    if(vtile.empty()) {
        MutexHolder holder(m_dataMutex, lockTime);
        m_tiles[tile->getTile()] = GlObjectPtr();
        return true;
    }

    switch(m_style->type()) {
    case ST_POINT:
        bufferArray = fillPoints(vtile, z);
        break;
    case ST_LINE:
        bufferArray = fillLines(vtile, z);
        break;
    case ST_FILL:
        bufferArray = fillPolygons(vtile, z);
        break;
    case ST_IMAGE:
        return true;
    }

    if(!bufferArray) {
        MutexHolder holder(m_dataMutex, lockTime);
        m_tiles[tile->getTile()] = GlObjectPtr();
        return true;
    }

    MutexHolder holder(m_dataMutex, lockTime);
    m_tiles[tile->getTile()] = GlObjectPtr(bufferArray);

    return true;
}

bool GlFeatureLayer::draw(const GlTilePtr &tile)
{
    if(!tile) {
        return true;
    }
    if(!m_style) {
        return true; // Should never happened
    }

    MutexHolder holder(m_dataMutex, 5);
    auto tileDataIt = m_tiles.find(tile->getTile());
    if(tileDataIt == m_tiles.end()) {
        return false; // Data not yet loaded
    }
    else if(!tileDataIt->second) {
        return true; // Out of tile extent
    }

    VectorGlObject *vectorGlObject = ngsDynamicCast(VectorGlObject,
                                                    tileDataIt->second);
    for(const GlBufferPtr& buff : vectorGlObject->buffers()) {

        if(buff->bound()) {
            buff->rebind();
        }
        else {
            buff->bind();
        }

        m_style->prepare(tile->getSceneMatrix(), tile->getInvViewMatrix(),
                         buff->type());
        m_style->draw(*buff);
    }
    return true;
}

void GlFeatureLayer::setStyle(const std::string &name)
{
    if(compare(name, m_style->name())) {
        return;
    }

    GlView *mapView = dynamic_cast<GlView*>(m_map);
    StylePtr newStyle = StylePtr(Style::createStyle(name, mapView->textureAtlas()));
    if(newStyle) {
        m_oldStyles.push_back(m_style);
        m_style = newStyle;
    }
}

bool GlFeatureLayer::load(const CPLJSONObject &store,
                          ObjectContainer *objectContainer)
{
    bool result = FeatureLayer::load(store, objectContainer);
    if(!result) {
        return false;
    }
    std::string styleName = store.GetString("style_name", "");
    if(!styleName.empty()) {
        GlView *mapView = dynamic_cast<GlView*>(m_map);
        m_style = StylePtr(Style::createStyle(styleName, mapView->textureAtlas()));
        return m_style->load(store.GetObj("style"));
    }
    return true;
}

CPLJSONObject GlFeatureLayer::save(const ObjectContainer *objectContainer) const
{
    CPLJSONObject out = FeatureLayer::save(objectContainer);
    if(m_style) {
        out.Add("style_name", m_style->name());
        out.Add("style", m_style->save());
    }
    return out;
}

void GlFeatureLayer::setFeatureClass(const FeatureClassPtr &featureClass)
{
    FeatureLayer::setFeatureClass(featureClass);
    GlView *mapView = dynamic_cast<GlView*>(m_map);
    switch(OGR_GT_Flatten(featureClass->geometryType())) {
    case wkbPoint:
    case wkbMultiPoint:
        m_style = StylePtr(Style::createStyle("primitivePoint", mapView->textureAtlas()));
        break;
    case wkbLineString:
    case wkbMultiLineString:
        m_style = StylePtr(Style::createStyle("simpleLine", mapView->textureAtlas()));
        break;
    case wkbPolygon:
    case wkbMultiPolygon:
        m_style = StylePtr(Style::createStyle("simpleFillBordered", mapView->textureAtlas()));
        break;
    }
}

VectorGlObject *GlFeatureLayer::fillPoints(const VectorTile &tile, float z)
{
    VectorGlObject *bufferArray = new VectorGlObject;
    auto items = tile.items();
    auto it = items.begin();
    unsigned short index = 0;
    GlBuffer *buffer = new GlBuffer(GlBuffer::BF_PT);
    PointStyle *style = ngsDynamicCast(PointStyle, m_style);
    while(it != items.end()) {
        VectorTileItem tileItem = *it;
        if(!m_hideFIDs.empty() && tileItem.isIdsPresent(m_hideFIDs)) {
            ++it;
            continue;
        }

        if(tileItem.pointCount() < 1) {
            ++it;
            continue;
        }

        for(size_t i = 0; i < tileItem.pointCount(); ++i) {
            if(!buffer->canStoreVertices(style->pointVerticesCount(), true)) {
                bufferArray->addBuffer(buffer);
                index = 0;
                buffer = new GlBuffer(GlBuffer::BF_PT);
            }

            const SimplePoint& pt = tileItem.point(i);
            index = style->addPoint(pt, z, index, buffer);
        }
        ++it;
    }

    bufferArray->addBuffer(buffer);

    return bufferArray;
}

VectorGlObject *GlFeatureLayer::fillLines(const VectorTile &tile, float z)
{
    VectorGlObject *bufferArray = new VectorGlObject;
    auto items = tile.items();
    auto it = items.begin();
    unsigned short index = 0;
    GlBuffer *buffer = new GlBuffer(GlBuffer::BF_LINE);
    SimpleLineStyle *style = ngsStaticCast(SimpleLineStyle, m_style);

    while(it != items.end()) {
        VectorTileItem tileItem = *it;
        if(tileItem.isIdsPresent(m_hideFIDs)) {
            ++it;
            continue;
        }

        if(tileItem.pointCount() < 2) {
            ++it;
            continue;
        }

        // Check if line is closed or not
        bool closed = tileItem.isClosed();

        Normal prevNormal;
        for(size_t i = 0; i < tileItem.pointCount() - 1; ++i) {
            const SimplePoint& pt1 = tileItem.point(i);
            const SimplePoint& pt2 = tileItem.point(i + 1);
            Normal normal = ngsGetNormals(pt1, pt2);

            if(i == 0 || i == tileItem.pointCount() - 2) { // Add cap
                if(!closed) {
                    if(i == 0) {
                        if(!buffer->canStoreVertices(style->lineCapVerticesCount(),
                                                     true)) {
                            bufferArray->addBuffer(buffer);
                            index = 0;
                            buffer = new GlBuffer(GlBuffer::BF_LINE);
                        }
                        index = style->addLineCap(pt1, normal, z, index, buffer);
                    }

                    if(i == tileItem.pointCount() - 2) {
                        if(!buffer->canStoreVertices(style->lineCapVerticesCount(),
                                                     true)) {
                            bufferArray->addBuffer(buffer);
                            index = 0;
                            buffer = new GlBuffer(GlBuffer::BF_LINE);
                        }

                        Normal reverseNormal;
                        reverseNormal.x = -normal.x;
                        reverseNormal.y = -normal.y;
                        index = style->addLineCap(pt2, reverseNormal, z, index, buffer);
                    }
                }
            }

            if(i != 0) { // Add join
                if(!buffer->canStoreVertices(style->lineJoinVerticesCount(),
                                             true)) {
                    bufferArray->addBuffer(buffer);
                    index = 0;
                    buffer = new GlBuffer(GlBuffer::BF_LINE);
                }
                index = style->addLineJoin(pt1, prevNormal, normal, z, index,
                                           buffer);
            }

            if(!buffer->canStoreVertices(12, true)) {
                bufferArray->addBuffer(buffer);
                index = 0;
                buffer = new GlBuffer(GlBuffer::BF_LINE);
            }

            index = style->addSegment(pt1, pt2, normal, z, index, buffer);
            prevNormal = normal;
        }
        ++it;
    }
    bufferArray->addBuffer(buffer);

    return bufferArray;
}

VectorGlObject *GlFeatureLayer::fillPolygons(const VectorTile &tile, float z)
{
    VectorGlObject *bufferArray = new VectorGlObject;
    auto items = tile.items();
    auto it = items.begin();
    unsigned short fillIndex = 0;
    unsigned short lineIndex = 0;
    GlBuffer *fillBuffer = new GlBuffer(GlBuffer::BF_FILL);
    GlBuffer *lineBuffer = new GlBuffer(GlBuffer::BF_LINE);
    SimpleLineStyle *style = ngsStaticCast(SimpleLineStyle, m_style);

    while(it != items.end()) {
        VectorTileItem tileItem = *it;
        if(tileItem.isIdsPresent(m_hideFIDs)) {
            ++it;
            continue;
        }

        auto points = tileItem.points();
        auto indices = tileItem.indices();

        if(points.size() < 3 || points.size() > GlBuffer::maxIndices() ||
                points.size() > GlBuffer::maxVertices()) {
            ++it;
            continue;
        }

        // Fill polygons
        if(!fillBuffer->canStoreVertices(points.size() * 3, false)) {
            bufferArray->addBuffer(fillBuffer);
            fillIndex = 0;
            fillBuffer = new GlBuffer(GlBuffer::BF_FILL);
        }

        for(auto point : points) {
            fillBuffer->addVertex(point.x);
            fillBuffer->addVertex(point.y);
            fillBuffer->addVertex(z);
        }

        // FIXME: Expected indices should fit to buffer as points can
        unsigned short maxFillIndex = 0;
        for(auto indexPoint : indices) {
            fillBuffer->addIndex(fillIndex + indexPoint);
            if(maxFillIndex < indexPoint) {
                maxFillIndex = indexPoint;
            }
        }
        fillIndex += maxFillIndex;


        // Fill borders
        // FIXME: May be more styles with borders
        if(compare(m_style->name(), "simpleFillBordered")) {

        auto borders = (*it).borderIndices();
        for(auto border : borders) {
            Normal prevNormal;
            Normal firstNormal;
            bool firstNormalSet = false;
            for(size_t i = 0; i < border.size() - 1; ++i) {
                auto borderIndex = border[i];
                auto borderIndex1 = border[i + 1];
                Normal normal = ngsGetNormals(points[borderIndex],
                                              points[borderIndex1]);

                if(i == border.size() - 2) {
                    if(!lineBuffer->canStoreVertices(style->lineCapVerticesCount(),
                                                     true)) {
                        bufferArray->addBuffer(lineBuffer);
                        lineIndex = 0;
                        lineBuffer = new GlBuffer(GlBuffer::BF_LINE);
                    }

                    Normal reverseNormal;
                    reverseNormal.x = -normal.x;
                    reverseNormal.y = -normal.y;
                    lineIndex = style->addLineJoin(points[borderIndex1],
                           firstNormal, reverseNormal, z, lineIndex, lineBuffer);
                }

                if(i != 0) {
                    if(!lineBuffer->canStoreVertices(style->lineJoinVerticesCount(),
                                                     true)) {
                        bufferArray->addBuffer(lineBuffer);
                        lineIndex = 0;
                        lineBuffer = new GlBuffer(GlBuffer::BF_LINE);
                    }
                    lineIndex = style->addLineJoin(points[borderIndex],
                                   prevNormal, normal, z, lineIndex, lineBuffer);
                }

                if(!lineBuffer->canStoreVertices(12, true)) {
                    bufferArray->addBuffer(lineBuffer);
                    lineIndex = 0;
                    lineBuffer = new GlBuffer(GlBuffer::BF_LINE);
                }

                lineIndex = style->addSegment(points[borderIndex],
                                              points[borderIndex1], normal, z,
                                              lineIndex, lineBuffer);

                prevNormal = normal;
                if(!firstNormalSet) {
                    firstNormal.x = -prevNormal.x;
                    firstNormal.y = -prevNormal.y;
                    firstNormalSet = true;
                }
            }
        }
        }

        ++it;
    }

    bufferArray->addBuffer(fillBuffer);
    bufferArray->addBuffer(lineBuffer);

    return bufferArray;
}

//------------------------------------------------------------------------------
// GlSelectableFeatureLayer
//------------------------------------------------------------------------------

GlSelectableFeatureLayer::GlSelectableFeatureLayer(Map *map,
                                                   const std::string &name) :
    GlFeatureLayer(map, name)
{
    GlView *mapView = dynamic_cast<GlView*>(map);
    if(mapView) {
        m_selectionStyles = mapView->selectionStyles();
    }
}

StylePtr GlSelectableFeatureLayer::selectionStyle() const
{
    if(!m_style) {
        return StylePtr();
    }

    return m_selectionStyles.find(m_style->type())->second;
}

bool GlSelectableFeatureLayer::drawSelection(const GlTilePtr &tile)
{
    if(!tile) {
        return true;
    }
    StylePtr style = selectionStyle();
    if(!style) {
        return true; // Not draw selected features if no style provided
    }

    MutexHolder holder(m_dataMutex, 5);
    auto tileDataIt = m_tiles.find(tile->getTile());
    if(tileDataIt == m_tiles.end()) {
        return false; // Data not yet loaded
    }
    else if(!tileDataIt->second) {
        return true; // Out of tile extent
    }

    VectorSelectableGlObject *vectorGlObject =
            ngsDynamicCast(VectorSelectableGlObject, tileDataIt->second);
    for(const GlBufferPtr& buff : vectorGlObject->selectionBuffers()) {
        if(buff->indexSize() == 0) {
            continue;
        }
        if(buff->bound()) {
            buff->rebind();
        }
        else {
            buff->bind();
        }

        style->prepare(tile->getSceneMatrix(), tile->getInvViewMatrix(),
                       buff->type());
        style->draw(*buff);
    }
    return true;
}

VectorGlObject* GlSelectableFeatureLayer::fillPoints(const VectorTile &tile,
                                                     float z)
{
    VectorSelectableGlObject *bufferArray = new VectorSelectableGlObject;
    auto items = tile.items();
    auto it = items.begin();
    unsigned short index = 0;
    GlBuffer *buffer = nullptr;
    PointStyle *style = nullptr;

    PointStyle *drawStyle = ngsDynamicCast(PointStyle, m_style);
    PointStyle *selectStyle = ngsDynamicCast(PointStyle, selectionStyle());
    GlBuffer *draw = new GlBuffer(drawStyle->bufferType());
    GlBuffer *select = new GlBuffer(selectStyle->bufferType());
    unsigned short drawIndex = 0;
    unsigned short selectIndex = 0;

    while(it != items.end()) {
        VectorTileItem tileItem = *it;
        if(tileItem.isIdsPresent(m_hideFIDs, true)) {
            ++it;
            continue;
        }

        if(tileItem.pointCount() < 1) {
            ++it;
            continue;
        }

        bool isSelect = false;
        if(tileItem.isIdsPresent(m_selectedFIDs, false)) {
            buffer = select;
            style = selectStyle;
            index = selectIndex;
            isSelect = true;
        }
        else {
            buffer = draw;
            style = drawStyle;
            index = drawIndex;
        }

        for(size_t i = 0; i < tileItem.pointCount(); ++i) {
            if(buffer->vertexSize() >= GlBuffer::maxVertices() - 7) {
                if(isSelect) {
                    bufferArray->addSelectionBuffer(select);
                    select = new GlBuffer(style->bufferType());
                    buffer = select;
                    selectIndex = 0;
                }
                else {
                    bufferArray->addBuffer(buffer);
                    draw = new GlBuffer(style->bufferType());
                    buffer = draw;
                    drawIndex = 0;
                }
                index = 0;
            }

            const SimplePoint &pt = tileItem.point(i);
            index = style->addPoint(pt, z, index, buffer);
        }
        ++it;

        if(isSelect) {
            selectIndex = index;
        }
        else {
            drawIndex = index;
        }
    }

    bufferArray->addBuffer(draw);
    bufferArray->addSelectionBuffer(select);

    return bufferArray;
}

VectorGlObject *GlSelectableFeatureLayer::fillLines(const VectorTile &tile,
                                                    float z)
{
    VectorSelectableGlObject *bufferArray = new VectorSelectableGlObject;
    auto items = tile.items();
    auto it = items.begin();
    unsigned short index = 0;
    GlBuffer *buffer = nullptr;
    GlBuffer *draw = new GlBuffer(GlBuffer::BF_LINE);
    GlBuffer *select = new GlBuffer(GlBuffer::BF_LINE);
    SimpleLineStyle *drawStyle = ngsDynamicCast(SimpleLineStyle, m_style);
    SimpleLineStyle *selectStyle = ngsDynamicCast(SimpleLineStyle, selectionStyle());
    SimpleLineStyle *style = nullptr;
    unsigned short drawIndex = 0;
    unsigned short selectIndex = 0;

    while(it != items.end()) {
        VectorTileItem tileItem = *it;
        if(tileItem.isIdsPresent(m_hideFIDs)) {
            ++it;
            continue;
        }

        if(tileItem.pointCount() < 2) {
            ++it;
            continue;
        }

        bool isSelect = false;
        if(tileItem.isIdsPresent(m_selectedFIDs, false)) {
            buffer = select;
            style = selectStyle;
            index = selectIndex;
            isSelect = true;
        }
        else {
            buffer = draw;
            style = drawStyle;
            index = drawIndex;
        }

        // Check if line is closed or not
        bool closed = tileItem.isClosed();

        Normal prevNormal;
        for(size_t i = 0; i < tileItem.pointCount() - 1; ++i) {
            const SimplePoint& pt1 = tileItem.point(i);
            const SimplePoint& pt2 = tileItem.point(i + 1);
            Normal normal = ngsGetNormals(pt1, pt2);

            if(i == 0 || i == tileItem.pointCount() - 2) { // Add cap
                if(!closed) {
                    if(i == 0) {
                        if(!buffer->canStoreVertices(style->lineCapVerticesCount(),
                                                     true)) {
                            if(isSelect) {
                                bufferArray->addSelectionBuffer(buffer);
                                select = new GlBuffer(GlBuffer::BF_LINE);
                                buffer = select;
                                selectIndex = 0;
                            }
                            else {
                                bufferArray->addBuffer(buffer);
                                draw = new GlBuffer(GlBuffer::BF_LINE);
                                buffer = draw;
                                drawIndex = 0;
                            }
                            index = 0;
                        }
                        index = style->addLineCap(pt1, normal, z, index, buffer);
                    }

                    if(i == tileItem.pointCount() - 2) {
                        if(!buffer->canStoreVertices(style->lineCapVerticesCount(),
                                                     true)) {
                            if(isSelect) {
                                bufferArray->addSelectionBuffer(buffer);
                                select = new GlBuffer(GlBuffer::BF_LINE);
                                buffer = select;
                                selectIndex = 0;
                            }
                            else {
                                bufferArray->addBuffer(buffer);
                                draw = new GlBuffer(GlBuffer::BF_LINE);
                                buffer = draw;
                                drawIndex = 0;
                            }
                            index = 0;
                        }

                        Normal reverseNormal;
                        reverseNormal.x = -normal.x;
                        reverseNormal.y = -normal.y;
                        index = style->addLineCap(pt2, reverseNormal, z, index,
                                                  buffer);
                    }
                }
            }

            if(i != 0) { // Add join
                if(!buffer->canStoreVertices(style->lineJoinVerticesCount(),
                                             true)) {
                    if(isSelect) {
                        bufferArray->addSelectionBuffer(buffer);
                        select = new GlBuffer(GlBuffer::BF_LINE);
                        buffer = select;
                        selectIndex = 0;
                    }
                    else {
                        bufferArray->addBuffer(buffer);
                        draw = new GlBuffer(GlBuffer::BF_LINE);
                        buffer = draw;
                        drawIndex = 0;
                    }
                    index = 0;
                }
                index = style->addLineJoin(pt1, prevNormal, normal, z, index,
                                           buffer);
            }

            if(!buffer->canStoreVertices(12, true)) {
                if(isSelect) {
                    bufferArray->addSelectionBuffer(buffer);
                    select = new GlBuffer(GlBuffer::BF_LINE);
                    buffer = select;
                    selectIndex = 0;
                }
                else {
                    bufferArray->addBuffer(buffer);
                    draw = new GlBuffer(GlBuffer::BF_LINE);
                    buffer = draw;
                    drawIndex = 0;
                }
                index = 0;
            }

            index = style->addSegment(pt1, pt2, normal, z, index, buffer);
            prevNormal = normal;
        }
        ++it;

        if(isSelect) {
            selectIndex = index;
        }
        else {
            drawIndex = index;
        }
    }
    bufferArray->addBuffer(draw);
    bufferArray->addSelectionBuffer(select);

    return bufferArray;
}

VectorGlObject *GlSelectableFeatureLayer::fillPolygons(const VectorTile& tile,
                                                       float z)
{
    VectorSelectableGlObject *bufferArray = new VectorSelectableGlObject;
    auto items = tile.items();
    auto it = items.begin();
    unsigned short fillIndex = 0;
    unsigned short lineIndex = 0;
    GlBuffer *drawFillBuffer = new GlBuffer(GlBuffer::BF_FILL);
    GlBuffer *drawLineBuffer = new GlBuffer(GlBuffer::BF_LINE);
    GlBuffer *selectFillBuffer = new GlBuffer(GlBuffer::BF_FILL);
    GlBuffer *selectLineBuffer = new GlBuffer(GlBuffer::BF_LINE);
    GlBuffer *fillBuffer = nullptr;
    GlBuffer *lineBuffer = nullptr;

    SimpleFillBorderedStyle *drawStyle = ngsDynamicCast(SimpleFillBorderedStyle,
                                                        m_style);
    SimpleLineStyle *drawLineStyle = drawStyle->lineStyle();

    SimpleFillBorderedStyle *selectStyle = ngsDynamicCast(SimpleFillBorderedStyle,
                                                          selectionStyle());
    SimpleLineStyle *selectLineStyle = selectStyle->lineStyle();

    SimpleFillBorderedStyle *style;
    SimpleLineStyle *lineStyle;

    unsigned short selectFillIndex = 0;
    unsigned short selectLineIndex = 0;
    unsigned short drawFillIndex = 0;
    unsigned short drawLineIndex = 0;

    while(it != items.end()) {
        VectorTileItem tileItem = *it;
        if(tileItem.isIdsPresent(m_hideFIDs)) {
            ++it;
            continue;
        }

        auto points = tileItem.points();
        auto indices = tileItem.indices();

        if(points.size() < 3 || points.size() > GlBuffer::maxIndices() ||
                points.size() > GlBuffer::maxVertices()) {
            ++it;
            continue;
        }

        bool isSelect = false;
        if(tileItem.isIdsPresent(m_selectedFIDs, false)) {
            fillBuffer = selectFillBuffer;
            lineBuffer = selectLineBuffer;
            style = selectStyle;
            lineStyle = selectLineStyle;
            isSelect = true;
            fillIndex = selectFillIndex;
            lineIndex = selectLineIndex;
        }
        else {
            fillBuffer = drawFillBuffer;
            lineBuffer = drawLineBuffer;
            style = drawStyle;
            lineStyle = drawLineStyle;
            fillIndex = drawFillIndex;
            lineIndex = drawLineIndex;
        }

        // Fill polygons
        if(!fillBuffer->canStoreVertices(points.size() * 3, false)) {
            fillIndex = 0;
            if(isSelect) {
                bufferArray->addSelectionBuffer(fillBuffer);
                selectFillBuffer = new GlBuffer(GlBuffer::BF_FILL);
                fillBuffer = selectFillBuffer;
                selectFillIndex = 0;
            }
            else {
                bufferArray->addBuffer(fillBuffer);
                drawFillBuffer = new GlBuffer(GlBuffer::BF_FILL);
                fillBuffer = drawFillBuffer;
                drawFillIndex = 0;
            }
        }

        for(auto point : points) {
            fillBuffer->addVertex(point.x);
            fillBuffer->addVertex(point.y);
            fillBuffer->addVertex(z);
        }

        // FIXME: Expected indices should fit to buffer as points can
//        unsigned short maxFillIndex = 0;
        for(auto indexPoint : indices) {
            ngsUnused(indexPoint);
            fillBuffer->addIndex(fillIndex++);
//            fillBuffer->addIndex(fillIndex + indexPoint);
//            if(maxFillIndex < indexPoint) {
//                maxFillIndex = indexPoint;
//            }
        }
//        fillIndex += maxFillIndex;


        // Fill borders
        // FIXME: May be more styles with borders
        if(compare(style->name(), "simpleFillBordered")) {

        auto borders = (*it).borderIndices();
        for(auto border : borders) {
            Normal prevNormal;
            Normal firstNormal;
            bool firstNormalSet = false;
            for(size_t i = 0; i < border.size() - 1; ++i) {
                auto borderIndex = border[i];
                auto borderIndex1 = border[i + 1];
                Normal normal = ngsGetNormals(points[borderIndex],
                                              points[borderIndex1]);

                if(i == border.size() - 2) {
                    if(!lineBuffer->canStoreVertices(lineStyle->lineCapVerticesCount(),
                                                     true)) {
                        lineIndex = 0;
                        if(isSelect) {
                            bufferArray->addSelectionBuffer(lineBuffer);
                            selectLineBuffer = new GlBuffer(GlBuffer::BF_LINE);
                            lineBuffer = selectLineBuffer;
                            selectLineIndex = 0;
                        }
                        else {
                            bufferArray->addBuffer(lineBuffer);
                            drawLineBuffer = new GlBuffer(GlBuffer::BF_LINE);
                            lineBuffer = drawLineBuffer;
                            drawLineIndex = 0;
                        }
                    }

                    Normal reverseNormal;
                    reverseNormal.x = -normal.x;
                    reverseNormal.y = -normal.y;
                    lineIndex = lineStyle->addLineJoin(points[borderIndex1],
                           firstNormal, reverseNormal, z, lineIndex, lineBuffer);
                }

                if(i != 0) {
                    if(!lineBuffer->canStoreVertices(lineStyle->lineJoinVerticesCount(),
                                                     true)) {
                        lineIndex = 0;
                        if(isSelect) {
                            bufferArray->addSelectionBuffer(lineBuffer);
                            selectLineBuffer = new GlBuffer(GlBuffer::BF_LINE);
                            lineBuffer = selectLineBuffer;
                            selectLineIndex = 0;
                        }
                        else {
                            bufferArray->addBuffer(lineBuffer);
                            drawLineBuffer = new GlBuffer(GlBuffer::BF_LINE);
                            lineBuffer = drawLineBuffer;
                            drawLineIndex = 0;
                        }
                    }
                    lineIndex = lineStyle->addLineJoin(points[borderIndex],
                                   prevNormal, normal, z, lineIndex, lineBuffer);
                }

                if(!lineBuffer->canStoreVertices(12, true)) {
                    lineIndex = 0;
                    if(isSelect) {
                        bufferArray->addSelectionBuffer(lineBuffer);
                        selectLineBuffer = new GlBuffer(GlBuffer::BF_LINE);
                        lineBuffer = selectLineBuffer;
                        selectLineIndex = 0;
                    }
                    else {
                        bufferArray->addBuffer(lineBuffer);
                        drawLineBuffer = new GlBuffer(GlBuffer::BF_LINE);
                        lineBuffer = drawLineBuffer;
                        drawLineIndex = 0;
                    }
                }

                lineIndex = lineStyle->addSegment(points[borderIndex],
                                              points[borderIndex1], normal,
                                              z, lineIndex, lineBuffer);

                prevNormal = normal;
                if(!firstNormalSet) {
                    firstNormal.x = -prevNormal.x;
                    firstNormal.y = -prevNormal.y;
                    firstNormalSet = true;
                }
            }
        }
        }

        ++it;

        if(isSelect) {
            selectLineIndex = lineIndex;
            selectFillIndex = fillIndex;
        }
        else {
            drawLineIndex = lineIndex;
            drawFillIndex = fillIndex;
        }

        z += 2.0f;
    }

    bufferArray->addBuffer(drawFillBuffer);
    bufferArray->addBuffer(drawLineBuffer);
    bufferArray->addSelectionBuffer(selectFillBuffer);
    bufferArray->addSelectionBuffer(selectLineBuffer);

    return bufferArray;
}

//------------------------------------------------------------------------------
// GlRasterLayer
//------------------------------------------------------------------------------

GlRasterLayer::GlRasterLayer(Map *map, const std::string &name) :
    RasterLayer(map, name),
    GlRenderLayer(),
    m_red(1),
    m_green(2),
    m_blue(3),
    m_alpha(0),
    m_transparency(0),
    m_dataType(GDT_Byte)
{
}

bool GlRasterLayer::fill(const GlTilePtr &tile, float z, bool isLastTry)
{
    double lockTime = CPLAtofM(CPLGetConfigOption("HTTP_TIMEOUT", "5"));
    if(!m_visible) {
        MutexHolder holder(m_dataMutex, lockTime);
        m_tiles[tile->getTile()] = GlObjectPtr();
        return true;
    }

    if(m_tiles.find(tile->getTile()) != m_tiles.end()) { // Already filled
        return true;
    }

    Envelope rasterExtent = m_raster->extent();
    Envelope tileExtent = tile->getExtent();

    // Increase extent to fix lose several pixels on tile border
    double tileExtentW = tileExtent.width();
    double tileExtentH = tileExtent.height();
    if(tileExtentW < 15000.0) {
        glm::vec4 v(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec4 s = tile->getSceneMatrix() * v;

        tileExtent.resize(1.0 + double(s[0]));

        tileExtentW = tileExtent.width();
        tileExtentH = tileExtent.height();
    }

    // FIXME: Reproject tile extent to raster extent

    Envelope outExt = rasterExtent.intersect(tileExtent);

    if(!outExt.isInit()) {
        CPLDebug("ngstore", "fill layer %s not intersect - x: %f, y: %f",
                 m_raster->name().c_str(), rasterExtent.minX(), rasterExtent.minY());
        MutexHolder holder(m_dataMutex, lockTime);
        m_tiles[tile->getTile()] = GlObjectPtr();
        return true;
    }

    // Create inverse geotransform to get pixel data
    double geoTransform[6] = { 0.0 };
    double invGeoTransform[6] = { 0.0 };
    bool noTransform = false;
    if(m_raster->geoTransform(geoTransform)) {
        noTransform = GDALInvGeoTransform(geoTransform, invGeoTransform) == 0;
    }
    else {
        noTransform = true;
    }

    // Calc output buffer width and height
    int outWidth = static_cast<int>(rasterExtent.width() *
                                    tile->getSizeInPixels() / tileExtentW);
    int outHeight = static_cast<int>(rasterExtent.height() *
                                     tile->getSizeInPixels() / tileExtentH);

    if(noTransform) {
        // Swap min/max Y
        rasterExtent.setMaxY(m_raster->height() - rasterExtent.minY());
        rasterExtent.setMinY(m_raster->height() - rasterExtent.maxY());
    }
    else {
        double minX, minY, maxX, maxY;
        GDALApplyGeoTransform(invGeoTransform, rasterExtent.minX(),
                              rasterExtent.minY(), &minX, &maxY);

        GDALApplyGeoTransform(invGeoTransform, rasterExtent.maxX(),
                              rasterExtent.maxY(), &maxX, &minY);
        rasterExtent.setMinX(minX);
        rasterExtent.setMaxX(maxX);
        rasterExtent.setMinY(minY);
        rasterExtent.setMaxY(maxY);
    }

    rasterExtent.fix();

    // Get width & height in pixels of raster area
    int width = static_cast<int>(std::ceil(rasterExtent.width()));
    int height = static_cast<int>(std::ceil(rasterExtent.height()));
    int minX = static_cast<int>(std::floor(rasterExtent.minX()));
    int minY = static_cast<int>(std::floor(rasterExtent.minY()));

    // Correct data
    if(minX < 0) {
        minX = 0;
    }
    if(minY < 0) {
        minY = 0;
    }
    if(width - minX > m_raster->width()) {
        width = m_raster->width() - minX;
    }
    if(height - minY > m_raster->height()) {
        height = m_raster->height() - minY;
    }

    // Memset 255 for buffer and read data skipping 4 byte
    int bandCount = 4;
    int bands[4];
    bands[0] = m_red;
    bands[1] = m_green;
    bands[2] = m_blue;
    bands[3] = m_alpha;

    int overview = MAX_ZOOM;
    bool smooth = false;
    if(outWidth >= width && outHeight >= height ) { // Read original raster
        outWidth = width;
        outHeight = height;
        smooth = true;
    }
    else { // Get closest overview and get overview data
        int minXOv = minX;
        int minYOv = minY;
        int outWidthOv = width;
        int outHeightOv = height;
        overview = m_raster->getBestOverview(minXOv, minYOv,
                                             outWidthOv, outHeightOv,
                                             outWidth, outHeight);
        if(overview >= -5) {
            outWidth = outWidthOv;
            outHeight = outHeightOv;
        }
    }

    int dataSize = GDALGetDataTypeSizeBytes(m_dataType);
    size_t bufferSize = static_cast<size_t>(outWidth * outHeight *
                                            dataSize * 4); // NOTE: We use RGBA to store textures
    GLubyte *pixData = static_cast<GLubyte*>(CPLMalloc(bufferSize));

    if(m_alpha == 0) {
        std::memset(pixData, 255 - m_transparency, bufferSize);
        if(!m_raster->pixelData(pixData, minX, minY, width, height, outWidth,
                                outHeight, m_dataType, bandCount, bands, true, true)) {
            CPLFree(pixData);

            if(isLastTry) {
                MutexHolder holder(m_dataMutex, lockTime);
                m_tiles[tile->getTile()] = GlObjectPtr();
                return true;
            }

            // TODO: Get overzoom or underzoom pixels here

            return false;
        }
    }
    else {
        if(!m_raster->pixelData(pixData, minX, minY, width, height, outWidth,
                                outHeight, m_dataType, bandCount, bands)) {
            CPLFree(pixData);

            if(isLastTry) {
                MutexHolder holder(m_dataMutex, lockTime);
                m_tiles[tile->getTile()] = GlObjectPtr();
                return true;
            }

            return false;
        }
    }

    GlImage *image = new GlImage;
    image->setImage(pixData, outWidth, outHeight); // NOTE: May be not working NOD
    image->setSmooth(smooth);

    // FIXME: Reproject intersect raster extent to tile extent
    GlBuffer *tileExtentBuff = new GlBuffer(GlBuffer::BF_TEX);
    tileExtentBuff->addVertex(static_cast<float>(outExt.minX()));
    tileExtentBuff->addVertex(static_cast<float>(outExt.minY()));
    tileExtentBuff->addVertex(z);
    tileExtentBuff->addVertex(0.0f);
    tileExtentBuff->addVertex(1.0f);
    tileExtentBuff->addIndex(0);
    tileExtentBuff->addVertex(static_cast<float>(outExt.minX()));
    tileExtentBuff->addVertex(static_cast<float>(outExt.maxY()));
    tileExtentBuff->addVertex(z);
    tileExtentBuff->addVertex(0.0f);
    tileExtentBuff->addVertex(0.0f);
    tileExtentBuff->addIndex(1);
    tileExtentBuff->addVertex(static_cast<float>(outExt.maxX()));
    tileExtentBuff->addVertex(static_cast<float>(outExt.maxY()));
    tileExtentBuff->addVertex(z);
    tileExtentBuff->addVertex(1.0f);
    tileExtentBuff->addVertex(0.0f);
    tileExtentBuff->addIndex(2);
    tileExtentBuff->addVertex(static_cast<float>(outExt.maxX()));
    tileExtentBuff->addVertex(static_cast<float>(outExt.minY()));
    tileExtentBuff->addVertex(z);
    tileExtentBuff->addVertex(1.0f);
    tileExtentBuff->addVertex(1.0f);
    tileExtentBuff->addIndex(0);
    tileExtentBuff->addIndex(2);
    tileExtentBuff->addIndex(3);

    GlObjectPtr tileData(new RasterGlObject(tileExtentBuff, image));

    MutexHolder holder(m_dataMutex, lockTime);
    m_tiles[tile->getTile()] = tileData;

    return true;
}

bool GlRasterLayer::draw(const GlTilePtr &tile)
{
    if(!tile) {
        return true;
    }
    if(!m_style) {
        return true; // Should never happened
    }

    double lockTime = CPLAtofM(CPLGetConfigOption("HTTP_TIMEOUT", "5"));
    m_dataMutex.acquire(lockTime);
    auto tileDataIt = m_tiles.find(tile->getTile());
    if(tileDataIt == m_tiles.end()) {
        m_dataMutex.release();
        return false; // Data not yet loaded
    }

    auto second = tileDataIt->second;
    m_dataMutex.release();

    if(!second) {
        return true; // Out of tile extent
    }

    RasterGlObject *rasterGlObject = ngsStaticCast(RasterGlObject, second);

    // Bind everything before call prepare and set matrices
    GlImage *img = rasterGlObject->getImageRef();
    ngsStaticCast(SimpleImageStyle, m_style)->setImage(img);
    GlBuffer *extBuff = rasterGlObject->getBufferRef();
    if(extBuff->bound()) {
        extBuff->rebind();
    }
    else {
        extBuff->bind();
    }

    m_style->prepare(tile->getSceneMatrix(), tile->getInvViewMatrix(),
                     extBuff->type());
    m_style->draw(*extBuff);

    return true;

/*/    Envelope ext = tile->getExtent();
//    ext.resize(0.9);

//    std::array<OGRPoint, 6> points;
//    points[0] = OGRPoint(ext.getMinX(), ext.getMinY());
//    points[1] = OGRPoint(ext.getMinX(), ext.getMaxY());
//    points[2] = OGRPoint(ext.getMaxX(), ext.getMaxY());
//    points[3] = OGRPoint(ext.getMaxX(), ext.getMinY());
//    points[4] = OGRPoint(ext.getMinX(), ext.getMinY());
//    points[5] = OGRPoint(ext.getMaxX(), ext.getMaxY());
//    for(size_t i = 0; i < points.size() - 1; ++i) {
//        Normal normal = ngsGetNormals(points[i], points[i + 1]);

//        GlBuffer buffer1;
//        // 0
//        buffer1.addVertex(points[i].getX());
//        buffer1.addVertex(points[i].getY());
//        buffer1.addVertex(0.0f);
//        buffer1.addVertex(-normal.x);
//        buffer1.addVertex(-normal.y);
//        buffer1.addIndex(0);

//        // 1
//        buffer1.addVertex(points[i + 1].getX());
//        buffer1.addVertex(points[i + 1].getY());
//        buffer1.addVertex(0.0f);
//        buffer1.addVertex(-normal.x);
//        buffer1.addVertex(-normal.y);
//        buffer1.addIndex(1);

//        // 2
//        buffer1.addVertex(points[i].getX());
//        buffer1.addVertex(points[i].getY());
//        buffer1.addVertex(0.0f);
//        buffer1.addVertex(normal.x);
//        buffer1.addVertex(normal.y);
//        buffer1.addIndex(2);

//        // 3
//        buffer1.addVertex(points[i + 1].getX());
//        buffer1.addVertex(points[i + 1].getY());
//        buffer1.addVertex(0.0f);
//        buffer1.addVertex(normal.x);
//        buffer1.addVertex(normal.y);

//        buffer1.addIndex(1);
//        buffer1.addIndex(2);
//        buffer1.addIndex(3);

//        SimpleLineStyle style;
//        style.setLineWidth(14.0f);
//        style.setColor({0, 0, 255, 255});
//        buffer1.bind();
//        style.prepare(tile->getSceneMatrix(), tile->getInvViewMatrix());
//        style.draw(buffer1);

//        buffer1.destroy();
//        style.destroy();
//    }

//    return true; */
}

void GlRasterLayer::setStyle(const std::string &name)
{
    if(compare(name, m_style->name())) {
        return;
    }

    GlView *mapView = dynamic_cast<GlView*>(m_map);
    StylePtr newStyle = StylePtr(Style::createStyle(name, mapView->textureAtlas()));
    if(newStyle) {
        m_oldStyles.push_back(m_style);
        m_style = newStyle;
    }
}

bool GlRasterLayer::load(const CPLJSONObject &store, ObjectContainer *objectContainer)
{
    bool result = RasterLayer::load(store, objectContainer);
    if(!result) {
        return false;
    }
    CPLJSONObject raster = store.GetObj("raster");
    if(raster.IsValid()) {
        m_red = static_cast<unsigned char>(raster.GetInteger("red", m_red));
        m_green = static_cast<unsigned char>(raster.GetInteger("green", m_green));
        m_blue = static_cast<unsigned char>(raster.GetInteger("blue", m_blue));
        m_alpha = static_cast<unsigned char>(raster.GetInteger("alpha", m_alpha));
        m_transparency = static_cast<unsigned char>(raster.GetInteger("transparency",
                                                                      m_transparency));
    }

    GlView *mapView = dynamic_cast<GlView*>(m_map);
    m_style = StylePtr(Style::createStyle("simpleImage", mapView->textureAtlas()));
    return true;
}

CPLJSONObject GlRasterLayer::save(const ObjectContainer *objectContainer) const
{
    CPLJSONObject out = RasterLayer::save(objectContainer);
    CPLJSONObject raster;
    raster.Add("red", m_red);
    raster.Add("green", m_green);
    raster.Add("blue", m_blue);
    raster.Add("alpha", m_alpha);
    raster.Add("transparency", m_transparency);
    out.Add("raster", raster);
    return out;
}

void GlRasterLayer::setRaster(const RasterPtr &raster)
{
    RasterLayer::setRaster(raster);
    // Create default style
    GlView *mapView = dynamic_cast<GlView*>(m_map);
    m_style = StylePtr(Style::createStyle("simpleImage", mapView->textureAtlas()));

    if(raster->bandCount() == 4) {
        m_alpha = 4;
    }
}

//------------------------------------------------------------------------------
// RasterGlObject
//------------------------------------------------------------------------------

RasterGlObject::RasterGlObject(GlBuffer *tileExtentBuff, GlImage *image) :
    GlObject(),
    m_extentBuffer(tileExtentBuff),
    m_image(image)
{

}

void RasterGlObject::bind()
{
    m_extentBuffer->bind();
    m_image->bind();
}

void RasterGlObject::rebind() const
{
    m_extentBuffer->rebind();
    m_image->rebind();
}

void RasterGlObject::destroy()
{
    m_extentBuffer->destroy();
    m_image->destroy();
}

//------------------------------------------------------------------------------
// VectorGlObject
//------------------------------------------------------------------------------

VectorGlObject::VectorGlObject() :
    GlObject()
{

}

void VectorGlObject::bind()
{
    if(m_bound) {
        return;
    }
    for(GlBufferPtr& buffer : m_buffers) {
        buffer->bind();
    }
    m_bound = true;
}

void VectorGlObject::rebind() const
{
    for(const GlBufferPtr& buffer : m_buffers) {
        buffer->rebind();
    }
}

void VectorGlObject::destroy()
{
    for(GlBufferPtr& buffer : m_buffers) {
        buffer->destroy();
    }
}

//------------------------------------------------------------------------------
// VectorGlObject
//------------------------------------------------------------------------------

VectorSelectableGlObject::VectorSelectableGlObject() :
    VectorGlObject()
{

}

void VectorSelectableGlObject::bind()
{
    if(m_bound) {
        return;
    }

    for(GlBufferPtr& buffer : m_buffers) {
        buffer->bind();
    }

    for(GlBufferPtr& buffer : m_selectionBuffers) {
        buffer->bind();
    }

    m_bound = true;
}

void VectorSelectableGlObject::rebind() const
{
    for(const GlBufferPtr& buffer : m_buffers) {
        buffer->rebind();
    }

    for(const GlBufferPtr& buffer : m_selectionBuffers) {
        buffer->rebind();
    }
}

void VectorSelectableGlObject::destroy()
{
    for(GlBufferPtr& buffer : m_buffers) {
        buffer->destroy();
    }
    m_buffers.clear();

    for(GlBufferPtr& buffer : m_selectionBuffers) {
        buffer->destroy();
    }
    m_selectionBuffers.clear();
    m_bound = false;
}

} // namespace ngs
