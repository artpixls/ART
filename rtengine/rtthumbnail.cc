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
#include "rtengine.h"
#include "rtthumbnail.h"
#include "../rtgui/options.h"
#include "image8.h"
#include <lcms2.h>
#include "curves.h"
#include <glibmm.h>
#include "improcfun.h"
#include "colortemp.h"
#include "mytime.h"
#include "utils.h"
#include "iccstore.h"
#include "iccmatrices.h"
#include "rawimagesource.h"
#include "stdimagesource.h"
#include <glib/gstdio.h>
#include "rawimage.h"
#include "rtjpeg.h"
#include "../rtgui/ppversion.h"
#include "improccoordinator.h"
#include "settings.h"
#include <locale.h>
#include "median.h"
#define BENCHMARK
#include "StopWatch.h"
#include "clutstore.h"

namespace {

void scale_colors (rtengine::RawImage *ri, float scale_mul[4], float cblack[4], bool multiThread)
{
    rtengine::RawImage::ImageType image = ri->get_image();
    const int height = ri->get_iheight();
    const int width = ri->get_iwidth();
    const int top_margin = ri->get_topmargin();
    const int left_margin = ri->get_leftmargin();
    const int raw_width = ri->get_rawwidth();
    const bool isFloat = ri->isFloat();
    const float * const float_raw_image = ri->get_FloatRawImage();

    if (ri->isBayer()) {
#ifdef _OPENMP
        #pragma omp parallel for if(multiThread)
#endif
        for (int row = 0; row < height; ++row) {
            unsigned c0 = ri->FC (row, 0);
            unsigned c1 = ri->FC (row, 1);
            int col = 0;

            for (; col < width - 1; col += 2) {
                float val0;
                float val1;
                if (isFloat) {
                    val0 = float_raw_image[(row + top_margin) * raw_width + col + left_margin];
                    val1 = float_raw_image[(row + top_margin) * raw_width + col + left_margin + 1];
                } else {
                    val0 = image[row * width + col][c0];
                    val1 = image[row * width + col + 1][c1];
                }
                val0 -= cblack[c0];
                val1 -= cblack[c1];
                val0 *= scale_mul[c0];
                val1 *= scale_mul[c1];
                image[row * width + col][c0] = rtengine::CLIP (val0);
                image[row * width + col + 1][c1] = rtengine::CLIP (val1);
            }

            if (col < width) { // in case width is odd
                float val0;
                if (isFloat) {
                    val0 = float_raw_image[(row + top_margin) * raw_width + col + left_margin];
                } else {
                    val0 = image[row * width + col][c0];
                }
                val0 -= cblack[c0];
                val0 *= scale_mul[c0];
                image[row * width + col][c0] = rtengine::CLIP (val0);
            }
        }
    } else if (ri->isXtrans()) {
#ifdef _OPENMP
        #pragma omp parallel for if(multiThread)
#endif
        for (int row = 0; row < height; ++row) {
            unsigned c[6];
            for (int i = 0; i < 6; ++i) {
                c[i] = ri->XTRANSFC (row, i);
            }

            int col = 0;

            for (; col < width - 5; col += 6) {
                for (int i = 0; i < 6; ++i) {
                    const unsigned ccol = c[i];
                    float val;
                    if (isFloat) {
                        val = float_raw_image[(row + top_margin) * raw_width + col + i + left_margin];
                    } else {
                        val = image[row * width + col + i][ccol];
                    }
                    val -= cblack[ccol];
                    val *= scale_mul[ccol];
                    image[row * width + col + i][ccol] = rtengine::CLIP (val);
                }
            }

            for (; col < width; ++col) { // remaining columns
                const unsigned ccol = ri->XTRANSFC (row, col);
                float val;
                if (isFloat) {
                    val = float_raw_image[(row + top_margin) * raw_width + col + left_margin];
                } else {
                    val = image[row * width + col][ccol];
                }
                val -= cblack[ccol];
                val *= scale_mul[ccol];
                image[row * width + col][ccol] = rtengine::CLIP (val);
            }
        }
    } else if (isFloat) {
#ifdef _OPENMP
        #pragma omp parallel for if(multiThread)
#endif
        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width; ++col) {
                for (int i = 0; i < ri->get_colors(); ++i) {
                    float val = float_raw_image[(row + top_margin) * raw_width + col + left_margin + i];
                    val -= cblack[i];
                    val *= scale_mul[i];
                    image[row * width + col][i] = val;
                }
            }
        }
    } else {
        const int size = ri->get_iheight() * ri->get_iwidth();

#ifdef _OPENMP
        #pragma omp parallel for if(multiThread)
#endif
        for (int i = 0; i < size; ++i) {
            for (int j = 0; j < 4; ++j) {
                float val = image[i][j];
                val -= cblack[j];
                val *= scale_mul[j];
                image[i][j] = rtengine::CLIP (val);
            }
        }
    }
}

} // namespace

extern Options options;

