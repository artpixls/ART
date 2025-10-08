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
#include "multilangmgr.h"
#include "toolpanelcoord.h"
#include "options.h"
#include "../rtengine/imagesource.h"
#include "../rtengine/dfmanager.h"
#include "../rtengine/ffmanager.h"
#include "../rtengine/improcfun.h"
#include "../rtengine/procevents.h"
#include "../rtengine/refreshmap.h"
#include "../rtengine/perspectivecorrection.h"
#include "../rtengine/improccoordinator.h"

using namespace rtengine::procparams;


namespace {

class TPScrolledWindow: public MyScrolledWindow {
public:
    TPScrolledWindow(): mgr_(nullptr) {}
    
    void setToolShortcutManager(ToolShortcutManager *mgr) { mgr_ = mgr; }

    bool on_scroll_event(GdkEventScroll *event) override
    {
        if (mgr_ && mgr_->shouldHandleScroll()) {
            return true;
        }
        return MyScrolledWindow::on_scroll_event(event);
    }

private:
    ToolShortcutManager *mgr_;
};

} // namespace


ToolPanelCoordinator::ToolPanelCoordinator (bool batch) : ipc (nullptr), favoritePanelSW(nullptr), hasChanged (false), editDataProvider (nullptr)
{

    favoritePanel = Gtk::manage(new ToolVBox ());
    exposurePanel = Gtk::manage(new ToolVBox ());
    detailsPanel = Gtk::manage(new ToolVBox ());
    colorPanel = Gtk::manage(new ToolVBox ());
    transformPanel = Gtk::manage(new ToolVBox ());
    rawPanel = Gtk::manage(new ToolVBox ());
    // advancedPanel = Gtk::manage(new ToolVBox ());
    localPanel = Gtk::manage(new ToolVBox());
    effectsPanel = Gtk::manage(new ToolVBox());

    coarse              = Gtk::manage (new CoarsePanel ());
    exposure = Gtk::manage(new Exposure());
    saturation = Gtk::manage(new Saturation());
    toneCurve           = Gtk::manage (new ToneCurve ());
    toneEqualizer = Gtk::manage(new ToneEqualizer());
    impulsedenoise      = Gtk::manage (new ImpulseDenoise ());
    defringe            = Gtk::manage (new Defringe ());
    denoise             = Gtk::manage (new Denoise ());
    textureBoost        = Gtk::manage (new TextureBoost());
    sharpening          = Gtk::manage (new Sharpening ());
    localContrast       = Gtk::manage(new LocalContrast());
    lcurve              = Gtk::manage (new LabCurve ());
    rgbcurves           = Gtk::manage (new RGBCurves ());
    geompanel            = Gtk::manage (new GeometryPanel());
    lenspanel           = Gtk::manage(new LensPanel());
    lensProf            = Gtk::manage (new LensProfilePanel ());
    distortion          = Gtk::manage (new Distortion ());
    rotate              = Gtk::manage (new Rotate ());
    whitebalance        = Gtk::manage (new WhiteBalance ());
    vignetting          = Gtk::manage (new Vignetting ());
    gradient            = Gtk::manage (new Gradient ());
    pcvignette          = Gtk::manage (new PCVignette ());
    perspective         = Gtk::manage (new PerspCorrection ());
    cacorrection        = Gtk::manage (new CACorrection ());
    chmixer             = Gtk::manage (new ChMixer ());
    blackwhite          = Gtk::manage (new BlackWhite ());
    hsl = Gtk::manage (new HSLEqualizer());
    resize              = Gtk::manage (new Resize ());
    prsharpening        = Gtk::manage (new PrSharpening());
    crop                = Gtk::manage (new Crop ());
    icm                 = Gtk::manage (new ICMPanel ());
    metadata            = Gtk::manage(new MetaDataPanel());
    filmSimulation      = Gtk::manage (new FilmSimulation ());
    softlight           = Gtk::manage(new SoftLight());
    dehaze              = Gtk::manage(new Dehaze());
    grain               = Gtk::manage(new FilmGrain());
    sensorbayer         = Gtk::manage (new SensorBayer ());
    sensorxtrans        = Gtk::manage (new SensorXTrans ());
    bayerprocess        = Gtk::manage (new BayerProcess ());
    xtransprocess       = Gtk::manage (new XTransProcess ());
    bayerpreprocess     = Gtk::manage (new BayerPreProcess ());
    preprocess          = Gtk::manage (new PreProcess ());
    darkframe           = Gtk::manage (new DarkFrame ());
    flatfield           = Gtk::manage (new FlatField ());
    rawcacorrection     = Gtk::manage (new RAWCACorr ());
    rawexposure         = Gtk::manage (new RAWExposure ());
    bayerrawexposure    = Gtk::manage (new BayerRAWExposure ());
    xtransrawexposure   = Gtk::manage (new XTransRAWExposure ());
    fattal              = Gtk::manage (new FattalToneMapping());
    logenc              = Gtk::manage(new LogEncoding());
    colorcorrection = Gtk::manage(new ColorCorrection());
    smoothing = Gtk::manage(new Smoothing());
    // cbdl = Gtk::manage(new DirPyrEqualizer());
    filmNegative        = Gtk::manage (new FilmNegative ());
    spot                = Gtk::manage (new Spot());


    // So Demosaic, Line noise filter, Green Equilibration, Ca-Correction (garder le nom de section identique!) and Black-Level will be moved in a "Bayer sensor" tool,
    // and a separate Demosaic and Black Level tool will be created in an "X-Trans sensor" tool

    // X-Trans demozaic methods: "3-pass (best), 1-pass (medium), fast"
    // Mettre  jour les profils fournis pour inclure les nouvelles section Raw, notamment pour "Default High ISO"
    // Valeurs par dfaut:
    //     Best -> low ISO
    //     Medium -> High ISO
    favorites.resize(options.favorites.size(), nullptr);

    // exposure
    addfavoritePanel(exposurePanel, exposure);
    addfavoritePanel(exposurePanel, toneEqualizer);    
    addfavoritePanel(exposurePanel, toneCurve);
    addfavoritePanel(exposurePanel, fattal);
    addfavoritePanel(exposurePanel, logenc);

    // details
    addfavoritePanel(detailsPanel, spot);
    addfavoritePanel(detailsPanel, sharpening);
    addfavoritePanel(detailsPanel, denoise);
    addfavoritePanel(detailsPanel, impulsedenoise);
    addfavoritePanel(detailsPanel, defringe);

    // color
    addfavoritePanel(colorPanel, whitebalance);
    addfavoritePanel(colorPanel, saturation);
    addfavoritePanel(colorPanel, chmixer);
    addfavoritePanel(colorPanel, hsl);
    addfavoritePanel(colorPanel, rgbcurves);
    addfavoritePanel(colorPanel, lcurve);
    addfavoritePanel(colorPanel, icm);

    // local
    addfavoritePanel(localPanel, colorcorrection);
    addfavoritePanel(localPanel, smoothing);
    //addfavoritePanel(localPanel, cbdl);
    addfavoritePanel(localPanel, textureBoost);
    addfavoritePanel(localPanel, localContrast);

    // effects
    addfavoritePanel(effectsPanel, blackwhite);
    addfavoritePanel(effectsPanel, filmSimulation);
    addfavoritePanel(effectsPanel, softlight);
    addfavoritePanel(effectsPanel, pcvignette);
    addfavoritePanel(effectsPanel, gradient);
    addfavoritePanel(effectsPanel, dehaze);
    addfavoritePanel(effectsPanel, grain);
    addfavoritePanel(effectsPanel, filmNegative);

    // transform
    addfavoritePanel(transformPanel, crop);
    addfavoritePanel(transformPanel, resize);
    //addfavoritePanel(resize->getPackBox(), prsharpening, 2);
    addfavoritePanel(transformPanel, prsharpening, 2);
    addfavoritePanel(transformPanel, geompanel);
    addfavoritePanel(transformPanel, lenspanel);
    toolPanels.pop_back();
    addfavoritePanel(geompanel->getPackBox(), rotate, 2);
    addfavoritePanel(geompanel->getPackBox(), perspective, 2);
    addfavoritePanel(lenspanel->getPackBox(), lensProf, 2);
    addfavoritePanel(lenspanel->getPackBox(), distortion, 2);
    addfavoritePanel(lenspanel->getPackBox(), cacorrection, 2);
    addfavoritePanel(lenspanel->getPackBox(), vignetting, 2);

    // raw
    addfavoritePanel(rawPanel, sensorbayer);
    addfavoritePanel(sensorbayer->getPackBox(), bayerprocess, 2);
    addfavoritePanel(sensorbayer->getPackBox(), bayerrawexposure, 2);
    addfavoritePanel(sensorbayer->getPackBox(), bayerpreprocess, 2);
    addfavoritePanel(sensorbayer->getPackBox(), rawcacorrection, 2);
    addfavoritePanel(rawPanel, sensorxtrans);
    addfavoritePanel(sensorxtrans->getPackBox(), xtransprocess, 2);
    addfavoritePanel(sensorxtrans->getPackBox(), xtransrawexposure, 2);
    addfavoritePanel(rawPanel, rawexposure);
    addfavoritePanel(rawPanel, preprocess);
    addfavoritePanel(rawPanel, darkframe);
    addfavoritePanel(rawPanel, flatfield);

    int favoriteCount = 0;
    for(auto it = favorites.begin(); it != favorites.end(); ++it) {
        if (*it) {
            addPanel(favoritePanel, *it);
            ++favoriteCount;
        }
    }

    toolPanels.push_back (coarse);
    toolPanels.push_back(metadata);
    toolPanels.push_back(lenspanel);

    toolPanelNotebook = new Gtk::Notebook ();
    toolPanelNotebook->set_name ("ToolPanelNotebook");

    exposurePanelSW    = Gtk::manage (new TPScrolledWindow ());
    detailsPanelSW     = Gtk::manage (new TPScrolledWindow ());
    colorPanelSW       = Gtk::manage (new TPScrolledWindow ());
    transformPanelSW   = Gtk::manage (new TPScrolledWindow ());
    rawPanelSW         = Gtk::manage (new TPScrolledWindow ());
    // advancedPanelSW    = Gtk::manage (new TPScrolledWindow ());
    localPanelSW = Gtk::manage(new TPScrolledWindow());
    effectsPanelSW = Gtk::manage(new TPScrolledWindow());
    updateVScrollbars (options.hideTPVScrollbar);

    // load panel endings
    for (int i = 0; i < 8; i++) {
        vbPanelEnd[i] = Gtk::manage (new Gtk::VBox ());
        imgPanelEnd[i] = Gtk::manage (new RTImage ("ornament1.svg"));
        imgPanelEnd[i]->show ();
        vbPanelEnd[i]->pack_start (*imgPanelEnd[i], Gtk::PACK_SHRINK);
        vbPanelEnd[i]->show_all();
    }

    if(favoriteCount > 0) {
        favoritePanelSW = Gtk::manage(new TPScrolledWindow());
        favoritePanelSW->add(*favoritePanel);
        favoritePanel->pack_start(*Gtk::manage(new Gtk::HSeparator), Gtk::PACK_SHRINK, 0);
        favoritePanel->pack_start(*vbPanelEnd[0], Gtk::PACK_SHRINK, 4);
    }

    exposurePanelSW->add  (*exposurePanel);
    exposurePanel->pack_start (*Gtk::manage (new Gtk::HSeparator), Gtk::PACK_SHRINK, 0);
    exposurePanel->pack_start (*vbPanelEnd[1], Gtk::PACK_SHRINK, 4);

    detailsPanelSW->add   (*detailsPanel);
    detailsPanel->pack_start (*Gtk::manage (new Gtk::HSeparator), Gtk::PACK_SHRINK, 0);
    detailsPanel->pack_start (*vbPanelEnd[2], Gtk::PACK_SHRINK, 4);

    colorPanelSW->add     (*colorPanel);
    colorPanel->pack_start (*Gtk::manage (new Gtk::HSeparator), Gtk::PACK_SHRINK, 0);
    colorPanel->pack_start (*vbPanelEnd[3], Gtk::PACK_SHRINK, 4);

    // advancedPanelSW->add       (*advancedPanel);
    // advancedPanel->pack_start (*Gtk::manage (new Gtk::HSeparator), Gtk::PACK_SHRINK, 0);
    // advancedPanel->pack_start (*vbPanelEnd[6], Gtk::PACK_SHRINK, 0);

    localPanelSW->add(*localPanel);
    localPanel->pack_start(*Gtk::manage(new Gtk::HSeparator), Gtk::PACK_SHRINK, 0);
    localPanel->pack_start(*vbPanelEnd[6], Gtk::PACK_SHRINK, 0);

    effectsPanelSW->add(*effectsPanel);
    effectsPanel->pack_start(*Gtk::manage(new Gtk::HSeparator), Gtk::PACK_SHRINK, 0);
    effectsPanel->pack_start(*vbPanelEnd[7], Gtk::PACK_SHRINK, 0);
    
    transformPanelSW->add (*transformPanel);
    transformPanel->pack_start (*Gtk::manage (new Gtk::HSeparator), Gtk::PACK_SHRINK, 0);
    transformPanel->pack_start (*vbPanelEnd[4], Gtk::PACK_SHRINK, 4);

    rawPanelSW->add       (*rawPanel);
    rawPanel->pack_start (*Gtk::manage (new Gtk::HSeparator), Gtk::PACK_SHRINK, 0);
    rawPanel->pack_start (*vbPanelEnd[5], Gtk::PACK_SHRINK, 0);

    toiF = Gtk::manage (new TextOrIcon ("star.svg", M ("MAIN_TAB_FAVORITES"), M ("MAIN_TAB_FAVORITES_TOOLTIP")));
    toiE = Gtk::manage (new TextOrIcon ("exposure.svg", M ("MAIN_TAB_EXPOSURE"), M ("MAIN_TAB_EXPOSURE_TOOLTIP")));
    toiD = Gtk::manage (new TextOrIcon ("detail.svg", M ("MAIN_TAB_DETAIL"), M ("MAIN_TAB_DETAIL_TOOLTIP")));
    toiC = Gtk::manage (new TextOrIcon ("color-circles.svg", M ("MAIN_TAB_COLOR"), M ("MAIN_TAB_COLOR_TOOLTIP")));
    toiW = Gtk::manage (new TextOrIcon ("atom.svg", M ("MAIN_TAB_ADVANCED"), M ("MAIN_TAB_ADVANCED_TOOLTIP")));
    toiT = Gtk::manage (new TextOrIcon ("transform.svg", M ("MAIN_TAB_TRANSFORM"), M ("MAIN_TAB_TRANSFORM_TOOLTIP")));
    toiR = Gtk::manage (new TextOrIcon ("bayer.svg", M ("MAIN_TAB_RAW"), M ("MAIN_TAB_RAW_TOOLTIP")));
    toiM = Gtk::manage (new TextOrIcon ("metadata.svg", M ("MAIN_TAB_METADATA"), M ("MAIN_TAB_METADATA_TOOLTIP")));
    toiL = Gtk::manage(new TextOrIcon("local-edit.svg", M("MAIN_TAB_LOCAL"), M("MAIN_TAB_LOCAL_TOOLTIP")));
    toiFx = Gtk::manage(new TextOrIcon("wand.svg", M("MAIN_TAB_EFFECTS"), M("MAIN_TAB_EFFECTS_TOOLTIP")));
    

    if (favoritePanelSW) {
        toolPanelNotebook->append_page (*favoritePanelSW,  *toiF);
    }
    toolPanelNotebook->append_page (*exposurePanelSW,  *toiE);
    toolPanelNotebook->append_page (*detailsPanelSW,   *toiD);
    toolPanelNotebook->append_page (*colorPanelSW,     *toiC);
    toolPanelNotebook->append_page(*localPanelSW, *toiL);
    toolPanelNotebook->append_page(*effectsPanelSW, *toiFx);
    toolPanelNotebook->append_page (*transformPanelSW, *toiT);
    toolPanelNotebook->append_page (*rawPanelSW,       *toiR);
    toolPanelNotebook->append_page (*metadata,    *toiM);
//    toolPanelNotebook->append_page (*advancedPanelSW,   *toiW);

    toolPanelNotebook->set_current_page (0);

    toolPanelNotebook->set_scrollable ();
    toolPanelNotebook->show_all ();

    for (auto toolPanel : toolPanels) {
        toolPanel->setListener (this);
    }

    whitebalance->setWBProvider (this);
    whitebalance->setSpotWBListener (this);
    darkframe->setDFProvider (this);
    flatfield->setFFProvider (this);
    geompanel->setLensGeomListener (this);
    rotate->setLensGeomListener (this);
    distortion->setLensGeomListener (this);
    perspective->setLensGeomListener(this);
    crop->setCropPanelListener (this);
    icm->setICMPanelListener (this);
    filmNegative->setFilmNegProvider (this);

    colorcorrection->setDeltaEColorProvider(this);
    smoothing->setDeltaEColorProvider(this);
    localContrast->setDeltaEColorProvider(this);
    textureBoost->setDeltaEColorProvider(this);
    
    toolBar = new ToolBar ();
    toolBar->setToolBarListener (this);
}

