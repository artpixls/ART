/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2019 Alberto Romei <aldrop8@gmail.com>
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
 *  along with RawTherapee.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <iomanip>

#include "filmnegative.h"

#include "eventmapper.h"
#include "options.h"
#include "rtimage.h"

#include "../rtengine/procparams.h"
#include "../rtengine/color.h"

namespace {

double toAdjuster(double v)
{
    return CLAMP(std::log2(v), 6, 16) - 6;
}

double fromAdjuster(double v)
{
    return std::pow(2, v + 6);
}

Adjuster* createExponentAdjuster(AdjusterListener* listener, const Glib::ustring& label, double minV, double maxV, double step, double defaultVal)
{
    Adjuster* const adj = Gtk::manage(new Adjuster(label, minV, maxV, step, defaultVal));
    adj->setAdjusterListener(listener);
    adj->setLogScale(6, 1, true);

    adj->delay = std::max(options.adjusterMinDelay, options.adjusterMaxDelay);

    adj->show();
    return adj;
}

Adjuster* createLevelAdjuster(AdjusterListener* listener, const Glib::ustring& label)
{
//    Adjuster* const adj = Gtk::manage(new Adjuster(label, 1.0, 65535.0, 1.0, rtengine::MAXVALF / 24.));
    Adjuster* const adj = Gtk::manage(new Adjuster(label, 0.0, 10.0, 0.01, toAdjuster(rtengine::MAXVALF / 24.)));
    adj->setAdjusterListener(listener);
//    adj->setLogScale(6, 1000.0, true);

    adj->delay = std::max(options.adjusterMinDelay, options.adjusterMaxDelay);

    adj->show();
    return adj;
}

Adjuster* createBalanceAdjuster(AdjusterListener* listener, const Glib::ustring& label, double minV, double maxV, double defaultVal,
                                const Glib::ustring& leftIcon, const Glib::ustring& rightIcon)
{
    Adjuster* const adj = Gtk::manage(new Adjuster(label, minV, maxV, 0.01, defaultVal,
                                      Gtk::manage(new RTImage(leftIcon)), Gtk::manage(new RTImage(rightIcon))));
    adj->setAdjusterListener(listener);
    adj->setLogScale(9, 0, true);

    adj->delay = std::max(options.adjusterMinDelay, options.adjusterMaxDelay);

    adj->show();
    return adj;
}


Glib::ustring fmt(const RGB& rgb)
{
    if (rgb.r <= 0.f && rgb.g <= 0.f && rgb.b <= 0.f) {
        return "- - -";
    } else {
        return Glib::ustring::format(std::fixed, std::setprecision(1), rgb.r) + " " +
               Glib::ustring::format(std::fixed, std::setprecision(1), rgb.g) + " " +
               Glib::ustring::format(std::fixed, std::setprecision(1), rgb.b);
    }
}


RGB getFilmNegativeExponents(const RGB &ref1, const RGB &ref2) // , const RGB &clearValsOut, const RGB &denseValsOut)
{
//    using rtengine::settings;

    RGB clearVals = ref1;
    RGB denseVals = ref2;

    // Detect which one is the dense spot, based on green channel
    if (clearVals.g < denseVals.g) {
        std::swap(clearVals, denseVals);
        //std::swap(clearValsOut, denseValsOut);
    }

    // if (settings->verbose) {
    //     printf("Clear input values: R=%g G=%g B=%g\n", static_cast<double>(clearVals.r), static_cast<double>(clearVals.g), static_cast<double>(clearVals.b));
    //     printf("Dense input values: R=%g G=%g B=%g\n", static_cast<double>(denseVals.r), static_cast<double>(denseVals.g), static_cast<double>(denseVals.b));

    //     // printf("Clear output values: R=%g G=%g B=%g\n", static_cast<double>(clearValsOut.r), static_cast<double>(clearValsOut.g), static_cast<double>(clearValsOut.b));
    //     // printf("Dense output values: R=%g G=%g B=%g\n", static_cast<double>(denseValsOut.r), static_cast<double>(denseValsOut.g), static_cast<double>(denseValsOut.b));
    // }

    const float denseGreenRatio = clearVals.g / denseVals.g;

    // Calculate logarithms in arbitrary base
    const auto logBase =
        [](float base, float num) -> float
        {
            return std::log(num) / std::log(base);
        };

    // const auto ratio =
    //     [](float a, float b) -> float
    //     {
    //         return a > b ? a / b : b / a;
    //     };

    RGB newExps;
    newExps.r = logBase(clearVals.r / denseVals.r, denseGreenRatio);
    newExps.g = 1.f; // logBase(ratio(clearVals.g, denseVals.g), ratio(denseValsOut.g, clearValsOut.g) );
    newExps.b = logBase(clearVals.b / denseVals.b, denseGreenRatio);



    // if (settings->verbose > 1) {
    //     printf("Film Negative - New exponents:  R=%g G=%g B=%g\n", static_cast<double>(newExps.r), static_cast<double>(newExps.g), static_cast<double>(newExps.b));
    // }

    // // Re-adjust color balance based on dense spot values and new exponents
    // calcBalance(rtengine::max(static_cast<float>(params->filmNegative.refInput.g), 1.f),
    //     -newExps[0], -newExps[1], -newExps[2],
    //     denseVals[0], denseVals[1], denseVals[2],
    //     rBal, bBal);

    return newExps;

}

void temp2rgb(double outLev, double temp, double green, RGB &refOut)
{
    rtengine::ColorTemp ct = rtengine::ColorTemp(temp, green, 1., "Custom");

    double rm, gm, bm;
    ct.getMultipliers(rm, gm, bm);

    double maxGain = rtengine::max(rm, gm, bm);

    refOut.r = (rm / maxGain) * outLev;
    refOut.g = (gm / maxGain) * outLev;
    refOut.b = (bm / maxGain) * outLev;
}


void rgb2temp(const RGB &refOut, double &outLev, double &temp, double &green)
{
    double maxVal = rtengine::max(refOut.r, refOut.g, refOut.b);

    rtengine::ColorTemp ct = rtengine::ColorTemp(
                                 refOut.r / maxVal,
                                 refOut.g / maxVal,
                                 refOut.b / maxVal,
                                 1.);

    outLev = maxVal;
    temp = ct.getTemp();
    green = ct.getGreen();
}


Gtk::Widget *pack_spot_picker(Gtk::Widget *spotbutton, MyComboBoxText *&spotsize)
{
    Gtk::Grid* spotgrid = Gtk::manage(new Gtk::Grid());
    spotgrid->get_style_context()->add_class("grid-spacing");
    setExpandAlignProperties(spotgrid, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);

    Gtk::Label *slab = Gtk::manage(new Gtk::Label(M("TP_WBALANCE_SIZE")));
    setExpandAlignProperties(slab, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);

    Gtk::Grid *wbsizehelper = Gtk::manage(new Gtk::Grid());
    wbsizehelper->set_name("WB-Size-Helper");
    setExpandAlignProperties(wbsizehelper, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);

    spotsize = Gtk::manage(new MyComboBoxText());
    setExpandAlignProperties(spotsize, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);
    spotsize->append("2");
    spotsize->append("4");
    spotsize->append("8");
    spotsize->append("16");
    spotsize->append("32");
    spotsize->set_active(4);

    wbsizehelper->attach(*spotsize, 0, 0, 1, 1);
    
    spotgrid->attach(*spotbutton, 0, 0, 1, 1);
    spotgrid->attach(*slab, 1, 0, 1, 1);
    spotgrid->attach(*wbsizehelper, 2, 0, 1, 1);

    return spotgrid;
}

} // namespace


