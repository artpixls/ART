/*
 *  This file is part of RawTherapee.
 *
 *  Created on: 20/nov/2010
 */

#include <strings.h>
#ifdef WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

#include "rawimage.h"
#include "settings.h"
#include "camconst.h"
#include "utils.h"
#include "metadata.h"
#include "image8.h"
#include "imagedata.h"
#include "imgiomanager.h"

#ifdef ART_USE_LIBRAW
# include <libraw.h>
#endif // ART_USE_LIBRAW


namespace rtengine {

extern const Settings *settings;
extern MyMutex *librawMutex;


RawImage::RawImage(const Glib::ustring &name)
    : DCraw()
    , data(nullptr)
    , prefilters(0)
    , filename(name)
    , rotate_deg(0)
    , profile_data(nullptr)
    , allocation(nullptr)
    , thumb_data(nullptr)
    , use_internal_decoder_(true)
    , use_imgio_(ThreeValBool::X)
    , imgio_filename_("")
{
    profile_length = 0;
    memset(maximum_c4, 0, sizeof(maximum_c4));
    memset(white, 0, sizeof(white));
    RT_matrix_from_constant = ThreeValBool::X;
    RT_blacklevel_from_constant = ThreeValBool::X;
    RT_whitelevel_from_constant = ThreeValBool::X;
    memset(make, 0, sizeof(make));
    memset(model, 0, sizeof(model));
}


RawImage::~RawImage()
{
    if (ifp) {
        fclose(ifp);
        ifp = nullptr;
    }

    if (image && use_internal_decoder_) {
        free(image);
    }

    if (allocation) {
        delete[] allocation;
        allocation = nullptr;
    }

    if (float_raw_image) {
        delete[] float_raw_image;
        float_raw_image = nullptr;
    }

    if (data) {
        delete[] data;
        data = nullptr;
    }

    if (profile_data) {
        delete[] profile_data;
        profile_data = nullptr;
    }

    if (thumb_data) {
        delete[] thumb_data;
    }
}


void RawImage::pre_interpolate()
{
    int w = width, h = height;
    if (!use_internal_decoder_) {
        width = iwidth;
        height = iheight;
    }
    DCraw::pre_interpolate();
    width = w;
    height = h;
}


eSensorType RawImage::getSensorType() const
{
    if (isBayer()) {
        return ST_BAYER;
    } else if (isXtrans()) {
        return ST_FUJI_XTRANS;
    } else if (isFoveon()) {
        return ST_FOVEON;
    }

    return ST_NONE;
}

/* Similar to dcraw scale_colors for coeff. calculation, but without actual pixels scaling.
 * need pixels in data[][] available
 */
void RawImage::get_colorsCoeff( float *pre_mul_, float *scale_mul_, float *cblack_, bool forceAutoWB)
{
    unsigned sum[8], c;
    unsigned W = this->get_width();
    unsigned H = this->get_height();
    float val;
    double dsum[8], dmin, dmax;

    if(isXtrans()) {
        // for xtrans files dcraw stores black levels in cblack[6] .. cblack[41], but all are equal, so we just use cblack[6]
        for (int c = 0; c < 4; c++) {
            if (cblack_ ) {
                cblack_[c] = (float) this->get_cblack(6);
            }
            if (pre_mul_) {
                pre_mul_[c] = this->get_pre_mul(c);
            }
        }
    } else if ((this->get_cblack(4) + 1) / 2 == 1 && (this->get_cblack(5) + 1) / 2 == 1) {
        if (cblack_) {
            for (int c = 0; c < 4; c++) {
                cblack_[c] = this->get_cblack(c);
            }
        }
        for (int c = 0; c < 4; c++) {
            if (cblack_) {
                cblack_[FC(c / 2, c % 2)] = this->get_cblack(6 + c / 2 % this->get_cblack(4) * this->get_cblack(5) + c % 2 % this->get_cblack(5));
            }
            if (pre_mul_) {
                pre_mul_[c] = this->get_pre_mul(c);
            }
        }
    } else {
        for (int c = 0; c < 4; c++) {
            if (cblack_) {
                cblack_[c] = (float) this->get_cblack(c);
            }
            if (pre_mul_) {
                pre_mul_[c] = this->get_pre_mul(c);
            }
        }
    }

    if (!cblack_ || !pre_mul_ || !scale_mul_) {
        return;
    }

    if (this->get_cam_mul(0) == -1 || forceAutoWB) {
        if(!data) { // this happens only for thumbnail creation when get_cam_mul(0) == -1
            compress_image(0, false);
        }
        memset(dsum, 0, sizeof dsum);

        constexpr float blackThreshold = 8.f;
        constexpr float whiteThreshold = 25.f;
        if (this->isBayer()) {
            // calculate number of pixels per color
            dsum[FC(0, 0) + 4] += (int)(((W + 1) / 2) * ((H + 1) / 2));
            dsum[FC(0, 1) + 4] += (int)(((W / 2) * ((H + 1) / 2)));
            dsum[FC(1, 0) + 4] += (int)(((W + 1) / 2) * (H / 2));
            dsum[FC(1, 1) + 4] += (int)((W / 2) * (H / 2));

#ifdef _OPENMP
            #pragma omp parallel private(val)
#endif
            {
                double dsumthr[8];
                memset(dsumthr, 0, sizeof dsumthr);
                float sum[4];
                // make local copies of the black and white values to avoid calculations and conversions
                float cblackfloat[4];
                float whitefloat[4];

                for (int c = 0; c < 4; c++) {
                    cblackfloat[c] = cblack_[c] + blackThreshold;
                    whitefloat[c] = this->get_white(c) - whiteThreshold;
                }

                float *tempdata = data[0];
#ifdef _OPENMP
                #pragma omp for nowait
#endif

                for (size_t row = 0; row < H; row += 8) {
                    size_t ymax = row + 8 < H ? row + 8 : H;

                    for (size_t col = 0; col < W ; col += 8) {
                        size_t xmax = col + 8 < W ? col + 8 : W;
                        memset(sum, 0, sizeof sum);

                        for (size_t y = row; y < ymax; y++)
                            for (size_t x = col; x < xmax; x++) {
                                int c = FC(y, x);
                                val = tempdata[y * W + x];

                                if (val > whitefloat[c] || val < cblackfloat[c]) { // calculate number of pixels to be subtracted from sum and skip the block
                                    dsumthr[FC(row, col) + 4]      += (int)(((xmax - col + 1) / 2) * ((ymax - row + 1) / 2));
                                    dsumthr[FC(row, col + 1) + 4]    += (int)(((xmax - col) / 2) * ((ymax - row + 1) / 2));
                                    dsumthr[FC(row + 1, col) + 4]    += (int)(((xmax - col + 1) / 2) * ((ymax - row) / 2));
                                    dsumthr[FC(row + 1, col + 1) + 4]  += (int)(((xmax - col) / 2) * ((ymax - row) / 2));
                                    goto skip_block2;
                                }

                                sum[c] += val;
                            }

                        for (int c = 0; c < 4; c++) {
                            dsumthr[c] += sum[c];
                        }

skip_block2:
                        ;
                    }
                }

#ifdef _OPENMP
                #pragma omp critical
#endif
                {
                    for (int c = 0; c < 4; c++) {
                        dsum[c] += dsumthr[c];
                    }

                    for (int c = 4; c < 8; c++) {
                        dsum[c] -= dsumthr[c];
                    }

                }
            }

            for(int c = 0; c < 4; c++) {
                dsum[c] -= cblack_[c] * dsum[c + 4];
            }

        } else if(isXtrans()) {
#ifdef _OPENMP
            #pragma omp parallel
#endif
            {
                double dsumthr[8];
                memset(dsumthr, 0, sizeof dsumthr);
                float sum[8];
                // make local copies of the black and white values to avoid calculations and conversions
                float cblackfloat[4];
                float whitefloat[4];

                for (int c = 0; c < 4; c++)
                {
                    cblackfloat[c] = cblack_[c] + blackThreshold;
                    whitefloat[c] = this->get_white(c) - whiteThreshold;
                }

#ifdef _OPENMP
                #pragma omp for nowait
#endif

                for (size_t row = 0; row < H; row += 8)
                    for (size_t col = 0; col < W ; col += 8)
                    {
                        memset(sum, 0, sizeof sum);

                        for (size_t y = row; y < row + 8 && y < H; y++)
                            for (size_t x = col; x < col + 8 && x < W; x++) {
                                int c = XTRANSFC(y, x);
                                float val = data[y][x];

                                if (val > whitefloat[c] || val < cblackfloat[c]) {
                                    goto skip_block3;
                                }
                                
                                val -= cblack_[c];

                                sum[c] += val;
                                sum[c + 4]++;
                            }

                        for (int c = 0; c < 8; c++) {
                            dsumthr[c] += sum[c];
                        }

skip_block3:
                        ;
                    }

#ifdef _OPENMP
                #pragma omp critical
#endif
                {
                    for (int c = 0; c < 8; c++)
                    {
                        dsum[c] += dsumthr[c];
                    }

                }
            }
        } else if (colors == 1) {
            for (int c = 0; c < 4; c++) {
                pre_mul_[c] = 1;
            }
        } else {
            for (size_t row = 0; row < H; row += 8)
                for (size_t col = 0; col < W ; col += 8) {
                    memset(sum, 0, sizeof sum);

                    for (size_t y = row; y < row + 8 && y < H; y++)
                        for (size_t x = col; x < col + 8 && x < W; x++)
                            for (int c = 0; c < 3; c++) {
                                val = data[y][3 * x + c];

                                if (val > this->get_white(c) - whiteThreshold || val < cblack_[c] + blackThreshold) {
                                    goto skip_block;
                                }

                                val -= cblack_[c];

                                sum[c] += val;
                                sum[c + 4]++;
                            }

                    for (c = 0; c < 8; c++) {
                        dsum[c] += sum[c];
                    }

skip_block:
                    ;
                }
        }

        for (int c = 0; c < 4; c++)
            if (dsum[c]) {
                pre_mul_[c] = dsum[c + 4] / dsum[c];
            }
    } else {
        memset(sum, 0, sizeof sum);

        for (size_t row = 0; row < 8; row++) {
            for (size_t col = 0; col < 8; col++) {
                int c = FC(row, col);

                if ((val = white[row][col] - cblack_[c]) > 0) {
                    sum[c] += val;
                }

                sum[c + 4]++;
            }
        }

        if (sum[0] && sum[1] && sum[2] && sum[3]) {
            for (int c = 0; c < 4; c++) {
                pre_mul_[c] = (float) sum[c + 4] / sum[c];
            }
        } else if (this->get_cam_mul(0) && this->get_cam_mul(2)) {
            pre_mul_[0] = this->get_cam_mul(0);
            pre_mul_[1] = this->get_cam_mul(1);
            pre_mul_[2] = this->get_cam_mul(2);
            pre_mul_[3] = this->get_cam_mul(3);
        } else if (colors == 1) {
            auto p = max(pre_mul_[0], pre_mul_[1], pre_mul_[2], pre_mul_[3]);
            for (int i = 0; i < 4; ++i) {
                pre_mul_[i] = p;
            }
        } else {
            // try getting from makernotes
            bool ok = true;
            float cmul[4];
            try {
                Exiv2Metadata md(get_filename());
                auto m = md.getMakernotes();
                auto it = m.find("WB_RGGBLevelsAsShot");
                if (it != m.end()) {
                    std::istringstream src(it->second);
                    for (int i = 0; i < 4; ++i) {
                        if (!(src >> cmul[i])) {
                            ok = false;
                            break;
                        }
                    }
                    std::swap(cmul[2], cmul[3]);
                } else {
                    ok = false;
                }
            } catch (std::exception &) {
                ok = false;
            }
            if (!ok) {
                fprintf(stderr, "Cannot use camera white balance.\n");
            } else {
                for (int i = 0; i < 4; ++i) {
                    pre_mul_[i] = cam_mul[i] = cmul[i];
                }
            }
        }
    }

    if (pre_mul_[3] == 0) {
        pre_mul_[3] = this->get_colors() < 4 ? pre_mul_[1] : 1;
    } else if (this->get_colors() < 4) {
        pre_mul_[3] = pre_mul_[1] = (pre_mul_[3] + pre_mul_[1]) / 2;
    }

    if (colors == 1) {
        // there are monochrome cameras with wrong matrix. We just replace with this one.
        rgb_cam[0][0] = 1; rgb_cam[1][0] = 0; rgb_cam[2][0] = 0;
        rgb_cam[0][1] = 0; rgb_cam[1][1] = 1; rgb_cam[2][1] = 0;
        rgb_cam[0][2] = 0; rgb_cam[1][2] = 0; rgb_cam[2][2] = 1;

        for (c = 1; c < 4; c++) {
            cblack_[c] = cblack_[0];
        }
    }

    bool multiple_whites = false;
    int largest_white = this->get_white(0);

    for (c = 1; c < 4; c++) {
        if (this->get_white(c) != this->get_white(0)) {
            multiple_whites = true;

            if (this->get_white(c) > largest_white) {
                largest_white = this->get_white(c);
            }
        }
    }

    if (multiple_whites) {
        // dcraw's pre_mul/cam_mul expects a single white, so if we have provided multiple whites we need
        // to adapt scaling to avoid color shifts.
        for (c = 0; c < 4; c++) {
            // we don't really need to do the largest_white division but do so just to keep pre_mul in similar
            // range as before adjustment so they don't look strangely large if someone would print them
            pre_mul_[c] *= (float)this->get_white(c) / largest_white;
        }
    }

    for (dmin = DBL_MAX, dmax = c = 0; c < 4; c++) {
        if (dmin > pre_mul_[c]) {
            dmin = pre_mul_[c];
        }

        if (dmax < pre_mul_[c]) {
            dmax = pre_mul_[c];
        }
    }

    for (c = 0; c < 4; c++) {
        int sat = this->get_white(c) - cblack_[c];
        scale_mul_[c] = (pre_mul_[c] /= dmax) * 65535.0 / sat;
    }

    if (settings->verbose) {
        float asn[4] = { 1 / cam_mul[0], 1 / cam_mul[1], 1 / cam_mul[2], 1 / cam_mul[3] };

        for (dmax = c = 0; c < 4; c++) {
            if (cam_mul[c] == 0) {
                asn[c] = 0;
            }

            if (asn[c] > dmax) {
                dmax = asn[c];
            }
        }

        for (c = 0; c < 4; c++) {
            asn[c] /= dmax;
        }

        printf("cam_mul:[%f %f %f %f], AsShotNeutral:[%f %f %f %f]\n",
               cam_mul[0], cam_mul[1], cam_mul[2], cam_mul[3], asn[0], asn[1], asn[2], asn[3]);
        printf("pre_mul:[%f %f %f %f], scale_mul:[%f %f %f %f], cblack:[%f %f %f %f]\n",
               pre_mul_[0], pre_mul_[1], pre_mul_[2], pre_mul_[3],
               scale_mul_[0], scale_mul_[1], scale_mul_[2], scale_mul_[3],
               cblack_[0], cblack_[1], cblack_[2], cblack_[3]);
        printf("rgb_cam:[ [ %f %f %f], [%f %f %f], [%f %f %f] ]%s\n",
               rgb_cam[0][0], rgb_cam[1][0], rgb_cam[2][0],
               rgb_cam[0][1], rgb_cam[1][1], rgb_cam[2][1],
               rgb_cam[0][2], rgb_cam[1][2], rgb_cam[2][2],
               (!this->isBayer()) ? " (not bayer)" : "");

    }
}


int RawImage::do_loadRaw(const Glib::ustring &fname, bool loadData, unsigned int imageNum, bool closeFile, ProgressListener *plistener, double progressRange, bool apply_corrections)
{
    ifname = fname.c_str(); //filename.c_str();
    image = nullptr;
    verbose = settings->verbose;
    oprof = nullptr;

    if(!ifp) {
        ifp = gfopen (ifname);  // Maps to either file map or direct fopen
    } else  {
        fseek (ifp, 0, SEEK_SET);
    }

    if (!ifp) {
        return 3;
    }

    imfile_set_plistener(ifp, plistener, 0.9 * progressRange);

    thumb_length = 0;
    thumb_offset = 0;
    thumb_load_raw = nullptr;
    use_camera_wb = 0;
    highlight = 1;
    half_size = 0;
    raw_image = nullptr;

    //***************** Read ALL raw file info
    // set the number of the frame to extract. If the number is larger then number of existing frames - 1, dcraw will handle that correctly

    shot_select = imageNum;
    use_internal_decoder_ = true;

#ifdef ART_USE_LIBRAW
    libraw_.reset(new LibRaw());
    {
        use_internal_decoder_ = false;
        libraw_->imgdata.params.use_camera_wb = 1;
        
        int err = libraw_->open_buffer(ifp->data, ifp->size);
        if (err == LIBRAW_FILE_UNSUPPORTED || err == LIBRAW_TOO_BIG) {
            // fallback to the internal one
            use_internal_decoder_ = true;
        } else if (err != LIBRAW_SUCCESS && strncmp(libraw_->imgdata.idata.software, "make_arq", 8) == 0) {
            use_internal_decoder_ = true;
        } else if (err == LIBRAW_FILE_UNSUPPORTED && (strncmp(libraw_->unpack_function_name(), "sony_arq_load_raw", 17) == 0 || strncmp(libraw_->imgdata.idata.software, "HDRMerge", 8) == 0)) {
            use_internal_decoder_ = true;
        } else if (err != LIBRAW_SUCCESS) {
            return err;
        } else if (libraw_->is_floating_point() && libraw_->imgdata.idata.dng_version) {
            use_internal_decoder_ = true;
        } else {
            auto &d = libraw_->imgdata.idata;
            is_raw = d.raw_count;
            strncpy(make, d.normalized_make, sizeof(make)-1);
            make[sizeof(make)-1] = 0;
            strncpy(model, d.normalized_model, sizeof(model)-1);
            model[sizeof(model)-1] = 0;
            RT_software = d.software;
            dng_version = d.dng_version;
            filters = d.filters;
            is_foveon = d.is_foveon;
            colors = d.colors;
            tiff_bps = 0;
        
            for (int i = 0; i < 6; ++i) {
                for (int j = 0; j < 6; ++j) {
                    xtrans[i][j] = d.xtrans[i][j];
                    xtrans_abs[i][j] = d.xtrans_abs[i][j];
                }
            }
            auto &s = libraw_->imgdata.sizes;
            raw_width = s.raw_width;
            raw_height = s.raw_height;
            width = s.width;
            height = s.height;
            top_margin = s.top_margin;
            left_margin = s.left_margin;
            iheight = s.iheight;
            iwidth = s.iwidth;
            flip = s.flip;

            auto &o = libraw_->imgdata.other;
            iso_speed = o.iso_speed;
            shutter = o.shutter;
            aperture = o.aperture;
            focal_len = o.focal_len;
            timestamp = o.timestamp;
            shot_order = o.shot_order;

            auto &io = libraw_->imgdata.rawdata.ioparams;
            shrink = io.shrink;
            zero_is_bad = io.zero_is_bad;
            fuji_width = io.fuji_width;
            raw_color = io.raw_color;
            mix_green = io.mix_green;

            auto &cd = libraw_->imgdata.color;
            black = cd.black;
            maximum = cd.maximum;
            tiff_bps = cd.raw_bps;

            for (size_t i = 0; i < sizeof(cblack)/sizeof(unsigned); ++i) {
                cblack[i] = cd.cblack[i];
            }
            // put the global black level where the rest of the code expects to find it
            if (dng_version && isXtrans() && cblack[6] == 0 && black > 0) {
                cblack[6] = black;
            }
            
            for (int i = 0; i < 4; ++i) {
                cam_mul[i] = cd.cam_mul[i];
                pre_mul[i] = cd.pre_mul[i];
            }
            
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 4; ++j) {
                    cmatrix[i][j] = cd.cmatrix[i][j];
                    rgb_cam[i][j] = cd.rgb_cam[i][j];
                }
            }

            for (int i = 0; i < 8; ++i) {
                for (int j = 0; j < 8; ++j) {
                    white[i][j] = cd.white[i][j];
                }
            }

            for (int i = 0; i < 8; ++i) {
                for (int j = 0; j < 4; ++j) {
                    mask[i][j] = s.mask[i][j];
                }
            }

            auto &mkn = libraw_->imgdata.makernotes;
        
            // if (!strcmp(make, "Panasonic") && mkn.panasonic.BlackLevelDim > 0) {
            //     memset(cblack, 0, sizeof(cblack));
            //     if (mkn.panasonic.BlackLevelDim >= 4) {
            //         for (size_t i = 0; i < 4; ++i) {
            //             cblack[i] = mkn.panasonic.BlackLevel[i];
            //         }
            //         black = 0;
            //     } else {
            //         black = mkn.panasonic.BlackLevel[0];
            //     }
            // } else
            if (!strcmp(make, "Canon") && isBayer() && !dng_version) {
                if (mkn.canon.AverageBlackLevel) {
                    memset(cblack, 0, sizeof(cblack));
                    for (size_t i = 0; i < 4; ++i) {
                        cblack[i] = mkn.canon.ChannelBlackLevel[i];
                    }
                    black = 0;
                }
                if (mkn.canon.SpecularWhiteLevel) {
                    maximum = mkn.canon.SpecularWhiteLevel;
                } else if (mkn.canon.NormalWhiteLevel) {
                    maximum = mkn.canon.NormalWhiteLevel;
                }
            }
            while (tiff_bps < 16 && (size_t(1) << size_t(tiff_bps)) < maximum) {
                ++tiff_bps;
            }

            if (dng_version) {
                RT_whitelevel_from_constant = ThreeValBool::F;
                RT_blacklevel_from_constant = ThreeValBool::F;
                if (!isBayer() && !isXtrans()) {
                    RT_matrix_from_constant = ThreeValBool::F;
                }
            } else if (strcmp(make, "Panasonic") != 0) {
                RT_whitelevel_from_constant = ThreeValBool::T;
                RT_blacklevel_from_constant = ThreeValBool::T;
            }

            if (is_foveon) {
                raw_width = width;
                raw_height = height;
                top_margin = 0;
                left_margin = 0;

                RT_whitelevel_from_constant = ThreeValBool::F;
                RT_blacklevel_from_constant = ThreeValBool::F;
            }

            static char xtrans_zero[6][6] = { { 0 } };
            if (isXtrans() && dng_version && memcmp(xtrans, xtrans_zero, sizeof(xtrans_zero)) == 0) {
                // workaround to libraw issues with some DNG files produced by dnglab
                use_internal_decoder_ = true;
            }
        }
    }
    if (use_internal_decoder_) {
        libraw_->recycle();
    }
