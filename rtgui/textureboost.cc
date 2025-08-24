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
#include "textureboost.h"
#include "eventmapper.h"
#include <iomanip>
#include <cmath>

using namespace rtengine;
using namespace rtengine::procparams;

//-----------------------------------------------------------------------------
// EPDMasksContentProvider
//-----------------------------------------------------------------------------

class EPDMasksContentProvider: public LabMasksContentProvider {
public:
    EPDMasksContentProvider(TextureBoost *parent):
        parent_(parent)
    {
    }

    Glib::ustring getToolName() override
    {
        return "textureboost";
    }

    Gtk::Widget *getWidget() override
    {
        return parent_->box;
    }

    void getEvents(rtengine::ProcEvent &mask_list, rtengine::ProcEvent &parametric_mask, rtengine::ProcEvent &h_mask, rtengine::ProcEvent &c_mask, rtengine::ProcEvent &l_mask, rtengine::ProcEvent &blur, rtengine::ProcEvent &show, rtengine::ProcEvent &area_mask, rtengine::ProcEvent &deltaE_mask, rtengine::ProcEvent &contrastThreshold_mask, rtengine::ProcEvent &drawn_mask, rtengine::ProcEvent &mask_postprocess, rtengine::ProcEvent &raster_mask) override
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
        raster_mask = parent_->EvRasterMask;
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
        parent_->data.insert(parent_->data.begin() + idx, TextureBoostParams::Region());
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

    bool resetPressed(int idx) override
    {
        parent_->data[idx] = TextureBoostParams::Region();
        //parent_->labMasks->setMasks({ Mask() }, -1);
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

    int getColumnCount() override
    {
        return 1;
    }
    
    Glib::ustring getColumnHeader(int col) override
    {
        return M("TP_EPD_LIST_TITLE");
    }
    
    Glib::ustring getColumnContent(int col, int row) override
    {
        auto &r = parent_->data[row];

        return Glib::ustring::compose(
            "%1 %2 %3", r.strength, r.detailThreshold, r.iterations); 
    }

    void getEditIDs(EditUniqueID &hcurve, EditUniqueID &ccurve, EditUniqueID &lcurve, EditUniqueID &deltaE) override
    {
        hcurve = EUID_LabMasks_H4;
        ccurve = EUID_LabMasks_C4;
        lcurve = EUID_LabMasks_L4;
        deltaE = EUID_LabMasks_DE4;
    }

private:
    TextureBoost *parent_;
};


//-----------------------------------------------------------------------------
// EPD
//-----------------------------------------------------------------------------

TextureBoost::TextureBoost(): FoldableToolPanel(this, "epd", M("TP_EPD_LABEL"), true, true, true)
{
    auto m = ProcEventMapper::getInstance();
    auto EVENT = LUMINANCECURVE;
    EvIterations = m->newEvent(EVENT, "HISTORY_MSG_EPD_ITERATIONS");
    EvDetailThreshold = m->newEvent(EVENT, "HISTORY_MSG_EPD_DETAIL_THRESHOLD");
    EvList = m->newEvent(EVENT, "HISTORY_MSG_EPD_LIST");
    EvParametricMask = m->newEvent(EVENT, "HISTORY_MSG_EPD_PARAMETRICMASK");
    EvHueMask = m->newEvent(EVENT, "HISTORY_MSG_EPD_HUEMASK");
    EvChromaticityMask = m->newEvent(EVENT, "HISTORY_MSG_EPD_CHROMATICITYMASK");
    EvLightnessMask = m->newEvent(EVENT, "HISTORY_MSG_EPD_LIGHTNESSMASK");
    EvMaskBlur = m->newEvent(EVENT, "HISTORY_MSG_EPD_MASKBLUR");
    EvShowMask = m->newEvent(EVENT, "HISTORY_MSG_EPD_SHOWMASK");
    EvAreaMask = m->newEvent(EVENT, "HISTORY_MSG_EPD_AREAMASK");
    EvDeltaEMask = m->newEvent(EVENT, "HISTORY_MSG_EPD_DELTAEMASK");
    EvContrastThresholdMask = m->newEvent(EVENT, "HISTORY_MSG_EPD_CONTRASTTHRESHOLDMASK");
    EvDrawnMask = m->newEvent(EVENT, "HISTORY_MSG_EPD_DRAWNMASK");
    EvMaskPostprocess = m->newEvent(EVENT, "HISTORY_MSG_EPD_MASK_POSTPROCESS");
    EvRasterMask = m->newEvent(EVENT, "HISTORY_MSG_EPD_RASTERMASK");

    EvToolEnabled.set_action(EVENT);
    EvToolReset.set_action(EVENT);

    strength = Gtk::manage(new Adjuster (M("TP_EPD_STRENGTH"), -2.0, 2.0, 0.01, 0));
    strength->setLogScale(2, 0, true);
    detailThreshold = Gtk::manage(new Adjuster (M("TP_EPD_DETAIL_THRESHOLD"), 0.01, 100.0, 0.01, 0.2));
    detailThreshold->setLogScale(10, 1, true);
    iterations = Gtk::manage(new Adjuster(M("TP_EPD_ITERATIONS"), 1, 5, 1, 1));

    box = Gtk::manage(new Gtk::VBox());

    strength->setAdjusterListener(this);
    detailThreshold->setAdjusterListener(this);
    iterations->setAdjusterListener(this);

    strength->show();
    detailThreshold->show();
    iterations->show();

    box->pack_start(*strength);
    box->pack_start(*detailThreshold);
    box->pack_start(*iterations);

    labMasksContentProvider.reset(new EPDMasksContentProvider(this));
    labMasks = Gtk::manage(new LabMasksPanel(labMasksContentProvider.get()));
    pack_start(*labMasks, Gtk::PACK_EXPAND_WIDGET, 4);   

    show_all_children();
}

void TextureBoost::read(const ProcParams *pp)
{
    disableListener();

    setEnabled(pp->textureBoost.enabled);
    data = pp->textureBoost.regions;
    auto m = pp->textureBoost.labmasks;
    if (data.empty()) {
        data.emplace_back(rtengine::procparams::TextureBoostParams::Region());
        m.emplace_back(rtengine::procparams::Mask());
    }
    labMasks->setMasks(m, pp->textureBoost.selectedRegion, pp->textureBoost.showMask >= 0 && pp->textureBoost.showMask == pp->textureBoost.selectedRegion);

    enableListener();
}

void TextureBoost::write(ProcParams *pp)
{
    pp->textureBoost.enabled = getEnabled();

    regionGet(labMasks->getSelected());
    pp->textureBoost.regions = data;

    labMasks->getMasks(pp->textureBoost.labmasks, pp->textureBoost.showMask);
    pp->textureBoost.selectedRegion = labMasks->getSelected();
    assert(pp->textureBoost.regions.size() == pp->textureBoost.labmasks.size());

    labMasks->updateSelected();
}

void TextureBoost::setDefaults(const ProcParams *defParams)
{
    strength->setDefault(defParams->textureBoost.regions[0].strength);
    detailThreshold->setDefault(defParams->textureBoost.regions[0].detailThreshold);
    iterations->setDefault(defParams->textureBoost.regions[0].iterations);

    initial_params = defParams->textureBoost;
}

void TextureBoost::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        labMasks->setEdited(true);

