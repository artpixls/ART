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
#include "stdimagesource.h"
#include "mytime.h"
#include "iccstore.h"
#include "imageio.h"
#include "curves.h"
#include "color.h"
#include "imgiomanager.h"
#include "../rtgui/multilangmgr.h"
#ifdef _OPENMP
# include <omp.h>
#endif
#include "clutstore.h"

#undef THREAD_PRIORITY_NORMAL

namespace rtengine {

extern const Settings* settings;

template<class T> void freeArray (T** a, int H)
{
    for (int i = 0; i < H; i++) {
        delete [] a[i];
    }

    delete [] a;
}
template<class T> T** allocArray (int W, int H)
{

    T** t = new T*[H];

    for (int i = 0; i < H; i++) {
        t[i] = new T[W];
    }

    return t;
}

#define HR_SCALE 2
StdImageSource::StdImageSource():
    ImageSource(),
    img(nullptr),
    plistener(nullptr),
    full(false),
    max{},
    rgbSourceModified(false),
    imgCopy(nullptr)
{

    embProfile = nullptr;
    idata = nullptr;
}

StdImageSource::~StdImageSource ()
{

    delete idata;

    if (img) {
        delete img;
    }

    if (imgCopy) {
        delete imgCopy;
    }

    if (embProfile) {
        cmsCloseProfile(embProfile);
    }
}

void StdImageSource::getSampleFormat (const Glib::ustring &fname, IIOSampleFormat &sFormat, IIOSampleArrangement &sArrangement)
{

    sFormat = IIOSF_UNKNOWN;
    sArrangement = IIOSA_UNKNOWN;

    if (hasJpegExtension(fname)) {
        // For now, png and jpeg files are converted to unsigned short by the loader itself,
        // but there should be functions that read the sample format first, like the TIFF case below
        sFormat = IIOSF_UNSIGNED_CHAR;
        sArrangement = IIOSA_CHUNKY;
        return;
    } else if (hasPngExtension(fname)) {
        int result = ImageIO::getPNGSampleFormat (fname, sFormat, sArrangement);

        if (result == IMIO_SUCCESS) {
            return;
        }
    } else if (hasTiffExtension(fname)) {
        int result = ImageIO::getTIFFSampleFormat (fname, sFormat, sArrangement);

        if (result == IMIO_SUCCESS) {
            return;
        }
    }

    return;
}

/*
 * This method make define the correspondence between the input image type
 * and RT's image data type (Image8, Image16 and Imagefloat), then it will
 * load the image into it
 */
int StdImageSource::load(const Glib::ustring &fname, int maxw_hint, int maxh_hint)
{
    fileName = fname;

    // First let's find out the input image's type

    IIOSampleFormat sFormat;
    IIOSampleArrangement sArrangement;
    getSampleFormat(fileName, sFormat, sArrangement);

    bool loaded = false;
    // Then create the appropriate object

    switch (sFormat) {
    case (IIOSF_UNSIGNED_CHAR): {
        img = new Image8;
        break;
    }

    case (IIOSF_UNSIGNED_SHORT): {
        img = new Image16;
        break;
    }

    case (IIOSF_LOGLUV24):
    case (IIOSF_LOGLUV32):
    case (IIOSF_FLOAT16):
    case (IIOSF_FLOAT24):
    case (IIOSF_FLOAT32): {
        img = new Imagefloat;
        break;
    }

    default:
        if (!ImageIOManager::getInstance()->load(fname, plistener, img, maxw_hint, maxh_hint)) {
            return IMIO_FILETYPENOTSUPPORTED;
        } else {
            loaded = true;
            maxw_hint = maxh_hint = 0;
        }
    }

    if (!loaded) {
        img->setSampleFormat(sFormat);
        img->setSampleArrangement(sArrangement);

        if (plistener) {
            plistener->setProgressStr ("PROGRESSBAR_LOADING");
            plistener->setProgress (0.0);
            img->setProgressListener (plistener);
        }

        // And load the image!

        int error = img->load(fname, maxw_hint, maxh_hint);

        if (error) {
            delete img;
            img = nullptr;
            return error;
        }
    }

    if (embProfile) {
        cmsCloseProfile(embProfile);
    }
    embProfile = nullptr;
    if (img->getEmbeddedProfile()) {
        embProfile = ProfileContent(img->getEmbeddedProfile()).toProfile();
    }

    idata = new FramesData(fname);

    if (idata->hasExif()) {
        int deg = 0;

        if (idata->getOrientation() == "Rotate 90 CW") {
            deg = 90;
        } else if (idata->getOrientation() == "Rotate 180") {
            deg = 180;
        } else if (idata->getOrientation() == "Rotate 270 CW") {
            deg = 270;
        }

        if (deg) {
            img->rotate(deg);
        }
    }

    if (plistener) {
        plistener->setProgressStr ("PROGRESSBAR_READY");
        plistener->setProgress (1.0);
    }

    //this is probably a mistake if embedded profile is not D65
    wb = ColorTemp (1.0, 1.0, 1.0, 1.0);

    return 0;
}

int StdImageSource::load(const Glib::ustring &fname)
{
    return load(fname, 0, 0);
}

void StdImageSource::getImage (const ColorTemp &ctemp, int tran, Imagefloat* image, const PreviewProps &pp, const ExposureParams &hrp, const RAWParams &raw)
{

    // the code will use OpenMP as of now.

    img->getStdImage(ctemp, tran, image, pp);

    // Hombre: we could have rotated the image here too, with just few line of code, but:
    // 1. it would require other modifications in the engine, so "do not touch that little plonker!"
    // 2. it's more optimized like this

    // Flip if needed
    if (tran & TR_HFLIP) {
        image->hflip();
    }

    if (tran & TR_VFLIP) {
        image->vflip();
    }
}

void StdImageSource::convertColorSpace(Imagefloat* image, const ColorManagementParams &cmp, const ColorTemp &wb)
{
    colorSpaceConversion(image, cmp, embProfile, img->getSampleFormat(), plistener, true);
}


namespace {

class ARTInputProfile {
public:
    ARTInputProfile(cmsHPROFILE prof, const procparams::ColorManagementParams &icm):
        mode_(MODE_INVALID),
        tc_(nullptr)
    {
        auto iws = ICCStore::getInstance()->workingSpaceInverseMatrix(icm.workingProfile);
        Mat33<float> m;
        float g = 0, s = 0;
        cmsCIEXYZ bp;
        if (ICCStore::getProfileMatrix(prof, m) && ICCStore::getProfileParametricTRC(prof, g, s) && (!cmsDetectDestinationBlackPoint(&bp, prof, INTENT_RELATIVE_COLORIMETRIC, 0) || (bp.X == 0 && bp.Y == 0 && bp.Z == 0))) {
            if (g == -2) {
                mode_ = MODE_PQ;
            } else if (g == -1) {
                mode_ = MODE_HLG;
            } else if (g == 1 && s == 0) {
                mode_ = MODE_LINEAR;
            } else {
                mode_ = MODE_GAMMA;
                LMCSToneCurveParams params;
                Color::compute_LCMS_tone_curve_params(g, s, params);
                tc_ = cmsBuildParametricToneCurve(0, 5, &params[0]);
                if (!tc_) {
                    mode_ = MODE_INVALID;
                }
            }
            matrix_ = dot_product(iws, m);
        }
    }

