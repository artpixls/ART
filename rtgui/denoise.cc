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
#include "denoise.h"
#include <iomanip>
#include <cmath>
#include "edit.h"
#include "guiutils.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;
extern Options options;

Denoise::Denoise():
    FoldableToolPanel(this, "dirpyrdenoise", M("TP_DIRPYRDENOISE_LABEL"), true, true, true)
{
    auto m = ProcEventMapper::getInstance();
    EvGuidedChromaRadius = m->newEvent(rtengine::HDR, "HISTORY_MSG_DENOISE_GUIDED_CHROMA_RADIUS");
    EvChrominanceAutoFactor = m->newEvent(rtengine::HDR, "HISTORY_MSG_DENOISE_CHROMINANCE_AUTO_FACTOR");
    EvLuminanceDetailThreshold = m->newEvent(rtengine::HDR, "HISTORY_MSG_DENOISE_LUMINANCE_DETAIL_THRESHOLD");
    EvColorSpace = m->newEvent(rtengine::HDR, "HISTORY_MSG_203");
    EvNlDetail = m->newEvent(rtengine::HDR, "HISTORY_MSG_DENOISE_NL_DETAIL");
    EvNlStrength = m->newEvent(rtengine::HDR, "HISTORY_MSG_DENOISE_NL_STRENGTH");
    EvToolReset.set_action(rtengine::HDR);

    Gtk::Frame *lumaFrame = Gtk::manage(new Gtk::Frame(M("TP_DIRPYRDENOISE_LUMINANCE_FRAME")));
    lumaFrame->set_label_align(0.025, 0.5);

    Gtk::VBox *lumaVBox = Gtk::manage(new Gtk::VBox());
    lumaVBox->set_spacing(2);

    Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
    hb->pack_start(*Gtk::manage(new Gtk::Label(M("TP_DIRPYRDENOISE_MAIN_MODE") + ": ")), Gtk::PACK_SHRINK, 1);
    hb->set_tooltip_markup(M("TP_DIRPYRDENOISE_MAIN_MODE_TOOLTIP"));

    aggressive = Gtk::manage(new MyComboBoxText ());
    aggressive->append(M("TP_DIRPYRDENOISE_MAIN_MODE_CONSERVATIVE"));
    aggressive->append(M("TP_DIRPYRDENOISE_MAIN_MODE_AGGRESSIVE"));
    aggressive->set_active(0);
    hb->pack_start(*aggressive, Gtk::PACK_EXPAND_WIDGET, 1);
    pack_start(*hb, Gtk::PACK_SHRINK, 1);

    hb = Gtk::manage(new Gtk::HBox());
    hb->pack_start(*Gtk::manage(new Gtk::Label(M("TP_DIRPYRDENOISE_MAIN_COLORSPACE") + ": ")), Gtk::PACK_SHRINK, 1);
    colorSpace = Gtk::manage(new MyComboBoxText ());
    colorSpace->append(M("TP_DIRPYRDENOISE_MAIN_COLORSPACE_RGB"));
    colorSpace->append(M("TP_DIRPYRDENOISE_MAIN_COLORSPACE_LAB"));
    colorSpace->set_active(0);
    hb->pack_start(*colorSpace, Gtk::PACK_EXPAND_WIDGET, 1);
    pack_start(*hb, Gtk::PACK_SHRINK, 1);
    
    gamma = Gtk::manage(new Adjuster(M("TP_DIRPYRDENOISE_MAIN_GAMMA"), 1.0, 3.0, 0.01, 1.7));
    gamma->set_tooltip_text(M("TP_DIRPYRDENOISE_MAIN_GAMMA_TOOLTIP"));
    gamma->setAdjusterListener(this);
    pack_start(*gamma, Gtk::PACK_EXPAND_WIDGET, 1);

    luminance = Gtk::manage(new Adjuster(M("TP_DIRPYRDENOISE_LUMINANCE_SMOOTHING"), 0, 100, 0.01, 0));
    luminanceDetail = Gtk::manage(new Adjuster(M("TP_DIRPYRDENOISE_LUMINANCE_DETAIL"), 0, 100, 0.01, 50));
    luminanceDetailThreshold = Gtk::manage(new Adjuster(M("TP_DIRPYRDENOISE_LUMINANCE_DETAIL_THRESHOLD"), 0, 100, 1, 0));

    lumaVBox->pack_start(*luminance);
    lumaVBox->pack_start(*luminanceDetail);
    lumaVBox->pack_start(*luminanceDetailThreshold);
    lumaFrame->add(*lumaVBox);
    pack_start(*lumaFrame);

    Gtk::Frame *chromaFrame = Gtk::manage(new Gtk::Frame(M("TP_DIRPYRDENOISE_CHROMINANCE_FRAME")));
    chromaFrame->set_label_align(0.025, 0.5);

    Gtk::VBox *chromaVBox = Gtk::manage(new Gtk::VBox());
    chromaVBox->set_spacing(2);

    hb = Gtk::manage(new Gtk::HBox());
    hb->pack_start(*Gtk::manage (new Gtk::Label (M("TP_DIRPYRDENOISE_CHROMINANCE_METHOD") + ":")), Gtk::PACK_SHRINK, 1);

    chrominanceMethod = Gtk::manage (new MyComboBoxText ());
    chrominanceMethod->append(M("TP_DIRPYRDENOISE_CHROMINANCE_MANUAL"));
    chrominanceMethod->append(M("TP_DIRPYRDENOISE_CHROMINANCE_AUTOGLOBAL"));
    chrominanceMethod->set_active(0);
//    chrominanceMethod->set_tooltip_markup (M("TP_DIRPYRDENOISE_CHROMINANCE_METHOD_TOOLTIP"));
    hb->pack_start(*chrominanceMethod);
    chromaVBox->pack_start(*hb);

    chrominanceAutoFactor = Gtk::manage(new Adjuster(M("TP_DIRPYRDENOISE_CHROMINANCE_AUTO_FACTOR"), 0, 1, 0.01, 1));
    chrominance = Gtk::manage(new Adjuster(M("TP_DIRPYRDENOISE_CHROMINANCE_MASTER"), 0, 100, 0.01, 15));
    chrominanceRedGreen = Gtk::manage(new Adjuster(M("TP_DIRPYRDENOISE_CHROMINANCE_REDGREEN"), -100, 100, 0.1, 0));
    chrominanceBlueYellow = Gtk::manage(new Adjuster(M("TP_DIRPYRDENOISE_CHROMINANCE_BLUEYELLOW"), -100, 100, 0.1, 0));

    chrominance->setLogScale(10, 0);
    chrominanceRedGreen->setLogScale(100, 0, true);
    chrominanceBlueYellow->setLogScale(100, 0, true);

    chromaVBox->pack_start(*chrominanceAutoFactor);
    chromaVBox->pack_start(*chrominance);
    chromaVBox->pack_start(*chrominanceRedGreen);
    chromaVBox->pack_start(*chrominanceBlueYellow);

    luminance->setAdjusterListener(this);
    luminanceDetail->setAdjusterListener(this);
    luminanceDetailThreshold->setAdjusterListener(this);
    chrominanceAutoFactor->setAdjusterListener(this);
    chrominance->setAdjusterListener(this);
    chrominanceRedGreen->setAdjusterListener(this);
    chrominanceBlueYellow->setAdjusterListener(this);

    chromaFrame->add(*chromaVBox);
    pack_start(*chromaFrame);

    luminance->hide();
    luminanceDetail->show();

    chrominance->show();
    chrominanceRedGreen->show();
    chrominanceBlueYellow->show();

    smoothingEnabled = Gtk::manage(new MyExpander(true, M("TP_DENOISE_SMOOTHING")));
    ToolParamBlock *smoothing = Gtk::manage(new ToolParamBlock());

    Gtk::VBox *smoothingBox = Gtk::manage(new Gtk::VBox());

    nlDetail = Gtk::manage(new Adjuster(M("TP_DIRPYRDENOISE_LUMINANCE_DETAIL"), 1, 100, 1, 50));
    nlStrength = Gtk::manage(new Adjuster(M("TP_DIRPYRDENOISE_LUMINANCE_FRAME"), 0, 100, 1, 0));
    smoothingBox->pack_start(*nlDetail);
    smoothingBox->pack_start(*nlStrength);
    nlDetail->setAdjusterListener(this);
    nlStrength->setAdjusterListener(this);

    guidedChromaRadius = Gtk::manage(new Adjuster(M("TP_DIRPYRDENOISE_CHROMINANCE_FRAME"), 0, 20, 1, 0));
    guidedChromaRadius->setAdjusterListener(this);
    smoothingBox->pack_start(*guidedChromaRadius);

    smoothing->pack_start(*smoothingBox);
    smoothingEnabled->add(*smoothing, false);
    smoothingEnabled->setLevel(2);
    pack_start(*smoothingEnabled);

    aggressive->signal_changed().connect(sigc::mem_fun(*this, &Denoise::aggressiveChanged));
    colorSpace->signal_changed().connect(sigc::mem_fun(*this, &Denoise::colorSpaceChanged));
    chrominanceMethod->signal_changed().connect(sigc::mem_fun(*this, &Denoise::chrominanceMethodChanged));
    smoothingEnabled->signal_enabled_toggled().connect(sigc::mem_fun(*this, &Denoise::smoothingEnabledToggled));
}


