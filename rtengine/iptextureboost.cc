/* -*- C++ -*-
 *
 *  This file is part of RawTherapee.
 *
 *  Copyright 2018 Alberto Griggio <alberto.griggio@gmail.com>
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
#include "masks.h"
#include "array2D.h"
#include "guidedfilter.h"
#include "rescale.h"
#include "gauss.h"
#include "rt_algo.h"

namespace rtengine {

namespace {

void texture_boost(array2D<float> &Y, const rtengine::procparams::TextureBoostParams::Region &pp, double scale, bool multithread, bool high_detail)
{
    float full_radius = pp.detailThreshold * 3.5f;
    float fradius = full_radius / scale;
    int radius = std::max(int(fradius + 0.5f), 1);
    float delta = radius / fradius;

    float epsilon = 0.001f;
    float s = pp.strength >= 0 ? pow_F(pp.strength / 2.f, 0.3f) * 2.f : pp.strength;
    float strength = s >= 0 ? 1.f + s : 1.f / (1.f - s);
    float strength2 = s >= 0 ? 1.f + s / 4.f: 1.f / (1.f - s / 2.f);

    //bool isguided = fradius >= 1.f;
    bool isguided = full_radius >= 1.f;

    int W = Y.width();
    int H = Y.height();

    array2D<float> tmpY(ARRAY2D_ALIGNED);
    array2D<float> *src = &Y;
    if (fradius > 1.f && delta > 1.01f) {
        W = int(W * delta + 0.5f);
        H = int(H * delta + 0.5f);
        tmpY(W, H);
        rescaleBilinear(Y, tmpY, multithread);
        src = &tmpY;
    }
    
    array2D<float> mid(W, H, ARRAY2D_ALIGNED);
    array2D<float> base(W, H, ARRAY2D_ALIGNED);

#ifdef __SSE2__
    const vfloat v65535 = F2V(65535.f);
    const vfloat vstrength = F2V(strength);
    const vfloat vstrength2 = F2V(strength2);
#endif

    float minval = RT_INFINITY;
    constexpr float lo = 1e-5f;
    constexpr float hi = 32.f;
#ifdef __SSE2__
    const vfloat vlo = F2V(lo);
    const vfloat vhi = F2V(hi);
#endif

#ifdef _OPENMP
#   pragma omp parallel for reduction(min:minval) if (multithread)
#endif
    for (int y = 0; y < H; ++y) {
        int x = 0;
#ifdef __SSE2__
        for (; x < W-3; x += 4) {
            vfloat v = LVFU((*src)[y][x]) / v65535;
            STVFU((*src)[y][x], v);
            STVFU(mid[y][x], vmaxf(vminf(v, vhi), vlo));
            minval = min(minval, v[0], v[1], v[2], v[3]);
        }
#endif
        for (; x < W; ++x) {
            float v = (*src)[y][x] / 65535.f;
            (*src)[y][x] = v;
            mid[y][x] = LIM(v, lo, hi);
            minval = min(minval, v);
        }
    }

#ifdef __SSE2__
    vfloat vminval = F2V(minval);
#endif

    std::unique_ptr<Convolution> gaussian_blur;
    if (!isguided && high_detail) {
        array2D<float> kernel;
        build_gaussian_kernel(fradius, kernel);
        gaussian_blur.reset(new Convolution(kernel, W, H, multithread));
    }

    for (int i = 0; i < pp.iterations; ++i) {
        float blend = 1.f / std::pow(2.f, i);
#ifdef __SSE2__
        vfloat vblend = F2V(blend);
#endif
        if (isguided) {
            guidedFilter(mid, mid, mid, radius, epsilon, multithread);
        } else {
            if (high_detail) {
                (*gaussian_blur)(mid, mid);
            } else {
#ifdef _OPENMP
#               pragma omp parallel if (multithread)
#endif
                gaussianBlur(mid, mid, W, H, fradius);
            }
        }
        guidedFilter(mid, mid, base, radius * 4, epsilon / 10.f, multithread);

#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            int x = 0;
#ifdef __SSE2__
            for (; x < W - 3; x += 4) {
                vfloat vy = LVFU((*src)[y][x]);
                vfloat vm = LVFU(mid[y][x]);
                vfloat vb = LVFU(base[y][x]);
                vfloat d = (vy - vm) * vstrength;
                vfloat d2 = (vm - vb) * vstrength2;
                STVFU((*src)[y][x], vintpf(vblend, vmaxf(vb + d + d2, vminval), vy));
            }
#endif
            for (; x < W; ++x) {
                float v = (*src)[y][x];
                float d = v - mid[y][x];
                d *= strength;
                float d2 = mid[y][x] - base[y][x];
                d2 *= strength2;
                (*src)[y][x] = intp(blend, std::max(base[y][x] + d + d2, minval), v);
            }
        }
    }

#ifdef _OPENMP
#   pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < H; ++y) {
        int x = 0;
#ifdef __SSE2__
        for (; x < W-3; x += 4) {
            vfloat v = LVFU((*src)[y][x]);
            STVFU((*src)[y][x], v * v65535);
        }
#endif
        for (; x < W; ++x) {
            (*src)[y][x] = (*src)[y][x] * 65535.f;
        }
    }

    if (src != &Y) {
        rescaleBilinear(*src, Y, multithread);
    }
}

} // namespace


bool ImProcFunctions::textureBoost(Imagefloat *rgb)
{
    PlanarWhateverData<float> *editWhatever = nullptr;
    EditUniqueID eid = pipetteBuffer ? pipetteBuffer->getEditID() : EUID_None;

    if ((eid == EUID_LabMasks_H4 || eid == EUID_LabMasks_C4 || eid == EUID_LabMasks_L4) && pipetteBuffer->getDataProvider()->getCurrSubscriber()->getPipetteBufferType() == BT_SINGLEPLANE_FLOAT) {
        editWhatever = pipetteBuffer->getSinglePlaneBuffer();
    }
    
    if (eid == EUID_LabMasks_DE4) {
        if (getDeltaEColor(rgb, deltaE.x, deltaE.y, offset_x, offset_y, full_width, full_height, scale, deltaE.L, deltaE.C, deltaE.H)) {
            deltaE.ok = true;
        }
    }
    
    if (params->textureBoost.enabled) {
        if (editWhatever) {
            MasksEditID id = static_cast<MasksEditID>(int(eid) - EUID_LabMasks_H4);
            fillPipetteMasks(rgb, editWhatever, id, multiThread);
        }
        
        int n = params->textureBoost.regions.size();
        int show_mask_idx = params->textureBoost.showMask;
        if (show_mask_idx >= n || (cur_pipeline != Pipeline::PREVIEW /*&& cur_pipeline != Pipeline::OUTPUT*/)) {
            show_mask_idx = -1;
        }
        std::vector<array2D<float>> mask(n);
        if (!generateMasks(rgb, "textureboost", raster_mask_mgr_, params->textureBoost.labmasks, offset_x, offset_y, full_width, full_height, scale, multiThread, show_mask_idx, &mask, nullptr, cur_pipeline == Pipeline::NAVIGATOR ? plistener : nullptr)) {
            return true; // show mask is active, nothing more to do
        }

        const int W = rgb->getWidth();
        const int H = rgb->getHeight();

        rgb->setMode(Imagefloat::Mode::YUV, multiThread);

        array2D<float> Y(W, H, rgb->g.ptrs, ARRAY2D_ALIGNED);

        for (int i = 0; i < n; ++i) {
            if (!params->textureBoost.labmasks[i].enabled) {
                continue;
            }
            
            auto &r = params->textureBoost.regions[i];
            if (r.strength != 0) {
                texture_boost(Y, r, scale, multiThread, scale == 1 || cur_pipeline == Pipeline::OUTPUT);
                const auto &blend = mask[i];

#ifdef _OPENMP
#               pragma omp parallel for if (multiThread)
#endif
                for (int y = 0; y < H; ++y) {
                    for (int x = 0; x < W; ++x) {
                        float &YY = rgb->g(y, x);
                        YY = intp(blend[y][x], Y[y][x], YY);
                        Y[y][x] = YY;
                    }
                }
            }
        }
    } else if (editWhatever) {
        editWhatever->fill(0.f);
    }

    return false;
}

} // namespace rtengine
