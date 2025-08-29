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
#include "vignetting.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;

Vignetting::Vignetting () : FoldableToolPanel(this, "vignetting", M("TP_VIGNETTING_LABEL"), false, true, true)
{
    EvToolEnabled.set_action(rtengine::TRANSFORM);
    EvToolReset.set_action(rtengine::TRANSFORM);

    amount = Gtk::manage (new Adjuster (M("TP_VIGNETTING_AMOUNT"), -100, 100, 1, 0));
    amount->setAdjusterListener (this);

    radius = Gtk::manage (new Adjuster (M("TP_VIGNETTING_RADIUS"), 0, 100, 1, 50));
    radius->setAdjusterListener (this);

    strength = Gtk::manage (new Adjuster (M("TP_VIGNETTING_STRENGTH"), 1, 100, 1, 1));
    strength->setAdjusterListener (this);

    centerX = Gtk::manage (new Adjuster (M("TP_VIGNETTING_CENTER_X"), -100, 100, 1, 0));
    centerX->setAdjusterListener (this);

    centerY = Gtk::manage (new Adjuster (M("TP_VIGNETTING_CENTER_Y"), -100, 100, 1, 0));
    centerY->setAdjusterListener (this);

    pack_start (*amount);
    pack_start (*radius);
    pack_start (*strength);
    pack_start (*centerX);
    pack_start (*centerY);

    show_all();
}

void Vignetting::read(const ProcParams* pp)
{
    disableListener ();

    setEnabled(pp->vignetting.enabled);
    amount->setValue (pp->vignetting.amount);
    radius->setValue (pp->vignetting.radius);
    strength->setValue (pp->vignetting.strength);
    centerX->setValue (pp->vignetting.centerX);
    centerY->setValue (pp->vignetting.centerY);

    enableListener ();
}

void Vignetting::write(ProcParams* pp)
{
    pp->vignetting.enabled = getEnabled();
    pp->vignetting.amount = amount->getIntValue ();
    pp->vignetting.radius = radius->getIntValue ();
    pp->vignetting.strength = strength->getIntValue ();
    pp->vignetting.centerX = centerX->getIntValue ();
    pp->vignetting.centerY = centerY->getIntValue ();
}

void Vignetting::setDefaults(const ProcParams* defParams)
{
    amount->setDefault (defParams->vignetting.amount);
    radius->setDefault (defParams->vignetting.radius);
    strength->setDefault (defParams->vignetting.strength);
    centerX->setDefault (defParams->vignetting.centerX);
    centerY->setDefault (defParams->vignetting.centerY);

    initial_params = defParams->vignetting;
}

void Vignetting::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled())  {
        if (a == amount) {
            listener->panelChanged (EvVignettingAmount, amount->getTextValue());
        } else if (a == radius) {
            listener->panelChanged (EvVignettingRadius, radius->getTextValue());
        } else if (a == strength) {
            listener->panelChanged (EvVignettingStrenght, strength->getTextValue());
        } else if (a == centerX || a == centerY) {
            listener->panelChanged (EvVignettingCenter, Glib::ustring::compose ("X=%1\nY=%2", centerX->getTextValue(), centerY->getTextValue()));
        }
    }
}

void Vignetting::adjusterAutoToggled(Adjuster* a, bool newval)
{
}

void Vignetting::trimValues (rtengine::procparams::ProcParams* pp)
{

    amount->trimValue(pp->vignetting.amount);
    radius->trimValue(pp->vignetting.radius);
    strength->trimValue(pp->vignetting.strength);
    centerX->trimValue(pp->vignetting.centerX);
    centerY->trimValue(pp->vignetting.centerY);
}


void Vignetting::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.vignetting = initial_params;
    }
    pp.vignetting.enabled = getEnabled();
    read(&pp);
}