void ToolPanelCoordinator::addPanel (Gtk::Box* where, FoldableToolPanel* panel, int level)
{

    panel->setParent (where);
    panel->setLevel (level);

    expList.push_back (panel->getExpander());
    where->pack_start (*panel->getExpander(), false, false);
    toolPanels.push_back (panel);

    auto l = panel->getPParamsChangeListener();
    if (l) {
        addPParamsChangeListener(l);
    }
}

void ToolPanelCoordinator::addfavoritePanel (Gtk::Box* where, FoldableToolPanel* panel, int level)
{
    // auto name = panel->getToolName();
    // auto it = std::find(options.favorites.begin(), options.favorites.end(), name);
    // if (it != options.favorites.end()) {
    //     int index = std::distance(options.favorites.begin(), it);
    //     favorites[index] = panel;
    // } else {
        addPanel(where, panel, level);
    // }
}

ToolPanelCoordinator::~ToolPanelCoordinator ()
{
    idle_register.destroy();

    closeImage ();

    delete toolPanelNotebook;
    delete toolBar;
}

void ToolPanelCoordinator::imageTypeChanged (bool isRaw, bool isBayer, bool isXtrans, bool isMono)
{
    if (isRaw) {
        if (isBayer) {
            idle_register.add(
                [this]() -> bool
                {
                    rawPanelSW->set_sensitive(true);
                    sensorxtrans->FoldableToolPanel::hide();
                    sensorbayer->FoldableToolPanel::show();
                    preprocess->FoldableToolPanel::show();
                    flatfield->FoldableToolPanel::show();

                    return false;
                }
            );
        }
        else if (isXtrans) {
            idle_register.add(
                [this]() -> bool
                {
                    rawPanelSW->set_sensitive(true);
                    sensorxtrans->FoldableToolPanel::show();
                    sensorbayer->FoldableToolPanel::hide();
                    preprocess->FoldableToolPanel::show();
                    flatfield->FoldableToolPanel::show();

                    return false;
                }
            );
        }
        else if (isMono) {
            idle_register.add(
                [this]() -> bool
                {
                    rawPanelSW->set_sensitive(true);
                    sensorbayer->FoldableToolPanel::hide();
                    sensorxtrans->FoldableToolPanel::hide();
                    preprocess->FoldableToolPanel::hide();
                    flatfield->FoldableToolPanel::show();

                    return false;
                }
            );
        } else {
            idle_register.add(
                [this]() -> bool
                {
                    rawPanelSW->set_sensitive(true);
                    sensorbayer->FoldableToolPanel::hide();
                    sensorxtrans->FoldableToolPanel::hide();
                    preprocess->FoldableToolPanel::hide();
                    flatfield->FoldableToolPanel::hide();

                    return false;
                }
            );
        }
    } else {
        idle_register.add(
            [this]() -> bool
            {
                rawPanelSW->set_sensitive(false);

                return false;
            }
        );
    }

}

