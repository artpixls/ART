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
#include "filepanel.h"

#include "rtwindow.h"
#include "inspector.h"
#include "placesbrowser.h"
#include "session.h"


FilePanel::FilePanel () :
    parent(nullptr), error(0),
    pane_pos_(-1)
{

    // Contains everything except for the batch Tool Panel and tabs (Fast Export, Inspect, etc)
    dirpaned = Gtk::manage ( new Gtk::HPaned () );
    dirpaned->set_position (options.dirBrowserWidth);

    // The directory tree
    dirBrowser = Gtk::manage ( new DirBrowser () );
    // Places
    placesBrowser = Gtk::manage ( new PlacesBrowser () );
    // Recent Folders
    recentBrowser = Gtk::manage ( new RecentBrowser () );

    // The whole left panel. Contains Places, Recent Folders and Folders.
    placespaned = Gtk::manage ( new Gtk::VPaned () );
    placespaned->set_name ("PlacesPaned");
    placespaned->set_size_request(200, 100);
    placespaned->set_position (options.dirBrowserHeight);

    Gtk::VBox* obox = Gtk::manage (new Gtk::VBox ());
    obox->get_style_context()->add_class ("plainback");
    obox->pack_start (*recentBrowser, Gtk::PACK_SHRINK, 4);
    obox->pack_start (*dirBrowser);

    placespaned->pack1 (*placesBrowser, false, true);
    placespaned->pack2 (*obox, true, true);

    dirpaned->pack1 (*placespaned, false, false);

    // Location bar
    fileCatalog = Gtk::manage(new FileCatalog(this));
    // Holds the location bar and thumbnails
    ribbonPane = Gtk::manage ( new Gtk::Paned() );
    ribbonPane->add(*fileCatalog);
    ribbonPane->set_size_request(50, 150);
    dirpaned->pack2 (*ribbonPane, true, true);

    DirBrowser::DirSelectionSignal dirSelected = dirBrowser->dirSelected ();
    dirSelected.connect (sigc::mem_fun (fileCatalog, &FileCatalog::dirSelected));
    dirSelected.connect (sigc::mem_fun (recentBrowser, &RecentBrowser::dirSelected));
    dirSelected.connect (sigc::mem_fun (placesBrowser, &PlacesBrowser::dirSelected));
//    dirSelected.connect (sigc::mem_fun (tpc, &BatchToolPanelCoordinator::dirSelected));
    fileCatalog->setDirSelector (sigc::mem_fun (dirBrowser, &DirBrowser::selectDir));
    placesBrowser->setDirSelector (sigc::mem_fun (dirBrowser, &DirBrowser::selectDir));
    recentBrowser->setDirSelector (sigc::mem_fun (dirBrowser, &DirBrowser::selectDir));
    fileCatalog->setFileSelectionListener (this);

    rightBox = Gtk::manage ( new Gtk::HBox () );
    rightBox->set_size_request(250, 100);
    rightNotebook = Gtk::manage ( new Gtk::Notebook () );
    rightNotebookSwitchConn = rightNotebook->signal_switch_page().connect_notify( sigc::mem_fun(*this, &FilePanel::on_NB_switch_page) );
    //Gtk::VBox* taggingBox = Gtk::manage ( new Gtk::VBox () );

    // history = Gtk::manage ( new History (false) );

    // tpc->addPParamsChangeListener (history);
    // history->setProfileChangeListener (tpc);
    // history->set_size_request(-1, 50);

    Gtk::ScrolledWindow* sFilterPanel = Gtk::manage ( new Gtk::ScrolledWindow() );
    filterPanel = Gtk::manage ( new FilterPanel () );
    sFilterPanel->add (*filterPanel);

    inspectorPanel = new Inspector(fileCatalog);
    fileCatalog->setInspector(inspectorPanel);
    inspectorPanel->signal_ready().connect(sigc::mem_fun(*this, &FilePanel::on_inspector_ready));

    fileCatalog->setFilterPanel (filterPanel);

    //------------------

    rightNotebook->set_tab_pos (Gtk::POS_RIGHT);

    // Gtk::Label* devLab = Gtk::manage ( new Gtk::Label (M("MAIN_TAB_DEVELOP")) );
    // devLab->set_name ("LabelRightNotebook");
    // devLab->set_angle (270);
    Gtk::Label* inspectLab = Gtk::manage ( new Gtk::Label (M("MAIN_TAB_INSPECT")) );
    inspectLab->set_name ("LabelRightNotebook");
    inspectLab->set_angle (270);
    inspectLab->set_tooltip_markup(M("MAIN_TAB_INSPECT_TOOLTIP"));
    Gtk::Label* filtLab = Gtk::manage ( new Gtk::Label (M("MAIN_TAB_FILTER")) );
    filtLab->set_name ("LabelRightNotebook");
    filtLab->set_angle (270);
    //Gtk::Label* tagLab = Gtk::manage ( new Gtk::Label (M("MAIN_TAB_TAGGING")) );
    //tagLab->set_angle (90);
    // Gtk::Label* exportLab = Gtk::manage ( new Gtk::Label (M("MAIN_TAB_EXPORT")) );
    // exportLab->set_name ("LabelRightNotebook");
    // exportLab->set_angle (90);

    // tpcPaned = Gtk::manage ( new Gtk::VPaned () );
    // tpcPaned->pack1 (*tpc->toolPanelNotebook, false, true);
    // tpcPaned->pack2 (*history, true, false);

    rightNotebook->append_page (*sFilterPanel, *filtLab);
    rightNotebook->append_page (*inspectorPanel, *inspectLab);
    // rightNotebook->append_page (*tpcPaned, *devLab);
    //rightNotebook->append_page (*taggingBox, *tagLab); commented out: currently the tab is empty ...
    rightNotebook->set_name ("RightNotebook");

    rightBox->pack_start (*rightNotebook);

    pack1(*dirpaned, true, true);
    pack2(*rightBox, false, false);

//    fileCatalog->setFileSelectionChangeListener (tpc);

    fileCatalog->setFileSelectionListener (this);

    idle_register.add(
        [this]() -> bool
        {
            init();
            return false;
        }
    );

    show_all ();
}

