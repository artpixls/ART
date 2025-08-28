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
#include "colorcorrection.h"
#include "eventmapper.h"
#include "mycurve.h"
#include "../rtengine/clutstore.h"
#include <iomanip>
#include <cmath>
#include <unordered_map>
#include <map>

using namespace rtengine;
using namespace rtengine::procparams;


namespace {

std::map<Glib::ustring, int> builtin_lut_to_idx;
std::map<int, std::pair<Glib::ustring, std::string>> builtin_luts;

void get_builtin_luts()
{
#ifdef ART_USE_CTL
    static bool done = false;

    if (!done) {
        done = true;
        std::vector<Glib::ustring> dirs = {
            Glib::build_filename(options.user_config_dir, "ctlscripts"),
            Glib::build_filename(options.ART_base_dir, "ctlscripts")
        };

        std::map<rtengine::CLUTStore::CLUTName, Glib::ustring> order;

        for (auto &dir : dirs) {
            try {
                if (Glib::file_test(dir, Glib::FILE_TEST_IS_DIR)) {
                    for (const auto &n : Glib::Dir(dir)) {
                        const std::string full_path = Glib::build_filename(dir, n);

                        if (!Glib::file_test(full_path, Glib::FILE_TEST_IS_DIR) && getExtension(full_path) == "ctl" && n[0] != '_') {
                            auto name = CLUTStore::getClutDisplayName(full_path);
                            if (order.find(name) == order.end()) {
                                order[name] = full_path;
                                if (options.rtSettings.verbose > 1) {
                                    std::cout << "Found user CTL script: " << full_path << std::endl;
                                }
                            }
                        }
                    }
                }
            } catch (Glib::Exception &exc) {
                if (options.rtSettings.verbose) {
                    std::cout << "ERROR in parsing user CTL scripts from " << dir << ": " << exc.what() << std::endl;
                }
            }
        }

        int idx = 5;
        for (auto &p : order) {
            builtin_lut_to_idx[p.second] = idx;
            builtin_luts[idx] = std::make_pair(p.first, Glib::filename_from_utf8(p.second));
            ++idx;
        }
        if (options.rtSettings.verbose) {
            std::cout << "Loaded " << builtin_luts.size() << " user CTL scripts"
                      << std::endl;
        }
    }
#endif // ART_USE_CTL
}

} // namespace

//-----------------------------------------------------------------------------
// ColorCorrectionMasksContentProvider
//-----------------------------------------------------------------------------

class ColorCorrectionMasksContentProvider: public MasksContentProvider {
public:
    ColorCorrectionMasksContentProvider(ColorCorrection *parent):
        parent_(parent)
    {
    }

    Gtk::Widget *getWidget() override
    {
        return parent_->box;
    }

    Glib::ustring getToolName() override
    {
        return "colorcorrection";
    }

    void getEvents(rtengine::ProcEvent &mask_list, rtengine::ProcEvent &parametric_mask, rtengine::ProcEvent &h_mask, rtengine::ProcEvent &c_mask, rtengine::ProcEvent &l_mask, rtengine::ProcEvent &blur, rtengine::ProcEvent &show, rtengine::ProcEvent &area_mask, rtengine::ProcEvent &deltaE_mask, rtengine::ProcEvent &contrastThreshold_mask, rtengine::ProcEvent &drawn_mask, rtengine::ProcEvent &mask_postprocess, rtengine::ProcEvent &linked_mask) override
    {
        mask_list = parent_->EvList;
        parametric_mask = parent_->EvParametricMask;
        h_mask = parent_->EvHueMask;
        c_mask = parent_->EvChromaticityMask;
        l_mask = parent_->EvLightnessMask;
        blur = parent_->EvMaskBlur;
        show = parent_->EvShowMask;
        area_mask = parent_->EvAreaMask;
        deltaE_mask = parent_->EvDeltaEMask;
        contrastThreshold_mask = parent_->EvContrastThresholdMask;
        drawn_mask = parent_->EvDrawnMask;
        mask_postprocess = parent_->EvMaskPostprocess;
        linked_mask = parent_->EvLinkedMask;
    }

    ToolPanelListener *listener() override
    {
        if (parent_->getEnabled()) {
            return parent_->listener;
        }
        return nullptr;
    }

    void selectionChanging(int idx) override
    {
        parent_->regionGet(idx);
    }

    void selectionChanged(int idx) override
    {
        parent_->regionShow(idx);
    }

    bool addPressed(int idx) override
    {
        parent_->data.insert(parent_->data.begin() + idx, rtengine::procparams::ColorCorrectionParams::Region());
        return true;
    }

    bool removePressed(int idx) override
    {
        parent_->data.erase(parent_->data.begin() + idx);
        return true;
    }
    
    bool copyPressed(int idx) override
    {
        parent_->data.insert(parent_->data.begin() + idx + 1, parent_->data[idx]);
        return true;
    }
    
    bool moveUpPressed(int idx) override
    {
        auto r = parent_->data[idx];
        parent_->data.erase(parent_->data.begin() + idx);
        --idx;
        parent_->data.insert(parent_->data.begin() + idx, r);
        return true;
    }
    
    bool moveDownPressed(int idx) override
    {
        auto r = parent_->data[idx];
        parent_->data.erase(parent_->data.begin() + idx);
        ++idx;
        parent_->data.insert(parent_->data.begin() + idx, r);
        return true;
    }

    bool resetPressed(int idx) override
    {
        parent_->data[idx] = ColorCorrectionParams::Region();
        return true;
    }

    int getColumnCount() override
    {
        return 1;
    }
    
    Glib::ustring getColumnHeader(int col) override
    {
        return M("TP_COLORCORRECTION_LIST_TITLE");
    }
    
