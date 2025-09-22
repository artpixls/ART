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

#include "masks.h"
#include "guidedfilter.h"
#include "sleef.h"
#include "coord.h"
#include "gauss.h"
#include "color.h"
#include "curves.h"
#include "iccstore.h"
#include "rt_algo.h"
#include "rt_math.h"
#include "opthelper.h"
#include "rescale.h"
#include "halffloat.h"
#include "stdimagesource.h"
#include "../rtgui/multilangmgr.h"

namespace rtengine {

using procparams::AreaMask;
using procparams::Mask;
using procparams::DrawnMask;

namespace {

bool generate_area_mask(int ox, int oy, int width, int height, const array2D<float> &guide, const AreaMask &areaMask, float scale, bool multithread, array2D<float> &global_mask, ProgressListener *plistener)
{
    if (!areaMask.enabled || areaMask.shapes.empty() || (areaMask.isTrivial() && areaMask.blur <= 0.f)) {
        return false;
    }

    float w2 = float(width) / 2.f;
    float h2 = float(height) / 2.f;

    Coord origin(ox, oy);

    const auto inside =
        [&](int x, int y) -> bool
        {
            return (x >= 0 && x < global_mask.width() &&
                    y >= 0 && y < global_mask.height());
        };

    const int dir[] = { 1, 1, 1, -1, -1, 1, -1, -1 };

    constexpr float bgcolor = 0.f;           // background layer, no effect applied
    constexpr float fgcolor = 1.f - bgcolor; // foreground layer, full effect applied

    // first fill with background
    global_mask(guide.width(), guide.height());
    global_mask.fill(bgcolor);

    float min_feather = RT_INFINITY;
    float global_min_feather = RT_INFINITY;

    int radius = 0;

    // will either hold a pointer to the global mask
    // or allocate data for rendering each shape
    // or used for intersecting shapes in single value mode
    array2D<float> shape_mask;
    DiagonalCurve contrast_curve(areaMask.contrast);

    for (const auto &area_ : areaMask.shapes) {
        if (!shape_mask.width()) {
            // allocated once, when needed
            shape_mask(global_mask.width(), global_mask.height());
        }
        if (area_->getType() != AreaMask::Shape::GRADIENT) {
            // GRADIENT will update the whole image, no need to initialize it here
            shape_mask.fill(area_->mode == AreaMask::Shape::SUBTRACT ? fgcolor : bgcolor);
        }
        float color;

        switch (area_->mode) {
        case AreaMask::Shape::ADD:
        case AreaMask::Shape::INTERSECT:
            color = fgcolor;
            break;
        case AreaMask::Shape::SUBTRACT:
        default:
            color = bgcolor;
            break;
        }

        switch (area_->getType()) {
        case AreaMask::Shape::RECTANGLE:
        {
            auto area = static_cast<AreaMask::Rectangle*>(area_.get());

            Coord center(w2 + area->x / 100.0 * w2, h2 + area->y / 100.0 * h2);
            float area_w = area->width / 100.0 * width;
            float area_h = area->height / 100.0 * height;

            float a_min = area_w / 2;
            float b_min = area_h / 2;
            float r = b_min / a_min;
            float a_max = std::sqrt(2) * a_min;
            float a = a_max - area->roundness / 100.0 * (a_max - a_min);

            min_feather = std::min(a_min, b_min);
            global_min_feather = rtengine::min<double>(global_min_feather, min_feather);

            const auto get =
                [&](int x, int y) -> Coord
                {
                    PolarCoord p(Coord(x, y));
                    double r, a;
                    p.get(r, a);
                    p.set(r, a - area->angle);
                    CoordD ret(p);
                    ret += center;
                    ret -= origin;
                    //return ret;
                    double rx, ry;
                    ret.get(rx, ry);
                    return Coord(int(rx + 0.5), int(ry + 0.5));
                };

            // draw the (bounded) ellipse
            for (int x = 0, n = int(a_min); x < n; ++x) {
                int yy = r * std::sqrt(a*a - float(x*x));
                for (int y = 0, m = std::min(yy, int(b_min)); y < m; ++y) {
                    for (int d = 0; d < 4; ++d) {
                        int dx = dir[2*d], dy = dir[2*d+1];
                        Coord point = get(dx * x, dy * y);
                        for (int i = -1; i < 2; ++i) {
                            for (int j = -1; j < 2; ++j) {
                                if (inside(point.x+i, point.y+j)) {
                                    shape_mask[point.y+j][point.x+i] = color;
                                }
                            }
                        }
                    }
                }
            }

            radius = std::max(int(area->feather / 100.0 * min_feather), 1);

            break;
        }
        case AreaMask::Shape::POLYGON:
        {
            auto area = static_cast<AreaMask::Polygon*>(area_.get());
            if (area->knots.size() < 3) {
                // Not enough knots to create an area
                break;
            }
            std::vector<AreaMask::Polygon::Knot> imgSpacePoly(area->knots.size());
            for (size_t i = 0; i < area->knots.size() ; ++i) {
                imgSpacePoly.at(i).x = AreaMask::Shape::toImgSpace(area->knots.at(i).x, width) - origin.x;
                imgSpacePoly.at(i).y = AreaMask::Shape::toImgSpace(area->knots.at(i).y, height) - origin.y;
                imgSpacePoly.at(i).roundness = area->knots.at(i).roundness;
            }
            auto v = area->get_tessellation(imgSpacePoly);
            min_feather = polyFill(shape_mask, global_mask.width(), global_mask.height(), v, color);
            global_min_feather = rtengine::min<double>(global_min_feather, min_feather);

            radius = std::max(int(area->feather / 100.0 * min_feather), 1);

            break;
        }
        case AreaMask::Shape::GRADIENT:
        {
            auto area = static_cast<AreaMask::Gradient*>(area_.get());

            Coord center(w2 + area->x / 100.0 * w2, h2 + area->y / 100.0 * h2);
            float color_start = fgcolor * (area_->mode == AreaMask::Shape::SUBTRACT ?
                                          (100. - area->strengthStart) :
                                          area->strengthStart) / 100.;
            float color_end =   fgcolor * (area_->mode == AreaMask::Shape::SUBTRACT ?
                                          (100. - area->strengthEnd) :
                                          area->strengthEnd) / 100.;
            float color_diff = color_end - color_start;
            float half_feather = area->feather * rtengine::norm2<double> (width, height) / 200.0;
            float feather = 2 * half_feather;
            int w = global_mask.width();
            int h = global_mask.height();

#ifdef _OPENMP
#       pragma omp parallel if (multithread)
#endif
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    Coord m(x, y);
                    m += origin;
                    m -= center;
                    PolarCoord p(m);
                    double r, a;
                    p.get(r, a);
                    p.set(r, a + area->angle);
                    Coord point(p);
                    point.y += half_feather;

                    if (point.y < 0) {
                        shape_mask[y][x] = color_start;
                    }
                    else if (point.y > feather) {
                        shape_mask[y][x] = color_end;
                    }
                    else {
                        // in the feather range
                        shape_mask[y][x] = (point.y / feather) * color_diff + color_start;
                    }
                }
            }

            break;
        }
        default:
            break;
        }

