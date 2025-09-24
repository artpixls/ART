/* -*- C++ -*-
 *
 *  This file is part of RawTherapee.
 *
 *  Copyright 2018 Alberto Griggio <alberto.griggio@gmail.com>
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

#include "maskspanel.h"
#include "eventmapper.h"
#include "../rtengine/iccstore.h"

#include <iostream>
#include <memory>
#include <unordered_map>

namespace {

using Shape = rtengine::procparams::AreaMask::Shape;

constexpr int ID_HUE_MASK = 5;

inline bool hasMask(const rtengine::procparams::Mask &m, const std::vector<double> &dflt, const std::vector<double> &mask)
{
    return m.parametricMask.enabled && !(mask.empty() || mask[0] == FCT_Linear || mask == dflt);
}


class DeltaEArea: public Gtk::DrawingArea, public BackBuffer, public EditSubscriber {
public:
    DeltaEArea():
        Gtk::DrawingArea(),
        EditSubscriber(ET_OBJECTS),
        L_(0.f), C_(0.f), H_(0.f)
    {
        // Editing geometry; create the spot rectangle
        Rectangle *spotRect = new Rectangle();
        spotRect->filled = false;
    
        visibleGeometry.push_back(spotRect);

        // Stick a dummy rectangle over the whole image in mouseOverGeometry.
        // This is to make sure the getCursor() call is fired everywhere.
        Rectangle *imgRect = new Rectangle();
        imgRect->filled = true;
        mouseOverGeometry.push_back(imgRect);
    }

    ~DeltaEArea()
    {
        for (auto geometry : visibleGeometry) {
            delete geometry;
        }

        for (auto geometry : mouseOverGeometry) {
            delete geometry;
        }        
    }

    void setColor(float L, float C, float H)
    {
        L_ = L;
        C_ = C;
        H_ = H;
        setDirty(true);
        queue_draw();
    }

    bool on_draw(const ::Cairo::RefPtr<Cairo::Context> &crf) override
    {
        Gtk::Allocation allocation = get_allocation();
        allocation.set_x(0);
        allocation.set_y(0);

        // setDrawRectangle will allocate the backbuffer Surface
        if (setDrawRectangle(Cairo::FORMAT_ARGB32, allocation)) {
            setDirty(true);
        }

        if (!isDirty() || !surfaceCreated()) {
            return true;
        }

        Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
        Gtk::Border padding = getPadding(style);  // already scaled
        Cairo::RefPtr<Cairo::Context> cr = getContext();

        if (isDirty()) {
            float h = H_ * 2.f * rtengine::RT_PI / 360.f;
            float a = C_ * std::cos(h);
            float b = C_ * std::sin(h);
            float R, G, B;
            auto iws = rtengine::ICCStore::getInstance()->workingSpaceInverseMatrix("sRGB");
            rtengine::Color::lab2rgb(L_ * 32768.f, a * 42000.f, b * 42000.f, R, G, B, iws);
            cr->set_line_cap(Cairo::LINE_CAP_SQUARE);

            const auto c = [](float v) { return std::pow(v / 65535.f, 1.f/2.2f); };
            double rr = c(R), gg = c(G), bb = c(B);
            getGUIColor(rr, gg, bb);
            cr->set_source_rgb(rr, gg, bb);
            cr->set_operator(Cairo::OPERATOR_OVER);
            cr->paint();
        }

        copySurface(crf);
        return false;
    }

    void on_style_updated() override
    {
        setDirty(true);
        queue_draw();
    }
    
    Gtk::SizeRequestMode get_request_mode_vfunc() const override
    {
        return Gtk::SIZE_REQUEST_HEIGHT_FOR_WIDTH;
    }
        
    void get_preferred_width_vfunc(int &minimum_width, int &natural_width) const override
    {
        Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
        Gtk::Border padding = getPadding(style);  // already scaled
        int s = RTScalable::getScale();
        int p = padding.get_left() + padding.get_right();

        minimum_width = 20 * s + p;
        natural_width = 50 * s + p;
    }
    
    void get_preferred_height_for_width_vfunc(int width, int &minimum_height, int &natural_height) const override
    {
        Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
        Gtk::Border padding = getPadding(style);  // already scaled

        minimum_height = natural_height = width - padding.get_left() - padding.get_right() + padding.get_top() + padding.get_bottom();
    }

    // EditSubscriber interface
    CursorShape getCursor(int objectID) override
    {
        return CSSpotWB;
    }
    
    bool mouseOver(int modifierKey) override
    {
        auto provider = getEditProvider();
        Rectangle *spotRect = static_cast<Rectangle *>(visibleGeometry.at(0));
        spotRect->setXYWH(provider->posImage.x - 16, provider->posImage.y - 16, 32, 32);

        return true;
    }
    
    bool button1Pressed(int modifierKey) override
    { 
        auto provider = getEditProvider();
        EditSubscriber::action = ES_ACTION_NONE;
        sig_spot_requested_.emit(provider->posImage);
        //switchOffEditMode();
        return true;
    }
    
    bool button1Released() override
    {
        EditSubscriber::action = ES_ACTION_NONE;
        return true;
    }
    
    typedef sigc::signal<void, rtengine::Coord> SigSpotRequested;
    SigSpotRequested signal_spot_requested() { return sig_spot_requested_; }

    void activateSpot()
    {
        subscribe();

        int w, h;
        getEditProvider()->getImageSize(w, h);

        // Stick a dummy rectangle over the whole image in mouseOverGeometry.
        // This is to make sure the getCursor() call is fired everywhere.
        Rectangle *imgRect = static_cast<Rectangle *>(mouseOverGeometry.at(0));
        imgRect->setXYWH(0, 0, w, h);
    }

private:
    void on_hide() override
    {
        if (isCurrentSubscriber()) {
            switchOffEditMode();
        }
    }

    float L_;
    float C_;
    float H_;

    SigSpotRequested sig_spot_requested_;
};


class DrawnMaskPanel: public MyExpander, public EditSubscriber, public AdjusterListener, public CurveListener {
private:
    enum PressureMode {
        PRESSURE_OFF,
        PRESSURE_HARDNESS,
        PRESSURE_RADIUS
    };

    class BrushPreview: public Rectangle {
    public:
        BrushPreview(): Rectangle()
        {
            image_w = 0;
            image_h = 0;
            filled = true;
        }

        void drawOuterGeometry(Cairo::RefPtr<Cairo::Context> &cr, ObjectMOBuffer *objectBuffer, EditCoordSystem &coordSystem) override
        {
        }
        
        void drawToMOChannel(Cairo::RefPtr<Cairo::Context> &cr, unsigned short id, ObjectMOBuffer *objectBuffer, EditCoordSystem &coordSystem) override
        {
        }
        
        void drawInnerGeometry(Cairo::RefPtr<Cairo::Context> &cr, ObjectMOBuffer *objectBuffer, EditCoordSystem &coordSystem) override
        {
            if (!strokes.empty()) {
                flags = F_AUTO_COLOR;
                state = PRELIGHT;
                RGBColor c = getInnerLineColor();
                double rr = c.getR(), gg = c.getG(), bb = c.getB();
                getGUIColor(rr, gg, bb);
                cr->set_source_rgb(rr, gg, bb);

                double r = std::min(image_w, image_h) * 0.25;

                
                for (auto &s : strokes) {
                    double cx, cy;
                    double radius = coordSystem.scaleValueToCanvas(s.radius * r);
                    coordSystem.imageCoordToScreen(s.x * image_w, s.y * image_h, cx, cy);
                    cr->arc(cx + 0.5, cy + 0.5, radius, 0., 2.*rtengine::RT_PI);
                    cr->fill();
                }
            }
        }

        std::vector<rtengine::procparams::DrawnMask::Stroke> strokes;
        int image_w;
        int image_h;
    };
    
public:
    DrawnMaskPanel():
        MyExpander(true, M("TP_LABMASKS_DRAWNMASK")),
        EditSubscriber(ET_OBJECTS),
        prev_erase_(false),
        pressure_mode_(PRESSURE_OFF),
        cur_pressure_(rtengine::RT_NAN)
    {
        brush_preview_ = new BrushPreview();
        
        // Editing geometry; create the spot rectangle
        pen_ = new Circle();
        pen_->radiusInImageSpace = true;
        pen_->filled = false;

        visibleGeometry.push_back(brush_preview_);
        visibleGeometry.push_back(pen_);

        const auto set_btn_style =
            [](Gtk::Button *w) -> void
            {
                w->set_relief(Gtk::RELIEF_NONE);
                w->get_style_context()->add_class(GTK_STYLE_CLASS_FLAT);
                //w->set_can_focus(false);
            };

        auto tb = Gtk::manage(new ToolParamBlock());
        add(*tb, false);
        setLevel(1);

        Gtk::VBox *vbg = Gtk::manage(new Gtk::VBox());
        Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());

        Gtk::Button *btncopy = Gtk::manage(new Gtk::Button());
        btncopy->add(*Gtk::manage(new RTImage("copy.png")));
        btncopy->set_tooltip_text(M("TP_LABMASKS_AREA_MASK_COPY_TOOLTIP"));
        btncopy->signal_clicked().connect(sigc::mem_fun(*this, &DrawnMaskPanel::on_copy_pressed));
        set_btn_style(btncopy);
        hb->pack_start(*btncopy, Gtk::PACK_SHRINK);

        Gtk::Button *btnpaste = Gtk::manage(new Gtk::Button());
        btnpaste->add(*Gtk::manage(new RTImage("paste.png")));
        btnpaste->set_tooltip_text(M("TP_LABMASKS_AREA_MASK_PASTE_TOOLTIP"));
        btnpaste->signal_clicked().connect(sigc::mem_fun(*this, &DrawnMaskPanel::on_paste_pressed));
        set_btn_style(btnpaste);
        hb->pack_start(*btnpaste, Gtk::PACK_SHRINK);

        info_ = Gtk::manage(new Gtk::Label(M("TP_LABMASKS_DRAWNMASK_INFO")));
        hb->pack_start(*info_, Gtk::PACK_EXPAND_WIDGET, 4);
        
        const char *img[3] = {
            "area-shape-intersect.png",
            "area-shape-add.png",
            "drawn-mask-add-bounded.png"
        };
        const char *tips[3] = {
            "TP_LABMASKS_DRAWNMASK_INTERSECT_TOOLTIP",
            "TP_LABMASKS_DRAWNMASK_ADD_TOOLTIP",
            "TP_LABMASKS_DRAWNMASK_ADD_BOUNDED_TOOLTIP"
        };
        for (int i = 0; i < 3; ++i) {
            mode_[i] = Gtk::manage(new Gtk::ToggleButton());
            mode_[i]->add(*Gtk::manage(new RTImage(img[i])));
            modeconn_[i] = mode_[i]->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &DrawnMaskPanel::on_mode_changed), i));
            set_btn_style(mode_[i]);
            mode_[i]->set_tooltip_text(M(tips[i]));
        }

        toggle_ = Gtk::manage(new Gtk::ToggleButton());
        toggle_->add(*Gtk::manage(new RTImage("brush.png")));
        set_btn_style(toggle_);
        toggle_->set_tooltip_markup(M("TP_LABMASKS_DRAWNMASK_TOGGLE_TIP"));
        toggle_->signal_toggled().connect(sigc::mem_fun(this, &DrawnMaskPanel::on_toggled));

        reset_ = Gtk::manage(new Gtk::Button());
        reset_->add(*Gtk::manage(new RTImage("undo-all.png")));
        set_btn_style(reset_);
        reset_->set_tooltip_text(M("TP_LABMASKS_DRAWNMASK_RESET_TIP"));
        reset_->signal_clicked().connect(sigc::mem_fun(this, &DrawnMaskPanel::on_reset));

        undo_ = Gtk::manage(new Gtk::Button());
        undo_->add(*Gtk::manage(new RTImage("undo.png")));
        set_btn_style(undo_);
        undo_->set_tooltip_text(M("TP_LABMASKS_DRAWNMASK_UNDO_TIP"));
        undo_->signal_clicked().connect(sigc::mem_fun(this, &DrawnMaskPanel::on_undo));
        
        vbg->pack_start(*hb);
        hb = Gtk::manage(new Gtk::HBox());
        
        Gtk::Frame *f = Gtk::manage(new Gtk::Frame(M("TP_LABMASKS_DRAWNMASK_PEN_SETTINGS")));
        Gtk::VBox *vb = Gtk::manage(new Gtk::VBox());
        f->add(*vb);

        radius_ = Gtk::manage(new Adjuster(M("TP_LABMASKS_DRAWNMASK_RADIUS"), 0.1, 100, 0.1, 10, Gtk::manage(new RTImage("pen-ocra-small.png"))));
        radius_->setLogScale(2, 0);
        vb->pack_start(*radius_);

        hardness_ = Gtk::manage(new Adjuster(M("TP_LABMASKS_DRAWNMASK_OPACITY"), 0, 100, 1, 100, Gtk::manage(new RTImage("pen-ocra-small.png"))));
        vb->pack_start(*hardness_);

        erase_ = Gtk::manage(new Gtk::CheckButton(M("TP_LABMASKS_DRAWNMASK_ERASE")));
        vb->pack_start(*erase_);

        vbg->pack_start(*f);
        hb->pack_start(*vbg);

        Gtk::Grid *grid = Gtk::manage(new Gtk::Grid());
        setExpandAlignProperties(grid, false, true, Gtk::ALIGN_CENTER, Gtk::ALIGN_CENTER);
        grid->attach(*reset_, 0, 1, 1, 1);
        grid->attach(*undo_, 0, 2, 1, 1);
        grid->attach(*toggle_, 0, 3, 1, 1);
        grid->attach(*mode_[0], 0, 4, 1, 1);
        grid->attach(*mode_[1], 0, 5, 1, 1);
        grid->attach(*mode_[2], 0, 6, 1, 1);
        hb->pack_start(*grid, Gtk::PACK_SHRINK, 2);
        
        tb->pack_start(*hb);

        auto glbl = Gtk::manage(new Gtk::Label(M("TP_LABMASKS_DRAWNMASK_GLOBAL")));
        setExpandAlignProperties(glbl, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
        tb->pack_start(*glbl);
        tb->pack_start(*Gtk::manage(new Gtk::HSeparator()));
        
        feather_ = Gtk::manage(new Adjuster(M("TP_LABMASKS_AREA_FEATHER"), 0, 100, 0.1, 0));
        tb->pack_start(*feather_);
        feather_->setAdjusterListener(this);
        feather_->setLogScale(10.0, 0.0);

        opacity_ = Gtk::manage(new Adjuster(M("TP_LABMASKS_DRAWNMASK_OPACITY"), 0, 100, 1, 0));
        tb->pack_start(*opacity_);
        opacity_->setAdjusterListener(this);

        smoothness_ = Gtk::manage(new Adjuster(M("TP_LABMASKS_DRAWNMASK_SMOOTHNESS"), 0, 100, 1, 0));
        tb->pack_start(*smoothness_);
        smoothness_->setAdjusterListener(this);
        smoothness_->setLogScale(10.0, 0.0);

        CurveEditorGroup *cg = Gtk::manage(new CurveEditorGroup(options.lastToneCurvesDir, M("TP_LABMASKS_AREA_CONTRAST")));
        cg->setCurveListener(this);

        contrast_ = static_cast<DiagonalCurveEditor *>(cg->addCurve(CT_Diagonal, ""));
        cg->curveListComplete();
        tb->pack_start(*cg, Gtk::PACK_SHRINK, 2);
        
        signal_enabled_toggled().connect(sigc::mem_fun(*this, &DrawnMaskPanel::on_enabled_toggled));

        tb->signal_unmap().connect(sigc::mem_fun(*this, &DrawnMaskPanel::on_hide));

        add_events(Gdk::KEY_PRESS_MASK);
        const auto keypress =
            [this](GdkEventKey *evt) -> bool
            {
                bool ctrl = evt->state & GDK_CONTROL_MASK;
                bool shift = evt->state & GDK_SHIFT_MASK;
                bool alt = evt->state & GDK_MOD1_MASK;

                if (ctrl && !alt) {
                    switch (evt->keyval) {
                    case GDK_KEY_z:
                    case GDK_KEY_Z:
                        if (shift) {
                            on_reset();
                        } else {
                            on_undo();
                        }
                        return true;
                    }
                }
                return false;
            };
        signal_key_press_event().connect(sigc::slot<bool, GdkEventKey *>(keypress), false);
    }

    ~DrawnMaskPanel()
    {
        brush_preview_clear_conn_.disconnect();
        for (auto geometry : visibleGeometry) {
            delete geometry;
        }
    }

    void adjusterChanged(Adjuster *a, double newval) override
    {
        if (mask_) {
            mask_->feather = feather_->getValue();
            mask_->opacity = rtengine::LIM01(opacity_->getValue() / 100.0);
            mask_->smoothness = smoothness_->getValue() / 100.0;
            sig_draw_updated_.emit();
        }
    }

    void curveChanged() override
    {
        if (mask_) {
            mask_->contrast = contrast_->getCurve();
            sig_draw_updated_.emit();
        }
    }

    void adjusterAutoToggled(Adjuster *a, bool newval) override {}

    // EditSubscriber interface
    CursorShape getCursor(int objectID) override
    {
        return CSEmpty;
    }
    
    bool mouseOver(int modifierKey) override
    {
        if (pressure_mode_ == PRESSURE_OFF) {
            bool ctrl = modifierKey & GDK_CONTROL_MASK;
            bool alt = modifierKey & GDK_MOD1_MASK;            
            if ((!ctrl && alt) != prev_erase_) {
                prev_erase_ = (!ctrl && alt);
                erase_->set_active(!erase_->get_active());
            }
        }
        update_pen(false);
        return true;
    }
    
    bool button1Pressed(int modifierKey, double pressure) override
    {
        if (std::isnan(pressure) && cur_pressure_ != PRESSURE_OFF) {
            update_pressure(PRESSURE_OFF);
        }
        begin_update_strokes();
        cur_pressure_ = pressure;
        EditSubscriber::action = ES_ACTION_DRAGGING;
        undo_stack_.push_back(mask_->strokes.size());
        bool ctrl = modifierKey & GDK_CONTROL_MASK;
        bool shift = modifierKey & GDK_SHIFT_MASK;
        bool alt = modifierKey & GDK_MOD1_MASK;
        bool dragging = shift && !mask_->strokes.empty();
        if (ctrl && !shift) {
            mask_->strokes.push_back(rtengine::procparams::DrawnMask::Stroke());
            dragging = false;
        } else if ((!ctrl && alt) != prev_erase_) {
            prev_erase_ = !ctrl && alt;
            erase_->set_active(!erase_->get_active());
            dragging = false;
        }
        add_stroke(dragging);
        return true;
    }

    bool drag1(int modifierKey, double pressure) override
    {
        if (std::isnan(pressure) && pressure_mode_ != PRESSURE_OFF) {
            update_pressure(PRESSURE_OFF);
        }
        cur_pressure_ = pressure;
        add_stroke(true);
        update_pen(true);
        return true;
    }
    
    bool button1Released() override
    {
        EditSubscriber::action = ES_ACTION_NONE;
        end_update_strokes();
        sig_draw_updated_.emit();
        return true;
    }

    bool button2Pressed(int modifierKey, double pressure) override
    {
        cur_pressure_ = pressure;
        bool ctrl = modifierKey & GDK_CONTROL_MASK;
        bool alt = modifierKey & GDK_MOD1_MASK;
        if (std::isnan(pressure)) {
            update_pressure(PRESSURE_OFF);
        } else if (!ctrl) {
            if (alt) {
                update_brush(true, false, 3);
            } else {
                erase_->set_active(!erase_->get_active());
            }
            EditSubscriber::action = ES_ACTION_PICKING;
        }
        return false;
    }

    bool button3Pressed(int modifierKey, double pressure) override
    {
        cur_pressure_ = pressure;
        bool ctrl = modifierKey & GDK_CONTROL_MASK;
        bool alt = modifierKey & GDK_MOD1_MASK;
        bool shift = modifierKey & GDK_SHIFT_MASK;
        if (!std::isnan(pressure) && !ctrl) {
            if (alt) {
                update_brush(true, false, -3);
            } else {
                update_pressure(PressureMode((int(pressure_mode_) + (shift ? -1 : 1)) % 3));
            }
            EditSubscriber::action = ES_ACTION_PICKING;
        } else {
            switchOffEditMode();
        }
        return false;
    }

    bool button2Released() override
    {
        EditSubscriber::action = ES_ACTION_NONE;
        return false;
    }

    bool button3Released() override
    {
        EditSubscriber::action = ES_ACTION_NONE;
        return false;
    }

    bool scroll(int bstate, GdkScrollDirection direction, double deltaX, double deltaY, bool &propagateEvent) override
    {
        double delta = (fabs(deltaX) > fabs(deltaY)) ? deltaX : deltaY;
        bool isUp = direction == GDK_SCROLL_UP || (direction == GDK_SCROLL_SMOOTH && delta < 0.0);
        int incr = isUp ? 1 : -1;

        bool ctrl = bstate & GDK_CONTROL_MASK;
        bool shift = bstate & GDK_SHIFT_MASK;

        if (update_brush(ctrl, shift, incr)) {
            propagateEvent = false;
            return true;
        } else {
            propagateEvent = true;
            return false;
        }
    }

    void switchOffEditMode() override
    {
        toggle_->set_active(false);
        update_pressure(PRESSURE_OFF);
    }

    void setTargetMask(rtengine::procparams::DrawnMask *mask, bool force=false)
    {
        if (mask != mask_ || force) {
            mask_ = nullptr;
            undo_stack_.clear();
            if (mask) {
                if (toggle_->get_active()) {
                    toggle_->set_active(false);
                    unsubscribe();
                }
                info_->set_markup(Glib::ustring::compose(M("TP_LABMASKS_DRAWNMASK_INFO"), mask->strokes.size()));
                setEnabled(mask->enabled);
                feather_->setValue(mask->feather);
                opacity_->setValue(rtengine::LIM01(mask->opacity) * 100.0);
                smoothness_->setValue(mask->smoothness * 100.0);
                contrast_->setCurve(mask->contrast);
                set_mode(int(mask->mode));
            }
            mask_ = mask;
            update_pressure(PRESSURE_OFF);
        }
    }

    typedef sigc::signal<void> SigDrawUpdated;
    SigDrawUpdated signal_draw_updated() { return sig_draw_updated_; }

private:
    void update_pressure(PressureMode m)
    {
        if (pressure_mode_ == m) {
            pressure_mode_ = PRESSURE_OFF;
        } else {
            pressure_mode_ = m;
        }
        radius_->setEnabled(true);
        hardness_->setEnabled(true);
        radius_->showIcons(false);
        hardness_->showIcons(false);
        switch (pressure_mode_) {
        case PRESSURE_RADIUS:
            radius_->setEnabled(false);
            radius_->showIcons(true);
            break;
        case PRESSURE_HARDNESS:
            hardness_->setEnabled(false);
            hardness_->showIcons(true);
            break;
        default:
            break;
        }
    }
    
    bool update_brush(bool ctrl, bool shift, int incr)
    {
        if (ctrl && !shift) {
            radius_->setValue(std::max(radius_->getValue() + incr, 0.0));
            update_pen(false);
            return true;
        } else if (ctrl && shift) {
            hardness_->setValue(std::max(hardness_->getValue() + incr, 0.0));
            return true;
        }
        return false;
    }
    
    void on_toggled()
    {
        if (mask_ && getEnabled() && getEditProvider()) {
            if (toggle_->get_active()) {
                auto p = getEditProvider();
                if (p && p->getCurrSubscriber()) {
                    p->getCurrSubscriber()->switchOffEditMode();
                }
                subscribe();
            } else {
                unsubscribe();
            }
        }
    }

    void on_reset()
    {
        if (mask_) {
            mask_->strokes.clear();
            info_->set_markup(Glib::ustring::compose(M("TP_LABMASKS_DRAWNMASK_INFO"), mask_->strokes.size()));
            sig_draw_updated_.emit();
        }
    }

    void on_enabled_toggled()
    {
        if (mask_) {
            mask_->enabled = getEnabled();
            if (!getEnabled()) {
                toggle_->set_active(false);
            }
            sig_draw_updated_.emit();
        }
    }

    void on_undo()
    {
        if (mask_ && !undo_stack_.empty()) {
            size_t n = undo_stack_.back();
            undo_stack_.pop_back();
            mask_->strokes.resize(n);
            info_->set_markup(Glib::ustring::compose(M("TP_LABMASKS_DRAWNMASK_INFO"), n));
            sig_draw_updated_.emit();
        }
    }

    void on_hide() override
    {
        if (isCurrentSubscriber()) {
            switchOffEditMode();
        }
    }
    
    void add_stroke(bool dragging)
    {
        auto provider = getEditProvider();
        int w, h;
        provider->getImageSize(w, h);
        rtengine::Coord p = provider->posImage;
        if (dragging) {
            p += provider->deltaImage;
        }
        double x = double(p.x) / double(w);
        double y = double(p.y) / double(h);
        double radius = 0;
        if (pressure_mode_ == PRESSURE_RADIUS) {
            radius = rtengine::SQR(rtengine::LIM01(int(cur_pressure_ * 100.0) / 100.0));
        } else {
            radius = radius_->getValue() / 100.0;
        }
        double hardness = 0;
        if (pressure_mode_ == PRESSURE_HARDNESS) {
            hardness = rtengine::LIM01(int(cur_pressure_ * 100.0) / 75.0);
        } else {
            hardness = hardness_->getValue() / 100.0;
        }
        bool erase = erase_->get_active();

        if (dragging) {
            auto prev = mask_->strokes.back();
            double dx = x - prev.x;
            double dy = y - prev.y;
            double dr = radius - prev.radius;
            double distance = std::sqrt(rtengine::SQR(dx * w) + rtengine::SQR(dy * h));
            double delta = std::min(radius, prev.radius) * std::min(w, h) * 0.25 * 0.3;
            if (delta > 0.0) {
                int steps = distance / delta + 0.5;
                for (int i = 1; i < steps; ++i) {
                    mask_->strokes.push_back(rtengine::procparams::DrawnMask::Stroke());
                    auto &s = mask_->strokes.back();
                    s.x = prev.x + (dx / steps) * i;
                    s.y = prev.y + (dy / steps) * i;
                    s.radius = prev.radius + (dr / steps) * i;
                    s.opacity = hardness;
                    s.erase = erase;

                    brush_preview_->strokes.push_back(s);
                }
            }
        }
        
        mask_->strokes.push_back(rtengine::procparams::DrawnMask::Stroke());
        auto &s = mask_->strokes.back();
        s.x = x;
        s.y = y;
        s.radius = radius;
        s.opacity = hardness;
        s.erase = erase;
        info_->set_markup(Glib::ustring::compose(M("TP_LABMASKS_DRAWNMASK_INFO"), mask_->strokes.size()));

        brush_preview_->strokes.push_back(s);
    }

    void update_pen(bool dragging)
    {
        auto provider = getEditProvider();
        int w, h;
        provider->getImageSize(w, h);
        pen_->center = provider->posImage;
        if (dragging) {
            pen_->center += provider->deltaImage;
        }
        if (pressure_mode_ == PRESSURE_RADIUS) {
            pen_->radius = rtengine::SQR(rtengine::LIM01(int(cur_pressure_ * 100.0) / 100.0)) * std::min(w, h) * 0.25;
        } else {
            pen_->radius = radius_->getValue() / 100.0 * std::min(w, h) * 0.25;
        }
    }

    void set_mode(int i)
    {
        for (int j = 0; j < 3; ++j) {
            ConnectionBlocker blocker(modeconn_[j]);
            mode_[j]->set_active(i == j);
        }
    }

    void on_mode_changed(int i)
    {
        if (!mode_[i]->get_active()) {
            ConnectionBlocker blocker(modeconn_[i]);
            mode_[i]->set_active(true);
            return;
        }
        set_mode(i);
        if (mask_) {
            mask_->mode = mode_[0]->get_active() ? rtengine::procparams::DrawnMask::INTERSECT : (mode_[1]->get_active() ? rtengine::procparams::DrawnMask::ADD : rtengine::procparams::DrawnMask::ADD_BOUNDED);
            sig_draw_updated_.emit();
        }
    }

    void on_copy_pressed()
    {
        if (mask_) {
            clipboard.setDrawnMask(*mask_);
        }
    }

    void on_paste_pressed()
    {
        if (mask_ && clipboard.hasDrawnMask()) {
            *mask_ = clipboard.getDrawnMask();
            undo_stack_.clear();
            info_->set_markup(Glib::ustring::compose(M("TP_LABMASKS_DRAWNMASK_INFO"), mask_->strokes.size()));
            feather_->setValue(mask_->feather);
            opacity_->setValue(rtengine::LIM01(mask_->opacity) * 100.0);
            smoothness_->setValue(mask_->smoothness * 100.0);
            contrast_->setCurve(mask_->contrast);
            set_mode(int(mask_->mode));
            sig_draw_updated_.emit();
        }
    }

    void begin_update_strokes()
    {
        auto provider = getEditProvider();
        if (provider) {
            provider->getImageSize(brush_preview_->image_w, brush_preview_->image_h);
        }
        
        if (pressure_mode_ != PRESSURE_OFF) {
            stroke_idx_ = mask_->strokes.size();
        } else {
            stroke_idx_ = -1;
        }

        if (brush_preview_clear_conn_.connected()) {
            brush_preview_clear_conn_.disconnect();
        }
        brush_preview_->strokes.clear();
    }

    void end_update_strokes()
    {
        double p = cur_pressure_;
        if (stroke_idx_ >= 0) {
            auto &s = mask_->strokes;
            p = 0;
            if (pressure_mode_ == PRESSURE_HARDNESS) {
                for (size_t i = stroke_idx_; i < s.size(); ++i) {
                    p = std::max(p, s[i].opacity);
                }
                for (size_t i = stroke_idx_; i < s.size(); ++i) {
                    s[i].opacity = p;
                }
            } else if (pressure_mode_ == PRESSURE_RADIUS) {
                for (size_t i = stroke_idx_; i < s.size(); ++i) {
                    p += s[i].radius;
                }
                if (!s.empty()) {
                    p /= s.size();
                }
            }
        }
        stroke_idx_ = -1;
        if (!mask_->strokes.empty() && pressure_mode_ != PRESSURE_OFF) {
            if (pressure_mode_ == PRESSURE_RADIUS) {
                radius_->setValue(p * 100.0);
            } else {
                hardness_->setValue(p * 100.0);
            }
        }
        if (pressure_mode_ == PRESSURE_HARDNESS) {
            mask_->strokes.push_back(rtengine::procparams::DrawnMask::Stroke());
        }

        const auto clear_strokes =
            [this]() -> bool
            {
                brush_preview_->strokes.clear();
                return false;
            };
        if (options.adjusterMinDelay > 0) {
            brush_preview_clear_conn_ = Glib::signal_timeout().connect(sigc::slot<bool>(clear_strokes), options.adjusterMinDelay);
        } else {
            clear_strokes();
        }
    }
    
    rtengine::procparams::DrawnMask *mask_;
    Circle *pen_;
    BrushPreview *brush_preview_;
    sigc::connection brush_preview_clear_conn_;
    
    Gtk::ToggleButton *toggle_;
    Gtk::Label *info_;
    Gtk::Button *reset_;
    Gtk::Button *undo_;
    Adjuster *feather_;
    Adjuster *radius_;
    Adjuster *opacity_;
    Adjuster *smoothness_;
    Adjuster *hardness_;
    Gtk::CheckButton *erase_;
    DiagonalCurveEditor *contrast_;
    Gtk::ToggleButton *mode_[3];
    sigc::connection modeconn_[3];

    SigDrawUpdated sig_draw_updated_;
    bool prev_erase_;
    std::vector<size_t> undo_stack_;

    PressureMode pressure_mode_;
    double cur_pressure_;
    int stroke_idx_;
};

bool on_release_event_ignore(GdkEventButton *event)
{
    return true;
}

} // namespace


MasksPanel::MasksPanel(MasksContentProvider *cp):
    Gtk::VBox(),
    cp_(cp),
    masks_(),
    selected_(0),
    area_shape_index_(0),
    listEdited(false),
    adl_(nullptr),
    deltaE_provider_(nullptr)
{
    Gtk::Widget *child = cp_->getWidget();
    MasksContentProvider::Events events;
    cp_->getEvents(events);
    EvMaskList = events.mask_list;
    EvParametricMask = events.parametric_mask;
    EvHMask = events.h_mask;
    EvCMask = events.c_mask;
    EvLMask = events.l_mask;
    EvMaskBlur = events.blur;
    EvShowMask = events.show;
    EvAreaMask = events.area_mask;
    EvDeltaEMask = events.deltaE_mask;
    EvContrastThresholdMask = events.contrastThreshold_mask;
    EvDrawnMask = events.drawn_mask;
    EvMaskPostprocess = events.mask_postprocess;
    EvLinkedMask = events.linked_mask;
    EvExternalMask = events.external_mask;
    EvAreaMaskVoid = ProcEventMapper::getInstance()->newEvent(rtengine::M_VOID, EvAreaMask.get_message());
    EvDeltaEMaskVoid = ProcEventMapper::getInstance()->newEvent(rtengine::M_VOID, EvDeltaEMask.get_message());
    EvMaskName = ProcEventMapper::getInstance()->newEvent(rtengine::M_VOID, "HISTORY_MSG_LABMASKS_MASK_NAME");
    
    CurveListener::setMulti(true);
    
    const auto add_button =
        [](Gtk::Button *btn, Gtk::Box *box, int h=20, Gtk::PackType type=Gtk::PackType::PACK_START, int border=0) -> void
        {
            setExpandAlignProperties(btn, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_START);
            btn->set_relief(Gtk::RELIEF_NONE);
            btn->get_style_context()->add_class(GTK_STYLE_CLASS_FLAT);
            btn->set_can_focus(false);
            if (h > 0) {
                btn->set_size_request(-1, h);
            }
            if (type == Gtk::PackType::PACK_START) {
                box->pack_start(*btn, false, false, border);
            } else {
                box->pack_end(*btn, false, false, border);
            }
        };
    
    int n = cp_->getColumnCount();

    list_model_columns_.reset(new ListColumns(n));
    list_model_ = Gtk::ListStore::create(*list_model_columns_);

    list_enabled_renderer_.property_mode() = Gtk::CELL_RENDERER_MODE_ACTIVATABLE;
    list_enabled_renderer_.signal_toggled().connect(sigc::mem_fun(this, &MasksPanel::onListEnabledToggled));
    list_enabled_column_.pack_start(list_enabled_renderer_);
    list_enabled_column_.set_cell_data_func(list_enabled_renderer_, sigc::mem_fun(this, &MasksPanel::setListEnabled));
    {
        auto img = Gtk::manage(new RTImage("power-on-small-faded.png"));
        img->show();
        list_enabled_column_.set_widget(*img);
    }
    list = Gtk::manage(new Gtk::TreeView());
    list->set_model(list_model_);
    list->set_size_request(-1, 150);
    list->set_can_focus(false);
    list->append_column(list_enabled_column_);
    list->append_column("#", list_model_columns_->id);
    list->append_column(M("TP_LABMASKS_MASK"), list_model_columns_->mask);
    for (int i = 0; i < n; ++i) {
        int col = list->append_column(cp_->getColumnHeader(i), list_model_columns_->cols[i]);
        list->get_column(col-1)->set_expand(true);
    }
    list->set_activate_on_single_click(true);
    setTreeViewCssProvider(list);
    
    selectionConn = list->get_selection()->signal_changed().connect(sigc::mem_fun(this, &MasksPanel::onSelectionChanged));
    Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
    Gtk::ScrolledWindow *scroll = Gtk::manage(new Gtk::ScrolledWindow());
    scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_NEVER);
    scroll->add(*list);
    hb->pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);
    Gtk::VBox *vb = Gtk::manage(new Gtk::VBox());
    reset = Gtk::manage(new Gtk::Button());
    reset->add(*Gtk::manage(new RTImage("undo-small.png")));
    reset->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onResetPressed));
    add_button(reset, vb);
    add = Gtk::manage(new Gtk::Button());
    add->add(*Gtk::manage(new RTImage("add-small.png")));
    add->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onAddPressed));
    add_button(add, vb);
    remove = Gtk::manage(new Gtk::Button());
    remove->add(*Gtk::manage(new RTImage("remove-small.png")));
    remove->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onRemovePressed));
    add_button(remove, vb);
    up = Gtk::manage(new Gtk::Button());
    up->add(*Gtk::manage(new RTImage("arrow-up-small.png")));
    up->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onUpPressed));
    add_button(up, vb);
    down = Gtk::manage(new Gtk::Button());
    down->add(*Gtk::manage(new RTImage("arrow-down-small.png")));
    down->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onDownPressed));
    add_button(down, vb);
    copy = Gtk::manage(new Gtk::Button());
    copy->add(*Gtk::manage(new RTImage("copy-small.png")));
    copy->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onCopyPressed));
    add_button(copy, vb);
    hb->pack_start(*vb, Gtk::PACK_SHRINK);
    pack_start(*hb, true, true);

    pack_start(*Gtk::manage(new Gtk::HSeparator()));
    pack_start(*child);
    pack_start(*Gtk::manage(new Gtk::HSeparator()));

    Gtk::VBox *mask_box = Gtk::manage(new Gtk::VBox());

    hb = Gtk::manage(new Gtk::HBox());

    maskCopy = Gtk::manage(new Gtk::Button());
    maskCopy->add(*Gtk::manage(new RTImage("copy.png")));
    maskCopy->set_tooltip_text(M("TP_LABMASKS_AREA_MASK_COPY_TOOLTIP"));
    maskCopy->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onMaskCopyPressed));
    add_button(maskCopy, hb, 24, Gtk::PackType::PACK_START, 2);
    
    maskPaste = Gtk::manage(new Gtk::Button());
    maskPaste->add(*Gtk::manage(new RTImage("paste.png")));
    maskPaste->set_tooltip_text(M("TP_LABMASKS_AREA_MASK_PASTE_TOOLTIP"));
    maskPaste->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onMaskPastePressed));
    add_button(maskPaste, hb, 24, Gtk::PackType::PACK_START, 2);
    
    showMask = Gtk::manage(new Gtk::CheckButton(M("TP_LABMASKS_SHOW")));
    showMask->signal_toggled().connect(sigc::mem_fun(*this, &MasksPanel::onShowMaskChanged));
    hb->pack_start(*showMask);

    maskInverted = Gtk::manage(new Gtk::CheckButton(M("TP_LABMASKS_INVERTED")));
    maskInverted->signal_toggled().connect(sigc::mem_fun(*this, &MasksPanel::onMaskInvertedChanged));
    hb->pack_start(*maskInverted);
    mask_box->pack_start(*hb);

    maskOpacity = Gtk::manage(new Adjuster(M("TP_LABMASKS_DRAWNMASK_OPACITY"), 0, 100, 1, 0, nullptr, nullptr, nullptr, nullptr, false, true));
    mask_box->pack_start(*maskOpacity);
    maskOpacity->setAdjusterListener(this);

    parametricMask = Gtk::manage(new MyExpander(true, M("TP_LABMASKS_PARAMETRIC")));
    ToolParamBlock *tb = Gtk::manage(new ToolParamBlock());
    parametricMask->add(*tb, false);
    parametricMask->setLevel(1);
    parametricMask->signal_enabled_toggled().connect(sigc::mem_fun(*this, &MasksPanel::onParametricMaskEnableToggled));
        
    maskEditorGroup = Gtk::manage(new CurveEditorGroup(options.lastColorToningCurvesDir, "", 0.7));
    maskEditorGroup->setCurveListener(this);

    rtengine::procparams::Mask default_params;

    EditUniqueID eh, ec, el, ede;
    cp_->getEditIDs(eh, ec, el, ede);

    hueMask = static_cast<FlatCurveEditor *>(maskEditorGroup->addCurve(CT_Flat, M("TP_LABMASKS_HUE"), nullptr, false, true));
    hueMask->setIdentityValue(0.);
    hueMask->setResetCurve(FlatCurveType(default_params.parametricMask.hue[0]), default_params.parametricMask.hue);
    hueMask->setCurveColorProvider(this, ID_HUE_MASK);
    hueMask->setBottomBarColorProvider(this, ID_HUE_MASK);
    hueMask->setEditID(eh, BT_SINGLEPLANE_FLOAT);

    chromaticityMask = static_cast<FlatCurveEditor *>(maskEditorGroup->addCurve(CT_Flat, M("TP_LABMASKS_CHROMATICITY"), nullptr, false, false));
    chromaticityMask->setIdentityValue(0.);
    chromaticityMask->setResetCurve(FlatCurveType(default_params.parametricMask.chromaticity[0]), default_params.parametricMask.chromaticity);
    chromaticityMask->setBottomBarColorProvider(this, ID_HUE_MASK+1);
    chromaticityMask->setEditID(ec, BT_SINGLEPLANE_FLOAT);

    lightnessMask = static_cast<FlatCurveEditor *>(maskEditorGroup->addCurve(CT_Flat, M("TP_LABMASKS_LIGHTNESS"), nullptr, false, false));
    lightnessMask->setIdentityValue(0.);
    lightnessMask->setResetCurve(FlatCurveType(default_params.parametricMask.lightness[0]), default_params.parametricMask.lightness);
    std::vector<GradientMilestone> milestones = {
        GradientMilestone(0., 0., 0., 0.),
        GradientMilestone(1., 1., 1., 1.)
    };
    lightnessMask->setBottomBarBgGradient(milestones);
    lightnessMask->setEditID(el, BT_SINGLEPLANE_FLOAT);

    maskEditorGroup->curveListComplete();
    maskEditorGroup->show();
    tb->pack_start(*maskEditorGroup);//, Gtk::PACK_SHRINK, 4);

    lightnessMaskDetail = Gtk::manage(new Adjuster(M("TP_LABMASKS_LIGHTNESS_DETAIL"), 0, 100, 1, 0));
    lightnessMaskDetail->setAdjusterListener(this);
    tb->pack_start(*lightnessMaskDetail);

    Gtk::Image *cicon = Gtk::manage(new RTImage("one-to-one-small.png"));
    contrastThreshold = Gtk::manage(new Adjuster(M("TP_LABMASKS_CONTRASTTHRESHOLDMASK"), -150, 150, 1, 0, cicon));
    contrastThreshold->setAdjusterListener(this);
    //mask_box->pack_start(*contrastThreshold);
    tb->pack_start(*contrastThreshold);

    maskBlur = Gtk::manage(new Adjuster(M("TP_LABMASKS_BLUR"), -10, 500, 0.1, 0));
    maskBlur->setLogScale(10, -10);
    maskBlur->setAdjusterListener(this);
    //mask_box->pack_start(*maskBlur);
    tb->pack_start(*maskBlur);

    mask_box->pack_start(*parametricMask);
    
    //-------------------------------------------------------------------------
    deltaEMask = Gtk::manage(new MyExpander(true, M("TP_LABMASKS_DELTAE")));
    deltaEMask->getLabelWidget()->set_tooltip_text(M("TP_LABMASKS_DELTAE_TOOLTIP"));
    DeltaEArea *dE_area = Gtk::manage(new DeltaEArea());
    deltaEColor = dE_area;
    const auto deltaAdj =
        [&](const Glib::ustring &lbl, double vmin, double vmax, double vdflt, unsigned int prec, int i) -> ThresholdAdjuster *
        {
            ThresholdAdjuster *a = Gtk::manage(new ThresholdAdjuster(lbl, 0, 100, 100, M("TP_LABMASKS_DELTAE_CHAN_WEIGHT"), 0, vmin, vmax, vdflt, lbl, prec, nullptr, false, true));
            a->setBgColorProvider(this, i);
            a->setAdjusterListener(this);
            a->setUpdatePolicy(RTUP_DYNAMIC);
            return a;
        };
    deltaEL = deltaAdj(M("TP_LABMASKS_DELTAE_L"), 0, 1, 0, 3, ID_HUE_MASK+2);
    deltaEC = deltaAdj(M("TP_LABMASKS_DELTAE_C"), 0, 1, 0, 3, ID_HUE_MASK+3);
    deltaEH = deltaAdj(M("TP_LABMASKS_DELTAE_H"), 0, 360, 0, 1, ID_HUE_MASK+4);
    deltaERange = Gtk::manage(new Adjuster(M("TP_LABMASKS_DELTAE_RANGE"), 0.1, 100, 0.1, 1));
    deltaERange->setLogScale(10.f, 3.f, true);
    deltaERange->setAdjusterListener(this);
    deltaEDecay = Gtk::manage(new Adjuster(M("TP_LABMASKS_DELTAE_DECAY"), 1, 100, 1, 1, nullptr, nullptr, nullptr, nullptr, false, false));//true));
    deltaEDecay->setLogScale(10.f, 10.f, true);
    deltaEDecay->setAdjusterListener(this);
    deltaEStrength = Gtk::manage(new Adjuster(M("TP_SOFTLIGHT_STRENGTH"), 0, 100, 1, 100));
    deltaEStrength->setAdjusterListener(this);
    deltaEInverted = Gtk::manage(new Gtk::CheckButton(M("TP_LABMASKS_INVERTED")));
    vb = Gtk::manage(new Gtk::VBox());
    vb->pack_start(*deltaERange);
    vb->pack_start(*deltaEDecay);
    hb = Gtk::manage(new Gtk::HBox());
    hb->pack_start(*deltaEInverted);
    hb->pack_start(*deltaEStrength);
    vb->pack_start(*hb);
    hb = Gtk::manage(new Gtk::HBox());
    hb->pack_start(*vb);
    vb = Gtk::manage(new Gtk::VBox());
    deltaEPick = Gtk::manage(new Gtk::Button(M("TP_LABMASKS_DELTAE_PICK")));
    vb->pack_start(*deltaEPick, Gtk::PACK_SHRINK);
    vb->pack_start(*deltaEColor, Gtk::PACK_EXPAND_WIDGET, 4);
    hb->pack_start(*vb, Gtk::PACK_SHRINK, 4);
    tb = Gtk::manage(new ToolParamBlock());
    tb->pack_start(*deltaEL);
    tb->pack_start(*deltaEC);
    tb->pack_start(*deltaEH);
    tb->pack_start(*hb);
    deltaEMask->add(*tb, false);
    deltaEMask->setLevel(1);
    mask_box->pack_start(*deltaEMask);
    deltaEMask->signal_enabled_toggled().connect(sigc::mem_fun(*this, &MasksPanel::onDeltaEMaskEnableToggled));
    dE_area->setEditID(ede, BT_SINGLEPLANE_FLOAT);
    dE_area->signal_spot_requested().connect(sigc::mem_fun(*this, &MasksPanel::onDeltaESpotRequested));
    deltaEPick->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onDeltaEPickClicked));
    deltaEInverted->signal_clicked().connect(
        sigc::slot<void>(
            [&]() -> void
            {
                auto l = getListener();
                if (l) {
                    l->panelChanged(deltaEMaskEvent(), M("GENERAL_CHANGED"));
                }
            }));
    //-------------------------------------------------------------------------
        
    areaMask = Gtk::manage(new MyExpander(true, M("TP_LABMASKS_AREA")));
    areaMask->signal_enabled_toggled().connect(sigc::mem_fun(*this, &MasksPanel::onAreaMaskEnableToggled));
    ToolParamBlock *area = Gtk::manage(new ToolParamBlock());
    hb = Gtk::manage(new Gtk::HBox());
    areaMaskButtonsHb = hb;

    areaMaskCopy = Gtk::manage(new Gtk::Button());
    areaMaskCopy->add(*Gtk::manage(new RTImage("copy.png")));
    areaMaskCopy->set_tooltip_text(M("TP_LABMASKS_AREA_MASK_COPY_TOOLTIP"));
    areaMaskCopy->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onAreaMaskCopyPressed));
    add_button(areaMaskCopy, hb, 24);
    
    areaMaskPaste = Gtk::manage(new Gtk::Button());
    areaMaskPaste->add(*Gtk::manage(new RTImage("paste.png")));
    areaMaskPaste->set_tooltip_text(M("TP_LABMASKS_AREA_MASK_PASTE_TOOLTIP"));
    areaMaskPaste->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onAreaMaskPastePressed));
    add_button(areaMaskPaste, hb, 24);

    hb->pack_start(*Gtk::manage(new Gtk::Label("")), Gtk::PACK_EXPAND_WIDGET);

    areaMaskDrawGradientAdd= new Gtk::Button();
    areaMaskDrawGradientAdd->add(*Gtk::manage(new RTImage("area-shape-gradient-add.png")));
    areaMaskDrawGradientAdd->set_tooltip_text(M("TP_LABMASKS_AREA_MASK_DRAW_GRADIENT_ADD_TOOLTIP"));
    areaMaskDrawGradientAdd->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onAreaMaskDrawGradientAddPressed));
    add_button(areaMaskDrawGradientAdd, hb, 24);

    areaMaskDrawPolygonAdd= new Gtk::Button();
    areaMaskDrawPolygonAdd->add(*Gtk::manage(new RTImage("area-shape-polygon-add.png")));
    areaMaskDrawPolygonAdd->set_tooltip_text(M("TP_LABMASKS_AREA_MASK_DRAW_POLYGON_ADD_TOOLTIP"));
    areaMaskDrawPolygonAdd->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onAreaMaskDrawPolygonAddPressed));
    add_button(areaMaskDrawPolygonAdd, hb, 24);

    areaMaskDrawRectangleAdd = new Gtk::Button();
    areaMaskDrawRectangleAdd->add(*Gtk::manage(new RTImage("area-shape-draw-add.png")));
    areaMaskDrawRectangleAdd->set_tooltip_text(M("TP_LABMASKS_AREA_MASK_DRAW_ADD_TOOLTIP"));
    areaMaskDrawRectangleAdd->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onAreaMaskDrawRectangleAddPressed));
    add_button(areaMaskDrawRectangleAdd, hb, 24);

    // areaMaskDrawRectangle = new Gtk::ToggleButton();
    // areaMaskDrawRectangle->add(*Gtk::manage(new RTImage("area-shape-draw.png")));
    // areaMaskDrawRectangle->set_tooltip_text(M("TP_LABMASKS_AREA_MASK_DRAW_TOOLTIP"));
    // areaMaskDrawConn = areaMaskDrawRectangle->signal_toggled().connect(sigc::mem_fun(*this, &MasksPanel::onRectangleAreaMaskDrawChanged));
    // add_button(areaMaskDrawRectangle, hb, 24);
    
    areaMaskToggle = new Gtk::ToggleButton();
    areaMaskToggle->add(*Gtk::manage(new RTImage("crosshair-adjust.png")));
    areaMaskToggle->set_tooltip_text(M("TP_LABMASKS_AREA_MASK_TOGGLE_TOOLTIP"));
    areaMaskToggle->signal_toggled().connect(sigc::mem_fun(*this, &MasksPanel::onAreaMaskToggleChanged));
    add_button(areaMaskToggle, hb, 24);
    area->pack_start(*hb);

    const auto add_adjuster =
        [&](Adjuster *a, Gtk::Box *b) -> void
        {
            areaMaskAdjusters.push_back(a);
            a->setAdjusterListener(this);
            if (b) {
                b->pack_start(*a);
            }
        };
    
    areaMaskFeather = Gtk::manage(new Adjuster(M("TP_LABMASKS_AREA_FEATHER"), 0, 100, 0.1, 0));
    add_adjuster(areaMaskFeather, area);
    areaMaskFeather->setLogScale(10.0, 0.0);

    areaMaskBlur = Gtk::manage(new Adjuster(M("TP_LABMASKS_BLUR"), 0, 500, 0.1, 0));
    add_adjuster(areaMaskBlur, area);
    areaMaskBlur->setLogScale(10.0, 0.0);
    
    CurveEditorGroup *cg = Gtk::manage(new CurveEditorGroup(options.lastToneCurvesDir, M("TP_LABMASKS_AREA_CONTRAST")));
    cg->setCurveListener(this);

    areaMaskContrast = static_cast<DiagonalCurveEditor *>(cg->addCurve(CT_Diagonal, ""));
    cg->curveListComplete();
    if (area) {
        area->pack_start(*cg, Gtk::PACK_SHRINK, 2);
    }
    
    Gtk::Frame *areaFrame = Gtk::manage(new Gtk::Frame(M("TP_LABMASKS_AREA_SHAPES")));
    areaFrame->set_label_align(0.025, 0.5);
    areaFrame->set_border_width(4);

    areaMaskShapes = Gtk::manage(new Gtk::ListViewText(2));
    areaMaskShapes->set_size_request(-1, 80);
    areaMaskShapes->set_can_focus(false);
    areaMaskShapes->set_column_title(0, "#");
    areaMaskShapes->set_column_title(1, M("TP_LABMASKS_AREA_SHAPE"));
    areaMaskShapes->set_activate_on_single_click(true);
    shapeSelectionConn = areaMaskShapes->get_selection()->signal_changed().connect(sigc::mem_fun(this, &MasksPanel::onAreaShapeSelectionChanged));
    hb = Gtk::manage(new Gtk::HBox());
    scroll = Gtk::manage(new Gtk::ScrolledWindow());
    scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_NEVER);
    scroll->add(*areaMaskShapes);
    hb->pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);
    vb = Gtk::manage(new Gtk::VBox());
    areaMaskReset = Gtk::manage(new Gtk::Button());
    areaMaskReset->add(*Gtk::manage(new RTImage("undo-small.png")));
    areaMaskReset->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onAreaShapeResetPressed));
    add_button(areaMaskReset, vb);
    areaMaskAdd = Gtk::manage(new Gtk::Button());
    areaMaskAdd->add(*Gtk::manage(new RTImage("add-small.png")));
    areaMaskAdd->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onAreaShapeAddPressed));
    add_button(areaMaskAdd, vb);
    areaMaskRemove = Gtk::manage(new Gtk::Button());
    areaMaskRemove->add(*Gtk::manage(new RTImage("remove-small.png")));
    areaMaskRemove->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onAreaShapeRemovePressed));
    add_button(areaMaskRemove, vb);
    areaMaskUp = Gtk::manage(new Gtk::Button());
    areaMaskUp->add(*Gtk::manage(new RTImage("arrow-up-small.png")));
    areaMaskUp->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &MasksPanel::onAreaShapeUpDownPressed), true));
    add_button(areaMaskUp, vb);
    areaMaskDown = Gtk::manage(new Gtk::Button());
    areaMaskDown->add(*Gtk::manage(new RTImage("arrow-down-small.png")));
    areaMaskDown->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &MasksPanel::onAreaShapeUpDownPressed), false));
    add_button(areaMaskDown, vb);
    hb->pack_start(*vb, Gtk::PACK_SHRINK);
    
    vb = Gtk::manage(new Gtk::VBox());
    vb->set_spacing(2);
    vb->pack_start(*hb);

    hb = Gtk::manage(new Gtk::HBox());
    hb->pack_start(*Gtk::manage(new Gtk::Label(M("TP_LABMASKS_AREA_SHAPE_MODE") + ":")), Gtk::PACK_SHRINK, 4);

    const char *img[3] = {
        "area-shape-add.png",
        "area-shape-subtract.png",
        "area-shape-intersect.png"
    };
    const char *tips[3] = {
        "TP_LABMASKS_AREA_SHAPE_MODE_ADD",
        "TP_LABMASKS_AREA_SHAPE_MODE_SUBTRACT",
        "TP_LABMASKS_AREA_SHAPE_MODE_INTERSECT"
    };
    for (int i = 2; i >= 0; --i) {
        areaMaskMode[i] = Gtk::manage(new Gtk::ToggleButton());
        areaMaskMode[i]->add(*Gtk::manage(new RTImage(img[i])));
        areaMaskMode[i]->set_tooltip_text(M(tips[i]));
        areaMaskModeConn[i] = areaMaskMode[i]->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &MasksPanel::onAreaShapeModeChanged), i));
        add_button(areaMaskMode[i], hb, 24, Gtk::PackType::PACK_END);
    }

    vb->pack_start(*hb);
    
    areaMaskRoundness = Gtk::manage(new Adjuster(M("TP_LABMASKS_AREA_ROUNDNESS"), 0, 100, 0.1, 0));
    add_adjuster(areaMaskRoundness, vb);
    areaMaskX = Gtk::manage(new Adjuster(M("TP_LABMASKS_AREA_X"), -100, 100, 0.1, 0));
    add_adjuster(areaMaskX, vb);
    areaMaskY = Gtk::manage(new Adjuster(M("TP_LABMASKS_AREA_Y"), -100, 100, 0.1, 0));
    add_adjuster(areaMaskY, vb);
    areaMaskWidth = Gtk::manage(new Adjuster(M("TP_LABMASKS_AREA_WIDTH"), 1, 200, 0.1, 100));
    areaMaskWidth->setLogScale(10, 1);
    add_adjuster(areaMaskWidth, vb);
    areaMaskHeight = Gtk::manage(new Adjuster(M("TP_LABMASKS_AREA_HEIGHT"), 1, 200, 0.1, 100));
    areaMaskHeight->setLogScale(10, 1);
    add_adjuster(areaMaskHeight, vb);
    areaMaskStrengthStart = Gtk::manage(new Adjuster(M("TP_LABMASKS_AREA_STRENGTH_START"), 0, 100, 0.1, 100));
    add_adjuster(areaMaskStrengthStart, vb);
    areaMaskStrengthEnd = Gtk::manage(new Adjuster(M("TP_LABMASKS_AREA_STRENGTH_END"), 0, 100, 0.1, 0));
    add_adjuster(areaMaskStrengthEnd, vb);
    areaMaskAngle180 = Gtk::manage(new Adjuster(M("TP_LABMASKS_AREA_ANGLE"), 0, 180, 0.1, 0));
    add_adjuster(areaMaskAngle180, vb);
    areaMaskAngle360 = Gtk::manage(new Adjuster(M("TP_LABMASKS_AREA_ANGLE"), -180, 180, 0.1, 0));
    add_adjuster(areaMaskAngle360, vb);
    areaMaskGradFeather = Gtk::manage(new Adjuster(M("TP_LABMASKS_AREA_FEATHER"), 0, 100, 0.1, 0));
    add_adjuster(areaMaskGradFeather, vb);

    areaMaskShapeFeather = Gtk::manage(new Adjuster(M("TP_LABMASKS_AREA_FEATHER"), 0, 100, 0.1, 0));
    add_adjuster(areaMaskShapeFeather, vb);
    areaMaskShapeFeather->setLogScale(100.0, 0.0);
    areaMaskShapeBlur = Gtk::manage(new Adjuster(M("TP_LABMASKS_BLUR"), 0, 500, 0.1, 0));
    add_adjuster(areaMaskShapeBlur, vb);
    areaMaskShapeBlur->setLogScale(10.0, 0.0);

    areaFrame->add(*vb);
    vb = Gtk::manage(new Gtk::VBox());
    vb->pack_start(*area);
    vb->pack_start(*areaFrame);
    areaMask->add(*vb, false);
    areaMask->setLevel(1);
    vb->signal_unmap().connect(sigc::mem_fun(*this, &MasksPanel::on_hide));
    mask_box->pack_start(*areaMask);

    drawnMask = Gtk::manage(new DrawnMaskPanel());
    mask_box->pack_start(*drawnMask);
    static_cast<DrawnMaskPanel *>(drawnMask)->signal_draw_updated().connect(sigc::mem_fun(this, &MasksPanel::onDrawnMaskUpdated));

    tb = Gtk::manage(new ToolParamBlock());
    external_mask_ = Gtk::manage(new MyExpander(true, M("TP_LABMASKS_EXTERNALMASK")));
    external_mask_->add(*tb, false);
    external_mask_->setLevel(1);
    external_mask_->signal_enabled_toggled().connect(sigc::mem_fun(*this, &MasksPanel::onExternalMaskChanged));

    external_mask_filename_ = Gtk::manage(new MyFileChooserButton(M("TP_LABMASKS_EXTERNALMASK_SELECT")));
    external_mask_filename_->signal_selection_changed().connect(sigc::mem_fun(*this, &MasksPanel::onExternalMaskChanged));
    external_mask_feather_ = Gtk::manage(new Adjuster(M("TP_LABMASKS_AREA_FEATHER"), 0, 100, 0.1, 0));
    external_mask_feather_->setAdjusterListener(this);

    hb = Gtk::manage(new Gtk::HBox());
    hb->pack_start(*Gtk::manage(new Gtk::Label(M("TP_LABMASKS_EXTERNALMASK_FILENAME") + ": ")), Gtk::PACK_SHRINK, 4);
    hb->pack_start(*external_mask_filename_, Gtk::PACK_EXPAND_WIDGET, 4);
    tb->pack_start(*hb);
    
    hb = Gtk::manage(new Gtk::HBox());
    tb->pack_start(*hb, Gtk::PACK_EXPAND_WIDGET, 0);    
    hb->pack_start(*external_mask_feather_, Gtk::PACK_EXPAND_WIDGET, 4);
    external_mask_inverted_ = Gtk::manage(new Gtk::CheckButton(M("TP_LABMASKS_INVERTED")));
    hb->pack_start(*external_mask_inverted_, Gtk::PACK_SHRINK, 4);
    external_mask_inverted_->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onExternalMaskChanged));    
    mask_box->pack_start(*external_mask_);
    
    tb = Gtk::manage(new ToolParamBlock());
    linked_mask_ = Gtk::manage(new MyExpander(true, M("TP_LABMASKS_LINKEDMASK")));
    linked_mask_->add(*tb, false);
    linked_mask_->setLevel(1);
    linked_mask_->signal_enabled_toggled().connect(sigc::mem_fun(*this, &MasksPanel::onLinkedMaskChanged));

    linked_mask_value_ = Gtk::manage(new MyComboBoxText());
    hb = Gtk::manage(new Gtk::HBox());
    tb->pack_start(*hb, Gtk::PACK_EXPAND_WIDGET, 0);
    hb->pack_start(*linked_mask_value_, Gtk::PACK_EXPAND_WIDGET, 4);
    linked_mask_value_->signal_changed().connect(sigc::mem_fun(*this, &MasksPanel::onLinkedMaskChanged));
    linked_mask_inverted_ = Gtk::manage(new Gtk::CheckButton(M("TP_LABMASKS_INVERTED")));
    hb->pack_start(*linked_mask_inverted_, Gtk::PACK_SHRINK, 4);
    linked_mask_inverted_->signal_clicked().connect(sigc::mem_fun(*this, &MasksPanel::onLinkedMaskChanged));
    
    mask_box->pack_start(*linked_mask_);
    
    // -----------------------------------------------------------------------
    MyExpander *ppMask = Gtk::manage(new MyExpander(false, M("TP_LABMASKS_POSTPROCESS")));
    {
        ToolParamBlock *tb = Gtk::manage(new ToolParamBlock());
        ppMask->add(*tb, false);
        ppMask->setLevel(1);

        maskPosterization = Gtk::manage(new Adjuster(M("TP_LABMASKS_POSTPROCESS_POSTERIZATION"), 0, 6, 1, 0));
        tb->pack_start(*maskPosterization);
        maskPosterization->setAdjusterListener(this);

        maskSmoothing = Gtk::manage(new Adjuster(M("TP_LABMASKS_POSTPROCESS_SMOOTHING"), 0, 100, 1, 0));
        tb->pack_start(*maskSmoothing);
        maskSmoothing->setAdjusterListener(this);
        
        CurveEditorGroup *cg = Gtk::manage(new CurveEditorGroup(options.lastToneCurvesDir, M("TP_LABMASKS_POSTPROCESS_CURVE")));
        cg->setCurveListener(this);

        maskCurve = static_cast<DiagonalCurveEditor *>(cg->addCurve(CT_Diagonal, ""));
        cg->curveListComplete();
        tb->pack_start(*cg, Gtk::PACK_SHRINK, 2);
    }
    mask_box->pack_start(*ppMask);
    // -----------------------------------------------------------------------

    mask_box->set_border_width(4);

    MyExpander *mask_exp = nullptr;
    {
        Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
        Gtk::Label *l = Gtk::manage(new Gtk::Label());
        l->set_markup("<b>" + M("TP_LABMASKS_MASK") + "</b>");
        l->set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
        hb->pack_start(*l);
        Gtk::Entry *e = Gtk::manage(new Gtk::Entry());
        hb->pack_start(*e, Gtk::PACK_EXPAND_WIDGET, 2);
        e->get_style_context()->add_class(GTK_STYLE_CLASS_FLAT);
        e->set_alignment(Gtk::ALIGN_END);
        e->set_placeholder_text("(" + M("TP_LABMASKS_MASK_UNNAMED") + ")");
        mask_exp = Gtk::manage(new MyExpander(false, hb));
        const auto on_activate =
            [this]() -> void
            {
                onMaskNameFocusOut(nullptr);
            };
        e->signal_activate().connect(sigc::slot<void>(on_activate));
        e->signal_button_release_event().connect(&on_release_event_ignore);
        e->add_events(Gdk::FOCUS_CHANGE_MASK);
        e->signal_focus_out_event().connect(sigc::mem_fun(*this, &MasksPanel::onMaskNameFocusOut));
        maskName = e;
    }
    mask_exp->add(*mask_box, false);
    mask_exp->setLevel(2);
    pack_start(*mask_exp);

    mask_exp_ = mask_exp;
    first_mask_exp_ = true;
    mask_exp_->signal_button_release_event().connect_notify(sigc::mem_fun(this, &MasksPanel::onMaskFold));
        
    maskBlur->delay = options.adjusterMaxDelay;

    mask_expanders_ = { parametricMask, areaMask, deltaEMask, drawnMask, external_mask_, linked_mask_, ppMask };
    for (auto e : mask_expanders_) {
        e->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &MasksPanel::onMaskExpanded), e));
    }

    add_events(Gdk::KEY_PRESS_MASK);
    const auto keypress =
        [this](GdkEventKey *evt) -> bool
        {
            bool ctrl = evt->state & GDK_CONTROL_MASK;
            bool shift = evt->state & GDK_SHIFT_MASK;
            bool alt = evt->state & GDK_MOD1_MASK;

            if (ctrl && !shift && !alt) {
                switch (evt->keyval) {
                case GDK_KEY_m:
                case GDK_KEY_M:
                    showMask->set_active(!showMask->get_active());
                    return true;
                case GDK_KEY_x:
                case GDK_KEY_X:
                    if (selected_ < masks_.size()) {
                        listEdited = true;
                        auto it = list_model_->children().begin();
                        for (size_t i = 0; i < selected_; ++i) {
                            ++it;
                        }
                        onListEnabledToggled(Gtk::TreePath(it).to_string());
                        maskShow(selected_, true);
                    }
                    return true;
                }
            }
            return false;
        };
    signal_key_press_event().connect(sigc::slot<bool, GdkEventKey *>(keypress), false);
}


MasksPanel::~MasksPanel()
{
    delete areaMaskToggle;
}


ToolPanelListener *MasksPanel::getListener()
{
    if (listenerDisabled.empty() || !listenerDisabled.back()) {
        return cp_->listener();
    }
    return nullptr;
}


void MasksPanel::disableListener()
{
    listenerDisabled.push_back(true);
    //static_cast<DrawnMaskPanel *>(drawnMask)->setTargetMask(nullptr);
}


void MasksPanel::enableListener()
{
    listenerDisabled.pop_back();
    // if (listenerDisabled.empty() || !listenerDisabled.back()) {
    //     static_cast<DrawnMaskPanel *>(drawnMask)->setTargetMask(&masks_[selected_].drawnMask);
    // }
}


void MasksPanel::onSelectionChanged()
{
    auto s = list->get_selection()->get_selected_rows();
    if (!s.empty()) {
        int idx = s[0][0];
        // update the selected values
        maskGet(selected_);
        cp_->selectionChanging(selected_);
        selected_ = idx;
        area_shape_index_ = 0;
        maskShow(selected_);
        if (showMask->get_active()) {
            onShowMaskChanged();
        }
    }
}


void MasksPanel::maskGet(int idx)
{
    if (idx < 0 || size_t(idx) >= masks_.size()) {
        return;
    }
    
    auto &r = masks_[idx];
    r.parametricMask.enabled = parametricMask->getEnabled();
    r.parametricMask.hue = hueMask->getCurve();
    r.parametricMask.chromaticity = chromaticityMask->getCurve();
    r.parametricMask.lightness = lightnessMask->getCurve();
    r.parametricMask.lightnessDetail = lightnessMaskDetail->getValue();
    r.parametricMask.blur = maskBlur->getValue();
    r.parametricMask.contrastThreshold = contrastThreshold->getValue();
    r.inverted = maskInverted->get_active();
    r.areaMask.enabled = areaMask->getEnabled();
    r.areaMask.feather = areaMaskFeather->getValue();
    r.areaMask.blur = areaMaskBlur->getValue();
    r.areaMask.contrast = areaMaskContrast->getCurve();
    if (area_shape_index_ < r.areaMask.shapes.size()) {
        auto &a = r.areaMask.shapes[area_shape_index_];
        a->feather = areaMaskShapeFeather->getValue();
        a->blur = areaMaskShapeBlur->getValue();
        switch (a->getType()) {
            case Shape::Type::POLYGON:
            {
                auto poly = static_cast<rtengine::procparams::AreaMask::Polygon*>(a.get());
                poly->knots = getPolygon();
                poly->mode = Shape::Mode(getAreaShapeMode());
                break;
            }
            case Shape::Type::GRADIENT:
            {
                auto gradient = static_cast<rtengine::procparams::AreaMask::Gradient*>(a.get());
                gradient->x = areaMaskX->getValue();
                gradient->y = areaMaskY->getValue();
                gradient->strengthStart = areaMaskStrengthStart->getValue();
                gradient->strengthEnd = areaMaskStrengthEnd->getValue();
                gradient->angle = areaMaskAngle360->getValue();
                gradient->feather = areaMaskGradFeather->getValue();
                gradient->mode = Shape::Mode(getAreaShapeMode());
                break;
            }
            case Shape::Type::RECTANGLE:
            default:
            {
                auto rect = static_cast<rtengine::procparams::AreaMask::Rectangle*>(a.get());
                rect->x = areaMaskX->getValue();
                rect->y = areaMaskY->getValue();
                rect->width = areaMaskWidth->getValue();
                rect->height = areaMaskHeight->getValue();
                rect->angle = areaMaskAngle180->getValue();
                rect->roundness = areaMaskRoundness->getValue();
                rect->mode = Shape::Mode(getAreaShapeMode());
            }
        }
    }
    r.deltaEMask.enabled = deltaEMask->getEnabled();
    double b, t;
    deltaEL->getValue(b, t);
    r.deltaEMask.L = t;
    r.deltaEMask.weight_L = b;
    deltaEC->getValue(b, t);
    r.deltaEMask.C = t;
    r.deltaEMask.weight_C = b;
    deltaEH->getValue(b, t);
    r.deltaEMask.H = t;
    r.deltaEMask.weight_H = b;
    r.deltaEMask.range = deltaERange->getValue();
    int sgn = deltaEInverted->get_active() ? -1 : 1;
    r.deltaEMask.decay = sgn * deltaEDecay->getValue();
    r.deltaEMask.strength = deltaEStrength->getValue();
    r.name = maskName->get_text();
    r.curve = maskCurve->getCurve();
    r.posterization = maskPosterization->getValue();
    r.smoothing = maskSmoothing->getValue();
    r.opacity = maskOpacity->getValue();
    r.linkedMask.enabled = linked_mask_->getEnabled();
    r.linkedMask.inverted = linked_mask_inverted_->get_active();
    int raster_idx = linked_mask_value_->get_active_row_number();
    if (raster_idx == 0) {
        r.linkedMask.toolname = "";
        r.linkedMask.name = "";
    } else if (raster_idx > 0 && size_t(raster_idx) <= available_linked_masks_.size()) {
        r.linkedMask.toolname = available_linked_masks_[raster_idx-1].toolname;
        r.linkedMask.name = available_linked_masks_[raster_idx-1].name;
    }
    r.externalMask.enabled = external_mask_->getEnabled();
    r.externalMask.inverted = external_mask_inverted_->get_active();
    r.externalMask.filename = Glib::filename_to_utf8(external_mask_filename_->get_filename());
    r.externalMask.feather = external_mask_feather_->getValue();
}


void MasksPanel::onAddPressed()
{
    int idx = std::min(size_t(selected_)+1, masks_.size());
    if (!cp_->addPressed(idx)) {
        return;
    }

    listEdited = true;
    // selected_ = masks_.size();
    // masks_.push_back(rtengine::procparams::Mask());
    auto it = masks_.insert(masks_.begin()+idx, rtengine::procparams::Mask());
    selected_ = it - masks_.begin();
    populateList();
    area_shape_index_ = 0;
    maskShow(selected_);

    auto l = getListener();
    if (l) {
        l->panelChanged(EvMaskList, M("HISTORY_CHANGED"));
    }
}


void MasksPanel::onRemovePressed()
{
    if (list_model_->children().size() <= 1 || !cp_->removePressed(selected_)) {
        return;
    }
    
    listEdited = true;
    masks_.erase(masks_.begin() + selected_);
    selected_ = rtengine::LIM(int(selected_)-1, 0, int(masks_.size()-1));
    populateList();
    area_shape_index_ = 0;
    maskShow(selected_);

    auto l = getListener();
    if (l) {
        l->panelChanged(EvMaskList, M("HISTORY_CHANGED"));
    }
}


void MasksPanel::onUpPressed()
{
    if (selected_ > 0 && cp_->moveUpPressed(selected_)) {
        listEdited = true;
        
        auto r = masks_[selected_];
        masks_.erase(masks_.begin() + selected_);
        --selected_;
        masks_.insert(masks_.begin() + selected_, r);
        populateList();

        auto l = getListener();
        if (l) {
            l->panelChanged(EvMaskList, M("HISTORY_CHANGED"));
        }
    }
}


void MasksPanel::onDownPressed()
{
    if (selected_ < masks_.size()-1 && cp_->moveDownPressed(selected_)) {
        listEdited = true;
        
        auto r = masks_[selected_];
        masks_.erase(masks_.begin() + selected_);
        ++selected_;
        masks_.insert(masks_.begin() + selected_, r);
        populateList();

        auto l = getListener();
        if (l) {
            l->panelChanged(EvMaskList, M("HISTORY_CHANGED"));
        }
    }
}


void MasksPanel::onCopyPressed()
{
    if (selected_ < masks_.size() && cp_->copyPressed(selected_)) {
        listEdited = true;
        
        auto r = masks_[selected_];
        int idx = std::min(size_t(selected_)+1, masks_.size());
        auto it = masks_.insert(masks_.begin()+idx, r);
        selected_ = it - masks_.begin();
        // masks_.push_back(r);
        // selected_ = masks_.size()-1;
        populateList();

        auto l = getListener();
        if (l) {
            l->panelChanged(EvMaskList, M("HISTORY_CHANGED"));
        }
    }
}


void MasksPanel::onResetPressed()
{
    if (selected_ < masks_.size() && cp_->resetPressed(selected_)) {
        listEdited = true;
        masks_[selected_] = rtengine::procparams::Mask();
        populateList();
        area_shape_index_ = 0;
        maskShow(selected_);
        
        auto l = getListener();
        if (l) {
            l->panelChanged(EvMaskList, M("HISTORY_CHANGED"));
        }
    }
}


void MasksPanel::onShowMaskChanged()
{
    auto l = getListener();
    if (l) {
        l->panelChanged(EvShowMask, showMask->get_active() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}


void MasksPanel::populateList()
{
    ConnectionBlocker b(selectionConn);
    list_model_->clear();
    rtengine::procparams::Mask dflt;

    int n = cp_->getColumnCount();
    for (size_t i = 0; i < masks_.size(); ++i) {
        auto &r = masks_[i];
        auto j = list_model_->append();
        auto row = *j;
        row[list_model_columns_->enabled] = r.enabled;
        row[list_model_columns_->id] = i+1;
        for (int c = 0; c < n; ++c) {
            row[list_model_columns_->cols[c]] = cp_->getColumnContent(c, i);
        }
        if (!r.name.empty()) {
            row[list_model_columns_->mask] = r.name;            
        } else {
            Glib::ustring am("");
            if (r.areaMask.enabled && !r.areaMask.isTrivial()) {
                am = Glib::ustring::compose("\n%1 shape%2", r.areaMask.shapes.size(), r.areaMask.shapes.size() > 1 ? "s" : "");
            }
            if (!r.drawnMask.isTrivial()) {
                if (am.empty()) {
                    am = "\n";
                } else {
                    am += " ";
                }
                am += Glib::ustring::compose("%1 stroke%2", r.drawnMask.strokes.size(), r.drawnMask.strokes.size() == 1 ? "" : "s");
            }
            if (r.externalMask.enabled && !r.externalMask.filename.empty()) {
                if (am.empty()) {
                    am = "\nE";
                } else {
                    am += " E";
                }
            }
            if (r.linkedMask.enabled && !r.linkedMask.toolname.empty() && !r.linkedMask.name.empty()) {
                if (am.empty()) {
                    am = "\n⇡";
                } else {
                    am += " ⇡";
                }
            }
            row[list_model_columns_->mask] = 
                Glib::ustring::compose(
                    "%1%2%3%4%7%5%6",
                    hasMask(r, dflt.parametricMask.hue, r.parametricMask.hue) ? "H" : "",
                    hasMask(r, dflt.parametricMask.chromaticity, r.parametricMask.chromaticity) ? "C" : "",
                    hasMask(r, dflt.parametricMask.lightness, r.parametricMask.lightness) ? "L" : "",
                    r.deltaEMask.enabled ? "ΔE" : "",
                    r.parametricMask.blur ? Glib::ustring::compose(" b=%1", r.parametricMask.blur) : "",
                    am, (r.parametricMask.enabled && r.parametricMask.contrastThreshold) ? Glib::ustring::compose(" c=%1", r.parametricMask.contrastThreshold) : "");
        }
    }
}


void MasksPanel::maskShow(int idx, bool list_only, bool unsub)
{
    disableListener();
    rtengine::procparams::Mask dflt;
    auto &r = masks_[idx];
    if (!list_only) {
        cp_->selectionChanged(idx);
        parametricMask->setEnabled(r.parametricMask.enabled);
        hueMask->setCurve(r.parametricMask.hue);
        chromaticityMask->setCurve(r.parametricMask.chromaticity);
        lightnessMask->setCurve(r.parametricMask.lightness);
        lightnessMaskDetail->setValue(r.parametricMask.lightnessDetail);
        contrastThreshold->setValue(r.parametricMask.contrastThreshold);
        maskBlur->setValue(r.parametricMask.blur);
        maskInverted->set_active(r.inverted);
        maskName->set_text(r.name);
        maskCurve->setCurve(r.curve);
        maskPosterization->setValue(r.posterization);
        maskSmoothing->setValue(r.smoothing);
        maskOpacity->setValue(r.opacity);

        if (unsub && isCurrentSubscriber()) {
            if (areaMaskToggle->get_active()) {
                switchOffEditMode();
            }
            else {
                unsubscribe();
            }
        }

        // this will also switch to the correct geometry
        populateShapeList(idx, area_shape_index_);

        areaMaskToggle->set_active(false);
        areaMask->setEnabled(r.areaMask.enabled);
        areaMaskFeather->setValue(r.areaMask.feather);
        areaMaskBlur->setValue(r.areaMask.blur);
        areaMaskContrast->setCurve(r.areaMask.contrast);
        if (area_shape_index_ < r.areaMask.shapes.size()) {
            auto &a = r.areaMask.shapes[area_shape_index_];
            areaMaskShapeFeather->setValue(a->feather);
            areaMaskShapeBlur->setValue(a->blur);
            switch (a->getType()) {
            case Shape::Type::RECTANGLE: {
                auto rect = static_cast<rtengine::procparams::AreaMask::Rectangle*>(a.get());
                areaMaskX->setValue(rect->x);
                areaMaskY->setValue(rect->y);
                areaMaskWidth->setValue(rect->width);
                areaMaskHeight->setValue(rect->height);
                areaMaskAngle180->setValue(rect->angle);
                areaMaskRoundness->setValue(rect->roundness);
                setAdjustersVisibility(true, Shape::Type::RECTANGLE);
                break;
                }
            case Shape::Type::GRADIENT: {
                auto gradient = static_cast<rtengine::procparams::AreaMask::Gradient*>(a.get());
                areaMaskX->setValue(gradient->x);
                areaMaskY->setValue(gradient->y);
                areaMaskStrengthStart->setValue(gradient->strengthStart);
                areaMaskStrengthEnd->setValue(gradient->strengthEnd);
                areaMaskAngle360->setValue(gradient->angle);
                areaMaskGradFeather->setValue(gradient->feather);
                setAdjustersVisibility(true, Shape::Type::GRADIENT);
                break;
                }
            case Shape::Type::POLYGON: {
                auto poly = static_cast<rtengine::procparams::AreaMask::Polygon*>(a.get());
                setPolygon(poly->knots);
                setAdjustersVisibility(false, Shape::Type::POLYGON);
                break;
                }
            default:
                break;
            }
            toggleAreaShapeMode(int(a->mode));

            switch (a->getType()) {
            case Shape::Type::RECTANGLE:
                updateRectangleAreaMask(false);
                setAdjustersVisibility(true, Shape::Type::RECTANGLE);
                break;
            case Shape::Type::GRADIENT:
                updateGradientAreaMask(false);
                setAdjustersVisibility(true, Shape::Type::GRADIENT);
                break;
            case Shape::Type::POLYGON:
                setAdjustersVisibility(false, Shape::Type::POLYGON);
                break;
            default:
                break;
            }
        }
        else {
            setAdjustersVisibility(false, Shape::Type::RECTANGLE);
        }

        deltaEMask->setEnabled(r.deltaEMask.enabled);
        deltaEL->setValue(r.deltaEMask.weight_L, r.deltaEMask.L);
        deltaEC->setValue(r.deltaEMask.weight_C, r.deltaEMask.C);
        deltaEH->setValue(r.deltaEMask.weight_H, r.deltaEMask.H);
        deltaERange->setValue(r.deltaEMask.range);
        deltaEDecay->setValue(std::abs(r.deltaEMask.decay));
        deltaEStrength->setValue(std::abs(r.deltaEMask.strength));
        deltaEInverted->set_active(r.deltaEMask.decay < 0);
        static_cast<DeltaEArea *>(deltaEColor)->setColor(r.deltaEMask.L, r.deltaEMask.C, r.deltaEMask.H);

        external_mask_->setEnabled(r.externalMask.enabled);
        external_mask_inverted_->set_active(r.externalMask.inverted);
        external_mask_filename_->set_filename(Glib::filename_from_utf8(r.externalMask.filename));
        external_mask_feather_->setValue(r.externalMask.feather);
        
        linked_mask_->setEnabled(r.linkedMask.enabled);
        linked_mask_inverted_->set_active(r.linkedMask.inverted);
        updateLinkedMaskList(nullptr);
    }
    static_cast<DrawnMaskPanel *>(drawnMask)->setTargetMask(&r.drawnMask,
                                                            !list_only);

    int n = cp_->getColumnCount();
    auto row = list_model_->children()[idx];
    row[list_model_columns_->enabled] = r.enabled;
    for (int c = 0; c < n; ++c) {
        row[list_model_columns_->cols[c]] = cp_->getColumnContent(c, idx);
    }
    if (!r.name.empty()) {
        row[list_model_columns_->mask] = r.name;
    } else {
        Glib::ustring am("");
        if (r.areaMask.enabled && !r.areaMask.isTrivial()) {
            am = Glib::ustring::compose("\n%1 shape%2", r.areaMask.shapes.size(),
                                        r.areaMask.shapes.size() > 1 ? "s" : "");
        }
        if (!r.drawnMask.isTrivial()) {
            if (am.empty()) {
                am = "\n";
            } else {
                am += " ";
            }
            am += Glib::ustring::compose("%1 stroke%2", r.drawnMask.strokes.size(), r.drawnMask.strokes.size() == 1 ? "" : "s");
        }
        if (r.externalMask.enabled && !r.externalMask.filename.empty()) {
            if (am.empty()) {
                am = "\nE";
            } else {
                am += " E";
            }
        }
        if (r.linkedMask.enabled && !r.linkedMask.toolname.empty() && !r.linkedMask.name.empty()) {
            if (am.empty()) {
                am = "\n⇡";
            } else {
                am += " ⇡";
            }
        }
        row[list_model_columns_->mask] = 
            Glib::ustring::compose(
                "%1%2%3%4%7%5%6",
                hasMask(r, dflt.parametricMask.hue, r.parametricMask.hue) ? "H" : "",
                hasMask(r, dflt.parametricMask.chromaticity, r.parametricMask.chromaticity) ? "C" : "",
                hasMask(r, dflt.parametricMask.lightness, r.parametricMask.lightness) ? "L" : "",
                r.deltaEMask.enabled ? "ΔE" : "",
                r.parametricMask.blur ? Glib::ustring::compose(" b=%1", r.parametricMask.blur) : "", am,
                (r.parametricMask.enabled && r.parametricMask.contrastThreshold) ? Glib::ustring::compose(" c=%1", r.parametricMask.contrastThreshold) : "");
    }
    Gtk::TreePath pth;
    pth.push_back(idx);
    list->get_selection()->select(pth);
    enableListener();
}


void MasksPanel::setEditProvider(EditDataProvider *provider)
{
    hueMask->setEditProvider(provider);
    chromaticityMask->setEditProvider(provider);
    lightnessMask->setEditProvider(provider);
    static_cast<DeltaEArea *>(deltaEColor)->setEditProvider(provider);
    AreaMask::setEditProvider(provider);
    static_cast<DrawnMaskPanel *>(drawnMask)->setEditProvider(provider);
}


void MasksPanel::onAreaMaskToggleChanged()
{
    if (areaMaskToggle->get_active()) {
        // areaMaskDrawRectangle->set_active(false);
        subscribe();
        Shape::Type shape_type = Shape::Type::RECTANGLE;
        if (selected_ < masks_.size()) {
            auto &a = masks_[selected_].areaMask;
            if (area_shape_index_ < a.shapes.size()) {
                shape_type = a.shapes[area_shape_index_]->getType();
            }
        }
        setAdjustersVisibility(true, shape_type);
    } else {
        unsubscribe();
        setAdjustersVisibility(false, Shape::Type::RECTANGLE /* ignored */);
    }
}


