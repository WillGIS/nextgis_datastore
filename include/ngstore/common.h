/******************************************************************************
 * Project:  libngstore
 * Purpose:  NextGIS store and visualisation support library
 * Author: Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
 ******************************************************************************
 *   Copyright (c) 2016-2019 NextGIS, <info@nextgis.com>
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
 
#ifndef NGSCOMMON_H
#define NGSCOMMON_H

#ifdef NGSTOR_STATIC
#   define NGS_EXTERN
#else
#   ifdef _WIN32
#    ifdef NGSTOR_EXPORTS
#      ifdef __GNUC__
#        define NGS_EXTERN __attribute__((dllexport))
#      else        
#        define NGS_EXTERN __declspec(dllexport)
#      endif 
#    else
#      ifdef __GNUC__
#        define NGS_EXTERN __attribute__((dllimport))
#      else        
#        define NGS_EXTERN __declspec(dllimport)
#      endif 
#    endif
#   else
#     if __GNUC__ >= 4
#       define NGS_EXTERN __attribute__((visibility("default")))
#     else
#       define NGS_EXTERN
#     endif 
#   endif
#endif

#if defined __cplusplus && !defined __ANDROID__
#define NGS_EXTERNC extern "C" NGS_EXTERN
#else
#define NGS_EXTERNC NGS_EXTERN
#endif

// TODO: Add gettext support
// 1. CMake add generate translation macros
// 2. Load translation catalog files code
// 3. Init locale, etc.
#ifdef HAVE_LIBINTL_H
#include <libintl.h>
#else
#define gettext(String) (String)
#define _(String) (String)
#endif

#endif // NGSCOMMON_H
