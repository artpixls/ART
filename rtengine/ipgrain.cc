/* -*- C++ -*-
 *
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2025 Alberto Griggio <alberto.griggio@gmail.com>
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
#include "rt_math.h"


namespace rtengine {

namespace {

class ProcParamsOverride {
public:
    ProcParamsOverride(const procparams::ProcParams *&pp, ImProcFunctions::Pipeline pipeline, double scale):
        params_(),
        prev_(pp),
        torestore_(pp)
    {
        auto strength = pp->grain.strength;
        auto iso = pp->grain.iso;
        auto color = pp->grain.color;
        
        constexpr int iso_min = 20;
        constexpr int iso_max = 6400;
        
        params_.smoothing.enabled = true;

        int coarseness = LIM01(float(iso - iso_min + 1) / float(iso_max - iso_min)) * 100.f + 0.5f;

        const int nlevels = pipeline == ImProcFunctions::Pipeline::OUTPUT ? 3 : int(std::ceil(3 / scale));
        if (color) {
            params_.smoothing.regions.emplace_back();
            params_.smoothing.masks.emplace_back();
            auto &r = params_.smoothing.regions.back();
            r.mode = procparams::SmoothingParams::Region::Mode::NOISE;
            r.channel = procparams::SmoothingParams::Region::Channel::CHROMINANCE;
            r.noise_strength = strength / 2;
            r.noise_coarseness = coarseness / 2;
        }
        for (int i = 0; i < nlevels; ++i) {
            params_.smoothing.regions.emplace_back();
            params_.smoothing.masks.emplace_back();
            auto &r = params_.smoothing.regions.back();
            r.mode = procparams::SmoothingParams::Region::Mode::NOISE;
            r.channel = procparams::SmoothingParams::Region::Channel::LUMINANCE;
            r.noise_strength = strength / (nlevels-i);
            r.noise_coarseness = coarseness / (i+1);
        }
    }

    ~ProcParamsOverride()
    {
        torestore_ = prev_;
    }

    procparams::ProcParams *get_params() { return &params_; }

private:
    rtengine::ProcParams params_;
    const rtengine::ProcParams *prev_;
    const rtengine::ProcParams *&torestore_;
};

} // namespace


void ImProcFunctions::filmGrain(Imagefloat *rgb)
{
    if (!params->grain.enabled) {
        return;
    }
    
    ProcParamsOverride pp(params, cur_pipeline, scale);
    params = pp.get_params();

    guidedSmoothing(rgb);
}

} // namespace rtengine
