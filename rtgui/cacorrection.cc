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
#include "cacorrection.h"
#include <iomanip>
#include "rtimage.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;

CACorrection::CACorrection () : FoldableToolPanel(this, "cacorrection", M("TP_CACORRECTION_LABEL"), false, true, true)
{
    EvToolEnabled.set_action(rtengine::TRANSFORM);
    EvToolReset.set_action(rtengine::TRANSFORM);

    Gtk::Image* icaredL =   Gtk::manage (new RTImage ("circle-red-cyan-small.svg"));
    Gtk::Image* icaredR =   Gtk::manage (new RTImage ("circle-cyan-red-small.svg"));
    Gtk::Image* icablueL =  Gtk::manage (new RTImage ("circle-blue-yellow-small.svg"));
    Gtk::Image* icablueR =  Gtk::manage (new RTImage ("circle-yellow-blue-small.svg"));

    red = Gtk::manage (new Adjuster (M("TP_CACORRECTION_RED"), -0.005, 0.005, 0.0001, 0, icaredL, icaredR));
    red->setAdjusterListener (this);

    blue = Gtk::manage (new Adjuster (M("TP_CACORRECTION_BLUE"), -0.005, 0.005, 0.0001, 0, icablueL, icablueR));
    blue->setAdjusterListener (this);

    pack_start (*red);
    pack_start (*blue);

    red->setLogScale(10, 0);
    blue->setLogScale(10, 0);

    show_all();
}

void CACorrection::read (const ProcParams* pp)
{
    disableListener ();

    setEnabled(pp->cacorrection.enabled);
    red->setValue (pp->cacorrection.red);
    blue->setValue (pp->cacorrection.blue);

    enableListener ();
}

void CACorrection::write (ProcParams* pp)
{
    pp->cacorrection.enabled = getEnabled();
    pp->cacorrection.red  = red->getValue ();
    pp->cacorrection.blue = blue->getValue ();
}

void CACorrection::setDefaults (const ProcParams* defParams)
{
    red->setDefault (defParams->cacorrection.red);
    blue->setDefault (defParams->cacorrection.blue);
    initial_params = defParams->cacorrection;
}

void CACorrection::adjusterChanged (Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        listener->panelChanged (EvCACorr, Glib::ustring::compose ("%1=%3\n%2=%4", M("TP_CACORRECTION_RED"), M("TP_CACORRECTION_BLUE"), Glib::ustring::format (std::setw(5), std::fixed, std::setprecision(4), red->getValue()), Glib::ustring::format (std::setw(5), std::fixed, std::setprecision(4), blue->getValue())));
    }
}

void CACorrection::adjusterAutoToggled(Adjuster* a, bool newval)
{
}


void CACorrection::trimValues (rtengine::procparams::ProcParams* pp)
{

    red->trimValue(pp->cacorrection.red);
    blue->trimValue(pp->cacorrection.blue);
}


void CACorrection::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.cacorrection = initial_params;
    }
    pp.cacorrection.enabled = getEnabled();
    read(&pp);
}
