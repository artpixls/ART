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
#include "adjuster.h"
#include <sigc++/slot.h>
#include <cmath>
#include "multilangmgr.h"
#include "../rtengine/rtengine.h"
#include "options.h"
#include "guiutils.h"
#include "rtimage.h"

#define MIN_RESET_BUTTON_HEIGHT 17

static double one2one(double val)
{
    return val;
}

Adjuster::Adjuster (Glib::ustring vlabel, double vmin, double vmax, double vstep, double vdefault, Gtk::Image *imgIcon1, Gtk::Image *imgIcon2, double2double_fun slider2value_, double2double_fun value2slider_, bool deprecated, bool compact)
{

    set_hexpand(true);
    set_vexpand(false);
    label = nullptr;
    adjusterListener = nullptr;
    afterReset = false;
    blocked = false;
    automatic = nullptr;
    eventPending = false;
    grid = NULL;
    imageIcon1 = imgIcon1;

    logBase = 0;
    logPivot = 0;
    logAnchorMiddle = false;

    if (imageIcon1) {
        setExpandAlignProperties(imageIcon1, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_CENTER);
    }

    imageIcon2 = imgIcon2;

    if (imageIcon2) {
        setExpandAlignProperties(imageIcon2, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_CENTER);
    }

    slider2value = slider2value_ ? slider2value_ : one2one;
    value2slider = value2slider_ ? value2slider_ : one2one;
    vMin = vmin;
    vMax = vmax;
    vStep = vstep;
    addMode = false;

    delay = options.adjusterMinDelay;

    set_column_spacing(0);
    set_column_homogeneous(false);
    set_row_spacing(0);
    set_row_homogeneous(false);

    editedCheckBox = nullptr;

    if (!vlabel.empty()) {
        adjustmentName = vlabel;
        label = Gtk::manage (new Gtk::Label (adjustmentName));
        setExpandAlignProperties(label, true, false, Gtk::ALIGN_START, Gtk::ALIGN_BASELINE);
    }

    reset = Gtk::manage (new Gtk::Button ());
    reset->add (*Gtk::manage (new RTImage ("undo-small.svg", "redo-small.svg")));
    setExpandAlignProperties(reset, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_CENTER);
    reset->set_relief (Gtk::RELIEF_NONE);
    reset->set_tooltip_markup(M("ADJUSTER_RESET_TO_DEFAULT"));
    reset->get_style_context()->add_class(GTK_STYLE_CLASS_FLAT);
    reset->set_can_focus(false);

    spin = Gtk::manage (new MySpinButton ());
    setExpandAlignProperties(spin, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_CENTER);
    spin->set_input_purpose(Gtk::INPUT_PURPOSE_DIGITS);

    reset->set_size_request (-1, spin->get_height() > MIN_RESET_BUTTON_HEIGHT ? spin->get_height() : MIN_RESET_BUTTON_HEIGHT);

    slider = Gtk::manage (new MyHScale ());
    setExpandAlignProperties(slider, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);
    slider->set_draw_value (false);
    //slider->set_has_origin(false);  // ------------------ This will remove the colored part on the left of the slider's knob

    const auto on_double_click =
        [this](GdkEventButton *event) -> bool
        {
            if (event->button == 1 && event->type == GDK_2BUTTON_PRESS) {
                resetValue(event->state & GDK_CONTROL_MASK);
                return false;
            }
            return true;
        };
    slider->add_events(Gdk::BUTTON_PRESS_MASK);
    slider->signal_button_press_event().connect(sigc::slot<bool, GdkEventButton *>(on_double_click));

    if (vlabel.empty() || compact) {
        // No label, everything goes in a single row
        if (vlabel.empty()) {
            attach_next_to(*slider, Gtk::POS_LEFT, 1, 1);
        } else {
            setExpandAlignProperties(label, false, false, Gtk::ALIGN_START, Gtk::ALIGN_BASELINE);            
            attach_next_to(*label, Gtk::POS_LEFT, 1, 1);
            attach_next_to(*slider, *label, Gtk::POS_RIGHT, 1, 1);
        }

        if (imageIcon1) {
            attach_next_to(*imageIcon1, *slider, Gtk::POS_LEFT, 1, 1);
        }

        if (imageIcon2) {
            attach_next_to(*imageIcon2, *slider, Gtk::POS_RIGHT, 1, 1);
            attach_next_to(*spin, *imageIcon2, Gtk::POS_RIGHT, 1, 1);
        } else {
            attach_next_to(*spin, *slider, Gtk::POS_RIGHT, 1, 1);
        }

        attach_next_to(*reset, *spin, Gtk::POS_RIGHT, 1, 1);
    } else {
        // A label is provided, spreading the widgets in 2 rows
        Gtk::HBox *hb = nullptr;
        if (deprecated) {
            hb = Gtk::manage(new Gtk::HBox());
            Gtk::Image *w = Gtk::manage(new RTImage("warning-small.svg"));
            w->set_tooltip_markup(M("GENERAL_DEPRECATED_TOOLTIP"));
            hb->pack_start(*w, Gtk::PACK_SHRINK, 2);
            hb->pack_start(*label, Gtk::PACK_SHRINK);
            setExpandAlignProperties(hb, true, false, Gtk::ALIGN_START, Gtk::ALIGN_BASELINE);
            attach_next_to(*hb, Gtk::POS_LEFT, 1, 1);
        } else {
            attach_next_to(*label, Gtk::POS_LEFT, 1, 1);
        }
        attach_next_to(*spin, Gtk::POS_RIGHT, 1, 1);
        // A second HBox is necessary
        grid = Gtk::manage(new Gtk::Grid());
        grid->attach_next_to(*slider, Gtk::POS_LEFT, 1, 1);

        if (imageIcon1) {
            grid->attach_next_to(*imageIcon1, *slider, Gtk::POS_LEFT, 1, 1);
        }

        if (imageIcon2) {
            grid->attach_next_to(*imageIcon2, Gtk::POS_RIGHT, 1, 1);
            grid->attach_next_to(*reset, *imageIcon2, Gtk::POS_RIGHT, 1, 1);
        } else {
            grid->attach_next_to(*reset, *slider, Gtk::POS_RIGHT, 1, 1);
        }

        if (deprecated) {
            attach_next_to(*grid, *hb, Gtk::POS_BOTTOM, 2, 1);
        } else {
            attach_next_to(*grid, *label, Gtk::POS_BOTTOM, 2, 1);
        }
    }

    setLimits (vmin, vmax, vstep, vdefault);

    defaultVal = shapeValue (vdefault);
    ctorDefaultVal = shapeValue (vdefault);
    editedState = defEditedState = Irrelevant;
    autoState = Irrelevant;

    sliderChange = slider->signal_value_changed().connect( sigc::mem_fun(*this, &Adjuster::sliderChanged) );
    spinChange = spin->signal_value_changed().connect ( sigc::mem_fun(*this, &Adjuster::spinChanged), true);
    reset->signal_button_release_event().connect_notify( sigc::mem_fun(*this, &Adjuster::resetPressed) );
    const auto keypress =
        [this](GdkEventKey *evt) -> bool
        {
            bool ctrl = evt->state & GDK_CONTROL_MASK;
            bool shift = evt->state & GDK_SHIFT_MASK;
            bool alt = evt->state & GDK_MOD1_MASK;
            double step, page;
            spin->get_increments(step, page);

            if (!ctrl && !shift && !alt) {
                switch (getKeyval(evt)) {
                case GDK_KEY_Up:
                    spin->set_value(spin->get_value() + step);
                    return true;
                case GDK_KEY_Down:
                    spin->set_value(spin->get_value() - step);
                    return true;
                case GDK_KEY_Page_Up:
                    spin->set_value(spin->get_value() + page);
                    return true;
                case GDK_KEY_Page_Down:
                    spin->set_value(spin->get_value() - page);
                    return true;
                }
            }
            return false;
        };
    slider->add_events(Gdk::KEY_PRESS_MASK);
    slider->signal_key_press_event().connect(sigc::slot<bool, GdkEventKey *>(keypress), false);

    show_all ();
}

