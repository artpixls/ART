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
 *  GNU General Public License for more details
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "blackwhite.h"
#include "rtimage.h"
#include "../rtengine/color.h"
#include <iomanip>
#include <cmath>
#include "guiutils.h"
#include "edit.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;

namespace {

constexpr int REL_RGB = 11;

const std::vector<std::array<const char *, 2>> presets = {
    {"NormalContrast", "TP_BWMIX_SET_NORMCONTAST"},
    {"HighContrast", "TP_BWMIX_SET_HIGHCONTAST"},
    {"Luminance", "TP_BWMIX_SET_LUMINANCE"},
    {"Landscape", "TP_BWMIX_SET_LANDSCAPE"},
    {"Portrait", "TP_BWMIX_SET_PORTRAIT"},
    {"LowSensitivity", "TP_BWMIX_SET_LOWSENSIT"},
    {"HighSensitivity", "TP_BWMIX_SET_HIGHSENSIT"},
    {"Panchromatic", "TP_BWMIX_SET_PANCHRO"},
    {"HyperPanchromatic", "TP_BWMIX_SET_HYPERPANCHRO"},
    {"Orthochromatic", "TP_BWMIX_SET_ORTHOCHRO"},
    {"RGB-Abs", "TP_BWMIX_SET_RGBABS"},
    {"RGB-Rel", "TP_BWMIX_SET_RGBREL"},
    {"InfraRed", "TP_BWMIX_SET_INFRARED"}
};

const std::vector<std::array<const char *, 2>> filters = {
    {"None", "TP_BWMIX_FILTER_NONE"},
    {"Red", "TP_BWMIX_FILTER_RED"},
    {"Orange", "TP_BWMIX_FILTER_REDYELLOW"},
    {"Yellow", "TP_BWMIX_FILTER_YELLOW"},
    {"YellowGreen", "TP_BWMIX_FILTER_GREENYELLOW"},
    {"Green", "TP_BWMIX_FILTER_GREEN"},
    {"Cyan", "TP_BWMIX_FILTER_BLUEGREEN"},
    {"Blue", "TP_BWMIX_FILTER_BLUE"},
    {"Purple", "TP_BWMIX_FILTER_PURPLE"}
};

} // namespace

namespace rtengine {

extern void computeBWMixerConstants(const Glib::ustring &setting, const Glib::ustring &filter, const Glib::ustring &algo,
                                    float &filcor, float &mixerRed, float &mixerGreen,
                                    float &mixerBlue, 
                                    float &kcorec, double &rrm, double &ggm, double &bbm);

} // namespace rtengine