    ~ARTInputProfile()
    {
        if (tc_) {
            cmsFreeToneCurve(tc_);
        }
    }

    operator bool() const { return mode_ != MODE_INVALID; }
    
    void operator()(const Imagefloat *src, Imagefloat *dst, bool multiThread)
    {
        const int W = src->getWidth();
        const int H = src->getHeight();

#ifdef _OPENMP
#       pragma omp parallel for if (multiThread)
#endif
        for (int y = 0; y < H; ++y) {
            Vec3<float> rgb;
            for (int x = 0; x < W; ++x) {
                rgb[0] = src->r(y, x) / 65535.f;
                rgb[1] = src->g(y, x) / 65535.f;
                rgb[2] = src->b(y, x) / 65535.f;

                for (int i = 0; i < 3; ++i) {
                    rgb[i] = eval(rgb[i]);
                }

                rgb = dot_product(matrix_, rgb);

                dst->r(y, x) = rgb[0] * 65535.f;
                dst->g(y, x) = rgb[1] * 65535.f;
                dst->b(y, x) = rgb[2] * 65535.f;
            }
        }
    }

    void operator()(const float *src, float *dst, int W)
    {
        Vec3<float> rgb;
        const int W3 = W * 3;
        for (int x = 0; x < W3; x += 3) {
            rgb[0] = src[x];
            rgb[1] = src[x+1];
            rgb[2] = src[x+2];

            for (int i = 0; i < 3; ++i) {
                rgb[i] = eval(rgb[i]);
            }

            rgb = dot_product(matrix_, rgb);

            dst[x] = rgb[0];
            dst[x+1] = rgb[1];
            dst[x+2] = rgb[2];
        }
    }

private:
    float eval(float x)
    {
        switch (mode_) {
        case MODE_LINEAR:
            return x;
        case MODE_PQ:
            return Color::eval_PQ_curve(x, false);
        case MODE_HLG:
            return Color::eval_HLG_curve(x, false);
        default: // MODE_GAMMA
            return cmsEvalToneCurveFloat(tc_, x);
        }
    }
    
