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
#include <cmath>
#include <glib.h>
#include <glibmm.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "alignedbuffer.h"
#include "rtengine.h"
#include "improcfun.h"
#include "curves.h"
#include "mytime.h"
#include "iccstore.h"
#include "imagesource.h"
#include "rtthumbnail.h"
#include "utils.h"
#include "iccmatrices.h"
#include "color.h"
#include "calc_distort.h"
#include "rt_math.h"
#include "improccoordinator.h"
#include "clutstore.h"
#include "StopWatch.h"
#include "../rtgui/ppversion.h"
#include "../rtgui/guiutils.h"
#include "refreshmap.h"

namespace rtengine {

using namespace procparams;

extern const Settings* settings;

ImProcFunctions::ImProcFunctions(const ProcParams* iparams, bool imultiThread):
    monitor(nullptr),
    monitorTransform(nullptr),
    params(iparams),
    scale(1),
    multiThread(imultiThread),
    cur_pipeline(Pipeline::OUTPUT),
    dcpProf(nullptr),
    dcpApplyState(nullptr),
    pipetteBuffer(nullptr),
    lumimul{},
    offset_x(0),
    offset_y(0),
    full_width(-1),
    full_height(-1),
    histToneCurve(nullptr),
    histCCurve(nullptr),
    histLCurve(nullptr),
    show_sharpening_mask(false),
    plistener(nullptr),
    progress_step(0),
    progress_end(1)
{
}


ImProcFunctions::~ImProcFunctions ()
{
    if (monitorTransform) {
        cmsDeleteTransform (monitorTransform);
    }
}

void ImProcFunctions::setScale (double iscale)
{
    scale = iscale;
}


void ImProcFunctions::updateColorProfiles (const Glib::ustring& monitorProfile, RenderingIntent monitorIntent, bool softProof, GamutCheck gamutCheck)
{
    // set up monitor transform
    if (monitorTransform) {
        cmsDeleteTransform (monitorTransform);
    }
    gamutWarning.reset(nullptr);

    monitorTransform = nullptr;
    monitor = nullptr;

    if (settings->color_mgmt_mode != Settings::ColorManagementMode::APPLICATION) {
        monitor = ICCStore::getInstance()->getActiveMonitorProfile();
    } else {
        if (!monitorProfile.empty()) {
            monitor = ICCStore::getInstance()->getProfile(monitorProfile);
        }
    }

    if (monitor) {
        MyMutex::MyLock lcmsLock (*lcmsMutex);

        cmsUInt32Number flags;
        //cmsHPROFILE iprof  = cmsCreateLab4Profile (nullptr);
        cmsHPROFILE iprof = nullptr;
        if (params->icm.outputProfile == procparams::ColorManagementParams::NoProfileString) {
            iprof = ICCStore::getInstance()->workingSpace(params->icm.workingProfile);
        } else {
            iprof = ICCStore::getInstance()->getProfile(params->icm.outputProfile);
        }
        if (!iprof) {
            iprof = ICCStore::getInstance()->getsRGBProfile();
        }
        
        cmsHPROFILE gamutprof = nullptr;
        cmsUInt32Number gamutbpc = 0;
        RenderingIntent gamutintent = RI_RELATIVE;

        bool softProofCreated = false;
        cmsHPROFILE oprof = nullptr;
        RenderingIntent outIntent;

        if (softProof) {
            flags = cmsFLAGS_SOFTPROOFING | cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE;

            if (!settings->printerProfile.empty()) {
                oprof = ICCStore::getInstance()->getProfile (settings->printerProfile);
                if (settings->printerBPC) {
                    flags |= cmsFLAGS_BLACKPOINTCOMPENSATION;
                }
                outIntent = settings->printerIntent;
            // } else {
            //     oprof = ICCStore::getInstance()->getProfile(params->icm.outputProfile);
            //     if (params->icm.outputBPC) {
            //         flags |= cmsFLAGS_BLACKPOINTCOMPENSATION;
            //     }
            //     outIntent = params->icm.outputIntent;
            }

            if (oprof) {
                // NOCACHE is for thread safety, NOOPTIMIZE for precision

                // if (gamutCheck) {
                //     flags |= cmsFLAGS_GAMUTCHECK;
                // }

                const auto make_gamma_table =
                    [](cmsHPROFILE prof, cmsTagSignature tag) -> void
                    {
                        cmsToneCurve *tc = static_cast<cmsToneCurve *>(cmsReadTag(prof, tag));
                        if (tc) {
                            const cmsUInt16Number *table = cmsGetToneCurveEstimatedTable(tc);
                            cmsToneCurve *tc16 = cmsBuildTabulatedToneCurve16(nullptr, cmsGetToneCurveEstimatedTableEntries(tc), table);
                            if (tc16) {
                                cmsWriteTag(prof, tag, tc16);
                                cmsFreeToneCurve(tc16);
                            }
                        }
                    };

                cmsHPROFILE softproof = ProfileContent(oprof).toProfile();
                if (softproof) {
                    make_gamma_table(softproof, cmsSigRedTRCTag);
                    make_gamma_table(softproof, cmsSigGreenTRCTag);
                    make_gamma_table(softproof, cmsSigBlueTRCTag);
                }

                monitorTransform = cmsCreateProofingTransform (
                    iprof, TYPE_RGB_FLT, //TYPE_Lab_FLT,
                                       monitor, TYPE_RGB_FLT,
                                       softproof,
                                       monitorIntent, outIntent,
                                       flags
                                   );

                if (softproof) {
                    cmsCloseProfile(softproof);
                }

                if (monitorTransform) {
                    softProofCreated = true;
                }

                // if (gamutCheck == GAMUT_CHECK_OUTPUT) {
                //     gamutprof = oprof;
                //     if (params->icm.outputBPC) {
                //         gamutbpc = cmsFLAGS_BLACKPOINTCOMPENSATION;
                //     }
                //     gamutintent = outIntent;
                // }
            }
        }

        if (gamutCheck == GAMUT_CHECK_MONITOR) {
            gamutprof = monitor;
            if (settings->monitorBPC) {
                gamutbpc = cmsFLAGS_BLACKPOINTCOMPENSATION;
            }
            gamutintent = monitorIntent;
        } else if (gamutCheck == GAMUT_CHECK_OUTPUT) {
            if (!oprof) {
                oprof = ICCStore::getInstance()->getProfile(params->icm.outputProfile);
                if (params->icm.outputBPC) {
                    flags |= cmsFLAGS_BLACKPOINTCOMPENSATION;
                }
                outIntent = params->icm.outputIntent;
            }
            gamutprof = oprof;
            if (params->icm.outputBPC) {
                gamutbpc = cmsFLAGS_BLACKPOINTCOMPENSATION;
            }
            gamutintent = outIntent;
        }

        if (!softProofCreated) {
            flags = cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE;

            if (settings->monitorBPC) {
                flags |= cmsFLAGS_BLACKPOINTCOMPENSATION;
            }

            monitorTransform = cmsCreateTransform (iprof, TYPE_RGB_FLT, 
                                                   monitor, TYPE_RGB_FLT, monitorIntent, flags);
        }

        if (gamutCheck && gamutprof) {
            gamutWarning.reset(new GamutWarning(gamutprof, gamutintent, gamutbpc));
        }

//        cmsCloseProfile (iprof);
    }
}

void ImProcFunctions::firstAnalysis (const Imagefloat* const original, const ProcParams &params, LUTu & histogram)
{

    TMatrix wprof = ICCStore::getInstance()->workingSpaceMatrix (params.icm.workingProfile);

    lumimul[0] = wprof[1][0];
    lumimul[1] = wprof[1][1];
    lumimul[2] = wprof[1][2];
    int W = original->getWidth();
    int H = original->getHeight();

    float lumimulf[3] = {static_cast<float> (lumimul[0]), static_cast<float> (lumimul[1]), static_cast<float> (lumimul[2])};

    // calculate histogram of the y channel needed for contrast curve calculation in exposure adjustments
    histogram.clear();

    if (multiThread) {

#ifdef _OPENMP
        const int numThreads = min (max (W * H / (int)histogram.getSize(), 1), omp_get_num_procs());
        #pragma omp parallel num_threads(numThreads) if(numThreads>1)
#endif
        {
            LUTu hist (histogram.getSize());
            hist.clear();
#ifdef _OPENMP
            #pragma omp for nowait
#endif

            for (int i = 0; i < H; i++) {
                for (int j = 0; j < W; j++) {

                    float r = original->r (i, j);
                    float g = original->g (i, j);
                    float b = original->b (i, j);

                    int y = (lumimulf[0] * r + lumimulf[1] * g + lumimulf[2] * b);
                    hist[y]++;
                }
            }

#ifdef _OPENMP
            #pragma omp critical
#endif
            histogram += hist;

        }
#ifdef _OPENMP
        static_cast<void> (numThreads); // to silence cppcheck warning
#endif
    } else {
        for (int i = 0; i < H; i++) {
            for (int j = 0; j < W; j++) {

                float r = original->r (i, j);
                float g = original->g (i, j);
                float b = original->b (i, j);

                int y = (lumimulf[0] * r + lumimulf[1] * g + lumimulf[2] * b);
                histogram[y]++;
            }
        }
    }
}


namespace {

void proPhotoBlue(Imagefloat *rgb, bool multiThread)
{
    // TODO
    const int W = rgb->getWidth();
    const int H = rgb->getHeight();
#ifdef _OPENMP
#   pragma omp parallel for if (multiThread)
#endif
    for (int y = 0; y < H; ++y) {
        int x = 0;
#ifdef __SSE2__
        for (; x < W - 3; x += 4) {
            vfloat rv = LVF(rgb->r(y, x));
            vfloat gv = LVF(rgb->g(y, x));
            vmask zeromask = vorm(vmaskf_eq(rv, ZEROV), vmaskf_eq(gv, ZEROV));
            if (_mm_movemask_ps((vfloat)zeromask)) {
                for (int k = 0; k < 4; ++k) {
                    float r = rgb->r(y, x+k);
                    float g = rgb->g(y, x+k);
                    float b = rgb->b(y, x+k);
                    
                    if ((r == 0.0f || g == 0.0f) && rtengine::min(r, g, b) >= 0.f) {
                        float h, s, v;
                        Color::rgb2hsv(r, g, b, h, s, v);
                        s *= 0.99f;
                        Color::hsv2rgb(h, s, v, rgb->r(y, x+k), rgb->g(y, x+k), rgb->b(y, x+k));
                    }
                }
            }
        }
#endif
        for (; x < W; ++x) {
            float r = rgb->r(y, x);
            float g = rgb->g(y, x);
            float b = rgb->b(y, x);

            if ((r == 0.0f || g == 0.0f) && rtengine::min(r, g, b) >= 0.f) {
                float h, s, v;
                Color::rgb2hsv(r, g, b, h, s, v);
                s *= 0.99f;
                Color::hsv2rgb(h, s, v, rgb->r(y, x), rgb->g(y, x), rgb->b(y, x));
            }
        }
    }
}


void dcpProfile(Imagefloat *img, DCPProfile *dcp, const DCPProfile::ApplyState *as, bool multithread)
{
    if (dcp && as) {
        img->setMode(Imagefloat::Mode::RGB, multithread);
        
        const int H = img->getHeight();
        const int W = img->getWidth();
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            float *r = img->r(y);
            float *g = img->g(y);
            float *b = img->b(y);
            dcp->step2ApplyTile(r, g, b, W, 1, 1, *as);
        }
    }
}

} // namespace


