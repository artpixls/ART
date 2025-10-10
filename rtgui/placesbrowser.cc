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
#include "placesbrowser.h"

#include <algorithm>

#ifdef WIN32
#include <windows.h>
#include <shlobj.h>
#include <Shlwapi.h>
#endif

#include "guiutils.h"
#include "rtimage.h"
#include "options.h"
#include "toolpanel.h"
#include "session.h"
#include "multilangmgr.h"

PlacesBrowser::PlacesBrowser ()
{

    scrollw = Gtk::manage (new Gtk::ScrolledWindow ());
    scrollw->set_policy (Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    pack_start (*scrollw);

    // Since Gtk3, we can't have image+text buttons natively. We'll comply to the Gtk guidelines and choose one of them (icons here)
    add = Gtk::manage (new Gtk::Button ());
    add->set_tooltip_text(M("MAIN_FRAME_PLACES_ADD"));
    setExpandAlignProperties(add, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_START);
    //add->get_style_context()->set_junction_sides(Gtk::JUNCTION_RIGHT);
    add->get_style_context()->add_class("Left");
    add->set_image (*Gtk::manage (new RTImage ("add-small.svg")));
    del = Gtk::manage (new Gtk::Button ());
    del->set_tooltip_text(M("MAIN_FRAME_PLACES_DEL"));
    setExpandAlignProperties(del, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_START);
    //del->get_style_context()->set_junction_sides(Gtk::JUNCTION_LEFT);
    del->get_style_context()->add_class("Right");
    del->set_image (*Gtk::manage (new RTImage ("remove-small.svg")));
    Gtk::Grid* buttonBox = Gtk::manage (new Gtk::Grid ());
    buttonBox->set_orientation(Gtk::ORIENTATION_HORIZONTAL);
    buttonBox->attach_next_to(*add, Gtk::POS_LEFT, 1, 1);
    buttonBox->attach_next_to(*del, *add, Gtk::POS_RIGHT, 1, 1);

    pack_start (*buttonBox, Gtk::PACK_SHRINK, 2);

    treeView = Gtk::manage (new Gtk::TreeView ());
    treeView->set_can_focus(false);
    scrollw->add (*treeView);

    placesModel = Gtk::ListStore::create (placesColumns);
    treeView->set_model (placesModel);
    treeView->set_headers_visible (true);
    treeView->set_tooltip_column(2);

    Gtk::TreeView::Column *iviewcol = Gtk::manage (new Gtk::TreeView::Column (M("MAIN_FRAME_PLACES")));
    Gtk::CellRendererPixbuf *iconCR  = Gtk::manage (new Gtk::CellRendererPixbuf());
    Gtk::CellRendererText *labelCR  = Gtk::manage (new Gtk::CellRendererText());
    labelCR->property_ellipsize() = Pango::ELLIPSIZE_MIDDLE;
    iviewcol->pack_start (*iconCR, false);
    iviewcol->pack_start (*labelCR, true);
    iviewcol->add_attribute (*iconCR, "gicon", 0);
    iviewcol->add_attribute (*labelCR, "text", placesColumns.label);
    treeView->append_column (*iviewcol);

    treeView->set_row_separator_func(sigc::mem_fun(*this, &PlacesBrowser::rowSeparatorFunc));

    vm = Gio::VolumeMonitor::get();

    vm->signal_mount_changed().connect (sigc::mem_fun(*this, &PlacesBrowser::mountChanged));
    vm->signal_mount_added().connect (sigc::mem_fun(*this, &PlacesBrowser::mountChanged));
    vm->signal_mount_removed().connect (sigc::mem_fun(*this, &PlacesBrowser::mountChanged));
    vm->signal_volume_changed().connect (sigc::mem_fun(*this, &PlacesBrowser::volumeChanged));
    vm->signal_volume_added().connect (sigc::mem_fun(*this, &PlacesBrowser::volumeChanged));
    vm->signal_volume_removed().connect (sigc::mem_fun(*this, &PlacesBrowser::volumeChanged));
    vm->signal_drive_connected().connect (sigc::mem_fun(*this, &PlacesBrowser::driveChanged));
    vm->signal_drive_disconnected().connect (sigc::mem_fun(*this, &PlacesBrowser::driveChanged));
    vm->signal_drive_changed().connect (sigc::mem_fun(*this, &PlacesBrowser::driveChanged));

    selection_conn_ = treeView->get_selection()->signal_changed().connect(sigc::mem_fun(*this, &PlacesBrowser::selectionChanged));
    add->signal_clicked().connect(sigc::mem_fun(*this, &PlacesBrowser::addPressed));
    del->signal_clicked().connect(sigc::mem_fun(*this, &PlacesBrowser::delPressed));

    session_monitor_ = Gio::File::create_for_path(art::session::filename())->monitor_file();
    session_monitor_->signal_changed().connect(sigc::mem_fun(*this, &PlacesBrowser::on_session_changed));

    show_all ();
}


PlacesBrowser::~PlacesBrowser()
{
    if (session_monitor_) {
        session_monitor_->cancel();
    }
}


void PlacesBrowser::on_session_changed(const Glib::RefPtr<Gio::File>& file, const Glib::RefPtr<Gio::File>& other_file, Gio::FileMonitorEvent event_type)
{
    bool is_session = art::session::check(lastSelectedDir);
    refreshPlacesList();
    if (is_session) {
        dirSelected(lastSelectedDir, "");
    }
}


// For drive letter comparison
bool compareMountByRoot (Glib::RefPtr<Gio::Mount> a, Glib::RefPtr<Gio::Mount> b)
{
    return a->get_root()->get_parse_name() < b->get_root()->get_parse_name();
}

void PlacesBrowser::setRow(Gtk::TreeModel::Row row,
                           Glib::RefPtr<Gio::Icon> icon, Glib::ustring label,
                           Glib::ustring root, PlaceType type,
                           bool rowSeparator) {
  row[placesColumns.icon] = std::move(icon);
  row[placesColumns.label] = std::move(label);
  row[placesColumns.root] = std::move(root);
  row[placesColumns.type] = type;
  row[placesColumns.rowSeparator] = rowSeparator;
}

void PlacesBrowser::refreshPlacesList ()
{
    placesModel->clear ();

    // append home directory
    Glib::RefPtr<Gio::File> hfile = Gio::File::create_for_path (userHomeDir());  // Will send back "My documents" on Windows now, which has no restricted access

    if (hfile && hfile->query_exists()) {
        try {
            if (auto info = hfile->query_info ()) {
                setRow(*(placesModel->append()), info->get_icon(),
                       info->get_display_name(), hfile->get_parse_name(),
                       PlaceType::DEFAULT_DIR_OR_SESSION, false);
            }
        } catch (Gio::Error&) {}
    }

    // append pictures directory
    auto hfile2 = Gio::File::create_for_path(userPicturesDir());

    if (hfile2 && hfile2->query_exists() && !hfile2->equal(hfile)) {
        try {
            if (auto info = hfile2->query_info ()) {
                setRow(*(placesModel->append()), info->get_icon(),
                       info->get_display_name(), hfile2->get_parse_name(),
                       PlaceType::DEFAULT_DIR_OR_SESSION, false);
            }
        } catch (Gio::Error&) {}
    }

    // session
    setRow(*(placesModel->append()),
         Gio::ThemedIcon::create("document-open-recent"),
         M("SESSION_LABEL") + " (" +
             std::to_string(art::session::list().size()) + ")",
         art::session::path(), PlaceType::DEFAULT_DIR_OR_SESSION, false);

    // append favorites
    if (!placesModel->children().empty()) {
        Gtk::TreeModel::Row newrow = *(placesModel->append());
        newrow[placesColumns.rowSeparator] = true;
    }

    for (size_t i = 0; i < options.favoriteDirs.size(); i++) {
        Glib::RefPtr<Gio::File> hfile = Gio::File::create_for_path (options.favoriteDirs[i]);

        if (hfile && hfile->query_exists()) {
            try {
                if (auto info = hfile->query_info ()) {
                    setRow(*(placesModel->append()), info->get_icon(),
                    info->get_display_name(), hfile->get_parse_name(),
                    PlaceType::FAVARITE_DIR, false);
                }
            } catch(Gio::Error&) {}
        }
    }
    
    if (!placesModel->children().empty()) {
        Gtk::TreeModel::Row newrow = *(placesModel->append());
        newrow[placesColumns.rowSeparator] = true;
    }

    // scan all drives
    std::vector<Glib::RefPtr<Gio::Drive> > drives = vm->get_connected_drives ();

    for (size_t j = 0; j < drives.size (); j++) {
        std::vector<Glib::RefPtr<Gio::Volume> > volumes = drives[j]->get_volumes ();

        if (volumes.empty()) {
            setRow(*(placesModel->append()), drives[j]->get_icon(),
                    drives[j]->get_name(), "", PlaceType::DRIVE, false);
        }

        for (size_t i = 0; i < volumes.size (); i++) {
            Glib::RefPtr<Gio::Mount> mount = volumes[i]->get_mount ();

            if (mount) { // placesed volumes
                setRow(*(placesModel->append()), mount->get_icon(),
                        mount->get_name(), mount->get_root ()->get_parse_name(),
                        PlaceType::MOUNT, false);
            } else { // unplacesed volumes
                setRow(*(placesModel->append()), volumes[i]->get_icon(),
                        volumes[i]->get_name(), "", PlaceType::VOLUME, false);
            }
        }
    }

    // volumes not belonging to drives
    std::vector<Glib::RefPtr<Gio::Volume> > volumes = vm->get_volumes ();

    for (size_t i = 0; i < volumes.size (); i++) {
        if (!volumes[i]->get_drive ()) {
            Glib::RefPtr<Gio::Mount> mount = volumes[i]->get_mount ();

            if (mount) { // placesed volumes
                setRow(*(placesModel->append()), mount->get_icon(),
                        mount->get_name(), mount->get_root ()->get_parse_name(),
                        PlaceType::MOUNT, false);
            } else { // unplacesed volumes
                setRow(*(placesModel->append()), volumes[i]->get_icon(),
                        volumes[i]->get_name(), "", PlaceType::VOLUME, false);
            }
        }
    }

    // places not belonging to volumes
    // (Drives in Windows)
    std::vector<Glib::RefPtr<Gio::Mount> > mounts = vm->get_mounts ();

#ifdef WIN32
    // on Windows, it's usual to sort by drive letter, not by name
    std::sort (mounts.begin(), mounts.end(), compareMountByRoot);
#endif

    for (size_t i = 0; i < mounts.size (); i++) {
        if (!mounts[i]->get_volume ()) {
            setRow(*(placesModel->append()), mounts[i]->get_icon(),
                    mounts[i]->get_name(), mounts[i]->get_root ()->get_parse_name(),
                    PlaceType::MOUNT, false);
        }
    }
}

bool PlacesBrowser::rowSeparatorFunc (const Glib::RefPtr<Gtk::TreeModel>& model, const Gtk::TreeModel::iterator& iter)
{

    return iter->get_value (placesColumns.rowSeparator);
}

void PlacesBrowser::mountChanged (const Glib::RefPtr<Gio::Mount>& m)
{
    GThreadLock lock;
    refreshPlacesList ();
}

void PlacesBrowser::volumeChanged (const Glib::RefPtr<Gio::Volume>& m)
{
    GThreadLock lock;
    refreshPlacesList ();
}

void PlacesBrowser::driveChanged (const Glib::RefPtr<Gio::Drive>& m)
{
    GThreadLock lock;
    refreshPlacesList ();
}

void PlacesBrowser::selectionChanged ()
{
    Glib::RefPtr<Gtk::TreeSelection> selection = treeView->get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();

    if (iter) {
        if (iter->get_value (placesColumns.type) == PlaceType::VOLUME) {
            std::vector<Glib::RefPtr<Gio::Volume> > volumes = vm->get_volumes ();

            for (size_t i = 0; i < volumes.size(); i++)
                if (volumes[i]->get_name () == iter->get_value (placesColumns.label)) {
                    volumes[i]->mount ();
                    break;
                }
        } else if (iter->get_value (placesColumns.type) == PlaceType::DRIVE) {
            std::vector<Glib::RefPtr<Gio::Drive> > drives = vm->get_connected_drives ();

            for (size_t i = 0; i < drives.size(); i++)
                if (drives[i]->get_name () == iter->get_value (placesColumns.label)) {
                    drives[i]->poll_for_media ();
                    break;
                }
        } else if (selectDir) {
            selectDir (iter->get_value (placesColumns.root));
        }
    }
}


void PlacesBrowser::dirSelected (const Glib::ustring& dirname, const Glib::ustring& openfile)
{
    lastSelectedDir = dirname;
    ConnectionBlocker b(selection_conn_);
    auto selection = treeView->get_selection();
    selection->unselect_all();
    auto children = placesModel->children();
    for (auto it = children.begin(), end = children.end(); it != end; ++it) {
        if ((*it)[placesColumns.root] == dirname) {
            selection->select(it);
            break;
        }
    }
}


void PlacesBrowser::addPressed ()
{

    if (lastSelectedDir == "" || art::session::check(lastSelectedDir)) {
        return;
    }

    // check if the dirname is already in the list. If yes, return.
    const auto all_items = placesModel->children();
    if (std::any_of(all_items.begin(), all_items.end(), [this](const Gtk::TreeModel::Row& row) {
        return row[placesColumns.root] == lastSelectedDir;
    })) {
        return;
    }

    // append
    Glib::RefPtr<Gio::File> hfile = Gio::File::create_for_path (lastSelectedDir);

    if (hfile && hfile->query_exists()) {
        try {
            if (auto info = hfile->query_info ()) {
                options.favoriteDirs.push_back (hfile->get_parse_name ());
                refreshPlacesList ();
            }
        } catch(Gio::Error&) {}
    }
}

void PlacesBrowser::delPressed ()
{

    // lookup the selected item in the bookmark
    Glib::RefPtr<Gtk::TreeSelection> selection = treeView->get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();

    if (iter && iter->get_value(placesColumns.type) == PlaceType::FAVARITE_DIR) {
        std::vector<Glib::ustring>::iterator i = std::find (options.favoriteDirs.begin(), options.favoriteDirs.end(), iter->get_value (placesColumns.root));

        if (i != options.favoriteDirs.end()) {
            options.favoriteDirs.erase (i);
        }

        refreshPlacesList();
    }
}

Glib::ustring PlacesBrowser::userHomeDir ()
{
#ifdef WIN32

    // get_home_dir crashes on some Windows configurations,
    // so we rather use the safe native functions here.
    WCHAR pathW[MAX_PATH];
    if (SHGetSpecialFolderPathW (NULL, pathW, CSIDL_PERSONAL, false)) {

        char pathA[MAX_PATH];
        if (WideCharToMultiByte (CP_UTF8, 0, pathW, -1, pathA, MAX_PATH, 0, 0)) {

            return Glib::ustring (pathA);
        }
    }

    return Glib::ustring ("C:\\");

#else

    return Glib::get_home_dir ();

#endif
}

Glib::ustring PlacesBrowser::userPicturesDir ()
{
#ifdef WIN32

    // get_user_special_dir crashes on some Windows configurations,
    // so we rather use the safe native functions here.
    WCHAR pathW[MAX_PATH];
    if (SHGetSpecialFolderPathW (NULL, pathW, CSIDL_MYPICTURES, false)) {

        char pathA[MAX_PATH];
        if (WideCharToMultiByte (CP_UTF8, 0, pathW, -1, pathA, MAX_PATH, 0, 0)) {

            return Glib::ustring (pathA);
        }
    }

    return Glib::ustring ("C:\\");

#else

    return Glib::get_user_special_dir (G_USER_DIRECTORY_PICTURES);

#endif
}