Denoise::~Denoise ()
{
    idle_register.destroy();
}


void Denoise::chromaChanged(double autchroma, double autred, double autblue)
{
    struct Data {
        Denoise *dn;
        double chroma;
        double red;
        double blue;
    };
    Data *d = new Data{this, autchroma, autred, autblue};
        
    idle_register.add([d]() -> bool
                      {
                          d->dn->chromaComputed(d->chroma, d->red, d->blue);
                          delete d;
                          return false;
                      });
}


bool Denoise::chromaComputed(double chroma, double red, double blue)
{
    disableListener();
    chrominance->setValue(chroma);
    chrominanceRedGreen->setValue(red);
    chrominanceBlueYellow->setValue(blue);
    enableListener();
    return false;
}


void Denoise::read(const ProcParams *pp)
{
    disableListener();

    setEnabled(pp->denoise.enabled);
    
    aggressive->set_active(pp->denoise.aggressive ? 1 : 0);
    colorSpace->set_active(pp->denoise.colorSpace == rtengine::procparams::DenoiseParams::ColorSpace::LAB ? 1 : 0);
    gamma->setValue(pp->denoise.gamma);
    luminance->setValue(pp->denoise.luminance);
    luminanceDetail->setValue(pp->denoise.luminanceDetail);
    luminanceDetailThreshold->setValue(pp->denoise.luminanceDetailThreshold);

    chrominanceMethod->set_active(int(pp->denoise.chrominanceMethod));
    chrominanceMethodChanged();
    chrominanceAutoFactor->setValue(pp->denoise.chrominanceAutoFactor);
    chrominance->setValue(pp->denoise.chrominance);
    chrominanceRedGreen->setValue(pp->denoise.chrominanceRedGreen);
    chrominanceBlueYellow->setValue(pp->denoise.chrominanceBlueYellow);

    smoothingEnabled->setEnabled(pp->denoise.smoothingEnabled);
    
    guidedChromaRadius->setValue(pp->denoise.guidedChromaRadius);
    nlDetail->setValue(pp->denoise.nlDetail);
    nlStrength->setValue(pp->denoise.nlStrength);

    enableListener ();
}