namespace rtengine {

extern const Settings *settings;

using namespace procparams;

Thumbnail* Thumbnail::loadFromImage (const Glib::ustring& fname, int &w, int &h, int fixwh, double wbEq)
{

    StdImageSource imgSrc;

    if (imgSrc.load(fname, std::max(w, 0), std::max(h, 0))) {
        return nullptr;
    }

    ImageIO* img = imgSrc.getImageIO();

    Thumbnail* tpp = new Thumbnail ();

    unsigned char* data;
    img->getEmbeddedProfileData (tpp->embProfileLength, data);

    if (data && tpp->embProfileLength) {
        tpp->embProfileData = new unsigned char [tpp->embProfileLength];
        memcpy (tpp->embProfileData, data, tpp->embProfileLength);
        tpp->embProfile = cmsOpenProfileFromMem(tpp->embProfileData,
                                                tpp->embProfileLength);
    }

    tpp->scaleForSave = 8192;
    tpp->defGain = 1.0;
    tpp->gammaCorrected = false;
    tpp->isRaw = 0;
    tpp->sensorType = ST_NONE;
    memset (tpp->colorMatrix, 0, sizeof (tpp->colorMatrix));
    tpp->colorMatrix[0][0] = 1.0;
    tpp->colorMatrix[1][1] = 1.0;
    tpp->colorMatrix[2][2] = 1.0;

    if (fixwh < 0 && w > 0 && h > 0) {
        int ww = h * img->getWidth() / img->getHeight();
        int hh = w * img->getHeight() / img->getWidth();
        if (ww <= w) {
            w = ww;
            tpp->scale = double(img->getHeight()) / h;
        } else {
            h = hh;
            tpp->scale = double(img->getWidth()) / w;
        }
    } else if (fixwh == 1) {
        w = h * img->getWidth() / img->getHeight();
        tpp->scale = (double)img->getHeight() / h;
    } else {
        h = w * img->getHeight() / img->getWidth();
        tpp->scale = (double)img->getWidth() / w;
    }

    h = std::max(h, 1);
    w = std::max(w, 1);

    // bilinear interpolation
    if (tpp->thumbImg) {
        delete tpp->thumbImg;
        tpp->thumbImg = nullptr;
    }

    // we want the same image type than the source file
    tpp->thumbImg = resizeToSameType (w, h, TI_Bilinear, img);

    double avg_r = 0;
    double avg_g = 0;
    double avg_b = 0;
    int n = 0;

    if (n > 0) {
        // ColorTemp cTemp;

        tpp->redAWBMul   = avg_r / double (n);
        tpp->greenAWBMul = avg_g / double (n);
        tpp->blueAWBMul  = avg_b / double (n);
        // tpp->wbEqual = wbEq;

        // cTemp.mul2temp (tpp->redAWBMul, tpp->greenAWBMul, tpp->blueAWBMul, tpp->wbEqual, tpp->autoWBTemp, tpp->autoWBGreen);
    }

    tpp->init ();

    return tpp;
}


Thumbnail* Thumbnail::loadQuickFromRaw (const Glib::ustring& fname, eSensorType &sensorType, int &w, int &h, int fixwh, bool rotate, bool forHistogramMatching)
{
    Thumbnail* tpp = new Thumbnail ();
    tpp->isRaw = 1;
    tpp->sensorType = sensorType;
    memset (tpp->colorMatrix, 0, sizeof (tpp->colorMatrix));
    tpp->colorMatrix[0][0] = 1.0;
    tpp->colorMatrix[1][1] = 1.0;
    tpp->colorMatrix[2][2] = 1.0;

    RawImage *ri = new RawImage(fname);
    unsigned int imageNum = 0;
    int r = ri->loadRaw(false, imageNum, false);

    if (r) {
        delete tpp;
        delete ri;
        sensorType = ST_NONE;
        return nullptr;
    }

    sensorType = ri->getSensorType();

    Image8 *img = ri->getThumbnail();

    // did we succeed?
    if (!img) {
        if (options.rtSettings.verbose) {
            std::cout << "Could not extract thumb from " << fname.c_str() << std::endl;
        }
        delete tpp;
        //delete img;
        delete ri;
        return nullptr;
    }

    if (forHistogramMatching) {
        // Special case, meaning that we want a full sized thumbnail image 
        w = img->getWidth();
        h = img->getHeight();
        tpp->scale = 1.;
    } else {
        if (fixwh == 1) {
            w = h * img->getWidth() / img->getHeight();
            tpp->scale = (double)img->getHeight() / h;
        } else {
            h = w * img->getHeight() / img->getWidth();
            tpp->scale = (double)img->getWidth() / w;
        }
    }

    if (tpp->thumbImg) {
        delete tpp->thumbImg;
        tpp->thumbImg = nullptr;
    }

    if (forHistogramMatching) {
        tpp->thumbImg = img;
    } else {
        tpp->thumbImg = resizeTo<Image8> (w, h, TI_Nearest, img);
        delete img;
    }

    if (rotate && ri->get_rotateDegree() > 0 && ri->thumbNeedsRotation()) {
        tpp->thumbImg->rotate(ri->get_rotateDegree());
        // width/height may have changed after rotating
        w = tpp->thumbImg->getWidth();
        h = tpp->thumbImg->getHeight();
    }

    if (!forHistogramMatching) {
        tpp->init ();
    }

    delete ri;

    return tpp;
}

#define FISRED(filter,row,col) \
    ((filter >> ((((row) << 1 & 14) + ((col) & 1)) << 1) & 3)==0 || !filter)
#define FISGREEN(filter,row,col) \
    ((filter >> ((((row) << 1 & 14) + ((col) & 1)) << 1) & 3)==1 || !filter)
#define FISBLUE(filter,row,col) \
    ((filter >> ((((row) << 1 & 14) + ((col) & 1)) << 1) & 3)==2 || !filter)

Thumbnail* Thumbnail::loadFromRaw (const Glib::ustring& fname, eSensorType &sensorType, int &w, int &h, int fixwh, double wbEq, bool rotate, bool forHistogramMatching)
{
    RawImage *ri = new RawImage (fname);
    unsigned int tempImageNum = 0;

    int r = ri->loadRaw(true, tempImageNum, false, nullptr, 1.0, false);

    if ( r ) {
        delete ri;
        sensorType = ST_NONE;
        return nullptr;
    }

    if (ri->getFrameCount() == 7) {
        // special case for Hasselblad H6D-100cMS pixelshift files
        // first frame is not bayer, load second frame
        int r = ri->loadRaw(true, 1, false, nullptr, 1.0, false);

        if ( r ) {
            delete ri;
            sensorType = ST_NONE;
            return nullptr;
        }
    }
    sensorType = ri->getSensorType();

    int width = ri->get_width();
    int height = ri->get_height();
    int iwidth = ri->get_iwidth();
    int iheight = ri->get_iheight();
    int left_margin = ri->get_leftmargin();
    int top_margin = ri->get_topmargin();
    
    rtengine::Thumbnail* tpp = new rtengine::Thumbnail;

    tpp->isRaw = true;
    tpp->sensorType = sensorType;
    tpp->embProfile = nullptr;
    tpp->embProfileData = nullptr;
    tpp->embProfileLength = ri->get_profileLen();

    if (ri->get_profileLen())
        tpp->embProfile = cmsOpenProfileFromMem (ri->get_profile(),
                          ri->get_profileLen()); //\ TODO check if mutex is needed

    tpp->redMultiplier = ri->get_pre_mul (0);
    tpp->greenMultiplier = ri->get_pre_mul (1);
    tpp->blueMultiplier = ri->get_pre_mul (2);

    float pre_mul[4], scale_mul[4], cblack[4];
    ri->get_colorsCoeff (pre_mul, scale_mul, cblack, false);
    scale_colors (ri, scale_mul, cblack, forHistogramMatching); // enable multithreading when forHistogramMatching is true

    ri->pre_interpolate();

    tpp->camwbRed = tpp->redMultiplier / pre_mul[0]; //ri->get_pre_mul(0);
    tpp->camwbGreen = tpp->greenMultiplier / pre_mul[1]; //ri->get_pre_mul(1);
    tpp->camwbBlue = tpp->blueMultiplier / pre_mul[2]; //ri->get_pre_mul(2);
    //tpp->defGain = 1.0 / min(ri->get_pre_mul(0), ri->get_pre_mul(1), ri->get_pre_mul(2));
    tpp->defGain = max (scale_mul[0], scale_mul[1], scale_mul[2], scale_mul[3]) / min (scale_mul[0], scale_mul[1], scale_mul[2], scale_mul[3]);
    tpp->defGain *= std::pow(2, ri->getBaselineExposure());
    tpp->scaleGain = scale_mul[0] / pre_mul[0]; // used to reconstruct scale_mul from filmnegativethumb.cc

    tpp->gammaCorrected = true;

    unsigned filter = ri->get_filters();
    int firstgreen = 1;

    // locate first green location in the first row
    if (ri->getSensorType() == ST_BAYER)
        while (!FISGREEN (filter, 1, firstgreen) && firstgreen < 3) {
            firstgreen++;
        }

    int skip = 1;

    if (ri->get_FujiWidth() != 0) {
        if (fixwh == 1) { // fix height, scale width
            skip = ((ri->get_height() - ri->get_FujiWidth()) / sqrt (0.5) - firstgreen - 1) / h;
        } else {
            skip = (ri->get_FujiWidth() / sqrt (0.5) - firstgreen - 1) / w;
        }
    } else {
        if (fixwh == 1) { // fix height, scale width
            skip = (ri->get_height() - firstgreen - 1) / h;
        } else {
            skip = (ri->get_width() - firstgreen - 1) / w;
        }
    }

    if (skip % 2) {
        skip--;
    }

    if (skip < 2) {
        skip = 2;
    }

    int hskip = skip, vskip = skip;

    if (!ri->get_model().compare ("D1X")) {
        hskip *= 2;
    }

    int rofs = 0;
    int tmpw = (width - 2) / hskip;
    int tmph = (height - 2) / vskip;

    rtengine::RawImage::ImageType image = ri->get_image();

    Imagefloat* tmpImg = new Imagefloat (tmpw, tmph);

    if (ri->getSensorType() == ST_BAYER) {
        // demosaicing! (sort of)
        for (int row = 1, y = 0; row < height - 1 && y < tmph; row += vskip, y++) {
            rofs = (row + top_margin) * iwidth;

            for (int col = firstgreen, x = 0; col < width - 1 && x < tmpw; col += hskip, x++) {
                int ofs = rofs + col + left_margin;
                int g = image[ofs][1];
                int r, b;

                if (FISRED (filter, row, col + 1)) {
                    r = (image[ofs + 1    ][0] + image[ofs - 1    ][0]) >> 1;
                    b = (image[ofs + iwidth][2] + image[ofs - iwidth][2]) >> 1;
                } else {
                    b = (image[ofs + 1    ][2] + image[ofs - 1    ][2]) >> 1;
                    r = (image[ofs + iwidth][0] + image[ofs - iwidth][0]) >> 1;
                }

                tmpImg->r (y, x) = r;
                tmpImg->g (y, x) = g;
                tmpImg->b (y, x) = b;
            }
        }
    } else if (ri->get_colors() == 1) {
        for (int row = 1, y = 0; row < height - 1 && y < tmph; row += vskip, y++) {
            rofs = (row + top_margin) * iwidth;

            for (int col = firstgreen, x = 0; col < width - 1 && x < tmpw; col
                    += hskip, x++) {
                int ofs = rofs + col + left_margin;
                tmpImg->r (y, x) = tmpImg->g (y, x) = tmpImg->b (y, x) = image[ofs][0];
            }
        }
    } else {
        if (ri->getSensorType() == ST_FUJI_XTRANS) {
            for ( int row = 1, y = 0; row < height - 1 && y < tmph; row += vskip, y++) {
                rofs = (row + top_margin) * iwidth;

                for ( int col = 1, x = 0; col < width - 1 && x < tmpw; col += hskip, x++ ) {
                    int ofs = rofs + col + left_margin;
                    float sum[3] = {};
                    int c;

                    for (int v = -1; v <= 1; v++) {
                        for (int h = -1; h <= 1; h++) {
                            c = ri->XTRANSFC (row + v, col + h);
                            sum[c] += image[ofs + v * iwidth + h][c];
                        }
                    }

                    c = ri->XTRANSFC (row, col);

                    switch (c) {
                        case 0:
                            tmpImg->r (y, x) = image[ofs][0];
                            tmpImg->g (y, x) = sum[1] / 5.f;
                            tmpImg->b (y, x) = sum[2] / 3.f;
                            break;

                        case 1:
                            tmpImg->r (y, x) = sum[0] / 2.f;
                            tmpImg->g (y, x) = image[ofs][1];
                            tmpImg->b (y, x) = sum[2] / 2.f;
                            break;

                        case 2:
                            tmpImg->r (y, x) = sum[0] / 3.f;
                            tmpImg->g (y, x) = sum[1] / 5.f;
                            tmpImg->b (y, x) = image[ofs][2];
                            break;
                    }
                }
            }
        } else {
            // int iwidth = ri->get_iwidth();
            // int iheight = ri->get_iheight();
            // int left_margin = ri->get_leftmargin();
            firstgreen += left_margin;
            // int top_margin = ri->get_topmargin();
            int wmax = tmpw;
            int hmax = tmph;

            if ((ri->get_maker() == "Sigma" || ri->get_maker() == "Pentax" || ri->get_maker() == "Sony") && ri->DNGVERSION()) { // Hack to prevent sigma dng files from crashing
                wmax = (width - 2 - left_margin) / hskip;
                hmax = (height - 2 - top_margin) / vskip;
            }

            int y = 0;

            for (int row = 1 + top_margin; row < iheight + top_margin  - 1 && y < hmax; row += vskip, y++) {
                rofs = row * iwidth;

                int x = 0;

                for (int col = firstgreen; col < iwidth + left_margin - 1 && x < wmax; col += hskip, x++) {
                    int ofs = rofs + col;
                    tmpImg->r (y, x) = image[ofs][0];
                    tmpImg->g (y, x) = image[ofs][1];
                    tmpImg->b (y, x) = image[ofs][2];
                }

                for (; x < tmpw; ++x) {
                    tmpImg->r (y, x) = tmpImg->g (y, x) = tmpImg->b (y, x) = 0;
                }
            }

            for (; y < tmph; ++y) {
                for (int x = 0; x < tmpw; ++x) {
                    tmpImg->r (y, x) = tmpImg->g (y, x) = tmpImg->b (y, x) = 0;
                }
            }
        }
    }

    if (ri->get_FujiWidth() != 0) {
        int fw = ri->get_FujiWidth() / hskip;
        double step = sqrt (0.5);
        int wide = fw / step;
        int high = (tmph - fw) / step;
        Imagefloat* fImg = new Imagefloat (wide, high);
        float r, c;

        for (int row = 0; row < high; row++)
            for (int col = 0; col < wide; col++) {
                int ur = r = fw + (row - col) * step;
                int uc = c = (row + col) * step;

                if (ur > tmph - 2 || uc > tmpw - 2) {
                    continue;
                }

                double fr = r - ur;
                double fc = c - uc;
                fImg->r (row, col) = (tmpImg->r (ur, uc) * (1 - fc) + tmpImg->r (ur, uc + 1) * fc) * (1 - fr)   +   (tmpImg->r (ur + 1, uc) * (1 - fc) + tmpImg->r (ur + 1, uc + 1) * fc) * fr;
                fImg->g (row, col) = (tmpImg->g (ur, uc) * (1 - fc) + tmpImg->g (ur, uc + 1) * fc) * (1 - fr)   +   (tmpImg->g (ur + 1, uc) * (1 - fc) + tmpImg->g (ur + 1, uc + 1) * fc) * fr;
                fImg->b (row, col) = (tmpImg->b (ur, uc) * (1 - fc) + tmpImg->b (ur, uc + 1) * fc) * (1 - fr)   +   (tmpImg->b (ur + 1, uc) * (1 - fc) + tmpImg->b (ur + 1, uc + 1) * fc) * fr;
            }

        delete tmpImg;
        tmpImg = fImg;
        tmpw = wide;
        tmph = high;
    }

    const bool rotate_90 =
        rotate
        && (
            ri->get_rotateDegree() == 90
            || ri->get_rotateDegree() == 270
        );

    if (rotate_90) {
        std::swap (tmpw, tmph);
    }

    if (fixwh == 1) { // fix height, scale width
        w = tmpw * h / tmph;
    } else {
        h = tmph * w / tmpw;
    }

    if (tpp->thumbImg) {
        delete tpp->thumbImg;
    }

    if (rotate_90) {
        tpp->thumbImg = resizeTo<Image16> (h, w, TI_Bilinear, tmpImg);
    } else {
        tpp->thumbImg = resizeTo<Image16> (w, h, TI_Bilinear, tmpImg);
    }

    delete tmpImg;


    if (ri->get_FujiWidth() != 0) {
        tpp->scale = (double) (height - ri->get_FujiWidth()) * 2.0 / (rotate_90 ? w : h);
    } else {
        tpp->scale = (double) height / (rotate_90 ? w : h);
    }
    if(!forHistogramMatching) { // we don't need this for histogram matching
        double pixSum[3] = {0.0};
        unsigned int n[3] = {0};
        const double clipval = 64000.0 / tpp->defGain;

        for (int i = 32; i < height - 32; i++) {
            int start, end;

            if (ri->get_FujiWidth() != 0) {
                int fw = ri->get_FujiWidth();
                start = ABS (fw - i) + 32;
                end = min (height + width - fw - i, fw + i) - 32;
            } else {
                start = 32;
                end = width - 32;
            }

            if (ri->getSensorType() == ST_BAYER) {
                int c0 = ri->FC(i, start);
                int c1 = ri->FC(i, start + 1);
                int j = start;
                int n0 = 0;
                int n1 = 0;
                double pixSum0 = 0.0;
                double pixSum1 = 0.0;
                for (; j < end - 1; j+=2) {
                    double v0 = image[i * width + j][c0];
                    if (v0 <= clipval) {
                        pixSum0 += v0;
                        n0++;
                    }
                    double v1 = image[i * width + j + 1][c1];
                    if (v1 <= clipval) {
                        pixSum1 += v1;
                        n1++;
                    }
                }
                if (j < end) {
                    double v0 = image[i * width + j][c0];
                    if (v0 <= clipval) {
                        pixSum0 += v0;
                        n0++;
                    }
                }
                n[c0] += n0;
                n[c1] += n1;
                pixSum[c0] += pixSum0;
                pixSum[c1] += pixSum1;
            } else if (ri->getSensorType() == ST_FUJI_XTRANS) {
                int c[6];
                for(int cc = 0; cc < 6; ++cc) {
                    c[cc] = ri->XTRANSFC(i, start + cc);
                }
                int j = start;
                for (; j < end - 5; j += 6) {
                    for(int cc = 0; cc < 6; ++cc) {
                        double d = image[i * width + j + cc][c[cc]];
                        if (d <= clipval) {
                            pixSum[c[cc]] += d;
                            n[c[cc]]++;
                        }
                    }
                }
                for (; j < end; j++) {
                    if (ri->ISXTRANSGREEN (i, j)) {
                        double d = image[i * width + j][1];
                        if (d <= clipval) {
                            pixSum[1] += d;
                            n[1]++;
                        }
                    } else if (ri->ISXTRANSRED (i, j)) {
                        double d = image[i * width + j][0];
                        if (d <= clipval) {
                            pixSum[0] += d;
                            n[0]++;
                        }
                    } else if (ri->ISXTRANSBLUE (i, j)) {
                        double d = image[i * width + j][2];
                        if (d <= clipval) {
                            pixSum[2] += d;
                            n[2]++;
                        }
                    }
                }
            } else { /* if(ri->getSensorType()==ST_FOVEON) */
                for (int j = start; j < end; j++) {
                    double r = image[i * width + j][0];
                    if (r <= clipval) {
                        pixSum[0] += r;
                        n[0]++;
                    }
                    double g = image[i * width + j][1];
                    if (g <= clipval) {
                        pixSum[1] += g;
                        n[1]++;
                    }
                    double b = image[i * width + j][2];
                    if (b <= clipval) {
                        pixSum[2] += b;
                        n[2]++;
                    }
                }
            }
        }

        if (ri->get_colors() == 1) {
            pixSum[0] = pixSum[1] = pixSum[2] = 1.;
            n[0] = n[1] = n[2] = 1;
        }
        pixSum[0] *= tpp->defGain;
        pixSum[1] *= tpp->defGain;
        pixSum[2] *= tpp->defGain;

        double reds = pixSum[0] / std::max(n[0], 1u) * tpp->camwbRed;
        double greens = pixSum[1] / std::max(n[1], 1u) * tpp->camwbGreen;
        double blues = pixSum[2] / std::max(n[2], 1u) * tpp->camwbBlue;

        tpp->redAWBMul   = ri->get_rgb_cam (0, 0) * reds + ri->get_rgb_cam (0, 1) * greens + ri->get_rgb_cam (0, 2) * blues;
        tpp->greenAWBMul = ri->get_rgb_cam (1, 0) * reds + ri->get_rgb_cam (1, 1) * greens + ri->get_rgb_cam (1, 2) * blues;
        tpp->blueAWBMul  = ri->get_rgb_cam (2, 0) * reds + ri->get_rgb_cam (2, 1) * greens + ri->get_rgb_cam (2, 2) * blues;
        // tpp->wbEqual = wbEq;

        // ColorTemp cTemp;
        // cTemp.mul2temp (tpp->redAWBMul, tpp->greenAWBMul, tpp->blueAWBMul, tpp->wbEqual, tpp->autoWBTemp, tpp->autoWBGreen);
    }

    if (rotate && ri->get_rotateDegree() > 0) {
        tpp->thumbImg->rotate (ri->get_rotateDegree());
    }

    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) {
            tpp->colorMatrix[a][b] = ri->get_rgb_cam (a, b);
        }