Adjuster::~Adjuster ()
{

    sliderChange.block (true);
    spinChange.block (true);
    delayConnection.block (true);
    adjusterListener = nullptr;

    if (automatic) {
        delete automatic;
    }
}

void Adjuster::addAutoButton (Glib::ustring tooltip)
{
    if (!automatic) {
        automatic = new Gtk::CheckButton ();
        //automatic->add (*Gtk::manage (new RTImage ("gears.svg")));
        automatic->set_tooltip_markup(tooltip.length() ? Glib::ustring::compose("<b>%1</b>\n\n%2", M("GENERAL_AUTO"), tooltip) : M("GENERAL_AUTO"));
        setExpandAlignProperties(automatic, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_CENTER);
        autoChange = automatic->signal_toggled().connect( sigc::mem_fun(*this, &Adjuster::autoToggled) );

        if (grid) {
            // Hombre, adding the checbox next to the reset button because adding it next to the spin button (as before)
            // would diminish the available size for the label and would require a much heavier reorganization of the grid !
            grid->attach_next_to(*automatic, *reset, Gtk::POS_RIGHT, 1, 1);
        } else {
            attach_next_to(*automatic, *reset, Gtk::POS_RIGHT, 1, 1);
        }
        automatic->show();
    }
}

void Adjuster::delAutoButton ()
{
    if (automatic) {
        removeIfThere(grid, automatic);
        delete automatic;
        automatic = nullptr;
    }
}