void MasksPanel::onMaskInvertedChanged()
{
    auto l = getListener();
    if (l) {
        l->panelChanged(EvAreaMask, M("GENERAL_CHANGED"));
    }
}


void MasksPanel::setAdjustersVisibility(bool visible, Shape::Type shape_type)
{
    visible &= areaMaskToggle->get_active();
    switch (shape_type) {
    case Shape::Type::RECTANGLE:
        areaMaskRoundness->set_visible(visible);
        areaMaskX->set_visible(visible);
        areaMaskY->set_visible(visible);
        areaMaskWidth->set_visible(visible);
        areaMaskHeight->set_visible(visible);
        areaMaskStrengthStart->set_visible(false);
        areaMaskStrengthEnd->set_visible(false);
        areaMaskAngle180->set_visible(visible);
        areaMaskAngle360->set_visible(false);
        areaMaskGradFeather->set_visible(false);

        areaMaskShapeFeather->set_visible(visible);
        areaMaskShapeBlur->set_visible(visible);
        break;
    case Shape::Type::POLYGON:
        areaMaskRoundness->set_visible(false);
        areaMaskX->set_visible(false);
        areaMaskY->set_visible(false);
        areaMaskWidth->set_visible(false);
        areaMaskHeight->set_visible(false);
        areaMaskStrengthStart->set_visible(false);
        areaMaskStrengthEnd->set_visible(false);
        areaMaskAngle180->set_visible(false);
        areaMaskAngle360->set_visible(false);
        areaMaskGradFeather->set_visible(false);

        areaMaskShapeFeather->set_visible(visible);
        areaMaskShapeBlur->set_visible(visible);
        break;
    case Shape::Type::GRADIENT:
        areaMaskRoundness->set_visible(false);
        areaMaskX->set_visible(visible);
        areaMaskY->set_visible(visible);
        areaMaskWidth->set_visible(false);
        areaMaskHeight->set_visible(false);
        areaMaskStrengthStart->set_visible(visible);
        areaMaskStrengthEnd->set_visible(visible);
        areaMaskAngle180->set_visible(false);
        areaMaskAngle360->set_visible(visible);
        areaMaskGradFeather->set_visible(visible);

        areaMaskShapeFeather->set_visible(false);
        areaMaskShapeBlur->set_visible(false);
        break;
    default:
        break;
    }
}


