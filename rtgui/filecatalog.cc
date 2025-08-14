/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>
 *  Copyright (c) 2011 Michael Ezra <www.michaelezra.com>
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
#include "filecatalog.h"

#include <iostream>
#include <iomanip>
#include <sstream>

#include <glib/gstdio.h>

#include "../rtengine/rt_math.h"

#include "guiutils.h"
#include "options.h"
#include "rtimage.h"
#include "cachemanager.h"
#include "multilangmgr.h"
#include "filepanel.h"
#include "thumbimageupdater.h"
#include "batchqueue.h"
#include "placesbrowser.h"
#include "fastexport.h"
#include "../rtengine/imagedata.h"
#include "session.h"
#include "rtwindow.h"

namespace {

class DirCompletion: public Gtk::EntryCompletion {
public:
    DirCompletion():
        root_("")
    { 
        model_ = Gtk::ListStore::create(record_);
        set_model(model_);
        set_text_column(record_.column);
    }
    
    void refresh(const Glib::ustring &root)
    {
        if (root != root_) {
            root_ = root;

            try {
                std::vector<Glib::ustring> entries;
                Glib::Dir dir(root_);
                for (auto fn : dir) {
                    auto pth = Glib::build_filename(root_, fn);
                    if (Glib::file_test(pth, Glib::FILE_TEST_IS_DIR)) {
                        entries.push_back(pth);
                    }
                }

                model_->clear();
                std::sort(entries.begin(), entries.end());
                for (auto &d : entries) {
                    auto row = *(model_->append());
                    row[record_.column] = d;
                }
            } catch (Glib::Exception &exc) {
            }
        }
    }

private:
    Glib::RefPtr<Gtk::ListStore> model_;

    class CompletionRecord: public Gtk::TreeModel::ColumnRecord {
    public:
        CompletionRecord()
        {
            add(column);
        }
        Gtk::TreeModelColumn<Glib::ustring> column;
    };

    CompletionRecord record_;
    Glib::ustring root_;
};


int thumb_order_to_index(Options::ThumbnailOrder order)
{
    switch (order) {
    case Options::ThumbnailOrder::FILENAME: return 0;
    case Options::ThumbnailOrder::FILENAME_REV: return 1;
    case Options::ThumbnailOrder::DATE: return 2;
    case Options::ThumbnailOrder::DATE_REV: return 3;
    case Options::ThumbnailOrder::MODTIME: return 4;
    case Options::ThumbnailOrder::MODTIME_REV: return 5;
    case Options::ThumbnailOrder::PROCTIME: return 6;
    case Options::ThumbnailOrder::PROCTIME_REV: return 7;
    default: return 0;
    }
}


Options::ThumbnailOrder index_to_thumb_order(int index)
{
    switch (index) {
    case 0: return Options::ThumbnailOrder::FILENAME;
    case 1: return Options::ThumbnailOrder::FILENAME_REV;
    case 2: return Options::ThumbnailOrder::DATE;
    case 3: return Options::ThumbnailOrder::DATE_REV;
    case 4: return Options::ThumbnailOrder::MODTIME;
    case 5: return Options::ThumbnailOrder::MODTIME_REV;
    case 6: return Options::ThumbnailOrder::PROCTIME;
    case 7: return Options::ThumbnailOrder::PROCTIME_REV;
    default: return Options::ThumbnailOrder::FILENAME;
    }
}


} // namespace


#define CHECKTIME 2000

