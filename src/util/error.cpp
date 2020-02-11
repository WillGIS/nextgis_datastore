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
#include "error.h"

#include "cpl_error.h"
#include "cpl_multiproc.h"
#include "cpl_string.h"

namespace ngs {

static CPLLock *hAtomicOpLock = nullptr;
static std::string gLastMsg;

int outMessage(enum ngsCode errorCode, const char *fmt, ...)
{
    if(errorCode >= COD_UNEXPECTED_ERROR && nullptr != fmt) {
        va_list args;

        // Expand the error message
        va_start(args, fmt);
        CPLErrorV(CE_Failure, CPLE_AppDefined, fmt, args);
        va_end(args);
    }
    CPLLockHolderD(&hAtomicOpLock, LOCK_SPIN)
    gLastMsg = CPLGetLastErrorMsg();
    return errorCode;
}

bool errorMessage(const char *fmt, ...)
{
    if(nullptr == fmt || EQUAL(fmt, "")) {
        return true; // No error
    }
    va_list args;

    // Expand the error message
    va_start(args, fmt);
    CPLErrorV(CE_Failure, CPLE_AppDefined, fmt, args);
    va_end(args);
    CPLLockHolderD(&hAtomicOpLock, LOCK_SPIN)
    gLastMsg = CPLGetLastErrorMsg();
    return false;
}

void warningMessage(const char *fmt, ...)
{
    if(nullptr != fmt) {
        va_list args;

        // Expand the error message
        va_start(args, fmt);
        CPLErrorV(CE_Warning, CPLE_AppDefined, fmt, args);
        va_end(args);
    }
    CPLLockHolderD(&hAtomicOpLock, LOCK_SPIN)
    gLastMsg = CPLGetLastErrorMsg();
}

const char *getLastError()
{
    CPLLockHolderD(&hAtomicOpLock, LOCK_SPIN)
    return gLastMsg.c_str();
}

void resetError()
{
    CPLLockHolderD(&hAtomicOpLock, LOCK_SPIN)
    gLastMsg.clear();
    CPLErrorReset();
}

}