    Glib::ustring getColumnContent(int col, int row) override
    {
        auto &r = parent_->data[row];
        switch (r.mode) {
        case rtengine::procparams::ColorCorrectionParams::Mode::RGB: {
            const auto lbl =
                [](const std::array<double, 3> &v) -> Glib::ustring
                {
                    return Glib::ustring::compose("{%1,%2,%3}", v[0], v[1], v[2]);
                };
            return Glib::ustring::compose("RGB S=%5 So=%6\ns=%1 o=%2\np=%3 c=%7 P=%4", lbl(r.slope), lbl(r.offset), lbl(r.power), lbl(r.pivot), r.inSaturation, r.outSaturation, lbl(r.compression));
        }   break;
        case rtengine::procparams::ColorCorrectionParams::Mode::HSL: {
            const auto lbl =
                [](const std::array<double, 3> &v) -> Glib::ustring
                {
                    return Glib::ustring::compose("{s=%1,o=%2,p=%3}", v[0], v[1], v[2]);
                };
            return Glib::ustring::compose("HSL S=%4 So=%5 h=%6 g=%7\nH=%1\nS=%2\nL=%3", lbl(r.hue), lbl(r.sat), lbl(r.factor), r.inSaturation, r.outSaturation, r.hueshift, r.hsl_gamma);
        }   break;
        case rtengine::procparams::ColorCorrectionParams::Mode::LUT:
            if (builtin_lut_to_idx.find(r.lutFilename) != builtin_lut_to_idx.end()) {
                return rtengine::CLUTStore::getClutDisplayName(r.lutFilename);
            } else {
                return Glib::ustring::compose("LUT %1", rtengine::CLUTStore::getClutDisplayName(r.lutFilename));
            }
            break;
        default: {
            const auto round_ab = [](float v) -> float
                                  {
                                      return int(v * 1000) / 1000.f;
                                  };
            Glib::ustring cx, cy;
            if (r.mode == rtengine::procparams::ColorCorrectionParams::Mode::YUV) {
                cx = Glib::ustring::compose("x=%1", round_ab(r.a));
                cy = Glib::ustring::compose("y=%1", round_ab(r.b));
            } else {
                cx = Glib::ustring::compose("az=%1", round_ab(r.a));
                cy = Glib::ustring::compose("bz=%1", round_ab(r.b));
            }
            Glib::ustring sop = Glib::ustring::compose("s=%1 o=%2 p=%3 c=%4 P=%5", r.slope[0], r.offset[0], r.power[0], r.compression[0], r.pivot[0]);
            return Glib::ustring::compose("%1 %2 S=%3 So=%4 h=%5\n%6", cx, cy, r.inSaturation, r.outSaturation, r.hueshift, sop);
        }   break;
        }
    }

    void getEditIDs(EditUniqueID &hcurve, EditUniqueID &ccurve, EditUniqueID &lcurve, EditUniqueID &deltaE) override
    {
        hcurve = EUID_Masks_H1;
        ccurve = EUID_Masks_C1;
        lcurve = EUID_Masks_L1;
        deltaE = EUID_Masks_DE1;
    }

private:
    ColorCorrection *parent_;
};

//-----------------------------------------------------------------------------
// ColorCorrection
//-----------------------------------------------------------------------------

namespace {

constexpr double SLOPE_LO = 0.01;
constexpr double SLOPE_HI = 32.0;
constexpr double SLOPE_MID = (SLOPE_HI - SLOPE_LO) / 2;
constexpr double SLOPE_MMID = SLOPE_LO + SLOPE_MID;
constexpr double SLOPE_PIVOT = 1.0;
constexpr double SLOPE_BASE = 32.0;

double slope_slider2val(double slider)
{
    if (slider >= SLOPE_MMID) {
        double range = SLOPE_HI - SLOPE_MMID;
        double x = (slider - SLOPE_MMID) / range;
        return SLOPE_PIVOT + (std::pow(SLOPE_BASE, x) - 1.0) / (SLOPE_BASE - 1.0) * (SLOPE_HI - SLOPE_PIVOT);
    } else {
        double range = SLOPE_MMID - SLOPE_LO;
        return SLOPE_LO + (slider - SLOPE_LO) / range;
    }
}

double slope_val2slider(double val)
{
    if (val >= SLOPE_PIVOT) {
        double range = SLOPE_HI - SLOPE_PIVOT;
        double x = (val - SLOPE_PIVOT) / range;
        return (SLOPE_LO + SLOPE_MID) + std::log(x * (SLOPE_BASE - 1.0) + 1.0) / std::log(SLOPE_BASE) * SLOPE_MID;
    } else {
        return SLOPE_LO + (val - SLOPE_LO) * SLOPE_MID;
    }
}


class CurveDisplay: public Gtk::DrawingArea, public BackBuffer {
public:
    CurveDisplay(ColorCorrection *parent, bool rgb):
        parent_(parent),
        rgb_(rgb)
    {
        get_style_context()->add_class("drawingarea");
    }

    bool on_draw(const Cairo::RefPtr<Cairo::Context> &cr) override
    {
        Gtk::Allocation allocation = get_allocation();
        allocation.set_x(0);
        allocation.set_y(0);
        if (setDrawRectangle(Cairo::FORMAT_ARGB32, allocation)) {
            setDirty(true);
        }
        if (isDirty() && surfaceCreated()) {
            parent_->drawCurve(rgb_, getContext(), get_style_context(), allocation.get_width(), allocation.get_height());
            setDirty(false);
            queue_draw();
        }
        copySurface(cr);
        return false;
    }

    Gtk::SizeRequestMode get_request_mode_vfunc() const
    {
        return Gtk::SIZE_REQUEST_HEIGHT_FOR_WIDTH;
    }


    void get_preferred_width_vfunc(int &minimum_width, int &natural_width) const
    {
        Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
        Gtk::Border padding = getPadding(style);  // already scaled
        int s = RTScalable::getScale();
        int p = padding.get_left() + padding.get_right();

        minimum_width = 50 * s + p;
        natural_width = 150 * s + p;  // same as GRAPH_SIZE from mycurve.h
    }


    void get_preferred_height_for_width_vfunc(int width, int &minimum_height, int &natural_height) const
    {
        Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
        Gtk::Border padding = getPadding(style);  // already scaled

        minimum_height = natural_height = width * 0.75 - padding.get_left() - padding.get_right() + padding.get_top() + padding.get_bottom();
    }
    
private:
    ColorCorrection *parent_;
    bool rgb_;
};


class HueShiftBar: public Gtk::DrawingArea, public BackBuffer {
public:
    HueShiftBar():
        cbar_(RTO_Left2Right) {}

    void setColorProvider(ColorProvider *cp, int i)
    {
        cbar_.setColorProvider(cp, i);
    }
    
    Gtk::SizeRequestMode get_request_mode_vfunc() const override { return Gtk::SIZE_REQUEST_CONSTANT_SIZE; }

    void get_preferred_height_vfunc (int& minimum_height, int& natural_height) const override
    {
        int minimumWidth = 0;
        int naturalWidth = 0;
        get_preferred_width_vfunc (minimumWidth, naturalWidth);
        get_preferred_height_for_width_vfunc (minimumWidth, minimum_height, natural_height);
    }

    void get_preferred_width_vfunc (int &minimum_width, int &natural_width) const override
    {
        int s = RTScalable::getScale();
        Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
        Gtk::Border padding = getPadding(style);  // already scaled
        int margins = padding.get_left() + padding.get_right();
        minimum_width = 60 * s + margins;
        natural_width = 150 * s + margins;
    }
        
    void get_preferred_height_for_width_vfunc (int width, int &minimum_height, int &natural_height) const override
    {
        int s = RTScalable::getScale();
        Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
        Gtk::Border padding = getPadding(style);  // already scaled
        int margins = padding.get_left() + padding.get_right();
        natural_height = minimum_height = 16 * s + margins;
    }
        