FileCatalog::FileCatalog(FilePanel* filepanel) :
    filepanel(filepanel),
    selectedDirectoryId(1),
    refresh_counter_(1),
    actionNextPrevious(NAV_NONE),
    listener(nullptr),
    fslistener(nullptr),
    hbToolBar1STB(nullptr),
    hasValidCurrentEFS(false),
    filterPanel(nullptr),
    filter_panel_update_(false),
    previewsToLoad(0),
    previewsLoaded(0),
    modifierKey(0),
    bqueue_(nullptr)
{
    inTabMode = false;

    set_name ("FileBrowser");

    //  construct and initialize thumbnail browsers
    fileBrowser = Gtk::manage( new FileBrowser() );
    fileBrowser->setFileBrowserListener (this);
    fileBrowser->setArrangement (ThumbBrowserBase::TB_Vertical);
    fileBrowser->show ();

    set_size_request(0, 250);
    // construct trash panel with the extra "empty trash" button
    trashButtonBox = Gtk::manage( new Gtk::VBox );
    Gtk::Button* emptyT = Gtk::manage( new Gtk::Button ());
    emptyT->set_tooltip_markup (M("FILEBROWSER_EMPTYTRASHHINT"));
    emptyT->set_image (*Gtk::manage(new RTImage ("trash-delete.png")));
    emptyT->signal_pressed().connect (sigc::mem_fun(*this, &FileCatalog::emptyTrash));
    trashButtonBox->pack_start (*emptyT, Gtk::PACK_SHRINK, 4);
    emptyT->show ();
    trashButtonBox->show ();

    //initialize hbToolBar1
    hbToolBar1 = Gtk::manage(new Gtk::HBox());

    //setup BrowsePath
    Gtk::HBox* hbBrowsePath = Gtk::manage(new Gtk::HBox());
    
    button_session_load_ = Gtk::manage(new Gtk::Button());
    button_session_load_->set_image(*Gtk::manage(new RTImage("folder-open-recent-small.png")));
    button_session_load_->set_tooltip_markup(M("FILEBROWSER_SESSION_LOAD_LABEL"));
    button_session_load_->set_relief(Gtk::RELIEF_NONE);
    button_session_load_->signal_clicked().connect(sigc::mem_fun(*this, &FileCatalog::sessionLoadPressed));
    hbBrowsePath->pack_start(*button_session_load_, Gtk::PACK_SHRINK, 0);
    button_session_load_->hide();

    button_session_save_ = Gtk::manage(new Gtk::Button ());
    button_session_save_->set_image(*Gtk::manage(new RTImage("save-small.png")));
    button_session_save_->set_tooltip_markup(M("FILEBROWSER_SESSION_SAVE_LABEL"));
    button_session_save_->set_relief(Gtk::RELIEF_NONE);
    button_session_save_->signal_clicked().connect(sigc::mem_fun(*this, &FileCatalog::sessionSavePressed));
    hbBrowsePath->pack_start(*button_session_save_, Gtk::PACK_SHRINK, 0);
    button_session_save_->hide();
    
    button_session_add_ = Gtk::manage(new Gtk::Button ());
    button_session_add_->set_image(*Gtk::manage(new RTImage("add-small.png")));
    button_session_add_->set_tooltip_markup(M("FILEBROWSER_SESSION_ADD_LABEL"));
    button_session_add_->set_relief(Gtk::RELIEF_NONE);
    button_session_add_->signal_clicked().connect(sigc::mem_fun(*this, &FileCatalog::sessionAddPressed));
    hbBrowsePath->pack_start(*button_session_add_, Gtk::PACK_SHRINK, 0);
    button_session_add_->hide();
    
    button_session_remove_ = Gtk::manage(new Gtk::Button ());
    button_session_remove_->set_image(*Gtk::manage(new RTImage("remove-small.png")));
    button_session_remove_->set_tooltip_markup(M("FILEBROWSER_SESSION_REMOVE_LABEL"));
    button_session_remove_->set_relief(Gtk::RELIEF_NONE);
    button_session_remove_->signal_clicked().connect(sigc::mem_fun(*this, &FileCatalog::sessionRemovePressed));
    hbBrowsePath->pack_start(*button_session_remove_, Gtk::PACK_SHRINK, 0);
    button_session_remove_->hide();
    
    iRefreshWhite = new RTImage("refresh-small.png");
    iRefreshRed = new RTImage("refresh-red-small.png");

    BrowsePath = Gtk::manage(new Gtk::Entry ());
    BrowsePath->set_width_chars (50);
    BrowsePath->set_tooltip_markup (M("FILEBROWSER_BROWSEPATHHINT"));
    buttonBrowsePath = Gtk::manage(new Gtk::Button ());
    buttonBrowsePath->set_image (*iRefreshWhite);
    buttonBrowsePath->set_tooltip_markup (M("FILEBROWSER_BROWSEPATHBUTTONHINT"));
    buttonBrowsePath->set_relief (Gtk::RELIEF_NONE);
    buttonBrowsePath->signal_clicked().connect( sigc::mem_fun(*this, &FileCatalog::browsePathRefresh) );

    button_recurse_ = Gtk::manage(new Gtk::ToggleButton());
    button_recurse_->set_image(*Gtk::manage(new RTImage("folder-recurse-small.png")));
    button_recurse_->set_tooltip_markup(M("FILEBROWSER_BROWSEPATH_RECURSIVE_TOOLTIP"));
    button_recurse_->set_relief(Gtk::RELIEF_NONE);
    recurse_conn_ = button_recurse_->signal_toggled().connect(sigc::mem_fun(*this, &FileCatalog::browsePathRefresh));
    is_toggling_recurse_ = false;
    
    hbBrowsePath->pack_start(*button_recurse_, Gtk::PACK_SHRINK, 0);
    hbBrowsePath->pack_start (*BrowsePath, Gtk::PACK_EXPAND_WIDGET, 0);
    hbBrowsePath->pack_start (*buttonBrowsePath, Gtk::PACK_SHRINK, 0);

    hbToolBar1->pack_start (*hbBrowsePath, Gtk::PACK_EXPAND_WIDGET, 0);

    browsePathCompletion = Glib::RefPtr<Gtk::EntryCompletion>(new DirCompletion());
    BrowsePath->set_completion(browsePathCompletion);
    browsePathCompletion->set_minimum_key_length(1);
    
    BrowsePath->signal_activate().connect (sigc::mem_fun(*this, &FileCatalog::buttonBrowsePathPressed)); //respond to the Enter key
    BrowsePath->signal_key_press_event().connect(sigc::mem_fun(*this, &FileCatalog::BrowsePath_key_pressed));
    BrowsePath->signal_changed().connect(sigc::mem_fun(*this, &FileCatalog::onBrowsePathChanged));

    //setup Query
    iQueryClear = new RTImage("cancel-small.png");
    Gtk::Label* labelQuery = Gtk::manage(new Gtk::Label(M("FILEBROWSER_QUERYLABEL")));
    Query = Gtk::manage(new Gtk::Entry ()); // cannot use Gtk::manage here as FileCatalog::getFilter will fail on Query->get_text()
    Query->set_text("");
    Query->set_width_chars (20); // TODO !!! add this value to options?
    Query->set_max_width_chars (20);
    Query->set_tooltip_markup (M("FILEBROWSER_QUERYHINT"));
    Gtk::HBox* hbQuery = Gtk::manage(new Gtk::HBox ());
    buttonQueryClear = Gtk::manage(new Gtk::Button ());
    buttonQueryClear->set_image (*iQueryClear);
    buttonQueryClear->set_tooltip_markup (M("FILEBROWSER_QUERYBUTTONHINT"));
    buttonQueryClear->set_relief (Gtk::RELIEF_NONE);
    buttonQueryClear->signal_clicked().connect( sigc::mem_fun(*this, &FileCatalog::buttonQueryClearPressed) );
    hbQuery->pack_start (*labelQuery, Gtk::PACK_SHRINK, 0);
    hbQuery->pack_start (*Query, Gtk::PACK_SHRINK, 0);
    hbQuery->pack_start (*buttonQueryClear, Gtk::PACK_SHRINK, 0);
    hbToolBar1->pack_start (*hbQuery, Gtk::PACK_SHRINK, 0);

    Query->signal_activate().connect (sigc::mem_fun(*this, &FileCatalog::executeQuery)); //respond to the Enter key
    Query->signal_key_press_event().connect(sigc::mem_fun(*this, &FileCatalog::Query_key_pressed));

    // if NOT a single row toolbar
    if (!options.FileBrowserToolbarSingleRow) {
        hbToolBar1STB = Gtk::manage(new MyScrolledToolbar());
        hbToolBar1STB->set_name("FileBrowserQueryToolbar");
        hbToolBar1STB->add(*hbToolBar1);
        pack_start (*hbToolBar1STB, Gtk::PACK_SHRINK, 0);
    }

    // setup button bar
    buttonBar = Gtk::manage( new Gtk::HBox () );
    buttonBar->set_name ("ToolBarPanelFileBrowser");
    MyScrolledToolbar *stb = Gtk::manage(new MyScrolledToolbar());
    stb->set_name("FileBrowserIconToolbar");
    stb->add(*buttonBar);
    pack_start (*stb, Gtk::PACK_SHRINK);

    tbLeftPanel_1 = new Gtk::ToggleButton ();
    iLeftPanel_1_Show = new RTImage("panel-to-right.png");
    iLeftPanel_1_Hide = new RTImage("panel-to-left.png");

    tbLeftPanel_1->set_relief(Gtk::RELIEF_NONE);
    tbLeftPanel_1->set_active (true);
    tbLeftPanel_1->set_tooltip_markup (M("MAIN_TOOLTIP_SHOWHIDELP1"));
    tbLeftPanel_1->set_image (*iLeftPanel_1_Hide);
    tbLeftPanel_1->signal_toggled().connect( sigc::mem_fun(*this, &FileCatalog::tbLeftPanel_1_toggled) );
    buttonBar->pack_start (*tbLeftPanel_1, Gtk::PACK_SHRINK);

    vSepiLeftPanel = new Gtk::VSeparator ();
    buttonBar->pack_start (*vSepiLeftPanel, Gtk::PACK_SHRINK);

    iFilterClear = new RTImage ("filter-clear.png");
    igFilterClear = new RTImage ("filter.png");
    bFilterClear = Gtk::manage(new Gtk::ToggleButton ());
    bFilterClear->set_active (true);
    bFilterClear->set_image(*iFilterClear);// (*Gtk::manage(new RTImage ("filter-clear.png")));
    bFilterClear->set_relief (Gtk::RELIEF_NONE);
    bFilterClear->set_tooltip_markup (M("FILEBROWSER_SHOWDIRHINT"));
    bFilterClear->signal_button_press_event().connect (sigc::mem_fun(*this, &FileCatalog::capture_event), false);
    bCateg[0] = bFilterClear->signal_toggled().connect (sigc::bind(sigc::mem_fun(*this, &FileCatalog::categoryButtonToggled), bFilterClear, true));
    buttonBar->pack_start (*bFilterClear, Gtk::PACK_SHRINK);
    buttonBar->pack_start (*Gtk::manage(new Gtk::VSeparator), Gtk::PACK_SHRINK);

    fltrVbox1 = Gtk::manage (new Gtk::VBox());
    fltrRankbox = Gtk::manage (new Gtk::HBox());
    fltrRankbox->get_style_context()->add_class("smallbuttonbox");
    fltrLabelbox = Gtk::manage (new Gtk::HBox());
    fltrLabelbox->get_style_context()->add_class("smallbuttonbox");

    iUnRanked = new RTImage ("star-gold-hollow-small.png");
    igUnRanked = new RTImage ("star-hollow-small.png");
    bUnRanked = Gtk::manage( new Gtk::ToggleButton () );
    bUnRanked->get_style_context()->add_class("smallbutton");
    bUnRanked->set_active (false);
    bUnRanked->set_image (*igUnRanked);
    bUnRanked->set_relief (Gtk::RELIEF_NONE);
    bUnRanked->set_tooltip_markup (M("FILEBROWSER_SHOWUNRANKHINT"));
    bCateg[1] = bUnRanked->signal_toggled().connect (sigc::bind(sigc::mem_fun(*this, &FileCatalog::categoryButtonToggled), bUnRanked, true));
    fltrRankbox->pack_start (*bUnRanked, Gtk::PACK_SHRINK);
    bUnRanked->signal_button_press_event().connect (sigc::mem_fun(*this, &FileCatalog::capture_event), false);

    for (int i = 0; i < 5; i++) {
        iranked[i] = new RTImage ("star-gold-small.png");
        igranked[i] = new RTImage ("star-small.png");
        iranked[i]->show ();
        igranked[i]->show ();
        bRank[i] = Gtk::manage( new Gtk::ToggleButton () );
        bRank[i]->get_style_context()->add_class("smallbutton");
        bRank[i]->set_image (*igranked[i]);
        bRank[i]->set_relief (Gtk::RELIEF_NONE);
        fltrRankbox->pack_start (*bRank[i], Gtk::PACK_SHRINK);
        bCateg[i + 2] = bRank[i]->signal_toggled().connect (sigc::bind(sigc::mem_fun(*this, &FileCatalog::categoryButtonToggled), bRank[i], true));
        bRank[i]->signal_button_press_event().connect (sigc::mem_fun(*this, &FileCatalog::capture_event), false);
    }

    // Toolbar
    // Similar image arrays in filebrowser.cc
    std::array<std::string, 6> clabelActiveIcons = {"circle-gray-small.png", "circle-red-small.png", "circle-yellow-small.png", "circle-green-small.png", "circle-blue-small.png", "circle-purple-small.png"};
    std::array<std::string, 6> clabelInactiveIcons = {"circle-empty-gray-small.png", "circle-empty-red-small.png", "circle-empty-yellow-small.png", "circle-empty-green-small.png", "circle-empty-blue-small.png", "circle-empty-purple-small.png"};

    iUnCLabeled = new RTImage(clabelActiveIcons[0]);
    igUnCLabeled = new RTImage(clabelInactiveIcons[0]);
    bUnCLabeled = Gtk::manage(new Gtk::ToggleButton());
    bUnCLabeled->get_style_context()->add_class("smallbutton");
    bUnCLabeled->set_active(false);
    bUnCLabeled->set_image(*igUnCLabeled);
    bUnCLabeled->set_relief(Gtk::RELIEF_NONE);
    bUnCLabeled->set_tooltip_markup(M("FILEBROWSER_SHOWUNCOLORHINT"));
    bCateg[7] = bUnCLabeled->signal_toggled().connect (sigc::bind(sigc::mem_fun(*this, &FileCatalog::categoryButtonToggled), bUnCLabeled, true));
    fltrLabelbox->pack_start(*bUnCLabeled, Gtk::PACK_SHRINK);
    bUnCLabeled->signal_button_press_event().connect (sigc::mem_fun(*this, &FileCatalog::capture_event), false);

    for (int i = 0; i < 5; i++) {
        iCLabeled[i] = new RTImage(clabelActiveIcons[i+1]);
        igCLabeled[i] = new RTImage(clabelInactiveIcons[i+1]);
        iCLabeled[i]->show();
        igCLabeled[i]->show();
        bCLabel[i] = Gtk::manage(new Gtk::ToggleButton());
        bCLabel[i]->get_style_context()->add_class("smallbutton");
        bCLabel[i]->set_image(*igCLabeled[i]);
        bCLabel[i]->set_relief(Gtk::RELIEF_NONE);
        fltrLabelbox->pack_start(*bCLabel[i], Gtk::PACK_SHRINK);
        bCateg[i + 8] = bCLabel[i]->signal_toggled().connect (sigc::bind(sigc::mem_fun(*this, &FileCatalog::categoryButtonToggled), bCLabel[i], true));
        bCLabel[i]->signal_button_press_event().connect (sigc::mem_fun(*this, &FileCatalog::capture_event), false);
    }

    fltrVbox1->pack_start (*fltrRankbox, Gtk::PACK_SHRINK, 0);
    fltrVbox1->pack_start (*fltrLabelbox, Gtk::PACK_SHRINK, 0);
    buttonBar->pack_start (*fltrVbox1, Gtk::PACK_SHRINK);

    bRank[0]->set_tooltip_markup (M("FILEBROWSER_SHOWRANK1HINT"));
    bRank[1]->set_tooltip_markup (M("FILEBROWSER_SHOWRANK2HINT"));
    bRank[2]->set_tooltip_markup (M("FILEBROWSER_SHOWRANK3HINT"));
    bRank[3]->set_tooltip_markup (M("FILEBROWSER_SHOWRANK4HINT"));
    bRank[4]->set_tooltip_markup (M("FILEBROWSER_SHOWRANK5HINT"));

    bCLabel[0]->set_tooltip_markup (M("FILEBROWSER_SHOWCOLORLABEL1HINT"));
    bCLabel[1]->set_tooltip_markup (M("FILEBROWSER_SHOWCOLORLABEL2HINT"));
    bCLabel[2]->set_tooltip_markup (M("FILEBROWSER_SHOWCOLORLABEL3HINT"));
    bCLabel[3]->set_tooltip_markup (M("FILEBROWSER_SHOWCOLORLABEL4HINT"));
    bCLabel[4]->set_tooltip_markup (M("FILEBROWSER_SHOWCOLORLABEL5HINT"));

    buttonBar->pack_start (*Gtk::manage(new Gtk::VSeparator), Gtk::PACK_SHRINK);

    fltrVbox2 = Gtk::manage (new Gtk::VBox());
    fltrEditedBox = Gtk::manage (new Gtk::HBox());
    fltrEditedBox->get_style_context()->add_class("smallbuttonbox");
    fltrRecentlySavedBox = Gtk::manage (new Gtk::HBox());
    fltrRecentlySavedBox->get_style_context()->add_class("smallbuttonbox");

    // bEdited
    // TODO The "g" variant was the more transparent variant of the icon, used
    // when the button was not toggled. Simplify this, change to ordinary
    // togglebutton, use CSS for opacity change.
    iEdited[0] = new RTImage ("tick-hollow-small.png");
    igEdited[0] = new RTImage ("tick-hollow-small.png");
    iEdited[1] = new RTImage ("tick-small.png");
    igEdited[1] = new RTImage ("tick-small.png");

    for (int i = 0; i < 2; i++) {
        iEdited[i]->show ();
        bEdited[i] = Gtk::manage(new Gtk::ToggleButton ());
        bEdited[i]->get_style_context()->add_class("smallbutton");
        bEdited[i]->set_active (false);
        bEdited[i]->set_image (*igEdited[i]);
        bEdited[i]->set_relief (Gtk::RELIEF_NONE);
        fltrEditedBox->pack_start (*bEdited[i], Gtk::PACK_SHRINK);
        //13, 14
        bCateg[i + 13] = bEdited[i]->signal_toggled().connect (sigc::bind(sigc::mem_fun(*this, &FileCatalog::categoryButtonToggled), bEdited[i], true));
        bEdited[i]->signal_button_press_event().connect (sigc::mem_fun(*this, &FileCatalog::capture_event), false);
    }

    bEdited[0]->set_tooltip_markup (M("FILEBROWSER_SHOWEDITEDNOTHINT"));
    bEdited[1]->set_tooltip_markup (M("FILEBROWSER_SHOWEDITEDHINT"));

    // RecentlySaved
    // TODO The "g" variant was the more transparent variant of the icon, used
    // when the button was not toggled. Simplify this, change to ordinary
    // togglebutton, use CSS for opacity change.
    iRecentlySaved[0] = new RTImage ("saved-no-small.png");
    igRecentlySaved[0] = new RTImage ("saved-no-small.png");
    iRecentlySaved[1] = new RTImage ("saved-yes-small.png");
    igRecentlySaved[1] = new RTImage ("saved-yes-small.png");

    for (int i = 0; i < 2; i++) {
        iRecentlySaved[i]->show ();
        bRecentlySaved[i] = Gtk::manage(new Gtk::ToggleButton ());
        bRecentlySaved[i]->get_style_context()->add_class("smallbutton");
        bRecentlySaved[i]->set_active (false);
        bRecentlySaved[i]->set_image (*igRecentlySaved[i]);
        bRecentlySaved[i]->set_relief (Gtk::RELIEF_NONE);
        fltrRecentlySavedBox->pack_start (*bRecentlySaved[i], Gtk::PACK_SHRINK);
        //15, 16
        bCateg[i + 15] = bRecentlySaved[i]->signal_toggled().connect (sigc::bind(sigc::mem_fun(*this, &FileCatalog::categoryButtonToggled), bRecentlySaved[i], true));
        bRecentlySaved[i]->signal_button_press_event().connect (sigc::mem_fun(*this, &FileCatalog::capture_event), false);
    }

    bRecentlySaved[0]->set_tooltip_markup (M("FILEBROWSER_SHOWRECENTLYSAVEDNOTHINT"));
    bRecentlySaved[1]->set_tooltip_markup (M("FILEBROWSER_SHOWRECENTLYSAVEDHINT"));

    fltrVbox2->pack_start (*fltrEditedBox, Gtk::PACK_SHRINK, 0);
    fltrVbox2->pack_start (*fltrRecentlySavedBox, Gtk::PACK_SHRINK, 0);
    buttonBar->pack_start (*fltrVbox2, Gtk::PACK_SHRINK);

    buttonBar->pack_start (*Gtk::manage(new Gtk::VSeparator), Gtk::PACK_SHRINK);

    // Trash
    iTrashShowEmpty = new RTImage("trash-empty-show.png") ;
    iTrashShowFull  = new RTImage("trash-full-show.png") ;

    bTrash = Gtk::manage( new Gtk::ToggleButton () );
    bTrash->set_image (*iTrashShowEmpty);
    bTrash->set_relief (Gtk::RELIEF_NONE);
    bTrash->set_tooltip_markup (M("FILEBROWSER_SHOWTRASHHINT"));
    bCateg[17] = bTrash->signal_toggled().connect (sigc::bind(sigc::mem_fun(*this, &FileCatalog::categoryButtonToggled), bTrash, true));
    bTrash->signal_button_press_event().connect (sigc::mem_fun(*this, &FileCatalog::capture_event), false);

    iNotTrash = new RTImage("trash-hide-deleted.png") ;
    iOriginal = new RTImage("filter-original.png");

    bNotTrash = Gtk::manage( new Gtk::ToggleButton () );
    bNotTrash->set_image (*iNotTrash);
    bNotTrash->set_relief (Gtk::RELIEF_NONE);
    bNotTrash->set_tooltip_markup (M("FILEBROWSER_SHOWNOTTRASHHINT"));
    bCateg[18] = bNotTrash->signal_toggled().connect (sigc::bind(sigc::mem_fun(*this, &FileCatalog::categoryButtonToggled), bNotTrash, true));
    bNotTrash->signal_button_press_event().connect (sigc::mem_fun(*this, &FileCatalog::capture_event), false);

    bOriginal = Gtk::manage( new Gtk::ToggleButton () );
    bOriginal->set_image (*iOriginal);
    bOriginal->set_tooltip_markup (M("FILEBROWSER_SHOWORIGINALHINT"));
    bOriginal->set_relief (Gtk::RELIEF_NONE);
    bCateg[19] = bOriginal->signal_toggled().connect (sigc::bind(sigc::mem_fun(*this, &FileCatalog::categoryButtonToggled), bOriginal, true));
    bOriginal->signal_button_press_event().connect (sigc::mem_fun(*this, &FileCatalog::capture_event), false);

    buttonBar->pack_start (*bTrash, Gtk::PACK_SHRINK);
    buttonBar->pack_start (*bNotTrash, Gtk::PACK_SHRINK);
    buttonBar->pack_start (*bOriginal, Gtk::PACK_SHRINK);
    buttonBar->pack_start (*Gtk::manage(new Gtk::VSeparator), Gtk::PACK_SHRINK);
    fileBrowser->trash_changed().connect( sigc::mem_fun(*this, &FileCatalog::trashChanged) );

    // 0  - bFilterClear
    // 1  - bUnRanked
    // 2  - bRank[0]
    // 3  - bRank[1]
    // 4  - bRank[2]
    // 5  - bRank[3]
    // 6  - bRank[4]
    // 7  - bUnCLabeled
    // 8  - bCLabel[0]
    // 9  - bCLabel[1]
    // 10 - bCLabel[2]
    // 11 - bCLabel[3]
    // 12 - bCLabel[4]
    // 13 - bEdited[0]
    // 14 - bEdited[1]
    // 15 - bRecentlySaved[0]
    // 16 - bRecentlySaved[1]
    // 17 - bTrash
    // 18 - bNotTrash
    // 19 - bOriginal

    categoryButtons[0] = bFilterClear;
    categoryButtons[1] = bUnRanked;

    for (int i = 0; i < 5; i++) {
        categoryButtons[i + 2] = bRank[i];
    }

    categoryButtons[7] = bUnCLabeled;

    for (int i = 0; i < 5; i++) {
        categoryButtons[i + 8] = bCLabel[i];
    }

    for (int i = 0; i < 2; i++) {
        categoryButtons[i + 13] = bEdited[i];
    }

    for (int i = 0; i < 2; i++) {
        categoryButtons[i + 15] = bRecentlySaved[i];
    }

    categoryButtons[17] = bTrash;
    categoryButtons[18] = bNotTrash;
    categoryButtons[19] = bOriginal;

    exifInfo = Gtk::manage(new Gtk::ToggleButton ());
    exifInfo->set_image (*Gtk::manage(new RTImage ("info.png")));
    exifInfo->set_relief (Gtk::RELIEF_NONE);
    exifInfo->set_tooltip_markup (M("FILEBROWSER_SHOWEXIFINFO"));
    exifInfo->set_active( options.showFileNames );
    exifInfo->signal_toggled().connect(sigc::mem_fun(*this, &FileCatalog::exifInfoButtonToggled));
    buttonBar->pack_start (*exifInfo, Gtk::PACK_SHRINK);

    // thumbnail zoom
    Gtk::HBox* zoomBox = Gtk::manage( new Gtk::HBox () );
    zoomInButton  = Gtk::manage(  new Gtk::Button () );
    zoomInButton->set_image (*Gtk::manage(new RTImage ("magnifier-plus.png")));
    zoomInButton->signal_pressed().connect (sigc::mem_fun(*this, &FileCatalog::zoomIn));
    zoomInButton->set_relief (Gtk::RELIEF_NONE);
    zoomInButton->set_tooltip_markup (M("FILEBROWSER_ZOOMINHINT"));
    zoomBox->pack_end (*zoomInButton, Gtk::PACK_SHRINK);
    zoomOutButton  = Gtk::manage( new Gtk::Button () );
    zoomOutButton->set_image (*Gtk::manage(new RTImage ("magnifier-minus.png")));
    zoomOutButton->signal_pressed().connect (sigc::mem_fun(*this, &FileCatalog::zoomOut));
    zoomOutButton->set_relief (Gtk::RELIEF_NONE);
    zoomOutButton->set_tooltip_markup (M("FILEBROWSER_ZOOMOUTHINT"));
    zoomBox->pack_end (*zoomOutButton, Gtk::PACK_SHRINK);

    buttonBar->pack_start (*zoomBox, Gtk::PACK_SHRINK);
    buttonBar->pack_start (*Gtk::manage(new Gtk::VSeparator), Gtk::PACK_SHRINK);

    {
        thumbOrder = Gtk::manage(new Gtk::MenuButton());
        thumbOrder->set_image(*Gtk::manage(new RTImage("az-sort.png")));
        Gtk::Menu *menu = Gtk::manage(new Gtk::Menu());
        thumbOrder->set_menu(*menu);

        const auto on_activate =
            [&]() -> void
            {
                for (size_t i = 0; i < thumbOrderItems.size(); ++i) {
                    auto l = static_cast<Gtk::Label *>(thumbOrderItems[i]->get_children()[0]);
                    l->set_markup(thumbOrderLabels[i]);
                }
                int sel = thumbOrder->get_menu()->property_active();
                auto mi = thumbOrder->get_menu()->get_active();
                if (mi) {
                    thumbOrder->set_tooltip_text(M("FILEBROWSER_SORT_LABEL") + ": " + thumbOrderLabels[sel]);
                    auto l = static_cast<Gtk::Label *>(mi->get_children()[0]);
                    l->set_markup("<b>" + thumbOrderLabels[sel] + "</b>");
                }
                options.thumbnailOrder = index_to_thumb_order(sel);
                fileBrowser->sortThumbnails();
            };

        const auto addItem =
            [&](const Glib::ustring &lbl) -> void
            {
                Gtk::MenuItem *mi = Gtk::manage(new Gtk::MenuItem(lbl));
                menu->append(*mi);
                thumbOrderItems.push_back(mi);
                thumbOrderLabels.push_back(lbl);
                mi->signal_activate().connect(sigc::slot<void>(on_activate));
            };
        addItem(M("FILEBROWSER_SORT_FILENAME"));
        addItem(M("FILEBROWSER_SORT_FILENAME_REV"));
        addItem(M("FILEBROWSER_SORT_DATE"));
        addItem(M("FILEBROWSER_SORT_DATE_REV"));
        addItem(M("FILEBROWSER_SORT_MODTIME"));
        addItem(M("FILEBROWSER_SORT_MODTIME_REV"));
        addItem(M("FILEBROWSER_SORT_PROCTIME"));
        addItem(M("FILEBROWSER_SORT_PROCTIME_REV"));
        menu->show_all_children();
        menu->set_active(thumb_order_to_index(options.thumbnailOrder));
        on_activate();
        thumbOrder->set_relief(Gtk::RELIEF_NONE);
        buttonBar->pack_start(*thumbOrder, Gtk::PACK_SHRINK);
    }
        
    
    //iRightArrow = new RTImage("right.png");
    //iRightArrow_red = new RTImage("right_red.png");

    // if it IS a single row toolbar
    if (options.FileBrowserToolbarSingleRow) {
        buttonBar->pack_start (*hbToolBar1, Gtk::PACK_EXPAND_WIDGET, 0);
    }

    tbRightPanel_1 = new Gtk::ToggleButton ();
    iRightPanel_1_Show = new RTImage("panel-to-left.png");
    iRightPanel_1_Hide = new RTImage("panel-to-right.png");

    tbRightPanel_1->set_relief(Gtk::RELIEF_NONE);
    tbRightPanel_1->set_active (true);
    tbRightPanel_1->set_tooltip_markup (M("MAIN_TOOLTIP_SHOWHIDERP1"));
    tbRightPanel_1->set_image (*iRightPanel_1_Hide);
    tbRightPanel_1->signal_toggled().connect( sigc::mem_fun(*this, &FileCatalog::tbRightPanel_1_toggled) );
    buttonBar->pack_end (*tbRightPanel_1, Gtk::PACK_SHRINK);

    selection_counter_ = Gtk::manage(new Gtk::Label(""));
    buttonBar->pack_end(*selection_counter_, Gtk::PACK_SHRINK, 4 * RTScalable::getScale());
    setExpandAlignProperties(selection_counter_, false, false, Gtk::ALIGN_END, Gtk::ALIGN_END);

    // add default panel
    hBox = Gtk::manage( new Gtk::HBox () );
    hBox->show ();
    hBox->pack_end (*fileBrowser);
    hBox->set_name ("FilmstripPanel");
    fileBrowser->applyFilter (getFilter()); // warning: can call this only after all objects used in getFilter (e.g. Query) are instantiated
    //printf("FileCatalog::FileCatalog  fileBrowser->applyFilter (getFilter())\n");
    pack_start (*hBox);

    enabled = true;

    lastScrollPos = 0;

    for (int i = 0; i < 18; i++) {
        hScrollPos[i] = 0;
        vScrollPos[i] = 0;
    }

    selectedDirectory = "";

    {
        MyMutex::MyLock lock(dirEFSMutex);
        if (options.remember_exif_filter_settings) {
            dirEFS = options.last_exif_filter_settings;
        }
    }
}