        // feather the shape's mask
        if (area_->getType() != AreaMask::Shape::GRADIENT && area_->feather != 0.) {
            guidedFilter(guide, shape_mask, shape_mask, radius, 1e-7, multithread);
        }

        // No contrast applied per shape

        // blur the shape's mask
        if (area_->getType() != AreaMask::Shape::GRADIENT && area_->blur > 0.) {
#ifdef _OPENMP
#       pragma omp parallel if (multithread)
#endif
            gaussianBlur(shape_mask, shape_mask, global_mask.width(), global_mask.height(), area_->blur / scale);
        }

        // merge the shape's mask into the global mask
        switch (area_->mode) {
        case AreaMask::Shape::ADD:
#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < global_mask.height(); ++y) {
                float *s = shape_mask[y];
                float *g = global_mask[y];
                for (int x = 0; x < global_mask.width(); ++x) {
                    if (*s == bgcolor || *g == fgcolor) {
                        ++s;
                        ++g;
                        continue;
                    }
                    else if (*s == fgcolor) {
                        *(g++) = fgcolor;
                        ++s;
                    }
                    else {
                        *g = rtengine::min<float>(*g + *(s++), 1.);
                        ++g;
                    }
                }
            }
            break;
        case AreaMask::Shape::INTERSECT:
#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < global_mask.height(); ++y) {
                float *s = shape_mask[y];
                float *g = global_mask[y];
                for (int x = 0; x < global_mask.width(); ++x) {
                    *(g++) *= *(s++);
                }
            }
            break;
        case AreaMask::Shape::SUBTRACT:
        default:
#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < global_mask.height(); ++y) {
                float *s = shape_mask[y];
                float *g = global_mask[y];
                for (int x = 0; x < global_mask.width(); ++x) {
                    if (*s == fgcolor || *g == bgcolor) {
                        ++s;
                        ++g;
                        continue;
                    }
                    else if (*s == bgcolor) {
                        *(g++) = bgcolor;
                        ++s;
                    }
                    else {
                        *g = rtengine::max<float>(*g - (1. - *(s++)), 0.);
                        ++g;
                    }
                }
            }
            break;
        }
    }

    // feather the shape's mask
    if (areaMask.feather != 0.) {
        radius = std::max(int(areaMask.feather / 100.0 * global_min_feather), 1);
        guidedFilter(guide, global_mask, global_mask, radius, 1e-7, multithread);
    }

    // blur the shape's mask
    if (areaMask.blur > 0.) {
#ifdef _OPENMP
#       pragma omp parallel if (multithread)
#endif
        gaussianBlur(global_mask, global_mask, global_mask.width(), global_mask.height(), areaMask.blur / scale);
    }

    bool is_empty = plistener ? true : false;

    // Apply contrast
    if (!contrast_curve.isIdentity()) {
        const auto contrast =
            [&contrast_curve](float x) -> float
            {
                return contrast_curve.getVal(x);
            };
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < global_mask.height(); ++y) {
            for (int x = 0; x < global_mask.width(); ++x) {
                float v = LIM01(global_mask[y][x]);
                // if (!areaMask.inverted) {
                //     v = 1.f - v;
                // }
                global_mask[y][x] = contrast(v);
                assert(global_mask[y][x] == global_mask[y][x]);
                if (is_empty && global_mask[y][x]) {
                    is_empty = false;
                }
            }
        }
    } else {
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < global_mask.height(); ++y) {
            for (int x = 0; x < global_mask.width(); ++x) {
                global_mask[y][x] = LIM01(global_mask[y][x]);
                assert(global_mask[y][x] == global_mask[y][x]);
                if (is_empty && global_mask[y][x]) {
                    is_empty = false;
                }
            }
        }
    }

    if (is_empty && plistener) {
        plistener->error(M("LABMASKS_AREA_MASK_EMPTY_WARNING"));
    }

    return true;
}