BlackWhite::BlackWhite():
    FoldableToolPanel(this, "blackwhite", M("TP_BWMIX_LABEL"), false, true, true)
{
    auto m = ProcEventMapper::getInstance();
    EvColorCast = m->newEvent(M_LUMINANCE, "HISTORY_MSG_BWMIX_COLORCAST");
    EvToolReset.set_action(rtengine::M_LUMINANCE);
    
    nextredbw = 0.3333;
    nextgreenbw = 0.3333;
    nextbluebw = 0.3333;

    mixerVBox = Gtk::manage (new Gtk::VBox ());
    mixerVBox->set_spacing(4);

    settingHBox = Gtk::manage (new Gtk::HBox ());
    settingHBox->set_spacing (2);
    settingHBox->set_tooltip_markup (M("TP_BWMIX_SETTING_TOOLTIP"));
    Gtk::Label *settingLabel = Gtk::manage (new Gtk::Label (M("TP_BWMIX_SETTING") + ":"));

    settingHBox->pack_start (*settingLabel, Gtk::PACK_SHRINK);
    setting = Gtk::manage (new MyComboBoxText ());
    for (auto &p : presets) {
        setting->append(M(p[1]));
    }
    setting->set_active (REL_RGB);
    settingHBox->pack_start (*setting);

    mixerVBox->pack_start (*settingHBox);
    settingconn = setting->signal_changed().connect ( sigc::mem_fun(*this, &BlackWhite::settingChanged) );

    RGBLabels = Gtk::manage(new Gtk::Label("---", Gtk::ALIGN_CENTER));
    RGBLabels->set_tooltip_text(M("TP_BWMIX_RGBLABEL_HINT"));
    mixerVBox->pack_start (*RGBLabels);

    //----------- Complementary Color checkbox ------------------------------

    filterSep = Gtk::manage (new  Gtk::HSeparator());
    mixerVBox->pack_start (*filterSep);

    filterHBox = Gtk::manage (new Gtk::HBox ());
    filterHBox->set_spacing (2);
    filterHBox->set_tooltip_markup (M("TP_BWMIX_FILTER_TOOLTIP"));
    Gtk::Label *filterLabel = Gtk::manage (new Gtk::Label (M("TP_BWMIX_FILTER") + ":"));
    filterHBox->pack_start (*filterLabel, Gtk::PACK_SHRINK);
    filter = Gtk::manage (new MyComboBoxText ());
    for (auto &p : filters) {
        filter->append(M(p[1]));
    }

    filter->set_active (0);
    filterHBox->pack_start (*filter);
    mixerVBox->pack_start (*filterHBox);
    filterconn = filter->signal_changed().connect ( sigc::mem_fun(*this, &BlackWhite::filterChanged) );

    //----------- RGB / ROYGCBPM Mixer ------------------------------

    imgIcon[0] = Gtk::manage (new RTImage ("circle-red-small.png"));
    imgIcon[1] = Gtk::manage (new RTImage ("circle-orange-small.png"));
    imgIcon[2] = Gtk::manage (new RTImage ("circle-yellow-small.png"));
    imgIcon[3] = Gtk::manage (new RTImage ("circle-green-small.png"));
    imgIcon[4] = Gtk::manage (new RTImage ("circle-cyan-small.png"));
    imgIcon[5] = Gtk::manage (new RTImage ("circle-blue-small.png"));
    imgIcon[6] = Gtk::manage (new RTImage ("circle-purple-small.png"));
    imgIcon[7] = Gtk::manage (new RTImage ("circle-magenta-small.png"));

    imgIcon[8]  = Gtk::manage (new RTImage ("circle-empty-red-small.png"));
    imgIcon[9]  = Gtk::manage (new RTImage ("circle-empty-green-small.png"));
    imgIcon[10] = Gtk::manage (new RTImage ("circle-empty-blue-small.png"));

    mixerVBox->pack_start (*Gtk::manage (new  Gtk::HSeparator()));

    mixerRed = Gtk::manage(new Adjuster (/*M("TP_BWMIX_RED")*/"", -100, 200, 1, 33, imgIcon[0]));

    mixerRed->setAdjusterListener (this);
    mixerRed->set_tooltip_markup (M("TP_BWMIX_RGB_TOOLTIP"));
    mixerRed->show();
    mixerVBox->pack_start( *mixerRed, Gtk::PACK_SHRINK, 0);

    mixerGreen = Gtk::manage(new Adjuster (/*M("TP_BWMIX_GREEN")*/"", -100, 200, 1, 33, imgIcon[3]));

    mixerGreen->setAdjusterListener (this);
    mixerGreen->set_tooltip_markup (M("TP_BWMIX_RGB_TOOLTIP"));
    mixerGreen->show();
    mixerVBox->pack_start( *mixerGreen, Gtk::PACK_SHRINK, 0);

    mixerBlue = Gtk::manage(new Adjuster (/*M("TP_BWMIX_BLUE")*/"", -100, 200, 1, 33, imgIcon[5]));

    mixerBlue->setAdjusterListener (this);
    mixerBlue->set_tooltip_markup (M("TP_BWMIX_RGB_TOOLTIP"));
    mixerBlue->show();
    mixerVBox->pack_start( *mixerBlue, Gtk::PACK_SHRINK, 0);

    pack_start(*mixerVBox, Gtk::PACK_SHRINK, 0);

    //----------- Gamma sliders ------------------------------

    gammaFrame = Gtk::manage (new Gtk::Frame (M("TP_BWMIX_GAMMA")));
    pack_start (*gammaFrame, Gtk::PACK_SHRINK, 0);

    Gtk::VBox *gammaVBox = Gtk::manage (new Gtk::VBox());
    gammaVBox->set_spacing(4);


    gammaRed = Gtk::manage(new Adjuster (/*M("TP_BWMIX_GAM_RED")*/"", -100, 100, 1, 0, imgIcon[8]));

    gammaRed->setAdjusterListener (this);
    gammaRed->set_tooltip_markup (M("TP_BWMIX_GAM_TOOLTIP"));
    gammaRed->show();
    gammaVBox->pack_start( *gammaRed, Gtk::PACK_SHRINK, 0);

    gammaGreen = Gtk::manage(new Adjuster (/*M("TP_BWMIX_GAM_GREEN")*/"", -100, 100, 1, 0, imgIcon[9]));

    gammaGreen->setAdjusterListener (this);
    gammaGreen->set_tooltip_markup (M("TP_BWMIX_GAM_TOOLTIP"));
    gammaGreen->show();
    gammaVBox->pack_start( *gammaGreen, Gtk::PACK_SHRINK, 0);

    gammaBlue = Gtk::manage(new Adjuster (/*M("TP_BWMIX_GAM_BLUE")*/"", -100, 100, 1, 0, imgIcon[10]));

    gammaBlue->setAdjusterListener (this);
    gammaBlue->set_tooltip_markup (M("TP_BWMIX_GAM_TOOLTIP"));
    gammaBlue->show();
    gammaVBox->pack_start( *gammaBlue, Gtk::PACK_SHRINK, 0);

    gammaFrame->add(*gammaVBox);

    colorCast = Gtk::manage(new ThresholdAdjuster(M("TP_BWMIX_COLORCAST"), 0., 100., 0., M("TP_BWMIX_SATURATION"), 1., 0., 360., 0., M("TP_BWMIX_HUE"), 1., nullptr, false));
    colorCast->setAdjusterListener(this);
    colorCast->setBgColorProvider(this, 1);
    colorCast->setUpdatePolicy(RTUP_DYNAMIC);

    pack_start(*colorCast, Gtk::PACK_SHRINK, 0);

    show_all();
}