FileCatalog::~FileCatalog()
{
    idle_register.destroy();

    for (int i = 0; i < 5; i++) {
        delete iranked[i];
        delete igranked[i];
        delete iCLabeled[i];
        delete igCLabeled[i];
    }

    for (int i = 0; i < 2; i++) {
        delete iEdited[i];
        delete igEdited[i];
        delete iRecentlySaved[i];
        delete igRecentlySaved[i];
    }

    delete iFilterClear;
    delete igFilterClear;
    delete iUnRanked;
    delete igUnRanked;
    delete iUnCLabeled;
    delete igUnCLabeled;
    delete iTrashShowEmpty;
    delete iTrashShowFull;
    delete iNotTrash;
    delete iOriginal;
    delete iRefreshWhite;
    delete iRefreshRed;
    delete iQueryClear;
    delete iLeftPanel_1_Show;
    delete iLeftPanel_1_Hide;
    delete iRightPanel_1_Show;
    delete iRightPanel_1_Hide;
}

bool FileCatalog::capture_event(GdkEventButton* event)
{
    // need to record modifiers on the button press, because signal_toggled does not pass the event.
    modifierKey = event->state;
    return false;
}

void FileCatalog::exifInfoButtonToggled()
{
    if (inTabMode) {
        options.filmStripShowFileNames =  exifInfo->get_active();
    } else {
        options.showFileNames =  exifInfo->get_active();
    }

    fileBrowser->refreshThumbImages ();
    refreshHeight();
}

void FileCatalog::on_realize()
{

    Gtk::VBox::on_realize();
    Pango::FontDescription fontd = get_pango_context()->get_font_description ();
    fileBrowser->get_pango_context()->set_font_description (fontd);
//    batchQueue->get_pango_context()->set_font_description (fontd);
}

void FileCatalog::closeDir ()
{

    // if (filterPanel) {
    //     filterPanel->set_sensitive (false);
    // }

    if (dir_refresh_conn_.connected()) {
        dir_refresh_conn_.disconnect();
    }
    if (dirMonitor) {
        dirMonitor->cancel();
    }

    // ignore old requests
    ++selectedDirectoryId;
    refresh_counter_ = 1;

    // terminate thumbnail preview loading
    previewLoader->removeAllJobs ();

    // terminate thumbnail updater
    thumbImageUpdater->removeAllJobs ();

    // remove entries
    selectedDirectory = "";
    fileBrowser->close ();
    fileNameList.clear ();
    file_name_set_.clear();

    {
        MyMutex::MyLock lock(dirEFSMutex);
        dirEFS.clear ();
        if (hasValidCurrentEFS && options.remember_exif_filter_settings && filterPanel) {
            dirEFS = options.last_exif_filter_settings = filterPanel->getFilter(false);
        }
    }
    hasValidCurrentEFS = false;
    redrawAll ();
}


namespace {

class FileSorter {
public:
    FileSorter(Options::ThumbnailOrder order): order_(order) {}

    bool operator()(const Glib::ustring &a, const Glib::ustring &b) const
    {
        switch (order_) {
        case Options::ThumbnailOrder::DATE:
        case Options::ThumbnailOrder::DATE_REV:
            return lt_date(a, b, order_ == Options::ThumbnailOrder::DATE_REV);
            break;
        case Options::ThumbnailOrder::MODTIME:
        case Options::ThumbnailOrder::MODTIME_REV:
            return lt_modtime(a, b, order_ == Options::ThumbnailOrder::MODTIME_REV);
            break;
        case Options::ThumbnailOrder::PROCTIME:
        case Options::ThumbnailOrder::PROCTIME_REV:
            return lt_proctime(a, b, order_ == Options::ThumbnailOrder::PROCTIME_REV);
            break;
        case Options::ThumbnailOrder::FILENAME_REV:
            return b < a;
            break;
        case Options::ThumbnailOrder::FILENAME:
        default:
            return a < b;
        }
    }

private:
    bool lt_date(const Glib::ustring &a, const Glib::ustring &b, bool reverse) const
    {
        try {
            // rtengine::FramesData ma(a);
            // rtengine::FramesData mb(b);
            // auto ta = ma.getDateTimeAsTS();
            // auto tb = mb.getDateTimeAsTS();
            auto ta = get_date(a);
            auto tb = get_date(b);
            if (ta == tb) {
                return a < b;
            }
            return (ta < tb) == !reverse;
        } catch (std::exception &) {
            return a < b;
        }
    }

