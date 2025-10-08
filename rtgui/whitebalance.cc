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
#include "whitebalance.h"
#include "../rtengine/refreshmap.h"
#include "eventmapper.h"
#include "../rtengine/colortemp.h"

#include <iomanip>
#include <iostream>

#include "rtimage.h"
#include "options.h"


using namespace rtengine;
using namespace rtengine::procparams;

std::vector<Glib::RefPtr<Gdk::Pixbuf>> WhiteBalance::wbPixbufs;

namespace {

constexpr double CENTERTEMP = 4750;

const std::vector<std::string> labels = {
    "TP_WBALANCE_CAMERA",
    "TP_WBALANCE_AUTO",
    "TP_WBALANCE_CUSTOM",
    "TP_WBALANCE_CUSTOM_MULT"
};

} // namespace

void WhiteBalance::init ()
{
    wbPixbufs.push_back(RTImage::createPixbufFromFile("wb-camera-small.svg"));
    wbPixbufs.push_back(RTImage::createPixbufFromFile("wb-auto-small.svg"));
    wbPixbufs.push_back(RTImage::createPixbufFromFile("wb-custom-small.svg"));
    wbPixbufs.push_back(RTImage::createPixbufFromFile("wb-custom2-small.svg"));
}

void WhiteBalance::cleanup ()
{
    for (size_t i = 0; i < wbPixbufs.size(); ++i) {
        wbPixbufs[i].reset();
    }
}


namespace {

constexpr double PIVOTPOINT = 0.75;
constexpr double PIVOTTEMP = CENTERTEMP * 2;
constexpr double RANGE_1 = PIVOTPOINT * (MAXTEMP - MINTEMP);
constexpr double RANGE_2 = (1.0 - PIVOTPOINT) * (MAXTEMP - MINTEMP);

double wbSlider2Temp(double sval)
{
    double s = sval - MINTEMP;
    if (s <= RANGE_1) {
        double r = s / RANGE_1;
        return MINTEMP + r * (PIVOTTEMP - MINTEMP);
    } else {
        double r = (s - RANGE_1) / RANGE_2;
        return PIVOTTEMP + r * (MAXTEMP - PIVOTTEMP);
    }
}


double wbTemp2Slider(double temp)
{
    if (temp <= PIVOTTEMP) {
        double t = temp - MINTEMP;
        double r = t / (PIVOTTEMP - MINTEMP);
        return MINTEMP + r * RANGE_1;
    } else {
        double t = temp - PIVOTTEMP;
        double r = t / (MAXTEMP - PIVOTTEMP);
        return MINTEMP + RANGE_1 + r * RANGE_2;
    }
}

} // namespace


