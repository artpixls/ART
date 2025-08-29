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
#include "chmixer.h"
#include "rtimage.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;

ChMixer::ChMixer (): FoldableToolPanel(this, "chmixer", M("TP_CHMIXER_LABEL"), false, true, true)
{
    auto m = ProcEventMapper::getInstance();
    EvMode = m->newEvent(rtengine::RGBCURVE, "HISTORY_MSG_CHMIXER_MODE");
    EvRedPrimary = m->newEvent(rtengine::RGBCURVE, "HISTORY_MSG_CHMIXER_RED_PRIMARY");
    EvGreenPrimary = m->newEvent(rtengine::RGBCURVE, "HISTORY_MSG_CHMIXER_GREEN_PRIMARY");
    EvBluePrimary = m->newEvent(rtengine::RGBCURVE, "HISTORY_MSG_CHMIXER_BLUE_PRIMARY");
    EvToolReset.set_action(rtengine::RGBCURVE);

    imgIcon[0] = Gtk::manage (new RTImage ("circle-red-small.png"));
    imgIcon[1] = Gtk::manage (new RTImage ("circle-green-red-small.png"));
    imgIcon[2] = Gtk::manage (new RTImage ("circle-blue-red-small.png"));
    imgIcon[3] = Gtk::manage (new RTImage ("circle-red-green-small.png"));
    imgIcon[4] = Gtk::manage (new RTImage ("circle-green-small.png"));
    imgIcon[5] = Gtk::manage (new RTImage ("circle-blue-green-small.png"));
    imgIcon[6] = Gtk::manage (new RTImage ("circle-red-blue-small.png"));
    imgIcon[7] = Gtk::manage (new RTImage ("circle-green-blue-small.png"));
    imgIcon[8] = Gtk::manage (new RTImage ("circle-blue-small.png"));

    Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
    mode = Gtk::manage(new MyComboBoxText());
    mode->append(M("TP_CHMIXER_MODE_RGB_MATRIX"));
    mode->append(M("TP_CHMIXER_MODE_PRIMARIES_CHROMA"));
    mode->set_active(0);
    mode->signal_changed().connect(sigc::mem_fun(*this, &ChMixer::modeChanged));
    hb->pack_start(*Gtk::manage(new Gtk::Label(M("TP_CHMIXER_MODE") + ": ")), Gtk::PACK_SHRINK);
    hb->pack_start(*mode);
    pack_start(*hb);

    matrix_box = Gtk::manage(new Gtk::VBox());

    Gtk::Label* rlabel = Gtk::manage (new Gtk::Label(M("TP_CHMIXER_RED")));
    rlabel->set_alignment(Gtk::ALIGN_START);

    constexpr double RANGE = 500.0;
    red[0] = Gtk::manage (new Adjuster ("",   -RANGE, RANGE, 0.1, 100, imgIcon[0]));
    red[1] = Gtk::manage (new Adjuster ("", -RANGE, RANGE, 0.1, 0, imgIcon[1]));
    red[2] = Gtk::manage (new Adjuster ("",  -RANGE, RANGE, 0.1, 0, imgIcon[2]));

    Gtk::HSeparator* rsep = Gtk::manage (new Gtk::HSeparator ());

    matrix_box->pack_start(*rlabel);

    for (int i = 0; i < 3; i++) {
        matrix_box->pack_start(*red[i]);
    }

    matrix_box->pack_start(*rsep, Gtk::PACK_EXPAND_WIDGET, 4);

    Gtk::Label* glabel = Gtk::manage (new Gtk::Label(M("TP_CHMIXER_GREEN")));
    glabel->set_alignment(Gtk::ALIGN_START);


    green[0] = Gtk::manage (new Adjuster ("",   -RANGE, RANGE, 0.1, 0, imgIcon[3]));
    green[1] = Gtk::manage (new Adjuster ("", -RANGE, RANGE, 0.1, 100, imgIcon[4]));
    green[2] = Gtk::manage (new Adjuster ("",  -RANGE, RANGE, 0.1, 0, imgIcon[5]));

    Gtk::HSeparator* gsep = Gtk::manage (new Gtk::HSeparator ());

    matrix_box->pack_start(*glabel);

    for (int i = 0; i < 3; i++) {
        matrix_box->pack_start(*green[i]);
    }

    matrix_box->pack_start(*gsep, Gtk::PACK_EXPAND_WIDGET, 4);

    Gtk::Label* blabel = Gtk::manage (new Gtk::Label(M("TP_CHMIXER_BLUE")));
    blabel->set_alignment(Gtk::ALIGN_START);
    blue[0] = Gtk::manage (new Adjuster ("",   -RANGE, RANGE, 0.1, 0, imgIcon[6]));
    blue[1] = Gtk::manage (new Adjuster ("", -RANGE, RANGE, 0.1, 0, imgIcon[7]));
    blue[2] = Gtk::manage (new Adjuster ("",  -RANGE, RANGE, 0.1, 100, imgIcon[8]));

    for (int i = 0; i < 3; i++) {
        red[i]->setAdjusterListener (this);
        green[i]->setAdjusterListener (this);
        blue[i]->setAdjusterListener (this);

        red[i]->setLogScale(25, 0, true);
        green[i]->setLogScale(25, 0, true);
        blue[i]->setLogScale(25, 0, true);
    }

    matrix_box->pack_start(*blabel);

    for (int i = 0; i < 3; i++) {
        matrix_box->pack_start(*blue[i]);
    }

    pack_start(*Gtk::manage(new Gtk::HSeparator()));
    pack_start(*matrix_box);

    primaries_box = Gtk::manage(new Gtk::VBox());
    std::array<std::pair<const char *, const char *>, 3> hue_imgs = {
        std::make_pair("redpurple", "orange"),
        std::make_pair("greenyellow", "greencyan"),
        std::make_pair("bluecyan", "bluepurple")
    };
    for (int c = 0; c < 3; ++c) {
        const char *chan = (c == 0 ? "R" : (c == 1 ? "G" : "B"));
        const char *img = (c == 0 ? "red" : (c == 1 ? "green" : "blue"));
        Gtk::Frame *f = Gtk::manage(new Gtk::Frame(""));
        Gtk::HBox *lbl = Gtk::manage(new Gtk::HBox());
        lbl->pack_start(*Gtk::manage(new RTImage(std::string("circle-") + img + "-small.png")), Gtk::PACK_SHRINK, 2);
        lbl->pack_start(*Gtk::manage(new Gtk::Label(M(std::string("TP_CHMIXER_PRIMARY_") + chan))));
        f->set_label_align(0.025, 0.5);
        f->set_label_widget(*lbl);
        Gtk::VBox *vb = Gtk::manage(new Gtk::VBox());
        vb->set_spacing(2);
        vb->set_border_width(2);
    
        hue_tweak[c] = Gtk::manage(new Adjuster(M("TP_CHMIXER_HUE"), -100, 100, 1, 0, Gtk::manage(new RTImage(std::string("circle-") + hue_imgs[c].first + "-small.png")), Gtk::manage(new RTImage(std::string("circle-") + hue_imgs[c].second + "-small.png"))));
        hue_tweak[c]->setAdjusterListener(this);
        vb->pack_start(*hue_tweak[c]);
        
        sat_tweak[c] = Gtk::manage(new Adjuster(M("TP_CHMIXER_SAT"), -100, 100, 1, 0));
        sat_tweak[c]->setAdjusterListener(this);
        vb->pack_start(*sat_tweak[c]);
        
        f->add(*vb);
        primaries_box->pack_start(*f);
    }
    pack_start(*primaries_box);
    
    show_all();
}


