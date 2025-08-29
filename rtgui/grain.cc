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
#include "grain.h"
#include "eventmapper.h"
#include <iomanip>
#include <cmath>

using namespace rtengine;
using namespace rtengine::procparams;

FilmGrain::FilmGrain(): FoldableToolPanel(this, "grain", M("TP_GRAIN_LABEL"), true, true, true)
{
    auto m = ProcEventMapper::getInstance();
    EvEnabled = m->newEvent(rtengine::DIRPYREQUALIZER, "HISTORY_MSG_GRAIN_ENABLED");
    EvStrength = m->newEvent(rtengine::DIRPYREQUALIZER, "HISTORY_MSG_GRAIN_STRENGTH");
    EvISO = m->newEvent(rtengine::DIRPYREQUALIZER, "HISTORY_MSG_GRAIN_ISO");
    EvColor = m->newEvent(rtengine::DIRPYREQUALIZER, "HISTORY_MSG_GRAIN_COLOR");
    EvToolReset.set_action(rtengine::DIRPYREQUALIZER);

    Gtk::HBox *hb = Gtk::manage (new Gtk::HBox ());
    hb->pack_start(*Gtk::manage(new Gtk::Label(M("TP_GRAIN_MODE") + ": ")), Gtk::PACK_SHRINK);
    color = Gtk::manage(new MyComboBoxText());
    color->append(M("TP_GRAIN_BW"));
    color->append(M("TP_GRAIN_COLOR"));
    hb->pack_start(*color);
    color->signal_changed().connect(sigc::mem_fun(*this, &FilmGrain::colorChanged));
    hb->show();
    color->show();
    
    iso = Gtk::manage(new Adjuster(M("TP_GRAIN_ISO"), 100., 6400., 50., 400.));
    iso->setAdjusterListener(this);
    iso->show();

    strength = Gtk::manage(new Adjuster(M("TP_GRAIN_STRENGTH"), 0., 100., 1., 25.));
    strength->setAdjusterListener(this);
    strength->show();

    pack_start(*hb);
    pack_start(*iso);
    pack_start(*strength);
}


void FilmGrain::read(const ProcParams *pp)
{
    disableListener();

    setEnabled(pp->grain.enabled);
    color->set_active(pp->grain.color ? 1 : 0);
    iso->setValue(pp->grain.iso);
    strength->setValue(pp->grain.strength);

    enableListener();
}


void FilmGrain::write(ProcParams *pp)
{
    pp->grain.enabled = getEnabled();
    pp->grain.iso = iso->getValue();
    pp->grain.strength = strength->getValue();
    pp->grain.color = color->get_active_row_number() == 1;
}

void FilmGrain::setDefaults(const ProcParams *defParams)
{
    iso->setDefault(defParams->grain.iso);
    strength->setDefault(defParams->grain.strength);

    initial_params = defParams->grain;
}


void FilmGrain::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        if (a == strength) {
            listener->panelChanged(EvStrength, a->getTextValue());
        } else if (a == iso) {
            listener->panelChanged(EvISO, a->getTextValue());
        }
    }
}


void FilmGrain::enabledChanged ()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged(EvEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged(EvEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvEnabled, M("GENERAL_DISABLED"));
        }
    }
}


void FilmGrain::colorChanged()
{
    if (listener) {
        listener->panelChanged(EvColor, color->get_active_text());
    }
}


void FilmGrain::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.grain = initial_params;
    }
    pp.grain.enabled = getEnabled();
    read(&pp);
}