    bool lt_modtime(const Glib::ustring &a, const Glib::ustring &b, bool reverse) const
    {
        auto ia = Gio::File::create_for_path(a)->query_info(G_FILE_ATTRIBUTE_TIME_MODIFIED);
        auto ib = Gio::File::create_for_path(b)->query_info(G_FILE_ATTRIBUTE_TIME_MODIFIED);
        auto ta = ia->modification_time();
        auto tb = ib->modification_time();
        if (ta == tb) {
            return a < b;
        }
        return (ta < tb) == !reverse;
    }

    bool lt_proctime(const Glib::ustring &a, const Glib::ustring &b, bool reverse) const
    {
        auto fa = options.getParamFile(a);
        auto fb = options.getParamFile(b);

        bool has_a = Glib::file_test(fa, Glib::FILE_TEST_EXISTS);
        bool has_b = Glib::file_test(fb, Glib::FILE_TEST_EXISTS);

        if (has_a != has_b) {
            return reverse ? has_a : has_b;
        } else if (has_a) {
            auto ia = Gio::File::create_for_path(fa)->query_info(G_FILE_ATTRIBUTE_TIME_MODIFIED);
            auto ib = Gio::File::create_for_path(fb)->query_info(G_FILE_ATTRIBUTE_TIME_MODIFIED);
            auto ta = ia->modification_time();
            auto tb = ib->modification_time();
            if (ta == tb) {
                return a < b;
            }
            return (ta < tb) == !reverse;
        } else {
            return a < b;
        }
    }

    time_t get_date(const Glib::ustring &us) const
    {
        CacheImageData d;
        if (cacheMgr->getImageData(us, d)) {
            if (!d.supported) {
                return time_t(-1);
            } else {
                return d.getDateTimeAsTS();
            }
        } else {
            return time_t(-1);
        }
    }

    Options::ThumbnailOrder order_;
};


} // namespace


std::vector<Glib::ustring> FileCatalog::getFileList(bool recursive)
{

    std::vector<Glib::ustring> names;

    const std::set<std::string> &extensions = options.parsedExtensionsSet;

    try {
        if (art::session::check(selectedDirectory)) {
            names = art::session::list();
        } else {
            const auto dir = Gio::File::create_for_path(selectedDirectory);

            auto enumerator = dir->enumerate_children("standard::name,standard::type,standard::is-hidden");
            Glib::ustring curdir = selectedDirectory;

            std::set<Glib::ustring> seen;
            std::vector<std::pair<Glib::RefPtr<Gio::FileEnumerator>, Glib::ustring>> to_process;

            while (true) {
                try {
                    const auto file = enumerator->next_file();
                    if (!file) {
                        if (to_process.empty()) {
                            break;
                        } else {
                            enumerator = to_process.back().first;
                            curdir = to_process.back().second;
                            to_process.pop_back();
                            continue;
                        }
                    }

                    if (file->get_file_type() == Gio::FILE_TYPE_DIRECTORY) {
                        if (recursive) {
                            auto sub = Glib::build_filename(curdir, file->get_name());
                            if (seen.insert(sub).second) {
                                auto d = Gio::File::create_for_path(sub);
                                auto e = d->enumerate_children("standard::name,standard::type,standard::is-hidden");
                                to_process.push_back(std::make_pair(e, sub));
                            }
                        }
                        continue;
                    }

                    if (!options.fbShowHidden && file->is_hidden()) {
                        continue;
                    }

                    const Glib::ustring fname = file->get_name();
                    const auto lastdot = fname.find_last_of('.');

                    if (lastdot >= fname.length() - 1) {
                        continue;
                    }

                    if (extensions.find(fname.substr(lastdot + 1).lowercase()) == extensions.end()) {
                        continue;
                    }

                    names.push_back(Glib::build_filename(curdir, fname));
                } catch (Glib::Exception& exception) {
                    if (options.rtSettings.verbose) {
                        std::cerr << exception.what() << std::endl;
                    }
                }
            }
        }
    } catch (std::exception &exception) {

        if (options.rtSettings.verbose) {
            std::cerr << "Failed to list directory \"" << selectedDirectory << "\": " << exception.what() << std::endl;
        }

    }

    std::sort(names.begin(), names.end(), FileSorter(options.thumbnailOrder));
    return names;
}

void FileCatalog::dirSelected(const Glib::ustring &dirname, const Glib::ustring &openfile)
{
    try {
        bool is_session = art::session::check(dirname);
        button_session_load_->set_visible(is_session);
        button_session_save_->set_visible(is_session);
        button_session_add_->set_visible(is_session);
        button_session_remove_->set_visible(is_session);
        button_recurse_->set_visible(!is_session);
        
        Glib::RefPtr<Gio::File> dir = is_session ? Gio::File::create_for_path(art::session::filename()) : Gio::File::create_for_path(dirname);

        if (!dir) {
            return;
        }

        if (!is_toggling_recurse_) {
            ConnectionBlocker recblock(recurse_conn_);
            button_recurse_->set_active(false);
        }
        is_toggling_recurse_ = false;

        closeDir();
        previewsToLoad = 0;
        previewsLoaded = 0;

        if (!is_session) {
            selectedDirectory = dir->get_parse_name();
        } else {
            selectedDirectory = dirname;
        }

        BrowsePath->set_text(selectedDirectory);
        buttonBrowsePath->set_image(*iRefreshWhite);

        const bool recursive = !is_session && button_recurse_->get_active();

        fileNameList = getFileList(recursive);
        
        // if openfile exists, we have to open it first (it is a command line argument)
        if (!openfile.empty()) {
            addAndOpenFile(openfile, true);
        }
        
        for (unsigned int i = 0; i < fileNameList.size(); i++) {
            file_name_set_.insert(fileNameList[i]);
            if (openfile.empty() || fileNameList[i] != openfile) { // if we opened a file at the beginning don't add it again
                addFile(fileNameList[i]);
            }
        }

        _refreshProgressBar ();

        if (previewsToLoad == 0) {
            filepanel->loadingThumbs(M("PROGRESSBAR_NOIMAGES"), 0);
        } else {
            filepanel->loadingThumbs(M("PROGRESSBAR_LOADINGTHUMBS"), 0);
        }

        dirMonitor = is_session ? dir->monitor_file() : dir->monitor_directory();
        dirMonitor->signal_changed().connect(sigc::mem_fun(*this, &FileCatalog::on_dir_changed));

    } catch (Glib::Exception& ex) {
        std::cout << ex.what();
    }
}

void FileCatalog::enableTabMode(bool enable)
{
    inTabMode = enable;

    if (enable) {
        if (options.showFilmStripToolBar) {
            showToolBar();
        } else {
            hideToolBar();
        }

        exifInfo->set_active( options.filmStripShowFileNames );

    } else {
        buttonBar->show();
        hbToolBar1->show();
        if (hbToolBar1STB) {
            hbToolBar1STB->show();
        }
        exifInfo->set_active( options.showFileNames );
    }

    fileBrowser->enableTabMode(inTabMode);

    redrawAll();
}

void FileCatalog::_refreshProgressBar ()
{
    // In tab mode, no progress bar at all
    // Also mention that this progress bar only measures the FIRST pass (quick thumbnails)
    // The second, usually longer pass is done multithreaded down in the single entries and is NOT measured by this
    if (!inTabMode && (!previewsToLoad || std::floor(100.f * previewsLoaded / previewsToLoad) != std::floor(100.f * (previewsLoaded - 1) / previewsToLoad))) {
        idle_register.add(
            [this]() -> bool
            {
                //GThreadLock lock;
                
                int tot = previewsToLoad ? previewsToLoad : previewsLoaded;
                int filteredCount = fileBrowser->getNumFiltered() < 0 ? tot : std::min(fileBrowser->getNumFiltered(), tot);

                Glib::ustring text = M("MAIN_FRAME_FILEBROWSER") +
                    (filteredCount != tot ? " [" + Glib::ustring::format(filteredCount) + "/" : " (")
                    + Glib::ustring::format(tot) +
                    (filteredCount != tot ? "]" : ")");

                auto label = filepanel->getParent()->getFileBrowserTabLabel();
                if (label) {
                    label->set_text(text);
                }
                
                if (previewsToLoad) {
                    filepanel->loadingThumbs(M("PROGRESSBAR_LOADINGTHUMBS"), float(previewsLoaded) / float(previewsToLoad));
                }
                return false;
            });
    }
}

void FileCatalog::previewReady (int dir_id, FileBrowserEntry* fdn)
{
    GThreadLock lock;
    
    if ( dir_id != selectedDirectoryId ) {
        delete fdn;
        return;
    }

    // put it into the "full directory" browser
    fileBrowser->addEntry (fdn);
    if (!options.thumb_delay_update) {
        if (++refresh_counter_ % 20 == 0) {
            fileBrowser->enableThumbRefresh();
        }
    }

    // update exif filter settings (minimal & maximal values of exif tags, cameras, lenses, etc...)
    const CacheImageData* cfs = fdn->thumbnail->getCacheImageData();

    {
        MyMutex::MyLock lock(dirEFSMutex);

        if (cfs->exifValid) {
            if (cfs->fnumber < dirEFS.fnumberFrom) {
                dirEFS.fnumberFrom = cfs->fnumber;
            }

            if (cfs->fnumber > dirEFS.fnumberTo) {
                dirEFS.fnumberTo = cfs->fnumber;
            }

            if (cfs->shutter < dirEFS.shutterFrom) {
                dirEFS.shutterFrom = cfs->shutter;
            }

            if (cfs->shutter > dirEFS.shutterTo) {
                dirEFS.shutterTo = cfs->shutter;
            }

            if (cfs->iso > 0 && cfs->iso < dirEFS.isoFrom) {
                dirEFS.isoFrom = cfs->iso;
            }

            if (cfs->iso > 0 && cfs->iso > dirEFS.isoTo) {
                dirEFS.isoTo = cfs->iso;
            }

            if (cfs->focalLen < dirEFS.focalFrom) {
                dirEFS.focalFrom = cfs->focalLen;
            }

            if (cfs->focalLen > dirEFS.focalTo) {
                dirEFS.focalTo = cfs->focalLen;
            }

            //TODO: ass filters for HDR and PixelShift files
        }

        if (g_date_valid_dmy(int(cfs->day), GDateMonth(cfs->month), cfs->year)) {
            Glib::Date d(cfs->day, Glib::Date::Month(cfs->month), cfs->year);
            if (d < dirEFS.dateFrom) {
                dirEFS.dateFrom = d;
            }
            if (d > dirEFS.dateTo) {
                dirEFS.dateTo = d;
            }
        }

        dirEFS.filetypes.insert (cfs->filetype);
        dirEFS.cameras.insert (cfs->getCamera());
        dirEFS.lenses.insert (cfs->lens);
        dirEFS.orientations.insert(cfs->getOrientationFilter());
        dirEFS.expcomp.insert (cfs->expcomp);

        filter_panel_update_ = true;
        if (filterPanel) {
            idle_register.add(
                [this]() -> bool
                {
                    //GThreadLock lock;
                    if (filter_panel_update_) {
                        filter_panel_update_ = false;
                        filterPanel->setFilter(dirEFS, false);
                        if (options.remember_exif_filter_settings) {
                            filterPanel->setFilter(options.last_exif_filter_settings, true);
                        }
                    }
                    return false;
                });
        }
    }

    previewsLoaded++;

    _refreshProgressBar();
}

// Called within GTK UI thread
void FileCatalog::previewsFinishedUI ()
{

    {
        GThreadLock lock; // All GUI access from idle_add callbacks or separate thread HAVE to be protected
        fileBrowser->enableThumbRefresh();
        //redrawAll ();
        previewsToLoad = 0;

        if (filterPanel) {
//            filterPanel->set_sensitive (true);

            if ( !hasValidCurrentEFS ) {
                MyMutex::MyLock lock(dirEFSMutex);
                currentEFS = dirEFS;
                filterPanel->setFilter(dirEFS, false);
                if (options.remember_exif_filter_settings) {
                    hasValidCurrentEFS = true;
                    currentEFS = options.last_exif_filter_settings;
                    filterPanel->setFilter(currentEFS, true);
                }
            } else {
                filterPanel->setFilter(currentEFS, true);
            }
        }

        // restart anything that might have been loaded low quality
        fileBrowser->refreshQuickThumbImages();
        fileBrowser->applyFilter(getFilter());  // refresh total image count
        fileBrowser->getFocus();
        _refreshProgressBar();
    }
    filepanel->loadingThumbs(M("PROGRESSBAR_READY"), 0);

    if (!imageToSelect_fname.empty()) {
        fileBrowser->selectImage(imageToSelect_fname);
        imageToSelect_fname = "";
    }

    if (!refImageForOpen_fname.empty() && actionNextPrevious != NAV_NONE) {
        fileBrowser->openNextPreviousEditorImage(refImageForOpen_fname, actionNextPrevious);
        refImageForOpen_fname = "";
        actionNextPrevious = NAV_NONE;
    }

    // newly added item might have been already trashed in a previous session
    trashChanged();
}

