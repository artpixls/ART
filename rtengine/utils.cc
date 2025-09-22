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
#include <cmath>
#include <cstring>
#include <cstdio>
#include "rt_math.h"

#include "utils.h"
#include "rt_math.h"

#include <sstream>
#include <iomanip>

#include <glib/gstdio.h>
#include <giomm.h>
#ifdef WIN32
#  include <windows.h>
#endif

namespace rtengine {

void poke255_uc(unsigned char*& dest, unsigned char r, unsigned char g, unsigned char b)
{
#if __BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__
    *(dest++) = b;
    *(dest++) = g;
    *(dest++) = r;
    *(dest++) = 0;
#else
    *(dest++) = 0;
    *(dest++) = r;
    *(dest++) = g;
    *(dest++) = b;
#endif
}

void poke01_d(unsigned char*& dest, double r, double g, double b, double a)
{
    double aa = (1.0 - a) * 255.;
#if __BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__
    *(dest++) = (unsigned char)(b * aa);
    *(dest++) = (unsigned char)(g * aa);
    *(dest++) = (unsigned char)(r * aa);
    *(dest++) = aa;
#else
    *(dest++) = aa;
    *(dest++) = (unsigned char)(r * aa);
    *(dest++) = (unsigned char)(g * aa);
    *(dest++) = (unsigned char)(b * aa);
#endif
}

// void poke01_f(unsigned char*& dest, float r, float g, float b)
// {
// #if __BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__
//     *(dest++) = (unsigned char)(b * 255.f);
//     *(dest++) = (unsigned char)(g * 255.f);
//     *(dest++) = (unsigned char)(r * 255.f);
//     *(dest++) = 0;
// #else
//     *(dest++) = 0;
//     *(dest++) = (unsigned char)(r * 255.f);
//     *(dest++) = (unsigned char)(g * 255.f);
//     *(dest++) = (unsigned char)(b * 255.f);
// #endif
// }

void bilinearInterp(const unsigned char* src, int sw, int sh, unsigned char* dst, int dw, int dh)
{
    int ix = 0;

    for (int i = 0; i < dh; i++) {
        int sy = i * sh / dh;

        if (sy >= sh) {
            sy = sh - 1;
        }

        double dy = (double)i * sh / dh - sy;
        int ny = sy + 1;

        if (ny >= sh) {
            ny = sy;
        }

        int or1 = 3 * sw * sy;
        int or2 = 3 * sw * ny;

        for (int j = 0; j < dw; j++) {
            int sx = j * sw / dw;

            if (sx >= sw) {
                sx = sw;
            }

            double dx = (double)j * sw / dw - sx;
            int nx = sx + 1;

            if (nx >= sw) {
                nx = sx;
            }

            int ofs11 = or1 + 3 * sx;
            int ofs12 = or1 + 3 * nx;
            int ofs21 = or2 + 3 * sx;
            int ofs22 = or2 + 3 * nx;
            unsigned int val = src[ofs11] * (1 - dx) * (1 - dy) + src[ofs12] * dx * (1 - dy) + src[ofs21] * (1 - dx) * dy + src[ofs22] * dx * dy;
            dst[ix++] = val;
            ofs11++;
            ofs12++;
            ofs21++;
            ofs22++;
            val = src[ofs11] * (1 - dx) * (1 - dy) + src[ofs12] * dx * (1 - dy) + src[ofs21] * (1 - dx) * dy + src[ofs22] * dx * dy;
            dst[ix++] = val;
            ofs11++;
            ofs12++;
            ofs21++;
            ofs22++;
            val = src[ofs11] * (1 - dx) * (1 - dy) + src[ofs12] * dx * (1 - dy) + src[ofs21] * (1 - dx) * dy + src[ofs22] * dx * dy;
            dst[ix++] = val;
        }
    }
}

void nearestInterp(const unsigned char* src, int sw, int sh, unsigned char* dst, int dw, int dh)
{
    int ix = 0;

    for (int i = 0; i < dh; i++) {
        int rofs = sw * (i * sh / dh);

        for (int j = 0; j < dw; j++) {
            int dx = rofs + j * sw / dw;
            dx *= 3;
            dst[ix++] = src[dx++];
            dst[ix++] = src[dx++];
            dst[ix++] = src[dx++];
        }
    }
}

void rotate(unsigned char* img, int& w, int& h, int deg)
{
    if (deg == 0) {
        return;
    }

    unsigned char* rotated = new unsigned char[3 * w * h];
    int ix = 0;

    if (deg == 90) {
        for (int i = 0; i < h; i++)
            for (int j = 0; j < w; j++) {
                rotated[3 * (j * h + h - i - 1) + 0] = img[ix++];
                rotated[3 * (j * h + h - i - 1) + 1] = img[ix++];
                rotated[3 * (j * h + h - i - 1) + 2] = img[ix++];
            }

        std::swap(w,h);
    } else if (deg == 270) {
        for (int i = 0; i < h; i++)
            for (int j = 0; j < w; j++) {
                rotated[3 * (h * (w - j - 1) + i) + 0] = img[ix++];
                rotated[3 * (h * (w - j - 1) + i) + 1] = img[ix++];
                rotated[3 * (h * (w - j - 1) + i) + 2] = img[ix++];
            }

        std::swap(w,h);
    } else /*if (deg == 180) */
        for (int i = 0; i < h; i++)
            for (int j = 0; j < w; j++) {
                rotated[3 * (w * (h - i - 1) + w - j - 1) + 0] = img[ix++];
                rotated[3 * (w * (h - i - 1) + w - j - 1) + 1] = img[ix++];
                rotated[3 * (w * (h - i - 1) + w - j - 1) + 2] = img[ix++];
            }

    memcpy(img, rotated, 3 * w * h);
    delete[] rotated;
}

void hflip(unsigned char* img, int w, int h)
{
    if(w > 0 && h > 0) {
        unsigned char* flipped = new unsigned char[3 * w * h];
        int ix = 0;

        for (int i = 0; i < h; i++)
            for (int j = 0; j < w; j++) {
                flipped[3 * (w * i + w - 1 - j) + 0] = img[ix++];
                flipped[3 * (w * i + w - 1 - j) + 1] = img[ix++];
                flipped[3 * (w * i + w - 1 - j) + 2] = img[ix++];
            }

        memcpy(img, flipped, 3 * w * h);
        delete[] flipped;
    }
}

void vflip(unsigned char* img, int w, int h)
{
    if(w > 0 && h > 0) {
        unsigned char* flipped = new unsigned char[3 * w * h];
        int ix = 0;

        for (int i = 0; i < h; i++)
            for (int j = 0; j < w; j++) {
                flipped[3 * (w * (h - 1 - i) + j) + 0] = img[ix++];
                flipped[3 * (w * (h - 1 - i) + j) + 1] = img[ix++];
                flipped[3 * (w * (h - 1 - i) + j) + 2] = img[ix++];
            }

        memcpy(img, flipped, 3 * w * h);
        delete[] flipped;
    }
}

Glib::ustring getFileExtension(const Glib::ustring& filename)
{
    const Glib::ustring::size_type lastdot_pos = filename.find_last_of('.');
    return
        lastdot_pos != Glib::ustring::npos
            ? filename.substr(lastdot_pos + 1).lowercase()
            : Glib::ustring();
}

bool hasJpegExtension(const Glib::ustring& filename)
{
   const Glib::ustring extension = getFileExtension(filename);
   return extension == "jpg" || extension == "jpeg";
}

bool hasTiffExtension(const Glib::ustring& filename)
{
   const Glib::ustring extension = getFileExtension(filename);
   return extension == "tif" || extension == "tiff";
}

bool hasPngExtension(const Glib::ustring& filename)
{
   return getFileExtension(filename) == "png";
}

void swab(const void* from, void* to, ssize_t n)
{
    // Adapted from glibc
    const char* char_from = static_cast<const char*>(from);
    char* char_to = static_cast<char*>(to);

    n &= ~static_cast<ssize_t>(1);

    while (n > 1) {
        const char b0 = char_from[--n], b1 = char_from[--n];
        char_to[n] = b0;
        char_to[n + 1] = b1;
    }
}


std::string getMD5(const Glib::ustring& fname, bool extended)
{

#ifdef WIN32

    std::unique_ptr<wchar_t, GFreeFunc> wfname(reinterpret_cast<wchar_t*>(g_utf8_to_utf16(fname.c_str(), -1, NULL, NULL, NULL)), g_free);

    WIN32_FILE_ATTRIBUTE_DATA fileAttr;
    if (GetFileAttributesExW(wfname.get(), GetFileExInfoStandard, &fileAttr)) {
        // We use name, size and creation time to identify a file.
        const auto identifier = Glib::ustring::compose("%1-%2-%3-%4", fileAttr.nFileSizeLow, fileAttr.ftCreationTime.dwHighDateTime, fileAttr.ftCreationTime.dwLowDateTime, fname);
        return Glib::Checksum::compute_checksum(Glib::Checksum::CHECKSUM_MD5, identifier);
    }

#else

    const auto file = Gio::File::create_for_path(fname);
    if (file) {

        try
        {
            const auto info = file->query_info("standard::*," G_FILE_ATTRIBUTE_TIME_MODIFIED);
            if (info) {
                // We only use name and size to identify a file.
                Glib::ustring identifier;
                if (!extended) {
                    identifier = Glib::ustring::compose("%1%2", fname, info->get_size());
                } else {
                    auto tv = info->modification_time();
                    identifier = Glib::ustring::compose("%1%2-%3%4", fname, info->get_size(), tv.tv_sec, tv.tv_usec);
                }
                return Glib::Checksum::compute_checksum(Glib::Checksum::CHECKSUM_MD5, identifier);
            }

        } catch(Gio::Error&) {}
    }

#endif

    return {};
}


std::string get_html_color(int r, int g, int b)
{
    std::ostringstream out;
    out << "#";
    out << std::setbase(16) << LIM(r, 0, 255) << LIM(g, 0, 255) << LIM(b, 0, 255);
    return out.str();
}


} // namespace rtengine

#if __SIZEOF_WCHAR_T__ == 4
Glib::ustring utf32_to_utf8(wchar_t* UTF32Buffer, size_t sizeOfUTF32Buffer)
{
    char *buffer2 = new char[sizeOfUTF32Buffer];
    char *pBuffer2 = buffer2;
    gchar a[6];
    for (size_t i=0; i < sizeOfUTF32Buffer/4; ++i) {
        gint bytesWritten = g_unichar_to_utf8((gunichar)UTF32Buffer[i], a);
        for (gint j=0; j < bytesWritten; ++j) {
            *(pBuffer2++) = a[j];
        }
    }
    Glib::ustring modelDesc(buffer2);
    delete [] buffer2;
    return modelDesc;
}
#endif
