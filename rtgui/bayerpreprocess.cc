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
#include "bayerpreprocess.h"
#include "guiutils.h"
#include "eventmapper.h"
#include <sstream>

using namespace rtengine;
using namespace rtengine::procparams;

BayerPreProcess::BayerPreProcess() : FoldableToolPanel(this, "bayerpreprocess", M("TP_PREPROCESS_LABEL"), false, true, true)
{
    EvToolEnabled.set_action(rtengine::DARKFRAME);
    EvToolReset.set_action(rtengine::DARKFRAME);
    
    auto m = ProcEventMapper::getInstance();
    EvLineDenoiseDirection = m->newEvent(rtengine::DARKFRAME, "HISTORY_MSG_PREPROCESS_LINEDENOISE_DIRECTION");
    EvPDAFLinesFilter = m->newEvent(rtengine::DARKFRAME, "HISTORY_MSG_PREPROCESS_PDAFLINESFILTER");
    EvDynamicRowNoiseFilter = m->newEvent(rtengine::DARKFRAME, "HISTORY_MSG_PREPROCESS_DYNROWNOISEFILTER");

    lineDenoise = Gtk::manage(new Adjuster(M("TP_PREPROCESS_LINEDENOISE"), 0, 1000, 1, 0));
    lineDenoise->setAdjusterListener(this);

    if (lineDenoise->delay < options.adjusterMaxDelay) {
        lineDenoise->delay = options.adjusterMaxDelay;
    }

    lineDenoise->show();

    greenEqThreshold = Gtk::manage(new Adjuster(M("TP_PREPROCESS_GREENEQUIL"), 0, 100, 1, 0));
    greenEqThreshold->setAdjusterListener(this);

    if (greenEqThreshold->delay < options.adjusterMaxDelay) {
        greenEqThreshold->delay = options.adjusterMaxDelay;
    }

    greenEqThreshold->show();

    Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
    hb->pack_start(*Gtk::manage(new Gtk::Label(M("TP_PREPROCESS_LINEDENOISE_DIRECTION") + ": ")), Gtk::PACK_SHRINK, 0);
    lineDenoiseDirection = Gtk::manage(new MyComboBoxText());
    lineDenoiseDirection->append(M("TP_PREPROCESS_LINEDENOISE_DIRECTION_HORIZONTAL"));
    lineDenoiseDirection->append(M("TP_PREPROCESS_LINEDENOISE_DIRECTION_VERTICAL"));
    lineDenoiseDirection->append(M("TP_PREPROCESS_LINEDENOISE_DIRECTION_BOTH"));
    lineDenoiseDirection->append(M("TP_PREPROCESS_LINEDENOISE_DIRECTION_PDAF_LINES"));
    lineDenoiseDirection->show();
    lineDenoiseDirection->signal_changed().connect(sigc::mem_fun(*this, &BayerPreProcess::lineDenoiseDirectionChanged));

    hb->pack_start(*lineDenoiseDirection);

    pack_start(*lineDenoise, Gtk::PACK_SHRINK, 4);
    pack_start(*hb, Gtk::PACK_SHRINK, 4);

    pack_start(*Gtk::manage(new Gtk::HSeparator()));

    pack_start(*greenEqThreshold, Gtk::PACK_SHRINK, 4);

    pdafLinesFilter = Gtk::manage(new Gtk::CheckButton((M("TP_PREPROCESS_PDAFLINESFILTER"))));
    pdafLinesFilter->show();
    pdafLinesFilter->signal_toggled().connect(sigc::mem_fun(*this, &BayerPreProcess::pdafLinesFilterChanged), true);

    pack_start(*Gtk::manage(new Gtk::HSeparator()));
    pack_start(*pdafLinesFilter, Gtk::PACK_SHRINK, 4);

    dynamicRowNoiseFilter = Gtk::manage(new Gtk::CheckButton((M("TP_PREPROCESS_DYNROWNOISEFILTER"))));
    dynamicRowNoiseFilter->show();
    dynamicRowNoiseFilter->signal_toggled().connect(sigc::mem_fun(*this, &BayerPreProcess::dynamicRowNoiseFilterChanged), true);
    pack_start(*dynamicRowNoiseFilter, Gtk::PACK_SHRINK, 4);
}

void BayerPreProcess::read(const rtengine::procparams::ProcParams* pp)
{
    disableListener();

    setEnabled(pp->raw.bayersensor.enable_preproc);

    lineDenoise->setValue(pp->raw.bayersensor.linenoise);
    int d = int(pp->raw.bayersensor.linenoiseDirection) - 1;

    if (d == 4) {
        --d;
    }

    lineDenoiseDirection->set_active(d);
    greenEqThreshold->setValue(pp->raw.bayersensor.greenthresh);
    pdafLinesFilter->set_active(pp->raw.bayersensor.pdafLinesFilter);
    dynamicRowNoiseFilter->set_active(pp->raw.bayersensor.dynamicRowNoiseFilter);

    enableListener();
}

void BayerPreProcess::write(rtengine::procparams::ProcParams* pp)
{
    pp->raw.bayersensor.enable_preproc = getEnabled();
    pp->raw.bayersensor.linenoise = lineDenoise->getIntValue();
    int d = lineDenoiseDirection->get_active_row_number() + 1;

    if (d == 4) {
        ++d;
    }

    pp->raw.bayersensor.linenoiseDirection = RAWParams::BayerSensor::LineNoiseDirection(d);
    pp->raw.bayersensor.greenthresh = greenEqThreshold->getIntValue();
    pp->raw.bayersensor.pdafLinesFilter = pdafLinesFilter->get_active();
    pp->raw.bayersensor.dynamicRowNoiseFilter = dynamicRowNoiseFilter->get_active();
}

void BayerPreProcess::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {

        Glib::ustring value = a->getTextValue();

        if (a == greenEqThreshold) {
            listener->panelChanged(EvPreProcessGEquilThresh,  value);
        } else if (a == lineDenoise) {
            listener->panelChanged(EvPreProcessLineDenoise,  value);
        }
    }
}

void BayerPreProcess::adjusterAutoToggled(Adjuster* a, bool newval)
{
}

void BayerPreProcess::setDefaults(const rtengine::procparams::ProcParams* defParams)
{
    lineDenoise->setDefault(defParams->raw.bayersensor.linenoise);
    greenEqThreshold->setDefault(defParams->raw.bayersensor.greenthresh);
    initial_params = defParams->raw.bayersensor;
}


void BayerPreProcess::trimValues(rtengine::procparams::ProcParams* pp)
{

    lineDenoise->trimValue(pp->raw.bayersensor.linenoise);
    greenEqThreshold->trimValue(pp->raw.bayersensor.greenthresh);
}


void BayerPreProcess::lineDenoiseDirectionChanged()
{
    if (listener && getEnabled()) {
        listener->panelChanged(EvLineDenoiseDirection, lineDenoiseDirection->get_active_text());
    }
}

void BayerPreProcess::pdafLinesFilterChanged()
{
    if (listener && getEnabled()) {
        listener->panelChanged(EvPDAFLinesFilter, pdafLinesFilter->get_active() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}


void BayerPreProcess::dynamicRowNoiseFilterChanged()
{
    if (listener && getEnabled()) {
        listener->panelChanged(EvDynamicRowNoiseFilter, dynamicRowNoiseFilter->get_active() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}


void BayerPreProcess::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.raw.bayersensor = initial_params;
    }
    pp.raw.bayersensor.enable_preproc = getEnabled();
    read(&pp);
}
