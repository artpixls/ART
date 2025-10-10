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
#include "rawcacorrection.h"
#include "eventmapper.h"
#include "guiutils.h"
#include "rtimage.h"

using namespace rtengine;
using namespace rtengine::procparams;

RAWCACorr::RAWCACorr () : FoldableToolPanel(this, "rawcacorrection", M("TP_RAWCACORR_LABEL"), false, true, true)
{
    EvToolEnabled.set_action(rtengine::DARKFRAME);
    EvToolReset.set_action(rtengine::DARKFRAME);
    
    auto m = ProcEventMapper::getInstance();
    EvPreProcessCAAutoiterations = m->newEvent(rtengine::DARKFRAME, "HISTORY_MSG_RAWCACORR_AUTOIT");
    EvPreProcessCAColourshift = m->newEvent(rtengine::DARKFRAME, "HISTORY_MSG_RAWCACORR_COLORSHIFT");
    EvPreProcessCAColourshiftHistory = m->newEvent(rtengine::M_VOID, "HISTORY_MSG_RAWCACORR_COLORSHIFT");

    Gtk::Image* icaredL =   Gtk::manage (new RTImage ("circle-red-cyan-small.svg"));
    Gtk::Image* icaredR =   Gtk::manage (new RTImage ("circle-cyan-red-small.svg"));
    Gtk::Image* icablueL =  Gtk::manage (new RTImage ("circle-blue-yellow-small.svg"));
    Gtk::Image* icablueR =  Gtk::manage (new RTImage ("circle-yellow-blue-small.svg"));

    caAutocorrect = Gtk::manage (new CheckBox(M("TP_RAWCACORR_AUTO")));
    caAutocorrect->setCheckBoxListener (this);

    caAutoiterations = Gtk::manage(new Adjuster (M("TP_RAWCACORR_AUTOIT"), 1, 5, 1, 2));
    caAutoiterations->setAdjusterListener (this);
    caAutoiterations->set_tooltip_markup(M("TP_RAWCACORR_AUTOIT_TOOLTIP"));

    if (caAutoiterations->delay < options.adjusterMaxDelay) {
        caAutoiterations->delay = options.adjusterMaxDelay;
    }

    caRed = Gtk::manage(new Adjuster (M("TP_RAWCACORR_CARED"), -4.0, 4.0, 0.1, 0, icaredL, icaredR));
    caRed->setAdjusterListener (this);

    if (caRed->delay < options.adjusterMaxDelay) {
        caRed->delay = options.adjusterMaxDelay;
    }

    caRed->show();
    caBlue = Gtk::manage(new Adjuster (M("TP_RAWCACORR_CABLUE"), -8.0, 8.0, 0.1, 0, icablueL, icablueR));
    caBlue->setAdjusterListener (this);

    if (caBlue->delay < options.adjusterMaxDelay) {
        caBlue->delay = options.adjusterMaxDelay;
    }

    caBlue->show();

    caRed->setLogScale(10, 0);
    caBlue->setLogScale(10, 0);

    pack_start( *caAutocorrect, Gtk::PACK_SHRINK, 4);
    pack_start( *caAutoiterations, Gtk::PACK_SHRINK, 4);
    pack_start( *caRed, Gtk::PACK_SHRINK, 4);
    pack_start( *caBlue, Gtk::PACK_SHRINK, 4);

    caAvoidcolourshift = Gtk::manage (new CheckBox(M("TP_RAWCACORR_AVOIDCOLORSHIFT")));
    caAvoidcolourshift->setCheckBoxListener (this);
    pack_start( *caAvoidcolourshift, Gtk::PACK_SHRINK, 4);


}

void RAWCACorr::read(const rtengine::procparams::ProcParams* pp)
{
    disableListener ();

    setEnabled(pp->raw.enable_ca);
    // disable Red and Blue sliders when caAutocorrect is enabled
    caAutoiterations->set_sensitive(pp->raw.ca_autocorrect);
    caRed->set_sensitive(!pp->raw.ca_autocorrect);
    caBlue->set_sensitive(!pp->raw.ca_autocorrect);
    caAutocorrect->setValue(pp->raw.ca_autocorrect);
    caAvoidcolourshift->setValue(pp->raw.ca_avoidcolourshift);
    caAutoiterations->setValue (pp->raw.caautoiterations);
    caRed->setValue (pp->raw.cared);
    caBlue->setValue (pp->raw.cablue);

    enableListener ();
}

void RAWCACorr::write(rtengine::procparams::ProcParams* pp)
{
    pp->raw.enable_ca = getEnabled();
    pp->raw.ca_autocorrect = caAutocorrect->getLastActive();
    pp->raw.ca_avoidcolourshift = caAvoidcolourshift->getLastActive();
    pp->raw.caautoiterations = caAutoiterations->getValue();
    pp->raw.cared = caRed->getValue();
    pp->raw.cablue = caBlue->getValue();
}

void RAWCACorr::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {

        Glib::ustring value = a->getTextValue();

        if (a == caAutoiterations) {
            listener->panelChanged (EvPreProcessCAAutoiterations,  value );
        } else if (a == caRed) {
            listener->panelChanged (EvPreProcessCARed,  value );
        } else if (a == caBlue) {
            listener->panelChanged (EvPreProcessCABlue,  value );
        }
    }
}

void RAWCACorr::adjusterAutoToggled(Adjuster* a, bool newval)
{
}

void RAWCACorr::checkBoxToggled (CheckBox* c, CheckValue newval)
{
    if (c == caAutocorrect) {
        // disable Red and Blue sliders when caAutocorrect is enabled
        caAutoiterations->set_sensitive(caAutocorrect->getLastActive ());
        caRed->set_sensitive(!caAutocorrect->getLastActive ());
        caBlue->set_sensitive(!caAutocorrect->getLastActive ());
        if (listener) {
            listener->panelChanged (EvPreProcessAutoCA, caAutocorrect->getLastActive() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
        }
    }
     else if (c == caAvoidcolourshift) {
        if (listener) {
            listener->panelChanged ((caAutocorrect->getLastActive() || caRed->getValue() != 0 || caBlue->getValue() != 0) ? EvPreProcessCAColourshift : EvPreProcessCAColourshiftHistory, caAvoidcolourshift->getLastActive() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
        }
    }
}


void RAWCACorr::setDefaults(const rtengine::procparams::ProcParams* defParams)
{
    caAutoiterations->setDefault( defParams->raw.caautoiterations);
    caRed->setDefault( defParams->raw.cared);
    caBlue->setDefault( defParams->raw.cablue);

    initial_params = defParams->raw;
}


void RAWCACorr::trimValues (rtengine::procparams::ProcParams* pp)
{

    caAutoiterations->trimValue(pp->raw.caautoiterations);
    caRed->trimValue(pp->raw.cared);
    caBlue->trimValue(pp->raw.cablue);
}


void RAWCACorr::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.raw = initial_params;
    }
    pp.raw.enable_ca = getEnabled();
    read(&pp);
}
