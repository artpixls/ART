/*
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
#include "options.h"
#include <cstdio>
#include <glib/gstdio.h>
#include <sstream>
#include <iostream>
#include "multilangmgr.h"
#include "addsetids.h"
#include "guiutils.h"
#include "version.h"
#include "../rtengine/metadata.h"

#ifdef _OPENMP
#include <omp.h>
#endif



#ifdef WIN32
#include <windows.h>
// for GCC32
#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif
#include <Shlobj.h>
#endif

// Global (system) settings directory, including images' profiles if used
Glib::ustring Options::ART_base_dir;
// User's settings directory, including images' profiles if used
Glib::ustring Options::user_config_dir;
// User's cached datas' directory
Glib::ustring Options::cacheBaseDir;

Options options;
Glib::ustring versionString = RTVERSION;
Glib::ustring paramFileExtension = ".arp";


Glib::ustring SaveFormat::getKey() const
{
    if (format == "jpg") {
        return format;
    } else if (format == "png") {
        return format + std::to_string(pngBits);
    } else if (format == "tif") {
        return format + std::to_string(tiffBits) + (tiffFloat ? "f" : "");
    } else {
        return format;
    }
}


Options::RenameOptions::RenameOptions()
{
    pattern = "%f.%e";
    sidecars = "";
    name_norm = 0;
    ext_norm = 0;
    allow_whitespace = false;
    on_existing = 0;
    progressive_number = 1;
}


Options::Options()
{
    defProfError = 0;
    setDefaults();
}

const char *DefaultLanguage = "English (US)";

inline bool Options::checkProfilePath(Glib::ustring &path)
{
    if (path.empty()) {
        return false;
    }

    Glib::ustring p = getUserProfilePath();

    if (!p.empty() && Glib::file_test(path + paramFileExtension, Glib::FILE_TEST_EXISTS)) {
        return true;
    }

    p = getGlobalProfilePath();

    return !p.empty() && Glib::file_test(path + paramFileExtension, Glib::FILE_TEST_EXISTS);
}

bool Options::checkDirPath(Glib::ustring &path, Glib::ustring errString)
{
    if (Glib::file_test(path, Glib::FILE_TEST_EXISTS) && Glib::file_test(path, Glib::FILE_TEST_IS_DIR)) {
        return true;
    } else {
        if (!errString.empty()) {
            std::cerr << errString << std::endl;
        }

        return false;
    }
}

void Options::updatePaths()
{

    Glib::ustring tmpPath;

    userProfilePath = "";
    globalProfilePath = "";

    if (Glib::path_is_absolute(profilePath)) {
        // absolute path
        if (!checkDirPath(profilePath, "")) {
            g_mkdir_with_parents(profilePath.c_str(), 511);

            if (!checkDirPath(profilePath, "")) {  // had problems with mkdir_with_parents return value on OS X, just check dir again
                Glib::ustring msg = Glib::ustring::compose("Creation of the user's processing profile directory \"%1\" failed!\n", profilePath);
                throw Error(msg);
            }
        }

        if (checkDirPath(profilePath, "Error: the user's processing profile path doesn't point to a directory or doesn't exist!\n")) {
            userProfilePath = profilePath;
            tmpPath = Glib::build_filename(ART_base_dir, "profiles");

            if (checkDirPath(tmpPath, "Error: the global's processing profile path doesn't point to a directory or doesn't exist!\n")) {
                if (userProfilePath != tmpPath) {
                    globalProfilePath = tmpPath;
                }
            }
        } else {
            tmpPath = Glib::build_filename(ART_base_dir, "profiles");

            if (checkDirPath(tmpPath, "Error: the global's processing profile path doesn't point to a directory or doesn't exist!\n")) {
                globalProfilePath = tmpPath;
            }
        }
    } else {
        // relative paths
        tmpPath = Glib::build_filename(user_config_dir, profilePath);

        if (!checkDirPath(tmpPath, "")) {
            g_mkdir_with_parents(tmpPath.c_str(), 511);

            if (!checkDirPath(tmpPath, "")) {
                Glib::ustring msg = Glib::ustring::compose("Creation of the user's processing profile directory \"%1\" failed!\n", tmpPath.c_str());
                throw Error(msg);
            }
        }

        if (checkDirPath(tmpPath, "Error: the user's processing profile path doesn't point to a directory!\n")) {
            userProfilePath = tmpPath;
        }

        tmpPath = Glib::build_filename(ART_base_dir, "profiles");

        if (checkDirPath(tmpPath, "Error: the user's processing profile path doesn't point to a directory or doesn't exist!\n")) {
            globalProfilePath = tmpPath;
        }
    }

    Glib::ustring preferredPath = getPreferredProfilePath();

    // Paths are updated only if the user or global profile path is set
    if (lastRgbCurvesDir.empty() || !Glib::file_test(lastRgbCurvesDir, Glib::FILE_TEST_EXISTS) || !Glib::file_test(lastRgbCurvesDir, Glib::FILE_TEST_IS_DIR)) {
        lastRgbCurvesDir = preferredPath;
    }

    if (lastLabCurvesDir.empty() || !Glib::file_test(lastLabCurvesDir, Glib::FILE_TEST_EXISTS) || !Glib::file_test(lastLabCurvesDir, Glib::FILE_TEST_IS_DIR)) {
        lastLabCurvesDir = preferredPath;
    }

    if (lastPFCurvesDir.empty() || !Glib::file_test(lastPFCurvesDir, Glib::FILE_TEST_EXISTS) || !Glib::file_test(lastPFCurvesDir, Glib::FILE_TEST_IS_DIR)) {
        lastPFCurvesDir = preferredPath;
    }

    if (lastHsvCurvesDir.empty() || !Glib::file_test(lastHsvCurvesDir, Glib::FILE_TEST_EXISTS) || !Glib::file_test(lastHsvCurvesDir, Glib::FILE_TEST_IS_DIR)) {
        lastHsvCurvesDir = preferredPath;
    }

    if (lastToneCurvesDir.empty() || !Glib::file_test(lastToneCurvesDir, Glib::FILE_TEST_EXISTS) || !Glib::file_test(lastToneCurvesDir, Glib::FILE_TEST_IS_DIR)) {
        lastToneCurvesDir = preferredPath;
    }

    if (lastProfilingReferenceDir.empty() || !Glib::file_test(lastProfilingReferenceDir, Glib::FILE_TEST_EXISTS) || !Glib::file_test(lastProfilingReferenceDir, Glib::FILE_TEST_IS_DIR)) {
        lastProfilingReferenceDir = preferredPath;
    }

    if (loadSaveProfilePath.empty() || !Glib::file_test(loadSaveProfilePath, Glib::FILE_TEST_EXISTS) || !Glib::file_test(loadSaveProfilePath, Glib::FILE_TEST_IS_DIR)) {
        loadSaveProfilePath = preferredPath;
    }

    if (lastICCProfCreatorDir.empty() || !Glib::file_test(lastICCProfCreatorDir, Glib::FILE_TEST_EXISTS) || !Glib::file_test(lastICCProfCreatorDir, Glib::FILE_TEST_IS_DIR)) {
        lastICCProfCreatorDir = preferredPath;
    }
}

Glib::ustring Options::getPreferredProfilePath()
{
    if (!userProfilePath.empty()) {
        return userProfilePath;
    } else if (!globalProfilePath.empty()) {
        return globalProfilePath;
    } else {
        return "";
    }
}

/** @brief Get the absolute path of the given filename or the "Neutral" special value
  *
  *@param profName  path + filename of the procparam to look for. A filename without path can be provided for backward compatibility.
  *                 In this case, this parameter will be updated with the new format.
  *@return Send back the absolute path of the given filename or "Neutral" if "Neutral" has been set to profName. Implementor will have
  *        to test for this particular value. If the absolute path is invalid (e.g. the file doesn't exist), it will return an empty string.
  */
Glib::ustring Options::findProfilePath(Glib::ustring &profName)
{
    if (profName.empty()) {
        return "";
    }

    if (profName == DEFPROFILE_INTERNAL) {
        return profName;
    }

    if (profName == DEFPROFILE_DYNAMIC) {
        return profName;
    }

    Glib::ustring p = profName.substr(0, 4);

    if (p == "${U}") {
        // the path starts by the User virtual path
        p = getUserProfilePath();
        Glib::ustring fullPath = Glib::build_filename(p, profName.substr(5) + paramFileExtension);

        if (!p.empty() && Glib::file_test(fullPath, Glib::FILE_TEST_EXISTS)) {
            return Glib::path_get_dirname(fullPath);
        }
    } else if (p == "${G}") {
        // the path starts by the User virtual path
        p = getGlobalProfilePath();
        Glib::ustring fullPath = Glib::build_filename(p, profName.substr(5) + paramFileExtension);

        if (!p.empty() && Glib::file_test(fullPath, Glib::FILE_TEST_EXISTS)) {
            return Glib::path_get_dirname(fullPath);
        }
    } else {
        // compatibility case -> convert the path to the new format
        p = getUserProfilePath();
        Glib::ustring fullPath = Glib::build_filename(p, profName + paramFileExtension);

        if (!p.empty() && Glib::file_test(fullPath, Glib::FILE_TEST_EXISTS)) {
            // update the profile path
            profName = Glib::build_filename("${U}", profName);
            return Glib::path_get_dirname(fullPath);
        }

        p = getGlobalProfilePath();
        fullPath = Glib::build_filename(p, profName + paramFileExtension);

        if (!p.empty() && Glib::file_test(fullPath, Glib::FILE_TEST_EXISTS)) {
            profName = Glib::build_filename("${G}", profName);
            return Glib::path_get_dirname(fullPath);
        }
    }

    return "";

}

