/* -*- C++ -*-
 *
 *  This file is part of ART.
 *
 *  Copyright 2020 Alberto Griggio <alberto.griggio@gmail.com>
 *
 *  ART is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  ART is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with ART.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sstream>
#include <iostream>
#include <exiv2/exiv2.hpp>

#include "gainmap.h"
#include "rawimage.h"
#include "rawimagesource.h"
#include "rescale.h"
#include "array2D.h"


namespace rtengine {

extern const Settings *settings;

namespace {

struct OutOfBounds: public std::exception {
    const char *what() const throw() { return "out of bounds"; }
};


GainMap read_gain_map(const uint8_t *data, size_t limit, size_t idx)
{
    GainMap ret;

    const auto get_ulong =
        [&]() -> uint32_t
        {
            if (idx + 4 > limit) {
                throw OutOfBounds();
            }
            uint32_t ret = Exiv2::getULong(data+idx, Exiv2::bigEndian);
            idx += 4;
            return ret;
        };

    const auto get_double =
        [&]() -> double
        {
            if (idx + 8 > limit) {
                throw OutOfBounds();
            }
            double ret = Exiv2::getDouble(data+idx, Exiv2::bigEndian);
            idx += 8;
            return ret;
        };

    const auto get_float =
        [&]() -> float
        {
            if (idx + 4 > limit) {
                throw OutOfBounds();
            }
            float ret = Exiv2::getFloat(data+idx, Exiv2::bigEndian);
            idx += 4;
            return ret;
        };
    
    ret.top = get_ulong();
    ret.left = get_ulong();
    ret.bottom = get_ulong();
    ret.right = get_ulong();
    ret.plane = get_ulong();
    ret.planes = get_ulong();
    ret.row_pitch = get_ulong();
    ret.col_pitch = get_ulong();
    ret.map_points_v = get_ulong();
    ret.map_points_h = get_ulong();
    ret.map_spacing_v = get_double();
    ret.map_spacing_h = get_double();
    ret.map_origin_v = get_double();
    ret.map_origin_h = get_double();
    ret.map_planes = get_ulong();
    
    size_t n = ret.map_points_v * ret.map_points_h * ret.map_planes;
    ret.map_gain.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        ret.map_gain.push_back(get_float());
    }
    return ret;
}


bool check_gain_map(const uint8_t *data, size_t limit,
                    size_t &idx, size_t &size)
{
    if (idx + 16 > limit) {
        return false;
    }
    uint32_t opid = Exiv2::getULong(data+idx, Exiv2::bigEndian);
    idx += 4;
    idx += 4; // version
    idx += 4; // flags
    size = Exiv2::getULong(data+idx, Exiv2::bigEndian);
    idx += 4;
    return opid == 9;
}


std::vector<GainMap> extract_gain_maps(const std::vector<uint8_t> &buf)
{
    std::vector<GainMap> ret;
    if (buf.size() < 4) {
        return ret;
    }
    const uint8_t *data = &buf[0];
    try {
        uint32_t num_entries = Exiv2::getULong(data, Exiv2::bigEndian);
        size_t idx = 4;
        for (size_t i = 0; i < num_entries; ++i) {
            size_t size = 0;
            if (check_gain_map(data, buf.size(), idx, size)) {
                ret.push_back(read_gain_map(data, buf.size(), idx));
            }
            idx += size;
            if (idx > buf.size()) {
                ret.clear();
                break;
            }
        }
    } catch (std::exception &exc) {
        ret.clear();
    }
    return ret;
}

} // namespace


std::vector<GainMap> GainMap::read(const std::vector<uint8_t> &buf)
{
    return extract_gain_maps(buf);
}


std::string GainMap::to_str() const
{
    std::ostringstream buf;
    buf << "[top=" << top
        << ", left=" << left
        << ", bottom=" << bottom
        << ", right=" << right
        << ", plane=" << plane
        << ", planes=" << planes
        << ", row_pitch=" << row_pitch
        << ", col_pitch=" << col_pitch
        << ", map_points_v=" << map_points_v
        << ", map_points_h=" << map_points_h
        << ", map_spacing_v=" << map_spacing_v
        << ", map_spacing_h=" << map_spacing_h
        << ", map_origin_v=" << map_origin_v
        << ", map_origin_h=" << map_origin_h
        << ", map_planes=" << map_planes << "]";
    return buf.str();
}


void RawImageSource::apply_gain_map(unsigned short black[4], std::vector<GainMap> &&maps, float scale_factor)
{
    if (maps.size() != 4) {
        if (settings->verbose) {
            std::cout << "GAIN MAP: found " << maps.size() << " maps, but 4 expected. Skipping" << std::endl;
        }
        return;
    }
    for (auto &m : maps) {
        if (m.bottom + 1 < uint32_t(H) || m.right + 1 < uint32_t(W) ||
            m.plane != 0 || m.planes != 1 || m.map_planes != 1 ||
            m.row_pitch != 2 || m.col_pitch != 2 ||
            m.map_origin_v != 0 || m.map_origin_h != 0) {
            if (settings->verbose) {
                std::cout << "GAIN MAP: unable to handle this map: " << m.to_str() << std::endl;
            }
            return; // not something we can handle yet
        }
    }

    if (settings->verbose) {
        std::cout << "GAIN MAP: applying maps with " << maps[0].map_points_h << "x" << maps[0].map_points_v << " points " << std::endl;
    }

    float fblack[4];
    for (int i = 0; i < 4; ++i) {
        fblack[i] = black[i];
    }

    // now we can apply each gain map to raw_data
    array2D<float> mvals;
    for (auto &m : maps) {
        mvals(m.map_points_h, m.map_points_v, &(m.map_gain[0]), 0);

        const float col_scale = float(m.map_points_h-1) / float(W);
        const float row_scale = float(m.map_points_v-1) / float(H);

        const unsigned yend = std::min(unsigned(H), m.bottom);
        const unsigned xend = std::min(unsigned(W), m.right);
        
#ifdef _OPENMP
#       pragma omp parallel for
#endif
        for (unsigned y = m.top; y < yend; y += m.row_pitch) {
            float ys = y * row_scale;
            for (unsigned x = m.left; x < xend; x += m.col_pitch) {
                float xs = x * col_scale;
                float f = getBilinearValue(mvals, xs, ys) * scale_factor;
                float b = fblack[FC(y, x)];
                rawData[y][x] = CLIP((rawData[y][x] - b) * f + b);
            }
        }
    }
}

} // namespace rtengine