    tpp->init();

    RawImageSource::computeFullSize(ri, TR_NONE, tpp->full_width, tpp->full_height);
    
    delete ri;
    return tpp;
}
#undef FISRED
#undef FISGREEN
#undef FISBLUE

void Thumbnail::init ()
{
    RawImageSource::inverse33 (colorMatrix, iColorMatrix);
    //colorMatrix is rgb_cam
    memset (cam2xyz, 0, sizeof (cam2xyz));

    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++) {
                cam2xyz[i][j] += xyz_sRGB[i][k] * colorMatrix[k][j];
            }

    camProfile = ICCStore::getInstance()->createFromMatrix (cam2xyz, false, "Camera");
}

Thumbnail::Thumbnail () :
    camProfile (nullptr),
    iColorMatrix{},
    cam2xyz{},
    thumbImg (nullptr),
    camwbRed (1.0),
    camwbGreen (1.0),
    camwbBlue (1.0),
    redAWBMul (-1.0),
    greenAWBMul (-1.0),
    blueAWBMul (-1.0),
    // autoWBTemp (2700),
    // autoWBGreen (1.0),
    // wbEqual (-1.0),
    embProfileLength (0),
    embProfileData (nullptr),
    embProfile (nullptr),
    redMultiplier (1.0),
    greenMultiplier (1.0),
    blueMultiplier (1.0),
    scale (1.0),
    defGain (1.0),
    scaleForSave (8192),
    gammaCorrected (false),
    colorMatrix{},
    scaleGain(1.0),
    sensorType(ST_NONE),
    isRaw(false),
    full_width(-1),
    full_height(-1)
{
}

