/* -*- C++ -*-
 *
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2018 Alberto Griggio <alberto.griggio@gmail.com>
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

#include <complex.h>
#include <fftw3.h>

#include "improcfun.h"
#include "guidedfilter.h"
#include "rt_math.h"
#include "rt_algo.h"
#include "curves.h"
#include "masks.h"
#include "sleef.h"
#include "gauss.h"
#include "alignedbuffer.h"
#include "ipdenoise.h"
#include "rescale.h"
#include <iostream>
#include <queue>

//#include <random>
#include "rng.h"

#define BENCHMARK
#include "StopWatch.h"

#ifdef _OPENMP
#include <omp.h>
#endif

extern Options options;

namespace rtengine {

extern MyMutex *fftwMutex;

namespace {

// code adapted from darktable, src/iop/blurs.c
/*
    This file is part of darktable,
    Copyright (C) 2021 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/
void blur_2D_Bspline(const array2D<float> &src, array2D<float> &dst)
{
    const int height = src.height();
    const int width = src.width();
    constexpr int FSIZE = 5;
    constexpr float filter[FSIZE] = { 1.0f / 16.0f, 4.0f / 16.0f, 6.0f / 16.0f, 4.0f / 16.0f, 1.0f / 16.0f };
    
    for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width; ++j) {
            float acc = 0.f;

            for (int ii = 0; ii < FSIZE; ++ii) {
                for (int jj = 0; jj < FSIZE; ++jj) {
                    const int row = LIM(i + (ii - (FSIZE - 1) / 2), 0, height - 1);
                    const int col = LIM(j + (jj - (FSIZE - 1) / 2), 0, width - 1);

                    acc += filter[ii] * filter[jj] * src[row][col];
                }
            }

            dst[j][i] = acc;
        }
    }
}


void create_lens_kernel(array2D<float> &buffer,
                        const float n, const float m, const float k, const float rotation)
{
    const int width = buffer.width();
    const int height = buffer.height();
    
    // n is number of diaphragm blades
    // m is the concavity, aka the number of vertices on straight lines (?)
    // k is the roundness vs. linearity factor
    //   see https://math.stackexchange.com/a/4160104/498090
    // buffer sizes need to be odd

    // Spatial coordinates rounding error
    const float eps = 1.f / (float)width;
    const float radius = float(width / 2);

    for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width; ++j) {
            // get normalized kernel coordinates in [-1 ; 1]
            const float x = (i - 1) / radius - 1;
            const float y = (j - 1) / radius - 1;

            // get current radial distance from kernel center
            const float r = hypotf(x, y);

            // get the radial distance at current angle of the shape envelope
            const float M = cosf((2.f * asinf(k) + RT_PI_F * m) / (2.f * n))
                / cosf((2.f * asinf(k * cosf(n * (atan2f(y, x) + rotation))) + RT_PI_F * m) / (2.f * n));

            // write 1 if we are inside the envelope of the shape, else 0
            buffer[i][j] = (M >= r + eps);
        }
    }
}


void create_motion_kernel(array2D<float> &buffer,
                          const float angle, const float curvature, const float offset)
{
    const int width = buffer.width();
    //const int height = buffer.height();

    buffer.fill(0.f);
    
    // Compute the polynomial params from user params
    const float A = curvature / 2.f;
    const float B = 1.f;
    const float C = -A * offset * offset + B * offset;
    // Note : C ensures the polynomial arc always goes through the central pixel
    // so we don't shift pixels. This is meant to allow seamless connection
    // with unmasked areas when using masked blur.

    // Spatial coordinates rounding error
    const float eps = 1.f / (float)width;

    const float radius = float(width / 2);
    const float corr_angle = -RT_PI_F / 4.f - angle;

    // Matrix of rotation
    const float M[2][2] = { { cosf(corr_angle), -sinf(corr_angle) },
                            { sinf(corr_angle), cosf(corr_angle) } };

    for (int i = 0; i < 8 * width; ++i) {
        // Note : for better smoothness of the polynomial discretization,
        // we oversample 8 times, meaning we evaluate the polynomial
        // every eighth of pixel

        // get normalized kernel coordinates in [-1 ; 1]
        const float x = (i / 8.f - 1) / radius - 1;
        //const float y = (j - 1) / radius - 1; // not used here

        // build the motion path : 2nd order polynomial
        const float X = x - offset;
        const float y = X * X * A + X * B + C;

        // rotate the motion path around the kernel center
        const float rot_x = x * M[0][0] + y * M[0][1];
        const float rot_y = x * M[1][0] + y * M[1][1];

        // convert back to kernel absolute coordinates ± eps
        const int y_f[2] = { int(roundf((rot_y + 1) * radius - eps)),
                             int(roundf((rot_y + 1) * radius + eps)) };
        const int x_f[2] = { int(roundf((rot_x + 1) * radius - eps)),
                             int(roundf((rot_x + 1) * radius + eps)) };

        // write 1 if we are inside the envelope of the shape, else 0
        // leave 1px padding on each border of the kernel for the anti-aliasing
        for (int l = 0; l < 2; l++) {
            for (int m = 0; m < 2; m++) {
                if (x_f[l] > 0 && x_f[l] < width - 1 && y_f[m] > 0 && y_f[m] < width - 1) {
                    buffer[y_f[m]][x_f[l]] = 1.f;
                }
            }
        }
    }
}


float compute_norm(array2D<float> &buffer)
{
    const int width = buffer.width();
    const int height = buffer.height();
    
    float norm = 0.f;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            norm += buffer[y][x];
        }
    }
    return norm;
}


void normalize(array2D<float> &buffer, const float norm)
{
    const int width = buffer.width();
    const int height = buffer.height();

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            buffer[y][x] /= norm;
        }
    }
}


bool build_blur_kernel(array2D<float> &out, const SmoothingParams &params, int region, double scale)
{
    const auto &r = params.regions[region];
    
    int radius = std::ceil(r.radius / scale);
    if (radius < 1) {
        return false;
    }
    
    int kersz = 2 * radius + 1;
    array2D<float> buf(kersz, kersz);
    out(kersz, kersz);
    constexpr float torad = RT_PI_F / 180.f;

    if (r.mode == SmoothingParams::Region::Mode::MOTION) {
        create_motion_kernel(buf, (r.angle + 90) * torad, r.curvature, r.offset);
    } else {
        assert(r.mode == SmoothingParams::Region::Mode::LENS);
        create_lens_kernel(buf, r.numblades, 1.f, 1.f, r.angle * torad + RT_PI_F);
    }
    blur_2D_Bspline(buf, out);
    float norm = compute_norm(out);
    normalize(out, norm);

    return true;
}


bool do_inpainting(const Imagefloat *src, Imagefloat *dst, const array2D<float> &mask, const SmoothingParams &params, int region, double scale, bool multithread)
{
    const auto &r = params.regions[region];
    int radius = std::ceil(r.radius / scale);
    if (radius < 1) {
        return false;
    }
    
    const int W = src->getWidth();
    const int H = src->getHeight();

    dst->allocate(W, H);
#ifdef _OPENMP
#   pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            dst->r(y, x) = src->r(y, x);
            dst->g(y, x) = src->g(y, x);
            dst->b(y, x) = src->b(y, x);
        }
    }

    float threshold = r.mode == SmoothingParams::Region::Mode::LENS ? 0.25f : 0.95f;
    inpaint(dst, mask, -threshold, 16.f / scale + 0.5, radius / 2, radius * 2, multithread, 4);

    return true;
}


void lens_motion_blur(ImProcData &im, Imagefloat *rgb, const array2D<float> &mask, int region)
{
    Imagefloat tmp;
    Imagefloat *src = rgb;
    if (do_inpainting(rgb, &tmp, mask, im.params->smoothing, region, im.scale, im.multiThread)) {
        rgb = &tmp;
    }
    
    array2D<float> kernel;
    if (build_blur_kernel(kernel, im.params->smoothing, region, im.scale)) {
        Convolution conv(kernel, rgb->getWidth(), rgb->getHeight(), im.multiThread);
        conv(rgb->r.ptrs, rgb->r.ptrs);
        conv(rgb->g.ptrs, rgb->g.ptrs);
        conv(rgb->b.ptrs, rgb->b.ptrs);
    }

    if (src != rgb) {
        const int W = src->getWidth();
        const int H = src->getHeight();
#ifdef _OPENMP
#       pragma omp parallel for if (im.multiThread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const float b = LIM01(mask[y][x]);
                src->r(y, x) = intp(b, rgb->r(y, x), src->r(y, x));
                src->g(y, x) = intp(b, rgb->g(y, x), src->g(y, x));
                src->b(y, x) = intp(b, rgb->b(y, x), src->b(y, x));
            }
        }
    }
}

//-----------------------------------------------------------------------------

enum class Channel {
    L,
    C,
    LC
};


void guided_smoothing(array2D<float> &R, array2D<float> &G, array2D<float> &B, const TMatrix &ws, const TMatrix &iws, Channel chan, int radius, float epsilon, double scale, bool multithread)
{
    const auto rgb2yuv =
        [&](float R, float G, float B, float &Y, float &u, float &v) -> void
        {
            Color::rgb2yuv(R, G, B, Y, u, v, ws);
        };

    const auto yuv2rgb =
        [&](float Y, float u, float v, float &R, float &G, float &B) -> void
        {
            Color::yuv2rgb(Y, u, v, R, G, B, ws);
        };
        
    int r = max(int(round(radius / scale)), 0);
    if (r > 0) {
        const int W = R.width();
        const int H = R.height();

        array2D<float> iR(W, H, R, ARRAY2D_ALIGNED);
        array2D<float> iG(W, H, G, ARRAY2D_ALIGNED);
        array2D<float> iB(W, H, B, ARRAY2D_ALIGNED);

        const bool rgb = (chan == Channel::LC);
        const bool luminance = (chan == Channel::L);

        if (rgb) {
            rtengine::guidedFilterLog(10.f, R, r, epsilon, multithread);
            rtengine::guidedFilterLog(10.f, G, r, epsilon, multithread);
            rtengine::guidedFilterLog(10.f, B, r, epsilon, multithread);
        } else {
            array2D<float> guide(W, H, ARRAY2D_ALIGNED);
#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    float l = Color::rgbLuminance(R[y][x], G[y][x], B[y][x], ws);
                    guide[y][x] = xlin2log(max(l, 0.f), 10.f);
                }
            }
            rtengine::guidedFilterLog(guide, 10.f, R, r, epsilon, multithread);
            rtengine::guidedFilterLog(guide, 10.f, G, r, epsilon, multithread);
            rtengine::guidedFilterLog(guide, 10.f, B, r, epsilon, multithread);

#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    float rr = R[y][x];
                    float gg = G[y][x];
                    float bb = B[y][x];
                    float ir = iR[y][x];
                    float ig = iG[y][x];
                    float ib = iB[y][x];
                
                    float iY, iu, iv;
                    float oY, ou, ov;
                    rgb2yuv(ir, ig, ib, iY, iu, iv);
                    rgb2yuv(rr, gg, bb, oY, ou, ov);
                    if (luminance) {
                        ou = iu;
                        ov = iv;
                    } else {
                        float bump = oY > 1e-5f ? iY / oY : 1.f;
                        ou *= bump;
                        ov *= bump;
                        oY = iY;
                    }
                    yuv2rgb(oY, ou, ov, R[y][x], G[y][x], B[y][x]);
                }
            }
        }
    }
}

void find_region(const array2D<float> &mask, int &min_x, int &min_y, int &max_x, int &max_y)
{
    const int W = mask.width();
    const int H = mask.height();
    
    min_x = W - 1;
    min_y = H - 1;
    max_x = 0;
    max_y = 0;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (mask[y][x] > 0.f) {
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
        }
    }

    ++max_x;
    ++max_y;
}


void gaussian_smoothing(array2D<float> &R, array2D<float> &G, array2D<float> &B, float *buf, const TMatrix &ws, Channel chan, double sigma, double scale, bool multithread)
{
    //static constexpr double HIGH_PRECISION_THRESHOLD = 2.0;
    
    double s = sigma / scale;
    const int W = R.width();
    const int H = R.height();

    array2D<float> kernel;
    const bool high_precision = s > 0;// && s < HIGH_PRECISION_THRESHOLD;
    std::unique_ptr<Convolution> conv;
    if (high_precision) {
        build_gaussian_kernel(s, kernel);
        conv.reset(new Convolution(kernel, W, H, multithread));
    }

    const auto blur =
        [&](array2D<float> &a) -> void
        {
            if (high_precision) {
                (*conv)(a, a);
            } else {
#ifdef _OPENMP
#               pragma omp parallel if (multithread)
#endif
                gaussianBlur(a, a, W, H, s, buf);
            }
        };

    if (chan == Channel::LC) {
        blur(R);
        blur(G);
        blur(B);
    } else {
        const bool luminance = (chan == Channel::L);
        array2D<float> iR(W, H, R, ARRAY2D_ALIGNED);
        array2D<float> iG(W, H, G, ARRAY2D_ALIGNED);
        array2D<float> iB(W, H, B, ARRAY2D_ALIGNED);

        blur(R);
        blur(G);
        blur(B);

#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float iY, iu, iv;
                float oY, ou, ov;
                Color::rgb2yuv(iR[y][x], iG[y][x], iB[y][x], iY, iu, iv, ws);
                Color::rgb2yuv(R[y][x], G[y][x], B[y][x], oY, ou, ov, ws);
                if (luminance) {
                    Color::yuv2rgb(oY, iu, iv, R[y][x], G[y][x], B[y][x], ws);
                } else {
                    Color::yuv2rgb(iY, ou, ov, R[y][x], G[y][x], B[y][x], ws);
                }
            }
        }
    }
}


void nlmeans_smoothing(array2D<float> &R, array2D<float> &G, array2D<float> &B, const TMatrix &ws, const TMatrix &iws, Channel chan, int strength, int detail, int iterations, double scale, bool multithread)
{
    array2D<float> iY;
    const int W = R.width(), H = R.height();

    if (chan == Channel::L) {
        iY(W, H, ARRAY2D_ALIGNED);
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            float u, v;
            for (int x = 0; x < W; ++x) {
                Color::rgb2yuv(R[y][x], G[y][x], B[y][x], iY[y][x], u, v, ws);
            }
        }
        for (int i = 0; i < iterations; ++i) {
            denoise::NLMeans(iY, 1.f, strength, detail, scale, multithread);
        }
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            float Y, u, v;
            for (int x = 0; x < W; ++x) {
                Color::rgb2yuv(R[y][x], G[y][x], B[y][x], Y, u, v, ws);
                Color::yuv2rgb(iY[y][x], u, v, R[y][x], G[y][x], B[y][x], ws);
            }
        }
    } else {
        if (chan == Channel::C) {
            iY(W, H, ARRAY2D_ALIGNED);
#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < H; ++y) {
                float u, v;
                for (int x = 0; x < W; ++x) {
                    Color::rgb2yuv(R[y][x], G[y][x], B[y][x], iY[y][x], u, v, ws);
                }
            }
        }
        
        for (int i = 0; i < iterations; ++i) {
            denoise::NLMeans(R, 1.f, strength, detail, scale, multithread);
            denoise::NLMeans(G, 1.f, strength, detail, scale, multithread);
            denoise::NLMeans(B, 1.f, strength, detail, scale, multithread);
        }
        
        if (chan == Channel::C) {
#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < H; ++y) {
                float Y, u, v;
                for (int x = 0; x < W; ++x) {
                    Color::rgb2yuv(R[y][x], G[y][x], B[y][x], Y, u, v, ws);
                    Color::yuv2rgb(iY[y][x], u, v, R[y][x], G[y][x], B[y][x], ws);
                }
            }
        }
    }
}


void add_noise(array2D<float> &R, array2D<float> &G, array2D<float> &B, const TMatrix &ws, int strength, int coarseness, double scale, Channel chan, bool multithread)
{
    BENCHFUN

    const int W = R.width();
    const int H = R.height();

    const float sf = LIM01(float(strength)/(chan == Channel::L ? 200.f : 100.f)) / scale;
    const float radius = (0.5f + 1.75f * float(coarseness)/100.f) / scale;
    
    RandomNumberGenerator rng(42 + int(chan) + coarseness);

    array2D<float> kernel;
    {
        const int sz = int(std::ceil(radius)) * 2 + 1;
        kernel(sz, sz);
        const int c = sz / 2;
        double totd = 0.0;
        for (int i = 0; i < sz; ++i) {
            for (int j = 0; j < sz; ++j) {
                float r = std::sqrt(SQR(i-c) + SQR(j-c));
                float d = r - radius;
                kernel[i][j] = d < 0.f ? 1.f : std::max(1.f - d, 0.f);
                totd += kernel[i][j];
            }
        }
        const float tot = totd;
        for (int i = 0; i < kernel.height(); ++i) {
            for (int j = 0; j < kernel.width(); ++j) {
                kernel[i][j] /= tot;
            }
        }
    }
    Convolution conv(kernel, W, H, multithread);

    constexpr uint32_t normd_size = 5233;
    float normd[normd_size];
    NormalDistribution d;
    for (uint32_t i = 0; i < normd_size; ++i) {
        normd[i] = d(rng);
    }
    
    const auto noise = 
        [&](array2D<float> &a, int chan) -> void
        {
            constexpr float chan_sd[5] = { 1.f, 0.7f, 1.f, 1.3f };
            const float c01 = float(coarseness) / 100.f;
            const float c = 655.35f / (20.f + std::pow(c01, 0.5f) * 80.f);
            const float sd = chan_sd[chan];

            array2D<float> noisebuf(W, H);

#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    float v = a[y][x];
                    //float mu = LIM01(v) * c;
                    float mu = std::max(v, 0.f) * c;
                    float r = normd[rng.randint(normd_size)] * sd;
                    float m = mu + sqrtf(mu) * r;
                    noisebuf[y][x] = m / c - v;
                }
            }

            conv(noisebuf, noisebuf);
            
#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    float n = noisebuf[y][x];
                    //a[y][x] += sf * n;
                    a[y][x] = std::max(a[y][x] + sf * n, 0.f);
                }
            }
        };
    

    if (chan == Channel::LC) {
        noise(R, 1);
        noise(G, 2);
        noise(B, 3);
    } else if (chan == Channel::L) {
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                Color::rgb2yuv(R[y][x], G[y][x], B[y][x], G[y][x], R[y][x], B[y][x], ws);
            }
        }

        noise(G, 0);

#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                Color::yuv2rgb(G[y][x], R[y][x], B[y][x], R[y][x], G[y][x], B[y][x], ws);
            }
        }
    } else {
        array2D<float> Y(W, H);
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float u, v;
                Color::rgb2yuv(R[y][x], G[y][x], B[y][x], Y[y][x], u, v, ws);
            }
        }
        noise(R, 1);
        noise(G, 2);
        noise(B, 3);
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float l, u, v;
                Color::rgb2yuv(R[y][x], G[y][x], B[y][x], l, u, v, ws);
                Color::yuv2rgb(Y[y][x], u, v, R[y][x], G[y][x], B[y][x], ws);
            }
        }
    }
}


// adapted from https://github.com/hotgluebanjo/halation-dctl
void halation(array2D<float> &R, array2D<float> &G, array2D<float> &B, int size, float color, bool multithread)
{
    if (size <= 0) {
        return;
    }
    
    array2D<float> kernel(2*size+1, 2*size+1);
    float radius = size;

    for (int i = -size, y = 0; i <= size; ++i, ++y) {
        for (int j = -size, x = 0; j <= size; ++j, ++x) {
            float dist = SQR(i) + SQR(j);
            float e = dist == 0.f ? 1.f : 1.f / dist;
            kernel[y][x] = e * std::max((radius - std::sqrt(dist)) / radius, 0.f);
        }
    }
    
    normalize(kernel, compute_norm(kernel));

    const int W = R.width();
    const int H = R.height();

    Convolution conv(kernel, W, H, multithread);
    array2D<float> hR(W, H);
    array2D<float> hG(W, H);
    array2D<float> hB(W, H);
    conv(R, hR);
    conv(G, hG);
    conv(B, hB);

    constexpr float cR = 0.7f;
    const float cG = 1.f - color / 3.f;
    constexpr float cB = 1.f;

    const auto halate =
        [](float &rgb, float blurred, float color) -> void
        {
            float halated = (rgb - blurred) * color;
            rgb = halated + blurred;
        };
    
#ifdef _OPENMP
#   pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            halate(R[y][x], hR[y][x], cR);
            halate(G[y][x], hG[y][x], cG);
            halate(B[y][x], hB[y][x], cB);
        }
    }
}


void wavelet_smoothing(array2D<float> &R, array2D<float> &G, array2D<float> &B, const TMatrix &ws, float strength, int levels, float gamma, double scale, Channel chan, bool multithread)
{
    if (strength <= 0.1f) {
        return;
    }
    
    const int W = R.width();
    const int H = R.height();
    const int nlevels = std::max(levels - int(std::log2(scale)), 2);
    constexpr float eps = 0.01f;
    const float s = SQR(float(strength)/125.f * (1.f + strength / 25.f));

    const auto mad =
        [&](float *coeffs, int len) -> float
        {
            float med = 0.f;
            AlignedBuffer<float> buf(len);
#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int i = 0; i < len; ++i) {
                buf.data[i] = std::abs(coeffs[i]);
            }
            findMinMaxPercentile(buf.data, len, 0.5f, med, 0.5f, med, multithread);
            return med / 0.6745f;
        };

    const auto wav =
        [&](float *data) -> void
        {
#ifdef _OPENMP
            int nthreads = multithread ? omp_get_num_procs() : 1;
#else
            int nthreads = 1;
#endif
            wavelet_decomposition wd(data, W, H, nlevels, 1, 1, nthreads);
            for (int lvl = 0; lvl < wd.maxlevel(); ++lvl) {
                for (int dir = 1; dir < 4; ++dir) {
                    const int lW = wd.level_W(lvl);
                    const int lH = wd.level_H(lvl);
                    float **coeffs = wd.level_coeffs(lvl);
                    float m = SQR(mad(coeffs[dir], lW * lH) * 65535.f);
                    float level_factor = m * 5.0 / float(lvl + 1);
                    const int n = lW * lH;
                    
#ifdef _OPENMP
#                   pragma omp parallel for if (multithread)
#endif
                    for (int i = 0; i < n; ++i) {
                        float mag = SQR(coeffs[dir][i] * 65535.f);
                        float sf = mag / (mag + level_factor * s * xexpf(-mag / (9 * level_factor * s)) + eps);
                        float f = SQR(sf) / (sf + eps);
                        coeffs[dir][i] *= f;
                    }
                }
            }
            wd.reconstruct(data);
        };
    
    if (gamma > 1.f) {
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                R[y][x] = pow_F(std::max(R[y][x], 0.f), 1.f/gamma);
                G[y][x] = pow_F(std::max(G[y][x], 0.f), 1.f/gamma);
                B[y][x] = pow_F(std::max(B[y][x], 0.f), 1.f/gamma);
            }
        }
    }

    if (chan == Channel::LC) {
        wav(R);
        wav(G);
        wav(B);
    } else {
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                Color::rgb2yuv(R[y][x], G[y][x], B[y][x], G[y][x], R[y][x], B[y][x], ws);
            }
        }

        if (chan == Channel::L) {
            wav(G);
        } else {
            wav(R);
            wav(B);
        }

#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                Color::yuv2rgb(G[y][x], R[y][x], B[y][x], R[y][x], G[y][x], B[y][x], ws);
            }
        }
    }

    if (gamma > 1.f) {
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                R[y][x] = pow_F(std::max(R[y][x], 0.f), gamma);
                G[y][x] = pow_F(std::max(G[y][x], 0.f), gamma);
                B[y][x] = pow_F(std::max(B[y][x], 0.f), gamma);
            }
        }        
    }
}

} // namespace


namespace denoise {

void denoiseGuidedSmoothing(ImProcData &im, Imagefloat *rgb)
{
    if (!im.params->denoise.smoothingEnabled || im.params->denoise.guidedChromaRadius == 0) {
        return;
    }

    rgb->normalizeFloatTo1(im.multiThread);

    const int W = rgb->getWidth();
    const int H = rgb->getHeight();
    array2D<float> R(W, H, rgb->r.ptrs, ARRAY2D_BYREFERENCE);
    array2D<float> G(W, H, rgb->g.ptrs, ARRAY2D_BYREFERENCE);
    array2D<float> B(W, H, rgb->b.ptrs, ARRAY2D_BYREFERENCE);

    TMatrix ws = ICCStore::getInstance()->workingSpaceMatrix(im.params->icm.workingProfile);
    TMatrix iws = ICCStore::getInstance()->workingSpaceInverseMatrix(im.params->icm.workingProfile);

    const float c_eps = 0.001f;

    guided_smoothing(R, G, B, ws, iws, Channel::C, im.params->denoise.guidedChromaRadius, c_eps, im.scale, im.multiThread);
    
    rgb->normalizeFloatTo65535(im.multiThread);
}

} // namespace denoise


bool ImProcFunctions::guidedSmoothing(Imagefloat *rgb)
{
    PlanarWhateverData<float> *editWhatever = nullptr;
    EditUniqueID eid = pipetteBuffer ? pipetteBuffer->getEditID() : EUID_None;

    if ((eid == EUID_Masks_H3 || eid == EUID_Masks_C3 || eid == EUID_Masks_L3) && pipetteBuffer->getDataProvider()->getCurrSubscriber()->getPipetteBufferType() == BT_SINGLEPLANE_FLOAT) {
        editWhatever = pipetteBuffer->getSinglePlaneBuffer();
    }

    if (eid == EUID_Masks_DE3) {
        if (getDeltaEColor(rgb, deltaE.x, deltaE.y, offset_x, offset_y, full_width, full_height, scale, deltaE.L, deltaE.C, deltaE.H)) {
            deltaE.ok = true;
        }
    }
        
    if (params->smoothing.enabled) {
        if (editWhatever) {
            MasksEditID id = static_cast<MasksEditID>(int(eid) - EUID_Masks_H3);
            fillPipetteMasks(rgb, editWhatever, id, multiThread);
        }
        
        int n = params->smoothing.regions.size();
        int show_mask_idx = params->smoothing.showMask;
        if (show_mask_idx >= n || (cur_pipeline != Pipeline::PREVIEW /*&& cur_pipeline != Pipeline::OUTPUT*/)) {
            show_mask_idx = -1;
        }
        std::vector<array2D<float>> mask(n);
        if (!generateMasks(rgb, "smoothing", linked_mask_mgr_, params->smoothing.masks, offset_x, offset_y, full_width, full_height, scale, multiThread, show_mask_idx, nullptr, &mask, cur_pipeline == Pipeline::NAVIGATOR ? plistener : nullptr)) {
            return true; // show mask is active, nothing more to do
        }

        const int W = rgb->getWidth();
        const int H = rgb->getHeight();

        Imagefloat working;
        rgb->setMode(Imagefloat::Mode::RGB, multiThread);
        rgb->normalizeFloatTo1(multiThread);

        TMatrix ws = ICCStore::getInstance()->workingSpaceMatrix(params->icm.workingProfile);
        TMatrix iws = ICCStore::getInstance()->workingSpaceInverseMatrix(params->icm.workingProfile);

        for (int i = 0; i < n; ++i) {
            if (!params->smoothing.masks[i].enabled) {
                continue;
            }

            int min_x, min_y, max_x, max_y;
            const auto &blend = mask[i];

            find_region(blend, min_x, min_y, max_x, max_y);
            int ww = max_x - min_x;
            int hh = max_y - min_y;

            auto &r = params->smoothing.regions[i];
            
            if (ww * hh < (W * H) / 2 && !(r.mode == SmoothingParams::Region::Mode::LENS || r.mode == SmoothingParams::Region::Mode::MOTION)) {
                working.allocate(ww, hh);
#ifdef _OPENMP
#               pragma omp parallel for if (multiThread)
#endif
                for (int y = min_y; y < max_y; ++y) {
                    int yy = y - min_y;
                    for (int x = min_x; x < max_x; ++x) {
                        int xx = x - min_x;
                        working.r(yy, xx) = rgb->r(y, x);
                        working.g(yy, xx) = rgb->g(y, x);
                        working.b(yy, xx) = rgb->b(y, x);
                    }
                }
            } else {
                min_x = 0;
                min_y = 0;
                max_x = W;
                max_y = H;
                ww = W;
                hh = H;

                rgb->copyTo(&working);
            }

            const int flags = r.mode == SmoothingParams::Region::Mode::WAVELETS ? 0 : ARRAY2D_BYREFERENCE;
            array2D<float> R(ww, hh, working.r.ptrs, flags);
            array2D<float> G(ww, hh, working.g.ptrs, flags);
            array2D<float> B(ww, hh, working.b.ptrs, flags);

            const bool glow = r.mode == SmoothingParams::Region::Mode::GAUSSIAN_GLOW;
            Channel ch = glow ? Channel::LC : Channel(int(r.channel));
            if (r.mode == SmoothingParams::Region::Mode::NOISE) {
                if (cur_pipeline == Pipeline::OUTPUT || cur_pipeline == Pipeline::PREVIEW) {
                    add_noise(R, G, B, ws, r.noise_strength, r.noise_coarseness, scale, ch, multiThread);
                }
            } else if (r.mode == SmoothingParams::Region::Mode::NLMEANS) {
                nlmeans_smoothing(R, G, B, ws, iws, ch, r.nlstrength, r.nldetail, r.iterations, scale, multiThread);
            } else if (r.mode == SmoothingParams::Region::Mode::LENS || r.mode == SmoothingParams::Region::Mode::MOTION) {
                ImProcData im(params, scale, multiThread);
                lens_motion_blur(im, &working, blend, i);
            } else if (r.mode == SmoothingParams::Region::Mode::HALATION) {
                halation(R, G, B, 50 * r.halation_size / scale, LIM01(r.halation_color+0.5), multiThread);
            } else if (r.mode == SmoothingParams::Region::Mode::WAVELETS) {
                wavelet_smoothing(R, G, B, ws, r.wav_strength, r.wav_levels, r.wav_gamma, scale, ch, multiThread);
#ifdef _OPENMP
#               pragma omp parallel for if (multiThread)
#endif
                for (int y = 0; y < R.height(); ++y) {
                    for (int x = 0; x < R.width(); ++x) {
                        working.r(y, x) = R[y][x];
                        working.g(y, x) = G[y][x];
                        working.b(y, x) = B[y][x];
                    }
                }
            } else if (r.mode != SmoothingParams::Region::Mode::GUIDED) {
                AlignedBuffer<float> buf(ww * hh);
                double sigma = r.sigma;
                for (int i = 0; i < r.iterations; ++i) {
                    gaussian_smoothing(R, G, B, buf.data, ws, ch, sigma, scale, multiThread);
                    if (glow) {
                        sigma *= 1.5;
                        float f = pow_F(r.falloff, i);
                        float f2 = 1.f + 1.f/f;

#ifdef _OPENMP
#                       pragma omp parallel for if (multiThread)
#endif
                        for (int y = min_y; y < max_y; ++y) {
                            int yy = y - min_y;
                            for (int x = min_x; x < max_x; ++x) {
                                int xx = x - min_x;
                                float &r = R[yy][xx];
                                float &g = G[yy][xx];
                                float &b = B[yy][xx];
                                r = (rgb->r(y, x) + r / f) / f2;
                                g = (rgb->g(y, x) + g / f) / f2;
                                b = (rgb->b(y, x) + b / f) / f2;
                            }
                        }
                    }
                }
            } else {
                const float epsilon = std::max(0.001f * std::pow(2, -r.epsilon), 1e-6);
                int radius = r.radius;
                for (int i = 0; i < r.iterations; ++i) {
                    guided_smoothing(R, G, B, ws, iws, ch, radius, epsilon, scale, multiThread);
                }
            }
            
#ifdef _OPENMP
#           pragma omp parallel for if (multiThread)
#endif
            for (int y = min_y; y < max_y; ++y) {
                int yy = y - min_y;
                for (int x = min_x; x < max_x; ++x) {
                    int xx = x - min_x;
                    float r = rgb->r(y, x);
                    float g = rgb->g(y, x);
                    float b = rgb->b(y, x);
                    float wr = working.r(yy, xx);
                    float wg = working.g(yy, xx);
                    float wb = working.b(yy, xx);
                    rgb->r(y, x) = intp(blend[y][x], wr, r);
                    rgb->g(y, x) = intp(blend[y][x], wg, g);
                    rgb->b(y, x) = intp(blend[y][x], wb, b);
                }
            }
        }

        rgb->normalizeFloatTo65535(multiThread);
    } else if (editWhatever) {
        editWhatever->fill(0.f);
    }

    return false;
}

} // namespace rtengine