void MasksPanel::updateRectangleAreaMask(bool from_mask)
{
    disableListener();
    if (from_mask) {
        areaMaskX->setValue(center_x_);
        areaMaskY->setValue(center_y_);
        areaMaskWidth->setValue(width_);
        areaMaskHeight->setValue(height_);
        areaMaskAngle180->setValue(angle_);
    } else {
        center_x_ = areaMaskX->getValue();
        center_y_ = areaMaskY->getValue();
        width_ = areaMaskWidth->getValue();
        height_ = areaMaskHeight->getValue();
        angle_ = areaMaskAngle180->getValue();
        updateGeometry();
    }
    enableListener();
}


void MasksPanel::updateGradientAreaMask(bool from_mask)
{
    disableListener();
    if (from_mask) {
        areaMaskX->setValue(center_x_);
        areaMaskY->setValue(center_y_);
        areaMaskStrengthStart->setValue(strength_start_);
        areaMaskStrengthEnd->setValue(strength_end_);
        areaMaskAngle360->setValue(angle_);
        areaMaskGradFeather->setValue(feather_);
    } else {
        center_x_ = areaMaskX->getValue();
        center_y_ = areaMaskY->getValue();
        strength_start_ = areaMaskStrengthStart->getValue();
        strength_end_ = areaMaskStrengthEnd->getValue();
        angle_ = areaMaskAngle360->getValue();
        feather_ = areaMaskGradFeather->getValue();
        updateGeometry();
    }
    enableListener();
}