bool generate_drawn_mask(int ox, int oy, int width, int height, const DrawnMask &drawnMask, const array2D<float> &guide, bool multithread, array2D<float> &mask)
{
    if (drawnMask.isTrivial()) {
        return false;
    }

    const int mask_w = guide.width();
    const int mask_h = guide.height();

    const bool add = drawnMask.mode != DrawnMask::INTERSECT;

    mask(mask_w, mask_h);
    const float bgcolor = 0.f;
    mask.fill(bgcolor);

    struct StrokeEval {
        StrokeEval(int ox, int oy, int width, int height,
                   int mask_w, int mask_h):
            ox_(ox), oy_(oy),
            width_(width), height_(height),
            mask_w_(mask_w), mask_h_(mask_h),
            radius_(-1),
            lx_(0), ly_(0),
            neg_(false)
        {
            curflag_ = 1;
            flag_.resize(mask_w_ * mask_h_, 0);
        }

        void update(const DrawnMask::Stroke &s)
        {
            int r = std::min(width_, height_) * s.radius * 0.25;

            if (r != radius_ || neg_ != s.erase || hardness_ != s.opacity) {
                if (neg_ != s.erase || hardness_ != s.opacity || !s.radius) {
                    ++curflag_;
                }

                radius_ = r;
                neg_ = s.erase;
                hardness_ = s.opacity;

                int w = 2*radius_ + 1;
                int h = 2*radius_ + 1;
                const float f = LIM01(hardness_);
                const float val = (neg_ ? -1.f : 1.f) + (1.f - f) * (neg_ ? 0.99f : -0.99f);
                
                buf_(w, h);
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        float d = std::sqrt(SQR(x - radius_) + SQR(y - radius_));
                        if (d <= radius_) {
                            buf_[y][x] = val;
                        } else {
                            buf_[y][x] = RT_NAN;
                        }
                    }
                }
            }

            int cx = width_ * s.x - ox_;
            int cy = height_ * s.y - oy_;
            lx_ = cx - radius_;
            ly_ = cy - radius_;
            lx = std::max(cx - radius_, 0);
            ly = std::max(cy - radius_, 0);
            ux = std::min(cx + radius_, mask_w_-1);
            uy = std::min(cy + radius_, mask_h_-1);
        }

        bool operator()(int x, int y, float &val)
        {
            val = buf_[y - ly_][x - lx_];
            size_t idx = y * mask_w_ + x;
            if (!xisnanf(val) && flag_[idx] != curflag_) {
                flag_[idx] = curflag_;
                return true;
            }
            return false;
        }

        bool is_bg(int x, int y) const
        {
            return flag_[y * mask_w_ + x] == 0;
        }

        int lx;
        int ly;
        int ux;
        int uy;

    private:
        int ox_;
        int oy_;
        int width_;
        int height_;
        int mask_w_;
        int mask_h_;
        int radius_;
        int lx_;
        int ly_;
        bool neg_;
        double hardness_;
        
        array2D<float> buf_;
        std::vector<uint16_t> flag_;
        std::vector<size_t> flagmods_;
        uint16_t curflag_;
    };

    const auto DUMP =
        [&](const char *name) -> void
        {
#if 0
            if (mask_w > 400) {
                Imagefloat tmp(mask_w, mask_h);
                for (int y = 0; y < mask_h; ++y) {
                    for (int x = 0; x < mask_w; ++x) {
                        tmp.r(y, x) = tmp.g(y, x) = tmp.b(y, x) = (mask[y][x] + 1.f) / 2.f * 65535.f;
                    }
                }
                tmp.saveTIFF(name, 16);
            }
#endif
        };
    

    double maxradius = 0.0;
    StrokeEval se(ox, oy, width, height, mask_w, mask_h);

    for (size_t i = 0; i < drawnMask.strokes.size(); ++i) {
        const auto &s = drawnMask.strokes[i];
        se.update(s);
        maxradius = std::max(maxradius, s.radius);
        for (int y = se.ly; y <= se.uy; ++y) {
            for (int x = se.lx; x <= se.ux; ++x) {
                float v = 0.f;
                if (se(x, y, v)) {
                    //mask[y][x] = LIM(mask[y][x] + v, -1.f, 1.f);
                    if (add) {
                        if (signf(mask[y][x]) == signf(v)) {
                            mask[y][x] = LIM(mask[y][x] + v, -1.f, 1.f);
                        } else {
                            mask[y][x] = LIM(LIM01(mask[y][x]) + v, -1.f, 1.f);
                        }
                    } else {
                        mask[y][x] = LIM01(mask[y][x] + v);
                    }
                }
            }
        }
    }
    DUMP("/tmp/after-strokes.tif");

    DiagonalCurve ccurve(drawnMask.contrast);
    const bool needscale = add && (drawnMask.smoothness > 0.f || drawnMask.feather > 0 || !ccurve.isIdentity());
    
    if (needscale) {
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < mask_h; ++y) {
            for (int x = 0; x < mask_w; ++x) {
                if (se.is_bg(x, y)) {
                    mask[y][x] = 0.5f;
                } else {
                    mask[y][x] = (mask[y][x] + 1.f) / 2.f;
                }
            }
        }
    }

    DUMP("/tmp/after-scale.tif");

    if (drawnMask.smoothness > 0.f) {
        float sigma = std::min(width, height) * maxradius * 0.2f * drawnMask.smoothness;
#ifdef _OPENMP
#       pragma omp parallel if (multithread)
#endif
        gaussianBlur(mask, mask, mask_w, mask_h, sigma);
    }

    DUMP("/tmp/after-smoothness.tif");

    if (drawnMask.feather > 0) {
        int radius = int(drawnMask.feather / 100.0 * std::min(width, height) * 0.1 + 0.5);
        if (radius > 0) {
            guidedFilterLog(guide, 2, /*mask,*/ mask, radius, 1e-5, multithread);
        }
    }

    DUMP("/tmp/after-feather.tif");

    if (!ccurve.isIdentity()) {
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < mask_h; ++y) {
            for (int x = 0; x < mask_w; ++x) {
                mask[y][x] = ccurve.getVal(mask[y][x]);
            }
        }
    }

    if (needscale) {
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < mask_h; ++y) {
            for (int x = 0; x < mask_w; ++x) {
                mask[y][x] = (mask[y][x] * 2.f - 1.f);
            }
        }
    }

#if 0
    if (mask_w > 400) {
        Imagefloat tmp(mask_w, mask_h);
        for (int y = 0; y < mask_h; ++y) {
            for (int x = 0; x < mask_w; ++x) {
                tmp.r(y, x) = tmp.g(y, x) = tmp.b(y, x) = (mask[y][x] + 1.f) / 2.f * 65535.f;
            }
        }
        tmp.saveTIFF("/tmp/drawnmask.tif", 16);
    }
#endif

    DUMP("/tmp/at-end.tif");
    
    return true;
}


template <class T>
void rgb2lab(Imagefloat::Mode mode, float R, float G, float B, float &L, float &a, float &b, const T ws[3][3])
{
    switch (mode) {
    case Imagefloat::Mode::RGB:
        Color::rgb2lab(R, G, B, L, a, b, ws);
        return;
    case Imagefloat::Mode::YUV:
        Color::yuv2rgb(G, B, R, R, G, B, ws);
        Color::rgb2lab(R, G, B, L, a, b, ws);
        return;
    case Imagefloat::Mode::XYZ:
        Color::XYZ2Lab(R, G, B, L, a, b);
        return;
    case Imagefloat::Mode::LAB:
        L = G;
        a = R;
        b = B;
        return;
    default:
        assert(false);
        L = a = b = 0.f;
        return;
    }
}


