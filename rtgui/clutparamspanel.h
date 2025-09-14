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
#pragma once

#include <gtkmm.h>
#include "toolpanel.h"
#include "adjuster.h"
#include "curvelistener.h"
#include "../rtengine/clutparams.h"


class CLUTParamsPanel: public Gtk::VBox, public AdjusterListener, public CurveListener {
public:
    CLUTParamsPanel();

    void setParams(const rtengine::CLUTParamDescriptorList &params);
    void setValue(const rtengine::CLUTParamValueMap &val);
    rtengine::CLUTParamValueMap getValue() const;
    
    sigc::signal<void> signal_changed() { return sig_changed_; }

    void adjusterChanged(Adjuster *a, double v) override { emit_signal(); }
    void adjusterAutoToggled(Adjuster *a, bool v) override {}
    void curveChanged() override { emit_signal(); }
    void curveChanged(CurveEditor *ce) override { emit_signal(); }
    
    Gtk::SizeRequestMode get_request_mode_vfunc () const override;

private:
    void emit_signal();
    void apply_preset();
    
    sigc::signal<void> sig_changed_;
    bool sig_blocked_;
    rtengine::CLUTParamDescriptorList params_;
    std::vector<void *> widgets_;
    sigc::connection presets_conn_;
    MyComboBoxText *presets_combo_;
};