BlackWhite::~BlackWhite ()
{
    idle_register.destroy();
}


void BlackWhite::read(const ProcParams* pp)
{

    disableListener ();
    filterconn.block(true);
    settingconn.block(true);
    enaccconn.block (true);

    for (size_t i = 0; i < presets.size(); ++i) {
        if (pp->blackwhite.setting == presets[i][0]) {
            setting->set_active(i);
            break;
        }
    }

    settingChanged();

    for (size_t i = 0; i < filters.size(); ++i) {
        if (pp->blackwhite.filter == filters[i][0]) {
            filter->set_active(i);
            break;
        }
    }

    filterChanged();

    setEnabled (pp->blackwhite.enabled);

    mixerRed->setValue (pp->blackwhite.mixerRed);
    mixerGreen->setValue (pp->blackwhite.mixerGreen);
    mixerBlue->setValue (pp->blackwhite.mixerBlue);
    gammaRed->setValue (pp->blackwhite.gammaRed);
    gammaGreen->setValue (pp->blackwhite.gammaGreen);
    gammaBlue->setValue (pp->blackwhite.gammaBlue);
    colorCast->setValue<int>(pp->blackwhite.colorCast);

    filterconn.block(false);
    settingconn.block(false);
    enaccconn.block (false);

    updateRGBLabel();

    enableListener ();
}

