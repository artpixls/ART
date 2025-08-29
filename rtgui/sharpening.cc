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
#include "sharpening.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;

Sharpening::Sharpening() : FoldableToolPanel(this, "sharpening", M("TP_SHARPENING_LABEL"), true, true, true)
{
    auto m = ProcEventMapper::getInstance();
    EvSharpenContrast = m->newEvent(rtengine::SHARPENING, "HISTORY_MSG_SHARPENING_CONTRAST");
    EvAutoRadiusOn = m->newEvent(rtengine::SHARPENING | rtengine::M_AUTOEXP, "HISTORY_MSG_SHARPENING_AUTORADIUS");
    EvAutoRadiusOff = m->newEvent(rtengine::M_VOID, "HISTORY_MSG_SHARPENING_AUTORADIUS");
    EvDeconvCornerBoost = m->newEvent(rtengine::SHARPENING, "HISTORY_MSG_SHARPENING_RLD_CORNERRADIUS");
    EvDeconvCornerLatitude = m->newEvent(rtengine::SHARPENING, "HISTORY_MSG_SHARPENING_RLD_CORNERLATITUDE");
    EvPSFKernel = m->newEvent(rtengine::SHARPENING, "HISTORY_MSG_SHARPENING_PSF_KERNEL");
    EvPSFIterations = m->newEvent(rtengine::SHARPENING, "HISTORY_MSG_SHARPENING_PSF_ITERATIONS");
    EvToolReset.set_action(rtengine::SHARPENING);

    Gtk::HBox* hb = Gtk::manage (new Gtk::HBox ());
    hb->show ();
    contrast = Gtk::manage(new Adjuster (M("TP_SHARPENING_CONTRAST"), 0, 200, 1, 20));
    contrast->setAdjusterListener (this);
    pack_start(*contrast);
    contrast->show();

    Gtk::Label* ml = Gtk::manage (new Gtk::Label (M("TP_SHARPENING_METHOD") + ":"));
    ml->show ();
    method = Gtk::manage (new MyComboBoxText ());
    method->append (M("TP_SHARPENING_USM"));
    method->append (M("TP_SHARPENING_RLD"));
    method->append (M("TP_SHARPENING_PSF"));
    method->show ();
    hb->pack_start(*ml, Gtk::PACK_SHRINK, 4);
    hb->pack_start(*method);
    pack_start (*hb);

    rld = new Gtk::VBox ();
    dradius = Gtk::manage (new Adjuster (M("TP_SHARPENING_EDRADIUS"), 0.4, 2.5, 0.01, 0.75));
    dradius->addAutoButton(M("TP_SHARPENING_RLD_AUTORADIUS_TOOLTIP"));
    damount = Gtk::manage (new Adjuster (M("TP_SHARPENING_RLD_AMOUNT"), 0.0, 100, 1, 100));
    deconvCornerBoost = Gtk::manage(new Adjuster(M("TP_SHARPENING_RLD_CORNERRADIUS"), 0.0, 1.0, 0.01, 0));
    deconvCornerLatitude = Gtk::manage(new Adjuster(M("TP_SHARPENING_RLD_CORNERLATITUDE"), 0, 100, 1, 25));
    rld->pack_start(*dradius);
    rld->pack_start(*damount);
    Gtk::Frame *cf = Gtk::manage(new Gtk::Frame(M("TP_SHARPENING_RLD_CORNERBOOST")));
    cf->set_label_align(0.025, 0.5);
    Gtk::VBox *cfb = Gtk::manage(new Gtk::VBox());
    cfb->pack_start(*deconvCornerBoost);
    cfb->pack_start(*deconvCornerLatitude);
    cf->add(*cfb);
    rld->pack_start(*cf);
    rld->show_all_children();
    rld->show();

    usm = new Gtk::VBox ();
    usm->show ();


    Gtk::HSeparator *hsep6a = Gtk::manage (new  Gtk::HSeparator());
    amount = Gtk::manage (new Adjuster (M("TP_SHARPENING_AMOUNT"), 1, 1000, 1, 200));
    radius = Gtk::manage (new Adjuster (M("TP_SHARPENING_RADIUS"), 0.3, 3, 0.01, 0.5));
    threshold = Gtk::manage (new ThresholdAdjuster (M("TP_SHARPENING_THRESHOLD"), 0., 2000., 20., 80., 2000., 1200., 0, false));
    threshold->setAdjusterListener (this);
    pack_start(*hsep6a, Gtk::PACK_SHRINK, 2);

    pack_start (*usm);

    usm->pack_start(*radius);
    usm->pack_start(*threshold);
    usm->pack_start(*amount);
    hsep6a->show ();
    radius->show ();
    threshold->show ();
    amount->show ();

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

    psf = new Gtk::VBox ();
    psf_kernel = Gtk::manage(new MyFileChooserButton(M("TP_SHARPENING_PSF_KERNEL")));
    psf_iterations = Gtk::manage(new Adjuster(M("TP_SHARPENING_PSF_ITERATIONS"), 1, 100, 1, 10));
    hb = Gtk::manage(new Gtk::HBox());
    hb->pack_start(*Gtk::manage(new Gtk::Label(M("TP_SHARPENING_PSF_KERNEL") + ": ")), Gtk::PACK_SHRINK);
    hb->pack_start(*psf_kernel, Gtk::PACK_EXPAND_WIDGET);
    psf->pack_start(*hb);
    psf->pack_start(*psf_iterations);
    psf->show_all_children();
    psf->show();

    dradius->setAdjusterListener (this);
    damount->setAdjusterListener (this);
    radius->setAdjusterListener (this);
    amount->setAdjusterListener (this);
    eradius->setAdjusterListener (this);
    etolerance->setAdjusterListener (this);
    hcamount->setAdjusterListener (this);
    deconvCornerBoost->setAdjusterListener(this);
    deconvCornerLatitude->setAdjusterListener(this);

    const auto on_kernel =
        [this]() -> void
        {
            if (listener && getEnabled()) {
                listener->panelChanged(EvPSFKernel, psf_kernel->get_filename());
            }
        };
    psf_kernel->signal_file_set().connect(sigc::slot<void>(on_kernel));
    psf_iterations->setAdjusterListener(this);

    edgebox->reference ();
    hcbox->reference ();
    usm->reference ();
    rld->reference ();
    psf->reference();

    eonlyConn = edgesonly->signal_toggled().connect( sigc::mem_fun(*this, &Sharpening::edgesonly_toggled) );
    hcConn    = halocontrol->signal_toggled().connect( sigc::mem_fun(*this, &Sharpening::halocontrol_toggled) );
    method->signal_changed().connect( sigc::mem_fun(*this, &Sharpening::method_changed) );
}