void ToolPanelCoordinator::setTweakOperator (rtengine::TweakOperator *tOperator)
{
    if (ipc && tOperator) {
        ipc->setTweakOperator(tOperator);
    }
}

void ToolPanelCoordinator::unsetTweakOperator (rtengine::TweakOperator *tOperator)
{
    if (ipc && tOperator) {
        ipc->unsetTweakOperator(tOperator);
    }
}

void ToolPanelCoordinator::refreshPreview (const rtengine::ProcEvent& event)
{
    if (!ipc) {
        return;
    }

    ProcParams* params = ipc->beginUpdateParams ();
    for (auto toolPanel : toolPanels) {
        toolPanel->write (params);
    }

    ipc->endUpdateParams (event);   // starts the IPC processing
}



void ToolPanelCoordinator::panelChanged(const rtengine::ProcEvent& event, const Glib::ustring& descr)
{
    if (!ipc) {
        return;
    }

    int changeFlags = rtengine::RefreshMapper::getInstance()->getAction(event);

    ProcParams* params = ipc->beginUpdateParams ();

    for (auto toolPanel : toolPanels) {
        toolPanel->write (params);
    }

    // Compensate rotation on flip
    if (event == rtengine::EvCTHFlip || event == rtengine::EvCTVFlip) {
        if (fabs (params->rotate.degree) > 0.001) {
            params->rotate.degree *= -1;
            changeFlags |= rtengine::RefreshMapper::getInstance()->getAction(rtengine::EvROTDegree);
            rotate->read (params);
        }
    }

    int tr = TR_NONE;

    if (params->coarse.rotate == 90) {
        tr = TR_R90;
    } else if (params->coarse.rotate == 180) {
        tr = TR_R180;
    } else if (params->coarse.rotate == 270) {
        tr = TR_R270;
    }

    // Update "on preview" geometry
    if (event == rtengine::EvPhotoLoaded || event == rtengine::EvProfileChanged || event == rtengine::EvHistoryBrowsed || event == rtengine::EvCTRotate) {
        // updating the "on preview" geometry
        int fw, fh;
        ipc->getInitialImage()->getImageSource()->getFullSize (fw, fh, tr);
        gradient->updateGeometry (params->gradient.centerX, params->gradient.centerY, params->gradient.feather, params->gradient.degree, fw, fh);
        colorcorrection->updateGeometry(fw, fh);
        smoothing->updateGeometry(fw, fh);
        // cbdl->updateGeometry(fw, fh);
        localContrast->updateGeometry(fw, fh);
        textureBoost->updateGeometry(fw, fh);
    }

    // some transformations make the crop change for convenience
    if (event == rtengine::EvCTHFlip) {
        crop->hFlipCrop ();
        crop->write (params);
    } else if (event == rtengine::EvCTVFlip) {
        crop->vFlipCrop ();
        crop->write (params);
    } else if (event == rtengine::EvCTRotate) {
        crop->rotateCrop (params->coarse.rotate, params->coarse.hflip, params->coarse.vflip);
        crop->write (params);
        resize->update (params->crop.enabled, params->crop.w, params->crop.h, ipc->getFullWidth(), ipc->getFullHeight());
        resize->write (params);
    } else if (event == rtengine::EvCrop) {
        resize->update (params->crop.enabled, params->crop.w, params->crop.h);
        resize->write (params);
    }

    ipc->endUpdateParams (changeFlags);   // starts the IPC processing

    hasChanged = true;

    for (auto paramcListener : paramcListeners) {
        paramcListener->procParamsChanged (params, event, descr);
    }
}

