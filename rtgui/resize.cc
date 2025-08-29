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
#include <iomanip>
#include "resize.h"
#include "guiutils.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;

namespace { constexpr double IN_TO_CM = 2.54; } 


Resize::Resize():
    FoldableToolPanel(this, "resize", M("TP_RESIZE_LABEL"), false, true, true),
    maxw(100000), maxh(100000)
{
    auto m = ProcEventMapper::getInstance();
    EvResizeAllowUpscaling = m->newEvent(rtengine::RESIZE, "HISTORY_MSG_RESIZE_ALLOWUPSCALING");
    EvUnit = m->newEvent(rtengine::RESIZE, "HISTORY_MSG_RESIZE_UNIT");
    EvPPI = m->newEvent(rtengine::RESIZE, "HISTORY_MSG_RESIZE_PPI");
    EvToolReset.set_action(rtengine::RESIZE);

    cropw = 0;
    croph = 0;

    Gtk::Table *combos = Gtk::manage(new Gtk::Table (2, 2));

    appliesTo = Gtk::manage (new MyComboBoxText ());
    appliesTo->append (M("TP_RESIZE_CROPPEDAREA"));
    appliesTo->append (M("TP_RESIZE_FULLIMAGE"));
    appliesTo->set_active (0);

    Gtk::Label *label = Gtk::manage (new Gtk::Label (M("TP_RESIZE_APPLIESTO")));
    label->set_alignment(0., 0.);
    combos->attach (*label, 0, 1, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 2, 2);
    combos->attach (*appliesTo, 1, 2, 0, 1, Gtk::EXPAND | Gtk::FILL, Gtk::SHRINK, 2, 2);

    spec = Gtk::manage (new MyComboBoxText ());
    spec->append (M("TP_RESIZE_SCALE"));
    spec->append (M("TP_RESIZE_WIDTH"));
    spec->append (M("TP_RESIZE_HEIGHT"));
    spec->append (M("TP_RESIZE_FITBOX"));
    spec->set_active (0);

    label = Gtk::manage (new Gtk::Label (M("TP_RESIZE_SPECIFY")));
    label->set_alignment(0., 0.);
    combos->attach (*label, 0, 1, 2, 3, Gtk::SHRINK, Gtk::SHRINK, 2, 2);
    combos->attach (*spec, 1, 2, 2, 3, Gtk::EXPAND | Gtk::FILL, Gtk::SHRINK, 2, 2);

    pack_start (*combos, Gtk::PACK_SHRINK, 4);

    scale = new Adjuster (M("TP_RESIZE_SCALE"), 0.01, MAX_SCALE, 0.01, 1.);
    scale->setAdjusterListener (this);
    scale->setLogScale(MAX_SCALE * 10.f, 1.f, true);

    pack_start (*scale, Gtk::PACK_SHRINK, 4);

    sizeBox = Gtk::manage (new Gtk::VBox ());

    Gtk::HBox* sbox = Gtk::manage (new Gtk::HBox ());
    Gtk::HBox* wbox = Gtk::manage (new Gtk::HBox ());
    Gtk::HBox* hbox = Gtk::manage (new Gtk::HBox ());
    w = Gtk::manage (new MySpinButton ());
    h = Gtk::manage (new MySpinButton ());
    wbox->set_spacing(3);
    wbox->pack_start (*Gtk::manage (new Gtk::Label (M("TP_RESIZE_W"))), Gtk::PACK_SHRINK, 0);
    wbox->pack_start (*w);
    hbox->set_spacing(3);
    hbox->pack_start (*Gtk::manage (new Gtk::Label (M("TP_RESIZE_H"))), Gtk::PACK_SHRINK, 0);
    hbox->pack_start (*h);
    sbox->set_spacing(4);
    sbox->pack_start (*wbox);
    sbox->pack_start (*hbox);

    sizeBox->pack_start(*sbox, Gtk::PACK_SHRINK, 0);

    endBox = Gtk::manage(new Gtk::VBox());

    hbox = Gtk::manage(new Gtk::HBox());
    hbox->set_spacing(3);
    hbox->pack_start(*Gtk::manage(new Gtk::Label(M("TP_RESIZE_UNIT") + ": ")), Gtk::PACK_SHRINK, 0);
    unit = Gtk::manage(new MyComboBoxText());
    unit->append(M("TP_RESIZE_PX"));
    unit->append(M("TP_RESIZE_CM"));
    unit->append(M("TP_RESIZE_IN"));
    unit->set_active(0);
    hbox->pack_start(*unit);

    allowUpscaling = Gtk::manage(new Gtk::CheckButton(M("TP_RESIZE_ALLOW_UPSCALING")));
    hbox->pack_start(*allowUpscaling);
    endBox->pack_start(*hbox);
    unitBox = hbox;
    unitBox->reference();

    // ppigrid START
    Gtk::Grid *ppigrid = Gtk::manage(new Gtk::Grid());
    ppigrid->get_style_context()->add_class("grid-spacing");
    ppigrid->set_column_homogeneous(true);
    setExpandAlignProperties(ppigrid, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);

    Gtk::HSeparator *ppiseparator = Gtk::manage(new Gtk::HSeparator());
    ppiseparator->get_style_context()->add_class("grid-row-separator");

    Gtk::Grid *ppisubgrid = Gtk::manage(new Gtk::Grid());
    ppisubgrid->get_style_context()->add_class("grid-spacing");
    setExpandAlignProperties(ppisubgrid, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);

    Gtk::Label *ppilab = Gtk::manage(new Gtk::Label(M("TP_CROP_PPI")));
    setExpandAlignProperties(ppilab, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);

    ppi = Gtk::manage(new MySpinButton());
    setExpandAlignProperties(ppi, true, false, Gtk::ALIGN_END, Gtk::ALIGN_CENTER);
    ppi->set_width_chars(4);

    ppisubgrid->attach(*ppilab, 0, 0, 1, 1);
    ppisubgrid->attach(*ppi, 1, 0, 1, 1);

    size_info_1 = Gtk::manage(new Gtk::Label (M("GENERAL_NA") + " cm x " + M("GENERAL_NA") + " cm"));
    setExpandAlignProperties(size_info_1, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_CENTER);

    size_info_2 = Gtk::manage (new Gtk::Label (M("GENERAL_NA") + " in x " + M("GENERAL_NA") + " in"));
    setExpandAlignProperties(size_info_2, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_CENTER);

    ppigrid->attach(*ppiseparator, 0, 0, 2, 1);
    ppigrid->attach(*size_info_1, 1, 1, 1, 1);
    ppigrid->attach(*size_info_2, 1, 2, 1, 1);
    ppigrid->attach(*ppisubgrid, 0, 1, 1, 2);
    endBox->pack_start(*ppigrid, Gtk::PACK_SHRINK, 4);

    pack_start(*endBox);

    ppi->set_value (300);
    // ppigrid END
    
    sizeBox->show_all();
    sizeBox->reference();

    allowUpscaling->signal_toggled().connect(sigc::mem_fun(*this, &Resize::allowUpscalingChanged));

    w->set_digits (0);
    w->set_increments (1, 100);
    w->set_value (800);
    w->set_range (32, MAX_SCALE * maxw);

    h->set_digits (0);
    h->set_increments (1, 100);
    h->set_value (600);
    h->set_range (32, MAX_SCALE * maxh);

    wconn = w->signal_value_changed().connect ( sigc::mem_fun(*this, &Resize::entryWChanged), true);
    hconn = h->signal_value_changed().connect ( sigc::mem_fun(*this, &Resize::entryHChanged), true);
    aconn = appliesTo->signal_changed().connect ( sigc::mem_fun(*this, &Resize::appliesToChanged) );
    sconn = spec->signal_changed().connect ( sigc::mem_fun(*this, &Resize::specChanged) );
    ppiconn = ppi->signal_value_changed().connect(sigc::mem_fun(*this, &Resize::ppiChanged));
    unitconn = unit->signal_changed().connect(sigc::mem_fun(*this, &Resize::unitChanged));

    packBox = Gtk::manage (new ToolParamBlock ());
    pack_end (*packBox);
    packBox->hide();
    packBox->set_tooltip_markup (M("TP_PRSHARPENING_TOOLTIP"));

    ppi->set_digits(0);
    ppi->set_increments(1, 100);
    ppi->set_range(50, 1200);
    ppi->set_value(300);    

    show_all();
}

