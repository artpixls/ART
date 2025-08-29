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

class EPDMasksContentProvider: public MasksContentProvider {
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

    void getEvents(Events &events) override
    {
        events.mask_list = parent_->EvList;
        events.parametric_mask = parent_->EvParametricMask;
        events.h_mask = parent_->EvHueMask;
        events.c_mask = parent_->EvChromaticityMask;
        events.l_mask = parent_->EvLightnessMask;
        events.blur = parent_->EvMaskBlur;
        events.show = parent_->EvShowMask;
        events.area_mask = parent_->EvAreaMask;
        events.deltaE_mask = parent_->EvDeltaEMask;
        events.contrastThreshold_mask = parent_->EvContrastThresholdMask;
        events.drawn_mask = parent_->EvDrawnMask;
        events.mask_postprocess = parent_->EvMaskPostprocess;
        events.linked_mask = parent_->EvLinkedMask;
        events.external_mask = parent_->EvExternalMask;
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
        //parent_->masks_->setMasks({ Mask() }, -1);
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
        hcurve = EUID_Masks_H4;
        ccurve = EUID_Masks_C4;
        lcurve = EUID_Masks_L4;
        deltaE = EUID_Masks_DE4;
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
    auto EVENT = rtengine::LUMINANCECURVE;
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
    EvLinkedMask = m->newEvent(EVENT, "HISTORY_MSG_EPD_LINKEDMASK");
    EvExternalMask = m->newEvent(EVENT, "HISTORY_MSG_EPD_EXTERNALMASK");

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

    masks_content_provider_.reset(new EPDMasksContentProvider(this));
    masks_ = Gtk::manage(new MasksPanel(masks_content_provider_.get()));
    pack_start(*masks_, Gtk::PACK_EXPAND_WIDGET, 4);   

    show_all_children();
}

void TextureBoost::read(const ProcParams *pp)
{
    disableListener();

    setEnabled(pp->textureBoost.enabled);
    data = pp->textureBoost.regions;
    auto m = pp->textureBoost.masks;
    if (data.empty()) {
        data.emplace_back(rtengine::procparams::TextureBoostParams::Region());
        m.emplace_back(rtengine::procparams::Mask());
    }
    masks_->setMasks(m, pp->textureBoost.selectedRegion, pp->textureBoost.showMask >= 0 && pp->textureBoost.showMask == pp->textureBoost.selectedRegion);

    enableListener();
}

void TextureBoost::write(ProcParams *pp)
{
    pp->textureBoost.enabled = getEnabled();

    regionGet(masks_->getSelected());
    pp->textureBoost.regions = data;

    masks_->getMasks(pp->textureBoost.masks, pp->textureBoost.showMask);
    pp->textureBoost.selectedRegion = masks_->getSelected();
    assert(pp->textureBoost.regions.size() == pp->textureBoost.masks.size());

    masks_->updateSelected();
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
        masks_->setEdited(true);

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
        masks_->switchOffEditMode();
    }
}


void TextureBoost::setEditProvider(EditDataProvider *provider)
{
    masks_->setEditProvider(provider);
}


void TextureBoost::procParamsChanged(
    const rtengine::procparams::ProcParams* params,
    const rtengine::ProcEvent& ev,
    const Glib::ustring& descr,
    const ParamsEdited* paramsEdited)
{
    masks_->updateLinkedMaskList(params);
}


void TextureBoost::updateGeometry(int fw, int fh)
{
    masks_->updateGeometry(fw, fh);
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
    masks_->setAreaDrawListener(l);
}


void TextureBoost::setDeltaEColorProvider(DeltaEColorProvider *p)
{
    masks_->setDeltaEColorProvider(p);
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
