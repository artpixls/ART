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
#include <functional>
#include <strings.h>
#include <glib/gstdio.h>
#include <tiff.h>
#include <glibmm/regex.h>
#include <sstream>

#include "imagedata.h"
#include "imagesource.h"
#include "rt_math.h"
#include "metadata.h"
#include "imgiomanager.h"
#pragma GCC diagnostic warning "-Wextra"
#define PRINT_HDR_PS_DETECTION 0

using namespace rtengine;


namespace rtengine {

extern const Settings *settings;

} // namespace rtengine


namespace {

std::string validateUtf8(const std::string &str, const std::string &on_error="???")
{
    if (Glib::ustring(str).validate()) {
        return str;
    }

    return on_error;
}

} // namespace


FramesMetaData* FramesMetaData::fromFile(const Glib::ustring& fname)
{
    return new FramesData(fname);
}

FramesData::FramesData(const Glib::ustring &fname, bool only_make_model):
    ok_(false),
    fname_(fname),
    dcrawFrameCount(0),
    time(),
    timeStamp(),
    iso_speed(0),
    aperture(0.),
    focal_len(0.),
    focal_len35mm(0.),
    focus_dist(0.f),
    shutter(0.),
    expcomp(0.),
    make("Unknown"),
    model("Unknown"),
    orientation("Unknown"),
    lens("Unknown"),
    software(""),
    sampleFormat(IIOSF_UNKNOWN),
    isPixelShift(false),
    isHDR(false),
    rating_(0),
    color_label_(-1),
    w_(-1),
    h_(-1),
    dng_(false),
    raw_(false)
{
    memset(&time, 0, sizeof(time));
    timeStamp = 0;
    iso_speed = 0;
    aperture = 0.0;
    focal_len = 0.0;
    focal_len35mm = 0.0;
    focus_dist = 0.0f;
    shutter = 0.0;
    expcomp = 0.0;
    make.clear();
    model.clear();
    serial.clear();
    orientation.clear();
    lens.clear();

    try {
        Exiv2Metadata meta(fname);
        meta.load();
        auto &exif = meta.exifData();
        ok_ = true;

        // taken and adapted from darktable (src/common/exif.cc)
/*
   This file is part of darktable,
   copyright (c) 2009--2013 johannes hanika.
   copyright (c) 2011 henrik andersson.
   copyright (c) 2012-2017 tobias ellinghaus.

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
        
        Exiv2::ExifData::const_iterator pos;
    
        const auto find_exif_tag =
            [&](const std::string &name) -> bool
            {
                try {
                    pos = exif.findKey(Exiv2::ExifKey(name));
                    return (pos != exif.end() && pos->size());                
                } catch (std::exception &e) {
                    if (settings->verbose) {                        
                        std::cerr << "Exiv2 WARNING -- error finding tag " << name << ": " << e.what() << std::endl;
                    }
                    return false;
                }            
            };

        const auto find_tag =
            [&](decltype(Exiv2::make) func) -> bool
            {
                pos = func(exif);
                return pos != exif.end() && pos->size();
            };

        /* List of tag names taken from exiv2's printSummary() in actions.cpp */

        if (find_tag(Exiv2::make)) {
            make = validateUtf8(pos->print(&exif));
        }
        
        if (find_tag(Exiv2::model)) {
            model = validateUtf8(pos->print(&exif));
        }

        if (make.size() > 0) {
            for (const auto& corp : {
                    "Canon",
                    "NIKON",
                    "EPSON",
                    "KODAK",
                    "Kodak",
                    "OLYMPUS",
                    "PENTAX",
                    "RICOH",
                    "MINOLTA",
                    "Minolta",
                    "Konica",
                    "CASIO",
                    "Sinar",
                    "Phase One",
                    "SAMSUNG",
                    "Mamiya",
                    "MOTOROLA",
                    "Leaf",
                    "Panasonic"
                  }) {
                if (make.find(corp) != std::string::npos) { // Simplify company names
                    make = corp;
                    break;
                }
            }
        }            
        make.erase(make.find_last_not_of(' ') + 1);
        model.erase(model.find_last_not_of(' ') + 1);

        if (make.length() > 0 && model.find(make + " ") == 0) {
            model = model.substr(make.length() + 1);
        }

        if (only_make_model) {
            return;
        }

        if (find_exif_tag("Exif.Image.Software")) {
            software = pos->print();
        }

        if (find_tag(Exiv2::exposureTime)) {
            shutter = pos->toFloat();
        }

        if (find_tag(Exiv2::fNumber)) {
            aperture = pos->toFloat();
        }

        /* Read ISO speed - Nikon happens to return a pair for Lo and Hi modes */
        if (find_tag(Exiv2::isoSpeed)) {
            // if standard exif iso tag, use the old way of interpreting the return value to be more regression-save
            if (strcmp(pos->key().c_str(), "Exif.Photo.ISOSpeedRatings") == 0) {
                int isofield = pos->count() > 1 ? 1 : 0;
                iso_speed = pos->toFloat(isofield);
            } else {
                std::string str = pos->print();
                iso_speed = std::atof(str.c_str());
            }
        }
        // some newer cameras support iso settings that exceed the 16 bit of exif's ISOSpeedRatings
        if (iso_speed == 65535 || iso_speed == 0) {
            if (find_exif_tag("Exif.PentaxDng.ISO") || find_exif_tag("Exif.Pentax.ISO")) {
                std::string str = pos->print();
                iso_speed = std::atof(str.c_str());
            } else if((!g_strcmp0(make.c_str(), "SONY") || !g_strcmp0(make.c_str(), "Canon"))
                    && find_exif_tag("Exif.Photo.RecommendedExposureIndex")) {
                iso_speed = pos->toFloat();
            }
        }

        if (find_tag(Exiv2::focalLength)) {
            // This works around a bug in exiv2 the developers refuse to fix
            // For details see http://dev.exiv2.org/issues/1083
            if (pos->key() == "Exif.Canon.FocalLength" && pos->count() == 4) {
                focal_len = pos->toFloat(1);
            } else {
                focal_len = pos->toFloat();
            }
        }

        if (find_exif_tag("Exif.Photo.FocalLengthIn35mmFilm")) {
            focal_len35mm = pos->toFloat();
        }

        // if (find_tag(Exiv2::subjectDistance)) {
        //     focus_dist = pos->toFloat();
        // }
        /*
         * Get the focus distance in meters.
         */
        if (Exiv2::testVersion(0, 27, 4) && find_exif_tag("Exif.NikonLd4.LensID") && exiv2_to_long(*pos) != 0) {
            // Z lens, need to specifically look for the second instance of
            // Exif.NikonLd4.FocusDistance unless using Exiv2 0.28.x and later
            // (also expanded to 2 bytes of precision since 0.28.1).
#if EXIV2_TEST_VERSION(0, 28, 0)
            if (find_exif_tag("Exif.NikonLd4.FocusDistance2")) {
                float value = pos->toFloat();
                if (Exiv2::testVersion(0, 28, 1)) {
                    value /= 256.f;
                }
#else
            pos = exif.end();
            for (auto it = exif.begin(); it != exif.end(); it++) {
                if (it->key() == "Exif.NikonLd4.FocusDistance") {
                    pos = it;
                }
            }
            if (pos != exif.end() && pos->size()) {
                float value = pos->toFloat();
#endif // EXIV2_TEST_VERSION
                focus_dist = 0.01 * std::pow(10, value / 40);
            }
        } else if (find_exif_tag("Exif.NikonLd2.FocusDistance")
            || find_exif_tag("Exif.NikonLd3.FocusDistance")
            || (Exiv2::testVersion(0, 27, 4)
                && find_exif_tag("Exif.NikonLd4.FocusDistance"))) {
            float value = pos->toFloat();
            focus_dist = (0.01 * std::pow(10, value / 40));
        } else if (find_exif_tag("Exif.OlympusFi.FocusDistance")) {
            /* the distance is stored as a rational (fraction). according to
             * http://www.dpreview.com/forums/thread/1173960?page=4

             * some Olympus cameras have a wrong denominator of 10 in there
             * while the nominator is always in mm.  thus we ignore the
             * denominator and divide with 1000.

             * "I've checked a number of E-1 and E-300 images, and I agree
             * that the FocusDistance looks like it is in mm for the
             * E-1. However, it looks more like cm for the E-300.

             * For both cameras, this value is stored as a rational. With
             * the E-1, the denominator is always 1, while for the E-300 it
             * is 10.

             * Therefore, it looks like the numerator in both cases is in mm
             * (which makes a bit of sense, in an odd sort of way). So I
             * think what I will do in ExifTool is to take the numerator and
             * divide by 1000 to display the focus distance in meters."  --
             * Boardhead, dpreview forums in 2005
             */
            int nominator = pos->toRational(0).first;
            focus_dist = std::max(0.0, (0.001 * nominator));
        } else if (find_exif_tag("Exif.CanonFi.FocusDistanceUpper")) {
            const float FocusDistanceUpper = pos->toFloat();
            if (FocusDistanceUpper <= 0.0f
                || (int)FocusDistanceUpper >= 0xffff) {
                focus_dist = 0.0f;
            } else {
                focus_dist = FocusDistanceUpper / 100.0;
                if (find_exif_tag("Exif.CanonFi.FocusDistanceLower")) {
                    const float FocusDistanceLower = pos->toFloat();
                    if (FocusDistanceLower > 0.0f && (int)FocusDistanceLower < 0xffff) {
                        focus_dist += FocusDistanceLower / 100.0;
                        focus_dist /= 2.0;
                    }
                }
            }
        } else if (find_exif_tag("Exif.CanonSi.SubjectDistance")) {
            focus_dist = pos->toFloat() / 100.0;
        } else if (find_tag(Exiv2::subjectDistance)) {
            focus_dist = pos->toFloat();
        } else if (Exiv2::testVersion(0,27,2) && find_exif_tag("Exif.Sony2Fp.FocusPosition2")) {
            const float focus_position = pos->toFloat();

            if (focus_position && find_exif_tag("Exif.Photo.FocalLengthIn35mmFilm")) {
                const float focal_length_35mm = pos->toFloat();

                /* http://u88.n24.queensu.ca/exiftool/forum/index.php/topic,3688.msg29653.html#msg29653 */
                focus_dist =
                    (std::pow(2, focus_position / 16 - 5) + 1) * focal_length_35mm / 1000;
            }
        }
        
        if (find_tag(Exiv2::orientation)) {
            static const std::vector<std::string> ormap = {
                "Unknown",
                "Horizontal (normal)",
                "Mirror horizontal",
                "Rotate 180",
                "Mirror vertical",
                "Mirror horizontal and rotate 270 CW",
                "Rotate 90 CW",
                "Mirror horizontal and rotate 90 CW",
                "Rotate 270 CW",
                "Unknown"
            };
            auto idx = exiv2_to_long(*pos);
            if (idx >= 0 && idx < long(ormap.size())) {
                orientation = ormap[idx];
            }
            //orientation = pos->print(&exif);
        }

        if (find_tag(Exiv2::lensName)) {
            lens = pos->print(&exif);
            auto p = pos;
            std::string lenstmp;
            if (find_exif_tag("Exif.CanonFi.RFLensType") && find_exif_tag("Exif.Canon.LensModel") && (lenstmp = pos->print(&exif)).size() > 0) {
                lens = lenstmp;
            } else if (p->count() == 1 && lens == std::to_string(exiv2_to_long(*p))) {
                if (find_exif_tag("Exif.Canon.LensModel")) {
                    lens = pos->print(&exif);
                } else if (find_exif_tag("Exif.Photo.LensModel")) {
                    lens = p->print(&exif);
                }
            }
        } else if (find_exif_tag("Exif.Photo.LensSpecification") && pos->count() == 4) {
            const auto round =
                [](float f) -> float
                {
                    return int(f * 10.f + 0.5f) / 10.f;
                };
            float fl_lo = round(pos->toFloat(0));
            float fl_hi = round(pos->toFloat(1));
            float fn_lo = round(pos->toFloat(2));
            float fn_hi = round(pos->toFloat(3));
            std::ostringstream buf;
            buf << fl_lo;
            if (fl_lo < fl_hi) {
                buf << "-" << fl_hi;
            }
            buf << "mm F" << fn_lo;
            if (fn_lo < fn_hi) {
                buf << "-" << fn_hi;
            }
            lens = buf.str();
        }
        if (lens.empty() || lens.find_first_not_of('-') == std::string::npos || Glib::Regex::match_simple("\\([0-9]+\\)", lens)) {
            lens = "Unknown";
        }

        if (find_exif_tag("Exif.Image.DateTimeOriginal") || find_exif_tag("Exif.Photo.DateTimeOriginal") || find_exif_tag("Exif.Photo.DateTimeDigitized") || find_exif_tag("Exif.Image.DateTime")) {
            std::string datetime_taken = validateUtf8(pos->print(&exif));
            if (sscanf(datetime_taken.c_str(), "%d:%d:%d %d:%d:%d", &time.tm_year, &time.tm_mon, &time.tm_mday, &time.tm_hour, &time.tm_min, &time.tm_sec) == 6) {
                auto d = Glib::DateTime::create_utc(time.tm_year, time.tm_mon, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
                if (d.gobj()) {
                    d.get_ymd(time.tm_year, time.tm_mon, time.tm_mday);
                    time.tm_year -= 1900;
                    time.tm_mon -= 1;
                    time.tm_hour = d.get_hour();
                    time.tm_min = d.get_minute();
                    time.tm_sec = d.get_second();
                    timeStamp = d.to_unix();
                }
            }
        }

        if (find_exif_tag("Exif.Image.ExposureBiasValue")) {
            expcomp = pos->toFloat();
        } else if (find_exif_tag("Exif.Photo.ExposureBiasValue")) {
            expcomp = pos->toFloat();
        }

        if (find_exif_tag("Exif.Image.Rating")) {
            rating_ = int16_t(uint16_t(exiv2_to_long(*pos)));
        } else {
            auto it = meta.xmpData().findKey(Exiv2::XmpKey("Xmp.xmp.Rating"));
            if (it != meta.xmpData().end() && it->size()) {
                rating_ = int16_t(uint16_t(exiv2_to_long(*it)));
            }
        }

        {
            auto it = meta.xmpData().findKey(Exiv2::XmpKey("Xmp.xmp.Label"));
            if (it != meta.xmpData().end()) {
                color_label_ = xmp_label2color(it->toString());
            }
        }            

        // try getting some metadata from ImageDescription
        if (!make.compare(0, 5, "KODAK") && !getISOSpeed() && !getFNumber() && !getFocalLen() && !getShutterSpeed() &&
            find_exif_tag("Exif.Image.ImageDescription")) {
            std::string s = pos->toString();
            std::string line;
            Glib::MatchInfo m;
            auto iso_re = Glib::Regex::create("ISO: +([0-9]+) *");
            auto aperture_re = Glib::Regex::create("Aperture: +F([0-9.]+) *");
            auto shutter_re = Glib::Regex::create("Shutter: +([0-9.]+) *");
            auto lens_re = Glib::Regex::create("Lens \\(mm\\): +([0-9.]+) *");
            auto expcomp_re = Glib::Regex::create("Exp Comp: +([0-9.]+) *");
            const auto d =
                [&m]() -> double {
                    std::string s = m.fetch(1);
                    return atof(s.c_str());
                };
            while (true) {
                auto p = s.find('\r');
                if (p == std::string::npos) {
                    break;
                }
                auto line = s.substr(0, p);
                s = s.substr(p+1);

                if (iso_re->match(line, m)) {
                    iso_speed = d();
                } else if (aperture_re->match(line, m)) {
                    aperture = d();
                } else if (shutter_re->match(line, m)) {
                    shutter = d();
                    if (shutter) {
                        shutter = 1.0/shutter;
                    }
                } else if (lens_re->match(line, m)) {
                    focal_len = d();
                } else if (expcomp_re->match(line, m)) {
                    expcomp = d();
                }
            }
        }

        meta.getDimensions(w_, h_);

        dng_ = find_exif_tag("Exif.Image.DNGVersion");
        
        // -----------------------
        // Special file type detection (HDR, PixelShift)
        // ------------------------
        std::array<std::string, 3> images = { "Image", "SubImage1" , "SubImage2" };
        std::string mainimage = images[0];
        for (size_t i = 0; i < images.size(); ++i) {
            if (find_exif_tag("Exif." + images[i] + ".NewSubfileType") && exiv2_to_long(*pos) == 0) {
                mainimage = images[i];
                break;
            }
        }
        
        uint16 bitspersample = 0, samplesperpixel = 0, sampleformat = 0, photometric = 0, compression = 0;
        auto bps = exif.findKey(Exiv2::ExifKey("Exif." + mainimage + ".BitsPerSample"));
        auto spp = exif.findKey(Exiv2::ExifKey("Exif." + mainimage + ".SamplesPerPixel"));
        auto sf = exif.findKey(Exiv2::ExifKey("Exif." + mainimage + ".SampleFormat"));
        auto pi = exif.findKey(Exiv2::ExifKey("Exif." + mainimage + ".PhotometricInterpretation"));
        auto c = exif.findKey(Exiv2::ExifKey("Exif." + mainimage + ".Compression"));

        if ((!make.compare (0, 6, "PENTAX") || (!make.compare (0, 5, "RICOH") && !model.compare (0, 6, "PENTAX")))) {
//             if (find_exif_tag("Exif.Pentax.HDR") && exiv2_to_long(*pos) > 0) {
//                 isHDR = true;
// #if PRINT_HDR_PS_DETECTION
//                 printf("HDR detected ! -> \"HDR\" tag found\n");
// #endif
//             } else
            if (find_exif_tag("Exif.Pentax.DriveMode")) {
                std::string buf = pos->toString(3);
                if (buf.substr(0, 3) == "HDR") {
                    isHDR = true;
#if PRINT_HDR_PS_DETECTION
                    printf("HDR detected ! -> DriveMode = \"HDR\"\n");
#endif
                }
            }

            if (!isHDR && (find_exif_tag("Exif.Pentax.Quality") ||
                           find_exif_tag("Exif.PentaxDng.Quality")) &&
                (exiv2_to_long(*pos) == 7 || exiv2_to_long(*pos) == 8)) {
                isPixelShift = true;
#if PRINT_HDR_PS_DETECTION
                printf("PixelShift detected ! -> \"Quality\" = 7\n");
#endif
            }
        }

        if (make == "SONY") {
            if (find_exif_tag("Exif.SubImage1.BitsPerSample") && exiv2_to_long(*pos) == 14) {
                if (find_exif_tag("Exif.SubImage1.SamplesPerPixel") && exiv2_to_long(*pos) == 4 &&
                    find_exif_tag("Exif.SubImage1.PhotometricInterpretation") && exiv2_to_long(*pos) == 32892 &&
                    find_exif_tag("Exif.SubImage1.Compression") && exiv2_to_long(*pos) == 1) {
                    isPixelShift = true;
                }
            } else if (bps != exif.end() && (exiv2_to_long(*bps) == 14 || exiv2_to_long(*bps) == 16) &&
                       spp != exif.end() && exiv2_to_long(*spp) == 4 &&
                       c != exif.end() && exiv2_to_long(*c) == 1 &&
                       find_exif_tag("Exif.Image.Software") &&
                       pos->toString() == "make_arq") {
                isPixelShift = true;
            }
        } else if (make == "FUJIFILM") {
            if (bps != exif.end() && exiv2_to_long(*bps) == 16 &&
                spp != exif.end() && exiv2_to_long(*spp) == 4 &&
                c != exif.end() && exiv2_to_long(*c) == 1 &&
                find_exif_tag("Exif.Image.Software") &&
                pos->toString() == "make_arq") {
                isPixelShift = true;
            }
        }

        sampleFormat = IIOSF_UNKNOWN;

        bool is_external = false;
        if (sf == exif.end()) {
            auto fmt = ImageIOManager::getInstance()->getFormat(fname);
            is_external = true;
            switch (fmt) {
            case ImageIOManager::FMT_UNKNOWN:
                is_external = false;
                break;
            case ImageIOManager::FMT_JPG:
                sampleformat = SAMPLEFORMAT_UINT;
                bitspersample = 8;
                break;
            case ImageIOManager::FMT_PNG:
                sampleformat = SAMPLEFORMAT_UINT;
                bitspersample = 8;
                break;
            case ImageIOManager::FMT_PNG16:
                sampleformat = SAMPLEFORMAT_UINT;
                bitspersample = 16;
                break;
            case ImageIOManager::FMT_TIFF:
                sampleformat = SAMPLEFORMAT_UINT;
                bitspersample = 16;
                break;
            case ImageIOManager::FMT_TIFF_FLOAT:
                sampleformat = SAMPLEFORMAT_IEEEFP;
                bitspersample = 32;
                break;
            case ImageIOManager::FMT_TIFF_FLOAT16:
                sampleformat = SAMPLEFORMAT_IEEEFP;
                bitspersample = 16;
                break;
            }
            if (is_external) {
                photometric = PHOTOMETRIC_RGB;
                samplesperpixel = 3;
                orientation = "";
            }
        }

        if (!is_external) {
            if (sf == exif.end())
                /*
                 * WARNING: This is a dirty hack!
                 * We assume that files which doesn't contain the TIFFTAG_SAMPLEFORMAT tag
                 * (which is the case with uncompressed TIFFs produced by RT!) are RGB files,
                 * but that may be not true.   --- Hombre
                 */
            {
                sampleformat = SAMPLEFORMAT_UINT;
            } else {
                sampleformat = exiv2_to_long(*sf);
            }

            if (bps == exif.end() || spp == exif.end() || pi == exif.end()) {
                return;
            }

            bitspersample = exiv2_to_long(*bps);
            samplesperpixel = exiv2_to_long(*spp);

            photometric = exiv2_to_long(*pi);
            if (photometric == PHOTOMETRIC_LOGLUV) {
                if (c == exif.end()) {
                    compression = COMPRESSION_NONE;
                } else {
                    compression = exiv2_to_long(*c);
                }
            }
        }

        if (photometric == PHOTOMETRIC_RGB || photometric == PHOTOMETRIC_MINISBLACK) {
            if (sampleformat == SAMPLEFORMAT_INT || sampleformat == SAMPLEFORMAT_UINT) {
                if (bitspersample == 8) {
                    sampleFormat = IIOSF_UNSIGNED_CHAR;
                } else if (bitspersample <= 16) {
                    sampleFormat = IIOSF_UNSIGNED_SHORT;
                }
            } else if (sampleformat == SAMPLEFORMAT_IEEEFP) {
                if (bitspersample==16) {
                    sampleFormat = IIOSF_FLOAT16;
                    isHDR = true;
#if PRINT_HDR_PS_DETECTION
                    printf("HDR detected ! -> sampleFormat = %d   (16-bit)\n", sampleFormat);
#endif
                }
                else if (bitspersample == 24) {
                    sampleFormat = IIOSF_FLOAT24;
                    isHDR = true;
#if PRINT_HDR_PS_DETECTION
                    printf("HDR detected ! -> sampleFormat = %d   (24-bit)\n", sampleFormat);
#endif
                }
                else if (bitspersample == 32) {
                    sampleFormat = IIOSF_FLOAT32;
                    isHDR = true;
#if PRINT_HDR_PS_DETECTION
                    printf("HDR detected ! -> sampleFormat = %d   (32-bit)\n", sampleFormat);
#endif
                }
            }
        } else if (photometric == PHOTOMETRIC_CFA) {
            if (sampleformat == SAMPLEFORMAT_IEEEFP) {
                if (bitspersample == 16) {
                    sampleFormat = IIOSF_FLOAT16;
                    isHDR = true;
#if PRINT_HDR_PS_DETECTION
                    printf("HDR detected ! -> sampleFormat = %d   (16-bit)\n", sampleFormat);
#endif
                }
                else if (bitspersample == 24) {
                    sampleFormat = IIOSF_FLOAT24;
                    isHDR = true;
#if PRINT_HDR_PS_DETECTION
                    printf("HDR detected ! -> sampleFormat = %d   (24-bit)\n", sampleFormat);
#endif
                }
                else if (bitspersample == 32) {
                    sampleFormat = IIOSF_FLOAT32;
                    isHDR = true;
#if PRINT_HDR_PS_DETECTION
                    printf("HDR detected ! -> sampleFormat = %d   (32-bit)\n", sampleFormat);
#endif
                }
            } else if (sampleformat == SAMPLEFORMAT_INT || sampleformat == SAMPLEFORMAT_UINT) {
                if (bitspersample == 8) {   // shouldn't occur...
                    sampleFormat = IIOSF_UNSIGNED_CHAR;
                } else if (bitspersample <= 16) {
                    sampleFormat = IIOSF_UNSIGNED_SHORT;
                }
            }
        } else if (photometric == 34892 || photometric == 32892  /* Linear RAW (see DNG spec ; 32892 seem to be a flaw from Sony's ARQ files) */) {
            if (sampleformat == SAMPLEFORMAT_IEEEFP) {
                sampleFormat = IIOSF_FLOAT32;
                isHDR = true;
#if PRINT_HDR_PS_DETECTION
                printf("HDR detected ! -> sampleFormat = %d\n", sampleFormat);
#endif
            } else if (sampleformat == SAMPLEFORMAT_INT || sampleformat == SAMPLEFORMAT_UINT) {
                if (bitspersample == 8) {   // shouldn't occur...
                    sampleFormat = IIOSF_UNSIGNED_CHAR;
                } else if (bitspersample <= 16) {
                    sampleFormat = IIOSF_UNSIGNED_SHORT;
                    if (find_exif_tag("Exif.Photo.MakerNote") && (!make.compare (0, 4, "SONY")) && bitspersample >= 12 && samplesperpixel == 4) {
                        isPixelShift = true;
#if PRINT_HDR_PS_DETECTION
                        printf("PixelShift detected ! -> \"Make\" = SONY, bitsPerPixel > 8, samplesPerPixel == 4\n");
#endif
                    }
                }
            }
        } else if (photometric == PHOTOMETRIC_LOGLUV) {
            if (compression == COMPRESSION_SGILOG24) {
                sampleFormat = IIOSF_LOGLUV24;
                isHDR = true;
#if PRINT_HDR_PS_DETECTION
                printf("HDR detected ! -> sampleFormat = %d\n", sampleFormat);
#endif
            } else if (compression == COMPRESSION_SGILOG) {
                sampleFormat = IIOSF_LOGLUV32;
                isHDR = true;
#if PRINT_HDR_PS_DETECTION
                printf("HDR detected ! -> sampleFormat = %d\n", sampleFormat);
#endif
            }
        }
    } catch (std::exception &e) {
        if (settings->verbose) {
            std::cerr << "EXIV2 ERROR: " << e.what() << std::endl;
        }
        ok_ = false;
    }

    if (ok_) {
        setInternalMakeModel(make + " " + model);
    }
}


bool FramesData::getPixelShift() const
{
    return isPixelShift;
}


bool FramesData::getHDR() const
{
    return isHDR;
}


std::string FramesData::getImageType() const
{
    return isPixelShift ? "PS" : isHDR ? "HDR" : "STD";
}


std::string FramesData::getSoftware() const
{
    return software;
}


IIOSampleFormat FramesData::getSampleFormat() const
{
    return sampleFormat;
}


bool FramesData::hasExif() const
{
    return ok_;
}


tm FramesData::getDateTime() const
{
    return time;
}


time_t FramesData::getDateTimeAsTS() const
{
    return timeStamp;
}


int FramesData::getISOSpeed() const
{
    return iso_speed;
}


double FramesData::getFNumber() const
{
    return aperture;
}


double FramesData::getFocalLen() const
{
    return focal_len;
}


double FramesData::getFocalLen35mm() const
{
    return focal_len35mm;
}


float FramesData::getFocusDist() const
{
    return focus_dist;
}


double FramesData::getShutterSpeed() const
{
    return shutter;
}


double FramesData::getExpComp() const
{
    return expcomp;
}


std::string FramesData::getMake() const
{
    return make;
}


std::string FramesData::getModel() const
{
    return model;
}


std::string FramesData::getLens() const
{
    return lens;
}


std::string FramesData::getSerialNumber() const
{
    return serial;
}


std::string FramesData::getOrientation() const
{
    return orientation;
}


void FramesData::setDCRawFrameCount(unsigned int frameCount)
{
    dcrawFrameCount = frameCount;
    if (dcrawFrameCount > 0) {
        raw_ = true;
    }
}

unsigned int FramesData::getFrameCount() const
{
    return dcrawFrameCount ? dcrawFrameCount : 1;
}


Glib::ustring FramesData::getFileName() const
{
    return fname_;
}


int FramesData::getRating() const
{
    return rating_;
}

//------inherited functions--------------//


std::string FramesMetaData::apertureToString(double aperture)
{

    char buffer[256];
    sprintf (buffer, "%0.1f", aperture);
    return buffer;
}

std::string FramesMetaData::shutterToString(double shutter)
{
    char buffer[256];

    if (shutter > 0.0 && shutter <= 0.5) {
        sprintf(buffer, "1/%0.0f", 1.0 / shutter);
    } else if (int(shutter) == shutter) {
        sprintf(buffer, "%d", int(shutter));
    } else {
        sprintf(buffer, "%0.1f", shutter);
    }

    return buffer;
}

std::string FramesMetaData::expcompToString(double expcomp, bool maskZeroexpcomp)
{

    char buffer[256];

    if (maskZeroexpcomp) {
        if (expcomp != 0.0) {
            sprintf (buffer, "%+0.2f", expcomp);
            return buffer;
        } else {
            return "";
        }
    } else {
        sprintf (buffer, "%+0.2f", expcomp);
        return buffer;
    }
}

double FramesMetaData::shutterFromString(std::string s)
{
    size_t i = s.find_first_of ('/');

    if (i == std::string::npos) {
        return atof (s.c_str());
    } else {
        return atof (s.substr(0, i).c_str()) / atof (s.substr(i + 1).c_str());
    }
}

double FramesMetaData::apertureFromString(std::string s)
{

    return atof(s.c_str());
}


namespace {

template<class T>
void set_exif(Exiv2::ExifData &exif, const std::string &key, T val)
{
    try {
        exif[key] = val;
    } catch (std::exception &exc) {
        if (settings->verbose) {
            std::cout << "Exif -- error setting " << key << " to " << val << ": " << exc.what() << std::endl;
        }
    }
}

} // namespace

void FramesData::fillBasicTags(Exiv2::ExifData &exif) const
{
    if (!hasExif()) {
        return;
    }
    set_exif(exif, "Exif.Photo.ISOSpeedRatings", getISOSpeed());
    set_exif(exif, "Exif.Photo.FNumber", Exiv2::URationalValue(Exiv2::URational(round(getFNumber() * 10), 10)));
    auto s = shutterToString(getShutterSpeed());
    auto p = s.find('.');
    if (p != std::string::npos) {
        assert(p == s.length()-2);
        s = s.substr(0, p) + s.substr(p+1) + "/10";
    } else if (s.find('/') == std::string::npos) {
        s += "/1";
    }
    set_exif(exif, "Exif.Photo.ExposureTime", s);
    set_exif(exif, "Exif.Photo.FocalLength", Exiv2::URationalValue(Exiv2::URational(getFocalLen() * 10, 10)));
    set_exif(exif, "Exif.Photo.ExposureBiasValue", Exiv2::RationalValue(Exiv2::Rational(round(getExpComp() * 100), 100)));
    set_exif(exif, "Exif.Image.Make", getMake());
    set_exif(exif, "Exif.Image.Model", getModel());
    set_exif(exif, "Exif.Photo.LensModel", getLens());
    char buf[256];
    auto t = getDateTime();
    strftime(buf, 256, "%Y:%m:%d %H:%M:%S", &t);
    set_exif(exif, "Exif.Photo.DateTimeOriginal", buf);
}


void FramesData::getDimensions(int &w, int &h) const
{
    w = w_;
    h = h_;
}


void FramesData::setDimensions(int w, int h)
{
    w_ = w;
    h_ = h;
}


void FramesData::setInternalMakeModel(const std::string &s)
{
    const auto upcase =
        [](const std::string &s)
        {
            std::string ret(s);
            for (auto &c : ret) {
                c = std::toupper(c);
            }
            return ret;
        };
        
    internal_make_model_ = upcase(s);
}


int FramesData::xmp_label2color(const std::string &label)
{
    static const std::map<std::string, int> m = {
        {"Red", 1},
        {"Yellow", 2},
        {"Green", 3},
        {"Blue", 4},
        {"Purple", 5}
    };
    auto it = m.find(label);
    if (it != m.end()) {
        return it->second;
    }
    return 0;
}
        

std::string FramesData::xmp_color2label(int color)
{
    switch (color) {
    case 1: return "Red";
    case 2: return "Yellow";
    case 3: return "Green";
    case 4: return "Blue";
    case 5: return "Purple";
    default:
        return "";
    }
}