Resize::~Resize ()
{
    idle_register.destroy();
    delete scale;
    delete unitBox;
    delete sizeBox;
}

void Resize::read(const ProcParams* pp)
{
    disableListener ();
    ConnectionBlocker ab(aconn);
    ConnectionBlocker wb(wconn);
    ConnectionBlocker hb(hconn);
    ConnectionBlocker sb(sconn);
    ConnectionBlocker ub(unitconn);
    ConnectionBlocker pb(ppiconn);
    scale->block(true);

    prev_unit = pp->resize.unit;

    scale->setValue (pp->resize.scale);
    w->set_value (pp->resize.width);
    h->set_value (pp->resize.height);
    setEnabled (pp->resize.enabled);
    spec->set_active (pp->resize.dataspec);
    allowUpscaling->set_active(pp->resize.allowUpscaling);
    unit->set_active(int(pp->resize.unit));
    ppi->set_value(pp->resize.ppi);

    updateInfoLabels();
    updateGUI();
    setRanges();

    appliesTo->set_active (0);

    if (pp->resize.appliesTo == "Cropped area") {
        appliesTo->set_active (0);
    } else if (pp->resize.appliesTo == "Full image") {
        appliesTo->set_active (1);
    }

    wDirty = false;
    hDirty = false;

    scale->block(false);
    enableListener ();
}