void ToolPanelCoordinator::profileChange(
    const PartialProfile *nparams,
    const rtengine::ProcEvent& event,
    const Glib::ustring& descr,
    const ParamsEdited* paramsEdited,
    bool fromLastSave
)
{
    int fw, fh, tr;

    if (!ipc) {
        return;
    }

    ProcParams *params = ipc->beginUpdateParams ();
    // Copy the current params as default values for the fusion
    ProcParams mergedParams(*params);

    // Reset IPTC values when switching procparams from the History
    if (event == rtengine::EvHistoryBrowsed) {
        mergedParams.metadata.iptc.clear();
        mergedParams.metadata.exif.clear();
    }

    // And apply the partial profile nparams to mergedParams
    nparams->applyTo(mergedParams);

    // Derive the effective changes, if it's a profile change, to prevent slow RAW rerendering if not necessary
    bool filterRawRefresh = false;

    if (event != rtengine::EvPhotoLoaded) {
        // ParamsEdited pe (true);
        // std::vector<rtengine::procparams::ProcParams> lParams (2);
        // lParams[0] = *params;
        // lParams[1] = *mergedParams;
        // pe.initFrom (lParams);

        // filterRawRefresh = pe.raw.isUnchanged() && pe.lensProf.isUnchanged();
        filterRawRefresh = (params->raw == mergedParams.raw) && (params->lensProf == mergedParams.lensProf) && (params->filmNegative == mergedParams.filmNegative) && (params->wb == mergedParams.wb);
    }

    *params = mergedParams;

    tr = TR_NONE;

    if (params->coarse.rotate == 90) {
        tr = TR_R90;
    } else if (params->coarse.rotate == 180) {
        tr = TR_R180;
    } else if (params->coarse.rotate == 270) {
        tr = TR_R270;
    }

    // trimming overflowing cropped area
    ipc->getInitialImage()->getImageSource()->getFullSize (fw, fh, tr);
    crop->trim (params, fw, fh);

    // updating the GUI with updated values
    for (auto toolPanel : toolPanels) {
        toolPanel->read (params);

        if (event == rtengine::EvPhotoLoaded || event == rtengine::EvProfileChanged) {
            toolPanel->autoOpenCurve();
        }
    }

    if (event == rtengine::EvPhotoLoaded || event == rtengine::EvProfileChanged || event == rtengine::EvHistoryBrowsed || event == rtengine::EvCTRotate) {
        // updating the "on preview" geometry
        gradient->updateGeometry (params->gradient.centerX, params->gradient.centerY, params->gradient.feather, params->gradient.degree, fw, fh);
        colorcorrection->updateGeometry(fw, fh);
        smoothing->updateGeometry(fw, fh);
        //cbdl->updateGeometry(fw, fh);
        localContrast->updateGeometry(fw, fh);
        textureBoost->updateGeometry(fw, fh);
    }

    // start the IPC processing
    if (filterRawRefresh) {
        ipc->endUpdateParams ( rtengine::RefreshMapper::getInstance()->getAction(event) & rtengine::ALLNORAW );
    } else {
        ipc->endUpdateParams (event);
    }

    hasChanged = event != rtengine::EvProfileChangeNotification;

    for (auto paramcListener : paramcListeners) {
        paramcListener->procParamsChanged (params, event, descr);
    }
}

