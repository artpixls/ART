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

#include <gtkmm.h>
#include "adjuster.h"
#include "options.h"
#include <vector>
#include "rtwindow.h"
#include "dynamicprofilepanel.h"

class Preferences : public Gtk::Dialog, public ProfileStoreListener {
    class ExtensionColumns: public Gtk::TreeModel::ColumnRecord {
    public:
        Gtk::TreeModelColumn<bool>  enabled;
        Gtk::TreeModelColumn<Glib::ustring>  ext;
        ExtensionColumns()
        {
            add (enabled);
            add (ext);
        }
    };
    ExtensionColumns extensionColumns;
    Glib::RefPtr<Gtk::ListStore> extensionModel;

    class ThemeFilename {
    public:
        Glib::ustring shortFName;
        Glib::ustring longFName;
        bool deprecated;

        ThemeFilename(Glib::ustring sfname, Glib::ustring lfname, bool d):
            shortFName(sfname), longFName(lfname), deprecated(d) {}
    };

    std::vector<ThemeFilename> themeFNames;
    Glib::RefPtr<Glib::Regex> regex;
    Glib::MatchInfo matchInfo;
    Splash* splash;
    ProfileStoreComboBox* rprofiles;
    Gtk::TreeIter currRawRow; // :)
    ProfileStoreComboBox* iprofiles;
    Gtk::TreeIter currImgRow;
    Gtk::ComboBoxText* languages;
    Gtk::CheckButton* ckbLangAutoDetect;
    Gtk::Entry* dateformat;
    Gtk::Entry* startupdir;
    Gtk::RadioButton* sdcurrent;
    Gtk::RadioButton* sdlast;
    Gtk::RadioButton* sdhome;
    Gtk::RadioButton* sdother;
    MyFileChooserButton* gimpDir;
    MyFileChooserButton* psDir;
    Gtk::Entry* editorToSendTo;
    Gtk::RadioButton* edGimp;
    Gtk::RadioButton* edPS;
    Gtk::RadioButton* edOther;

    Gtk::RadioButton *editor_dir_temp;
    Gtk::RadioButton *editor_dir_current;
    Gtk::RadioButton *editor_dir_custom;
    MyFileChooserButton *editor_dir_custom_path;
    Gtk::CheckButton *editor_float32;
    Gtk::CheckButton *editor_bypass_output_profile;
    
    MyFileChooserButton* darkFrameDir;
    MyFileChooserButton* flatFieldDir;
    MyFileChooserButton* clutsDir;
    Gtk::Label *dfLabel;
    Gtk::Label *ffLabel;

    Gtk::CheckButton* showDateTime;
    Gtk::CheckButton* showBasicExif;
    Gtk::CheckButton* showExpComp;

    MyFileChooserButton* iccDir;
    MyFileChooserButton* monitorIccDir;
    MyFileChooserButton *prtProfile;
    Gtk::ComboBoxText* prtIntent;
    Gtk::CheckButton* prtBPC;
    Gtk::ComboBoxText* monProfile;
    Gtk::ComboBoxText* monIntent;
    Gtk::CheckButton* monBPC;
    Gtk::CheckButton* cbAutoMonProfile;
    //Gtk::CheckButton* cbAutocielab;
    Gtk::SpinButton*  hlThresh;
    Gtk::SpinButton*  shThresh;

    Gtk::SpinButton*  panFactor;
    Gtk::CheckButton* rememberZoomPanCheckbutton;

//   Gtk::ComboBoxText* view;
//    Gtk::ComboBoxText* grey;
//    Gtk::ComboBoxText* greySc;
    Gtk::ComboBoxText* dnv;
    Gtk::ComboBoxText* dnti;
    Gtk::ComboBoxText* dnaut;
    Gtk::ComboBoxText* dnautsimpl;
    Gtk::ComboBoxText* dnwavlev;
    Gtk::ComboBoxText* dnliss;

    Gtk::ComboBoxText* cprevdemo;
    Gtk::CheckButton* ctiffserialize;
    Gtk::ComboBoxText* curveBBoxPosC;

    Gtk::ComboBoxText* themeCBT;
    Gtk::FontButton* mainFontFB;
    Gtk::FontButton* colorPickerFontFB;
    // Gtk::ColorButton* cropMaskColorCB;
    // Gtk::ColorButton* navGuideColorCB;
    Gtk::CheckButton* pseudoHiDPI;

    Gtk::ColorButton *theme_bg_color;
    Gtk::ColorButton *theme_fg_color;
    Gtk::ColorButton *theme_hl_color;
    Gtk::Label *theme_bg_lbl;
    Gtk::Label *theme_fg_lbl;
    Gtk::Label *theme_hl_lbl;
    Gtk::Button *theme_colors_reset;

    Gtk::SpinButton*   maxRecentFolders;
    Gtk::SpinButton*   maxThumbHeightSB;
    Gtk::SpinButton*   maxCacheEntriesSB;
    Gtk::Entry*     extension;
    Gtk::TreeView*  extensions;
    Gtk::Button*    addExt;
    Gtk::Button*    delExt;
    Gtk::Button*    moveExtUp;
    Gtk::Button*    moveExtDown;
    Gtk::CheckButton* overlayedFileNames;
    Gtk::CheckButton* filmStripOverlayedFileNames;
    Gtk::CheckButton* sameThumbSize;

    Gtk::SpinButton*  threadsSpinBtn;
    Gtk::SpinButton*  clutCacheSizeSB;
    Gtk::SpinButton*  maxInspectorBuffersSB;
    Gtk::SpinButton* thumbUpdateThreadLimit;
    Gtk::CheckButton *thumbDelayUpdate;
    Gtk::CheckButton *thumbLazyCaching;
    Gtk::CheckButton *thumb_cache_processed_;
    Gtk::CheckButton *ctl_scripts_fast_preview_;

