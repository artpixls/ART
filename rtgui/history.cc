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
#include <set>
#include "history.h"
#include "multilangmgr.h"
#include "rtimage.h"
#include "guiutils.h"
#include "eventmapper.h"

#include <iostream>

using namespace rtengine;
using namespace rtengine::procparams;


History::History (bool bookmarkSupport) :
    historyVPaned(nullptr),
    blistener(nullptr),
    tpc(nullptr),
    bmnum(1),
    snapshotListener(nullptr),
    shapshot_update_(false)
{

    blistenerLock = false; // sets default that the Before preview will not be locked
    // History List
    // ~~~~~~~~~~~~
    Gtk::ScrolledWindow* hscrollw = Gtk::manage (new Gtk::ScrolledWindow ());
    hscrollw->set_policy (Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);

    Gtk::Frame* histFrame = Gtk::manage (new Gtk::Frame (M("HISTORY_LABEL")));
    histFrame->set_label_align(0.025, 0.5);
    histFrame->set_name ("HistoryPanel");
    histFrame->add (*hscrollw);

    hTreeView = Gtk::manage (new Gtk::TreeView ());
    hscrollw->add (*hTreeView);

    historyModel = Gtk::ListStore::create (historyColumns);
    hTreeView->set_model (historyModel);
    hTreeView->set_headers_visible (false);
    hTreeView->set_hscroll_policy(Gtk::SCROLL_MINIMUM);
    hTreeView->set_vscroll_policy(Gtk::SCROLL_NATURAL);
    hTreeView->set_size_request(80, -1);

    Gtk::CellRendererText *changecrt = Gtk::manage (new Gtk::CellRendererText());
    changecrt->property_ellipsize() = Pango::ELLIPSIZE_END;
    Gtk::CellRendererText *valuecrt  = Gtk::manage (new Gtk::CellRendererText());
    valuecrt->property_ellipsize() = Pango::ELLIPSIZE_END;
    Gtk::TreeView::Column *hviewcol = Gtk::manage (new Gtk::TreeView::Column (""));
    hviewcol->pack_start (*changecrt, true);
    hviewcol->add_attribute (changecrt->property_text()/*markup ()*/, historyColumns.text);
    hviewcol->set_expand(true);
    hviewcol->set_resizable (true);
    hviewcol->set_fixed_width(35);
    hviewcol->set_min_width(35);
    hviewcol->set_sizing(Gtk::TREE_VIEW_COLUMN_AUTOSIZE);

    Gtk::TreeView::Column *hviewcol2 = Gtk::manage (new Gtk::TreeView::Column (""));
    hviewcol2->pack_start (*valuecrt, true);
    hviewcol2->add_attribute (valuecrt->property_text()/*markup ()*/, historyColumns.value);
    hviewcol2->set_expand(true);
    hviewcol2->set_resizable(true);
    hviewcol2->set_fixed_width(35);
    hviewcol2->set_min_width(35);
    hviewcol2->set_sizing(Gtk::TREE_VIEW_COLUMN_AUTOSIZE);
    valuecrt->set_alignment(1.f, 0.f);

    hTreeView->set_has_tooltip(true);
    hTreeView->signal_query_tooltip().connect( sigc::mem_fun(*this, &History::on_query_tooltip) );
    hTreeView->append_column (*hviewcol);
    hTreeView->append_column (*hviewcol2);

    selchangehist = hTreeView->get_selection()->signal_changed().connect(sigc::mem_fun(*this, &History::historySelectionChanged));

    // Bookmark List
    // ~~~~~~~~~~~~~

    Gtk::HBox* ahbox = Gtk::manage (new Gtk::HBox ());
    addBookmark = Gtk::manage (new Gtk::Button ());  // M("HISTORY_NEWSNAPSHOT")
    setExpandAlignProperties(addBookmark, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_START);
    //addBookmark->get_style_context()->set_junction_sides(Gtk::JUNCTION_RIGHT);
    addBookmark->get_style_context()->add_class("Left");
    addBookmark->set_tooltip_markup (M("HISTORY_NEWSNAPSHOT_TOOLTIP"));
    Gtk::Image* addimg = Gtk::manage (new RTImage ("add-small.png"));
    addBookmark->set_image (*addimg);
    ahbox->pack_start (*addBookmark);

    delBookmark = Gtk::manage (new Gtk::Button ());  // M("HISTORY_DELSNAPSHOT")
    setExpandAlignProperties(delBookmark, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_START);
    //delBookmark->get_style_context()->set_junction_sides(Gtk::JUNCTION_LEFT);
    delBookmark->get_style_context()->add_class("Right");
    Gtk::Image* delimg = Gtk::manage (new RTImage ("remove-small.png"));
    delBookmark->set_image (*delimg);
    ahbox->pack_start (*delBookmark);

    bscrollw = Gtk::manage (new Gtk::ScrolledWindow ());
//    bscrollw->set_policy (Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    bscrollw->set_policy (Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    bscrollw->set_size_request (-1, 45);

    Gtk::Frame* bmFrame = Gtk::manage (new Gtk::Frame (M("HISTORY_SNAPSHOTS")));
    bmFrame->set_label_align(0.025, 0.5);
    bmFrame->set_name("Snapshots");
    Gtk::VBox* bmBox = Gtk::manage (new Gtk::VBox ());
    bmFrame->add (*bmBox);
    bmBox->pack_start (*bscrollw, Gtk::PACK_EXPAND_WIDGET, 4);
    bmBox->pack_end (*ahbox, Gtk::PACK_SHRINK, 4);
    bmBox->set_size_request(-1,100);

    if (bookmarkSupport) {
        historyVPaned = Gtk::manage ( new Gtk::VPaned () );
        historyVPaned->pack1 (*histFrame, true, true);
        historyVPaned->pack2 (*bmFrame, false, false);
        pack_start(*historyVPaned);
    } else {
        pack_start (*histFrame);
    }


    bTreeView = Gtk::manage (new Gtk::TreeView ());
    bscrollw->add (*bTreeView);

    bookmarkModel = Gtk::ListStore::create (bookmarkColumns);
    bTreeView->set_model (bookmarkModel);
    bTreeView->set_headers_visible (false);
    bTreeView->append_column_editable (M("HISTORY_SNAPSHOTS"), bookmarkColumns.text);

    selchangebm = bTreeView->get_selection()->signal_changed().connect(sigc::mem_fun(*this, &History::bookmarkSelectionChanged));
    bTreeView->add_events(Gdk::BUTTON_PRESS_MASK);
    bTreeView->signal_button_press_event().connect(sigc::mem_fun(*this, &History::onPressEvent), false);

    addBookmark->signal_clicked().connect( sigc::mem_fun(*this, &History::addBookmarkPressed) );
    delBookmark->signal_clicked().connect( sigc::mem_fun(*this, &History::delBookmarkPressed) );
    static_cast<Gtk::CellRendererText *>(bTreeView->get_column_cell_renderer(0))->signal_edited().connect(sigc::mem_fun(*this, &History::snapshotNameEdited));
    static_cast<Gtk::CellRendererText *>(bTreeView->get_column_cell_renderer(0))->property_ellipsize() = Pango::ELLIPSIZE_MIDDLE;


    //hTreeView->set_grid_lines (Gtk::TREE_VIEW_GRID_LINES_HORIZONTAL);
    hTreeView->set_grid_lines (Gtk::TREE_VIEW_GRID_LINES_BOTH);
    //hTreeView->signal_size_allocate().connect( sigc::mem_fun(*this, &History::resized) );

    hTreeView->set_enable_search(false);
    bTreeView->set_enable_search(false);

    show_all_children ();
}


