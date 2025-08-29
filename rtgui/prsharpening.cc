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
#include <cmath>
#include "prsharpening.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;

PrSharpening::PrSharpening () : FoldableToolPanel(this, "prsharpening", M("TP_PRSHARPENING_LABEL"), false, true, true)
{
    auto m = ProcEventMapper::getInstance();
    EvPrShrContrast = m->newEvent(rtengine::M_LUMINANCE, "HISTORY_MSG_PRSHARPEN_CONTRAST");
    EvToolReset.set_action(rtengine::M_LUMINANCE);

    std::vector<GradientMilestone> milestones;
    milestones.push_back( GradientMilestone(0.0, 0.0, 0.0, 0.0) );
    milestones.push_back( GradientMilestone(1.0, 1.0, 1.0, 1.0) );

    //setEnabledTooltipMarkup(M("TP_PRSHARPENING_TOOLTIP"));

    Gtk::HBox* hb = Gtk::manage (new Gtk::HBox ());
    hb->show ();
    contrast = Gtk::manage(new Adjuster (M("TP_SHARPENING_CONTRAST"), 0, 200, 1, 15));
    contrast->setAdjusterListener (this);
    pack_start(*contrast);
    contrast->show();

    Gtk::Label* ml = Gtk::manage (new Gtk::Label (M("TP_SHARPENING_METHOD") + ":"));
    ml->show ();
    method = Gtk::manage (new MyComboBoxText ());
    method->append (M("TP_SHARPENING_USM"));
    method->append (M("TP_SHARPENING_RLD"));
    method->show ();
    hb->pack_start(*ml, Gtk::PACK_SHRINK, 4);
    hb->pack_start(*method);
    pack_start (*hb);

    rld = new Gtk::VBox ();
    dradius = Gtk::manage (new Adjuster (M("TP_SHARPENING_EDRADIUS"), 0.4, 2.5, 0.01, 0.45));
    damount = Gtk::manage (new Adjuster (M("TP_SHARPENING_RLD_AMOUNT"), 0.0, 100, 1, 100));
    rld->pack_start (*dradius);
    rld->pack_start (*damount);
    dradius->show ();
    damount->show ();
    rld->show ();

    usm = new Gtk::VBox ();
    usm->show ();

    Gtk::HSeparator *hsep6a = Gtk::manage (new  Gtk::HSeparator());
    amount = Gtk::manage (new Adjuster (M("TP_SHARPENING_AMOUNT"), 1, 1000, 1, 200));
    radius = Gtk::manage (new Adjuster (M("TP_SHARPENING_RADIUS"), 0.3, 3, 0.01, 0.5));
    threshold = Gtk::manage (new ThresholdAdjuster (M("TP_SHARPENING_THRESHOLD"), 0., 2000., 20., 80., 2000., 1200., 0, false));
    threshold->setAdjusterListener (this);
    threshold->setBgGradient(milestones);
    pack_start(*hsep6a, Gtk::PACK_SHRINK, 2);

    pack_start (*usm);

    usm->pack_start(*radius);
    usm->pack_start(*amount);
    usm->pack_start(*threshold);
    hsep6a->show ();
    radius->show ();
    amount->show ();
    threshold->show ();

    Gtk::HSeparator *hsep6 = Gtk::manage (new  Gtk::HSeparator());
    edgesonly = Gtk::manage (new Gtk::CheckButton (M("TP_SHARPENING_ONLYEDGES")));
    edgesonly->set_active (false);
    edgebox = new Gtk::VBox ();
    eradius = Gtk::manage (new Adjuster (M("TP_SHARPENING_EDRADIUS"), 0.5, 2.5, 0.1, 1.9));
    etolerance = Gtk::manage (new Adjuster (M("TP_SHARPENING_EDTOLERANCE"), 10, 10000, 100, 1800));
    usm->pack_start(*hsep6, Gtk::PACK_SHRINK, 2);
    usm->pack_start(*edgesonly);
    edgebox->pack_start(*eradius);
    edgebox->pack_start(*etolerance);
    edgebox->show ();
    edgebin = Gtk::manage (new Gtk::VBox ());
    usm->pack_start (*edgebin);
    edgebin->show ();
    hsep6->show();
    edgesonly->show();
    eradius->show();
    etolerance->show();

    Gtk::HSeparator *hsep6b = Gtk::manage (new  Gtk::HSeparator());
    halocontrol = Gtk::manage (new Gtk::CheckButton (M("TP_SHARPENING_HALOCONTROL")));
    halocontrol->set_active (false);
    hcbox = new Gtk::VBox ();
    hcamount = Gtk::manage (new Adjuster (M("TP_SHARPENING_HCAMOUNT"), 1, 100, 1, 75));
    usm->pack_start(*hsep6b, Gtk::PACK_SHRINK, 2);
    usm->pack_start(*halocontrol);
    hcbox->pack_start(*hcamount);
    hcbox->show ();
    hcbin = Gtk::manage (new Gtk::VBox ());
    usm->pack_start (*hcbin);
    hcbin->show ();
    hsep6b->show ();
    halocontrol->show ();
    hcamount->show ();

    dradius->setAdjusterListener (this);
    damount->setAdjusterListener (this);
    radius->setAdjusterListener (this);
    amount->setAdjusterListener (this);
    eradius->setAdjusterListener (this);
    etolerance->setAdjusterListener (this);
    hcamount->setAdjusterListener (this);

    edgebox->reference ();
    hcbox->reference ();
    usm->reference ();
    rld->reference ();

    eonlyConn = edgesonly->signal_toggled().connect( sigc::mem_fun(*this, &PrSharpening::edgesonly_toggled) );
    hcConn    = halocontrol->signal_toggled().connect( sigc::mem_fun(*this, &PrSharpening::halocontrol_toggled) );
    method->signal_changed().connect( sigc::mem_fun(*this, &PrSharpening::method_changed) );
}