WhiteBalance::WhiteBalance () : FoldableToolPanel(this, "whitebalance", M("TP_WBALANCE_LABEL"), false, true, true), wbp(nullptr), wblistener(nullptr)
{
    auto m = ProcEventMapper::getInstance();
    EvToolReset.set_action(rtengine::WHITEBALANCE);
    EvWBMult = m->newEvent(rtengine::WHITEBALANCE, "HISTORY_MSG_WBALANCE_MULT");

    Gtk::Grid* methodgrid = Gtk::manage(new Gtk::Grid());
    methodgrid->get_style_context()->add_class("grid-spacing");
    setExpandAlignProperties(methodgrid, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);

    Gtk::Label* lab = Gtk::manage (new Gtk::Label (M("TP_WBALANCE_METHOD") + ":"));
    setExpandAlignProperties(lab, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);

    // Create the Tree model
    refTreeModel = Gtk::TreeStore::create(methodColumns);
    // Create the Combobox
    method = Gtk::manage (new MyComboBox ());
    setExpandAlignProperties(method, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);
    // Assign the model to the Combobox
    method->set_model(refTreeModel);

//    fillMethods();

    //Add the model columns to the Combo (which is a kind of view),
    //rendering them in the default way:
    method->pack_start(methodColumns.colIcon, false);
    method->pack_start(methodColumns.colLabel, true);

    std::vector<Gtk::CellRenderer*> cells = method->get_cells();
    Gtk::CellRendererText* cellRenderer = dynamic_cast<Gtk::CellRendererText*>(cells.at(1));
    cellRenderer->property_ellipsize() = Pango::ELLIPSIZE_MIDDLE;

    method->set_active (0); // Camera
    methodgrid->attach (*lab, 0, 0, 1, 1);
    methodgrid->attach (*method, 1, 0, 1, 1);
    pack_start (*methodgrid, Gtk::PACK_SHRINK, 0 );
    opt = 0;

    Gtk::Grid* spotgrid = Gtk::manage(new Gtk::Grid());
    spotgrid->get_style_context()->add_class("grid-spacing");
    setExpandAlignProperties(spotgrid, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);

    spotbutton = Gtk::manage (new Gtk::Button (M("TP_WBALANCE_PICKER")));
    setExpandAlignProperties(spotbutton, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);
    spotbutton->get_style_context()->add_class("independent");
    spotbutton->set_tooltip_text(M("TP_WBALANCE_SPOTWB"));
    spotbutton->set_image (*Gtk::manage (new RTImage ("color-picker-small.svg")));

    Gtk::Label* slab = Gtk::manage (new Gtk::Label (M("TP_WBALANCE_SIZE")));
    setExpandAlignProperties(slab, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);

    Gtk::Grid* wbsizehelper = Gtk::manage(new Gtk::Grid());
    wbsizehelper->set_name("WB-Size-Helper");
    setExpandAlignProperties(wbsizehelper, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);

    spotsize = Gtk::manage (new MyComboBoxText ());
    setExpandAlignProperties(spotsize, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);
    spotsize->append ("2");

    if (options.whiteBalanceSpotSize == 2) {
        spotsize->set_active(0);
    }

    spotsize->append ("4");

    if (options.whiteBalanceSpotSize == 4) {
        spotsize->set_active(1);
    }

    spotsize->append ("8");

    if (options.whiteBalanceSpotSize == 8) {
        spotsize->set_active(2);
    }

    spotsize->append ("16");

    if (options.whiteBalanceSpotSize == 16) {
        spotsize->set_active(3);
    }

    spotsize->append ("32");

    if (options.whiteBalanceSpotSize == 32) {
        spotsize->set_active(4);
    }

    wbsizehelper->attach (*spotsize, 0, 0, 1, 1);

    spotgrid->attach (*spotbutton, 0, 0, 1, 1);
    spotgrid->attach (*slab, 1, 0, 1, 1);
    spotgrid->attach (*wbsizehelper, 2, 0, 1, 1);
    pack_start (*spotgrid, Gtk::PACK_SHRINK, 0 );

    Gtk::HSeparator *separator = Gtk::manage (new  Gtk::HSeparator());
    separator->get_style_context()->add_class("grid-row-separator");
    pack_start(*separator, Gtk::PACK_SHRINK, 0);

    Gtk::Image* itempL =  Gtk::manage (new RTImage ("circle-blue-small.svg"));
    Gtk::Image* itempR =  Gtk::manage (new RTImage ("circle-yellow-small.svg"));
    Gtk::Image* igreenL = Gtk::manage (new RTImage ("circle-magenta-small.svg"));
    Gtk::Image* igreenR = Gtk::manage (new RTImage ("circle-green-small.svg"));
    Gtk::Image* iblueredL = Gtk::manage (new RTImage ("circle-blue-small.svg"));
    Gtk::Image* iblueredR = Gtk::manage (new RTImage ("circle-red-small.svg"));

    temp = Gtk::manage(new Adjuster(M("TP_WBALANCE_TEMPERATURE"), MINTEMP, MAXTEMP, 5, CENTERTEMP, itempL, itempR, &wbSlider2Temp, &wbTemp2Slider));
    green = Gtk::manage (new Adjuster(M("TP_WBALANCE_GREEN"), MINGREEN, MAXGREEN, 0.001, 1.0, igreenL, igreenR));
    green->setLogScale(100, 1, true);
    equal = Gtk::manage(new Adjuster(M("TP_WBALANCE_EQBLUERED"), MINEQUAL, MAXEQUAL, 0.001, 1.0, iblueredL, iblueredR, nullptr, nullptr, true));
    // cache_customTemp (0);
    // cache_customGreen (0);
    // cache_customEqual (0);
    equal->set_tooltip_markup (M("TP_WBALANCE_EQBLUERED_TOOLTIP"));
    temp->show();
    green->show();
    equal->show();

    temp->delay = options.adjusterMaxDelay;
    green->delay = options.adjusterMaxDelay;
    equal->delay = options.adjusterMaxDelay;
    
    tempBox = Gtk::manage(new Gtk::VBox());

    temp_warning_ = Gtk::manage(new Gtk::HBox());
    auto warnico = Gtk::manage(new RTImage("warning.svg"));
    auto warnlbl = Gtk::manage(new Gtk::Label(M("WARNING_INVALID_WB_TEMP_TINT")));
    warnico->show();
    warnlbl->show();
    temp_warning_->pack_start(*warnico, Gtk::PACK_SHRINK);
    temp_warning_->pack_start(*warnlbl, Gtk::PACK_EXPAND_WIDGET, 4);
    setExpandAlignProperties(warnlbl, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    temp_warning_->hide();
    
    tempBox->pack_start(*temp_warning_);
    tempBox->pack_start(*temp);
    tempBox->pack_start(*green);
    tempBox->pack_start(*equal);
    tempBox->show();
    pack_start(*tempBox);

    temp->setAdjusterListener(this);
    green->setAdjusterListener(this);
    equal->setAdjusterListener(this);

    multBox = Gtk::manage(new Gtk::VBox());
    {
        static const std::vector<std::string> label = {
            "TP_COLORCORRECTION_CHANNEL_R",
            "TP_COLORCORRECTION_CHANNEL_G",
            "TP_COLORCORRECTION_CHANNEL_B"
        };
        static const std::vector<std::string> icon = {
            "circle-red-small.svg",
            "circle-green-small.svg",
            "circle-blue-small.svg"
        };
        for (size_t i = 0; i < 3; ++i) {
            mult[i] = Gtk::manage(new Adjuster(M(label[i]), 0.1, 20, 0.0001, 1, Gtk::manage(new RTImage(icon[i]))));
            multBox->pack_start(*mult[i]);
            mult[i]->show();
            mult[i]->setAdjusterListener(this);
            mult[i]->setLogScale(100, 1, true);
            mult[i]->delay = options.adjusterMaxDelay;
        }
    }
    multBox->show();
    pack_start(*multBox);
    
    spotbutton->signal_pressed().connect( sigc::mem_fun(*this, &WhiteBalance::spotPressed) );
    methconn = method->signal_changed().connect( sigc::mem_fun(*this, &WhiteBalance::methodChanged) );
    spotsize->signal_changed().connect( sigc::mem_fun(*this, &WhiteBalance::spotSizeChanged) );
}


WhiteBalance::~WhiteBalance()
{
    idle_register.destroy();
}


void WhiteBalance::enabledChanged()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged(EvWBEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged(EvWBEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvWBEnabled, M("GENERAL_DISABLED"));
        }
    }
}