void ToolPanelCoordinator::setDefaults(const ProcParams* defparams)
{
    if (defparams) {
        for (auto toolPanel : toolPanels) {
            toolPanel->setDefaults(defparams);
        }
    }
}

CropGUIListener* ToolPanelCoordinator::getCropGUIListener ()
{

    return crop;
}

void ToolPanelCoordinator::initImage (rtengine::StagedImageProcessor* ipc_, bool raw)
{

    ipc = ipc_;
    toneCurve->disableListener ();
    toneCurve->enableAll ();
    toneCurve->enableListener ();

    if (ipc) {
        const rtengine::FramesMetaData* pMetaData = ipc->getInitialImage()->getMetaData();
        metadata->setImageData(pMetaData);

        ipc->setAutoExpListener(this);
        ipc->setFrameCountListener(bayerprocess);
        ipc->setFlatFieldAutoClipListener(flatfield);
        ipc->setBayerAutoContrastListener(bayerprocess);
        ipc->setXtransAutoContrastListener(xtransprocess);
        ipc->setAutoWBListener(whitebalance);
        ipc->setAutoChromaListener(denoise);
        ipc->setSizeListener(crop);
        ipc->setSizeListener(resize);
        ipc->setImageTypeListener(this);
        ipc->setAutoLogListener(logenc);
        ipc->setAutoDeconvRadiusListener(sharpening);
        ipc->setFilmNegListener(filmNegative);

        auto dn = Glib::path_get_dirname(ipc->getInitialImage()->getFileName());
        flatfield->setShortcutPath(dn);
        colorcorrection->setExternalMaskPath(dn);
        localContrast->setExternalMaskPath(dn);
        textureBoost->setExternalMaskPath(dn);
        smoothing->setExternalMaskPath(dn);

        icm->setRawMeta (raw, (const rtengine::FramesData*)pMetaData);
        lensProf->setRawMeta(raw, pMetaData);
        perspective->setRawMeta(raw, pMetaData);
    }

    exposure->setRaw(raw);
    toneCurve->setRaw(raw);
    hasChanged = true;
}


void ToolPanelCoordinator::closeImage ()
{

    if (ipc) {
        ipc->stopProcessing ();
        ipc = nullptr;
    }
}

void ToolPanelCoordinator::closeAllTools()
{

    for (size_t i = 0; i < options.tpOpen.size(); i++)
        if (i < expList.size()) {
            expList.at (i)->set_expanded (false);
        }
}