    enum Mode { MODE_INVALID, MODE_LINEAR, MODE_GAMMA, MODE_HLG, MODE_PQ };
    Mode mode_;
    Mat33<float> matrix_;
    cmsToneCurve *tc_;
};

} // namespace

void StdImageSource::colorSpaceConversion(Imagefloat* im, const ColorManagementParams &cmp, cmsHPROFILE embedded, IIOSampleFormat sampleFormat, ProgressListener *plistener)
{
    colorSpaceConversion(im, cmp, embedded, sampleFormat, plistener, false);
}

void StdImageSource::colorSpaceConversion (Imagefloat* im, const ColorManagementParams &cmp, cmsHPROFILE embedded, IIOSampleFormat sampleFormat, ProgressListener *plistener, bool multithread)
{

    bool skipTransform = false;
    cmsHPROFILE in = nullptr;
    cmsHPROFILE out = ICCStore::getInstance()->workingSpace (cmp.workingProfile);

    if (cmp.inputProfile == "(embedded)" || cmp.inputProfile == "" || cmp.inputProfile == "(camera)" || cmp.inputProfile == "(cameraICC)") {
        if (embedded) {
            in = embedded;
        } else {
            if (sampleFormat & (IIOSF_LOGLUV24 | IIOSF_LOGLUV32)) {// | IIOSF_FLOAT16 | IIOSF_FLOAT24 | IIOSF_FLOAT32)) {
                skipTransform = true;
            } else {
                in = ICCStore::getInstance()->getsRGBProfile ();
            }
        }
    } else {
        if (cmp.inputProfile != "(none)") {
            in = ICCStore::getInstance()->getProfile (cmp.inputProfile);
#ifdef ART_USE_OCIO
            if (!in) {
                int num_threads = 1;
#ifdef _OPENMP
                num_threads = multithread ? omp_get_num_procs() : 1;
#endif // _OPENMP
                OCIOInputProfile lut(cmp.inputProfile, cmp.workingProfile, num_threads);
                if (lut) {
                    lut(im);
                    return;
                }
            }
#endif // ART_USE_OCIO
            if (!in && plistener) {
                plistener->error(Glib::ustring::compose(M("ERROR_MSG_FILE_READ"), cmp.inputProfile));
            }

            if (in == nullptr && embedded) {
                in = embedded;
            } else if (in == nullptr) {
                if (sampleFormat & (IIOSF_LOGLUV24 | IIOSF_LOGLUV32 | IIOSF_FLOAT16 | IIOSF_FLOAT24 | IIOSF_FLOAT32)) {
                    skipTransform = true;
                } else {
                    in = ICCStore::getInstance()->getsRGBProfile ();
                }
            }
        }
    }

    if (!skipTransform && in) {
        if(in == embedded && cmsGetColorSpace(in) != cmsSigRgbData) { // if embedded profile is not an RGB profile, use sRGB
            printf("embedded profile is not an RGB profile, using sRGB as input profile\n");
            in = ICCStore::getInstance()->getsRGBProfile ();
        }

        lcmsMutex->lock();
        ARTInputProfile artprof(in, cmp);
        cmsHTRANSFORM hTransform = nullptr;
        if (!artprof) {
            hTransform = cmsCreateTransform (in, TYPE_RGB_FLT, out, TYPE_RGB_FLT, INTENT_RELATIVE_COLORIMETRIC,
                                                       cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE);
        }
        lcmsMutex->unlock();

        if (artprof) {
            if (settings->verbose) {
                printf("stdimagesource: ART ICC profile detected, using built-in color space conversion\n");
            }
            artprof(im, im, multithread);
        } else if (hTransform) {
            // Convert to the [0.0 ; 1.0] range
            im->normalizeFloatTo1();

            im->ExecCMSTransform(hTransform, multithread);

            // Converting back to the [0.0 ; 65535.0] range
            im->normalizeFloatTo65535();

            cmsDeleteTransform(hTransform);
        } else {
            printf("Could not convert from %s to %s\n", in == embedded ? "embedded profile" : cmp.inputProfile.data(), cmp.workingProfile.data());
        }
    }
}

void StdImageSource::getFullSize (int& w, int& h, int tr)
{

    w = img->getWidth();
    h = img->getHeight();

    if ((tr & TR_ROT) == TR_R90 || (tr & TR_ROT) == TR_R270) {
        w = img->getHeight();
        h = img->getWidth();
    }
}

void StdImageSource::getSize (const PreviewProps &pp, int& w, int& h)
{
    w = pp.getWidth() / pp.getSkip() + (pp.getWidth() % pp.getSkip() > 0);
    h = pp.getHeight() / pp.getSkip() + (pp.getHeight() % pp.getSkip() > 0);
}


void StdImageSource::getAutoWBMultipliers (double &rm, double &gm, double &bm)
{
    if (redAWBMul != -1.) {
        rm = redAWBMul;
        gm = greenAWBMul;
        bm = blueAWBMul;
        return;
    }

    img->getAutoWBMultipliers(rm, gm, bm);

    wbMul2Camera(rm, gm, bm);
    rm = LIM(rm, 0.0, MAX_WB_MUL);
    gm = LIM(gm, 0.0, MAX_WB_MUL);
    bm = LIM(bm, 0.0, MAX_WB_MUL);
    wbCamera2Mul(rm, gm, bm);
    
    redAWBMul = rm;
    greenAWBMul = gm;
    blueAWBMul = bm;
}

ColorTemp StdImageSource::getSpotWB (std::vector<Coord2D> &red, std::vector<Coord2D> &green, std::vector<Coord2D>& blue, int tran, double equal)
{
    int rn, gn, bn;
    double reds, greens, blues;
    img->getSpotWBData(reds, greens, blues, rn, gn, bn, red, green, blue, tran);
    double img_r, img_g, img_b;
    wb.getMultipliers (img_r, img_g, img_b);

    if( settings->verbose ) {
        printf ("AVG: %g %g %g\n", reds / rn, greens / gn, blues / bn);
    }

    return ColorTemp (reds / rn * img_r, greens / gn * img_g, blues / bn * img_b, equal);
}

void StdImageSource::flushRGB()
{
    img->allocate(0, 0);
    if (imgCopy) {
        delete imgCopy;
        imgCopy = nullptr;
    }
}


void StdImageSource::wbMul2Camera(double &rm, double &gm, double &bm)
{
    rm = 1.0 / rm;
    gm = 1.0 / gm;
    bm = 1.0 / bm;
}


void StdImageSource::wbCamera2Mul(double &rm, double &gm, double &bm)
{
    rm = 1.0 / rm;
    gm = 1.0 / gm;
    bm = 1.0 / bm;
}

} // namespace rtengine