FilmNegative::FilmNegative() :
    FoldableToolPanel(this, "filmnegative", M("TP_FILMNEGATIVE_LABEL"), false, true, true),
    EditSubscriber(ET_OBJECTS),
    NEUTRAL_TEMP(rtengine::ColorTemp(1., 1., 1., 1.)),
    evFilmNegativeExponents(ProcEventMapper::getInstance()->newEvent(ALLNORAW, "HISTORY_MSG_FILMNEGATIVE_VALUES")),
    evFilmNegativeEnabled(ProcEventMapper::getInstance()->newEvent(ALLNORAW, "HISTORY_MSG_FILMNEGATIVE_ENABLED")),
    evFilmNegativeRefSpot(ProcEventMapper::getInstance()->newEvent(ALLNORAW, "HISTORY_MSG_FILMNEGATIVE_REF_SPOT")),
    evFilmNegativeBalance(ProcEventMapper::getInstance()->newEvent(ALLNORAW, "HISTORY_MSG_FILMNEGATIVE_BALANCE")),
    evFilmNegativeColorSpace(ProcEventMapper::getInstance()->newEvent(ALLNORAW, "HISTORY_MSG_FILMNEGATIVE_COLORSPACE")),
    refInputValues({0.f, 0.f, 0.f}),
    paramsUpgraded(false),
    refLuminance({{0.f, 0.f, 0.f}, 0.f}),
    fnp(nullptr),
    colorSpace(Gtk::manage(new MyComboBoxText())),
    greenExp(createExponentAdjuster(this, M("TP_FILMNEGATIVE_GREEN"), 0.3, 4, 0.01, 1.5)),  // master exponent (green channel)
    redRatio(createExponentAdjuster(this, M("TP_FILMNEGATIVE_RED"), 0.3, 5, 0.01, (2.04 / 1.5))), // ratio of red exponent to master exponent
    blueRatio(createExponentAdjuster(this, M("TP_FILMNEGATIVE_BLUE"), 0.3, 5, 0.01, (1.29 / 1.5))), // ratio of blue exponent to master exponent
    spotButton(Gtk::manage(new Gtk::ToggleButton(M("TP_FILMNEGATIVE_PICK")))),
    refInputLabel(Gtk::manage(new Gtk::Label(Glib::ustring::compose(M("TP_FILMNEGATIVE_REF_LABEL"), "- - -")))),
    refSpotButton(Gtk::manage(new Gtk::ToggleButton(M("TP_FILMNEGATIVE_REF_PICK")))),
    outputLevel(createLevelAdjuster(this, M("TP_FILMNEGATIVE_OUT_LEVEL"))),  // ref level
    greenBalance(createBalanceAdjuster(this, M("TP_FILMNEGATIVE_GREENBALANCE"), -3.0, 3.0, 0.0, "circle-magenta-small.png", "circle-green-small.png")),  // green balance
    blueBalance(createBalanceAdjuster(this, M("TP_FILMNEGATIVE_BLUEBALANCE"), -3.0, 3.0, 0.0, "circle-blue-small.png", "circle-yellow-small.png"))  // blue balance
{
    EvToolReset.set_action(ALLNORAW);
        
    setExpandAlignProperties(spotButton, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);
    spotButton->get_style_context()->add_class("independent");
    spotButton->set_tooltip_text(M("TP_FILMNEGATIVE_GUESS_TOOLTIP"));
    spotButton->set_image(*Gtk::manage(new RTImage("color-picker-small.png")));

    refSpotButton->set_tooltip_text(M("TP_FILMNEGATIVE_REF_TOOLTIP"));
    setExpandAlignProperties(refSpotButton, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);

    setExpandAlignProperties(refInputLabel, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);

    colorSpace->append(M("TP_FILMNEGATIVE_COLORSPACE_INPUT"));
    colorSpace->append(M("TP_FILMNEGATIVE_COLORSPACE_WORKING"));
    setExpandAlignProperties(colorSpace, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);
    colorSpace->set_tooltip_markup(M("TP_FILMNEGATIVE_COLORSPACE_TOOLTIP"));

    csGrid = Gtk::manage(new Gtk::Grid());
    Gtk::Label* csLabel = Gtk::manage(new Gtk::Label(M("TP_FILMNEGATIVE_COLORSPACE")));
    csGrid->attach(*csLabel, 0, 0, 1, 1);
    csGrid->attach(*colorSpace, 1, 0, 1, 1);
    Gtk::Image *w = Gtk::manage(new RTImage("warning-small.png"));
    w->set_tooltip_markup(M("GENERAL_DEPRECATED_TOOLTIP"));
    csGrid->attach(*w, 2, 0, 1, 1);

    pack_start(*csGrid);

    colorSpace->set_active((int)ColorSpace::WORKING);
    colorSpace->signal_changed().connect(sigc::mem_fun(*this, &FilmNegative::colorSpaceChanged));
    colorSpace->show();

    pack_start(*greenExp, Gtk::PACK_SHRINK, 0);
    pack_start(*redRatio, Gtk::PACK_SHRINK, 0);
    pack_start(*blueRatio, Gtk::PACK_SHRINK, 0);
    //pack_start(*spotButton, Gtk::PACK_SHRINK, 0);
    pack_start(*pack_spot_picker(spotButton, spotsize), Gtk::PACK_SHRINK, 0);

    Gtk::Separator* const sep = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL));
    sep->get_style_context()->add_class("grid-row-separator");
    pack_start(*sep, Gtk::PACK_SHRINK, 0);

    pack_start(*refInputLabel, Gtk::PACK_SHRINK, 0);

    pack_start(*outputLevel, Gtk::PACK_SHRINK, 0);
    pack_start(*blueBalance, Gtk::PACK_SHRINK, 0);
    pack_start(*greenBalance, Gtk::PACK_SHRINK, 0);

    //pack_start(*refSpotButton, Gtk::PACK_SHRINK, 0);
    pack_start(*pack_spot_picker(refSpotButton, refspotsize), Gtk::PACK_SHRINK, 0);    

    spotButton->signal_toggled().connect(sigc::mem_fun(*this, &FilmNegative::editToggled));

    refSpotButton->signal_toggled().connect(sigc::mem_fun(*this, &FilmNegative::refSpotToggled));

    // Editing geometry; create the spot rectangle
    Rectangle* const spotRect = new Rectangle();
    spotRect->filled = false;

    visibleGeometry.push_back(spotRect);

    // Stick a dummy rectangle over the whole image in mouseOverGeometry.
    // This is to make sure the getCursor() call is fired everywhere.
    Rectangle* const imgRect = new Rectangle();
    imgRect->filled = true;

    mouseOverGeometry.push_back(imgRect);
}