void ToolPanelCoordinator::openAllTools()
{

    for (size_t i = 0; i < options.tpOpen.size(); i++)
        if (i < expList.size()) {
            expList.at (i)->set_expanded (true);
        }
}

void ToolPanelCoordinator::updateToolState()
{

    for (size_t i = 0; i < options.tpOpen.size(); i++)
        if (i < expList.size()) {
            expList.at (i)->set_expanded (options.tpOpen.at (i));
        }

    if (options.tpOpen.size() > expList.size()) {
        size_t sizeWavelet = options.tpOpen.size() - expList.size();
        std::vector<int> temp;

        for (size_t i = 0; i < sizeWavelet; i++) {
            temp.push_back (options.tpOpen.at (i + expList.size()));
        }
    }
}

void ToolPanelCoordinator::readOptions ()
{

    crop->readOptions ();
}

void ToolPanelCoordinator::writeOptions ()
{

    crop->writeOptions ();

    if (options.autoSaveTpOpen) {
        writeToolExpandedStatus (options.tpOpen);
    }
}


void ToolPanelCoordinator::writeToolExpandedStatus (std::vector<int> &tpOpen)
{
    tpOpen.clear ();

    for (size_t i = 0; i < expList.size(); i++) {
        tpOpen.push_back (expList.at (i)->get_expanded ());
    }
}


void ToolPanelCoordinator::spotWBselected(int x, int y, Thumbnail* thm)
{
    if (!ipc) {
        return;
    }

//    toolBar->setTool (TOOL_HAND);
    int rect = whitebalance->getSize ();
    int ww = ipc->getFullWidth();
    int hh = ipc->getFullHeight();

    if (x - rect > 0 && y - rect > 0 && x + rect < ww && y + rect < hh) {
        rtengine::ColorTemp ctemp;
        ipc->getSpotWB(x, y, rect, ctemp);
        whitebalance->setWB(ctemp);
    }
}

void ToolPanelCoordinator::sharpMaskSelected(bool sharpMask)
{
    if (!ipc) {
        return;
    }
    ipc->beginUpdateParams ();
    ipc->setSharpMask(sharpMask);
    ipc->endUpdateParams (rtengine::EvShrEnabled);
}

int ToolPanelCoordinator::getSpotWBRectSize() const
{
    return whitebalance->getSize();
}

void ToolPanelCoordinator::cropSelectionReady()
{
    toolBar->setTool (TMHand);

    if (!ipc) {
        return;
    }
}

void ToolPanelCoordinator::rotateSelectionReady(double rotate_deg, Thumbnail* thm)
{
    toolBar->setTool (TMHand);

    if (!ipc) {
        return;
    }

    if (rotate_deg != 0.0) {
        rotate->straighten (rotate_deg);
    }
}

ToolBar* ToolPanelCoordinator::getToolBar() const
{
    return toolBar;
}

CropGUIListener* ToolPanelCoordinator::startCropEditing(Thumbnail* thm)
{
    return crop;
}

void ToolPanelCoordinator::autoCropRequested ()
{

    if (!ipc) {
        return;
    }

    int x1, y1, x2, y2, w, h;
    toolBar->setTool(TMCropSelect);
    ipc->getAutoCrop (crop->getRatio(), x1, y1, w, h);
    x2 = x1 + w - 1;
    y2 = y1 + h - 1;
    crop->cropInit (x1, y1, w, h);
    crop->cropResized (x1, y1, x2, y2);
    crop->cropManipReady (x1, y1, w, h);
}

rtengine::RawImage* ToolPanelCoordinator::getDF()
{
    if (!ipc) {
        return nullptr;
    }

    const rtengine::FramesMetaData *imd = ipc->getInitialImage()->getMetaData();

    if (imd) {
        int iso = imd->getISOSpeed();
        double shutter = imd->getShutterSpeed();
        std::string maker ( imd->getMake()  );
        std::string model ( imd->getModel() );
        time_t timestamp = imd->getDateTimeAsTS();

        return rtengine::dfm.searchDarkFrame ( maker, model, iso, shutter, timestamp);
    }

    return nullptr;
}

rtengine::RawImage* ToolPanelCoordinator::getFF()
{
    if (!ipc) {
        return nullptr;
    }

    const rtengine::FramesMetaData *imd = ipc->getInitialImage()->getMetaData();

    if (imd) {
        // int iso = imd->getISOSpeed();              temporarilly removed because unused
        // double shutter = imd->getShutterSpeed();   temporarilly removed because unused
        double aperture = imd->getFNumber();
        double focallength = imd->getFocalLen();
        std::string maker ( imd->getMake()  );
        std::string model ( imd->getModel() );
        std::string lens (  imd->getLens()  );
        time_t timestamp = imd->getDateTimeAsTS();

        return rtengine::ffm.searchFlatField ( maker, model, lens, focallength, aperture, timestamp);
    }

    return nullptr;
}

Glib::ustring ToolPanelCoordinator::GetCurrentImageFilePath()
{
    if (!ipc) {
        return "";
    }

    return ipc->getInitialImage()->getFileName();
}


bool ToolPanelCoordinator::hasEmbeddedFF()
{
    if (ipc) {
        const rtengine::FramesMetaData *imd = ipc->getInitialImage()->getMetaData();

        if (imd) {
            auto gm = imd->getGainMaps();
            return !gm.empty();
        }
    }
    return false;
}


void ToolPanelCoordinator::straightenRequested ()
{

    if (!ipc) {
        return;
    }

    toolBar->setTool (TMStraighten);
}

double ToolPanelCoordinator::autoDistorRequested ()
{
    if (!ipc) {
        return 0.0;
    }

    return rtengine::ImProcFunctions::getAutoDistor (ipc->getInitialImage()->getFileName(), 400);
}


void ToolPanelCoordinator::updateTransformPreviewRequested(rtengine::ProcEvent event, bool render_perspective)
{
    if (!ipc) {
        return;
    }

    ipc->beginUpdateParams()->perspective.enabled = render_perspective;
    ipc->endUpdateParams(event);
}


