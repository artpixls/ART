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
#include <iomanip>
#include "rotate.h"
#include "eventmapper.h"
#include "guiutils.h"
#include "rtimage.h"

using namespace rtengine;
using namespace rtengine::procparams;

Rotate::Rotate () : FoldableToolPanel(this, "rotate", M("TP_ROTATE_LABEL"), false, true, true)
{
    rlistener = nullptr;

    EvToolEnabled.set_action(rtengine::TRANSFORM);
    EvToolReset.set_action(rtengine::TRANSFORM);

    //TODO the action of the rotation slider is counter-intuitive
    Gtk::Image* irotateL =   Gtk::manage (new RTImage ("rotate-right-small.png"));
    Gtk::Image* irotateR =   Gtk::manage (new RTImage ("rotate-left-small.png"));

    degree = Gtk::manage (new Adjuster (M("TP_ROTATE_DEGREE"), -45, 45, 0.01, 0, irotateL, irotateR));
    degree->setAdjusterListener (this);
    pack_start (*degree);

    selectStraight = Gtk::manage (new Gtk::Button (M("TP_ROTATE_SELECTLINE")));
    selectStraight->set_image (*Gtk::manage (new RTImage ("rotate-straighten-small.png")));
    selectStraight->get_style_context()->add_class("independent");
    pack_start (*selectStraight, Gtk::PACK_SHRINK, 2);

    selectStraight->signal_pressed().connect( sigc::mem_fun(*this, &Rotate::selectStraightPressed) );

    degree->setLogScale(2, 0);

    show_all ();
}

void Rotate::read(const ProcParams* pp)
{
    disableListener ();
    setEnabled(pp->rotate.enabled);
    degree->setValue (pp->rotate.degree);
    enableListener ();
}

void Rotate::write(ProcParams* pp)
{
    pp->rotate.enabled = getEnabled();
    pp->rotate.degree = degree->getValue ();
}

void Rotate::setDefaults(const ProcParams* defParams)
{
    degree->setDefault (defParams->rotate.degree);
    initial_params = defParams->rotate;
}

void Rotate::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        listener->panelChanged(EvROTDegree, Glib::ustring::format (std::setw(3), std::fixed, std::setprecision(2), degree->getValue()));
    }
}

void Rotate::adjusterAutoToggled(Adjuster* a, bool newval)
{
}

void Rotate::straighten (double deg)
{
    setEnabled(true);
    degree->setValue (degree->getValue() + deg);
    degree->setEditedState (Edited);

    if (listener) {
        listener->panelChanged (EvROTDegree, Glib::ustring::format (std::setw(3), std::fixed, std::setprecision(2), degree->getValue()));
    }
}

void Rotate::selectStraightPressed ()
{

    if (rlistener) {
        rlistener->straightenRequested ();
    }
}


void Rotate::trimValues (rtengine::procparams::ProcParams* pp)
{
    degree->trimValue(pp->rotate.degree);
}


void Rotate::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.rotate = initial_params;
    }
    pp.rotate.enabled = getEnabled();
    read(&pp);
}
