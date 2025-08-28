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
#include "improccoordinator.h"
#include "curves.h"
#include "mytime.h"
#include "refreshmap.h"
#include "../rtgui/ppversion.h"
#include "colortemp.h"
#include "improcfun.h"
#include "iccstore.h"
#include <iostream>
#include <fstream>
#include <string>
#include "color.h"
#include "metadata.h"
#include "perspectivecorrection.h"
#include "threadpool.h"
#ifdef _OPENMP
#include <omp.h>
#endif

namespace rtengine {

using rtengine::procparams::WBParams;

namespace {

constexpr int VECTORSCOPE_SIZE = 128;

using rtengine::Coord2D;

} // namespace


extern const Settings* settings;

ImProcCoordinator::ImProcCoordinator():
    orig_prev(nullptr),
    oprevi(nullptr),
    spotprev(nullptr),

    drcomp_11_dcrop_cache(nullptr),
    previmg(nullptr),
    workimg(nullptr),
    imgsrc(nullptr),
    lastAwbEqual(0.),
    ipf(&params, true),
    monitorIntent(RI_RELATIVE),
    softProof(false),
    gamutCheck(GAMUT_CHECK_OFF),
    sharpMask(false),
    scale(10),
    highDetailPreprocessComputed(false),
    highDetailRawComputed(false),
    allocated(false), 

    vhist16(65536),
    histRed(256), histRedRaw(256),
    histGreen(256), histGreenRaw(256),
    histBlue(256), histBlueRaw(256),
    histLuma(256),
    histToneCurve(256),
    histLCurve(256),
    histCCurve(256),
    //histLLCurve(256),
    histLCAM(256),
    histCCAM(256),
    histChroma(256),
    histLRETI(256),

    hist_lrgb_dirty(false),
    hist_raw_dirty(false),
    vectorscopeScale(0),
    vectorscope_hc_dirty(false),
    vectorscope_hs_dirty(false),
    vectorscope_hc(VECTORSCOPE_SIZE, VECTORSCOPE_SIZE),
    vectorscope_hs(VECTORSCOPE_SIZE, VECTORSCOPE_SIZE),
    waveformScale(0),
    waveform_dirty(false),
    waveformRed(0, 0),
    waveformGreen(0, 0),
    waveformBlue(0, 0),
    waveformLuma(0, 0),
    