void WhiteBalance::adjusterChanged(Adjuster* a, double newval)
{
    int m = getActiveMethod();
    {
        ConnectionBlocker blocker(methconn);
        if (a == mult[0] || a == mult[1] || a == mult[2]) {
            method->set_active(int(WBParams::CUSTOM_MULT));
        } else if (m <= int(WBParams::AUTO)) {
            method->set_active(int(WBParams::CUSTOM_TEMP));
        }
    }

    syncSliders(a == mult[0] || a == mult[1] || a == mult[2]);
    
    if (listener && getEnabled()) {
        if (a == temp) {
            listener->panelChanged(EvWBTemp, Glib::ustring::format ((int)a->getValue()));
        } else if (a == green) {
            listener->panelChanged(EvWBGreen, Glib::ustring::format (std::setw(4), std::fixed, std::setprecision(3), a->getValue()));
        } else if (a == equal) {
            listener->panelChanged(EvWBequal, Glib::ustring::format (std::setw(4), std::fixed, std::setprecision(3), a->getValue()));
        } else if (a == mult[0] || a == mult[1] || a == mult[2]) {
            listener->panelChanged(EvWBMult, Glib::ustring::compose("%1 %2 %3", mult[0]->getTextValue(), mult[1]->getTextValue(), mult[2]->getTextValue()));
        }
    }
}