void Options::setDefaults()
{
    windowWidth = 1200;
    windowHeight = 680;
    windowX = 0;
    windowY = 0;
    windowMaximized = true;
    windowMonitor = 0;
    meowMonitor = -1;
    meowFullScreen = false;
    meowMaximized = true;
    meowWidth = 1200;
    meowHeight = 680;
    meowX = 0;
    meowY = 0;
    saveAsDialogWidth = 920;
    saveAsDialogHeight = 680;
    saveFormat.format = "jpg";
    saveFormat.jpegQuality = 92;
    saveFormat.jpegSubSamp = 2;
    saveFormat.pngBits = 8;
    saveFormat.tiffBits = 16;
    saveFormat.tiffFloat = false;
    saveFormat.tiffUncompressed = true;
    saveFormat.saveParams = true;

    saveFormatBatch.format = "jpg";
    saveFormatBatch.jpegQuality = 92;
    saveFormatBatch.jpegSubSamp = 2;
    saveFormatBatch.pngBits = 8;
    saveFormatBatch.tiffBits = 16;
    saveFormatBatch.tiffFloat = false;
    saveFormatBatch.tiffUncompressed = true;
    saveFormatBatch.saveParams = true;

    savePathTemplate = "%p1/converted/%f";
    savePathFolder = "";
    saveUsePathTemplate = true;
    defProfRaw = DEFPROFILE_RAW;
    defProfImg = DEFPROFILE_IMG;
    dateFormat = "%Y-%m-%d";
    adjusterMinDelay = 100;
    adjusterMaxDelay = 200;
    startupDir = STARTUPDIR_LAST;
    startupPath = "";
    useBundledProfiles = true;
    detailWindowWidth = -1;
    detailWindowHeight = -1;
    dirBrowserWidth = 260;
    dirBrowserHeight = 350;
    dirBrowserSortType = Gtk::SORT_ASCENDING;
    dir_browser_single_click = false;
    preferencesWidth = 800;
    preferencesHeight = 600;
    toolPanelWidth = 400;
    browserToolPanelWidth = 465;
    browserToolPanelHeight = 600;
    browserToolPanelOpened = true;
    browserDirPanelOpened = true;
    editorFilmStripOpened = true;
    inspectorDirPanelOpened = true;
    historyPanelWidth = 330;
    fontFamily = "default";
    fontSize = 10;
    CPFontFamily = "default";
    CPFontSize = 8;
    pseudoHiDPISupport = false;
    lastScale = 5;
    panAccelFactor = 5;
    rememberZoomAndPan = true;
    fbShowDateTime = true;
    fbShowBasicExif = true;
    fbShowExpComp = false;
#ifdef WIN32
    // use windows setting for visibility of hidden files/folders
    SHELLFLAGSTATE sft = { 0 };
    SHGetSettings(&sft, SSF_SHOWALLOBJECTS);
    fbShowHidden = sft.fShowAllObjects;
#else
    fbShowHidden = false;
#endif
    navRGBUnit = NavigatorUnit::PERCENT;
    navLCHUnit = NavigatorUnit::PERCENT;
    multiUser = true;
    profilePath = "profiles";
    loadSaveProfilePath = "";           // will be corrected in load as otherwise construction fails
    lastCopyMovePath = "";
    version = "0.0.0.0";                // temporary value; will be correctly set in RTWindow::on_realize
    thumbSize = 160;
    thumbSizeTab = 160;
    thumbSizeQueue = 160;
    sameThumbSize = false;               // preferring speed of switch between file browser and single editor tab
    thumbnailOrder = ThumbnailOrder::FILENAME;
    showHistory = true;
    showInfo = true;
    cropPPI = 600;
    showClippedHighlights = false;
    showClippedShadows = false;
    highlightThreshold = 253;           // was 254
    shadowThreshold = 8;                // was 0
    bgcolor = 1;
    language = DefaultLanguage;
    languageAutoDetect = langMgr.isOSLanguageDetectSupported();
    lastSaveAsPath = "";
    overwriteOutputFile = false;        // if TRUE, existing output JPGs/PNGs are overwritten, instead of adding ..-1.jpg, -2.jpg etc.
    theme = "Default";
    maxThumbnailHeight = 250;
    maxThumbnailWidth = 800;
    maxCacheEntries = 20000;
    thumbInterp = 1;
    autoSuffix = true;
    forceFormatOpts = true;
    saveMethodNum = 0;              // 0->immediate, 1->putToQueuHead, 2->putToQueueTail
    saveParamsFile = true;              // was false, but saving the procparams files next to the file make more sense when reorganizing file tree than in a cache
    saveParamsCache = false;            // there's no need to save the procparams files in a cache if saveParamsFile is true
    paramsLoadLocation = PLL_Input;     // was PLL_Cache
    params_out_embed = false;
    params_sidecar_strip_extension = false;
    procQueueEnabled = false;
    gimpDir = "";
    psDir = "";
    customEditorProg = "";
    CPBKeys = CPBKT_TID;
    editorToSendTo = 1;
    editor_out_dir = EDITOR_OUT_DIR_TEMP;
    editor_custom_out_dir = "";
    editor_float32 = false;
    editor_bypass_output_profile = false;
    favoriteDirs.clear();
    tpOpen.clear();
    autoSaveTpOpen = true;
    //crvOpen.clear ();
    parseExtensions.clear();
    favorites.clear();
    parseExtensionsEnabled.clear();
    parsedExtensions.clear();
    parsedExtensionsSet.clear();
    thumbnailZoomRatios.clear();
    thumbnailZoomRatios.push_back(0.2);
    thumbnailZoomRatios.push_back(0.3);
    thumbnailZoomRatios.push_back(0.45);
    thumbnailZoomRatios.push_back(0.6);
    thumbnailZoomRatios.push_back(0.8);
    thumbnailZoomRatios.push_back(1.0);
    overlayedFileNames = false;
    filmStripOverlayedFileNames = false;
    internalThumbIfUntouched = true;    // if TRUE, only fast, internal preview images are taken if the image is not edited yet
    showFileNames = true;
    filmStripShowFileNames = false;
    tabbedUI = false;
    multiDisplayMode = 0;
    filmstripBottom = true;
    histogramPosition = HISTOGRAM_POS_RIGHT;
    histogramRed = true;
    histogramGreen = true;
    histogramBlue = true;
    histogramLuma = false;
    histogramChroma = false;
    //histogramRAW = false;
    histogramBar = true;
    histogramHeight = 200;
    histogramDrawMode = 0;
    histogram_scaling_factor = 10.0;
    histogramScopeType = ScopeType::HISTOGRAM;
    histogramShowOptionButtons = false;
    histogramTraceBrightness = 1;
    curvebboxpos = 1;
    prevdemo = PD_Sidecar;
    rgbDenoiseThreadLimit = 0;
    clutCacheSize = 5;
    thumb_delay_update = false;
    thumb_lazy_caching = true;
    thumb_cache_processed = true;
    profile_append_mode = false;
    maxInspectorBuffers = 2; //  a rather conservative value for low specced systems...
    inspectorDelay = 0;
    serializeTiffRead = true;
    denoiseZoomedOut = true;
    wb_preview_mode = WB_BEFORE_HIGH_DETAIL;

    FileBrowserToolbarSingleRow = false;
    hideTPVScrollbar = false;
    whiteBalanceSpotSize = 8;
    showFilmStripToolBar = false;
    menuGroupRank = true;
    menuGroupLabel = true;
    menuGroupFileOperations = true;
    menuGroupProfileOperations = true;
    menuGroupExtProg = true;

    fastexport_resize_width              = 1920;
    fastexport_resize_height             = 1920;

    clutsDir = "./cluts";

    cutOverlayBrush = std::vector<double> (4);
    cutOverlayBrush[3] = 0.667;  // :-p

    navGuideBrush = std::vector<double> (4);
    //default to red
    navGuideBrush[0] = 1.0;
    navGuideBrush[1] = 0.0;
    navGuideBrush[2] = 0.0;
    navGuideBrush[3] = 1.0;

    sndEnable = true;
    sndLngEditProcDoneSecs = 3.0;
#ifdef __linux__
    sndBatchQueueDone = "complete";
    sndLngEditProcDone = "window-attention";
#endif

    rtSettings.darkFramesPath = "";
    rtSettings.flatFieldsPath = "";
#ifdef WIN32
    const gchar* sysRoot = g_getenv("SystemRoot");  // Returns e.g. "c:\Windows"

    if (sysRoot != NULL) {
        rtSettings.iccDirectory = Glib::ustring(sysRoot) + Glib::ustring("\\System32\\spool\\drivers\\color");
    } else {
        rtSettings.iccDirectory = "C:\\WINDOWS\\System32\\spool\\drivers\\color";
    }

#elif defined __APPLE__
    rtSettings.iccDirectory = "/library/ColorSync/Profiles/Displays";
#else
    rtSettings.iccDirectory = "/usr/share/color/icc";
#endif

    rtSettings.monitorIccDirectory = rtSettings.iccDirectory;

    rtSettings.printerProfile = Glib::ustring();
    rtSettings.printerIntent = rtengine::RI_RELATIVE;
    rtSettings.printerBPC = true;
    rtSettings.monitorProfile = Glib::ustring();
    rtSettings.monitorIntent = rtengine::RI_RELATIVE;
    rtSettings.monitorBPC = true;
    rtSettings.autoMonitorProfile = false;
    rtSettings.verbose = 0;
    rtSettings.HistogramWorking = false;

    lastIccDir = rtSettings.iccDirectory;
    lastDarkframeDir = rtSettings.darkFramesPath;
    lastFlatfieldDir = rtSettings.flatFieldsPath;

    // There is no reasonable default for curves. We can still suppose that they will take place
    // in a subdirectory of the user's own ProcParams presets, i.e. in a subdirectory
    // of the one pointed to by the "profile" field.
    // The following fields will then be initialized when "profile" will have its final value,
    // at the end of the "updatePaths" method.
    lastRgbCurvesDir = "";
    lastLabCurvesDir = "";
    lastPFCurvesDir = "";
    lastHsvCurvesDir = "";
    lastToneCurvesDir = "";
    lastProfilingReferenceDir = "";
    lastLensProfileDir = "";
    lastICCProfCreatorDir = "";
    gimpPluginShowInfoDialog = true;

    last_session_add_dir = "";
    last_session_loadsave_dir = "";
    
    maxRecentFolders = 15;
    rtSettings.lensfunDbDirectory = ""; // set also in main.cc and main-cli.cc

    rtSettings.thumbnail_inspector_mode = rtengine::Settings::ThumbnailInspectorMode::JPEG;
    rtSettings.thumbnail_inspector_raw_curve = rtengine::Settings::ThumbnailInspectorRawCurve::LINEAR;
    thumbnail_inspector_zoom_fit = false;
    thumbnail_inspector_show_info = false;
    thumbnail_inspector_enable_cms = false;
    thumbnail_inspector_show_histogram = false;
    thumbnail_inspector_hover = false;

    thumbnail_rating_mode = Options::ThumbnailRatingMode::XMP;
#if defined WIN32 || defined __APPLE__
    rtSettings.xmp_sidecar_style = rtengine::Settings::XmpSidecarStyle::STD;
#else
    rtSettings.xmp_sidecar_style = rtengine::Settings::XmpSidecarStyle::EXT;
#endif
    rtSettings.metadata_xmp_sync = rtengine::Settings::MetadataXmpSync::READ;
    rtSettings.exiftool_path = "exiftool";
#ifdef WIN32
    rtSettings.exiftool_path += ".exe";
#endif
    rtSettings.thread_pool_size = 0;
    rtSettings.ctl_scripts_fast_preview = true;
    rtSettings.imgio_raw_cache_size = 10;
    
    show_exiftool_makernotes = false;

    browser_width_for_inspector = 0;

    // batch_queue_use_profile = false;
    // batch_queue_profile_path = "";

    toolpanels_disable = false;
    adjuster_force_linear = false;

    error_message_duration = 5000;
    max_error_messages = 3;

    falseColorsMap = {
        {2, "#FFFFFF"},
        {10, "#0000FF"},
        {20, "#2290FF"},
        {42, "#4B4B4B"},
        {48, "#FF11FC"},
        {52, "#7B7B7B"},
        {58, "#00FF00"},
        {78, "#ADADAD"},
        {84, "#AEAE00"},
        {94, "#FFFF00"},
        {100, "#FF7F00"},
        {108, "#FF0000"}
    };
    clipped_highlights_color = "";
    clipped_shadows_color = "";

    renaming = RenameOptions();
    sidecar_autosave_interval = 0;

    editor_keyboard_scroll_step = 50;
    adjuster_shortcut_scrollwheel_factor = 4;

    remember_exif_filter_settings = false;
    last_exif_filter_settings = ExifFilterSettings();

    theme_bg_color.assign({72, 72, 72});
    theme_fg_color.assign({170, 170, 170});
    theme_hl_color.assign({227, 146, 67});

    rtSettings.os_monitor_profile = rtengine::Settings::StdMonitorProfile::SRGB;
}


Options* Options::copyFrom(Options* other)
{
    *this = *other;
    return this;
}

void Options::filterOutParsedExtensions()
{
    parsedExtensions.clear();
    parsedExtensionsSet.clear();

    for (unsigned int i = 0; i < parseExtensions.size(); i++)
        if (parseExtensionsEnabled[i]) {
            parsedExtensions.push_back(parseExtensions[i].lowercase());
            parsedExtensionsSet.emplace(parseExtensions[i].lowercase());
        }
}