FilePanel::~FilePanel ()
{
    idle_register.destroy();

    rightNotebookSwitchConn.disconnect();

    if (inspectorPanel) {
        delete inspectorPanel;
    }
}

void FilePanel::on_realize ()
{
    Gtk::HPaned::on_realize ();
//    tpc->closeAllTools();
    pane_pos_ = get_position();
}


void FilePanel::setAspect ()
{
    fileCatalog->setupSidePanels();
    int winW = get_allocated_width();
    placespaned->set_position(options.dirBrowserHeight);
    dirpaned->set_position(options.dirBrowserWidth);
    set_position(winW - options.browserToolPanelWidth);

    rightNotebook->set_current_page(0);
}


void FilePanel::init ()
{
    dirBrowser->fillDirTree ();
    placesBrowser->refreshPlacesList ();

    if (!argv1.empty() && art::session::check(argv1)) {
        dirBrowser->open(argv1);
    } else if (!argv1.empty() && Glib::file_test(argv1, Glib::FILE_TEST_EXISTS)) {
        Glib::ustring d(argv1);
        if (!Glib::file_test(d, Glib::FILE_TEST_IS_DIR)) {
            d = Glib::path_get_dirname(d);
        }
        dirBrowser->open(d);
    } else {
        if (options.startupDir == Options::STARTUPDIR_HOME) {
            dirBrowser->open (PlacesBrowser::userPicturesDir ());
        } else if (options.startupDir == Options::STARTUPDIR_CURRENT) {
            dirBrowser->open (options.ART_base_dir);
        } else if (options.startupDir == Options::STARTUPDIR_CUSTOM || options.startupDir == Options::STARTUPDIR_LAST) {
            if (options.startupPath.length() && ((Glib::file_test(options.startupPath, Glib::FILE_TEST_EXISTS) && Glib::file_test(options.startupPath, Glib::FILE_TEST_IS_DIR)) || art::session::check(options.startupPath))) {
                dirBrowser->open (options.startupPath);
            } else {
                // Fallback option if the path is empty or the folder doesn't exist
                dirBrowser->open (PlacesBrowser::userPicturesDir ());
            }
        }
    }
}


void FilePanel::on_inspector_ready()
{
    for (const auto &e : fileCatalog->fileBrowser->getEntries()) {
        if (e->selected) {
            inspectorPanel->switchImage(e->filename);
            fileCatalog->selectImage(e->filename, false);
            break;
        }
    }
}


bool FilePanel::isInspectorVisible() const
{
    return rightNotebook->get_current_page() == 1;
}