FilmNegative::~FilmNegative()
{
    idle_register.destroy();

    for (auto geometry : visibleGeometry) {
        delete geometry;
    }

    for (auto geometry : mouseOverGeometry) {
        delete geometry;
    }
}


void FilmNegative::readOutputSliders(RGB &refOut)
{
    temp2rgb(fromAdjuster(outputLevel->getValue()),
             NEUTRAL_TEMP.getTemp() / std::pow(2., blueBalance->getValue()),
             NEUTRAL_TEMP.getGreen() / std::pow(2., greenBalance->getValue()),
             refOut);
}

void FilmNegative::writeOutputSliders(const RGB &refOut)
{
    double outLev, cTemp, green;
    rgb2temp(refOut, outLev, cTemp, green);

    outputLevel->setValue(toAdjuster(outLev));
    blueBalance->setValue(std::log2(NEUTRAL_TEMP.getTemp() / cTemp));
    greenBalance->setValue(std::log2(NEUTRAL_TEMP.getGreen() / green));
}


void FilmNegative::read(const rtengine::procparams::ProcParams* pp)
{
    disableListener();

    setEnabled(pp->filmNegative.enabled);

    // Reset luminance reference each time params are read
    refLuminance.lum = 0.f;

    colorSpace->set_active(CLAMP((int)pp->filmNegative.colorSpace, 0, 1));
    redRatio->setValue(pp->filmNegative.redRatio);
    greenExp->setValue(pp->filmNegative.greenExp);
    blueRatio->setValue(pp->filmNegative.blueRatio);

    refInputValues = pp->filmNegative.refInput;

    // If reference input values are not set in params, estimated values will be passed in later
    // (after processing) via FilmNegListener
    refInputLabel->set_markup(
        Glib::ustring::compose(M("TP_FILMNEGATIVE_REF_LABEL"), fmt(refInputValues)));

    csGrid->set_visible(!(pp->filmNegative.backCompat == BackCompat::CURRENT && pp->filmNegative.colorSpace == rtengine::procparams::FilmNegativeParams::ColorSpace::WORKING));

    if (pp->filmNegative.backCompat == BackCompat::CURRENT) {
        outputLevel->show();
        blueBalance->show();
        greenBalance->show();
    } else {
        outputLevel->hide();
        blueBalance->hide();
        greenBalance->hide();
    }

    // If reference output values are not set in params, set the default output
    // chosen for median estimation: gray 1/24th of max
    if (pp->filmNegative.refOutput.r <= 0) {
        float gray = rtengine::MAXVALF / 24.f;
        writeOutputSliders({gray, gray, gray});
    } else {
        writeOutputSliders(pp->filmNegative.refOutput);
    }

    enableListener();
}

