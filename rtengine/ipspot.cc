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

#include "improcfun.h"
#include "alpha.h"
#include "procparams.h"
#include "imagesource.h"
#include "imagefloat.h"
#include "rt_math.h"
#include "guidedfilter.h"
#include <iostream>
#include <set>

#define BENCHMARK
#include "StopWatch.h"

namespace rtengine {

namespace {

inline float find_sigma(float r, float f)
{
    if (f < 1e-2f) {
        return 1e-2f;
    }
    float m = 0.1f;
    int iter = 0;
    constexpr int maxiter = 100;
    while (true) {
        float sigma = 5.f * SQR(f * m);
        float val = xexpf((-SQR(std::max(f - r, 0.f)) / sigma));
        ++iter;
        if (val < 5e-3f || iter >= maxiter) {
            return sigma;
        }
        m *= 0.9f;
    }
}

inline float feather_factor(float x, float radius, float sigma)
{
    return LIM01(xexpf((-SQR(std::max(x - radius, 0.f)) / sigma)));
}


//-----------------------------------------------------------------------------
// adapted from gimpheal.c in GIMP
/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * gimpheal.c
 * Copyright (C) Jean-Yves Couleaud <cjyves@free.fr>
 * Copyright (C) 2013 Loren Merritt
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

void heal_laplace_loop(Imagefloat *img, const array2D<int32_t> &mask)
{
    const int width = img->getWidth();
    const int height = img->getHeight();
    
    const int MAX_ITER = std::min(std::max(width, height) * 2, 1000);

    constexpr float w = 1.4f;
    constexpr float w1 = 1.f - w;
    constexpr float w2 = w / 4.f;

    const auto next =
        [&](float **chan, int x, int y) -> void
        {
            float &cur = chan[y][x];
            float left = chan[y][x-1];
            float top = chan[y-1][x];
            float right = chan[y][x+1];
            float bottom = chan[y+1][x];
            float upd = cur * w1 + (left + top + right + bottom) * w2;
            cur = upd;
        };

#ifdef __SSE2__
    const vfloat w1v = F2V(w1);
    const vfloat w2v = F2V(w2);
    const vint iZEROv = vcast_vi_i(0);
    
    const auto vnext =
        [&](float **chan, int x, int y) -> void
        {
            vfloat cur = LVFU(chan[y][x]);
            vfloat left = LVFU(chan[y][x-1]);
            vfloat top = LVFU(chan[y-1][x]);
            vfloat right = LVFU(chan[y][x+1]);
            vfloat bottom = LVFU(chan[y+1][x]);
            vfloat upd = cur * w1v + (left + top + right + bottom) * w2v;
            STVFU(chan[y][x], upd);
        };
#endif

    for (int iter = 0; iter < MAX_ITER; ++iter) {
#ifdef _OPENMP
#       pragma omp parallel for schedule(dynamic,16)
#endif
        for (int y = 1; y < height-1; ++y) {
            int x = 1;
#ifdef __SSE2__
            for (; x < width-1-3; x+=4) {
                vint m = _mm_loadu_si128(reinterpret_cast<vint *>(&mask[y][x]));
                if (vtest(vnotm(vmaski_eq(m, iZEROv)))) {
                    vnext(img->r.ptrs, x, y);
                    vnext(img->g.ptrs, x, y);
                    vnext(img->b.ptrs, x, y);
                }
            }
#endif
            for (; x < width-1; ++x) {
                if (mask[y][x]) {
                    next(img->r.ptrs, x, y);
                    next(img->g.ptrs, x, y);
                    next(img->b.ptrs, x, y);
                }
            }
        }
    }
}


void heal(Imagefloat *src, Imagefloat *dst,
          int src_x, int src_y, int dst_x, int dst_y,
          int x1, int x2, int y1, int y2,
          int center_x, int center_y,
          float radius, float featherRadius, float opacity, int detail)
{
    BENCHFUN
        
    const int W = x2 - x1 + 1;
    const int H = y2 - y1 + 1;

    Imagefloat diff(W, H);
    array2D<int32_t> mask(W, H);
    const float detail_exp = 0.125f * (detail + 1);
    
#ifdef _OPENMP
#   pragma omp parallel for schedule(dynamic,16)
#endif
    for (int y = 0; y < H; ++y) {
        int sy = src_y + y;
        int dy = dst_y + y;
        float ry = float(y+y1 - center_y) - featherRadius;
        
        for (int x = 0; x < W; ++x) {
            float rx = float(x+x1 - center_x) - featherRadius;
            float r = sqrt(SQR(rx) + SQR(ry));

            mask[y][x] = (r <= featherRadius);
            float w = 1.f - pow_F(LIM01(radius - r)/radius, detail_exp);
            int sx = src_x + x;
            int dx = dst_x + x;
            diff.r(y, x) = w * (dst->r(dy, dx) - src->r(sy, sx));
            diff.g(y, x) = w * (dst->g(dy, dx) - src->g(sy, sx));
            diff.b(y, x) = w * (dst->b(dy, dx) - src->b(sy, sx));
        }
    }
    heal_laplace_loop(&diff, mask);

    const float sigma = find_sigma(radius, featherRadius);

    int sy = src_y;
    int dy = dst_y;
    for (int y = y1; y <= y2; ++y) {
        float ry = float(y - center_y) - featherRadius;

        int sx = src_x;
        int dx = dst_x;
        for (int x = x1; x <= x2; ++x) {
            float rx = float(x - center_x) - featherRadius;
            float r = sqrt(SQR(rx) + SQR(ry));
            float blend = opacity * (r <= radius ? 1.f : feather_factor(r, radius, sigma));
            float dr = diff.r(y-y1, x-x1);
            float dg = diff.g(y-y1, x-x1);
            float db = diff.b(y-y1, x-x1);
            float res_r = src->r(sy, sx) + dr;
            float res_g = src->g(sy, sx) + dg;
            float res_b = src->b(sy, sx) + db;

            dst->r(dy, dx) = intp(blend, res_r, dst->r(dy, dx));
            dst->g(dy, dx) = intp(blend, res_g, dst->g(dy, dx));
            dst->b(dy, dx) = intp(blend, res_b, dst->b(dy, dx));
            
            ++sx;
            ++dx;
        }
        ++sy;
        ++dy;
    }
}

//-----------------------------------------------------------------------------

} // namespace


class SpotBox {
public:
    enum class Type {
        SOURCE,
        TARGET,
        FINAL
    };