//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

double ImProcFunctions::getAutoDistor  (const Glib::ustring &fname, int thumb_size)
{
    if (fname != "") {
        int w_raw = -1, h_raw = thumb_size;
        int w_thumb = -1, h_thumb = thumb_size;

        eSensorType sensorType = rtengine::ST_NONE;
        Thumbnail* thumb = rtengine::Thumbnail::loadQuickFromRaw (fname, sensorType, w_thumb, h_thumb, 1, FALSE);

        if (!thumb) {
            return 0.0;
        }

        Thumbnail* raw =   rtengine::Thumbnail::loadFromRaw(fname, sensorType, w_raw, h_raw, 1, 1.0, FALSE);

        if (!raw) {
            delete thumb;
            return 0.0;
        }

        if (h_thumb != h_raw) {
            delete thumb;
            delete raw;
            return 0.0;
        }

        int width;

        if (w_thumb > w_raw) {
            width = w_raw;
        } else {
            width = w_thumb;
        }

        unsigned char* thumbGray;
        unsigned char* rawGray;
        thumbGray = thumb->getGrayscaleHistEQ (width);
        rawGray = raw->getGrayscaleHistEQ (width);

        if (!thumbGray || !rawGray) {
            if (thumbGray) {
                delete thumbGray;
            }

            if (rawGray) {
                delete rawGray;
            }

            delete thumb;
            delete raw;
            return 0.0;
        }

        double dist_amount;
        int dist_result = calcDistortion (thumbGray, rawGray, width, h_thumb, 1, dist_amount);

        if (dist_result == -1) { // not enough features found, try increasing max. number of features by factor 4
            calcDistortion (thumbGray, rawGray, width, h_thumb, 4, dist_amount);
        }

        delete[] thumbGray;
        delete[] rawGray;
        delete thumb;
        delete raw;
        return dist_amount;
    } else {
        return 0.0;
    }
}

