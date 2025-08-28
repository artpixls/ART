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
#include "maskspanel.h"

class Smoothing: public ToolParamBlock, public AdjusterListener, public FoldableToolPanel, public PParamsChangeListener
{
public:

    Smoothing();

    void read(const rtengine::procparams::ProcParams *pp) override;
    void write(rtengine::procparams::ProcParams *pp) override;
    void setDefaults(const rtengine::procparams::ProcParams *defParams) override;

    void adjusterChanged(Adjuster *a, double newval) override;
    void enabledChanged() override;
    void adjusterAutoToggled(Adjuster *a, bool newval) override {}

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

private:
    void regionGet(int idx);
    void regionShow(int idx);
    void channelChanged();
    void modeChanged();
    
    rtengine::ProcEvent EvEnabled;
    rtengine::ProcEvent EvChannel;
    rtengine::ProcEvent EvRadius;
    rtengine::ProcEvent EvEpsilon;
    rtengine::ProcEvent EvIterations;
    rtengine::ProcEvent EvMode;
    rtengine::ProcEvent EvSigma;
    rtengine::ProcEvent EvFalloff;
    rtengine::ProcEvent EvNLStrength;
    rtengine::ProcEvent EvNLDetail;
    rtengine::ProcEvent EvNumBlades;
    rtengine::ProcEvent EvAngle;
    rtengine::ProcEvent EvCurvature;
    rtengine::ProcEvent EvOffset;
    rtengine::ProcEvent EvNoiseStrength;
    rtengine::ProcEvent EvNoiseCoarseness;
    rtengine::ProcEvent EvHalationSize;
    rtengine::ProcEvent EvHalationColor;
    rtengine::ProcEvent EvWavStrength;
    rtengine::ProcEvent EvWavLevels;
    rtengine::ProcEvent EvWavGamma;

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

    std::vector<rtengine::procparams::SmoothingParams::Region> data;

    friend class SmoothingMasksContentProvider;
    std::unique_ptr<MasksContentProvider> masks_content_provider_;
    MasksPanel *masks_;

    MyComboBoxText *channel;
    MyComboBoxText *mode;
    Adjuster *radius;
    Adjuster *epsilon;
    Adjuster *iterations;
    Adjuster *sigma;
    Adjuster *falloff;
    Adjuster *nlstrength;
    Adjuster *nldetail;
    Adjuster *numblades;
    Adjuster *angle;
    Adjuster *curvature;
    Adjuster *offset;
    Adjuster *noise_strength;
    Adjuster *noise_coarseness;
    Adjuster *halation_size;
    Adjuster *halation_color;
    Adjuster *wav_strength;
    Adjuster *wav_levels;
    Adjuster *wav_gamma;
    Gtk::VBox *box;
    Gtk::HBox *chan_box;
    Gtk::VBox *guided_box;
    Gtk::VBox *gaussian_box;
    Gtk::VBox *nl_box;
    Gtk::VBox *lens_motion_box;
    Gtk::VBox *noise_box;
    Gtk::VBox *halation_box;
    Gtk::VBox *wavelets_box;

    rtengine::procparams::SmoothingParams initial_params;
};