class DeltaEEvaluator {
public:
    DeltaEEvaluator(const std::vector<Mask> &masks)
    {
        masks_.reserve(masks.size());
        for (auto &m : masks) {
            masks_.push_back(m.deltaEMask);
            refs_.push_back(cmsCIELab());
            auto &de = masks_.back();
            auto &v = refs_.back();
            double h = de.H * 2.f * RT_PI / 360.f;
            v.L = de.L * 100.f;
            v.a = de.C * std::cos(h) * 100.f;
            v.b = de.C * std::sin(h) * 100.f;
        }
    }
    
    float operator()(size_t idx, float L, float a, float b)
    {
        auto &m = masks_[idx];
        if (!m.enabled || m.weight_L + m.weight_C + m.weight_H == 0) {
            return 1.f;
        }
        cmsCIELab c = { L * 100.f, a * 100.f, b * 100.f };
        auto &r = refs_[idx];
        const auto W = [](int w) -> double { return w ? 100.0 / w : 1000.0; };
        auto d = cmsCIE2000DeltaE(&r, &c, W(m.weight_L), W(m.weight_C), W(m.weight_H));
        float decay = std::abs(m.decay);
        bool invert = m.decay < 0.f;
        float ret = getval(m.range, 1.0 + LIM01(decay/100.0), d);
        if (m.strength < 100) {
            ret *= float(m.strength)/100.f;
        }
        if (invert) {
            ret = 1.f - ret;
        }
        return ret;
    }

private:
    float getval(float range, float decay, float d)
    {
        if (d <= range * 0.4f) {
            return 1.f;
        } else if (d >= range * 5.f) {
            return 0.f;
        } else {
            // Generalised logistic function (see Wikipedia)
            return 1.f - 1.f / (1 + 100.f * xexpf(-decay * (d - range)));
        }
    }
    
    std::vector<DeltaEMask> masks_;
    std::vector<cmsCIELab> refs_;
};


bool contrast_threshold_mask(int width, int height, float scale, array2D<float> &guide, int threshold, double blur, bool multithread, array2D<float> &mask)
{
    if (threshold == 0) {
        return false;
    }
    
    int W = guide.width();
    int H = guide.height();
    mask(W, H);

    int d = std::max(W, H);
    float s = float(d) / 1920.f;
    array2D<float> *src = &guide;
    array2D<float> *dst = &mask;
    array2D<float> tmpsrc;
    array2D<float> tmpdst;
    if (s > 1.f) {
        scale *= s;
        int ww = W / s;
        int hh = H / s;
        tmpsrc(ww, hh);
        rescaleBilinear(guide, tmpsrc, multithread);
        src = &tmpsrc;
        tmpdst(ww, hh);
        dst = &tmpdst;
    }
    float s_scale = std::sqrt(scale);
    
    float thresh = float(std::abs(threshold))/100.f * s_scale;
    float bl = std::max(blur, 2.0) / s_scale;
    //bool neg = threshold < 0;
    buildBlendMask(*src, *dst, src->width(), src->height(), thresh, 1.f, false, bl, 32768.f);

    if (dst != &mask) {
        rescaleBilinear(*dst, mask, multithread);
    }

    return true;
}


bool mask_postprocess(int width, int height, float scale, const array2D<float> &guide, const std::vector<double> &curve, int posterization, int smoothing, bool multithread, array2D<float> &mask)
{
    DiagonalCurve ccurve(curve);
    if (!posterization && ccurve.isIdentity()) {
        return false;
    }

    int W = mask.width();
    int H = mask.height();

    if (posterization) {
        constexpr float pp[] = { 30.f, 20.f, 10.f, 5.f, 3.f, 2.f };
        const int idx = LIM(posterization, 0, 6)-1;
        const float p = pp[idx];
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                mask[y][x] = int(mask[y][x] * p + 0.5) / p;
            }
        }
    }

    if (!ccurve.isIdentity()) {
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                mask[y][x] = ccurve.getVal(mask[y][x]);
            }
        }
    }

    if (posterization && smoothing) {
        const float radius_coeff = 10.f * (101.f - float(LIM(smoothing, 0, 100)));
        const float radius = (max(width, height) / radius_coeff);
        const float epsilon = 0.015f;
        array2D<float> threshold(W, H);
        constexpr float l = 0.0;
        constexpr float h = 0.25;
        float f = LIM01(float(smoothing)/100.f);
        float f2 = std::max(f-l, 0.f) / (h-l);
        const float fillval = LIM01((f < l ? 0.f : (f > h ? 1.f : (f2 < 0.5f ? 2.f * SQR(f2) : 1.f - 2.f * SQR(1.f - f2)))));
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                threshold[y][x] = mask[y][x] > 1e-4f ? 1.f : fillval;
            }
        }
        rtengine::guidedFilter(guide, mask, mask, radius, epsilon, multithread);
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                mask[y][x] *= threshold[y][x];
            }
        }
    }

    return true;
}


} // namespace


//-----------------------------------------------------------------------------
// LinkedMaskManager
//-----------------------------------------------------------------------------

LinkedMaskManager::LinkedMaskManager()
{
}


void LinkedMaskManager::init(const rtengine::ProcParams &pparams)
{
    masks_.clear();
    needed_.clear();
    
    const auto add =
        [&](const std::vector<procparams::Mask> &masks) -> void
        {
            for (auto &m : masks) {
                if (m.enabled && m.linkedMask.enabled && !m.linkedMask.toolname.empty() && !m.linkedMask.name.empty()) {
                    needed_.insert(key(m.linkedMask.toolname, m.linkedMask.name));
                }
            }
        };
    auto mp = pparams.get_maskable();
    for (auto p : mp) {
        add(p->get_masks());
    }
}