PrSharpening::~PrSharpening ()
{

    delete usm;
    delete rld;
    delete edgebox;
    delete hcbox;
}


void PrSharpening::read(const ProcParams* pp)
{

    disableListener ();

    setEnabled (pp->prsharpening.enabled);

    eonlyConn.block (true);
    edgesonly->set_active (pp->prsharpening.edgesonly);
    eonlyConn.block (false);
    lastEdgesOnly = pp->prsharpening.edgesonly;

    hcConn.block (true);
    halocontrol->set_active (pp->prsharpening.halocontrol);
    hcConn.block (false);
    lastHaloControl = pp->prsharpening.halocontrol;

    contrast->setValue      (pp->prsharpening.contrast);
    amount->setValue        (pp->prsharpening.amount);
    radius->setValue        (pp->prsharpening.radius);
    threshold->setValue<int>(pp->prsharpening.threshold);
    eradius->setValue       (pp->prsharpening.edges_radius);
    etolerance->setValue    (pp->prsharpening.edges_tolerance);
    hcamount->setValue      (pp->prsharpening.halocontrol_amount);

    dradius->setValue       (pp->prsharpening.deconvradius);
    damount->setValue       (pp->prsharpening.deconvamount);

    removeIfThere (edgebin, edgebox, false);

    if (edgesonly->get_active ()) {
        edgebin->pack_start (*edgebox);
    }

    removeIfThere (hcbin, hcbox, false);

    if (halocontrol->get_active ()) {
        hcbin->pack_start (*hcbox);
    }

    if (pp->prsharpening.method == "usm") {
        method->set_active (0);
    } else if (pp->prsharpening.method == "rld") {
        method->set_active (1);
    }

    enableListener ();
}

void PrSharpening::write(ProcParams* pp)
{

    pp->prsharpening.contrast         = contrast->getValue ();
    pp->prsharpening.amount           = (int)amount->getValue();
    pp->prsharpening.enabled          = getEnabled ();
    pp->prsharpening.radius           = radius->getValue ();
    pp->prsharpening.threshold        = threshold->getValue<int> ();
    pp->prsharpening.edgesonly        = edgesonly->get_active ();
    pp->prsharpening.edges_radius     = eradius->getValue ();
    pp->prsharpening.edges_tolerance  = (int)etolerance->getValue ();
    pp->prsharpening.halocontrol      = halocontrol->get_active ();
    pp->prsharpening.halocontrol_amount = (int)hcamount->getValue ();
    pp->prsharpening.deconvradius     = dradius->getValue ();
    pp->prsharpening.deconvamount     = (int)damount->getValue ();

    if (method->get_active_row_number() == 0) {
        pp->prsharpening.method = "usm";
    } else if (method->get_active_row_number() == 1) {
        pp->prsharpening.method = "rld";
    }
}

void PrSharpening::setDefaults(const ProcParams* defParams)
{
    contrast->setDefault (defParams->prsharpening.contrast);
    amount->setDefault (defParams->prsharpening.amount);
    radius->setDefault (defParams->prsharpening.radius);
    threshold->setDefault<int> (defParams->prsharpening.threshold);
    eradius->setDefault (defParams->prsharpening.edges_radius);
    etolerance->setDefault (defParams->prsharpening.edges_tolerance);
    hcamount->setDefault (defParams->prsharpening.halocontrol_amount);
    damount->setDefault (defParams->prsharpening.deconvamount);
    dradius->setDefault (defParams->prsharpening.deconvradius);

    initial_params = defParams->prsharpening;
}

