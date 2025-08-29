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
#include "saturation.h"
#include "adjuster.h"
#include <sigc++/slot.h>
#include <iomanip>
#include "ppversion.h"
#include "edit.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;

Saturation::Saturation():
    FoldableToolPanel(this, "saturation", M("TP_SATURATION_LABEL"), false, true, true)
{
    auto m = ProcEventMapper::getInstance();
    EvVibrance = m->newEvent(rtengine::LUMINANCECURVE, "HISTORY_MSG_SATURATION_VIBRANCE");
    EvToolEnabled.set_action(rtengine::LUMINANCECURVE);
    EvToolReset.set_action(rtengine::LUMINANCECURVE);
    // autolevels = nullptr;
    
    saturation = Gtk::manage (new Adjuster (M("TP_SATURATION_SATURATION"), -100, 100, 1, 0));
    pack_start (*saturation);
    vibrance = Gtk::manage (new Adjuster (M("TP_SATURATION_VIBRANCE"), -100, 100, 1, 0));
    pack_start(*vibrance);

    saturation->setLogScale(2, 0, true);
    vibrance->setLogScale(2, 0, true);

    saturation->setAdjusterListener(this);
    vibrance->setAdjusterListener(this);
}


Saturation::~Saturation ()
{
}


void Saturation::read(const ProcParams* pp)
{
    disableListener ();

    setEnabled(pp->saturation.enabled);
    saturation->setValue(pp->saturation.saturation);
    vibrance->setValue(pp->saturation.vibrance);

    enableListener ();
}


void Saturation::write(ProcParams* pp)
{
    pp->saturation.enabled = getEnabled();
    pp->saturation.saturation = (int)saturation->getValue ();
    pp->saturation.vibrance = (int)vibrance->getValue ();
}


void Saturation::setDefaults (const ProcParams* defParams)
{
    saturation->setDefault(defParams->saturation.saturation);
    vibrance->setDefault(defParams->saturation.vibrance);
}


void Saturation::adjusterChanged(Adjuster* a, double newval)
{
    if (!listener || !getEnabled()) {
        return;
    }

    Glib::ustring costr = Glib::ustring::format ((int)a->getValue());

    if (a == saturation) {
        listener->panelChanged(EvSaturation, costr);
    } else if (a == vibrance) {
        listener->panelChanged(EvVibrance, costr);
    }
}

void Saturation::adjusterAutoToggled(Adjuster* a, bool newval)
{
}


void Saturation::trimValues (rtengine::procparams::ProcParams* pp)
{
    saturation->trimValue(pp->saturation.saturation);
    vibrance->trimValue(pp->saturation.vibrance);

    initial_params = pp->saturation;
}


void Saturation::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.saturation = initial_params;
    }
    pp.saturation.enabled = getEnabled();
    read(&pp);
}


void Saturation::registerShortcuts(ToolShortcutManager *mgr)
{
    mgr->addShortcut(GDK_KEY_s, this, saturation);
    mgr->addShortcut(GDK_KEY_v, this, vibrance);
}