void WhiteBalance::adjusterAutoToggled(Adjuster* a, bool newval)
{
}


void WhiteBalance::methodChanged()
{
    int m = getActiveMethod();
    auto row = *(method->get_active());
    int preset = row[methodColumns.colPreset];
    // bool update_scale = false;//true;

    disableListener();
    bool check_temp = false;

    if (preset >= 0) {
        ConnectionBlocker methblock(methconn);
        method->set_active(WBParams::CUSTOM_MULT);
        for (int i = 0; i < 3; ++i) {
            mult[i]->setValue(presets[preset].mult[i]);
        }
        syncSliders(true);
        method->set_active(WBParams::CUSTOM_TEMP);
        check_temp = true;
    }

    Glib::ustring label;
    
    switch (m) {
    case int(WBParams::CAMERA): {
        if (wbp) {
            rtengine::ColorTemp ct;
            wbp->getCamWB(ct);
            double m[3];
            ct.getMultipliers(m[0], m[1], m[2]);
            wbp->convertWBMul2Cam(m[0], m[1], m[2]);
            for (int i = 0; i < 3; ++i) {
                mult[i]->setValue(m[i]);
            }
            syncSliders(true);
            check_temp = true;
        }
    } break;
    case int(WBParams::CUSTOM_TEMP):
    case int(WBParams::CUSTOM_MULT):
        break;
    default:
        break;
    }

    if (m < int(labels.size())) {
        label = M(labels[m]);
    }

    if (m != int(WBParams::AUTO)) {
        updateMethodGui(check_temp);
    }

    if (preset >= 0) {
        label = M("TP_WBALANCE_PRESET") + ": " + presets[preset].label;
    }
    
    enableListener();

    if (listener && getEnabled()) {
        listener->panelChanged(EvWBMethod, label);
    }
}


void WhiteBalance::spotPressed ()
{
    if (wblistener) {
        wblistener->spotWBRequested (getSize());
    }
}


void WhiteBalance::spotSizeChanged ()
{
    options.whiteBalanceSpotSize = getSize();

    if (wblistener) {
        wblistener->spotWBRequested (getSize());
    }
}


void WhiteBalance::read(const ProcParams* pp)
{
    disableListener();

    ConnectionBlocker blocker(methconn);

    fillMethods();
    method->set_active(std::min(int(pp->wb.method), 3));

    temp->setValue(pp->wb.temperature);
    green->setValue(pp->wb.green);
    equal->setValue(pp->wb.equal);

    double m[3];
    for (int i = 0; i < 3; ++i) {
        m[i] = pp->wb.mult[i] / pp->wb.mult[1];
        mult[i]->setValue(m[i]);
    }

    bool check_temp = true;
    temp_warning_->hide();

    switch (pp->wb.method) {
    case WBParams::CAMERA:
        if (wbp) {
            rtengine::ColorTemp ctemp;
            wbp->getCamWB(ctemp);
            double m[3];
            ctemp.getMultipliers(m[0], m[1], m[2]);
            wbp->convertWBMul2Cam(m[0], m[1], m[2]);
            for (int i = 0; i < 3; ++i) {
                mult[i]->setValue(m[i]);
            }
            syncSliders(true);
        }
        break;
    case WBParams::CUSTOM_TEMP:
        check_temp = false;
        break;
    case WBParams::AUTO:
        break;
    case WBParams::CUSTOM_MULT:
        syncSliders(true);
        break;
    case WBParams::CUSTOM_MULT_LEGACY: {
        rtengine::ColorTemp ct(m[0], m[1], m[2], 1.0);
        if (wbp) {
            wbp->convertWBMul2Cam(m[0], m[1], m[2]);
            for (int i = 0; i < 3; ++i) {
                mult[i]->setValue(m[i]);
            }
            syncSliders(true);
        }
    } break;
    default:
        syncSliders(false);
        break;
    }

    equal->set_visible(equal->getValue() != 1);

    setEnabled(pp->wb.enabled);
    updateMethodGui(check_temp);

    enableListener();
}