void FileCatalog::previewsFinished (int dir_id)
{

    if ( dir_id != selectedDirectoryId ) {
        return;
    }

    if (!hasValidCurrentEFS) {
        MyMutex::MyLock lock(dirEFSMutex);
        currentEFS = dirEFS;
    }

    idle_register.add(
        [this]() -> bool
        {
            previewsFinishedUI();
            return false;
        }
    );
}

void FileCatalog::setEnabled (bool e)
{
    enabled = e;
}

void FileCatalog::redrawAll ()
{
    fileBrowser->queue_draw ();
}

void FileCatalog::refreshThumbImages ()
{
    fileBrowser->refreshThumbImages ();
}

void FileCatalog::refreshHeight ()
{
    int newHeight = fileBrowser->getEffectiveHeight();

    if (newHeight < 5) {  // This may occur if there's no thumbnail.
        int w, h;
        get_size_request(w, h);
        newHeight = h;
    }

    if (hbToolBar1STB && hbToolBar1STB->is_visible()) {
        newHeight += hbToolBar1STB->get_height();
    }

    if (buttonBar->is_visible()) {
        newHeight += buttonBar->get_height();
    }

    set_size_request(0, newHeight + 2); // HOMBRE: yeah, +2, there's always 2 pixels missing... sorry for this dirty hack O:)
}

void FileCatalog::_openImage() //const std::vector<Thumbnail*>& tmb)
{
    if (enabled && listener) {
        auto &tmb = to_open_;
        bool continueToLoad = true;

        size_t j = 0;
        for (size_t i = 0; i < tmb.size() && continueToLoad; i++) {
            // Open the image here, and stop if in Single Editor mode, or if an image couldn't
            // be opened, would it be because the file doesn't exist or because of lack of RAM
            auto res = listener->fileSelected(tmb[i]);
            if (res == FileSelectionListener::Result::BUSY) {
                tmb[j++] = tmb[i];
                continue;
            }
            
            if (res == FileSelectionListener::Result::FAIL && !options.tabbedUI) {
                continueToLoad = false;
            }

            tmb[i]->decreaseRef ();
        }

        tmb.resize(j);
    }
}

void FileCatalog::filterApplied()
{
    idle_register.add(
        [this]() -> bool
        {
            _refreshProgressBar();
            return false;
        }
    );
}

void FileCatalog::openRequested(const std::vector<Thumbnail*>& tmb)
{
    to_open_ = tmb;
    for (const auto thumb : tmb) {
        thumb->increaseRef();
    }

    idle_register.add(
        [this]() -> bool
        {
            _openImage();
            return !to_open_.empty();
        }
    );
}


// void FileCatalog::copyMoveRequested(const std::vector<FileBrowserEntry*>& tbe, bool moveRequested)
// {
//     if (tbe.empty()) {
//         return;
//     }

//     Glib::ustring fc_title;

//     if (moveRequested) {
//         fc_title = M("FILEBROWSER_POPUPMOVETO");
//     } else {
//         fc_title = M("FILEBROWSER_POPUPCOPYTO");
//     }

//     Gtk::FileChooserDialog fc (getToplevelWindow (this), fc_title, Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER );
//     fc.add_button( M("GENERAL_CANCEL"), Gtk::RESPONSE_CANCEL);
//     fc.add_button( M("GENERAL_OK"), Gtk::RESPONSE_OK);
//     if (!options.lastCopyMovePath.empty() && Glib::file_test(options.lastCopyMovePath, Glib::FILE_TEST_IS_DIR)) {
//         fc.set_current_folder(options.lastCopyMovePath);
//     } else {
//         // open dialog at the 1-st file's path
//         fc.set_current_folder(Glib::path_get_dirname(tbe[0]->filename));
//     }

//     if (fc.run() == Gtk::RESPONSE_OK) {
//         const bool is_session = art::session::check(selectedDirectory);
//         std::vector<Glib::ustring> session_add, session_rem;
        
//         if (moveRequested) {
//             removeFromBatchQueue(tbe);
//         }
        
//         options.lastCopyMovePath = fc.get_current_folder();

//         // iterate through selected files
//         for (unsigned int i = 0; i < tbe.size(); i++) {
//             Glib::ustring src_fPath = tbe[i]->filename;
//             Glib::ustring src_Dir = Glib::path_get_dirname(src_fPath);
//             Glib::RefPtr<Gio::File> src_file = Gio::File::create_for_path ( src_fPath );

//             if (!src_file) {
//                 continue;    // if file is missing - skip it
//             }

//             Glib::ustring fname = src_file->get_basename();
//             Glib::ustring fname_noExt = removeExtension(fname);
//             Glib::ustring fname_Ext = getExtension(fname);

//             // construct  destination File Paths
//             Glib::ustring dest_fPath = Glib::build_filename (options.lastCopyMovePath, fname);
//             Glib::ustring dest_fPath_param = options.getParamFile(dest_fPath);

//             if (moveRequested && (src_Dir == options.lastCopyMovePath)) {
//                 continue;
//             }

//             /* comparison of src_Dir and dest_Dir is done per image for compatibility with
//             possible future use of Collections as source where each file's source path may be different.*/

//             bool filecopymovecomplete = false;
//             int i_copyindex = 1;

//             while (!filecopymovecomplete) {
//                 // check for filename conflicts at destination - prevent overwriting (actually RT will crash on overwriting attempt)
//                 if (!Glib::file_test(dest_fPath, Glib::FILE_TEST_EXISTS) && !Glib::file_test(dest_fPath_param, Glib::FILE_TEST_EXISTS) && !Glib::file_test(Thumbnail::getXmpSidecarPath(dest_fPath), Glib::FILE_TEST_EXISTS)) {
//                     // copy/move file to destination
//                     Glib::RefPtr<Gio::File> dest_file = Gio::File::create_for_path ( dest_fPath );

//                     if (moveRequested) {
//                         if (is_session) {
//                             session_rem.push_back(src_fPath);
//                             session_add.push_back(dest_fPath);
//                         }
                        
//                         // move file
//                         src_file->move(dest_file);
//                         // re-attach cache files
//                         cacheMgr->renameEntry (src_fPath, tbe[i]->thumbnail->getMD5(), dest_fPath);
//                         // remove from browser
//                         fileBrowser->delEntry (src_fPath);

//                         previewsLoaded--;
//                     } else {
//                         src_file->copy(dest_file);
//                     }


//                     // attempt to copy/move paramFile only if it exist next to the src
//                     Glib::RefPtr<Gio::File> scr_param = Gio::File::create_for_path (options.getParamFile(src_fPath));

//                     if (Glib::file_test(options.getParamFile(src_fPath), Glib::FILE_TEST_EXISTS)) {
//                         Glib::RefPtr<Gio::File> dest_param = Gio::File::create_for_path ( dest_fPath_param);

//                         // copy/move paramFile to destination
//                         if (moveRequested) {
//                             if (Glib::file_test(options.getParamFile(dest_fPath), Glib::FILE_TEST_EXISTS)) {
//                                 // profile already got copied to destination from cache after cacheMgr->renameEntry
//                                 // delete source profile as cleanup
//                                 ::g_remove(options.getParamFile(src_fPath).c_str());
//                             } else {
//                                 scr_param->move(dest_param);
//                             }
//                         } else {
//                             scr_param->copy(dest_param);
//                         }
//                     }

//                     auto xmp_sidecar = Thumbnail::getXmpSidecarPath(src_fPath);
//                     if (Glib::file_test(xmp_sidecar, Glib::FILE_TEST_EXISTS)) {
//                         auto s = Gio::File::create_for_path(xmp_sidecar);
//                         auto dst_xmp_sidecar = Thumbnail::getXmpSidecarPath(dest_fPath);
//                         auto dst = Gio::File::create_for_path(dst_xmp_sidecar);
//                         if (moveRequested) {
//                             if (Glib::file_test(dst_xmp_sidecar, Glib::FILE_TEST_EXISTS)) {
//                                 ::g_remove(xmp_sidecar.c_str());
//                             } else {
//                                 s->move(dst);
//                             }
//                         } else {
//                             s->copy(dst);
//                         }
//                     }

//                     filecopymovecomplete = true;
//                 } else {
//                     // adjust destination fname to avoid conflicts (append "_<index>", preserve extension)
//                     Glib::ustring dest_fname = Glib::ustring::compose("%1%2%3%4%5", fname_noExt, "_", i_copyindex, ".", fname_Ext);
//                     // re-construct  destination File Paths
//                     dest_fPath = Glib::build_filename (options.lastCopyMovePath, dest_fname);
//                     dest_fPath_param = options.getParamFile(dest_fPath);
//                     i_copyindex++;
//                 }
//             }
//         }

//         if (is_session) {
//             art::session::remove(session_rem);
//             art::session::add(session_add);
//         } else {
//             redrawAll();
//         }

//         _refreshProgressBar();
//     }
// }


void FileCatalog::developRequested(const std::vector<FileBrowserEntry*>& tbe, bool fastmode)
{
    if (listener) {
        std::vector<BatchQueueEntry*> entries;

        for (size_t i = 0; i < tbe.size(); i++) {
            FileBrowserEntry* fbe = tbe[i];
            Thumbnail* th = fbe->thumbnail;
            if (!th->hasProcParams()) {
                th->createProcParamsForUpdate(false, false, true);
            }
            rtengine::procparams::ProcParams params = th->getProcParams();

            auto pjob = create_processing_job(fbe->filename, th->getType() == FT_Raw, params, fastmode);

            int pw;
            int ph = BatchQueue::calcMaxThumbnailHeight();
            th->getThumbnailSize (pw, ph);

            auto bqh = new BatchQueueEntry(pjob, params, fbe->filename, pw, ph, th);
            entries.push_back(bqh);
        }

        listener->addBatchQueueJobs(entries);
    }
}

void FileCatalog::selectionChanged(const std::vector<Thumbnail*>& tbe)
{
    if (fslistener) {
        fslistener->selectionChanged (tbe);
    }
    if (tbe.size() <= 1) {
        selection_counter_->set_text("");
    } else {
        selection_counter_->set_text(Glib::ustring::compose(M("FILEBROWSER_SELECTION_COUNTER"), tbe.size()));
    }
}

void FileCatalog::clearFromCacheRequested(const std::vector<FileBrowserEntry*>& tbe, bool leavenotrace)
{
    if (tbe.empty()) {
        return;
    }

    for (unsigned int i = 0; i < tbe.size(); i++) {
        Glib::ustring fname = tbe[i]->filename;
        // remove from cache
        cacheMgr->clearFromCache (fname, leavenotrace);
    }
}

bool FileCatalog::isInTabMode() const
{
    return inTabMode;
}