    void get_preferred_width_for_height_vfunc (int height, int &minimum_width, int &natural_width) const override
    {
        get_preferred_width_vfunc (minimum_width, natural_width);
    }
        
    bool on_draw(const ::Cairo::RefPtr< Cairo::Context> &cr) override
    {
        // on_realize & updateBackBuffer have to be called before
        if (get_realized() && get_allocated_width() && get_allocated_height()) {
            if (isDirty()) {
                updateBackBuffer();
            }

            if (surface) {
                copySurface(cr);
            }
        }

        return true;
    }

private:
    void updateBackBuffer()
    {
        if (!get_realized() || !isDirty() || !get_allocated_width() || !get_allocated_height())  {
            return;
        }

        // This will create or update the size of the BackBuffer::surface
        setDrawRectangle(Cairo::FORMAT_ARGB32, 0, 0, get_allocated_width(), get_allocated_height(), true);

        if (!surface) {
            return;
        }

        Cairo::RefPtr<Cairo::Context> cr = Cairo::Context::create(surface);
        Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
        Gtk::Border padding = getPadding(style);  // already scaled

        cr->set_source_rgba (0., 0., 0., 0.);
        cr->set_operator (Cairo::OPERATOR_CLEAR);
        cr->paint ();
        cr->set_operator (Cairo::OPERATOR_OVER);

        int w = get_allocated_width ();
        int h = get_allocated_height ();


        double innerBarX = (double)padding.get_left();
        double innerBarY = padding.get_top();
        double innerBarW = w - padding.get_right();
        double innerBarH = h - padding.get_bottom();
        if (cbar_.canGetColors()) {
            cbar_.setDirty(true);
            cbar_.setDrawRectangle(innerBarX, innerBarY, innerBarW, innerBarH);
            cbar_.expose(*this, cr);
        } else {
            style->render_background(cr, innerBarX, innerBarY, innerBarW, innerBarH);
        }
        if (!is_sensitive()) {
            cr->set_source_rgba(0., 0., 0., 0.5);
            cr->rectangle(innerBarX, innerBarY, innerBarW, innerBarH);
            cr->fill();
        }
    }       
    
    ColoredBar cbar_;
};


} // namespace