void FilePanel::on_NB_switch_page(Gtk::Widget* page, guint page_num)
{
    if (page_num == 1) {
        // switching the inspector "on"
        fileCatalog->enableInspector();
        pane_pos_ = get_position();
        if (options.browser_width_for_inspector <= 1) {
            int pos = fileCatalog->fileBrowser->getColumnWidth();
            pos += rightBox->get_width();
            options.browser_width_for_inspector = pos;
        }
        set_position(options.browser_width_for_inspector);
        queue_draw();
    } else {
        // switching the inspector "off"
        if (inspectorPanel->isActive()) {
            options.browser_width_for_inspector = get_position();
        }
        fileCatalog->disableInspector();
        set_position(pane_pos_);
        queue_draw();
    }
}


FileSelectionListener::Result FilePanel::fileSelected (Thumbnail* thm)
{
    if (!parent) {
        return FileSelectionListener::Result::FAIL;
    }

    // Check if it's already open BEFORE loading the file
    if (options.tabbedUI && parent->selectEditorPanel(thm->getFileName())) {
        return FileSelectionListener::Result::OK;
    }

    if (!options.tabbedUI && parent->epanel->getIsProcessing()) {
        return FileSelectionListener::Result::BUSY;
    }

    // try to open the file
    bool loading = thm->imageLoad( true );

    if( !loading ) {
        return FileSelectionListener::Result::FAIL;
    }

    pendingLoadMutex.lock();
    if (parent->epanel) {
        parent->epanel->setIsProcessing();
    }
    
    pendingLoad *pl = new pendingLoad();
    pl->complete = false;
    pl->pc = nullptr;
    pl->thm = thm;
    pendingLoads.push_back(pl);
    if (!options.tabbedUI) {
        parent->epanel->close();
    }
    pendingLoadMutex.unlock();

    ProgressConnector<rtengine::InitialImage*> *ld = new ProgressConnector<rtengine::InitialImage*>();
    ld->startFunc (sigc::bind(sigc::ptr_fun(&rtengine::InitialImage::load), thm->getFileName (), thm->getType() == FT_Raw, &error, parent->getProgressListener()),
                   sigc::bind(sigc::mem_fun(*this, &FilePanel::imageLoaded), thm, ld) );
    return FileSelectionListener::Result::OK;
}

bool FilePanel::addBatchQueueJobs(const std::vector<BatchQueueEntry*>& entries)
{
    if (parent) {
        parent->addBatchQueueJobs (entries);
    }

    return true;
}

