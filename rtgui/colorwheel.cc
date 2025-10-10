/** -*- C++ -*-
 *  
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2017 Alberto Griggio <alberto.griggio@gmail.com>
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

// adapted from the "color correction" module of Darktable. Original copyright follows
/*
    copyright (c) 2009--2010 johannes hanika.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "colorwheel.h"
#include "../rtengine/iccstore.h"
#include "../rtengine/coord.h"
#include "guiutils.h"
#include <iostream>

using rtengine::Color;


//-----------------------------------------------------------------------------
// ColorWheelArea
//-----------------------------------------------------------------------------

bool ColorWheelArea::notifyListener()
{
    sig_changed_.emit();
    return false;
}


ColorWheelArea::ColorWheelArea(bool enable_low):
    Gtk::DrawingArea(),
    x_(0.f), y_(0.f),
    default_x_(0.f), default_y_(0.f),
    listener(nullptr),
    edited(false),
    point_active_(false),
    is_dragged_(false),
    lock_angle_(false),
    lock_radius_(false),
    scale(1),
    defaultScale(1)
{
    set_can_focus(false); // prevent moving the grid while you're moving a point
    add_events(Gdk::EXPOSURE_MASK | Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::POINTER_MOTION_MASK);
    set_name("ColorWheel");
    get_style_context()->add_class("drawingarea");
}

void ColorWheelArea::getParams(double &x, double &y) const
{
    x = x_;
    y = y_;
}


void ColorWheelArea::setParams(double x, double y, bool notify)
{
    const double lo = -1.0;
    const double hi = 1.0;
    x_ = rtengine::LIM(x, lo, hi);
    y_ = rtengine::LIM(y, lo, hi);
    queue_draw();
    if (notify) {
        notifyListener();
    }
}


void ColorWheelArea::setScale(double s, bool notify)
{
    scale = s;
    queue_draw();
    if (notify) {
        notifyListener();
    }
}


double ColorWheelArea::getScale() const
{
    return scale;
}


void ColorWheelArea::setDefault(double x, double y, double s)
{
    default_x_ = x;
    default_y_ = y;
    defaultScale = s;
}


void ColorWheelArea::reset(bool toInitial)
{
    if (toInitial) {
        setScale(defaultScale, false);
        setParams(default_x_, default_y_, true);
    } else {
        setScale(1.0, false);
        setParams(0., 0., true);
    }
}


void ColorWheelArea::setEdited(bool yes)
{
    edited = yes;
}


bool ColorWheelArea::getEdited() const
{
    return edited;
}


void ColorWheelArea::on_style_updated()
{
    setDirty(true);
    queue_draw();
}


bool ColorWheelArea::on_draw(const ::Cairo::RefPtr<Cairo::Context> &crf)
{
    Gtk::Allocation allocation = get_allocation();
    allocation.set_x(0);
    allocation.set_y(0);

    // setDrawRectangle will allocate the backbuffer Surface
    if (setDrawRectangle(Cairo::FORMAT_ARGB32, allocation)) {
        setDirty(true);
    }

    if (!isDirty() || !surfaceCreated()) {
        return true;
    }

    Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
    Gtk::Border padding = getPadding(style);  // already scaled
    Cairo::RefPtr<Cairo::Context> cr = getContext();

    if (isDirty()) {
        int width = allocation.get_width();
        int height = allocation.get_height();

        int s = RTScalable::getScale();

        cr->set_line_cap(Cairo::LINE_CAP_SQUARE);

        // clear background
        cr->set_source_rgba(0., 0., 0., 0.);
        cr->set_operator(Cairo::OPERATOR_CLEAR);
        cr->paint();
        cr->set_operator(Cairo::OPERATOR_OVER);

        // drawing the cells
        cr->translate(inset * s + padding.get_left(), inset * s + padding.get_top());
        cr->set_antialias(Cairo::ANTIALIAS_NONE);
        width -= 2 * inset * s + padding.get_right() + padding.get_left();
        height -= 2 * inset * s + padding.get_top() + padding.get_bottom();

        // flip y:
        cr->translate(0, height);
        cr->scale(1., -1.);
        const float h2 = height * 0.5f;
        const float w2 = width * 0.5f;
        const float radius = std::min(w2, h2);
        const float inner_radius = radius * 0.93f;
        const float factor = scale / 1.5f;
        for (int j = 0; j < height; ++j) {
            float jj = j - h2;
            for (int i = 0; i < width; ++i) {
                float ii = i - w2;
                float R, G, B;
                float d = std::sqrt(rtengine::SQR(ii) + rtengine::SQR(jj));
                if (d <= radius) {
                    float s = d / radius;
                    float h = atan2(jj, ii) / (2.f * rtengine::RT_PI_F);
                    if (h < 0.f) {
                        h += 1.f;
                    } else if (h > 1.f) {
                        h -= 1.f;
                    }
                    // Color::hsl2rgb(h, s * factor, 0.5f, R, G, B);
                    // R /= 65535.f;
                    // G /= 65535.f;
                    // B /= 65535.f;

                    float d1 = radius - d;
                    float w = 1.f;
                    float alpha = 1.f;
                    float d2 = d - inner_radius;
                    if (d1 < 1.f) {
                        w = d1;
                        cr->set_antialias(Cairo::ANTIALIAS_DEFAULT);
                    } else {
                        cr->set_antialias(Cairo::ANTIALIAS_NONE);
                    }
                    if (d2 <= 0) {
                        alpha = 0.1f + 0.15f * d / inner_radius;
                        s *= 0.8f;
                    } else if (d2 < 1.f) {
                        w += d2;
                        cr->set_antialias(Cairo::ANTIALIAS_DEFAULT);
                    }
                    float xoff = ii > 0 ? -w : 0.f;
                    float yoff = jj > 0 ? -w : 0.f;

                    Color::hsl2rgb(h, s * factor, 0.5f, R, G, B);
                    R /= 65535.f;
                    G /= 65535.f;
                    B /= 65535.f;

                    getGUIColor(R, G, B);
                    
                    // R = rtengine::intp(alpha, R, bg_r);
                    // G = rtengine::intp(alpha, G, bg_g);
                    // B = rtengine::intp(alpha, B, bg_b);
                    
                    cr->set_source_rgba(R, G, B, alpha);
                    //cr->set_source_rgb(R, G, B);
                    cr->rectangle(i+xoff, j+yoff, w, w);
                    cr->fill();
                }
            }
        }

        // draw the grid
        {
            cr->set_antialias(Cairo::ANTIALIAS_DEFAULT);
            cr->set_line_width(0.5 * double(s));
            double r = 0.2, g = 0.2, b = 0.2;
            getGUIColor(r, g, b);
            cr->set_source_rgb(r, g, b);
            for (int a = 0; a < 360; a += 30) {
                cr->move_to(w2, h2);
                rtengine::CoordD c(rtengine::PolarCoord(radius, a));
                cr->line_to(w2 + c.x, h2 + c.y);
                cr->stroke();
            }
            for (int i = 1; i < 4; ++i) {
                cr->arc(w2, h2, radius / 4.0 * i, 0, 2.0 * rtengine::RT_PI);
                cr->stroke();
            }
        }

        // drawing the connection line
        cr->set_antialias(Cairo::ANTIALIAS_DEFAULT);
        float hia = x_, hib = y_;
        float loa = width * 0.5f;
        float lob = height * 0.5f;
        hia = .5f * (width + width * hia);
        hib = .5f * (height + height * hib);
        cr->set_line_width(2. * double(s));
        double r = 0.6, g = 0.6, b = 0.6;
        getGUIColor(r, g, b);
        cr->set_source_rgb(r, g, b);
        cr->move_to(loa, lob);
        cr->line_to(hia, hib);
        cr->stroke();

        // drawing points
        r = g = b = 0.9;
        getGUIColor(r, g, b);
        cr->set_source_rgb(r, g, b);
        if (point_active_) {
            cr->arc(hia, hib, 5 * s, 0, 2. * rtengine::RT_PI);
        } else {
            cr->arc(hia, hib, 3 * s, 0, 2. * rtengine::RT_PI);
        }
        cr->fill();
    }

    copySurface(crf);
    return false;
}


bool ColorWheelArea::on_button_press_event(GdkEventButton *event)
{
    if (event->button == 1) {
        if (event->type == GDK_2BUTTON_PRESS) {
            x_ = y_ = 0.f;
            edited = true;
            notifyListener();
            queue_draw();
        } else if (event->type == GDK_BUTTON_PRESS && point_active_) {
            is_dragged_ = true;
            lock_angle_ = event->state & GDK_CONTROL_MASK;
            lock_radius_ = event->state & GDK_SHIFT_MASK;
        }
        return false;
    } else if (event->button == 3 && point_active_) {
        sig_right_click_.emit();
    }
    return true;
}


bool ColorWheelArea::on_button_release_event(GdkEventButton *event)
{
    if (event->button == 1) {
        is_dragged_ = false;
        lock_angle_ = false;
        lock_radius_ = false;
        return false;
    }
    return true;
}


bool ColorWheelArea::on_motion_notify_event(GdkEventMotion *event)
{
    if (is_dragged_ && delayconn.connected()) {
        delayconn.disconnect();
    }

    Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
    Gtk::Border padding = getPadding(style);  // already scaled

    bool old_active = point_active_;

    int s = RTScalable::getScale();
    int width = get_allocated_width() - 2 * inset * s - padding.get_right() - padding.get_left();
    int height = get_allocated_height() - 2 * inset * s - padding.get_top() - padding.get_bottom();
    const float mouse_x = std::min(double(std::max(event->x - inset * s - padding.get_right(), 0.)), double(width));
    const float mouse_y = std::min(double(std::max(get_allocated_height() - 1 - event->y - inset * s - padding.get_bottom(), 0.)), double(height));
    float ma = (2.0 * mouse_x - width) / float(width);
    float mb = (2.0 * mouse_y - height) / float(height);
    
    rtengine::PolarCoord prev(rtengine::CoordD(x_, y_));
    
    rtengine::PolarCoord p(rtengine::CoordD(ma, mb));
    p.radius = std::min(p.radius, 1.0);

    if (is_dragged_) {
        if (lock_angle_) {
            p.angle = prev.angle;
        }
        if (lock_radius_) {
            p.radius = prev.radius;
        }
    }
    
    rtengine::CoordD c(p);
    ma = c.x;
    mb = c.y;
        
    if (is_dragged_) {
        x_ = ma;
        y_ = mb;
        edited = true;
        grab_focus();
        if (options.adjusterMinDelay == 0) {
            notifyListener();
        } else {
            delayconn = Glib::signal_timeout().connect(sigc::mem_fun(*this, &ColorWheelArea::notifyListener), options.adjusterMinDelay);
        }
        queue_draw();
    } else {
        point_active_ = false;
        float ha = x_;
        float hb = y_;
        const float thrs = 0.05f;
        const float disthi = (ha - ma) * (ha - ma) + (hb - mb) * (hb - mb);
        if (disthi < thrs * thrs) {
            point_active_ = true;
        }
        if ((!old_active && point_active_) || (old_active && !point_active_)) {
            queue_draw();
        }
    }
    return true;
}


Gtk::SizeRequestMode ColorWheelArea::get_request_mode_vfunc() const
{
    return Gtk::SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}


void ColorWheelArea::get_preferred_width_vfunc(int &minimum_width, int &natural_width) const
{
    Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
    Gtk::Border padding = getPadding(style);  // already scaled
    int s = RTScalable::getScale();
    int p = padding.get_left() + padding.get_right();

    minimum_width = 50 * s + p;
    natural_width = 150 * s + p;  // same as GRAPH_SIZE from mycurve.h
}


void ColorWheelArea::get_preferred_height_for_width_vfunc(int width, int &minimum_height, int &natural_height) const
{
    Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
    Gtk::Border padding = getPadding(style);  // already scaled

    minimum_height = natural_height = width - padding.get_left() - padding.get_right() + padding.get_top() + padding.get_bottom();
}


//-----------------------------------------------------------------------------
// ColorWheel
//-----------------------------------------------------------------------------

ColorWheel::ColorWheel(bool use_scale):
    EditSubscriber(ET_PIPETTE),
    grid(),
    savedparams_{}
{
    Gtk::Button *reset = Gtk::manage(new Gtk::Button());
    reset->set_tooltip_markup(M("ADJUSTER_RESET_TO_DEFAULT"));
    reset->add(*Gtk::manage(new RTImage("undo-small.svg", "redo-small.svg")));
    reset->signal_button_release_event().connect(sigc::mem_fun(*this, &ColorWheel::resetPressed));

    setExpandAlignProperties(reset, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_START);
    reset->set_relief(Gtk::RELIEF_NONE);
    reset->get_style_context()->add_class(GTK_STYLE_CLASS_FLAT);
    reset->set_can_focus(false);
    reset->set_size_request(-1, 20);

    edit_ = Gtk::manage(new Gtk::ToggleButton());
    edit_->add(*Gtk::manage(new RTImage("color-picker-small.svg")));
    setExpandAlignProperties(edit_, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_START);
    edit_->set_relief(Gtk::RELIEF_NONE);
    edit_->get_style_context()->add_class(GTK_STYLE_CLASS_FLAT);
    edit_->set_can_focus(false);
    edit_->set_size_request(-1, 20);

    Gtk::VBox *vb = Gtk::manage(new Gtk::VBox());
    vb->pack_start(*reset, false, false, 4);
    vb->pack_start(*edit_, false, false, 4);
    if (use_scale) {
        scale = Gtk::manage(new Gtk::VScale(0.2, 2.5, 0.01));
        scale->set_inverted(true);
        scale->set_value(1.0);
        scale->set_draw_value(false);
        scale->set_has_origin(false);
        RTImage *icon = Gtk::manage(new RTImage("volume-small.svg"));
        vb->pack_start(*icon, false, false);
        vb->pack_start(*scale, true, true);
    } else {
        scale = nullptr;
    }
    scalebox_ = vb;

    pack_start(grid, true, true);
    pack_start(*vb, false, false);

    if (use_scale) {
        scaleconn = scale->signal_value_changed().connect(sigc::mem_fun(*this, &ColorWheel::scaleChanged));
    }

    const auto toggle_subscription =
        [this]()
        {
            if (edit_->get_active()) {
                this->subscribe();
                if (scale) {
                    grid.setScale(scale->get_value(), true);
                }
            } else {
                this->switchOffEditMode();
            }
        };
    editconn_ = edit_->signal_toggled().connect(sigc::slot<void>(toggle_subscription));
    
    show_all_children();

    grid.signal_right_click().connect(sigc::slot<void>([this]() { onRightClickPressed(); }));
}


bool ColorWheel::resetPressed(GdkEventButton *event)
{
    grid.reset(event->state & GDK_CONTROL_MASK);
    if (scale) {
        ConnectionBlocker sb(scaleconn);
        scale->set_value(grid.getScale());
    }
    onResetPressed();
    return false;
}

void ColorWheel::scaleChanged()
{
    if (timerconn.connected()) {
        timerconn.disconnect();
    }
    timerconn = Glib::signal_timeout().connect(sigc::slot<bool>([this]() -> bool { grid.setScale(scale->get_value(), true); return false; }), options.adjusterMaxDelay);
}


void ColorWheel::getParams(double &x, double &y, double &s) const
{
    grid.getParams(x, y);
    s = grid.getScale();
    x *= s;
    y *= s;
}


void ColorWheel::setParams(double x, double y, double s, bool notify)
{
    ConnectionBlocker sb(scaleconn);
    double ha1 = x / s;
    double hb1 = y / s;
    rtengine::PolarCoord ph(rtengine::CoordD(ha1, hb1));
    if (ph.radius > 1) {
        s *= ph.radius;
    }
    while (rtengine::max(std::abs(x / s), std::abs(y / s)) > 1.0) {
        s *= 2;
    }
    if (scale) {
        scale->set_value(s);
    }
    grid.setScale(s, false);
    grid.setParams(x / s, y / s, notify);
}


void ColorWheel::setDefault(double x, double y, double s)
{
    grid.setDefault(x, y, s);
}


void ColorWheel::onRightClickPressed()
{
    Gtk::Popover p(grid);
    p.set_pointing_to(Gdk::Rectangle(0, grid.get_height()/2, grid.get_width(), grid.get_height()/2));
    Gtk::HBox hb;
    p.set_border_width(16);
    p.add(hb);

    Gtk::Label lx(M("TP_COLORCORRECTION_H") + ": ");
    Gtk::SpinButton hue;
    hue.set_range(0, 360);
    hue.set_digits(1);
    hue.set_increments(0.1, 1);
    Gtk::Label ly(M("TP_COLORCORRECTION_S") + ": ");
    Gtk::SpinButton sat;
    sat.set_range(0, 250);
    sat.set_digits(1);
    sat.set_increments(0.1, 1);
    hb.pack_start(lx);
    hb.pack_start(hue);
    Gtk::Label spc("  ");
    hb.pack_start(spc);
    hb.pack_start(ly);
    hb.pack_start(sat);
    
    // Gtk::Label lx("X: ");
    // Gtk::SpinButton x;
    // x.set_range(-2.5, 2.5);
    // x.set_digits(3);
    // x.set_increments(0.01, 0.1);
    // Gtk::Label ly("Y: ");
    // Gtk::SpinButton y;
    // y.set_range(-2.5, 2.5);
    // y.set_digits(3);
    // y.set_increments(0.01, 0.1);
    // hb.pack_start(lx);
    // hb.pack_start(x);
    // Gtk::Label spc("  ");
    // hb.pack_start(spc);
    // hb.pack_start(ly);
    // hb.pack_start(y);

    double vx, vy, vs;
    getParams(vx, vy, vs);
    // x.set_value(vx);
    // y.set_value(vy);
    double vhue = std::atan2(vy, vx) * 180 / rtengine::RT_PI;
    if (vhue < 0) {
        vhue += 360;
    } else if (vhue > 360) {
        vhue -= 360;
    }
    double s = std::sqrt(vx * vx + vy * vy);
    double vsat = s * 100;
    //vsat /= vs;
    hue.set_value(vhue);
    sat.set_value(vsat);
    
    int result = 0;

    p.signal_closed().connect(
        sigc::slot<void>(
            [&]()
            {
                result = 1;
                double h = hue.get_value() * rtengine::RT_PI / 180.0;
                double s = sat.get_value() / 100.0;
                double x = s * std::cos(h);
                double y = s * std::sin(h);
                if (x != vx || y != vy) {
                    setParams(x, y, vs, true);
                }
                // if (x.get_value() != vx || y.get_value() != vy) {
                //     setParams(x.get_value(), y.get_value(), vs, true);
                // }
            })
        );

    p.show_all_children();
    p.set_modal(true);
    p.show();
    //p.popup();

    while (result == 0) {
        gtk_main_iteration();
    }
}


// EditSubscriber interface
CursorShape ColorWheel::getCursor(int objectID)
{
    return CSHandOpen;
}


namespace {

double find_scale(double x, double y)
{
    double s = 0.2;
    double v = std::max(std::abs(x), std::abs(y));
    while (v / s > 0.5 && s < 2.5) {
        s += 0.1;
    }
    return s;
}

} // namespace


bool ColorWheel::mouseOver(int modifierKey)
{
    auto p = getEditProvider();
    if (p && p->object) {
        double x = p->pipetteVal[0];
        double y = p->pipetteVal[2];
        double s = find_scale(x, y);
        if (!(modifierKey & GDK_SHIFT_MASK)) {
            // invert
            rtengine::PolarCoord p(rtengine::CoordD(x, y));
            p.angle += 180;
            rtengine::CoordD c(p);
            x = c.x;
            y = c.y;
        }
        setParams(x, y, s, false);
    }
    return true;
}


bool ColorWheel::button1Pressed(int modifierKey)
{
    auto p = getEditProvider();
    if (p && p->object) {
        double x = p->pipetteVal[0];
        double y = p->pipetteVal[2];
        double s = find_scale(x, y);
        if (!(modifierKey & GDK_SHIFT_MASK)) {
            // invert
            rtengine::PolarCoord p(rtengine::CoordD(x, y));
            p.angle += 180;
            rtengine::CoordD c(p);
            x = c.x;
            y = c.y;
        }
        setParams(x, y, s, true);
        getParams(savedparams_[0], savedparams_[1], savedparams_[2]);
        switchOffEditMode();
    }
    return true;
}


void ColorWheel::subscribe()
{
    getParams(savedparams_[0], savedparams_[1], savedparams_[2]);
    EditSubscriber::subscribe();
}


void ColorWheel::unsubscribe()
{
    ConnectionBlocker b(editconn_);
    edit_->set_active(false);
    EditSubscriber::unsubscribe();
    setParams(savedparams_[0], savedparams_[1], savedparams_[2], true);
}


//-----------------------------------------------------------------------------
// HueSatColorWheel
//-----------------------------------------------------------------------------

HueSatColorWheel::HueSatColorWheel(double sat_scale):
    ColorWheel(false),
    satscale_(sat_scale)
{
    removeIfThere(scalebox_, edit_);
}


HueSatColorWheel::~HueSatColorWheel()
{
    edit_->unreference();
}


void HueSatColorWheel::getParams(double &hue, double &sat) const
{
    double x, y;
    grid.getParams(x, y);
    hue = std::atan2(y, x) * 180 / rtengine::RT_PI;
    if (hue < 0) {
        hue += 360;
    } else if (hue > 360) {
        hue -= 360;
    }
    double s = std::sqrt(x * x + y * y);
    sat = s * 100;
    if (satscale_ > 0) {
        sat /= satscale_;
    }
}


void HueSatColorWheel::setParams(double hue, double sat, bool notify)
{
    double h = hue * rtengine::RT_PI / 180.0;
    double s = sat / 100.0;
    if (satscale_ > 0) {
        s = std::min(s * satscale_, 1.0);
    }
    double x = s * std::cos(h);
    double y = s * std::sin(h);
    grid.setScale(1.5, false);
    grid.setParams(x, y, notify);
}


void HueSatColorWheel::setDefault(double hue, double sat)
{
    double h = hue * rtengine::RT_PI / 180.0;
    double s = sat / 100.0;
    if (satscale_ > 0) {
        s = std::min(s * satscale_, 1.0);
    }
    double x = s * std::cos(h);
    double y = s * std::sin(h);
    ColorWheel::setDefault(x, y, 1.5);
}


void HueSatColorWheel::onResetPressed()
{
    grid.setScale(1.5, false);
}


void HueSatColorWheel::onRightClickPressed()
{
    Gtk::Popover p(grid);
    p.set_pointing_to(Gdk::Rectangle(0, grid.get_height()/2, grid.get_width(), grid.get_height()/2));
    Gtk::HBox hb;
    p.set_border_width(16);
    p.add(hb);
    Gtk::Label lx(M("TP_COLORCORRECTION_H") + ": ");
    Gtk::SpinButton hue;
    hue.set_range(0, 360);
    hue.set_digits(1);
    hue.set_increments(0.1, 1);
    Gtk::Label ly(M("TP_COLORCORRECTION_S") + ": ");
    Gtk::SpinButton sat;
    sat.set_range(0, 100);
    sat.set_digits(1);
    sat.set_increments(0.1, 1);
    hb.pack_start(lx);
    hb.pack_start(hue);
    Gtk::Label spc("  ");
    hb.pack_start(spc);
    hb.pack_start(ly);
    hb.pack_start(sat);

    double vhue, vsat;
    getParams(vhue, vsat);
    hue.set_value(vhue);
    sat.set_value(vsat);
    
    int result = 0;

    p.signal_closed().connect(
        sigc::slot<void>(
            [&]()
            {
                result = 1;
                if (hue.get_value() != vhue || sat.get_value() != vsat) {
                    setParams(hue.get_value(), sat.get_value(), true);
                }
            })
        );

    p.show_all_children();
    p.set_modal(true);
    p.show();
    //p.popup();

    while (result == 0) {
        gtk_main_iteration();
    }
}