ColorCorrection::ColorCorrection(): FoldableToolPanel(this, "colorcorrection", M("TP_COLORCORRECTION_LABEL"), false, true, true)
{
    auto m = ProcEventMapper::getInstance();
    auto EVENT = LUMINANCECURVE | M_LUMACURVE;
    EvEnabled = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_ENABLED");
    EvColorWheel = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_AB");
    EvInSaturation = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_INSATURATION");
    EvOutSaturation = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_OUTSATURATION");
    EvLightness = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_LIGHTNESS");
    EvSlope = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_SLOPE");
    EvOffset = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_OFFSET");
    EvPower = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_POWER");
    EvPivot = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_PIVOT");
    EvMode = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_MODE");
    EvRgbLuminance = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_RGBLUMINANCE");
    EvHueShift = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_HUESHIFT");
    EvCompression = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_COMPRESSION");
    EvLUT = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_LUT");
    EvLUTParams = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_LUT_PARAMS");

    EvHSLGamma = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_HSL_GAMMA");

    EvList = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_LIST");
    EvParametricMask = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_PARAMETRICMASK");
    EvHueMask = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_HUEMASK");
    EvChromaticityMask = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_CHROMATICITYMASK");
    EvLightnessMask = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_LIGHTNESSMASK");
    EvMaskBlur = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_MASKBLUR");
    EvShowMask = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_SHOWMASK");
    EvAreaMask = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_AREAMASK");
    EvDeltaEMask = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_DELTAEMASK");
    EvContrastThresholdMask = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_CONTRASTTHRESHOLDMASK");
    EvDrawnMask = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_DRAWNMASK");
    EvMaskPostprocess = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_MASK_POSTPROCESS");
    EvLinkedMask = m->newEvent(EVENT, "HISTORY_MSG_COLORCORRECTION_LINKEDMASK");

    EvToolReset.set_action(EVENT);

    box = Gtk::manage(new Gtk::VBox());

    Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
    mode = Gtk::manage(new MyComboBoxText());
    mode->append(M("TP_COLORCORRECTION_MODE_COMBINED"));
    mode->append(M("TP_COLORCORRECTION_MODE_JZAZBZ"));
    mode->append(M("TP_COLORCORRECTION_MODE_RGBCHANNELS"));
    mode->append(M("TP_COLORCORRECTION_MODE_HSL"));
    mode->append(M("TP_COLORCORRECTION_MODE_LUT"));

    get_builtin_luts();
    for (auto &p : builtin_luts) {
        mode->append(p.second.first);
    }
    
    mode->set_active(0);
    mode->signal_changed().connect(sigc::mem_fun(*this, &ColorCorrection::modeChanged));
    
    hb->pack_start(*Gtk::manage(new Gtk::Label(M("TP_COLORCORRECTION_MODE") + ": ")), Gtk::PACK_SHRINK);
    hb->pack_start(*mode);
    box->pack_start(*hb);

    {
        hueframe = Gtk::manage(new Gtk::Frame(M("TP_COLORCORRECTION_HUESHIFT")));
        Gtk::VBox *vb = Gtk::manage(new Gtk::VBox());

        hueshift = Gtk::manage(new Adjuster(""/*M("TP_COLORCORRECTION_HUESHIFT")*/, -180, 180, 0.1, 0, nullptr, nullptr, nullptr, nullptr, false, true));
        vb->pack_start(*hueshift, Gtk::PACK_SHRINK, 4);
        hueshift->setLogScale(90, 0, true);
        hueshift->setAdjusterListener(this);
        hueshift_bar = Gtk::manage(new HueShiftBar());
        vb->pack_start(*hueshift_bar, Gtk::PACK_SHRINK, 4);
        static_cast<HueShiftBar *>(hueshift_bar)->setColorProvider(this, 1);
        hueframe->add(*vb);
        box->pack_start(*hueframe);
    }
    
    box_combined = Gtk::manage(new Gtk::VBox());
    box_rgb = Gtk::manage(new Gtk::VBox());

    wheel = Gtk::manage(new ColorWheel());
    wheel->setEditID(EUID_ColorCorrection_Wheel, BT_IMAGEFLOAT);
    wheel->signal_changed().connect(sigc::mem_fun(*this, &ColorCorrection::wheelChanged));
    Gtk::Notebook *nb = Gtk::manage(new Gtk::Notebook());
    nb->set_tab_pos(Gtk::POS_BOTTOM);
    nb->set_name("ExpanderBox");
    {
        auto w = Gtk::manage(new RTImage("circle-dot-big.png"));
        nb->append_page(*wheel, *w);
    }    
    box_combined->pack_start(*nb, Gtk::PACK_SHRINK, 4);//wheel);

    satframe = Gtk::manage(new Gtk::Frame(M("TP_COLORCORRECTION_SATURATION")));
    Gtk::VBox *satbox = Gtk::manage(new Gtk::VBox());
    inSaturation = Gtk::manage(new Adjuster(M("TP_COLORCORRECTION_IN"), -100, 100, 1, 0, nullptr, nullptr, nullptr, nullptr, false, true));
    inSaturation->setAdjusterListener(this);
    satbox->pack_start(*inSaturation, Gtk::PACK_EXPAND_WIDGET, 4);

    outSaturation = Gtk::manage(new Adjuster(M("TP_COLORCORRECTION_OUT"), -100, 100, 1, 0, nullptr, nullptr, nullptr, nullptr, false, true));
    outSaturation->setAdjusterListener(this);
    satbox->pack_start(*outSaturation, Gtk::PACK_EXPAND_WIDGET, 4);
    satframe->add(*satbox);
    box->pack_start(*satframe);

    double2double_fun slope_v2s = options.adjuster_force_linear ? nullptr : slope_val2slider;
    double2double_fun slope_s2v = options.adjuster_force_linear ? nullptr : slope_slider2val;

    curve_lum = Gtk::manage(new CurveDisplay(this, false));
    {
        auto w = Gtk::manage(new RTImage("curve-spline.png"));
        nb->append_page(*curve_lum, *w);
    }
    //box_combined->pack_start(*curve_lum, Gtk::PACK_SHRINK, 4);    

    slope = Gtk::manage(new Adjuster(M("TP_COLORCORRECTION_SLOPE"), 0.01, 32.0, 0.001, 1, nullptr, nullptr, slope_s2v, slope_v2s));
//    slope->setLogScale(32, 1, true);
    slope->setAdjusterListener(this);
    box_combined->pack_start(*slope);
    offset = Gtk::manage(new Adjuster(M("TP_COLORCORRECTION_OFFSET"), -0.15, 0.15, 0.0001, 0));
    offset->setAdjusterListener(this);
    box_combined->pack_start(*offset);
    power = Gtk::manage(new Adjuster(M("TP_COLORCORRECTION_POWER"), 0.1, 4.0, 0.001, 1));
    power->setAdjusterListener(this);
    power->setLogScale(10, 1, true);
    box_combined->pack_start(*power);
    pivot = Gtk::manage(new Adjuster(M("TP_COLORCORRECTION_PIVOT"), 0.001, 2.0, 0.001, 1));
    pivot->setAdjusterListener(this);
    pivot->setLogScale(100, 0.18, true);
    box_combined->pack_start(*pivot);
    compression = Gtk::manage(new Adjuster(M("TP_COLORCORRECTION_COMPRESSION"), 0, 1, 0.001, 0));
    compression->setAdjusterListener(this);
    compression->setLogScale(25, 0);
    box_combined->pack_start(*compression);

    sync_rgb_sliders = Gtk::manage(new Gtk::CheckButton(M("TP_COLORCORRECTION_SYNC_SLIDERS")));
    box_rgb->pack_start(*sync_rgb_sliders, Gtk::PACK_SHRINK, 4);

    rgbluminance = Gtk::manage(new Gtk::CheckButton(M("TP_COLORCORRECTION_RGBLUMINANCE")));
    rgbluminance->signal_toggled().connect(
        sigc::slot<void>(
            [this]() -> void
            {
                if (listener && getEnabled()) {
                    listener->panelChanged(EvRgbLuminance, rgbluminance->get_active() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
                }
            }));
    box_rgb->pack_start(*rgbluminance, Gtk::PACK_SHRINK, 4);

    curve_rgb = Gtk::manage(new CurveDisplay(this, true));
    box_rgb->pack_start(*curve_rgb, Gtk::PACK_SHRINK, 4);
    
    nb = Gtk::manage(new Gtk::Notebook());
    nb->set_name("ExpanderBox");
    box_rgb->pack_start(*nb);
    
    for (int c = 0; c < 3; ++c) {
        const char *chan = (c == 0 ? "R" : (c == 1 ? "G" : "B"));
        const char *img = (c == 0 ? "red" : (c == 1 ? "green" : "blue"));
        //Gtk::Frame *f = Gtk::manage(new Gtk::Frame(""));
        Gtk::HBox *lbl = Gtk::manage(new Gtk::HBox());
        lbl->pack_start(*Gtk::manage(new RTImage(std::string("circle-") + img + "-small.png")), Gtk::PACK_SHRINK, 2);
        lbl->pack_start(*Gtk::manage(new Gtk::Label(M(std::string("TP_COLORCORRECTION_CHANNEL_") + chan))));
        //f->set_label_align(0.025, 0.5);
        //f->set_label_widget(*lbl);
        Gtk::VBox *vb = Gtk::manage(new Gtk::VBox());
        vb->set_spacing(2);
        vb->set_border_width(2);
    
        slope_rgb[c] = Gtk::manage(new Adjuster(M("TP_COLORCORRECTION_SLOPE"), 0.01, 10.0, 0.001, 1, nullptr, nullptr, slope_s2v, slope_v2s));
        //slope_rgb[c]->setLogScale(10, 1, true);
        slope_rgb[c]->setAdjusterListener(this);
        vb->pack_start(*slope_rgb[c]);
        offset_rgb[c] = Gtk::manage(new Adjuster(M("TP_COLORCORRECTION_OFFSET"), -0.15, 0.15, 0.0001, 0));
        offset_rgb[c]->setAdjusterListener(this);
        vb->pack_start(*offset_rgb[c]);
        power_rgb[c] = Gtk::manage(new Adjuster(M("TP_COLORCORRECTION_POWER"), 0.1, 4.0, 0.001, 1));
        power_rgb[c]->setAdjusterListener(this);
        power_rgb[c]->setLogScale(10, 1, true);
        vb->pack_start(*power_rgb[c]);
        pivot_rgb[c] = Gtk::manage(new Adjuster(M("TP_COLORCORRECTION_PIVOT"), 0.001, 2.0, 0.001, 1));
        pivot_rgb[c]->setAdjusterListener(this);
        pivot_rgb[c]->setLogScale(100, 0.18, true);
        vb->pack_start(*pivot_rgb[c]);
        compression_rgb[c] = Gtk::manage(new Adjuster(M("TP_COLORCORRECTION_COMPRESSION"), 0, 1, 0.001, 0));
        compression_rgb[c]->setAdjusterListener(this);
        compression_rgb[c]->setLogScale(25, 0);
        vb->pack_start(*compression_rgb[c]);

        //f->add(*vb);
        //box_rgb->pack_start(*f);
        lbl->show_all();
        nb->append_page(*vb, *lbl);
    }

    box_hsl = Gtk::manage(new Gtk::VBox());
    Gtk::HBox *box_hsl_h = Gtk::manage(new Gtk::HBox());
    for (int c = 0; c < 3; ++c) {
        const char *chan = (c == 0 ? "SLOPE" : (c == 1 ? "OFFSET" : "POWER"));
        Gtk::Frame *f = Gtk::manage(new Gtk::Frame(M(std::string("TP_COLORCORRECTION_") + chan)));
        Gtk::VBox *vb = Gtk::manage(new Gtk::VBox());
        vb->set_spacing(2);
        vb->set_border_width(2);
    
        double sscale = c == 0 ? 1.4 : (c == 1 ? 3 : 1.8);
        huesat[c] = Gtk::manage(new HueSatColorWheel(sscale));
        huesat[c]->signal_changed().connect(sigc::bind(sigc::mem_fun(*this, &ColorCorrection::hslWheelChanged), c));
        auto s = RTScalable::getScale();
        huesat[c]->set_size_request(200 * s, -1);
        Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
        hb->pack_start(*Gtk::manage(new Gtk::Label("")), Gtk::PACK_EXPAND_WIDGET, 10);
        hb->pack_start(*huesat[c], Gtk::PACK_SHRINK);
        hb->pack_start(*Gtk::manage(new Gtk::Label("")), Gtk::PACK_EXPAND_WIDGET, 10);
        vb->pack_start(*hb);

        if (c == 1) {
            lfactor[c] = Gtk::manage(new Adjuster("", -10.0, 10.0, 0.001, 0, Gtk::manage(new RTImage("circle-black-small.png")), Gtk::manage(new RTImage("circle-white-small.png")), nullptr, nullptr, false, false));
        } else {
            lfactor[c] = Gtk::manage(new Adjuster("", -50.0, 50.0, 0.1, 0, Gtk::manage(new RTImage("circle-black-small.png")), Gtk::manage(new RTImage("circle-white-small.png")), nullptr, nullptr, false, false));
        }
        lfactor[c]->setAdjusterListener(this);
        vb->pack_start(*lfactor[c]);

        f->add(*vb);
        box_hsl->pack_start(*f);
    }
    box_hsl->pack_start(*box_hsl_h);

    hsl_gamma = Gtk::manage(new Adjuster(M("TP_COLORCORRECTION_HSL_GAMMA"), 1, 3, 0.1, 2.4));
    box_hsl->pack_start(*hsl_gamma);
    hsl_gamma->setAdjusterListener(this);
    
    lut_filename = Gtk::manage(new MyFileChooserButton(M("TP_COLORCORRECTION_LUT_SELECT")));
    if (!options.clutsDir.empty()) {
        lut_filename->set_current_folder(options.clutsDir);
    }
    box_lut = Gtk::manage(new Gtk::VBox());
    Gtk::HBox *hbox_lut = Gtk::manage(new Gtk::HBox());
    hbox_lut->pack_start(*Gtk::manage(new Gtk::Label(M("TP_COLORCORRECTION_LUT_FILENAME") + ": ")), Gtk::PACK_SHRINK, 4);
    hbox_lut->pack_start(*lut_filename, Gtk::PACK_EXPAND_WIDGET, 4);
    box_lut->pack_start(*hbox_lut);
    lut_filename_box = hbox_lut;

    lut_params = Gtk::manage(new CLUTParamsPanel());
    box_lut->pack_start(*lut_params);

    {
        Glib::RefPtr<Gtk::FileFilter> filter_lut = Gtk::FileFilter::create();
        filter_lut->set_name(M("FILECHOOSER_FILTER_LUT"));
        filter_lut->add_pattern("*.png");
        filter_lut->add_pattern("*.PNG");
        filter_lut->add_pattern("*.tif");
        filter_lut->add_pattern("*.tiff");
#ifdef ART_USE_OCIO
        filter_lut->add_pattern("*.clf");
        filter_lut->add_pattern("*.CLF");
        filter_lut->add_pattern("*.clfz");
        filter_lut->add_pattern("*.CLFZ");
        filter_lut->add_pattern("*.json");
#endif // ART_USE_OCIO
#ifdef ART_USE_CTL
        filter_lut->add_pattern("*.ctl");
        filter_lut->add_pattern("*.CTL");
#endif // ART_USE_CTL
        Glib::RefPtr<Gtk::FileFilter> filter_any = Gtk::FileFilter::create();
        filter_any->set_name(M("FILECHOOSER_FILTER_ANY"));
        filter_any->add_pattern("*");

        lut_filename->add_filter(filter_lut);
        lut_filename->add_filter(filter_any);
    }
    lut_filename->signal_selection_changed().connect(sigc::mem_fun(*this, &ColorCorrection::lutChanged));
    lut_params->signal_changed().connect(sigc::mem_fun(*this, &ColorCorrection::lutParamsChanged));

    hueshift->delay = options.adjusterMaxDelay;
    inSaturation->delay = options.adjusterMaxDelay;
    outSaturation->delay = options.adjusterMaxDelay;
    slope->delay = options.adjusterMaxDelay;
    offset->delay = options.adjusterMaxDelay;
    power->delay = options.adjusterMaxDelay;
    pivot->delay = options.adjusterMaxDelay;
    compression->delay = options.adjusterMaxDelay;
    for (int c = 0; c < 3; ++c) {
        slope_rgb[c]->delay = options.adjusterMaxDelay;
        offset_rgb[c]->delay = options.adjusterMaxDelay;
        power_rgb[c]->delay = options.adjusterMaxDelay;
        pivot_rgb[c]->delay = options.adjusterMaxDelay;
        lfactor[c]->delay = options.adjusterMaxDelay;
        compression_rgb[c]->delay = options.adjusterMaxDelay;
    }
    hsl_gamma->delay = options.adjusterMaxDelay;

    masks_content_provider_.reset(new ColorCorrectionMasksContentProvider(this));
    masks_ = Gtk::manage(new MasksPanel(masks_content_provider_.get()));
    pack_start(*masks_, Gtk::PACK_EXPAND_WIDGET, 4);

    box->pack_start(*box_combined);
    box->pack_start(*box_rgb);
    box->pack_start(*box_hsl);
    box->pack_start(*box_lut);

    show_all_children();
}


void ColorCorrection::read(const ProcParams *pp)
{
    disableListener();

    setEnabled(pp->colorcorrection.enabled);
    data = pp->colorcorrection.regions;
    auto m = pp->colorcorrection.masks;
    if (data.empty()) {
        data.emplace_back(rtengine::procparams::ColorCorrectionParams::Region());
        m.emplace_back(rtengine::procparams::Mask());
    }
    masks_->setMasks(m, pp->colorcorrection.selectedRegion, pp->colorcorrection.showMask >= 0 && pp->colorcorrection.showMask == pp->colorcorrection.selectedRegion);

    modeChanged();
    enabledChanged();

    enableListener();
}


void ColorCorrection::write(ProcParams *pp)
{
    pp->colorcorrection.enabled = getEnabled();

    regionGet(masks_->getSelected());
    pp->colorcorrection.regions = data;

    masks_->getMasks(pp->colorcorrection.masks, pp->colorcorrection.showMask);
    pp->colorcorrection.selectedRegion = masks_->getSelected();
    assert(pp->colorcorrection.regions.size() == pp->colorcorrection.masks.size());

    masks_->updateSelected();
}


void ColorCorrection::setDefaults(const ProcParams *defParams)
{
    hueshift->setDefault(defParams->colorcorrection.regions[0].hueshift);//, defParams->colorcorrection.regions[0].hueshift);
    inSaturation->setDefault(defParams->colorcorrection.regions[0].inSaturation);
    outSaturation->setDefault(defParams->colorcorrection.regions[0].outSaturation);
    slope->setDefault(defParams->colorcorrection.regions[0].slope[0]);
    offset->setDefault(defParams->colorcorrection.regions[0].offset[0]);
    power->setDefault(defParams->colorcorrection.regions[0].power[0]);
    pivot->setDefault(defParams->colorcorrection.regions[0].pivot[0]);
    compression->setDefault(defParams->colorcorrection.regions[0].compression[0]);
    for (int c = 0; c < 3; ++c) {
        slope_rgb[c]->setDefault(defParams->colorcorrection.regions[0].slope[c]);
        offset_rgb[c]->setDefault(defParams->colorcorrection.regions[0].offset[c]);
        power_rgb[c]->setDefault(defParams->colorcorrection.regions[0].power[c]);
        pivot_rgb[c]->setDefault(defParams->colorcorrection.regions[0].pivot[c]);
        compression_rgb[c]->setDefault(defParams->colorcorrection.regions[0].compression[c]);
    }
    hsl_gamma->setDefault(defParams->colorcorrection.regions[0].hsl_gamma);

    initial_params = defParams->colorcorrection;
}


void ColorCorrection::adjusterChanged(Adjuster* a, double newval)
{
    rtengine::ProcEvent evt;
    Glib::ustring msg = a->getTextValue();
    if (a == inSaturation) {
        evt = EvInSaturation;
    } else if (a == outSaturation) {
        evt = EvOutSaturation;
    } else if (a == slope) {
        evt = EvSlope;
    } else if (a == offset) {
        evt = EvOffset;
    } else if (a == power) {
        evt = EvPower;
    } else if (a == pivot) {
        evt = EvPivot;
    } else if (a == hueshift) {
        evt = EvHueShift;
        hueshift_bar->queue_draw();
    } else if (a == compression) {
        evt = EvCompression;
    } else if (a == hsl_gamma) {
        evt = EvHSLGamma;
    } else {
        Adjuster **targets = nullptr;
        for (int c = 0; c < 3; ++c) {
            if (a == slope_rgb[c]) {
                evt = EvSlope;
                targets = slope_rgb;
                break;
            } else if (a == offset_rgb[c]) {
                evt = EvOffset;
                targets = offset_rgb;
                break;
            } else if (a == power_rgb[c]) {
                evt = EvPower;
                targets = power_rgb;
                break;
            } else if (a == pivot_rgb[c]) {
                evt = EvPivot;
                targets = pivot_rgb;
                break;
            } else if (a == compression_rgb[c]) {
                evt = EvCompression;
                targets = compression_rgb;
                break;
            } else if (a == lfactor[c]) {
                evt = c == 0 ? EvSlope : (c == 1 ? EvOffset : EvPower);
                break;
            }
        }
        if (targets && sync_rgb_sliders->get_active()) {
            for (int c = 0; c < 3; ++c) {
                targets[c]->setAdjusterListener(nullptr);
                targets[c]->setValue(newval);
                targets[c]->setAdjusterListener(this);
            }
        }
        if (evt != 0) {
            if (targets) {
                msg = Glib::ustring::compose("R=%1 G=%2 B=%3",
                                             targets[0]->getTextValue(),
                                             targets[1]->getTextValue(),
                                             targets[2]->getTextValue());
            } else {
                double h, s, l;
                if (evt == EvSlope) {
                    l = lfactor[0]->getValue();
                    huesat[0]->getParams(h, s);
                } else if (evt == EvOffset) {
                    l = lfactor[1]->getValue();
                    huesat[1]->getParams(h, s);
                } else {
                    l = lfactor[2]->getValue();
                    huesat[2]->getParams(h, s);
                }
                msg = Glib::ustring::compose("H=%1 S=%2 L=%3", h, s, l);
            }
        }
    }

    static_cast<CurveDisplay *>(curve_rgb)->setDirty(true);
    curve_rgb->queue_draw();

    static_cast<CurveDisplay *>(curve_lum)->setDirty(true);
    curve_lum->queue_draw();
    
    if (listener && getEnabled() && evt != 0) {
        listener->panelChanged(evt, msg);
    }
}


void ColorCorrection::enabledChanged ()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged(EvEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged(EvEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvEnabled, M("GENERAL_DISABLED"));
        }
    }
    if (options.toolpanels_disable) {
        box_combined->set_sensitive(getEnabled());
        box_rgb->set_sensitive(getEnabled());
        box_hsl->set_sensitive(getEnabled());
    }

    if (listener && !getEnabled()) {
        masks_->switchOffEditMode();
    }
}


