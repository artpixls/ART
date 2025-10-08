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
#include "tonecurve.h"
#include "adjuster.h"
#include <sigc++/slot.h>
#include <iomanip>
#include "ppversion.h"
#include "edit.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;

namespace {

//constexpr int ID_HUE_MASK = 1;

const std::vector<double> default_satcurve{
    FCT_MinMaxCPoints,
    0, 0.5,
    0.35, 0.35,
    1, 0.5,
    0.35, 0.35
};

int mode2idx(ToneCurveParams::TcMode mode)
{
    switch (mode) {
    case ToneCurveParams::TcMode::STD: return 1;
    case ToneCurveParams::TcMode::WEIGHTEDSTD: return 2;
    case ToneCurveParams::TcMode::FILMLIKE: return 3;
    case ToneCurveParams::TcMode::SATANDVALBLENDING: return 4;
    case ToneCurveParams::TcMode::LUMINANCE: return 5;
    case ToneCurveParams::TcMode::PERCEPTUAL: return 6;
    case ToneCurveParams::TcMode::NEUTRAL: return 0;
    }
    return 0;
}


ToneCurveParams::TcMode idx2mode(int idx)
{
    return idx == 0 ? ToneCurveParams::TcMode::NEUTRAL : ToneCurveParams::TcMode(idx-1);
}

} // namespace


