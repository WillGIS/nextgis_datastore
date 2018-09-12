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
#ifndef NGSGLIMAGE_H
#define NGSGLIMAGE_H

#include "functions.h"

#include "ds/raster.h"

namespace ngs {

class GlImage : public GlObject
{
public:
    GlImage();
    virtual ~GlImage() override;
    void setImage(GLubyte *imageData, GLsizei width, GLsizei height) {
        m_imageData = static_cast<GLubyte*>(imageData);
        m_width = width;
        m_height = height;
    }
    void setImage(const ImageData &data) {
        m_imageData = static_cast<GLubyte*>(data.buffer);
        m_width = data.width;
        m_height = data.height;
    }

    size_t width() const { return static_cast<size_t>(m_width); }
    size_t height() const { return static_cast<size_t>(m_height); }

    // GlObject interface
public:
    virtual void bind() override;
    virtual void rebind() const override;
    virtual void destroy() override;

    GLuint id() const { return m_id; }
    void setSmooth(bool smooth) { m_smooth = smooth; }

protected:
    GLubyte *m_imageData;
    GLsizei m_width, m_height;
    GLuint m_id;
    bool m_smooth;
};

using GlImagePtr = std::shared_ptr<GlImage>;

} // namespace ngs

#endif // NGSGLIMAGE_H
