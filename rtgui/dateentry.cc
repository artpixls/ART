/** -*- C++ -*-
 *  
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2020 Alberto Griggio <alberto.griggio@gmail.com>
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

#include "dateentry.h"
#include "rtimage.h"
#include "options.h"
#include <time.h>
#include <iostream>
#include <iomanip>
#include <sstream>


DateEntry::DateEntry(): Gtk::HBox()
{
    entry_ = Gtk::manage(new Gtk::Entry());
    entry_->property_width_chars() = 1;
    entry_->property_xalign() = 1;
    entry_->add_events(Gdk::FOCUS_CHANGE_MASK);
    pack_start(*entry_, Gtk::PACK_EXPAND_WIDGET, 0);
    pack_start(*Gtk::manage(button_ = new Gtk::Button()), 0, 0);
    button_->add(*Gtk::manage(new RTImage("expander-open-small.svg")));
    button_->add_events(Gdk::BUTTON_PRESS_MASK);
    button_->signal_button_press_event().connect_notify(sigc::mem_fun(this, &DateEntry::on_button));
    entry_->signal_activate().connect(sigc::mem_fun(this, &DateEntry::on_enter));
    const auto on_focus_out =
        [this](GdkEventFocus *) -> bool
        {
            on_enter();
            return false;
        };
    entry_->signal_focus_out_event().connect(sigc::slot<bool, GdkEventFocus *>(on_focus_out));
    dialog_ = nullptr;
    calendar_ = nullptr;
}


DateEntry::~DateEntry()
{
    if (dialog_) {
        delete dialog_;
    }
}


void DateEntry::on_button(const GdkEventButton *evt)
{ 
    int pos_x = evt->x_root - evt->x;
    int pos_y = evt->y_root - evt->y;

    auto ea = entry_->get_allocation();
    pos_x -= ea.get_width();
    pos_y += ea.get_height();

    int x_win = pos_x;
    int y_win = pos_y;

    auto top = get_toplevel();
    Gtk::Window *win = top ? dynamic_cast<Gtk::Window *>(top) : nullptr;
    dialog_ = new Gtk::Dialog("", Gtk::DIALOG_MODAL);
    if (win) {
        dialog_->set_transient_for(*win);
    }
    dialog_->property_skip_taskbar_hint() = true;
    dialog_->property_skip_pager_hint() = true;
    dialog_->set_decorated(false);

    dialog_->move(x_win, y_win);

    calendar_ = Gtk::manage(new Gtk::Calendar());
    dialog_->get_vbox()->pack_start(*calendar_, 0, 0);

    //calendar_->set_date(date_);
    calendar_->select_month(int(date_.get_month())-1, date_.get_year());
    calendar_->select_day(date_.get_day());

    dialog_->signal_button_press_event().connect(sigc::mem_fun(this, &DateEntry::on_date_selected));
    dialog_->get_action_area()->set_size_request(-1, 0);

    dialog_->show_all();
    dialog_->run();
}


bool DateEntry::on_date_selected(const GdkEventButton *evt)
{
    calendar_->get_date(date_);
    set_date(date_);
    dialog_->hide();
    delete dialog_;
    dialog_ = nullptr;
    sig_date_changed_.emit();
    return false;
}


void DateEntry::set_date(const Glib::Date &date)
{
    date_ = date;
    entry_->set_text(date_.format_string(options.dateFormat));
}


void DateEntry::on_enter()
{
    struct tm t;
    memset(&t, 0, sizeof(struct tm));
    std::string val = entry_->get_text();
    std::string fmt = options.dateFormat;
    std::istringstream s(val);
    try {
        if (s >> std::get_time(&t, fmt.c_str())) {
            Glib::Date d(t.tm_mday, Glib::Date::Month(t.tm_mon+1), 1900 + t.tm_year);
            if (d.valid()) {
                set_date(d);
            } else {
                set_date(date_);
            }
        } else {
            set_date(date_);
        }
    } catch (std::istream::failure &err) {
        set_date(date_);
    }
}
