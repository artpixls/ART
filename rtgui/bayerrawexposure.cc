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
#include "bayerrawexposure.h"
#include "guiutils.h"
#include "../rtengine/refreshmap.h"

using namespace rtengine;
using namespace rtengine::procparams;

BayerRAWExposure::BayerRAWExposure () : FoldableToolPanel(this, "bayerrawexposure", M("TP_EXPOS_BLACKPOINT_LABEL"), false, true, true)
{
    EvToolEnabled.set_action(rtengine::DARKFRAME);
    EvToolReset.set_action(rtengine::DARKFRAME);
    
    PexBlack1 = Gtk::manage(new Adjuster (M("TP_RAWEXPOS_BLACK_1"), -2048, 2048, 1, 0)); //black level
    PexBlack1->setAdjusterListener (this);

    if (PexBlack1->delay < options.adjusterMaxDelay) {
        PexBlack1->delay = options.adjusterMaxDelay;
    }

    PexBlack1->show();
    PexBlack2 = Gtk::manage(new Adjuster (M("TP_RAWEXPOS_BLACK_2"), -2048, 2048, 1, 0)); //black level
    PexBlack2->setAdjusterListener (this);

    if (PexBlack2->delay < options.adjusterMaxDelay) {
        PexBlack2->delay = options.adjusterMaxDelay;
    }

    PexBlack2->show();
    PexBlack3 = Gtk::manage(new Adjuster (M("TP_RAWEXPOS_BLACK_3"), -2048, 2048, 1, 0)); //black level
    PexBlack3->setAdjusterListener (this);

    if (PexBlack3->delay < options.adjusterMaxDelay) {
        PexBlack3->delay = options.adjusterMaxDelay;
    }

    PexBlack3->show();
    PexBlack0 = Gtk::manage(new Adjuster (M("TP_RAWEXPOS_BLACK_0"), -2048, 2048, 1, 0)); //black level
    PexBlack0->setAdjusterListener (this);

    if (PexBlack0->delay < options.adjusterMaxDelay) {
        PexBlack0->delay = options.adjusterMaxDelay;
    }

    PexBlack0->show();
    PextwoGreen = Gtk::manage(new CheckBox(M("TP_RAWEXPOS_TWOGREEN")));// two green
    PextwoGreen->set_active (true);
    PextwoGreen->setCheckBoxListener (this);

    pack_start( *PexBlack1, Gtk::PACK_SHRINK, 0);//black R
    pack_start( *PexBlack0, Gtk::PACK_SHRINK, 0);//black G1
    pack_start( *PexBlack3, Gtk::PACK_SHRINK, 0);//black G2
    pack_start( *PexBlack2, Gtk::PACK_SHRINK, 0);//black B
    pack_start( *PextwoGreen, Gtk::PACK_SHRINK, 0);//black 2 green

    PexBlack0->setLogScale(100, 0);
    PexBlack1->setLogScale(100, 0);
    PexBlack2->setLogScale(100, 0);
    PexBlack3->setLogScale(100, 0);
}

void BayerRAWExposure::read(const rtengine::procparams::ProcParams* pp)
{
    disableListener ();

    setEnabled(pp->raw.bayersensor.enable_black);
    
    PextwoGreen->setValue (pp->raw.bayersensor.twogreen);

    PexBlack0->setValue (pp->raw.bayersensor.black0);//black
    PexBlack1->setValue (pp->raw.bayersensor.black1);//black
    PexBlack2->setValue (pp->raw.bayersensor.black2);//black

    if(!PextwoGreen->getLastActive()) {
        PexBlack3->setValue (pp->raw.bayersensor.black3);
    } else {
        PexBlack3->setValue (PexBlack0->getValue());
    }

    enableListener ();
}

void BayerRAWExposure::write( rtengine::procparams::ProcParams* pp)
{
    pp->raw.bayersensor.enable_black = getEnabled();
    pp->raw.bayersensor.black0 = PexBlack0->getValue();// black
    pp->raw.bayersensor.black1 = PexBlack1->getValue();// black
    pp->raw.bayersensor.black2 = PexBlack2->getValue();// black
    pp->raw.bayersensor.twogreen = PextwoGreen->getLastActive();

    if(PextwoGreen->getLastActive()) {
        pp->raw.bayersensor.black3 = pp->raw.bayersensor.black0;   // active or desactive 2 green together
    } else {
        pp->raw.bayersensor.black3 = PexBlack3->getValue();
    }
}

void BayerRAWExposure::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        Glib::ustring value = a->getTextValue();

        if (a == PexBlack0) {
            if(!PextwoGreen->getLastActive()) {
                listener->panelChanged (EvPreProcessExpBlackzero,  value );
            } else {
                listener->panelChanged (EvPreProcessExpBlackzero,  value );
                PexBlack3->setValue (PexBlack0->getValue());
            }
        } else if (a == PexBlack1) {
            listener->panelChanged (EvPreProcessExpBlackone,  value );
        } else if (a == PexBlack2) {
            listener->panelChanged (EvPreProcessExpBlacktwo,  value );
        } else if (a == PexBlack3)    {
            if(!PextwoGreen->getLastActive()) {
                listener->panelChanged (EvPreProcessExpBlackthree,  value );
            } else {
                listener->panelChanged (EvPreProcessExpBlackthree,  value );
                PexBlack0->setValue (PexBlack3->getValue());
            }
        }
    } else if (PextwoGreen->getLastActive()) {
        if (a == PexBlack0) {
            PexBlack3->setValue(PexBlack0->getValue());
        } else if (a == PexBlack3) {
            PexBlack0->setValue(PexBlack3->getValue());
        }
    }
}

void BayerRAWExposure::adjusterAutoToggled(Adjuster* a, bool newval)
{
}

void BayerRAWExposure::checkBoxToggled (CheckBox* c, CheckValue newval)
{
    if (c == PextwoGreen) {
        if (listener && getEnabled()) {
            listener->panelChanged (EvPreProcessExptwoGreen, PextwoGreen->getLastActive() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
        }
        if (PextwoGreen->getLastActive()) {
            PexBlack3->setValue (PexBlack0->getValue());//two green together
        }
    }
}

void BayerRAWExposure::setDefaults(const rtengine::procparams::ProcParams* defParams)
{
    PexBlack0->setDefault( defParams->raw.bayersensor.black0);
    PexBlack1->setDefault( defParams->raw.bayersensor.black1);
    PexBlack2->setDefault( defParams->raw.bayersensor.black2);
    PexBlack3->setDefault( defParams->raw.bayersensor.black3);

    initial_params = defParams->raw.bayersensor;
}


void BayerRAWExposure::trimValues (rtengine::procparams::ProcParams* pp)
{

    PexBlack0->trimValue(pp->raw.bayersensor.black0);
    PexBlack1->trimValue(pp->raw.bayersensor.black1);
    PexBlack2->trimValue(pp->raw.bayersensor.black2);
    PexBlack3->trimValue(pp->raw.bayersensor.black3);
}


void BayerRAWExposure::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.raw.bayersensor = initial_params;
    }
    pp.raw.bayersensor.enable_black = getEnabled();
    read(&pp);
}
