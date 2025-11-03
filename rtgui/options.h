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

#include <set>
#include <gtkmm.h>
#include "../rtengine/rtengine.h"
#include <exception>
#include "exiffiltersettings.h"

struct SaveFormat {
    SaveFormat(
        const Glib::ustring& _format,
        int _png_bits,
        int _jpeg_quality,
        int _jpeg_sub_samp,
        int _tiff_bits,
        bool _tiff_float,
        bool _tiff_uncompressed,
        bool _save_params
    ) :
        format(_format),
        pngBits(_png_bits),
        jpegQuality(_jpeg_quality),
        jpegSubSamp(_jpeg_sub_samp),
        tiffBits(_tiff_bits),
        tiffFloat(_tiff_float),
        tiffUncompressed(_tiff_uncompressed),
        saveParams(_save_params)
    {
    }
    SaveFormat(
        const Glib::ustring& _format,
        int _png_bits,
        int _tiff_bits,
        bool _tiff_float
    ) :
        SaveFormat(
            _format,
            _png_bits,
            90,
            2,
            _tiff_bits,
            _tiff_float,
            true,
            true
        )
    {
    }
    SaveFormat() :
        SaveFormat("jpg", 8, 8, false)
    {
    }

    Glib::ustring getKey() const;

    Glib::ustring format;
    int pngBits;
    int jpegQuality;
    int jpegSubSamp;  // 1=best compression, 3=best quality
    int tiffBits;
    bool tiffFloat;
    bool tiffUncompressed;
    bool saveParams;
};

enum ThFileType {FT_Invalid = -1, FT_None = 0, FT_Raw = 1, FT_Jpeg = 2, FT_Tiff = 3, FT_Png = 4, FT_Custom = 5, FT_Tiff16 = 6, FT_Png16 = 7, FT_Custom16 = 8};
enum PPLoadLocation {PLL_Cache = 0, PLL_Input = 1};
enum CPBKeyType {CPBKT_TID = 0, CPBKT_NAME = 1, CPBKT_TID_NAME = 2};
enum prevdemo_t {PD_Sidecar = 1, PD_Fast = 0};

class Options {
public:
    enum {
        STARTUPDIR_CURRENT = 0,
        STARTUPDIR_HOME = 1,
        STARTUPDIR_CUSTOM = 2,
        STARTUPDIR_LAST = 3
    };

    static constexpr const char *THEMEREGEXSTR = "^(.+)-GTK3-(\\d{1,2})?_(\\d{1,2})?(-DEPRECATED)?\\.css$";

    // Profile name to use for internal values' profile
    static constexpr const char *DEFPROFILE_INTERNAL = "Neutral";
    // Special name for the Dynamic profile
    static constexpr const char *DEFPROFILE_DYNAMIC = "Dynamic";
    // Default bundled profile name to use for Standard images
    static constexpr const char *DEFPROFILE_IMG = DEFPROFILE_INTERNAL;
    // Default bundled profile name to use for Raw images
    static constexpr const char *DEFPROFILE_RAW = DEFPROFILE_DYNAMIC;

    static constexpr const char *DEFAULT_THEME = "Default";

    static constexpr const char *SESSION_PATH = ":session:";
    
    class Error: public std::exception {
    public:
        explicit Error (const Glib::ustring &msg): msg_ (msg) {}
        const char *what() const throw() override
        {
            return msg_.c_str();
        }
        const Glib::ustring &get_msg() const throw()
        {
            return msg_;
        }

    private:
        Glib::ustring msg_;
    };

private:
    enum class DefProfError : short {
        defProfRawMissing        = 1 << 0,
        bundledDefProfRawMissing = 1 << 1,
        defProfImgMissing        = 1 << 2,
        bundledDefProfImgMissing = 1 << 3
    };
    short defProfError;
    Glib::ustring userProfilePath;
    Glib::ustring globalProfilePath;
    bool checkProfilePath (Glib::ustring &path);
    bool checkDirPath (Glib::ustring &path, Glib::ustring errString);
    void updatePaths();
    int getString (const char* src, char* dst);
    void error (int line);
    /**
     * Safely reads a directory from the configuration file and only applies it
     * to the provided destination variable if there is a non-empty string in
     * the configuration.
     *
     * @param keyFile file to read configuration from
     * @param section name of the section in the configuration file
     * @param entryName name of the entry in the configuration file
     * @param destination destination variable to store to
     * @return @c true if @p destination was changed
     */
    bool safeDirGet (const Glib::KeyFile& keyFile, const Glib::ustring& section,
                     const Glib::ustring& entryName, Glib::ustring& destination);

public:

    static Glib::ustring ART_base_dir;
    static Glib::ustring user_config_dir;
    

    enum class NavigatorUnit {
        PERCENT,
        R0_255,
        R0_1,
        _COUNT
    };

    enum class ScopeType {
        NONE = -1,
        HISTOGRAM,
        HISTOGRAM_RAW,
        PARADE,
        VECTORSCOPE_HC,
        VECTORSCOPE_HS,
        WAVEFORM
    };

    enum class ThumbnailOrder {
        FILENAME,
        DATE,
        DATE_REV,
        MODTIME,
        MODTIME_REV,
        PROCTIME,
        PROCTIME_REV,
        FILENAME_REV
    };
    
    SaveFormat saveFormat;
    SaveFormat saveFormatBatch;
    Glib::ustring savePathTemplate;
    Glib::ustring savePathFolder;
    bool saveUsePathTemplate;
    Glib::ustring defProfRaw;
    Glib::ustring defProfImg;
    Glib::ustring dateFormat;
    int adjusterMinDelay;
    int adjusterMaxDelay;
    int startupDir;
    Gtk::SortType dirBrowserSortType;
    bool dir_browser_single_click;
    Glib::ustring startupPath;
    Glib::ustring profilePath; // can be an absolute or relative path; depending on this value, bundled profiles may not be found
    bool useBundledProfiles;   // only used if multiUser == true
    Glib::ustring lastCopyMovePath;
    Glib::ustring loadSaveProfilePath;
    Glib::ustring lastSaveAsPath;
    int saveAsDialogWidth;
    int saveAsDialogHeight;
    int toolPanelWidth;
    int browserToolPanelWidth;
    int browserToolPanelHeight;
    bool browserToolPanelOpened;
    bool browserDirPanelOpened;
    bool editorFilmStripOpened;
    bool inspectorDirPanelOpened;
    int historyPanelWidth;
    int windowX;
    int windowY;
    int windowWidth;
    int windowHeight;
    bool windowMaximized;
    int windowMonitor;
    int meowMonitor;
    bool meowFullScreen;
    bool meowMaximized;
    int meowWidth;
    int meowHeight;
    int meowX;
    int meowY;
    int detailWindowWidth;
    int detailWindowHeight;
    int dirBrowserWidth;
    int dirBrowserHeight;
    int preferencesWidth;
    int preferencesHeight;
    int lastScale;
    int panAccelFactor;
    Glib::ustring fontFamily;    // RT's main font family
    int fontSize;                // RT's main font size (units: pt)
    Glib::ustring CPFontFamily;  // ColorPicker font family
    int CPFontSize;              // ColorPicker font size (units: pt)
    bool pseudoHiDPISupport;
    bool fbShowDateTime;
    bool fbShowBasicExif;
    bool fbShowExpComp;
    bool fbShowHidden;
    NavigatorUnit navRGBUnit;
    NavigatorUnit navLCHUnit;
    bool multiUser;
    Glib::ustring version;
    int thumbSize;
    int thumbSizeTab;
    int thumbSizeQueue;
    bool sameThumbSize;     // Will use only one thumb size for the file browser and the single editor tab, and avoid recomputing them
    ThumbnailOrder thumbnailOrder;
    bool showHistory;
    bool showInfo;
    bool filmstripBottom;
    bool showClippedHighlights;
    bool showClippedShadows;
    int highlightThreshold;
    int shadowThreshold;
    int bgcolor;
    Glib::ustring language;
    bool languageAutoDetect;
    Glib::ustring theme;
    static Glib::ustring cacheBaseDir;
    bool autoSuffix;
    bool forceFormatOpts;
    int saveMethodNum;
    bool saveParamsFile;
    bool saveParamsCache;
    PPLoadLocation paramsLoadLocation;
    bool params_out_embed;
    bool params_sidecar_strip_extension;
    
    bool procQueueEnabled;
    Glib::ustring gimpDir;
    Glib::ustring psDir;
    Glib::ustring customEditorProg;
    Glib::ustring CPBPath; // Custom Profile Builder's path
    CPBKeyType CPBKeys; // Custom Profile Builder's key type
    int editorToSendTo;
    enum EditorOutDir {
        EDITOR_OUT_DIR_TEMP,
        EDITOR_OUT_DIR_CURRENT,
        EDITOR_OUT_DIR_CUSTOM
    };
    EditorOutDir editor_out_dir; // output directory for "open in external editor"
    Glib::ustring editor_custom_out_dir;
    bool editor_float32;
    bool editor_bypass_output_profile;
        