    // Gtk::CheckButton* ckbmenuGroupRank;
    // Gtk::CheckButton* ckbmenuGroupLabel;
    // Gtk::CheckButton* ckbmenuGroupFileOperations;
    // Gtk::CheckButton* ckbmenuGroupProfileOperations;
    // Gtk::CheckButton* ckbmenuGroupExtProg;

    Gtk::CheckButton* chOverwriteOutputFile;
    Gtk::SpinButton *autosaveInterval;

    Gtk::ComboBoxText* saveParamsPreference;
    Gtk::CheckButton* useBundledProfiles;
    Gtk::ComboBoxText* loadParamsPreference;
    Gtk::ComboBoxText *saveOutParamsPreference;
    Gtk::ComboBoxText* editorLayout;
    Gtk::ComboBoxText *paramsSidecarStripExtension;
    RTWindow* parent;

    Gtk::CheckButton* ckbSndEnable;
    Gtk::Entry* txtSndBatchQueueDone;
    Gtk::Entry* txtSndLngEditProcDone;
    Gtk::SpinButton* spbSndLngEditProcDoneSecs;

    Gtk::CheckButton* ckbInternalThumbIfUntouched;

    Gtk::Entry* txtCustProfBuilderPath;
    Gtk::ComboBoxText* custProfBuilderLabelType;

    Gtk::CheckButton* ckbHistogramPositionLeft;
    Gtk::CheckButton* ckbFileBrowserToolbarSingleRow;
    Gtk::CheckButton* ckbShowFilmStripToolBar;
    Gtk::CheckButton* ckbHideTPVScrollbar;
    Gtk::CheckButton *adjuster_force_linear;

    Gtk::CheckButton* ckbAutoSaveTpOpen;
    Gtk::Button* btnSaveTpOpenNow;
    Gtk::CheckButton *ckbTpDisable;

    DynamicProfilePanel *dynProfilePanel;

    Gtk::CheckButton *denoiseZoomedOut;
    Gtk::CheckButton *thumbRatingMode;
    Gtk::ComboBoxText *metadataSyncCombo;
    Gtk::ComboBoxText *xmpSidecarCombo;
    Gtk::Entry *exiftoolPath;
    Gtk::CheckButton *show_exiftool_makernotes;
    Gtk::ComboBoxText *wbpreview;
    Gtk::CheckButton *remember_metadata_filters;
    Gtk::CheckButton *dir_browser_single_click;
    Gtk::CheckButton *remember_rename_options;

    Gtk::CheckButton *thumbnailInspectorHover;

    MySpinButton *fastexport_max_width;
    MySpinButton *fastexport_max_height;
    
    Glib::ustring storedValueRaw;
    Glib::ustring storedValueImg;

    Options moptions;
    sigc::connection tconn, sconn, fconn, cpfconn, addc, setc, dfconn, ffconn, bpconn, rpconn, ipconn;
    sigc::connection autoMonProfileConn, sndEnableConn, langAutoDetectConn;
    Glib::ustring initialTheme;
    Glib::ustring initialFontFamily;
    int initialFontSize;
    bool newFont;
    bool newCPFont;

    void fillPreferences ();
    void storePreferences ();
    void parseDir       (Glib::ustring dirname, std::vector<Glib::ustring>& items, Glib::ustring ext);
    void parseThemeDir  (Glib::ustring dirname);
    void updateDFinfos ();
    void updateFFinfos ();
    void workflowUpdate();
    void themeChanged  ();
    void fontChanged   ();
    void cpFontChanged ();
    void forRAWComboChanged ();
    void forImageComboChanged ();
    void layoutComboChanged ();
    void bundledProfilesChanged ();
    void iccDirChanged ();
    bool splashClosed (GdkEventAny* event);

    int getThemeRowNumber (Glib::ustring& longThemeFName);

    Gtk::ScrolledWindow *swGeneral;
    Gtk::ScrolledWindow *swImageProcessing;
    Gtk::ScrolledWindow *swDynamicProfile;
    Gtk::ScrolledWindow *swFileBrowser;
    Gtk::ScrolledWindow *swColorMan;
    Gtk::ScrolledWindow *swPerformance;
    Gtk::ScrolledWindow *swSounds;
    Gtk::ScrolledWindow *swFastExport;

    Gtk::Widget *getGeneralPanel();
    Gtk::Widget *getImageProcessingPanel();
    Gtk::Widget *getDynamicProfilePanel();
    Gtk::Widget *getFileBrowserPanel();
    Gtk::Widget *getColorManPanel();
    Gtk::Widget *getPerformancePanel();
    Gtk::Widget *getSoundsPanel();

public:
    explicit Preferences (RTWindow *rtwindow);
    ~Preferences () override;

    void savePressed ();
    void loadPressed ();
    void okPressed ();
    void cancelPressed ();
    void aboutPressed ();
    void autoMonProfileToggled ();
    void sndEnableToggled ();
    void langAutoDetectToggled ();

    void selectStartupDir ();
    void addExtPressed ();
    void delExtPressed ();
    void moveExtUpPressed ();
    void moveExtDownPressed ();
    void darkFrameChanged ();
    void flatFieldChanged ();
    void clearProfilesPressed ();
    void clearThumbImagesPressed ();
    void clearAllPressed ();

    void storeCurrentValue() override;
    void updateProfileList() override;
    void restoreValue() override;

    static void switchThemeTo(const Glib::ustring &newTheme, const Options *opts=nullptr);
    static void switchFontTo(const Glib::ustring &newFontFamily, const int newFontSize);
};