#endif // ART_USE_LIBRAW

    if (use_internal_decoder_) {
        identify();
    }
    
    // in case dcraw didn't handle the above mentioned case...
    shot_select = std::min(shot_select, std::max(is_raw, 1u) - 1);

    const auto is_mosaic = isBayer() || isXtrans();
    if (!is_raw || (colors != 1 && colors != 3 &&
                    !(!is_mosaic && colors == 4 && !use_internal_decoder_))) {
        fclose(ifp);
        ifp = nullptr;

        if (plistener) {
            plistener->setProgress(1.0 * progressRange);
        }

        return 2;
    }

    if (use_internal_decoder_) {
        if (!strcmp(make,"Fujifilm") && raw_height * raw_width * 2u != raw_size) {
            if (raw_width * raw_height * 7u / 4u == raw_size) {
                load_raw = &RawImage::fuji_14bit_load_raw;
            } else {
                parse_fuji_compressed_header();
            }
        }
    }

    if (flip == 5) {
        this->rotate_deg = 270;
    } else if (flip == 3) {
        this->rotate_deg = 180;
    } else if (flip == 6) {
        this->rotate_deg = 90;
    } else if (flip % 90 == 0 && flip < 360) {
        this->rotate_deg = flip;
    } else {
        this->rotate_deg = 0;
    }

    if (loadData) {
        use_camera_wb = 1;
        shrink = 0;

        if (settings->verbose) {
            printf("Loading %s %s image from %s using %s...\n",
                   make, model, filename.c_str(),
                   use_internal_decoder_ ? "the internal decoder" : "libraw");
        }

        iheight = height;
        iwidth  = width;

        if (use_internal_decoder_) {
            if (filters || colors == 1) {
                raw_image = (ushort *) calloc ((static_cast<unsigned int>(raw_height) + 7u) * static_cast<unsigned int>(raw_width), 2);
                merror (raw_image, "main()");
            }

            // dcraw needs this global variable to hold pixel data
            image = (ImageType)calloc (static_cast<unsigned int>(height) * static_cast<unsigned int>(width) * sizeof * image + meta_length, 1);
            meta_data = (char *) (image + static_cast<unsigned int>(height) * static_cast<unsigned int>(width));

            if (!image) {
                return 200;
            }

            // Load raw pixels data
            fseek(ifp, data_offset, SEEK_SET);
            (this->*load_raw)();
        } else {
#ifdef ART_USE_LIBRAW
            libraw_->imgdata.rawparams.shot_select = shot_select;
            
            int err = libraw_->open_buffer(ifp->data, ifp->size);
            if (err) {
                return err;
            }
            {
#ifdef LIBRAW_USE_OPENMP
                MyMutex::MyLock lock(*librawMutex);
#endif
                err = libraw_->unpack();
            }
            if (err) {
                return err;
            }

            auto &rd = libraw_->imgdata.rawdata;
            raw_image = rd.raw_image;
            if (rd.float_image) {
                float_raw_image = new float[raw_width * raw_height];
                for (int y = 0; y < raw_height; ++y) {
                    for (int x = 0; x < raw_width; ++x) {
                        size_t idx = y * raw_width + x;
                        float_raw_image[idx] = rd.float_image[idx];
                    }
                }
            } else {
#ifdef LIBRAW_USE_OPENMP
                MyMutex::MyLock lock(*librawMutex);
#endif
                float_raw_image = nullptr;
                err = libraw_->raw2image();
                if (err) {
                    return err;
                }
                image = libraw_->imgdata.image;
            }

            // get our custom camera matrices, but don't mess with black/white levels yet
            // (if we have custom levels in json files, we will get them later)
            auto bl = RT_blacklevel_from_constant;
            auto wl = RT_whitelevel_from_constant;
            RT_blacklevel_from_constant = ThreeValBool::F;
            RT_whitelevel_from_constant = ThreeValBool::F;
            
            adobe_coeff(make, model);
            
            RT_blacklevel_from_constant = bl;
            RT_whitelevel_from_constant = wl;

            if (libraw_->imgdata.color.profile_length) {
                profile_length = libraw_->imgdata.color.profile_length;
                profile_data = new char[profile_length];
                memcpy(profile_data, libraw_->imgdata.color.profile, profile_length);
            }

            if (isBayer() && RT_blacklevel_from_constant == ThreeValBool::T && max(black, cblack[0], cblack[1], cblack[2], cblack[3]) == 0) {
                auto &black_stat = libraw_->imgdata.color.black_stat;
                for (int i = 0; i < 4; ++i) {
                    cblack[i] = black_stat[i] / (1+black_stat[i+4]);
                }
            }
#endif // ART_USE_LIBRAW
        }

        if (!float_raw_image) { // apply baseline exposure only for float DNGs
            RT_baseline_exposure = 0;
        }

        if (plistener) {
            plistener->setProgress(0.9 * progressRange);
        }

        CameraConstantsStore* ccs = CameraConstantsStore::getInstance();
        CameraConst *cc = ccs->get(make, model);
        bool raw_crop_cc = false;
        int orig_raw_width = width;
        int orig_raw_height = height;
        int raw_top_margin = 0;
        int raw_left_margin = 0;
        bool adjust_margins = false;

        if (raw_image) {
            unsigned raw_filters = filters;
            orig_raw_width = raw_width;
            orig_raw_height = raw_height;
            
            if (cc && cc->has_rawCrop(raw_width, raw_height)) { 
                raw_crop_cc = true;
                int lm, tm, w, h;
                cc->get_rawCrop(raw_width, raw_height, lm, tm, w, h);

                if ((w < 0 || h < 0) && !use_internal_decoder_) {
                    raw_crop_cc = false;
                } else {
                    // protect against DNG files that are already cropped
                    if (int(raw_width) <= w+lm) {
                        lm = max(int(raw_width) - w, 0);
                    }
                    if (int(raw_height) <= h+tm) {
                        tm = max(int(raw_height) - h, 0);
                    }

                    if (use_internal_decoder_) {
                        if(isXtrans()) {
                            shiftXtransMatrix(6 - ((top_margin - tm)%6), 6 - ((left_margin - lm)%6));
                        } else {
                            if(((int)top_margin - tm) & 1) { // we have an odd border difference
                                filters = (filters << 4) | (filters >> 28);    // left rotate filters by 4 bits
                            }
                        }
                        left_margin = lm;
                        top_margin = tm;
                    } else {
                        if (lm < left_margin) {
                            lm = left_margin;
                        }
                        if (tm < top_margin) {
                            tm = top_margin;
                        }
                        // make sure we do not rotate filters
                        if (isXtrans()) {
                            if ((tm - top_margin) % 6) {
                                tm = top_margin;
                            }
                            if ((lm - left_margin) % 6) {
                                lm = left_margin;
                            }
                        } else {
                            if ((tm - top_margin) & 1) {
                                tm = top_margin;
                            }
                            if ((lm - left_margin) & 1) {
                                lm = left_margin;
                            }
                        }
                        raw_left_margin = lm - left_margin;
                        raw_top_margin = tm - top_margin;
                    }

                    if (w < 0) {
                        width += w;
                        width -= left_margin;
                        iwidth += w;
                        iwidth -= left_margin;
                    } else if (w > 0) {
                        width = min((int)width, w);
                        if (use_internal_decoder_) {
                            iwidth = width;
                        } else if (width > iwidth) {
                            width = iwidth;
                        }
                    }

                    if (h < 0) {
                        height += h;
                        height -= top_margin;
                        iheight += h;
                        iheight -= top_margin;
                    } else if (h > 0) {
                        height = min((int)height, h);
                        if (use_internal_decoder_) {
                            iheight = height;
                        } else if (height > iheight) {
                            height = iheight;
                        }
                    }
                }
            }

            if (cc && cc->has_rawMask(orig_raw_width, orig_raw_height, 0) && !dng_version) {
                for (int i = 0; i < 8 && cc->has_rawMask(orig_raw_width, orig_raw_height, i); i++) {
                    cc->get_rawMask(orig_raw_width, orig_raw_height, i, mask[i][0], mask[i][1], mask[i][2], mask[i][3]);
                    if (apply_corrections && i == 0 && !cc->has_rawMask(orig_raw_width, orig_raw_height, 1) && mask[i][3] < left_margin && mask[i][2] * 1.01 >= height) {
                        // compute per-row median of black levels, used to fix
                        // dynamic row pattern noise. Technique suggested (and
                        // illustrated) by user Peter @pixls.us
                        // 
                        // this matches the optical black area in Canon sensors
                        // 
                        unsigned tmp_filters = filters;
                        filters = raw_filters;
                        
                        std::array<int, 4> v;
                        std::array<std::vector<int>, 4> m;
                        for (int r = top_margin; r < raw_height; ++r) {
                            for (auto &mv : m) {
                                mv.clear();
                            }
                            for (int j = mask[i][1]; j < mask[i][3]; ++j) {
                                int c = FC(r, j);
                                auto b = raw_image[r * raw_width + j];
                                m[c].push_back(b);
                            }
                            for (int c = 0; c < 4; ++c) {
                                std::sort(m[c].begin(), m[c].end());
                                v[c] = m[c].empty() ? 0 : m[c][m[c].size()/2];
                            }
                            raw_optical_black_med_.emplace_back(std::move(v));
                        }

                        filters = tmp_filters;
                    }
                }
            }

            if (use_internal_decoder_) {
                crop_masked_pixels();
                free(raw_image);
#ifdef ART_USE_LIBRAW                
            } else if (!float_raw_image && !dng_version && strncmp(libraw_->unpack_function_name(), "canon_600_load_raw", 18) != 0) {
                set_black_from_masked_areas();
#endif // ART_USE_LIBRAW
            }
            raw_image = nullptr;
            adjust_margins = !float_raw_image; //true;
        } else {
            if (get_maker() == "Sigma" && cc && cc->has_rawCrop(width, height) && use_internal_decoder_) { // foveon images
                raw_crop_cc = true;
                int lm, tm, w, h;
                cc->get_rawCrop(width, height, lm, tm, w, h);
                left_margin = lm;
                top_margin = tm;

                if (w < 0) {
                    width += w;
                    width -= left_margin;
                    iwidth += w;
                    iwidth -= left_margin;
                } else if (w > 0) {
                    width = min((int)width, w);
                    iwidth = width;
                }

                if (h < 0) {
                    height += h;
                    height -= top_margin;
                    iheight += h;
                    iheight -= top_margin;
                } else if (h > 0) {
                    height = min((int)height, h);
                    iheight = height;
                }
            }
        }

        // Load embedded profile
        if (use_internal_decoder_) {
            if (profile_length) {
                profile_data = new char[profile_length];
                fseek(ifp, profile_offset, SEEK_SET);
                fread(profile_data, 1, profile_length, ifp);
            }
        }

        /*
          Setting the black level, there are three sources:
          dcraw single value 'black' or multi-value 'cblack', can be calculated or come
          from a hard-coded table or come from a stored value in the raw file, and
          finally there's 'black_c4' which are table values provided by RT camera constants.
          Any of these may or may not be set.

          We reduce these sources to one four channel black level, and do this by picking
          the highest found.
        */
        int black_c4[4] = { -1, -1, -1, -1 };

        bool white_from_cc = false;
        bool black_from_cc = false;

        if (cc) {
            for (int i = 0; i < 4; i++) {
                if (RT_blacklevel_from_constant == ThreeValBool::T) {
                    int blackFromCc = cc->get_BlackLevel(i, iso_speed);
                    // if black level from camconst > 0xffff it is an absolute value.
                    if (blackFromCc != -1) {
                        black_c4[i] = blackFromCc > 0xffff ? (blackFromCc & 0xffff) : blackFromCc + cblack[i];
                    }
                }

                // load 4 channel white level here, will be used if available
                if (RT_whitelevel_from_constant == ThreeValBool::T) {
                    maximum_c4[i] = cc->get_WhiteLevel(i, iso_speed, aperture);

                    if(tiff_bps > 0 && maximum_c4[i] > 0 && !isFoveon()) {
                        unsigned compare = ((uint64_t)1 << tiff_bps) - 1; // use uint64_t to avoid overflow if tiff_bps == 32

                        while(static_cast<uint64_t>(maximum_c4[i]) > compare) {
                            maximum_c4[i] >>= 1;
                        }
                    }
                }
            }
        }

        if (black_c4[0] == -1) {
            if (isXtrans()) {
                for (int c = 0; c < 4; c++) {
                    black_c4[c] = cblack[6];
                }
            } else {
                // RT constants not set, bring in the DCRAW single channel black constant
                for (int c = 0; c < 4; c++) {
                    black_c4[c] = black + cblack[c];
                }
            }
        } else {
            black_from_cc = true;
        }

        if (maximum_c4[0] > 0) {
            white_from_cc = true;
        }

        for (int c = 0; c < 4; c++) {
            if (static_cast<int>(cblack[c]) < black_c4[c]) {
                cblack[c] = black_c4[c];
                cblack[4] = cblack[5] = 0;
            }
        }

        if (settings->verbose) {
            const char *decoder = use_internal_decoder_ ? "dcraw" : "libraw";
            if (cc) {
                printf("constants exists for \"%s %s\" in camconst.json\n", make, model);
            } else {
                printf("no constants in camconst.json exists for \"%s %s\" (relying only on %s defaults)\n", make, model, decoder);
            }

            printf("raw dimensions: %d x %d\n", orig_raw_width, orig_raw_height);
            printf("black levels: R:%d G1:%d B:%d G2:%d (provided by %s)\n", get_cblack(0), get_cblack(1), get_cblack(2), get_cblack(3),
                   black_from_cc ? "camconst.json" : decoder);
            printf("white levels: R:%d G1:%d B:%d G2:%d (provided by %s)\n", get_white(0), get_white(1), get_white(2), get_white(3),
                   white_from_cc ? "camconst.json" : decoder);
            printf("raw crop: %d %d %d %d (provided by %s)\n", left_margin, top_margin, width, height, raw_crop_cc ? "camconst.json" : decoder);
            printf("color matrix provided by %s\n", (cc && cc->has_dcrawMatrix() && RT_matrix_from_constant == ThreeValBool::T) ? "camconst.json" : decoder);
            if (cc && cc->has_dcrawMatrix() && RT_matrix_from_constant == ThreeValBool::T) {
                const short *mx = cc->get_dcrawMatrix();
                printf("[");
                const char *sep = "";
                for (int j = 0; j < 12; j++) {
                    if (!mx[j]) {
                        break;
                    }
                    printf("%s%d", sep, mx[j]);
                    sep = ", ";
                }
                printf("]\n");
            }
        }

        if (adjust_margins) {
            top_margin = raw_top_margin;
            left_margin = raw_left_margin;
        }
    }

    if (closeFile) {
        fclose(ifp);
        ifp = nullptr;
    }

    if (plistener) {
        plistener->setProgress(1.0 * progressRange);
    }

    return 0;
}