inline const rtengine::ProcEvent &MasksPanel::areaMaskEvent() const
{
    return areaMask->getEnabled() ? EvAreaMask : EvAreaMaskVoid;
}


bool MasksPanel::button1Released()
{
    if (last_object_ != -1) {
        if (getGeometryType() == Shape::Type::RECTANGLE) {
            updateRectangleAreaMask(true);
        } else if (getGeometryType() == Shape::Type::GRADIENT) {
            updateGradientAreaMask(true);
        }
        onAreaShapeSelectionChanged();
        populateShapeList(selected_, area_shape_index_);
        auto l = getListener();
        if (l) {
            l->panelChanged(areaMaskEvent(), M("GENERAL_CHANGED"));
        }
    }
    return AreaMask::button1Released();
}


bool MasksPanel::pick3(bool picked)
{
    if (AreaMask::pick3(picked)) {
        onAreaShapeSelectionChanged();
        populateShapeList(selected_, area_shape_index_);
        auto l = getListener();
        if (l) {
            l->panelChanged(areaMaskEvent(), M("GENERAL_CHANGED"));
        }
        return true;
    }
    return false;
}


bool MasksPanel::scroll(int modifierKey, GdkScrollDirection direction, double deltaX, double deltaY, bool &propagateEvent)
{
    if (AreaMask::scroll(modifierKey, direction, deltaX, deltaY, propagateEvent)) {
        if (scrollDelayConn.connected()) {
            scrollDelayConn.disconnect();
        }
        scrollDelayConn = Glib::signal_timeout().connect (sigc::mem_fun(*this, &MasksPanel::onKnotRoundnessUpdated), 500);
        return true;
    }
    EditDataProvider *provider = getEditProvider();
    if (provider) {
        bool is_rectangle = getGeometryType() == Shape::Type::RECTANGLE;
        bool shift = modifierKey & GDK_SHIFT_MASK;
        bool ctrl = modifierKey & GDK_CONTROL_MASK;
        bool alt = modifierKey & GDK_MOD1_MASK;

        int imW, imH;
        provider->getImageSize(imW, imH);

        double delta = (direction == GDK_SCROLL_SMOOTH ?
                        deltaY
                        : (direction == GDK_SCROLL_UP ? 3. : -3.));
        Adjuster *target = nullptr;
        if (is_rectangle && shift && !ctrl && !alt) {
            target = areaMaskRoundness;
        } else if (ctrl && !shift && !alt) {
            target = areaMaskShapeFeather;
        } else if (alt && !shift && !ctrl) {
            target = areaMaskShapeBlur;
        }
        if (target) {
            double old_val = target->getValue();
            double new_val = old_val - delta;
            target->trimValue(new_val);
            if (new_val != old_val) {
                target->setValue(new_val);
                if (scrollDelayConn.connected()) {
                    scrollDelayConn.disconnect();
                }
                scrollDelayConn = Glib::signal_timeout().connect(sigc::mem_fun(*this, &MasksPanel::onKnotRoundnessUpdated), 500);
            }
            propagateEvent = false;
            return true;
        }
    }
    return false;
}


