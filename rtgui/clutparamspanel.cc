/* -*- C++ -*-
 *
 *  This file is part of ART.
 *
 *  Copyright 2023 Alberto Griggio <alberto.griggio@gmail.com>
 *
 *  ART is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  ART is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with ART.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "clutparamspanel.h"
#include "guiutils.h"
#include "multilangmgr.h"
#include "curveeditor.h"
#include "curveeditorgroup.h"
#include <map>

namespace {

class CLUTParamsCurveEditorGroup: public CurveEditorGroup {
public:
    CLUTParamsCurveEditorGroup(Glib::ustring &curveDir, Glib::ustring groupLabel="", float curvesRatio=1.f):
        CurveEditorGroup(curveDir, groupLabel, curvesRatio) {}

    const std::vector<CurveEditor *> getCurveEditors() const { return curveEditors; }
};

} // namespace


CLUTParamsPanel::CLUTParamsPanel():
    sig_blocked_(false),
    presets_combo_(nullptr)
{
    set_orientation(Gtk::ORIENTATION_VERTICAL);
}


Gtk::SizeRequestMode CLUTParamsPanel::get_request_mode_vfunc() const
{
    return Gtk::SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}


void CLUTParamsPanel::setParams(const rtengine::CLUTParamDescriptorList &params)
{
    widgets_.clear();
    for (auto c : get_children()) {
        remove(*c);
    }
    presets_combo_ = nullptr;

    params_ = params;

    if (params.empty()) {
        return;
    }

    const auto lbl =
        [](const Glib::ustring &l) -> Glib::ustring
        {
            if (!l.empty() && l[0] == '$') {
                auto pos = l.find(';');
                if (pos != Glib::ustring::npos) {
                    auto key = l.substr(1, pos-1);
                    auto dflt = l.substr(pos+1);
                    auto res = M(key);
                    return (res == key) ? dflt : res;
                } else {
                    return M(l.c_str()+1);
                }
            } else {
                return l;
            }
        };

    Gtk::VBox *vb = this;

    std::vector<std::pair<Glib::ustring, Gtk::Box *>> groups;
    std::map<Glib::ustring, int> group_is_curve;

    const auto reset =
        [this]() -> void
        {
            setValue({});
            emit_signal();
        };
    Gtk::Button *r = Gtk::manage(new Gtk::Button());
    r->set_tooltip_markup(M("ADJUSTER_RESET_TO_DEFAULT"));
    r->add(*Gtk::manage(new RTImage("undo-small.svg", "redo-small.svg")));
    r->signal_clicked().connect(sigc::slot<void>(reset));
    setExpandAlignProperties(r, false, false, Gtk::ALIGN_END, Gtk::ALIGN_CENTER);
    r->set_relief(Gtk::RELIEF_NONE);
    r->get_style_context()->add_class(GTK_STYLE_CLASS_FLAT);
    r->set_can_focus(false);
    r->set_size_request(-1, 20);
    Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
    auto sep = Gtk::manage(new Gtk::HSeparator());
    sep->set_vexpand(false);
    sep->set_valign(Gtk::ALIGN_CENTER);
    hb->pack_start(*sep);
    hb->pack_start(*r, Gtk::PACK_SHRINK, 2);
    vb->pack_start(*hb);

    const auto &presets = params_.get_presets();
    if (!presets.empty()) {
        Gtk::HBox *b = Gtk::manage(new Gtk::HBox());
        b->pack_start(*Gtk::manage(new Gtk::Label(M("TP_COLORCORRECTION_LUT_PRESET") + ": ")), Gtk::PACK_SHRINK);
        presets_combo_ = Gtk::manage(new MyComboBoxText());
        presets_combo_->append("(" + M("GENERAL_NONE") + ")");
        for (auto &p : presets) {
            presets_combo_->append(lbl(p.second));
        }
        b->pack_start(*presets_combo_, Gtk::PACK_EXPAND_WIDGET);
        vb->pack_start(*b, Gtk::PACK_SHRINK, 4);
        presets_conn_ = presets_combo_->signal_changed().connect(sigc::mem_fun(*this, &CLUTParamsPanel::apply_preset));
    }

    for (auto &d : params) {
        if (!d.gui_group.empty()) {
            switch (d.type) {
            case rtengine::CLUTParamType::PT_CURVE:
            case rtengine::CLUTParamType::PT_FLATCURVE:
            case rtengine::CLUTParamType::PT_FLATCURVE_PERIODIC:
                if (group_is_curve.find(d.gui_group) == group_is_curve.end()) {
                    group_is_curve[d.gui_group] = 1;
                } else if (group_is_curve[d.gui_group] > 0) {
                    ++group_is_curve[d.gui_group];
                }
                break;
            default:
                group_is_curve[d.gui_group] = 0;
            }
        }
    }
    
    for (auto &d : params) {
        void *w = nullptr;
        Gtk::Box *box = nullptr;
        if (!d.gui_group.empty() && !group_is_curve[d.gui_group]) {
            for (auto &p : groups) {
                if (p.first == d.gui_group) {
                    box = p.second;
                    break;
                }
            }
            if (!box) {
                Gtk::Frame *e = Gtk::manage(new Gtk::Frame(lbl(d.gui_group)));
                Gtk::VBox *tb = Gtk::manage(new Gtk::VBox());
                e->set_name("ExpanderBox2");
                e->add(*tb);
                vb->pack_start(*e);
                box = tb;
                groups.emplace_back(d.gui_group, tb);
            }
        }
        if (!box) {
            box = vb;
        }
        bool tooltip_ok = true;
        switch (d.type) {
        case rtengine::CLUTParamType::PT_BOOL: {
            Gtk::CheckButton *b = Gtk::manage(new Gtk::CheckButton(lbl(d.gui_name)));
            b->signal_toggled().connect(sigc::mem_fun(*this, &CLUTParamsPanel::emit_signal));
            w = b;
            box->pack_start(*b);
        }   break;
        case rtengine::CLUTParamType::PT_CHOICE: {
            MyComboBoxText *c = Gtk::manage(new MyComboBoxText());
            for (auto l : d.choices) {
                c->append(lbl(l));
            }
            Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
            hb->pack_start(*Gtk::manage(new Gtk::Label(lbl(d.gui_name) + ": ")), Gtk::PACK_SHRINK);
            hb->pack_start(*c);
            c->signal_changed().connect(sigc::mem_fun(*this, &CLUTParamsPanel::emit_signal));
            w = c;
            box->pack_start(*hb);
        }   break;
        case rtengine::CLUTParamType::PT_CURVE:
        case rtengine::CLUTParamType::PT_FLATCURVE:
        case rtengine::CLUTParamType::PT_FLATCURVE_PERIODIC: {
            CLUTParamsCurveEditorGroup *grp = nullptr;
            bool grp_shared = !d.gui_group.empty() && group_is_curve[d.gui_group];
            Glib::ustring label = "";
            Glib::ustring grp_label = lbl(d.gui_name);
            bool grp_pack = false;
            if (grp_shared) {
                label = lbl(d.gui_name);
                grp_label = lbl(d.gui_group);
                for (auto &p : groups) {
                    if (p.first == d.gui_group) {
                        grp = static_cast<CLUTParamsCurveEditorGroup *>(p.second);
                        break;
                    }
                }
            }
            if (!grp) {
                grp = Gtk::manage(new CLUTParamsCurveEditorGroup(options.lastColorToningCurvesDir, grp_label));
                grp_pack = true;
                if (grp_shared) {
                    groups.emplace_back(d.gui_group, grp);
                }
            }
            grp->setCurveListener(this);
            CurveEditor *ce = nullptr;
            if (d.type == rtengine::CLUTParamType::PT_CURVE) {
                ce = grp->addCurve(CT_Diagonal, label);
                static_cast<DiagonalCurveEditor *>(ce)->setResetCurve(DiagonalCurveType(d.value_default[0]), d.value_default);
            } else {
                ce = grp->addCurve(CT_Flat, label, nullptr, false, d.type == rtengine::CLUTParamType::PT_FLATCURVE_PERIODIC);
                static_cast<FlatCurveEditor *>(ce)->setResetCurve(FlatCurveType(d.value_default[0]), d.value_default);
            }
            if (!d.gui_bottom_gradient.empty()) {
                std::vector<GradientMilestone> ms;
                for (auto &g : d.gui_bottom_gradient) {
                    ms.push_back(GradientMilestone(g[0], g[1], g[2], g[3]));
                }
                ce->setBottomBarBgGradient(ms);
            }
            if (!d.gui_left_gradient.empty()) {
                std::vector<GradientMilestone> ms;
                for (auto &g : d.gui_left_gradient) {
                    ms.push_back(GradientMilestone(g[0], g[1], g[2], g[3]));
                }
                ce->setLeftBarBgGradient(ms);
            }
            if (!grp_shared || int(grp->getCurveEditors().size()) == group_is_curve[d.gui_group]) {
                grp->curveListComplete();
            }
            if (grp_pack) {
                box->pack_start(*grp);
            }
            w = ce;
            if (!d.gui_tooltip.empty()) {
                ce->setTooltip(lbl(d.gui_tooltip));
            }
            tooltip_ok = false;
        }   break;
        case rtengine::CLUTParamType::PT_INT:
        case rtengine::CLUTParamType::PT_FLOAT:
        default: {
            Adjuster *a = Gtk::manage(new Adjuster(lbl(d.gui_name), d.value_min, d.value_max, d.gui_step, d.value_default.empty() ? 0.0 : d.value_default[0]));
            a->setAdjusterListener(this);
            box->pack_start(*a);
            w = a;
        }   break;
        }
        if (!d.gui_tooltip.empty() && tooltip_ok) {
            static_cast<Gtk::Widget *>(w)->set_tooltip_markup(lbl(d.gui_tooltip));
        }
        widgets_.push_back(w);
    }
    show_all_children();
}


rtengine::CLUTParamValueMap CLUTParamsPanel::getValue() const
{
    rtengine::CLUTParamValueMap values;
    
    for (size_t i = 0; i < params_.size(); ++i) {
        auto w = widgets_[i];
        auto &d = params_[i];

        std::vector<double> v = { 0 };
        
        switch (d.type) {
        case rtengine::CLUTParamType::PT_BOOL: 
            v[0] = static_cast<Gtk::CheckButton *>(w)->get_active();
            break;
        case rtengine::CLUTParamType::PT_CHOICE:
            v[0] = static_cast<MyComboBoxText *>(w)->get_active_row_number();
            break;
        case rtengine::CLUTParamType::PT_CURVE:
        case rtengine::CLUTParamType::PT_FLATCURVE:
        case rtengine::CLUTParamType::PT_FLATCURVE_PERIODIC:
            v = static_cast<CurveEditor *>(w)->getCurve();
            break;
        case rtengine::CLUTParamType::PT_INT:
        case rtengine::CLUTParamType::PT_FLOAT:
        default:
            v[0] = static_cast<Adjuster *>(w)->getValue();
            break;
        }

        values[d.name] = v;
    }

    return values;
}


void CLUTParamsPanel::setValue(const rtengine::CLUTParamValueMap &val)
{
    bool prev = sig_blocked_;
    sig_blocked_ = true;

    for (size_t j = params_.size(); j > 0; --j) {
        size_t i = j-1;
        auto w = widgets_[i];
        auto &d = params_[i];

        auto it = val.find(d.name);
        auto vv = it != val.end() ? it->second : d.value_default;
        auto v = vv.empty() ? 0.0 : vv[0];
        
        switch (d.type) {
        case rtengine::CLUTParamType::PT_BOOL: 
            static_cast<Gtk::CheckButton *>(w)->set_active(bool(v));
            break;
        case rtengine::CLUTParamType::PT_CHOICE:
            static_cast<MyComboBoxText *>(w)->set_active(int(v));
            break;
        case rtengine::CLUTParamType::PT_CURVE:
        case rtengine::CLUTParamType::PT_FLATCURVE:
        case rtengine::CLUTParamType::PT_FLATCURVE_PERIODIC: {
            CurveEditor *ce = static_cast<CurveEditor *>(w);
            ce->setCurve(vv);
            ce->openIfNonlinear();
        }   break;
        case rtengine::CLUTParamType::PT_INT:
        case rtengine::CLUTParamType::PT_FLOAT:
        default:
            static_cast<Adjuster *>(w)->setValue(v);
            break;
        }
    }

    const auto &presets = params_.get_presets();
    if (!presets.empty()) {
        ConnectionBlocker b(presets_conn_);
        presets_combo_->set_active(0);
        for (size_t i = 0; i < presets.size(); ++i) {
            rtengine::CLUTParamValueMap pv;
            params_.apply_preset(presets[i].first, pv);
            bool found = true;
            for (auto &p : pv) {
                auto it = val.find(p.first);
                if (it == val.end() || p.second != it->second) {
                    found = false;
                    break;
                }
            }
            if (found) {
                presets_combo_->set_active(i+1);
                break;
            }
        } 
    }

    sig_blocked_ = prev;
}


void CLUTParamsPanel::emit_signal()
{
    if (!sig_blocked_) {
        sig_changed_.emit();
        const auto &presets = params_.get_presets();
        if (!presets.empty()) {
            ConnectionBlocker b(presets_conn_);
            int idx = presets_combo_->get_active_row_number();
            if (idx > 0) {
                rtengine::CLUTParamValueMap pv;
                params_.apply_preset(presets[idx-1].first, pv);
                auto val = getValue();
                for (auto &p : pv) {
                    if (p.second != val[p.first]) {
                        presets_combo_->set_active(0);
                        break;
                    }
                }
            }     
        }
    }
}


void CLUTParamsPanel::apply_preset()
{
    if (presets_combo_) {
        const auto &presets = params_.get_presets();
        auto idx = presets_combo_->get_active_row_number();
        if (idx > 0) {
            auto val = getValue();
            params_.apply_preset(presets[idx-1].first, val);
            setValue(val);
            emit_signal();
        }
    }
}