void Adjuster::throwOnButtonRelease(bool throwOnBRelease)
{

    if (throwOnBRelease) {
        if (!buttonReleaseSlider.connected()) {
            buttonReleaseSlider = slider->signal_button_release_event().connect_notify( sigc::mem_fun(*this, &Adjuster::sliderReleased) );
        }

        if (!buttonReleaseSpin.connected()) {
            buttonReleaseSpin = spin->signal_button_release_event().connect_notify( sigc::mem_fun(*this, &Adjuster::spinReleased) );    // Use the same callback hook
        }
    } else {
        if (buttonReleaseSlider.connected()) {
            buttonReleaseSlider.disconnect();
        }

        if (buttonReleaseSpin.connected()) {
            buttonReleaseSpin.disconnect();
        }
    }

    eventPending = false;
}

void Adjuster::setDefault (double def, bool hard)
{

    defaultVal = shapeValue (def);
    if (hard) {
        ctorDefaultVal = defaultVal;
    }
}

void Adjuster::setDefaultEditedState (EditedState eState)
{

    defEditedState = eState;
}

void Adjuster::autoToggled ()
{

    if (adjusterListener && !blocked) {
        adjusterListener->adjusterAutoToggled(this, automatic->get_active());
    }
}

void Adjuster::sliderReleased (GdkEventButton* event)
{

    if ((event != nullptr) && (event->button == 1)) {
        if (delayConnection.connected()) {
            delayConnection.disconnect ();
        }

        notifyListener();
    }
}

void Adjuster::spinReleased (GdkEventButton* event)
{

    if ((event != nullptr) && delay == 0) {
        if (delayConnection.connected()) {
            delayConnection.disconnect ();
        }

        notifyListener();
    }
}

