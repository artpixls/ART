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
#include "controllines.h"
#include "perspective.h"
#include "rtimage.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;


namespace {

void controlLinesToValues(const std::vector<rtengine::ControlLine>& lines,
                          std::vector<int> &values)
{
    values.clear();

    for (auto &&line : lines) {
        int type = -1;
        switch (line.type) {
            case rtengine::ControlLine::VERTICAL:
                type = 0;
                break;
            case rtengine::ControlLine::HORIZONTAL:
                type = 1;
                break;
        }
        values.push_back(type);

        values.push_back(line.x1);
        values.push_back(line.y1);
        values.push_back(line.x2);
        values.push_back(line.y2);
    }
}

std::vector<rtengine::ControlLine> valuesToControlLines(const std::vector<int> &values)
{
    auto line_count = values.size() / 5;
    std::vector<rtengine::ControlLine> lines(line_count);

    auto values_iter = values.begin();
    for (auto &&line : lines) {
        switch (*(values_iter++)) {
            case 0:
                line.type = rtengine::ControlLine::VERTICAL;
                break;
            case 1:
                line.type = rtengine::ControlLine::HORIZONTAL;
                break;
        }
        
        line.x1 = *(values_iter++);
        line.y1 = *(values_iter++);
        line.x2 = *(values_iter++);
        line.y2 = *(values_iter++);
    }

    return lines;
}


class LinesCallbacks: public ControlLineManager::Callbacks {
protected:
    PerspCorrection *tool;

public:
    explicit LinesCallbacks(PerspCorrection *tool): tool(tool) {}

    void lineChanged() override
    {
        if (tool) {
            tool->lineChanged();
        }
    }
    
    void switchOffEditMode() override
    {
        if (tool) {
            tool->switchOffEditMode();
        }
    }
};

} // namespace