int RawImage::loadRaw(bool loadData, unsigned int imageNum, bool closeFile, ProgressListener *plistener, double progressRange, bool apply_corrections)
{
    switch (use_imgio_) {
    case ThreeValBool::F:
        return do_loadRaw(filename, loadData, imageNum, closeFile, plistener, progressRange, apply_corrections);
    case ThreeValBool::T:
        return do_loadRaw(imgio_filename_, loadData, imageNum, closeFile, plistener, progressRange, apply_corrections);
    default: {
        FramesData md(filename, true);
        use_imgio_ = ImageIOManager::getInstance()->loadRaw(filename, md.getMake(), md.getModel(), imgio_filename_) ? ThreeValBool::T : ThreeValBool::F;
        if (use_imgio_ == ThreeValBool::T) {
            return do_loadRaw(imgio_filename_, loadData, imageNum, closeFile, plistener, progressRange, apply_corrections);
        } else {
            return do_loadRaw(filename, loadData, imageNum, closeFile, plistener, progressRange, apply_corrections);
        }
    }
    }
}


void RawImage::set_black_from_masked_areas()
{
    unsigned mblack[8] = { 0 };
    unsigned zero = 0;
    for (unsigned m = 0; m < 8; ++m) {
        for (int row = std::max(int(mask[m][0]), 0); row < std::min(int(mask[m][2]), int(raw_height)); row++) {
            for (int col = std::max(int(mask[m][1]), 0); col < std::min(int(mask[m][3]), int(raw_width)); col++) {
                unsigned c = FC(row-top_margin, col-left_margin);
                unsigned val = raw_image[row * raw_width + col];
                mblack[c] += val;
                mblack[4+c]++;
                zero += !val;
            }
        }
    }
    if (zero < mblack[4] && mblack[5] && mblack[6] && mblack[7]) {
        for (int c = 0; c < 4; ++c) {
            cblack[c] = mblack[c] / mblack[4+c];
        }
        cblack[4] = cblack[5] = cblack[6] = 0;
    }
}    