bool LinkedMaskManager::store_mask(const Glib::ustring &toolname, const Glib::ustring &name, const array2D<float> *mask1, const array2D<float> *mask2, bool multithread)
{
    if (name.empty()) {
        return false;
    }
    auto k = key(toolname, name);
    if (needed_.find(k) == needed_.end()) {
        return false;
    }
    auto &m = masks_[k];
    assert(mask1);
    const int W = mask1->width();
    const int H = mask1->height();
    m(W, H);
    constexpr uint32_t shift = 16U;
#ifdef _OPENMP
#   pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            uint32_t v1 = uint32_t(DNG_FloatToHalf((*mask1)[y][x]));
            uint32_t v2 = mask2 ? uint32_t(DNG_FloatToHalf((*mask2)[y][x])) : v1;
            uint32_t v = (v1 << shift) | v2;
            m[y][x] = v;
        }
    }
    return true;
}


bool LinkedMaskManager::apply_mask(const Glib::ustring &toolname, const Glib::ustring &name, bool inverted, array2D<float> *out1, array2D<float> *out2, bool multithread, ProgressListener *plistener)
{
    auto k = key(toolname, name);
    auto it = masks_.find(k);
    if (it == masks_.end()) {
        if (plistener && !toolname.empty() && !name.empty()) {
            plistener->error(Glib::ustring::compose(M("LINKED_MASK_NOT_FOUND_WARNING"), toolname, name));
        }
        return false;
    }
    auto &m = it->second;
    const int H = m.height();
    const int W = m.width();

    constexpr uint32_t shift = 16U;
    constexpr uint32_t bitmask = (uint32_t(1) << shift)-1U;

    assert(out1);

#ifdef _OPENMP
#   pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            uint32_t v = m[y][x];
            float f1 = DNG_HalfToFloat(v >> shift);
            float f2 = DNG_HalfToFloat(v & bitmask);
            if (inverted) {
                f1 = 1.f - f1;
                f2 = 1.f - f2;
            }
            (*out1)[y][x] *= f1;
            if (out2) {
                (*out2)[y][x] *= f2;
            }
        }
    }
    return true;
}


std::string LinkedMaskManager::key(const Glib::ustring &toolname, const Glib::ustring &name)
{
    return toolname.raw() + "|" + name.raw();
}


bool LinkedMaskManager::is_needed(const Glib::ustring &toolname, const Glib::ustring &name)
{
    return needed_.find(key(toolname, name)) != needed_.end();
}


//-----------------------------------------------------------------------------
// ExternalMaskManager
//-----------------------------------------------------------------------------

std::unique_ptr<ExternalMaskManager> ExternalMaskManager::instance_;

ExternalMaskManager::ExternalMaskManager():
    cache_(4)
{
}


ExternalMaskManager *ExternalMaskManager::getInstance()
{
    return instance_.get();
}


void ExternalMaskManager::init()
{
    instance_.reset(new ExternalMaskManager());
}


void ExternalMaskManager::cleanup()
{
    instance_.reset(nullptr);
}


bool ExternalMaskManager::apply_mask(const Glib::ustring &filename, bool inverted, double feather, int offset_x, int offset_y, int full_width, int full_height, const array2D<float> &guide, array2D<float> *out, bool multithread, ProgressListener *plistener)
{
    std::string key = Glib::filename_from_utf8(filename) + "\n" + getMD5(filename, true);
    std::shared_ptr<array2D<float>> mask;
    if (!cache_.get(key, mask)) {
        StdImageSource src;
        if (src.load(filename) != 0) {
            if (plistener && !filename.empty()) {
                plistener->error(Glib::ustring::compose(M("EXTERNAL_MASK_LOAD_FAILED_WARNING"), filename));
            }
            return false;
        }
        ImageIO *imio = src.getImageIO();
        const int W = imio->getWidth();
        const int H = imio->getHeight();
        Imagefloat img(W, H);
        PreviewProps pp(0, 0, W, H, 1);
        imio->getStdImage(ColorTemp(), TR_NONE, &img, pp);

        mask = std::make_shared<array2D<float>>();
        array2D<float> &a = *mask;
        a(W, H);

#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                a[y][x] = LIM01(img.g(y, x) / 65535.f);
            }
        }

        cache_.set(key, mask);
    }

    const array2D<float> &a = *mask;
    
    const int dW = full_width;
    const int dH = full_height;

    const int W = a.width();
    const int H = a.height();

    if (plistener) {
        int mask_ratio = int(float(W)/float(H) * 100 + 0.5);
        int image_ratio = int(float(full_width)/float(full_height) * 100 + 0.5);
        if (mask_ratio != image_ratio) {
            plistener->error(M("EXTERNAL_MASK_ASPECT_RATIO_WARNING"));
        }
    }
    
    const float col_scale = float(W) / float(dW);
    const float row_scale = float(H) / float(dH);

    const int oW = guide.width();
    const int oH = guide.height();
    (*out)(oW, oH);

    
#ifdef _OPENMP
#   pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < oH; ++y) {
        float sy = (y + offset_y) * row_scale;
        for (int x = 0; x < oW; ++x) {
            float sx = (x + offset_x) * col_scale;
            float val = getBilinearValue(a, sx, sy);
            if (inverted) {
                val = 1.f - val;
            }
            (*out)[y][x] = val;
        }
    }

    if (feather > 0) {
        int radius = int(feather / 100.0 * std::min(full_width, full_height) * 0.1 + 0.5);
        if (radius > 0) {
            guidedFilter(guide, *out, *out, radius, 1e-7, multithread);
        }
    }

    return true;
}

//-----------------------------------------------------------------------------