void ImProcFunctions::rgb2lab (Imagefloat &src, LabImage &dst, const Glib::ustring &workingSpace)
{
    src.assignColorSpace(workingSpace);
    src.toLab(dst, true);
}

void ImProcFunctions::lab2rgb (const LabImage &src, Imagefloat &dst, const Glib::ustring &workingSpace)
{
    dst.assignColorSpace(workingSpace);
    dst.assignMode(Imagefloat::Mode::RGB);
    
    TMatrix wiprof = ICCStore::getInstance()->workingSpaceInverseMatrix ( workingSpace );
    const float wip[3][3] = {
        {static_cast<float> (wiprof[0][0]), static_cast<float> (wiprof[0][1]), static_cast<float> (wiprof[0][2])},
        {static_cast<float> (wiprof[1][0]), static_cast<float> (wiprof[1][1]), static_cast<float> (wiprof[1][2])},
        {static_cast<float> (wiprof[2][0]), static_cast<float> (wiprof[2][1]), static_cast<float> (wiprof[2][2])}
    };

    const int W = dst.getWidth();
    const int H = dst.getHeight();
#ifdef __SSE2__
    vfloat wipv[3][3];

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            wipv[i][j] = F2V (wiprof[i][j]);
        }
    }

#endif

#ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic,16)
#endif

    for (int i = 0; i < H; i++) {
        int j = 0;
#ifdef __SSE2__

        for (; j < W - 3; j += 4) {
            vfloat X, Y, Z;
            vfloat R, G, B;
            Color::Lab2XYZ (LVFU (src.L[i][j]), LVFU (src.a[i][j]), LVFU (src.b[i][j]), X, Y, Z);
            Color::xyz2rgb (X, Y, Z, R, G, B, wipv);
            STVFU (dst.r (i, j), R);
            STVFU (dst.g (i, j), G);
            STVFU (dst.b (i, j), B);
        }

#endif

        for (; j < W; j++) {
            float X, Y, Z;
            Color::Lab2XYZ (src.L[i][j], src.a[i][j], src.b[i][j], X, Y, Z);
            Color::xyz2rgb (X, Y, Z, dst.r (i, j), dst.g (i, j), dst.b (i, j), wip);
        }
    }
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%