void History::initHistory ()
{

    ConnectionBlocker selBlocker(selchangehist);
    historyModel->clear ();
    bookmarkModel->clear ();
}

void History::historySelectionChanged ()
{

    Glib::RefPtr<Gtk::TreeSelection> selection = hTreeView->get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();

    if (iter) {
        Gtk::TreeModel::Row row = *iter;

        if (row) {
            bTreeView->get_selection()->unselect_all ();
        }

        if (row && tpc) {
            const auto &pparams = row[historyColumns.params];
            // ParamsEdited pe(true);
            // PartialProfile pp(&pparams, &pe);
            // ParamsEdited paramsEdited = row[historyColumns.paramsEdited];
            FullPartialProfile pp(pparams);
            tpc->profileChange (&pp, EvHistoryBrowsed, row[historyColumns.text], nullptr);//&paramsEdited);
        }

        if (blistener && !blistenerLock) {
            // Gtk::TreeModel::Path path = historyModel->get_path (iter);
            // path.prev ();
            // iter = historyModel->get_iter (path);

            if (blistener && iter) {
                blistener->historyBeforeAfterChanged (iter->get_value (historyColumns.params));
            }
        }
    }
}

void History::bookmarkSelectionChanged ()
{

    Glib::RefPtr<Gtk::TreeSelection> selection = bTreeView->get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();

    if (iter) {
        Gtk::TreeModel::Row row = *iter;

        if (shapshot_update_) {
            shapshot_update_ = false;
            if (confirmBookmarkUpdate() && row) {
                selection = hTreeView->get_selection();
                iter = selection->get_selected();
                if (!iter && historyModel->children().size() > 0) {
                    iter = historyModel->children().begin();
                    for (size_t i = 1; i < historyModel->children().size(); ++i) {
                        ++iter;
                    }
                }
                if (iter) {
                    Gtk::TreeModel::Row hrow = *iter;
                    ProcParams params = hrow[historyColumns.params];
                    row[bookmarkColumns.params] = params;

                    if (snapshotListener) {
                        snapshotListener->snapshotsChanged(getSnapshots());
                    }
                }
            }
        } else {
            if (row) {
                hTreeView->get_selection()->unselect_all();
            }

            if (row && tpc) {
                const auto &pparams = row[bookmarkColumns.params];
                // ParamsEdited pe(true);
                // PartialProfile pp(&pparams, &pe);
                // ParamsEdited paramsEdited = row[bookmarkColumns.paramsEdited];
                FullPartialProfile pp(pparams);
                tpc->profileChange (&pp, EvBookmarkSelected, row[bookmarkColumns.text], nullptr);//&paramsEdited);
            }
        }
    }
}