void ToolPanelCoordinator::spotWBRequested (int size)
{

    if (!ipc) {
        return;
    }

    toolBar->setTool (TMSpotWB);
}


void ToolPanelCoordinator::cropSelectRequested()
{
    if (!ipc) {
        return;
    }
    toolBar->setTool(TMCropSelect);
}


void ToolPanelCoordinator::cropEnableChanged(bool enabled)
{
    if (!ipc) {
        return;
    }
    if (!enabled && toolBar->getTool() == TMCropSelect) {
        toolBar->setTool(TMHand);
    }
}


// void ToolPanelCoordinator::cropResetRequested()
// {
//     if (!ipc) {
//         return;
//     }
//     if (toolBar->getTool() == TMCropSelect) {
//         toolBar->setTool(TMHand);
//     }
// }

void ToolPanelCoordinator::controlLineEditModeChanged(bool active)
{
    if (!ipc) {
        return;
    }

    if (active) {
        toolBar->setTool(TMPerspective);
    }
}


void ToolPanelCoordinator::saveInputICCReference(const Glib::ustring& fname, bool apply_wb)
{
    if (ipc) {
        ipc->saveInputICCReference (fname, apply_wb);
    }
}

void ToolPanelCoordinator::updateCurveBackgroundHistogram(
    const LUTu& histToneCurve,
    const LUTu& histLCurve,
    const LUTu& histCCurve,
    const LUTu& histLCAM,
    const LUTu& histCCAM,
    const LUTu& histRed,
    const LUTu& histGreen,
    const LUTu& histBlue,
    const LUTu& histLuma,
    const LUTu& histLRETI
)
{
    toneCurve->updateCurveBackgroundHistogram(histToneCurve, histLCurve, histCCurve,histLCAM,  histCCAM, histRed, histGreen, histBlue, histLuma, histLRETI);
    lcurve->updateCurveBackgroundHistogram(histToneCurve, histLCurve, histCCurve, histLCAM, histCCAM, histRed, histGreen, histBlue, histLuma, histLRETI);
    rgbcurves->updateCurveBackgroundHistogram(histToneCurve, histLCurve, histCCurve, histLCAM, histCCAM, histRed, histGreen, histBlue, histLuma, histLRETI);
}

void ToolPanelCoordinator::foldAllButOne (Gtk::Box* parent, FoldableToolPanel* openedSection)
{

    for (auto toolPanel : toolPanels) {
        if (toolPanel->getParent() != nullptr) {
            ToolPanel* currentTP = toolPanel;

            if (currentTP->getParent() == parent) {
                // Section in the same tab, we unfold it if it's not the one that has been clicked
                if (currentTP != openedSection) {
                    currentTP->setExpanded (false);
                } else {
                    if (!currentTP->getExpanded()) {
                        currentTP->setExpanded (true);
                    }
                }
            }
        }
    }
}

bool ToolPanelCoordinator::handleShortcutKey (GdkEventKey* event)
{

    //bool ctrl = event->state & GDK_CONTROL_MASK;  temporarily removed because unused
    //bool shift = event->state & GDK_SHIFT_MASK;   temporarily removed because unused
    bool alt = event->state & GDK_MOD1_MASK;

    if (alt) {
        switch (event->keyval) {
            case GDK_KEY_u:
                if (favoritePanelSW) {
                    toolPanelNotebook->set_current_page (toolPanelNotebook->page_num (*favoritePanelSW));
                }
                return true;

            case GDK_KEY_e:
                toolPanelNotebook->set_current_page (toolPanelNotebook->page_num (*exposurePanelSW));
                return true;

            case GDK_KEY_d:
                toolPanelNotebook->set_current_page (toolPanelNotebook->page_num (*detailsPanelSW));
                return true;

            case GDK_KEY_c:
                toolPanelNotebook->set_current_page (toolPanelNotebook->page_num (*colorPanelSW));
                return true;

            case GDK_KEY_t:
                toolPanelNotebook->set_current_page (toolPanelNotebook->page_num (*transformPanelSW));
                return true;

            case GDK_KEY_r:
                toolPanelNotebook->set_current_page (toolPanelNotebook->page_num (*rawPanelSW));
                return true;

            case GDK_KEY_f:
                toolPanelNotebook->set_current_page(toolPanelNotebook->page_num(*effectsPanelSW));
                return true;

            case GDK_KEY_x:
                toolPanelNotebook->set_current_page(toolPanelNotebook->page_num(*localPanelSW));
                return true;
            
            case GDK_KEY_m:
                toolPanelNotebook->set_current_page (toolPanelNotebook->page_num (*metadata));
                return true;
        }
    }

    return false;
}

void ToolPanelCoordinator::updateVScrollbars (bool hide)
{
    GThreadLock lock; // All GUI access from idle_add callbacks or separate thread HAVE to be protected
    Gtk::PolicyType policy = hide ? Gtk::POLICY_NEVER : Gtk::POLICY_AUTOMATIC;
    if (favoritePanelSW) {
        favoritePanelSW->set_policy     (Gtk::POLICY_AUTOMATIC, policy);
    }
    exposurePanelSW->set_policy     (Gtk::POLICY_AUTOMATIC, policy);
    detailsPanelSW->set_policy      (Gtk::POLICY_AUTOMATIC, policy);
    colorPanelSW->set_policy        (Gtk::POLICY_AUTOMATIC, policy);
    transformPanelSW->set_policy    (Gtk::POLICY_AUTOMATIC, policy);
    rawPanelSW->set_policy          (Gtk::POLICY_AUTOMATIC, policy);
    // advancedPanelSW->set_policy      (Gtk::POLICY_AUTOMATIC, policy);
    localPanelSW->set_policy(Gtk::POLICY_AUTOMATIC, policy);
    effectsPanelSW->set_policy(Gtk::POLICY_AUTOMATIC, policy);

    for (auto currExp : expList) {
        currExp->updateVScrollbars (hide);
    }
}