float** RawImage::compress_image(unsigned int frameNum, bool freeImage)
{
    if (!image) {
        return nullptr;
    }

    if (isBayer() || isXtrans()) {
        if (!allocation) {
            // shift the beginning of all frames but the first by 32 floats to avoid cache miss conflicts on CPUs which have <= 4-way associative L1-Cache
            allocation = new float[static_cast<unsigned int>(height) * static_cast<unsigned int>(width) + frameNum * 32u];
            data = new float*[height];

            for (int i = 0; i < height; i++) {
                data[i] = allocation + i * width + frameNum * 32;
            }
        }
    } else if (colors == 1) {
        // Monochrome
        if (!allocation) {
            allocation = new float[static_cast<unsigned long>(height) * static_cast<unsigned long>(width)];
            data = new float*[height];

            for (int i = 0; i < height; i++) {
                data[i] = allocation + i * width;
            }
        }
    } else {
        if (!allocation) {
            allocation = new float[3UL * static_cast<unsigned long>(height) * static_cast<unsigned long>(width)];
            data = new float*[height];

            for (int i = 0; i < height; i++) {
                data[i] = allocation + 3 * i * width;
            }
        }
    }

    // copy pixel raw data: the compressed format earns space
    if( float_raw_image ) {
#ifdef _OPENMP
        #pragma omp parallel for
#endif

        for (int row = 0; row < height; row++)
            for (int col = 0; col < width; col++) {
                this->data[row][col] = float_raw_image[(row + top_margin) * raw_width + col + left_margin];
            }

        delete [] float_raw_image;
        float_raw_image = nullptr;
    } else if (filters != 0 && !isXtrans()) {
#ifdef _OPENMP
        #pragma omp parallel for
#endif

        for (int row = 0; row < height; row++)
            for (int col = 0; col < width; col++) {
                this->data[row][col] = image[(row + top_margin) * iwidth + col + left_margin][FC(row, col)];
            }
    } else if (isXtrans()) {
#ifdef _OPENMP
        #pragma omp parallel for
#endif

        for (int row = 0; row < height; row++)
            for (int col = 0; col < width; col++) {
                this->data[row][col] = image[(row + top_margin) * iwidth + col + left_margin][XTRANSFC(row, col)];
            }
    } else if (colors == 1) {
#ifdef _OPENMP
        #pragma omp parallel for
#endif

        for (int row = 0; row < height; row++)
            for (int col = 0; col < width; col++) {
                this->data[row][col] = image[row * iwidth + col][0];
            }
    } else {
        if((get_maker() == "Sigma" || get_maker() == "Pentax" || get_maker() == "Sony") && dng_version) { // Hack to prevent sigma dng files and dng files from PixelShift2DNG from crashing
            height -= top_margin;
            width -= left_margin;
        }
#ifdef _OPENMP
        #pragma omp parallel for
#endif

        for (int row = 0; row < height; row++)
            for (int col = 0; col < width; col++) {
                this->data[row][3 * col + 0] = image[(row + top_margin) * iwidth + col + left_margin][0];
                this->data[row][3 * col + 1] = image[(row + top_margin) * iwidth + col + left_margin][1];
                this->data[row][3 * col + 2] = image[(row + top_margin) * iwidth + col + left_margin][2];
            }
    }

    if (freeImage) {
        if (use_internal_decoder_) {
            free(image); // we don't need this anymore
        } else {
#ifdef ART_USE_LIBRAW
            libraw_->recycle();
#endif // ART_USE_LIBRAW
        }
        image = nullptr;
    }
    return data;
}