void ColorCorrection::setEditProvider(EditDataProvider *provider)
{
    masks_->setEditProvider(provider);
    wheel->setEditProvider(provider);
}


void ColorCorrection::procParamsChanged(
    const rtengine::procparams::ProcParams* params,
    const rtengine::ProcEvent& ev,
    const Glib::ustring& descr,
    const ParamsEdited* paramsEdited)
{
    masks_->updateLinkedMaskList(params);
}


void ColorCorrection::updateGeometry(int fw, int fh)
{
    masks_->updateGeometry(fw, fh);
}


void ColorCorrection::regionGet(int idx)
{
    if (idx < 0 || size_t(idx) >= data.size()) {
        return;
    }
    
    auto &r = data[idx];
    switch (mode->get_active_row_number()) {
    case 0:
        r.mode = rtengine::procparams::ColorCorrectionParams::Mode::YUV;
        break;
    case 1:
        r.mode = rtengine::procparams::ColorCorrectionParams::Mode::JZAZBZ;
        break;
    case 2:
        r.mode = rtengine::procparams::ColorCorrectionParams::Mode::RGB;
        break;
    case 3:
        r.mode = rtengine::procparams::ColorCorrectionParams::Mode::HSL;
        break;
    case 4:
    default:
        r.mode = rtengine::procparams::ColorCorrectionParams::Mode::LUT;
        break;
    }
    r.inSaturation = inSaturation->getValue();
    r.outSaturation = outSaturation->getValue();
    r.hueshift = hueshift->getValue();
    r.hsl_gamma = hsl_gamma->getValue();
    // double top, bot;
    // hueshift->getValue(bot, top);
    // r.hueshift = top;
    if (r.mode != rtengine::procparams::ColorCorrectionParams::Mode::RGB) {
        wheel->getParams(r.a, r.b, r.abscale);
        for (int c = 0; c < 3; ++c) {
            r.slope[c] = slope->getValue();
            r.offset[c] = offset->getValue();
            r.power[c] = power->getValue();
            r.pivot[c] = pivot->getValue();
            r.compression[c] = compression->getValue();
        }
    } else {
        r.a = r.b = 0.f;
        for (int c = 0; c < 3; ++c) {
            r.slope[c] = slope_rgb[c]->getValue();
            r.offset[c] = offset_rgb[c]->getValue();
            r.power[c] = power_rgb[c]->getValue();
            r.pivot[c] = pivot_rgb[c]->getValue();
            r.compression[c] = compression_rgb[c]->getValue();
        }
    }
    for (int c = 0; c < 3; ++c) {
        double b, t;
        huesat[c]->getParams(t, b);
        r.hue[c] = t;
        r.sat[c] = b;
        r.factor[c] = lfactor[c]->getValue();
    }
    r.rgbluminance = rgbluminance->get_active();
    r.lutFilename = Glib::filename_to_utf8(lut_filename->get_filename());
    r.lut_params = lut_params->getValue();
}


