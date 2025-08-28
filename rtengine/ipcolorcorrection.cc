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
#include "guidedfilter.h"
//#define BENCHMARK
#include "StopWatch.h"
#include "sleef.h"
#include "coord.h"
#include "gauss.h"
#include "masks.h"
#include "clutstore.h"
#include "../rtgui/multilangmgr.h"


namespace rtengine {

bool ImProcFunctions::colorCorrection(Imagefloat *rgb)
{
    BENCHFUN
        
    PlanarWhateverData<float> *editWhatever = nullptr;
    Imagefloat *imgbuf = nullptr;
    EditUniqueID eid = pipetteBuffer ? pipetteBuffer->getEditID() : EUID_None;

    const int H = rgb->getHeight();
    const int W = rgb->getWidth();
        
    if ((eid == EUID_LabMasks_H1 || eid == EUID_LabMasks_C1 || eid == EUID_LabMasks_L1) && pipetteBuffer->getDataProvider()->getCurrSubscriber()->getPipetteBufferType() == BT_SINGLEPLANE_FLOAT) {
        editWhatever = pipetteBuffer->getSinglePlaneBuffer();
    }

    if (eid == EUID_LabMasks_DE1) {
        if (getDeltaEColor(rgb, deltaE.x, deltaE.y, offset_x, offset_y, full_width, full_height, scale, deltaE.L, deltaE.C, deltaE.H)) {
            deltaE.ok = true;
        }
    }

    if ((eid == EUID_ColorCorrection_Wheel || eid == EUID_ColorCorrection_Wheel_Jzazbz) && pipetteBuffer->getDataProvider()->getCurrSubscriber()->getPipetteBufferType() == BT_IMAGEFLOAT) {
        imgbuf = pipetteBuffer->getImgFloatBuffer();
    }
    
    if (!params->colorcorrection.enabled) {
        if (editWhatever) {
            editWhatever->fill(0.f);
        }
        if (imgbuf) {
#ifdef _OPENMP
#           pragma omp parallel for if (multiThread)
#endif
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    imgbuf->g(y, x) = 0.f;
                    imgbuf->r(y, x) = 0.f;
                    imgbuf->b(y, x) = 0.f;
                }
            }
        }
        return false;
    }

    if (editWhatever) {
        MasksEditID id = static_cast<MasksEditID>(int(eid) - EUID_LabMasks_H1);
        fillPipetteMasks(rgb, editWhatever, id, multiThread);
    }
    
    const auto abcoord =
        [](float x) -> float
        {
            return SGN(x) * xlog2lin(std::abs(x), 4.f);
        };

    TMatrix dws = ICCStore::getInstance()->workingSpaceMatrix(params->icm.workingProfile);
    float ws[3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            ws[i][j] = dws[i][j];
        }
    }

    TMatrix diws = ICCStore::getInstance()->workingSpaceInverseMatrix(params->icm.workingProfile);
    float iws[3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            iws[i][j] = diws[i][j];
        }
    }
        
    const auto hs2uv =
        [&](float h, float s, float &u, float &v) -> void
        {
            if (h < 0.f) {
                h += 1.f;
            } else if (h > 1.f) {
                h -= 1.f;
            }
            float R, G, B;
            Color::hsl2rgb(h, s, 0.5f, R, G, B);
            R /= 65535.f;
            G /= 65535.f;
            B /= 65535.f;
            float Y;
            Color::rgb2yuv(R, G, B, Y, u, v, ws);
            float s2;
            Color::yuv2hsl(u, v, h, s2);
            Color::hsl2yuv(h, s, u, v);
        };
    
    const auto abcoord2 =
        [&](float x, float y, float &a, float &b) -> void
        {
            x = abcoord(x);
            y = abcoord(y);
            float h = atan2(y, x) / (2.f * RT_PI_F);
            float s = std::sqrt(SQR(x) + SQR(y));
            float u, v;
            hs2uv(h, s, u, v);
            a = v;
            b = u;
        };

    const auto yuv2jzazbz =
        [&](float &Y, float &u, float &v) -> void
        {
            float R, G, B;
            Color::yuv2rgb(Y, u, v, R, G, B, ws);
            Color::rgb2jzazbz(R / 65535.f, G / 65535.f, B / 65535.f, Y, v, u, ws);
        };

    const auto jzazbz2yuv =
        [&](float &Jz, float &bz, float &az) -> void
        {
            float R, G, B;
            Color::jzazbz2rgb(Jz, az, bz, R, G, B, iws);
            Color::rgb2yuv(R * 65535.f, G * 65535.f, B * 65535.f, Jz, bz, az, ws);
        };

    const auto yuv2hsl =
        [&](float Y, float u, float v, float &h, float &s, float &l) -> void
        {
            float R, G, B;
            Color::yuv2rgb(Y, u, v, R, G, B, ws);
            Color::rgb2hsl(R, G, B, h, s, l);
            h *= 2.f * RT_PI_F;
        };

    const auto hsl2yuv =
        [&](float h, float s, float l, float &Y, float &u, float &v) -> void
        {
            h /= (2.f * RT_PI_F);
            if (h < 0.f) {
                h += 1.f;
            } else if (h > 1.f) {
                h -= 1.f;
            }
            float R, G, B;
            Color::hsl2rgb(h, s, l, R, G, B);
            Color::rgb2yuv(R, G, B, Y, u, v, ws);
        };
    
    if (imgbuf) {
        const auto translate_uv =
            [&](float &u, float &v) -> void
            {
                float os = std::sqrt(SQR(u) + SQR(v));
                float R, G, B;
                const float Y = 0.5f;
                Color::yuv2rgb(Y, u, v, R, G, B, ws);
                float h, s, l;
                Color::rgb2hsl(R * 65535.f, G * 65535.f, B * 65535.f, h, s, l);
                h = h * 2.f * RT_PI_F;
                Color::hsl2yuv(h, os, u, v);
            };
        
        const bool jzazbz = eid == EUID_ColorCorrection_Wheel_Jzazbz;
        rgb->setMode(Imagefloat::Mode::YUV, multiThread);
#ifdef _OPENMP
#           pragma omp parallel for if (multiThread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                imgbuf->g(y, x) = 0.f;
                float f = std::max(rgb->g(y, x), 0.f);
                float u = rgb->b(y, x);
                float v = rgb->r(y, x);
                if (jzazbz) {
                    yuv2jzazbz(f, u, v);
                    f *= 65535.f;
                    u *= 65535.f;
                    v *= 65535.f;
                }
                if (f > 1e-5f) {
                    u /= f;
                    v /= f;
                } else {
                    u = 0.f;
                    v = 0.f;
                }
                translate_uv(u, v);
                imgbuf->r(y, x) = SGN(v) * xlin2log(std::abs(v), 4.f);
                imgbuf->b(y, x) = SGN(u) * xlin2log(std::abs(u), 4.f);
            }
        }
    }

    const int n = params->colorcorrection.regions.size();
    int show_mask_idx = params->colorcorrection.showMask;
    if (show_mask_idx >= n || (cur_pipeline != Pipeline::PREVIEW /*&& cur_pipeline != Pipeline::OUTPUT*/)) {
        show_mask_idx = -1;
    }

    std::vector<array2D<float>> abmask(n);
    std::vector<array2D<float>> Lmask(n);

    if (!generateMasks(rgb, "colorcorrection", linked_mask_mgr_, params->colorcorrection.labmasks, offset_x, offset_y, full_width, full_height, scale, multiThread, show_mask_idx, &Lmask, &abmask, cur_pipeline == Pipeline::NAVIGATOR ? plistener : nullptr)) {
        return true; // show mask is active, nothing more to do
    }
    
    float abca[n];
    float abcb[n];
    float rs[n];
    float rsout[n];
    float rslope[n][3];
    float roffset[n][3];
    float rpower[n][3];
    float rpivot[n][3];
    float rcompression[n][3][2];
    char rgbmode[n];
    bool enabled[n];
    bool jzazbz[n];
    bool hsl[n];
    float rhs[n];
    std::unique_ptr<CLUTApplication> lut[n];
    float hslgamma[n];

    const auto reset =
        [&](int i) -> void
        {
            abca[i] = 0.f;
            abcb[i] = 0.f;
            rs[i] = 1.f;
            rsout[i] = 1.f;
            enabled[i] = false;
            jzazbz[i] = false;
            hsl[i] = false;
            rhs[i] = 0.f;
            lut[i].reset(nullptr);
            for (int j = 0; j < 3; ++j) {
                rslope[i][j] = 1.f;
                roffset[i][j] = 0.f;
                rpower[i][j] = 1.f;
                rpivot[i][j] = 1.f;
                rcompression[i][j][0] = 0.f;
                rcompression[i][j][1] = 0.f;
            }
            hslgamma[i] = 1.f;
        };
    
    for (int i = 0; i < n; ++i) {
        reset(i);
        
        auto &r = params->colorcorrection.regions[i];
        rgbmode[i] = int(r.mode != ColorCorrectionParams::Mode::YUV &&
                         r.mode != ColorCorrectionParams::Mode::JZAZBZ);
        for (int c = 0; c < 3; ++c) {
            rcompression[i][c][0] = rcompression[i][c][1] = 0;
        }
        if (rgbmode[i]) {
            if (r.rgbluminance) {
                rgbmode[i] = 2;
            }
            abca[i] = 0.f;
            abcb[i] = 0.f;
            jzazbz[i] = false;
            hsl[i] = r.mode == ColorCorrectionParams::Mode::HSL;
        } else {
            jzazbz[i] = r.mode == ColorCorrectionParams::Mode::JZAZBZ;
            hsl[i] = false;
            abcoord2(r.a, r.b, abca[i], abcb[i]);
        }
        rs[i] = 1.f + r.inSaturation / 100.f;
        rsout[i] = 1.f + r.outSaturation / 100.f;
        enabled[i] = false;
        if (r.mode == ColorCorrectionParams::Mode::HSL) {
            float R, G, B;
            float u, v;
            float satcoeff[] = { 2.5f, 2.5f, 2.5f };
            for (int c = 0; c < 3; ++c) {
                float hue = (float(r.hue[c]) / 180.f) * RT_PI_F;
                float sat = std::pow(float(r.sat[c]) / 100.f, satcoeff[c]);
                float f = (r.factor[c] / 100.f) + 1.f;
                //Color::hsl2yuv(hue, sat, u, v);
                hs2uv(hue / (2 * RT_PI_F), sat, u, v);
                Color::yuv2rgb(0.5f, u, v, R, G, B, ws);
                R *= 2.f;
                G *= 2.f;
                B *= 2.f;
                switch (c) {
                case 0: // SLOPE
                    rslope[i][0] = R * f;
                    rslope[i][1] = G * f;
                    rslope[i][2] = B * f;
                    break;
                case 1: // OFFSET
                    roffset[i][0] = R + f - 2.f;
                    roffset[i][1] = G + f - 2.f;
                    roffset[i][2] = B + f - 2.f;
                    break;
                default: // POWER
                    rpower[i][0] = (2.f - R) * (2.f - f);
                    rpower[i][1] = (2.f - G) * (2.f - f);
                    rpower[i][2] = (2.f - B) * (2.f - f);
                    break;
                }
                rpivot[i][c] = 1.f;
            }
            for (int c = 0; c < 3; ++c) {
                if (rslope[i][c] != 1.f || roffset[i][c] != 0.f || rpower[i][c] != 1.f) {
                    enabled[i] = true;
                }
            }
            hslgamma[i] = r.hsl_gamma;
        } else {
            for (int c = 0; c < 3; ++c) {
                int j = rgbmode[i] ? c : 0;
                rslope[i][c] = r.slope[j];
                roffset[i][c] = r.offset[j];
                rpower[i][c] = 1.0 / r.power[j];
                rpivot[i][c] = r.pivot[j];
                auto compr = r.compression[j] * 100.0;
                if (compr > 0) {
                    rcompression[i][c][0] = compr;
                    double y0 = std::pow((rslope[i][c] + roffset[i][c])/rpivot[i][c], rpower[i][c]) * rpivot[i][c];
                    rcompression[i][c][1] = std::log(1.0 + y0 * compr) / rslope[i][c];
                } else {
                    rcompression[i][c][0] = rcompression[i][c][1] = 0;
                }
                if (rslope[i][c] != 1.f || roffset[i][c] != 0.f || rpower[i][c] != 1.f || rcompression[i][c][1] != 0.f) {
                    enabled[i] = true;
                }
            }
        }
        if (r.mode != ColorCorrectionParams::Mode::RGB) {
            rhs[i] = r.hueshift * RT_PI_F_180;
        } else {
            rhs[i] = 0.f;
        }

        lut[i].reset(nullptr);
        if (r.mode == ColorCorrectionParams::Mode::LUT) {
            reset(i);
#ifdef _OPENMP
            int num_threads = multiThread ? omp_get_max_threads() : 1;
#else
            int num_threads = 1;
#endif
            CLUTApplication::Quality q = CLUTApplication::Quality::HIGHEST;
            switch (cur_pipeline) {
            case Pipeline::THUMBNAIL:
                q = CLUTApplication::Quality::LOW;
                break;
            case Pipeline::NAVIGATOR:
                q = CLUTApplication::Quality::MEDIUM;
                break;
            case Pipeline::PREVIEW:
                if (scale > 1) {
                    q = CLUTApplication::Quality::HIGH;
                }
                break;
            default:
                break;
            }
            if (!r.lutFilename.empty()) {
                lut[i].reset(new CLUTApplication(r.lutFilename, params->icm.workingProfile, 1.f, num_threads));
                if (!(*lut[i])) {
                    lut[i].reset(nullptr);
                    if (plistener) {
                        plistener->error(Glib::ustring::compose(M("TP_COLORCORRECTION_LABEL") + " - " + M("ERROR_MSG_FILE_READ"), r.lutFilename.empty() ? "(" + M("GENERAL_NONE") + ")" : r.lutFilename));
                    }
                } else if (!lut[i]->set_param_values(r.lut_params, q)) {
                    lut[i].reset(nullptr);
                    if (plistener) {
                        plistener->error(Glib::ustring::compose(M("TP_COLORCORRECTION_LABEL") + " - " + M("ERROR_MSG_INVALID_LUT_PARAMS"), r.lutFilename));
                    }
                }
            }
        }
    }

    const float max_ws = max(ws[1][0], ws[1][1], ws[1][2]);
    const float fR = max_ws / ws[1][0];
    const float fG = max_ws / ws[1][1];
    const float fB = max_ws / ws[1][2];

    const auto CDL = 
        [&](int region, float &Y, float &u, float &v) -> void
        {
            const float *slope = rslope[region];
            const float *offset = roffset[region];
            const float *power = rpower[region];
            const float *pivot = rpivot[region];
            const float saturation = rs[region];
            const float hueshift = rhs[region];
            const float outsaturation = rsout[region];
            const auto *compression = rcompression[region];

            if (hueshift != 0.f) {
                float h, s;
                if (hsl[region]) {
                    float l;
                    yuv2hsl(Y, u, v, h, s, l);
                    h += hueshift;
                    hsl2yuv(h, s, l, Y, u, v);
                } else {
                    if (jzazbz[region]) {
                        yuv2jzazbz(Y, u, v);
                    }
                    Color::yuv2hsl(u, v, h, s);
                    h += hueshift;
                    Color::hsl2yuv(h, s, u, v);
                    if (jzazbz[region]) {
                        jzazbz2yuv(Y, u, v);
                    }
                }
            }

            if (rgbmode[region]) {
                if (saturation != 1.f) {
                    u *= saturation;
                    v *= saturation;
                }
                if (enabled[region]) {
                    float rgb[3];
                    Color::yuv2rgb(Y, u, v, rgb[0], rgb[1], rgb[2], ws);
                    const bool use_gamma = hsl[region] && hslgamma[region] != 1.f;
                    for (int i = 0; i < 3; ++i) {
                        float v = (rgb[i] / 65535.f);
                        if (use_gamma && v > 0.f) {
                            v = pow_F(v, 1.f / hslgamma[region]);
                        }
                        v = v * slope[i] + offset[i]/2.f;
                        if (v > 0.f) {
                            if (pivot[i] != 1.f) {
                                v = pow_F(v / pivot[i], power[i]) * pivot[i];
                            } else {
                                v = pow_F(v, power[i]);
                            }
                            if (compression[i][0] != 0.f) {
                                v = xlogf(v * compression[i][0] + 1.f) / compression[i][1];
                            }
                        } else {
                            v = 0.f;
                        }
                        if (use_gamma && v > 0.f) {
                            v = pow_F(v, hslgamma[region]);
                        }
                        rgb[i] = v * 65535.f;
                    }
                    if (rgbmode[region] != 2) {
                        Color::rgb2yuv(rgb[0], rgb[1], rgb[2], Y, u, v, ws);
                    } else {
                        float rr, gg, bb;
                        Color::yuv2rgb(Y, u, v, rr, gg, bb, ws);
                        float Y1 = Color::rgbLuminance(rr + (rgb[0] - rr) * fR,
                                                       gg + (rgb[1] - gg) * fG,
                                                       bb + (rgb[2] - bb) * fB,
                                                       ws);
                        if (Y > 0.f) {
                            float f = Y1 / Y;
                            u *= f;
                            v *= f;
                        }
                        Y = Y1;
                    }
                }

                float f = max(Y, 0.f);
                u += f * abcb[region];
                v += f * abca[region];

                if (outsaturation != 1.f) {
                    u *= outsaturation;
                    v *= outsaturation;
                }
            } else {
                if (enabled[region]) {
                    float YY = (Y / 65535.f) * slope[0] + offset[0]/2.f;
                    if (YY > 0.f) {
                        if (pivot[0] != 1.f) {
                            YY = pow_F(YY / pivot[0], power[0]) * pivot[0];
                        } else {
                            YY = pow_F(YY, power[0]);
                        }
                        if (compression[0][0] != 0.f) {
                            YY = xlogf(YY * compression[0][0] + 1.f) / compression[0][1];
                        }
                        YY *= 65535.f;
                    } else {
                        YY = 0.f;
                    }
                    if (Y > 0.f) {
                        float f = YY / Y;
                        Y = YY;
                        u *= f;
                        v *= f;
                    } else {
                        Y = YY;
                    }
                }

                if (jzazbz[region]) {
                    yuv2jzazbz(Y, u, v);
                }
                    
                if (saturation != 1.f) {
                    u *= saturation;
                    v *= saturation;
                }

                float f = max(Y, 0.f);
                u += f * abcb[region];
                v += f * abca[region];

                if (outsaturation != 1.f) {
                    u *= outsaturation;
                    v *= outsaturation;
                }

                if (jzazbz[region]) {
                    jzazbz2yuv(Y, u, v);
                }
            }
        };