    struct Rectangle {
        int x1;
        int y1;
        int x2;
        int y2;

        Rectangle() : Rectangle(0, 0, 0, 0) {}
        Rectangle(int X1, int Y1, int X2, int Y2) : x1(X1), y1(Y1), x2(X2), y2(Y2) {}

        int getWidth() {
            return x2 - x1 + 1;
        }

        int getHeight() {
            return y2 - y1 + 1;
        }

        bool intersects(const Rectangle &other) const {
            return (other.x1 <= x2 && other.x2 >= x1)
                && (other.y1 <= y2 && other.y2 >= y1);
        }

        bool getIntersection(const Rectangle &other, std::unique_ptr<Rectangle> &intersection) const {
            if (intersects(other)) {
                std::unique_ptr<Rectangle> intsec(
                    new Rectangle(
                        rtengine::max(x1, other.x1),
                        rtengine::max(y1, other.y1),
                        rtengine::min(x2, other.x2),
                        rtengine::min(y2, other.y2)
                    )
                );

                if (intsec->x1 > intsec->x2 || intsec->y1 > intsec->y2) {
                    return false;
                }

                intersection = std::move(intsec);
                return true;
            }
            if (intersection) {
                // There's no intersection, we delete the Rectangle structure
                intersection.release();
            }
            return false;
        }

        Rectangle& operator+=(const Coord &v) {
            x1 += v.x;
            y1 += v.y;
            x2 += v.x;
            y2 += v.y;
            return *this;
        }

        Rectangle& operator-=(const Coord &v) {
            x1 -= v.x;
            y1 -= v.y;
            x2 -= v.x;
            y2 -= v.y;
            return *this;
        }

        Rectangle& operator/=(int v) {
            if (v == 1) {
                return *this;
            }

            int w = x2 - x1 + 1;
            int h = y2 - y1 + 1;
            w = w / v + static_cast<bool>(w % v);
            h = h / v + static_cast<bool>(h % v);
            x1 /= v;
            y1 /= v;
            x2 = x1 + w - 1;
            y2 = y1 + h - 1;

            return *this;
        }
    };

private:
    Type type;
    Imagefloat* image;

public:
    // top/left and bottom/right coordinates of the spot in image space (at some point divided by scale factor)
    Rectangle spotArea;
    // top/left and bottom/right coordinates of the spot in scaled image space (on borders, imgArea won't cover spotArea)
    Rectangle imgArea;
    // top/left and bottom/right coordinates of useful part of the image in scaled image space (rounding error workaround)
    Rectangle intersectionArea;
    float radius;
    float featherRadius;
    float opacity;
    int detail;