void Adjuster::resetValue (bool toInitial)
{
    if (editedState != Irrelevant) {
        editedState = defEditedState;

        if (editedCheckBox) {
            editedChange.block (true);
            editedCheckBox->set_active (defEditedState == Edited);
            editedChange.block (false);
        }

        refreshLabelStyle ();
    }

    afterReset = true;

    if (toInitial) {
        // resetting to the initial editing value, when the image has been loaded
        setSliderValue(addMode ? defaultVal : value2slider(defaultVal));
    } else {
        // resetting to the slider default value
        if (addMode) {
            setSliderValue(0.);
        } else {
            setSliderValue(value2slider(ctorDefaultVal));
        }
    }
}

// Please note that it won't change the "Auto" CheckBox's state, if there
void Adjuster::resetPressed (GdkEventButton* event)
{

    if ((event != nullptr) && (event->state & GDK_CONTROL_MASK) && (event->button == 1)) {
        resetValue(true);
    } else {
        resetValue(false);
    }
}

double Adjuster::shapeValue (double a)
{
    double val = round(a * pow(double(10), digits)) / pow(double(10), digits);
    return val == -0.0 ? 0.0 : val;
}

void Adjuster::setLimits (double vmin, double vmax, double vstep, double vdefault)
{

    sliderChange.block (true);
    spinChange.block (true);

    for (digits = 0; fabs(vstep * pow(double(10), digits) - floor(vstep * pow(double(10), digits))) > 0.000000000001; digits++);

    spin->set_digits (digits);
    spin->set_increments (vstep, 2.0 * vstep);
    spin->set_range (vmin, vmax);
    spin->updateSize();
    spin->set_value (shapeValue(vdefault));
    slider->set_digits (digits);
    slider->set_increments (vstep, 2.0 * vstep);
    slider->set_range (addMode ? vmin : value2slider(vmin), addMode ? vmax : value2slider(vmax));
    setSliderValue(addMode ? shapeValue(vdefault) : value2slider(shapeValue(vdefault)));
    //defaultVal = shapeValue (vdefault);
    sliderChange.block (false);
    spinChange.block (false);
}

void Adjuster::setAddMode(bool addM)
{
    if (addM != addMode) {
        // Switching the Adjuster to the new mode
        addMode = addM;

        if (addM) {
            // Switching to the relative mode
            double range = -vMin + vMax;

            if (range < 0.) {
                range = -range;
            }

            setLimits(-range, range, vStep, 0);
        } else {
            // Switching to the absolute mode
            setLimits(vMin, vMax, vStep, defaultVal);
        }
    }
}

void Adjuster::spinChanged ()
{

    if (delayConnection.connected()) {
        delayConnection.disconnect ();
    }

    sliderChange.block (true);
    setSliderValue(addMode ? spin->get_value () : value2slider(spin->get_value ()));
    sliderChange.block (false);

    if (delay == 0) {
        if (adjusterListener && !blocked) {
            if (!buttonReleaseSlider.connected() || afterReset) {
                eventPending = false;
                if (automatic) {
                    setAutoValue(false);
                }
                adjusterListener->adjusterChanged (this, spin->get_value ());
            } else {
                eventPending = true;
            }
        }
    } else {
        eventPending = true;
        delayConnection = Glib::signal_timeout().connect (sigc::mem_fun(*this, &Adjuster::notifyListener), delay);
    }

    if (editedState == UnEdited) {
        editedState = Edited;

        if (editedCheckBox) {
            editedChange.block (true);
            editedCheckBox->set_active (true);
            editedChange.block (false);
        }

        refreshLabelStyle ();
    }

    afterReset = false;
}

