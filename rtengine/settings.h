/* -*- C++ -*-
 *  
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>
 *
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  RawTherapee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include "procparams.h"

namespace rtengine {

/** This structure holds the global parameters used by the RT engine. */
class Settings {
public:
    Settings();
    
    Glib::ustring   iccDirectory;           ///< The directory containing the possible output icc profiles
    Glib::ustring monitorIccDirectory;

    Glib::ustring   printerProfile;         ///< ICC profile name used for soft-proofing a printer output
    RenderingIntent printerIntent;          ///< Colorimetric intent used with the above profile
    bool            printerBPC;             ///< Black Point Compensation for the Labimage->Printer->Monitor transform
    Glib::ustring   monitorProfile;         ///< ICC profile name used for the monitor
    RenderingIntent monitorIntent;          ///< Colorimetric intent used with the above profile
    bool            monitorBPC;             ///< Black Point Compensation for the Labimage->Monitor transform (directly, i.e. not soft-proofing and no WCS in between)
    bool            autoMonitorProfile;     ///< Try to auto-determine the correct monitor color profile
    int verbose;
    Glib::ustring   darkFramesPath;         ///< The default directory for dark frames
    Glib::ustring   flatFieldsPath;         ///< The default directory for flat fields

    bool            HistogramWorking;       // true: histogram is display the value of the image computed in the Working profile
                                            // false: histogram is display the value of the image computed in the Output profile
    Glib::ustring   lensfunDbDirectory; ///< The directory containing the lensfun database. If empty, the system defaults will be used (as described in http://lensfun.sourceforge.net/manual/dbsearch.html)

    enum class ThumbnailInspectorMode {
        JPEG,
        RAW
    };
    ThumbnailInspectorMode thumbnail_inspector_mode;
    enum class ThumbnailInspectorRawCurve {
        LINEAR,
        FILM,
        SHADOW_BOOST,
        RAW_CLIPPING
    };
    ThumbnailInspectorRawCurve thumbnail_inspector_raw_curve;

    enum class XmpSidecarStyle {
        STD, // FILENAME.xmp for FILENAME.ext
        EXT  // FILENAME.ext.xmp for FILENAME.ext
    };
    XmpSidecarStyle xmp_sidecar_style;

    enum class MetadataXmpSync {
        NONE,
        READ,
        READ_WRITE
    };
    MetadataXmpSync metadata_xmp_sync;

    Glib::ustring exiftool_path;

    int thread_pool_size;

    bool ctl_scripts_fast_preview;

    enum class StdMonitorProfile {
        SRGB,
        DISPLAY_P3,
        ADOBE_RGB
    };
    StdMonitorProfile os_monitor_profile;

    enum class ColorManagementMode {
        APPLICATION,
        OS_SRGB,
        OS_STD_MONITOR_PROFILE
    };
    static ColorManagementMode color_mgmt_mode;

    int imgio_raw_cache_size;
};

} // namespace rtengine