void Resize::write(ProcParams* pp)
{
    int dataSpec = spec->get_active_row_number();

    pp->resize.scale  = scale->getValue();

    pp->resize.appliesTo = "Cropped area";

    if (appliesTo->get_active_row_number() == 0) {
        pp->resize.appliesTo = "Cropped area";
    } else if (appliesTo->get_active_row_number() == 1) {
        pp->resize.appliesTo = "Full image";
    }

    pp->resize.dataspec = dataSpec;
    pp->resize.width = w->get_value();
    pp->resize.height = h->get_value();
    pp->resize.enabled = getEnabled();
    //printf("  L:%d   H:%d\n", pp->resize.width, pp->resize.height);

    pp->resize.allowUpscaling = allowUpscaling->get_active();

    pp->resize.unit = ResizeParams::Unit(unit->get_active_row_number());
    pp->resize.ppi = ppi->get_value_as_int();
}


void Resize::setDefaults(const ProcParams* defParams)
{
    scale->setDefault (defParams->resize.scale);
    initial_params = defParams->resize;
}


void Resize::adjusterChanged(Adjuster* a, double newval)
{
    ConnectionBlocker wb(wconn);
    ConnectionBlocker hb(hconn);
    h->set_value((croph && appliesTo->get_active_row_number() == 0 ? croph : maxh) * a->getValue());
    w->set_value((cropw && appliesTo->get_active_row_number() == 0 ? cropw : maxw) * a->getValue());
    updateInfoLabels();

    if (listener && getEnabled()) {
        listener->panelChanged (EvResizeScale, Glib::ustring::format (std::setw(5), std::fixed, std::setprecision(2), scale->getValue()));
    }
}

void Resize::adjusterAutoToggled(Adjuster* a, bool newval)
{
}

int Resize::getComputedWidth()
{

    if (cropw && appliesTo->get_active_row_number() == 0)
        // we use the crop dimensions
    {
        return (int)((double)(cropw) * (to_px(h->get_value()) / (double)(croph)) + 0.5);
    } else
        // we use the image dimensions
    {
        return (int)((double)(maxw) * (to_px(h->get_value()) / (double)(maxh)) + 0.5);
    }
}

int Resize::getComputedHeight()
{

    if (croph && appliesTo->get_active_row_number() == 0)
        // we use the crop dimensions
    {
        return (int)((double)(croph) * (to_px(w->get_value()) / (double)(cropw)) + 0.5);
    } else
        // we use the image dimensions
    {
        return (int)((double)(maxh) * (to_px(w->get_value()) / (double)(maxw)) + 0.5);
    }
}

void Resize::appliesToChanged ()
{

    //printf("\nPASSAGE EN MODE \"%s\"\n\n", appliesTo->get_active_text().c_str());
    setDimensions();

    if (listener && getEnabled()) {
        //printf("Appel du listener\n");
        listener->panelChanged (EvResizeAppliesTo, appliesTo->get_active_text());
    }
}