void ToolPanelCoordinator::updateTPVScrollbar (bool hide)
{
    updateVScrollbars (hide);
}

void ToolPanelCoordinator::toolSelected (ToolMode tool)
{
    GThreadLock lock; // All GUI access from idle_add callbacks or separate thread HAVE to be protected

    switch (tool) {
        case TMCropSelect:
            crop->setSelecting(true);
            crop->setExpanded (true);
            toolPanelNotebook->set_current_page (toolPanelNotebook->page_num (*transformPanelSW));
            break;

        case TMSpotWB:
            whitebalance->setExpanded (true);
            toolPanelNotebook->set_current_page (toolPanelNotebook->page_num (*colorPanelSW));
            break;

        case TMStraighten:
            geompanel->setExpanded (true);
            rotate->setExpanded (true);
            toolPanelNotebook->set_current_page (toolPanelNotebook->page_num (*transformPanelSW));
            break;

        case TMPerspective:
            perspective->setControlLineEditMode(true);
            perspective->setExpanded(true);
            geompanel->setExpanded(true);
            toolPanelNotebook->set_current_page(toolPanelNotebook->page_num(*transformPanelSW));
            break;
        default:
            break;
    }

    if (tool != TMCropSelect) {
        crop->setSelecting(false);
    }
}


void ToolPanelCoordinator::toolDeselected(ToolMode tool)
{
    if (tool == TMPerspective) {
        perspective->requestApplyControlLines();
    }
}


void ToolPanelCoordinator::editModeSwitchedOff ()
{
    if (editDataProvider) {
        editDataProvider->switchOffEditMode();
    }
}

void ToolPanelCoordinator::dirSelected (const Glib::ustring& dirname, const Glib::ustring& openfile)
{

    flatfield->setShortcutPath (dirname);
}

void ToolPanelCoordinator::setEditProvider (EditDataProvider *provider)
{
    editDataProvider = provider;

    for (size_t i = 0; i < toolPanels.size(); i++) {
        toolPanels.at (i)->setEditProvider (provider);
    }
}

void ToolPanelCoordinator::autoPerspectiveRequested(bool horiz, bool vert, double &angle, double &horizontal, double &vertical, double &shear, const std::vector<rtengine::ControlLine> *lines)
{
    angle = 0;
    horizontal = 0;
    vertical = 0;
    shear = 0;
    
    if (!ipc || !(horiz || vert)) {
        return;
    }

    rtengine::ImageSource *src = dynamic_cast<rtengine::ImageSource *>(ipc->getInitialImage());
    if (!src) {
        return;
    }

    rtengine::procparams::ProcParams params;
    ipc->getParams(&params);

    rtengine::PerspectiveCorrection::Direction dir;
    if (horiz && vert) {
        dir = rtengine::PerspectiveCorrection::BOTH;
    } else if (horiz) {
        dir = rtengine::PerspectiveCorrection::HORIZONTAL;
    } else {
        dir = rtengine::PerspectiveCorrection::VERTICAL;
    }

    auto res = rtengine::PerspectiveCorrection::autocompute(src, dir, &params, src->getMetaData(), lines);
    angle = res.angle;
    horizontal = res.horizontal;
    vertical = res.vertical;
    shear = res.shear;
}


void ToolPanelCoordinator::autoExpChanged(double expcomp, int bright, int contr, int black, int hlcompr, int hlcomprthresh, bool hlrecons)
{
    // exposure->autoExpChanged(expcomp, bright, contr, black, hlcompr, hlcomprthresh, hlrecons);
}


void ToolPanelCoordinator::autoMatchedToneCurveChanged(const std::vector<double>& curve, const std::vector<double>& curve2)
{
    toneCurve->autoMatchedToneCurveChanged(curve, curve2);
}


void ToolPanelCoordinator::setAreaDrawListener(AreaDrawListener *listener)
{
    colorcorrection->setAreaDrawListener(listener);
    smoothing->setAreaDrawListener(listener);
    localContrast->setAreaDrawListener(listener);
    textureBoost->setAreaDrawListener(listener);
}


bool ToolPanelCoordinator::getDeltaELCH(EditUniqueID id, rtengine::Coord pos, float &L, float &C, float &H)
{
    if (ipc) {
        GThreadUnLock unlock;
        return ipc->getDeltaELCH(id, pos.x, pos.y, L, C, H);
    }
    return false;
}


void ToolPanelCoordinator::setProgressListener(rtengine::ProgressListener *pl)
{
    metadata->setProgressListener(pl);
}


void ToolPanelCoordinator::setToolShortcutManager(ToolShortcutManager *mgr)
{
    for (auto p : toolPanels) {
        p->registerShortcuts(mgr);
    }

    //static_cast<TPScrolledWindow *>(favoritePanelSW)->setToolShortcutManager(mgr);
    static_cast<TPScrolledWindow *>(exposurePanelSW)->setToolShortcutManager(mgr);
    static_cast<TPScrolledWindow *>(detailsPanelSW)->setToolShortcutManager(mgr);
    static_cast<TPScrolledWindow *>(colorPanelSW)->setToolShortcutManager(mgr);
    static_cast<TPScrolledWindow *>(transformPanelSW)->setToolShortcutManager(mgr);
    static_cast<TPScrolledWindow *>(rawPanelSW)->setToolShortcutManager(mgr);
    static_cast<TPScrolledWindow *>(localPanelSW)->setToolShortcutManager(mgr);
    static_cast<TPScrolledWindow *>(effectsPanelSW)->setToolShortcutManager(mgr);
}


bool ToolPanelCoordinator::getFilmNegativeSpot(rtengine::Coord spot, int spotSize, RGB &refInput, RGB &refOutput)
{
    return ipc && static_cast<rtengine::ImProcCoordinator *>(ipc)->getFilmNegativeSpot(spot.x, spot.y, spotSize, refInput, refOutput);
}