Thumbnail::~Thumbnail ()
{

    delete thumbImg;
    delete [] embProfileData;

    if (embProfile) {
        cmsCloseProfile (embProfile);
    }

    if (camProfile) {
        cmsCloseProfile (camProfile);
    }
}

// Simple processing of RAW internal JPGs
IImage8* Thumbnail::quickProcessImage (const procparams::ProcParams& params, int rheight, rtengine::TypeInterpolation interp)
{

    int rwidth;

    if (params.coarse.rotate == 90 || params.coarse.rotate == 270) {
        rwidth = rheight;
        rheight = thumbImg->getHeight() * rwidth / thumbImg->getWidth();
    } else {
        rwidth = thumbImg->getWidth() * rheight / thumbImg->getHeight();
    }

    Image8* baseImg = resizeTo<Image8> (rwidth, rheight, interp, thumbImg);

    if (params.coarse.rotate) {
        baseImg->rotate (params.coarse.rotate);
    }

    if (params.coarse.hflip) {
        baseImg->hflip ();
    }

    if (params.coarse.vflip) {
        baseImg->vflip ();
    }

    return baseImg;
}

// Full thumbnail processing, second stage if complete profile exists
IImage8* Thumbnail::processImage (const procparams::ProcParams& params, eSensorType sensorType, int rheight, TypeInterpolation interp, const FramesMetaData *metadata, double& myscale, bool forMonitor, bool forHistogramMatching)
{
    std::string camName = metadata->getCamera();
    
    // // check if the WB's equalizer value has changed
    // if (wbEqual < (params.wb.equal - 5e-4) || wbEqual > (params.wb.equal + 5e-4)) {
    //     wbEqual = params.wb.equal;
    //     // recompute the autoWB
    //     ColorTemp cTemp;
    //     cTemp.mul2temp (redAWBMul, greenAWBMul, blueAWBMul, wbEqual, autoWBTemp, autoWBGreen);
    // }

    // compute WB multipliers
    ColorTemp currWB;

    if (!params.wb.enabled) {
        currWB = ColorTemp();
    } else if (params.wb.method == WBParams::CAMERA || params.wb.method == WBParams::CUSTOM_MULT) {
        double cr = camwbRed;
        double cg = camwbGreen;
        double cb = camwbBlue;
        if (params.wb.method == WBParams::CUSTOM_MULT) {
            cr = redMultiplier / params.wb.mult[0];
            cg = greenMultiplier / params.wb.mult[1];
            cb = blueMultiplier / params.wb.mult[2];
        }
        //recall colorMatrix is rgb_cam
        double cam_r = colorMatrix[0][0] * cr + colorMatrix[0][1] * cg + colorMatrix[0][2] * cb;
        double cam_g = colorMatrix[1][0] * cr + colorMatrix[1][1] * cg + colorMatrix[1][2] * cb;
        double cam_b = colorMatrix[2][0] * cr + colorMatrix[2][1] * cg + colorMatrix[2][2] * cb;
        currWB = ColorTemp (cam_r, cam_g, cam_b);//, params.wb.equal);
    } else if (params.wb.method == WBParams::AUTO) {
        //currWB = ColorTemp (autoWBTemp, autoWBGreen, wbEqual, "Custom");
        currWB = ColorTemp(redAWBMul, greenAWBMul, blueAWBMul);
    } else if (params.wb.method == WBParams::CUSTOM_TEMP) {
        currWB = ColorTemp(params.wb.temperature, params.wb.green, params.wb.equal, "Custom");
    } else if (params.wb.method == WBParams::CUSTOM_MULT_LEGACY) {
        currWB = ColorTemp(params.wb.mult[0], params.wb.mult[1], params.wb.mult[2], 1.0);
    }
    double rm, gm, bm;
    if (currWB.getTemp() < 0) {
        rm = redMultiplier;
        gm = greenMultiplier;
        bm = blueMultiplier;
    } else {
        double r, g, b;
        currWB.getMultipliers (r, g, b);
        //iColorMatrix is cam_rgb
        rm = iColorMatrix[0][0] * r + iColorMatrix[0][1] * g + iColorMatrix[0][2] * b;
        gm = iColorMatrix[1][0] * r + iColorMatrix[1][1] * g + iColorMatrix[1][2] * b;
        bm = iColorMatrix[2][0] * r + iColorMatrix[2][1] * g + iColorMatrix[2][2] * b;
    }
    rm = camwbRed / rm;
    gm = camwbGreen / gm;
    bm = camwbBlue / bm;
    double mul_lum = 0.299 * rm + 0.587 * gm + 0.114 * bm;
    float rmi, gmi, bmi;

    rmi = rm * defGain / mul_lum;
    gmi = gm * defGain / mul_lum;
    bmi = bm * defGain / mul_lum;

    // The RAW exposure is not reflected since it's done in preprocessing. If we only have e.g. the cached thumb,
    // that is already preprocessed. So we simulate the effect here roughly my modifying the exposure accordingly
    if (isRaw) {
        rmi *= params.raw.expos;
        gmi *= params.raw.expos;
        bmi *= params.raw.expos;
    }

    // resize to requested width and perform coarse transformation
    int rwidth;

    if (params.coarse.rotate == 90 || params.coarse.rotate == 270) {
        rwidth = rheight;
        rheight = int (size_t (thumbImg->getHeight()) * size_t (rwidth) / size_t (thumbImg->getWidth()));
    } else {
        rwidth = int (size_t (thumbImg->getWidth()) * size_t (rheight) / size_t (thumbImg->getHeight()));
    }

    rwidth = std::max(rwidth, 1);
    rheight = std::max(rheight, 1);

    Imagefloat* baseImg = resizeTo<Imagefloat> (rwidth, rheight, interp, thumbImg);
    if (params.filmNegative.enabled) {
        if (params.filmNegative.backCompat == FilmNegativeParams::BackCompat::V1) {
            processFilmNegative(params, baseImg, rwidth, rheight);
        } else if (params.filmNegative.backCompat == FilmNegativeParams::BackCompat::V2) {
            processFilmNegativeV2(params, baseImg, rwidth, rheight);
        }
    }
    baseImg->assignColorSpace(params.icm.workingProfile);

    if (params.coarse.rotate) {
        baseImg->rotate (params.coarse.rotate);
        rwidth = baseImg->getWidth();
        rheight = baseImg->getHeight();
    }

    if (params.coarse.hflip) {
        baseImg->hflip ();
    }

    if (params.coarse.vflip) {
        baseImg->vflip ();
    }

    const bool hrenabled = params.exposure.enabled && params.exposure.hrmode != procparams::ExposureParams::HR_OFF;

    // apply white balance and raw white point (simulated)
    for (int i = 0; i < rheight; i++) {
#ifdef _OPENMP
        #pragma omp simd
#endif
        for (int j = 0; j < rwidth; j++) {
            float red = baseImg->r (i, j) * rmi;
            float green = baseImg->g (i, j) * gmi;
            float blue = baseImg->b (i, j) * bmi;
            
            // avoid magenta highlights if highlight recovery is enabled
            if (hrenabled && red > MAXVALF && blue > MAXVALF) {
                baseImg->r(i, j) = baseImg->g(i, j) = baseImg->b(i, j) = CLIP((red + green + blue) / 3.f);
            } else {
                baseImg->r(i, j) = CLIP(red);
                baseImg->g(i, j) = CLIP(green);
                baseImg->b(i, j) = CLIP(blue);
            }
        }
    }

    int fw = baseImg->getWidth();
    int fh = baseImg->getHeight();

    ImProcFunctions ipf (&params, forHistogramMatching); // enable multithreading when forHistogramMatching is true

    if (params.filmNegative.enabled && params.filmNegative.backCompat == FilmNegativeParams::BackCompat::CURRENT && params.filmNegative.colorSpace == FilmNegativeParams::ColorSpace::INPUT) {
        ipf.filmNegativeProcess(baseImg, baseImg, params.filmNegative);
    }

    // perform color space transformation

    if (isRaw) {
        double pre_mul[3] = { redMultiplier, greenMultiplier, blueMultiplier };
        RawImageSource::colorSpaceConversion (baseImg, params.icm, currWB, pre_mul, embProfile, camProfile, cam2xyz, camName, metadata->getFileName());
    } else {
        StdImageSource::colorSpaceConversion (baseImg, params.icm, embProfile, thumbImg->getSampleFormat());
    }

    if (params.filmNegative.enabled && params.filmNegative.backCompat == FilmNegativeParams::BackCompat::CURRENT && params.filmNegative.colorSpace != FilmNegativeParams::ColorSpace::INPUT) {
        ipf.filmNegativeProcess(baseImg, baseImg, params.filmNegative);
    }
     
    int origFW;
    int origFH;
    double tscale = 0.0;
    getDimensions (origFW, origFH, tscale);
    ipf.setScale((origFW * tscale) / rwidth);
    //ipf.updateColorProfiles (ICCStore::getInstance()->getDefaultMonitorProfileName(), options.rtSettings.monitorIntent, false, false);
    ipf.setMonitorTransform(ICCStore::getInstance()->getThumbnailMonitorTransform());

    LUTu hist16 (65536);

    ipf.firstAnalysis (baseImg, params, hist16);

    bool stop = ipf.process(ImProcFunctions::Pipeline::THUMBNAIL, ImProcFunctions::Stage::STAGE_0, baseImg);
    
    // perform transform
    if (ipf.needsTransform()) {
        Imagefloat* trImg = new Imagefloat(fw, fh, baseImg);
        ipf.transform (baseImg, trImg, 0, 0, 0, 0, fw, fh, origFW * tscale + 0.5, origFH * tscale + 0.5, metadata, 0, false); // Raw rotate degree not detectable here
        delete baseImg;
        baseImg = trImg;
    }

    DCPProfile *dcpProf = nullptr;
    DCPProfile::ApplyState as;

    if (isRaw) {
        cmsHPROFILE dummy;
        std::unique_ptr<CLUTApplication> dummy_lut_prof;
        RawImageSource::findInputProfile(params.icm, nullptr, camName, metadata->getFileName(), &dcpProf, dummy, dummy_lut_prof);

        if (dcpProf) {
            dcpProf->setStep2ApplyState (params.icm.workingProfile, params.icm.toneCurve, params.icm.applyLookTable, params.icm.applyBaselineExposureOffset, as);
        }
    }
    ipf.setDCPProfile(dcpProf, as);

    stop = stop || ipf.process(ImProcFunctions::Pipeline::THUMBNAIL, ImProcFunctions::Stage::STAGE_1, baseImg);
    stop = stop || ipf.process(ImProcFunctions::Pipeline::THUMBNAIL, ImProcFunctions::Stage::STAGE_2, baseImg);
    stop = stop || ipf.process(ImProcFunctions::Pipeline::THUMBNAIL, ImProcFunctions::Stage::STAGE_3, baseImg);
    
    // obtain final image
    Image8 *readyImg = nullptr;
    if (forMonitor) {
        readyImg = new Image8 (fw, fh);
        ipf.rgb2monitor(baseImg, readyImg, true);
    } else {
        readyImg = ipf.rgb2out(baseImg, 0, 0, fw, fh, params.icm, false);
    }
    delete baseImg;

    // calculate scale
    if (params.coarse.rotate == 90 || params.coarse.rotate == 270) {
        myscale = scale * thumbImg->getWidth() / fh;
    } else {
        myscale = scale * thumbImg->getHeight() / fh;
    }

    myscale = 1.0 / myscale;

    ipf.setMonitorTransform(nullptr);
    
    return readyImg;
}