void Resize::update(bool isCropped, int cw, int ch, int ow, int oh)
{
    // updating crop values now
    if (isCropped) {
        cropw = cw;
        croph = ch;
    } else {
        cropw = 0;
        croph = 0;
    }

    // updating the full image dimensions
    if (ow && oh) {
        maxw = ow;
        maxh = oh;
    }

    // updating the GUI synchronously
    setDimensions();
}


void Resize::sizeChanged(int mw, int mh, int ow, int oh)
{
    // updating max values now
    maxw = ow;
    maxh = oh;

    // updating the GUI synchronously
    setDimensions();
}


void Resize::setDimensions()
{
    idle_register.add(
        [this]() -> bool
        {
            ConnectionBlocker wb(wconn);
            ConnectionBlocker hb(hconn);
            scale->block(true);

            int refw, refh;

            if (appliesTo->get_active_row_number() == 0 && cropw) {
                // Applies to Cropped area
                refw = cropw;
                refh = croph;
            } else {
                // Applies to Full image or crop is disabled
                refw = maxw;
                refh = maxh;
            }

            setRanges();
            updateInfoLabels();

            switch (spec->get_active_row_number()) {
                case 0: {
                    // Scale mode
                    w->set_value(from_px(static_cast<double>(static_cast<int>(static_cast<double>(refw) * scale->getValue() + 0.5))));
                    h->set_value(from_px(static_cast<double>(static_cast<int>(static_cast<double>(refh) * scale->getValue() + 0.5))));
                    break;
                }

                case 1: {
                    // Width mode
                    const double tmp_scale = to_px(w->get_value()) / static_cast<double>(refw);
                    scale->setValue(tmp_scale);
                    h->set_value(from_px(static_cast<double>(static_cast<int>(static_cast<double>(refh) * tmp_scale + 0.5))));
                    break;
                }

                case 2: {
                    // Height mode
                    const double tmp_scale = to_px(h->get_value()) / static_cast<double>(refh);
                    scale->setValue(tmp_scale);
                    w->set_value(from_px(static_cast<double>(static_cast<int>(static_cast<double>(refw) * tmp_scale + 0.5))));
                    break;
                }

                case 3: {
                    // Bounding box mode
                    const double tmp_scale =
                        to_px(w->get_value()) / to_px(h->get_value()) < static_cast<double>(refw) / static_cast<double>(refh)
                            ? to_px(w->get_value()) / static_cast<double>(refw)
                            : to_px(h->get_value()) / static_cast<double>(refh);

                    scale->setValue(tmp_scale);
                    break;
                }

                default: {
                    break;
                }
            }

            scale->block(false);

            return false;
        }
    );
}

void Resize::fitBoxScale()
{
    double tmpScale;
    double neww = to_px(w->get_value());
    double newh = to_px(h->get_value());

    if (cropw && appliesTo->get_active_row_number() == 0) {
        // we use the crop dimensions
        if (((double)(cropw) / (double)(croph)) > (neww / newh)) {
            // the new scale is given by the image width
            tmpScale = neww / (double)(cropw);
        } else {
            // the new scale is given by the image height
            tmpScale = newh / (double)(croph);
        }
    } else {
        // we use the image dimensions
        if (((double)(maxw) / (double)(maxh)) > (neww / newh)) {
            // the new scale is given by the image width
            tmpScale = neww / (double)(maxw);
        } else {
            // the new scale is given by the image height
            tmpScale = newh / (double)(maxh);
        }
    }

    scale->setValue (tmpScale);
}

void Resize::entryWChanged ()
{

    wDirty = true;

    // updating width
    if (spec->get_active_row_number() == 3) {
        // Fit box mode
        fitBoxScale();
    } else {
        // Other modes
        ConnectionBlocker hb(hconn);
        scale->block(true);

        h->set_value (from_px(getComputedHeight()));
        scale->setValue (to_px(w->get_value ()) / (cropw && appliesTo->get_active_row_number() == 0 ? (double)cropw : (double)maxw));

        scale->block(false);
    }

    updateInfoLabels();

    if (listener) {
        if (spec->get_active_row_number() == 3) {
            notifyBBox();
        } else {
            if (getEnabled()) {
                listener->panelChanged (EvResizeWidth, Glib::ustring::format (w->get_value()));
            }
        }
    }
}