void ChMixer::read(const ProcParams* pp)
{
    disableListener ();

    setEnabled(pp->chmixer.enabled);
    if (pp->chmixer.mode == ChannelMixerParams::RGB_MATRIX) {
        mode->set_active(0);
    } else {
        mode->set_active(1);
    }
    
    for (int i = 0; i < 3; i++) {
        red[i]->setValue(pp->chmixer.red[i] / 10.0);
        green[i]->setValue(pp->chmixer.green[i] / 10.0);
        blue[i]->setValue(pp->chmixer.blue[i] / 10.0);

        hue_tweak[i]->setValue(pp->chmixer.hue_tweak[i]);
        sat_tweak[i]->setValue(pp->chmixer.sat_tweak[i]);
    }

    modeChanged();

    enableListener();
}


void ChMixer::write(ProcParams* pp)
{
    for (int i = 0; i < 3; i++) {
        pp->chmixer.red[i] = red[i]->getValue() * 10;
        pp->chmixer.green[i] = green[i]->getValue() * 10;
        pp->chmixer.blue[i] = blue[i]->getValue() * 10;

        pp->chmixer.hue_tweak[i] = hue_tweak[i]->getValue();
        pp->chmixer.sat_tweak[i] = sat_tweak[i]->getValue();
    }
    pp->chmixer.enabled = getEnabled();
    pp->chmixer.mode = mode->get_active_row_number() == 0 ? ChannelMixerParams::RGB_MATRIX : ChannelMixerParams::PRIMARIES_CHROMA;
    
}


