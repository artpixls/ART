/* -*- C++ -*-
 *  
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
#ifndef _RAWIMAGESOURCE_
#define _RAWIMAGESOURCE_

#include "imagesource.h"
#include "dcp.h"
#include "array2D.h"
#include "curves.h"
#include "color.h"
#include "iimage.h"
#include <iostream>
#include "pixelsmap.h"
#define HR_SCALE 2

namespace rtengine
{

class RawImageSource : public ImageSource
{

private:
    static DiagonalCurve *phaseOneIccCurve;
    static DiagonalCurve *phaseOneIccCurveInv;
    static LUTf invGrad;  // for fast_demosaic
    static LUTf initInvGrad ();
    static void colorSpaceConversion_(Imagefloat* im, const ColorManagementParams& cmp, const ColorTemp &wb, double pre_mul[3], cmsHPROFILE camprofile, double cam[3][3], cmsHPROFILE in, DCPProfile *dcpProf, ProgressListener *plistener, bool multithread);

protected:
    static int defTransform(const RawImage *ri, int tran);
    
    MyMutex getImageMutex;  // locks getImage

    int W, H;
    ColorTemp camera_wb;
    ProgressListener* plistener;
    float scale_mul[4]; // multiplier for each color
    float c_black[4]; // copy of cblack Dcraw for black level
    float c_white[4];
    float cblacksom[4];
    float ref_pre_mul[4];
    double refwb_red;
    double refwb_green;
    double refwb_blue;
    double rgb_cam[3][3];
    double cam_rgb[3][3];
    double xyz_cam[3][3];
    double cam_xyz[3][3];
    bool fuji;
    bool d1x;
    int border;
    float chmax[4], hlmax[4], clmax[4];
    double initialGain; // initial gain calculated after scale_colors
    double camInitialGain;
    double defGain;
    cmsHPROFILE camProfile;
    bool rgbSourceModified;

    RawImage* ri;  // Copy of raw pixels, NOT corrected for initial gain, blackpoint etc.
    RawImage* riFrames[6] = {nullptr};
    unsigned int currFrame = 0;
    unsigned int numFrames = 0;
    int flatFieldAutoClipValue = 0;
    array2D<float> rawData;  // holds preprocessed pixel values, rowData[i][j] corresponds to the ith row and jth column
    array2D<float> *rawDataFrames[6] = {nullptr};
    array2D<float> *rawDataBuffer[5] = {nullptr};

    // the interpolated green plane:
    array2D<float> green;
    // the interpolated red plane:
    array2D<float> red;
    // the interpolated blue plane:
    array2D<float> blue;
    bool rawDirty;
    float psRedBrightness[4];
    float psGreenBrightness[4];
    float psBlueBrightness[4];

    std::vector<double> histMatchingCache;
    std::vector<double> histMatchingCache2;
    ColorManagementParams histMatchingParams;

    void processFalseColorCorrectionThread (Imagefloat* im, array2D<float> &rbconv_Y, array2D<float> &rbconv_I, array2D<float> &rbconv_Q, array2D<float> &rbout_I, array2D<float> &rbout_Q, const int row_from, const int row_to);
    void hlRecovery(float* red, float* green, float* blue, int width, float* hlmax);
    void transformRect       (const PreviewProps &pp, int tran, int &sx1, int &sy1, int &width, int &height, int &fw);
    void transformPosition   (int x, int y, int tran, int& tx, int& ty);

    unsigned FC(int row, int col) const
    {
        return ri->FC(row, col);
    }
    inline void getRowStartEnd (int x, int &start, int &end);
    static void getProfilePreprocParams(cmsHPROFILE in, float& gammafac, float& lineFac, float& lineSum);

    void HLRecovery_inpaint(int blur);
    void highlight_recovery_opposed(float scale_mul[3], const ColorTemp &wb);

public:
    RawImageSource ();
    ~RawImageSource () override;

    int load(const Glib::ustring &fname) override { return load(fname, false); }
    int load(const Glib::ustring &fname, bool firstFrameOnly);
    void preprocess(const RAWParams &raw, const LensProfParams &lensProf, const CoarseTransformParams& coarse, bool prepareDenoise=true, const ColorTemp &wb=ColorTemp()) override;
    void demosaic(const RAWParams &raw, bool autoContrast, double &contrastThreshold) override;
    void flushRawData() override;
    void flushRGB() override;
    void HLRecovery_Global(const ExposureParams &hrp) override;
    void refinement(int PassCount);
    void setBorder(unsigned int rawBorder) override {border = rawBorder;}
    bool isRGBSourceModified() const override
    {
        return rgbSourceModified;   // tracks whether cached rgb output of demosaic has been modified
    }

    void        processFlatField(const RAWParams &raw, RawImage *riFlatFile, array2D<float> &rawData, unsigned short black[4]);
    void        copyOriginalPixels(const RAWParams &raw, RawImage *ri, RawImage *riDark, RawImage *riFlatFile, array2D<float> &rawData  );
    void        cfaboxblur  (RawImage *riFlatFile, float* cfablur, int boxH, int boxW);
    void        scaleColors (int winx, int winy, int winw, int winh, const RAWParams &raw, array2D<float> &rawData); // raw for cblack

    void        getImage    (const ColorTemp &ctemp, int tran, Imagefloat* image, const PreviewProps &pp, const ExposureParams &hrp, const RAWParams &raw) override;
    eSensorType getSensorType () const override
    {
        return ri != nullptr ? ri->getSensorType() : ST_NONE;
    }
    bool        isMono () const override
    {
        return ri->get_colors() == 1;
    }
    ColorTemp   getWB       () const override
    {
        return camera_wb;
    }
    void        getAutoWBMultipliers (double &rm, double &gm, double &bm) override;
    ColorTemp   getSpotWB   (std::vector<Coord2D> &red, std::vector<Coord2D> &green, std::vector<Coord2D> &blue, int tran, double equal) override;
    bool        isWBProviderReady () override
    {
        return rawData;
    }

    double      getDefGain  () const override
    {
        return defGain;
    }

    void        getFullSize (int& w, int& h, int tr = TR_NONE) override;
    void        getSize     (const PreviewProps &pp, int& w, int& h) override;
    int         getRotateDegree() const override
    {
        return ri->get_rotateDegree();
    }

    ImageMatrices* getImageMatrices () override
    {
        return &imatrices;
    }
    bool        isRAW() const override
    {
        return true;
    }

    void        setProgressListener (ProgressListener* pl) override
    {
        plistener = pl;
    }
    void        getRAWHistogram (LUTu & histRedRaw, LUTu & histGreenRaw, LUTu & histBlueRaw) override;
    void getAutoMatchedToneCurve(const ColorManagementParams &cp, std::vector<double> &outCurve, std::vector<double> &outCurve2) override;
    DCPProfile *getDCP(const ColorManagementParams &cmp, DCPProfile::ApplyState &as) override;

    void convertColorSpace(Imagefloat* image, const ColorManagementParams &cmp, const ColorTemp &wb) override;
    static bool findInputProfile(Glib::ustring inProfile, cmsHPROFILE embedded, std::string camName, const Glib::ustring &filename, DCPProfile **dcpProf, cmsHPROFILE& in, ProgressListener *plistener=nullptr);
    static void colorSpaceConversion(Imagefloat* im, const ColorManagementParams& cmp, const ColorTemp &wb, double pre_mul[3], cmsHPROFILE embedded, cmsHPROFILE camprofile, double cam[3][3], const std::string &camName, const Glib::ustring &fileName, ProgressListener *plistener=nullptr);
    static void inverse33 (const double (*coeff)[3], double (*icoeff)[3]);

    static void HLRecovery_blend (float* rin, float* gin, float* bin, int width, float maxval, float* hlmax);
    static void init ();
    static void cleanup ();
    void setCurrentFrame(unsigned int frameNum) override {
        if (numFrames == 2 && frameNum == 2) { // special case for averaging of two frames
            currFrame = frameNum;
            ri = riFrames[0];
        } else  {
            currFrame = std::min(numFrames - 1, frameNum);
            ri = riFrames[currFrame];
        }
    }
    int getFrameCount() override {return numFrames;}
    int getFlatFieldAutoClipValue() override {return flatFieldAutoClipValue;}

    class GreenEqulibrateThreshold {
    public:
        explicit GreenEqulibrateThreshold(float thresh): thresh_(thresh) {}
        virtual ~GreenEqulibrateThreshold() {}
        virtual float operator()(int row, int column) const { return thresh_; }
    protected:
        const float thresh_;
    };

    class CFALineDenoiseRowBlender {
    public:
        virtual ~CFALineDenoiseRowBlender() {}
        virtual float operator()(int row) const { return 1.f; }
    };
    
    static void computeFullSize(const RawImage *ri, int tr, int &w, int &h, int border=-1);

protected:
    typedef unsigned short ushort;
    void processFalseColorCorrection (Imagefloat* i, const int steps);
    inline  void convert_row_to_YIQ (const float* const r, const float* const g, const float* const b, float* Y, float* I, float* Q, const int W);
    inline  void convert_row_to_RGB (float* r, float* g, float* b, const float* const Y, const float* const I, const float* const Q, const int W);
    inline  void convert_to_RGB (float &r, float &g, float &b, const float Y, const float I, const float Q);

    inline  void interpolate_row_g (float* agh, float* agv, int i);
    inline  void interpolate_row_rb (float* ar, float* ab, float* pg, float* cg, float* ng, int i);
    inline  void interpolate_row_rb_mul_pp (const array2D<float> &rawData, float* ar, float* ab, float* pg, float* cg, float* ng, int i, float r_mul, float g_mul, float b_mul, int x1, int width, int skip);

    float* CA_correct_RT(
        bool autoCA,
        size_t autoIterations,
        double cared,
        double cablue,
        bool avoidColourshift,
        const array2D<float> &rawData,
        double* fitParamsTransfer,
        bool fitParamsIn,
        bool fitParamsOut,
        float* buffer,
        bool freeBuffer
    );
    void ddct8x8s(int isgn, float a[8][8]);

    int interpolateBadPixelsBayer(const PixelsMap &bitmapBads, array2D<float> &rawData);
    int interpolateBadPixelsNColours(const PixelsMap &bitmapBads, int colours);
    int interpolateBadPixelsXtrans(const PixelsMap &bitmapBads);
    int findHotDeadPixels(PixelsMap &bpMap, float thresh, bool findHotPixels, bool findDeadPixels) const;
    int findZeroPixels(PixelsMap &bpMap) const;
    void cfa_linedn (float linenoiselevel, bool horizontal, bool vertical, const CFALineDenoiseRowBlender &rowblender);//Emil's line denoise

    void green_equilibrate_global (array2D<float> &rawData);
    void green_equilibrate (const GreenEqulibrateThreshold &greenthresh, array2D<float> &rawData);//Emil's green equilibration

    void nodemosaic(bool bw);
    void eahd_demosaic();
    void hphd_demosaic();
    void vng4_demosaic(const array2D<float> &rawData, array2D<float> &red, array2D<float> &green, array2D<float> &blue);
    void ppg_demosaic();
    void jdl_interpolate_omp();
    void igv_interpolate(int winw, int winh);
    void lmmse_interpolate_omp(int winw, int winh, const array2D<float> &rawData, array2D<float> &red, array2D<float> &green, array2D<float> &blue, int iterations);
    void amaze_demosaic_RT(int winx, int winy, int winw, int winh, const array2D<float> &rawData, array2D<float> &red, array2D<float> &green, array2D<float> &blue);//Emil's code for AMaZE
    void dual_demosaic_RT(bool isBayer, const RAWParams &raw, int winw, int winh, const array2D<float> &rawData, array2D<float> &red, array2D<float> &green, array2D<float> &blue, double &contrast, bool autoContrast = false);
    void fast_demosaic();//Emil's code for fast demosaicing
    void dcb_demosaic(int iterations, bool dcb_enhance);
    void ahd_demosaic();
    void rcd_demosaic();
    void border_interpolate(unsigned int border, float (*image)[4], unsigned int start = 0, unsigned int end = 0);
    void border_interpolate2(int winw, int winh, int lborders, const array2D<float> &rawData, array2D<float> &red, array2D<float> &green, array2D<float> &blue);
    void dcb_initTileLimits(int &colMin, int &rowMin, int &colMax, int &rowMax, int x0, int y0, int border);
    void fill_raw( float (*cache )[3], int x0, int y0, float** rawData);
    void fill_border( float (*cache )[3], int border, int x0, int y0);
    void copy_to_buffer(float (*image2)[2], float (*image)[3]);
    void dcb_hid(float (*image)[3], int x0, int y0);
    void dcb_color(float (*image)[3], int x0, int y0);
    void dcb_hid2(float (*image)[3], int x0, int y0);
    void dcb_map(float (*image)[3], uint8_t *map, int x0, int y0);
    void dcb_correction(float (*image)[3], uint8_t *map, int x0, int y0);
    void dcb_pp(float (*image)[3], int x0, int y0);
    void dcb_correction2(float (*image)[3], uint8_t *map, int x0, int y0);
    void restore_from_buffer(float (*image)[3], float (*image2)[2]);
    void dcb_refinement(float (*image)[3], uint8_t *map, int x0, int y0);
    void dcb_color_full(float (*image)[3], int x0, int y0, float (*chroma)[2]);
    void cielab (const float (*rgb)[3], float* l, float* a, float *b, const int width, const int height, const int labWidth, const float xyz_cam[3][3]);
    void xtransborder_interpolate (int border, array2D<float> &red, array2D<float> &green, array2D<float> &blue);
    void xtrans_interpolate (const int passes, const bool useCieLab);
    void fast_xtrans_interpolate (const array2D<float> &rawData, array2D<float> &red, array2D<float> &green, array2D<float> &blue);
    void fast_xtrans_interpolate_blend (const float* const * blend, const array2D<float> &rawData, array2D<float> &red, array2D<float> &green, array2D<float> &blue);
    void pixelshift(int winx, int winy, int winw, int winh, const RAWParams &rawParams, unsigned int frame, const std::string &make, const std::string &model, float rawWpCorrection);
    void bayer_bilinear_demosaic(const float *const * blend, const array2D<float> &rawData, array2D<float> &red, array2D<float> &green, array2D<float> &blue);
    void    hflip       (Imagefloat* im);
    void    vflip       (Imagefloat* im);
    void getRawValues(int x, int y, int rotate, int &R, int &G, int &B) override;

    bool getDeconvAutoRadius(float *out=nullptr) override;

    void apply_gain_map(unsigned short black[4], std::vector<GainMap> &&maps);

public:
    float get_pre_mul(int c) const { return ri ? ri->get_pre_mul(c) : 1.f; }

    void wbMul2Camera(double &rm, double &gm, double &bm) override;
    void wbCamera2Mul(double &rm, double &gm, double &bm) override;    

    void getWBMults(const ColorTemp &ctemp, const procparams::RAWParams &raw, std::array<float, 4>& scale_mul, float &autoGainComp, float &rm, float &gm, float &bm) const override;
};

} // namespace rtengine
#endif