Sharpening::~Sharpening ()
{
    idle_register.destroy();

    delete psf;
    delete usm;
    delete rld;
    delete edgebox;
    delete hcbox;
}


void Sharpening::read(const ProcParams* pp)
{
    disableListener();

    setEnabled(pp->sharpening.enabled);

    eonlyConn.block(true);
    edgesonly->set_active(pp->sharpening.edgesonly);
    eonlyConn.block(false);
    lastEdgesOnly = pp->sharpening.edgesonly;

    hcConn.block(true);
    halocontrol->set_active(pp->sharpening.halocontrol);
    hcConn.block(false);
    lastHaloControl = pp->sharpening.halocontrol;

    contrast->setValue(pp->sharpening.contrast);
    // blur->setValue(pp->sharpening.blurradius);
    amount->setValue(pp->sharpening.amount);
    radius->setValue(pp->sharpening.radius);
    threshold->setValue<int>(pp->sharpening.threshold);
    eradius->setValue(pp->sharpening.edges_radius);
    etolerance->setValue(pp->sharpening.edges_tolerance);
    hcamount->setValue(pp->sharpening.halocontrol_amount);

    dradius->setValue(pp->sharpening.deconvradius);
    damount->setValue(pp->sharpening.deconvamount);
    dradius->setAutoValue(pp->sharpening.deconvAutoRadius);
    deconvCornerBoost->setValue(pp->sharpening.deconvCornerBoost);
    deconvCornerLatitude->setValue(pp->sharpening.deconvCornerLatitude);

    removeIfThere(edgebin, edgebox, false);

    if (edgesonly->get_active()) {
        edgebin->pack_start(*edgebox);
    }

    removeIfThere(hcbin, hcbox, false);

    if (halocontrol->get_active()) {
        hcbin->pack_start(*hcbox);
    }

    if (pp->sharpening.method == "usm") {
        method->set_active(0);
    } else if (pp->sharpening.method == "rld") {
        method->set_active(1);
    } else if (pp->sharpening.method == "psf") {
        method->set_active(2);
    }

    psf_kernel->set_filename(pp->sharpening.psf_kernel);
    psf_iterations->setValue(pp->sharpening.psf_iterations);

    enabledChanged();

    enableListener();
}