void WhiteBalance::write(ProcParams* pp)
{
    pp->wb.enabled = getEnabled();
    pp->wb.method = WBParams::Type(getActiveMethod());
    pp->wb.temperature = temp->getIntValue ();
    pp->wb.green = green->getValue();
    pp->wb.equal = equal->getValue();
    for (int i = 0; i < 3; ++i) {
        pp->wb.mult[i] = mult[i]->getValue();
    }
}


void WhiteBalance::setDefaults(const ProcParams* defParams)
{

    equal->setDefault (defParams->wb.equal);

    if (wbp && defParams->wb.method == WBParams::CAMERA) {
        rtengine::ColorTemp ctemp;
        wbp->getCamWB(ctemp);

        // FIXME: Seems to be always -1.0, called too early? Broken!
        if (ctemp.getTemp() > 0) {
            temp->setDefault(ctemp.getTemp());
            green->setDefault(ctemp.getGreen());
        }
    } else {
        temp->setDefault(defParams->wb.temperature);
        green->setDefault(defParams->wb.green);
    }
    // Recomputing AutoWB if it's the current method will happen in improccoordinator.cc

    for (int i = 0; i < 3; ++i) {
        mult[i]->setDefault(defParams->wb.mult[i]);
    }

    initial_params = defParams->wb;
}


int WhiteBalance::getSize ()
{
    return atoi(spotsize->get_active_text().c_str());
}


void WhiteBalance::setWB(rtengine::ColorTemp ctemp)
{
    disableListener();
    ConnectionBlocker methblocker(methconn);
//    int m = getActiveMethod();
    method->set_active(int(WBParams::CUSTOM_TEMP));
    setEnabled(true);
    double mm[3];
    ctemp.getMultipliers(mm[0], mm[1], mm[2]);
    if (wbp) {
        wbp->convertWBMul2Cam(mm[0], mm[1], mm[2]);
    }
    for (int i = 0; i < 3; ++i) {
        mult[i]->setValue(mm[i]);
    }
    syncSliders(true);
    updateMethodGui(true);
    enableListener();

    if (listener) {
        if (tempBox->is_visible()) {
            listener->panelChanged(EvWBTemp, Glib::ustring::compose("%1, %2", (int)temp->getValue(), green->getTextValue()));
        } else {
            listener->panelChanged(EvWBMult, Glib::ustring::compose("%1 %2 %3", mult[0]->getTextValue(), mult[1]->getTextValue(), mult[2]->getTextValue()));
        }
    }

    // green->setLogScale(100, vgreen, true);
}


void WhiteBalance::trimValues (rtengine::procparams::ProcParams* pp)
{
    temp->trimValue(pp->wb.temperature);
    green->trimValue(pp->wb.green);
    equal->trimValue(pp->wb.equal);
    for (int i = 0; i < 3; ++i) {
        mult[i]->trimValue(pp->wb.mult[i]);
    }
}


void WhiteBalance::WBChanged(rtengine::ColorTemp ctemp)
{
    idle_register.add(
        [this, ctemp]() -> bool
        {
            disableListener();
            setEnabled(true);
            temp->setDefault(ctemp.getTemp());
            green->setDefault(ctemp.getGreen());
            double m[3];
            ctemp.getMultipliers(m[0], m[1], m[2]);
            if (wbp) {
                wbp->convertWBMul2Cam(m[0], m[1], m[2]);
            }
            for (int i = 0; i < 3; ++i) {
                mult[i]->setValue(m[i] / m[1]);
            }
            syncSliders(true);
            updateMethodGui(true);
            enableListener();
            // green->setLogScale(100, greenVal, true);

            return false;
        }
    );
}