int Thumbnail::getImageWidth (const procparams::ProcParams& params, int rheight, float &ratio)
{
    if (!thumbImg) {
        return 0;    // Can happen if thumb is just building and GUI comes in with resize wishes
    }

    int rwidth;

    if (params.coarse.rotate == 90 || params.coarse.rotate == 270) {
        ratio = (float) (thumbImg->getHeight()) / (float) (thumbImg->getWidth());
    } else {
        ratio = (float) (thumbImg->getWidth()) / (float) (thumbImg->getHeight());
    }

    rwidth = (int) (ratio * (float)rheight);

    return rwidth;
}

void Thumbnail::getDimensions (int& w, int& h, double& scaleFac)
{
    if (thumbImg) {
        w = thumbImg->getWidth();
        h = thumbImg->getHeight();
        scaleFac = scale;
    } else {
        w = 0;
        h = 0;
        scale = 1;
    }
}

void Thumbnail::getCamWB(ColorTemp &out)
{

    double cam_r = colorMatrix[0][0] * camwbRed + colorMatrix[0][1] * camwbGreen + colorMatrix[0][2] * camwbBlue;
    double cam_g = colorMatrix[1][0] * camwbRed + colorMatrix[1][1] * camwbGreen + colorMatrix[1][2] * camwbBlue;
    double cam_b = colorMatrix[2][0] * camwbRed + colorMatrix[2][1] * camwbGreen + colorMatrix[2][2] * camwbBlue;
    ColorTemp currWB = ColorTemp (cam_r, cam_g, cam_b, 1.0);  // we do not take the equalizer into account here, because we want camera's WB
    // temp = currWB.getTemp ();
    // green = currWB.getGreen ();
    out = currWB;
}


void Thumbnail::getAutoWB(ColorTemp &out, double equal)
{
    // if (equal != wbEqual) {
        // compute the values depending on equal
    ColorTemp cTemp(redAWBMul, greenAWBMul, blueAWBMul);
    //     wbEqual = equal;
    //     // compute autoWBTemp and autoWBGreen
    //     cTemp.mul2temp (redAWBMul, greenAWBMul, blueAWBMul, wbEqual, autoWBTemp, autoWBGreen);
    // // }

    // temp = autoWBTemp;
    // green = autoWBGreen;
        out = cTemp;
}

void Thumbnail::getAutoWBMultipliers (double& rm, double& gm, double& bm)
{
    rm = redAWBMul;
    gm = greenAWBMul;
    bm = blueAWBMul;
}


void Thumbnail::getSpotWB(const procparams::ProcParams& params, int xp, int yp, int rect, ColorTemp &out)
{

    std::vector<Coord2D> points, red, green, blue;

    for (int i = yp - rect; i <= yp + rect; i++)
        for (int j = xp - rect; j <= xp + rect; j++) {
            points.push_back (Coord2D (j, i));
        }

    int fw = thumbImg->getWidth(), fh = thumbImg->getHeight();

    if (params.coarse.rotate == 90 || params.coarse.rotate == 270) {
        fw = thumbImg->getHeight();
        fh = thumbImg->getWidth();
    }

    ImProcFunctions ipf (&params, false);
    ipf.transCoord (fw, fh, points, red, green, blue);
    int tr = getCoarseBitMask (params.coarse);
    // calculate spot wb (copy & pasted from stdimagesource)
    double reds = 0, greens = 0, blues = 0;
    int rn = 0, gn = 0, bn = 0;
    thumbImg->getSpotWBData (reds, greens, blues, rn, gn, bn, red, green, blue, tr);
    reds = reds / rn * camwbRed;
    greens = greens / gn * camwbGreen;
    blues = blues / bn * camwbBlue;

    double rm = colorMatrix[0][0] * reds + colorMatrix[0][1] * greens + colorMatrix[0][2] * blues;
    double gm = colorMatrix[1][0] * reds + colorMatrix[1][1] * greens + colorMatrix[1][2] * blues;
    double bm = colorMatrix[2][0] * reds + colorMatrix[2][1] * greens + colorMatrix[2][2] * blues;

    ColorTemp ct (rm, gm, bm, params.wb.equal);
    // rtemp = ct.getTemp ();
    // rgreen = ct.getGreen ();
    out = ct;
}