void ChMixer::setDefaults(const ProcParams* defParams)
{
    for (int i = 0; i < 3; i++) {
        red[i]->setDefault(defParams->chmixer.red[i] / 10.f);
        green[i]->setDefault(defParams->chmixer.green[i] / 10.f);
        blue[i]->setDefault(defParams->chmixer.blue[i] / 10.f);

        hue_tweak[i]->setDefault(defParams->chmixer.hue_tweak[i]);
        sat_tweak[i]->setDefault(defParams->chmixer.sat_tweak[i]);
    }

    initial_params = defParams->chmixer;
}


void ChMixer::adjusterChanged(Adjuster* a, double newval)
{

    if (listener && getEnabled()) {
        for (int i = 0; i < 3; ++i) {
            if (a == hue_tweak[i] || a == sat_tweak[i]) {
                auto event = EvRedPrimary;
                if (i == 1) {
                    event = EvGreenPrimary;
                } else if (i == 2) {
                    event = EvBluePrimary;
                }
                Glib::ustring descr = Glib::ustring::compose("H=%1 S=%2", hue_tweak[i]->getValue(), sat_tweak[i]->getValue());
                listener->panelChanged(event, descr);
                return;
            }
        }
        
        Glib::ustring descr = Glib::ustring::compose ("R=%1,%2,%3\nG=%4,%5,%6\nB=%7,%8,%9",
                              red[0]->getValue(), red[1]->getValue(), red[2]->getValue(),
                              green[0]->getValue(), green[1]->getValue(), green[2]->getValue(),
                              blue[0]->getValue(), blue[1]->getValue(), blue[2]->getValue());
        listener->panelChanged(EvChMixer, descr);
    }
}


void ChMixer::adjusterAutoToggled(Adjuster* a, bool newval)
{
}


void ChMixer::enabledChanged()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged(EvChMixer, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged(EvChMixer, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvChMixer, M("GENERAL_DISABLED"));
        }
    }
}


void ChMixer::trimValues (rtengine::procparams::ProcParams* pp)
{

    for (int i = 0; i < 3; i++) {
        double r = pp->chmixer.red[i] / 10.0;
        double g = pp->chmixer.green[i] / 10.0;
        double b = pp->chmixer.blue[i] / 10.0;
        red[i]->trimValue(r);
        green[i]->trimValue(g);
        blue[i]->trimValue(b);
        pp->chmixer.red[i] = r * 10;
        pp->chmixer.green[i] = g * 10;
        pp->chmixer.blue[i] = b * 10;

        hue_tweak[i]->trimValue(pp->chmixer.hue_tweak[i]);
        sat_tweak[i]->trimValue(pp->chmixer.sat_tweak[i]);
    }
}


void ChMixer::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.chmixer = initial_params;
    }
    pp.chmixer.enabled = getEnabled();
    read(&pp);
}


void ChMixer::modeChanged()
{
    if (mode->get_active_row_number() == 0) {
        matrix_box->show();
        primaries_box->hide();
    } else {
        matrix_box->hide();
        primaries_box->show();
    }

    if (listener && getEnabled()) {
        listener->panelChanged(EvMode, M("GENERAL_CHANGED"));
    }
}