    SpotBox (int tl_x, int tl_y, int br_x, int br_y, int radius, int feather_radius, Imagefloat* image, Type type) :
       type(type),
       image(image),
       spotArea(tl_x, tl_y, br_x, br_y),
       imgArea(spotArea),
       intersectionArea(),
       radius(radius),
       featherRadius(feather_radius),
       opacity(1.f),
       detail(0)
    {}

    SpotBox (int tl_x, int tl_y, int radius, int feather_radius, Imagefloat* image, Type type) :
       type(type),
       image(image),
       spotArea(tl_x, tl_y, image ? tl_x + image->getWidth() - 1 : 0, image ? tl_y + image->getHeight() - 1 : 0),
       imgArea(spotArea),
       intersectionArea(),
       radius(radius),
       featherRadius(feather_radius),
       opacity(1.f),
       detail(0)
    {}

    SpotBox (SpotEntry &spot, Type type) :
        type(type),
        image(nullptr),
        intersectionArea(),
        radius(spot.radius),
        featherRadius(int(spot.getFeatherRadius() + 0.5f)),  // rounding to int before resizing
        opacity(spot.opacity),
        detail(spot.detail)
    {
        spotArea.x1 = int ((type == Type::SOURCE ? spot.sourcePos.x : spot.targetPos.x) - featherRadius);
        spotArea.x2 = int ((type == Type::SOURCE ? spot.sourcePos.x : spot.targetPos.x) + featherRadius);
        spotArea.y1 = int ((type == Type::SOURCE ? spot.sourcePos.y : spot.targetPos.y) - featherRadius);
        spotArea.y2 = int ((type == Type::SOURCE ? spot.sourcePos.y : spot.targetPos.y) + featherRadius);
        imgArea = spotArea;
    }

    ~SpotBox() {
        if (image && type != Type::FINAL) {
            delete image;
        }
    }

    SpotBox& operator /=(int v) {
        if (v == 1) {
            return *this;
        }
        spotArea /= v;
        imgArea /= v;
        radius /= float(v);
        featherRadius = getWidth() / 2.f;
        // intersectionArea doesn't need resize, because it's set after resizing
        return *this;
    }

    int getWidth() {
        return spotArea.getWidth();
    }

    int getHeight() {
        return spotArea.getHeight();
    }

    int getImageWidth() {
        return imgArea.getWidth();
    }

    int getImageHeight() {
        return imgArea.getHeight();
    }

    int getIntersectionWidth() {
        return intersectionArea.getWidth();
    }

    int getIntersectionHeight() {
        return intersectionArea.getHeight();
    }

    bool checkImageSize() {
        if (!image || getImageWidth() != image->getWidth() || getImageHeight() != image->getHeight()) {
            return false;
        }
        return true;
    }

    void tuneImageSize() {
        if (!image) {
            return;
        }
        if (getImageWidth() > image->getWidth()) {
            imgArea.x2 = imgArea.x1 + image->getWidth() - 1;
        }
        if (getImageHeight() > image->getHeight()) {
            imgArea.y2 = imgArea.y1 + image->getHeight() - 1;
        }
    }

    Imagefloat *getImage() {  // TODO: this should send back a const value, but getImage don't want it to be const...
        return image;
    }

    void allocImage() {
        int newW = imgArea.x2 - imgArea.x1 + 1;
        int newH = imgArea.y2 - imgArea.y1 + 1;

        if (image && type != Type::FINAL && (image->getWidth() != newW || image->getHeight() != newH)) {
            delete image;
            image = nullptr;
        }
        if (image == nullptr) {
            image = new Imagefloat(newW, newH);
        }
    }

    bool spotIntersects(const SpotBox &other) const {
        return spotArea.intersects(other.spotArea);
    }

    bool getSpotIntersection(const SpotBox &other, std::unique_ptr<Rectangle> &intersection) const {
        return spotArea.getIntersection(other.spotArea, intersection);
    }

    bool imageIntersects(const SpotBox &other, bool atDestLocation=false) const {
        if (atDestLocation) {
            Coord v(other.spotArea.x1 - spotArea.x1, other.spotArea.y1 - spotArea.y1);
            Rectangle imgArea2(imgArea.x1, imgArea.y1, imgArea.x2, imgArea.y2);
            imgArea2 += v;
            return imgArea2.intersects(other.imgArea);
        }
        return imgArea.intersects(other.imgArea);
    }