PerspCorrection::PerspCorrection() : FoldableToolPanel(this, "perspective", M("TP_PERSPECTIVE_LABEL"), false, true, true)
{
    EvToolEnabled.set_action(rtengine::TRANSFORM);
    EvToolReset.set_action(rtengine::TRANSFORM);
    
    auto m = ProcEventMapper::getInstance();
    EvPerspCorrLens = m->newEvent(rtengine::TRANSFORM, "HISTORY_MSG_PERSPECTIVE_LENS");
    EvPerspControlLines = m->newEvent(rtengine::M_VOID, "HISTORY_MSG_PERSPECTIVE_CTRL_LINE");
    EvPerspRender = m->newAnonEvent(rtengine::TRANSFORM);
    
    lgl = nullptr;
    panel_listener = nullptr;
    metadata = nullptr;

    Gtk::Image *ipersHL = Gtk::manage(new RTImage("perspective-horizontal-left-small.svg"));
    Gtk::Image *ipersHR = Gtk::manage(new RTImage("perspective-horizontal-right-small.svg"));
    Gtk::Image *ipersVL = Gtk::manage(new RTImage("perspective-vertical-bottom-small.svg"));
    Gtk::Image *ipersVR = Gtk::manage(new RTImage("perspective-vertical-top-small.svg"));
    Gtk::Image *ipersSL = Gtk::manage(new RTImage("perspective-shear-left-small.svg"));
    Gtk::Image *ipersSR = Gtk::manage(new RTImage("perspective-shear-right-small.svg"));
    Gtk::Image *irotateL = Gtk::manage(new RTImage("rotate-right-small.svg"));
    Gtk::Image *irotateR = Gtk::manage(new RTImage("rotate-left-small.svg"));
    Gtk::Image *iaspectL = Gtk::manage(new RTImage("perspective-aspect-vertical-small.svg"));
    Gtk::Image *iaspectR = Gtk::manage(new RTImage("perspective-aspect-horizontal-small.svg"));

    horiz = Gtk::manage(new Adjuster(M("TP_PERSPECTIVE_HORIZONTAL"), -200, 200, 0.1, 0, ipersHL, ipersHR));
    horiz->setAdjusterListener(this);

    vert = Gtk::manage(new Adjuster(M("TP_PERSPECTIVE_VERTICAL"), -200, 200, 0.1, 0, ipersVL, ipersVR));
    vert->setAdjusterListener(this);

    angle = Gtk::manage(new Adjuster(M("TP_PERSPECTIVE_ANGLE"), -20, 20, 0.01, 0, irotateL, irotateR));
    shear = Gtk::manage(new Adjuster(M("TP_PERSPECTIVE_SHEAR"), -50, 50, 0.1, 0, ipersSL, ipersSR));
    flength = Gtk::manage(new Adjuster(M("TP_PERSPECTIVE_FLENGTH"), 1, 1200, 0.1, 28));
    cropfactor = Gtk::manage(new Adjuster(M("TP_PERSPECTIVE_CROPFACTOR"), 0.1, 10, 0.01, 1));
    aspect = Gtk::manage(new Adjuster(M("TP_PERSPECTIVE_ASPECT"), 0.5, 2, 0.001, 1, iaspectL, iaspectR));
    angle->setAdjusterListener(this);
    shear->setAdjusterListener(this);
    flength->setAdjusterListener(this);
    cropfactor->setAdjusterListener(this);
    aspect->setAdjusterListener(this);

    pack_start(*horiz);
    pack_start(*vert);
    pack_start(*shear);
    pack_start(*angle);
    pack_start(*aspect);
    pack_start(*flength);
    pack_start(*cropfactor);

    horiz->setLogScale(2, 0);
    vert->setLogScale(2, 0);
    flength->setLogScale(100, 1);
    cropfactor->setLogScale(2, 1);
    aspect->setLogScale(100, 1, true);

    // Begin control lines interface.
    Gtk::Image *ipers_draw = Gtk::manage(new RTImage("edit.svg"));
    Gtk::Image *ipers_trash = Gtk::manage(new RTImage("trash-empty.svg"));
    Gtk::Image *ipers_apply = Gtk::manage(new RTImage("tick.svg"));
    
    lines_button_apply = Gtk::manage(new Gtk::Button());
    lines_button_apply->set_image(*ipers_apply);
    lines_button_apply->set_tooltip_text(M("GENERAL_APPLY"));
    lines_button_apply->set_sensitive(false);
    lines_button_apply->signal_pressed().connect(sigc::mem_fun(*this, &PerspCorrection::linesApplyButtonPressed));

    lines_button_edit = Gtk::manage (new Gtk::ToggleButton());
    lines_button_edit->set_image(*ipers_draw);
    lines_button_edit->set_tooltip_text(M("GENERAL_EDIT"));
    lines_button_edit->signal_toggled().connect(sigc::mem_fun(*this, &PerspCorrection::linesEditButtonPressed));

    lines_button_erase = Gtk::manage (new Gtk::Button());
    lines_button_erase->set_image(*ipers_trash);
    lines_button_erase->set_tooltip_text(M("GENERAL_DELETE_ALL"));
    lines_button_erase->set_sensitive(false);
    lines_button_erase->signal_pressed().connect(sigc::mem_fun(*this, &PerspCorrection::linesEraseButtonPressed));

    lines.reset(new ControlLineManager());
    lines->callbacks = std::make_shared<LinesCallbacks>(this);

    Gtk::HBox *control_lines_box = Gtk::manage(new Gtk::HBox());
    Gtk::Label *control_lines_label = Gtk::manage(new Gtk::Label(M("TP_PERSPECTIVE_CONTROL_LINES") + ": "));
    control_lines_label->set_tooltip_markup(M("TP_PERSPECTIVE_CONTROL_LINES_TOOLTIP") );
    control_lines_box->pack_start(*control_lines_label, Gtk::PACK_SHRINK);
    control_lines_box->pack_start(*lines_button_edit);
    control_lines_box->pack_start(*lines_button_apply);
    control_lines_box->pack_start(*lines_button_erase);
    // End control lines interface.

    auto_horiz = Gtk::manage(new Gtk::Button());
    auto_horiz->add(*Gtk::manage(new RTImage("perspective-horizontal-left.svg")));
    auto_horiz->get_style_context()->add_class("independent");
    auto_horiz->set_tooltip_markup(M("TP_PERSPECTIVE_AUTO_HORIZONTAL_TOOLTIP"));

    auto_vert = Gtk::manage(new Gtk::Button());
    auto_vert->add(*Gtk::manage(new RTImage("perspective-vertical-top.svg")));
    auto_vert->get_style_context()->add_class("independent");
    auto_vert->set_tooltip_markup(M("TP_PERSPECTIVE_AUTO_VERTICAL_TOOLTIP"));

    auto_both = Gtk::manage(new Gtk::Button());
    auto_both->add(*Gtk::manage(new RTImage("perspective-horizontal-vertical.svg")));
    auto_both->get_style_context()->add_class("independent");
    auto_both->set_tooltip_markup(M("TP_PERSPECTIVE_AUTO_BOTH_TOOLTIP"));

    Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
    hb->pack_start(*Gtk::manage(new Gtk::Label(M("TP_PERSPECTIVE_AUTO") + ": ")), Gtk::PACK_SHRINK, 4);
    hb->pack_start(*auto_horiz, Gtk::PACK_EXPAND_WIDGET, 2);
    hb->pack_start(*auto_vert, Gtk::PACK_EXPAND_WIDGET, 2);
    hb->pack_start(*auto_both, Gtk::PACK_EXPAND_WIDGET, 2);
    auto_horiz->show();
    auto_vert->show();
    auto_both->show();

    auto_horiz->signal_pressed().connect(sigc::bind(sigc::mem_fun(*this, &PerspCorrection::autoPressed), auto_horiz));
    auto_vert->signal_pressed().connect(sigc::bind(sigc::mem_fun(*this, &PerspCorrection::autoPressed), auto_vert));
    auto_both->signal_pressed().connect(sigc::bind(sigc::mem_fun(*this, &PerspCorrection::autoPressed), auto_both));

    pack_start(*Gtk::manage (new  Gtk::HSeparator()));
    pack_start(*control_lines_box);
    pack_start(*Gtk::manage (new  Gtk::HSeparator()));
    pack_start(*hb, Gtk::PACK_EXPAND_WIDGET, 4);
    hb->show();
    
    show_all();
}