void History::procParamsChanged(
    const ProcParams* params,
    const ProcEvent& ev,
    const Glib::ustring& descr,
    const ParamsEdited* paramsEdited
)
{
    // to prevent recursion, we filter out the events triggered by the history and events that should not be registered
    if (ev == EvHistoryBrowsed || ev == EvMonitorTransform) {
        return;
    }

    // construct formatted list content
    Glib::ustring text = M(ProcEventMapper::getInstance()->getHistoryMsg(ev));
    
    if (text.empty() && descr.empty()) {
        return;
    }

    selchangehist.block (true);
    selchangebm.block (true);

    if (ev == EvPhotoLoaded) {
        initHistory ();
    }

    Glib::RefPtr<Gtk::TreeSelection> selection = hTreeView->get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();

    // remove all rows after the selection
    if (iter) {
        ++iter;

        while (iter) {
            iter = historyModel->erase (iter);
        }
    }

    // lookup the last remaining item in the list
    int size = historyModel->children().size ();
    Gtk::TreeModel::Row row;

    if (size > 0) {
        row = historyModel->children()[size - 1];
    }

    // if there is no last item or its chev!=ev, create a new one
    if (size == 0 || !row || row[historyColumns.chev] != ev  || ev == EvProfileChanged) {
        Gtk::TreeModel::Row newrow = *(historyModel->append());
        newrow[historyColumns.text] = text;//escapeHtmlChars(text);
        newrow[historyColumns.value] = descr;//g_markup_escape_text(descr.c_str(), -1);
        newrow[historyColumns.chev] = ev;
        newrow[historyColumns.params] = *params;
        //newrow[historyColumns.paramsEdited] = paramsEdited ? *paramsEdited : defParamsEdited;

        if (ev != EvBookmarkSelected) {
            selection->select (newrow);
        }

        if (blistener && /*row &&*/ !blistenerLock) {
        //     blistener->historyBeforeAfterChanged (row[historyColumns.params]);
        // } else if (blistener && size == 0 && !blistenerLock) {
            blistener->historyBeforeAfterChanged (newrow[historyColumns.params]);
        }
    }
    // else just update it
    else {
        row[historyColumns.text] = text;
        row[historyColumns.value] = g_markup_escape_text(descr.c_str(), -1);
        row[historyColumns.chev] = ev;
        row[historyColumns.params] = *params;
        //row[historyColumns.paramsEdited] = paramsEdited ? *paramsEdited : defParamsEdited;

        if (ev != EvBookmarkSelected) {
            selection->select (row);
        }
        if (blistener && /*row &&*/ !blistenerLock) {
            blistener->historyBeforeAfterChanged (row[historyColumns.params]);
        }
    }

    if (ev != EvBookmarkSelected) {
        bTreeView->get_selection()->unselect_all ();
    }


    if (!selection->get_selected_rows().empty()) {
        std::vector<Gtk::TreeModel::Path> selp = selection->get_selected_rows();
        hTreeView->scroll_to_row (*selp.begin());
    }

    selchangehist.block (false);
    selchangebm.block (false);
}