    bool mutuallyClipImageArea(SpotBox &other) {
        Coord v(other.spotArea.x1 - spotArea.x1, other.spotArea.y1 - spotArea.y1);
        Rectangle imgArea2 = imgArea;
        imgArea2 += v;
        std::unique_ptr<Rectangle> intersection;
        if (!imgArea2.getIntersection(other.imgArea, intersection)) {
            return false;
        }
        other.intersectionArea = *intersection;
        Coord v2(-v.x, -v.y);
        *intersection -= v;
        intersectionArea = *intersection;
        return true;
    }

    bool setIntersectionWith(const SpotBox &other) {
        if (!spotIntersects(other)) {
            return false;
        }
        imgArea.x1 = rtengine::max(spotArea.x1, other.spotArea.x1);
        imgArea.x2 = rtengine::min(spotArea.x2, other.spotArea.x2);
        imgArea.y1 = rtengine::max(spotArea.y1, other.spotArea.y1);
        imgArea.y2 = rtengine::min(spotArea.y2, other.spotArea.y2);
        if (imgArea.x1 > imgArea.x2 || imgArea.y1 > imgArea.y2) {
            return false;
        }
        return true;
    }

    bool processIntersectionWith(SpotBox &destBox) {
        Imagefloat *dstImg = destBox.image;

        if (image == nullptr || dstImg == nullptr) {
            std::cerr << "One of the source or destination SpotBox image is missing !" << std::endl;
            return false;
        }

        const int src_y = intersectionArea.y1 - imgArea.y1;
        const int dst_y = destBox.intersectionArea.y1 - destBox.imgArea.y1;
        const int src_x = intersectionArea.x1 - imgArea.x1;
        const int dst_x = destBox.intersectionArea.x1 - destBox.imgArea.x1;
        const int x1 = intersectionArea.x1, x2 = intersectionArea.x2;
        const int y1 = intersectionArea.y1, y2 = intersectionArea.y2;
        
        if (detail >= 2) {
            int center_x = spotArea.x1, center_y = spotArea.y1;
            heal(image, dstImg, src_x, src_y, dst_x, dst_y, x1, x2, y1, y2, center_x, center_y, radius, featherRadius, opacity, detail - 2);
            return true;
        }

        const float sigma = find_sigma(radius, featherRadius);

        array2D<float> srcY;
        array2D<float> dstY;

        constexpr float detail_blend = 0.6f;
        if (detail > 0) {
            int W = x2 - x1 + 1;
            int H = y2 - y1 + 1;
            srcY(W, H);
            dstY(W, H);
            
            constexpr float eps = 0.0005f;
            const int gr = radius * 0.2f;

            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    srcY[y][x] = image->g(y+src_y, x+src_x) / 65535.f;
                }
            }
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    dstY[y][x] = dstImg->g(y+dst_y, x+dst_x) / 65535.f;
                }
            }

            guidedFilter(srcY, srcY, srcY, gr, eps, false);
            guidedFilter(dstY, dstY, dstY, gr, eps, false);
        }

        int srcImgY = intersectionArea.y1 - imgArea.y1;
        int dstImgY = destBox.intersectionArea.y1 - destBox.imgArea.y1;
        for (int y = intersectionArea.y1; y <= intersectionArea.y2; ++y) {
            float dy = float(y - spotArea.y1) - featherRadius;

            int srcImgX = intersectionArea.x1 - imgArea.x1;
            int dstImgX = destBox.intersectionArea.x1 - destBox.imgArea.x1;
            for (int x = intersectionArea.x1; x <= intersectionArea.x2; ++x) {
                float dx = float(x - spotArea.x1) - featherRadius;
                float r = sqrt(dx * dx + dy * dy);

                if (r >= featherRadius) {
                    ++srcImgX;
                    ++dstImgX;
                    continue;
                }
                
                float blend = opacity;
                if (r > radius) {
                    blend = LIM01(blend * feather_factor(r, radius, sigma));
                }

                if (blend < 1e-3f) {
                    ++srcImgX;
                    ++dstImgX;
                    continue;
                }

                if (detail == 0) {
                    dstImg->r(dstImgY, dstImgX) = intp(blend, image->r(srcImgY, srcImgX), dstImg->r(dstImgY, dstImgX));
                    dstImg->g(dstImgY, dstImgX) = intp(blend, image->g(srcImgY, srcImgX), dstImg->g(dstImgY, dstImgX));
                    dstImg->b(dstImgY, dstImgX) = intp(blend, image->b(srcImgY, srcImgX), dstImg->b(dstImgY, dstImgX));
                } else {
                    float &sr = image->r(srcImgY, srcImgX);
                    float &sg = image->g(srcImgY, srcImgX);
                    float &sb = image->b(srcImgY, srcImgX);

                    float &dr = dstImg->r(dstImgY, dstImgX);
                    float &dg = dstImg->g(dstImgY, dstImgX);
                    float &db = dstImg->b(dstImgY, dstImgX);

                    float sY = sg;
                    float dY = dg;

                    float su = sY - sb;
                    float sv = sr - sY;
                    float du = dY - db;
                    float dv = dr - dY;

                    float sY_base = srcY[y-y1][x-x1] * 65535.f;
                    float sY_detail = sY - sY_base;

                    float dY_base = dstY[y-y1][x-x1] * 65535.f;
                    float dY_detail = dY - dY_base;

                    float res_Y = sY_base + intp(detail_blend, dY_detail, sY_detail);
                    dY = intp(blend, res_Y, dY);
                    du = intp(blend, su, du);
                    dv = intp(blend, sv, dv);

                    dr = dv + dY;
                    db = dY - du;
                    dg = dY;
                }

                ++srcImgX;
                ++dstImgX;
            }
            ++srcImgY;
            ++dstImgY;
        }

        return true;
    }

    // Copy the intersecting part
    bool copyImgTo(SpotBox &destBox) {
        Imagefloat *destImg = destBox.image;

        if (image == nullptr || destImg == nullptr) {
            std::cerr << "One of the source or destination SpotBox image is missing !" << std::endl;
            return false;
        }

        std::unique_ptr<Rectangle> intersection;

        if (!intersectionArea.getIntersection(destBox.intersectionArea, intersection)) {
            return false;
        }

        Imagefloat *srcImg = image;
        Imagefloat *dstImg = destBox.image;

        int srcImgY = intersection->y1 - imgArea.y1;
        int dstImgY = intersection->y1 - destBox.imgArea.y1;
        for (int y = intersection->y1; y <= intersection->y2; ++y) {
            int srcImgX = intersection->x1 - imgArea.x1;
            int dstImgX = intersection->x1 - destBox.imgArea.x1;

            for (int x = intersection->x1; x <= intersection->x2; ++x) {
                dstImg->r(dstImgY, dstImgX) = srcImg->r(srcImgY, srcImgX);
                dstImg->g(dstImgY, dstImgX) = srcImg->g(srcImgY, srcImgX);
                dstImg->b(dstImgY, dstImgX) = srcImg->b(srcImgY, srcImgX);
                ++srcImgX;
                ++dstImgX;
            }
            ++srcImgY;
            ++dstImgY;
        }

        return true;
    }
};