void Adjuster::sliderChanged ()
{

    if (delayConnection.connected()) {
        delayConnection.disconnect ();
    }

    spinChange.block (true);
    double v = shapeValue(getSliderValue());
    spin->set_value (addMode ? v : slider2value(v));
    spinChange.block (false);

    if (delay == 0 || afterReset) {
        if (adjusterListener && !blocked) {
            if (!buttonReleaseSlider.connected() || afterReset) {
                eventPending = false;
                if (automatic) {
                    setAutoValue(false);
                }
                adjusterListener->adjusterChanged (this, spin->get_value ());
            } else {
                eventPending = true;
            }
        }
    } else {
        eventPending = true;
        delayConnection = Glib::signal_timeout().connect (sigc::mem_fun(*this, &Adjuster::notifyListener), delay);
    }

    if (!afterReset && editedState == UnEdited) {
        editedState = Edited;

        if (editedCheckBox) {
            editedChange.block (true);
            editedCheckBox->set_active (true);
            editedChange.block (false);
        }

        refreshLabelStyle ();
    }

    afterReset = false;
}

void Adjuster::setValue (double a)
{

    spinChange.block (true);
    sliderChange.block (true);
    spin->set_value (shapeValue (a));
    setSliderValue(addMode ? shapeValue(a) : value2slider(shapeValue (a)));
    sliderChange.block (false);
    spinChange.block (false);
    afterReset = false;
}

void Adjuster::setAutoValue (bool a)
{
    if (automatic) {
        bool oldVal = autoChange.block(true);
        automatic->set_active(a);
        autoChange.block(oldVal);
    }
}

bool Adjuster::notifyListener ()
{

    if (eventPending && adjusterListener != nullptr && !blocked) {
        if (automatic) {
            setAutoValue(false);
        }
        adjusterListener->adjusterChanged (this, spin->get_value ());
    }

    eventPending = false;

    return false;
}

bool Adjuster::notifyListenerAutoToggled ()
{

    if (adjusterListener != nullptr && !blocked) {
        adjusterListener->adjusterAutoToggled(this, automatic->get_active());
    }

    return false;
}

void Adjuster::setEnabled (bool enabled)
{

    bool autoVal = automatic && !editedCheckBox ? automatic->get_active() : true;
    spin->set_sensitive (enabled && autoVal);
    slider->set_sensitive (enabled && autoVal);

    if (automatic) {
        automatic->set_sensitive (enabled);
    }
}

void Adjuster::setEditedState (EditedState eState)
{

    if (editedState != eState) {
        if (editedCheckBox) {
            editedChange.block (true);
            editedCheckBox->set_active (eState == Edited);
            editedChange.block (false);
        }

        editedState = eState;
        refreshLabelStyle ();
    }
}

EditedState Adjuster::getEditedState ()
{

    if (editedState != Irrelevant && editedCheckBox) {
        editedState = editedCheckBox->get_active () ? Edited : UnEdited;
    }

    return editedState;
}

void Adjuster::showEditedCB ()
{

    if (label) {
        removeIfThere(this, label, false);
    }

    if (!editedCheckBox) {
        editedCheckBox = Gtk::manage(new Gtk::CheckButton (adjustmentName));
        editedCheckBox->set_vexpand(false);

        if (grid) {
            editedCheckBox->set_hexpand(true);
            editedCheckBox->set_halign(Gtk::ALIGN_START);
            editedCheckBox->set_valign(Gtk::ALIGN_CENTER);
            attach_next_to(*editedCheckBox, *spin, Gtk::POS_LEFT, 1, 1);
        } else {
            editedCheckBox->set_hexpand(false);
            editedCheckBox->set_halign(Gtk::ALIGN_START);
            editedCheckBox->set_valign(Gtk::ALIGN_CENTER);

            if (imageIcon1) {
                attach_next_to(*editedCheckBox, *imageIcon1, Gtk::POS_LEFT, 1, 1);
            } else {
                attach_next_to(*editedCheckBox, *slider, Gtk::POS_LEFT, 1, 1);
            }
        }

        editedChange = editedCheckBox->signal_toggled().connect( sigc::mem_fun(*this, &Adjuster::editedToggled) );
        editedCheckBox->show();
    }
}