void Options::readFromFile(Glib::ustring fname)
{
    setlocale(LC_NUMERIC, "C");  // to set decimal point to "."

    Glib::KeyFile keyFile;

    if (!Glib::file_test(fname, Glib::FILE_TEST_EXISTS)) {
        Glib::ustring msg = Glib::ustring::compose("Options file %1 does not exist", fname);
        throw Error(msg);
    }

    try {
        if (keyFile.load_from_file(fname)) {

// --------------------------------------------------------------------------------------------------------

            if (keyFile.has_group("General")) {
                if (keyFile.has_key("General", "TabbedEditor")) {
                    tabbedUI = keyFile.get_boolean("General", "TabbedEditor");
                }

                if (keyFile.has_key("General", "StartupDirectory")) {
                    if (keyFile.get_string("General", "StartupDirectory") == "home") {
                        startupDir = STARTUPDIR_HOME;
                    } else if (keyFile.get_string("General", "StartupDirectory") == "current") {
                        startupDir = STARTUPDIR_CURRENT;
                    } else if (keyFile.get_string("General", "StartupDirectory") == "last") {
                        startupDir = STARTUPDIR_LAST;
                    } else if (keyFile.get_string("General", "StartupDirectory") == "custom") {
                        startupDir = STARTUPDIR_CUSTOM;
                    }
                }

                if (keyFile.has_key("General", "StartupPath")) {
                    startupPath = keyFile.get_string("General", "StartupPath");
                }

                if (keyFile.has_key("General", "DateFormat")) {
                    dateFormat = keyFile.get_string("General", "DateFormat");
                }

                if (keyFile.has_key("General", "AdjusterMinDelay")) {
                    adjusterMinDelay = keyFile.get_integer("General", "AdjusterMinDelay");
                }

                if (keyFile.has_key("General", "AdjusterMaxDelay")) {
                    adjusterMaxDelay = keyFile.get_integer("General", "AdjusterMaxDelay");
                }

                if (keyFile.has_key("General", "MultiUser")) {
                    multiUser = keyFile.get_boolean("General", "MultiUser");
                }

                if (keyFile.has_key("General", "Version")) {
                    version = keyFile.get_string("General", "Version");
                }

                if (keyFile.has_key("General", "Language")) {
                    language = keyFile.get_string("General", "Language");
                }

                if (keyFile.has_key("General", "LanguageAutoDetect")) {
                    languageAutoDetect = keyFile.get_boolean("General", "LanguageAutoDetect");
                }

                if (keyFile.has_key("General", "Theme")) {
                    theme = keyFile.get_string("General", "Theme");
                }

                if (keyFile.has_key("General", "DarkFramesPath")) {
                    rtSettings.darkFramesPath = keyFile.get_string("General", "DarkFramesPath");
                }

                if (keyFile.has_key("General", "FlatFieldsPath")) {
                    rtSettings.flatFieldsPath = keyFile.get_string("General", "FlatFieldsPath");
                }

                if (keyFile.has_key("General", "Verbose")) {
                    try {
                        rtSettings.verbose = keyFile.get_integer("General", "Verbose");
                    } catch (Glib::Error &e) {
                        rtSettings.verbose = keyFile.get_boolean("General", "Verbose");
                    }
                }

                if (keyFile.has_key("General", "ErrorMessageDuration")) {
                    error_message_duration = keyFile.get_integer("General", "ErrorMessageDuration");
                }

                if (keyFile.has_key("General", "MaxErrorMessages")) {
                    max_error_messages = keyFile.get_integer("General", "MaxErrorMessages");
                }

                if (keyFile.has_key("General", "EditorKeyboardScrollStep")) {
                    editor_keyboard_scroll_step = keyFile.get_integer("General", "EditorKeyboardScrollStep");
                }

                if (keyFile.has_key("General", "AdjusterShortcutScrollWheelFactor")) {
                    adjuster_shortcut_scrollwheel_factor = keyFile.get_integer("General", "AdjusterShortcutScrollWheelFactor");
                }
            }

            if (keyFile.has_group("External Editor")) {
                if (keyFile.has_key("External Editor", "EditorKind")) {
                    editorToSendTo = keyFile.get_integer("External Editor", "EditorKind");
                }

                if (keyFile.has_key("External Editor", "GimpDir")) {
                    gimpDir = keyFile.get_string("External Editor", "GimpDir");
                }

                if (keyFile.has_key("External Editor", "PhotoshopDir")) {
                    psDir = keyFile.get_string("External Editor", "PhotoshopDir");
                }

                if (keyFile.has_key("External Editor", "CustomEditor")) {
                    customEditorProg = keyFile.get_string("External Editor", "CustomEditor");
                }

                if (keyFile.has_key("External Editor", "OutputDir")) {
                    int v = keyFile.get_integer("External Editor", "OutputDir");
                    if (v < int(EDITOR_OUT_DIR_TEMP) || v > int(EDITOR_OUT_DIR_CUSTOM)) {
                        editor_out_dir = EDITOR_OUT_DIR_TEMP;
                    } else {
                        editor_out_dir = EditorOutDir(v);
                    }
                }

                if (keyFile.has_key("External Editor", "CustomOutputDir")) {
                    editor_custom_out_dir = keyFile.get_string("External Editor", "CustomOutputDir");
                }

                if (keyFile.has_key("External Editor", "Float32")) {
                    editor_float32 = keyFile.get_boolean("External Editor", "Float32");
                }

                if (keyFile.has_key("External Editor", "BypassOutputProfile")) {
                    editor_bypass_output_profile = keyFile.get_boolean("External Editor", "BypassOutputProfile");
                }
            }

            if (keyFile.has_group("Output")) {
                if (keyFile.has_key("Output", "Format")) {
                    saveFormat.format = keyFile.get_string("Output", "Format");
                }

                if (keyFile.has_key("Output", "JpegQuality")) {
                    saveFormat.jpegQuality = keyFile.get_integer("Output", "JpegQuality");
                }

                if (keyFile.has_key("Output", "JpegSubSamp")) {
                    saveFormat.jpegSubSamp = keyFile.get_integer("Output", "JpegSubSamp");
                }

                if (keyFile.has_key("Output", "PngBps")) {
                    saveFormat.pngBits = keyFile.get_integer("Output", "PngBps");
                }

                if (keyFile.has_key("Output", "TiffBps")) {
                    saveFormat.tiffBits = keyFile.get_integer("Output", "TiffBps");
                }

                if (keyFile.has_key ("Output", "TiffFloat")) {
                    saveFormat.tiffFloat = keyFile.get_boolean ("Output", "TiffFloat");
                }

                if (keyFile.has_key("Output", "TiffUncompressed")) {
                    saveFormat.tiffUncompressed = keyFile.get_boolean("Output", "TiffUncompressed");
                }

                if (keyFile.has_key("Output", "SaveProcParams")) {
                    saveFormat.saveParams = keyFile.get_boolean("Output", "SaveProcParams");
                }


                if (keyFile.has_key("Output", "FormatBatch")) {
                    saveFormatBatch.format = keyFile.get_string("Output", "FormatBatch");
                }

                if (keyFile.has_key("Output", "JpegQualityBatch")) {
                    saveFormatBatch.jpegQuality = keyFile.get_integer("Output", "JpegQualityBatch");
                }

                if (keyFile.has_key("Output", "JpegSubSampBatch")) {
                    saveFormatBatch.jpegSubSamp = keyFile.get_integer("Output", "JpegSubSampBatch");
                }

                if (keyFile.has_key("Output", "PngBpsBatch")) {
                    saveFormatBatch.pngBits = keyFile.get_integer("Output", "PngBpsBatch");
                }

                if (keyFile.has_key("Output", "TiffBpsBatch")) {
                    saveFormatBatch.tiffBits = keyFile.get_integer("Output", "TiffBpsBatch");
                }

                if (keyFile.has_key ("Output", "TiffFloatBatch")) {
                    saveFormatBatch.tiffFloat = keyFile.get_boolean ("Output", "TiffFloatBatch");
                }

                if (keyFile.has_key("Output", "TiffUncompressedBatch")) {
                    saveFormatBatch.tiffUncompressed = keyFile.get_boolean("Output", "TiffUncompressedBatch");
                }

                if (keyFile.has_key("Output", "SaveProcParamsBatch")) {
                    saveFormatBatch.saveParams = keyFile.get_boolean("Output", "SaveProcParamsBatch");
                }

                if (keyFile.has_key("Output", "Path")) {
                    savePathTemplate = keyFile.get_string("Output", "Path");
                }

                if (keyFile.has_key("Output", "PathTemplate")) {
                    savePathTemplate = keyFile.get_string("Output", "PathTemplate");
                }

                if (keyFile.has_key("Output", "PathFolder")) {
                    savePathFolder = keyFile.get_string("Output", "PathFolder");
                }

                if (keyFile.has_key("Output", "AutoSuffix")) {
                    autoSuffix = keyFile.get_boolean("Output", "AutoSuffix");
                }

                if (keyFile.has_key("Output", "ForceFormatOpts")) {
                    forceFormatOpts = keyFile.get_boolean("Output", "ForceFormatOpts");
                }

                if (keyFile.has_key("Output", "SaveMethodNum")) {
                    saveMethodNum = keyFile.get_integer("Output", "SaveMethodNum");
                }

                if (keyFile.has_key("Output", "UsePathTemplate")) {
                    saveUsePathTemplate = keyFile.get_boolean("Output", "UsePathTemplate");
                }

                if (keyFile.has_key("Output", "LastSaveAsPath")) {
                    lastSaveAsPath = keyFile.get_string("Output", "LastSaveAsPath");
                }

                if (keyFile.has_key("Output", "OverwriteOutputFile")) {
                    overwriteOutputFile = keyFile.get_boolean("Output", "OverwriteOutputFile");
                }

                if (keyFile.has_key("Output", "ProcParamsAutosaveInterval")) {
                    sidecar_autosave_interval = keyFile.get_integer("Output", "ProcParamsAutosaveInterval");
                }
            }

            if (keyFile.has_group("Profiles")) {
                if (keyFile.has_key("Profiles", "Directory")) {
                    profilePath = keyFile.get_string("Profiles", "Directory");
                }

                if (keyFile.has_key("Profiles", "UseBundledProfiles")) {
                    useBundledProfiles = keyFile.get_boolean("Profiles", "UseBundledProfiles");
                }

                if (keyFile.has_key("Profiles", "LoadSaveProfilePath")) {
                    loadSaveProfilePath = keyFile.get_string("Profiles", "LoadSaveProfilePath");
                }

                if (keyFile.has_key("Profiles", "RawDefault")) {
                    defProfRaw = keyFile.get_string("Profiles", "RawDefault");
                }

                if (keyFile.has_key("Profiles", "ImgDefault")) {
                    defProfImg = keyFile.get_string("Profiles", "ImgDefault");
                }

                if (keyFile.has_key("Profiles", "AppendMode")) {
                    profile_append_mode = keyFile.get_boolean("Profiles", "AppendMode");
                }

                if (keyFile.has_key("Profiles", "SaveParamsWithFile")) {
                    saveParamsFile = keyFile.get_boolean("Profiles", "SaveParamsWithFile");
                }

                if (keyFile.has_key("Profiles", "SaveParamsToCache")) {
                    saveParamsCache = keyFile.get_boolean("Profiles", "SaveParamsToCache");
                }

                if (keyFile.has_key("Profiles", "LoadParamsFromLocation")) {
                    paramsLoadLocation = (PPLoadLocation)keyFile.get_integer("Profiles", "LoadParamsFromLocation");
                }

                if (keyFile.has_key("Profiles", "EmbedParamsInMetadata")) {
                    params_out_embed = keyFile.get_boolean("Profiles", "EmbedParamsInMetadata");
                }

                if (keyFile.has_key("Profiles", "ParamsSidecarStripExtension")) {
                    params_sidecar_strip_extension = keyFile.get_boolean("Profiles", "ParamsSidecarStripExtension");
                }
                
                if (keyFile.has_key("Profiles", "CustomProfileBuilder")) {
                    CPBPath = keyFile.get_string("Profiles", "CustomProfileBuilder");  // for backward compatibility only
                }

                if (keyFile.has_key("Profiles", "CustomProfileBuilderPath")) {
                    CPBPath = keyFile.get_string("Profiles", "CustomProfileBuilderPath");
                }

                if (keyFile.has_key("Profiles", "CustomProfileBuilderKeys")) {
                    CPBKeys = (CPBKeyType)keyFile.get_integer("Profiles", "CustomProfileBuilderKeys");
                }
            }

            if (keyFile.has_group("File Browser")) {
                if (keyFile.has_key("File Browser", "ThumbnailSize")) {
                    thumbSize = keyFile.get_integer("File Browser", "ThumbnailSize");
                }

                if (keyFile.has_key("File Browser", "ThumbnailSizeTab")) {
                    thumbSizeTab = keyFile.get_integer("File Browser", "ThumbnailSizeTab");
                }

                if (keyFile.has_key("File Browser", "ThumbnailSizeQueue")) {
                    thumbSizeQueue = keyFile.get_integer("File Browser", "ThumbnailSizeQueue");
                }

                if (keyFile.has_key("File Browser", "SameThumbSize")) {
                    sameThumbSize = keyFile.get_integer("File Browser", "SameThumbSize");
                }

                if (keyFile.has_key("File Browser", "ThumbnailOrder")) {
                    thumbnailOrder = ThumbnailOrder(keyFile.get_integer("File Browser", "ThumbnailOrder"));
                }
                
                if (keyFile.has_key("File Browser", "BrowserShowsDate")) {
                    fbShowDateTime = keyFile.get_boolean("File Browser", "BrowserShowsDate");
                }

                if (keyFile.has_key("File Browser", "BrowserShowsExif")) {
                    fbShowBasicExif = keyFile.get_boolean("File Browser", "BrowserShowsExif");
                }

                if (keyFile.has_key("File Browser", "BrowserShowsExpComp")) {
                    fbShowExpComp = keyFile.get_boolean("File Browser", "BrowserShowsExpComp");
                }

#ifndef WIN32
                if (keyFile.has_key("File Browser", "BrowserShowsHidden")) {
                    fbShowHidden = keyFile.get_boolean("File Browser", "BrowserShowsHidden");
                }
#endif

                if (keyFile.has_key("File Browser", "MaxPreviewHeight")) {
                    maxThumbnailHeight = keyFile.get_integer("File Browser", "MaxPreviewHeight");
                }

                if (keyFile.has_key("File Browser", "MaxPreviewWidth")) {
                    maxThumbnailWidth = keyFile.get_integer("File Browser", "MaxPreviewWidth");
                }
                
                if (keyFile.has_key("File Browser", "MaxCacheEntries")) {
                    maxCacheEntries = keyFile.get_integer("File Browser", "MaxCacheEntries");
                }

                if (keyFile.has_key("File Browser", "ParseExtensions")) {
                    auto l = keyFile.get_string_list("File Browser", "ParseExtensions");
                    if (!l.empty()) {
                        parseExtensions = l;
                    }
                }

                if (keyFile.has_key("File Browser", "ParseExtensionsEnabled")) {
                    auto l = keyFile.get_integer_list("File Browser", "ParseExtensionsEnabled");
                    if (!l.empty()) {
                        parseExtensionsEnabled = l;
                    }
                }

                if (keyFile.has_key("File Browser", "ThumbnailInterpolation")) {
                    thumbInterp = keyFile.get_integer("File Browser", "ThumbnailInterpolation");
                }

                if (keyFile.has_key("File Browser", "FavoriteDirs")) {
                    favoriteDirs = keyFile.get_string_list("File Browser", "FavoriteDirs");
                }

                if (keyFile.has_key("File Browser", "ThumbnailZoomRatios")) {
                    thumbnailZoomRatios = keyFile.get_double_list("File Browser", "ThumbnailZoomRatios");
                }

                if (keyFile.has_key("File Browser", "OverlayedFileNames")) {
                    overlayedFileNames = keyFile.get_boolean("File Browser", "OverlayedFileNames");
                }

                if (keyFile.has_key("File Browser", "FilmStripOverlayedFileNames")) {
                    filmStripOverlayedFileNames = keyFile.get_boolean("File Browser", "FilmStripOverlayedFileNames");
                }

                if (keyFile.has_key("File Browser", "ShowFileNames")) {
                    showFileNames = keyFile.get_boolean("File Browser", "ShowFileNames");
                }

                if (keyFile.has_key("File Browser", "FilmStripShowFileNames")) {
                    filmStripShowFileNames = keyFile.get_boolean("File Browser", "FilmStripShowFileNames");
                }

                if (keyFile.has_key("File Browser", "InternalThumbIfUntouched")) {
                    internalThumbIfUntouched = keyFile.get_boolean("File Browser", "InternalThumbIfUntouched");
                }

                // if (keyFile.has_key("File Browser", "menuGroupRank")) {
                //     menuGroupRank = keyFile.get_boolean("File Browser", "menuGroupRank");
                // }

                // if (keyFile.has_key("File Browser", "menuGroupLabel")) {
                //     menuGroupLabel = keyFile.get_boolean("File Browser", "menuGroupLabel");
                // }

                // if (keyFile.has_key("File Browser", "menuGroupFileOperations")) {
                //     menuGroupFileOperations = keyFile.get_boolean("File Browser", "menuGroupFileOperations");
                // }

                // if (keyFile.has_key("File Browser", "menuGroupProfileOperations")) {
                //     menuGroupProfileOperations = keyFile.get_boolean("File Browser", "menuGroupProfileOperations");
                // }

                // if (keyFile.has_key("File Browser", "menuGroupExtProg")) {
                //     menuGroupExtProg = keyFile.get_boolean("File Browser", "menuGroupExtProg");
                // }

                if (keyFile.has_key("File Browser", "MaxRecentFolders")) {
                    maxRecentFolders = keyFile.get_integer("File Browser", "MaxRecentFolders");
                }

                recentFolders.reserve(maxRecentFolders + 10);  // reserve some more than maxRecentFolders, because at runtime it stores more than that

                if (keyFile.has_key("File Browser", "RecentFolders")) {
                    recentFolders = keyFile.get_string_list("File Browser", "RecentFolders");
                }

                if (keyFile.has_key("File Browser", "ThumbnailRatingMode")) {
                    auto s = keyFile.get_string("File Browser", "ThumbnailRatingMode");
                    if (s == "procparams") {
                        thumbnail_rating_mode = ThumbnailRatingMode::PROCPARAMS;
                    } else if (s == "xmp") {
                        thumbnail_rating_mode = ThumbnailRatingMode::XMP;
                    } else {
                        thumbnail_rating_mode = ThumbnailRatingMode::PROCPARAMS;
                    }
                }
            }

            if (keyFile.has_group("Clipping Indication")) {
                if (keyFile.has_key("Clipping Indication", "HighlightThreshold")) {
                    highlightThreshold = keyFile.get_integer("Clipping Indication", "HighlightThreshold");
                }

                if (keyFile.has_key("Clipping Indication", "ShadowThreshold")) {
                    shadowThreshold = keyFile.get_integer("Clipping Indication", "ShadowThreshold");
                }
            }

            if (keyFile.has_group("Performance")) {
                if (keyFile.has_key("Performance", "RgbDenoiseThreadLimit")) {
                    rgbDenoiseThreadLimit = keyFile.get_integer("Performance", "RgbDenoiseThreadLimit");
                }

                if (keyFile.has_key("Performance", "ClutCacheSize")) {
                    clutCacheSize = keyFile.get_integer("Performance", "ClutCacheSize");
                }

                if (keyFile.has_key("Performance", "MaxInspectorBuffers")) {
                    maxInspectorBuffers = keyFile.get_integer("Performance", "MaxInspectorBuffers");
                }

                if (keyFile.has_key("Performance", "InspectorDelay")) {
                    inspectorDelay = keyFile.get_integer("Performance", "InspectorDelay");
                }

                if (keyFile.has_key("Performance", "PreviewDemosaicFromSidecar")) {
                    prevdemo = (prevdemo_t)keyFile.get_integer("Performance", "PreviewDemosaicFromSidecar");
                }

                if (keyFile.has_key("Performance", "SerializeTiffRead")) {
                    serializeTiffRead = keyFile.get_boolean("Performance", "SerializeTiffRead");
                }

                if (keyFile.has_key("Performance", "DenoiseZoomedOut")) {
                    denoiseZoomedOut = keyFile.get_boolean("Performance", "DenoiseZoomedOut");
                }

                if (keyFile.has_key("Performance", "WBPreviewMode")) {
                    int v = keyFile.get_integer("Performance", "WBPreviewMode");
                    wb_preview_mode = WBPreviewMode(rtengine::LIM(v, int(WB_AFTER), int(WB_BEFORE_HIGH_DETAIL)));
                }

                if (keyFile.has_key("Performance", "ThumbUpdateThreadLimit")) {
                    rtSettings.thread_pool_size = keyFile.get_integer("Performance", "ThumbUpdateThreadLimit");
                }

                if (keyFile.has_key("Performance", "ThumbDelayUpdate")) {
                    thumb_delay_update = keyFile.get_boolean("Performance", "ThumbDelayUpdate");
                }
                
                if (keyFile.has_key("Performance", "ThumbLazyCaching")) {
                    thumb_lazy_caching = keyFile.get_boolean("Performance", "ThumbLazyCaching");
                }

                if (keyFile.has_key("Performance", "ThumbCacheProcessed")) {
                    thumb_cache_processed = keyFile.get_boolean("Performance", "ThumbCacheProcessed");
                }

                if (keyFile.has_key("Performance", "CTLScriptsFastPreview")) {
                    rtSettings.ctl_scripts_fast_preview = keyFile.get_boolean("Performance", "CTLScriptsFastPreview");
                }

                if (keyFile.has_key("Performance", "RAWImageIOCacheSize")) {
                    rtSettings.imgio_raw_cache_size = keyFile.get_integer("Performance", "RAWImageIOCacheSize");
                }
            }

            if (keyFile.has_group("Inspector")) {
                if (keyFile.has_key("Inspector", "Mode")) {
                    rtSettings.thumbnail_inspector_mode = static_cast<rtengine::Settings::ThumbnailInspectorMode>(keyFile.get_integer("Inspector", "Mode"));
                }

                if (keyFile.has_key("Inspector", "RawCurve")) {
                    rtSettings.thumbnail_inspector_raw_curve = static_cast<rtengine::Settings::ThumbnailInspectorRawCurve>(keyFile.get_integer("Inspector", "RawCurve"));
                }

                if (keyFile.has_key("Inspector", "ZoomFit")) {
                    thumbnail_inspector_zoom_fit = keyFile.get_boolean("Inspector", "ZoomFit");
                }
                
                if (keyFile.has_key("Inspector", "ShowInfo")) {
                    thumbnail_inspector_show_info = keyFile.get_boolean("Inspector", "ShowInfo");
                }

                if (keyFile.has_key("Inspector", "ShowHistogram")) {
                    thumbnail_inspector_show_histogram = keyFile.get_boolean("Inspector", "ShowHistogram");
                }
                
                if (keyFile.has_key("Inspector", "EnableCMS")) {
                    thumbnail_inspector_enable_cms = keyFile.get_boolean("Inspector", "EnableCMS");
                }

                if (keyFile.has_key("Inspector", "BrowserWidth")) {
                    browser_width_for_inspector = keyFile.get_integer("Inspector", "BrowserWidth");
                }

                if (keyFile.has_key("Inspector", "ThumbnailHover")) {
                    thumbnail_inspector_hover = keyFile.get_boolean("Inspector", "ThumbnailHover");
                }
            }

            if (keyFile.has_group("GUI")) {
                // if (keyFile.has_key("GUI", "Favorites")) {
                //     favorites = keyFile.get_string_list("GUI", "Favorites");
                // }

                if (keyFile.has_key("GUI", "WindowWidth")) {
                    windowWidth = keyFile.get_integer("GUI", "WindowWidth");
                }

                if (keyFile.has_key("GUI", "WindowHeight")) {
                    windowHeight = keyFile.get_integer("GUI", "WindowHeight");
                }

                if (keyFile.has_key("GUI", "WindowX")) {
                    windowX = keyFile.get_integer("GUI", "WindowX");
                }

                if (keyFile.has_key("GUI", "WindowY")) {
                    windowY = keyFile.get_integer("GUI", "WindowY");
                }

                if (keyFile.has_key("GUI", "WindowMonitor")) {
                    windowMonitor = keyFile.get_integer("GUI", "WindowMonitor");
                }

                if (keyFile.has_key("GUI", "MeowMonitor")) {
                    meowMonitor = keyFile.get_integer("GUI", "MeowMonitor");
                }

                if (keyFile.has_key("GUI", "MeowFullScreen")) {
                    meowFullScreen = keyFile.get_boolean("GUI", "MeowFullScreen");
                }

                if (keyFile.has_key("GUI", "MeowMaximized")) {
                    meowMaximized = keyFile.get_boolean("GUI", "MeowMaximized");
                }

                if (keyFile.has_key("GUI", "MeowWidth")) {
                    meowWidth = keyFile.get_integer("GUI", "MeowWidth");
                }

                if (keyFile.has_key("GUI", "MeowHeight")) {
                    meowHeight = keyFile.get_integer("GUI", "MeowHeight");
                }

                if (keyFile.has_key("GUI", "MeowX")) {
                    meowX = keyFile.get_integer("GUI", "MeowX");
                }

                if (keyFile.has_key("GUI", "MeowY")) {
                    meowY = keyFile.get_integer("GUI", "MeowY");
                }

                if (keyFile.has_key("GUI", "WindowMaximized")) {
                    windowMaximized = keyFile.get_boolean("GUI", "WindowMaximized");
                }

                if (keyFile.has_key("GUI", "DetailWindowWidth")) {
                    detailWindowWidth = keyFile.get_integer("GUI", "DetailWindowWidth");
                }

                if (keyFile.has_key("GUI", "DetailWindowHeight")) {
                    detailWindowHeight = keyFile.get_integer("GUI", "DetailWindowHeight");
                }

                if (keyFile.has_key("GUI", "DirBrowserWidth")) {
                    dirBrowserWidth = keyFile.get_integer("GUI", "DirBrowserWidth");
                }

                if (keyFile.has_key("GUI", "DirBrowserHeight")) {
                    dirBrowserHeight = keyFile.get_integer("GUI", "DirBrowserHeight");
                }

                if (keyFile.has_key("GUI", "SortType")) {
                    dirBrowserSortType = static_cast<Gtk::SortType>(keyFile.get_integer("GUI", "SortType"));
                }

                if (keyFile.has_key("GUI", "DirBrowserSingleClick")) {
                    dir_browser_single_click = keyFile.get_boolean("GUI", "DirBrowserSingleClick");
                }

                if (keyFile.has_key("GUI", "PreferencesWidth")) {
                    preferencesWidth = keyFile.get_integer("GUI", "PreferencesWidth");
                }

                if (keyFile.has_key("GUI", "PreferencesHeight")) {
                    preferencesHeight = keyFile.get_integer("GUI", "PreferencesHeight");
                }

                if (keyFile.has_key("GUI", "SaveAsDialogWidth")) {
                    saveAsDialogWidth = keyFile.get_integer("GUI", "SaveAsDialogWidth");
                }

                if (keyFile.has_key("GUI", "SaveAsDialogHeight")) {
                    saveAsDialogHeight = keyFile.get_integer("GUI", "SaveAsDialogHeight");
                }

                if (keyFile.has_key("GUI", "ToolPanelWidth")) {
                    toolPanelWidth = keyFile.get_integer("GUI", "ToolPanelWidth");
                }

                if (keyFile.has_key("GUI", "BrowserToolPanelWidth")) {
                    browserToolPanelWidth = keyFile.get_integer("GUI", "BrowserToolPanelWidth");
                }

                if (keyFile.has_key("GUI", "BrowserToolPanelHeight")) {
                    browserToolPanelHeight = keyFile.get_integer("GUI", "BrowserToolPanelHeight");
                }

                if (keyFile.has_key("GUI", "BrowserToolPanelOpened")) {
                    browserToolPanelOpened = keyFile.get_boolean("GUI", "BrowserToolPanelOpened");
                }

                if (keyFile.has_key("GUI", "BrowserDirPanelOpened")) {
                    browserDirPanelOpened = keyFile.get_boolean("GUI", "BrowserDirPanelOpened");
                }

                if (keyFile.has_key("GUI", "EditorFilmStripOpened")) {
                    editorFilmStripOpened = keyFile.get_boolean("GUI", "EditorFilmStripOpened");
                }

                if (keyFile.has_key("GUI", "InspectorDirPanelOpened")) {
                    inspectorDirPanelOpened = keyFile.get_boolean("GUI", "InspectorDirPanelOpened");
                }

                if (keyFile.has_key("GUI", "HistoryPanelWidth")) {
                    historyPanelWidth = keyFile.get_integer("GUI", "HistoryPanelWidth");
                }

                if (keyFile.has_key("GUI", "FontFamily")) {
                    fontFamily = keyFile.get_string("GUI", "FontFamily");
                }

                if (keyFile.has_key("GUI", "FontSize")) {
                    fontSize = keyFile.get_integer("GUI", "FontSize");
                }

                if (keyFile.has_key("GUI", "CPFontFamily")) {
                    CPFontFamily = keyFile.get_string("GUI", "CPFontFamily");
                }

                if (keyFile.has_key("GUI", "CPFontSize")) {
                    CPFontSize = keyFile.get_integer("GUI", "CPFontSize");
                }

                if (keyFile.has_key("GUI", "PseudoHiDPISupport")) {
                	pseudoHiDPISupport = keyFile.get_boolean("GUI", "PseudoHiDPISupport");
                }

                if (keyFile.has_key("GUI", "LastPreviewScale")) {
                    lastScale = keyFile.get_integer("GUI", "LastPreviewScale");
                }

                if (keyFile.has_key("GUI", "PanAccelFactor")) {
                    panAccelFactor = keyFile.get_integer("GUI", "PanAccelFactor");
                }

                if (keyFile.has_key("GUI", "RememberZoomAndPan")) {
                    rememberZoomAndPan = keyFile.get_boolean("GUI", "RememberZoomAndPan");
                }

                if (keyFile.has_key("GUI", "ShowHistory")) {
                    showHistory = keyFile.get_boolean("GUI", "ShowHistory");
                }

                if (keyFile.has_key("GUI", "ShowInfo")) {
                    showInfo = keyFile.get_boolean("GUI", "ShowInfo");
                }

                if (keyFile.has_key("GUI", "FilmStripBottom")) {
                    filmstripBottom = keyFile.get_boolean("GUI", "FilmStripBottom");
                }

                if (keyFile.has_key("GUI", "ShowClippedHighlights")) {
                    showClippedHighlights = keyFile.get_boolean("GUI", "ShowClippedHighlights");
                }

                if (keyFile.has_key("GUI", "ShowClippedShadows")) {
                    showClippedShadows = keyFile.get_boolean("GUI", "ShowClippedShadows");
                }

                if (keyFile.has_key("GUI", "FrameColor")) {
                    bgcolor = rtengine::LIM(keyFile.get_integer("GUI", "FrameColor"), 1, 3);
                }

                if (keyFile.has_key("GUI", "ProcessingQueueEnbled")) {
                    procQueueEnabled = keyFile.get_boolean("GUI", "ProcessingQueueEnbled");
                }

                if (keyFile.has_key("GUI", "ToolPanelsExpanded")) {
                    tpOpen = keyFile.get_integer_list("GUI", "ToolPanelsExpanded");
                }

                if (keyFile.has_key("GUI", "ToolPanelsExpandedAutoSave")) {
                    autoSaveTpOpen = keyFile.get_boolean("GUI", "ToolPanelsExpandedAutoSave");
                }

                if (keyFile.has_key("GUI", "MultiDisplayMode")) {
                    multiDisplayMode = keyFile.get_integer("GUI", "MultiDisplayMode");
                }

                //if (keyFile.has_key ("GUI", "CurvePanelsExpanded")) crvOpen = keyFile.get_integer_list ("GUI", "CurvePanelsExpanded");
                // if (keyFile.has_key("GUI", "CutOverlayBrush")) {
                //     cutOverlayBrush = keyFile.get_double_list("GUI", "CutOverlayBrush");
                // }

                // if (keyFile.has_key("GUI", "NavGuideBrush")) {
                //     navGuideBrush = keyFile.get_double_list("GUI", "NavGuideBrush");
                // }

                if (keyFile.has_key("GUI", "HistogramPosition")) {
                    histogramPosition = keyFile.get_integer("GUI", "HistogramPosition");
                }

                if (keyFile.has_key("GUI", "HistogramRed")) {
                    histogramRed = keyFile.get_boolean("GUI", "HistogramRed");
                }

                if (keyFile.has_key("GUI", "HistogramGreen")) {
                    histogramGreen = keyFile.get_boolean("GUI", "HistogramGreen");
                }

                if (keyFile.has_key("GUI", "HistogramBlue")) {
                    histogramBlue = keyFile.get_boolean("GUI", "HistogramBlue");
                }

                if (keyFile.has_key("GUI", "HistogramLuma")) {
                    histogramLuma = keyFile.get_boolean("GUI", "HistogramLuma");
                }

                if (keyFile.has_key("GUI", "HistogramChroma")) {
                    histogramChroma = keyFile.get_boolean("GUI", "HistogramChroma");
                }

                if (keyFile.has_key("GUI", "HistogramRAW")) {
                    //histogramRAW = keyFile.get_boolean("GUI", "HistogramRAW");
                    if (keyFile.get_boolean("GUI", "HistogramRAW")) {
                        histogramScopeType = ScopeType::HISTOGRAM_RAW;
                    }
                }

                if (keyFile.has_key("GUI", "HistogramBar")) {
                    histogramBar = keyFile.get_boolean("GUI", "HistogramBar");
                }

                if (keyFile.has_key ("GUI", "HistogramHeight")) {
                    histogramHeight = keyFile.get_integer ("GUI", "HistogramHeight");
                }

                if (keyFile.has_key ("GUI", "HistogramDrawMode")) {
                    histogramDrawMode = keyFile.get_integer ("GUI", "HistogramDrawMode");
                }

                if (keyFile.has_key("GUI", "HistogramScalingFactor")) {
                    histogram_scaling_factor = keyFile.get_double("GUI", "HistogramScalingFactor");
                }
                
                if (keyFile.has_key("GUI", "NavigatorRGBUnit")) {
                    navRGBUnit = (NavigatorUnit)keyFile.get_integer("GUI", "NavigatorRGBUnit");
                }

                if (keyFile.has_key("GUI", "NavigatorLCHUnit")) {
                    navLCHUnit = (NavigatorUnit)keyFile.get_integer("GUI", "NavigatorLCHUnit");
                }

                if (keyFile.has_key("GUI", "HistogramScopeType")) {
                    histogramScopeType = static_cast<ScopeType>(keyFile.get_integer("GUI", "HistogramScopeType"));
                }

                if (keyFile.has_key("GUI", "HistogramShowOptionButtons")) {
                    histogramShowOptionButtons = keyFile.get_boolean("GUI", "HistogramShowOptionButtons");
                }

                if (keyFile.has_key("GUI", "HistogramTraceBrightness")) {
                    histogramTraceBrightness = keyFile.get_double("GUI", "HistogramTraceBrightness");
                }

                if (keyFile.has_key("GUI", "ShowFilmStripToolBar")) {
                    showFilmStripToolBar = keyFile.get_boolean("GUI", "ShowFilmStripToolBar");
                }

                if (keyFile.has_key("GUI", "FileBrowserToolbarSingleRow")) {
                    FileBrowserToolbarSingleRow = keyFile.get_boolean("GUI", "FileBrowserToolbarSingleRow");
                }

                if (keyFile.has_key("GUI", "HideTPVScrollbar")) {
                    hideTPVScrollbar = keyFile.get_boolean("GUI", "HideTPVScrollbar");
                }

                if (keyFile.has_key("GUI", "HistogramWorking")) {
                    rtSettings.HistogramWorking = keyFile.get_boolean("GUI", "HistogramWorking");
                }

                if (keyFile.has_key("GUI", "CurveBBoxPosition")) {
                    curvebboxpos = keyFile.get_integer("GUI", "CurveBBoxPosition");
                }

                if (keyFile.has_key("GUI", "ToolPanelsDisable")) {
                    toolpanels_disable = keyFile.get_boolean("GUI", "ToolPanelsDisable");
                }

                if (keyFile.has_key("GUI", "AdjusterForceLinear")) {
                    adjuster_force_linear = keyFile.get_boolean("GUI", "AdjusterForceLinear");
                }
            }

            if (keyFile.has_group("Crop Settings")) {
                if (keyFile.has_key("Crop Settings", "PPI")) {
                    cropPPI = keyFile.get_integer("Crop Settings", "PPI");
                }
            }

            if (keyFile.has_group("Color Management")) {
                if (keyFile.has_key("Color Management", "ICCDirectory")) {
                    rtSettings.iccDirectory = keyFile.get_string("Color Management", "ICCDirectory");
                }
                if (keyFile.has_key("Color Management", "MonitorICCDirectory")) {
                    rtSettings.monitorIccDirectory = keyFile.get_string("Color Management", "MonitorICCDirectory");
                } else {
                    rtSettings.monitorIccDirectory = rtSettings.iccDirectory;
                }

                if (keyFile.has_key("Color Management", "PrinterIntent")) {
                    rtSettings.printerIntent = static_cast<rtengine::RenderingIntent>(keyFile.get_integer("Color Management", "PrinterIntent"));
                }

                if (keyFile.has_key("Color Management", "PrinterBPC")) {
                    rtSettings.printerBPC = keyFile.get_boolean("Color Management", "PrinterBPC");
                }

                if (keyFile.has_key("Color Management", "PrinterProfile")) {
                    rtSettings.printerProfile = keyFile.get_string("Color Management", "PrinterProfile");
                }

                if (keyFile.has_key("Color Management", "MonitorProfile")) {
                    rtSettings.monitorProfile = keyFile.get_string("Color Management", "MonitorProfile");
                }

                if (keyFile.has_key("Color Management", "AutoMonitorProfile")) {
                    rtSettings.autoMonitorProfile = keyFile.get_boolean("Color Management", "AutoMonitorProfile");
                }

                if (keyFile.has_key("Color Management", "Intent")) {
                    rtSettings.monitorIntent = static_cast<rtengine::RenderingIntent>(keyFile.get_integer("Color Management", "Intent"));
                }

                if (keyFile.has_key("Color Management", "MonitorBPC")) {
                    rtSettings.monitorBPC = keyFile.get_boolean("Color Management", "MonitorBPC");
                }

                if (keyFile.has_key("Color Management", "WhiteBalanceSpotSize")) {
                    whiteBalanceSpotSize = keyFile.get_integer("Color Management", "WhiteBalanceSpotSize");
                }

                if (keyFile.has_key("Color Management", "ClutsDirectory")) {
                    clutsDir = keyFile.get_string("Color Management", "ClutsDirectory");
                }

                if (keyFile.has_key("Color Management", "OSMonitorProfile")) {
                    auto s = keyFile.get_string("Color Management", "OSMonitorProfile").lowercase();
                    if (s == "displayp3") {
                        rtSettings.os_monitor_profile = rtengine::Settings::StdMonitorProfile::DISPLAY_P3;
                    } else if (s == "adobergb") {
                        rtSettings.os_monitor_profile = rtengine::Settings::StdMonitorProfile::ADOBE_RGB;
                    } else {
                        rtSettings.os_monitor_profile = rtengine::Settings::StdMonitorProfile::SRGB;
                    }
                }
            }

            if (keyFile.has_group("Sounds")) {
                if (keyFile.has_key("Sounds", "Enable")) {
                    sndEnable = keyFile.get_boolean("Sounds", "Enable");
                }

                if (keyFile.has_key("Sounds", "BatchQueueDone")) {
                    sndBatchQueueDone = keyFile.get_string("Sounds", "BatchQueueDone");
                }

                if (keyFile.has_key("Sounds", "LngEditProcDone")) {
                    sndLngEditProcDone = keyFile.get_string("Sounds", "LngEditProcDone");
                }

                if (keyFile.has_key("Sounds", "LngEditProcDoneSecs")) {
                    sndLngEditProcDoneSecs = keyFile.get_double("Sounds", "LngEditProcDoneSecs");
                }
            }

            if (keyFile.has_group("Fast Export")) {
                if (keyFile.has_key("Fast Export", "fastexport_resize_width")) {
                    fastexport_resize_width = keyFile.get_integer("Fast Export", "fastexport_resize_width");
                } else if (keyFile.has_key("Fast Export", "MaxWidth")) {
                    fastexport_resize_width = keyFile.get_integer("Fast Export", "MaxWidth");
                }

                if (keyFile.has_key("Fast Export", "fastexport_resize_height")) {
                    fastexport_resize_height = keyFile.get_integer("Fast Export", "fastexport_resize_height");
                } else if (keyFile.has_key("Fast Export", "MaxHeight")) {
                    fastexport_resize_height = keyFile.get_integer("Fast Export", "MaxHeight");
                } 
            }

            if (keyFile.has_group("Dialogs")) {
                safeDirGet(keyFile, "Dialogs", "LastIccDir", lastIccDir);
                safeDirGet(keyFile, "Dialogs", "LastDarkframeDir", lastDarkframeDir);
                safeDirGet(keyFile, "Dialogs", "LastFlatfieldDir", lastFlatfieldDir);
                safeDirGet(keyFile, "Dialogs", "LastRgbCurvesDir", lastRgbCurvesDir);
                safeDirGet(keyFile, "Dialogs", "LastLabCurvesDir", lastLabCurvesDir);
                safeDirGet(keyFile, "Dialogs", "LastPFCurvesDir", lastPFCurvesDir);
                safeDirGet(keyFile, "Dialogs", "LastHsvCurvesDir", lastHsvCurvesDir);

                safeDirGet(keyFile, "Dialogs", "LastToneCurvesDir", lastToneCurvesDir);
                safeDirGet(keyFile, "Dialogs", "LastProfilingReferenceDir", lastProfilingReferenceDir);
                safeDirGet(keyFile, "Dialogs", "LastLensProfileDir", lastLensProfileDir);
                safeDirGet(keyFile, "Dialogs", "LastICCProfCreatorDir", lastICCProfCreatorDir);
                safeDirGet(keyFile, "Dialogs", "LastCopyMovePath", lastCopyMovePath);
                safeDirGet(keyFile, "Dialogs", "LastSessionAddDir", last_session_add_dir);
                safeDirGet(keyFile, "Dialogs", "LastSessionLoadSaveDir", last_session_loadsave_dir);

                if (keyFile.has_key("Dialogs", "GimpPluginShowInfoDialog")) {
                    gimpPluginShowInfoDialog = keyFile.get_boolean("Dialogs", "GimpPluginShowInfoDialog");
                }
            }

            if (keyFile.has_group("Lensfun")) {
                if (keyFile.has_key("Lensfun", "DBDirectory")) {
                    rtSettings.lensfunDbDirectory = keyFile.get_string("Lensfun", "DBDirectory");
                }
            }

            if (keyFile.has_group("Metadata")) {
                if (keyFile.has_key("Metadata", "XMPSidecarStyle")) {
                    std::string val = keyFile.get_string("Metadata", "XMPSidecarStyle");
                    if (val == "ext") {
                        rtSettings.xmp_sidecar_style = rtengine::Settings::XmpSidecarStyle::EXT;
                    } else {
                        rtSettings.xmp_sidecar_style = rtengine::Settings::XmpSidecarStyle::STD;
                    }
                }
                if (keyFile.has_key("Metadata", "XMPSynchronization")) {
                    std::string val = keyFile.get_string("Metadata", "XMPSynchronization");
                    if (val == "read") {
                        rtSettings.metadata_xmp_sync = rtengine::Settings::MetadataXmpSync::READ;
                    } else if (val == "readwrite") {
                        rtSettings.metadata_xmp_sync = rtengine::Settings::MetadataXmpSync::READ_WRITE;
                    } else {
                        rtSettings.metadata_xmp_sync = rtengine::Settings::MetadataXmpSync::NONE;
                    }
                }
                if (keyFile.has_key("Metadata", "ExiftoolPath")) {
                    rtSettings.exiftool_path = keyFile.get_string("Metadata", "ExiftoolPath");
                }
                if (keyFile.has_key("Metadata", "ShowExiftoolMakernotes")) {
                    show_exiftool_makernotes = keyFile.get_boolean("Metadata", "ShowExiftoolMakernotes");
                }
            }

            if (keyFile.has_group("False Colors Map")) {
                const Glib::ustring g = "False Colors Map";
                falseColorsMap.clear();
                for (auto key : keyFile.get_keys(g)) {
                    if (key.find("IRE_") == 0) {
                        size_t end = 0;
                        std::string s = key.substr(4);
                        int ire = std::stoi(s, &end);
                        if (end >= s.size()) {
                            falseColorsMap[ire] = keyFile.get_string(g, key);
                        }
                    }
                }
                if (falseColorsMap.empty() ||
                    falseColorsMap.rbegin()->first < 108) {
                    falseColorsMap[108] = "#000000";
                }
                if (keyFile.has_key(g, "ClippedHighlights")) {
                    clipped_highlights_color = keyFile.get_string(g, "ClippedHighlights");
                }
                if (keyFile.has_key(g, "ClippedShadows")) {
                    clipped_shadows_color = keyFile.get_string(g, "ClippedShadows");
                }
            }

            if (keyFile.has_group("Renaming")) {
                const char *g = "Renaming";
                if (keyFile.has_key(g, "Pattern")) {
                    renaming.pattern = keyFile.get_string(g, "Pattern");
                }
                if (keyFile.has_key(g, "Sidecars")) {
                    renaming.sidecars = keyFile.get_string(g, "Sidecars");
                }
                if (keyFile.has_key(g, "NameNormalization")) {
                    renaming.name_norm = keyFile.get_integer(g, "NameNormalization");
                }
                if (keyFile.has_key(g, "ExtNormalization")) {
                    renaming.ext_norm = keyFile.get_integer(g, "ExtNormalization");
                }
                if (keyFile.has_key(g, "AllowWhitespace")) {
                    renaming.allow_whitespace = keyFile.get_boolean(g, "AllowWhitespace");
                }
                if (keyFile.has_key(g, "OnExisting")) {
                    renaming.on_existing = keyFile.get_integer(g, "OnExisting");
                }
                if (keyFile.has_key(g, "ProgressiveNumber")) {
                    renaming.progressive_number = keyFile.get_integer(g, "ProgressiveNumber");
                }
            }

            if (keyFile.has_group("ExifFilterSettings")) {
                const char *g = "ExifFilterSettings";
                if (keyFile.has_key(g, "Remember")) {
                    remember_exif_filter_settings = keyFile.get_boolean(g, "Remember");
                }
                last_exif_filter_settings.load(keyFile, g);
            }

            if (keyFile.has_group("Theme Colors")) {
                auto group = "Theme Colors";
                theme_bg_color = keyFile.get_integer_list(group, "Background");
                theme_bg_color.resize(3);
                theme_fg_color = keyFile.get_integer_list(group, "Foreground");
                theme_fg_color.resize(3);
                theme_hl_color = keyFile.get_integer_list(group, "Highlight");
                theme_hl_color.resize(3);
            }

            ExportProfileInfo default_export_profile_info;
            if (keyFile.has_key("Output", "BatchQueueUseProfile")) {
                default_export_profile_info.enabled = keyFile.get_boolean("Output", "BatchQueueUseProfile");
            }
                    
            if (keyFile.has_key("Output", "BatchQueueProfile")) {
                default_export_profile_info.profile = keyFile.get_string("Output", "BatchQueueProfile");
            }
            
            for (auto group : keyFile.get_groups()) {
                if (group.find("Export Profile ") != 0) {
                    continue;
                }
                auto key = group.substr(15);
                auto &info = export_profile_map[key];
                info = default_export_profile_info;
                if (keyFile.has_key(group, "Profile")) {
                    info.profile = keyFile.get_string(group, "Profile");
                }
                if (keyFile.has_key(group, "Enabled")) {
                    info.enabled = keyFile.get_boolean(group, "Enabled");
                }
            }

// --------------------------------------------------------------------------------------------------------

            filterOutParsedExtensions();

            return;

        }
    } catch (Glib::Error &err) {
        Glib::ustring msg = Glib::ustring::compose("Options::readFromFile / Error code %1 while reading values from \"%2\":\n%3", err.code(), fname, err.what());

        if (options.rtSettings.verbose) {
            printf("%s\n", msg.c_str());
        }

        throw Error(msg);
    } catch (...) {
        Glib::ustring msg = Glib::ustring::compose("Options::readFromFile / Unknown exception while trying to load \"%1\"!", fname);

        if (options.rtSettings.verbose) {
            printf("%s\n", msg.c_str());
        }

        throw Error(msg);
    }
}