ToneCurve::ToneCurve():
    FoldableToolPanel(this, "tonecurve", M("TP_TONECURVE_LABEL"), false, true, true)
{
    auto m = ProcEventMapper::getInstance();
    EvHistMatching = m->newEvent(rtengine::AUTOEXP, "HISTORY_MSG_HISTMATCHING");
    EvHistMatchingBatch = m->newEvent(rtengine::M_VOID, "HISTORY_MSG_HISTMATCHING");
    EvSatCurve = m->newEvent(rtengine::LUMINANCECURVE, "HISTORY_MSG_TONECURVE_SATCURVE");
    EvPerceptualStrength = m->newEvent(rtengine::LUMINANCECURVE, "HISTORY_MSG_TONECURVE_PERCEPTUAL_STRENGTH");
    EvContrastLegacy = m->newEvent(rtengine::LUMINANCECURVE, "HISTORY_MSG_TONECURVE_CONTRAST_LEGACY");
    EvWhitePoint = m->newEvent(rtengine::LUMINANCECURVE, "HISTORY_MSG_TONECURVE_WHITEPOINT");
    EvMode = m->newEvent(rtengine::LUMINANCECURVE, "HISTORY_MSG_TONECURVE_MODE");
    EvBaseCurve = m->newEvent(rtengine::LUMINANCECURVE, "HISTORY_MSG_TONECURVE_BASECURVE");
    EvToolEnabled.set_action(rtengine::AUTOEXP);
    EvToolReset.set_action(rtengine::AUTOEXP);

    basecurve_ = Gtk::manage(new MyComboBoxText());
    basecurve_->append(M("TP_TONECURVE_BASECURVE_LINEAR"));
    basecurve_->append(M("TP_TONECURVE_BASECURVE_ROLLOFF"));
    basecurve_->append(M("TP_TONECURVE_BASECURVE_SCURVE"));
    basecurve_->signal_changed().connect(sigc::mem_fun(*this, &ToneCurve::baseCurveChanged));
    Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
    hb->pack_start(*Gtk::manage(new Gtk::Label(M("TP_TONECURVE_BASECURVE") + ": ")), Gtk::PACK_SHRINK);
    hb->pack_start(*basecurve_, Gtk::PACK_EXPAND_WIDGET);
    hb->show_all();
    pack_start(*hb);

    contrast = Gtk::manage (new Adjuster(M("TP_BRIGHTCONTRSAT_CONTRAST"), -100, 100, 1, 0));
    pack_start(*contrast);
    contrast->setLogScale(4, 0, true);
    contrast->setAdjusterListener(this);

    CurveListener::setMulti(true);

    std::vector<GradientMilestone> bottomMilestones;
    bottomMilestones.push_back( GradientMilestone(0., 0., 0., 0.) );
    bottomMilestones.push_back( GradientMilestone(1., 1., 1., 1.) );

//----------- Curve 1 ------------------------------
    pack_start (*Gtk::manage (new  Gtk::HSeparator()));

    histmatching = Gtk::manage(new Gtk::ToggleButton(M("TP_EXPOSURE_HISTMATCHING")));
    histmatching->set_tooltip_markup(M("TP_EXPOSURE_HISTMATCHING_TOOLTIP"));
    histmatchconn = histmatching->signal_toggled().connect(sigc::mem_fun(*this, &ToneCurve::histmatchingToggled));
    pack_start(*histmatching, true, true, 2);

    toneCurveMode = Gtk::manage (new MyComboBoxText ());
    toneCurveMode->append (M("TP_EXPOSURE_TCMODE_NEUTRAL"));
    toneCurveMode->append (M("TP_EXPOSURE_TCMODE_STANDARD"));
    toneCurveMode->append (M("TP_EXPOSURE_TCMODE_WEIGHTEDSTD"));
    toneCurveMode->append (M("TP_EXPOSURE_TCMODE_FILMLIKE"));
    toneCurveMode->append (M("TP_EXPOSURE_TCMODE_SATANDVALBLENDING"));
    toneCurveMode->append (M("TP_EXPOSURE_TCMODE_LUMINANCE"));
    toneCurveMode->append (M("TP_EXPOSURE_TCMODE_PERCEPTUAL"));
    toneCurveMode->set_active (0);
    toneCurveMode->set_tooltip_text(M("TP_EXPOSURE_TCMODE_LABEL1"));

    curveEditorG = new CurveEditorGroup (options.lastToneCurvesDir, M("TP_EXPOSURE_CURVEEDITOR1"));
    curveEditorG->setCurveListener (this);

    const auto mk_mode_box =
        [&](MyComboBoxText *b) -> Gtk::HBox *
        {
            Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
            Gtk::Image *w = Gtk::manage(new RTImage("warning-small.svg"));
            w->set_tooltip_markup(M("GENERAL_DEPRECATED_TOOLTIP"));
            hb->pack_start(*b, Gtk::PACK_EXPAND_WIDGET);
            hb->pack_start(*w, Gtk::PACK_SHRINK, 2);
            setExpandAlignProperties(hb, true, false, Gtk::ALIGN_START, Gtk::ALIGN_BASELINE);
            return hb;
        };
    mode1_box_ = mk_mode_box(toneCurveMode);

    shape = static_cast<DiagonalCurveEditor*>(curveEditorG->addCurve(CT_Diagonal, "", mode1_box_));
    shape->setEditID(EUID_ToneCurve1, BT_IMAGEFLOAT);
    shape->setBottomBarBgGradient(bottomMilestones);
    shape->setLeftBarBgGradient(bottomMilestones);

    // This will add the reset button at the end of the curveType buttons
    curveEditorG->curveListComplete();

    pack_start( *curveEditorG, Gtk::PACK_SHRINK, 2);

    tcmodeconn = toneCurveMode->signal_changed().connect( sigc::mem_fun(*this, &ToneCurve::curveMode1Changed), true );

//----------- Curve 2 ------------------------------

    toneCurveMode2 = Gtk::manage (new MyComboBoxText ());
    toneCurveMode2->append (M("TP_EXPOSURE_TCMODE_NEUTRAL"));
    toneCurveMode2->append (M("TP_EXPOSURE_TCMODE_STANDARD"));
    toneCurveMode2->append (M("TP_EXPOSURE_TCMODE_WEIGHTEDSTD"));
    toneCurveMode2->append (M("TP_EXPOSURE_TCMODE_FILMLIKE"));
    toneCurveMode2->append (M("TP_EXPOSURE_TCMODE_SATANDVALBLENDING"));
    toneCurveMode2->append (M("TP_EXPOSURE_TCMODE_LUMINANCE"));
    toneCurveMode2->append (M("TP_EXPOSURE_TCMODE_PERCEPTUAL"));
    toneCurveMode2->set_active (0);
    toneCurveMode2->set_tooltip_text(M("TP_EXPOSURE_TCMODE_LABEL2"));

    curveEditorG2 = new CurveEditorGroup (options.lastToneCurvesDir, M("TP_EXPOSURE_CURVEEDITOR2"));
    curveEditorG2->setCurveListener (this);

    mode2_box_ = mk_mode_box(toneCurveMode2);
    shape2 = static_cast<DiagonalCurveEditor*>(curveEditorG2->addCurve(CT_Diagonal, "", mode2_box_));
    shape2->setEditID(EUID_ToneCurve2, BT_IMAGEFLOAT);
    shape2->setBottomBarBgGradient(bottomMilestones);
    shape2->setLeftBarBgGradient(bottomMilestones);

    // This will add the reset button at the end of the curveType buttons
    curveEditorG2->curveListComplete();
    curveEditorG2->setTooltip(M("TP_EXPOSURE_CURVEEDITOR2_TOOLTIP"));

    pack_start( *curveEditorG2, Gtk::PACK_SHRINK, 2);

    tcmode2conn = toneCurveMode2->signal_changed().connect( sigc::mem_fun(*this, &ToneCurve::curveMode2Changed), true );

    perceptualStrength = new Adjuster(M("TP_TONECURVE_PERCEPTUAL_STRENGTH"), 0, 100, 1, 100);
    pack_start(*perceptualStrength);
    perceptualStrength->setAdjusterListener(this);

    satcurveG = Gtk::manage(new CurveEditorGroup(options.lastColorToningCurvesDir, M("TP_TONECURVE_SATCURVE"), 0.7));
    satcurveG->setCurveListener(this);

    satcurve = static_cast<FlatCurveEditor *>(satcurveG->addCurve(CT_Flat, "", nullptr, false, false));
    satcurve->setResetCurve(FlatCurveType(default_satcurve[0]), default_satcurve);
    satcurve->setEditID(EUID_ToneCurveSaturation, BT_SINGLEPLANE_FLOAT);
    satcurve->setBottomBarBgGradient(bottomMilestones);
    
    satcurve2_ = static_cast<DiagonalCurveEditor *>(satcurveG->addCurve(CT_Diagonal, ""));
    satcurve2_->setBottomBarColorProvider(this, 1);
    satcurve2_->setLeftBarColorProvider(this, 2);
    satcurve2_->setEditID(EUID_ToneCurveSaturation2, BT_SINGLEPLANE_FLOAT);

    satcurveG->curveListComplete();
    satcurveG->show();

    // satcurve->getCurveTypeButton()->signal_changed().connect(sigc::mem_fun(*this, &ToneCurve::updateSatCurves));

    pack_start(*satcurveG, Gtk::PACK_SHRINK, 2);

    contrast_legacy_ = Gtk::manage(new Gtk::CheckButton(M("TP_TONECURVE_CONTRAST_LEGACY")));
    contrast_legacy_box_ = new Gtk::HBox();
    contrast_legacy_box_->pack_start(*contrast_legacy_, Gtk::PACK_EXPAND_WIDGET, 0);
    {
        Gtk::Image *w = Gtk::manage(new RTImage("warning-small.svg"));
        w->set_tooltip_markup(M("GENERAL_DEPRECATED_TOOLTIP"));
        contrast_legacy_box_->pack_start(*w, Gtk::PACK_SHRINK, 2);
    }
    contrast_legacy_box_->show_all();

    whitePoint = Gtk::manage(new Adjuster(M("TP_TONECURVE_WHITEPOINT"), 1, 100.0, 0.1, 1.0));
    whitePoint->setAdjusterListener(this);
    whitePoint->setLogScale(40, 1);
    pack_start(*whitePoint);

    mode_ = Gtk::manage(new MyComboBoxText());
    mode_->append(M("TP_EXPOSURE_TCMODE_NEUTRAL"));
    mode_->append(M("TP_EXPOSURE_TCMODE_STANDARD"));
    mode_->append(M("TP_EXPOSURE_TCMODE_WEIGHTEDSTD"));
    mode_->append(M("TP_EXPOSURE_TCMODE_FILMLIKE"));
    mode_->append(M("TP_EXPOSURE_TCMODE_SATANDVALBLENDING"));
    mode_->append(M("TP_EXPOSURE_TCMODE_LUMINANCE"));
    mode_->append(M("TP_EXPOSURE_TCMODE_PERCEPTUAL"));
    mode_->set_active(0);
    mode_->signal_changed().connect(sigc::mem_fun(*this, &ToneCurve::modeChanged), true);
    
    mode_box_ = new Gtk::HBox();
    mode_box_->pack_start(*Gtk::manage(new Gtk::Label(M("TP_TONECURVE_MODE") + ": ")), Gtk::PACK_SHRINK);
    mode_box_->pack_start(*mode_, Gtk::PACK_EXPAND_WIDGET);
    mode_box_->show_all();

    contrast_legacy_->signal_toggled().connect(sigc::mem_fun(*this, &ToneCurve::contrastLegacyToggled));
}