    int maxThumbnailHeight;
    int maxThumbnailWidth;
    std::size_t maxCacheEntries;
    int thumbInterp; // 0: nearest, 1: bilinear
    std::vector<Glib::ustring> parseExtensions;   // List containing all extensions type
    std::vector<int> parseExtensionsEnabled;      // List of bool to retain extension or not
    std::vector<Glib::ustring> parsedExtensions;  // List containing all retained extensions (lowercase)
    std::set<std::string> parsedExtensionsSet;  // Set containing all retained extensions (lowercase)
    std::vector<int> tpOpen;
    bool autoSaveTpOpen;
    rtengine::Settings rtSettings;

    std::vector<Glib::ustring> favoriteDirs;
    bool internalThumbIfUntouched;
    bool overwriteOutputFile;

    std::vector<double> thumbnailZoomRatios;
    bool overlayedFileNames;
    bool filmStripOverlayedFileNames;
    bool showFileNames;
    bool filmStripShowFileNames;
    bool tabbedUI;
    bool rememberZoomAndPan;
    int multiDisplayMode;  // 0=none, 1=Edit panels on other display
    std::vector<double> cutOverlayBrush;  // Red;Green;Blue;Alpha , all ranging 0..1
    std::vector<double> navGuideBrush;  // Red;Green;Blue;Alpha , all ranging 0..1

    Glib::ustring sndBatchQueueDone;
    Glib::ustring sndLngEditProcDone;
    double sndLngEditProcDoneSecs;  // Minimum processing time seconds till the sound is played
    bool sndEnable;

    enum { HISTOGRAM_POS_OFF = 0, HISTOGRAM_POS_LEFT = 1, HISTOGRAM_POS_RIGHT = 2};
    int histogramPosition;  // 0=disabled, 1=left pane, 2=right pane
    bool histogramRed;
    bool histogramGreen;
    bool histogramBlue;
    bool histogramLuma;
    bool histogramChroma;
    //bool histogramRAW;
    bool histogramBar;
    int histogramHeight;
    int histogramDrawMode;
    double histogram_scaling_factor;
    ScopeType histogramScopeType;
    bool histogramShowOptionButtons;
    float histogramTraceBrightness;

    bool FileBrowserToolbarSingleRow;
    bool hideTPVScrollbar;
    int whiteBalanceSpotSize;
    int curvebboxpos; // 0=above, 1=right, 2=below, 3=left

    bool showFilmStripToolBar;

    // cropping options
    int cropPPI;

    // Performance options
    Glib::ustring clutsDir;
    int rgbDenoiseThreadLimit; // maximum number of threads for the denoising tool ; 0 = use the maximum available
    int maxInspectorBuffers;   // maximum number of buffers (i.e. images) for the Inspector feature
    int inspectorDelay;
    int clutCacheSize;
    bool thumb_delay_update;
    bool thumb_lazy_caching;
    bool thumb_cache_processed;
    bool profile_append_mode;  // Used as reminder for the ProfilePanel "mode"
    prevdemo_t prevdemo; // Demosaicing method used for the <100% preview
    bool serializeTiffRead;
    bool denoiseZoomedOut;
    enum WBPreviewMode {
        WB_AFTER, // apply WB after demosaicing (faster)
        WB_BEFORE, // always apply WB before demosaicing
        WB_BEFORE_HIGH_DETAIL // apply WB before demosaicing only at 1:1
    };
    WBPreviewMode wb_preview_mode;

    bool menuGroupRank;
    bool menuGroupLabel;
    bool menuGroupFileOperations;
    bool menuGroupProfileOperations;
    bool menuGroupExtProg;

    // fast export options
    int fastexport_resize_width;
    int fastexport_resize_height;

    std::vector<Glib::ustring> favorites;
    // Dialog settings
    Glib::ustring lastIccDir;
    Glib::ustring lastDarkframeDir;
    Glib::ustring lastFlatfieldDir;
    Glib::ustring lastRgbCurvesDir;
    Glib::ustring lastLabCurvesDir;
    Glib::ustring lastPFCurvesDir;
    Glib::ustring lastHsvCurvesDir;
    Glib::ustring lastToneCurvesDir;
    Glib::ustring lastColorToningCurvesDir;
    Glib::ustring lastProfilingReferenceDir;
    Glib::ustring lastLensProfileDir;
    Glib::ustring lastICCProfCreatorDir;
    Glib::ustring last_session_add_dir;
    Glib::ustring last_session_loadsave_dir;
    bool gimpPluginShowInfoDialog;

