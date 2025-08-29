/*
 *  This file is part of RawTherapee.
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
 *
 *  2010 Ilya Popov <ilia_popov@rambler.ru>
 */

#include "hslequalizer.h"
#include "eventmapper.h"
#include "../rtengine/color.h"
#include "../rtengine/iccmatrices.h"

using namespace rtengine;
using namespace rtengine::procparams;

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

namespace {

void hsv2rgb01(float h, float s, float v, float &r, float &g, float &b)
{
    // try to get a better visual match -- this is empirical and to be confirmed
    h -= 1.f/19.f;
    if (h > 1.f) {
        h -= 1.f;
    } else if (h < 0.f) {
        h += 1.f;
    }

    Color::hsv2rgb01(h, s, v, r, g, b);
}

} // namespace

HSLEqualizer::HSLEqualizer():
    FoldableToolPanel(this, "hslequalizer", M("TP_HSVEQUALIZER_LABEL"), false, true, true)
{
    auto m = ProcEventMapper::getInstance();
    EvHSLSmoothing = m->newEvent(rtengine::RGBCURVE, "HISTORY_MSG_HSL_SMOOTHING");
    EvToolReset.set_action(rtengine::RGBCURVE);
    
    std::vector<GradientMilestone> bottomMilestones;
    for (int i = 0; i < 7; i++) {
        float R, G, B;
        float x = float(i) * (1.0f / 6.0);
        hsv2rgb01(x, 0.5f, 0.6f, R, G, B);
        bottomMilestones.push_back(GradientMilestone(double(x), double(R), double(G), double(B)));
    }
    
    curveEditorG = new CurveEditorGroup(options.lastHsvCurvesDir, M("TP_HSVEQUALIZER_CHANNEL"));
    curveEditorG->setCurveListener(this);

    hshape = static_cast<FlatCurveEditor *>(curveEditorG->addCurve(CT_Flat, M("TP_HSVEQUALIZER_HUE")));
    hshape->setEditID(EUID_HSL_H, BT_SINGLEPLANE_FLOAT);
    hshape->setBottomBarBgGradient(bottomMilestones);
    hshape->setCurveColorProvider(this, 1);

    sshape = static_cast<FlatCurveEditor *>(curveEditorG->addCurve(CT_Flat, M("TP_HSVEQUALIZER_SAT")));
    sshape->setEditID(EUID_HSL_S, BT_SINGLEPLANE_FLOAT);
    sshape->setBottomBarBgGradient(bottomMilestones);
    sshape->setCurveColorProvider(this, 2);

    lshape = static_cast<FlatCurveEditor *>(curveEditorG->addCurve(CT_Flat, M("TP_HSVEQUALIZER_VAL")));
    lshape->setEditID(EUID_HSL_V, BT_SINGLEPLANE_FLOAT);
    lshape->setBottomBarBgGradient(bottomMilestones);
    lshape->setCurveColorProvider(this, 3);

    setMulti(true);

    curveEditorG->curveListComplete();

    smoothing = Gtk::manage(new Adjuster(M("TP_HSL_SMOOTHING"), 0, 10, 1, 0));
    smoothing->setAdjusterListener(this);

    pack_start(*curveEditorG, Gtk::PACK_SHRINK, 4);
    pack_start(*smoothing);

    default_flat_curve_ = { FCT_MinMaxCPoints };
    // Point for RGBCMY colors
    for (int i = 0; i < 6; i++) {
        default_flat_curve_.push_back((1. / 6.) * i);
        default_flat_curve_.push_back(0.5);
        default_flat_curve_.push_back(0.35);
        default_flat_curve_.push_back(0.35);
    }
    
}


HSLEqualizer::~HSLEqualizer()
{
    delete curveEditorG;
}


void HSLEqualizer::read(const ProcParams *pp)
{
    disableListener();

    hshape->setCurve(default_flat_curve_);
    sshape->setCurve(default_flat_curve_);
    lshape->setCurve(default_flat_curve_);
    
    hshape->setCurve(pp->hsl.hCurve);
    sshape->setCurve(pp->hsl.sCurve);
    lshape->setCurve(pp->hsl.lCurve);
    smoothing->setValue(pp->hsl.smoothing);
    setEnabled(pp->hsl.enabled);

    enableListener();
}


void HSLEqualizer::setEditProvider(EditDataProvider *provider)
{
    hshape->setEditProvider(provider);
    sshape->setEditProvider(provider);
    lshape->setEditProvider(provider);
}


void HSLEqualizer::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        listener->panelChanged(EvHSLSmoothing, a->getTextValue());
    }
}


void HSLEqualizer::adjusterAutoToggled(Adjuster* a, bool newval)
{
}


void HSLEqualizer::autoOpenCurve()
{
    // Open up the first curve if selected
    bool active = hshape->openIfNonlinear();

    if (!active) {
        sshape->openIfNonlinear();
    }

    if (!active) {
        lshape->openIfNonlinear();
    }
}


void HSLEqualizer::write(ProcParams *pp)
{
    pp->hsl.enabled = getEnabled();
    pp->hsl.hCurve = hshape->getCurve();
    pp->hsl.sCurve = sshape->getCurve();
    pp->hsl.lCurve = lshape->getCurve();
    pp->hsl.smoothing = smoothing->getValue();
}

/*
 * Curve listener
 *
 * If more than one curve has been added, the curve listener is automatically
 * set to 'multi=true', and send a pointer of the modified curve in a parameter
 */
void HSLEqualizer::curveChanged(CurveEditor *ce)
{
    if (listener && getEnabled()) {
        if (ce == hshape) {
            listener->panelChanged(EvHSVEqualizerH, M("HISTORY_CUSTOMCURVE"));
        }

        if (ce == sshape) {
            listener->panelChanged(EvHSVEqualizerS, M("HISTORY_CUSTOMCURVE"));
        }

        if (ce == lshape) {
            listener->panelChanged(EvHSVEqualizerV, M("HISTORY_CUSTOMCURVE"));
        }
    }
}


void HSLEqualizer::colorForValue(double valX, double valY, enum ColorCaller::ElemType elemType, int callerId, ColorCaller *caller)
{
    float R = 0.5, G = 0.5, B = 0.5;

    if (callerId == 1) {
        float h = (valY - 0.5) * 0.3 + valX;
        if (h > 1.0f) {
            h -= 1.0f;
        } else if (h < 0.0f) {
            h += 1.0f;
        }
        hsv2rgb01(h, 0.5f, 0.6f, R, G, B);
    } else if (callerId == 2) { // Saturation = f(Hue)
        hsv2rgb01(valX, valY, 0.6f, R, G, B);
    } else if (callerId == 3) { // Value = f(Hue)
        hsv2rgb01(valX, 0.5f, 0.6f + (valY - 0.5f) * 0.2, R, G, B);
    }    
    caller->ccRed = R;
    caller->ccGreen = G;
    caller->ccBlue = B;
}


void HSLEqualizer::enabledChanged()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged(EvHSVEqEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged(EvHSVEqEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvHSVEqEnabled, M("GENERAL_DISABLED"));
        }
    }
}


void HSLEqualizer::setDefaults(const ProcParams *def)
{
    smoothing->setDefault(def->hsl.smoothing);
    initial_params = def->hsl;
}


void HSLEqualizer::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.hsl = initial_params;
    }
    pp.hsl.enabled = getEnabled();
    read(&pp);
}
