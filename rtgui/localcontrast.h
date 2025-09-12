/** -*- C++ -*-
 *  
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2017 Alberto Griggio <alberto.griggio@gmail.com>
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
#include "curveeditor.h"
#include "curveeditorgroup.h"
#include "maskspanel.h"

class LocalContrast: public ToolParamBlock, public AdjusterListener, public FoldableToolPanel, public CurveListener, public PParamsChangeListener {
private:
    void regionGet(int idx);
    void regionShow(int idx);
    
    std::vector<rtengine::procparams::LocalContrastParams::Region> regionData;

    friend class LocalContrastMasksContentProvider;
    std::unique_ptr<MasksContentProvider> masks_content_provider_;
    MasksPanel *masks_;

    Gtk::Box *box;
    Adjuster *contrast;
    CurveEditorGroup *cg;
    FlatCurveEditor *curve;
    
    rtengine::ProcEvent EvLocalContrastEnabled;
    rtengine::ProcEvent EvLocalContrastContrast;
    rtengine::ProcEvent EvLocalContrastCurve;
    
    rtengine::ProcEvent EvList;
    rtengine::ProcEvent EvParametricMask;
    rtengine::ProcEvent EvHueMask;
    rtengine::ProcEvent EvChromaticityMask;
    rtengine::ProcEvent EvLightnessMask;
    rtengine::ProcEvent EvMaskBlur;
    rtengine::ProcEvent EvShowMask;
    rtengine::ProcEvent EvAreaMask;
    rtengine::ProcEvent EvDeltaEMask;
    rtengine::ProcEvent EvContrastThresholdMask;
    rtengine::ProcEvent EvDrawnMask;
    rtengine::ProcEvent EvMaskPostprocess;
    rtengine::ProcEvent EvLinkedMask;
    rtengine::ProcEvent EvExternalMask;

    rtengine::procparams::LocalContrastParams initial_params;

public:

    LocalContrast();

    void read(const rtengine::procparams::ProcParams *pp) override;
    void write(rtengine::procparams::ProcParams *pp) override;
    void setDefaults(const rtengine::procparams::ProcParams *defParams) override;
    void adjusterChanged(Adjuster *a, double newval) override;
    void adjusterAutoToggled(Adjuster* a, bool newval) override;
    void enabledChanged() override;
    void curveChanged() override;
    void autoOpenCurve() override;

    void setEditProvider(EditDataProvider *provider) override;

    PParamsChangeListener *getPParamsChangeListener() override { return this; }
    void procParamsChanged(
        const rtengine::procparams::ProcParams* params,
        const rtengine::ProcEvent& ev,
        const Glib::ustring& descr,
        const ParamsEdited* paramsEdited = nullptr) override;
    void clearParamChanges() override {}

    void updateGeometry(int fullWidth, int fullHeight);
    void setAreaDrawListener(AreaDrawListener *l);
    void setDeltaEColorProvider(DeltaEColorProvider *p);

    void toolReset(bool to_initial) override;

    void setExternalMaskPath(const Glib::ustring &dir) { masks_->setExternalMaskPath(dir); }
};