bool generateMasks(Imagefloat *rgb, const Glib::ustring &toolname, LinkedMaskManager &mmgr, const std::vector<Mask> &masks, int offset_x, int offset_y, int full_width, int full_height, double scale, bool multithread, int show_mask_idx, std::vector<array2D<float>> *Lmask, std::vector<array2D<float>> *abmask, ProgressListener *plistener)
{
    int n = masks.size();
    if (show_mask_idx < 0 || show_mask_idx >= n || !masks[show_mask_idx].enabled) {
        show_mask_idx = -1;
    }
    std::vector<std::unique_ptr<FlatCurve>> hmask(n);
    std::vector<std::unique_ptr<FlatCurve>> cmask(n);
    std::vector<std::unique_ptr<FlatCurve>> lmask(n);
    std::vector<float> ldetail(n);
    std::vector<bool> needed(n, true);

    const int W = rgb->getWidth();
    const int H = rgb->getHeight();
    const auto mode = rgb->mode();

    const Mask dflt;

    const int begin_idx = max(show_mask_idx, 0);
    const int end_idx = (show_mask_idx < 0 ? n : show_mask_idx+1);
    bool has_mask = false;

    for (int i = 0; i < end_idx; ++i) {
        auto &r = masks[i];
        if (i < begin_idx && !mmgr.is_needed(toolname, r.name)) {
            needed[i] = false;
            continue;
        }
        if (r.deltaEMask.enabled) {
            has_mask = true;
        }
        if (r.parametricMask.enabled && !r.parametricMask.hue.empty() && r.parametricMask.hue[0] != FCT_Linear && r.parametricMask.hue != dflt.parametricMask.hue) {
            hmask[i].reset(new FlatCurve(r.parametricMask.hue, true));
            has_mask = true;
        }
        if (r.parametricMask.enabled && !r.parametricMask.chromaticity.empty() && r.parametricMask.chromaticity[0] != FCT_Linear && r.parametricMask.chromaticity != dflt.parametricMask.chromaticity) {
            cmask[i].reset(new FlatCurve(r.parametricMask.chromaticity, false));
            has_mask = true;
        }
        if (r.parametricMask.enabled && !r.parametricMask.lightness.empty() && r.parametricMask.lightness[0] != FCT_Linear && r.parametricMask.lightness != dflt.parametricMask.lightness) {
            lmask[i].reset(new FlatCurve(r.parametricMask.lightness, false));
            has_mask = true;
            ldetail[i] = LIM01(float(r.parametricMask.lightnessDetail) / 100.f);
        }
        if (r.opacity < 100) {
            has_mask = true;
        }
    }

    assert(!abmask || abmask->size() == size_t(n));
    assert(!Lmask || Lmask->size() == size_t(n));

    bool has_lmask = false;
    for (int i = 0; i < end_idx; ++i) {
        if (!needed[i]) {
            continue;
        }
        if (abmask) {
            (*abmask)[i](W, H, ARRAY2D_CLEAR_DATA);
        }
        if (Lmask) {
            (*Lmask)[i](W, H, ARRAY2D_CLEAR_DATA);
            has_lmask = true;
        }
    }

    array2D<float> guide(W, H);
    TMatrix ws = ICCStore::getInstance()->workingSpaceMatrix(rgb->colorSpace());
    float wp[3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            wp[i][j] = ws[i][j];
        }
    }

    array2D<float> LL;
    if (has_lmask) {
        LL(W, H);

        constexpr float base_posterization = 40.f;
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float a, b;
                rgb2lab(mode, rgb->r(y, x), rgb->g(y, x), rgb->b(y, x), guide[y][x], a, b, wp);
                guide[y][x] /= 32768.f;
                float l = guide[y][x];
                float ll = round(l * base_posterization) / base_posterization;
                LL[y][x] = ll;
                assert(std::isfinite(LL[y][x]));
            }
        }
        const float radius = max(max(full_width, W), max(full_height, H)) / 30.f;
        const float epsilon = 0.001f;
        int r2 = 10.f / scale;
        if (r2 > 0) {
            rtengine::guidedFilter(guide, guide, guide, r2, 0.01f, multithread);
        }
        rtengine::guidedFilter(guide, LL, LL, radius, epsilon, multithread);

#if 0
        if (W > 300) {
            Imagefloat tmp(W, H);
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    tmp.r(y, x) = tmp.g(y, x) = tmp.b(y, x) = LL[y][x] * 65535.f;
                }
            }
            tmp.saveTIFF("/tmp/mask.tif", 16);
        }
#endif        
    }
    
    // magic constant c_factor: normally chromaticity is in [0; 42000] (see color.h), but here we use the constant to match how the chromaticity pipette works (see improcfun.cc lines 4705-4706 and color.cc line 1930
    constexpr float c_factor = 327.68f * (42000.f / 48000.f);

    DeltaEEvaluator dE(masks);

