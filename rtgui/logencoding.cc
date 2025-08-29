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
#include "logencoding.h"
#include "eventmapper.h"
#include <iomanip>
#include <cmath>

using namespace rtengine;
using namespace rtengine::procparams;

LogEncoding::LogEncoding():
    FoldableToolPanel(this, "log", M("TP_LOGENC_LABEL"), false, true, true)
{
    auto m = ProcEventMapper::getInstance();
    const auto EVENT = rtengine::LUMINANCECURVE;
    EvEnabled = m->newEvent(rtengine::RGBCURVE | rtengine::M_AUTOEXP, "HISTORY_MSG_LOGENC_ENABLED");
    EvAuto = m->newEvent(rtengine::AUTOEXP, "HISTORY_MSG_LOGENC_AUTO");
    EvAutoGainOn = m->newEvent(rtengine::AUTOEXP, "HISTORY_MSG_LOGENC_AUTOGAIN");
    EvAutoGainOff = m->newEvent(rtengine::M_VOID, "HISTORY_MSG_LOGENC_AUTOGAIN");
    EvAutoBatch = m->newEvent(rtengine::M_VOID, "HISTORY_MSG_LOGENC_AUTO");
    EvGain = m->newEvent(EVENT, "HISTORY_MSG_LOGENC_GAIN");
    EvGainAuto = m->newEvent(rtengine::AUTOEXP, "HISTORY_MSG_LOGENC_GAIN");
    EvTargetGray = m->newEvent(EVENT, "HISTORY_MSG_LOGENC_TARGET_GRAY");
    EvBlackEv = m->newEvent(EVENT, "HISTORY_MSG_LOGENC_BLACK_EV");
    EvWhiteEv = m->newEvent(EVENT, "HISTORY_MSG_LOGENC_WHITE_EV");
    EvRegularization = m->newEvent(EVENT, "HISTORY_MSG_LOGENC_REGULARIZATION");
    EvSatControl = m->newEvent(EVENT, "HISTORY_MSG_LOGENC_SATCONTROL");
    EvHLCompression = m->newEvent(EVENT, "HISTORY_MSG_LOGENC_HLCOMPRESSION");
    EvToolReset.set_action(EVENT);

    autocompute = Gtk::manage(new Gtk::ToggleButton(M("TP_LOGENC_AUTO")));
    autoconn = autocompute->signal_toggled().connect(sigc::mem_fun(*this, &LogEncoding::autocomputeToggled));
    
    gain = Gtk::manage(new Adjuster(M("TP_LOGENC_GAIN"), -10.0, 10.0, 0.05, 0.0));
    gain->addAutoButton();
    targetGray = Gtk::manage(new Adjuster(M("TP_LOGENC_TARGET_GRAY"), 5.0, 80.0, 0.1, 18.0));
    blackEv = Gtk::manage(new Adjuster(M("TP_LOGENC_BLACK_EV"), -16.0, 0.0, 0.01, -13.5));
    whiteEv = Gtk::manage(new Adjuster(M("TP_LOGENC_WHITE_EV"), 1.0, 32.0, 0.01, 2.5));
    regularization = Gtk::manage(new Adjuster(M("TP_LOGENC_REGULARIZATION"), 0, 100, 1, 65));
    satcontrol = Gtk::manage(new Gtk::CheckButton(M("TP_TM_FATTAL_SATCONTROL")));
    satcontrol->signal_toggled().connect(sigc::mem_fun(*this, &LogEncoding::satcontrolChanged), true);

    highlightCompression = Gtk::manage(new Adjuster(M("TP_LOGENC_HLCOMPRESSION"), 0, 100, 1, 0));
    
    gain->delay = options.adjusterMaxDelay;
    blackEv->delay = options.adjusterMaxDelay;
    whiteEv->delay = options.adjusterMaxDelay;
    targetGray->delay = options.adjusterMaxDelay;
    highlightCompression->delay = options.adjusterMaxDelay;
    
    whiteEv->setAdjusterListener(this);
    gain->setAdjusterListener(this);
    blackEv->setAdjusterListener(this);
    targetGray->setAdjusterListener(this);
    regularization->setAdjusterListener(this);
    highlightCompression->setAdjusterListener(this);

    whiteEv->setLogScale(16, 0);
    blackEv->setLogScale(2, -8);

    gain->show();
    autocompute->show();
    whiteEv->show();
    blackEv->show();
    targetGray->show();
    satcontrol->show();
    highlightCompression->show();

    //gain->setLogScale(10, 18, true);
    gain->setLogScale(64, 0, true);
    targetGray->setLogScale(10, 18, true);

    pack_start(*targetGray);
    pack_start(*gain);
    pack_start(*whiteEv);
    pack_start(*blackEv);
    pack_start(*highlightCompression);
    pack_start(*regularization);
    pack_start(*satcontrol);
    pack_start(*autocompute);
}