void ColorCorrection::regionShow(int idx)
{
    const bool disable = listener;
    if (disable) {
        disableListener();
    }
    auto &r = data[idx];
    inSaturation->setValue(r.inSaturation);
    outSaturation->setValue(r.outSaturation);
    hueshift->setValue(r.hueshift);
    hsl_gamma->setValue(r.hsl_gamma);
    if (wheel->isCurrentSubscriber()) {
        wheel->unsubscribe();
    }
    wheel->setParams(r.a, r.b, r.abscale, false);
    slope->setValue(r.slope[0]);
    offset->setValue(r.offset[0]);
    power->setValue(r.power[0]);
    pivot->setValue(r.pivot[0]);
    compression->setValue(r.compression[0]);
    for (int c = 0; c < 3; ++c) {
        slope_rgb[c]->setValue(r.slope[c]);
        offset_rgb[c]->setValue(r.offset[c]);
        power_rgb[c]->setValue(r.power[c]);
        pivot_rgb[c]->setValue(r.pivot[c]);
        compression_rgb[c]->setValue(r.compression[c]);
    }
    for (int c = 0; c < 3; ++c) {
        huesat[c]->setParams(r.hue[c], r.sat[c], false);
        lfactor[c]->setValue(r.factor[c]);
    }
    rgbluminance->set_active(r.rgbluminance);
    //modeChanged();
    lut_filename->set_filename(Glib::filename_from_utf8(r.lutFilename));
    lut_params->setParams(rtengine::CLUTApplication::get_param_descriptors(r.lutFilename));
    lut_params->setValue(r.lut_params);

    switch (r.mode) {
    case rtengine::procparams::ColorCorrectionParams::Mode::RGB:
        mode->set_active(2);
        break;
    case rtengine::procparams::ColorCorrectionParams::Mode::JZAZBZ:
        mode->set_active(1);
        break;
    case rtengine::procparams::ColorCorrectionParams::Mode::HSL:
        mode->set_active(3);
        break;
    case rtengine::procparams::ColorCorrectionParams::Mode::LUT: {
        auto it = builtin_lut_to_idx.find(r.lutFilename);
        if (it != builtin_lut_to_idx.end()) {
            mode->set_active(it->second);
        } else {
            mode->set_active(4);
        }
    }   break;
    default:
        mode->set_active(0);
    }

    static_cast<CurveDisplay *>(curve_rgb)->setDirty(true);
    curve_rgb->queue_draw();

    static_cast<CurveDisplay *>(curve_lum)->setDirty(true);
    curve_lum->queue_draw();
    
    if (disable) {
        enableListener();
    }
}


