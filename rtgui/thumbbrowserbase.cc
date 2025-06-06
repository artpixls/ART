/*
 *  This file is part of RawTherapee.
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
#include <numeric>
#include <iostream>

#include <glibmm.h>

#include "multilangmgr.h"
#include "options.h"
#include "thumbbrowserbase.h"

#include "../rtengine/mytime.h"
#include "../rtengine/rt_math.h"

ThumbBrowserBase::ThumbBrowserBase ()
    : location(THLOC_FILEBROWSER), inspector(nullptr), isInspectorActive(false), eventTime(0), lastClicked(nullptr), anchor(nullptr), previewHeight(options.thumbSize), numOfCols(1), arrangement(TB_Horizontal),
      use_hscroll_(true),
      use_vscroll_(true)
{
    inW = -1;
    inH = -1;

    setExpandAlignProperties(&internal, true, true, Gtk::ALIGN_FILL, Gtk::ALIGN_FILL);
    setExpandAlignProperties(&hscroll, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);
    setExpandAlignProperties(&vscroll, false, true, Gtk::ALIGN_CENTER, Gtk::ALIGN_FILL);
    attach (internal, 0, 0, 1, 1);
    attach (vscroll, 1, 0, 1, 1);
    attach (hscroll, 0, 1, 1, 1);

    internal.setParent (this);
    internal.set_can_focus(true);

    show_all ();

    vscroll.signal_value_changed().connect( sigc::mem_fun(*this, &ThumbBrowserBase::scrollChanged) );
    hscroll.signal_value_changed().connect( sigc::mem_fun(*this, &ThumbBrowserBase::scrollChanged) );

    internal.signal_size_allocate().connect( sigc::mem_fun(*this, &ThumbBrowserBase::internalAreaResized) );
}

void ThumbBrowserBase::scrollChanged ()
{
    {
        MYWRITERLOCK(l, entryRW);

        double dx, dy;
        getScrollPosition(dx, dy);
        int x = dx, y = dy;

        for (size_t i = 0; i < fd.size(); i++) {
            fd[i]->setOffset(x, y);
        }
    }

    // internal.setPosition ((int)(hscroll.get_value()), (int)(vscroll.get_value()));

    if (!internal.isDirty()) {
        internal.setDirty ();
        internal.queue_draw ();
    }
}

void ThumbBrowserBase::scroll (int direction, double deltaX, double deltaY)
{
    double delta = 0.0;
    if (abs(deltaX) > abs(deltaY)) {
        delta = deltaX;
    } else {
        delta = deltaY;
    }
    if (direction == GDK_SCROLL_SMOOTH && delta == 0.0) {
        // sometimes this case happens. To avoid scrolling the wrong direction in this case, we just do nothing
        // This is probably no longer necessary now that coef is no longer quantized to +/-1.0 but why waste CPU cycles?
        return;
    }
    //GDK_SCROLL_SMOOTH can come in as many events with small deltas, don't quantize these to +/-1.0 so trackpads work well
    double coef;
    if(direction == GDK_SCROLL_SMOOTH) {
        coef = delta;
    } else if (direction == GDK_SCROLL_DOWN) {
        coef = +1.0;
    } else {
        coef = -1.0;
    }

    // GUI already acquired when here
    if (direction == GDK_SCROLL_UP || direction == GDK_SCROLL_DOWN || direction == GDK_SCROLL_SMOOTH) {
        if (arrangement == TB_Vertical) {
            double currValue = vscroll.get_value();
            double newValue = rtengine::LIM<double>(currValue + coef * vscroll.get_adjustment()->get_step_increment(),
                                                    vscroll.get_adjustment()->get_lower (),
                                                    vscroll.get_adjustment()->get_upper());
            if (newValue != currValue) {
                vscroll.set_value (newValue);
            }
        } else {
            double currValue = hscroll.get_value();
            double newValue = rtengine::LIM<double>(currValue + coef * hscroll.get_adjustment()->get_step_increment(),
                                                    hscroll.get_adjustment()->get_lower(),
                                                    hscroll.get_adjustment()->get_upper());
            if (newValue != currValue) {
                hscroll.set_value (newValue);
            }
        }
    }
}

void ThumbBrowserBase::scrollPage (int direction)
{
    // GUI already acquired when here
    // GUI already acquired when here
    if (direction == GDK_SCROLL_UP || direction == GDK_SCROLL_DOWN) {
        if (arrangement == TB_Vertical) {
            double currValue = vscroll.get_value();
            double newValue = rtengine::LIM<double>(currValue + (direction == GDK_SCROLL_DOWN ? +1 : -1) * vscroll.get_adjustment()->get_page_increment(),
                                                    vscroll.get_adjustment()->get_lower(),
                                                    vscroll.get_adjustment()->get_upper());
            if (newValue != currValue) {
                vscroll.set_value (newValue);
            }
        } else {
            double currValue = hscroll.get_value();
            double newValue = rtengine::LIM<double>(currValue + (direction == GDK_SCROLL_DOWN ? +1 : -1) * hscroll.get_adjustment()->get_page_increment(),
                                                    hscroll.get_adjustment()->get_lower(),
                                                    hscroll.get_adjustment()->get_upper());
            if (newValue != currValue) {
                hscroll.set_value (newValue);
            }
        }
    }
}

namespace
{

typedef std::vector<ThumbBrowserEntryBase*> ThumbVector;
typedef ThumbVector::iterator ThumbIterator;

inline void clearSelection (ThumbVector& selected)
{
    for (ThumbIterator thumb = selected.begin (); thumb != selected.end (); ++thumb)
        (*thumb)->selected = false;

    selected.clear ();
}

inline void addToSelection (ThumbBrowserEntryBase* entry, ThumbVector& selected)
{
    if (entry->selected || entry->filtered)
        return;

    entry->selected = true;
    selected.push_back (entry);
}

inline void removeFromSelection (const ThumbIterator& iterator, ThumbVector& selected)
{
    (*iterator)->selected = false;
    selected.erase (iterator);
}

}

void ThumbBrowserBase::selectSingle (ThumbBrowserEntryBase* clicked)
{
    clearSelection(selected);
    anchor = clicked;

    if (clicked) {
        addToSelection(clicked, selected);
    }
}

void ThumbBrowserBase::selectRange (ThumbBrowserEntryBase* clicked, bool additional)
{
    if (!anchor) {
        anchor = clicked;
        if (selected.empty()) {
            addToSelection(clicked, selected);
            return;
        }
    }

    if (!additional || !lastClicked) {
        // Extend the current range w.r.t to first selected entry.
        ThumbIterator back = std::find(fd.begin(), fd.end(), clicked);
        ThumbIterator front = anchor == clicked ? back : std::find(fd.begin(), fd.end(), anchor);

        if (front > back) {
            std::swap(front, back);
        }

        clearSelection(selected);

        for (; front <= back && front != fd.end(); ++front) {
            addToSelection(*front, selected);
        }
    } else {
        // Add an additional range w.r.t. the last clicked entry.
        ThumbIterator last = std::find(fd.begin(), fd.end(), lastClicked);
        ThumbIterator current = std::find(fd.begin(), fd.end(), clicked);

        if (last > current) {
            std::swap(last, current);
        }

        for (; last <= current && last != fd.end(); ++last) {
            addToSelection(*last, selected);
        }
    }
}

void ThumbBrowserBase::selectSet (ThumbBrowserEntryBase* clicked)
{
    const ThumbIterator iterator = std::find(selected.begin(), selected.end(), clicked);

    if (iterator != selected.end()) {
        removeFromSelection(iterator, selected);
    } else {
        addToSelection(clicked, selected);
    }
    anchor = clicked;
}

static void scrollToEntry (double& h, double& v, int iw, int ih, ThumbBrowserEntryBase* entry)
{
    const int hmin = entry->getX ();
    const int hmax = hmin + entry->getEffectiveWidth () - iw;
    const int vmin = entry->getY ();
    const int vmax = vmin + entry->getEffectiveHeight () - ih;

    if (hmin < 0) {
        h += hmin;
    } else if (hmax > 0) {
        h += hmax;
    }

    if(vmin < 0) {
        v += vmin;
    } else if (vmax > 0) {
        v += vmax;
    }
}

void ThumbBrowserBase::selectPrev (int distance, bool enlarge)
{
    double h, v;
    getScrollPosition (h, v);

    {
        MYWRITERLOCK(l, entryRW);

        if (!selected.empty ()) {
            std::vector<ThumbBrowserEntryBase*>::iterator front = std::find (fd.begin (), fd.end (), selected.front ());
            std::vector<ThumbBrowserEntryBase*>::iterator back = std::find (fd.begin (), fd.end (), selected.back ());
            std::vector<ThumbBrowserEntryBase*>::iterator last = std::find (fd.begin (), fd.end (), lastClicked);

            if (front > back) {
                std::swap(front, back);
            }

            std::vector<ThumbBrowserEntryBase*>::iterator& curr = last == front ? front : back;

            // find next thumbnail at filtered distance before current
            for (; curr >= fd.begin (); --curr) {
                if (!(*curr)->filtered) {
                    if (distance-- == 0) {
                        // clear current selection
                        for (size_t i = 0; i < selected.size (); ++i) {
                            selected[i]->selected = false;
                            redrawEntryNeeded (selected[i]);
                        }

                        selected.clear ();

                        // make sure the newly selected thumbnail is visible and make it current
                        scrollToEntry (h, v, internal.get_width (), internal.get_height (), *curr);
                        lastClicked = *curr;

                        // either enlarge current selection or set new selection
                        if(enlarge) {
                            // reverse direction if distance is too large
                            if(front > back) {
                                std::swap(front, back);
                            }

                            for (; front <= back; ++front) {
                                if (!(*front)->filtered) {
                                    (*front)->selected = true;
                                    redrawEntryNeeded (*front);
                                    selected.push_back (*front);
                                }
                            }
                        } else {
                            (*curr)->selected = true;
                            redrawEntryNeeded (*curr);
                            selected.push_back (*curr);
                        }

                        break;
                    }
                }
            }
        }

        MYWRITERLOCK_RELEASE(l);
        selectionChanged ();
    }

    setScrollPosition (h, v);
}

void ThumbBrowserBase::selectNext (int distance, bool enlarge)
{
    double h, v;
    getScrollPosition (h, v);

    {
        MYWRITERLOCK(l, entryRW);

        if (!selected.empty ()) {
            std::vector<ThumbBrowserEntryBase*>::iterator front = std::find (fd.begin (), fd.end (), selected.front ());
            std::vector<ThumbBrowserEntryBase*>::iterator back = std::find (fd.begin (), fd.end (), selected.back ());
            std::vector<ThumbBrowserEntryBase*>::iterator last = std::find (fd.begin (), fd.end (), lastClicked);

            if (front > back) {
                std::swap(front, back);
            }

            std::vector<ThumbBrowserEntryBase*>::iterator& curr = last == back ? back : front;

            // find next thumbnail at filtered distance after current
            for (; curr < fd.end (); ++curr) {
                if (!(*curr)->filtered) {
                    if (distance-- == 0) {
                        // clear current selection
                        for (size_t i = 0; i < selected.size (); ++i) {
                            selected[i]->selected = false;
                            redrawEntryNeeded (selected[i]);
                        }

                        selected.clear ();

                        // make sure the newly selected thumbnail is visible and make it current
                        scrollToEntry (h, v, internal.get_width (), internal.get_height (), *curr);
                        lastClicked = *curr;

                        // either enlarge current selection or set new selection
                        if(enlarge) {
                            // reverse direction if distance is too large
                            if(front > back) {
                                std::swap(front, back);
                            }

                            for (; front <= back && front != fd.end(); ++front) {
                                if (!(*front)->filtered) {
                                    (*front)->selected = true;
                                    redrawEntryNeeded (*front);
                                    selected.push_back (*front);
                                }
                            }
                        } else {
                            (*curr)->selected = true;
                            redrawEntryNeeded (*curr);
                            selected.push_back (*curr);
                        }

                        break;
                    }
                }
            }
        }

        MYWRITERLOCK_RELEASE(l);
        selectionChanged ();
    }

    setScrollPosition (h, v);
}

void ThumbBrowserBase::selectFirst (bool enlarge)
{
    double h, v;
    getScrollPosition (h, v);

    {
        MYWRITERLOCK(l, entryRW);

        if (!fd.empty ()) {
            // find first unfiltered entry
            std::vector<ThumbBrowserEntryBase*>::iterator first = fd.begin ();

            for (; first < fd.end (); ++first) {
                if (!(*first)->filtered) {
                    break;
                }
            }

            if (first == fd.end()) {
                return;
            }

            scrollToEntry (h, v, internal.get_width (), internal.get_height (), *first);

            ThumbBrowserEntryBase* lastEntry = lastClicked;
            lastClicked = *first;

            if(selected.empty ()) {
                (*first)->selected = true;
                redrawEntryNeeded (*first);
                selected.push_back (*first);
            } else {
                std::vector<ThumbBrowserEntryBase*>::iterator back = std::find (fd.begin (), fd.end (), lastEntry ? lastEntry : selected.back ());

                if (first > back) {
                    std::swap(first, back);
                }

                // clear current selection
                for (size_t i = 0; i < selected.size (); ++i) {
                    selected[i]->selected = false;
                    redrawEntryNeeded (selected[i]);
                }

                selected.clear ();

                // either enlarge current selection or set new selection
                for (; first <= back; ++first) {
                    if (!(*first)->filtered) {
                        (*first)->selected = true;
                        redrawEntryNeeded (*first);
                        selected.push_back (*first);
                    }

                    if (!enlarge) {
                        break;
                    }
                }
            }
        }

        MYWRITERLOCK_RELEASE(l);
        selectionChanged ();
    }

    setScrollPosition (h, v);
}

void ThumbBrowserBase::selectLast (bool enlarge)
{
    double h, v;
    getScrollPosition (h, v);

    {
        MYWRITERLOCK(l, entryRW);

        if (!fd.empty ()) {
            // find last unfiltered entry
            std::vector<ThumbBrowserEntryBase*>::iterator last = fd.end () - 1;

            for (; last >= fd.begin (); --last) {
                if (!(*last)->filtered) {
                    break;
                }
            }

            scrollToEntry (h, v, internal.get_width (), internal.get_height (), *last);

            ThumbBrowserEntryBase* lastEntry = lastClicked;
            lastClicked = *last;

            if(selected.empty()) {
                (*last)->selected = true;
                redrawEntryNeeded (*last);
                selected.push_back (*last);
            } else {
                std::vector<ThumbBrowserEntryBase*>::iterator front = std::find (fd.begin (), fd.end (), lastEntry ? lastEntry : selected.front ());

                if (last < front) {
                    std::swap(last, front);
                }

                // clear current selection
                for (size_t i = 0; i < selected.size (); ++i) {
                    selected[i]->selected = false;
                    redrawEntryNeeded (selected[i]);
                }

                selected.clear ();

                // either enlarge current selection or set new selection
                for (; front <= last; --last) {
                    if (!(*last)->filtered) {
                        (*last)->selected = true;
                        redrawEntryNeeded (*last);
                        selected.push_back (*last);
                    }

                    if (!enlarge) {
                        break;
                    }
                }

                std::reverse(selected.begin (), selected.end ());
            }
        }

        MYWRITERLOCK_RELEASE(l);
        selectionChanged ();
    }

    setScrollPosition (h, v);
}


void ThumbBrowserBase::selectEntry(const ThumbBrowserEntryBase *entry)
{
    {
        MYWRITERLOCK(l, entryRW);
        selectSingle(const_cast<ThumbBrowserEntryBase *>(entry));
        MYWRITERLOCK_RELEASE(l);
    }
    selectionChanged();
    redraw();
}


void ThumbBrowserBase::resizeThumbnailArea (int w, int h)
{

    inW = w;
    inH = h;

    if (hscroll.get_value() + internal.get_width() > inW) {
        hscroll.set_value (inW - internal.get_width());
    }

    if (vscroll.get_value() + internal.get_height() > inH) {
        vscroll.set_value (inH - internal.get_height());
    }

    configScrollBars ();
}

void ThumbBrowserBase::internalAreaResized (Gtk::Allocation& req)
{

    if (inW > 0 && inH > 0) {
        configScrollBars ();
        redraw ();
    }
}

void ThumbBrowserBase::configScrollBars ()
{

    // HOMBRE:DELETE ME?
    GThreadLock tLock; // Acquire the GUI

    if (inW > 0 && inH > 0) {
        int ih = internal.get_height();
        if (arrangement == TB_Horizontal) {
            auto ha = hscroll.get_adjustment();
            int iw = internal.get_width();
            ha->set_upper(inW);
            ha->set_lower(0);
            ha->set_step_increment(!fd.empty() ? fd[0]->getEffectiveWidth() : 0);
            ha->set_page_increment(iw);
            ha->set_page_size(iw);
            if (iw >= inW) {
                hscroll.hide();
                use_hscroll_ = false;
            } else {
                hscroll.show();
                use_hscroll_ = true;
            }
        } else {
            hscroll.hide();
            use_hscroll_ = false;
            // int px, py;
            // internal.getPosition(px, py);
            // if (px > 0) {
            //     internal.setPosition(0, py);
            //     queue_draw();
            // }
        }

        auto va = vscroll.get_adjustment();
        va->set_upper(inH);
        va->set_lower(0);
        const auto height = !fd.empty() ? fd[0]->getEffectiveHeight() : 0;
        va->set_step_increment(height);
        va->set_page_increment(height == 0 ? ih : (ih / height) * height);
        va->set_page_size(ih);

        if (ih >= inH) {
            vscroll.hide();
            use_vscroll_ = false;
        } else {
            vscroll.show();
            use_vscroll_ = true;
        }

        scrollChanged();
    }
}

void ThumbBrowserBase::arrangeFiles(bool checkfilter)
{
    MYREADERLOCK(l, entryRW);

    // GUI already locked by ::redraw, the only caller of this method for now.
    // We could lock it one more time, there's no harm excepted (negligible) speed penalty
    //GThreadLock lock;

    int rowHeight = 0;
    for (const auto entry : fd) {
        if (checkfilter) {
            // apply filter
            entry->filtered = !checkFilter(entry);
        }

        // compute size of the items
        if (!entry->filtered) {
            rowHeight = std::max(entry->getMinimalHeight(), rowHeight);
        }
    }
    if (arrangement == TB_Horizontal) {
        numOfCols = 1;

        int currx = 0;

        for (unsigned int ct = 0; ct < fd.size(); ++ct) {
            // arrange items in the column
            int curry = 0;

            for (; ct < fd.size() && fd[ct]->filtered; ++ct) {
                fd[ct]->drawable = false;
            }

            if (ct < fd.size()) {
                const int maxw = fd[ct]->getMinimalWidth();

                fd[ct]->setPosition(currx, curry, maxw, rowHeight);
                fd[ct]->drawable = true;
                currx += maxw;
                curry += rowHeight;
            }
        }

        MYREADERLOCK_RELEASE(l);
        // This will require a Writer access
        resizeThumbnailArea(currx, !fd.empty() ? fd[0]->getEffectiveHeight() : rowHeight);
    } else {
        const int availWidth = internal.get_width();

        // initial number of columns
        numOfCols = 0;
        int colsWidth = 0;

        for (unsigned int i = 0; i < fd.size(); ++i) {
            if (!fd[i]->filtered && colsWidth + fd[i]->getMinimalWidth() <= availWidth) {
                colsWidth += fd[numOfCols]->getMinimalWidth();
                ++numOfCols;
                if(colsWidth > availWidth) {
                    break;
                }
            }
        }

        if (numOfCols < 1) {
            numOfCols = 1;
        }

        std::vector<int> colWidths;

        for (; numOfCols > 0; --numOfCols) {
            // compute column widths
            colWidths.assign(numOfCols, 0);

            for (unsigned int i = 0, j = 0; i < fd.size(); ++i) {
                if (!fd[i]->filtered && fd[i]->getMinimalWidth() > colWidths[j % numOfCols]) {
                    colWidths[j % numOfCols] = fd[i]->getMinimalWidth();
                }

                if (!fd[i]->filtered) {
                    ++j;
                }
            }

            // if not wider than the space available, arrange it and we are ready
            colsWidth = std::accumulate(colWidths.begin(), colWidths.end(), 0);

            if (numOfCols == 1 || colsWidth < availWidth) {
                break;
            }
        }

        // arrange files
        int curry = 0;

        for (unsigned int ct = 0; ct < fd.size();) {
            // arrange items in the row
            int currx = 0;

            for (int i = 0; ct < fd.size() && i < numOfCols; ++i, ++ct) {
                for (; ct < fd.size() && fd[ct]->filtered; ++ct) {
                    fd[ct]->drawable = false;
                }

                if (ct < fd.size()) {
                    fd[ct]->setPosition(currx, curry, colWidths[i], rowHeight);
                    fd[ct]->drawable = true;
                    currx += colWidths[i];
                }
            }

            if (currx > 0) { // there were thumbnails placed in the row
                curry += rowHeight;
            }
        }

        MYREADERLOCK_RELEASE(l);
        // This will require a Writer access
        resizeThumbnailArea (colsWidth, curry);
    }
}

void ThumbBrowserBase::disableInspector()
{
    if (inspector) {
        inspector->setActive(false);
    }
}

void ThumbBrowserBase::enableInspector()
{
    if (inspector) {
        inspector->setActive(true);
    }
}

bool ThumbBrowserBase::Internal::on_configure_event(GdkEventConfigure *configure_event)
{
    return true;
}

void ThumbBrowserBase::Internal::on_style_updated()
{
    style = get_style_context ();
    textn = style->get_color(Gtk::STATE_FLAG_NORMAL);
    texts = style->get_color(Gtk::STATE_FLAG_SELECTED);
    bgn = style->get_background_color(Gtk::STATE_FLAG_NORMAL);
    bgs = style->get_background_color(Gtk::STATE_FLAG_SELECTED);
    bgp = style->get_background_color(Gtk::STATE_FLAG_PRELIGHT);
    hl = style->get_color(Gtk::STATE_FLAG_ACTIVE);
}

void ThumbBrowserBase::Internal::on_realize()
{
    // Gtk signals automatically acquire the GUI (i.e. this method is enclosed by gdk_thread_enter and gdk_thread_leave)
    Cairo::FontOptions cfo;
    cfo.set_antialias (Cairo::ANTIALIAS_SUBPIXEL);
    get_pango_context()->set_cairo_font_options (cfo);

    Gtk::DrawingArea::on_realize();

    style = get_style_context ();
    textn = style->get_color(Gtk::STATE_FLAG_NORMAL);
    texts = style->get_color(Gtk::STATE_FLAG_SELECTED);
    bgn = style->get_background_color(Gtk::STATE_FLAG_NORMAL);
    bgs = style->get_background_color(Gtk::STATE_FLAG_SELECTED);
    bgp = style->get_background_color(Gtk::STATE_FLAG_PRELIGHT);
    hl = style->get_color(Gtk::STATE_FLAG_ACTIVE);

    set_can_focus(true);
    add_events(Gdk::EXPOSURE_MASK | Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::POINTER_MOTION_MASK | Gdk::SCROLL_MASK | Gdk::SMOOTH_SCROLL_MASK | Gdk::KEY_PRESS_MASK);
    set_has_tooltip (true);
    signal_query_tooltip().connect( sigc::mem_fun(*this, &ThumbBrowserBase::Internal::on_query_tooltip) );
}

bool ThumbBrowserBase::Internal::on_query_tooltip (int x, int y, bool keyboard_tooltip, const Glib::RefPtr<Gtk::Tooltip>& tooltip)
{
    // Gtk signals automatically acquire the GUI (i.e. this method is enclosed by gdk_thread_enter and gdk_thread_leave)
    Glib::ustring ttip = "";

    {
        MYREADERLOCK(l, parent->entryRW);

        for (size_t i = 0; i < parent->fd.size(); i++)
            if (parent->fd[i]->drawable && parent->fd[i]->inside (x, y)) {
                ttip = parent->fd[i]->getToolTip (x, y);
                break;
            }
    }

    if (ttip != "") {
        tooltip->set_markup(ttip);
        return true;
    } else {
        return false;
    }
}

void ThumbBrowserBase::on_style_updated ()
{
    // GUI will be acquired by refreshThumbImages
    refreshThumbImages ();
}

ThumbBrowserBase::Internal::Internal () : //ofsX(0), ofsY(0),
                                          parent(nullptr), dirty(true)
{
    Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
    set_name("FileCatalog");
}

void ThumbBrowserBase::Internal::setParent (ThumbBrowserBase* p)
{
    parent = p;
}

// void ThumbBrowserBase::Internal::setPosition (int x, int y)
// {
//     ofsX = x;
//     ofsY = y;
// }


// void ThumbBrowserBase::Internal::getPosition(int &x, int &y)
// {
//     x = ofsX;
//     y = ofsY;
// }


bool ThumbBrowserBase::Internal::on_key_press_event (GdkEventKey* event)
{
    // Gtk signals automatically acquire the GUI (i.e. this method is enclosed by gdk_thread_enter and gdk_thread_leave)
    return parent->keyPressed(event);
}

bool ThumbBrowserBase::Internal::on_button_press_event (GdkEventButton* event)
{
    // Gtk signals automatically acquire the GUI (i.e. this method is enclosed by gdk_thread_enter and gdk_thread_leave)
    grab_focus ();

    parent->eventTime = event->time;

    parent->buttonPressed ((int)event->x, (int)event->y, event->button, event->type, event->state, 0, 0, get_width(), get_height());
    Glib::RefPtr<Gdk::Window> window = get_window();

    GdkRectangle rect;
    rect.x = 0;
    rect.y = 0;
    rect.width = window->get_width();
    rect.height = window->get_height();

    gdk_window_invalidate_rect (window->gobj(), &rect, true);
    gdk_window_process_updates (window->gobj(), true);

    return true;
}

void ThumbBrowserBase::buttonPressed (int x, int y, int button, GdkEventType type, int state, int clx, int cly, int clw, int clh)
{
    // GUI already acquired

    ThumbBrowserEntryBase* fileDescr = nullptr;
    bool handled = false;

    {
        MYREADERLOCK(l, entryRW);

        for (size_t i = 0; i < fd.size(); i++)
            if (fd[i]->drawable) {
                if (fd[i]->inside (x, y) && fd[i]->insideWindow (clx, cly, clw, clh)) {
                    fileDescr = fd[i];
                }

                bool b = fd[i]->pressNotify (button, type, state, x, y);
                handled = handled || b;
            }
    }

    if (handled || (fileDescr && fileDescr->processing)) {
        return;
    }

    {
        MYWRITERLOCK(l, entryRW);

        if (selected.size() == 1 && type == GDK_2BUTTON_PRESS && button == 1) {
            doubleClicked (selected[0]);
        } else if (button == 1 && type == GDK_BUTTON_PRESS) {
            if (fileDescr && (state & GDK_SHIFT_MASK))
                selectRange (fileDescr, state & GDK_CONTROL_MASK);
            else if (fileDescr && (state & GDK_CONTROL_MASK))
                selectSet (fileDescr);
            else
                selectSingle (fileDescr);

            lastClicked = fileDescr;
            MYWRITERLOCK_RELEASE(l);
            selectionChanged ();
        } else if (fileDescr && button == 3 && type == GDK_BUTTON_PRESS) {
            if (!fileDescr->selected) {
                selectSingle (fileDescr);

                lastClicked = fileDescr;
                MYWRITERLOCK_RELEASE(l);
                selectionChanged ();
            }

            MYWRITERLOCK_RELEASE(l);
            rightClicked (fileDescr);
        }
    } // end of MYWRITERLOCK(l, entryRW);

}

bool ThumbBrowserBase::Internal::on_draw(const ::Cairo::RefPtr< Cairo::Context> &cr)
{
    // Gtk signals automatically acquire the GUI (i.e. this method is enclosed by gdk_thread_enter and gdk_thread_leave)

    dirty = false;

    int w = get_width();
    int h = get_height();

    // draw thumbnails

    cr->set_antialias(Cairo::ANTIALIAS_NONE);
    cr->set_line_join(Cairo::LINE_JOIN_MITER);
    style->render_background(cr, 0., 0., w, h);
    Glib::RefPtr<Pango::Context> context = get_pango_context ();
    context->set_font_description (style->get_font());

    // std::cout << "\nREDRAWING\n" << std::endl;

    {
        MYWRITERLOCK(l, parent->entryRW);

        for (size_t i = 0; i < parent->fd.size() && !dirty; i++) { // if dirty meanwhile, cancel and wait for next redraw
            if (!parent->fd[i]->drawable || !parent->fd[i]->insideWindow (0, 0, w, h)) {
                parent->fd[i]->updatepriority = false;
            } else {
                parent->fd[i]->updatepriority = true;
                // if (parent->fd[i]->requestDraw()) {
                //     // to_draw.push_back(parent->fd[i]);
                // } else {
                //     // done = false;
                // }
                parent->fd[i]->draw (cr);
            }
        }
    }
    style->render_frame(cr, 0., 0., w, h);
    

    return true;
}

Gtk::SizeRequestMode ThumbBrowserBase::Internal::get_request_mode_vfunc () const
{
    return Gtk::SIZE_REQUEST_CONSTANT_SIZE;
}

void ThumbBrowserBase::Internal::get_preferred_height_vfunc (int &minimum_height, int &natural_height) const
{
    minimum_height = 20 * RTScalable::getScale();
    natural_height = 80 * RTScalable::getScale();
}

void ThumbBrowserBase::Internal::get_preferred_width_vfunc (int &minimum_width, int &natural_width) const
{
    minimum_width = 200 * RTScalable::getScale();
    natural_width = 1000 * RTScalable::getScale();
}

void ThumbBrowserBase::Internal::get_preferred_height_for_width_vfunc (int width, int &minimum_height, int &natural_height) const
{
    get_preferred_height_vfunc(minimum_height, natural_height);
}

void ThumbBrowserBase::Internal::get_preferred_width_for_height_vfunc (int height, int &minimum_width, int &natural_width) const
{
    get_preferred_width_vfunc (minimum_width, natural_width);
}


bool ThumbBrowserBase::Internal::on_button_release_event (GdkEventButton* event)
{
    // Gtk signals automatically acquire the GUI (i.e. this method is enclosed by gdk_thread_enter and gdk_thread_leave)
    int w = get_width();
    int h = get_height();

    MYREADERLOCK(l, parent->entryRW);

    for (size_t i = 0; i < parent->fd.size(); i++)
        if (parent->fd[i]->drawable && parent->fd[i]->insideWindow (0, 0, w, h)) {
            ThumbBrowserEntryBase* tbe = parent->fd[i];
            MYREADERLOCK_RELEASE(l);
            // This will require a Writer access...
            tbe->releaseNotify (event->button, event->type, event->state, (int)event->x, (int)event->y);
            MYREADERLOCK_ACQUIRE(l);
        }

    return true;
}

bool ThumbBrowserBase::Internal::on_motion_notify_event (GdkEventMotion* event)
{
    // Gtk signals automatically acquire the GUI (i.e. this method is enclosed by gdk_thread_enter and gdk_thread_leave)
    int w = get_width();
    int h = get_height();

    MYREADERLOCK(l, parent->entryRW);

    for (size_t i = 0; i < parent->fd.size(); i++)
        if (parent->fd[i]->drawable && parent->fd[i]->insideWindow (0, 0, w, h)) {
            parent->fd[i]->motionNotify ((int)event->x, (int)event->y);
        }

    return true;
}

bool ThumbBrowserBase::Internal::on_scroll_event (GdkEventScroll* event)
{
    // Gtk signals automatically acquire the GUI (i.e. this method is enclosed by gdk_thread_enter and gdk_thread_leave)
    parent->scroll (event->direction, event->delta_x, event->delta_y);
    return true;
}


void ThumbBrowserBase::redraw (bool checkfilter)
{

    GThreadLock lock;
    arrangeFiles(checkfilter);
    queue_draw();
}

void ThumbBrowserBase::zoomChanged (bool zoomIn)
{

    int newHeight = 0;
    int optThumbSize = getThumbnailHeight();

    if (zoomIn)
        for (size_t i = 0; i < options.thumbnailZoomRatios.size(); i++) {
            newHeight = (int)(options.thumbnailZoomRatios[i] * getMaxThumbnailHeight());

            if (newHeight > optThumbSize) {
                break;
            }
        }
    else
        for (size_t i = options.thumbnailZoomRatios.size() - 1; i > 0; i--) {
            newHeight = (int)(options.thumbnailZoomRatios[i] * getMaxThumbnailHeight());

            if (newHeight < optThumbSize) {
                break;
            }
        }

    previewHeight = newHeight;

    saveThumbnailHeight(newHeight);

    {
        MYWRITERLOCK(l, entryRW);

        for (size_t i = 0; i < fd.size(); i++) {
            fd[i]->resize(previewHeight);
        }
    }

    redraw ();
}

void ThumbBrowserBase::refreshThumbImages ()
{

    int previewHeight = getThumbnailHeight();
    {
        MYWRITERLOCK(l, entryRW);

        for (size_t i = 0; i < fd.size(); i++) {
            fd[i]->resize (previewHeight);
        }
    }

    redraw ();
}

void ThumbBrowserBase::refreshQuickThumbImages ()
{
    MYWRITERLOCK(l, entryRW);

    for (size_t i = 0; i < fd.size(); ++i) {
        fd[i]->refreshQuickThumbnailImage ();
    }
}

void ThumbBrowserBase::refreshEditedState (const std::set<Glib::ustring>& efiles)
{

    editedFiles = efiles;
    {
        MYREADERLOCK(l, entryRW);

        for (size_t i = 0; i < fd.size(); i++) {
            fd[i]->framed = editedFiles.find (fd[i]->filename) != editedFiles.end();
        }
    }

    queue_draw ();
}

void ThumbBrowserBase::setArrangement (Arrangement a)
{

    arrangement = a;
    redraw ();
}

void ThumbBrowserBase::enableTabMode(bool enable)
{
    location = enable ? THLOC_EDITOR : THLOC_FILEBROWSER;
    arrangement = enable ? ThumbBrowserBase::TB_Horizontal : ThumbBrowserBase::TB_Vertical;

    if ((!options.sameThumbSize && (options.thumbSizeTab != options.thumbSize)) || (options.showFileNames || options.filmStripShowFileNames)) {

        MYWRITERLOCK(l, entryRW);

        for (size_t i = 0; i < fd.size(); i++) {
            fd[i]->resize (getThumbnailHeight());
        }
    }

    redraw ();

    // Scroll to selected position if going into ribbon mode or back
    // Tab mode is horizontal, file browser is vertical
    {
        MYREADERLOCK(l, entryRW);

        if (!selected.empty()) {
            if (enable) {
                double h = selected[0]->getStartX();
                MYREADERLOCK_RELEASE(l);
                hscroll.set_value (std::min(h, hscroll.get_adjustment()->get_upper()));
            } else {
                double v = selected[0]->getStartY();
                MYREADERLOCK_RELEASE(l);
                vscroll.set_value (std::min(v, vscroll.get_adjustment()->get_upper()));
            }
        }
    }
}

void ThumbBrowserBase::initEntry (ThumbBrowserEntryBase* entry)
{
    double x, y;
    getScrollPosition(x, y);
    entry->setOffset(x, y);
}

void ThumbBrowserBase::getScrollPosition (double& h, double& v)
{
    h = use_hscroll_ ? hscroll.get_value() : 0;
    v = use_vscroll_ ? vscroll.get_value() : 0;
}

void ThumbBrowserBase::setScrollPosition (double h, double v)
{
    if (use_hscroll_) {
        hscroll.set_value(h > hscroll.get_adjustment()->get_upper() ? hscroll.get_adjustment()->get_upper() : h);
    }
    if (use_vscroll_) {
        vscroll.set_value (v > vscroll.get_adjustment()->get_upper() ? vscroll.get_adjustment()->get_upper() : v);
    }
}

// needed for auto-height in single tab
int ThumbBrowserBase::getEffectiveHeight()
{
    int h = hscroll.get_height() + 2; // have 2 pixels rounding error for scroll bars to appear

    MYREADERLOCK(l, entryRW);

    // Filtered items do not change in size, so take a non-filtered
    for (size_t i = 0; i < fd.size(); i++)
        if (!fd[i]->filtered) {
            h += fd[i]->getEffectiveHeight();
            break;
        }

    return h;
}

void ThumbBrowserBase::redrawEntryNeeded(ThumbBrowserEntryBase* entry)
{

    // HOMBRE:DELETE ME?
    GThreadLock tLock; // Acquire the GUI

    if (entry->insideWindow (0, 0, internal.get_width(), internal.get_height())) {
        // std::cout << "REDRAW NEEDED: " << entry->shortname << std::endl;
        
        if (!internal.isDirty ()) {
            // std::cout << "   QUEUING" << std::endl;
            internal.setDirty ();
            internal.queue_draw ();
        }
    }
}


void ThumbBrowserBase::getFocus()
{
    internal.grab_focus();
    if (!fd.empty() && selected.empty()) {
        selectFirst(false);
    }
}