bool RawImage::is_supportedThumb() const
{
    return ( (thumb_width * thumb_height) > 0 &&
             ( write_thumb == &rtengine::RawImage::jpeg_thumb ||
               write_thumb == &rtengine::RawImage::ppm_thumb) &&
             !thumb_load_raw );
}


bool RawImage::is_ppmThumb() const
{
    return ( (thumb_width * thumb_height) > 0 &&
             write_thumb == &rtengine::RawImage::ppm_thumb &&
             !thumb_load_raw );
}

void RawImage::getXtransMatrix(int XtransMatrix[6][6])
{
    for(int row = 0; row < 6; row++)
        for(int col = 0; col < 6; col++) {
            XtransMatrix[row][col] = xtrans[row][col];
        }
}


void RawImage::getRgbCam(float rgbcam[3][4])
{
    for(int row = 0; row < 3; row++)
        for(int col = 0; col < 4; col++) {
            rgbcam[row][col] = rgb_cam[row][col];
        }

}


bool RawImage::get_thumbSwap() const
{
    return (order == 0x4949) == (ntohs(0x1234) == 0x1234);
}


bool RawImage::checkThumbOk() const
{
    if (!is_supportedThumb()) {
        return false;
    }

    if (get_thumbOffset() >= get_file()->size) {
        return false;
    }

    const ssize_t length =
        fdata (get_thumbOffset(), get_file())[1] != 0xD8 && is_ppmThumb()
        ? get_thumbWidth() * get_thumbHeight() * (get_thumbBPS() / 8) * 3
        : get_thumbLength();

    return get_thumbOffset() + length <= get_file()->size;
}