#ifdef __SSE2__
    vfloat vws[3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            vws[i][j] = F2V(float(dws[i][j]));
        }
    }

    const vfloat vfR = F2V(fR);
    const vfloat vfG = F2V(fG);
    const vfloat vfB = F2V(fB);

    vfloat vabcb[n];
    vfloat vabca[n];
    vfloat vrsout[n];
    for (int i = 0; i < n; ++i) {
        vabcb[i] = F2V(abcb[i]);
        vabca[i] = F2V(abca[i]);
        vrsout[i] = F2V(rsout[i]);
    }

    const auto vyuv2jzazbz =
        [&](vfloat &Y, vfloat &u, vfloat &v) -> void
        {
            float Yk, uk, vk;
            for (int k = 0; k < 4; ++k) {
                Yk = Y[k];
                uk = u[k];
                vk = v[k];
                yuv2jzazbz(Yk, uk, vk);
                Y[k] = Yk;
                u[k] = uk;
                v[k] = vk;
            }
        };

    const auto vjzazbz2yuv =
        [&](vfloat &Jz, vfloat &az, vfloat &bz) -> void
        {
            float Jk, ak, bk;
            for (int k = 0; k < 4; ++k) {
                Jk = Jz[k];
                ak = az[k];
                bk = bz[k];
                jzazbz2yuv(Jk, ak, bk);
                Jz[k] = Jk;
                az[k] = ak;
                bz[k] = bk;
            }
        };

    vfloat v65535 = F2V(65535.f);
    vfloat v1 = F2V(1.f);
    
    const auto CDL_v =
        [&](int region, vfloat &Y, vfloat &u, vfloat &v) -> void
        {
            const float *slope = rslope[region];
            const float *offset = roffset[region];
            const float *power = rpower[region];
            const float *pivot = rpivot[region];
            const float saturation = rs[region];
            const float hueshift = rhs[region];
            const float outsaturation = rsout[region];
            const auto *compression = rcompression[region];

            if (hueshift != 0.f) {
                float h, s, uk, vk;
                if (hsl[region]) {
                    float l, Yk;
                    for (int k = 0; k < 4; ++k) {
                        yuv2hsl(Y[k], u[k], v[k], h, s, l);
                        h += hueshift;
                        hsl2yuv(h, s, l, Yk, uk, vk);
                        Y[k] = Yk;
                        u[k] = uk;
                        v[k] = vk;
                    }
                } else {
                    if (jzazbz[region]) {
                        vyuv2jzazbz(Y, u, v);
                    }
                    for (int k = 0; k < 4; ++k) {
                        Color::yuv2hsl(u[k], v[k], h, s);
                        h += hueshift;
                        Color::hsl2yuv(h, s, uk, vk);
                        u[k] = uk;
                        v[k] = vk;
                    }
                    if (jzazbz[region]) {
                        vjzazbz2yuv(Y, u, v);
                    }
                }
            }
                
            if (rgbmode[region]) {
                if (saturation != 1.f) {
                    vfloat vsaturation = F2V(saturation);
                    u *= vsaturation;
                    v *= vsaturation;
                }
                if (enabled[region]) {
                    vfloat rgb[3];
                    Color::yuv2rgb(Y, u, v, rgb[0], rgb[1], rgb[2], vws);
                    const bool use_gamma = hsl[region] && hslgamma[region] != 1.f;
                    vfloat vgamma = F2V(1.f/ hslgamma[region]);
                    vfloat vigamma = F2V(hslgamma[region]);
                    
                    for (int i = 0; i < 3; ++i) {
                        vfloat vslope = F2V(slope[i]);
                        vfloat voffset = F2V(offset[i] / 2.f);
                        vfloat vpower = F2V(power[i]);
                        vfloat vpivot = F2V(pivot[i]);

                        vfloat v = (rgb[i] / v65535);
                        if (use_gamma) {
                            v = vself(vmaskf_gt(v, ZEROV), pow_F(v, vgamma), v);
                        }
                        v = v * vslope + voffset;
                        if (pivot[i] != 1.f) {
                            v = vself(vmaskf_gt(v, ZEROV), pow_F(v / vpivot, vpower) * vpivot, ZEROV);
                        } else {
                            v = vself(vmaskf_gt(v, ZEROV), pow_F(v, vpower), ZEROV);
                        }
                        if (compression[i][0] != 0.f) {
                            vfloat vc0 = F2V(compression[i][0]);
                            vfloat vc1 = F2V(compression[i][1]);
                            v = vmaxf(v, ZEROV);
                            v = xlogf(v * vc0 + v1) / vc1;
                        }
                        if (use_gamma) {
                            v = vself(vmaskf_gt(v, ZEROV), pow_F(v, vigamma), v);
                        }
                        rgb[i] = v * v65535;
                    }
                    if (rgbmode[region] != 2) {
                        Color::rgb2yuv(rgb[0], rgb[1], rgb[2], Y, u, v, vws);
                    } else {
                        vfloat rr, gg, bb;
                        Color::yuv2rgb(Y, u, v, rr, gg, bb, vws);
                        vfloat Y1 = Color::rgbLuminance(rr + (rgb[0] - rr) * vfR,
                                                        gg + (rgb[1] - gg) * vfG,
                                                        bb + (rgb[2] - bb) * vfB,
                                                        vws);
                        for (int k = 0; k < 4; ++k) {
                            if (Y[k] > 0.f) {
                                float f = Y1[k] / Y[k];
                                u[k] *= f;
                                v[k] *= f;
                            }
                        }
                        Y = Y1;
                    }
                }

                vfloat fv = vmaxf(Y, ZEROV);
                u += fv * vabcb[region];
                v += fv * vabca[region];

                if (outsaturation != 1.f) {
                    u *= vrsout[region];
                    v *= vrsout[region];
                }
            } else {
                if (enabled[region]) {
                    vfloat vslope = F2V(slope[0]);
                    vfloat voffset = F2V(offset[0] / 2.f);
                    vfloat vpower = F2V(power[0]);
                    vfloat vpivot = F2V(pivot[0]);
                    vfloat YY = (Y / v65535) * vslope + voffset;
                    if (pivot[0] != 1.f) {
                        YY = vself(vmaskf_gt(YY, ZEROV), pow_F(YY / vpivot, vpower) * vpivot, ZEROV);
                    } else {
                        YY = vself(vmaskf_gt(YY, ZEROV), pow_F(YY, vpower), ZEROV);
                    }
                    if (compression[0][0] != 0.f) {
                        vfloat vc0 = F2V(compression[0][0]);
                        vfloat vc1 = F2V(compression[0][1]);
                        YY = vmaxf(YY, ZEROV);
                        YY = xlogf(YY * vc0 + v1) / vc1;
                    }
                    YY *= v65535;
                    vfloat f = vself(vmaskf_gt(Y, ZEROV), YY / Y, v1);
                    Y = YY;
                    u *= f;
                    v *= f;
                }

                if (jzazbz[region]) {
                    vyuv2jzazbz(Y, u, v);
                }
                    
                if (saturation != 1.f) {
                    vfloat vsaturation = F2V(saturation);
                    u *= vsaturation;
                    v *= vsaturation;
                }

                vfloat fv = vmaxf(Y, ZEROV);
                u += fv * vabcb[region];
                v += fv * vabca[region];

                if (outsaturation != 1.f) {
                    u *= vrsout[region];
                    v *= vrsout[region];
                }

                if (jzazbz[region]) {
                    vjzazbz2yuv(Y, u, v);
                }
            }
        };