void Adjuster::refreshLabelStyle ()
{

    /*  Glib::RefPtr<Gtk::StyleContext> style = label->get_style_context ();
        Pango::FontDescription fd = style->get_font ();
        fd.set_weight (editedState==Edited ? Pango::WEIGHT_BOLD : Pango::WEIGHT_NORMAL);
        style->set_font (fd);
        label->set_style (style);
        label->queue_draw ();*/
}

void Adjuster::editedToggled ()
{

    if (adjusterListener && !blocked) {
        if (automatic) {
            setAutoValue(false);
        }
        adjusterListener->adjusterChanged (this, spin->get_value ());
    }

    eventPending = false;
}

void Adjuster::trimValue (double &val)
{

    val = rtengine::LIM(val, vMin, vMax);

}

void Adjuster::trimValue (int &val)
{

    val = rtengine::LIM(val, static_cast<int>(vMin), static_cast<int>(vMax));

}

void Adjuster::trimValue (float &val)
{

    val = rtengine::LIM(val, static_cast<float>(vMin), static_cast<float>(vMax));

}


inline double Adjuster::getSliderValue()
{
    double val = slider->get_value();
    if (logBase) {
        if (logAnchorMiddle) {
            double mid = (vMax - vMin) / 2;
            double mmid = vMin + mid;
            if (val >= mmid) {
                double range = vMax - mmid;
                double x = (val - mmid) / range;
                val = logPivot + (pow(logBase, x) - 1.0) / (logBase - 1.0) * (vMax - logPivot);
            } else {
                double range = mmid - vMin;
                double x = (mmid - val) / range;
                val = logPivot - (pow(logBase, x) - 1.0) / (logBase - 1.0) * (logPivot - vMin);
            }
        } else {
            if (val >= logPivot) {
                double range = vMax - logPivot;
                double x = (val - logPivot) / range;
                val = logPivot + (pow(logBase, x) - 1.0) / (logBase - 1.0) * range;
            } else {
                double range = logPivot - vMin;
                double x = (logPivot - val) / range;
                val = logPivot - (pow(logBase, x) - 1.0) / (logBase - 1.0) * range;
            }
        }
    }
    return val;
}


inline void Adjuster::setSliderValue(double val)
{
    if (logBase) {
        if (logAnchorMiddle) {
            double mid = (vMax - vMin) / 2;
            if (val >= logPivot) {
                double range = vMax - logPivot;
                double x = (val - logPivot) / range;
                val = (vMin + mid) + log(x * (logBase - 1.0) + 1.0) / log(logBase) * mid;
            } else {
                double range = logPivot - vMin;
                double x = (logPivot - val) / range;
                val = (vMin + mid) - log(x * (logBase - 1.0) + 1.0) / log(logBase) * mid;
            }
        } else {
            if (val >= logPivot) {
                double range = vMax - logPivot;
                double x = (val - logPivot) / range;
                val = logPivot + log(x * (logBase - 1.0) + 1.0) / log(logBase) * range;
            } else {
                double range = logPivot - vMin;
                double x = (logPivot - val) / range;
                val = logPivot - log(x * (logBase - 1.0) + 1.0) / log(logBase) * range;
            }
        }
    }
    slider->set_value(val);
}


void Adjuster::setLogScale(double base, double pivot, bool anchorMiddle)
{
    if (!options.adjuster_force_linear) {
        spinChange.block(true);
        sliderChange.block(true);

        double cur = getSliderValue();
        logBase = base;
        logPivot = pivot;
        logAnchorMiddle = anchorMiddle;
        setSliderValue(cur);
    
        sliderChange.block(false);
        spinChange.block(false);
    }
}


void Adjuster::showIcons(bool yes)
{
    if (imageIcon1) {
        imageIcon1->set_visible(yes);
    }
    if (imageIcon2) {
        imageIcon2->set_visible(yes);
    }
}


void Adjuster::forceNotifyListener()
{
    if (adjusterListener) {
        adjusterListener->adjusterChanged(this, spin->get_value ());
    }
}