void Thumbnail::transformPixel (int x, int y, int tran, int& tx, int& ty)
{

    int W = thumbImg->getWidth();
    int H = thumbImg->getHeight();
    int sw = W, sh = H;

    if ((tran & TR_ROT) == TR_R90 || (tran & TR_ROT) == TR_R270) {
        sw = H;
        sh = W;
    }

    int ppx = x, ppy = y;

    if (tran & TR_HFLIP) {
        ppx = sw - 1 - x ;
    }

    if (tran & TR_VFLIP) {
        ppy = sh - 1 - y;
    }

    tx = ppx;
    ty = ppy;

    if ((tran & TR_ROT) == TR_R180) {
        tx = W - 1 - ppx;
        ty = H - 1 - ppy;
    } else if ((tran & TR_ROT) == TR_R90) {
        tx = ppy;
        ty = H - 1 - ppx;
    } else if ((tran & TR_ROT) == TR_R270) {
        tx = W - 1 - ppy;
        ty = ppx;
    }

    tx /= scale;
    ty /= scale;
}

unsigned char* Thumbnail::getGrayscaleHistEQ (int trim_width)
{
    if (!thumbImg) {
        return nullptr;
    }

    if (thumbImg->getWidth() < trim_width) {
        return nullptr;
    }

    // to utilize the 8 bit color range of the thumbnail we brighten it and apply gamma correction
    unsigned char* tmpdata = new unsigned char[thumbImg->getHeight() * trim_width];
    int ix = 0;

    if (gammaCorrected) {
        // if it's gamma correct (usually a RAW), we have the problem that there is a lot noise etc. that makes the maximum way too high.
        // Strategy is limit a certain percent of pixels so the overall picture quality when scaling to 8 bit is way better
        const double BurnOffPct = 0.03; // *100 = percent pixels that may be clipped

        // Calc the histogram
        unsigned int* hist16 = new unsigned int [65536];
        memset (hist16, 0, sizeof (int) * 65536);

        if (thumbImg->getType() == sImage8) {
            Image8 *image = static_cast<Image8*> (thumbImg);
            image->calcGrayscaleHist (hist16);
        } else if (thumbImg->getType() == sImage16) {
            Image16 *image = static_cast<Image16*> (thumbImg);
            image->calcGrayscaleHist (hist16);
        } else if (thumbImg->getType() == sImagefloat) {
            Imagefloat *image = static_cast<Imagefloat*> (thumbImg);
            image->calcGrayscaleHist (hist16);
        } else {
            printf ("getGrayscaleHistEQ #1: Unsupported image type \"%s\"!\n", thumbImg->getType());
        }

        // Go down till we cut off that many pixels
        unsigned long cutoff = thumbImg->getHeight() * thumbImg->getHeight() * 4 * BurnOffPct;

        int max_;
        unsigned long sum = 0;

        for (max_ = 65535; max_ > 16384 && sum < cutoff; max_--) {
            sum += hist16[max_];
        }

        delete[] hist16;

        scaleForSave = 65535 * 8192 / max_;

        // Correction and gamma to 8 Bit
        if (thumbImg->getType() == sImage8) {
            Image8 *image = static_cast<Image8*> (thumbImg);

            for (int i = 0; i < thumbImg->getHeight(); i++)
                for (int j = (thumbImg->getWidth() - trim_width) / 2; j < trim_width + (thumbImg->getWidth() - trim_width) / 2; j++) {
                    unsigned short r_, g_, b_;
                    image->convertTo (image->r (i, j), r_);
                    image->convertTo (image->g (i, j), g_);
                    image->convertTo (image->b (i, j), b_);
                    int r = Color::gammatabThumb[min (r_, static_cast<unsigned short> (max_)) * scaleForSave >> 13];
                    int g = Color::gammatabThumb[min (g_, static_cast<unsigned short> (max_)) * scaleForSave >> 13];
                    int b = Color::gammatabThumb[min (b_, static_cast<unsigned short> (max_)) * scaleForSave >> 13];
                    tmpdata[ix++] = (r * 19595 + g * 38469 + b * 7472) >> 16;
                }
        } else if (thumbImg->getType() == sImage16) {
            Image16 *image = static_cast<Image16*> (thumbImg);

            for (int i = 0; i < thumbImg->getHeight(); i++)
                for (int j = (thumbImg->getWidth() - trim_width) / 2; j < trim_width + (thumbImg->getWidth() - trim_width) / 2; j++) {
                    unsigned short r_, g_, b_;
                    image->convertTo (image->r (i, j), r_);
                    image->convertTo (image->g (i, j), g_);
                    image->convertTo (image->b (i, j), b_);
                    int r = Color::gammatabThumb[min (r_, static_cast<unsigned short> (max_)) * scaleForSave >> 13];
                    int g = Color::gammatabThumb[min (g_, static_cast<unsigned short> (max_)) * scaleForSave >> 13];
                    int b = Color::gammatabThumb[min (b_, static_cast<unsigned short> (max_)) * scaleForSave >> 13];
                    tmpdata[ix++] = (r * 19595 + g * 38469 + b * 7472) >> 16;
                }
        } else if (thumbImg->getType() == sImagefloat) {
            Imagefloat *image = static_cast<Imagefloat*> (thumbImg);

            for (int i = 0; i < thumbImg->getHeight(); i++)
                for (int j = (thumbImg->getWidth() - trim_width) / 2; j < trim_width + (thumbImg->getWidth() - trim_width) / 2; j++) {
                    unsigned short r_, g_, b_;
                    image->convertTo (image->r (i, j), r_);
                    image->convertTo (image->g (i, j), g_);
                    image->convertTo (image->b (i, j), b_);
                    int r = Color::gammatabThumb[min (r_, static_cast<unsigned short> (max_)) * scaleForSave >> 13];
                    int g = Color::gammatabThumb[min (g_, static_cast<unsigned short> (max_)) * scaleForSave >> 13];
                    int b = Color::gammatabThumb[min (b_, static_cast<unsigned short> (max_)) * scaleForSave >> 13];
                    tmpdata[ix++] = (r * 19595 + g * 38469 + b * 7472) >> 16;
                }
        }
    } else {
        // If it's not gamma corrected (usually a JPG) we take the normal maximum
        int max = 0;

        if (thumbImg->getType() == sImage8) {
            Image8 *image = static_cast<Image8*> (thumbImg);
            unsigned char max_ = 0;

            for (int row = 0; row < image->getHeight(); row++)
                for (int col = 0; col < image->getWidth(); col++) {
                    if (image->r (row, col) > max_) {
                        max_ = image->r (row, col);
                    }

                    if (image->g (row, col) > max_) {
                        max_ = image->g (row, col);
                    }

                    if (image->b (row, col) > max_) {
                        max_ = image->b (row, col);
                    }
                }

            image->convertTo (max_, max);

            if (max < 16384) {
                max = 16384;
            }

            scaleForSave = 65535 * 8192 / max;

            // Correction and gamma to 8 Bit
            for (int i = 0; i < image->getHeight(); i++)
                for (int j = (image->getWidth() - trim_width) / 2; j < trim_width + (image->getWidth() - trim_width) / 2; j++) {
                    unsigned short rtmp, gtmp, btmp;
                    image->convertTo (image->r (i, j), rtmp);
                    image->convertTo (image->g (i, j), gtmp);
                    image->convertTo (image->b (i, j), btmp);
                    int r = rtmp * scaleForSave >> 21;
                    int g = gtmp * scaleForSave >> 21;
                    int b = btmp * scaleForSave >> 21;
                    tmpdata[ix++] = (r * 19595 + g * 38469 + b * 7472) >> 16;
                }
        } else if (thumbImg->getType() == sImage16) {
            Image16 *image = static_cast<Image16*> (thumbImg);
            unsigned short max_ = 0;

            for (int row = 0; row < image->getHeight(); row++)
                for (int col = 0; col < image->getWidth(); col++) {
                    if (image->r (row, col) > max_) {
                        max_ = image->r (row, col);
                    }

                    if (image->g (row, col) > max_) {
                        max_ = image->g (row, col);
                    }

                    if (image->b (row, col) > max_) {
                        max_ = image->b (row, col);
                    }
                }

            image->convertTo (max_, max);

            if (max < 16384) {
                max = 16384;
            }

            scaleForSave = 65535 * 8192 / max;

            // Correction and gamma to 8 Bit
            for (int i = 0; i < image->getHeight(); i++)
                for (int j = (image->getWidth() - trim_width) / 2; j < trim_width + (image->getWidth() - trim_width) / 2; j++) {
                    unsigned short rtmp, gtmp, btmp;
                    image->convertTo (image->r (i, j), rtmp);
                    image->convertTo (image->g (i, j), gtmp);
                    image->convertTo (image->b (i, j), btmp);
                    int r = rtmp * scaleForSave >> 21;
                    int g = gtmp * scaleForSave >> 21;
                    int b = btmp * scaleForSave >> 21;
                    tmpdata[ix++] = (r * 19595 + g * 38469 + b * 7472) >> 16;
                }
        } else if (thumbImg->getType() == sImagefloat) {
            Imagefloat *image = static_cast<Imagefloat*> (thumbImg);
            float max_ = 0.f;

            for (int row = 0; row < image->getHeight(); row++)
                for (int col = 0; col < image->getWidth(); col++) {
                    if (image->r (row, col) > max_) {
                        max_ = image->r (row, col);
                    }

                    if (image->g (row, col) > max_) {
                        max_ = image->g (row, col);
                    }

                    if (image->b (row, col) > max_) {
                        max_ = image->b (row, col);
                    }
                }

            image->convertTo (max_, max);

            if (max < 16384) {
                max = 16384;
            }

            scaleForSave = 65535 * 8192 / max;

            // Correction and gamma to 8 Bit
            for (int i = 0; i < image->getHeight(); i++)
                for (int j = (image->getWidth() - trim_width) / 2; j < trim_width + (image->getWidth() - trim_width) / 2; j++) {
                    unsigned short rtmp, gtmp, btmp;
                    image->convertTo (image->r (i, j), rtmp);
                    image->convertTo (image->g (i, j), gtmp);
                    image->convertTo (image->b (i, j), btmp);
                    int r = rtmp * scaleForSave >> 21;
                    int g = gtmp * scaleForSave >> 21;
                    int b = btmp * scaleForSave >> 21;
                    tmpdata[ix++] = (r * 19595 + g * 38469 + b * 7472) >> 16;
                }
        } else {
            printf ("getGrayscaleHistEQ #2: Unsupported image type \"%s\"!\n", thumbImg->getType());
        }
    }

    // histogram equalization
    unsigned int hist[256] = {0};

    for (int i = 0; i < ix; i++) {
        hist[tmpdata[i]]++;
    }

    int cdf = 0, cdf_min = -1;

    for (int i = 0; i < 256; i++) {
        cdf += hist[i];

        if (cdf > 0 && cdf_min == -1) {
            cdf_min = cdf;
        }

        if (cdf_min != -1) {
            hist[i] = (cdf - cdf_min) * 255 / ((thumbImg->getHeight() * trim_width) - cdf_min);
        }
    }

    for (int i = 0; i < ix; i++) {
        tmpdata[i] = hist[tmpdata[i]];
    }

    return tmpdata;
}