#endif // __SSE2__

    rgb->setMode(Imagefloat::Mode::YUV, multiThread);

#ifdef _OPENMP
    #pragma omp parallel if (multiThread)
#endif
    {
        AlignedBuffer<float> rbuf(W);
        AlignedBuffer<float> gbuf(W);
        AlignedBuffer<float> bbuf(W);
        
#ifdef _OPENMP
        #pragma omp for
#endif
        for (int y = 0; y < H; ++y) {
            for (int i = 0; i < n; ++i) {
                if (!params->colorcorrection.labmasks[i].enabled) {
                    continue;
                }
                if (lut[i]) {
                    for (int x = 0; x < W; ++x) {
                        Color::yuv2rgb(rgb->g(y, x), rgb->b(y, x), rgb->r(y, x), rbuf.data[x], gbuf.data[x], bbuf.data[x], ws);
                    }
#ifdef _OPENMP
                    int thread_id = omp_get_thread_num();
#else
                    int thread_id = 0;
#endif
                    lut[i]->apply(thread_id, W, rbuf.data, gbuf.data, bbuf.data);
                    for (int x = 0; x < W; ++x) {
                        float Y, u, v;
                        Color::rgb2yuv(rbuf.data[x], gbuf.data[x], bbuf.data[x], Y, u, v, ws);
                        float blend = abmask[i][y][x];
                        float lblend = Lmask[i][y][x];
                        rgb->g(y, x) = intp(lblend, Y, rgb->g(y, x));
                        rgb->b(y, x) = intp(blend, u, rgb->b(y, x));
                        rgb->r(y, x) = intp(blend, v, rgb->r(y, x));
                    }
                } else {
                    int x = 0;
#ifdef __SSE2__
                    for (; x < W - 3; x += 4) {
                        vfloat Yv = LVF(rgb->g(y, x));
                        vfloat uv = LVF(rgb->b(y, x));
                        vfloat vv = LVF(rgb->r(y, x));

                        vfloat blendv = LVFU(abmask[i][y][x]);
                        vfloat lblendv = LVFU(Lmask[i][y][x]);

                        vmask some_blend = vmaskf_gt(blendv, ZEROV);
                        vmask some_lblend = vmaskf_gt(lblendv, ZEROV);
                        if (_mm_movemask_ps((vfloat)some_blend) || _mm_movemask_ps((vfloat)some_lblend)) {
                            vfloat Y_newv = Yv;
                            vfloat u_newv = uv;
                            vfloat v_newv = vv;

                            CDL_v(i, Y_newv, u_newv, v_newv);
                    
                            Yv = vintpf(lblendv, Y_newv, Yv);
                            uv = vintpf(blendv, u_newv, uv);
                            vv = vintpf(blendv, v_newv, vv);
                        }
                        STVF(rgb->g(y, x), Yv);
                        STVF(rgb->b(y, x), uv);
                        STVF(rgb->r(y, x), vv);
                    }
#endif // __SSE2__
                    for (; x < W; ++x) {
                        float Y = rgb->g(y, x);
                        float u = rgb->b(y, x);
                        float v = rgb->r(y, x);

                        float blend = abmask[i][y][x];
                        float lblend = Lmask[i][y][x];

                        if (blend > 0.f || lblend > 0.f) {
                            float Y_new = Y;
                            float u_new = u;
                            float v_new = v;

                            CDL(i, Y_new, u_new, v_new);
                    
                            Y = intp(lblend, Y_new, Y);
                            u = intp(blend, u_new, u);
                            v = intp(blend, v_new, v);
                        }

                        rgb->g(y, x) = Y;
                        rgb->b(y, x) = u;
                        rgb->r(y, x) = v;
                    }
                }
            }
        }
    }

    return false;
}

} // namespace rtengine
