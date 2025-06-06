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
#include <giomm.h>
#include "multilangmgr.h"

class PlacesBrowser: public Gtk::VBox {
public:
    typedef sigc::slot<void, const Glib::ustring&> DirSelectionSlot;

private:
    enum class PlaceType : uint8_t {
      MOUNT,
      VOLUME,
      DRIVE,
      DEFAULT_DIR_OR_SESSION,
      FAVARITE_DIR
    };
    class PlacesColumns: public Gtk::TreeModel::ColumnRecord {
    public:
        Gtk::TreeModelColumn<Glib::RefPtr<Gio::Icon>> icon;
        Gtk::TreeModelColumn<Glib::ustring> label;
        Gtk::TreeModelColumn<Glib::ustring> root;
        Gtk::TreeModelColumn<PlaceType> type;
        Gtk::TreeModelColumn<bool> rowSeparator;
        PlacesColumns()
        {
            add(icon);
            add(label);
            add(root);
            add(type);
            add(rowSeparator);
        }
    };
    PlacesColumns placesColumns;
    Gtk::ScrolledWindow *scrollw;
    Gtk::TreeView *treeView;
    Glib::RefPtr<Gtk::ListStore> placesModel;
    Glib::RefPtr<Gio::VolumeMonitor> vm;
    DirSelectionSlot selectDir;
    Glib::ustring lastSelectedDir;
    Gtk::Button *add;
    Gtk::Button *del;
    sigc::connection selection_conn_;
    Glib::RefPtr<Gio::FileMonitor> session_monitor_;

    void on_session_changed(const Glib::RefPtr<Gio::File>& file, const Glib::RefPtr<Gio::File>& other_file, Gio::FileMonitorEvent event_type);

    void setRow(Gtk::TreeModel::Row row, Glib::RefPtr<Gio::Icon> icon,
                Glib::ustring label, Glib::ustring root, PlaceType type,
                bool rowSeparator);
    
public:

    PlacesBrowser();
    ~PlacesBrowser();

    void setDirSelector(const DirSelectionSlot& selectDir);
    void dirSelected(const Glib::ustring& dirname, const Glib::ustring& openfile);

    void refreshPlacesList();
    void mountChanged(const Glib::RefPtr<Gio::Mount>& m);
    void volumeChanged(const Glib::RefPtr<Gio::Volume>& v);
    void driveChanged(const Glib::RefPtr<Gio::Drive>& d);
    bool rowSeparatorFunc(const Glib::RefPtr<Gtk::TreeModel>& model, const Gtk::TreeModel::iterator& iter);
    void selectionChanged();
    void addPressed();
    void delPressed();

public:

    static Glib::ustring userHomeDir();
    static Glib::ustring userPicturesDir();
};

inline void PlacesBrowser::setDirSelector(const PlacesBrowser::DirSelectionSlot& selectDir)
{
    this->selectDir = selectDir;
}