void ColorCorrection::modeChanged()
{
    removeIfThere(box, box_combined);
    removeIfThere(box, box_rgb);
    removeIfThere(box, box_hsl);
    removeIfThere(box, box_lut);
    int row = mode->get_active_row_number();
    if (row < 2) {
        box->pack_start(*box_combined);
    } else if (row == 2) {
        box->pack_start(*box_rgb);
    } else if (row == 3) {
        box->pack_start(*box_hsl);
    } else {
        box->pack_start(*box_lut);
        // if (!lut_filename_box->is_visible()) {
        //     lut_filename->set_filename("");
        //     lut_params->setParams({});
        //     lut_params->setValue({});
        // }
        lut_filename_box->set_visible(row == 4);
        if (row > 4) {
            auto fn = builtin_luts[row].second;
            if (lut_filename->get_filename() != fn) {
                lut_filename->set_filename(fn);
                lut_params->setParams(rtengine::CLUTApplication::get_param_descriptors(fn));
                lut_params->setValue({});
            }
        }
    }
    satframe->set_visible(mode->get_active_row_number() < 4);
    hueframe->set_visible(mode->get_active_row_number() != 2 && mode->get_active_row_number() < 4);
    if (listener && getEnabled()) {
        masks_->setEdited(true);        
        listener->panelChanged(EvMode, mode->get_active_text());
    }
    auto eid = (row == 1) ? EUID_ColorCorrection_Wheel_Jzazbz : EUID_ColorCorrection_Wheel;
    wheel->setEditID(eid, BT_IMAGEFLOAT);

    static_cast<CurveDisplay *>(curve_rgb)->setDirty(true);
    curve_rgb->queue_draw();

    static_cast<CurveDisplay *>(curve_lum)->setDirty(true);
    curve_lum->queue_draw();
}


void ColorCorrection::setListener(ToolPanelListener *tpl)
{
     ToolPanel::setListener(tpl);
}


void ColorCorrection::setAreaDrawListener(AreaDrawListener *l)
{
    masks_->setAreaDrawListener(l);
}


void ColorCorrection::setDeltaEColorProvider(DeltaEColorProvider *p)
{
    masks_->setDeltaEColorProvider(p);
}


void ColorCorrection::adjusterChanged(ThresholdAdjuster *a, double newBottom, double newTop)
{
    // if (listener && getEnabled() && a == hueshift) {
    //     Glib::ustring st, sb;
    //     hueshift->getValue(sb, st);
    //     listener->panelChanged(EvHueShift, st);
    // }
}