void Sharpening::write(ProcParams* pp)
{

    pp->sharpening.contrast         = contrast->getValue ();
    // pp->sharpening.blurradius       = blur->getValue ();
    pp->sharpening.amount           = (int)amount->getValue();
    pp->sharpening.enabled          = getEnabled ();
    pp->sharpening.radius           = radius->getValue ();
    pp->sharpening.threshold        = threshold->getValue<int> ();
    pp->sharpening.edgesonly        = edgesonly->get_active ();
    pp->sharpening.edges_radius     = eradius->getValue ();
    pp->sharpening.edges_tolerance  = (int)etolerance->getValue ();
    pp->sharpening.halocontrol      = halocontrol->get_active ();
    pp->sharpening.halocontrol_amount = (int)hcamount->getValue ();
    pp->sharpening.deconvradius     = dradius->getValue ();
    pp->sharpening.deconvamount     = (int)damount->getValue ();
    pp->sharpening.deconvAutoRadius = dradius->getAutoValue();
    pp->sharpening.deconvCornerBoost = deconvCornerBoost->getValue();
    pp->sharpening.deconvCornerLatitude = deconvCornerLatitude->getValue();

    if (method->get_active_row_number() == 0) {
        pp->sharpening.method = "usm";
    } else if (method->get_active_row_number() == 1) {
        pp->sharpening.method = "rld";
    } else if (method->get_active_row_number() == 2) {
        pp->sharpening.method = "psf";
    }
    pp->sharpening.psf_kernel = psf_kernel->get_filename();
    pp->sharpening.psf_iterations = psf_iterations->getValue();
}

void Sharpening::setDefaults(const ProcParams* defParams)
{
    contrast->setDefault (defParams->sharpening.contrast);
    // blur->setDefault (defParams->sharpening.blurradius);
    amount->setDefault (defParams->sharpening.amount);
    radius->setDefault (defParams->sharpening.radius);
    threshold->setDefault<int> (defParams->sharpening.threshold);
    eradius->setDefault (defParams->sharpening.edges_radius);
    etolerance->setDefault (defParams->sharpening.edges_tolerance);
    hcamount->setDefault (defParams->sharpening.halocontrol_amount);
    damount->setDefault (defParams->sharpening.deconvamount);
    dradius->setDefault (defParams->sharpening.deconvradius);
    deconvCornerBoost->setDefault(defParams->sharpening.deconvCornerBoost);
    deconvCornerLatitude->setDefault(defParams->sharpening.deconvCornerLatitude);
    psf_iterations->setDefault(defParams->sharpening.psf_iterations);

    initial_params = defParams->sharpening;
}

void Sharpening::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled() ) {

        Glib::ustring costr;

        if (a == radius || a == dradius /*|| a == blur*/) {
            costr = Glib::ustring::format (std::setw(3), std::fixed, std::setprecision(2), a->getValue());
        } else if (a == eradius) {
            costr = Glib::ustring::format (std::setw(2), std::fixed, std::setprecision(1), a->getValue());
        } else {
            costr = Glib::ustring::format ((int)a->getValue());
        }

        if (a == contrast) {
            listener->panelChanged (EvSharpenContrast, costr);
        } else if (a == amount) {
            listener->panelChanged (EvShrAmount, costr);
        } else if (a == radius) {
            listener->panelChanged (EvShrRadius, costr);
        // } else if (a == blur) {
        //     listener->panelChanged (EvSharpenBlur, costr);
        } else if (a == eradius) {
            listener->panelChanged (EvShrEdgeRadius, costr);
        } else if (a == etolerance) {
            listener->panelChanged (EvShrEdgeTolerance, costr);
        } else if (a == hcamount) {
            listener->panelChanged (EvShrHaloAmount, costr);
        } else if (a == dradius) {
            listener->panelChanged (EvShrDRadius, costr);
        } else if (a == damount) {
            listener->panelChanged (EvShrDAmount, costr);
        } else if (a == deconvCornerBoost) {
            listener->panelChanged(EvDeconvCornerBoost, a->getTextValue());
        } else if (a == deconvCornerLatitude) {
            listener->panelChanged(EvDeconvCornerLatitude, a->getTextValue());
        } else if (a == psf_iterations) {
            listener->panelChanged(EvPSFIterations, a->getTextValue());
        }
    }
}

