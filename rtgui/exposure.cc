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
#include "exposure.h"
#include "adjuster.h"
#include <sigc++/slot.h>
#include <iomanip>
#include "ppversion.h"
#include "edit.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;

Exposure::Exposure():
    FoldableToolPanel(this, "exposure", M("TP_EXPOSURE_LABEL"), false, true, true)
{
    auto m = ProcEventMapper::getInstance();
    EvToolEnabled.set_action(rtengine::DARKFRAME);
    EvToolReset.set_action(rtengine::DARKFRAME);
    EvBlack = m->newEvent(rtengine::AUTOEXP, "HISTORY_MSG_EXPOSURE_BLACK");
    EvHRBlur = m->newEvent(rtengine::DARKFRAME, "HISTORY_MSG_EXPOSURE_HRBLUR");

//-------------- Highlight Reconstruction -----------------
    pack_start (*Gtk::manage (new  Gtk::HSeparator()));

    hrmode = Gtk::manage (new MyComboBoxText ());
    hrmode->append(M("TP_HLREC_OFF"));
    hrmode->append(M("TP_HLREC_BLEND"));
    hrmode->append(M("TP_HLREC_COLOR"));
    hrmode->append(M("TP_HLREC_COLORBLEND"));

    hrmode->set_active(ExposureParams::HR_OFF);
    Gtk::HBox *hlrbox = Gtk::manage(new Gtk::HBox());
    Gtk::Label* lab = Gtk::manage(new Gtk::Label(M("TP_HLREC_LABEL") + ": "));
    hlrbox->pack_start(*lab, Gtk::PACK_SHRINK);
    hlrbox->pack_start(*hrmode);
    pack_start (*hlrbox);

    hrmode->signal_changed().connect ( sigc::mem_fun(*this, &Exposure::hrmodeChanged) );

    hrblur = Gtk::manage(new Adjuster(M("TP_EXPOSURE_HRBLUR"), 0, 3, 1, 0));
    pack_start(*hrblur);
    hrblur->setAdjusterListener(this);

    //----------- Exposure Compensation ---------------------
    pack_start(*Gtk::manage(new Gtk::HSeparator()));

    expcomp = Gtk::manage(new Adjuster(M("TP_EXPOSURE_EXPCOMP"), -12, 12, 0.05, 0));
    expcomp->setLogScale(64, 0, true);
    pack_start(*expcomp);

    black = Gtk::manage(new Adjuster(M("TP_EXPOSURE_BLACK"), -2, 2, 0.001, 0));
    black->setLogScale(100, 0, true);
    pack_start(*black);

    expcomp->setAdjusterListener(this);
    black->setAdjusterListener(this);
}


void Exposure::read(const ProcParams* pp)
{
    disableListener();

    setEnabled(pp->exposure.enabled);
    expcomp->setValue(pp->exposure.expcomp);
    hrmode->set_active(pp->exposure.hrmode);
    black->setValue(pp->exposure.black);
    hrblur->setValue(pp->exposure.hrblur);
    showHRBlur();

    enableListener();
}


void Exposure::write(ProcParams *pp)
{
    pp->exposure.enabled = getEnabled();
    pp->exposure.expcomp = expcomp->getValue();
    pp->exposure.hrmode = ExposureParams::HighlightReconstruction(hrmode->get_active_row_number());
    pp->exposure.black = black->getValue();
    pp->exposure.hrblur = hrblur->getValue();
}

void Exposure::hrmodeChanged ()
{
    if (listener && getEnabled()) {
        listener->panelChanged(EvHRMethod, hrmode->get_active_text());
    }
    showHRBlur();
}


void Exposure::setRaw(bool raw)
{
    disableListener();
    if (raw) {
        hrmode->set_sensitive(true);
    } else {
        hrmode->set_active(0);
        hrmode->set_sensitive(false);
    }
    showHRBlur();
    enableListener();
}


void Exposure::setDefaults(const ProcParams* defParams)
{
    expcomp->setDefault(defParams->exposure.expcomp);
    black->setDefault(defParams->exposure.black);
    hrblur->setDefault(defParams->exposure.hrblur);

    initial_params = defParams->exposure;
}


void Exposure::adjusterChanged(Adjuster* a, double newval)
{
    if (!listener || !getEnabled()) {
        return;
    }

    Glib::ustring costr;

    if (a == expcomp) {
        costr = Glib::ustring::format (std::setw(3), std::fixed, std::setprecision(2), a->getValue());
        listener->panelChanged (EvExpComp, costr);
    } else if (a == black) {
        listener->panelChanged(EvBlack, a->getTextValue());
    } else if (a == hrblur) {
        listener->panelChanged(EvHRBlur, a->getTextValue());
    }
}


void Exposure::adjusterAutoToggled(Adjuster* a, bool newval)
{
}


void Exposure::trimValues (rtengine::procparams::ProcParams* pp)
{
    expcomp->trimValue(pp->exposure.expcomp);
    black->trimValue(pp->exposure.black);
    hrblur->trimValue(pp->exposure.hrblur);
}


void Exposure::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.exposure = initial_params;
    }
    pp.exposure.enabled = getEnabled();
    read(&pp);
}


void Exposure::showHRBlur()
{
    hrblur->set_visible(hrmode->get_active_row_number() == 2);
}


void Exposure::registerShortcuts(ToolShortcutManager *mgr)
{
    mgr->addShortcut(GDK_KEY_e, this, expcomp);
    mgr->addShortcut(GDK_KEY_q, this, black);
}
