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
#include "labcurve.h"
#include <iomanip>
#include "../rtengine/improcfun.h"
#include "edit.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;


LabCurve::LabCurve():
    FoldableToolPanel(this, "labcurves", M("TP_LABCURVE_LABEL"), false, true, true)
{
    EvToolReset.set_action(rtengine::LUMINANCECURVE);
    
    std::vector<GradientMilestone> milestones;

    brightness = Gtk::manage (new Adjuster (M("TP_LABCURVE_BRIGHTNESS"), -100., 100., 1., 0.));
    contrast = Gtk::manage (new Adjuster (M("TP_LABCURVE_CONTRAST"), -100., 100., 1., 0.));
    chromaticity = Gtk::manage (new Adjuster (M("TP_LABCURVE_CHROMATICITY"), -100., 100., 1., 0.));
    chromaticity->set_tooltip_markup(M("TP_LABCURVE_CHROMA_TOOLTIP"));

    pack_start(*brightness);
    pack_start(*contrast);
    pack_start(*chromaticity);

    brightness->setAdjusterListener (this);
    contrast->setAdjusterListener (this);
    chromaticity->setAdjusterListener (this);

    brightness->setLogScale(2, 0, true);
    contrast->setLogScale(2, 0, true);
    chromaticity->setLogScale(2, 0, true);

    curveEditorG = new CurveEditorGroup (options.lastLabCurvesDir);
    curveEditorG->setCurveListener(this);

    lshape = static_cast<DiagonalCurveEditor*>(curveEditorG->addCurve(CT_Diagonal, "L*"));
    lshape->setTooltip(M("TP_LABCURVE_CURVEEDITOR_LL_TOOLTIP"));
    lshape->setEditID(EUID_Lab_LCurve, BT_SINGLEPLANE_FLOAT);

    ashape = static_cast<DiagonalCurveEditor*>(curveEditorG->addCurve(CT_Diagonal, "a*"));
    ashape->setEditID(EUID_Lab_aCurve, BT_SINGLEPLANE_FLOAT);

    ashape->setRangeLabels(
        M("TP_LABCURVE_CURVEEDITOR_A_RANGE1"), M("TP_LABCURVE_CURVEEDITOR_A_RANGE2"),
        M("TP_LABCURVE_CURVEEDITOR_A_RANGE3"), M("TP_LABCURVE_CURVEEDITOR_A_RANGE4")
    );
    //from green to magenta
    milestones.clear();
    milestones.push_back( GradientMilestone(0., 0., 1., 0.) );
    milestones.push_back( GradientMilestone(1., 1., 0., 1.) );
    ashape->setBottomBarBgGradient(milestones);
    ashape->setLeftBarBgGradient(milestones);
    milestones.clear();

    bshape = static_cast<DiagonalCurveEditor*>(curveEditorG->addCurve(CT_Diagonal, "b*"));
    bshape->setRangeLabels(
        M("TP_LABCURVE_CURVEEDITOR_B_RANGE1"), M("TP_LABCURVE_CURVEEDITOR_B_RANGE2"),
        M("TP_LABCURVE_CURVEEDITOR_B_RANGE3"), M("TP_LABCURVE_CURVEEDITOR_B_RANGE4")
    );
    bshape->setEditID(EUID_Lab_bCurve, BT_SINGLEPLANE_FLOAT);

    //from blue to yellow
    milestones.clear();
    milestones.push_back( GradientMilestone(0., 0., 0., 1.) );
    milestones.push_back( GradientMilestone(1., 1., 1., 0.) );
    bshape->setBottomBarBgGradient(milestones);
    bshape->setLeftBarBgGradient(milestones);
    milestones.clear();
    curveEditorG->curveListComplete();

    pack_start (*curveEditorG, Gtk::PACK_SHRINK, 4);
}


LabCurve::~LabCurve ()
{
    delete curveEditorG;
}


void LabCurve::read(const ProcParams* pp)
{
    disableListener ();

    brightness->setValue(pp->labCurve.brightness);
    contrast->setValue(pp->labCurve.contrast);
    chromaticity->setValue(pp->labCurve.chromaticity);
    lshape->setCurve(pp->labCurve.lcurve);
    ashape->setCurve(pp->labCurve.acurve);
    bshape->setCurve(pp->labCurve.bcurve);
    setEnabled(pp->labCurve.enabled);

    enableListener();
}

