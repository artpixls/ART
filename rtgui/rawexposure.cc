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
#include "rawexposure.h"
#include "guiutils.h"
#include <sstream>
#include "../rtengine/refreshmap.h"

using namespace rtengine;
using namespace rtengine::procparams;

RAWExposure::RAWExposure () : FoldableToolPanel(this, "rawexposure", M("TP_EXPOS_WHITEPOINT_LABEL"), false, true, true)
{
    EvToolEnabled.set_action(rtengine::DARKFRAME);
    EvToolReset.set_action(rtengine::DARKFRAME);
    
    PexPos = Gtk::manage(new Adjuster (M("TP_RAWEXPOS_LINEAR"), 0.1, 32.0, 0.01, 1));
    PexPos->setAdjusterListener (this);

    if (PexPos->delay < options.adjusterMaxDelay) {
        PexPos->delay = options.adjusterMaxDelay;
    }

    PexPos->show();
    pack_start( *PexPos, Gtk::PACK_SHRINK, 4);//exposi
    PexPos->setLogScale(100, 0);
}

void RAWExposure::read(const rtengine::procparams::ProcParams* pp)
{
    disableListener ();
    setEnabled(pp->raw.enable_whitepoint);
    PexPos->setValue (pp->raw.expos);
    enableListener ();
}

void RAWExposure::write(rtengine::procparams::ProcParams* pp)
{
    pp->raw.enable_whitepoint = getEnabled();
    pp->raw.expos = PexPos->getValue();
}

void RAWExposure::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        Glib::ustring value = a->getTextValue();

        if (a == PexPos ) {
            listener->panelChanged (EvPreProcessExpCorrLinear,  value );
        }
    }
}

void RAWExposure::adjusterAutoToggled(Adjuster* a, bool newval)
{
}


void RAWExposure::setDefaults(const rtengine::procparams::ProcParams* defParams)
{
    PexPos->setDefault( defParams->raw.expos);
}

void RAWExposure::trimValues (rtengine::procparams::ProcParams* pp)
{

    PexPos->trimValue(pp->raw.expos);
}


void RAWExposure::toolReset(bool to_initial)
{
    disableListener();
    PexPos->resetValue(to_initial);
    enableListener();
}