void PerspCorrection::read(const ProcParams* pp)
{
    disableListener ();

    horiz->setValue (pp->perspective.horizontal);
    vert->setValue (pp->perspective.vertical);
    angle->setValue(pp->perspective.angle);
    shear->setValue(pp->perspective.shear);
    aspect->setValue(pp->perspective.aspect);
    if (pp->perspective.flength > 0) {
        flength->setValue(pp->perspective.flength);
        cropfactor->setValue(pp->perspective.cropfactor);
    } else if (metadata) {
        do_set_metadata(metadata);
    } else {
        flength->setValue(28);
        cropfactor->setValue(1);
    }
    lines->setLines(valuesToControlLines(pp->perspective.control_lines));
    setEnabled(pp->perspective.enabled);

    enableListener ();
}

void PerspCorrection::write(ProcParams* pp)
{
    pp->perspective.enabled = getEnabled();
    pp->perspective.horizontal  = horiz->getValue ();
    pp->perspective.vertical = vert->getValue ();
    pp->perspective.angle = angle->getValue();
    pp->perspective.shear = shear->getValue();
    pp->perspective.flength = flength->getValue();
    pp->perspective.cropfactor = cropfactor->getValue();
    pp->perspective.aspect = aspect->getValue();
    std::vector<rtengine::ControlLine> control_lines;
    lines->toControlLines(control_lines);
    controlLinesToValues(control_lines, pp->perspective.control_lines);
}

void PerspCorrection::setDefaults(const ProcParams* defParams)
{
    horiz->setDefault (defParams->perspective.horizontal);
    vert->setDefault (defParams->perspective.vertical);
    angle->setDefault(defParams->perspective.angle);
    shear->setDefault(defParams->perspective.shear);
    flength->setDefault(defParams->perspective.flength);
    cropfactor->setDefault(defParams->perspective.cropfactor);
    aspect->setDefault(defParams->perspective.aspect);

    initial_params = defParams->perspective;
}

void PerspCorrection::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        if (a == flength || a == cropfactor || a == aspect) {
            listener->panelChanged(EvPerspCorrLens, Glib::ustring::compose("%1=%2  %3=%4\n%5=%6", M("TP_PERSPECTIVE_FLENGTH"), flength->getValue(), M("TP_PERSPECTIVE_CROPFACTOR"), cropfactor->getValue(), M("TP_PERSPECTIVE_ASPECT"), aspect->getValue()));
        } else {
            listener->panelChanged(EvPerspCorr, Glib::ustring::compose ("%1=%2  %3=%4\n%5=%6  %7=%8", M("TP_PERSPECTIVE_HORIZONTAL"), horiz->getValue(), M("TP_PERSPECTIVE_VERTICAL"), vert->getValue(), M("TP_PERSPECTIVE_ANGLE"), angle->getValue(), M("TP_PERSPECTIVE_SHEAR"), shear->getValue()));
        }
    }
}

void PerspCorrection::adjusterAutoToggled(Adjuster* a, bool newval)
{
}


void PerspCorrection::trimValues (rtengine::procparams::ProcParams* pp)
{
    horiz->trimValue(pp->perspective.horizontal);
    vert->trimValue(pp->perspective.vertical);
    angle->trimValue(pp->perspective.angle);
    shear->trimValue(pp->perspective.shear);
    flength->trimValue(pp->perspective.flength);
    cropfactor->trimValue(pp->perspective.cropfactor);
    aspect->trimValue(pp->perspective.aspect);
}