void Sharpening::adjusterAutoToggled(Adjuster* a, bool newval)
{
    if (listener && a == dradius) {
        auto e = (!newval) ? EvAutoRadiusOff : EvAutoRadiusOn;
        listener->panelChanged(e, newval ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}

void Sharpening::adjusterChanged(ThresholdAdjuster* a, double newBottom, double newTop)
{
}

void Sharpening::adjusterChanged(ThresholdAdjuster* a, double newBottomLeft, double newTopLeft, double newBottomRight, double newTopRight)
{
}

void Sharpening::adjusterChanged(ThresholdAdjuster* a, int newBottom, int newTop)
{
}

void Sharpening::adjusterChanged(ThresholdAdjuster* a, int newBottomLeft, int newTopLeft, int newBottomRight, int newTopRight)
{
    if (listener && getEnabled()) {
        if (a == threshold) {
            listener->panelChanged(EvShrThresh, threshold->getHistoryString());
        }
    }
}

void Sharpening::adjusterChanged2(ThresholdAdjuster* a, int newBottomL, int newTopL, int newBottomR, int newTopR)
{
}

void Sharpening::enabledChanged ()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged (EvShrEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged (EvShrEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged (EvShrEnabled, M("GENERAL_DISABLED"));
        }
    }
    if (options.toolpanels_disable) {
        rld->set_sensitive(getEnabled());
        usm->set_sensitive(getEnabled());
        edgebox->set_sensitive(getEnabled());
        hcbox->set_sensitive(getEnabled());
    }
}

void Sharpening::edgesonly_toggled ()
{
    removeIfThere (edgebin, edgebox, false);

    if (edgesonly->get_active ()) {
        edgebin->pack_start (*edgebox);
    }

    if (listener && getEnabled() ) {
        if (edgesonly->get_inconsistent()) {
            listener->panelChanged (EvShrEdgeOnly, M("GENERAL_INITIALVALUES"));
        } else if (edgesonly->get_active ()) {
            listener->panelChanged (EvShrEdgeOnly, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged (EvShrEdgeOnly, M("GENERAL_DISABLED"));
        }
    }
}

void Sharpening::halocontrol_toggled ()
{
    removeIfThere (hcbin, hcbox, false);

    if (halocontrol->get_active ()) {
        hcbin->pack_start (*hcbox);
    }

    if (listener && getEnabled() ) {
        if (halocontrol->get_inconsistent()) {
            listener->panelChanged (EvShrHaloControl, M("GENERAL_INITIALVALUES"));
        } else if (halocontrol->get_active ()) {
            listener->panelChanged (EvShrHaloControl, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged (EvShrHaloControl, M("GENERAL_DISABLED"));
        }
    }
}

void Sharpening::method_changed ()
{
    removeIfThere(this, usm, false);
    removeIfThere(this, rld, false);
    removeIfThere(this, psf, false);

    if (method->get_active_row_number() == 0) {
        pack_start(*usm);
    } else if (method->get_active_row_number() == 1) {
        pack_start(*rld);
    } else if (method->get_active_row_number() == 2) {
        pack_start(*psf);
    }

    if (listener && getEnabled()) {
        listener->panelChanged(EvShrMethod, method->get_active_text ());
    }

}

void Sharpening::trimValues (rtengine::procparams::ProcParams* pp)
{
    contrast->trimValue(pp->sharpening.contrast);
    // blur->trimValue(pp->sharpening.blurradius);
    radius->trimValue(pp->sharpening.radius);
    dradius->trimValue(pp->sharpening.deconvradius);
    amount->trimValue(pp->sharpening.amount);
    damount->trimValue(pp->sharpening.deconvamount);
    eradius->trimValue(pp->sharpening.edges_radius);
    etolerance->trimValue(pp->sharpening.edges_tolerance);
    hcamount->trimValue(pp->sharpening.halocontrol_amount);
    deconvCornerBoost->trimValue(pp->sharpening.deconvCornerBoost);
    deconvCornerLatitude->trimValue(pp->sharpening.deconvCornerLatitude);
    psf_iterations->trimValue(pp->sharpening.psf_iterations);
}


void Sharpening::autoDeconvRadiusChanged(float radius)
{
    idle_register.add(
        [this, radius]() -> bool
        {
            disableListener();
            if (radius < 0) {
                dradius->delAutoButton();
            } else {
                dradius->addAutoButton(M("TP_SHARPENING_RLD_AUTORADIUS_TOOLTIP"));
                dradius->setValue(radius);
            }
            enableListener();
            return false;
        }
    );
}


void Sharpening::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.sharpening = initial_params;
    }
    pp.sharpening.enabled = getEnabled();
    read(&pp);
}