#ifdef _OPENMP
#       pragma omp parallel if (multithread)
#endif
    {
#ifdef __SSE2__
        float cBuffer[W];
        float hBuffer[W];
        float lBuffer[W];
        float aBuffer[W];
        float bBuffer[W];
#endif
#ifdef _OPENMP
#           pragma omp for schedule(dynamic, 16)
#endif
        for (int y = 0; y < H; ++y) {
#ifdef __SSE2__
            for (int x = 0; x < W; ++x) {
                rgb2lab(mode, rgb->r(y, x), rgb->g(y, x), rgb->b(y, x), lBuffer[x], aBuffer[x], bBuffer[x], wp);
                lBuffer[x] /= 32768.f;
                aBuffer[x] /= 42000.f;
                bBuffer[x] /= 42000.f;
            }
            if (has_mask) {
                // vectorized precalculation
                Color::Lab2Lch(aBuffer, bBuffer, cBuffer, hBuffer, W);
                for (int x = 0; x < W; ++x) {
                    cBuffer[x] *= c_factor;
                }
            }
#endif
            for (int x = 0; x < W; ++x) {
#ifdef __SSE2__
                const float l = lBuffer[x]; // / 32768.f;
                const float a = aBuffer[x]; // / 42000.f;
                const float b = bBuffer[x]; // / 42000.f;
#else
                float l, a, b;
                rgb2lab(mode, rgb->r(y, x), rgb->g(y, x), rgb->b(y, x), l, a, b, wp);
                l /= 32768.f;
                a /= 42000.f;
                b /= 42000.f;
#endif
                guide[y][x] = LIM01(l);
                // if (has_lmask) {
                //     l = intp(ldetail, LL[y][x], l);
                // }

                if (has_mask) {
#ifdef __SSE2__
                    // use precalculated values
                    float c = cBuffer[x];
                    float h = hBuffer[x];
#else
                    float c, h;
                    Color::Lab2Lch(a, b, c, h);
                    c *= c_factor;
#endif
                    c = xlin2log(c, 50.f);
                        
                    h = Color::huelab_to_huehsv2(h);
                    h += 1.f/6.f; // offset the hue because we start from purple instead of red
                    if (h > 1.f) {
                        h -= 1.f;
                    }
                    h = xlin2log(h, 3.f);

                    for (int i = 0; i < end_idx; ++i) {
                        if (!needed[i]) {
                            continue;
                        }
                        auto &hm = hmask[i];
                        auto &cm = cmask[i];
                        auto &lm = lmask[i];
                        float ll = has_lmask ? intp(ldetail[i], LL[y][x], l) : l;
                        float blend = /*LIM01*/(dE(i, l, a, b) * (hm ? hm->getVal(h) : 1.f) * (cm ? cm->getVal(c) : 1.f) * (lm ? lm->getVal(ll) : 1.f));
                        if (Lmask) {
                            (*Lmask)[i][y][x] = blend;
                        }
                        if (abmask) {
                            (*abmask)[i][y][x] = blend;
                        }
                    }
                }
            }
        }
    }

    if (has_mask) {
#ifdef __SSE2__
        const auto LIM01v =
            [](vfloat v) -> vfloat
            {
                return vminf(vmaxf(v, F2V(0)), F2V(1));
            };
#endif

        static constexpr float NO_BLUR = -10.f;
        
        for (int i = 0; i < end_idx; ++i) {
            if (!needed[i]) {
                continue;
            }
            float blur = masks[i].parametricMask.enabled ? masks[i].parametricMask.blur : 0.f;
            if (blur > NO_BLUR) {
                blur = blur < 0.f ? -1.f/blur : 1.f + blur;
                int r1 = max(int(4 / scale * blur + 0.5), 1);
                int r2 = max(int(25 / scale * blur + 0.5), 1);
                if (abmask) {
                    rtengine::guidedFilter(guide, (*abmask)[i], (*abmask)[i], r1, 0.001, multithread);
                }
                if (Lmask) {
                    rtengine::guidedFilter(guide, (*Lmask)[i], (*Lmask)[i], r2, 0.0001, multithread);
                }
            }
#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < H; ++y) {
                int x = 0;
#ifdef __SSE2__
                for (; x < W - 3; x += 4) {
                    if (abmask) {
                        STVFU((*abmask)[i][y][x], LIM01v(LVFU((*abmask)[i][y][x])));
                    }
                    if (Lmask) {
                        STVFU((*Lmask)[i][y][x], LIM01v(LVFU((*Lmask)[i][y][x])));
                    }
                }
#endif
                for (; x < W; ++x) {
                    if (abmask) {
                        (*abmask)[i][y][x] = LIM01((*abmask)[i][y][x]);
                    }
                    if (Lmask) {
                        (*Lmask)[i][y][x] = LIM01((*Lmask)[i][y][x]);
                    }
                }
            }
        }
    } else {
        for (int i = 0; i < end_idx; ++i) {
            if (!needed[i]) {
                continue;
            }
            if (Lmask) {
                (*Lmask)[i].fill(1.f);
            }
            if (abmask) {
                (*abmask)[i].fill(1.f);
            }
        }
    }

    if (full_width < 0) {
        full_width = W;
    }
    if (full_height < 0) {
        full_height = H;
    }

    array2D<float> amask;

    const auto apply_brush =
        [&](int i, bool add_bounded) -> void
        {
            if ((masks[i].drawnMask.mode == DrawnMask::ADD_BOUNDED) != add_bounded) {
                return;
            }
            if (generate_drawn_mask(offset_x, offset_y, full_width, full_height, masks[i].drawnMask, guide, multithread, amask)) {
                const bool add = masks[i].drawnMask.mode != DrawnMask::INTERSECT;
                const float alpha = LIM01(masks[i].drawnMask.opacity);
#ifdef _OPENMP
#                   pragma omp parallel for if (multithread)
#endif
                for (int y = 0; y < H; ++y) {
                    for (int x = 0; x < W; ++x) {
                        const float f = alpha * amask[y][x];
                        if (add) {
                            if (abmask) {
                                (*abmask)[i][y][x] = LIM01((*abmask)[i][y][x] + f);
                            }
                            if (Lmask) {
                                (*Lmask)[i][y][x] = LIM01((*Lmask)[i][y][x] + f);
                            }
                        } else {
                            if (abmask) {
                                (*abmask)[i][y][x] *= f;
                            }
                            if (Lmask) {
                                (*Lmask)[i][y][x] *= f;
                            }
                        }
                    }
                }
            }
        };


    for (int i = 0; i < end_idx; ++i) {
        if (!needed[i]) {
            continue;
        }
        if (masks[i].parametricMask.enabled && 
            contrast_threshold_mask(full_width, full_height, scale, guide, masks[i].parametricMask.contrastThreshold, masks[i].parametricMask.blur, multithread, amask)) {
            bool neg = masks[i].parametricMask.contrastThreshold < 0;
#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    float f = neg ? 1.f - amask[y][x] : amask[y][x];
                    if (abmask) {
                        (*abmask)[i][y][x] *= f;
                    }
                    if (Lmask) {
                        (*Lmask)[i][y][x] *= f;
                    }
                }
            }
        }

        apply_brush(i, true);
    
        if (generate_area_mask(offset_x, offset_y, full_width, full_height, guide, masks[i].areaMask, scale, multithread, amask, plistener)) {
#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    if (abmask) {
                        (*abmask)[i][y][x] *= amask[y][x];
                    }
                    if (Lmask) {
                        (*Lmask)[i][y][x] *= amask[y][x];
                    }
                }
            }
        }
        auto &em = masks[i].externalMask;
        if (em.enabled && ExternalMaskManager::getInstance()->apply_mask(em.filename, em.inverted, em.feather, offset_x, offset_y, full_width, full_height, guide, &amask, multithread, plistener)) {
#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    if (abmask) {
                        (*abmask)[i][y][x] *= amask[y][x];
                    }
                    if (Lmask) {
                        (*Lmask)[i][y][x] *= amask[y][x];
                    }
                }
            }            
        }
        auto &rm = masks[i].linkedMask;
        if (rm.enabled) {
            array2D<float> *m1 = nullptr;
            array2D<float> *m2 = nullptr;
            if (Lmask && abmask) {
                m1 = &((*abmask)[i]);
                m2 = &((*Lmask)[i]);
            } else if (Lmask) {
                m1 = &((*Lmask)[i]);
            } else {
                m1 = &((*abmask)[i]);
            }
            mmgr.apply_mask(rm.toolname, rm.name, rm.inverted, m1, m2, multithread, plistener);
        }

        apply_brush(i, false);

        const auto &curve = masks[i].curve;
        auto posterization = masks[i].posterization;
        auto smoothing = masks[i].smoothing;
        if (abmask) {
            mask_postprocess(full_width, full_height, scale, guide, curve, posterization, smoothing, multithread, (*abmask)[i]);
        }
        if (Lmask) {
            mask_postprocess(full_width, full_height, scale, guide, curve, posterization, smoothing, multithread, (*Lmask)[i]);
        }
    
        if (masks[i].inverted) {
#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    if (abmask) {
                        (*abmask)[i][y][x] = 1.f - (*abmask)[i][y][x];
                    }
                    if (Lmask) {
                        (*Lmask)[i][y][x] = 1.f - (*Lmask)[i][y][x];
                    }
                }
            }
        }
        if (masks[i].opacity < 100) {
            const float b = LIM01(float(masks[i].opacity) / 100.f);
#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    if (abmask) {
                        (*abmask)[i][y][x] *= b;
                    }
                    if (Lmask) {
                        (*Lmask)[i][y][x] *= b;
                    }
                }
            }
        }

        if (Lmask || abmask) {
            const array2D<float> *m1 = nullptr;
            const array2D<float> *m2 = nullptr;
            if (Lmask && abmask) {
                m1 = &((*abmask)[i]);
                m2 = &((*Lmask)[i]);
            } else if (Lmask) {
                m1 = &((*Lmask)[i]);
            } else {
                m1 = &((*abmask)[i]);
            }
            mmgr.store_mask(toolname, masks[i].name, m1, m2, multithread);
        }
    }

    if (show_mask_idx >= 0) {
        TMatrix iws = ICCStore::getInstance()->workingSpaceInverseMatrix(rgb->colorSpace());
        float iwp[3][3];
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                iwp[i][j] = iws[i][j];
            }
        }
        
        auto *smask = abmask ? abmask : Lmask;
        