void PerspCorrection::autoPressed(Gtk::Button *which)
{
    if (!lgl) {
        return;
    }
    
    double a, h, v, s;
    bool hh = false;
    bool vv = false;

    if (which == auto_horiz) {
        hh = true;
    } else if (which == auto_vert) {
        vv = true;
    } else if (which == auto_both) {
        hh = true;
        vv = true;
    }

    lgl->autoPerspectiveRequested(hh, vv, a, h, v, s);

    disableListener();
    setEnabled(true);
    angle->setValue(a);
    horiz->setValue(h);
    vert->setValue(v);
    shear->setValue(s);
    enableListener();

    adjusterChanged(nullptr, 0);
}


void PerspCorrection::applyControlLines()
{
    if (!lgl) {
        return;
    }

    std::vector<rtengine::ControlLine> control_lines;
    int h_count = 0, v_count = 0;
    double a = angle->getValue();
    double h = horiz->getValue();
    double v = vert->getValue();
    double s = shear->getValue();

    lines->toControlLines(control_lines);

    for (unsigned int i = 0; i < lines->size(); i++) {
        if (control_lines[i].type == rtengine::ControlLine::HORIZONTAL) {
            h_count++;
        } else if (control_lines[i].type == rtengine::ControlLine::VERTICAL) {
            v_count++;
        }
    }
    lgl->autoPerspectiveRequested(h_count > 1, v_count > 1, a, h, v, s, &control_lines);

    disableListener();
    setEnabled(true);
    angle->setValue(a);
    horiz->setValue(h);
    vert->setValue(v);
    shear->setValue(s);
    enableListener();

    adjusterChanged(nullptr, 0);
}


void PerspCorrection::do_set_metadata(const rtengine::FramesMetaData *meta)
{
    metadata = meta;
    if (metadata) {
        double f = metadata->getFocalLen();
        double f35 = metadata->getFocalLen35mm();
        if (f > 0) {
            flength->setValue(f);
            flength->setDefault(f, true);
            if (f35 > 0) {
                double crop = f35 / f;
                cropfactor->setValue(crop);
                cropfactor->setDefault(crop, true);
            }
        }
    }
}


void PerspCorrection::setRawMeta(bool raw, const rtengine::FramesMetaData *meta)
{
    disableListener();
    do_set_metadata(meta);
    enableListener();
}


void PerspCorrection::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.perspective = initial_params;
    }
    pp.perspective.enabled = getEnabled();
    read(&pp);
}


void PerspCorrection::switchOffEditMode()
{
    lines_button_edit->set_active(false);
}

void PerspCorrection::setEditProvider(EditDataProvider*provider)
{
    lines->setEditProvider(provider);
}

void PerspCorrection::lineChanged()
{
    if (listener) {
        listener->panelChanged(EvPerspControlLines, M("HISTORY_CHANGED"));
    }
}

void PerspCorrection::linesApplyButtonPressed()
{
    applyControlLines();
    lines_button_edit->set_active(false);
}

void PerspCorrection::linesEditButtonPressed()
{
    if (lines_button_edit->get_active()) { // Enter edit mode.
        lines->setActive(true);
        lines->setDrawMode(true);
        if (lgl) {
            lgl->updateTransformPreviewRequested(EvPerspRender, false);
        }
        lines_button_apply->set_sensitive(true);
        lines_button_erase->set_sensitive(true);
        //setCamBasedEventsActive(false);
        if (panel_listener) {
            panel_listener->controlLineEditModeChanged(true);
        }
    } else { // Leave edit mode.
        //setCamBasedEventsActive(true);
        lines_button_apply->set_sensitive(false);
        lines_button_erase->set_sensitive(false);
        if (lgl) {
            lgl->updateTransformPreviewRequested(EvPerspRender, true);
        }
        lines->setDrawMode(false);
        lines->setActive(false);
        if (panel_listener) {
            panel_listener->controlLineEditModeChanged(false);
        }
    }
}

void PerspCorrection::linesEraseButtonPressed()
{
    lines->removeAll();
}


void PerspCorrection::requestApplyControlLines()
{
    if (lines_button_apply->is_sensitive()) {
        linesApplyButtonPressed();
    }
}


void PerspCorrection::setControlLineEditMode(bool active)
{
    lines_button_edit->set_active(active);
}