    fw(0), fh(0), tr(0),
    fullw(1), fullh(1),
    pW(-1), pH(-1),
    plistener(nullptr),
    imageListener(nullptr),
    aeListener(nullptr),
    awbListener(nullptr),
    flatFieldAutoClipListener(nullptr),
    bayerAutoContrastListener(nullptr),
    xtransAutoContrastListener(nullptr),
    frameCountListener(nullptr),
    imageTypeListener(nullptr),
    filmNegListener(nullptr),
    adnListener(nullptr),
    hListener(nullptr),
    autoLogListener(nullptr),
    autoRadiusListener(nullptr),
    resultValid(false),
    tweakOperator(nullptr),
    lastOutputProfile("BADFOOD"),
    lastOutputIntent(RI__COUNT),
    lastOutputBPC(false),
    //thread(nullptr),
    changeSinceLast(0),
    updaterRunning(false),
    destroying(false),
    highQualityComputed(false)
{
    for (int i = 0; i < 3; ++i) {
        bufs_[i] = nullptr;
    }
    for (auto &s : pipeline_stop_) {
        s = false;
    }
}

void ImProcCoordinator::assign(ImageSource* imgsrc)
{
    this->imgsrc = imgsrc;
    denoiseInfoStore.valid = false;
}

ImProcCoordinator::~ImProcCoordinator()
{
    destroying = true;
    // updaterThreadStart.lock();

    wait_not_running();

    {
        MyMutex::MyLock lock(mProcessing);
        freeAll();

        if (drcomp_11_dcrop_cache) {
            delete drcomp_11_dcrop_cache;
            drcomp_11_dcrop_cache = nullptr;
        }
    }

    std::vector<Crop*> toDel = crops;

    for (size_t i = 0; i < toDel.size(); i++) {
        delete toDel[i];
    }

    imgsrc->decreaseRef();

    // updaterThreadStart.unlock();
}

DetailedCrop* ImProcCoordinator::createCrop(::EditDataProvider *editDataProvider, bool isDetailWindow)
{

    return new Crop(this, editDataProvider, isDetailWindow);
}
void ImProcCoordinator::backupParams()
{
    paramsBackup.setDefaults();
    paramsBackup = params;
}

void ImProcCoordinator::restoreParams()
{
    params = paramsBackup;
}


// todo: bitmask containing desired actions, taken from changesSinceLast
// cropCall: calling crop, used to prevent self-updates  ...doesn't seem to be used
void ImProcCoordinator::updatePreviewImage(int todo, bool panningRelatedChange)
{
    MyMutex::MyLock processingLock(mProcessing);
    int numofphases = 14;
    int readyphase = 0;

    DCPProfile *dcpProf = imgsrc->getDCP(params.icm, dcpApplyState);
    ipf.setDCPProfile(dcpProf, dcpApplyState);
    ipf.setViewport(0, 0, -1, -1);
    ipf.setOutputHistograms(&histToneCurve, &histCCurve, &histLCurve);

    if (todo == CROP && ipf.needsPCVignetting()) {
        todo |= M_LUMINANCE; //TRANSFORM;    // Change about Crop does affect TRANSFORM
    }

    bool highDetailNeeded = false;
    // Check if any detail crops need high detail. If not, take a fast path short cut
    if (!highDetailNeeded) {
        for (size_t i = 0; i < crops.size(); i++)
            if (crops[i]->get_skip() == 1) {   // skip=1 -> full resolution
                highDetailNeeded = true;
                break;
            }
    }
    bool highDetailNeeded_WB = highDetailNeeded;
    if ((todo & M_HIGHQUAL) || options.prevdemo == PD_Sidecar) {
        highDetailNeeded = true;
        todo |= M_AUTOEXP;
    }

    ipf.setPipetteBuffer(nullptr);
    bool stop = false;
                    
    if (((todo & ALL) == ALL) || (todo & M_MONITOR) || panningRelatedChange || (highDetailNeeded && options.prevdemo != PD_Sidecar)) {
        if (todo == CROP && ipf.needsPCVignetting()) {
            todo |= TRANSFORM;    // Change about Crop does affect TRANSFORM
        }
    
        RAWParams rp = params.raw;
        if (!highDetailNeeded) {
            // if below 100% magnification, take a fast path
            if (rp.bayersensor.method != RAWParams::BayerSensor::Method::NONE && rp.bayersensor.method != RAWParams::BayerSensor::Method::MONO) {
                rp.bayersensor.method = RAWParams::BayerSensor::Method::FAST;
            }
    
            //bayerrp.all_enhance = false;
    
            if (rp.xtranssensor.method != RAWParams::XTransSensor::Method::NONE && rp.xtranssensor.method != RAWParams::XTransSensor::Method::MONO) {
                rp.xtranssensor.method = RAWParams::XTransSensor::Method::FAST;
            }
    
            rp.bayersensor.ccSteps = 0;
            rp.xtranssensor.ccSteps = 0;
            //rp.deadPixelFilter = rp.hotPixelFilter = false;
        }
    
        progress("Applying white balance, color correction & sRGB conversion...", 100 * readyphase / numofphases);
    
        if (frameCountListener) {
            frameCountListener->FrameCountChanged(imgsrc->getFrameCount(), params.raw.bayersensor.imageNum);
        }

        imgsrc->setCurrentFrame(params.raw.bayersensor.imageNum);

        ColorTemp preproc_wb;
        const bool wb_todo = todo & (M_WHITEBALANCE | M_PREPROC | M_INIT);

        if (wb_todo) {
            updateWB();
            if (options.wb_preview_mode != Options::WB_AFTER) {
                highQualityComputed = false;
            }
        }
        
        switch (options.wb_preview_mode) {
        case Options::WB_BEFORE:
            if (wb_todo) {
                preproc_wb = currWB;
                todo |= M_PREPROC | M_RAW;
            }
            break;
        case Options::WB_BEFORE_HIGH_DETAIL:
            if ((wb_todo && highDetailNeeded_WB) || (todo & M_HIGHQUAL)) {
                preproc_wb = currWB;
                todo |= M_PREPROC | M_RAW;
                if (todo & M_HIGHQUAL) {
                    todo |= M_AUTOEXP;
                }
            }
            break;
        case Options::WB_AFTER:
        default:
            break;
        }
        if (todo & M_INIT) {
            preproc_wb = currWB;
        }
        
        // raw auto CA is bypassed if no high detail is needed, so we have to compute it when high detail is needed
        if ((todo & M_PREPROC) || (!highDetailPreprocessComputed && highDetailNeeded)) {
            imgsrc->preprocess(rp, params.lensProf, params.coarse, true, preproc_wb);
            if (flatFieldAutoClipListener && rp.ff_AutoClipControl) {
                flatFieldAutoClipListener->flatFieldAutoClipValueChanged(imgsrc->getFlatFieldAutoClipValue());
            }
            imgsrc->getRAWHistogram(histRedRaw, histGreenRaw, histBlueRaw);
            hist_raw_dirty = !(hListener && hListener->updateHistogramRaw());
    
            highDetailPreprocessComputed = highDetailNeeded;
        }
    
        if (imageTypeListener) {
            imageTypeListener->imageTypeChanged(imgsrc->isRAW(), imgsrc->getSensorType() == ST_BAYER, imgsrc->getSensorType() == ST_FUJI_XTRANS, imgsrc->isMono());
        }

        if ((todo & M_RAW) || (!highDetailRawComputed && highDetailNeeded)) {
            if (settings->verbose) {
                if (imgsrc->getSensorType() == ST_BAYER) {
                    std::cout << "Demosaic Bayer image n." << rp.bayersensor.imageNum + 1 << " using method: " << RAWParams::BayerSensor::getMethodString(rp.bayersensor.method) << std::endl;
                } else if (imgsrc->getSensorType() == ST_FUJI_XTRANS) {
                    std::cout << "Demosaic X-Trans image with using method: " << RAWParams::XTransSensor::getMethodString(rp.xtranssensor.method) << std::endl;
                }
            }
            if(imgsrc->getSensorType() == ST_BAYER) {
                if(params.raw.bayersensor.method != RAWParams::BayerSensor::Method::PIXELSHIFT) {
                    imgsrc->setBorder(params.raw.bayersensor.border);
                } else {
                    imgsrc->setBorder(std::max(params.raw.bayersensor.border, 2));
                }
            } else if (imgsrc->getSensorType() == ST_FUJI_XTRANS) {
                imgsrc->setBorder(params.raw.xtranssensor.border);
            }
            bool autoContrast = imgsrc->getSensorType() == ST_BAYER ? params.raw.bayersensor.dualDemosaicAutoContrast : params.raw.xtranssensor.dualDemosaicAutoContrast;
            double contrastThreshold = imgsrc->getSensorType() == ST_BAYER ? params.raw.bayersensor.dualDemosaicContrast : params.raw.xtranssensor.dualDemosaicContrast;
            imgsrc->demosaic(rp, autoContrast, contrastThreshold); //enabled demosaic

            if (imgsrc->getSensorType() == ST_BAYER && bayerAutoContrastListener && autoContrast) {
                bayerAutoContrastListener->autoContrastChanged(autoContrast ? contrastThreshold : -1.0);
            }
            if (imgsrc->getSensorType() == ST_FUJI_XTRANS && xtransAutoContrastListener && autoContrast) {
                xtransAutoContrastListener->autoContrastChanged(autoContrast ? contrastThreshold : -1.0);
            }
    
            // if a demosaic happened we should also call getimage later, so we need to set the M_INIT flag
            todo |= M_INIT;
            highDetailRawComputed = highDetailNeeded;
        }   
    
        setScale(scale);
        int tr = getCoarseBitMask(params.coarse);    
        imgsrc->getFullSize(fw, fh, tr);
        PreviewProps pp(0, 0, fw, fh, scale);
        ipf.setScale(scale);

        if (todo & (M_INIT | M_LINDENOISE | M_HDR)) {
            MyMutex::MyLock initLock(minit);  // Also used in crop window
    
            if (params.wb.method == WBParams::AUTO) {
                if (lastAwbEqual != params.wb.equal) {
                    double rm, gm, bm;
                    imgsrc->getAutoWBMultipliers(rm, gm, bm);
    
                    if (rm != -1.) {
                        //autoWB.update(rm, gm, bm, params.wb.equal);
                        if (params.wb.equal == 1) {
                            autoWB = ColorTemp(rm, gm, bm);
                        } else {
                            autoWB = ColorTemp(rm, gm, bm, params.wb.equal);
                        }
                        lastAwbEqual = params.wb.equal;
                    } else {
                        lastAwbEqual = -1.;
                        autoWB.useDefaults(params.wb.equal);
                    }
                }
                
                currWB = autoWB;
            }
            if (params.wb.enabled) {
                params.wb.temperature = currWB.getTemp();
                params.wb.green = currWB.getGreen();
            }
    
            if (params.wb.method == WBParams::AUTO && awbListener && params.wb.enabled) {
                awbListener->WBChanged(currWB);
            }
    
            //setScale(scale);
            imgsrc->getImage(currWB, tr, orig_prev, pp, params.exposure, params.raw);
            // if (todo & M_INIT) {
            //     denoiseInfoStore.pparams = params;
            //     denoiseInfoStore.valid = false;
            // } else {
                denoiseInfoStore.valid = denoiseInfoStore.update_pparams(params);
            // }

            bool converted = false;
            if (params.filmNegative.colorSpace == FilmNegativeParams::ColorSpace::WORKING) {
                converted = true;
                imgsrc->convertColorSpace(orig_prev, params.icm, currWB);
            }            
            // Perform negative inversion. If needed, upgrade filmNegative params for backwards compatibility with old profiles
            if (params.filmNegative.enabled) {
                if (ipf.filmNegativeProcess(orig_prev, orig_prev, params.filmNegative, params.raw, imgsrc, currWB) && filmNegListener) {
                    filmNegListener->filmRefValuesChanged(params.filmNegative.refInput, params.filmNegative.refOutput);
                }
            }
            if (!converted) {
                imgsrc->convertColorSpace(orig_prev, params.icm, currWB);
            }
    
            ipf.firstAnalysis(orig_prev, params, vhist16);
        }

        orig_prev->assignColorSpace(params.icm.workingProfile);
        readyphase++;

        if (todo & M_SPOT) {
            if (params.spot.enabled && !params.spot.entries.empty()) {
                allocCache(spotprev);
                orig_prev->copyTo(spotprev);
                PreviewProps pp(0, 0, fw, fh, scale);
                ipf.removeSpots(spotprev, imgsrc, params.spot.entries, pp, currWB, &params.icm, tr, nullptr);
            } else {
                if (spotprev) {
                    delete spotprev;
                    spotprev = nullptr;
                }
            }
        }
        if (spotprev) {
            oprevi = spotprev;
        } else {
            oprevi = orig_prev;
            // if (oprevi == orig_prev) {
            //     oprevi = new Imagefloat(pW, pH);
            // }
            // spotprev->copyData(orig_prev);
        }
    
        if ((todo & M_HDR) && (params.fattal.enabled || params.dehaze.enabled)) {
            if (drcomp_11_dcrop_cache) {
                delete drcomp_11_dcrop_cache;
                drcomp_11_dcrop_cache = nullptr;
            }
    
            pipeline_stop_[0] = ipf.process(ImProcFunctions::Pipeline::NAVIGATOR, ImProcFunctions::Stage::STAGE_0, oprevi);//orig_prev);
    
            // if (oprevi != orig_prev) {
            //     delete oprevi;
            // }
        }
        stop = pipeline_stop_[0];
    
        // oprevi = orig_prev;
        
        progress("Rotate / Distortion...", 100 * readyphase / numofphases);
        // Remove transformation if unneeded
        if (ipf.needsTransform()) {
            assert(oprevi);
            Imagefloat *op = oprevi;
            oprevi = new Imagefloat(pW, pH, op);
            ipf.transform(op, oprevi, 0, 0, 0, 0, pW, pH, fw, fh,
                          imgsrc->getMetaData(), imgsrc->getRotateDegree(), false);
        }
    
        readyphase++;
        progress("Preparing shadow/highlight map...", 100 * readyphase / numofphases);
    
        readyphase++;

        if ((todo & M_INIT) && autoRadiusListener) {
            if (!imgsrc->getDeconvAutoRadius(nullptr)) {
                autoRadiusListener->autoDeconvRadiusChanged(-1);
            } else {
                autoRadiusListener->autoDeconvRadiusChanged(params.sharpening.deconvradius);
            }
        }
        
        if (todo & M_AUTOEXP) {
            if (params.toneCurve.histmatching) {
                if (!params.toneCurve.fromHistMatching) {
                    imgsrc->getAutoMatchedToneCurve(params.icm, params.toneCurve.curve, params.toneCurve.curve2);
                }
                params.toneCurve.fromHistMatching = true;
    
                if (aeListener) {
                    aeListener->autoMatchedToneCurveChanged(params.toneCurve.curve, params.toneCurve.curve2);
                }
            }

            if (params.logenc.enabled && params.logenc.autocompute) {
                ipf.getAutoLog(imgsrc, params.logenc);
                if (autoLogListener) {
                    autoLogListener->logEncodingChanged(params.logenc);
                }
            }

            if (params.sharpening.deconvAutoRadius) {
                float r = -1.f;
                if (imgsrc->getDeconvAutoRadius(&r)) {
                    params.sharpening.deconvradius = r;
                } else {
                    r = -1.f;
                }
                if (autoRadiusListener) {
                    autoRadiusListener->autoDeconvRadiusChanged(r);
                }
            }
        }
    
        progress("Exposure curve & CIELAB conversion...", 100 * readyphase / numofphases);
    
        if ((todo & M_RGBCURVE) || (todo & M_CROP)) {
            // if it's just crop we just need the histogram, no image updates
            if (todo & M_RGBCURVE) {
                //initialize rrm bbm ggm different from zero to avoid black screen in some cases
                oprevi->copyTo(bufs_[0]);
                pipeline_stop_[1] = stop || ipf.process(ImProcFunctions::Pipeline::NAVIGATOR, ImProcFunctions::Stage::STAGE_1, bufs_[0]);
            }
    
            // compute L channel histogram
            int x1, y1, x2, y2;
            params.crop.mapToResized(pW, pH, scale, x1, x2,  y1, y2);
        }
        stop = stop || pipeline_stop_[1];
    
        readyphase++;
    
        if (todo & M_LUMACURVE) {
            bufs_[0]->copyTo(bufs_[1]);
            pipeline_stop_[2] = stop || ipf.process(ImProcFunctions::Pipeline::NAVIGATOR, ImProcFunctions::Stage::STAGE_2, bufs_[1]);
        }
        stop = stop || pipeline_stop_[2];

        if (todo & (M_LUMINANCE | M_COLOR)) {
            bufs_[1]->copyTo(bufs_[2]);
            pipeline_stop_[3] = stop || ipf.process(ImProcFunctions::Pipeline::NAVIGATOR, ImProcFunctions::Stage::STAGE_3, bufs_[2]);
        }
        stop = stop || pipeline_stop_[3];
    
        // Update the monitor color transform if necessary
        if ((todo & M_MONITOR) || (lastOutputProfile != params.icm.outputProfile) || lastOutputIntent != params.icm.outputIntent || lastOutputBPC != params.icm.outputBPC) {
            lastOutputProfile = params.icm.outputProfile;
            lastOutputIntent = params.icm.outputIntent;
            lastOutputBPC = params.icm.outputBPC;
            ipf.updateColorProfiles(monitorProfile, monitorIntent, softProof, gamutCheck);
        }
    }

    // process crop, if needed
    for (size_t i = 0; i < crops.size(); i++)
        if (crops[i]->hasListener() && (panningRelatedChange || (highDetailNeeded && options.prevdemo != PD_Sidecar) || (todo & (M_MONITOR | M_RGBCURVE | M_LUMACURVE)) || crops[i]->get_skip() == 1)) {
            crops[i]->update(todo);     // may call ourselves
        }

    if (panningRelatedChange || (todo & M_MONITOR)) {
        progress("Conversion to RGB...", 100 * readyphase / numofphases);

        if ((todo != CROP && todo != MINUPDATE) || (todo & M_MONITOR)) {
            MyMutex::MyLock prevImgLock(previmg->getMutex());

            try {
                // Computing the preview image, i.e. converting from WCS->Monitor color space (soft-proofing disabled) or WCS->Printer profile->Monitor color space (soft-proofing enabled)
                ipf.rgb2monitor(bufs_[2], previmg);

                // Computing the internal image for analysis, i.e. conversion from WCS->Output profile
                delete workimg;
                workimg = ipf.rgb2out(bufs_[2], 0, 0, pW, pH, params.icm);
            } catch (char * str) {
                progress("Error converting file...", 0);
                return;
            }
        }

        if (!resultValid) {
            resultValid = true;

            if (imageListener) {
                imageListener->setImage(previmg, scale, params.crop);
            }
        }

        if (imageListener)
            // TODO: The WB tool should be advertised too in order to get the AutoWB's temp and green values
        {
            imageListener->imageReady(params.crop);
        }

        readyphase++;

        hist_lrgb_dirty = vectorscope_hc_dirty = vectorscope_hs_dirty = waveform_dirty = true;
        if (hListener) {
            if (hListener->updateHistogram()) {
                updateLRGBHistograms();
            }
            if (hListener->updateVectorscopeHC()) {
                updateVectorscopeHC();
            }
            if (hListener->updateVectorscopeHS()) {
                updateVectorscopeHS();
            }
            if (hListener->updateWaveform()) {
                updateWaveforms();
            }
            notifyHistogramChanged();
        }
    }
    if (orig_prev != oprevi && oprevi != spotprev) {
        delete oprevi;
        oprevi = nullptr;
    }
}


void ImProcCoordinator::updateWB()
{
    MyMutex::MyLock initLock(minit);
    
    currWB = ColorTemp(params.wb.temperature, params.wb.green, params.wb.equal, "Custom");
    
    if (!params.wb.enabled) {
        currWB = ColorTemp();
    } else {
        switch (params.wb.method) {
        case WBParams::CAMERA:
            currWB = imgsrc->getWB();
            break;
        case WBParams::CUSTOM_TEMP:
            currWB = ColorTemp(params.wb.temperature, params.wb.green, params.wb.equal, "Custom");
            break;
        case WBParams::CUSTOM_MULT_LEGACY:
            currWB = ColorTemp(params.wb.mult[0], params.wb.mult[1], params.wb.mult[2], 1.0);            
            break;
        case WBParams::CUSTOM_MULT: {
            double rm = params.wb.mult[0];
            double gm = params.wb.mult[1];
            double bm = params.wb.mult[2];
            imgsrc->wbCamera2Mul(rm, gm, bm);
            currWB = ColorTemp(rm, gm, bm);//, 1.0);            
        } break;
        case WBParams::AUTO:
        default:
            currWB = ColorTemp();
        }
    }
}


void ImProcCoordinator::setTweakOperator (TweakOperator *tOperator)
{
    if (tOperator) {
        tweakOperator = tOperator;
    }
}


void ImProcCoordinator::unsetTweakOperator (TweakOperator *tOperator)
{
    if (tOperator && tOperator == tweakOperator) {
        tweakOperator = nullptr;
    }
}


void ImProcCoordinator::freeAll()
{

    if (allocated) {
        if (spotprev && spotprev != oprevi) {
            delete spotprev;
        }
        spotprev = nullptr;
        if (orig_prev != oprevi) {
            delete oprevi;
        }

        oprevi    = nullptr;
        delete orig_prev;
        orig_prev = nullptr;
        for (int i = 3; i > 0; --i) {
            if (bufs_[i-1]) {
                delete bufs_[i-1];
                bufs_[i-1] = nullptr;
            }
        }

        if (imageListener) {
            imageListener->delImage(previmg);
        } else {
            delete previmg;
        }

        delete workimg;

    }

    allocated = false;
}

void ImProcCoordinator::allocCache (Imagefloat* &imgfloat)
{
    if (imgfloat == nullptr) {
        imgfloat = new Imagefloat(pW, pH);
    } else {
        imgfloat->allocate(pW, pH);
    }
}

/** @brief Handles image buffer (re)allocation and trigger sizeChanged of SizeListener[s]
 * If the scale change, this method will free all buffers and reallocate ones of the new size.
 * It will then tell to the SizeListener that size has changed (sizeChanged)
 *
 * @param prevscale New Preview's scale.
 */
void ImProcCoordinator::setScale(int prevscale)
{

    tr = getCoarseBitMask(params.coarse);

    int nW, nH;
    imgsrc->getFullSize(fw, fh, tr);

    prevscale++;

    do {
        prevscale--;
        PreviewProps pp(0, 0, fw, fh, prevscale);
        imgsrc->getSize(pp, nW, nH);
    } while (nH < 400 && prevscale > 1 && (nW * nH < 1000000));  // sctually hardcoded values, perhaps a better choice is possible

    if (nW != pW || nH != pH) {

        freeAll();

        pW = nW;
        pH = nH;

        orig_prev = new Imagefloat(pW, pH);
        oprevi = orig_prev;
        for (int i = 0; i < 3; ++i) {
            bufs_[i] = new Imagefloat(pW, pH);
        }
        previmg = new Image8(pW, pH);
        workimg = new Image8(pW, pH);

        allocated = true;
    }

    scale = prevscale;
    resultValid = false;
    fullw = fw;
    fullh = fh;

    orig_prev->assignColorSpace(params.icm.workingProfile);
    if (oprevi && oprevi != orig_prev) {
        oprevi->assignColorSpace(params.icm.workingProfile);
    }
    for (int i = 0; i < 3; ++i) {
        bufs_[i]->assignColorSpace(params.icm.workingProfile);
    }
    
    if (!sizeListeners.empty())
        for (size_t i = 0; i < sizeListeners.size(); i++) {
            sizeListeners[i]->sizeChanged(fullw, fullh, fw, fh);
        }
}


void ImProcCoordinator::notifyHistogramChanged()
{
    if (hListener) {
        hListener->histogramChanged(
            histRed,
            histGreen,
            histBlue,
            histLuma,
            histToneCurve,
            histLCurve,
            histCCurve,
            histLCAM,
            histCCAM,
            histRedRaw,
            histGreenRaw,
            histBlueRaw,
            histChroma,
            histLRETI,
            vectorscopeScale,
            vectorscope_hc,
            vectorscope_hs,
            waveformScale,
            waveformRed,
            waveformGreen,
            waveformBlue,
            waveformLuma
        );
    }
}


bool ImProcCoordinator::updateLRGBHistograms()
{

    if (!hist_lrgb_dirty) {
        return false;
    }

    int x1, y1, x2, y2;
    params.crop.mapToResized(pW, pH, scale, x1, x2, y1, y2);

#ifdef _OPENMP
    #pragma omp parallel sections
#endif
    {
#ifdef _OPENMP
        #pragma omp section
#endif
        {
            histLuma.clear();
            histChroma.clear();

            for (int i = y1; i < y2; i++)
                for (int j = x1; j < x2; j++)
                {
                    float L, a, b;
                    bufs_[2]->getLab(i, j, L, a, b);
                    histChroma[(int)(sqrtf(SQR(a) + SQR(b)) / 188.f)]++;      //188 = 48000/256
                    histLuma[(int)(L / 128.f)]++;
                }
        }
// #ifdef _OPENMP
//         #pragma omp section
// #endif
//         {
//             histLuma.clear();

//             for (int i = y1; i < y2; i++)
//                 for (int j = x1; j < x2; j++)
//                 {
//                     histLuma[(int)(nprevl->L[i][j] / 128.f)]++;
//                 }
//         }
#ifdef _OPENMP
        #pragma omp section
#endif
        {
            histRed.clear();
            histGreen.clear();
            histBlue.clear();

            for (int i = y1; i < y2; i++)
            {
                int ofs = (i * pW + x1) * 3;

                for (int j = x1; j < x2; j++) {
                    int r = workimg->data[ofs++];
                    int g = workimg->data[ofs++];
                    int b = workimg->data[ofs++];

                    histRed[r]++;
                    histGreen[g]++;
                    histBlue[b]++;
                }
            }
        }
    }

    hist_lrgb_dirty = false;
    return true;

}


namespace {

void rgb2lab(const Image8 &src, int x, int y, int w, int h, float L[], float a[], float b[], const procparams::ColorManagementParams &icm)
{ // Adapted from ImProcFunctions::lab2rgb
    const int src_width = src.getWidth();
    const int src_height = src.getHeight();

    if (x < 0) {
        x = 0;
    }

    if (y < 0) {
        y = 0;
    }

    if (x + w > src_width) {
        w = src_width - x;
    }

    if (y + h > src_height) {
        h = src_height - y;
    }

    Glib::ustring profile;

    cmsHPROFILE oprof = nullptr;

    if (settings->HistogramWorking) {
        profile = icm.workingProfile;
    } else {
        profile = icm.outputProfile;

        if (icm.outputProfile.empty() || icm.outputProfile == ColorManagementParams::NoICMString) {
            profile = "sRGB";
        }
        oprof = ICCStore::getInstance()->getProfile(profile);
    }

    if (oprof) {
        cmsUInt32Number flags = cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE; // NOCACHE is important for thread safety

        if (icm.outputBPC) {
            flags |= cmsFLAGS_BLACKPOINTCOMPENSATION;
        }

        lcmsMutex->lock();
        cmsHPROFILE LabIProf  = cmsCreateLab4Profile(nullptr);
        cmsHTRANSFORM hTransform = cmsCreateTransform (oprof, TYPE_RGB_8, LabIProf, TYPE_Lab_FLT, icm.outputIntent, flags);
        cmsCloseProfile(LabIProf);
        lcmsMutex->unlock();

        // cmsDoTransform is relatively expensive
#ifdef _OPENMP
        #pragma omp parallel
#endif
        {
            AlignedBuffer<float> oBuf(3 * w);
            float *outbuffer = oBuf.data;
            int condition = y + h;

#ifdef _OPENMP
            #pragma omp for schedule(dynamic,16)
#endif

            for (int i = y; i < condition; i++) {
                const int ix = 3 * (x + i * src_width);
                int iy = 0;
                float* rL = L + (i - y) * w;
                float* ra = a + (i - y) * w;
                float* rb = b + (i - y) * w;

                cmsDoTransform (hTransform, src.data + ix, outbuffer, w);

                for (int j = 0; j < w; j++) {
                    rL[j] = outbuffer[iy++] * 327.68f;
                    ra[j] = outbuffer[iy++] * 327.68f;
                    rb[j] = outbuffer[iy++] * 327.68f;
                }
            }
        } // End of parallelization

        cmsDeleteTransform(hTransform);
    } else {
        TMatrix wprof = ICCStore::getInstance()->workingSpaceMatrix(profile);
        const float wp[3][3] = {
            {static_cast<float>(wprof[0][0]), static_cast<float>(wprof[0][1]), static_cast<float>(wprof[0][2])},
            {static_cast<float>(wprof[1][0]), static_cast<float>(wprof[1][1]), static_cast<float>(wprof[1][2])},
            {static_cast<float>(wprof[2][0]), static_cast<float>(wprof[2][1]), static_cast<float>(wprof[2][2])}
        };

        const int x2 = x + w;
        const int y2 = y + h;
        constexpr float rgb_factor = 65355.f / 255.f;

#ifdef _OPENMP
#       pragma omp parallel for schedule(dynamic,16) //if (multiThread)
#endif
        for (int i = y; i < y2; i++) {
            int offset = (i - y) * w;
            for (int j = x; j < x2; j++) {
                float X, Y, Z;
                // lab2rgb uses gamma2curve, which is gammatab_srgb.
                const auto &igamma = Color::igammatab_srgb;
                Color::rgbxyz(igamma[rgb_factor * src.r(i, j)], igamma[rgb_factor * src.g(i, j)], igamma[rgb_factor * src.b(i, j)], X, Y, Z, wp);
                Color::XYZ2Lab(X, Y, Z, L[offset], a[offset], b[offset]);
                offset++;
            }
        }
    }
}

} // namespace

bool ImProcCoordinator::updateVectorscopeHC()
{
    if (!workimg || !vectorscope_hc_dirty) {
        return false;
    }

    int x1, y1, x2, y2;
    params.crop.mapToResized(pW, pH, scale, x1, x2, y1, y2);

    constexpr int size = VECTORSCOPE_SIZE;
    constexpr float norm_factor = size / (128.f * 655.36f);
    vectorscope_hc.fill(0);

    vectorscopeScale = (x2 - x1) * (y2 - y1);

    const std::unique_ptr<float[]> a(new float[vectorscopeScale]);
    const std::unique_ptr<float[]> b(new float[vectorscopeScale]);
    const std::unique_ptr<float[]> L(new float[vectorscopeScale]);
    rgb2lab(*workimg, x1, y1, x2 - x1, y2 - y1, L.get(), a.get(), b.get(), params.icm);
    
#ifdef _OPENMP
    #pragma omp parallel
#endif
    {
        array2D<int> vectorscopeThr(size, size, ARRAY2D_CLEAR_DATA);
#ifdef _OPENMP
        #pragma omp for nowait
#endif
        for (int i = y1; i < y2; ++i) {
            for (int j = x1, ofs_lab = (i - y1) * (x2 - x1); j < x2; ++j, ++ofs_lab) {
                const int col = norm_factor * a[ofs_lab] + size / 2 + 0.5f;
                const int row = norm_factor * b[ofs_lab] + size / 2 + 0.5f;
                if (col >= 0 && col < size && row >= 0 && row < size) {
                    vectorscopeThr[row][col]++;
                }
            }
        }
#ifdef _OPENMP
        #pragma omp critical
#endif
        {
            for (int y = 0; y < vectorscope_hc.height(); ++y) {
#ifdef _OPENMP
#               pragma omp simd
#endif
                for (int x = 0; x < vectorscope_hc.width(); ++x) {
                    vectorscope_hc[y][x] += vectorscopeThr[y][x];
                }
            }
        }
    }

    vectorscope_hc_dirty = false;
    return true;
}


bool ImProcCoordinator::updateVectorscopeHS()
{
    if (!workimg || !vectorscope_hs_dirty) {
        return false;
    }

    int x1, y1, x2, y2;
    params.crop.mapToResized(pW, pH, scale, x1, x2, y1, y2);

    constexpr int size = VECTORSCOPE_SIZE;
    vectorscope_hs.fill(0);

    vectorscopeScale = (x2 - x1) * (y2 - y1);

#ifdef _OPENMP
    #pragma omp parallel
#endif
    {
        array2D<int> vectorscopeThr(size, size, ARRAY2D_CLEAR_DATA);
#ifdef _OPENMP
        #pragma omp for nowait
#endif
        for (int i = y1; i < y2; ++i) {
            int ofs = (i * pW + x1) * 3;
            for (int j = x1; j < x2; ++j) {
                const float red = 257.f * workimg->data[ofs++];
                const float green = 257.f * workimg->data[ofs++];
                const float blue = 257.f * workimg->data[ofs++];
                float h, s, l;
                Color::rgb2hslfloat(red, green, blue, h, s, l);
                const auto sincosval = xsincosf(2.f * RT_PI_F * h);
                const int col = s * sincosval.y * (size / 2) + size / 2;
                const int row = s * sincosval.x * (size / 2) + size / 2;
                if (col >= 0 && col < size && row >= 0 && row < size) {
                    vectorscopeThr[row][col]++;
                }
            }
        }
#ifdef _OPENMP
        #pragma omp critical
#endif
        {
            for (int y = 0; y < vectorscope_hs.height(); ++y) {
#ifdef _OPENMP
#               pragma omp simd
#endif
                for (int x = 0; x < vectorscope_hs.width(); ++x) {
                    vectorscope_hs[y][x] += vectorscopeThr[y][x];
                }
            }
        }
    }

    vectorscope_hs_dirty = false;
    return true;
}


bool ImProcCoordinator::updateWaveforms()
{
    if (!workimg) {
        // free memory
        waveformRed.free();
        waveformGreen.free();
        waveformBlue.free();
        waveformLuma.free();
        return true;
    }

    if (!waveform_dirty) {
        return false;
    }

    int x1, y1, x2, y2;
    params.crop.mapToResized(pW, pH, scale, x1, x2, y1, y2);
    int waveform_width = waveformRed.width();

    if (waveform_width != x2 - x1) {
        // Resize waveform arrays.
        waveform_width = x2 - x1;
        waveformRed(waveform_width, 256);
        waveformGreen(waveform_width, 256);
        waveformBlue(waveform_width, 256);
        waveformLuma(waveform_width, 256);
    }

    // Start with zero.
    waveformRed.fill(0);
    waveformGreen.fill(0);
    waveformBlue.fill(0);
    waveformLuma.fill(0);

    constexpr float luma_factor = 255.f / 32768.f;
    for (int i = y1; i < y2; i++) {
        int ofs = (i * pW + x1) * 3;
        //float* L_row = nprevl->L[i] + x1;

        for (int j = 0; j < waveform_width; j++) {
            waveformRed[workimg->data[ofs++]][j]++;
            waveformGreen[workimg->data[ofs++]][j]++;
            waveformBlue[workimg->data[ofs++]][j]++;
            float L, a, b;
            bufs_[2]->getLab(i, j, L, a, b);
            waveformLuma[LIM<int>(L * luma_factor + 0.5, 0, 255)][j]++;
        }
    }

    waveformScale = y2 - y1;
    waveform_dirty = false;
    return true;
}


void ImProcCoordinator::progress(Glib::ustring str, int pr)
{

    /*  if (plistener) {
        plistener->setProgressStr (str);
        plistener->setProgress ((double)pr / 100.0);
      }*/
}

bool ImProcCoordinator::getAutoWB(ColorTemp &out, double equal)
{

    if (imgsrc) {
        if (lastAwbEqual != equal) {
// Issue 2500            MyMutex::MyLock lock(minit);  // Also used in crop window
            double rm, gm, bm;
            imgsrc->getAutoWBMultipliers(rm, gm, bm);

            if (rm != -1) {
                autoWB.update(rm, gm, bm, equal);
                lastAwbEqual = equal;
            } else {
                lastAwbEqual = -1.;
                autoWB.useDefaults(equal);
            }
        }

        // temp = autoWB.getTemp();
        // green = autoWB.getGreen();
        out = autoWB;
        return true;
    } else {
        //temp = autoWB.getTemp();
        // temp = -1.0;
        // green = -1.0;
        out = ColorTemp();
        return false;
    }
}

void ImProcCoordinator::getCamWB(ColorTemp &out)
{
    if (imgsrc) {
        out = imgsrc->getWB();
    } else {
        out = rtengine::ColorTemp();
    }
}

void ImProcCoordinator::getSpotWB(int x, int y, int rect, ColorTemp &out)
{
    ColorTemp ret;

    {
        MyMutex::MyLock lock(mProcessing);
        std::vector<Coord2D> points, red, green, blue;

        for (int i = y - rect; i <= y + rect; i++)
            for (int j = x - rect; j <= x + rect; j++) {
                points.push_back(Coord2D(j, i));
            }

        ipf.transCoord(fw, fh, points, red, green, blue);

        int tr = getCoarseBitMask(params.coarse);

        ret = imgsrc->getSpotWB(red, green, blue, tr, params.wb.equal);
        currWB = ColorTemp(params.wb.temperature, params.wb.green, params.wb.equal, "Custom");
        //double rr,gg,bb;
        //currWB.getMultipliers(rr,gg,bb);

    }

    if (ret.getTemp() > 0) {
        out = ret;
        // temp = ret.getTemp();
        // tgreen = ret.getGreen();
    } else {
        out = currWB;
        // temp = currWB.getTemp();
        // tgreen = currWB.getGreen();
    }
}

void ImProcCoordinator::getAutoCrop(double ratio, int &x, int &y, int &w, int &h)
{

    MyMutex::MyLock lock(mProcessing);

    LensCorrection *pLCPMap = nullptr;

    if (params.lensProf.useLcp() && imgsrc->getMetaData()->getFocalLen() > 0) {
        const std::shared_ptr<LCPProfile> pLCPProf = LCPStore::getInstance()->getProfile(params.lensProf.lcpFile);

        if (pLCPProf) pLCPMap = new LCPMapper(pLCPProf, imgsrc->getMetaData()->getFocalLen(), imgsrc->getMetaData()->getFocalLen35mm(), imgsrc->getMetaData()->getFocusDist(),
                                                  0, false, params.lensProf.useDist, fullw, fullh, params.coarse, imgsrc->getRotateDegree());
    }

    double fillscale = ipf.getTransformAutoFill(fullw, fullh, pLCPMap);

    if (ratio > 0) {
        w = fullw * fillscale;
        h = w / ratio;

        if (h > fullh * fillscale) {
            h = fullh * fillscale;
            w = h * ratio;
        }
    } else {
        w = fullw * fillscale;
        h = fullh * fillscale;
    }

    x = (fullw - w) / 2;
    y = (fullh - h) / 2;

    if (params.perspective.enabled && !params.commonTrans.autofill) {
        int xx, yy, ww, hh;
        PerspectiveCorrection::autocrop(w, h, ratio > 0, params.perspective, imgsrc->getMetaData(), xx, yy, ww, hh);
        x += xx;
        y += yy;
        w = ww;
        h = hh;
    }
}


void ImProcCoordinator::setMonitorProfile(const Glib::ustring& profile, RenderingIntent intent)
{
    monitorProfile = profile;
    monitorIntent = intent;
}

void ImProcCoordinator::getMonitorProfile(Glib::ustring& profile, RenderingIntent& intent) const
{
    profile = monitorProfile;
    intent = monitorIntent;
}

void ImProcCoordinator::setSoftProofing(bool softProof, GamutCheck gamutCheck)
{
    this->softProof = softProof;
    this->gamutCheck = gamutCheck;
}

void ImProcCoordinator::setSharpMask (bool sharpMask)
{
    this->sharpMask = sharpMask;
}

void ImProcCoordinator::saveInputICCReference(const Glib::ustring& fname, bool apply_wb)
{

    MyMutex::MyLock lock(mProcessing);

    int fW, fH;

    int tr = getCoarseBitMask(params.coarse);

    imgsrc->getFullSize(fW, fH, tr);
    PreviewProps pp(0, 0, fW, fH, 1);
    ProcParams ppar = params;
    ppar.exposure.hrmode = procparams::ExposureParams::HR_OFF;
    ppar.icm.inputProfile = "(none)";
    Imagefloat* im = new Imagefloat(fW, fH);
    im->assignColorSpace(ppar.icm.workingProfile);
    imgsrc->preprocess(ppar.raw, ppar.lensProf, ppar.coarse);
    double dummy = 0.0;
    imgsrc->demosaic(ppar.raw, false, dummy);
    ColorTemp currWB;

    if (apply_wb) {
        switch (params.wb.method) {
        case WBParams::CAMERA:
            currWB = imgsrc->getWB();
            break;
        case WBParams::AUTO:
            if (lastAwbEqual != params.wb.equal) {
                double rm, gm, bm;
                imgsrc->getAutoWBMultipliers(rm, gm, bm);

                if (rm != -1.) {
                    autoWB.update(rm, gm, bm, params.wb.equal);
                    lastAwbEqual = params.wb.equal;
                } else {
                    lastAwbEqual = -1.;
                    autoWB.useDefaults(params.wb.equal);
                }
            }

            currWB = autoWB;
            break;
        case WBParams::CUSTOM_TEMP:
            currWB = ColorTemp(params.wb.temperature, params.wb.green, params.wb.equal, "Custom");
            break;
        case WBParams::CUSTOM_MULT_LEGACY:
            currWB = ColorTemp(params.wb.mult[0], params.wb.mult[1], params.wb.mult[2], 1.0);
            break;
        case WBParams::CUSTOM_MULT: {
            double rm = params.wb.mult[0];
            double gm = params.wb.mult[1];
            double bm = params.wb.mult[2];
            imgsrc->wbCamera2Mul(rm, gm, bm);
            currWB = ColorTemp(rm, gm, bm);//, 1.0);
        } break;
        }            
    }

    imgsrc->getImage(currWB, tr, im, pp, ppar.exposure, ppar.raw);
    ImProcFunctions ipf(&ppar, true);

    if (ipf.needsTransform()) {
        Imagefloat* trImg = new Imagefloat(fW, fH, im);
        ipf.transform(im, trImg, 0, 0, 0, 0, fW, fH, fW, fH,
                      imgsrc->getMetaData(), imgsrc->getRotateDegree(), true);
        delete im;
        im = trImg;
    }

    if (params.crop.enabled) {
        Imagefloat *tmpim = new Imagefloat(params.crop.w, params.crop.h, im);
        int cx = params.crop.x;
        int cy = params.crop.y;
        int cw = params.crop.w;
        int ch = params.crop.h;
#ifdef _OPENMP
        #pragma omp parallel for
#endif

        for (int i = cy; i < cy + ch; i++) {
            for (int j = cx; j < cx + cw; j++) {
                tmpim->r(i - cy, j - cx) = im->r(i, j);
                tmpim->g(i - cy, j - cx) = im->g(i, j);
                tmpim->b(i - cy, j - cx) = im->b(i, j);
            }
        }

        delete im;
        im = tmpim;
    }

    // image may contain out of range samples, clip them to avoid wrap-arounds
#ifdef _OPENMP
    #pragma omp parallel for
#endif

    for (int i = 0; i < im->getHeight(); i++) {
        for (int j = 0; j < im->getWidth(); j++) {
            im->r(i, j) = CLIP(im->r(i, j));
            im->g(i, j) = CLIP(im->g(i, j));
            im->b(i, j) = CLIP(im->b(i, j));
        }
    }

    int imw, imh;
    double tmpScale = ipf.resizeScale(&params, fW, fH, imw, imh);

    if (tmpScale != 1.0) {
        Imagefloat* tempImage = new Imagefloat(imw, imh, im);
        ipf.resize(im, tempImage, tmpScale);
        delete im;
        im = tempImage;
    }

    // im->setMetadata(imgsrc->getMetaData()->getRootExifData());
    im->setMetadata(Exiv2Metadata(imgsrc->getFileName(), false));

    im->saveTIFF(fname, 16, false, true);
    delete im;

    if (plistener) {
        plistener->setProgressState(false);
    }

    //im->saveJPEG (fname, 85);
}

void ImProcCoordinator::stopProcessing()
{

    // updaterThreadStart.lock();

    if (updaterRunning) {
        changeSinceLast = 0;
        wait_not_running();
    }
    // if (updaterRunning && thread) {
    //     changeSinceLast = 0;
    //     thread->join();
    // }

    // updaterThreadStart.unlock();
}


void ImProcCoordinator::startProcessing()
{
    if (!destroying) {
        if (!updaterRunning) {
            // updaterThreadStart.lock();
            set_updater_running(true);
            // updaterThreadStart.unlock();

            rtengine::ThreadPool::add_task(rtengine::ThreadPool::Priority::HIGHEST, sigc::mem_fun(*this, &ImProcCoordinator::process));
        }
    }
}


void ImProcCoordinator::startProcessing(int changeCode)
{
    paramsUpdateMutex.lock();
    changeSinceLast |= changeCode;
    paramsUpdateMutex.unlock();

    startProcessing();
}

void ImProcCoordinator::process()
{
    if (plistener) {
        plistener->setProgressState(true);
        ipf.setProgressListener(plistener, crops.size() + 1);
    }

    paramsUpdateMutex.lock();

    bool changed = false;
    while (changeSinceLast) {
        const bool panningRelatedChange = true;
        params = nextParams;
        int change = changeSinceLast;
        changeSinceLast = 0;
        if (tweakOperator) {
            // TWEAKING THE PROCPARAMS FOR THE SPOT ADJUSTMENT MODE
            backupParams();
            tweakOperator->tweakParams(params);
        } 
        /* TODODANCAT see if this is needed here anymore
        else if (paramsBackup) {
            paramsBackup.release();
        }
        */
        paramsUpdateMutex.unlock();

        // M_VOID means no update, and is a bit higher that the rest
        if (change & (M_VOID - 1)) {
            updatePreviewImage(change, panningRelatedChange);
            changed = true;
        }

        paramsUpdateMutex.lock();

        if (tweakOperator) {
            restoreParams();
        }
    }

    paramsUpdateMutex.unlock();

    set_updater_running(false);

    if (plistener) {
        if (!changed) {
            plistener->setProgressStr("PROGRESSBAR_READY");
        }
        plistener->setProgressState(false);
    }
}

ProcParams* ImProcCoordinator::beginUpdateParams()
{
    paramsUpdateMutex.lock();

    return &nextParams;
}

void ImProcCoordinator::endUpdateParams(ProcEvent change)
{
    int action = RefreshMapper::getInstance()->getAction(change);
    endUpdateParams(action);
}

void ImProcCoordinator::endUpdateParams(int changeFlags)
{
    changeSinceLast |= changeFlags;
    for (auto p : nextParams.get_maskable()) {
        for (auto &m : p->get_masks()) {
            if (m.rasterMask.enabled && !m.rasterMask.toolname.empty() && !m.rasterMask.name.empty()) {
                changeSinceLast |= LINKEDMASK;
                goto out;
            }
        }
    }
  out:

    paramsUpdateMutex.unlock();
    startProcessing();
}

bool ImProcCoordinator::getHighQualComputed()
{
    // this function may only be called from detail windows
    if (!highQualityComputed) {
        if (options.prevdemo == PD_Sidecar && options.wb_preview_mode != Options::WB_BEFORE_HIGH_DETAIL) {
            // we already have high quality preview
            setHighQualComputed();
        } else {
            for (size_t i = 0; i < crops.size() - 1; ++i) { // -1, because last entry is the freshly created detail window
                if (crops[i]->get_skip() == 1) {   // there is at least one crop with skip == 1 => we already have high quality preview
                    setHighQualComputed();
                    break;
                }
            }
        }
    }

    return highQualityComputed;
}

void ImProcCoordinator::setHighQualComputed()
{
    highQualityComputed = true;
}


bool ImProcCoordinator::getDeltaELCH(EditUniqueID id, int x, int y, float &L, float &C, float &H)
{
    int change = ipf.setDeltaEData(id, x, y);
    if (!change) {
        return false;
    }
    startProcessing(change);

    bool ret = false;
    // updaterThreadStart.lock();
    if (updaterRunning) {// && thread) {
        wait_not_running();
        //thread->join();
        if (ipf.deltaE.ok) {
            ret = true;
            L = ipf.deltaE.L;
            C = ipf.deltaE.C;
            H = ipf.deltaE.H;
        }
    }
    ipf.setDeltaEData(EUID_None, -1, -1);
    // updaterThreadStart.unlock();

    return ret;
}



void ImProcCoordinator::requestUpdateWaveform()
{
    if (!hListener) {
        return;
    }
    bool updated = updateWaveforms();
    if (updated) {
        notifyHistogramChanged();
    }
}

void ImProcCoordinator::requestUpdateHistogram()
{
    if (!hListener) {
        return;
    }
    bool updated = updateLRGBHistograms();
    if (updated) {
        notifyHistogramChanged();
    }
}

void ImProcCoordinator::requestUpdateHistogramRaw()
{
    if (!hListener) {
        return;
    }
    // Don't need to actually update histogram because it is always
    // up-to-date.
    if (hist_raw_dirty) {
        hist_raw_dirty = false;
        notifyHistogramChanged();
    }
}

void ImProcCoordinator::requestUpdateVectorscopeHC()
{
    if (!hListener) {
        return;
    }
    bool updated = updateVectorscopeHC();
    if (updated) {
        notifyHistogramChanged();
    }
}

void ImProcCoordinator::requestUpdateVectorscopeHS()
{
    if (!hListener) {
        return;
    }
    bool updated = updateVectorscopeHS();
    if (updated) {
        notifyHistogramChanged();
    }
}


void ImProcCoordinator::wait_not_running()
{
    std::unique_lock<std::mutex> lck(updater_mutex_);
    while (updaterRunning) {
        updater_cond_.wait(lck);
    }
}


void ImProcCoordinator::set_updater_running(bool val)
{
    std::unique_lock<std::mutex> lck(updater_mutex_);
    if (val) {
        while (updaterRunning) {
            updater_cond_.wait(lck);
        }
        updaterRunning = true;
    } else {
        updaterRunning = false;
        updater_cond_.notify_all();
    }
}


bool ImProcCoordinator::is_running() const
{
    if (updaterRunning) {
        return true;
    }
    for (auto c : crops) {
        if (c->updating) {
            return true;
        }
    }
    return false;
}


bool ImProcCoordinator::is_mask_image() const
{
    if (sharpMask) {
        return true;
    }

#define CHECK_MASK_(p) \
    if (p.enabled && p.showMask >= 0 && size_t(p.showMask) < p.labmasks.size() && p.labmasks[p.showMask].enabled) { \
        return true; \
    }
    
    CHECK_MASK_(params.colorcorrection);
    CHECK_MASK_(params.smoothing);
    CHECK_MASK_(params.textureBoost);
    CHECK_MASK_(params.localContrast);
#undef CHECK_MASK_

    return false;
}

} // namespace rtengine
