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
#ifndef _HISTORY_
#define _HISTORY_

#include <gtkmm.h>
#include "../rtengine/rtengine.h"
#include "pparamschangelistener.h"
#include "profilechangelistener.h"
#include "paramsedited.h"

class HistoryBeforeAfterListener
{
public:
    virtual ~HistoryBeforeAfterListener() = default;
    virtual void historyBeforeAfterChanged(const rtengine::procparams::ProcParams& params) = 0;
};

class History : public Gtk::VBox, public PParamsChangeListener
{

public:

    class HistoryColumns : public Gtk::TreeModel::ColumnRecord
    {
    public:
        Gtk::TreeModelColumn<Glib::ustring>  text;
        Gtk::TreeModelColumn<Glib::ustring>  value;
        Gtk::TreeModelColumn<rtengine::procparams::ProcParams>     params;
        Gtk::TreeModelColumn<rtengine::ProcEvent>    chev;
        //Gtk::TreeModelColumn<ParamsEdited>     paramsEdited;
        HistoryColumns()
        {
            add(text);
            add(value);
            add(chev);
            add(params);
            //add(paramsEdited);
        }
    };
    HistoryColumns historyColumns;
    class BookmarkColumns : public Gtk::TreeModel::ColumnRecord
    {
    public:
        Gtk::TreeModelColumn<Glib::ustring>  text;
        Gtk::TreeModelColumn<rtengine::procparams::ProcParams>     params;
        //Gtk::TreeModelColumn<ParamsEdited>     paramsEdited;
        BookmarkColumns()
        {
            add(text);
            add(params);
            //add(paramsEdited);
        }
    };
    BookmarkColumns bookmarkColumns;

protected:
    Gtk::VPaned*            historyVPaned;
    Gtk::TreeView*          hTreeView;
    Glib::RefPtr<Gtk::ListStore> historyModel;

    Gtk::ScrolledWindow*    bscrollw;
    Gtk::TreeView*          bTreeView;
    Glib::RefPtr<Gtk::ListStore> bookmarkModel;

    Gtk::Button*            addBookmark;
    Gtk::Button*            delBookmark;

    sigc::connection        selchangehist;
    sigc::connection        selchangebm;

    HistoryBeforeAfterListener * blistener;
    ProfileChangeListener* tpc;
    //ParamsEdited defParamsEdited;
    int bmnum;

    PParamsSnapshotListener *snapshotListener;
    bool shapshot_update_;

    bool blistenerLock;
    
    bool on_query_tooltip(int x, int y, bool keyboard_tooltip, const Glib::RefPtr<Gtk::Tooltip>& tooltip);

    std::vector<std::pair<Glib::ustring, rtengine::procparams::ProcParams>> getSnapshots();

    bool onPressEvent(GdkEventButton *event);
    bool confirmBookmarkUpdate();

public:

    explicit History (bool bookmarkSupport = true);

    void setProfileChangeListener     (ProfileChangeListener* tpc_)
    {
        tpc = tpc_;
    }
    void setHistoryBeforeAfterListener (HistoryBeforeAfterListener* bll)
    {
        blistener = bll;
    }

    void setBeforeAfterLock(bool yes);
    bool getBeforeAfterLock() const { return blistenerLock; }

    // pparamschangelistener interface
    void procParamsChanged(
        const rtengine::procparams::ProcParams* params,
        const rtengine::ProcEvent& ev,
        const Glib::ustring& descr,
        const ParamsEdited* paramsEdited = nullptr
    ) override;
    void clearParamChanges () override;

    void historySelectionChanged ();
    void bookmarkSelectionChanged ();
    void initHistory ();

    bool getBeforeAfterParams(rtengine::procparams::ProcParams& params);

    void addBookmarkWithText (Glib::ustring text);
    void addBookmarkPressed ();
    void delBookmarkPressed ();
    void snapshotNameEdited(const Glib::ustring &sold, const Glib::ustring &snew);

    //void resized (Gtk::Allocation& req);

    void undo ();
    void redo ();

    void resetSnapShotNumber()
    {
        bmnum = 1;
    }

    void setPParamsSnapshotListener(PParamsSnapshotListener *l);
    void setSnapshots(const std::vector<std::pair<Glib::ustring, rtengine::procparams::ProcParams>> &snapshots);
    void enableSnapshots(bool yes);

    void activateNextSnapshot();
    void activatePrevSnapshot();
};

#endif
