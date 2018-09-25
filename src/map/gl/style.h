/******************************************************************************
 * Project:  libngstore
 * Purpose:  NextGIS store and visualisation support library
 * Author: Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
 * Author: NikitaFeodonit, nfeodonit@yandex.com
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

#ifndef NGSGLSTYLE_H
#define NGSGLSTYLE_H

#include <vector>

#include "cpl_json.h"

#include "buffer.h"
#include "image.h"
#include "program.h"

#include "ds/geometry.h"
#include "ngstore/api.h"

namespace ngs
{
typedef std::map<std::string, GlImagePtr> TextureAtlas;

//------------------------------------------------------------------------------
// Style
//------------------------------------------------------------------------------

class Style : public GlObject
{
public:
    enum ShaderType {
        SH_VERTEX,
        SH_FRAGMENT
    };

public:
    Style();
    virtual ~Style() override = default;
    virtual bool prepare(const glm::mat4 &msMatrix, const glm::mat4 &vsMatrix,
                         enum GlBuffer::BufferType type);
    virtual void draw(const GlBuffer &buffer) const;
    virtual bool load(const CPLJSONObject &store) = 0;
    virtual CPLJSONObject save() const = 0;
    virtual std::string name() const = 0;
    virtual enum ngsStyleType type() const { return m_styleType; }

    //static
public:
    static Style *createStyle(const std::string &name, const TextureAtlas &atlas);


    // GlObject interface
public:
    virtual void bind() override {}
    virtual void rebind() const override {}
    virtual void destroy() override { m_program.destroy(); } // NOTE: Destroy only style stored GlObjects (i.e. program)

protected:
    virtual const GLchar *shaderSource(enum ShaderType type);

protected:
    const GLchar *m_vertexShaderSource;
    const GLchar *m_fragmentShaderSource;
    GlProgram m_program;
    enum ngsStyleType m_styleType;
};

using StylePtr = std::shared_ptr<Style>;

//------------------------------------------------------------------------------
// SimpleVectorStyle
//------------------------------------------------------------------------------

class SimpleVectorStyle : public Style
{
public:
    SimpleVectorStyle();
    virtual void setColor(const ngsRGBA &color) { m_color = ngsRGBA2Gl(color); }
    virtual ngsRGBA color() const { return ngsGl2RGBA(m_color); }
    virtual enum GlBuffer::BufferType bufferType() const = 0;

    // Style interface
public:
    virtual bool prepare(const glm::mat4 &msMatrix, const glm::mat4 &vsMatrix,
                         enum GlBuffer::BufferType type) override;
    virtual bool load(const CPLJSONObject &store) override;
    virtual CPLJSONObject save() const override;

protected:
    GlColor m_color;
};

//------------------------------------------------------------------------------
// PointStyle
//------------------------------------------------------------------------------

enum PointType {
    PT_UNKNOWN = 0,
    PT_SQUARE,
    PT_RECTANGLE,
    PT_CIRCLE,
    PT_TRIANGLE,
    PT_DIAMOND,
    PT_STAR,
    PT_MARKER
};

class PointStyle : public SimpleVectorStyle
{
public:
    explicit PointStyle(enum PointType type = PT_CIRCLE);

    enum PointType pointType() const { return m_type; }
    virtual void setType(enum PointType type) { m_type = type; }
    float size() const { return m_size; }
    void setSize(float size) { m_size = size; }
    float rotation() const { return m_rotation; }
    void setRotation(float rotation) { m_rotation = rotation; }

    virtual unsigned short addPoint(const SimplePoint &pt, float z,
                                    unsigned short index,
                                    GlBuffer *buffer) = 0;
    virtual size_t pointVerticesCount() const = 0;

    // Style interface
public:
    virtual bool load(const CPLJSONObject &store) override;
    virtual CPLJSONObject save() const override;

protected:
    enum PointType m_type;
    float m_size;
    float m_rotation;
};

using PointStylePtr = std::shared_ptr<PointStyle>;

//------------------------------------------------------------------------------
// SimplePointStyle
//------------------------------------------------------------------------------

class SimplePointStyle : public PointStyle
{
public:
    explicit SimplePointStyle(enum PointType type = PT_CIRCLE);

    // PointStyle interface
public:
    virtual unsigned short addPoint(const SimplePoint &pt, float z,
                                    unsigned short index,
                                    GlBuffer *buffer) override;
    virtual size_t pointVerticesCount() const override { return 3; }
    virtual enum GlBuffer::BufferType bufferType() const override {
        return GlBuffer::BF_PT;
    }

    // Style interface
public:
    virtual bool prepare(const glm::mat4 &msMatrix, const glm::mat4 &vsMatrix,
                         enum GlBuffer::BufferType type) override;
    virtual void draw(const GlBuffer &buffer) const override;
    virtual std::string name() const override { return "simplePoint"; }
};


//------------------------------------------------------------------------------
// PrimitivePointStyle
// https://stackoverflow.com/a/11923070
//------------------------------------------------------------------------------

class PrimitivePointStyle : public PointStyle
{
public:
    explicit PrimitivePointStyle(enum PointType type = PT_CIRCLE);

    unsigned char segmentCount() const { return m_segmentCount; }
    void setSegmentCount(unsigned char segmentCount) { m_segmentCount = segmentCount; }

    // PointStyle interface
public:
    virtual void setType(enum PointType type) override;
    virtual unsigned short addPoint(const SimplePoint &pt, float z,
                                    unsigned short index,
                                    GlBuffer *buffer) override;
    virtual size_t pointVerticesCount() const override;
    virtual enum GlBuffer::BufferType bufferType() const override {
        return GlBuffer::BF_FILL;
    }

    // Style interface
public:
    virtual bool prepare(const glm::mat4 &msMatrix, const glm::mat4 &vsMatrix,
                         enum GlBuffer::BufferType type) override;
    virtual void draw(const GlBuffer &buffer) const override;
    virtual bool load(const CPLJSONObject &store) override;
    virtual CPLJSONObject save() const override;
    virtual std::string name() const override { return "primitivePoint"; }

protected:
    unsigned char m_segmentCount;
    unsigned char m_starEndsCount;
};

//------------------------------------------------------------------------------
// SimpleLineStyle
//------------------------------------------------------------------------------

enum CapType {
    CT_BUTT,
    CT_ROUND,
    CT_SQUARE
};

enum JoinType {
    JT_MITER,
    JT_ROUND,
    JT_BEVELED
};

class SimpleLineStyle : public SimpleVectorStyle
{
public:
    SimpleLineStyle();

    float width() const { return m_width; }
    void setWidth(float lineWidth) { m_width = lineWidth * 0.25f; }
    enum CapType capType() const;
    void setCapType(const CapType &capType);
    enum JoinType joinType() const;
    void setJoinType(const JoinType &joinType);
    unsigned char segmentCount() const;
    void setSegmentCount(unsigned char segmentCount);

    unsigned short addLineCap(const SimplePoint &point, const Normal &normal,
                              float z, unsigned short index, GlBuffer *buffer);
    size_t lineCapVerticesCount() const;
    unsigned short addLineJoin(const SimplePoint &point, const Normal &prevNormal,
                               const Normal &normal, float z, unsigned short index,
                               GlBuffer *buffer);
    size_t lineJoinVerticesCount() const;
    virtual unsigned short addSegment(const SimplePoint &pt1, const SimplePoint &pt2,
                              const Normal &normal, float z,
                              unsigned short index, GlBuffer *buffer);

    // SimpleVectorStyle
public:
    virtual enum GlBuffer::BufferType bufferType() const override {
        return GlBuffer::BF_LINE;
    }

    // Style interface
public:
    virtual bool prepare(const glm::mat4 &msMatrix, const glm::mat4 &vsMatrix,
                         enum GlBuffer::BufferType type) override;
    virtual void draw(const GlBuffer &buffer) const override;
    virtual bool load(const CPLJSONObject &store) override;
    virtual CPLJSONObject save() const override;
    virtual std::string name() const override { return "simpleLine"; }

protected:
    GLint m_normalId;
    GLint m_vLineWidthId;

    float m_width;
    enum CapType m_capType;
    enum JoinType m_joinType;
    unsigned char m_segmentCount;
};

//------------------------------------------------------------------------------
// SimpleFillStyle
//------------------------------------------------------------------------------

class SimpleFillStyle : public SimpleVectorStyle
{
public:
    SimpleFillStyle();

    // Style interface
public:
    virtual void draw(const GlBuffer &buffer) const override;
    virtual bool prepare(const glm::mat4 &msMatrix, const glm::mat4 &vsMatrix,
                         enum GlBuffer::BufferType type) override;
    virtual std::string name() const override { return "simpleFill"; }

    // SimpleVectorStyle
public:
    virtual enum GlBuffer::BufferType bufferType() const override {
        return GlBuffer::BF_FILL;
    }
};

//------------------------------------------------------------------------------
// SimpleFillBorderedStyle
//------------------------------------------------------------------------------

class SimpleFillBorderedStyle : public Style
{
public:
    SimpleFillBorderedStyle();

    float borderWidth() const { return m_line.width(); }
    void setBorderWidth(float borderWidth) { m_line.setWidth(borderWidth); }
    void setBorderColor(const ngsRGBA &color) { m_line.setColor(color); }
    void setColor(const ngsRGBA &color) { m_fill.setColor(color); }
    ngsRGBA color() const { return m_fill.color(); }
    enum CapType capType() const;
    void setCapType(const CapType &capType);
    enum JoinType joinType() const;
    void setJoinType(const JoinType &joinType);
    unsigned char segmentCount() const;
    void setSegmentCount(unsigned char segmentCount);
    SimpleLineStyle *lineStyle() { return &m_line; }
    SimpleFillStyle *fillStyle() { return &m_fill; }

    // Style interface
public:
    virtual bool prepare(const glm::mat4 &msMatrix, const glm::mat4 &vsMatrix,
                         enum GlBuffer::BufferType type) override;
    virtual void draw(const GlBuffer &buffer) const override;
    virtual bool load(const CPLJSONObject &store) override;
    virtual CPLJSONObject save() const override;
    virtual std::string name() const override { return "simpleFillBordered"; }

protected:
    SimpleFillStyle m_fill;
    SimpleLineStyle m_line;
};

//------------------------------------------------------------------------------
// SimpleImageStyle
//------------------------------------------------------------------------------

class SimpleImageStyle : public Style
{
public:
    SimpleImageStyle();
    void setImage(GlImage * const image) { m_image = image; }

    // Style interface
public:
    virtual bool prepare(const glm::mat4 &msMatrix, const glm::mat4 &vsMatrix,
                         enum GlBuffer::BufferType type) override;
    virtual void draw(const GlBuffer &buffer) const override;

protected:
    GlImage *m_image;

    // Style interface
public:
    virtual bool load(const CPLJSONObject &store) override;
    virtual CPLJSONObject save() const override { return CPLJSONObject(); }
    virtual std::string name() const override { return "simpleImage"; }
};

//------------------------------------------------------------------------------
// MarkerStyle
//------------------------------------------------------------------------------

class MarkerStyle : public PointStyle
{
public:
    MarkerStyle(const TextureAtlas &textureAtlas);
    void setIcon(const std::string &iconSetName, unsigned short index,
                 unsigned char width, unsigned char height);

    // PointStyle
public:
    virtual void setType(enum PointType type) override;
    virtual size_t pointVerticesCount() const override { return 4; }
    virtual unsigned short addPoint(const SimplePoint &pt, float z,
                                    unsigned short index,
                                    GlBuffer *buffer) override;
    virtual enum GlBuffer::BufferType bufferType() const override {
        return GlBuffer::BF_TEX;
    }

    // Style interface
public:
    virtual bool prepare(const glm::mat4 &msMatrix, const glm::mat4 &vsMatrix,
                         enum GlBuffer::BufferType type) override;
    virtual void draw(const GlBuffer &buffer) const override;
    virtual bool load(const CPLJSONObject &store) override;
    virtual CPLJSONObject save() const override;
    virtual std::string name() const override { return "marker"; }

protected:
    GlImage *m_iconSet;
    CPLString m_iconSetName;
    unsigned short m_iconIndex;
    unsigned short m_iconWidth;
    unsigned short m_iconHeight;
    TextureAtlas m_textureAtlas;

protected:
    float m_ulx, m_uly, m_lrx, m_lry;
};

//------------------------------------------------------------------------------
// LocationStyle
//------------------------------------------------------------------------------

class LocationStyle
{
public:
    enum Status {
        LS_STAY,
        LS_MOVE
    };

public:
    LocationStyle() {}
    virtual ~LocationStyle() = default;
    virtual void setStatus(enum Status status) = 0;
};

//------------------------------------------------------------------------------
// SimpleLocationStyle
//------------------------------------------------------------------------------

class SimpleLocationStyle : public PrimitivePointStyle, public LocationStyle
{
public:
    explicit SimpleLocationStyle(enum PointType type = PT_CIRCLE) :
        PrimitivePointStyle(type) {}

    // Style interface
    virtual std::string name() const override { return "simpleLocation"; }

    // LocationStyle interface
public:
    virtual void setStatus(enum Status status) override;
};

//------------------------------------------------------------------------------
// MarkerLocationStyle
//------------------------------------------------------------------------------

class MarkerLocationStyle : public MarkerStyle, public LocationStyle
{
public:
    explicit MarkerLocationStyle(const TextureAtlas &textureAtlas) :
        MarkerStyle(textureAtlas) {}

    // Style interface
    virtual std::string name() const override { return "markerLocation"; }
    virtual bool load(const CPLJSONObject &store) override;
    virtual CPLJSONObject save() const override;


    // LocationStyle interface
public:
    virtual void setStatus(enum Status status) override;

protected:
    void setIndex(unsigned short index);

protected:
    unsigned short m_stayIndex, m_moveIndex;
};

//------------------------------------------------------------------------------
// EditPointStyle
//------------------------------------------------------------------------------

class EditPointStyle
{
public:
    EditPointStyle() {}
    virtual ~EditPointStyle() = default;
    virtual void setEditElementType(enum ngsEditElementType type) = 0;
};

//------------------------------------------------------------------------------
// SimpleEditPointStyle
//------------------------------------------------------------------------------

class SimpleEditPointStyle : public PrimitivePointStyle, public EditPointStyle
{
public:
    explicit SimpleEditPointStyle(enum PointType type = PT_STAR) :
        PrimitivePointStyle(type) {}
    virtual ~SimpleEditPointStyle() override = default;

    // EditPointStyle interface
public:
    virtual void setEditElementType(enum ngsEditElementType type) override;
};

//------------------------------------------------------------------------------
// MarkerEditPointStyle
//------------------------------------------------------------------------------

class MarkerEditPointStyle : public MarkerStyle, public EditPointStyle
{
public:
    explicit MarkerEditPointStyle(const TextureAtlas &textureAtlas);
    virtual ~MarkerEditPointStyle() override = default;

    // EditPointStyle interface
public:
    virtual void setEditElementType(enum ngsEditElementType type) override;

    // Style interface
public:
    virtual std::string name() const override { return "markerEditPoint"; }
    virtual bool load(const CPLJSONObject &store) override;
    virtual CPLJSONObject save() const override;

protected:
    void setIndex(unsigned short index);

protected:
    unsigned short m_pointIndex;
    unsigned short m_selectedPointIndex;
    unsigned short m_walkPointIndex;
    unsigned short m_medianPointIndex;
    unsigned short m_selectedMedianPointIndex;
};

//------------------------------------------------------------------------------
// EditLineStyle
//------------------------------------------------------------------------------

class EditLineStyle : public SimpleLineStyle
{
public:
    EditLineStyle();
    virtual ~EditLineStyle() override = default;

    void setEditElementType(enum ngsEditElementType type);

    // Style interface
public:
    virtual std::string name() const override { return "editLine"; }
    virtual bool load(const CPLJSONObject &store) override;
    virtual CPLJSONObject save() const override;

protected:
    ngsRGBA m_lineColor;
    ngsRGBA m_selectedLineColor;
};

typedef std::shared_ptr<EditLineStyle> EditLineStylePtr;

//------------------------------------------------------------------------------
// EditPolygonStyle
//------------------------------------------------------------------------------

class EditFillStyle : public SimpleFillStyle
{
public:
    EditFillStyle();
    virtual ~EditFillStyle() override = default;

    void setEditElementType(enum ngsEditElementType type);

    // Style interface
public:
    virtual std::string name() const override { return "editFill"; }
    virtual bool load(const CPLJSONObject &store) override;
    virtual CPLJSONObject save() const override;

protected:
    ngsRGBA m_fillColor;
    ngsRGBA m_selectedFillColor;
};

typedef std::shared_ptr<EditFillStyle> EditFillStylePtr;

//------------------------------------------------------------------------------
// SimpleEditCrossStyle
//------------------------------------------------------------------------------

class SimpleEditCrossStyle : public SimplePointStyle
{
public:
    explicit SimpleEditCrossStyle(enum PointType type = PT_CIRCLE);
    virtual ~SimpleEditCrossStyle() = default;
};

}  // namespace ngs

#endif  // NGSGLSTYLE_H
