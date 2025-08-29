/* -*- C++ -*-
 *  
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
#pragma once

#include "rtengine.h"
#include "improcfun.h"
#include "image8.h"
#include "image16.h"
#include "imagesource.h"
#include "procevents.h"
#include "dcrop.h"
#include "LUT.h"
#include "../rtgui/threadutils.h"

#include <mutex>
#include <condition_variable>

namespace rtengine {

using namespace procparams;

class Crop;

/** @brief Manages the image processing, espc. of the preview windows
  *
  * There is one ImProcCoordinator per edit panel.
  *
  * The ImProcCoordinator handle an sized down image representation of the full image, that is used when paning
  * and in the Navigator object.
  *
  * Each ImProcCoordinator handles an rtengine::Crop list, which process images too with their own pipeline,
  * but using this class' LUT and other precomputed parameters. The main preview area is displaying a non framed Crop object,
  * while detail windows are framed Crop objects.
  */
class ImProcCoordinator: public StagedImageProcessor, public HistogramObservable {

    friend class Crop;

protected:
    Imagefloat *orig_prev;
    Imagefloat *oprevi;
    Imagefloat *spotprev;
    Imagefloat *bufs_[3];
    std::array<bool, 4> pipeline_stop_;
    
    Imagefloat *drcomp_11_dcrop_cache; // global cache for dynamicRangeCompression used in 1:1 detail windows (except when denoise is active)
    Image8 *previmg;  // displayed image in monitor color space, showing the output profile as well (soft-proofing enabled, which then correspond to workimg) or not
    Image8 *workimg;  // internal image in output color space for analysis

    ImageSource* imgsrc;

    ColorTemp currWB;
    ColorTemp autoWB;

    double lastAwbEqual;

    ImProcFunctions ipf;
    DCPProfile::ApplyState dcpApplyState;

    Glib::ustring monitorProfile;
    RenderingIntent monitorIntent;
    bool softProof;
    GamutCheck gamutCheck;
    bool sharpMask;

    int scale;
    bool highDetailPreprocessComputed;
    bool highDetailRawComputed;
    bool allocated;

    void freeAll ();

    // Precomputed values used by DetailedCrop ----------------------------------------------

    LUTu vhist16;
    LUTu histRed, histRedRaw;
    LUTu histGreen, histGreenRaw;
    LUTu histBlue, histBlueRaw;
    LUTu histLuma, histToneCurve, histLCurve, histCCurve;
    LUTu /*histLLCurve,*/ histLCAM, histCCAM, histChroma, histLRETI;

    bool hist_lrgb_dirty;
    /// Used to simulate a lazy update of the raw histogram.
    bool hist_raw_dirty;
    int vectorscopeScale;
    bool vectorscope_hc_dirty, vectorscope_hs_dirty;
    array2D<int> vectorscope_hc, vectorscope_hs;
    /// Waveform's intensity. Same as height of reference image.
    int waveformScale;
    bool waveform_dirty;
    array2D<int> waveformRed, waveformGreen, waveformBlue, waveformLuma;    
    // ------------------------------------------------------------------------------------

    int fw, fh, tr, fullw, fullh;
    int pW, pH;

    ProgressListener* plistener;
    PreviewImageListener* imageListener;
    AutoExpListener* aeListener;
    AutoWBListener* awbListener;
    FlatFieldAutoClipListener *flatFieldAutoClipListener;
    AutoContrastListener *bayerAutoContrastListener;
    AutoContrastListener *xtransAutoContrastListener;
    FrameCountListener *frameCountListener;
    ImageTypeListener *imageTypeListener;
    FilmNegListener *filmNegListener;

    AutoChromaListener* adnListener;

    HistogramListener* hListener;
    std::vector<SizeListener*> sizeListeners;

    AutoLogListener *autoLogListener;
    AutoDeconvRadiusListener *autoRadiusListener;

    std::vector<Crop*> crops;

    bool resultValid;

    MyMutex minit;  // to gain mutually exclusive access to ... to what exactly?
    void backupParams();
    void restoreParams();

    void progress (Glib::ustring str, int pr);
    void reallocAll ();
    void allocCache (Imagefloat* &imgfloat);
    void setScale (int prevscale);
    void updatePreviewImage (int todo, bool panningRelatedChange);
    void updateWB();

    void notifyHistogramChanged();
    /// Updates L, R, G, and B histograms. Returns true unless not updated.
    bool updateLRGBHistograms();
    /// Updates the H-C vectorscope. Returns true unless not updated.
    bool updateVectorscopeHC();
    /// Updates the H-S vectorscope. Returns true unless not updated.
    bool updateVectorscopeHS();
    /// Updates all waveforms. Returns true unless not updated.
    bool updateWaveforms();
    
    MyMutex mProcessing;
    ProcParams params;
    ProcParams paramsBackup;
    TweakOperator* tweakOperator;

    // for optimization purpose, the output profile, output rendering intent and
    // output BPC will trigger a regeneration of the profile on parameter change only
    // and automatically
    Glib::ustring lastOutputProfile;
    RenderingIntent lastOutputIntent;
    bool lastOutputBPC;

    // members of the updater:
    std::mutex updater_mutex_;
    std::condition_variable updater_cond_;
    