#ifdef _OPENMP
        #pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                auto blend = smask ? (*smask)[show_mask_idx][y][x] : 0.f;
                float l, a, b;
                rgb2lab(mode, rgb->r(y, x), rgb->g(y, x), rgb->b(y, x), l, a, b, wp);
                a = 0.f;
                b = blend * 42000.f;
                l = LIM(l + 32768.f * blend, 0.f, 32768.f);
                Color::lab2rgb(l, a, b, rgb->r(y, x), rgb->g(y, x), rgb->b(y, x), iwp);
            }
        }
        rgb->assignMode(Imagefloat::Mode::RGB);

        return false;
    }

    return true;
}


void fillPipetteMasks(Imagefloat *rgb, PlanarWhateverData<float> *editWhatever, MasksEditID id, bool multithread)
{
    TMatrix ws = ICCStore::getInstance()->workingSpaceMatrix(rgb->colorSpace());
    float wp[3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            wp[i][j] = ws[i][j];
        }
    }
    const int W = rgb->getWidth();
    const int H = rgb->getHeight();
    const auto mode = rgb->mode();
    
#ifdef _OPENMP
#   pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float v = 0.f;
            float l, a, b;
            rgb2lab(mode, rgb->r(y, x), rgb->g(y, x), rgb->b(y, x), l, a, b, wp);
            switch (id) {
            case MasksEditID::H:
                v = Color::huelab_to_huehsv2(xatan2f(b, a));
                break;
            case MasksEditID::C:
                v = LIM01<float>(std::sqrt(SQR(a) + SQR(b) + 0.001f) / 48000.f);
                break;
            case MasksEditID::L:
                v = LIM01<float>(l / 32768.f);
                break;
            }
            editWhatever->v(y, x) = v;
        }
    }
}


bool getDeltaEColor(Imagefloat *rgb, int x, int y, int offset_x, int offset_y, int full_width, int full_height, double scale, float &L, float &C, float &H)
{
    std::vector<float> med_L, med_a, med_b;
    TMatrix ws = ICCStore::getInstance()->workingSpaceMatrix(rgb->colorSpace());
    const auto mode = rgb->mode();
    x = x / scale - offset_x;
    y = y / scale - offset_y;
    const int off = int(32.0 / scale + 0.5);
    if (x < 0 || x >= rgb->getWidth() || y < 0 || y >= rgb->getHeight()) {
        return false;
    }
    for (int i = max(y-off, 0), end = min(y+off, rgb->getHeight()); i < end; ++i) {
        for (int j = max(x-off, 0), end = min(x+off, rgb->getWidth()); j < end; ++j) {
            float L, a, b;
            rgb2lab(mode, rgb->r(i, j), rgb->g(i, j), rgb->b(i, j), L, a, b, ws);
            med_L.push_back(L);
            med_a.push_back(a);
            med_b.push_back(b);
        }
    }

    std::sort(med_L.begin(), med_L.end());
    std::sort(med_a.begin(), med_a.end());
    std::sort(med_b.begin(), med_b.end());

    auto idx = med_L.size()/2;
    L = med_L[idx] / 32768.f;
    float a = med_a[idx] / 42000.f;
    float b = med_b[idx] / 42000.f;
    C = sqrtf(SQR(a) + SQR(b));
    H = xatan2f(b, a) * 360.f / (2.f * RT_PI);
    if (H < 0.f) {
        H += 360.f;
    } else if (H > 360.f) {
        H -= 360.f;
    }
    return true;
}

} // namespace rtengine