void FilmNegative::write(rtengine::procparams::ProcParams* pp)
{
    pp->filmNegative.colorSpace = rtengine::procparams::FilmNegativeParams::ColorSpace(colorSpace->get_active_row_number());

    pp->filmNegative.redRatio = redRatio->getValue();
    pp->filmNegative.greenExp = greenExp->getValue();
    pp->filmNegative.blueRatio = blueRatio->getValue();

    pp->filmNegative.enabled = getEnabled();

    pp->filmNegative.refInput = refInputValues;

    readOutputSliders(pp->filmNegative.refOutput);

    if (paramsUpgraded) {
        pp->filmNegative.backCompat = BackCompat::CURRENT;
    }

}

void FilmNegative::setDefaults(const rtengine::procparams::ProcParams* defParams)
{
    redRatio->setValue(defParams->filmNegative.redRatio);
    greenExp->setValue(defParams->filmNegative.greenExp);
    blueRatio->setValue(defParams->filmNegative.blueRatio);

    initial_params = defParams->filmNegative;
    
    float gray = rtengine::MAXVALF / 24.f;
    writeOutputSliders({gray, gray, gray});
}


void FilmNegative::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        if (a == redRatio || a == greenExp || a == blueRatio) {
            listener->panelChanged(
                evFilmNegativeExponents,
                Glib::ustring::compose(
                    "Ref=%1\nR=%2\nB=%3",
                    greenExp->getValue(),
                    redRatio->getValue(),
                    blueRatio->getValue()
                )
            );
        } else if (a == outputLevel || a == greenBalance || a == blueBalance) {

            // Reset luminance reference when output level/color sliders are changed
            refLuminance.lum = 0.f;

            listener->panelChanged(
                evFilmNegativeBalance,
                Glib::ustring::compose(
                    "Lev=%1 G=%2 B=%3",
                    outputLevel->getValue(),
                    greenBalance->getValue(),
                    blueBalance->getValue()
                )
            );
        }
    }
}