void LogEncoding::read(const ProcParams *pp)
{
    disableListener();
    ConnectionBlocker cbl(autoconn);

    setEnabled(pp->logenc.enabled);

    autocompute->set_active(pp->logenc.autocompute);    
    gain->setValue(pp->logenc.gain);
    gain->setAutoValue(pp->logenc.autogain);
    blackEv->setValue(pp->logenc.blackEv);
    whiteEv->setValue(pp->logenc.whiteEv);
    targetGray->setValue(pp->logenc.targetGray);
    regularization->setValue(pp->logenc.regularization);
    satcontrol->set_active(pp->logenc.satcontrol);
    highlightCompression->setValue(pp->logenc.highlightCompression);

    enableListener();
}

void LogEncoding::write(ProcParams *pp)
{
    pp->logenc.enabled = getEnabled();
    pp->logenc.autocompute = autocompute->get_active();
    pp->logenc.autogain = gain->getAutoValue();
    pp->logenc.gain = gain->getValue();
    pp->logenc.blackEv = blackEv->getValue();
    pp->logenc.whiteEv = whiteEv->getValue();
    pp->logenc.targetGray = targetGray->getValue();
    pp->logenc.regularization = regularization->getValue();
    pp->logenc.satcontrol = satcontrol->get_active();
    pp->logenc.highlightCompression = highlightCompression->getValue();
}

void LogEncoding::setDefaults(const ProcParams *defParams)
{
    gain->setDefault(defParams->logenc.gain);
    blackEv->setDefault(defParams->logenc.blackEv);
    whiteEv->setDefault(defParams->logenc.whiteEv);
    targetGray->setDefault(defParams->logenc.targetGray);
    regularization->setDefault(defParams->logenc.regularization);
    highlightCompression->setDefault(defParams->logenc.highlightCompression);

    initial_params = defParams->logenc;
}

void LogEncoding::adjusterChanged(Adjuster* a, double newval)
{
    ConnectionBlocker cbl(autoconn);
    if (a != gain && a != targetGray) {
        autocompute->set_active(false);
    }
    
    if (listener && getEnabled()) {
        if (a == gain) {
            listener->panelChanged(autocompute->get_active() ? EvGainAuto : EvGain, a->getTextValue());
        } else if (a == blackEv) {
            listener->panelChanged(EvBlackEv, a->getTextValue());
        } else if (a == whiteEv) {
            listener->panelChanged(EvWhiteEv, a->getTextValue());
        } else if (a == targetGray) {
            listener->panelChanged(EvTargetGray, a->getTextValue());
        } else if (a == regularization) {
            listener->panelChanged(EvRegularization, a->getTextValue());
        } else if (a == highlightCompression) {
            listener->panelChanged(EvHLCompression, a->getTextValue());
        }
    }
}

void LogEncoding::adjusterAutoToggled(Adjuster* a, bool newval)
{
    if (listener) {
        if (a == gain) {
            auto e = (!newval) ? EvAutoGainOff : EvAutoGainOn;
            listener->panelChanged(e, newval ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
        }
    }
}

void LogEncoding::enabledChanged ()
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


void LogEncoding::autocomputeToggled()
{
    if (listener) {
        if (autocompute->get_active()) {
            listener->panelChanged(EvAuto, M("GENERAL_ENABLED"));
            blackEv->setEnabled(false);
            whiteEv->setEnabled(false);
            //targetGray->setEnabled(false);
        } else {
            listener->panelChanged(EvAuto, M("GENERAL_DISABLED"));
            // blackEv->setEnabled(true);
            // whiteEv->setEnabled(true);
        }
    }
}


void LogEncoding::logEncodingChanged(const LogEncodingParams &params)
{
    GThreadLock lock;
    
    disableListener();
    ConnectionBlocker cbl(autoconn);

    // blackEv->setEnabled(!params.autocompute);
    // whiteEv->setEnabled(!params.autocompute);
    blackEv->setEnabled(true);
    whiteEv->setEnabled(true);
//    targetGray->setEnabled(true);

    gain->setValue(params.gain);
    blackEv->setValue(params.blackEv);
    whiteEv->setValue(params.whiteEv);
//    targetGray->setValue(params.targetGray);
    
    enableListener();
}


void LogEncoding::satcontrolChanged()
{
    if (listener && getEnabled()) {
        listener->panelChanged(EvSatControl, satcontrol->get_active() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}


void LogEncoding::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.logenc = initial_params;
    }
    pp.logenc.enabled = getEnabled();
    read(&pp);
}


void LogEncoding::registerShortcuts(ToolShortcutManager *mgr)
{
    mgr->addShortcut(GDK_KEY_g, this, gain);
    mgr->addShortcut(GDK_KEY_b, this, targetGray);
    mgr->addShortcut(GDK_KEY_w, this, whiteEv);
    mgr->addShortcut(GDK_KEY_d, this, blackEv);
    mgr->addShortcut(GDK_KEY_k, this, highlightCompression);
}