ToneCurve::~ToneCurve ()
{
    idle_register.destroy();

    delete curveEditorG;
    delete curveEditorG2;
    delete perceptualStrength;
    delete contrast_legacy_box_;
    delete mode_box_;
}


void ToneCurve::read(const ProcParams* pp)
{
    disableListener ();

    tcmodeconn.block(true);
    tcmode2conn.block(true);

    setEnabled(pp->toneCurve.enabled);

    contrast->setValue(pp->toneCurve.contrast);

    shape->setCurve (pp->toneCurve.curve);
    shape2->setCurve (pp->toneCurve.curve2);

    toneCurveMode->set_active(mode2idx(pp->toneCurve.curveMode));
    toneCurveMode2->set_active(mode2idx(pp->toneCurve.curveMode2));

    histmatching->set_active(pp->toneCurve.histmatching);
    fromHistMatching = pp->toneCurve.fromHistMatching;

    satcurve->setCurve(default_satcurve);
    satcurve->setCurve(pp->toneCurve.saturation);
    satcurve2_->setCurve(pp->toneCurve.saturation2);

    tcmode2conn.block(false);
    tcmodeconn.block(false);

    perceptualStrength->setValue(pp->toneCurve.perceptualStrength);

    showPerceptualStrength();

    mode_->set_active(mode2idx(pp->toneCurve.curveMode));
    contrast_legacy_->set_active(pp->toneCurve.contrastLegacyMode);

    removeIfThere(this, mode_box_, false);
    removeIfThere(this, contrast_legacy_box_, false);
    removeIfThere(this, perceptualStrength, false);
    
    if (pp->toneCurve.contrast != 0 && pp->toneCurve.contrastLegacyMode) {
        pack_start(*contrast_legacy_box_, Gtk::PACK_SHRINK, 2);
        reorder_child(*contrast_legacy_box_, 1);
    }

    if (pp->toneCurve.perceptualStrength != 100) {
        pack_start(*perceptualStrength, Gtk::PACK_SHRINK, 2);
        reorder_child(*perceptualStrength, get_children().size()-2);
    }

    bool new_mode = (pp->toneCurve.curve.empty() || pp->toneCurve.curve[0] == DCT_Linear || pp->toneCurve.curve2.empty() || pp->toneCurve.curve2[0] == DCT_Linear || pp->toneCurve.curveMode == pp->toneCurve.curveMode2);
    mode1_box_->set_visible(!new_mode);
    mode2_box_->set_visible(!new_mode);
    setExpandAlignProperties(shape->getCurveTypeButton()->buttonGroup, new_mode, true, Gtk::ALIGN_FILL, Gtk::ALIGN_FILL);
    setExpandAlignProperties(shape2->getCurveTypeButton()->buttonGroup, new_mode, true, Gtk::ALIGN_FILL, Gtk::ALIGN_FILL);

    if (new_mode) {
        pack_start(*mode_box_, Gtk::PACK_SHRINK, 2);
        reorder_child(*mode_box_, 0);
    }

    whitePoint->setValue(pp->toneCurve.whitePoint);
    showWhitePoint();

    //updateSatCurves(0);
    basecurve_->set_active(int(pp->toneCurve.basecurve));
    
    enableListener();
}