bool RawImage::thumbNeedsRotation() const
{
    std::string fname = get_filename();
    std::string suffix = fname.length() > 4 ? fname.substr(fname.length() - 3) : "";

    for (unsigned int i = 0; i < suffix.length(); i++) {
        suffix[i] = std::tolower(suffix[i]);
    }

    // Leaf .mos, Mamiya .mef and Phase One .iiq files have thumbnails already rotated.
    if (suffix != "mos" && suffix != "mef" && suffix != "iiq")  {
        return true;
    } else {
        return false;
    }
}


float RawImage::get_optical_black(int row, int col) const
{
    if (raw_optical_black_med_.empty() || size_t(row) >= raw_optical_black_med_.size()) {
        return 0.f;
    }
    int c = FC(row, col);
    return raw_optical_black_med_[row][c];
}


Image8 *RawImage::getThumbnail()
{
    if (use_internal_decoder_) {
        if (!checkThumbOk()) {
            return nullptr;
        }

        Image8 *img = new Image8();
        img->setSampleFormat(IIOSF_UNSIGNED_CHAR);
        img->setSampleArrangement(IIOSA_CHUNKY);

        const char *data = reinterpret_cast<const char *>(fdata(get_thumbOffset(), get_file()));

        int err = 1;
        if ((unsigned char)data[1] == 0xd8) {
            err = img->loadJPEGFromMemory(data, get_thumbLength());
        } else if (is_ppmThumb()) {
            err = img->loadPPMFromMemory(data, get_thumbWidth(), get_thumbHeight(), get_thumbSwap(), get_thumbBPS());
        }

        // did we succeed?
        if (err) {
            delete img;
            img = nullptr;
        }

        return img;
    }
#ifdef ART_USE_LIBRAW
    if (!ifp) {
        return nullptr;
    } else {
        int err = libraw_->unpack_thumb();
        if (err) {
            return nullptr;
        }
        auto &t = libraw_->imgdata.thumbnail;
        if (!t.thumb) {
            return nullptr;
        } else if (t.tformat != LIBRAW_THUMBNAIL_JPEG && t.tformat != LIBRAW_THUMBNAIL_BITMAP) {
            return nullptr;
        } else {
            Image8 *img = new Image8();
            img->setSampleFormat(IIOSF_UNSIGNED_CHAR);
            img->setSampleArrangement(IIOSA_CHUNKY);
            if (t.tformat == LIBRAW_THUMBNAIL_JPEG) {
                err = img->loadJPEGFromMemory(t.thumb, t.tlength);
            } else {
                err = img->loadPPMFromMemory(t.thumb, t.twidth, t.theight, false, 8);
            }
            if (err) {
                delete img;
                return nullptr;
            } else {
                return img;
            }
        }
    }
#endif // ART_USE_LIBRAW
    return nullptr;
}

} //namespace rtengine

