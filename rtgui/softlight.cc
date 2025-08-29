/** -*- C++ -*-
 *  
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2018 Alberto Griggio <alberto.griggio@gmail.com>
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
#include "softlight.h"
#include "eventmapper.h"
#include <iomanip>
#include <cmath>

using namespace rtengine;
using namespace rtengine::procparams;

SoftLight::SoftLight(): FoldableToolPanel(this, "softlight", M("TP_SOFTLIGHT_LABEL"), false, true, true)
{
    auto m = ProcEventMapper::getInstance();
    EvSoftLightEnabled = m->newEvent(rtengine::M_LUMINANCE, "HISTORY_MSG_SOFTLIGHT_ENABLED");
    EvSoftLightStrength = m->newEvent(rtengine::M_LUMINANCE, "HISTORY_MSG_SOFTLIGHT_STRENGTH");
    EvToolEnabled.set_action(rtengine::M_LUMINANCE);
    
    strength = Gtk::manage(new Adjuster(M("TP_SOFTLIGHT_STRENGTH"), 0., 100., 1., 30.));
    strength->setAdjusterListener(this);
    strength->show();

    pack_start(*strength);
}


void SoftLight::read(const ProcParams *pp)
{
    disableListener();

    setEnabled(pp->softlight.enabled);
    strength->setValue(pp->softlight.strength);

    enableListener();
}


void SoftLight::write(ProcParams *pp)
{
    pp->softlight.strength = strength->getValue();
    pp->softlight.enabled = getEnabled();
}

void SoftLight::setDefaults(const ProcParams *defParams)
{
    strength->setDefault(defParams->softlight.strength);
    initial_params = defParams->softlight;
}


void SoftLight::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        listener->panelChanged(EvSoftLightStrength, a->getTextValue());
    }
}


void SoftLight::adjusterAutoToggled(Adjuster* a, bool newval)
{
}


void SoftLight::enabledChanged ()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged(EvSoftLightEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged(EvSoftLightEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvSoftLightEnabled, M("GENERAL_DISABLED"));
        }
    }
}


void SoftLight::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.softlight = initial_params;
    }
    pp.softlight.enabled = getEnabled();
    read(&pp);
}


void SoftLight::registerShortcuts(ToolShortcutManager *mgr)
{
    mgr->addShortcut(GDK_KEY_o, this, strength);
}