bool Thumbnail::writeImage (const Glib::ustring& fname)
{

    if (!thumbImg) {
        return false;
    }

    Glib::ustring fullFName = fname + ".rtti";

    FILE* f = g_fopen (fullFName.c_str (), "wb");

    if (!f) {
        return false;
    }

    fwrite (thumbImg->getType(), sizeof (char), strlen (thumbImg->getType()), f);
    fputc ('\n', f);
    guint32 w = guint32 (thumbImg->getWidth());
    guint32 h = guint32 (thumbImg->getHeight());
    fwrite (&w, sizeof (guint32), 1, f);
    fwrite (&h, sizeof (guint32), 1, f);

    if (thumbImg->getType() == sImage8) {
        Image8 *image = static_cast<Image8*> (thumbImg);
        image->writeData (f);
    } else if (thumbImg->getType() == sImage16) {
        Image16 *image = static_cast<Image16*> (thumbImg);
        image->writeData (f);
    } else if (thumbImg->getType() == sImagefloat) {
        Imagefloat *image = static_cast<Imagefloat*> (thumbImg);
        image->writeData (f);
    }

    //thumbImg->writeData(f);
    fclose (f);
    return true;
}

bool Thumbnail::readImage (const Glib::ustring& fname)
{

    if (thumbImg) {
        delete thumbImg;
        thumbImg = nullptr;
    }

    Glib::ustring fullFName = fname + ".rtti";

    if (!Glib::file_test(fullFName, Glib::FILE_TEST_EXISTS)) {
        return false;
    }

    FILE* f = g_fopen(fullFName.c_str (), "rb");

    if (!f) {
        return false;
    }

    char imgType[31];  // 30 -> arbitrary size, but should be enough for all image type's name
    fgets(imgType, 30, f);
    imgType[strlen(imgType) - 1] = '\0'; // imgType has a \n trailing character, so we overwrite it by the \0 char

    guint32 width, height;

    if (fread(&width, 1, sizeof(guint32), f) < sizeof(guint32)) {
        width = 0;
    }

    if (fread(&height, 1, sizeof(guint32), f) < sizeof(guint32)) {
        height = 0;
    }

    bool success = false;

    if (std::min(width , height) > 0) {
        if (!strcmp(imgType, sImage8)) {
            Image8 *image = new Image8(width, height);
            image->readData(f);
            thumbImg = image;
            success = true;
        } else if (!strcmp(imgType, sImage16)) {
            Image16 *image = new Image16(width, height);
            image->readData(f);
            thumbImg = image;
            success = true;
        } else if (!strcmp(imgType, sImagefloat)) {
            Imagefloat *image = new Imagefloat(width, height);
            image->readData(f);
            thumbImg = image;
            success = true;
        } else {
            printf ("readImage: Unsupported image type \"%s\"!\n", imgType);
        }
    }
    fclose(f);
    return success;
}

bool Thumbnail::readData  (const Glib::ustring& fname)
{
    setlocale (LC_NUMERIC, "C"); // to set decimal point to "."
    Glib::KeyFile keyFile;

    try {
        MyMutex::MyLock thmbLock (thumbMutex);

        try {
            keyFile.load_from_file (fname);
        } catch (Glib::Error&) {
            return false;
        }

        if (keyFile.has_group ("LiveThumbData")) {
            if (keyFile.has_key ("LiveThumbData", "CamWBRed")) {
                camwbRed            = keyFile.get_double ("LiveThumbData", "CamWBRed");
            }

            if (keyFile.has_key ("LiveThumbData", "CamWBGreen")) {
                camwbGreen          = keyFile.get_double ("LiveThumbData", "CamWBGreen");
            }

            if (keyFile.has_key ("LiveThumbData", "CamWBBlue")) {
                camwbBlue           = keyFile.get_double ("LiveThumbData", "CamWBBlue");
            }

            if (keyFile.has_key ("LiveThumbData", "RedAWBMul")) {
                redAWBMul           = keyFile.get_double ("LiveThumbData", "RedAWBMul");
            }

            if (keyFile.has_key ("LiveThumbData", "GreenAWBMul")) {
                greenAWBMul         = keyFile.get_double ("LiveThumbData", "GreenAWBMul");
            }

            if (keyFile.has_key ("LiveThumbData", "BlueAWBMul")) {
                blueAWBMul          = keyFile.get_double ("LiveThumbData", "BlueAWBMul");
            }

            if (keyFile.has_key ("LiveThumbData", "RedMultiplier")) {
                redMultiplier       = keyFile.get_double ("LiveThumbData", "RedMultiplier");
            }

            if (keyFile.has_key ("LiveThumbData", "GreenMultiplier")) {
                greenMultiplier     = keyFile.get_double ("LiveThumbData", "GreenMultiplier");
            }

            if (keyFile.has_key ("LiveThumbData", "BlueMultiplier")) {
                blueMultiplier      = keyFile.get_double ("LiveThumbData", "BlueMultiplier");
            }

            if (keyFile.has_key ("LiveThumbData", "Scale")) {
                scale               = keyFile.get_double ("LiveThumbData", "Scale");
            }

            if (keyFile.has_key ("LiveThumbData", "DefaultGain")) {
                defGain             = keyFile.get_double ("LiveThumbData", "DefaultGain");
            }

            if (keyFile.has_key ("LiveThumbData", "ScaleForSave")) {
                scaleForSave        = keyFile.get_integer ("LiveThumbData", "ScaleForSave");
            }

            if (keyFile.has_key ("LiveThumbData", "GammaCorrected")) {
                gammaCorrected      = keyFile.get_boolean ("LiveThumbData", "GammaCorrected");
            }

            if (keyFile.has_key ("LiveThumbData", "ColorMatrix")) {
                std::vector<double> cm = keyFile.get_double_list ("LiveThumbData", "ColorMatrix");
                int ix = 0;

                for (int i = 0; i < 3; i++)
                    for (int j = 0; j < 3; j++) {
                        colorMatrix[i][j] = cm[ix++];
                    }
            }

            if (keyFile.has_key("LiveThumbData", "ScaleGain")) {
                scaleGain = keyFile.get_double("LiveThumbData", "ScaleGain");
            }
        }

        return true;
    } catch (Glib::Error &err) {
        if (options.rtSettings.verbose) {
            printf ("Thumbnail::readData / Error code %d while reading values from \"%s\":\n%s\n", err.code(), fname.c_str(), err.what().c_str());
        }
    } catch (...) {
        if (options.rtSettings.verbose) {
            printf ("Thumbnail::readData / Unknown exception while trying to load \"%s\"!\n", fname.c_str());
        }
    }

    return false;
}