void BlackWhite::write (ProcParams* pp)
{
    pp->blackwhite.enabled = getEnabled();
    pp->blackwhite.mixerRed = mixerRed->getValue ();
    pp->blackwhite.mixerGreen = mixerGreen->getValue ();
    pp->blackwhite.mixerBlue = mixerBlue->getValue ();
    pp->blackwhite.gammaRed = gammaRed->getValue ();
    pp->blackwhite.gammaGreen = gammaGreen->getValue ();
    pp->blackwhite.gammaBlue = gammaBlue->getValue ();
    pp->blackwhite.setting = getSettingString();
    pp->blackwhite.filter = getFilterString();
    pp->blackwhite.colorCast = colorCast->getValue<int>();
}


void BlackWhite::settingChanged ()
{

    if ( setting->get_active_row_number() == 12 ) {
        // Infrared
        filter->set_active (0);
        hideFilter();
    } else {
        // RGB Presets
        showFilter();
    }

    updateRGBLabel();

    if (listener && getEnabled()) {
        listener->panelChanged (EvBWsetting, setting->get_active_text ());
    }
}


void BlackWhite::filterChanged ()
{
    // Checking "listener" to avoid "autoch" getting toggled off because it has to change the sliders when toggling on

    updateRGBLabel();

    if (listener && getEnabled()) {
        listener->panelChanged (EvBWfilter, filter->get_active_text ());
    }
}


void BlackWhite::enabledChanged ()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged (EvBWChmixEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged (EvBWChmixEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged (EvBWChmixEnabled, M("GENERAL_DISABLED"));
        }
    }
}


void BlackWhite::setDefaults (const ProcParams* defParams)
{
    mixerRed->setDefault (defParams->blackwhite.mixerRed);
    mixerGreen->setDefault (defParams->blackwhite.mixerGreen);
    mixerBlue->setDefault (defParams->blackwhite.mixerBlue);
    gammaRed->setDefault (defParams->blackwhite.gammaRed);
    gammaGreen->setDefault (defParams->blackwhite.gammaGreen);
    gammaBlue->setDefault (defParams->blackwhite.gammaBlue);
    colorCast->setDefault(defParams->blackwhite.colorCast);

    initial_params = defParams->blackwhite;
}


void BlackWhite::adjusterChanged(Adjuster* a, double newval)
{
    if (a == mixerRed || a == mixerGreen || a == mixerBlue) {
        updateRGBLabel(false);
    }

    if (listener  && getEnabled()) {
        Glib::ustring value = a->getTextValue();

        if (a == mixerRed) {
            listener->panelChanged (EvBWred, value );
        } else if (a == mixerGreen) {
            listener->panelChanged (EvBWgreen, value );
        } else if (a == mixerBlue) {
            listener->panelChanged (EvBWblue, value );
        } else if (a == gammaGreen) {
            listener->panelChanged (EvBWgreengam, value );
        } else if (a == gammaBlue) {
            listener->panelChanged (EvBWbluegam, value );
        } else if (a == gammaRed) {
            listener->panelChanged (EvBWredgam, value );
        }
    }
}


void BlackWhite::adjusterChanged(ThresholdAdjuster *a, double newBottom, double newTop)
{
    if (listener && getEnabled() && a == colorCast) {
        listener->panelChanged(
            EvColorCast,
            Glib::ustring::compose(Glib::ustring(M("TP_BWMIX_HUE") + ": %1" + "\n" + M("TP_BWMIX_SATURATION") + ": %2"), int(newTop), int(newBottom)));
    }
}


void BlackWhite::adjusterAutoToggled(Adjuster* a, bool newval)
{
}