void Denoise::write(ProcParams *pp)
{
    pp->denoise.enabled = getEnabled();
    if (aggressive->get_active_row_number() < 2) {
        pp->denoise.aggressive = aggressive->get_active_row_number();
    }
    pp->denoise.colorSpace = colorSpace->get_active_row_number() == 1 ? rtengine::procparams::DenoiseParams::ColorSpace::LAB : rtengine::procparams::DenoiseParams::ColorSpace::RGB;
    pp->denoise.gamma = gamma->getValue();
    pp->denoise.luminance = luminance->getValue();
    pp->denoise.luminanceDetail = luminanceDetail->getValue();
    pp->denoise.luminanceDetailThreshold = luminanceDetailThreshold->getValue();
    if (chrominanceMethod->get_active_row_number() < 2) {
        pp->denoise.chrominanceMethod = static_cast<DenoiseParams::ChrominanceMethod>(chrominanceMethod->get_active_row_number());
    }
    pp->denoise.chrominance = chrominance->getValue();
    pp->denoise.chrominanceAutoFactor = chrominanceAutoFactor->getValue();
    pp->denoise.chrominanceRedGreen = chrominanceRedGreen->getValue();
    pp->denoise.chrominanceBlueYellow = chrominanceBlueYellow->getValue();

    pp->denoise.smoothingEnabled = smoothingEnabled->getEnabled();
    pp->denoise.guidedChromaRadius = guidedChromaRadius->getValue();
    pp->denoise.nlDetail = nlDetail->getValue();
    pp->denoise.nlStrength = nlStrength->getValue();
}


void Denoise::chrominanceMethodChanged()
{
    bool is_auto = (chrominanceMethod->get_active_row_number() == 1);
    chrominance->set_visible(!is_auto);
    chrominanceRedGreen->set_visible(!is_auto);
    chrominanceBlueYellow->set_visible(!is_auto);
    chrominanceAutoFactor->set_visible(is_auto);

    if (listener && getEnabled() ) {
        listener->panelChanged(EvDPDNCmet, chrominanceMethod->get_active_text());
    }
}