void History::clearParamChanges ()
{
    initHistory ();
}

void History::addBookmarkWithText (Glib::ustring text)
{
    // lookup the selected item in the history
    Glib::RefPtr<Gtk::TreeSelection> selection = hTreeView->get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();
    Gtk::TreeModel::Row row = *iter;

    if (!row) {
        return;
    }

    // append new row to bookmarks
    auto newit = bookmarkModel->append();
    Gtk::TreeModel::Row newrow = *newit;//(bookmarkModel->append());
    newrow[bookmarkColumns.text] = text;
    ProcParams params = row[historyColumns.params];
    newrow[bookmarkColumns.params] = params;
    // ParamsEdited paramsEdited = row[historyColumns.paramsEdited];
    // newrow[bookmarkColumns.paramsEdited] = paramsEdited;

    bTreeView->set_cursor(Gtk::TreeModel::Path(newit), *bTreeView->get_column(0), true);
}

void History::addBookmarkPressed ()
{

    if (hTreeView->get_selection()->get_selected()) {
        Glib::ustring name;
        std::set<Glib::ustring> names;
        for (auto p : getSnapshots()) {
            names.insert(p.first);
        }
        do {
            name = Glib::ustring::compose("%1 %2", M("HISTORY_SNAPSHOT"), bmnum++);
        } while (names.find(name) != names.end());
        addBookmarkWithText(name);

        if (snapshotListener) {
            snapshotListener->snapshotsChanged(getSnapshots());
        }
    }
}

void History::delBookmarkPressed ()
{

    // lookup the selected item in the bookmark
    Glib::RefPtr<Gtk::TreeSelection> selection = bTreeView->get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();

    if (!iter) {
        return;
    }

    // remove selected bookmark
    bookmarkModel->erase (iter);
    // select last item in history
    int size = historyModel->children().size ();
    Gtk::TreeModel::Row row = historyModel->children()[size - 1];
    hTreeView->get_selection()->select (row);

    if (snapshotListener) {
        snapshotListener->snapshotsChanged(getSnapshots());
    }
}

void History::undo ()
{

    Glib::RefPtr<Gtk::TreeSelection> selection = hTreeView->get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();

    if (iter && iter != historyModel->children().begin()) {
        selection->select (--iter);
    } else if (!iter) {
        int size = historyModel->children().size ();

        if (size > 1) {
            selection->select (historyModel->children().operator [](size - 2));
        }
    }
}

void History::redo ()
{

    Glib::RefPtr<Gtk::TreeSelection> selection = hTreeView->get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();

    if (iter) {
        ++iter;

        if (iter != historyModel->children().end()) {
            selection->select (iter);
        }
    } else {
        int size = historyModel->children().size ();

        if (size > 1) {
            selection->select (historyModel->children().operator [](size - 2));
        }
    }
}

/*
void History::resized (Gtk::Allocation& req)
{
}
*/

bool History::getBeforeAfterParams(rtengine::procparams::ProcParams& params)
{

    int size = historyModel->children().size ();

    if (size == 0 || !blistener) {
        return false;
    }

    Gtk::TreeModel::Row row;
    row = historyModel->children()[size - 1];//size == 1 ? 0 : size - 2];
    params = row[historyColumns.params];
    return true;
}

bool History::on_query_tooltip(int x, int y, bool keyboard_tooltip, const Glib::RefPtr<Gtk::Tooltip>& tooltip) {
    bool displayTooltip = false;

    Gtk::TreeModel::Path path;
    int x2 = -1;
    int y2 = -1;
    hTreeView->convert_widget_to_bin_window_coords(x, y, x2, y2);
    bool hasPath = hTreeView->get_path_at_pos(x2, y2, path);

    if (hasPath) {
        if (path && !path.empty()) {
            Gtk::TreeModel::iterator iter = historyModel->get_iter(path);
            if (iter) {
                Glib::ustring text, val;
                iter->get_value<Glib::ustring>(0, text);
                iter->get_value<Glib::ustring>(1, val);

                /*
                 *
                 *
                 * Why this doesn't work ???
                 *
                 *
                 *
                Gtk::Label *left = Gtk::manage (new Gtk::Label(param+" :"));
                Gtk::Label *right = Gtk::manage (new Gtk::Label(val));
                right->set_justify(Gtk::JUSTIFY_LEFT);
                Gtk::HBox *hbox = Gtk::manage (new Gtk::HBox());
                hbox->set_spacing(5);
                hbox->pack_start(*left, Gtk::PACK_SHRINK, 0);
                hbox->pack_start(*right, Gtk::PACK_SHRINK, 0);
                tooltip->set_custom(*hbox);
                */

                tooltip->set_text(text + " : " + val);
                displayTooltip = true;
            }
        }
    }
    return displayTooltip;
}


