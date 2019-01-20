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
#ifndef NGSPROGRESS_H
#define NGSPROGRESS_H

#include "ngstore/api.h"

namespace ngs {

/**
 * @brief The Progress class The class for indication progress of some operation.
 */
class Progress
{
public:
    explicit Progress(ngsProgressFunc progressFunc = nullptr,
             void* progressArguments = nullptr);
    virtual ~Progress() = default;
    virtual bool onProgress(enum ngsCode status,
                            double complete,
                            const char* format, ...) const;

    virtual void setTotalSteps(unsigned char value) { m_totalSteps = value; }
    virtual void setStep(unsigned char value) { m_step = value; }
    unsigned char totalSteps() const { return m_totalSteps; }
    unsigned char step() const { return m_step; }
protected:
    ngsProgressFunc m_progressFunc;
    void *m_progressArguments;
    unsigned char m_totalSteps;
    unsigned char m_step;
};

int ngsGDALProgress(double complete, const char *message,  void *progressArg);

}

#endif // NGSPROGRESS_H