void ToneCurve::autoOpenCurve()
{
    shape->openIfNonlinear();
    shape2->openIfNonlinear();
    satcurve->openIfNonlinear();
    satcurve2_->openIfNonlinear();
}


void ToneCurve::setEditProvider(EditDataProvider *provider)
{
    shape->setEditProvider(provider);
    shape2->setEditProvider(provider);
    satcurve->setEditProvider(provider);
    satcurve2_->setEditProvider(provider);
}


void ToneCurve::write(ProcParams* pp)
{
    pp->toneCurve.enabled = getEnabled();
    pp->toneCurve.contrast = contrast->getValue();
    pp->toneCurve.curve = shape->getCurve ();
    pp->toneCurve.curve2 = shape2->getCurve ();

    bool legacy_curve_mode = toneCurveMode->is_visible();
    int tcMode = mode_->get_active_row_number();

    if (legacy_curve_mode) {
        tcMode = toneCurveMode->get_active_row_number();
    }

    pp->toneCurve.curveMode = idx2mode(tcMode);

    if (legacy_curve_mode) {
        tcMode = toneCurveMode2->get_active_row_number();
    }

    pp->toneCurve.curveMode2 = idx2mode(tcMode);

    pp->toneCurve.histmatching = histmatching->get_active();
    pp->toneCurve.fromHistMatching = fromHistMatching;

    pp->toneCurve.saturation = satcurve->getCurve();
    pp->toneCurve.saturation2 = satcurve2_->getCurve();

    pp->toneCurve.perceptualStrength = perceptualStrength->getValue();
    pp->toneCurve.contrastLegacyMode = contrast_legacy_->get_active();

    pp->toneCurve.whitePoint = whitePoint->getValue();

    pp->toneCurve.basecurve = ToneCurveParams::BcMode(basecurve_->get_active_row_number());
}