void WhiteBalance::syncSliders(bool from_mult)
{
    if (!wbp) {
        return;
    }

    temp_warning_->hide();
    
    disableListener();
    if (from_mult) {
        double m[3] = { mult[0]->getValue(), mult[1]->getValue(), mult[2]->getValue() };
        wbp->convertWBCam2Mul(m[0], m[1], m[2]);
        rtengine::ColorTemp ct(m[0], m[1], m[2]);
        temp->setValue(ct.getTemp());
        green->setValue(ct.getGreen());
        equal->setValue(1.0);

        rtengine::ColorTemp ct2(ct.getTemp(), ct.getGreen(), 1.0, "");
        double m2[3];
        ct2.getMultipliers(m2[0], m2[1], m2[2]);
        if (rtengine::max(std::abs(m[0]-m2[0]), std::abs(m[1]-m2[1]), std::abs(m[2]-m2[2])) > 1e-2) {
            temp_warning_->show();
        }
    } else {
        rtengine::ColorTemp ct(temp->getValue(), green->getValue(), equal->getValue(), "");
        double m[3];
        ct.getMultipliers(m[0], m[1], m[2]);
        wbp->convertWBMul2Cam(m[0], m[1], m[2]);
        for (int i = 0; i < 3; ++i) {
            mult[i]->setValue(m[i]);
        }
    }
    enableListener();
}


void WhiteBalance::updateMethodGui(bool check_temp)
{
//    temp_warning_->hide();
    switch (getActiveMethod()) {
    case int(WBParams::CAMERA):
    case int(WBParams::AUTO):
    case int(WBParams::CUSTOM_TEMP):
        multBox->hide();
        tempBox->show();
        if (check_temp && wbp) {
            rtengine::ColorTemp ct1(temp->getValue(), green->getValue(), equal->getValue(), "");
            double m1[3];
            ct1.getMultipliers(m1[0], m1[1], m1[2]);
            double m2[3] = { mult[0]->getValue(), mult[1]->getValue(), mult[2]->getValue() };
            wbp->convertWBCam2Mul(m2[0], m2[1], m2[2]);
            if (rtengine::max(std::abs(m1[0]-m2[0]), std::abs(m1[1]-m2[1]), std::abs(m1[2]-m2[2])) > 1e-2) {
                multBox->show();
                tempBox->hide();
                ConnectionBlocker methblocker(methconn);
                if (getActiveMethod() == int(WBParams::CUSTOM_TEMP)) {
                    method->set_active(int(WBParams::CUSTOM_MULT));
                }
            }
        }
        break;
    default:
        tempBox->hide();
        multBox->show();
        break;
    }
}


void WhiteBalance::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.wb = initial_params;
    }
    pp.wb.enabled = getEnabled();
    read(&pp);
}


inline int WhiteBalance::getActiveMethod()
{
    return std::min(method->get_active_row_number(), int(WBParams::CUSTOM_MULT));
}


void WhiteBalance::fillMethods()
{
    refTreeModel->clear();
    presets.clear();

    Gtk::TreeModel::Row row, childrow;

    for (size_t i = 0; i < wbPixbufs.size(); ++i) {
        row = *(refTreeModel->append());
        row[methodColumns.colIcon] = wbPixbufs[i];
        row[methodColumns.colLabel] = M(labels[i]);
        row[methodColumns.colPreset] = -1;
    }

    if (wbp) {
        presets = wbp->getWBPresets();
        if (!presets.empty()) {
            row = *(refTreeModel->append());
            row[methodColumns.colIcon] = wbPixbufs[0];
            row[methodColumns.colLabel] = M("TP_WBALANCE_PRESET");
            row[methodColumns.colPreset] = -1;
        }
        int i = 0;
        for (auto &p : presets) {
            childrow = *(refTreeModel->append(row.children()));
            childrow[methodColumns.colLabel] = p.label;
            childrow[methodColumns.colPreset] = i;
            ++i;
        }
    }
}


void WhiteBalance::registerShortcuts(ToolShortcutManager *mgr)
{
    mgr->addShortcut(GDK_KEY_t, this, temp);
    mgr->addShortcut(GDK_KEY_i, this, green);
}
