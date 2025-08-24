/* -*- C++ -*-
 *
 *  This file is part of RawTherapee.
 *
 *  Ported from G'MIC by Alberto Griggio <alberto.griggio@gmail.com>
 *
 *  The original implementation in G'MIC was authored by Arto Huotari, and was
 *  released under the CeCILL free software license (see
 *  http://www.cecill.info/licences/Licence_CeCILL_V2-en.html)
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

#ifdef _OPENMP
#include <omp.h>
#endif

#include "improcfun.h"
#include "gauss.h"
#include "array2D.h"
#include "cplx_wavelet_dec.h"
#include "curves.h"
#include "masks.h"


namespace rtengine {

namespace {

class WavOpacityCurveWL {
private:
    LUTf lutOpacityCurveWL;  // 0xffff range
    void Set(const Curve &pCurve);

public:
    virtual ~WavOpacityCurveWL() {};
    WavOpacityCurveWL();

    void Reset();
    void Set(const Curve *pCurve);
    void Set(const std::vector<double> &curvePoints);
    float operator[](float index) const
    {
        return lutOpacityCurveWL ? lutOpacityCurveWL[index] : 0.f;
    }

    operator bool (void) const
    {
        return lutOpacityCurveWL;
    }
};

WavOpacityCurveWL::WavOpacityCurveWL() {}

void WavOpacityCurveWL::Reset()
{
    lutOpacityCurveWL.reset();
}

void WavOpacityCurveWL::Set(const Curve &pCurve)
{
    if (pCurve.isIdentity()) {
        lutOpacityCurveWL.reset(); // raise this value if the quality suffers from this number of samples
        return;
    }

    lutOpacityCurveWL(501); // raise this value if the quality suffers from this number of samples

    for (int i = 0; i < 501; i++) {
        lutOpacityCurveWL[i] = pCurve.getVal(double(i) / 500.);
    }
}

void WavOpacityCurveWL::Set(const std::vector<double> &curvePoints)
{
    if (!curvePoints.empty() && curvePoints[0] > FCT_Linear && curvePoints[0] < FCT_Unchanged) {
        FlatCurve tcurve(curvePoints, false, CURVES_MIN_POLY_POINTS / 2);
        tcurve.setIdentityValue(0.);
        Set(tcurve);
    } else {
        Reset();
    }
}


void eval_avg(float *RESTRICT DataList, int datalen, float &averagePlus, float &averageNeg, float &max, float &min, bool multiThread)
{
    //find absolute mean
    int countP = 0, countN = 0;
    double averaP = 0.0, averaN = 0.0; // use double precision for large summations

    float thres = 5.f;//different fom zero to take into account only data large enough
    max = 0.f;
    min = 0.f;

#ifdef _OPENMP
#    pragma omp parallel if (multiThread)
#endif
    {
        float lmax = 0.f, lmin = 0.f;
#ifdef _OPENMP
        #pragma omp for reduction(+:averaP,averaN,countP,countN) nowait
#endif

        for(int i = 0; i < datalen; i++) {
            if(DataList[i] >= thres) {
                averaP += DataList[i];

                if(DataList[i] > lmax) {
                    lmax = DataList[i];
                }

                countP++;
            } else if(DataList[i] < -thres) {
                averaN += DataList[i];

                if(DataList[i] < lmin) {
                    lmin = DataList[i];
                }

                countN++;
            }
        }

#ifdef _OPENMP
        #pragma omp critical
#endif
        {
            max = max > lmax ? max : lmax;
            min = min < lmin ? min : lmin;
        }
    }

    if(countP > 0) {
        averagePlus = averaP / countP;
    } else {
        averagePlus = 0;
    }

    if(countN > 0) {
        averageNeg = averaN / countN;
    } else {
        averageNeg = 0;
    }
}


void eval_sigma(float *RESTRICT DataList, int datalen, float averagePlus, float averageNeg, float &sigmaPlus, float &sigmaNeg, bool multiThread)
{
    int countP = 0, countN = 0;
    double variP = 0.0, variN = 0.0; // use double precision for large summations
    float thres = 5.f;//different fom zero to take into account only data large enough

#ifdef _OPENMP
#    pragma omp parallel for reduction(+:variP,variN,countP,countN) if (multiThread)
#endif
    for(int i = 0; i < datalen; i++) {
        if(DataList[i] >= thres) {
            variP += SQR(DataList[i] - averagePlus);
            countP++;
        } else if(DataList[i] <= -thres) {
            variN += SQR(DataList[i] - averageNeg);
            countN++;
        }
    }

    if(countP > 0) {
        sigmaPlus = sqrt(variP / countP);
    } else {
        sigmaPlus = 0;
    }

    if(countN > 0) {
        sigmaNeg = sqrt(variN / countN);
    } else {
        sigmaNeg = 0;
    }
}


void eval_level(float **wl, int level, int W_L, int H_L, float *mean, float *meanN, float *sigma, float *sigmaN, float *MaxP, float *MaxN, bool multiThread)
{
    float avLP[4], avLN[4];
    float maxL[4], minL[4];
    float sigP[4], sigN[4];
    float AvL, AvN, SL, SN, maxLP, maxLN;

    for (int dir = 1; dir < 4; dir++) {
        eval_avg(wl[dir], W_L * H_L,  avLP[dir], avLN[dir], maxL[dir], minL[dir], multiThread);
        eval_sigma(wl[dir], W_L * H_L, avLP[dir], avLN[dir], sigP[dir], sigN[dir], multiThread);
    }

    AvL = 0.f;
    AvN = 0.f;
    SL = 0.f;
    SN = 0.f;
    maxLP = 0.f;
    maxLN = 0.f;

    for (int dir = 1; dir < 4; dir++) {
        AvL += avLP[dir];
        AvN += avLN[dir];
        SL += sigP[dir];
        SN += sigN[dir];
        maxLP += maxL[dir];
        maxLN += minL[dir];
    }

    AvL /= 3;
    AvN /= 3;
    SL /= 3;
    SN /= 3;
    maxLP /= 3;
    maxLN /= 3;

    mean[level] = AvL;
    meanN[level] = AvN;
    sigma[level] = SL;
    sigmaN[level] = SN;
    MaxP[level] = maxLP;
    MaxN[level] = maxLN;
}


void evaluate_params(wavelet_decomposition &wd, float *mean, float *meanN, float *sigma, float *sigmaN, float *MaxP, float *MaxN, bool multiThread)
{
    int maxlvl = wd.maxlevel();

    for (int lvl = 0; lvl < maxlvl; lvl++) {
        int Wlvl_L = wd.level_W(lvl);
        int Hlvl_L = wd.level_H(lvl);

        float **wl = wd.level_coeffs(lvl);

        eval_level(wl, lvl, Wlvl_L, Hlvl_L, mean, meanN, sigma, sigmaN, MaxP, MaxN, multiThread);
    }
}


void local_contrast_wavelets(array2D<float> &Y, const LocalContrastParams::Region &params, double scale, bool multiThread)
{
    const int W = Y.width();
    const int H = Y.height();

    int wavelet_level = 7;
    int dim = min(W, H);
    while ((1 << wavelet_level) >= dim && wavelet_level > 1) {
        --wavelet_level;
    }
    int skip = scale;
    wavelet_decomposition wd(static_cast<float *>(Y), W, H, wavelet_level, 1, skip);

    // if (wd.memoryAllocationFailed) {
    //     return;
    // }

    const float contrast = params.contrast;
    int maxlvl = wd.maxlevel();

    if (contrast != 0) {
        int W_L = wd.level_W(0);
        int H_L = wd.level_H(0);
        float *wl0 = wd.coeff0;

        float maxh = 2.5f; //amplification contrast above mean
        float maxl = 2.5f; //reduction contrast under mean
        float multL = contrast * (maxl - 1.f) / 100.f + 1.f;
        float multH = contrast * (maxh - 1.f) / 100.f + 1.f;
        double avedbl = 0.0; // use double precision for large summations
        float max0 = 0.f;
        float min0 = FLT_MAX;

#ifdef _OPENMP
#       pragma omp parallel for reduction(+:avedbl) if (multiThread)
#endif
        for (int i = 0; i < W_L * H_L; i++) {
            avedbl += wl0[i];
        }

#ifdef _OPENMP
#       pragma omp parallel if (multiThread)
#endif
        {
            float lminL = FLT_MAX;
            float lmaxL = 0.f;

#ifdef _OPENMP
#           pragma omp for
#endif
            for (int i = 0; i < W_L * H_L; i++) {
                lminL = min(lminL, wl0[i]);
                lmaxL = max(lmaxL, wl0[i]);
            }

#ifdef _OPENMP
#           pragma omp critical
#endif
            {
                min0 = min(min0, lminL);
                max0 = max(max0, lmaxL);
            }
        }

        max0 /= 327.68f;
        min0 /= 327.68f;
        float ave = avedbl / double(W_L * H_L);
        float av = ave / 327.68f;
        float ah = (multH - 1.f) / (av - max0);
        float bh = 1.f - max0 * ah;
        float al = (multL - 1.f) / (av - min0);
        float bl = 1.f - min0 * al;

        if (max0 > 0.0) { 
#ifdef _OPENMP
#           pragma omp parallel for if (multiThread)
#endif
            for (int i = 0; i < W_L * H_L; i++) {
                if (wl0[i] < 32768.f) {
                    float prov;

                    if (wl0[i] > ave) {
                        float kh = ah * (wl0[i] / 327.68f) + bh;
                        prov = wl0[i];
                        wl0[i] = ave + kh * (wl0[i] - ave);
                    } else {
                        float kl = al * (wl0[i] / 327.68f) + bl;
                        prov = wl0[i];
                        wl0[i] = ave - kl * (ave - wl0[i]);
                    }

                    float diflc = wl0[i] - prov;
                    wl0[i] =  prov + diflc;
                }
            }
        }
    }

    float mean[10];
    float meanN[10];
    float sigma[10];
    float sigmaN[10];
    float MaxP[10];
    float MaxN[10];
    evaluate_params(wd, mean, meanN, sigma, sigmaN, MaxP, MaxN, multiThread);

    WavOpacityCurveWL curve;
    if (params.curve.empty() || params.curve[0] == FCT_Linear) {
        LocalContrastParams::Region dflt;
        curve.Set(dflt.curve);
    } else {
        curve.Set(params.curve);
    }

    for (int dir = 1; dir < 4; dir++) {
        for (int level = 0; level < maxlvl; ++level) {
            int W_L = wd.level_W(level);
            int H_L = wd.level_H(level);
            float **wl = wd.level_coeffs(level);

            if (MaxP[level] > 0.f && mean[level] != 0.f && sigma[level] != 0.f) {
                float insigma = 0.666f; //SD
                float logmax = log(MaxP[level]); //log Max
                float rapX = (mean[level] + sigma[level]) / MaxP[level]; //rapport between sD / max
                float inx = log(insigma);
                float iny = log(rapX);
                float rap = inx / iny; //koef
                float asig = 0.166f / sigma[level];
                float bsig = 0.5f - asig * mean[level];
                float amean = 0.5f / mean[level];

#ifdef _OPENMP
#               pragma omp parallel for if (multiThread)
    //schedule(dynamic, W_L * 16) if (multiThread)
#endif
                for (int i = 0; i < W_L * H_L; i++) {
                    float absciss;
                    float &val = wl[dir][i];

                    if (std::isnan(val)) { // ALB -- TODO: this can happen in
                                           // wavelet_decomposition, find out
                                           // why
                        continue;
                    }

                    if (fabsf(val) >= (mean[level] + sigma[level])) { //for max
                        float valcour = xlogf(fabsf(val));
                        float valc = valcour - logmax;
                        float vald = valc * rap;
                        absciss = xexpf(vald);
                    } else if (fabsf(val) >= mean[level]) {
                        absciss = asig * fabsf(val) + bsig;
                    } else {
                        absciss = amean * fabsf(val);
                    }

                    float kc = curve[absciss * 500.f] - 0.5f;
                    float reduceeffect = kc <= 0.f ? 1.f : 1.5f;

                    float kinterm = 1.f + reduceeffect * kc;
                    kinterm = kinterm <= 0.f ? 0.01f : kinterm;

                    val *=  kinterm;
                }
            }
        }
    }

    wd.reconstruct(static_cast<float *>(Y), 1.f);
}

} // namespace


bool ImProcFunctions::localContrast(Imagefloat *rgb)
{
    PlanarWhateverData<float> *editWhatever = nullptr;
    EditUniqueID eid = pipetteBuffer ? pipetteBuffer->getEditID() : EUID_None;

    if ((eid == EUID_LabMasks_H2 || eid == EUID_LabMasks_C2 || eid == EUID_LabMasks_L2) && pipetteBuffer->getDataProvider()->getCurrSubscriber()->getPipetteBufferType() == BT_SINGLEPLANE_FLOAT) {
        editWhatever = pipetteBuffer->getSinglePlaneBuffer();
    }

    if (eid == EUID_LabMasks_DE2) {
        if (getDeltaEColor(rgb, deltaE.x, deltaE.y, offset_x, offset_y, full_width, full_height, scale, deltaE.L, deltaE.C, deltaE.H)) {
            deltaE.ok = true;
        }
    }
    
    if (params->localContrast.enabled) {
        rgb->setMode(Imagefloat::Mode::LAB, multiThread);
        
        if (editWhatever) {
            MasksEditID id = static_cast<MasksEditID>(int(eid) - EUID_LabMasks_H2);
            fillPipetteMasks(rgb, editWhatever, id, multiThread);
        }
        
        int n = params->localContrast.regions.size();
        int show_mask_idx = params->localContrast.showMask;
        if (show_mask_idx >= n || (cur_pipeline != Pipeline::PREVIEW /*&& cur_pipeline != Pipeline::OUTPUT*/)) {
            show_mask_idx = -1;
        }
        std::vector<array2D<float>> mask(n);
        if (!generateMasks(rgb, "localcontrast", raster_mask_mgr_, params->localContrast.labmasks, offset_x, offset_y, full_width, full_height, scale, multiThread, show_mask_idx, &mask, nullptr, cur_pipeline == Pipeline::NAVIGATOR ? plistener : nullptr)) {
            return true; // show mask is active, nothing more to do
        }

        const int W = rgb->getWidth();
        const int H = rgb->getHeight();
        
        array2D<float> L(W, H, rgb->g.ptrs);

        for (int i = 0; i < n; ++i) {
            if (!params->localContrast.labmasks[i].enabled) {
                continue;
            }
            
            auto &r = params->localContrast.regions[i];
            local_contrast_wavelets(L, r, scale, multiThread);
            const auto &blend = mask[i];
#ifdef _OPENMP
#           pragma omp parallel for if (multiThread)
#endif
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    float l = rgb->g(y, x);
                    rgb->g(y, x) = intp(blend[y][x], L[y][x], l);
                    L[y][x] = rgb->g(y, x);
                }
            }
        }
    } else if (editWhatever) {
        editWhatever->fill(0.f);
    }

    return false;
}

} // namespace rtengine