void FileCatalog::categoryButtonToggled (Gtk::ToggleButton* b, bool isMouseClick)
{

    //was control key pressed
    bool control_down = modifierKey & GDK_CONTROL_MASK;

    //was shift key pressed
    bool shift_down   = modifierKey & GDK_SHIFT_MASK;

    // The event is process here, we can clear modifierKey now, it'll be set again on the next even
    modifierKey = 0;

    const int numCateg = sizeof(bCateg) / sizeof(bCateg[0]);
    const int numButtons = sizeof(categoryButtons) / sizeof(categoryButtons[0]);

    for (int i = 0; i < numCateg; i++) {
        bCateg[i].block (true);
    }

    // button already toggled when entering this function from a mouse click, so
    // we switch it back to its initial state.
    if (isMouseClick) {
        b->set_active(!b->get_active());
    }

    //if both control and shift keys were pressed, do nothing
    if (!(control_down && shift_down)) {

        fileBrowser->getScrollPosition (hScrollPos[lastScrollPos], vScrollPos[lastScrollPos]);

        //we look how many stars are already toggled on, if any
        int toggled_stars_count = 0, buttons = 0, start_star = 0, toggled_button = 0;

        for (int i = 0; i < numButtons; i++) {
            if (categoryButtons[i]->get_active()) {
                if (i > 0 && i < 17) {
                    toggled_stars_count ++;
                    start_star = i;
                }

                buttons |= (1 << i);
            }

            if (categoryButtons[i] == b) {
                toggled_button = i;
            }
        }

        // if no modifier key is pressed,
        if (!(control_down || shift_down)) {
            // if we're deselecting non-trashed or original
            if (toggled_button >= 18 && toggled_button <= 19 && (buttons & (1 << toggled_button))) {
                categoryButtons[0]->set_active (true);

                for (int i = 1; i < numButtons; i++) {
                    categoryButtons[i]->set_active (false);
                }
            }
            // if we're deselecting the only star still active
            else if (toggled_stars_count == 1 && (buttons & (1 << toggled_button))) {
                // activate clear-filters
                categoryButtons[0]->set_active (true);
                // deactivate the toggled filter
                categoryButtons[toggled_button]->set_active (false);
            }
            // if we're deselecting trash
            else if (toggled_button == 17 && (buttons & (1 << toggled_button))) {
                categoryButtons[0]->set_active (true);
                categoryButtons[17]->set_active (false);
            } else {
                // activate the toggled filter, deactivate the rest
                for (int i = 0; i < numButtons; i++) {
                    categoryButtons[i]->set_active (i == toggled_button);
                }
            }
        }
        //modifier key allowed only for stars and color labels...
        else if (toggled_button > 0 && toggled_button < 17) {
            if (control_down) {
                //control is pressed
                if (toggled_stars_count == 1 && (buttons & (1 << toggled_button))) {
                    //we're deselecting the only star still active, so we activate clear-filters
                    categoryButtons[0]->set_active(true);
                    //and we deselect the toggled star
                    categoryButtons[toggled_button]->set_active (false);
                } else if (toggled_stars_count >= 1) {
                    //we toggle the state of a star (eventually another one than the only one selected)
                    categoryButtons[toggled_button]->set_active(!categoryButtons[toggled_button]->get_active());
                } else {
                    //no star selected
                    //we deselect the 2 non star filters
                    if (buttons &  1    ) {
                        categoryButtons[0]->set_active(false);
                    }

                    if (buttons & (1 << 17)) {
                        categoryButtons[17]->set_active(false);
                    }

                    //and we toggle on the star
                    categoryButtons[toggled_button]->set_active (true);
                }
            } else {
                //shift is pressed, only allowed if 0 or 1 star & labels is selected
                if (!toggled_stars_count) {
                    //we deselect the 2 non star filters
                    if (buttons &  1      ) {
                        categoryButtons[0]->set_active(false);
                    }

                    if (buttons & (1 << 7)) {
                        categoryButtons[7]->set_active(false);
                    }

                    if (buttons & (1 << 13)) {
                        categoryButtons[13]->set_active(false);
                    }

                    if (buttons & (1 << 17)) {
                        categoryButtons[17]->set_active(false);
                    }

                    //and we set the start star to 1 (unrated images)
                    start_star = 1;
                    //we act as if one star were selected
                    toggled_stars_count = 1;
                }

                if (toggled_stars_count == 1) {
                    int current_star = std::min(start_star, toggled_button);
                    int last_star   = std::max(start_star, toggled_button);

                    //we permute the start and the end star for the next loop
                    for (; current_star <= last_star; current_star++) {
                        //we toggle on all the star in the range
                        if (!(buttons & (1 << current_star))) {
                            categoryButtons[current_star]->set_active(true);
                        }
                    }
                }

                //if more than one star & color label is selected, do nothing
            }
        }
        // ...or non-trashed or original with Control modifier
        else if (toggled_button >= 18 && toggled_button <= 19 && control_down) {
            Gtk::ToggleButton* categoryButton = categoryButtons[toggled_button];
            categoryButton->set_active (!categoryButton->get_active ());

            // If it was the first or last one, we reset the clear filter.
            if (buttons == 1 || buttons == (1 << toggled_button)) {
                bFilterClear->set_active (!categoryButton->get_active ());
            }
        }

        bool active_now, active_before;

        // FilterClear: set the right images
        // TODO: swapping FilterClear icon needs more work in categoryButtonToggled
        /*active_now = bFilterClear->get_active();
        active_before = buttons & (1 << (0)); // 0
        if      ( active_now && !active_before) bFilterClear->set_image (*iFilterClear);
        else if (!active_now &&  active_before) bFilterClear->set_image (*igFilterClear);*/

        // rank: set the right images
        for (int i = 0; i < 5; i++) {
            active_now = bRank[i]->get_active();
            active_before = buttons & (1 << (i + 2)); // 2,3,4,5,6

            if      ( active_now && !active_before) {
                bRank[i]->set_image (*iranked[i]);
            } else if (!active_now &&  active_before) {
                bRank[i]->set_image (*igranked[i]);
            }
        }

        active_now = bUnRanked->get_active();
        active_before = buttons & (1 << (1)); // 1

        if      ( active_now && !active_before) {
            bUnRanked->set_image (*iUnRanked);
        } else if (!active_now &&  active_before) {
            bUnRanked->set_image (*igUnRanked);
        }

        // color labels: set the right images
        for (int i = 0; i < 5; i++) {
            active_now = bCLabel[i]->get_active();
            active_before = buttons & (1 << (i + 8)); // 8,9,10,11,12

            if      ( active_now && !active_before) {
                bCLabel[i]->set_image (*iCLabeled[i]);
            } else if (!active_now &&  active_before) {
                bCLabel[i]->set_image (*igCLabeled[i]);
            }
        }

        active_now = bUnCLabeled->get_active();
        active_before = buttons & (1 << (7)); // 7

        if      ( active_now && !active_before) {
            bUnCLabeled->set_image (*iUnCLabeled);
        } else if (!active_now &&  active_before) {
            bUnCLabeled->set_image (*igUnCLabeled);
        }

        // Edited: set the right images
        for (int i = 0; i < 2; i++) {
            active_now = bEdited[i]->get_active();
            active_before = buttons & (1 << (i + 13)); //13,14

            if      ( active_now && !active_before) {
                bEdited[i]->set_image (*iEdited[i]);
            } else if (!active_now &&  active_before) {
                bEdited[i]->set_image (*igEdited[i]);
            }
        }

        // RecentlySaved: set the right images
        for (int i = 0; i < 2; i++) {
            active_now = bRecentlySaved[i]->get_active();
            active_before = buttons & (1 << (i + 15)); //15,16

            if      ( active_now && !active_before) {
                bRecentlySaved[i]->set_image (*iRecentlySaved[i]);
            } else if (!active_now &&  active_before) {
                bRecentlySaved[i]->set_image (*igRecentlySaved[i]);
            }
        }

        fileBrowser->applyFilter (getFilter ());
        _refreshProgressBar();

        //rearrange panels according to the selected filter
        removeIfThere (hBox, trashButtonBox);

        if (bTrash->get_active ()) {
            hBox->pack_start (*trashButtonBox, Gtk::PACK_SHRINK, 4);
        }

        hBox->queue_draw ();

        fileBrowser->setScrollPosition (hScrollPos[lastScrollPos], vScrollPos[lastScrollPos]);
    }

    for (int i = 0; i < numCateg; i++) {
        bCateg[i].block (false);
    }
}

BrowserFilter FileCatalog::getFilter ()
{

    BrowserFilter filter;

    bool anyRankFilterActive = bUnRanked->get_active () || bRank[0]->get_active () || bRank[1]->get_active () || bRank[2]->get_active () || bRank[3]->get_active () || bRank[4]->get_active ();
    bool anyCLabelFilterActive = bUnCLabeled->get_active () || bCLabel[0]->get_active () || bCLabel[1]->get_active () || bCLabel[2]->get_active () || bCLabel[3]->get_active () || bCLabel[4]->get_active ();
    bool anyEditedFilterActive = bEdited[0]->get_active() || bEdited[1]->get_active();
    bool anyRecentlySavedFilterActive = bRecentlySaved[0]->get_active() || bRecentlySaved[1]->get_active();
    const bool anySupplementaryActive = bNotTrash->get_active() || bOriginal->get_active();
    /*
     * filter is setup in 2 steps
     * Step 1: handle individual filters
    */
    filter.showRanked[0] = bFilterClear->get_active() || bUnRanked->get_active () || bTrash->get_active () || anySupplementaryActive ||
                           anyCLabelFilterActive || anyEditedFilterActive || anyRecentlySavedFilterActive;

    filter.showCLabeled[0] = bFilterClear->get_active() || bUnCLabeled->get_active () || bTrash->get_active ()  || anySupplementaryActive ||
                             anyRankFilterActive || anyEditedFilterActive || anyRecentlySavedFilterActive;

    for (int i = 1; i <= 5; i++) {
        filter.showRanked[i] = bFilterClear->get_active() || bRank[i - 1]->get_active () || bTrash->get_active () || anySupplementaryActive ||
                               anyCLabelFilterActive || anyEditedFilterActive || anyRecentlySavedFilterActive;

        filter.showCLabeled[i] = bFilterClear->get_active() || bCLabel[i - 1]->get_active () || bTrash->get_active ()  || anySupplementaryActive ||
                                 anyRankFilterActive || anyEditedFilterActive || anyRecentlySavedFilterActive;
    }

    for (int i = 0; i < 2; i++) {
        filter.showEdited[i] = bFilterClear->get_active() || bEdited[i]->get_active () || bTrash->get_active ()  || anySupplementaryActive ||
                               anyRankFilterActive || anyCLabelFilterActive || anyRecentlySavedFilterActive;

        filter.showRecentlySaved[i] = bFilterClear->get_active() || bRecentlySaved[i]->get_active () || bTrash->get_active ()  || anySupplementaryActive ||
                                      anyRankFilterActive || anyCLabelFilterActive || anyEditedFilterActive;
    }

    filter.multiselect = false;

    /*
     * Step 2
     * handle the case when more than 1 filter is selected. This overrides values set in Step
     * if no filters in a group are active, filter.show for each member of that group will be set to true
     * otherwise they are set based on UI input
     */
    if ((anyRankFilterActive && anyCLabelFilterActive ) ||
            (anyRankFilterActive && anyEditedFilterActive ) ||
            (anyRankFilterActive && anyRecentlySavedFilterActive ) ||
            (anyCLabelFilterActive && anyEditedFilterActive ) ||
            (anyCLabelFilterActive && anyRecentlySavedFilterActive ) ||
            (anyEditedFilterActive && anyRecentlySavedFilterActive) ||
            (anySupplementaryActive && (anyRankFilterActive || anyCLabelFilterActive || anyEditedFilterActive || anyRecentlySavedFilterActive))) {

        filter.multiselect = true;
        filter.showRanked[0] = anyRankFilterActive ? bUnRanked->get_active () : true;
        filter.showCLabeled[0] = anyCLabelFilterActive ? bUnCLabeled->get_active () : true;

        for (int i = 1; i <= 5; i++) {
            filter.showRanked[i] = anyRankFilterActive ? bRank[i - 1]->get_active () : true;
            filter.showCLabeled[i] = anyCLabelFilterActive ? bCLabel[i - 1]->get_active () : true;
        }

        for (int i = 0; i < 2; i++) {
            filter.showEdited[i] = anyEditedFilterActive ? bEdited[i]->get_active() : true;
            filter.showRecentlySaved[i] = anyRecentlySavedFilterActive ? bRecentlySaved[i]->get_active() : true;
        }
    }


    filter.showTrash = bTrash->get_active () || !bNotTrash->get_active ();
    filter.showNotTrash = !bTrash->get_active ();
    filter.showOriginal = bOriginal->get_active();

    if (!filterPanel) {
        filter.exifFilterEnabled = false;
    } else {
        if (!hasValidCurrentEFS) {
            MyMutex::MyLock lock(dirEFSMutex);
            filter.exifFilter = dirEFS;
        } else {
            filter.exifFilter = currentEFS;
        }

        filter.exifFilterEnabled = filterPanel->isEnabled ();
    }

    //TODO add support for more query options. e.g by date, iso, f-number, etc
    //TODO could use date:<value>;iso:<value>  etc
    // default will be filename

    /* // this is for safe execution if getFilter is called before Query object is instantiated
    Glib::ustring tempQuery;
    tempQuery="";
    if (Query) tempQuery = Query->get_text();
    */
    filter.queryString = Query->get_text(); // full query string from Query Entry
    filter.queryFileName = Query->get_text(); // for now Query is only by file name

    return filter;
}

void FileCatalog::filterChanged ()
{
    //TODO !!! there is too many repetitive and unnecessary executions of
    // " fileBrowser->applyFilter (getFilter()); " throughout the code
    // this needs further analysis and cleanup
    fileBrowser->applyFilter (getFilter());
    _refreshProgressBar();
}

void FileCatalog::reparseDirectory ()
{

    if (selectedDirectory.empty()) {
        return;
    }

    const bool is_session = art::session::check(selectedDirectory);
    
    if (!is_session && !Glib::file_test(selectedDirectory, Glib::FILE_TEST_IS_DIR)) {
        closeDir();
        return;
    }

    const bool recursive = !is_session && button_recurse_->get_active();

    auto new_file_list = getFileList(recursive);
    std::set<std::string> seen;

    if (is_session) {
        for (auto &n : new_file_list) {
            seen.insert(n.collate_key());
        }
    }

    // check if a thumbnailed file has been deleted
    const std::vector<ThumbBrowserEntryBase*>& t = fileBrowser->getEntries();
    std::vector<Glib::ustring> fileNamesToDel;

    for (const auto &entry : t) {
        if (is_session) {
            if (seen.find(entry->filename.collate_key()) == seen.end()) {
                fileNamesToDel.push_back(entry->filename);
            }
        } else {
            if (!Glib::file_test(entry->filename, Glib::FILE_TEST_EXISTS)) {
                fileNamesToDel.push_back(entry->filename);
            }
        }
    }

    for (const auto& toDelete : fileNamesToDel) {
        delete fileBrowser->delEntry(toDelete);
        cacheMgr->deleteEntry(toDelete);
        --previewsLoaded;
    }

    if (!fileNamesToDel.empty()) {
        _refreshProgressBar();
    }

    // check if a new file has been added
    // build a set of collate-keys for faster search
    seen.clear();
    for (const auto& oldName : fileNameList) {
        seen.insert(oldName.collate_key());
    }

    fileNameList = std::move(new_file_list);
    file_name_set_.clear();
    
    for (const auto& newName : fileNameList) {
        file_name_set_.insert(newName);
        if (seen.find(newName.collate_key()) == seen.end()) {
            addFile(newName);
            _refreshProgressBar();
        }
    }
}