void ToneCurve::setRaw(bool raw)
{
    disableListener();
    histmatching->set_sensitive(raw);
    enableListener();
}


void ToneCurve::setDefaults(const ProcParams* defParams)
{
    contrast->setDefault(defParams->toneCurve.contrast);
    perceptualStrength->setDefault(defParams->toneCurve.perceptualStrength);
    whitePoint->setDefault(defParams->toneCurve.whitePoint);

    initial_params = defParams->toneCurve;
}


void ToneCurve::curveChanged(CurveEditor *ce)
{
    showWhitePoint();
    if (listener && getEnabled()) {
        if (ce == shape) {
            setHistmatching(false);
            listener->panelChanged(EvToneCurve1, M("HISTORY_CUSTOMCURVE"));
        } else if (ce == shape2) {
            setHistmatching(false);
            listener->panelChanged(EvToneCurve2, M("HISTORY_CUSTOMCURVE"));
        } else if (ce == satcurve || ce == satcurve2_) {
            listener->panelChanged(EvSatCurve, M("HISTORY_CUSTOMCURVE"));
        }
    }
}

void ToneCurve::curveMode1Changed ()
{
    showPerceptualStrength();
    showWhitePoint();
    //if (listener)  listener->panelChanged (EvToneCurveMode, toneCurveMode->get_active_text());
    if (listener && getEnabled()) {
        //setHistmatching(false);
        Glib::signal_idle().connect (sigc::mem_fun(*this, &ToneCurve::curveMode1Changed_));
    }
}

bool ToneCurve::curveMode1Changed_ ()
{
    if (listener && getEnabled()) {
        listener->panelChanged(EvToneCurveMode1, toneCurveMode->get_active_text());
    }

    return false;
}

void ToneCurve::curveMode2Changed ()
{
    showPerceptualStrength();
    showWhitePoint();
    //if (listener)  listener->panelChanged (EvToneCurveMode, toneCurveMode->get_active_text());
    if (listener && getEnabled()) {
        //setHistmatching(false);
        Glib::signal_idle().connect (sigc::mem_fun(*this, &ToneCurve::curveMode2Changed_));
    }
}

bool ToneCurve::curveMode2Changed_ ()
{
    if (listener && getEnabled()) {
        listener->panelChanged(EvToneCurveMode2, toneCurveMode2->get_active_text());
    }

    return false;
}

float ToneCurve::blendPipetteValues(CurveEditor *ce, float chan1, float chan2, float chan3)
{
    // assuming that all the channels are used...
    if (ce == shape) {
        if (toneCurveMode->get_active_row_number() == 4) {
            return chan1 * 0.2126729f + chan2 * 0.7151521f + chan3 * 0.0721750f;
        }
    } else if (ce == shape2) {
        if (toneCurveMode2->get_active_row_number() == 4) {
            return chan1 * 0.2126729f + chan2 * 0.7151521f + chan3 * 0.0721750f;
        }
    }

    return CurveListener::blendPipetteValues(ce, chan1, chan2, chan3);
}


void ToneCurve::enableAll(bool yes)
{
    curveEditorG->set_sensitive(yes);
    toneCurveMode->set_sensitive(yes);
    curveEditorG2->set_sensitive(yes);
    toneCurveMode2->set_sensitive(yes);
    histmatching->set_sensitive(yes);
    satcurveG->set_sensitive(yes);
    contrast->set_sensitive(yes);
    perceptualStrength->set_sensitive(yes);
    whitePoint->set_sensitive(yes);
    basecurve_->set_sensitive(yes);
}