void FilmNegative::enabledChanged()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged(evFilmNegativeEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged(evFilmNegativeEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(evFilmNegativeEnabled, M("GENERAL_DISABLED"));
        }
    }
}

void FilmNegative::colorSpaceChanged()
{
    if (listener) {
        listener->panelChanged(evFilmNegativeColorSpace, colorSpace->get_active_text());
    }
}

void FilmNegative::filmRefValuesChanged(const RGB &refInput, const RGB &refOutput)
{

    idle_register.add(
        [this, refInput, refOutput]() -> bool {
            refInputValues = refInput;
            paramsUpgraded = true;
    
            disableListener();
    
            refInputLabel->set_markup(
                Glib::ustring::compose(M("TP_FILMNEGATIVE_REF_LABEL"), fmt(refInputValues)));
    
            writeOutputSliders(refOutput);
    
            outputLevel->show();
            blueBalance->show();
            greenBalance->show();
    
            enableListener();
            return false;
        }
    );

}

void FilmNegative::setFilmNegProvider(FilmNegProvider* provider)
{
    fnp = provider;
}

void FilmNegative::setEditProvider(EditDataProvider* provider)
{
    EditSubscriber::setEditProvider(provider);
}

CursorShape FilmNegative::getCursor(int objectID)
{
    return CSSpotWB;
}

bool FilmNegative::mouseOver(int modifierKey)
{
    EditDataProvider* const provider = getEditProvider();
    Rectangle* const spotRect = static_cast<Rectangle*>(visibleGeometry.at(0));
    spotRect->setXYWH(provider->posImage.x - 16, provider->posImage.y - 16, 32, 32);

    return true;
}