bool Thumbnail::writeData  (const Glib::ustring& fname)
{
    MyMutex::MyLock thmbLock (thumbMutex);

    Glib::ustring keyData;

    try {

        Glib::KeyFile keyFile;

        try {
            keyFile.load_from_file (fname);
        } catch (Glib::Error&) {}

        keyFile.set_double  ("LiveThumbData", "CamWBRed", camwbRed);
        keyFile.set_double  ("LiveThumbData", "CamWBGreen", camwbGreen);
        keyFile.set_double  ("LiveThumbData", "CamWBBlue", camwbBlue);
        keyFile.set_double  ("LiveThumbData", "RedAWBMul", redAWBMul);
        keyFile.set_double  ("LiveThumbData", "GreenAWBMul", greenAWBMul);
        keyFile.set_double  ("LiveThumbData", "BlueAWBMul", blueAWBMul);
        keyFile.set_double  ("LiveThumbData", "RedMultiplier", redMultiplier);
        keyFile.set_double  ("LiveThumbData", "GreenMultiplier", greenMultiplier);
        keyFile.set_double  ("LiveThumbData", "BlueMultiplier", blueMultiplier);
        keyFile.set_double  ("LiveThumbData", "Scale", scale);
        keyFile.set_double  ("LiveThumbData", "DefaultGain", defGain);
        keyFile.set_integer ("LiveThumbData", "ScaleForSave", scaleForSave);
        keyFile.set_boolean ("LiveThumbData", "GammaCorrected", gammaCorrected);
        Glib::ArrayHandle<double> cm ((double*)colorMatrix, 9, Glib::OWNERSHIP_NONE);
        keyFile.set_double_list("LiveThumbData", "ColorMatrix", cm);
        keyFile.set_double("LiveThumbData", "ScaleGain", scaleGain);

        keyData = keyFile.to_data ();

    } catch (Glib::Error& err) {
        if (options.rtSettings.verbose) {
            printf ("Thumbnail::writeData / Error code %d while reading values from \"%s\":\n%s\n", err.code(), fname.c_str(), err.what().c_str());
        }
    } catch (...) {
        if (options.rtSettings.verbose) {
            printf ("Thumbnail::writeData / Unknown exception while trying to save \"%s\"!\n", fname.c_str());
        }
    }

    if (keyData.empty ()) {
        return false;
    }

    FILE *f = g_fopen (fname.c_str (), "wt");

    if (!f) {
        if (options.rtSettings.verbose) {
            printf ("Thumbnail::writeData / Error: unable to open file \"%s\" with write access!\n", fname.c_str());
        }

        return false;
    } else {
        fprintf (f, "%s", keyData.c_str ());
        fclose (f);
    }

    return true;
}

bool Thumbnail::readEmbProfile  (const Glib::ustring& fname)
{

    embProfileData = nullptr;
    embProfile = nullptr;
    embProfileLength = 0;

    FILE* f = g_fopen (fname.c_str (), "rb");

    if (f) {
        if (!fseek (f, 0, SEEK_END)) {
            int profileLength = ftell (f);

            if (profileLength > 0) {
                embProfileLength = profileLength;

                if (!fseek (f, 0, SEEK_SET)) {
                    embProfileData = new unsigned char[embProfileLength];
                    embProfileLength = fread (embProfileData, 1, embProfileLength, f);
                    embProfile = cmsOpenProfileFromMem (embProfileData, embProfileLength);
                }
            }
        }

        fclose (f);
        return embProfile != nullptr;
    }

    return false;
}

bool Thumbnail::writeEmbProfile (const Glib::ustring& fname)
{

    if (embProfileData) {
        FILE* f = g_fopen (fname.c_str (), "wb");

        if (f) {
            fwrite (embProfileData, 1, embProfileLength, f);
            fclose (f);
            return true;
        }
    }

    return false;
}


unsigned char* Thumbnail::getImage8Data()
{
    if (thumbImg && thumbImg->getType() == rtengine::sImage8) {
        Image8* img8 = static_cast<Image8*> (thumbImg);
        return img8->data;
    }

    return nullptr;
}


Thumbnail* Thumbnail::loadInfoFromRaw(const Glib::ustring &fname, eSensorType &sensorType, int &w, int &h, int fixwh)
{
    RawImage *ri = new RawImage(fname);
    unsigned int tempImageNum = 0;

    int r = ri->loadRaw(false, tempImageNum, false, nullptr, 1.0, false);

    if (r) {
        delete ri;
        sensorType = ST_NONE;
        return nullptr;
    }

    if (ri->getFrameCount() == 7) {
        // special case for Hasselblad H6D-100cMS pixelshift files
        // first frame is not bayer, load second frame
        int r = ri->loadRaw(false, 1, false, nullptr, 1.0, false);

        if (r) {
            delete ri;
            sensorType = ST_NONE;
            return nullptr;
        }
    }
    sensorType = ri->getSensorType();

    int width = ri->get_width();
    int height = ri->get_height();
    rtengine::Thumbnail *tpp = new rtengine::Thumbnail;

    tpp->isRaw = true;
    tpp->sensorType = sensorType;
    tpp->embProfile = nullptr;
    tpp->embProfileData = nullptr;
    tpp->embProfileLength = 0;

    tpp->redMultiplier = ri->get_pre_mul (0);
    tpp->greenMultiplier = ri->get_pre_mul (1);
    tpp->blueMultiplier = ri->get_pre_mul (2);

    tpp->camwbRed = 1.0;
    tpp->camwbGreen = 1.0;
    tpp->camwbBlue = 1.0;
    tpp->defGain = 1.0;

    float pre_mul[4], scale_mul[4], cblack[4];
    ri->get_colorsCoeff (pre_mul, scale_mul, cblack, false);
    tpp->defGain = max(scale_mul[0], scale_mul[1], scale_mul[2], scale_mul[3]) / min (scale_mul[0], scale_mul[1], scale_mul[2], scale_mul[3]);
    tpp->defGain *= std::pow(2, ri->getBaselineExposure());
    
    int skip = 1;
    int firstgreen = 1;

    if (ri->get_FujiWidth() != 0) {
        if (fixwh == 1) { // fix height, scale width
            skip = ((ri->get_height() - ri->get_FujiWidth()) / sqrt (0.5) - firstgreen - 1) / h;
        } else {
            skip = (ri->get_FujiWidth() / sqrt (0.5) - firstgreen - 1) / w;
        }
    } else {
        if (fixwh == 1) { // fix height, scale width
            skip = (ri->get_height() - firstgreen - 1) / h;
        } else {
            skip = (ri->get_width() - firstgreen - 1) / w;
        }
    }

    if (skip % 2) {
        skip--;
    }

    if (skip < 2) {
        skip = 2;
    }

    int hskip = skip, vskip = skip;

    if (!ri->get_model().compare ("D1X")) {
        hskip *= 2;
    }

    int tmpw = (width - 2) / hskip;
    int tmph = (height - 2) / vskip;


    if (ri->get_FujiWidth() != 0) {
        int fw = ri->get_FujiWidth() / hskip;
        double step = sqrt (0.5);
        int wide = fw / step;
        int high = (tmph - fw) / step;
        tmpw = wide;
        tmph = high;
    }

    const bool rotate_90 =
        true
        && (
            ri->get_rotateDegree() == 90
            || ri->get_rotateDegree() == 270
        );

    if (rotate_90) {
        std::swap (tmpw, tmph);
    }

    if (fixwh == 1) { // fix height, scale width
        w = tmpw * h / tmph;
    } else {
        h = tmph * w / tmpw;
    }


    if (ri->get_FujiWidth() != 0) {
        tpp->scale = (double) (height - ri->get_FujiWidth()) * 2.0 / (rotate_90 ? w : h);
    } else {
        tpp->scale = (double) height / (rotate_90 ? w : h);
    }

    RawImageSource::computeFullSize(ri, TR_NONE, tpp->full_width, tpp->full_height);
    
    delete ri;
    return tpp;
}


} // namespace rtengine