bool Options::safeDirGet(const Glib::KeyFile& keyFile, const Glib::ustring& section,
                         const Glib::ustring& entryName, Glib::ustring& destination)
{
    try {

        if (keyFile.has_key(section, entryName) && !keyFile.get_string(section, entryName).empty()) {
            destination = keyFile.get_string(section, entryName);
            return true;
        }

    } catch (Glib::KeyFileError&) {}

    return false;
}

void Options::saveToFile(Glib::ustring fname)
{

    Glib::ustring keyData;

    try {

        Glib::KeyFile keyFile;

        keyFile.set_boolean("General", "TabbedEditor", tabbedUI);

        if (startupDir == STARTUPDIR_HOME) {
            keyFile.set_string("General", "StartupDirectory", "home");
        } else if (startupDir == STARTUPDIR_CURRENT) {
            keyFile.set_string("General", "StartupDirectory", "current");
        } else if (startupDir == STARTUPDIR_CUSTOM) {
            keyFile.set_string("General", "StartupDirectory", "custom");
        } else if (startupDir == STARTUPDIR_LAST) {
            keyFile.set_string("General", "StartupDirectory", "last");
        }

        keyFile.set_string("General", "StartupPath", startupPath);
        keyFile.set_string("General", "DateFormat", dateFormat);
        keyFile.set_integer("General", "AdjusterMinDelay", adjusterMinDelay);
        keyFile.set_integer("General", "AdjusterMaxDelay", adjusterMaxDelay);
        keyFile.set_boolean("General", "MultiUser", multiUser);
        keyFile.set_string("General", "Language", language);
        keyFile.set_boolean("General", "LanguageAutoDetect", languageAutoDetect);
        keyFile.set_string("General", "Theme", theme);
        keyFile.set_string("General", "Version", RTVERSION);
        keyFile.set_string("General", "DarkFramesPath", rtSettings.darkFramesPath);
        keyFile.set_string("General", "FlatFieldsPath", rtSettings.flatFieldsPath);
        keyFile.set_integer("General", "Verbose", rtSettings.verbose);
        keyFile.set_integer("General", "ErrorMessageDuration", error_message_duration);
        keyFile.set_integer("General", "MaxErrorMessages", max_error_messages);
        keyFile.set_integer("General", "EditorKeyboardScrollStep", editor_keyboard_scroll_step);
        keyFile.set_integer("General", "AdjusterShortcutScrollWheelFactor", adjuster_shortcut_scrollwheel_factor);
        keyFile.set_integer("External Editor", "EditorKind", editorToSendTo);
        keyFile.set_string("External Editor", "GimpDir", gimpDir);
        keyFile.set_string("External Editor", "PhotoshopDir", psDir);
        keyFile.set_string("External Editor", "CustomEditor", customEditorProg);
        keyFile.set_integer("External Editor", "OutputDir", int(editor_out_dir));
        keyFile.set_string("External Editor", "CustomOutputDir", editor_custom_out_dir);
        keyFile.set_boolean("External Editor", "Float32", editor_float32);
        keyFile.set_boolean("External Editor", "BypassOutputProfile", editor_bypass_output_profile);

        keyFile.set_boolean("File Browser", "BrowserShowsDate", fbShowDateTime);
        keyFile.set_boolean("File Browser", "BrowserShowsExif", fbShowBasicExif);
        keyFile.set_boolean("File Browser", "BrowserShowsExpComp", fbShowExpComp);
#ifndef WIN32
        keyFile.set_boolean("File Browser", "BrowserShowsHidden", fbShowHidden);
#endif
        keyFile.set_integer("File Browser", "ThumbnailSize", thumbSize);
        keyFile.set_integer("File Browser", "ThumbnailSizeTab", thumbSizeTab);
        keyFile.set_integer("File Browser", "ThumbnailSizeQueue", thumbSizeQueue);
        keyFile.set_integer("File Browser", "SameThumbSize", sameThumbSize);
        keyFile.set_integer("File Browser", "ThumbnailOrder", int(thumbnailOrder));
        keyFile.set_integer("File Browser", "MaxPreviewHeight", maxThumbnailHeight);
        keyFile.set_integer("File Browser", "MaxPreviewWidth", maxThumbnailWidth);
        keyFile.set_integer("File Browser", "MaxCacheEntries", maxCacheEntries);
        Glib::ArrayHandle<Glib::ustring> pext = parseExtensions;
        keyFile.set_string_list("File Browser", "ParseExtensions", pext);
        Glib::ArrayHandle<int> pextena = parseExtensionsEnabled;
        keyFile.set_integer_list("File Browser", "ParseExtensionsEnabled", pextena);
        keyFile.set_integer("File Browser", "ThumbnailInterpolation", thumbInterp);
        Glib::ArrayHandle<Glib::ustring> pfav = favoriteDirs;
        keyFile.set_string_list("File Browser", "FavoriteDirs", pfav);
        Glib::ArrayHandle<double> ptzoom = thumbnailZoomRatios;
        keyFile.set_double_list("File Browser", "ThumbnailZoomRatios", ptzoom);
        keyFile.set_boolean("File Browser", "OverlayedFileNames", overlayedFileNames);
        keyFile.set_boolean("File Browser", "FilmStripOverlayedFileNames", filmStripOverlayedFileNames);
        keyFile.set_boolean("File Browser", "ShowFileNames", showFileNames);
        keyFile.set_boolean("File Browser", "FilmStripShowFileNames", filmStripShowFileNames);
        keyFile.set_boolean("File Browser", "InternalThumbIfUntouched", internalThumbIfUntouched);
        // keyFile.set_boolean("File Browser", "menuGroupRank", menuGroupRank);
        // keyFile.set_boolean("File Browser", "menuGroupLabel", menuGroupLabel);
        // keyFile.set_boolean("File Browser", "menuGroupFileOperations", menuGroupFileOperations);
        // keyFile.set_boolean("File Browser", "menuGroupProfileOperations", menuGroupProfileOperations);
        // keyFile.set_boolean("File Browser", "menuGroupExtProg", menuGroupExtProg);
        keyFile.set_integer("File Browser", "MaxRecentFolders", maxRecentFolders);
        {
            std::vector<Glib::ustring> temp;
            temp.reserve(maxRecentFolders);

            for (unsigned int i = 0; i < std::min(recentFolders.size(), maxRecentFolders); i++) {
                temp.push_back(recentFolders[i]);
            }

            keyFile.set_string_list("File Browser", "RecentFolders", temp);
        }
        switch (thumbnail_rating_mode) {
        case ThumbnailRatingMode::XMP:
            keyFile.set_string("File Browser", "ThumbnailRatingMode", "xmp");
            break;
        default: // ThumbnailRatingMode::PROCPARAMS
            keyFile.set_string("File Browser", "ThumbnailRatingMode", "procparams");
            break;
        }
        keyFile.set_integer("Clipping Indication", "HighlightThreshold", highlightThreshold);
        keyFile.set_integer("Clipping Indication", "ShadowThreshold", shadowThreshold);

        keyFile.set_integer("Performance", "RgbDenoiseThreadLimit", rgbDenoiseThreadLimit);
        keyFile.set_integer("Performance", "ClutCacheSize", clutCacheSize);
        keyFile.set_integer("Performance", "MaxInspectorBuffers", maxInspectorBuffers);
        keyFile.set_integer("Performance", "InspectorDelay", inspectorDelay);
        keyFile.set_integer("Performance", "PreviewDemosaicFromSidecar", prevdemo);
        keyFile.set_boolean("Performance", "SerializeTiffRead", serializeTiffRead);
        keyFile.set_boolean("Performance", "DenoiseZoomedOut", denoiseZoomedOut);
        keyFile.set_integer("Performance", "ThumbUpdateThreadLimit", rtSettings.thread_pool_size);
        keyFile.set_boolean("Performance", "ThumbDelayUpdate", thumb_delay_update);
        keyFile.set_boolean("Performance", "ThumbLazyCaching", thumb_lazy_caching);
        keyFile.set_boolean("Performance", "ThumbCacheProcessed", thumb_cache_processed);
        keyFile.set_boolean("Performance", "CTLScriptsFastPreview", rtSettings.ctl_scripts_fast_preview);
        keyFile.set_integer("Performance", "WBPreviewMode", wb_preview_mode);
        keyFile.set_integer("Performance", "RAWImageIOCacheSize", rtSettings.imgio_raw_cache_size);
        
        keyFile.set_integer("Inspector", "Mode", int(rtSettings.thumbnail_inspector_mode));
        keyFile.set_integer("Inspector", "RawCurve", int(rtSettings.thumbnail_inspector_raw_curve));
        keyFile.set_boolean("Inspector", "ZoomFit", thumbnail_inspector_zoom_fit);
        keyFile.set_boolean("Inspector", "ShowInfo", thumbnail_inspector_show_info);
        keyFile.set_boolean("Inspector", "ShowHistogram", thumbnail_inspector_show_histogram);
        keyFile.set_boolean("Inspector", "EnableCMS", thumbnail_inspector_enable_cms);
        keyFile.set_integer("Inspector", "BrowserWidth", browser_width_for_inspector);
        keyFile.set_boolean("Inspector", "ThumbnailHover", thumbnail_inspector_hover);

        keyFile.set_string("Output", "Format", saveFormat.format);
        keyFile.set_integer("Output", "JpegQuality", saveFormat.jpegQuality);
        keyFile.set_integer("Output", "JpegSubSamp", saveFormat.jpegSubSamp);
        keyFile.set_integer("Output", "PngBps", saveFormat.pngBits);
        keyFile.set_integer("Output", "TiffBps", saveFormat.tiffBits);
        keyFile.set_boolean("Output", "TiffFloat", saveFormat.tiffFloat);
        keyFile.set_boolean("Output", "TiffUncompressed", saveFormat.tiffUncompressed);
        keyFile.set_boolean("Output", "SaveProcParams", saveFormat.saveParams);

        keyFile.set_string("Output", "FormatBatch", saveFormatBatch.format);
        keyFile.set_integer("Output", "JpegQualityBatch", saveFormatBatch.jpegQuality);
        keyFile.set_integer("Output", "JpegSubSampBatch", saveFormatBatch.jpegSubSamp);
        keyFile.set_integer("Output", "PngBpsBatch", saveFormatBatch.pngBits);
        keyFile.set_integer("Output", "TiffBpsBatch", saveFormatBatch.tiffBits);
        keyFile.set_boolean("Output", "TiffFloatBatch", saveFormatBatch.tiffFloat);
        keyFile.set_boolean("Output", "TiffUncompressedBatch", saveFormatBatch.tiffUncompressed);
        keyFile.set_boolean("Output", "SaveProcParamsBatch", saveFormatBatch.saveParams);

        keyFile.set_string("Output", "PathTemplate", savePathTemplate);
        keyFile.set_string("Output", "PathFolder", savePathFolder);
        keyFile.set_boolean("Output", "AutoSuffix", autoSuffix);
        keyFile.set_boolean("Output", "ForceFormatOpts", forceFormatOpts);
        keyFile.set_integer("Output", "SaveMethodNum", saveMethodNum);
        keyFile.set_boolean("Output", "UsePathTemplate", saveUsePathTemplate);
        keyFile.set_string("Output", "LastSaveAsPath", lastSaveAsPath);
        keyFile.set_boolean("Output", "OverwriteOutputFile", overwriteOutputFile);
        // keyFile.set_boolean("Output", "BatchQueueUseProfile", batch_queue_use_profile);
        // keyFile.set_string("Output", "BatchQueueProfile", batch_queue_profile_path);
        keyFile.set_integer("Output", "ProcParamsAutosaveInterval", sidecar_autosave_interval);

        keyFile.set_string("Profiles", "Directory", profilePath);
        keyFile.set_boolean("Profiles", "UseBundledProfiles", useBundledProfiles);
        keyFile.set_string("Profiles", "LoadSaveProfilePath", loadSaveProfilePath);
        keyFile.set_string("Profiles", "RawDefault", defProfRaw);
        keyFile.set_string("Profiles", "ImgDefault", defProfImg);
        keyFile.set_boolean("Profiles", "AppendMode", profile_append_mode);
        keyFile.set_boolean("Profiles", "SaveParamsWithFile", saveParamsFile);
        keyFile.set_boolean("Profiles", "SaveParamsToCache", saveParamsCache);
        keyFile.set_integer("Profiles", "LoadParamsFromLocation", paramsLoadLocation);
        keyFile.set_boolean("Profiles", "EmbedParamsInMetadata", params_out_embed);
        keyFile.set_boolean("Profiles", "ParamsSidecarStripExtension", params_sidecar_strip_extension);
        keyFile.set_string("Profiles", "CustomProfileBuilderPath", CPBPath);
        keyFile.set_integer("Profiles", "CustomProfileBuilderKeys", CPBKeys);

        // Glib::ArrayHandle<Glib::ustring> ahfavorites = favorites;
        // keyFile.set_string_list("GUI", "Favorites", ahfavorites);
        keyFile.set_integer("GUI", "WindowWidth", windowWidth);
        keyFile.set_integer("GUI", "WindowHeight", windowHeight);
        keyFile.set_integer("GUI", "WindowX", windowX);
        keyFile.set_integer("GUI", "WindowY", windowY);
        keyFile.set_integer("GUI", "WindowMonitor", windowMonitor);
        keyFile.set_integer("GUI", "MeowMonitor", meowMonitor);
        keyFile.set_boolean("GUI", "MeowFullScreen", meowFullScreen);
        keyFile.set_boolean("GUI", "MeowMaximized", meowMaximized);
        keyFile.set_integer("GUI", "MeowWidth", meowWidth);
        keyFile.set_integer("GUI", "MeowHeight", meowHeight);
        keyFile.set_integer("GUI", "MeowX", meowX);
        keyFile.set_integer("GUI", "MeowY", meowY);
        keyFile.set_boolean("GUI", "WindowMaximized", windowMaximized);
        keyFile.set_integer("GUI", "DetailWindowWidth", detailWindowWidth);
        keyFile.set_integer("GUI", "DetailWindowHeight", detailWindowHeight);
        keyFile.set_integer("GUI", "DirBrowserWidth", dirBrowserWidth);
        keyFile.set_integer("GUI", "DirBrowserHeight", dirBrowserHeight);
        keyFile.set_integer("GUI", "SortType", dirBrowserSortType);
        keyFile.set_integer("GUI", "DirBrowserSingleClick", dir_browser_single_click);
        keyFile.set_integer("GUI", "PreferencesWidth", preferencesWidth);
        keyFile.set_integer("GUI", "PreferencesHeight", preferencesHeight);
        keyFile.set_integer("GUI", "SaveAsDialogWidth", saveAsDialogWidth);
        keyFile.set_integer("GUI", "SaveAsDialogHeight", saveAsDialogHeight);
        keyFile.set_integer("GUI", "ToolPanelWidth", toolPanelWidth);
        keyFile.set_integer("GUI", "BrowserToolPanelWidth", browserToolPanelWidth);
        keyFile.set_integer("GUI", "BrowserToolPanelHeight", browserToolPanelHeight);
        keyFile.set_boolean("GUI", "BrowserToolPanelOpened", browserToolPanelOpened);
        keyFile.set_boolean("GUI", "EditorFilmStripOpened", editorFilmStripOpened);
        keyFile.set_boolean("GUI", "BrowserDirPanelOpened", browserDirPanelOpened);
        keyFile.set_boolean("GUI", "InspectorDirPanelOpened", inspectorDirPanelOpened);
        keyFile.set_integer("GUI", "HistoryPanelWidth", historyPanelWidth);
        keyFile.set_string("GUI", "FontFamily", fontFamily);
        keyFile.set_integer("GUI", "FontSize", fontSize);
        keyFile.set_string("GUI", "CPFontFamily", CPFontFamily);
        keyFile.set_integer("GUI", "CPFontSize", CPFontSize);
        keyFile.set_boolean("GUI", "PseudoHiDPISupport", pseudoHiDPISupport);
        keyFile.set_integer("GUI", "LastPreviewScale", lastScale);
        keyFile.set_integer("GUI", "PanAccelFactor", panAccelFactor);
        keyFile.set_boolean("GUI", "RememberZoomAndPan", rememberZoomAndPan);
        keyFile.set_boolean("GUI", "ShowHistory", showHistory);
        keyFile.set_boolean("GUI", "ShowInfo", showInfo);
        keyFile.set_boolean("GUI", "FilmStripBottom", filmstripBottom);
        keyFile.set_boolean("GUI", "ShowClippedHighlights", showClippedHighlights);
        keyFile.set_boolean("GUI", "ShowClippedShadows", showClippedShadows);
        keyFile.set_integer("GUI", "FrameColor", bgcolor);
        keyFile.set_boolean("GUI", "ProcessingQueueEnbled", procQueueEnabled);
        Glib::ArrayHandle<int> tpopen = tpOpen;
        keyFile.set_integer_list ("GUI", "ToolPanelsExpanded", tpopen);
        keyFile.set_boolean ("GUI", "ToolPanelsExpandedAutoSave", autoSaveTpOpen);
        keyFile.set_integer ("GUI", "MultiDisplayMode", multiDisplayMode);
        // keyFile.set_double_list ("GUI", "CutOverlayBrush", cutOverlayBrush);
        // keyFile.set_double_list ("GUI", "NavGuideBrush", navGuideBrush);
        keyFile.set_integer ("GUI", "HistogramPosition", histogramPosition);
        keyFile.set_boolean ("GUI", "HistogramRed", histogramRed);
        keyFile.set_boolean ("GUI", "HistogramGreen", histogramGreen);
        keyFile.set_boolean ("GUI", "HistogramBlue", histogramBlue);
        keyFile.set_boolean ("GUI", "HistogramLuma", histogramLuma);
        keyFile.set_boolean ("GUI", "HistogramChroma", histogramChroma);
        //keyFile.set_boolean ("GUI", "HistogramRAW", histogramRAW);
        keyFile.set_boolean ("GUI", "HistogramBar", histogramBar);
        keyFile.set_integer ("GUI", "HistogramHeight", histogramHeight);
        keyFile.set_integer ("GUI", "HistogramDrawMode", histogramDrawMode);
        keyFile.set_double("GUI", "HistogramScalingFactor", histogram_scaling_factor);
        keyFile.set_integer("GUI", "HistogramScopeType", rtengine::toUnderlying(histogramScopeType));
        keyFile.set_boolean("GUI", "HistogramShowOptionButtons", histogramShowOptionButtons);
        keyFile.set_double("GUI", "HistogramTraceBrightness", histogramTraceBrightness);
        keyFile.set_integer ("GUI", "NavigatorRGBUnit", (int)navRGBUnit);
        keyFile.set_integer ("GUI", "NavigatorLCHUnit", (int)navLCHUnit);
        keyFile.set_boolean ("GUI", "ShowFilmStripToolBar", showFilmStripToolBar);
        keyFile.set_boolean ("GUI", "FileBrowserToolbarSingleRow", FileBrowserToolbarSingleRow);
        keyFile.set_boolean ("GUI", "HideTPVScrollbar", hideTPVScrollbar);
        keyFile.set_boolean ("GUI", "HistogramWorking", rtSettings.HistogramWorking);
        keyFile.set_integer ("GUI", "CurveBBoxPosition", curvebboxpos);
        keyFile.set_boolean("GUI", "ToolPanelsDisable", toolpanels_disable);
        keyFile.set_boolean("GUI", "AdjusterForceLinear", adjuster_force_linear);

        //Glib::ArrayHandle<int> crvopen = crvOpen;
        //keyFile.set_integer_list ("GUI", "CurvePanelsExpanded", crvopen);

        keyFile.set_integer("Crop Settings", "PPI", cropPPI);

        keyFile.set_string("Color Management", "PrinterProfile", rtSettings.printerProfile);
        keyFile.set_integer("Color Management", "PrinterIntent", rtSettings.printerIntent);
        keyFile.set_boolean("Color Management", "PrinterBPC", rtSettings.printerBPC);

        keyFile.set_string("Color Management", "ICCDirectory", rtSettings.iccDirectory);
        keyFile.set_string("Color Management", "MonitorICCDirectory", rtSettings.monitorIccDirectory);
        keyFile.set_string("Color Management", "MonitorProfile", rtSettings.monitorProfile);
        keyFile.set_boolean("Color Management", "AutoMonitorProfile", rtSettings.autoMonitorProfile);
        keyFile.set_integer("Color Management", "Intent", rtSettings.monitorIntent);
        keyFile.set_boolean("Color Management", "MonitorBPC", rtSettings.monitorBPC);

        keyFile.set_integer("Color Management", "WhiteBalanceSpotSize", whiteBalanceSpotSize);
        keyFile.set_string("Color Management", "ClutsDirectory", clutsDir);

        Glib::ustring os_mon_prof = "";
        switch (rtSettings.os_monitor_profile) {
        case rtengine::Settings::StdMonitorProfile::DISPLAY_P3:
            os_mon_prof = "DisplayP3";
            break;
        case rtengine::Settings::StdMonitorProfile::ADOBE_RGB:
            os_mon_prof = "AdobeRGB";
            break;
        default:
            os_mon_prof = "sRGB";
            break;
        }
        keyFile.set_string("Color Management", "OSMonitorProfile", os_mon_prof);

        keyFile.set_boolean("Sounds", "Enable", sndEnable);
        keyFile.set_string("Sounds", "BatchQueueDone", sndBatchQueueDone);
        keyFile.set_string("Sounds", "LngEditProcDone", sndLngEditProcDone);
        keyFile.set_double("Sounds", "LngEditProcDoneSecs", sndLngEditProcDoneSecs);

        keyFile.set_integer("Fast Export", "MaxWidth", fastexport_resize_width);
        keyFile.set_integer("Fast Export", "MaxHeight", fastexport_resize_height);

        keyFile.set_string("Dialogs", "LastIccDir", lastIccDir);
        keyFile.set_string("Dialogs", "LastDarkframeDir", lastDarkframeDir);
        keyFile.set_string("Dialogs", "LastFlatfieldDir", lastFlatfieldDir);
        keyFile.set_string("Dialogs", "LastRgbCurvesDir", lastRgbCurvesDir);
        keyFile.set_string("Dialogs", "LastLabCurvesDir", lastLabCurvesDir);
        keyFile.set_string("Dialogs", "LastPFCurvesDir", lastPFCurvesDir);
        keyFile.set_string("Dialogs", "LastHsvCurvesDir", lastHsvCurvesDir);
        keyFile.set_string("Dialogs", "LastToneCurvesDir", lastToneCurvesDir);
        keyFile.set_string("Dialogs", "LastProfilingReferenceDir", lastProfilingReferenceDir);
        keyFile.set_string("Dialogs", "LastLensProfileDir", lastLensProfileDir);
        keyFile.set_string("Dialogs", "LastICCProfCreatorDir", lastICCProfCreatorDir);
        keyFile.set_string("Dialogs", "LastCopyMovePath", lastCopyMovePath);
        keyFile.set_string("Dialogs", "LastSessionAddDir", last_session_add_dir);
        keyFile.set_string("Dialogs", "LastSessionLoadSaveDir", last_session_loadsave_dir);
        keyFile.set_boolean("Dialogs", "GimpPluginShowInfoDialog", gimpPluginShowInfoDialog);

        keyFile.set_string("Lensfun", "DBDirectory", rtSettings.lensfunDbDirectory);

        switch (rtSettings.xmp_sidecar_style) {
        case rtengine::Settings::XmpSidecarStyle::EXT:
            keyFile.set_string("Metadata", "XMPSidecarStyle", "ext");
            break;
        default:
            keyFile.set_string("Metadata", "XMPSidecarStyle", "std");
        }

        switch (rtSettings.metadata_xmp_sync) {
        case rtengine::Settings::MetadataXmpSync::READ:
            keyFile.set_string("Metadata", "XMPSynchronization", "read");
            break;
        case rtengine::Settings::MetadataXmpSync::READ_WRITE:
            keyFile.set_string("Metadata", "XMPSynchronization", "readwrite");
            break;
        default:
            keyFile.set_string("Metadata", "XMPSynchronization", "none");
        }

        keyFile.set_string("Metadata", "ExiftoolPath", rtSettings.exiftool_path);
        keyFile.set_boolean("Metadata", "ShowExiftoolMakernotes", show_exiftool_makernotes);

        for (auto &p : falseColorsMap) {
            keyFile.set_string("False Colors Map", "IRE_" + std::to_string(p.first), p.second);
        }
        keyFile.set_string("False Colors Map", "ClippedHighlights", clipped_highlights_color);
        keyFile.set_string("False Colors Map", "ClippedShadows", clipped_shadows_color);

        keyFile.set_string("Renaming", "Pattern", renaming.pattern);
        keyFile.set_string("Renaming", "Sidecars", renaming.sidecars);
        keyFile.set_integer("Renaming", "NameNormalization", renaming.name_norm);
        keyFile.set_integer("Renaming", "ExtNormalization", renaming.ext_norm);
        keyFile.set_boolean("Renaming", "AllowWhitespace", renaming.allow_whitespace);
        keyFile.set_integer("Renaming", "OnExisting", renaming.on_existing);
        keyFile.set_integer("Renaming", "ProgressiveNumber", renaming.progressive_number);

        keyFile.set_boolean("ExifFilterSettings", "Remember", remember_exif_filter_settings);
        last_exif_filter_settings.save(keyFile, "ExifFilterSettings");

        keyFile.set_integer_list("Theme Colors", "Background", theme_bg_color);
        keyFile.set_integer_list("Theme Colors", "Foreground", theme_fg_color);
        keyFile.set_integer_list("Theme Colors", "Highlight", theme_hl_color);

        for (auto &p : export_profile_map) {
            keyFile.set_string("Export Profile " + p.first, "Profile", p.second.profile);
            keyFile.set_boolean("Export Profile " + p.first, "Enabled", p.second.enabled);
        }

        keyData = keyFile.to_data();

    } catch (Glib::KeyFileError &e) {
        throw Error(e.what());
    }

    FILE *f = g_fopen(fname.c_str(), "wt");

    if (f == nullptr) {
        std::cout << "Warning! Unable to save your preferences to: " << fname << std::endl;
        Glib::ustring msg_ = Glib::ustring::compose(M("MAIN_MSG_WRITEFAILED"), fname.c_str());
        throw Error(msg_);
    } else {
        fprintf(f, "%s", keyData.c_str());
        fclose(f);
    }

    if (options.rtSettings.verbose) {
        std::cout << "options saved to " << fname << std::endl;
    }
}