        if(a == strength) {
            listener->panelChanged(EvEPDStrength, Glib::ustring::format(std::setw(2), std::fixed, std::setprecision(2), a->getValue()));
        } else if(a == detailThreshold) {
            listener->panelChanged(EvDetailThreshold, Glib::ustring::format(std::setw(2), std::fixed, std::setprecision(2), a->getValue()));
        } else if(a == iterations) {
            listener->panelChanged(EvIterations, a->getTextValue());
        }
    }
}

void TextureBoost::adjusterAutoToggled(Adjuster* a, bool newval)
{
}

void TextureBoost::enabledChanged ()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged (EvEPDEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged (EvEPDEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged (EvEPDEnabled, M("GENERAL_DISABLED"));
        }
    }

    if (listener && !getEnabled()) {
        labMasks->switchOffEditMode();
    }
}


void TextureBoost::setEditProvider(EditDataProvider *provider)
{
    labMasks->setEditProvider(provider);
}


void TextureBoost::procParamsChanged(
    const rtengine::procparams::ProcParams* params,
    const rtengine::ProcEvent& ev,
    const Glib::ustring& descr,
    const ParamsEdited* paramsEdited)
{
    labMasks->updateRasterMaskList(params);
}


void TextureBoost::updateGeometry(int fw, int fh)
{
    labMasks->updateGeometry(fw, fh);
}


void TextureBoost::regionGet(int idx)
{
    if (idx < 0 || size_t(idx) >= data.size()) {
        return;
    }
    
    auto &r = data[idx];
    r.strength = strength->getValue();
    r.detailThreshold = detailThreshold->getValue();
    r.iterations = iterations->getValue();
}


void TextureBoost::regionShow(int idx)
{
    const bool disable = listener;
    if (disable) {
        disableListener();
    }

    auto &r = data[idx];
    strength->setValue(r.strength);
    detailThreshold->setValue(r.detailThreshold);
    iterations->setValue(r.iterations);
    
    if (disable) {
        enableListener();
    }
}


void TextureBoost::setAreaDrawListener(AreaDrawListener *l)
{
    labMasks->setAreaDrawListener(l);
}


void TextureBoost::setDeltaEColorProvider(DeltaEColorProvider *p)
{
    labMasks->setDeltaEColorProvider(p);
}


void TextureBoost::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.textureBoost = initial_params;
    }
    pp.textureBoost.enabled = getEnabled();
    read(&pp);
}