void BlackWhite::updateRGBLabel(bool from_preset)
{
    float kcorrec = 1.f;
    float r, g, b;

    r = mixerRed->getValue();
    g = mixerGreen->getValue();
    b = mixerBlue->getValue();

    double mixR, mixG, mixB;
    float filcor;
    Glib::ustring sSetting = getSettingString();
    rtengine::computeBWMixerConstants(sSetting, getFilterString(), "", filcor, r, g, b, kcorrec, mixR, mixG, mixB);

    if( filcor != 1.f) {
        r = kcorrec * r / (r + g + b);
        g = kcorrec * g / (r + g + b);
        b = kcorrec * b / (r + g + b);
    }

    RGBLabels->set_text(
        Glib::ustring::compose(M("TP_BWMIX_RGBLABEL"),
                               Glib::ustring::format(std::fixed, std::setprecision(1), r * 100.),
                               Glib::ustring::format(std::fixed, std::setprecision(1), g * 100.),
                               Glib::ustring::format(std::fixed, std::setprecision(1), b * 100.),
                               Glib::ustring::format(std::fixed, std::setprecision(0), ceil(kcorrec * 100./*(r+g+b)*100.)*/)))
        );

    // We have to update the RGB sliders too if preset values has been chosen
    if (from_preset) {
        if (sSetting != "RGB-Abs" && sSetting != "RGB-Rel") {
            mixerRed->setValue(mixR);
            mixerGreen->setValue(mixG);
            mixerBlue->setValue(mixB);
        }
    } else {
        if (sSetting != "RGB-Abs" && sSetting != "RGB-Rel") {
            disableListener();
            setting->set_active(REL_RGB);
            enableListener();
        }
    }
}


void BlackWhite::trimValues (rtengine::procparams::ProcParams* pp)
{

    mixerRed->trimValue (pp->blackwhite.mixerRed);
    mixerGreen->trimValue (pp->blackwhite.mixerGreen);
    mixerBlue->trimValue (pp->blackwhite.mixerBlue);
    gammaRed->trimValue (pp->blackwhite.gammaRed);
    gammaGreen->trimValue (pp->blackwhite.gammaGreen);
    gammaBlue->trimValue (pp->blackwhite.gammaBlue);
}

void BlackWhite::showFilter()
{
    filterHBox->show();
    filterSep->show();
}

void BlackWhite::hideFilter()
{
    filterHBox->hide();
    filterSep->hide();
}


Glib::ustring BlackWhite::getSettingString()
{
    Glib::ustring retVal;
    int r = setting->get_active_row_number();
    if (r >= 0 && size_t(r) < presets.size()) {
        return presets[r][0];
    }
    return "RGB-Rel";
}

Glib::ustring BlackWhite::getFilterString()
{
    int r = filter->get_active_row_number();
    if (r >= 0 && size_t(r) < filters.size()) {
        return filters[r][0];
    }
    return "None";
}


void BlackWhite::colorForValue(double valX, double valY, enum ColorCaller::ElemType elemType, int callerId, ColorCaller *caller)
{

    float R = 0.f, G = 0.f, B = 0.f;

    const auto to_hue =
        [](float x) -> float
        {
            x -= 0.05f;
            if (x < 0.f) {
                x += 1.f;
            }
            return x;
        };

    if (callerId == 1) {  // Slider 1 background
        if (valY <= 0.5) {
            // the hue range
            Color::hsv2rgb01(to_hue(valX), 1.0f, 0.65f, R, G, B);
        } else {
            // the strength applied to the current hue
            double strength, hue;
            colorCast->getValue(strength, hue);
            Color::hsv2rgb01(to_hue(hue / 360.f), 1.f, 1.f, R, G, B);
            const double gray = 0.46;
            R = (gray * (1.0 - valX)) + R * valX;
            G = (gray * (1.0 - valX)) + G * valX;
            B = (gray * (1.0 - valX)) + B * valX;
        }
    }

    caller->ccRed = double(R);
    caller->ccGreen = double(G);
    caller->ccBlue = double(B);
}


void BlackWhite::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.blackwhite = initial_params;
    }
    pp.blackwhite.enabled = getEnabled();
    read(&pp);
}