    size_t maxRecentFolders;                   // max. number of recent folders stored in options file
    std::vector<Glib::ustring> recentFolders;  // List containing all recent folders

    enum class ThumbnailRatingMode {
        PROCPARAMS, // store ranking and color labels in procparams sidecars
        XMP // store in FILENAME.xmp for FILENAME.raw
    };
    ThumbnailRatingMode thumbnail_rating_mode;

    bool thumbnail_inspector_zoom_fit;
    bool thumbnail_inspector_show_info;
    bool thumbnail_inspector_enable_cms;
    int browser_width_for_inspector;
    bool thumbnail_inspector_show_histogram;
    bool thumbnail_inspector_hover;

    // Glib::ustring batch_queue_profile_path;
    // bool batch_queue_use_profile;

    bool toolpanels_disable;
    bool adjuster_force_linear;
    
    int error_message_duration; // in milliseconds
    int max_error_messages;

    // maps a IRE value to a false color
    // taken from
    // https://www.premiumbeat.com/blog/how-to-use-false-color-nail-skin-tone-exposure/
    std::map<int, std::string> falseColorsMap;
    std::string clipped_highlights_color;
    std::string clipped_shadows_color;

    struct RenameOptions {
        Glib::ustring pattern;
        Glib::ustring sidecars;
        int name_norm;
        int ext_norm;
        bool allow_whitespace;
        int on_existing;
        int progressive_number;
        bool remember;

        RenameOptions();
    };
    RenameOptions renaming;

    int sidecar_autosave_interval; // in seconds

    int editor_keyboard_scroll_step; // in pixels
    int adjuster_shortcut_scrollwheel_factor; // to control the adjustment step when using tool shortcuts with the mouse wheel

    bool remember_exif_filter_settings;
    ExifFilterSettings last_exif_filter_settings;

    bool show_exiftool_makernotes;

    std::vector<int> theme_bg_color; // RGB in 0-255
    std::vector<int> theme_fg_color;
    std::vector<int> theme_hl_color;
    std::string svg_color;
    static const std::string svg_dark_color;
    static const std::string svg_light_color;

    enum class PreviewResamplingQuality {
        LOW,
        MEDIUM,
        HIGH
    };
    PreviewResamplingQuality preview_resampling_quality;

    struct ExportProfileInfo {
        Glib::ustring profile;
        bool enabled;
        ExportProfileInfo(const Glib::ustring &p="", bool e=false):
            profile(p), enabled(e) {}
    };
    std::map<Glib::ustring, ExportProfileInfo> export_profile_map;

    Options();

    Options *copyFrom(Options *other);
    void filterOutParsedExtensions();
    void setDefaults();
    void readFromFile(Glib::ustring fname);
    void saveToFile(Glib::ustring fname);
    static void load(bool lightweight=false, int verbose=-1);
    static void save();

    // if multiUser=false, send back the global profile path
    Glib::ustring getPreferredProfilePath();
    Glib::ustring getUserProfilePath();
    Glib::ustring getGlobalProfilePath();
    Glib::ustring findProfilePath (Glib::ustring &profName);
    bool is_parse_extention(Glib::ustring fname);
    bool has_retained_extention(const Glib::ustring& fname);
    bool is_new_version();
    bool is_extention_enabled(const Glib::ustring& ext);
    bool is_defProfRawMissing();
    bool is_bundledDefProfRawMissing();
    bool is_defProfImgMissing();
    bool is_bundledDefProfImgMissing();
    void setDefProfRawMissing(bool value);
    void setBundledDefProfRawMissing(bool value);
    void setDefProfImgMissing(bool value);
    void setBundledDefProfImgMissing(bool value);
    static Glib::ustring getICCProfileCopyright();

    Glib::ustring getParamFile(const Glib::ustring &fname);
    Glib::ustring getXmpSidecarFile(const Glib::ustring &fname);
};

extern Options options;
extern Glib::ustring argv1;
extern bool simpleEditor;
extern bool gimpPlugin;
extern bool remote;
extern Glib::ustring versionString;
extern Glib::ustring paramFileExtension;