void ToneCurve::trimValues (rtengine::procparams::ProcParams* pp)
{
}


void ToneCurve::updateCurveBackgroundHistogram(
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
    shape->updateBackgroundHistogram(histToneCurve);
    shape2->updateBackgroundHistogram(histToneCurve);
}


void ToneCurve::setHistmatching(bool enabled)
{
    fromHistMatching = enabled;
    if (histmatching->get_active()) {
        histmatchconn.block(true);
        histmatching->set_active(enabled);
        histmatchconn.block(false);
        histmatching->set_inconsistent(false);
    }
}


void ToneCurve::histmatchingToggled()
{
    if (listener) {
        if (histmatching->get_active()) {
            fromHistMatching = false;
            disableListener();
            setEnabled(true);
            enableListener();
            listener->panelChanged(EvHistMatching, M("GENERAL_ENABLED"));
            enableAll(false);
        } else if (getEnabled()) {
            listener->panelChanged(EvHistMatching, M("GENERAL_DISABLED"));
        }
    }
}


void ToneCurve::autoMatchedToneCurveChanged(const std::vector<double> &curve, const std::vector<double> &curve2)
{
    nextToneCurve = curve;
    nextToneCurve2 = curve2;

    idle_register.add(
        [this]() -> bool
        {
            GThreadLock lock;
            disableListener();
            enableAll();

            // toneCurveMode->set_active(rtengine::toUnderlying(nextToneCurveMode));
            shape->setCurve(nextToneCurve);
            shape2->setCurve(nextToneCurve2);
            shape->openIfNonlinear();
            shape2->openIfNonlinear();

            enableListener();
            fromHistMatching = true;

            return false;
        });
}


void ToneCurve::adjusterChanged(Adjuster *a, double newval)
{
    if (!listener || !getEnabled()) {
        return;
    }

    Glib::ustring costr = Glib::ustring::format((int)a->getValue());

    if (a == contrast) {
        listener->panelChanged(EvContrast, costr);
    } else if (a == perceptualStrength) {
        listener->panelChanged(EvPerceptualStrength, costr);
    } else if (a == whitePoint) {
        listener->panelChanged(EvWhitePoint, a->getTextValue());
    }
}


void ToneCurve::showPerceptualStrength()
{
    perceptualStrength->set_visible(
        toneCurveMode->get_active_row_number() == 5 ||
        toneCurveMode2->get_active_row_number() == 5);
}


void ToneCurve::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.toneCurve = initial_params;
    }
    pp.toneCurve.enabled = getEnabled();
    read(&pp);
}


void ToneCurve::contrastLegacyToggled()
{
    showWhitePoint();
    if (listener && getEnabled()) {
        listener->panelChanged(EvContrastLegacy, contrast_legacy_->get_active() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}


void ToneCurve::modeChanged()
{
    showPerceptualStrength();
    showWhitePoint();
    if (listener && getEnabled()) {
        listener->panelChanged(EvMode, mode_->get_active_text());
    }
}


void ToneCurve::showWhitePoint()
{
    ProcParams pp;
    write(&pp);
    whitePoint->set_visible(pp.toneCurve.hasWhitePoint());
}


void ToneCurve::registerShortcuts(ToolShortcutManager *mgr)
{
    mgr->addShortcut(GDK_KEY_c, this, contrast);
}


void ToneCurve::colorForValue(double valX, double valY, enum ColorCaller::ElemType elemType, int callerId, ColorCaller *caller)
{
    float R = 0.5, G = 0.5, B = 0.5;

    if (callerId == 1) {
        rtengine::Color::hsv2rgb01(float(valY), float(valX), 0.8f, R, G, B);
    } else if (callerId == 2) {
        rtengine::Color::hsv2rgb01(float(valY), 1.f-float(valX), 0.8f, R, G, B);
    } 
    caller->ccRed = R;
    caller->ccGreen = G;
    caller->ccBlue = B;
}


void ToneCurve::baseCurveChanged()
{
    if (listener && getEnabled()) {
        listener->panelChanged(EvBaseCurve, basecurve_->get_active_text());
    }
}