void FileCatalog::on_dir_changed(const Glib::RefPtr<Gio::File>& file, const Glib::RefPtr<Gio::File>& other_file, Gio::FileMonitorEvent event_type)
{
    if (art::session::check(selectedDirectory)) {
        GThreadLock lock;
        reparseDirectory();
    } else if (options.has_retained_extention(file->get_parse_name())
               && (event_type == Gio::FILE_MONITOR_EVENT_CREATED || event_type == Gio::FILE_MONITOR_EVENT_DELETED || event_type == Gio::FILE_MONITOR_EVENT_CHANGED)) {
        const auto doit =
            [this]() -> bool
            {
                GThreadLock lock;
                reparseDirectory();
                return false;
            };
        if (dir_refresh_conn_.connected()) {
            dir_refresh_conn_.disconnect();
        }
        dir_refresh_conn_ = Glib::signal_timeout().connect(sigc::slot<bool>(doit), DIR_REFRESH_DELAY);
    }
}


void FileCatalog::addFile (const Glib::ustring& fName)
{
    if (!fName.empty()) {
        previewLoader->add(selectedDirectoryId, fName, this);
        previewsToLoad++;
    }
}

void FileCatalog::addAndOpenFile(const Glib::ustring& fname, bool force)
{
    auto file = Gio::File::create_for_path(fname);

    if (!file ) {
        return;
    }

    if (!file->query_exists()) {
        return;
    }

    try {

        const auto info = file->query_info();

        if (!info) {
            return;
        }

        bool in_catalog = true;
        const auto lastdot = info->get_name().find_last_of('.');
        if (lastdot != Glib::ustring::npos) {
            if (!options.is_extention_enabled(info->get_name().substr(lastdot + 1))) {
                in_catalog = false;
            }
        } else {
            in_catalog = false;
        }

        if (!in_catalog && !force) {
            return;
        }

        // if supported, load thumbnail first
        const auto tmb = cacheMgr->getEntry(fname);//file->get_parse_name());

        if (!tmb) {
            return;
        }

        if (in_catalog) {
            FileBrowserEntry *entry = new FileBrowserEntry(tmb, file->get_parse_name());
            previewReady(selectedDirectoryId, entry);
            // open the file
        }
        tmb->increaseRef();
        to_open_ = {tmb};
        idle_register.add(
            [this]() -> bool
            {
                _openImage();
                return !to_open_.empty();
            }
        );

    } catch(Gio::Error&) {}
}

void FileCatalog::emptyTrash ()
{

    const auto& t = fileBrowser->getEntries();
    std::vector<FileBrowserEntry*> toDel;

    for (const auto entry : t) {
        if (static_cast<FileBrowserEntry*>(entry)->thumbnail->getInTrash()) {
            toDel.push_back(static_cast<FileBrowserEntry*>(entry));
        }
    }
    if (toDel.size() > 0) {
        deleteRequested(toDel, false);
        trashChanged();
    }
}

bool FileCatalog::trashIsEmpty ()
{

    const auto& t = fileBrowser->getEntries();

    for (const auto entry : t) {
        if ((static_cast<FileBrowserEntry*>(entry))->thumbnail->getInTrash()) {
            return false;
        }
    }
    return true;
}

void FileCatalog::zoomIn ()
{

    fileBrowser->zoomIn ();
    refreshHeight();

}
void FileCatalog::zoomOut ()
{

    fileBrowser->zoomOut ();
    refreshHeight();

}
void FileCatalog::refreshEditedState (const std::set<Glib::ustring>& efiles)
{

    editedFiles = efiles;
    fileBrowser->refreshEditedState (efiles);
}

// void FileCatalog::exportRequested()
// {
// }

// Called within GTK UI thread
void FileCatalog::exifFilterChanged ()
{

    currentEFS = filterPanel->getFilter ();
    hasValidCurrentEFS = true;
    fileBrowser->applyFilter (getFilter ());
    _refreshProgressBar();
}

void FileCatalog::setFilterPanel (FilterPanel* fpanel)
{

    filterPanel = fpanel;
    //filterPanel->set_sensitive (false);
    filterPanel->setFilterPanelListener (this);
}


void FileCatalog::trashChanged ()
{
    if (trashIsEmpty()) {
        bTrash->set_image(*iTrashShowEmpty);
    } else {
        bTrash->set_image(*iTrashShowFull);
    }
}

// Called within GTK UI thread
void FileCatalog::buttonQueryClearPressed ()
{
    Query->set_text("");
    FileCatalog::executeQuery ();
}

// Called within GTK UI thread
void FileCatalog::executeQuery()
{
    // if BrowsePath text was changed, do a full browse;
    // otherwise filter only

    if (BrowsePath->get_text() != selectedDirectory) {
        buttonBrowsePathPressed ();
    } else {
        FileCatalog::filterChanged ();
    }
}

bool FileCatalog::Query_key_pressed (GdkEventKey *event)
{

    bool shift = event->state & GDK_SHIFT_MASK;

    switch (event->keyval) {
    case GDK_KEY_Escape:

        // Clear Query if the Escape character is pressed within it
        if (shift) {
            FileCatalog::buttonQueryClearPressed ();
            return true;
        }

        break;

    default:
        break;
    }

    return false;
}

void FileCatalog::updateFBQueryTB (bool singleRow)
{
    hbToolBar1->reference();

    if (singleRow) {
        if (hbToolBar1STB) {
            hbToolBar1STB->remove_with_viewport();
            removeIfThere(this, hbToolBar1STB, false);
            buttonBar->pack_start(*hbToolBar1, Gtk::PACK_EXPAND_WIDGET, 0);
            hbToolBar1STB = nullptr;
        }
    } else {
        if (!hbToolBar1STB) {
            removeIfThere(buttonBar, hbToolBar1, false);
            hbToolBar1STB = Gtk::manage(new MyScrolledToolbar());
            hbToolBar1STB->set_name("FileBrowserQueryToolbar");
            hbToolBar1STB->add(*hbToolBar1);
            hbToolBar1STB->show();
            pack_start (*hbToolBar1STB, Gtk::PACK_SHRINK, 0);
            reorder_child(*hbToolBar1STB, 0);
        }
    }

    hbToolBar1->unreference();
}

void FileCatalog::updateFBToolBarVisibility (bool showFilmStripToolBar)
{
    if (showFilmStripToolBar) {
        showToolBar();
    } else {
        hideToolBar();
    }

    refreshHeight();
}

void FileCatalog::buttonBrowsePathPressed ()
{
    auto BrowsePathValue = getBrowsePath();
    BrowsePath->set_text(BrowsePathValue);

    // validate the path
    if ((art::session::check(BrowsePathValue) || Glib::file_test(BrowsePathValue, Glib::FILE_TEST_IS_DIR)) && selectDir) {
        selectDir(BrowsePathValue);
    } else { // error, likely path not found: show red arrow
        buttonBrowsePath->set_image (*iRefreshRed);
    }
}


void FileCatalog::browsePathRefresh()
{
    is_toggling_recurse_ = true;
    buttonBrowsePathPressed();
}


bool FileCatalog::BrowsePath_key_pressed (GdkEventKey *event)
{

    bool shift = event->state & GDK_SHIFT_MASK;

    switch (event->keyval) {
    case GDK_KEY_Escape:

        // On Escape character Reset BrowsePath to selectedDirectory
        if (shift) {
            BrowsePath->set_text(selectedDirectory);
            // place cursor at the end
            BrowsePath->select_region(BrowsePath->get_text_length(), BrowsePath->get_text_length());
            return true;
        }

        break;

    default:
        break;
    }

    return false;
}

void FileCatalog::tbLeftPanel_1_visible (bool visible)
{
    if (visible) {
        tbLeftPanel_1->show();
        vSepiLeftPanel->show();
    } else {
        tbLeftPanel_1->hide();
        vSepiLeftPanel->hide();
    }
}
void FileCatalog::tbRightPanel_1_visible (bool visible)
{
    if (visible) {
        tbRightPanel_1->show();
    } else {
        tbRightPanel_1->hide();
    }
}
void FileCatalog::tbLeftPanel_1_toggled ()
{
//    removeIfThere (filepanel->dirpaned, filepanel->placespaned, false);

    bool in_inspector = fileBrowser && fileBrowser->getInspector() && fileBrowser->getInspector()->isActive();

    if (tbLeftPanel_1->get_active()) {
//        filepanel->dirpaned->pack1 (*filepanel->placespaned, false, true);
        filepanel->placespaned->show();
        tbLeftPanel_1->set_image (*iLeftPanel_1_Hide);
        if (in_inspector) {
            options.inspectorDirPanelOpened = true;
        } else {
            options.browserDirPanelOpened = true;
        }
    } else {
        filepanel->placespaned->hide();
        tbLeftPanel_1->set_image (*iLeftPanel_1_Show);
        if (in_inspector) {
            options.inspectorDirPanelOpened = false;
        } else {
            options.browserDirPanelOpened = false;
        }
    }
}

void FileCatalog::tbRightPanel_1_toggled ()
{
    if (tbRightPanel_1->get_active()) {
        filepanel->showRightBox(true);
        tbRightPanel_1->set_image (*iRightPanel_1_Hide);
        options.browserToolPanelOpened = true;
    } else {
        filepanel->showRightBox(false);
        tbRightPanel_1->set_image (*iRightPanel_1_Show);
        options.browserToolPanelOpened = false;
    }
}

bool FileCatalog::CheckSidePanelsVisibility()
{
    return tbLeftPanel_1->get_active() || tbRightPanel_1->get_active();
}

void FileCatalog::toggleSidePanels()
{
    // toggle left AND right panels

    bool bAllSidePanelsVisible;
    bAllSidePanelsVisible = CheckSidePanelsVisibility();

    tbLeftPanel_1->set_active (!bAllSidePanelsVisible);
    tbRightPanel_1->set_active (!bAllSidePanelsVisible);
}

void FileCatalog::toggleLeftPanel()
{
    tbLeftPanel_1->set_active (!tbLeftPanel_1->get_active());
}

void FileCatalog::toggleRightPanel()
{
    tbRightPanel_1->set_active (!tbRightPanel_1->get_active());
}


void FileCatalog::selectImage (Glib::ustring fname, bool clearFilters)
{

    Glib::ustring dirname = Glib::path_get_dirname(fname);
    if (/* art::session::check(selectedDirectory) &&*/ file_name_set_.find(fname) != file_name_set_.end()) {
        dirname = selectedDirectory;
    }

    if (!dirname.empty()) {
        BrowsePath->set_text(dirname);


        if (clearFilters) { // clear all filters
            Query->set_text("");
            categoryButtonToggled(bFilterClear, false);

            // disable exif filters
            if (filterPanel->isEnabled()) {
                filterPanel->setEnabled (false);
            }
        }

        if (BrowsePath->get_text() != selectedDirectory) {
            // reload or refresh thumbs and select image
            buttonBrowsePathPressed ();
            // the actual selection of image will be handled asynchronously at the end of FileCatalog::previewsFinishedUI
            imageToSelect_fname = fname;
        } else {
            // FileCatalog::filterChanged ();//this will be replaced by queue_draw() in fileBrowser->selectImage
            fileBrowser->selectImage(fname);
            imageToSelect_fname = "";
        }
    }
}


bool FileCatalog::isSelected(const Glib::ustring &fname) const
{
    return fileBrowser->isSelected(fname);
}


void FileCatalog::openNextPreviousEditorImage (Glib::ustring fname, bool clearFilters, eRTNav nextPrevious)
{

    Glib::ustring dirname = Glib::path_get_dirname(fname);

    if (!dirname.empty()) {
        BrowsePath->set_text(dirname);


        if (clearFilters) { // clear all filters
            Query->set_text("");
            categoryButtonToggled(bFilterClear, false);

            // disable exif filters
            if (filterPanel->isEnabled()) {
                filterPanel->setEnabled (false);
            }
        }

        if (BrowsePath->get_text() != selectedDirectory) {
            // reload or refresh thumbs and select image
            buttonBrowsePathPressed ();
            // the actual selection of image will be handled asynchronously at the end of FileCatalog::previewsFinishedUI
            refImageForOpen_fname = fname;
            actionNextPrevious = nextPrevious;
        } else {
            // FileCatalog::filterChanged ();//this was replace by queue_draw() in fileBrowser->selectImage
            fileBrowser->openNextPreviousEditorImage(fname, nextPrevious);
            refImageForOpen_fname = "";
            actionNextPrevious = NAV_NONE;
        }
    }
}