void ColorCorrection::colorForValue(double valX, double valY, enum ColorCaller::ElemType elemType, int callerId, ColorCaller *caller)
{
    float R = 0.f, G = 0.f, B = 0.f;

    const auto to_hue =
        [](float x) -> float
        {
            //x -= 0.05f;
            if (x < 0.f) {
                x += 1.f;
            } else if (x > 1.f) {
                x -= 1.f;
            }
            return x;
        };

    if (callerId == 1) {  // Slider 1 background
        if (valY <= 0.5) {
            // the hue range
            Color::hsv2rgb01(to_hue(valX), 1.0f, 0.65f, R, G, B);
        } else {
            // the shifted hue
            // double b, t;
            double t = hueshift->getValue();
            double shift = SGN(t) * std::pow(std::abs(t) / 180.0, 0.8) * 0.5;
            double hue = to_hue(valX + shift);
            Color::hsv2rgb01(hue, 1.0f, 0.65f, R, G, B);
        }
    }

    caller->ccRed = double(R);
    caller->ccGreen = double(G);
    caller->ccBlue = double(B);
}


void ColorCorrection::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.colorcorrection = initial_params;
    }
    pp.colorcorrection.enabled = getEnabled();
    read(&pp);
}


void ColorCorrection::wheelChanged()
{
    if (listener && getEnabled()) {
        const auto round =
            [](float v) -> float
            {
                return int(v * 1000) / 1000.f;
            };
        double x, y, s;
        wheel->getParams(x, y, s);
        listener->panelChanged(EvColorWheel, Glib::ustring::compose(M("TP_COLORCORRECTION_ABVALUES"), round(x), round(y)));
    }
}


void ColorCorrection::hslWheelChanged(int c)
{
    if (listener && getEnabled()) {
        const auto round =
            [](float v) -> float
            {
                return int(v * 10) / 10.f;
            };
        double h, s, l;
        huesat[c]->getParams(h, s);
        l = lfactor[c]->getValue();
        listener->panelChanged(c == 0 ? EvSlope : (c == 1 ? EvOffset : EvPower), Glib::ustring::compose("H=%1 S=%2 L=%3", round(h), round(s), l));
    }
}


void ColorCorrection::drawCurve(bool rgb, Cairo::RefPtr<Cairo::Context> cr, Glib::RefPtr<Gtk::StyleContext> style, int W, int H)
{
    const double s = (double)RTScalable::getScale();

    Gtk::StateFlags state = !is_sensitive() ? Gtk::STATE_FLAG_INSENSITIVE : Gtk::STATE_FLAG_NORMAL;

    cr->set_line_cap(Cairo::LINE_CAP_SQUARE);

    // clear background
    cr->set_source_rgba(0., 0., 0., 0.);
    cr->set_operator(Cairo::OPERATOR_CLEAR);
    cr->paint();
    cr->set_operator(Cairo::OPERATOR_OVER);

    style->render_background(cr, 0, 0, W, H);

    Gdk::RGBA c;

    cr->set_line_width(1.0 * s);

    // draw the grid lines:
    cr->set_line_width (1.0 * s);
    c = style->get_border_color(state);
    cr->set_source_rgb(c.get_red(), c.get_green(), c.get_blue());
    cr->set_antialias(Cairo::ANTIALIAS_NONE);

    double dx = H / 10.0;
    double dy = W / 10.0;
    for (int i = 0; i <= 10; i++) {
        // horizontal lines
        cr->move_to(0, H - i * dx);
        cr->line_to(W, H - i * dx);
        // vertical lines
        cr->move_to(i * dy, 0);
        cr->line_to(i * dy, H);
    }

    cr->stroke();

    // draw f(x)=x line
    c = style->get_color(state);
    cr->set_source_rgba(c.get_red(), c.get_green(), c.get_blue(), 0.4);

    std::valarray<double> ds(1);
    ds[0] = 4 * s;
    cr->set_dash(ds, 0);
    cr->move_to(0, H);
    cr->line_to(W, 0);
    cr->stroke();
    cr->unset_dash();

    cr->set_antialias(Cairo::ANTIALIAS_SUBPIXEL);
    cr->set_line_width(1.0 * s);

    c = style->get_color(state);

    struct CurveEval {
        CurveEval(int width, int height,
                  double s, double o, double p, double v, double c, double y=0):
            W(width),
            H(height),
            slope(s),
            offset(o/2.0),
            power(1.0/p),
            pivot(v),
            y0(y)
        {
            double compr = c * 100.0;
            c0 = compr;
            if (compr > 0) {
                double y0 = std::pow((slope + offset)/pivot, power) * pivot;
                c1 = std::log(1.0 + y0 * c0) / slope;
            } else {
                c1 = 0;
            }
        }

        double operator()(double x)
        {
            double y = (x/W) * slope + offset;
            if (y > 0) {
                if (pivot != 1.0) {
                    y = std::pow(y / pivot, power) * pivot;
                } else {
                    y = std::pow(y, power);
                }
                if (c0 != 0.f) {
                    y = std::log(y * c0 + 1.0) / c1;
                }
            }
            return (H - std::max(y, 0.0) * H) + y0;
        }

        int W;
        int H;
        double slope;
        double offset;
        double power;
        double pivot;
        double c0;
        double c1;
        double y0;
    };
            

    // draw curve
    if (!rgb) {
        CurveEval getVal(W, H, slope->getValue(), offset->getValue(), power->getValue(), pivot->getValue(), compression->getValue());

        cr->set_source_rgb(c.get_red(), c.get_green(), c.get_blue());
        cr->move_to(0, getVal(0));

        for (int i = 1; i < W; ++i) {
            cr->line_to(i, getVal(i));
        }
        cr->stroke();
    } else {
        for (int j = 0; j < 3; ++j) {
            CurveEval getVal(W, H, slope_rgb[j]->getValue(), offset_rgb[j]->getValue(), power_rgb[j]->getValue(), pivot_rgb[j]->getValue(), compression_rgb[j]->getValue(), j * s);

            double r = 0.0, g = 0.0, b = 0.0;
            if (j == 0) {
                r = 1.0;
            } else if (j == 1) {
                g = 1.0;
            } else {
                b = 1.0;
            }
            getGUIColor(r, g, b);
            cr->set_source_rgb(r, g, b);
            cr->move_to(0, getVal(0));

            for (int i = 1; i < W; ++i) {
                cr->line_to(i, getVal(i));
            }
            cr->stroke();            
        }
    }
}


void ColorCorrection::lutChanged()
{
    if (listener) {
        auto fn = Glib::filename_to_utf8(lut_filename->get_filename());

        bool lut_is_builtin = builtin_lut_to_idx.find(fn) != builtin_lut_to_idx.end();
        lut_params->setParams(rtengine::CLUTApplication::get_param_descriptors(fn));
        lut_params->setValue({});

        if (lut_is_builtin) {
            listener->panelChanged(EvMode, mode->get_active_text());
        } else {
            listener->panelChanged(EvLUT, Glib::path_get_basename(fn));
        }
    }
}


void ColorCorrection::lutParamsChanged()
{
    if (listener) {
        listener->panelChanged(EvLUTParams, M("GENERAL_CHANGED"));
    }
}