void Options::load(bool lightweight, int verbose)
{
#ifdef __APPLE__
#  ifdef ART_MACOS_GDK_QUARTZ_COLORSPACE
    rtengine::Settings::color_mgmt_mode = rtengine::Settings::ColorManagementMode::OS_STD_MONITOR_PROFILE;
#  else
    rtengine::Settings::color_mgmt_mode = rtengine::Settings::ColorManagementMode::OS_SRGB;
#  endif
//#elif defined WIN32
#else
    rtengine::Settings::color_mgmt_mode = rtengine::Settings::ColorManagementMode::APPLICATION;
// #else
//     const gchar *desktop_env = g_getenv("XDG_SESSION_TYPE");
//     if (desktop_env && strcmp(desktop_env, "wayland") == 0) {
//         rtengine::Settings::color_mgmt_mode = rtengine::Settings::ColorManagementMode::OS_SRGB;
//     } else {
//         rtengine::Settings::color_mgmt_mode = rtengine::Settings::ColorManagementMode::APPLICATION;
//     }        
#endif

    if (verbose >= 0) {
        options.rtSettings.verbose = verbose;
    }
    
    // Find the application data path

    const gchar* path;
    Glib::ustring dPath;

    path = g_getenv("ART_SETTINGS");

    if (path != nullptr) {
        user_config_dir = Glib::ustring(path);

        if (!Glib::path_is_absolute(user_config_dir)) {
            Glib::ustring msg = Glib::ustring::compose("Settings path %1 is not absolute", user_config_dir);
            throw Error(msg);
        }
    } else {
#ifdef WIN32
        WCHAR pathW[MAX_PATH] = {0};

        if (SHGetSpecialFolderPathW(NULL, pathW, CSIDL_LOCAL_APPDATA, false)) {
            char pathA[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, pathW, -1, pathA, MAX_PATH, 0, 0);
            user_config_dir = Glib::build_filename(Glib::ustring(pathA), Glib::ustring(CACHEFOLDERNAME));
        }

#else
        user_config_dir = Glib::build_filename(Glib::ustring(g_get_user_config_dir()), Glib::ustring(CACHEFOLDERNAME));
#endif
    }

    if (options.rtSettings.verbose) {
        printf("Settings directory (user_config_dir) = %s\n", user_config_dir.c_str());
    }

    // Set the cache folder in RT's base folder
    cacheBaseDir = Glib::build_filename(ART_base_dir, "mycache");

    // Read the global option file (the one located in the application's base folder)
    Glib::ustring lensfun_db_dir = options.rtSettings.lensfunDbDirectory;
    bool lensfun_db_dir_update = false;
    try {
        options.readFromFile(Glib::build_filename(ART_base_dir, "options"));
        if (options.rtSettings.lensfunDbDirectory != lensfun_db_dir) {
            lensfun_db_dir = options.rtSettings.lensfunDbDirectory;
            lensfun_db_dir_update = true;
        }
    } catch (Options::Error &) {
        // ignore errors here
    }

    if (!options.multiUser && path == nullptr) {
        user_config_dir = Glib::build_filename(ART_base_dir, "mysettings");
    }

    // Modify the path of the cache folder to the one provided in ART_CACHE environment variable
    path = g_getenv("ART_CACHE");

    if (path != nullptr) {
        cacheBaseDir = Glib::ustring(path);

        if (!Glib::path_is_absolute(cacheBaseDir)) {
            Glib::ustring msg = Glib::ustring::compose("Cache base dir %1 is not absolute", cacheBaseDir);
            throw Error(msg);
        }
    }
    // No environment variable provided, so falling back to the multi user mode, if enabled
    else if (options.multiUser) {
#ifdef WIN32
        cacheBaseDir = Glib::build_filename(user_config_dir, "cache");
#else
        cacheBaseDir = Glib::build_filename(Glib::ustring(g_get_user_cache_dir()), Glib::ustring(CACHEFOLDERNAME));
#endif
    }

    // Read the user option file (the one located somewhere in the user's home folder)
    // Those values supersets those of the global option file
    try {
        options.readFromFile(Glib::build_filename(user_config_dir, "options"));
        if (verbose >= 0) {
            options.rtSettings.verbose = verbose;
        }
        if (options.is_new_version() && lensfun_db_dir_update &&
            !Glib::path_is_absolute(options.rtSettings.lensfunDbDirectory)) {
            options.rtSettings.lensfunDbDirectory = lensfun_db_dir;
        }
    } catch (Options::Error &) {
        // If the local option file does not exist or is broken, and the local cache folder does not exist, recreate it
        if (!g_mkdir_with_parents(user_config_dir.c_str(), 511)) {
            // Save the option file
            options.saveToFile(Glib::build_filename(user_config_dir, "options"));
        }
    }

#ifdef __APPLE__

    if (options.multiUser) {
        // make sure .local/share exists on OS X so we don't get problems with recently-used.xbel
        g_mkdir_with_parents(g_get_user_data_dir(), 511);
    }

#endif

    if (options.rtSettings.verbose) {
        printf("Cache directory (cacheBaseDir) = %s\n", cacheBaseDir.c_str());
    }

    // Update profile's path and recreate it if necessary
    options.updatePaths();

    // Check default Raw and Img procparams existence
    if (options.defProfRaw.empty()) {
        options.defProfRaw = DEFPROFILE_RAW;
    } else {
        if (!options.findProfilePath(options.defProfRaw).empty()) {
            if (options.rtSettings.verbose) {
                std::cout << "Default profile for raw images \"" << options.defProfRaw << "\" found" << std::endl;
            }
        } else {
            if (options.defProfRaw != DEFPROFILE_RAW) {
                options.setDefProfRawMissing(true);

                Glib::ustring dpr(DEFPROFILE_RAW);

                if (options.findProfilePath(dpr).empty()) {
                    options.setBundledDefProfRawMissing(true);
                }
            } else {
                options.setBundledDefProfRawMissing(true);
            }
        }
    }

    if (options.defProfImg.empty()) {
        options.defProfImg = DEFPROFILE_IMG;
    } else {
        if (!options.findProfilePath(options.defProfImg).empty()) {
            if (options.rtSettings.verbose) {
                std::cout << "Default profile for non-raw images \"" << options.defProfImg << "\" found" << std::endl;
            }
        } else {
            if (options.defProfImg != DEFPROFILE_IMG) {
                options.setDefProfImgMissing(true);

                Glib::ustring dpi(DEFPROFILE_IMG);

                if (options.findProfilePath(dpi).empty()) {
                    options.setBundledDefProfImgMissing(true);
                }
            } else {
                options.setBundledDefProfImgMissing(true);
            }
        }
    }

    // We handle languages using a hierarchy of translations.  The top of the hierarchy is default.  This includes a default translation for all items
    // (most likely using simple English).  The next level is the language: for instance, English, French, Chinese, etc.  This file should contain a
    // generic translation for all items which differ from default.  Finally there is the locale.  This is region-specific items which differ from the
    // language file.  These files must be name in the format <Language> (<LC>), where Language is the name of the language which it inherits from,
    // and LC is the local code.  Some examples of this would be English (US) (American English), French (FR) (France French), French (CA) (Canadian
    // French), etc.
    //
    // Each level will only contain the differences between itself and its parent translation.  For instance, English (UK) or English (CA) may
    // include the translation "HISTORY_MSG_34;Avoid Colour Clipping" where English would translate it as "HISTORY_MSG_34;Avoid Color Clipping" (note
    // the difference in the spelling of 'colour').
    //
    // It is important that when naming the translation files, that you stick to the format <Language> or <Language> (<LC>).  We depend on that to figure
    // out which are the parent translations.  Furthermore, there must be a file <Language> for each locale <Language> (<LC>) -- you cannot have
    // 'French (CA)' unless there is a file 'French'.

    Glib::ustring defaultTranslation = Glib::build_filename(ART_base_dir, "languages", "default");
    Glib::ustring languageTranslation = "";
    Glib::ustring localeTranslation = "";

    Glib::ustring user_default_translation = Glib::build_filename(user_config_dir, "languages", "default");
    Glib::ustring user_language_translation = "";
    Glib::ustring user_locale_translation = "";

    Glib::ustring ctl_default_translation = "";
    Glib::ustring user_ctl_default_translation = "";
    Glib::ustring ctl_language_translation = "";
    Glib::ustring ctl_locale_translation = "";
    Glib::ustring user_ctl_language_translation = "";
    Glib::ustring user_ctl_locale_translation = "";

#ifdef ART_USE_CTL
    ctl_default_translation = Glib::build_filename(ART_base_dir, "ctlscripts", "languages", "default");
    user_ctl_default_translation = Glib::build_filename(user_config_dir, "ctlscripts", "languages", "default");
#endif // ART_USE_CTL

    if (options.languageAutoDetect) {
        options.language = langMgr.getOSUserLanguage();
    }

    if (!options.language.empty()) {
        std::vector<Glib::ustring> langPortions = Glib::Regex::split_simple(" ", options.language);

        if (langPortions.size() >= 1) {
            languageTranslation = Glib::build_filename(ART_base_dir, "languages", langPortions.at(0));
            user_language_translation = Glib::build_filename(user_config_dir, "languages", langPortions.at(0));

#ifdef ART_USE_CTL
            ctl_language_translation = Glib::build_filename(ART_base_dir, "ctlscripts", "languages", langPortions.at(0));
            user_ctl_language_translation = Glib::build_filename(user_config_dir, "ctlscripts", "languages", langPortions.at(0));
#endif // ART_USE_CTL
        }

        if (langPortions.size() >= 2) {
            localeTranslation = Glib::build_filename(ART_base_dir, "languages", options.language);
            user_locale_translation = Glib::build_filename(user_config_dir, "languages", options.language);

#ifdef ART_USE_CTL
            ctl_locale_translation = Glib::build_filename(ART_base_dir, "ctlscripts", "languages", options.language);
            user_ctl_locale_translation = Glib::build_filename(user_config_dir, "ctlscripts", "languages", options.language);
#endif // ART_USE_CTL
        }
    }

    std::vector<Glib::ustring> fnames = {
        user_locale_translation, localeTranslation, user_ctl_locale_translation, ctl_locale_translation, 
        user_language_translation, languageTranslation, user_ctl_language_translation, ctl_language_translation, 
        user_default_translation, defaultTranslation, user_ctl_default_translation, ctl_default_translation
    };
    langMgr.load(options.language, fnames);

    rtengine::init(&options.rtSettings, ART_base_dir, user_config_dir, !lightweight);
}