    // MyMutex updaterThreadStart;
    MyMutex paramsUpdateMutex;
    int  changeSinceLast;
    bool updaterRunning;
    ProcParams nextParams;
    bool destroying;
    void startProcessing ();
    void process ();
    bool highQualityComputed;

    void wait_not_running();
    void set_updater_running(bool val);
    int update_change_flags(const ProcParams &pp, int flags);
    
public:

    ImProcCoordinator ();
    ~ImProcCoordinator () override;
    void assign     (ImageSource* imgsrc);

    void        getParams (procparams::ProcParams* dst) override
    {
        *dst = params;
    }

    void        startProcessing (int changeCode) override;
    ProcParams* beginUpdateParams () override;
    void        endUpdateParams (ProcEvent change) override;  // must be called after beginUpdateParams, triggers update
    void        endUpdateParams (int changeFlags) override;
    void        stopProcessing () override;


    void setPreviewScale    (int scale) override
    {
        setScale (scale);
    }
    int  getPreviewScale    () override
    {
        return scale;
    }

    //void fullUpdatePreviewImage  ();

    int getFullWidth () override
    {
        return fullw;
    }
    int getFullHeight () override
    {
        return fullh;
    }

    int getPreviewWidth () override
    {
        return pW;
    }
    int getPreviewHeight () override
    {
        return pH;
    }

    DetailedCrop* createCrop  (::EditDataProvider *editDataProvider, bool isDetailWindow) override;
    void setTweakOperator (TweakOperator *tOperator) override;
    void unsetTweakOperator (TweakOperator *tOperator) override;

    bool getAutoWB(ColorTemp &out, double equal) override;
    void getCamWB(ColorTemp &out) override;
    void getSpotWB(int x, int y, int rectSize, ColorTemp &out) override;
    void getAutoCrop (double ratio, int &x, int &y, int &w, int &h) override;
    bool getHighQualComputed() override;
    void setHighQualComputed() override;
    void setMonitorProfile (const Glib::ustring& profile, RenderingIntent intent) override;
    void getMonitorProfile (Glib::ustring& profile, RenderingIntent& intent) const override;
    void setSoftProofing   (bool softProof, GamutCheck gamutCheck) override;
    void setSharpMask      (bool sharpMask) override;
    bool updateTryLock () override
    {
        //return updaterThreadStart.trylock();
        set_updater_running(true);
        return true;
    }
    void updateUnLock () override
    {
        //updaterThreadStart.unlock();
        set_updater_running(false);
    }

    bool is_running() const;

    void setProgressListener (ProgressListener* pl) override
    {
        plistener = pl;
    }
    void setPreviewImageListener    (PreviewImageListener* il) override
    {
        imageListener = il;
    }
    void setSizeListener     (SizeListener* il) override
    {
        sizeListeners.push_back (il);
    }
    void delSizeListener     (SizeListener* il) override
    {
        std::vector<SizeListener*>::iterator it = std::find (sizeListeners.begin(), sizeListeners.end(), il);

        if (it != sizeListeners.end()) {
            sizeListeners.erase (it);
        }
    }
    void setAutoExpListener  (AutoExpListener* ael) override
    {
        aeListener = ael;
    }
    void setHistogramListener (HistogramListener *h) override
    {
        if (hListener) {
            hListener->setObservable(nullptr);
        }
        hListener = h;
        if (h) {
            h->setObservable(this);
        }
    }
    void setAutoWBListener   (AutoWBListener* awb) override
    {
        awbListener = awb;
    }
    void setAutoChromaListener  (AutoChromaListener* adn) override
    {
        adnListener = adn;
    }

    void setFrameCountListener  (FrameCountListener* fcl) override
    {
        frameCountListener = fcl;
    }

    void setFlatFieldAutoClipListener  (FlatFieldAutoClipListener* ffacl) override
    {
        flatFieldAutoClipListener = ffacl;
    }
    void setBayerAutoContrastListener  (AutoContrastListener* acl) override
    {
        bayerAutoContrastListener = acl;
    }

    void setXtransAutoContrastListener  (AutoContrastListener* acl) override
    {
        xtransAutoContrastListener = acl;
    }

    void setImageTypeListener  (ImageTypeListener* itl) override
    {
        imageTypeListener = itl;
    }

    void setFilmNegListener(FilmNegListener* fnl) override
    {
        filmNegListener = fnl;
    }

    void setAutoLogListener(AutoLogListener *l) override
    {
        autoLogListener = l;
    }

    void setAutoDeconvRadiusListener(AutoDeconvRadiusListener *l) override
    {
        autoRadiusListener = l;
    }

    void saveInputICCReference (const Glib::ustring& fname, bool apply_wb) override;

    InitialImage*  getInitialImage () override
    {
        return imgsrc;
    }

    bool getDeltaELCH(EditUniqueID id, int x, int y, float &L, float &C, float &H) override;

    ImProcFunctions::DenoiseInfoStore denoiseInfoStore;

    void requestUpdateHistogram() override;
    void requestUpdateHistogramRaw() override;
    void requestUpdateVectorscopeHC() override;
    void requestUpdateVectorscopeHS() override;
    void requestUpdateWaveform() override;

    // returns true if some mask is being displayed on the image
    bool is_mask_image() const;

    bool getFilmNegativeSpot(int x, int y, const int spotSize, FilmNegativeParams::RGB &refInput, FilmNegativeParams::RGB &refOutput);
};

} // namespace rtengine