void LabCurve::autoOpenCurve()
{
    // Open up the first curve if selected
    bool active = lshape->openIfNonlinear();

    if (!active) {
        ashape->openIfNonlinear();
    }

    if (!active) {
        bshape->openIfNonlinear();
    }
}


void LabCurve::setEditProvider(EditDataProvider *provider)
{
    lshape->setEditProvider(provider);
    ashape->setEditProvider(provider);
    bshape->setEditProvider(provider);
}


void LabCurve::write(ProcParams* pp)
{
    pp->labCurve.enabled = getEnabled();
    pp->labCurve.brightness = brightness->getValue ();
    pp->labCurve.contrast = (int)contrast->getValue ();
    pp->labCurve.chromaticity = (int)chromaticity->getValue ();
    pp->labCurve.lcurve = lshape->getCurve ();
    pp->labCurve.acurve = ashape->getCurve ();
    pp->labCurve.bcurve = bshape->getCurve ();
}


void LabCurve::setDefaults(const ProcParams* defParams)
{
    brightness->setDefault(defParams->labCurve.brightness);
    contrast->setDefault(defParams->labCurve.contrast);
    chromaticity->setDefault(defParams->labCurve.chromaticity);

    initial_params = defParams->labCurve;
}


void LabCurve::curveChanged(CurveEditor* ce)
{
    if (listener && getEnabled()) {
        if (ce == lshape) {
            listener->panelChanged(EvLLCurve, M("HISTORY_CUSTOMCURVE"));
        }

        if (ce == ashape) {
            listener->panelChanged(EvLaCurve, M("HISTORY_CUSTOMCURVE"));
        }

        if (ce == bshape) {
            listener->panelChanged(EvLbCurve, M("HISTORY_CUSTOMCURVE"));
        }
    }
}


void LabCurve::adjusterChanged(Adjuster* a, double newval)
{
    Glib::ustring costr;

    if (a == brightness) {
        costr = Glib::ustring::format (std::setw(3), std::fixed, std::setprecision(2), a->getValue());
    } else {
        costr = Glib::ustring::format ((int)a->getValue());
    }

    if (a == brightness) {
        if (listener && getEnabled()) {
            listener->panelChanged (EvLBrightness, costr);
        }
    } else if (a == contrast) {
        if (listener && getEnabled()) {
            listener->panelChanged (EvLContrast, costr);
        }
    } else if (a == chromaticity) {
        if (listener && getEnabled()) {
            listener->panelChanged (EvLSaturation, costr);
        }
    }
}


void LabCurve::adjusterAutoToggled(Adjuster* a, bool newval)
{
}


void LabCurve::updateCurveBackgroundHistogram(
    const LUTu& histToneCurve,
    const LUTu& histLCurve,
    const LUTu& histCCurve,
    const LUTu& histLCAM,
    const LUTu& histCCAM,
    const LUTu& histRed,
    const LUTu& histGreen,
    const LUTu& histBlue,
    const LUTu& histLuma,
    const LUTu& histLRETI
)
{
    lshape->updateBackgroundHistogram(histLCurve);
}


void LabCurve::trimValues(rtengine::procparams::ProcParams* pp)
{
    brightness->trimValue(pp->labCurve.brightness);
    contrast->trimValue(pp->labCurve.contrast);
    chromaticity->trimValue(pp->labCurve.chromaticity);
}


void LabCurve::enabledChanged()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged (EvLEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged (EvLEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged (EvLEnabled, M("GENERAL_DISABLED"));
        }
    }
}


void LabCurve::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.labCurve = initial_params;
    }
    pp.labCurve.enabled = getEnabled();
    read(&pp);
}


void LabCurve::registerShortcuts(ToolShortcutManager *mgr)
{
    mgr->addShortcut(GDK_KEY_i, this, brightness);
    mgr->addShortcut(GDK_KEY_j, this, contrast);
    mgr->addShortcut(GDK_KEY_n, this, chromaticity);
}