void Options::save()
{
    options.saveToFile(Glib::build_filename(user_config_dir, "options"));
}

/*
 * return true if ext is a parsed extension (retained or not)
 */
bool Options::is_parse_extention(Glib::ustring fname)
{
    Glib::ustring ext = getExtension(fname).lowercase();

    if (!ext.empty()) {
        // there is an extension to the filename

        // look out if it has one of the listed extensions (selected or not)
        for (unsigned int i = 0; i < parseExtensions.size(); i++) {
            if (ext == parseExtensions[i]) {
                return true;
            }
        }
    }

    return false;
}

/*
 * return true if fname ends with one of the retained image file extensions
 */
bool Options::has_retained_extention(const Glib::ustring& fname)
{
    return parsedExtensionsSet.find(getExtension(fname).lowercase()) != parsedExtensionsSet.end();
}

// Pattern matches "5.1" from "5.1-23-g12345678", when comparing option.version to RTVERSION
bool Options::is_new_version()
{
    if (versionString.find('.') == Glib::ustring::npos) {
        return false;
    }
    
    const std::string vs[] = {versionString, version};
    std::vector<std::string> vMajor;

    for (const auto& v : vs) {
        vMajor.emplace_back(v, 0, v.find_first_not_of("0123456789."));
    }

    return vMajor.size() == 2 && vMajor[0] != vMajor[1];
}