void Resize::entryHChanged ()
{

    hDirty = true;

    if (listener) {
        if (spec->get_active_row_number() == 3) {
            // Fit box mode
            fitBoxScale();
        } else {
            // Other modes
            ConnectionBlocker wb(wconn);
            scale->block(true);

            w->set_value(from_px(getComputedWidth()));
            scale->setValue(to_px(h->get_value()) / (croph && appliesTo->get_active_row_number() == 0 ? (double)croph : (double)maxh));

            scale->block(false);
        }
    }

    updateInfoLabels();

    if (listener) {
        if (spec->get_active_row_number() == 3) {
            notifyBBox();
        } else {
            if (getEnabled()) {
                listener->panelChanged (EvResizeHeight, Glib::ustring::format (h->get_value()));
            }
        }
    }
}

void Resize::specChanged ()
{

    switch (spec->get_active_row_number()) {
    case (0):
        // Scale mode
        scale->sliderChanged();
        unit->set_active(0);
        break;

    case (1):
        // Width mode
        w->set_value(from_px(getComputedWidth()));
        entryWChanged();
        break;

    case (2):
        // Height mode
        h->set_value(from_px(getComputedHeight()));
        entryHChanged();
        break;

    case (3):
        // Bounding box mode
        notifyBBox();
        break;

    default:
        break;
    }

    updateGUI();
    setRanges();
}


void Resize::updateGUI ()
{

    removeIfThere(this, scale, false);
    removeIfThere(this, sizeBox, false);
    removeIfThere(endBox, unitBox, false);

    switch (spec->get_active_row_number()) {
    case (0):
        // Scale mode
        pack_start(*scale, Gtk::PACK_SHRINK, 4);
        reorder_child(*endBox, 4);
        break;

    case (1):
        // Width mode
        pack_start(*sizeBox, Gtk::PACK_SHRINK, 4);
        reorder_child(*endBox, 4);
        w->set_sensitive (true);
        h->set_sensitive (false);
        break;

    case (2):
        // Height mode
        pack_start(*sizeBox, Gtk::PACK_SHRINK, 4);
        reorder_child(*endBox, 4);
        w->set_sensitive (false);
        h->set_sensitive (true);
        break;

    case (3):
        // Bounding box mode
        pack_start(*sizeBox, Gtk::PACK_SHRINK, 4);
        reorder_child(*endBox, 4);
        w->set_sensitive (true);
        h->set_sensitive (true);
        break;

    default:
        break;
    }

    //unit->set_sensitive(spec->get_active_row_number() > 0);
    if (spec->get_active_row_number() > 0) {
        endBox->pack_start(*unitBox);
        endBox->reorder_child(*unitBox, 0);
    }
}

void Resize::notifyBBox()
{
    updateInfoLabels();

    if (listener && getEnabled()) {
        listener->panelChanged (EvResizeBoundingBox, Glib::ustring::compose("(%1x%2)", w->get_value(), h->get_value() ));
    }
}


void Resize::enabledChanged ()
{

    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged (EvResizeEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged (EvResizeEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged (EvResizeEnabled, M("GENERAL_DISABLED"));
        }
    }
}


void Resize::setRanges()
{
    int refw, refh;
    if (appliesTo->get_active_row_number() == 0 && cropw) {
        // Applies to Cropped area
        refw = cropw;
        refh = croph;
    } else {
        // Applies to Full image or crop is disabled
        refw = maxw;
        refh = maxh;
    }
    if (spec->get_active_row_number() == 3) { // bounding box
        refw = refh = std::max(refw, refh);
    }
    
    int factor = MAX_SCALE;//allowUpscaling->get_active() ? MAX_SCALE : 1;
    w->set_range(from_px(32), from_px(factor * refw));
    h->set_range(from_px(32), from_px(factor * refh));
    double v = scale->getValue();
    scale->block(true);
    scale->setLimits(0.01, factor, 0.01, 1.0);
    scale->setLogScale(factor * 10.f, 1.f, factor > 1);
    scale->setValue(v);
    scale->block(false);
}


