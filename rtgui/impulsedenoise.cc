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
#include "impulsedenoise.h"
#include "../rtengine/refreshmap.h"
#include <cmath>
#include <iomanip>
#include "guiutils.h"

using namespace rtengine;
using namespace rtengine::procparams;

ImpulseDenoise::ImpulseDenoise () : FoldableToolPanel(this, "impulsedenoise", M("TP_IMPULSEDENOISE_LABEL"), false, true, true)
{
    EvToolReset.set_action(rtengine::DETAIL);
    
    thresh = Gtk::manage (new Adjuster (M("TP_IMPULSEDENOISE_THRESH"), 0, 100, 1, 50));

    pack_start (*thresh);

    thresh->setAdjusterListener (this);

    show_all_children ();
}

void ImpulseDenoise::read(const ProcParams* pp)
{
    disableListener ();

    setEnabled(pp->impulseDenoise.enabled);
    thresh->setValue (pp->impulseDenoise.thresh);

    enableListener ();
}

void ImpulseDenoise::write(ProcParams* pp)
{
    pp->impulseDenoise.thresh    = thresh->getValue ();
    pp->impulseDenoise.enabled   = getEnabled();
}

void ImpulseDenoise::setDefaults(const ProcParams* defParams)
{
    thresh->setDefault(defParams->impulseDenoise.thresh);
    initial_params = defParams->impulseDenoise;
}

void ImpulseDenoise::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        listener->panelChanged (EvIDNThresh, Glib::ustring::format (std::setw(2), std::fixed, std::setprecision(1), a->getValue()));
    }
}

void ImpulseDenoise::adjusterAutoToggled(Adjuster* a, bool newval)
{
}

void ImpulseDenoise::enabledChanged ()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged (EvIDNEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged (EvIDNEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged (EvIDNEnabled, M("GENERAL_DISABLED"));
        }
    }
}


void ImpulseDenoise::trimValues (rtengine::procparams::ProcParams* pp)
{

    thresh->trimValue(pp->impulseDenoise.thresh);
}


void ImpulseDenoise::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.impulseDenoise = initial_params;
    }
    pp.impulseDenoise.enabled = getEnabled();
    read(&pp);
}
