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
#pragma once

#include <gtkmm.h>
#include "adjuster.h"
#include "toolpanel.h"

class FilmGrain: public ToolParamBlock, public AdjusterListener, public FoldableToolPanel {
public:
    FilmGrain();

    void read(const rtengine::procparams::ProcParams *pp) override;
    void write(rtengine::procparams::ProcParams *pp) override;
    void setDefaults(const rtengine::procparams::ProcParams *defParams) override;
    void adjusterChanged(Adjuster *a, double newval) override;
    void enabledChanged() override;
    void adjusterAutoToggled(Adjuster* a, bool newval) override {}
    void colorChanged();

    void toolReset(bool to_initial) override;

private:
    MyComboBoxText *color;
    Adjuster *iso;
    Adjuster *strength;

    rtengine::ProcEvent EvEnabled;
    rtengine::ProcEvent EvISO;
    rtengine::ProcEvent EvStrength;
    rtengine::ProcEvent EvColor;

    rtengine::procparams::GrainParams initial_params;
};

