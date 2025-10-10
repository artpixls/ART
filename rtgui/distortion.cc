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
#include "distortion.h"
#include <iomanip>
#include "rtimage.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;

Distortion::Distortion (): FoldableToolPanel(this, "distortion", M("TP_DISTORTION_LABEL"), false, true, true)
{
    rlistener = nullptr;

    auto m = ProcEventMapper::getInstance();
    EvAuto = m->newEvent(rtengine::TRANSFORM, "HISTORY_MSG_DISTORTION_AUTO");
    EvAutoLoad = m->newAnonEvent(rtengine::TRANSFORM);
    is_auto_load_event_ = false;
    
    EvToolEnabled.set_action(rtengine::TRANSFORM);
    EvToolReset.set_action(rtengine::TRANSFORM);
    
    autoDistor = Gtk::manage (new Gtk::ToggleButton(M("GENERAL_AUTO")));
    autoDistor->set_image (*Gtk::manage (new RTImage ("distortion-auto-small.svg")));
    autoDistor->get_style_context()->add_class("independent");
    autoDistor->set_alignment(0.5f, 0.5f);
    autoDistor->set_tooltip_text (M("TP_DISTORTION_AUTO_TIP"));
    idConn = autoDistor->signal_toggled().connect(sigc::mem_fun(*this, &Distortion::idPressed));
    autoDistor->show();
    pack_start(*autoDistor);

    Gtk::Image *idistL = Gtk::manage(new RTImage("distortion-pincushion-small.svg"));
    Gtk::Image *idistR = Gtk::manage(new RTImage("distortion-barrel-small.svg"));

    distor = Gtk::manage (new Adjuster (M("TP_DISTORTION_AMOUNT"), -0.5, 0.5, 0.001, 0, idistL, idistR));
    distor->setAdjusterListener (this);

    distor->setLogScale(2, 0);
    
    distor->show();
    pack_start (*distor);
}


Distortion::~Distortion()
{
    idle_register.destroy();
}


void Distortion::read(const ProcParams* pp)
{
    disableListener ();
    setEnabled(pp->distortion.enabled);
    distor->setValue (pp->distortion.amount);
    autoDistor->set_active(pp->distortion.autocompute);
    if (pp->distortion.enabled && pp->distortion.autocompute && rlistener) {
        idle_register.add(
            [this]() -> bool
            {
                GThreadLock lock;
                is_auto_load_event_ = true;
                idPressed();
                return false;
            });
    }
    enableListener();
}

void Distortion::write(ProcParams* pp)
{
    pp->distortion.enabled = getEnabled();
    pp->distortion.amount = distor->getValue ();
    pp->distortion.autocompute = autoDistor->get_active();
}

void Distortion::setDefaults(const ProcParams* defParams)
{
    distor->setDefault (defParams->distortion.amount);
    initial_params = defParams->distortion;
}

void Distortion::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        ConnectionBlocker b(idConn);
        autoDistor->set_active(false);
        listener->panelChanged (EvDISTAmount, Glib::ustring::format (std::setw(4), std::fixed, std::setprecision(3), a->getValue()));
    }
}

void Distortion::adjusterAutoToggled(Adjuster* a, bool newval)
{
}

void Distortion::idPressed ()
{
    if (listener) {
        if (autoDistor->get_active()) {
            if (!getEnabled()) {
                setEnabled(true);
            }
            if (rlistener) {
                double new_amount = rlistener->autoDistorRequested();
                distor->setValue(new_amount);
                // adjusterChanged(distor, new_amount);
                // ConnectionBlocker b(idConn);
                // autoDistor->set_active(true);
            }
        }
        if (getEnabled()) {
            if (is_auto_load_event_) {
                listener->panelChanged(EvAutoLoad, "");
                is_auto_load_event_ = false;
            } else {
                listener->panelChanged(EvAuto, autoDistor->get_active() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
            }
        }
    }
}

void Distortion::trimValues (rtengine::procparams::ProcParams* pp)
{
    distor->trimValue(pp->distortion.amount);
}


void Distortion::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.distortion = initial_params;
    }
    pp.distortion.enabled = getEnabled();
    read(&pp);
}


void Distortion::enabledChanged()
{
    FoldableToolPanel::enabledChanged();
    if (getEnabled() && listener && autoDistor->get_active()) {
        idPressed();
    }
}