void ImProcFunctions::setViewport(int ox, int oy, int fw, int fh)
{
    offset_x = ox;
    offset_y = oy;
    full_width = fw;
    full_height = fh;
}


void ImProcFunctions::setOutputHistograms(LUTu *histToneCurve, LUTu *histCCurve, LUTu *histLCurve)
{
    this->histToneCurve = histToneCurve;
    this->histCCurve = histCCurve;
    this->histLCurve = histLCurve;
}


void ImProcFunctions::setShowSharpeningMask(bool yes)
{
    show_sharpening_mask = yes;
}


namespace {

constexpr int NUM_PIPELINE_STEPS = 23;

} // namespace

void ImProcFunctions::setProgressListener(ProgressListener *pl, int num_previews)
{
    plistener = pl;
    progress_step = 0;
    progress_end = NUM_PIPELINE_STEPS * std::max(num_previews, 1);
    if (plistener) {
        plistener->setProgressStr("PROGRESSBAR_PROCESSING");
        plistener->setProgress(0);
    }
}


template <class Ret, class Method>
Ret ImProcFunctions::apply(Method op, Imagefloat *img)
{
    if (plistener) {
        float percent = float(++progress_step) / float(progress_end);
        plistener->setProgress(percent);
    }
    return (this->*op)(img);
}