bool FilePanel::imageLoaded( Thumbnail* thm, ProgressConnector<rtengine::InitialImage*> *pc )
{

    pendingLoadMutex.lock();

    // find our place in the array and mark the entry as complete
    for (unsigned int i = 0; i < pendingLoads.size(); i++) {
        if (pendingLoads[i]->thm == thm) {
            pendingLoads[i]->pc = pc;
            pendingLoads[i]->complete = true;
            break;
        }
    }

    // The purpose of the pendingLoads vector is to open tabs in the same order as the loads where initiated. It has no effect on single editor mode.
    while (pendingLoads.size() > 0 && pendingLoads.front()->complete) {
        pendingLoad *pl = pendingLoads.front();

        if (pl->pc->returnValue()) {
            if (options.tabbedUI) {
                EditorPanel* epanel;
                {
#ifdef WIN32
                    int winGdiHandles = GetGuiResources( GetCurrentProcess(), GR_GDIOBJECTS);
                    if(winGdiHandles > 0 && winGdiHandles <= 8500) // 0 means we don't have the rights to access the function, 8500 because the limit is 10000 and we need about 1500 free handles
#endif
                    {
                    GThreadLock lock; // Acquiring the GUI... not sure that it's necessary, but it shouldn't harm
                    epanel = Gtk::manage (new EditorPanel ());
                    parent->addEditorPanel (epanel, pl->thm->getFileName());
                    }
#ifdef WIN32
                    else {
                        Glib::ustring msg_ = Glib::ustring("<b>") + M("MAIN_MSG_CANNOTLOAD") + " \"" + thm->getFileName() + "\" .\n" + M("MAIN_MSG_TOOMANYOPENEDITORS") + "</b>";
                        Gtk::MessageDialog msgd (*parent, msg_, true, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
                        msgd.run ();
                        goto MAXGDIHANDLESREACHED;
                    }
#endif
                }
                epanel->open(pl->thm, pl->pc->returnValue() );

                if (!(options.multiDisplayMode > 0)) {
                    parent->set_title_decorated(pl->thm->getFileName());
                }
            } else {
                {
                    GThreadLock lock; // Acquiring the GUI... not sure that it's necessary, but it shouldn't harm
                    parent->SetEditorCurrent();
                }
                parent->epanel->open(pl->thm, pl->pc->returnValue() );
                parent->set_title_decorated(pl->thm->getFileName());
            }
        } else {
            if (parent->epanel) {
                parent->epanel->refreshProcessingState(false);
            }
            Glib::ustring msg_ = Glib::ustring("<b>") + M("MAIN_MSG_CANNOTLOAD") + " \"" + thm->getFileName() + "\" .\n</b>";
            Gtk::MessageDialog msgd (*parent, msg_, true, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
            msgd.run ();
        }
#ifdef WIN32
MAXGDIHANDLESREACHED:
#endif
        //delete pl->pc;
        pl->pc->destroy();

        {
            GThreadLock lock; // Acquiring the GUI... not sure that it's necessary, but it shouldn't harm
            parent->setProgress(0.);
            parent->setProgressStr("");
        }

        pendingLoads.erase(pendingLoads.begin());
        delete pl;
    }

    pendingLoadMutex.unlock();

    thm->imageLoad( false );

    return false; // MUST return false from idle function
}

void FilePanel::saveOptions ()
{

    // int winW, winH;
    // parent->get_size(winW, winH);
    //int winW = get_allocated_width();
    options.dirBrowserWidth = dirpaned->get_position ();
    options.dirBrowserHeight = placespaned->get_position ();
    //options.browserToolPanelWidth = winW - (rightNotebook->get_current_page() == 0 ? get_position() : pane_pos_);
    // options.browserToolPanelHeight = tpcPaned->get_position ();

    if (options.startupDir == Options::STARTUPDIR_LAST && fileCatalog->lastSelectedDir () != "") {
        options.startupPath = fileCatalog->lastSelectedDir ();
    }

    if (inspectorPanel->isActive()) {
        options.browser_width_for_inspector = get_position();
    }

    fileCatalog->closeDir ();
}

void FilePanel::open (const Glib::ustring& d)
{

    if (Glib::file_test (d, Glib::FILE_TEST_IS_DIR)) {
        dirBrowser->open (d.c_str());
    } else if (Glib::file_test (d, Glib::FILE_TEST_EXISTS)) {
        dirBrowser->open (Glib::path_get_dirname(d), Glib::path_get_basename(d));
    }
}

void FilePanel::optionsChanged ()
{

    // tpc->optionsChanged ();
    fileCatalog->refreshThumbImages ();
}

bool FilePanel::handleShortcutKey (GdkEventKey* event)
{

    // if(tpc->getToolBar() && tpc->getToolBar()->handleShortcutKey(event)) {
    //     return true;
    // }

    // if(tpc->handleShortcutKey(event)) {
    //     return true;
    // }

    if(fileCatalog->handleShortcutKey(event)) {
        return true;
    }

    auto keyval = getKeyval(event);
    if (event->state & GDK_CONTROL_MASK && (keyval == GDK_KEY_i || keyval == GDK_KEY_I)) {
        rightNotebook->set_current_page((rightNotebook->get_current_page() + 1) % 2);
        return true;
    }

    return false;
}

void FilePanel::loadingThumbs(Glib::ustring str, double rate)
{
    GThreadLock lock; // All GUI access from idle_add callbacks or separate thread HAVE to be protected

    if( !str.empty()) {
        parent->setProgressStr(str);
    }

    parent->setProgress( rate );
}

void FilePanel::updateTPVScrollbar (bool hide)
{
//    tpc->updateTPVScrollbar (hide);
}


void FilePanel::showRightBox(bool yes)
{
    rightBox->set_visible(yes);
}


bool FilePanel::on_button_release_event(GdkEventButton *event)
{
    if (event->window == get_handle_window()->gobj()) {
        idle_register.add(
            [this]() -> bool
            {
                int winW = get_allocated_width();
                options.browserToolPanelWidth = winW - (rightNotebook->get_current_page() == 0 ? get_position() : pane_pos_);
                return false;
            });        
    }
    return Gtk::HPaned::on_button_press_event(event);
}