void History::setPParamsSnapshotListener(PParamsSnapshotListener *l)
{
    snapshotListener = l;
}


void History::setSnapshots(const std::vector<std::pair<Glib::ustring, rtengine::procparams::ProcParams>> &snapshots)
{
    bookmarkModel->clear();
    for (auto &p : snapshots) {
        auto it = bookmarkModel->append();
        auto &row = *it;
        row[bookmarkColumns.text] = p.first;
        row[bookmarkColumns.params] = p.second;
    }
}


std::vector<std::pair<Glib::ustring, rtengine::procparams::ProcParams>> History::getSnapshots()
{
    std::vector<std::pair<Glib::ustring, rtengine::procparams::ProcParams>> ret;
    for (const auto &row : bookmarkModel->children()) {
        ret.push_back(std::make_pair(row[bookmarkColumns.text], row[bookmarkColumns.params]));
    }
    return ret;
}


void History::snapshotNameEdited(const Glib::ustring &sold, const Glib::ustring &snew)
{
    if (snapshotListener) {
        snapshotListener->snapshotsChanged(getSnapshots());
    }
}


void History::enableSnapshots(bool yes)
{
    bTreeView->set_sensitive(yes);
}


bool History::onPressEvent(GdkEventButton *event)
{
    shapshot_update_ = event->button == 3;
    if (shapshot_update_) {
        ConnectionBlocker block_sel(selchangebm);
        bTreeView->get_selection()->unselect_all();
    }

    return false;
}


bool History::confirmBookmarkUpdate()
{
    Gtk::Popover p(*bTreeView);
    Gtk::HBox hb;
    Gtk::VBox vb;
    p.set_border_width(16);
    p.add(vb);
    Gtk::Label lbl(M("HISTORY_SNAPSHOT_CONFIRM_UPDATE"));
    Gtk::Label space("");
    vb.pack_start(lbl);
    hb.pack_start(space, Gtk::PACK_EXPAND_WIDGET);
    Gtk::Button ok(M("GENERAL_OK"));
    Gtk::Button cancel(M("GENERAL_CANCEL"));
    hb.pack_start(ok);
    hb.pack_start(cancel);
    vb.pack_start(hb);

    int result = 0;

    ok.signal_clicked().connect(sigc::slot<void>([&](){ result = 1; p.hide(); }));
    cancel.signal_clicked().connect(sigc::slot<void>([&](){ result = -1; p.hide(); }));
    p.signal_closed().connect(sigc::slot<void>([&](){ if (!result) result = -1; }));

    p.show_all_children();
    p.set_modal(true);
    p.show();
    //p.popup();

    while (result == 0) {
        gtk_main_iteration();
    }

    return result == 1;
}


void History::setBeforeAfterLock(bool yes)
{
    blistenerLock = yes;
    if (blistener && !blistenerLock) {
        Glib::RefPtr<Gtk::TreeSelection> selection = hTreeView->get_selection();
        Gtk::TreeModel::iterator iter = selection->get_selected();
        if (iter) {
            blistener->historyBeforeAfterChanged(iter->get_value(historyColumns.params));
        }
    }
}


void History::activateNextSnapshot()
{
    auto children = bookmarkModel->children();
    if (children.size() == 0) {
        return;
    }
    
    Glib::RefPtr<Gtk::TreeSelection> selection = bTreeView->get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();

    if (!iter) {
        selection->select(children.begin());
        return;
    }
    ++iter;
    if (iter == children.end()) {
        selection->select(children.begin());
    } else {
        selection->select(iter);
    }
}


void History::activatePrevSnapshot()
{
    auto children = bookmarkModel->children();
    if (children.size() == 0) {
        return;
    }
    
    Glib::RefPtr<Gtk::TreeSelection> selection = bTreeView->get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();

    Gtk::TreeModel::iterator last = children.begin();
    for (int i = 1; i < children.size(); ++i) {
        ++last;
    }

    if (!iter) {
        selection->select(children.begin());
    } else if (iter == children.begin()) {
        selection->select(last);
    } else {
        --iter;
        selection->select(iter);
    }
}