bool ImProcFunctions::process(Pipeline pipeline, Stage stage, Imagefloat *img)
{
    bool stop = false;
    cur_pipeline = pipeline;

#define STEP_(op) apply<void>(&ImProcFunctions::op, img)
#define STEP_s_(op) apply<bool>(&ImProcFunctions::op, img)
        
    switch (stage) {
    case Stage::STAGE_0:
        STEP_(dehaze);
        STEP_(dynamicRangeCompression);
        break;
    case Stage::STAGE_1:
        STEP_(channelMixer);
        STEP_(exposure);
        STEP_(hslEqualizer);
        stop = STEP_s_(toneEqualizer);
        if (params->icm.workingProfile == "ProPhoto") {
            proPhotoBlue(img, multiThread);
        }
        break;
    case Stage::STAGE_2:
        if (params->icm.dcp_look_early) {
            dcpProfile(img, dcpProf, dcpApplyState, multiThread);
        }
        linked_mask_mgr_.init(*params);
        if (pipeline == Pipeline::OUTPUT || pipeline == Pipeline::PREVIEW) {
            stop = STEP_s_(sharpening);
            if (!stop) {
                STEP_(impulsedenoise);
                STEP_(defringe);
            }
        }
        stop = stop || STEP_s_(colorCorrection);
        stop = stop || STEP_s_(guidedSmoothing);
        break;
    case Stage::STAGE_3:
        STEP_(creativeGradients);
        stop = stop || STEP_s_(textureBoost);
        if (!stop) { 
            STEP_(filmGrain);
            STEP_(logEncoding);
            STEP_(saturationVibrance);
            if (!params->icm.dcp_look_early) {
                dcpProfile(img, dcpProf, dcpApplyState, multiThread);
            }
            if (!params->filmSimulation.after_tone_curve) {
                STEP_(filmSimulation);
            }
            STEP_(toneCurve);
            if (params->filmSimulation.after_tone_curve) {
                STEP_(filmSimulation);
            }
            STEP_(rgbCurves);
            STEP_(labAdjustments);
            STEP_(softLight);
        }
        stop = stop || STEP_s_(localContrast);
        if (!stop) {
            STEP_(blackAndWhite);
        }
        if (pipeline == Pipeline::PREVIEW && params->prsharpening.enabled) {
            double s = scale;
            int fw = full_width * s, fh = full_height * s;
            int imw, imh;
            double s2 = resizeScale(params, fw, fh, imw, imh);
            scale = std::max(s * s2, 1.0);
            STEP_s_(prsharpening);
            scale = s;
        }
        break;
    }
    return stop;
}


int ImProcFunctions::setDeltaEData(EditUniqueID id, double x, double y)
{
    deltaE.ok = false;
    deltaE.x = x;
    deltaE.y = y;
    deltaE.L = 0;
    deltaE.C = 0;
    deltaE.H = 0;

    switch (id) {
    case EUID_Masks_DE1:
        return LUMINANCECURVE | M_LUMACURVE;
    case EUID_Masks_DE2:
        return DISPLAY;
    case EUID_Masks_DE3:
        return LUMINANCECURVE | M_LUMACURVE;
    case EUID_Masks_DE4:
        return DISPLAY;
    default:
        return 0;
    }
}


} // namespace rtengine