void PrSharpening::adjusterChanged (Adjuster* a, double newval)
{
    if (listener && getEnabled() ) {

        Glib::ustring costr;

        if (a == radius || a == dradius) {
            costr = Glib::ustring::format (std::setw(3), std::fixed, std::setprecision(2), a->getValue());
        } else if (a == eradius) {
            costr = Glib::ustring::format (std::setw(2), std::fixed, std::setprecision(1), a->getValue());
        } else {
            costr = Glib::ustring::format ((int)a->getValue());
        }

        if (a == contrast) {
            listener->panelChanged (EvPrShrContrast, costr);
        } else if (a == amount) {
            listener->panelChanged (EvPrShrAmount, costr);
        } else if (a == radius) {
            listener->panelChanged (EvPrShrRadius, costr);
        } else if (a == eradius) {
            listener->panelChanged (EvPrShrEdgeRadius, costr);
        } else if (a == etolerance) {
            listener->panelChanged (EvPrShrEdgeTolerance, costr);
        } else if (a == hcamount) {
            listener->panelChanged (EvPrShrHaloAmount, costr);
        } else if (a == dradius) {
            listener->panelChanged (EvPrShrDRadius, costr);
        } else if (a == damount) {
            listener->panelChanged (EvPrShrDAmount, costr);
        }
    }
}

void PrSharpening::adjusterAutoToggled(Adjuster* a, bool newval)
{
}

void PrSharpening::enabledChanged ()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged (EvPrShrEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged (EvPrShrEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged (EvPrShrEnabled, M("GENERAL_DISABLED"));
        }
    }
}

void PrSharpening::edgesonly_toggled ()
{
    removeIfThere (edgebin, edgebox, false);

    if (edgesonly->get_active ()) {
        edgebin->pack_start (*edgebox);
    }

    if (listener && getEnabled() ) {
        if (edgesonly->get_inconsistent()) {
            listener->panelChanged (EvPrShrEdgeOnly, M("GENERAL_INITIALVALUES"));
        } else if (edgesonly->get_active ()) {
            listener->panelChanged (EvPrShrEdgeOnly, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged (EvPrShrEdgeOnly, M("GENERAL_DISABLED"));
        }
    }
}

void PrSharpening::halocontrol_toggled ()
{
    removeIfThere (hcbin, hcbox, false);

    if (halocontrol->get_active ()) {
        hcbin->pack_start (*hcbox);
    }

    if (listener && getEnabled() ) {
        if (halocontrol->get_inconsistent()) {
            listener->panelChanged (EvPrShrHaloControl, M("GENERAL_INITIALVALUES"));
        } else if (halocontrol->get_active ()) {
            listener->panelChanged (EvPrShrHaloControl, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged (EvPrShrHaloControl, M("GENERAL_DISABLED"));
        }
    }
}

void PrSharpening::method_changed ()
{
    removeIfThere (this, usm, false);
    removeIfThere (this, rld, false);

    if (method->get_active_row_number() == 0) {
        pack_start (*usm);
    } else if (method->get_active_row_number() == 1) {
        pack_start (*rld);
    }

    if (listener && getEnabled() ) {
        listener->panelChanged (EvPrShrMethod, method->get_active_text ());
    }

}

void PrSharpening::adjusterChanged(ThresholdAdjuster* a, double newBottom, double newTop)
{
}

void PrSharpening::adjusterChanged(ThresholdAdjuster* a, double newBottomLeft, double newTopLeft, double newBottomRight, double newTopRight)
{
}

void PrSharpening::adjusterChanged(ThresholdAdjuster* a, int newBottom, int newTop)
{
}

void PrSharpening::adjusterChanged(ThresholdAdjuster* a, int newBottomLeft, int newTopLeft, int newBottomRight, int newTopRight)
{
    if (listener && getEnabled() ) {
        if(a == threshold) {
            listener->panelChanged (EvPrShrThresh, threshold->getHistoryString());
        }
    }
}

void PrSharpening::adjusterChanged2(ThresholdAdjuster* a, int newBottomL, int newTopL, int newBottomR, int newTopR)
{
}


void PrSharpening::trimValues (rtengine::procparams::ProcParams* pp)
{
    contrast->trimValue(pp->prsharpening.contrast);
    radius->trimValue(pp->prsharpening.radius);
    dradius->trimValue(pp->prsharpening.deconvradius);
    amount->trimValue(pp->prsharpening.amount);
    damount->trimValue(pp->prsharpening.deconvamount);
    eradius->trimValue(pp->prsharpening.edges_radius);
    etolerance->trimValue(pp->prsharpening.edges_tolerance);
    hcamount->trimValue(pp->prsharpening.halocontrol_amount);
}


void PrSharpening::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.prsharpening = initial_params;
    }
    pp.prsharpening.enabled = getEnabled();
    read(&pp);
}