void Resize::allowUpscalingChanged()
{
    setRanges();
    
    if (listener) {
        if (allowUpscaling->get_inconsistent()) {
            listener->panelChanged(EvResizeAllowUpscaling, M("GENERAL_UNCHANGED"));
        } else if (allowUpscaling->get_active()) {
            listener->panelChanged(EvResizeAllowUpscaling, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvResizeAllowUpscaling, M("GENERAL_DISABLED"));
        }
    }
}


void Resize::trimValues (rtengine::procparams::ProcParams* pp)
{

    scale->trimValue(pp->resize.scale);
}


void Resize::ppiChanged()
{
    updateInfoLabels();
    if (listener && getEnabled()) {
        listener->panelChanged(EvPPI, Glib::ustring::format(ppi->get_value_as_int()));
    }
}


void Resize::unitChanged()
{
    //updateInfoLabels();
    int pw = to_px(w->get_value(), prev_unit);
    int ph = to_px(h->get_value(), prev_unit);
    prev_unit = ResizeParams::Unit(unit->get_active_row_number());
    double nw = from_px(pw, prev_unit);
    double nh = from_px(ph, prev_unit);
    ConnectionBlocker wb(wconn);
    ConnectionBlocker hb(hconn);
    if (prev_unit == ResizeParams::PX) {
        w->set_digits(0);
        h->set_digits(0);
    } else {
        w->set_digits(1);
        h->set_digits(1);
    }
    setRanges();
    w->set_value(nw);
    h->set_value(nh);
    updateInfoLabels();
    if (listener && getEnabled()) {
        listener->panelChanged(EvUnit, unit->get_active_text());
    }
}


void Resize::updateInfoLabels()
{
    double iw = 0, ih = 0;
    switch (ResizeParams::Unit(unit->get_active_row_number())) {
    case ResizeParams::CM:
        iw = w->get_value() / IN_TO_CM * ppi->get_value();
        ih = h->get_value() / IN_TO_CM * ppi->get_value();
        size_info_1->set_text(Glib::ustring::compose("%1 px x %2 px", std::round(iw), std::round(ih)));
        size_info_2->set_text(Glib::ustring::compose("%1 in x %2 in", Glib::ustring::format(std::setprecision(3), w->get_value() / IN_TO_CM), Glib::ustring::format(std::setprecision(3), h->get_value() / IN_TO_CM)));
        break;
    case ResizeParams::INCHES:
        iw = w->get_value() * ppi->get_value();
        ih = h->get_value() * ppi->get_value();
        size_info_1->set_text(Glib::ustring::compose("%1 px x %2 px", std::round(iw), std::round(ih)));
        size_info_2->set_text(Glib::ustring::compose("%1 cm x %2 cm", Glib::ustring::format(std::setprecision(3), w->get_value() * IN_TO_CM), Glib::ustring::format(std::setprecision(3), h->get_value() * IN_TO_CM)));
        break;
    default:
        iw = w->get_value() / ppi->get_value();
        ih = h->get_value() / ppi->get_value();
        size_info_1->set_text(Glib::ustring::compose("%1 cm x %2 cm", Glib::ustring::format(std::setprecision(3), iw * IN_TO_CM), Glib::ustring::format(std::setprecision(3), ih * IN_TO_CM)));
        size_info_2->set_text(Glib::ustring::compose("%1 in x %2 in", Glib::ustring::format(std::setprecision(3), iw), Glib::ustring::format(std::setprecision(3), ih)));
        break;
    }
}


double Resize::from_px(int p, ResizeParams::Unit u)
{
    switch (u) {
    case ResizeParams::CM:
        return double(p) / ppi->get_value() * IN_TO_CM;
    case ResizeParams::INCHES:
        return double(p) / ppi->get_value();
    default:
        return p;
    }
}


double Resize::from_px(int p)
{
    return from_px(p, ResizeParams::Unit(unit->get_active_row_number()));
}


int Resize::to_px(double p, ResizeParams::Unit u)
{
    switch (u) {
    case ResizeParams::CM:
        return std::round(ppi->get_value() * (p / IN_TO_CM));
    case ResizeParams::INCHES:
        return std::round(ppi->get_value() * p);
    default:
        return p;
    }    
}


int Resize::to_px(double p)
{
    return to_px(p, ResizeParams::Unit(unit->get_active_row_number()));
}


void Resize::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.resize = initial_params;
    }
    pp.resize.enabled = getEnabled();
    read(&pp);
}