void ImProcFunctions::removeSpots(rtengine::Imagefloat* img, ImageSource* imgsrc, const std::vector<SpotEntry> &entries, const PreviewProps &pp, const ColorTemp &currWB, const ColorManagementParams *cmp, int tr, DenoiseInfoStore *dnstore)
{
    //Get the clipped image areas (src & dst) from the source image

    std::vector< std::shared_ptr<SpotBox> > srcSpotBoxs;
    std::vector< std::shared_ptr<SpotBox> > dstSpotBoxs;
    int fullImgWidth = 0;
    int fullImgHeight = 0;
    imgsrc->getFullSize(fullImgWidth, fullImgHeight, tr);
    SpotBox fullImageBox(0, 0, fullImgWidth - 1, fullImgHeight - 1, 0, 0, nullptr, SpotBox::Type::FINAL);
    SpotBox cropBox(pp.getX(), pp.getY(),
                    pp.getX() + pp.getWidth() - 1, pp.getY() + pp.getHeight() - 1,
                    0, 0, img, SpotBox::Type::FINAL);

    std::set<int> visibleSpots;   // list of dest spots intersecting the preview's crop
    int i = 0;

    const auto convert =
        [&](Imagefloat *img) -> void
        {
            bool converted = false;            
            if (params->filmNegative.colorSpace == FilmNegativeParams::ColorSpace::WORKING) {
                converted = true;
                imgsrc->convertColorSpace(img, *cmp, currWB);
            }
            if (params->filmNegative.enabled) {
                auto fnp = params->filmNegative;
                filmNegativeProcess(img, img, fnp, params->raw, imgsrc, currWB);
            }
            if (!converted) {
                imgsrc->convertColorSpace(img, *cmp, currWB);
            }
            if (params->denoise.enabled && dnstore && std::max(img->getWidth(), img->getHeight()) > 16) {
                denoise(imgsrc, currWB, img, *dnstore, params->denoise);
            }
        };

    for (auto entry : params->spot.entries) {
        std::shared_ptr<SpotBox> srcSpotBox(new SpotBox(entry,  SpotBox::Type::SOURCE));
        std::shared_ptr<SpotBox> dstSpotBox(new SpotBox(entry,  SpotBox::Type::TARGET));
        if (   !srcSpotBox->setIntersectionWith(fullImageBox)
            || !dstSpotBox->setIntersectionWith(fullImageBox)
            || !srcSpotBox->imageIntersects(*dstSpotBox, true))
        {
            continue;
            ++i;
        }

        // If spot intersect the preview image, add it to the visible spots
        if (dstSpotBox->spotIntersects(cropBox)) {
            visibleSpots.insert(i);
        }
        ++i;

        // Source area
        PreviewProps spp(srcSpotBox->imgArea.x1, srcSpotBox->imgArea.y1,
                         srcSpotBox->getImageWidth(), srcSpotBox->getImageHeight(), pp.getSkip());
        int w = 0;
        int h = 0;
        imgsrc->getSize(spp, w, h);
        *srcSpotBox /= pp.getSkip();
        srcSpotBox->allocImage();
        Imagefloat *srcImage = srcSpotBox->getImage();

        imgsrc->getImage(currWB, tr, srcSpotBox->getImage(), spp, params->exposure, params->raw);
        if (cmp) {
            convert(srcImage);
        }
        assert(srcSpotBox->checkImageSize());


        // Destination area
        spp.set(dstSpotBox->imgArea.x1, dstSpotBox->imgArea.y1, dstSpotBox->getImageWidth(),
                dstSpotBox->getImageHeight(), pp.getSkip());
        *dstSpotBox /= pp.getSkip();
        dstSpotBox->allocImage();
        Imagefloat *dstImage = dstSpotBox->getImage();
        imgsrc->getImage(currWB, tr, dstSpotBox->getImage(), spp, params->exposure, params->raw);
        if (cmp) {
            convert(dstImage);
        }
        assert(dstSpotBox->checkImageSize());

        // Update the intersectionArea between src and dest
        if (srcSpotBox->mutuallyClipImageArea(*dstSpotBox)) {
            srcSpotBoxs.push_back(srcSpotBox);
            dstSpotBoxs.push_back(dstSpotBox);
        }

    }

    // Construct list of upstream dependancies

    std::set<int> requiredSpots = visibleSpots;  // starting point, visible spots are necessarilly required spots
    for (auto i = requiredSpots.rbegin(); i != requiredSpots.rend(); i++) {
        int spotNbr = *i;
        requiredSpots.insert(spotNbr);
        if (spotNbr > 0) {
            for (int j = spotNbr - 1; j >= 0; --j) {
                if ((srcSpotBoxs.at(spotNbr))->imageIntersects(*dstSpotBoxs.at(j))) {
                    requiredSpots.insert(spotNbr);
                }
            }
        }
    }

    // Process spots and copy them downstream

    for (auto i = requiredSpots.begin(); i != requiredSpots.end(); i++) {
        // Process
        srcSpotBoxs.at(*i)->processIntersectionWith(*dstSpotBoxs.at(*i));

        // Propagate
        std::set<int> positiveSpots;  // For DEBUG purpose only !
        auto j = i;
        ++j;
        while (j != requiredSpots.end()) {
            bool intersectionFound = false;
            int i_ = *i;
            int j_ = *j;
            intersectionFound |= dstSpotBoxs.at(i_)->copyImgTo(*srcSpotBoxs.at(j_));
            intersectionFound |= dstSpotBoxs.at(i_)->copyImgTo(*dstSpotBoxs.at(j_));
            if (intersectionFound) {
                positiveSpots.insert(j_);
            }
            ++j;
        }
    }

    // Copy the dest spot to the preview image
    cropBox /= pp.getSkip();
    cropBox.tuneImageSize();
    cropBox.intersectionArea = cropBox.imgArea;

    for (auto i : visibleSpots) {
        dstSpotBoxs.at(i)->copyImgTo(cropBox);
    }
}

} // namespace rtengine