void Denoise::aggressiveChanged()
{
    if (listener && getEnabled() ) {
        listener->panelChanged(EvDPDNsmet, aggressive->get_active_text());
    }
}


void Denoise::colorSpaceChanged()
{
    if (listener && getEnabled() ) {
        listener->panelChanged(EvColorSpace, colorSpace->get_active_text());
    }
}


void Denoise::setDefaults(const ProcParams *defParams)
{
    luminance->setDefault(defParams->denoise.luminance);
    luminanceDetail->setDefault(defParams->denoise.luminanceDetail);
    luminanceDetailThreshold->setDefault(defParams->denoise.luminanceDetailThreshold);
    chrominance->setDefault(defParams->denoise.chrominance);
    chrominanceAutoFactor->setDefault(defParams->denoise.chrominanceAutoFactor);
    chrominanceRedGreen->setDefault(defParams->denoise.chrominanceRedGreen);
    chrominanceBlueYellow->setDefault(defParams->denoise.chrominanceBlueYellow);
    gamma->setDefault(defParams->denoise.gamma);

    guidedChromaRadius->setDefault(defParams->denoise.guidedChromaRadius);
    nlDetail->setDefault(defParams->denoise.nlDetail);
    nlStrength->setDefault(defParams->denoise.nlStrength);

    initial_params = defParams->denoise;
}


void Denoise::adjusterChanged(Adjuster* a, double newval)
{
    const Glib::ustring costr = Glib::ustring::format (std::setw(3), std::fixed, std::setprecision(2), a->getValue());

    if (listener && getEnabled()) {
        if (a == luminanceDetail) {
            listener->panelChanged(EvDPDNLdetail, costr);
        } else if (a == luminance) {
            listener->panelChanged(EvDPDNLuma, costr);
        } else if (a == luminanceDetailThreshold) {
            listener->panelChanged(EvLuminanceDetailThreshold, costr);
        } else if (a == chrominance) {
            listener->panelChanged(EvDPDNChroma, costr);
        } else if (a == chrominanceRedGreen) {
            listener->panelChanged(EvDPDNredchro, costr);
        } else if (a == chrominanceBlueYellow) {
            listener->panelChanged(EvDPDNbluechro, costr);
        } else if (a == gamma) {
            listener->panelChanged(EvDPDNGamma, costr);
        } else if (a == guidedChromaRadius && smoothingEnabled->getEnabled()) {
            listener->panelChanged(EvGuidedChromaRadius, costr);
        } else if (a == chrominanceAutoFactor) {
            listener->panelChanged(EvChrominanceAutoFactor, costr);
        } else if (a == nlDetail) {
            listener->panelChanged(EvNlDetail, costr);
        } else if (a == nlStrength) {
            listener->panelChanged(EvNlStrength, costr);
        }
    }
}

void Denoise::adjusterAutoToggled(Adjuster* a, bool newval)
{
}


void Denoise::enabledChanged()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged(EvDPDNEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged(EvDPDNEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvDPDNEnabled, M("GENERAL_DISABLED"));
        }
    }
}


void Denoise::smoothingEnabledToggled()
{
    if (listener) {
        if (smoothingEnabled->getEnabled()) {
            listener->panelChanged(EvDPDNmedian, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvDPDNmedian, M("GENERAL_DISABLED"));
        }
    }
}


void Denoise::trimValues (rtengine::procparams::ProcParams* pp)
{
    luminance->trimValue(pp->denoise.luminance);
    luminanceDetail->trimValue(pp->denoise.luminanceDetail);
    luminanceDetailThreshold->trimValue(pp->denoise.luminanceDetailThreshold);
    chrominance->trimValue(pp->denoise.chrominance);
    chrominanceAutoFactor->trimValue(pp->denoise.chrominanceAutoFactor);
    chrominanceRedGreen->trimValue(pp->denoise.chrominanceRedGreen);
    chrominanceBlueYellow->trimValue(pp->denoise.chrominanceBlueYellow);
    gamma->trimValue(pp->denoise.gamma);
    guidedChromaRadius->trimValue(pp->denoise.guidedChromaRadius);
    nlDetail->trimValue(pp->denoise.nlDetail);
    nlStrength->trimValue(pp->denoise.nlStrength);
}


void Denoise::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.denoise = initial_params;
    }
    pp.denoise.enabled = getEnabled();
    read(&pp);
}
