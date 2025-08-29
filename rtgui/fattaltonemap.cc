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
#include "fattaltonemap.h"
#include "eventmapper.h"
#include <iomanip>
#include <cmath>

using namespace rtengine;
using namespace rtengine::procparams;

FattalToneMapping::FattalToneMapping(): FoldableToolPanel(this, "fattal", M("TP_TM_FATTAL_LABEL"), false, true, true)
{
    auto m = ProcEventMapper::getInstance();
    EvSatControl = m->newEvent(rtengine::HDR, "HISTORY_MSG_TM_FATTAL_SATCONTROL");
    EvToolReset.set_action(rtengine::HDR);

    amount = Gtk::manage(new Adjuster(M("TP_TM_FATTAL_AMOUNT"), 1., 100., 1., 20.));
    threshold = Gtk::manage(new Adjuster(M("TP_TM_FATTAL_THRESHOLD"), -100., 300., 1., 30.0));
    satcontrol = Gtk::manage(new Gtk::CheckButton(M("TP_TM_FATTAL_SATCONTROL")));
    satcontrol->signal_toggled().connect(sigc::mem_fun(*this, &FattalToneMapping::satcontrolChanged), true);

    amount->setAdjusterListener(this);
    threshold->setAdjusterListener(this);

    threshold->setLogScale(10, 0);

    amount->show();
    threshold->show();

    pack_start(*amount);
    pack_start(*threshold);
    pack_start(*satcontrol);
}

void FattalToneMapping::read(const ProcParams *pp)
{
    disableListener();

    setEnabled(pp->fattal.enabled);
    threshold->setValue(pp->fattal.threshold);
    amount->setValue(pp->fattal.amount);
    satcontrol->set_active(pp->fattal.satcontrol);

    enableListener();
}

void FattalToneMapping::write(ProcParams *pp)
{
    pp->fattal.threshold = threshold->getValue();
    pp->fattal.amount = amount->getValue();
    pp->fattal.enabled = getEnabled();
    pp->fattal.satcontrol = satcontrol->get_active();
}

void FattalToneMapping::setDefaults(const ProcParams *defParams)
{
    threshold->setDefault(defParams->fattal.threshold);
    amount->setDefault(defParams->fattal.amount);

    initial_params = defParams->fattal;
}

void FattalToneMapping::adjusterChanged(Adjuster* a, double newval)
{
    if(listener && getEnabled()) {
        if(a == threshold) {
            listener->panelChanged(EvTMFattalThreshold, a->getTextValue());
        } else if(a == amount) {
            listener->panelChanged(EvTMFattalAmount, a->getTextValue());
        }
    }
}

void FattalToneMapping::adjusterAutoToggled(Adjuster* a, bool newval)
{
}

void FattalToneMapping::enabledChanged ()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged (EvTMFattalEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged (EvTMFattalEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged (EvTMFattalEnabled, M("GENERAL_DISABLED"));
        }
    }
}


void FattalToneMapping::satcontrolChanged()
{
    if (listener && getEnabled()) {
        listener->panelChanged(EvSatControl, satcontrol->get_active() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}


void FattalToneMapping::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.fattal = initial_params;
    }
    pp.fattal.enabled = getEnabled();
    read(&pp);
}