bool FilmNegative::button1Pressed(int modifierKey)
{
    EditDataProvider* const provider = getEditProvider();
    EditSubscriber::action = EditSubscriber::ES_ACTION_NONE;

    if (listener) {
        if (spotButton->get_active()) {
            int sz = 2 << spotsize->get_active_row_number();
            refSpotCoords.push_back(provider->posImage);
            if (refSpotCoords.size() == 2) {
                // User has selected 2 reference gray spots. Calculating new exponents
                // from channel values and updating parameters.

                RGB ref1, ref2, dummy;
                if (fnp->getFilmNegativeSpot(refSpotCoords[0], sz, ref1, dummy) &&
                        fnp->getFilmNegativeSpot(refSpotCoords[1], sz, ref2, dummy)) {
                    disableListener();

                    RGB newExps = getFilmNegativeExponents(ref1, ref2);
                    // Leaving green exponent unchanged, setting red and blue exponents based on
                    // the ratios between newly calculated exponents.
                    redRatio->setValue(newExps.r / newExps.g);
                    blueRatio->setValue(newExps.b / newExps.g);

                    enableListener();

                    if (getEnabled()) {
                        listener->panelChanged(
                            evFilmNegativeExponents,
                            Glib::ustring::compose(
                                "Ref=%1\nR=%2\nB=%3",
                                greenExp->getValue(),
                                redRatio->getValue(),
                                blueRatio->getValue()
                            )
                        );
                    }
                }
                switchOffEditMode();
            }
        } else if (refSpotButton->get_active()) {
            disableListener();

            int sz = 2 << refspotsize->get_active_row_number();
            
            // If the luminance reference is not set, copy the current reference input
            // values, and the corresponding output luminance
            if(refLuminance.lum <= 0.f) {
                RGB out;
                readOutputSliders(out);
                refLuminance.input = refInputValues;
                refLuminance.lum = rtengine::Color::rgbLuminance(out.r, out.g, out.b);
            }

            RGB refOut;
            fnp->getFilmNegativeSpot(provider->posImage, sz, refInputValues, refOut);
            // Output luminance of the sampled spot
            float spotLum = rtengine::Color::rgbLuminance(refOut.r, refOut.g, refOut.b);
            float rexp = -(greenExp->getValue() * redRatio->getValue());
            float gexp = -greenExp->getValue();
            float bexp = -(greenExp->getValue() * blueRatio->getValue());

            RGB mult = {
                spotLum / pow_F(rtengine::max(refInputValues.r, 1.f), rexp),
                spotLum / pow_F(rtengine::max(refInputValues.g, 1.f), gexp),
                spotLum / pow_F(rtengine::max(refInputValues.b, 1.f), bexp)
            };

            // Calculate the new luminance of the initial luminance reference spot, by
            // applying current multipliers
            float newRefLum = rtengine::Color::rgbLuminance(
                mult.r * pow_F(rtengine::max(refLuminance.input.r, 1.f), rexp),
                mult.g * pow_F(rtengine::max(refLuminance.input.g, 1.f), gexp),
                mult.b * pow_F(rtengine::max(refLuminance.input.b, 1.f), bexp));

            // Choose a suitable gray value for the sampled spot, so that luminance
            // of the initial reference spot is preserved.
            float gray = spotLum * (refLuminance.lum / newRefLum);

            writeOutputSliders({gray, gray, gray});

            refInputLabel->set_text(
                Glib::ustring::compose(M("TP_FILMNEGATIVE_REF_LABEL"), fmt(refInputValues)));

            enableListener();

            listener->panelChanged(
                evFilmNegativeRefSpot,
                Glib::ustring::compose(
                    "%1, %2, %3",
                    round(refInputValues.r), round(refInputValues.g), round(refInputValues.b)
                )
            );
            switchOffEditMode();
        } else {
            switchOffEditMode();
        }
    } else {
        switchOffEditMode();
    }

    return true;
}

bool FilmNegative::button1Released()
{
    EditSubscriber::action = EditSubscriber::ES_ACTION_NONE;
    return true;
}

bool FilmNegative::button3Pressed(int modifierKey)
{
    EditSubscriber::action = EditSubscriber::ES_ACTION_NONE;
    switchOffEditMode();
    return true;
}

void FilmNegative::switchOffEditMode()
{
    refSpotCoords.clear();
    unsubscribe();
    spotButton->set_active(false);
    refSpotButton->set_active(false);
}

void FilmNegative::editToggled()
{
    if (spotButton->get_active()) {

        refSpotButton->set_active(false);
        refSpotCoords.clear();

        subscribe();

        int w, h;
        getEditProvider()->getImageSize(w, h);

        // Stick a dummy rectangle over the whole image in mouseOverGeometry.
        // This is to make sure the getCursor() call is fired everywhere.
        Rectangle* const imgRect = static_cast<Rectangle*>(mouseOverGeometry.at(0));
        imgRect->setXYWH(0, 0, w, h);
    } else {
        refSpotCoords.clear();
        unsubscribe();
    }
}


void FilmNegative::refSpotToggled()
{
    if (refSpotButton->get_active()) {

        spotButton->set_active(false);
        refSpotCoords.clear();

        subscribe();

        int w, h;
        getEditProvider()->getImageSize(w, h);

        // Stick a dummy rectangle over the whole image in mouseOverGeometry.
        // This is to make sure the getCursor() call is fired everywhere.
        Rectangle* const imgRect = static_cast<Rectangle*>(mouseOverGeometry.at(0));
        imgRect->setXYWH(0, 0, w, h);

    } else {
        refSpotCoords.clear();
        unsubscribe();
    }
}


void FilmNegative::toolReset(bool to_initial)
{
    rtengine::procparams::ProcParams pp;
    if (to_initial) {
        pp.filmNegative = initial_params;
    }
    pp.filmNegative.enabled = getEnabled();
    read(&pp);
}
