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
#include "dehaze.h"
#include "eventmapper.h"
#include <iomanip>
#include <cmath>

using namespace rtengine;
using namespace rtengine::procparams;


Dehaze::Dehaze(): FoldableToolPanel(this, "dehaze", M("TP_DEHAZE_LABEL"), false, true, true)
{
    auto m = ProcEventMapper::getInstance();
    EvDehazeEnabled = m->newEvent(rtengine::HDR, "HISTORY_MSG_DEHAZE_ENABLED");
    EvDehazeStrength = m->newEvent(rtengine::HDR, "HISTORY_MSG_DEHAZE_STRENGTH");
    EvDehazeShowDepthMap = m->newEvent(rtengine::HDR, "HISTORY_MSG_DEHAZE_SHOW_DEPTH_MAP");
    EvDehazeDepth = m->newEvent(rtengine::HDR, "HISTORY_MSG_DEHAZE_DEPTH");
    EvDehazeLuminance = m->newEvent(rtengine::HDR, "HISTORY_MSG_DEHAZE_LUMINANCE");
    EvDehazeBlackpoint = m->newEvent(rtengine::HDR, "HISTORY_MSG_DEHAZE_BLACKPOINT");
    EvToolReset.set_action(rtengine::HDR);
    
    std::vector<GradientMilestone> bottomMilestones;
    bottomMilestones.push_back(GradientMilestone(0., 0., 0., 0.));
    bottomMilestones.push_back(GradientMilestone(1., 1., 1., 1.));

    CurveEditorGroup *strength_group = Gtk::manage(new CurveEditorGroup(options.lastToneCurvesDir, M("TP_DEHAZE_STRENGTH"), 0.7));
    strength_group->setCurveListener(this);
    strength = static_cast<FlatCurveEditor *>(strength_group->addCurve(CT_Flat, "", nullptr, false, false));
    ProcParams pp;
    strength->setResetCurve(FlatCurveType(pp.dehaze.strength[0]), pp.dehaze.strength);
    strength->setEditID(EUID_DehazeStrength, BT_SINGLEPLANE_FLOAT);
    strength->setBottomBarBgGradient(bottomMilestones);
    strength_group->curveListComplete();
    strength_group->show();

    // strength = Gtk::manage(new Adjuster(M("TP_DEHAZE_STRENGTH"), -100., 100., 1., 50.));
    // strength->setAdjusterListener(this);
    // strength->show();

    depth = Gtk::manage(new Adjuster(M("TP_DEHAZE_DEPTH"), 0., 100., 1., 25., nullptr, nullptr, nullptr, nullptr, true));
    depth->setAdjusterListener(this);
    depth->show();

    Gtk::HBox *hb = Gtk::manage (new Gtk::HBox ());
    hb->pack_start(*Gtk::manage(new Gtk::Label(M("TP_DEHAZE_MODE") + ": ")), Gtk::PACK_SHRINK);
    luminance = Gtk::manage(new MyComboBoxText());
    luminance->append(M("TP_DEHAZE_RGB"));
    luminance->append(M("TP_DEHAZE_LUMINANCE"));
    hb->pack_start(*luminance);
    pack_start(*hb);
    luminance->signal_changed().connect(sigc::mem_fun(*this, &Dehaze::luminanceChanged));
    hb->show();
    luminance->show();

    blackpoint = Gtk::manage(new Adjuster(M("TP_DEHAZE_BLACKPOINT"), 0, 100, 1, 0));
    blackpoint->setAdjusterListener(this);
    blackpoint->show();
    
    showDepthMap = Gtk::manage(new Gtk::CheckButton(M("TP_DEHAZE_SHOW_DEPTH_MAP")));
    showDepthMap->signal_toggled().connect(sigc::mem_fun(*this, &Dehaze::showDepthMapChanged));
    showDepthMap->show();

    Gtk::VBox *vb = Gtk::manage(new Gtk::VBox());
    vb->pack_start(*strength_group);
    pack_start(*vb);
    pack_start(*depth);
    pack_start(*blackpoint);
    pack_start(*showDepthMap);
}


void Dehaze::read(const ProcParams *pp)
{
    disableListener();

    setEnabled(pp->dehaze.enabled);
    strength->setCurve(pp->dehaze.strength);
    depth->setValue(pp->dehaze.depth);
    showDepthMap->set_active(pp->dehaze.showDepthMap);
    luminance->set_active(pp->dehaze.luminance ? 1 : 0);
    blackpoint->setValue(pp->dehaze.blackpoint);

    ProcParams dp;
    depth->set_visible(pp->dehaze.depth != dp.dehaze.depth);
    showDepthMap->set_visible(pp->dehaze.showDepthMap != dp.dehaze.showDepthMap);

    enableListener();
}


void Dehaze::write(ProcParams *pp)
{
    pp->dehaze.strength = strength->getCurve();
    pp->dehaze.depth = depth->getValue();
    pp->dehaze.enabled = getEnabled();
    pp->dehaze.showDepthMap = showDepthMap->get_active();
    pp->dehaze.luminance = luminance->get_active_row_number() == 1;
    pp->dehaze.blackpoint = blackpoint->getValue();
}

void Dehaze::setDefaults(const ProcParams *defParams)
{
    depth->setDefault(defParams->dehaze.depth);
    blackpoint->setDefault(defParams->dehaze.blackpoint);

    initial_params = defParams->dehaze;
}


void Dehaze::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        if (a == depth) {
            listener->panelChanged(EvDehazeDepth, a->getTextValue());
        } else if (a == blackpoint) {
            listener->panelChanged(EvDehazeBlackpoint, a->getTextValue());
        }
    }
}


void Dehaze::enabledChanged ()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged(EvDehazeEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged(EvDehazeEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvDehazeEnabled, M("GENERAL_DISABLED"));
        }
    }
}


void Dehaze::showDepthMapChanged()
{
    if (listener) {
        listener->panelChanged(EvDehazeShowDepthMap, showDepthMap->get_active() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}


void Dehaze::luminanceChanged()
{
    if (listener) {
        listener->panelChanged(EvDehazeLuminance, luminance->get_active_row_number() == 1 ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}


void Dehaze::curveChanged()
{
    if (listener && getEnabled()) {
        listener->panelChanged(EvDehazeStrength, M("HISTORY_CUSTOMCURVE"));
    }
}


void Dehaze::setEditProvider(EditDataProvider *p)
{
    strength->setEditProvider(p);
}


void Dehaze::autoOpenCurve()
{
    strength->openIfNonlinear();
}


void Dehaze::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.dehaze = initial_params;
    }
    pp.dehaze.enabled = getEnabled();
    bool depth_visible = depth->is_visible();
    bool dmap_visible = showDepthMap->is_visible();
    read(&pp);
    depth->set_visible(depth->is_visible() || depth_visible);
    showDepthMap->set_visible(showDepthMap->is_visible() || dmap_visible);
}