bool
DCraw::dcraw_coeff_overrides(const char make[], const char model[], const int iso_speed, short trans[12], int *black_level, int *white_level)
{
    static const int dcraw_arw2_scaling_bugfix_shift = 2;
    static const struct {
        const char *prefix;
        int black_level, white_level; // set to -1 for no change
        short trans[12]; // set first value to 0 for no change
    } table[] = {

        {
            "Canon EOS 5D Mark III", -1, 0x3a98,  /* RT */
            { 6722, -635, -963, -4287, 12460, 2028, -908, 2162, 5668 }
        },
        {
            "Canon EOS 5D", -1, 0xe6c, /* RT */
            { 6319, -793, -614, -5809, 13342, 2738, -1132, 1559, 7971 }
        },
        {
            "Canon EOS 6D", -1, 0x3c82,
            { 7034, -804, -1014, -4420, 12564, 2058, -851, 1994, 5758 }
        },
        {
            "Canon EOS 7D", -1, 0x3510, /* RT - Colin Walker */
            { 5962, -171, -732, -4189, 12307, 2099, -911, 1981, 6304 }
        },
        {
            "Canon EOS 20D", -1, 0xfff,  /* RT */
            { 7590, -1646, -673, -4697, 12411, 2568, -627, 1118, 7295 }
        },
        {
            "Canon EOS 40D", -1, 0x3f60,  /* RT */
            { 6070, -531, -883, -5763, 13647, 2315, -1533, 2582, 6801 }
        },
        {
            "Canon EOS 60D", -1, 0x2ff7, /* RT - Colin Walker */
            { 5678, -179, -718, -4389, 12381, 2243, -869, 1819, 6380 }
        },
        {
            "Canon EOS 450D", -1, 0x390d, /* RT */
            { 6246, -1272, -523, -5075, 12357, 3075, -1035, 1825, 7333 }
        },
        {
            "Canon EOS 550D", -1, 0x3dd7, /* RT - Lebedev*/
            { 6519, -772, -703, -4994, 12737, 2519, -1387, 2492, 6175 }
        },
        {
            "Canon EOS-1D Mark III", 0, 0x3bb0,  /* RT */
            { 7406, -1592, -646, -4856, 12457, 2698, -432, 726, 7921 }
        },
        {
            "Canon PowerShot G10", -1, -1,  /* RT */
            { 12535, -5030, -796, -2711, 10134, 3006, -413, 1605, 5264 }
        },
        {
            "Canon PowerShot G12", -1, -1,  /* RT */
            { 12222, -4097, -1380, -2876, 11016, 2130, -888, 1630, 4434 }
        },


        {
            "Fujifilm X100", -1, -1,  /* RT - Colin Walker */
            { 10841, -3288, -807, -4652, 12552, 2344, -642, 1355, 7206 }
        },


        {
            "Nikon D200", -1, 0xfbc, /* RT */
            { 8498, -2633, -295, -5423, 12869, 2860, -777, 1077, 8124 }
        },
        {
            "Nikon D3000", -1, -1, /* RT */
            { 9211, -2521, -104, -6487, 14280, 2394, -754, 1122, 8033 }
        },
        {
            "Nikon D3100", -1, -1, /* RT */
            { 7729, -2212, -481, -5709, 13148, 2858, -1295, 1908, 8936 }
        },
        {
            "Nikon D3S", -1, -1, /* RT */
            { 8792, -2663, -344, -5221, 12764, 2752, -1491, 2165, 8121 }
        },
        {
            "Nikon D5200", -1, -1, // color matrix copied from D5200 DNG D65 matrix
            { 8322, -3112, -1047, -6367, 14342, 2179, -988, 1638, 6394 }
        },
        {
            "Nikon D7000", -1, -1, /* RT - Tanveer(tsk1979) */
            { 7530, -1942, -255, -4318, 11390, 3362, -926, 1694, 7649 }
        },
        {
            "Nikon D7100", -1, -1, // color matrix and WP copied from D7100 DNG D65 matrix
            { 8322, -3112, -1047, -6367, 14342, 2179, -988, 1638, 6394 }
        },
        {
            "Nikon D700", -1, -1, /* RT */
            { 8364, -2503, -352, -6307, 14026, 2492, -1134, 1512, 8156 }
        },
        {
            "Nikon COOLPIX A", -1, 0x3e00, // color matrix and WP copied from "COOLPIX A" DNG D65 matrix
            { 8198, -2239, -724, -4871, 12389, 2798, -1043, 205, 7181 }
        },


        {
            "Olympus E-30", -1, 0xfbc,  /* RT - Colin Walker */
            { 8510, -2355, -693, -4819, 12520, 2578, -1029, 2067, 7752 }
        },
        {
            "Olympus E-5", -1, 0xeec, /* RT - Colin Walker */
            { 9732, -2629, -999, -4899, 12931, 2173, -1243, 2353, 7457 }
        },
        {
            "Olympus E-P1", -1, 0xffd, /* RT - Colin Walker */
            { 8834, -2344, -804, -4691, 12503, 2448, -978, 1919, 7603 }
        },
        {
            "Olympus E-P2", -1, 0xffd, /* RT - Colin Walker */
            { 7758, -1619, -800, -5002, 12886, 2349, -985, 1964, 8305 }
        },
        {
            "Olympus E-P3", -1, -1, /* RT - Colin Walker */
            { 7041, -1794, -336, -3790, 11192, 2984, -1364, 2625, 6217 }
        },
        {
            "Olympus E-PL1s", -1, -1, /* RT - Colin Walker */
            { 9010, -2271, -838, -4792, 12753, 2263, -1059, 2058, 7589 }
        },
        {
            "Olympus E-PL1", -1, -1, /* RT - Colin Walker */
            { 9010, -2271, -838, -4792, 12753, 2263, -1059, 2058, 7589 }
        },
        {
            "Olympus E-PL2", -1, -1, /* RT - Colin Walker */
            { 11975, -3351, -1184, -4500, 12639, 2061, -1230, 2353, 7009 }
        },
        {
            "Olympus E-PL3", -1, -1, /* RT - Colin Walker */
            { 7041, -1794, -336, -3790, 11192, 2984, -1364, 2625, 6217 }
        },
        {
            "Olympus XZ-1", -1, -1, /* RT - Colin Walker */
            { 8665, -2247, -762, -2424, 10372, 2382, -1011, 2286, 5189 }
        },


        /* since Dcraw_v9.21 Panasonic BlackLevel is read from exif (tags 0x001c BlackLevelRed, 0x001d BlackLevelGreen, 0x001e BlackLevelBlue
          and we define here the needed offset of around 15. The total BL is BL + BLoffset (cblack + black)  */

        {
            "Panasonic DMC-FZ150", 15, 0xfd2,  /* RT */
            { 10435, -3208, -72, -2293, 10506, 2067, -486, 1725, 4682 }
        },
        {
            "Panasonic DMC-G10", 15, 0xf50, /* RT - Colin Walker - variable WL 3920 - 4080 */
            { 8310, -1811, -960, -4941, 12990, 2151, -1378, 2468, 6860 }
        },
        {
            "Panasonic DMC-G1", 15, 0xf50,  /* RT - Colin Walker - variable WL 3920 - 4080 */
            { 7477, -1615, -651, -5016, 12769, 2506, -1380, 2475, 7240 }
        },
        {
            "Panasonic DMC-G2", 15, 0xf50,  /* RT - Colin Walker - variable WL 3920 - 4080  */
            { 8310, -1811, -960, -4941, 12990, 2151, -1378, 2468, 6860 }
        },
        {
            "Panasonic DMC-G3", 15, 0xfdc,  /* RT - Colin Walker - WL 4060 */
            { 6051, -1406, -671, -4015, 11505, 2868, -1654, 2667, 6219 }
        },
        {
            "Panasonic DMC-G5", 15, 0xfdc,   /* RT - WL 4060 */
            { 7122, -2092, -419, -4643, 11769, 3283, -1363, 2413, 5944 }
        },
        {
            "Panasonic DMC-GF1", 15, 0xf50, /* RT - Colin Walker - Variable WL 3920 - 4080 */
            { 7863, -2080, -668, -4623, 12331, 2578, -1020, 2066, 7266 }
        },
        {
            "Panasonic DMC-GF2", 15, 0xfd2, /* RT - Colin Walker - WL 4050 */
            { 7694, -1791, -745, -4917, 12818, 2332, -1221, 2322, 7197 }
        },
        {
            "Panasonic DMC-GF3", 15, 0xfd2, /* RT - Colin Walker - WL 4050 */
            { 8074, -1846, -861, -5026, 12999, 2239, -1320, 2375, 7422 }
        },
        {
            "Panasonic DMC-GH1", 15, 0xf5a,  /* RT - Colin Walker - variable WL 3930 - 4080 */
            { 6360, -1557, -375, -4201, 11504, 3086, -1378, 2518, 5843 }
        },
        {
            "Panasonic DMC-GH2", 15, 0xf5a,  /* RT - Colin Walker - variable WL 3930 - 4080 */
//        { 6855,-1765,-456,-4223,11600,2996,-1450,2602,5761 } }, disabled due to problems with underwater WB
            { 7780, -2410, -806, -3913, 11724, 2484, -1018, 2390, 5298 }
        }, // dcraw original

        {
            "Pentax K200D", -1, -1,  /* RT */
            { 10962, -4428, -542, -5486, 13023, 2748, -569, 842, 8390 }
        },


        {
            "Leica Camera AG M9 Digital Camera", -1, -1,  /* RT */
            { 7181, -1706, -55, -3557, 11409, 2450, -621, 2072, 7533 }
        },


        {
            "SONY NEX-3", 128 << dcraw_arw2_scaling_bugfix_shift, -1, /* RT - Colin Walker */
            { 5145, -741, -123, -4915, 12310, 2945, -794, 1489, 6906 }
        },
        {
            "SONY NEX-5", 128 << dcraw_arw2_scaling_bugfix_shift, -1, /* RT - Colin Walker */
            { 5154, -716, -115, -5065, 12506, 2882, -988, 1715, 6800 }
        },
        {
            "Sony NEX-5N", 128 << dcraw_arw2_scaling_bugfix_shift, -1, /* RT - Colin Walker */
            { 5130, -1055, -269, -4473, 11797, 3050, -701, 1310, 7121 }
        },
        {
            "Sony NEX-5R", 128 << dcraw_arw2_scaling_bugfix_shift, -1,
            { 6129, -1545, -418, -4930, 12490, 2743, -977, 1693, 6615 }
        },
        {
            "SONY NEX-C3", 128 << dcraw_arw2_scaling_bugfix_shift, -1,  /* RT - Colin Walker */
            { 5130, -1055, -269, -4473, 11797, 3050, -701, 1310, 7121 }
        },
        {
            "Sony SLT-A77", 128 << dcraw_arw2_scaling_bugfix_shift, -1,  /* RT - Colin Walker */
            { 5126, -830, -261, -4788, 12196, 2934, -948, 1602, 7068 }
        },
    };

    *black_level = -1;
    *white_level = -1;

    const bool is_pentax_dng = dng_version && !strncmp(RT_software.c_str(), "PENTAX", 6);
    
    if (RT_blacklevel_from_constant == ThreeValBool::F && !is_pentax_dng) {
        *black_level = black;
    }
    if (RT_whitelevel_from_constant == ThreeValBool::F && !is_pentax_dng) {
        *white_level = maximum;
    }
    memset(trans, 0, sizeof(*trans) * 12);

    {
        // test if we have any information in the camera constants store, if so we take that.
        rtengine::CameraConstantsStore* ccs = rtengine::CameraConstantsStore::getInstance();
        rtengine::CameraConst *cc = ccs->get(make, model);

        if (cc) {
            if (RT_blacklevel_from_constant == ThreeValBool::T) {
                *black_level = cc->get_BlackLevel(0, iso_speed);
            }
            if (RT_whitelevel_from_constant == ThreeValBool::T) {
                *white_level = cc->get_WhiteLevel(0, iso_speed, aperture);
            }

            if (RT_matrix_from_constant == ThreeValBool::T && cc->has_dcrawMatrix()) {
                const short *mx = cc->get_dcrawMatrix();

                for (int j = 0; j < 12; j++) {
                    trans[j] = mx[j];
                }
            }

            return true;
        }
    }

    char name[strlen(make) + strlen(model) + 32];
    sprintf(name, "%s %s", make, model);

    for (size_t i = 0; i < sizeof table / sizeof(table[0]); i++) {
        if (strcasecmp(name, table[i].prefix) == 0) {
            if (RT_blacklevel_from_constant == ThreeValBool::T) {
                *black_level = table[i].black_level;
            }
            if (RT_whitelevel_from_constant == ThreeValBool::T) {
                *white_level = table[i].white_level;
            }

            for (int j = 0; j < 12; j++) {
                trans[j] = table[i].trans[j];
            }

            return true;
        }
    }

    return false;
}