bool FileCatalog::handleShortcutKey (GdkEventKey* event)
{

    bool ctrl = event->state & GDK_CONTROL_MASK;
    bool shift = event->state & GDK_SHIFT_MASK;
    bool alt = event->state & GDK_MOD1_MASK;
    bool altgr = event->state & (GDK_MOD2_MASK | GDK_MOD5_MASK);
    modifierKey = event->state;

    // GUI Layout
    switch(event->keyval) {
    case GDK_KEY_l:
        if (!alt && !ctrl) {
            tbLeftPanel_1->set_active (!tbLeftPanel_1->get_active());    // toggle left panel
        }

        if (!alt && ctrl) {
            tbRightPanel_1->set_active (!tbRightPanel_1->get_active());    // toggle right panel
        }

        if (alt && ctrl) {
            tbLeftPanel_1->set_active (!tbLeftPanel_1->get_active()); // toggle left panel
            tbRightPanel_1->set_active (!tbRightPanel_1->get_active()); // toggle right panel
        }

        return true;

    case GDK_KEY_m:
        if (!ctrl && !alt) {
            toggleSidePanels();
        }

        return true;
    }

    if (!shift) {
        switch(event->keyval) {
        case GDK_KEY_Escape:
            BrowsePath->set_text(selectedDirectory);
            fileBrowser->getFocus();
            return true;
        }
    }

    if (!alt && !shift && !altgr) { // shift is reserved for ranking
        switch(event->hardware_keycode) {
        case HWKeyCode::KEY_0:
            categoryButtonToggled(bUnRanked, false);
            return true;

        case HWKeyCode::KEY_1:
            categoryButtonToggled(bRank[0], false);
            return true;

        case HWKeyCode::KEY_2:
            categoryButtonToggled(bRank[1], false);
            return true;

        case HWKeyCode::KEY_3:
            categoryButtonToggled(bRank[2], false);
            return true;

        case HWKeyCode::KEY_4:
            categoryButtonToggled(bRank[3], false);
            return true;

        case HWKeyCode::KEY_5:
            categoryButtonToggled(bRank[4], false);
            return true;

        case HWKeyCode::KEY_6:
            categoryButtonToggled(bEdited[0], false);
            return true;

        case HWKeyCode::KEY_7:
            categoryButtonToggled(bEdited[1], false);
            return true;
        }
    }

    if (!alt && !shift) {
        switch(event->keyval) {

        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (BrowsePath->is_focus()) {
                FileCatalog::buttonBrowsePathPressed ();
                return true;
            }

            break;
        }
    }

    if (alt && !shift) { // shift is reserved for color labeling
        switch(event->hardware_keycode) {
        case HWKeyCode::KEY_0:
            categoryButtonToggled(bUnCLabeled, false);
            return true;

        case HWKeyCode::KEY_1:
            categoryButtonToggled(bCLabel[0], false);
            return true;

        case HWKeyCode::KEY_2:
            categoryButtonToggled(bCLabel[1], false);
            return true;

        case HWKeyCode::KEY_3:
            categoryButtonToggled(bCLabel[2], false);
            return true;

        case HWKeyCode::KEY_4:
            categoryButtonToggled(bCLabel[3], false);
            return true;

        case HWKeyCode::KEY_5:
            categoryButtonToggled(bCLabel[4], false);
            return true;

        case HWKeyCode::KEY_6:
            categoryButtonToggled(bRecentlySaved[0], false);
            return true;

        case HWKeyCode::KEY_7:
            categoryButtonToggled(bRecentlySaved[1], false);
            return true;
        }
    }

    if (!ctrl && !alt) {
        switch(event->keyval) {
        case GDK_KEY_d:
        case GDK_KEY_D:
            categoryButtonToggled(bFilterClear, false);
            return true;
        }
    }

    if (!ctrl || (alt && !options.tabbedUI)) {
        switch(event->keyval) {
        case GDK_KEY_i:
        //case GDK_KEY_I:
            exifInfo->set_active (!exifInfo->get_active());
            return true;

        case GDK_KEY_plus:
        case GDK_KEY_equal:
            zoomIn();
            return true;

        case GDK_KEY_minus:
        case GDK_KEY_underscore:
            zoomOut();
            return true;
        }
    }

    if (ctrl && !alt) {
        switch (event->keyval) {
        case GDK_KEY_o:
            if (button_session_load_->is_visible()) {
                sessionLoadPressed();
                return true;
            }
        case GDK_KEY_O:
            if (!BrowsePath->has_focus()) {
                BrowsePath->select_region(0, BrowsePath->get_text_length());
                BrowsePath->grab_focus();
            } else {
                fileBrowser->getFocus();
            }
            return true;

        case GDK_KEY_f:
            if (!Query->has_focus()) {
                Query->select_region(0, Query->get_text_length());
                Query->grab_focus();
            } else {
                fileBrowser->getFocus();
            }
            return true;

        case GDK_KEY_t:
        case GDK_KEY_T:
            modifierKey = 0; // HOMBRE: yet another hack.... otherwise the shortcut won't work
            categoryButtonToggled(bTrash, false);
            return true;

        case GDK_KEY_Delete:
            if (shift && bTrash->get_active()) {
                emptyTrash();
                return true;
            }
            break;

        case GDK_KEY_plus:
            if (button_session_add_->is_visible()) {
                sessionAddPressed();
                return true;
            }
            break;

        case GDK_KEY_minus:
            if (button_session_remove_->is_visible()) {
                sessionRemovePressed();
                return true;
            }
            break;

        case GDK_KEY_s:
            if (button_session_save_->is_visible()) {
                sessionSavePressed();
                return true;
            }
            break;
        }
    }

    if (!ctrl && !alt && shift) {
        switch (event->keyval) {
        case GDK_KEY_t:
        case GDK_KEY_T:
            if (inTabMode) {
                if (options.showFilmStripToolBar) {
                    hideToolBar();
                } else {
                    showToolBar();
                }

                options.showFilmStripToolBar = !options.showFilmStripToolBar;
            }

            return true;
        }
    }

    if (!ctrl && !alt && !shift) {
        switch (event->keyval) {
        case GDK_KEY_t:
        case GDK_KEY_T:
            if (inTabMode) {
                if (options.showFilmStripToolBar) {
                    hideToolBar();
                } else {
                    showToolBar();
                }

                options.showFilmStripToolBar = !options.showFilmStripToolBar;
            }

            refreshHeight();
            return true;

        case GDK_KEY_F5:
            FileCatalog::browsePathRefresh();
            return true;
        }
    }

    return fileBrowser->keyPressed(event);
}

void FileCatalog::showToolBar()
{
    if (hbToolBar1STB) {
        hbToolBar1STB->show();
    }

    buttonBar->show();
}

void FileCatalog::hideToolBar()
{
    if (hbToolBar1STB) {
        hbToolBar1STB->hide();
    }

    buttonBar->hide();
}


Glib::ustring FileCatalog::getBrowsePath()
{
    auto txt = BrowsePath->get_text();
    if (art::session::check(txt)) {
        return txt;
    }
    
    Glib::ustring expanded = "";
    auto prefix = txt.substr(0, 1);
    if (prefix == "~") { // home directory
        expanded = PlacesBrowser::userHomeDir();
    } else if (prefix == "!") { // user's pictures directory
        expanded = PlacesBrowser::userPicturesDir();
    }

    if (!expanded.empty()) {
        return Glib::ustring::compose("%1%2", expanded, txt.substr(1));
    } else {
        return txt;
    }
}


void FileCatalog::onBrowsePathChanged()
{
    auto txt = getBrowsePath();
    auto pos = txt.find_last_of(G_DIR_SEPARATOR_S);
    if (pos != Glib::ustring::npos) {
        auto root = txt.substr(0, pos+1);
        Glib::RefPtr<DirCompletion>::cast_static(browsePathCompletion)->refresh(root);
    }
}


void FileCatalog::disableInspector()
{
    if (fileBrowser) {
        fileBrowser->disableInspector();
        tbRightPanel_1->show();
        tbRightPanel_1->set_active(options.browserToolPanelOpened);
        tbLeftPanel_1->set_active(options.browserDirPanelOpened);
    }
}


void FileCatalog::enableInspector()
{
    if (fileBrowser) {
        fileBrowser->enableInspector();
        tbLeftPanel_1->set_active(options.inspectorDirPanelOpened);
        if (!tbRightPanel_1->get_active()) {
            toggleRightPanel();
            options.browserToolPanelOpened = false;
        }
        tbRightPanel_1->hide();
    }
}


void FileCatalog::setupSidePanels()
{
    tbLeftPanel_1->set_active(options.browserDirPanelOpened);
    tbRightPanel_1->set_active(options.browserToolPanelOpened);
    filepanel->showRightBox(options.browserToolPanelOpened);
    filepanel->placespaned->set_visible(options.browserDirPanelOpened);
    enableInspector();
    disableInspector();
}


void FileCatalog::removeFromBatchQueue(const std::vector<FileBrowserEntry*> &tbe)
{
    if (!bqueue_) {
        return;
    }

    std::set<Glib::ustring> tbset;
    for (auto entry : tbe) {
        if (entry->thumbnail && entry->thumbnail->isEnqueued()) {
            tbset.insert(entry->thumbnail->getFileName());
        }
    }

    std::vector<ThumbBrowserEntryBase *> tocancel;
    for (auto entry : bqueue_->getEntries()) {
        if (entry->thumbnail && tbset.find(entry->thumbnail->getFileName()) != tbset.end()) {
            tocancel.push_back(entry);
        }
    }

    bqueue_->cancelItems(tocancel, true);
}


void FileCatalog::sessionAddPressed()
{
    Gtk::FileChooserDialog dialog(getToplevelWindow(this), M("FILEBROWSER_SESSION_ADD_LABEL"), Gtk::FILE_CHOOSER_ACTION_OPEN);
    bindCurrentFolder(dialog, options.last_session_add_dir);

    dialog.add_button(M("GENERAL_CANCEL"), Gtk::RESPONSE_CANCEL);
    dialog.add_button(M("PREFERENCES_ADD"), Gtk::RESPONSE_OK);
    dialog.set_select_multiple(true);

    int result = dialog.run();
    dialog.hide();

    if (result == Gtk::RESPONSE_OK) {
        std::vector<Glib::ustring> toadd;
        for (auto f : dialog.get_files()) {
            toadd.push_back(f->get_path());
        }
        art::session::add(toadd);
    }
}


void FileCatalog::sessionRemovePressed()
{
    std::vector<Glib::ustring> todel;
    ThumbBrowserEntryBase *tosel = nullptr;
    bool sel = true;
    for (auto e : fileBrowser->getEntries()) {
        if (e->selected) {
            todel.push_back(e->filename);
            sel = true;
        } else if (sel) {
            tosel = e;
            sel = false;
        }
    }
    if (tosel) {
        fileBrowser->selectImage(tosel->thumbnail->getFileName());
    }
    art::session::remove(todel);
}


void FileCatalog::sessionLoadPressed()
{
    Gtk::FileChooserDialog dialog(getToplevelWindow(this), M("FILEBROWSER_SESSION_LOAD_LABEL"), Gtk::FILE_CHOOSER_ACTION_OPEN);
    bindCurrentFolder(dialog, options.last_session_loadsave_dir);

    dialog.add_button(M("GENERAL_CANCEL"), Gtk::RESPONSE_CANCEL);
    dialog.add_button(M("GENERAL_OPEN"), Gtk::RESPONSE_OK);

    Glib::RefPtr<Gtk::FileFilter> filter_ars = Gtk::FileFilter::create();
    filter_ars->set_name(M("FILECHOOSER_FILTER_ARS"));
    filter_ars->add_pattern("*.ars");
    dialog.add_filter(filter_ars);

    Glib::RefPtr<Gtk::FileFilter> filter_any = Gtk::FileFilter::create();
    filter_any->set_name(M("FILECHOOSER_FILTER_ANY"));
    filter_any->add_pattern("*");
    dialog.add_filter(filter_any);

    int result = dialog.run();
    dialog.hide();

    if (result == Gtk::RESPONSE_OK) {
        auto fname = dialog.get_filename();
        art::session::load(fname);
    }    
}


void FileCatalog::sessionSavePressed()
{
    Gtk::FileChooserDialog dialog(getToplevelWindow(this), M("FILEBROWSER_SESSION_SAVE_LABEL"), Gtk::FILE_CHOOSER_ACTION_SAVE);
    bindCurrentFolder(dialog, options.last_session_loadsave_dir);

    dialog.add_button(M("GENERAL_CANCEL"), Gtk::RESPONSE_CANCEL);
    dialog.add_button(M("GENERAL_SAVE"), Gtk::RESPONSE_OK);

    Glib::RefPtr<Gtk::FileFilter> filter_ars = Gtk::FileFilter::create();
    filter_ars->set_name(M("FILECHOOSER_FILTER_ARS"));
    filter_ars->add_pattern("*.ars");
    dialog.add_filter(filter_ars);

    Glib::RefPtr<Gtk::FileFilter> filter_any = Gtk::FileFilter::create();
    filter_any->set_name(M("FILECHOOSER_FILTER_ANY"));
    filter_any->add_pattern("*");
    dialog.add_filter(filter_any);

    while (true) {
        int result = dialog.run();
        //dialog.hide();

        if (result == Gtk::RESPONSE_OK) {
            auto fname = dialog.get_filename();
            if (getExtension(fname).empty()) {
                fname += ".ars";
            }
            if (confirmOverwrite(dialog, fname)) {
                art::session::save(fname);
                break;
            }
        } else {
            break;
        }
    }
}