/*
 * return true if ext is an enabled extension
 */
bool Options::is_extention_enabled(const Glib::ustring& ext)
{
    return parsedExtensionsSet.find(ext.lowercase()) != parsedExtensionsSet.end();
}

Glib::ustring Options::getUserProfilePath()
{
    return userProfilePath;
}

Glib::ustring Options::getGlobalProfilePath()
{
    return globalProfilePath;
}

bool Options::is_defProfRawMissing()
{
    return defProfError & rtengine::toUnderlying(DefProfError::defProfRawMissing);
}
bool Options::is_defProfImgMissing()
{
    return defProfError & rtengine::toUnderlying(DefProfError::defProfImgMissing);
}
void Options::setDefProfRawMissing(bool value)
{
    if (value) {
        defProfError |= rtengine::toUnderlying(DefProfError::defProfRawMissing);
    } else {
        defProfError &= ~rtengine::toUnderlying(DefProfError::defProfRawMissing);
    }
}
void Options::setDefProfImgMissing(bool value)
{
    if (value) {
        defProfError |= rtengine::toUnderlying(DefProfError::defProfImgMissing);
    } else {
        defProfError &= ~rtengine::toUnderlying(DefProfError::defProfImgMissing);
    }
}
bool Options::is_bundledDefProfRawMissing()
{
    return defProfError & rtengine::toUnderlying(DefProfError::bundledDefProfRawMissing);
}
bool Options::is_bundledDefProfImgMissing()
{
    return defProfError & rtengine::toUnderlying(DefProfError::bundledDefProfImgMissing);
}
void Options::setBundledDefProfRawMissing(bool value)
{
    if (value) {
        defProfError |= rtengine::toUnderlying(DefProfError::bundledDefProfRawMissing);
    } else {
        defProfError &= ~rtengine::toUnderlying(DefProfError::bundledDefProfRawMissing);
    }
}
void Options::setBundledDefProfImgMissing(bool value)
{
    if (value) {
        defProfError |= rtengine::toUnderlying(DefProfError::bundledDefProfImgMissing);
    } else {
        defProfError &= ~rtengine::toUnderlying(DefProfError::bundledDefProfImgMissing);
    }
}
Glib::ustring Options::getICCProfileCopyright()
{
    Glib::Date now;
    now.set_time_current();
    return Glib::ustring::compose("Copyright RawTherapee %1, CC0", now.get_year());
}


Glib::ustring Options::getParamFile(const Glib::ustring &fname)
{
    if (params_sidecar_strip_extension) {
        return removeExtension(fname) + paramFileExtension;
    } else {
        return fname + paramFileExtension;
    }
}


Glib::ustring Options::getXmpSidecarFile(const Glib::ustring &fname)
{
    return rtengine::Exiv2Metadata::xmpSidecarPath(fname);
}