bool MasksPanel::onKnotRoundnessUpdated()
{
    auto l = getListener();
    if (l) {
        l->panelChanged(areaMaskEvent(), M("GENERAL_CHANGED"));
    }
    return false;
}


void MasksPanel::switchOffEditMode()
{
    static_cast<DeltaEArea *>(deltaEColor)->switchOffEditMode();
    static_cast<DrawnMaskPanel *>(drawnMask)->switchOffEditMode();
    
    areaMaskToggle->set_active(false);
    AreaMask::switchOffEditMode();
    grab_focus();
}


void MasksPanel::onAreaMaskEnableToggled()
{
    auto l = getListener();
    if (l) {
        l->panelChanged(EvAreaMask, areaMask->getEnabled() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}


void MasksPanel::curveChanged(CurveEditor* ce)
{
    auto l = getListener();
    if (l) {
        if (ce == areaMaskContrast) {
            l->panelChanged(areaMaskEvent(), M("GENERAL_CHANGED"));
        } else if (ce == hueMask) {
            l->panelChanged(EvHMask, M("HISTORY_CUSTOMCURVE"));
        } else if (ce == chromaticityMask) {
            l->panelChanged(EvCMask, M("HISTORY_CUSTOMCURVE"));
        } else if (ce == lightnessMask) {
            l->panelChanged(EvLMask, M("HISTORY_CUSTOMCURVE"));
        } else if (ce == maskCurve) {
            l->panelChanged(EvMaskPostprocess, M("TP_LABMASKS_POSTPROCESS_CURVE") + ": " + M("HISTORY_CUSTOMCURVE"));
        }
    }
}


void MasksPanel::adjusterChanged(Adjuster *a, double newval)
{
    auto l = getListener();

    if (a == maskBlur) {
        if (l) {
            l->panelChanged(EvMaskBlur, a->getTextValue());
        }
    } else if (std::find(areaMaskAdjusters.begin(), areaMaskAdjusters.end(), a) != areaMaskAdjusters.end()) {
        onAreaShapeSelectionChanged();
        populateShapeList(selected_, area_shape_index_);
        if (l) {
            l->panelChanged(areaMaskEvent(), M("GENERAL_CHANGED"));
        }
    } else if (a == deltaERange || a == deltaEDecay || a == deltaEStrength) {
        if (l) {
            l->panelChanged(deltaEMaskEvent(), M("GENERAL_CHANGED"));
        }
    } else if (a == contrastThreshold) {
        if (l) {
            l->panelChanged(EvContrastThresholdMask, a->getTextValue());
        }
    } else if (a == lightnessMaskDetail) {
        if (l) {
            l->panelChanged(EvLMask, a->getTextValue());
        }
    } else if (a == maskPosterization) {
        if (l) {
            l->panelChanged(EvMaskPostprocess, M("TP_LABMASKS_POSTPROCESS_POSTERIZATION") + ": " + a->getTextValue());
        }
    } else if (a == maskSmoothing) {
        if (l) {
            l->panelChanged(EvMaskPostprocess, M("TP_LABMASKS_POSTPROCESS_SMOOTHING") + ": " + a->getTextValue());
        }
    } else if (a == maskOpacity) {
        if (l) {
            l->panelChanged(EvMaskPostprocess, M("TP_LABMASKS_DRAWNMASK_OPACITY") + ": " + a->getTextValue());
        }
    } else if (a == external_mask_feather_) {
        if (l) {
            l->panelChanged(EvExternalMask, M("GENERAL_CHANGED"));
        }
    }
    maskShow(selected_, true);
}


void MasksPanel::adjusterChanged(ThresholdAdjuster *a, double newBottom, double newTop)
{
    if (a == deltaEL || a == deltaEC || a == deltaEH) {
        auto l = getListener();
        double b, L, C, H;
        deltaEL->getValue(b, L);
        deltaEC->getValue(b, C);
        deltaEH->getValue(b, H);
        static_cast<DeltaEArea *>(deltaEColor)->setColor(L, C, H);
        if (a == deltaEH) {
            deltaEL->queue_draw();
            deltaEC->queue_draw();
        }
        if (l) {
            l->panelChanged(deltaEMaskEvent(), M("GENERAL_CHANGED"));
        }
    }
}


void MasksPanel::adjusterAutoToggled(Adjuster *a, bool newval)
{
}


void MasksPanel::setMasks(const std::vector<rtengine::procparams::Mask> &masks, int selected_idx, bool show_mask)
{
    disableListener();
    ConnectionBlocker b(selectionConn);
    
    masks_ = masks;
    selected_ = 0;
    showMask->set_active(false);
    if (selected_idx >= 0 && size_t(selected_idx) < masks.size()) {
        selected_ = selected_idx;
        if (show_mask) {
            showMask->set_active(true);
        }
    }
    static_cast<DrawnMaskPanel *>(drawnMask)->setTargetMask(nullptr);
    populateList();
    area_shape_index_ = 0;
    maskShow(selected_);
    enableListener();
}
        

void MasksPanel::getMasks(std::vector<rtengine::procparams::Mask> &masks, int &show_mask_idx)
{
    maskGet(selected_);
    masks = masks_;
    if (showMask->get_active()) {
        show_mask_idx = selected_;
    } else {
        show_mask_idx = -1;
    }
}


int MasksPanel::getSelected()
{
    return selected_;
}


void MasksPanel::colorForValue(double valX, double valY, enum ColorCaller::ElemType elemType, int callerId, ColorCaller *caller)
{
    float R = 0.f, G = 0.f, B = 0.f;
    double alpha = 0.f;

    auto iws = rtengine::ICCStore::getInstance()->workingSpaceInverseMatrix("sRGB");

    const auto lch2rgb =
        [iws](float l, float c, float h, float &R, float &G, float &B) -> void
        {
            float d = h / 180.0 * rtengine::RT_PI_F;
            float a = c * std::cos(d);
            float b = c * std::sin(d);
            rtengine::Color::lab2rgb(l * 32768.f, a * 32768.f, b * 32768.f, R, G, B, iws);
            R = rtengine::LIM01(rtengine::Color::gamma_srgbclipped(R) / 65535.f);
            G = rtengine::LIM01(rtengine::Color::gamma_srgbclipped(G) / 65535.f);
            B = rtengine::LIM01(rtengine::Color::gamma_srgbclipped(B) / 65535.f);
        };
    
    if (callerId == ID_HUE_MASK) {
        float x = valX - 1.f/6.f;
        if (x < 0.f) {
            x += 1.f;
        }
        x = rtengine::log2lin(x, 3.f);
        rtengine::Color::hsv2rgb01(x, 0.5f, 0.65f, R, G, B);
    } else if (callerId == ID_HUE_MASK+1) {
        rtengine::Color::hsv2rgb01(float(valY), float(valX), 0.8f, R, G, B);
    } else if (callerId == ID_HUE_MASK+2) {
        double dummy, w, h;
        deltaEH->getValue(dummy, h);
        lch2rgb(valX, 0.5f, h, R, G, B);
        deltaEL->getValue(w, dummy);
        alpha = rtengine::LIM01(1.0 - w/100.0);
    } else if (callerId == ID_HUE_MASK+3) {
        double dummy, w, h;
        deltaEH->getValue(dummy, h);
        lch2rgb(0.65f, valX, h, R, G, B);
        deltaEC->getValue(w, dummy);
        alpha = rtengine::LIM01(1.0 - w/100.0);        
    } else if (callerId == ID_HUE_MASK+4) {
        double dummy, w;
        lch2rgb(0.65f, 0.5f, valX * 360.f, R, G, B);
        deltaEH->getValue(w, dummy);
        alpha = rtengine::LIM01(1.0 - w/100.0);        
    }

    caller->ccRed = double(R);
    caller->ccGreen = double(G);
    caller->ccBlue = double(B);
    caller->ccAlpha = alpha;
}


float MasksPanel::blendPipetteValues(CurveEditor *ce, float chan1, float chan2, float chan3)
{
    if (ce == chromaticityMask && chan1 > 0.f) {
        return rtengine::lin2log(chan1, 50.f);
    } else if (ce == hueMask && chan1 > 0.f) {
        float x = chan1 + 1.f/6.f;
        if (x > 1.f) {
            x -= 1.f;
        }
        return rtengine::lin2log(x, 3.f);
    }
    return CurveListener::blendPipetteValues(ce, chan1, chan2, chan3);
}


void MasksPanel::setEdited(bool yes)
{
    listEdited = yes;
    hueMask->setUnChanged(!yes);
    chromaticityMask->setUnChanged(!yes);
    lightnessMask->setUnChanged(!yes);
    maskBlur->setEditedState(yes ? Edited : UnEdited);
    maskInverted->set_inconsistent(!yes);
    showMask->set_inconsistent(!yes);
    areaMask->set_inconsistent(!yes);
    for (auto a : areaMaskAdjusters) {
        a->setEditedState(yes ? Edited : UnEdited);
    }
    areaMaskContrast->setUnChanged(!yes);
}


bool MasksPanel::getEdited()
{
    for (auto a : areaMaskAdjusters) {
        if (a->getEditedState() == Edited) {
            return true;
        }
    }
    return listEdited
        || !hueMask->isUnChanged()
        || !chromaticityMask->isUnChanged()
        || !lightnessMask->isUnChanged()
        || maskBlur->getEditedState() == Edited
        || !maskInverted->get_inconsistent()
        || !showMask->get_inconsistent()
        || !areaMask->get_inconsistent()
        || !areaMaskContrast->isUnChanged();
}


void MasksPanel::updateSelected()
{
    maskShow(selected_, true, false);
}


void MasksPanel::onAreaShapeSelectionChanged()
{
    if (selected_ < masks_.size() && area_shape_index_ < masks_[selected_].areaMask.shapes.size()) {
        disableListener();

        auto &s = masks_[selected_].areaMask.shapes[area_shape_index_];
        s->mode = Shape::Mode(getAreaShapeMode());
        s->feather = areaMaskShapeFeather->getValue();
        s->blur = areaMaskShapeBlur->getValue();
        switch (s->getType()) {
        case Shape::Type::RECTANGLE:
        {
            auto rect = static_cast<rtengine::procparams::AreaMask::Rectangle*>(s.get());
            updateRectangleAreaMask(false);
            rect->x = center_x_;
            rect->y = center_y_;
            rect->width = width_;
            rect->height = height_;
            rect->angle = angle_;
            rect->roundness = areaMaskRoundness->getValue();
            break;
        }
        case Shape::Type::POLYGON:
        {
            auto poly = static_cast<rtengine::procparams::AreaMask::Polygon*>(s.get());
            poly->knots = getPolygon();
            break;
        }
        case Shape::Type::GRADIENT:
        {
            auto gradient = static_cast<rtengine::procparams::AreaMask::Gradient*>(s.get());
            updateGradientAreaMask(false);
            gradient->x = center_x_;
            gradient->y = center_y_;
            gradient->strengthStart = strength_start_;
            gradient->strengthEnd = strength_end_;
            gradient->feather = feather_;
            gradient->angle = angle_;
            break;
        }
        default:
            break;
        }

        auto sel = areaMaskShapes->get_selected();
        unsigned int newidx = sel.empty() ? area_shape_index_ : sel[0];

        if (newidx != area_shape_index_) {
            areaShapeSelect(newidx, false);
        }
        enableListener();
    }
}


void MasksPanel::onAreaShapeResetPressed()
{
    if (selected_ < masks_.size()) {
        disableListener();
        setGeometryType(rteMaskShape::Type::RECTANGLE);
        deleteGeometry();
        if (areaMaskToggle->get_active()) {
            unsubscribe();
            areaMaskToggle->set_active(false);
        }
        listEdited = true;
        masks_[selected_].areaMask.shapes.clear();
        area_shape_index_ = 0;
        center_x_ = defaultAreaShape.x;
        center_y_ = defaultAreaShape.y;
        width_ = defaultAreaShape.width;
        height_ = defaultAreaShape.height;
        angle_ = defaultAreaShape.angle;
        updateRectangleAreaMask(true);
        populateShapeList(selected_, area_shape_index_);
        clearPolygon();
        updateGeometry();
        maskShow(selected_, true);        
        enableListener();
        auto l = getListener();
        if (l) {
            l->panelChanged(areaMaskEvent(), M("GENERAL_CHANGED"));
        }
    }
}


void MasksPanel::shapeAddPressed(Shape::Type type, bool list_only)
{
    if (selected_ < masks_.size()) {
        listEdited = true;
        auto &am = masks_[selected_].areaMask;
        am.shapes.emplace_back();
        switch (type) {
        case Shape::Type::RECTANGLE:
            am.shapes.back().reset(new rtengine::procparams::AreaMask::Rectangle(defaultAreaShape));
            for (size_t j = am.shapes.size()-1; j > 0; --j) {
                if (am.shapes[j-1]->getType() == Shape::Type::RECTANGLE) {
                    am.shapes.back() = am.shapes[j-1]->clone();
                    am.shapes.back()->feather = 0;
                    am.shapes.back()->blur = 0;
                    break;
                }
            }
            break;
        case Shape::Type::GRADIENT:
            am.shapes.back().reset(new rtengine::procparams::AreaMask::Gradient());
            break;
        case Shape::Type::POLYGON:
            am.shapes.back().reset(new rtengine::procparams::AreaMask::Polygon());
            break;
        default:
            break;
        }
        populateShapeList(selected_, -1);
        areaShapeSelect(am.shapes.size()-1, true);
        maskShow(selected_, list_only);
        auto l = getListener();
        if (l) {
            l->panelChanged(areaMaskEvent(), M("GENERAL_CHANGED"));
        }
    }
}


void MasksPanel::onAreaShapeAddPressed()
{
    // shape type automatically chosen, depending on the currently selected shape
    Shape::Type t = Shape::Type::RECTANGLE;
    if (selected_ < masks_.size() && area_shape_index_ < masks_[selected_].areaMask.shapes.size() && masks_[selected_].areaMask.shapes.size() > 0) {
        t = masks_[selected_].areaMask.shapes[area_shape_index_]->getType();
    }
    shapeAddPressed(t, true);
}


void MasksPanel::onAreaShapeRemovePressed()
{
    if (selected_ < masks_.size() && area_shape_index_ < masks_[selected_].areaMask.shapes.size()) {// && masks_[selected_].areaMask.shapes.size() > 1) {
        listEdited = true;
        masks_[selected_].areaMask.shapes.erase(masks_[selected_].areaMask.shapes.begin() + area_shape_index_);
        populateShapeList(selected_, -1);
        areaShapeSelect(area_shape_index_ > 0 ? area_shape_index_ - 1 : 0, true);
        maskShow(selected_, true);
        auto l = getListener();
        if (l) {
            l->panelChanged(areaMaskEvent(), M("GENERAL_CHANGED"));
        }
    }
}


void MasksPanel::onAreaShapeUpDownPressed(bool up)
{
    if (selected_ < masks_.size() && area_shape_index_ < masks_[selected_].areaMask.shapes.size() && (up ? area_shape_index_ > 0 : area_shape_index_ + 1 < masks_[selected_].areaMask.shapes.size()) && masks_[selected_].areaMask.shapes.size() > 1) {
        listEdited = true;
        auto &shapes = masks_[selected_].areaMask.shapes;
        int off = up ? -1 : 1;
        std::swap(shapes[area_shape_index_], shapes[area_shape_index_+off]);
        populateShapeList(selected_, -1);
        areaShapeSelect(area_shape_index_ + off, true);
        maskShow(selected_, true);
        auto l = getListener();
        if (l) {
            l->panelChanged(areaMaskEvent(), M("GENERAL_CHANGED"));
        }
    }
}


void MasksPanel::toggleAreaShapeMode(int i)
{
    for (int j = 0; j < 3; ++j) {
        ConnectionBlocker blocker(areaMaskModeConn[j]);
        areaMaskMode[j]->set_active(i == j);
    }
}


void MasksPanel::updateShapeButtonsSensitivity()
{
    bool has_shape = false;
    // bool is_rectangle = false;
    if (selected_ < masks_.size()) {
        auto &a = masks_[selected_].areaMask;
        if (area_shape_index_ < a.shapes.size()) {
            has_shape = true;
            // is_rectangle = a.shapes[area_shape_index_]->getType() == Shape::Type::RECTANGLE;
        }
    }
    if (!has_shape && areaMaskToggle->get_active()) {
        switchOffEditMode();
    }
    areaMaskCopy->set_sensitive(has_shape);
    areaMaskToggle->set_sensitive(has_shape);
    // areaMaskDrawRectangle->set_sensitive(is_rectangle);
    for (int i = 0; i < 3; i++) {
        areaMaskMode[i]->set_sensitive(has_shape);
    }
}


int MasksPanel::getAreaShapeMode()
{
    for (int j = 0; j < 3; ++j) {
        if (areaMaskMode[j]->get_active()) {
            return j;
        }
    }
    return 0;
}


void MasksPanel::onAreaShapeModeChanged(int i)
{
    if (!areaMaskMode[i]->get_active()) {
        ConnectionBlocker blocker(areaMaskModeConn[i]);
        areaMaskMode[i]->set_active(true);
        return;
    }
    toggleAreaShapeMode(i);
    onAreaShapeSelectionChanged();
    populateShapeList(selected_, area_shape_index_);
    auto l = getListener();
    if (l) {
        l->panelChanged(areaMaskEvent(), M("GENERAL_CHANGED"));
    }
    maskShow(selected_, true);
}


void MasksPanel::populateShapeList(int idx, int sel)
{
    ConnectionBlocker b(shapeSelectionConn);
    areaMaskShapes->clear_items();
    auto &r = masks_[idx];
    const auto rd =
        [](double v) -> double
        {
            return int(v * 10) / 10.0;
        };

    const auto m =
        [](Shape::Mode mode) -> const char *
        {
            switch (mode) {
            case Shape::SUBTRACT: return " (-)";
            case Shape::INTERSECT: return " (*)";
            default: return "";
            }
        };
    
    for (size_t i = 0; i < r.areaMask.shapes.size(); ++i) {
        auto &a = r.areaMask.shapes[i];
        auto j = areaMaskShapes->append(std::to_string(i+1));
        Glib::ustring label;
        switch (a->getType()){
        case Shape::Type::POLYGON: {
            auto poly = static_cast<rtengine::procparams::AreaMask::Polygon*>(a.get());
            label = Glib::ustring::compose(M("TP_LABMASKS_AREA_SHAPE_POLY_NONEMPTY") + "%2", poly->knots.size(), m(poly->mode));
            label += Glib::ustring::compose(" | %1 %2", rd(a->feather), rd(a->blur));
        } break;
        case Shape::Type::GRADIENT: {
            auto gradient = static_cast<rtengine::procparams::AreaMask::Gradient*>(a.get());
            label = Glib::ustring::compose("%1 %2 %3 %4 %5 %6 %7", rd(gradient->x), rd(gradient->y), rd(gradient->strengthStart), rd(gradient->strengthEnd), rd(gradient->angle), rd(gradient->feather), m(gradient->mode));
        } break;
        case Shape::Type::RECTANGLE:
        default: {
            auto rect = static_cast<rtengine::procparams::AreaMask::Rectangle*>(a.get());
            label = Glib::ustring::compose("%1 %2 %3 %4 %5 %6 %7", rd(rect->x), rd(rect->y), rd(rect->width), rd(rect->height), rd(rect->angle), rd(rect->roundness), m(rect->mode));
            label += Glib::ustring::compose(" | %1 %2", rd(a->feather), rd(a->blur));
        } break;
        }
        areaMaskShapes->set_text(j, 1, label);
    }
    if (sel >= 0 && size_t(sel) < r.areaMask.shapes.size()) {
        Gtk::TreePath pth;
        pth.push_back(sel);
        areaMaskShapes->get_selection()->select(pth);
        setGeometryType(r.areaMask.shapes[sel]->getType());
    }
    updateShapeButtonsSensitivity();
}


void MasksPanel::onAreaMaskCopyPressed()
{
    if (selected_ < masks_.size()) {
        clipboard.setAreaMask(masks_[selected_].areaMask);
    }
}


void MasksPanel::onAreaMaskPastePressed()
{
    if (selected_ < masks_.size() && clipboard.hasAreaMask()) {
        listEdited = true;
        disableListener();
        masks_[selected_].areaMask = clipboard.getAreaMask();
        auto &a = masks_[selected_].areaMask;
        area_shape_index_ = 0;
        if (area_shape_index_ < a.shapes.size()) {
            switch (a.shapes[area_shape_index_]->getType()) {
            case Shape::Type::POLYGON:
            {
                auto s = static_cast<rtengine::procparams::AreaMask::Polygon*>(a.shapes[area_shape_index_].get());
                setAdjustersVisibility(false, Shape::Type::POLYGON);
                setPolygon(s->knots);
                break;
            }
            case Shape::Type::GRADIENT:
            {
                auto s = static_cast<rtengine::procparams::AreaMask::Gradient*>(a.shapes[area_shape_index_].get());
                setAdjustersVisibility(false, Shape::Type::GRADIENT);
                center_x_ = s->x;
                center_y_ = s->y;
                strength_start_ = s->strengthStart;
                strength_end_ = s->strengthEnd;
                feather_ = s->feather;
                angle_ = s->angle;
                break;
            }
            case Shape::Type::RECTANGLE:
            default:
            {
                auto s = static_cast<rtengine::procparams::AreaMask::Rectangle*>(a.shapes[area_shape_index_].get());
                setAdjustersVisibility(true, Shape::Type::RECTANGLE);
                center_x_ = s->x;
                center_y_ = s->y;
                width_ = s->width;
                height_ = s->height;
                angle_ = s->angle;
                areaMaskRoundness->setValue(s->roundness);
                updateRectangleAreaMask(true);
            }
            }
            auto &s = a.shapes[area_shape_index_];
            setGeometryType(a.shapes[area_shape_index_]->getType());
            updateGeometry();
            areaMaskShapeFeather->setValue(s->feather);
            areaMaskShapeBlur->setValue(s->blur);
            areaMaskContrast->setCurve(a.contrast);
            toggleAreaShapeMode(int(s->mode));
        }
        areaMaskFeather->setValue(a.feather);
        areaMaskBlur->setValue(a.blur);
        populateShapeList(selected_, area_shape_index_);
        maskShow(selected_, true);        
        enableListener();
        auto l = getListener();
        if (l) {
            l->panelChanged(areaMaskEvent(), M("GENERAL_CHANGED"));
        }
    }
}


void MasksPanel::areaShapeSelect(int sel, bool update_list)
{
    area_shape_index_ = sel;
    if (size_t(sel) < masks_[selected_].areaMask.shapes.size()) {
        auto &ns = masks_[selected_].areaMask.shapes[sel];
        switch (ns->getType()) {
        case Shape::Type::POLYGON:
        {
            auto s = static_cast<rtengine::procparams::AreaMask::Polygon*>(ns.get());
            setPolygon(s->knots);
            setAdjustersVisibility(false, Shape::Type::POLYGON);
            setGeometryType(Shape::Type::POLYGON);
            break;
        }
        case Shape::Type::GRADIENT:
        {
            auto s = static_cast<rtengine::procparams::AreaMask::Gradient*>(ns.get());
            setAdjustersVisibility(true, Shape::Type::GRADIENT);
            center_x_ = s->x;
            center_y_ = s->y;
            strength_start_ = s->strengthStart;
            strength_end_ = s->strengthEnd;
            feather_ = s->feather;
            angle_ = s->angle;
            setGeometryType(Shape::Type::GRADIENT);
            updateGradientAreaMask(true);
            break;
        }
        case Shape::Type::RECTANGLE:
        default:
        {
            auto s = static_cast<rtengine::procparams::AreaMask::Rectangle*>(ns.get());
            setAdjustersVisibility(true, Shape::Type::RECTANGLE);
            center_x_ = s->x;
            center_y_ = s->y;
            width_ = s->width;
            height_ = s->height;
            angle_ = s->angle;
            areaMaskRoundness->setValue(s->roundness);
            setGeometryType(Shape::Type::RECTANGLE);
            updateRectangleAreaMask(true);
        }
        }
        updateGeometry();

        areaMaskShapeFeather->setValue(ns->feather);
        areaMaskShapeBlur->setValue(ns->blur);

        toggleAreaShapeMode(int(ns->mode));
        if (areaMaskToggle->get_active()) {
            areaMaskToggle->set_active(false);
            areaMaskToggle->set_active(true);
        }
    } else {
        updateGeometry();
        toggleAreaShapeMode(0);
        setAdjustersVisibility(false, Shape::Type::RECTANGLE);
        update_list = false;
    }

    updateShapeButtonsSensitivity();

    if (update_list) {
        ConnectionBlocker b(shapeSelectionConn);
        Gtk::TreePath pth;
        pth.push_back(sel);
        areaMaskShapes->get_selection()->select(pth);
    }
}


void MasksPanel::setAreaDrawListener(AreaDrawListener *l)
{
    adl_ = l;
}


void MasksPanel::updateRectangleArea(AreaDrawUpdater::Phase phase, int x1, int y1, int x2, int y2)
{
    EditDataProvider *provider = getEditProvider();

    if (!provider) {
        return;
    }

    int imW = 0, imH = 0;
    provider->getImageSize(imW, imH);
    if (!imW || !imH) {
        return;
    }

    if (x1 > x2) {
        std::swap(x1, x2);
    }
    if (y1 > y2) {
        std::swap(y1, y2);
    }
    width_ = float(x2 - x1) * 100.f / imW;
    height_ = float(y2 - y1) * 100.f / imH;
    center_x_ = (x1 + (x2 - x1)/2 - imW/2) * 200.f / imW;
    center_y_ = (y1 + (y2 - y1)/2 - imH/2) * 200.f / imH;
    angle_ = 0;
    if (phase == AreaDrawUpdater::BEGIN) {
        subscribe();
    }
    updateGeometry();
    if (phase == AreaDrawUpdater::END) {
        updateRectangleAreaMask(true);
        onAreaShapeSelectionChanged();
        populateShapeList(selected_, area_shape_index_);
        ConnectionBlocker blocker(areaMaskDrawConn);
        // areaMaskDrawRectangle->set_active(false);
        areaMaskToggle->set_active(true);

        auto l = getListener();
        if (l) {
            l->panelChanged(areaMaskEvent(), M("GENERAL_CHANGED"));
        }
    }
}


void MasksPanel::cancelUpdateRectangleArea()
{
    updateRectangleAreaMask(false);
    areaMaskToggle->set_active(true);
}


// void MasksPanel::onRectangleAreaMaskDrawChanged()
// {
//     if (adl_) {
//         if (areaMaskDrawRectangle->get_active()) {
//             areaMaskToggle->set_active(false);
//             adl_->startRectangleDrawingArea(this);
//         } else {
//             adl_->stopRectangleDrawingArea();
//         }
//     }
// }


void MasksPanel::onAreaMaskDrawRectangleAddPressed()
{
    shapeAddPressed(Shape::Type::RECTANGLE, false);
    // areaMaskDrawRectangle->set_active(true);
//    areaMaskToggle->set_active(true);
    if (adl_) {
        adl_->startRectangleDrawingArea(this);
    }
}

void MasksPanel::onAreaMaskDrawPolygonAddPressed()
{
    shapeAddPressed(Shape::Type::POLYGON, false);
    areaMaskToggle->set_active(true);
}

void MasksPanel::onAreaMaskDrawGradientAddPressed()
{
    shapeAddPressed(Shape::Type::GRADIENT, false);
    areaMaskToggle->set_active(true);
}



void MasksPanel::on_map()
{
    Gtk::VBox::on_map();
    if (first_mask_exp_) {
        // parametricMask->set_expanded(false);
        // areaMask->set_expanded(false);
        // deltaEMask->set_expanded(false);
        // drawnMask->set_expanded(false);
        // mask_exp_->set_expanded(false);
        for (auto e : mask_expanders_) {
            e->set_expanded(false);
        }
        first_mask_exp_ = false;
    }
}


void MasksPanel::onMaskFold(GdkEventButton *evt)
{
    if (mask_exp_->get_expanded()) {
        if (showMask->get_active()) {
            showMask->set_active(false);
        }
        if (isCurrentSubscriber()) {
            switchOffEditMode();
        }
    }
}


void MasksPanel::on_hide()
{
    if (isCurrentSubscriber()) {
        switchOffEditMode();
    }
}


void MasksPanel::onParametricMaskEnableToggled()
{
    auto l = getListener();
    if (l) {
        l->panelChanged(EvParametricMask, parametricMask->getEnabled() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}


void MasksPanel::onDeltaEMaskEnableToggled()
{
    auto l = getListener();
    if (l) {
        l->panelChanged(EvDeltaEMask, deltaEMask->getEnabled() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}


inline const rtengine::ProcEvent &MasksPanel::deltaEMaskEvent() const
{
    return deltaEMask->getEnabled() ? EvDeltaEMask : EvDeltaEMaskVoid;
}


void MasksPanel::onDeltaEPickClicked()
{
    static_cast<DeltaEArea *>(deltaEColor)->activateSpot();
}


void MasksPanel::onDeltaESpotRequested(rtengine::Coord pos)
{
    if (deltaE_provider_) {
        deltaEMask->set_sensitive(false);
        float L, C, H;
        if (deltaE_provider_->getDeltaELCH(static_cast<DeltaEArea *>(deltaEColor)->getEditID(), pos, L, C, H)) {
            double b, t;
            deltaEL->getValue(b, t);
            deltaEL->setValue(b, L);
            deltaEC->getValue(b, t);
            deltaEC->setValue(b, C);
            deltaEH->getValue(b, t);
            deltaEH->setValue(b, H);
            static_cast<DeltaEArea *>(deltaEColor)->setColor(L, C, H);
            auto l = getListener();
            if (l) {
                l->panelChanged(EvDeltaEMask, M("GENERAL_CHANGED"));
            }
        }
        deltaEMask->set_sensitive(true);
    }
    static_cast<DeltaEArea *>(deltaEColor)->switchOffEditMode();
}


void MasksPanel::setDeltaEColorProvider(DeltaEColorProvider *p)
{
    deltaE_provider_ = p;
}


void MasksPanel::onListEnabledToggled(const Glib::ustring &path)
{
    auto it = list_model_->get_iter(path);
    if (it) {
        auto row = *it;
        row[list_model_columns_->enabled] = !row[list_model_columns_->enabled];
        auto idx = row[list_model_columns_->id] - 1;
        auto &r = masks_[idx];
        r.enabled = row[list_model_columns_->enabled];
        auto l = getListener();
        if (l) {
            l->panelChanged(EvMaskList, M("HISTORY_CHANGED"));
        }
    }
}


void MasksPanel::setListEnabled(Gtk::CellRenderer *renderer, const Gtk::TreeModel::iterator &it)
{
    auto row = *it;
    static_cast<Gtk::CellRendererToggle *>(renderer)->set_active(row[list_model_columns_->enabled]);
}


void MasksPanel::onDrawnMaskUpdated()
{
    auto l = getListener();
    if (l) {
        l->panelChanged(EvDrawnMask, M("GENERAL_CHANGED"));
    }
}


bool MasksPanel::onMaskNameFocusOut(GdkEventFocus *e)
{
    Glib::ustring curname = selected_ < masks_.size() ? masks_[selected_].name : "";
    auto l = getListener();
    if (l) {
        l->panelChanged(EvMaskName, M("GENERAL_CHANGED"));
    }
    maskShow(selected_, true);
    const bool changed = !curname.empty() && curname != maskName->get_text();
    if (l && changed && used_linked_masks_.find(curname.raw()) != used_linked_masks_.end()) {
        l->panelChanged(EvLinkedMask, M("GENERAL_CHANGED"));
    }
    return false;
}


void MasksPanel::onMaskExpanded(GdkEventButton *evt, MyExpander *exp)
{
    if (evt->button == 3) {
        exp->set_expanded(true);
        for (auto e : mask_expanders_) {
            if (e != exp) {
                e->set_expanded(false);
            }
        }
    }
}


void MasksPanel::onMaskCopyPressed()
{
    if (selected_ < masks_.size()) {
        clipboard.setMask(masks_[selected_]);
    }
}


void MasksPanel::onMaskPastePressed()
{
    if (selected_ < masks_.size() && clipboard.hasMask()) {
        auto name = masks_[selected_].name;
        auto enabled = masks_[selected_].enabled;
        masks_[selected_] = clipboard.getMask();
        masks_[selected_].name = name;
        masks_[selected_].enabled = enabled;
        maskShow(selected_, false);

        auto l = getListener();
        if (l) {
            l->panelChanged(EvMaskList, M("HISTORY_CHANGED"));
        }
    }
}


void MasksPanel::updateLinkedMaskList(const rtengine::procparams::ProcParams *params)
{
    static std::unordered_map<std::string, std::string> tools_labels;
    Glib::ustring mytoolname = cp_->getToolName();
    if (params) {
        available_linked_masks_.clear();
        used_linked_masks_.clear();
        const auto add =
            [&](const std::vector<rtengine::procparams::Mask> &masks, const Glib::ustring &tool, const Glib::ustring &mytoolname) -> void
            {
                for (unsigned int i = 0, n = masks.size(); i < n; ++i) {
                    auto &m = masks[i];
                    if (!m.name.empty()) {
                        available_linked_masks_.emplace_back(tool, m.name, i);
                    }
                    if (m.linkedMask.enabled && m.linkedMask.toolname == mytoolname) {
                        used_linked_masks_.insert(m.linkedMask.name.raw());
                    }
                }
            };
        auto mp = params->get_maskable();
        for (auto p : mp) {
            add(p->get_masks(), p->get_name(), mytoolname);
            if (p->get_name() == mytoolname) {
                break;
            }
        }
        if (tools_labels.empty()) {
            for (auto p : mp) {
                tools_labels[p->get_name()] = p->get_gui_label();
            }
        }
    }
    disableListener();
    linked_mask_value_->remove_all();
    linked_mask_value_->append("(" + M("GENERAL_NONE") + ")");
    int n = 0;
    for (auto &p : available_linked_masks_) {
        if (p.toolname == mytoolname && p.idx >= selected_) {
            break;
        }
        Glib::ustring label(p.toolname);
        auto it = tools_labels.find(label);
        if (it != tools_labels.end()) {
            label = M(it->second);
        }
        linked_mask_value_->append(label + " ▸ " + p.name);
        ++n;
    }
    linked_mask_value_->set_active(0);
    if (selected_ < masks_.size()) {
        auto &m = masks_[selected_];
        for (int i = 0; i < n; ++i) {
            if (available_linked_masks_[i].toolname == m.linkedMask.toolname && available_linked_masks_[i].name == m.linkedMask.name) {
                linked_mask_value_->set_active(i+1);
                break;
            }
        }
    }
    enableListener();
}


void MasksPanel::onLinkedMaskChanged()
{
    auto l = getListener();
    if (l) {
        l->panelChanged(EvLinkedMask, M("GENERAL_CHANGED"));
    }
}


void MasksPanel::onExternalMaskChanged()
{
    auto l = getListener();
    if (l) {
        l->panelChanged(EvExternalMask, M("GENERAL_CHANGED"));
    }
}


void MasksPanel::setExternalMaskPath(const Glib::ustring &dir)
{
    external_mask_filename_->set_current_folder(Glib::filename_from_utf8(dir));
}
