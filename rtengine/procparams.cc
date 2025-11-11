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

#include <map>
#include <iterator>
#include <iostream>

#include <locale.h>

#include <glib/gstdio.h>

#include <giomm.h>
#include <sstream>
#include <fstream>

#include "curves.h"
#include "procparams.h"
#include "rtengine.h"
#include "metadata.h"
#include "halffloat.h"
#include "base64.h"
#include "iccstore.h"
#include "compress.h"
#include "clutstore.h"

#include "../rtgui/multilangmgr.h"
#include "../rtgui/options.h"
#include "../rtgui/paramsedited.h"
#include "../rtgui/ppversion.h"
#include "../rtgui/version.h"
#include "../rtgui/pathutils.h"

namespace rtengine { namespace procparams {

namespace {

class ConversionError: public std::exception {
    const char *what() const noexcept { return "base64 error"; }
};

std::string to_xmp(const Glib::ustring &data)
{
    auto res = compress(data.raw());
    return base64encode(res);
}


Glib::ustring from_xmp(const std::string &data)
{
    try {
        auto buf = base64decode(data);
        return decompress(buf);
    } catch (std::exception &e) {
        throw ConversionError();
    }
}

std::vector<double> unpack_list(const std::string &data)
{
    std::vector<double> ret;
    if (data.empty()) {
        return ret;
    }
    std::vector<uint8_t> buf;
    try {
        buf = base64decode(data);
    } catch (std::exception &exc) {
        throw ConversionError();
    }
    Exiv2::byte *p = reinterpret_cast<Exiv2::byte *>(&(buf[0]));
    for (size_t i = 0; i < buf.size(); i += sizeof(uint16_t)) {
        uint16_t v = Exiv2::getUShort(p + i, Exiv2::littleEndian);
        float f = DNG_HalfToFloat(v);
        ret.push_back(f);
    }
    return ret;
}


std::string pack_list(const std::vector<double> &data)
{
    if (data.empty()) {
        return "";
    }
    std::vector<uint8_t> bytes(data.size() * sizeof(uint16_t));
    auto p = &(bytes[0]);
    size_t off = 0;
    for (auto f : data) {
        uint16_t v = DNG_FloatToHalf(f);
        long o = Exiv2::us2Data(p + off, v, Exiv2::littleEndian);
        off += o;
    }
    return base64encode(bytes);
}

} // namespace


const short SpotParams::minRadius = 2;
const short SpotParams::maxRadius = 200;

//-----------------------------------------------------------------------------
// KeyFile
//-----------------------------------------------------------------------------

bool KeyFile::has_group(const Glib::ustring &grp) const
{
    return kf_.has_group(GRP(grp));
}


bool KeyFile::has_key(const Glib::ustring &grp, const Glib::ustring &key) const
{
    return kf_.has_key(GRP(grp), key);
}


Glib::ArrayHandle<Glib::ustring> KeyFile::get_keys(const Glib::ustring &grp) const
{
    return kf_.get_keys(GRP(grp));
}


Glib::ustring KeyFile::get_string(const Glib::ustring &grp, const Glib::ustring &key) const
{
    return kf_.get_string(GRP(grp), key);
}


int KeyFile::get_integer(const Glib::ustring &grp, const Glib::ustring &key) const
{
    return kf_.get_integer(GRP(grp), key);
}


double KeyFile::get_double(const Glib::ustring &grp, const Glib::ustring &key) const
{
    return kf_.get_double(GRP(grp), key);
}


bool KeyFile::get_boolean(const Glib::ustring &grp, const Glib::ustring &key) const
{
    return kf_.get_boolean(GRP(grp), key);
}


Glib::ArrayHandle<Glib::ustring> KeyFile::get_string_list(const Glib::ustring &grp, const Glib::ustring &key) const
{
    return kf_.get_string_list(GRP(grp), key);
}


Glib::ArrayHandle<int> KeyFile::get_integer_list(const Glib::ustring &grp, const Glib::ustring &key) const
{
    return kf_.get_integer_list(GRP(grp), key);
}


Glib::ArrayHandle<double> KeyFile::get_double_list(const Glib::ustring &grp, const Glib::ustring &key) const
{
    return kf_.get_double_list(GRP(grp), key);
}


void KeyFile::set_string(const Glib::ustring &grp, const Glib::ustring &key, const Glib::ustring &string)
{
    kf_.set_string(GRP(grp), key, string);
}


void KeyFile::set_boolean(const Glib::ustring &grp, const Glib::ustring &key, bool value)
{
    kf_.set_boolean(GRP(grp), key, value);
}


void KeyFile::set_integer(const Glib::ustring &grp, const Glib::ustring &key, int value)
{
    kf_.set_integer(GRP(grp), key, value);
}


void KeyFile::set_double(const Glib::ustring &grp, const Glib::ustring &key, double value)
{
    kf_.set_double(GRP(grp), key, value);
}


void KeyFile::set_string_list(const Glib::ustring &grp, const Glib::ustring &key, const Glib::ArrayHandle<Glib::ustring> &list)
{
    kf_.set_string_list(GRP(grp), key, list);
}


void KeyFile::set_integer_list(const Glib::ustring &grp, const Glib::ustring &key, const Glib::ArrayHandle<int> &list)
{
    kf_.set_integer_list(GRP(grp), key, list);
}


void KeyFile::set_double_list(const Glib::ustring &grp, const Glib::ustring &key, const Glib::ArrayHandle<double> &list)
{
    kf_.set_double_list(GRP(grp), key, list);
}


bool KeyFile::load_from_file(const Glib::ustring &fn)
{
    filename_ = fn;
    return kf_.load_from_file(fn);
}


bool KeyFile::load_from_data(const Glib::ustring &data)
{
    return kf_.load_from_data(data);
}


Glib::ustring KeyFile::to_data()
{
    return kf_.to_data();
}


namespace {

Glib::ustring expandRelativePath(const Glib::ustring &procparams_fname, const Glib::ustring &prefix, Glib::ustring embedded_fname)
{
    if (embedded_fname == "" || !Glib::path_is_absolute(procparams_fname)) {
        return embedded_fname;
    }

    if (prefix != "") {
        if (embedded_fname.length() < prefix.length() || embedded_fname.substr(0, prefix.length()) != prefix) {
            return embedded_fname;
        }

        embedded_fname = embedded_fname.substr(prefix.length());
    }

    if (Glib::path_is_absolute(embedded_fname)) {
        return prefix + embedded_fname;
    }

    Glib::ustring absPath = prefix + Glib::path_get_dirname(procparams_fname) + G_DIR_SEPARATOR_S + embedded_fname;
    return absPath;
}


void getFromKeyfile(
    const KeyFile& keyfile,
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    int& value
)
{
    value = keyfile.get_integer(group_name, key);
}

void getFromKeyfile(
    const KeyFile& keyfile,
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    double& value
)
{
    value = keyfile.get_double(group_name, key);
}

void getFromKeyfile(
    const KeyFile& keyfile,
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    bool& value
)
{
    try {
        value = keyfile.get_boolean(group_name, key);
    } catch (Glib::KeyFileError &e) {
        int v = keyfile.get_integer(group_name, key);
        value = v;
    }
}

void getFromKeyfile(
    const KeyFile& keyfile,
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    Glib::ustring& value
)
{
    value = keyfile.get_string(group_name, key);
}

void getFromKeyfile(
    const KeyFile& keyfile,
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    std::vector<double>& value
)
{
    value = keyfile.get_double_list(group_name, key);
    rtengine::sanitizeCurve(value);
}

void getFromKeyfile(
    const KeyFile& keyfile,
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    std::vector<float>& value
)
{
    std::vector<double> tmpval = keyfile.get_double_list(group_name, key);
    value.assign(tmpval.begin(), tmpval.end());
}

void getFromKeyfile(
    const KeyFile& keyfile,
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    std::vector<std::string>& value
)
{
    auto tmpval = keyfile.get_string_list(group_name, key);
    value.assign(tmpval.begin(), tmpval.end());
}

template<typename T>
bool assignFromKeyfile(const KeyFile& keyfile, const Glib::ustring& group_name, const Glib::ustring& key, T &value)
{
    try {
        if (keyfile.has_key(group_name, key)) {
            getFromKeyfile(keyfile, group_name, key, value);

            return true;
        }
    } catch (Glib::KeyFileError &exc) {
        auto pl = keyfile.progressListener();
        if (pl) {
            pl->error(Glib::ustring::compose("WARNING: %1: %2", keyfile.filename(), exc.what()));
            return false;
        } else {
            throw exc;
        }
    }

    return false;
}

template<typename T, typename = typename std::enable_if<std::is_enum<T>::value>::type>
bool assignFromKeyfile(const KeyFile& keyfile, const Glib::ustring& group_name, const Glib::ustring& key, const std::map<std::string, T>& mapping, T& value)
{
    try {
        if (keyfile.has_key(group_name, key)) {
            Glib::ustring v;
            getFromKeyfile(keyfile, group_name, key, v);

            const typename std::map<std::string, T>::const_iterator m = mapping.find(v);

            if (m != mapping.end()) {
                value = m->second;
            } else {
                return false;
            }

            return true;
        }
    } catch (Glib::KeyFileError &exc) {
        auto pl = keyfile.progressListener();
        if (pl) {
            pl->error(Glib::ustring::compose("WARNING: %1: %2", keyfile.filename(), exc.what()));
            return false;
        } else {
            throw exc;
        }
    }
    
    return false;
}

void putToKeyfile(
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    int value,
    KeyFile& keyfile
)
{
    keyfile.set_integer(group_name, key, value);
}

void putToKeyfile(
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    double value,
    KeyFile& keyfile
)
{
    keyfile.set_double(group_name, key, value);
}

void putToKeyfile(
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    bool value,
    KeyFile& keyfile
)
{
    keyfile.set_boolean(group_name, key, value);
}

void putToKeyfile(
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    const Glib::ustring& value,
    KeyFile& keyfile
)
{
    keyfile.set_string(group_name, key, value);
}

void putToKeyfile(
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    const std::vector<int>& value,
    KeyFile& keyfile
)
{
    const Glib::ArrayHandle<int> list = value;
    keyfile.set_integer_list(group_name, key, list);
}

void putToKeyfile(
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    const std::vector<double>& value,
    KeyFile& keyfile
)
{
    const Glib::ArrayHandle<double> list = value;
    keyfile.set_double_list(group_name, key, list);
}

void putToKeyfile(
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    const std::vector<std::string>& value,
    KeyFile& keyfile
)
{
    const Glib::ArrayHandle<Glib::ustring> list = value;
    keyfile.set_string_list(group_name, key, list);
}

template<typename T>
bool saveToKeyfile(
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    const T& value,
    KeyFile& keyfile
)
{
    putToKeyfile(group_name, key, value, keyfile);
    return true;
}

template<typename T, typename = typename std::enable_if<std::is_enum<T>::value>::type>
bool saveToKeyfile(
    const Glib::ustring& group_name,
    const Glib::ustring& key,
    const std::map<T, const char*>& mapping,
    const T& value,
    KeyFile& keyfile
)
{
    const typename std::map<T, const char*>::const_iterator m = mapping.find(value);

    if (m != mapping.end()) {
        keyfile.set_string(group_name, key, m->second);
        return true;
    }

    return false;
}


Glib::ustring filenameToUri(const Glib::ustring &fname, const Glib::ustring &basedir)
{
    if (fname.empty() || basedir.empty()) {
        return fname;
    }

    const auto stripif =
        [](std::string &p, const Glib::ustring &d) -> bool
        {
            std::string dd = Glib::filename_from_utf8(d + G_DIR_SEPARATOR_S);
            if (p.substr(0, dd.length()) == dd) {
                p = std::string(G_DIR_SEPARATOR_S) + p.substr(dd.length());
                return true;
            }
            return false;
        };
    
    try {
        auto fn = Glib::filename_from_utf8(fname);
        auto home = Glib::get_home_dir();
        if (Glib::path_is_absolute(fname)) {
            if (stripif(fn, options.ART_base_dir)) {
                return Glib::filename_to_uri(fn, "S");
            } else if (stripif(fn, options.user_config_dir)) {
                return Glib::filename_to_uri(fn, "U");
            } else if (Glib::path_get_dirname(fname) == basedir) {
                fn = fname_to_utf8(Glib::path_get_basename(fname));
                return Glib::filename_to_uri(Glib::ustring(G_DIR_SEPARATOR_S) + fn, "B");
            } else if (!home.empty() && stripif(fn, home)) {
                return Glib::filename_to_uri(fn, "H");
            } else {
                return Glib::filename_to_uri(fn);
            }
        } else {
            return Glib::filename_to_uri(Glib::ustring(G_DIR_SEPARATOR_S) + fn, "R");
        }
    } catch (Glib::ConvertError &exc) {
        //std::cout << "ERROR IN CONVERTING TO URI: " << exc.what() << std::endl;
        return fname;
    }
}


Glib::ustring filenameFromUri(const Glib::ustring &uri, const Glib::ustring &basedir)
{
    if (uri.empty()) {
        return uri;
    }
    
    try {
        // Glib::filename_from_uri seems to have troubles on windows (leads to
        // crashes), so we use the C function directly
        gchar *h = nullptr;
        gchar *fn = g_filename_from_uri(uri.c_str(), &h, nullptr);
        if (!fn) {
            return uri;
        }
        std::string f(fn);
        g_free(fn);
        if (h) {
            std::string hn(h);
            g_free(h);
            f = f.substr(1);
            if (hn == "U") {
                f = Glib::build_filename(Glib::filename_from_utf8(options.user_config_dir), f);
            } else if (hn == "S") {
                f = Glib::build_filename(Glib::filename_from_utf8(options.ART_base_dir), f);
            } else if (hn == "B") {
                f = Glib::build_filename(Glib::filename_from_utf8(basedir), f);
            } else if (hn == "H") {
                f = Glib::build_filename(Glib::get_home_dir(), f);
            } else if (hn != "R") {
                return uri;
            }
        }
        Glib::ustring ret = fname_to_utf8(f);
        return ret;
    } catch (Glib::ConvertError &e) {
        return uri;
    }
}

} // namespace



AreaMask::Shape::Shape():
    mode(ADD),
    feather(0.),
    blur(0.)
{
}

AreaMask::Rectangle::Rectangle():
    x(0.),
    y(0.),
    width(100.),
    height(100.),
    angle(0.),
    roundness(0.)
{
}


AreaMask::Gradient::Gradient():
    x(0.),
    y(0.),
    strengthStart(100.),
    strengthEnd(0.),
    angle(0.)
{
    feather = 25.;
}


std::unique_ptr<AreaMask::Shape> AreaMask::Rectangle::clone() const
{
    return std::unique_ptr<Shape>(new Rectangle(*this));
}


AreaMask::Polygon::Knot::Knot():
    x(0.),
    y(0.),
    roundness(0.)
{
}

void AreaMask::Polygon::Knot::setPos(CoordD &pos)
{
    x = pos.x;
    y = pos.y;
}


std::unique_ptr<AreaMask::Shape> AreaMask::Polygon::clone() const
{
    return std::unique_ptr<Shape>(new Polygon(*this));
}

std::unique_ptr<AreaMask::Shape> AreaMask::Gradient::clone() const
{
    return std::unique_ptr<Shape>(new Gradient(*this));
}

bool AreaMask::Shape::operator==(const Shape &other) const
{
    return mode == other.mode
           && feather == other.feather
           && blur == other.blur;
}


bool AreaMask::Shape::operator!=(const Shape &other) const
{
    return !(*this == other);
}


bool AreaMask::Polygon::Knot::operator==(const Knot &other) const
{
    return
        x == other.x
        && y == other.y
        && roundness == other.roundness;
}


int AreaMask::Shape::toImgSpace(double v, int imSize)
{
    double s2 = imSize / 2.;
    return s2 + v / 100. * s2;
}


double AreaMask::Shape::toParamRange(int v, int imSize)
{
    double s2 = imSize / 2.;
    return (double(v) - s2) * 100. / s2;
}


void AreaMask::Polygon::knots_to_list(std::vector<double> &out) const
{
    if (knots.empty()) {
        out.clear();
        return;
    }

    out.resize(knots.size() * 3);

    for (size_t i = 0, j = 0; i < knots.size() ; ++i) {
        out[j++] = knots[i].x;
        out[j++] = knots[i].y;
        out[j++] = knots[i].roundness;
    }
}


void AreaMask::Polygon::knots_from_list(const std::vector<double> &v)
{
    size_t size = v.size() / 3;
    knots.resize(size);

    for (size_t i = 0, j = 0; i < size ; ++i) {
        knots[i].x = v.at(j++);
        knots[i].y = v.at(j++);
        knots[i].roundness = v.at(j++);
    }
}


bool AreaMask::Polygon::Knot::operator!=(const Knot &other) const
{
    return !(*this == other);
}


bool AreaMask::Rectangle::operator==(const Shape &other) const
{
    const Rectangle *o = dynamic_cast<const Rectangle *>(&other);
    if (!o) {
        return false;
    }
    
    return
        x == o->x
        && y == o->y
        && width == o->width
        && height == o->height
        && angle == o->angle
        && roundness == o->roundness
        && AreaMask::Shape::operator==(other);
}


bool AreaMask::Rectangle::operator!=(const Shape &other) const
{
    return !(*this == other);
}


bool AreaMask::Polygon::operator==(const Shape &other) const
{
    const Polygon *o = dynamic_cast<const Polygon *>(&other);
    if (!o) {
        return false;
    }
    
    return
        knots == o->knots
        && AreaMask::Shape::operator==(other);
}


bool AreaMask::Polygon::operator!=(const Shape &other) const
{
    return !(*this == other);
}


bool AreaMask::Gradient::operator==(const Shape &other) const
{
    const Gradient *o = dynamic_cast<const Gradient *>(&other);
    if (!o) {
        return false;
    }

    return
        x == o->x
        && y == o->y
        && strengthStart == o->strengthStart
        && strengthEnd == o->strengthEnd
        && angle == o->angle
        && AreaMask::Shape::operator==(other);
}


bool AreaMask::Gradient::operator!=(const Shape &other) const
{
    return !(*this == other);
}


AreaMask::AreaMask():
    enabled(false),
    feather(0),
    blur(0),
    contrast{DCT_Linear},
    shapes{}
{
}


bool AreaMask::operator==(const AreaMask &other) const
{
    if (enabled == other.enabled
        && feather == other.feather
        && blur == other.blur
        && contrast == other.contrast
        && shapes.size() == other.shapes.size()) {
        for (size_t i = 0, n = shapes.size(); i < n; ++i) {
            if (*(shapes[i]) != *(other.shapes[i])) {
                return false;
            }
        }
        return true;
    } else {
        return false;
    }
}


bool AreaMask::operator!=(const AreaMask &other) const
{
    return !(*this == other);
}


bool AreaMask::isTrivial() const
{
    AreaMask n;
    n.enabled = true;
    return (!enabled || *this == n);
}


AreaMask::AreaMask(const AreaMask &other):
    enabled(other.enabled),
    feather(other.feather),
    blur(other.blur),
    contrast(other.contrast)
{
    for (const auto &s : other.shapes) {
        shapes.emplace_back(s->clone());
    }
}


AreaMask &AreaMask::operator=(const AreaMask &other)
{
    enabled = other.enabled;
    feather = other.feather;
    blur = other.blur;
    contrast = other.contrast;

    shapes.clear();
    for (const auto &s : other.shapes) {
        shapes.emplace_back(s->clone());
    }
    return *this;
}


DeltaEMask::DeltaEMask():
    enabled(false),
    L(0.0),
    C(0.0),
    H(0.0),
    range(1.0),
    decay(1),
    strength(100),
    weight_L(50),
    weight_C(75),
    weight_H(100)
{
}


bool DeltaEMask::operator==(const DeltaEMask &other) const
{
    return enabled == other.enabled
        && L == other.L
        && C == other.C
        && H == other.H
        && range == other.range
        && decay == other.decay
        && strength == other.strength
        && weight_L == other.weight_L
        && weight_C == other.weight_C
        && weight_H == other.weight_H;
}


bool DeltaEMask::operator!=(const DeltaEMask &other) const
{
    return !(*this == other);
}


DrawnMask::Stroke::Stroke():
    x(-1),
    y(-1),
    radius(0),
    opacity(1),
    erase(false)
{
}


bool DrawnMask::Stroke::operator==(const Stroke &other) const
{
    return x == other.x
        && y == other.y
        && radius == other.radius
        && erase == other.erase
        && opacity == other.opacity;
}


bool DrawnMask::Stroke::operator!=(const Stroke &other) const
{
    return !(*this == other);
}


DrawnMask::DrawnMask():
    enabled(false),
    feather(0.0),
    opacity(1),
    smoothness(0),
    contrast{DCT_Linear},
    strokes(),
    mode(INTERSECT)
{
}


bool DrawnMask::operator==(const DrawnMask &other) const
{
    return enabled == other.enabled
        && feather == other.feather
        && opacity == other.opacity
        && smoothness == other.smoothness
        && contrast == other.contrast
        && strokes == other.strokes
        && mode == other.mode;
}


bool DrawnMask::operator!=(const DrawnMask &other) const
{
    return !(*this == other);
}


bool DrawnMask::isTrivial() const
{
    return !enabled;
}


void DrawnMask::strokes_to_list(std::vector<double> &out) const
{
    if (strokes.empty()) {
        return;
    }

    const auto same =
        [](const Stroke &a, const Stroke &b) -> bool
        {
            return a.radius == b.radius
                && a.erase == b.erase
                && a.opacity == b.opacity;
        };

    size_t pos = 0;
    while (pos < strokes.size()) {
        size_t n = 1;
        while (n < 2048 // 2048 is the upper bound for integers that can be
                        // represented precisely as HalfFloats. Anything
                        // larger might be rounded up or down, leading to mask
                        // corruption
               && pos + n < strokes.size()
               && same(strokes[pos + n], strokes[pos])) {
            ++n;
        }
        assert(n > 0);
        out.push_back(n);
        out.push_back(strokes[pos].radius);
        out.push_back(int(!strokes[pos].erase));
        out.push_back(strokes[pos].opacity);
        for (size_t i = 0; i < n; ++i) {
            out.push_back(strokes[pos].x);
            out.push_back(strokes[pos].y);
            ++pos;
        }
    }
}


void DrawnMask::strokes_from_list(const std::vector<double> &v)
{
    strokes.clear();
    size_t pos = 0;
    while (pos + 4 < v.size()) {
        int n = v[pos++];
        Stroke s;
        s.radius = v[pos++];
        s.erase = !bool(v[pos++]);
        s.opacity = v[pos++];
        for (int i = 0; i < n && pos + 1 < v.size(); ++i) {
            strokes.push_back(s);
            strokes.back().x = v[pos++];
            strokes.back().y = v[pos++];
        }
    }
}


ParametricMask::ParametricMask():
    enabled(false),
    blur(0),
    hue{
        FCT_MinMaxCPoints,
            0.166666667,
            1.,
            0.35,
            0.35,
            0.8287775246,
            1.,
            0.35,
            0.35
    },
    chromaticity{
        FCT_MinMaxCPoints,
            0.,
            1.,
            0.35,
            0.35,
            1.,
            1.,
            0.35,
            0.35
            },
    lightness{
        FCT_MinMaxCPoints,
            0.,
            1.,
            0.35,
            0.35,
            1.,
            1.,
            0.35,
            0.35
            },
    lightnessDetail(0),
    contrastThreshold(0)
{
}


bool ParametricMask::operator==(const ParametricMask &other) const
{
    return enabled == other.enabled
        && blur == other.blur
        && hue == other.hue
        && chromaticity == other.chromaticity
        && lightness == other.lightness
        && lightnessDetail == other.lightnessDetail
        && contrastThreshold == other.contrastThreshold;
}


bool ParametricMask::operator!=(const ParametricMask &other) const
{
    return !(*this == other);
}


LinkedMask::LinkedMask():
    enabled(false),
    inverted(false),
    toolname(""),
    name("")
{
}


bool LinkedMask::operator==(const LinkedMask &other) const
{
    return enabled == other.enabled
        && inverted == other.inverted
        && toolname == other.toolname
        && name == other.name;
}


bool LinkedMask::operator!=(const LinkedMask &other) const
{
    return !(*this == other);
}


ExternalMask::ExternalMask():
    enabled(false),
    inverted(false),
    filename(""),
    feather(0)
{
}


bool ExternalMask::operator==(const ExternalMask &other) const
{
    return enabled == other.enabled
        && inverted == other.inverted
        && filename == other.filename
        && feather == other.feather;
}


bool ExternalMask::operator!=(const ExternalMask &other) const
{
    return !(*this == other);
}


Mask::Mask():
    enabled(true),
    inverted(false),
    parametricMask(),
    areaMask(),
    deltaEMask(),
    drawnMask(),
    externalMask(),
    linkedMask(),
    name(""),
    curve{DCT_Linear},
    posterization(0),
    smoothing(0),
    opacity(100)
{
}


bool Mask::operator==(const Mask &other) const
{
    return enabled == other.enabled
        && inverted == other.inverted
        && parametricMask == other.parametricMask
        && areaMask == other.areaMask
        && deltaEMask == other.deltaEMask
        && drawnMask == other.drawnMask
        && externalMask == other.externalMask
        && linkedMask == other.linkedMask
        && name == other.name
        && curve == other.curve
        && posterization == other.posterization
        && smoothing == other.smoothing
        && opacity == other.opacity;
}


bool Mask::operator!=(const Mask &other) const
{
    return !(*this == other);
}


namespace {

AreaMask::Shape::Mode str2mode(const Glib::ustring &mode)
{
    if (mode == "subtract") {
        return AreaMask::Shape::SUBTRACT;
    } else if (mode == "intersect") {
        return AreaMask::Shape::INTERSECT;
    } else {
        return AreaMask::Shape::ADD;
    }
}


Glib::ustring mode2str(AreaMask::Shape::Mode mode)
{
    switch (mode) {
    case AreaMask::Shape::ADD: return "add";
    case AreaMask::Shape::SUBTRACT: return "subtract";
    case AreaMask::Shape::INTERSECT: return "intersect";
    default:
        assert(false);
        return "";
    }
}

AreaMask::Shape::Type str2type(const Glib::ustring &type)
{
    if (type == "rectangle") {
        return AreaMask::Shape::RECTANGLE;
    } else if (type == "gradient") {
        return AreaMask::Shape::GRADIENT;
    } else {
        return AreaMask::Shape::POLYGON;
    }
}


Glib::ustring type2str(AreaMask::Shape::Type type)
{
    switch (type) {
    case AreaMask::Shape::RECTANGLE: return "rectangle";
    case AreaMask::Shape::GRADIENT: return "gradient";
    case AreaMask::Shape::POLYGON: return "polygon";
    default:
        assert(false);
        return "";
    }
}

} // namespace


bool Mask::load(int ppVersion, const KeyFile &keyfile, const Glib::ustring &basedir, const Glib::ustring &group_name, const Glib::ustring &prefix, const Glib::ustring &suffix)
{
    bool ret = false;
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "MaskEnabled" + suffix, enabled);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "MaskInverted" + suffix, inverted);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "MaskName" + suffix, name);
    if (ppVersion < 1008) {
        parametricMask.enabled = true;
    } else {
        ret |= assignFromKeyfile(keyfile, group_name, prefix + "ParametricMaskEnabled" + suffix, parametricMask.enabled);
    }
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "HueMask" + suffix, parametricMask.hue);
    if (assignFromKeyfile(keyfile, group_name, prefix + "ChromaticityMask" + suffix, parametricMask.chromaticity)) {
        if (ppVersion < 1023) {
            for (size_t i = 1; i < parametricMask.chromaticity.size(); i += 4) {
                auto &x = parametricMask.chromaticity[i];
                x = lin2log(log2lin(x, 10.0), 50.0);
            }
        }
        ret = true;
    }
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "LightnessMask" + suffix, parametricMask.lightness);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "LightnessMaskDetail" + suffix, parametricMask.lightnessDetail);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "ContrastThresholdMask" + suffix, parametricMask.contrastThreshold);
    if (ppVersion < 1008) {
        ret |= assignFromKeyfile(keyfile, group_name, prefix + "MaskBlur" + suffix, parametricMask.blur);
        areaMask.blur = parametricMask.blur;
    } else {
        ret |= assignFromKeyfile(keyfile, group_name, prefix + "ParametricMaskBlur" + suffix, parametricMask.blur);
        ret |= assignFromKeyfile(keyfile, group_name, prefix + "AreaMaskBlur" + suffix, areaMask.blur);
    }
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "AreaMaskEnabled" + suffix, areaMask.enabled);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "AreaMaskFeather" + suffix, areaMask.feather);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "AreaMaskContrast" + suffix, areaMask.contrast);
    if (areaMask.contrast.empty() || areaMask.contrast[0] < DCT_Linear || areaMask.contrast[0] >= DCT_Unchanged) {
        areaMask.contrast = {DCT_Linear};
    }
    std::vector<std::unique_ptr<AreaMask::Shape>> s;
    for (int i = 0; ; ++i) {
        Glib::ustring type;
        Glib::ustring mode;
        bool found = true; // skipping the entire element if one key is missing
        std::string n = i ? std::string("_") + std::to_string(i) + "_" : "";
        if (ppVersion >= 1014) {
            found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "Type" + suffix, type);
        }
        if (ppVersion < 1014 || found) {
            std::unique_ptr<AreaMask::Shape> shape;
            if (ppVersion < 1014 || str2type(type) == AreaMask::Shape::Type::RECTANGLE) {
                AreaMask::Rectangle *rect = new AreaMask::Rectangle();
                found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "X" + suffix, rect->x);
                found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "Y" + suffix, rect->y);
                found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "Width" + suffix, rect->width);
                found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "Height" + suffix, rect->height);
                found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "Angle" + suffix, rect->angle);
                found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "Roundness" + suffix, rect->roundness);
                if (found) {
                    shape.reset(rect);
                } else {
                    delete rect;
                }
            } else if (str2type(type) == AreaMask::Shape::Type::POLYGON) {
                AreaMask::Polygon *poly = new AreaMask::Polygon();
                if (ppVersion < 1019) {
                    std::vector<float> v; // important: use vector<float> to avoid calling rtengine::sanitizeCurve -- need to think of a better way
                    if ((found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "Knots" + suffix, v))) {
                        ret = true;
                        std::vector<double> vv(v.begin(), v.end());
                        poly->knots_from_list(vv);
                    }
                } else {
                    Glib::ustring raw;
                    if ((found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "Knots" + suffix, raw))) {
                        ret = true;
                        std::vector<double> vv = unpack_list(raw);//.raw());
                        poly->knots_from_list(vv);
                    }
                }
                if (found) {
                    shape.reset(poly);
                } else {
                    delete poly;
                }
            } else if (str2type(type) == AreaMask::Shape::Type::GRADIENT) {
                AreaMask::Gradient *gradient = new AreaMask::Gradient();
                found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "X" + suffix, gradient->x);
                found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "Y" + suffix, gradient->y);
                found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "StrengthStart" + suffix, gradient->strengthStart);
                found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "StrengthEnd" + suffix, gradient->strengthEnd);
                found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "Angle" + suffix, gradient->angle);
                if (found) {
                    shape.reset(gradient);
                } else {
                    delete gradient;
                }
            }
            found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "Mode" + suffix, mode);
            if (ppVersion >= 1015) {
                found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "ShapeFeather" + suffix, shape->feather);
                found &= assignFromKeyfile(keyfile, group_name, prefix + "AreaMask" + n + "ShapeBlur" + suffix, shape->blur);
            }
            if (found) {
                shape->mode = str2mode(mode);
                s.emplace_back(std::move(shape));
                ret = true;
            } else {
                break;
            }
        } else {
            break;
        }
    }
    if (!s.empty()) {
        areaMask.shapes = std::move(s);
    }
    
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "DeltaEMaskEnabled" + suffix, deltaEMask.enabled);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "DeltaEMaskL" + suffix, deltaEMask.L);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "DeltaEMaskC" + suffix, deltaEMask.C);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "DeltaEMaskH" + suffix, deltaEMask.H);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "DeltaEMaskRange" + suffix, deltaEMask.range);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "DeltaEMaskDecay" + suffix, deltaEMask.decay);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "DeltaEMaskStrength" + suffix, deltaEMask.strength);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "DeltaEMaskWeightL" + suffix, deltaEMask.weight_L);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "DeltaEMaskWeightC" + suffix, deltaEMask.weight_C);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "DeltaEMaskWeightH" + suffix, deltaEMask.weight_H);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "DrawnMaskEnabled" + suffix, drawnMask.enabled);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "DrawnMaskFeather" + suffix, drawnMask.feather);
    if (ppVersion < 1038) {
        double transparency = 0;
        if (assignFromKeyfile(keyfile, group_name, prefix + "DrawnMaskTransparency" + suffix, transparency)) {
            ret = true;
            drawnMask.opacity = 1.0 - LIM01(transparency);
        }
    } else {
        ret |= assignFromKeyfile(keyfile, group_name, prefix + "DrawnMaskOpacity" + suffix, drawnMask.opacity);
    }
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "DrawnMaskSmoothness" + suffix, drawnMask.smoothness);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "DrawnMaskContrast" + suffix, drawnMask.contrast);
    if (ppVersion < 1019) {
        bool addmode = false;
        if (assignFromKeyfile(keyfile, group_name, prefix + "DrawnMaskAddMode" + suffix, addmode)) {
            ret = true;
            drawnMask.mode = addmode ? DrawnMask::ADD : DrawnMask::INTERSECT;
        }
    } else {
        int mode = 0;
        if (assignFromKeyfile(keyfile, group_name, prefix + "DrawnMaskMode" + suffix, mode)) {
            ret = true;
            drawnMask.mode = DrawnMask::Mode(LIM(mode, 0, 2));
        }
    }
    if (ppVersion < 1019) {
        std::vector<float> v; // important: use vector<float> to avoid calling rtengine::sanitizeCurve -- need to think of a better way
        if (assignFromKeyfile(keyfile, group_name, prefix + "DrawnMaskStrokes" + suffix, v)) {
            ret = true;
            std::vector<double> vv(v.begin(), v.end());
            drawnMask.strokes_from_list(vv);
        }
    } else {
        Glib::ustring raw;
        if (assignFromKeyfile(keyfile, group_name, prefix + "DrawnMaskStrokes" + suffix, raw)) {
            ret = true;
            std::vector<double> vv = unpack_list(raw);
            drawnMask.strokes_from_list(vv);
            if (ppVersion < 1022 && !drawnMask.strokes.empty()) {
                std::vector<DrawnMask::Stroke> stmp;
                stmp.swap(drawnMask.strokes);
                drawnMask.strokes.push_back(stmp[0]);
                for (size_t i = 1; i < stmp.size(); ++i) {
                    auto &p = drawnMask.strokes.back();
                    auto &s = stmp[i];
                    if (p.radius != s.radius && p.radius > 0 && s.radius > 0 &&
                        p.opacity == s.opacity && p.erase == s.erase) {
                        drawnMask.strokes.push_back(DrawnMask::Stroke());
                    }
                    drawnMask.strokes.push_back(s);
                }
            }
        }
    }
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "MaskCurve" + suffix, curve);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "MaskPosterization" + suffix, posterization);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "MaskSmoothing" + suffix, smoothing);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "MaskOpacity" + suffix, opacity);

    ret |= assignFromKeyfile(keyfile, group_name, prefix + "LinkedMaskEnabled" + suffix, linkedMask.enabled);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "LinkedMaskInverted" + suffix, linkedMask.inverted);
    {
        Glib::ustring tmp;
        if (assignFromKeyfile(keyfile, group_name, prefix + "LinkedMask" + suffix, tmp) && !tmp.empty()) {
            auto idx = tmp.find('|');
            if (idx == Glib::ustring::npos) {
                throw Glib::KeyFileError(Glib::KeyFileError::INVALID_VALUE, "invalid value for " + prefix + "LinkedMask" + suffix + ": " + tmp);
            }
            linkedMask.toolname = tmp.substr(0, idx);
            linkedMask.name = tmp.substr(idx+1);
            ret = true;
        }
    }

    ret |= assignFromKeyfile(keyfile, group_name, prefix + "ExternalMaskEnabled" + suffix, externalMask.enabled);
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "ExternalMaskInverted" + suffix, externalMask.inverted);
    if (assignFromKeyfile(keyfile, group_name, prefix + "ExternalMaskFilename" + suffix, externalMask.filename)) {
        externalMask.filename = filenameFromUri(externalMask.filename, basedir);
        ret = true;
    }
    ret |= assignFromKeyfile(keyfile, group_name, prefix + "ExternalMaskFeather" + suffix, externalMask.feather);
    
    return ret;
}


void Mask::save(KeyFile &keyfile, const Glib::ustring &basedir, const Glib::ustring &group_name, const Glib::ustring &prefix, const Glib::ustring &suffix) const
{
    putToKeyfile(group_name, prefix + "MaskEnabled" + suffix, enabled, keyfile);
    putToKeyfile(group_name, prefix + "MaskInverted" + suffix, inverted, keyfile);
    putToKeyfile(group_name, prefix + "MaskName" + suffix, name, keyfile);
    putToKeyfile(group_name, prefix + "MaskCurve" + suffix, curve, keyfile);
    putToKeyfile(group_name, prefix + "MaskPosterization" + suffix, posterization, keyfile);
    putToKeyfile(group_name, prefix + "MaskSmoothing" + suffix, smoothing, keyfile);
    putToKeyfile(group_name, prefix + "MaskOpacity" + suffix, opacity, keyfile);
    putToKeyfile(group_name, prefix + "ParametricMaskEnabled" + suffix, parametricMask.enabled, keyfile);
    putToKeyfile(group_name, prefix + "HueMask" + suffix, parametricMask.hue, keyfile);
    putToKeyfile(group_name, prefix + "ChromaticityMask" + suffix, parametricMask.chromaticity, keyfile);
    putToKeyfile(group_name, prefix + "LightnessMask" + suffix, parametricMask.lightness, keyfile);
    putToKeyfile(group_name, prefix + "LightnessMaskDetail" + suffix, parametricMask.lightnessDetail, keyfile);
    putToKeyfile(group_name, prefix + "ContrastThresholdMask" + suffix, parametricMask.contrastThreshold, keyfile);
    putToKeyfile(group_name, prefix + "ParametricMaskBlur" + suffix, parametricMask.blur, keyfile);
    putToKeyfile(group_name, prefix + "AreaMaskEnabled" + suffix, areaMask.enabled, keyfile);
    putToKeyfile(group_name, prefix + "AreaMaskFeather" + suffix, areaMask.feather, keyfile);
    putToKeyfile(group_name, prefix + "AreaMaskBlur" + suffix, areaMask.blur, keyfile);
    putToKeyfile(group_name, prefix + "AreaMaskContrast" + suffix, areaMask.contrast, keyfile);

    for (size_t i = 0; i < areaMask.shapes.size(); ++i) {
        auto &a = areaMask.shapes[i];
        std::string n = i ? std::string("_") + std::to_string(i) + "_" : "";
        putToKeyfile(group_name, prefix + "AreaMask" + n + "Type" + suffix, type2str(a->getType()), keyfile);
        switch (a->getType()) {
        case AreaMask::Shape::Type::POLYGON:
        {
            auto poly = static_cast<AreaMask::Polygon*>(a.get());
            std::vector<double> v;
            poly->knots_to_list(v);
            putToKeyfile(group_name, prefix + "AreaMask" + n + "Knots" + suffix, pack_list(v), keyfile);
            break;
        }
        case AreaMask::Shape::Type::GRADIENT:
        {
            auto gradient = static_cast<AreaMask::Gradient*>(a.get());
            putToKeyfile(group_name, prefix + "AreaMask" + n + "X" + suffix, gradient->x, keyfile);
            putToKeyfile(group_name, prefix + "AreaMask" + n + "Y" + suffix, gradient->y, keyfile);
            putToKeyfile(group_name, prefix + "AreaMask" + n + "StrengthStart" + suffix, gradient->strengthStart, keyfile);
            putToKeyfile(group_name, prefix + "AreaMask" + n + "StrengthEnd" + suffix, gradient->strengthEnd, keyfile);
            putToKeyfile(group_name, prefix + "AreaMask" + n + "Angle" + suffix, gradient->angle, keyfile);
            break;
        }
        case AreaMask::Shape::Type::RECTANGLE:
        default:
        {
            auto rect = static_cast<AreaMask::Rectangle*>(a.get());
            putToKeyfile(group_name, prefix + "AreaMask" + n + "X" + suffix, rect->x, keyfile);
            putToKeyfile(group_name, prefix + "AreaMask" + n + "Y" + suffix, rect->y, keyfile);
            putToKeyfile(group_name, prefix + "AreaMask" + n + "Width" + suffix, rect->width, keyfile);
            putToKeyfile(group_name, prefix + "AreaMask" + n + "Height" + suffix, rect->height, keyfile);
            putToKeyfile(group_name, prefix + "AreaMask" + n + "Angle" + suffix, rect->angle, keyfile);
            putToKeyfile(group_name, prefix + "AreaMask" + n + "Roundness" + suffix, rect->roundness, keyfile);
        }
        }
        putToKeyfile(group_name, prefix + "AreaMask" + n + "Mode" + suffix, mode2str(a->mode), keyfile);
        putToKeyfile(group_name, prefix + "AreaMask" + n + "ShapeFeather" + suffix, a->feather, keyfile);
        putToKeyfile(group_name, prefix + "AreaMask" + n + "ShapeBlur" + suffix, a->blur, keyfile);
    }

    putToKeyfile(group_name, prefix + "DeltaEMaskEnabled" + suffix, deltaEMask.enabled, keyfile);
    putToKeyfile(group_name, prefix + "DeltaEMaskL" + suffix, deltaEMask.L, keyfile);
    putToKeyfile(group_name, prefix + "DeltaEMaskC" + suffix, deltaEMask.C, keyfile);
    putToKeyfile(group_name, prefix + "DeltaEMaskH" + suffix, deltaEMask.H, keyfile);
    putToKeyfile(group_name, prefix + "DeltaEMaskRange" + suffix, deltaEMask.range, keyfile);
    putToKeyfile(group_name, prefix + "DeltaEMaskDecay" + suffix, deltaEMask.decay, keyfile);
    putToKeyfile(group_name, prefix + "DeltaEMaskStrength" + suffix, deltaEMask.strength, keyfile);
    putToKeyfile(group_name, prefix + "DeltaEMaskWeightL" + suffix, deltaEMask.weight_L, keyfile);
    putToKeyfile(group_name, prefix + "DeltaEMaskWeightC" + suffix, deltaEMask.weight_C, keyfile);
    putToKeyfile(group_name, prefix + "DeltaEMaskWeightH" + suffix, deltaEMask.weight_H, keyfile);
    putToKeyfile(group_name, prefix + "DrawnMaskEnabled" + suffix, drawnMask.enabled, keyfile);
    putToKeyfile(group_name, prefix + "DrawnMaskFeather" + suffix, drawnMask.feather, keyfile);
    putToKeyfile(group_name, prefix + "DrawnMaskOpacity" + suffix, drawnMask.opacity, keyfile);
    putToKeyfile(group_name, prefix + "DrawnMaskSmoothness" + suffix, drawnMask.smoothness, keyfile);
    putToKeyfile(group_name, prefix + "DrawnMaskContrast" + suffix, drawnMask.contrast, keyfile);
    putToKeyfile(group_name, prefix + "DrawnMaskMode" + suffix, int(drawnMask.mode), keyfile);
    std::vector<double> v;
    drawnMask.strokes_to_list(v);
    putToKeyfile(group_name, prefix + "DrawnMaskStrokes" + suffix, pack_list(v), keyfile);

    putToKeyfile(group_name, prefix + "LinkedMaskEnabled" + suffix, linkedMask.enabled, keyfile);
    putToKeyfile(group_name, prefix + "LinkedMaskInverted" + suffix, linkedMask.inverted, keyfile);
    {
        Glib::ustring tmp;
        if (!linkedMask.toolname.empty() || !linkedMask.name.empty()) {
            tmp = linkedMask.toolname + "|" + linkedMask.name;
        }
        putToKeyfile(group_name, prefix + "LinkedMask" + suffix, tmp, keyfile);
    }

    putToKeyfile(group_name, prefix + "ExternalMaskEnabled" + suffix, externalMask.enabled, keyfile);
    putToKeyfile(group_name, prefix + "ExternalMaskInverted" + suffix, externalMask.inverted, keyfile);
    putToKeyfile(group_name, prefix + "ExternalMaskFilename" + suffix, filenameToUri(externalMask.filename, basedir), keyfile);
    putToKeyfile(group_name, prefix + "ExternalMaskFeather" + suffix, externalMask.feather, keyfile);
}


ExposureParams::ExposureParams():
    enabled(true),
    hrmode(HR_OFF),
    expcomp(0),
    black(0),
    hrblur(0)
{
}


bool ExposureParams::operator==(const ExposureParams &other) const
{
    return enabled == other.enabled
        && hrmode == other.hrmode
        && expcomp == other.expcomp
        && black == other.black
        && hrblur == other.hrblur;
}


bool ExposureParams::operator!=(const ExposureParams &other) const
{
    return !(*this == other);
}


SaturationParams::SaturationParams():
    enabled(false),
    saturation(0),
    vibrance(0)
{
}


bool SaturationParams::operator==(const SaturationParams &other) const
{
    return enabled == other.enabled
        && saturation == other.saturation
        && vibrance == other.vibrance;
}


bool SaturationParams::operator!=(const SaturationParams &other) const
{
    return !(*this == other);
}


ToneCurveParams::ToneCurveParams():
    enabled(false),
    contrast(0),
    curve{
        DCT_Linear
    },
    curve2{
        DCT_Linear
    },
    curveMode(ToneCurveParams::TcMode::NEUTRAL),
    curveMode2(ToneCurveParams::TcMode::NEUTRAL),
    histmatching(false),
    fromHistMatching(false),
    saturation{ FCT_Linear },
    saturation2{ DCT_Linear },
    perceptualStrength(100),
    contrastLegacyMode(false),
    whitePoint(1.0),
    basecurve(ToneCurveParams::BcMode::LINEAR)
{
}


bool ToneCurveParams::operator ==(const ToneCurveParams& other) const
{
    return enabled == other.enabled
        && contrast == other.contrast
        && histmatching == other.histmatching
        && (histmatching || (curve == other.curve))
        && (histmatching || (curve2 == other.curve2))
        && curveMode == other.curveMode
        && curveMode2 == other.curveMode2
        //&& fromHistMatching == other.fromHistMatching
        && saturation == other.saturation
        && saturation2 == other.saturation2
        && perceptualStrength == other.perceptualStrength
        && contrastLegacyMode == other.contrastLegacyMode
        && whitePoint == other.whitePoint;
}


bool ToneCurveParams::operator !=(const ToneCurveParams& other) const
{
    return !(*this == other);
}


bool ToneCurveParams::hasWhitePoint() const
{
    const auto good =
        [](const std::vector<double> &c, TcMode m) -> bool
        {
            if (c.empty() || c[0] == DCT_Empty || c[0] == DCT_Linear) {
                return true;
            }
            return m != TcMode::SATANDVALBLENDING && m != TcMode::PERCEPTUAL;
        };
    return !contrastLegacyMode && good(curve, curveMode) && good(curve2, curveMode2);
}


LabCurveParams::LabCurveParams() :
    enabled(false),
    brightness(0),
    contrast(0),
    chromaticity(0),
    lcurve{
        DCT_Linear
    },
    acurve{
        DCT_Linear
    },
    bcurve{
        DCT_Linear
    }
{
}

bool LabCurveParams::operator ==(const LabCurveParams& other) const
{
    return
        enabled == other.enabled
        && brightness == other.brightness
        && contrast == other.contrast
        && chromaticity == other.chromaticity
        && lcurve == other.lcurve
        && acurve == other.acurve
        && bcurve == other.bcurve;
}

bool LabCurveParams::operator !=(const LabCurveParams& other) const
{
    return !(*this == other);
}

RGBCurvesParams::RGBCurvesParams() :
    enabled(false),
    rcurve{
        DCT_Linear
    },
    gcurve{
        DCT_Linear
    },
    bcurve{
        DCT_Linear
    }
{
}

bool RGBCurvesParams::operator ==(const RGBCurvesParams& other) const
{
    return
        enabled == other.enabled
        && rcurve == other.rcurve
        && gcurve == other.gcurve
        && bcurve == other.bcurve;
}

bool RGBCurvesParams::operator !=(const RGBCurvesParams& other) const
{
    return !(*this == other);
}


LocalContrastParams::Region::Region():
    contrast(0),
    curve{
        FCT_MinMaxCPoints,
        0.0,
        0.5,
        0.0,
        0.0,
        1.0,
        0.5,
        0.0,
        0.0
    }
{
}


bool LocalContrastParams::Region::operator==(const Region &other) const
{
    return contrast == other.contrast
        && curve == other.curve;
}


bool LocalContrastParams::Region::operator!=(const Region &other) const
{
    return !(*this == other);
}


LocalContrastParams::LocalContrastParams():
    enabled(false),
    regions{Region()},
    masks{Mask()},
    showMask(-1),
    selectedRegion(0)
{
}


bool LocalContrastParams::operator==(const LocalContrastParams &other) const
{
    return
        enabled == other.enabled
        && regions == other.regions
        && masks == other.masks
        && showMask == other.showMask;
}


bool LocalContrastParams::operator!=(const LocalContrastParams &other) const
{
    return !(*this == other);
}


SharpeningParams::SharpeningParams() :
    enabled(false),
    contrast(20.0),
    radius(0.5),
    amount(200),
    threshold(20, 80, 2000, 1200, false),
    edgesonly(false),
    edges_radius(1.9),
    edges_tolerance(1800),
    halocontrol(false),
    halocontrol_amount(85),
    method("rld"),
    deconvamount(100),
    deconvradius(0.75),
    deconvAutoRadius(true),
    deconvCornerBoost(0.0),
    deconvCornerLatitude(25),
    psf_kernel(""),
    psf_iterations(10)
{
}

bool SharpeningParams::operator ==(const SharpeningParams& other) const
{
    return
        enabled == other.enabled
        && contrast == other.contrast
        && radius == other.radius
        && amount == other.amount
        && threshold == other.threshold
        && edgesonly == other.edgesonly
        && edges_radius == other.edges_radius
        && edges_tolerance == other.edges_tolerance
        && halocontrol == other.halocontrol
        && halocontrol_amount == other.halocontrol_amount
        && method == other.method
        && deconvamount == other.deconvamount
        && deconvAutoRadius == other.deconvAutoRadius
        && (deconvAutoRadius || (deconvradius == other.deconvradius))
        && deconvCornerBoost == other.deconvCornerBoost
        && deconvCornerLatitude == other.deconvCornerLatitude
        && psf_kernel == other.psf_kernel
        && psf_iterations == other.psf_iterations;
}

bool SharpeningParams::operator !=(const SharpeningParams& other) const
{
    return !(*this == other);
}


WBParams::WBParams() :
    enabled(true),
    method(CAMERA),
    temperature(6504),
    green(1.0),
    equal(1.0),
    mult{1.0, 1.0, 1.0}
{
}

bool WBParams::operator ==(const WBParams& other) const
{
    const bool m = (method == Type::AUTO || method == Type::CAMERA);
    return
        enabled == other.enabled
        && method == other.method
        && (m || (temperature == other.temperature))
        && (m || (green == other.green))
        && (m || (equal == other.equal))
        && (m || (mult == other.mult));
}

bool WBParams::operator !=(const WBParams& other) const
{
    return !(*this == other);
}


DefringeParams::DefringeParams() :
    enabled(false),
    radius(2.0),
    threshold(13),
    huecurve{
        FCT_MinMaxCPoints,
        0.166666667,
        0.,
        0.35,
        0.35,
        0.347,
        0.,
        0.35,
        0.35,
        0.513667426,
        0,
        0.35,
        0.35,
        0.668944571,
        0.,
        0.35,
        0.35,
        0.8287775246,
        0.97835991,
        0.35,
        0.35,
        0.9908883827,
        0.,
        0.35,
        0.35
    }
{
}

bool DefringeParams::operator ==(const DefringeParams& other) const
{
    return
        enabled == other.enabled
        && radius == other.radius
        && threshold == other.threshold
        && huecurve == other.huecurve;
}

bool DefringeParams::operator !=(const DefringeParams& other) const
{
    return !(*this == other);
}

ImpulseDenoiseParams::ImpulseDenoiseParams() :
    enabled(false),
    thresh(50)
{
}

bool ImpulseDenoiseParams::operator ==(const ImpulseDenoiseParams& other) const
{
    return
        enabled == other.enabled
        && thresh == other.thresh;
}

bool ImpulseDenoiseParams::operator !=(const ImpulseDenoiseParams& other) const
{
    return !(*this == other);
}

DenoiseParams::DenoiseParams() :
    enabled(false),
    colorSpace(ColorSpace::RGB),
    aggressive(false),
    gamma(1.7),
    luminance(0),
    luminanceDetail(0),
    luminanceDetailThreshold(0),
    chrominanceMethod(ChrominanceMethod::AUTOMATIC),
    chrominanceAutoFactor(1),
    chrominance(15),
    chrominanceRedGreen(0),
    chrominanceBlueYellow(0),
    smoothingEnabled(false),
    guidedChromaRadius(3),
    nlDetail(80),
    nlStrength(0)
{
}


bool DenoiseParams::operator ==(const DenoiseParams& other) const
{
    return
        enabled == other.enabled
        && colorSpace == other.colorSpace
        && aggressive == other.aggressive
        && gamma == other.gamma
        && luminance == other.luminance
        && luminanceDetail == other.luminanceDetail
        && luminanceDetailThreshold == other.luminanceDetailThreshold
        && chrominanceMethod == other.chrominanceMethod
        && chrominanceAutoFactor == other.chrominanceAutoFactor
        && ((chrominanceMethod == ChrominanceMethod::AUTOMATIC) || (chrominance == other.chrominance))
        && ((chrominanceMethod == ChrominanceMethod::AUTOMATIC) || (chrominanceRedGreen == other.chrominanceRedGreen))
        && ((chrominanceMethod == ChrominanceMethod::AUTOMATIC) || (chrominanceBlueYellow == other.chrominanceBlueYellow))
        && smoothingEnabled == other.smoothingEnabled
        && guidedChromaRadius == other.guidedChromaRadius
        && nlDetail == other.nlDetail
        && nlStrength == other.nlStrength;
}


bool DenoiseParams::operator !=(const DenoiseParams& other) const
{
    return !(*this == other);
}


TextureBoostParams::Region::Region():
    strength(0),
    detailThreshold(0.2),
    iterations(1)
{
}


bool TextureBoostParams::Region::operator==(const Region &other) const
{
    return strength == other.strength
        && detailThreshold == other.detailThreshold
        && iterations == other.iterations;
}


bool TextureBoostParams::Region::operator!=(const Region &other) const
{
    return !(*this == other);
}

SpotEntry::SpotEntry() :
    radius(25),
    feather(1.f),
    opacity(1.f),
    detail(2)
{
}
float SpotEntry::getFeatherRadius() const
{
    return radius * (1.f + feather);
}

bool SpotEntry::operator ==(const SpotEntry& other) const
{
    return other.sourcePos == sourcePos
        && other.targetPos == targetPos
        && other.radius == radius
        && other.feather == feather
        && other.opacity == opacity
        && other.detail == detail;
}

bool SpotEntry::operator !=(const SpotEntry& other) const
{
    return !(*this == other);
}


SpotParams::SpotParams() :
    enabled(false)
{
    entries.clear ();
}

bool SpotParams::operator ==(const SpotParams& other) const
{
    return enabled == other.enabled && entries == other.entries;    
}

bool SpotParams::operator !=(const SpotParams& other) const
{
    return !(*this == other);
}

TextureBoostParams::TextureBoostParams() :
    enabled(false),
    regions{Region()},
    masks{Mask()},
    showMask(-1),
    selectedRegion(0)
{
}

bool TextureBoostParams::operator ==(const TextureBoostParams& other) const
{
    return
        enabled == other.enabled
        && regions == other.regions
        && masks == other.masks
        && showMask == other.showMask;
}

bool TextureBoostParams::operator !=(const TextureBoostParams& other) const
{
    return !(*this == other);
}


LogEncodingParams::LogEncodingParams():
    enabled(false),
    autocompute(false),
    autogain(false),
    gain(0.0),
    targetGray(18.0),
    blackEv(-13.5),
    whiteEv(2.5),
    regularization(60),
    satcontrol(true),
    highlightCompression(0)
{
}

bool LogEncodingParams::operator ==(const LogEncodingParams& other) const
{
    return
        enabled == other.enabled
        && autocompute == other.autocompute
        && autogain == other.autogain
        && targetGray == other.targetGray
        && (autocompute || (blackEv == other.blackEv))
        && (autocompute || (whiteEv == other.whiteEv))
        && (autogain || (gain == other.gain))
        && regularization == other.regularization
        && satcontrol == other.satcontrol
        && highlightCompression == other.highlightCompression;
}

bool LogEncodingParams::operator !=(const LogEncodingParams& other) const
{
    return !(*this == other);
}


FattalToneMappingParams::FattalToneMappingParams() :
    enabled(false),
    threshold(30),
    amount(20),
    satcontrol(false)
{
}

bool FattalToneMappingParams::operator ==(const FattalToneMappingParams& other) const
{
    return
        enabled == other.enabled
        && threshold == other.threshold
        && amount == other.amount
        && satcontrol == other.satcontrol;
}

bool FattalToneMappingParams::operator !=(const FattalToneMappingParams& other) const
{
    return !(*this == other);
}


ToneEqualizerParams::ToneEqualizerParams():
    enabled(false),
    bands{0,0,0,0,0},
    regularization(4),
    show_colormap(false),
    pivot(0)
{
}


bool ToneEqualizerParams::operator ==(const ToneEqualizerParams& other) const
{
    return
        enabled == other.enabled
        && bands == other.bands
        && regularization == other.regularization
        && show_colormap == other.show_colormap
        && pivot == other.pivot;
}


bool ToneEqualizerParams::operator !=(const ToneEqualizerParams& other) const
{
    return !(*this == other);
}


CropParams::CropParams() :
    enabled(false),
    x(-1),
    y(-1),
    w(15000),
    h(15000),
    fixratio(true),
    ratio("As Image"),
    orientation("As Image"),
    guide("Frame")
{
}

bool CropParams::operator ==(const CropParams& other) const
{
    return
        enabled == other.enabled
        && x == other.x
        && y == other.y
        && w == other.w
        && h == other.h
        && fixratio == other.fixratio
        && ratio == other.ratio
        && orientation == other.orientation
        && guide == other.guide;
}

bool CropParams::operator !=(const CropParams& other) const
{
    return !(*this == other);
}

void CropParams::mapToResized(int resizedWidth, int resizedHeight, int scale, int& x1, int& x2, int& y1, int& y2) const
{
    x1 = 0, x2 = resizedWidth, y1 = 0, y2 = resizedHeight;

    if (enabled) {
        x1 = min(resizedWidth - 1, max(0, x / scale));
        y1 = min(resizedHeight - 1, max(0, y / scale));
        x2 = min(resizedWidth, max(0, (x + w) / scale));
        y2 = min(resizedHeight, max(0, (y + h) / scale));
    }
}

CoarseTransformParams::CoarseTransformParams() :
    rotate(0),
    hflip(false),
    vflip(false)
{
}

bool CoarseTransformParams::operator ==(const CoarseTransformParams& other) const
{
    return
        rotate == other.rotate
        && hflip == other.hflip
        && vflip == other.vflip;
}

bool CoarseTransformParams::operator !=(const CoarseTransformParams& other) const
{
    return !(*this == other);
}

CommonTransformParams::CommonTransformParams() :
    autofill(true)
{
}

bool CommonTransformParams::operator ==(const CommonTransformParams& other) const
{
    return autofill == other.autofill;
}

bool CommonTransformParams::operator !=(const CommonTransformParams& other) const
{
    return !(*this == other);
}

RotateParams::RotateParams() :
    enabled(false),
    degree(0.0)
{
}

bool RotateParams::operator ==(const RotateParams& other) const
{
    return enabled == other.enabled && degree == other.degree;
}

bool RotateParams::operator !=(const RotateParams& other) const
{
    return !(*this == other);
}

DistortionParams::DistortionParams() :
    enabled(false),
    amount(0.0),
    autocompute(false)
{
}

bool DistortionParams::operator ==(const DistortionParams& other) const
{
    return enabled == other.enabled
        && autocompute == other.autocompute
        && (autocompute || (amount == other.amount));
}

bool DistortionParams::operator !=(const DistortionParams& other) const
{
    return !(*this == other);
}

LensProfParams::LensProfParams() :
    lcMode(LcMode::NONE),
    useDist(true),
    useVign(true),
    useCA(false)
{
}

bool LensProfParams::operator ==(const LensProfParams& other) const
{
    return
        lcMode == other.lcMode
        && lcpFile == other.lcpFile
        && useCA == other.useCA
        && ((lcMode == LcMode::LENSFUNAUTOMATCH || lcMode == LcMode::EXIF) || (lfCameraMake == other.lfCameraMake))
        && ((lcMode == LcMode::LENSFUNAUTOMATCH || lcMode == LcMode::EXIF) || (lfCameraModel == other.lfCameraModel))
        && ((lcMode == LcMode::LENSFUNAUTOMATCH || lcMode == LcMode::EXIF) || (lfLens == other.lfLens));
}

bool LensProfParams::operator !=(const LensProfParams& other) const
{
    return !(*this == other);
}

bool LensProfParams::useLensfun() const
{
    return lcMode == LcMode::LENSFUNAUTOMATCH || lcMode == LcMode::LENSFUNMANUAL;
}

bool LensProfParams::lfAutoMatch() const
{
    return lcMode == LcMode::LENSFUNAUTOMATCH;
}

bool LensProfParams::useLcp() const
{
    return lcMode == LcMode::LCP && lcpFile.length() > 0;
}

bool LensProfParams::lfManual() const
{
    return lcMode == LcMode::LENSFUNMANUAL;
}


bool LensProfParams::useExif() const
{
    return lcMode == LcMode::EXIF;
}


bool LensProfParams::needed() const
{
    return useLensfun() || useLcp() || useExif();
}


const std::vector<const char*>& LensProfParams::getMethodStrings() const
{
    static const std::vector<const char*> method_strings = {
        "none",
        "lfauto",
        "lfmanual",
        "lcp",
        "exif"
    };
    return method_strings;
}

Glib::ustring LensProfParams::getMethodString(LcMode mode) const
{
    return getMethodStrings()[toUnderlying(mode)];
}

LensProfParams::LcMode LensProfParams::getMethodNumber(const Glib::ustring& mode) const
{
    for (std::vector<const char*>::size_type i = 0; i < getMethodStrings().size(); ++i) {
        if (getMethodStrings()[i] == mode) {
            return static_cast<LcMode>(i);
        }
    }

    return LcMode::NONE;
}

PerspectiveParams::PerspectiveParams() :
    enabled(false),
    horizontal(0.0),
    vertical(0.0),
    angle(0.0),
    shear(0.0),
    flength(0),
    cropfactor(1),
    aspect(1),
    control_lines()
{
}

bool PerspectiveParams::operator ==(const PerspectiveParams& other) const
{
    return
        enabled == other.enabled
        && horizontal == other.horizontal
        && vertical == other.vertical
        && angle == other.angle
        && shear == other.shear
        && flength == other.flength
        && cropfactor == other.cropfactor
        && aspect == other.aspect
        && control_lines == other.control_lines;
}

bool PerspectiveParams::operator !=(const PerspectiveParams& other) const
{
    return !(*this == other);
}

GradientParams::GradientParams() :
    enabled(false),
    degree(0.0),
    feather(25),
    strength(0.60),
    centerX(0),
    centerY(0)
{
}

bool GradientParams::operator ==(const GradientParams& other) const
{
    return
        enabled == other.enabled
        && degree == other.degree
        && feather == other.feather
        && strength == other.strength
        && centerX == other.centerX
        && centerY == other.centerY;
}

bool GradientParams::operator !=(const GradientParams& other) const
{
    return !(*this == other);
}

PCVignetteParams::PCVignetteParams() :
    enabled(false),
    strength(0.60),
    feather(50),
    roundness(50),
    centerX(0),
    centerY(0)
{
}

bool PCVignetteParams::operator ==(const PCVignetteParams& other) const
{
    return
        enabled == other.enabled
        && strength == other.strength
        && feather == other.feather
        && roundness == other.roundness
        && centerX == other.centerX
        && centerY == other.centerY;
}

bool PCVignetteParams::operator !=(const PCVignetteParams& other) const
{
    return !(*this == other);
}

VignettingParams::VignettingParams() :
    enabled(false),
    amount(0),
    radius(50),
    strength(1),
    centerX(0),
    centerY(0)
{
}

bool VignettingParams::operator ==(const VignettingParams& other) const
{
    return
        enabled == other.enabled
        && amount == other.amount
        && radius == other.radius
        && strength == other.strength
        && centerX == other.centerX
        && centerY == other.centerY;
}

bool VignettingParams::operator !=(const VignettingParams& other) const
{
    return !(*this == other);
}

ChannelMixerParams::ChannelMixerParams() :
    enabled(false),
    mode(RGB_MATRIX),
    red{1000, 0, 0},
    green{0, 1000, 0},
    blue{0, 0, 1000},
    hue_tweak{0, 0, 0},
    sat_tweak{0, 0, 0}
{
}

bool ChannelMixerParams::operator ==(const ChannelMixerParams& other) const
{
    if (enabled != other.enabled || mode != other.mode) {
        return false;
    }

    for (unsigned int i = 0; i < 3; ++i) {
        if (
            red[i] != other.red[i]
            || green[i] != other.green[i]
            || blue[i] != other.blue[i]
            || hue_tweak[i] != other.hue_tweak[i]
            || sat_tweak[i] != other.sat_tweak[i]
        ) {
            return false;
        }
    }

    return true;
}

bool ChannelMixerParams::operator !=(const ChannelMixerParams& other) const
{
    return !(*this == other);
}

BlackWhiteParams::BlackWhiteParams() :
    enabled(false),
    filter("None"),
    setting("RGB-Rel"),
    mixerRed(33),
    mixerGreen(33),
    mixerBlue(33),
    gammaRed(0),
    gammaGreen(0),
    gammaBlue(0),
    colorCast(0, 0, false)
{
}

bool BlackWhiteParams::operator ==(const BlackWhiteParams& other) const
{
    return
        enabled == other.enabled
        && filter == other.filter
        && setting == other.setting
        && mixerRed == other.mixerRed
        && mixerGreen == other.mixerGreen
        && mixerBlue == other.mixerBlue
        && gammaRed == other.gammaRed
        && gammaGreen == other.gammaGreen
        && gammaBlue == other.gammaBlue
        && colorCast == other.colorCast;
}

bool BlackWhiteParams::operator !=(const BlackWhiteParams& other) const
{
    return !(*this == other);
}


HSLEqualizerParams::HSLEqualizerParams():
    enabled(false),
    hCurve{FCT_Linear},
    sCurve{FCT_Linear},
    lCurve{FCT_Linear},
    smoothing(0)
{
}


bool HSLEqualizerParams::operator==(const HSLEqualizerParams &other) const
{
    return enabled == other.enabled
        && hCurve == other.hCurve
        && sCurve == other.sCurve
        && lCurve == other.lCurve
        && smoothing == other.smoothing;
}


bool HSLEqualizerParams::operator!=(const HSLEqualizerParams &other) const
{
    return !(*this == other);
}


CACorrParams::CACorrParams() :
    enabled(false),
    red(0.0),
    blue(0.0)
{
}

bool CACorrParams::operator ==(const CACorrParams& other) const
{
    return
        enabled == other.enabled
        && red == other.red
        && blue == other.blue;
}

bool CACorrParams::operator !=(const CACorrParams& other) const
{
    return !(*this == other);
}

ResizeParams::ResizeParams() :
    enabled(false),
    scale(1.0),
    appliesTo("Cropped area"),
    dataspec(3),
    width(900),
    height(900),
    allowUpscaling(false),
    ppi(300),
    unit(PX)
{
}

bool ResizeParams::operator ==(const ResizeParams& other) const
{
    return
        enabled == other.enabled
        && scale == other.scale
        && appliesTo == other.appliesTo
        && dataspec == other.dataspec
        && width == other.width
        && height == other.height
        && allowUpscaling == other.allowUpscaling
        && ppi == other.ppi
        && unit == other.unit;
}

bool ResizeParams::operator !=(const ResizeParams& other) const
{
    return !(*this == other);
}


int ResizeParams::get_width() const
{
    switch (unit) {
    case PX: return width;
    case CM: return std::round(ppi * (width / 2.54)); 
    case INCHES: return std::round(ppi * width);
    default:
        assert(false);
        return width;
    }
}


int ResizeParams::get_height() const
{
    switch (unit) {
    case PX: return height;
    case CM: return std::round(ppi * (height / 2.54));
    case INCHES: return std::round(ppi * height);
    default:
        assert(false);
        return height;
    }
}


const Glib::ustring ColorManagementParams::NoICMString = Glib::ustring("No ICM: sRGB output");
const Glib::ustring ColorManagementParams::NoProfileString = Glib::ustring("(none)");

ColorManagementParams::ColorManagementParams() :
    inputProfile("(cameraICC)"),
    toneCurve(false),
    applyLookTable(false),
    applyBaselineExposureOffset(true),
    applyHueSatMap(true),
    dcpIlluminant(0),
    dcp_look_early(false),
    workingProfile("Rec2020"),
    outputProfile("RTv2_sRGB"),
    outputIntent(RI_RELATIVE),
    outputBPC(true),
    inputProfileCAT(false)
{
}

bool ColorManagementParams::operator ==(const ColorManagementParams& other) const
{
    return
        inputProfile == other.inputProfile
        && toneCurve == other.toneCurve
        && applyLookTable == other.applyLookTable
        && applyBaselineExposureOffset == other.applyBaselineExposureOffset
        && applyHueSatMap == other.applyHueSatMap
        && dcpIlluminant == other.dcpIlluminant
        && workingProfile == other.workingProfile
        && outputProfile == other.outputProfile
        && outputIntent == other.outputIntent
        && outputBPC == other.outputBPC
        && inputProfileCAT == other.inputProfileCAT
        && dcp_look_early == other.dcp_look_early;
}

bool ColorManagementParams::operator !=(const ColorManagementParams& other) const
{
    return !(*this == other);
}


FilmSimulationParams::FilmSimulationParams() :
    enabled(false),
    strength(100),
    after_tone_curve(false)
{
}

bool FilmSimulationParams::operator ==(const FilmSimulationParams& other) const
{
    return
        enabled == other.enabled
        && clutFilename == other.clutFilename
        && strength == other.strength
        && after_tone_curve == other.after_tone_curve
        && lut_params == other.lut_params;
}

bool FilmSimulationParams::operator !=(const FilmSimulationParams& other) const
{
    return !(*this == other);
}


SoftLightParams::SoftLightParams() :
    enabled(false),
    strength(30)
{
}

bool SoftLightParams::operator ==(const SoftLightParams& other) const
{
    return
        enabled == other.enabled
        && strength == other.strength;
}

bool SoftLightParams::operator !=(const SoftLightParams& other) const
{
    return !(*this == other);
}


DehazeParams::DehazeParams() :
    enabled(false),
    strength{
        FCT_MinMaxCPoints,
        0.0,
        0.75,
        0.0,
        0.0,
        1.0,
        0.75,
        0.0,
        0.0
    },
    showDepthMap(false),
    depth(25),
    luminance(false),
    blackpoint(0)
{
}

bool DehazeParams::operator ==(const DehazeParams& other) const
{
    return
        enabled == other.enabled
        && strength == other.strength
        && showDepthMap == other.showDepthMap
        && depth == other.depth
        && luminance == other.luminance
        && blackpoint == other.blackpoint;
}

bool DehazeParams::operator !=(const DehazeParams& other) const
{
    return !(*this == other);
}


GrainParams::GrainParams():
    enabled(false),
    iso(400),
    strength(50),
    color(false)
{
}

bool GrainParams::operator==(const GrainParams &other) const
{
    return enabled == other.enabled
        && iso == other.iso
        && strength == other.strength
        && color == other.color;
}

bool GrainParams::operator!=(const GrainParams &other) const
{
    return !(*this == other);
}


SmoothingParams::Region::Region():
    mode(Mode::GUIDED),
    channel(Channel::RGB),
    radius(0),
    sigma(0),
    epsilon(0),
    iterations(1),
    falloff(1),
    nldetail(50),
    nlstrength(0),
    numblades(9),
    angle(0),
    curvature(0),
    offset(0),
    noise_strength(10),
    noise_coarseness(30),
    halation_size(1.0),
    halation_color(0.0),
    wav_strength(0),
    wav_levels(5),
    wav_gamma(2.2)
{
}


bool SmoothingParams::Region::operator==(const Region &other) const
{
    return mode == other.mode
        && channel == other.channel
        && radius == other.radius
        && sigma == other.sigma
        && epsilon == other.epsilon
        && iterations == other.iterations
        && falloff == other.falloff
        && nlstrength == other.nlstrength
        && nldetail == other.nldetail
        && numblades == other.numblades
        && angle == other.angle
        && curvature == other.curvature
        && offset == other.offset
        && noise_strength == other.noise_strength
        && noise_coarseness == other.noise_coarseness
        && halation_size == other.halation_size
        && halation_color == other.halation_color
        && wav_strength == other.wav_strength
        && wav_levels == other.wav_levels
        && wav_gamma == other.wav_gamma;
}


bool SmoothingParams::Region::operator!=(const Region &other) const
{
    return !(*this == other);
}


SmoothingParams::SmoothingParams():
    enabled(false),
    regions{Region()},
    masks{Mask()},
    showMask(-1),
    selectedRegion(0)
{
}


bool SmoothingParams::operator==(const SmoothingParams &other) const
{
    return enabled == other.enabled
        && regions == other.regions
        && masks == other.masks
        && showMask == other.showMask;
}


bool SmoothingParams::operator!=(const SmoothingParams &other) const
{
    return !(*this == other);
}


ColorCorrectionParams::Region::Region():
    a(0),
    b(0),
    abscale(1),
    inSaturation(0),
    outSaturation(0),
    slope{1,1,1},
    offset{0,0,0},
    power{1,1,1},
    pivot{1,1,1},
    hue{0,0,0},
    sat{0,0,0},
    factor{0,0,0},
    compression{0,0,0},
    rgbluminance(false),
    hueshift(0),
    lutFilename(""),
    lut_params{},
    hsl_gamma(2.4),
    mode(ColorCorrectionParams::Mode::JZAZBZ)
{
}


bool ColorCorrectionParams::Region::operator==(const Region &other) const
{
    return a == other.a
        && b == other.b
        && abscale == other.abscale
        && inSaturation == other.inSaturation
        && outSaturation == other.outSaturation
        && slope == other.slope
        && offset == other.offset
        && power == other.power
        && pivot == other.pivot
        && hue == other.hue
        && sat == other.sat
        && factor == other.factor
        && compression == other.compression
        && rgbluminance == other.rgbluminance
        && hueshift == other.hueshift
        && lutFilename == other.lutFilename
        && lut_params == other.lut_params
        && hsl_gamma == other.hsl_gamma
        && mode == other.mode;
}


bool ColorCorrectionParams::Region::operator!=(const Region &other) const
{
    return !(*this == other);
}


ColorCorrectionParams::ColorCorrectionParams():
    enabled(false),
    regions{Region()},
    masks{Mask()},
    showMask(-1),
    selectedRegion(0)
{
}


bool ColorCorrectionParams::operator==(const ColorCorrectionParams &other) const
{
    return enabled == other.enabled
        && regions == other.regions
        && masks == other.masks
        && showMask == other.showMask;
}


bool ColorCorrectionParams::operator!=(const ColorCorrectionParams &other) const
{
    return !(*this == other);
}


RAWParams::BayerSensor::BayerSensor() :
    method(Method::RCD),
    border(4),
    imageNum(0),
    ccSteps(0),
    black0(0.0),
    black1(0.0),
    black2(0.0),
    black3(0.0),
    twogreen(true),
    linenoise(0),
    linenoiseDirection(LineNoiseDirection::BOTH),
    greenthresh(0),
    dcb_iterations(2),
    lmmse_iterations(2),
    dualDemosaicAutoContrast(true),
    dualDemosaicContrast(20),
    pixelShiftMotionCorrectionMethod(PSMotionCorrectionMethod::AUTO),
    pixelShiftEperIso(0.0),
    pixelShiftSigma(1.0),
    pixelShiftShowMotion(false),
    pixelShiftShowMotionMaskOnly(false),
    pixelShiftHoleFill(true),
    pixelShiftMedian(false),
    pixelShiftGreen(true),
    pixelShiftBlur(true),
    pixelShiftSmoothFactor(0.7),
    pixelShiftEqualBright(false),
    pixelShiftEqualBrightChannel(false),
    pixelShiftNonGreenCross(true),
    pixelShiftDemosaicMethod(getPSDemosaicMethodString(PSDemosaicMethod::AMAZE)),
    dcb_enhance(true),
    pdafLinesFilter(false),
    dynamicRowNoiseFilter(false),
    enable_black(false),
    enable_preproc(false)
{
}

bool RAWParams::BayerSensor::operator ==(const BayerSensor& other) const
{
    return
        method == other.method
        && border == other.border
        && imageNum == other.imageNum
        && ccSteps == other.ccSteps
        && black0 == other.black0
        && black1 == other.black1
        && black2 == other.black2
        && black3 == other.black3
        && twogreen == other.twogreen
        && linenoise == other.linenoise
        && linenoiseDirection == other.linenoiseDirection
        && greenthresh == other.greenthresh
        && dcb_iterations == other.dcb_iterations
        && lmmse_iterations == other.lmmse_iterations
        && dualDemosaicAutoContrast == other.dualDemosaicAutoContrast
        && dualDemosaicContrast == other.dualDemosaicContrast
        && pixelShiftMotionCorrectionMethod == other.pixelShiftMotionCorrectionMethod
        && pixelShiftEperIso == other.pixelShiftEperIso
        && pixelShiftSigma == other.pixelShiftSigma
        && pixelShiftShowMotion == other.pixelShiftShowMotion
        && pixelShiftShowMotionMaskOnly == other.pixelShiftShowMotionMaskOnly
        && pixelShiftHoleFill == other.pixelShiftHoleFill
        && pixelShiftMedian == other.pixelShiftMedian
        && pixelShiftGreen == other.pixelShiftGreen
        && pixelShiftBlur == other.pixelShiftBlur
        && pixelShiftSmoothFactor == other.pixelShiftSmoothFactor
        && pixelShiftEqualBright == other.pixelShiftEqualBright
        && pixelShiftEqualBrightChannel == other.pixelShiftEqualBrightChannel
        && pixelShiftNonGreenCross == other.pixelShiftNonGreenCross
        && pixelShiftDemosaicMethod == other.pixelShiftDemosaicMethod
        && dcb_enhance == other.dcb_enhance
        && pdafLinesFilter == other.pdafLinesFilter
        && dynamicRowNoiseFilter == other.dynamicRowNoiseFilter
        && enable_black == other.enable_black
        && enable_preproc == other.enable_preproc;
}

bool RAWParams::BayerSensor::operator !=(const BayerSensor& other) const
{
    return !(*this == other);
}

void RAWParams::BayerSensor::setPixelShiftDefaults()
{
    pixelShiftMotionCorrectionMethod = RAWParams::BayerSensor::PSMotionCorrectionMethod::AUTO;
    pixelShiftEperIso = 0.0;
    pixelShiftSigma = 1.0;
    pixelShiftHoleFill = true;
    pixelShiftMedian = false;
    pixelShiftGreen = true;
    pixelShiftBlur = true;
    pixelShiftSmoothFactor = 0.7;
    pixelShiftEqualBright = false;
    pixelShiftEqualBrightChannel = false;
    pixelShiftNonGreenCross = true;
    pixelShiftDemosaicMethod = getPSDemosaicMethodString(PSDemosaicMethod::AMAZE);
}

const std::vector<const char*>& RAWParams::BayerSensor::getMethodStrings()
{
    static const std::vector<const char*> method_strings {
        "amaze",
        "rcd",
        "lmmse",
        "igv",
        "amazebilinear",
        "rcdbilinear",
        "vng4",
        "fast",
        "mono",
        "pixelshift",
        "none"
        // "amazevng4",
        // "rcdvng4",
        // "dcb",
        // "dcbbilinear",
        // "dcbvng4",
        // "ahd",
        // "eahd",
        // "hphd"
    };
    return method_strings;
}

Glib::ustring RAWParams::BayerSensor::getMethodString(Method method)
{
    size_t i = toUnderlying(method);
    auto &v = getMethodStrings();
    return i < v.size() ? v[i] : "";
}

const std::vector<const char*>& RAWParams::BayerSensor::getPSDemosaicMethodStrings()
{
    static const std::vector<const char*> method_strings {
        "amaze",
        "amazevng4",
        "lmmse"
    };
    return method_strings;
}

Glib::ustring RAWParams::BayerSensor::getPSDemosaicMethodString(PSDemosaicMethod method)
{
    return getPSDemosaicMethodStrings()[toUnderlying(method)];
}



RAWParams::XTransSensor::XTransSensor() :
    method(Method::THREE_PASS),
    dualDemosaicAutoContrast(true),
    dualDemosaicContrast(20),
    border(7),
    ccSteps(0),
    blackred(0.0),
    blackgreen(0.0),
    blackblue(0.0),
    enable_black(false)
{
}

bool RAWParams::XTransSensor::operator ==(const XTransSensor& other) const
{
    return
        method == other.method
        && dualDemosaicAutoContrast == other.dualDemosaicAutoContrast
        && dualDemosaicContrast == other.dualDemosaicContrast
        && border == other.border
        && ccSteps == other.ccSteps
        && blackred == other.blackred
        && blackgreen == other.blackgreen
        && blackblue == other.blackblue
        && enable_black == other.enable_black;
}

bool RAWParams::XTransSensor::operator !=(const XTransSensor& other) const
{
    return !(*this == other);
}

const std::vector<const char*>& RAWParams::XTransSensor::getMethodStrings()
{
    static const std::vector<const char*> method_strings {
        "4-pass",
        "3-pass (best)",
        "2-pass",
        "1-pass (medium)",
        "fast",
        "mono",
        "none"
    };
    return method_strings;
}

Glib::ustring RAWParams::XTransSensor::getMethodString(Method method)
{
    return getMethodStrings()[toUnderlying(method)];
}

RAWParams::RAWParams() :
    df_autoselect(false),
    ff_AutoSelect(false),
    ff_BlurRadius(32),
    ff_BlurType(getFlatFieldBlurTypeString(FlatFieldBlurType::AREA)),
    ff_AutoClipControl(false),
    ff_clipControl(0),
    ff_embedded(false),
    ca_autocorrect(false),
    ca_avoidcolourshift(true),
    caautoiterations(2),
    cared(0.0),
    cablue(0.0),
    expos(1.0),
    hotPixelFilter(false),
    deadPixelFilter(false),
    hotdeadpix_thresh(100),
    enable_darkframe(false),
    enable_flatfield(false),
    enable_ca(false),
    enable_hotdeadpix(false),
    enable_whitepoint(false)
{
}

bool RAWParams::operator ==(const RAWParams& other) const
{
    return
        bayersensor == other.bayersensor
        && xtranssensor == other.xtranssensor
        && df_autoselect == other.df_autoselect
        && (df_autoselect || (dark_frame == other.dark_frame))
        && ff_AutoSelect == other.ff_AutoSelect
        && (ff_AutoSelect || (ff_file == other.ff_file))
        && ff_BlurRadius == other.ff_BlurRadius
        && ff_BlurType == other.ff_BlurType
        && ff_AutoClipControl == other.ff_AutoClipControl
        && ff_clipControl == other.ff_clipControl
        && ff_embedded == other.ff_embedded
        && ca_autocorrect == other.ca_autocorrect
        && ca_avoidcolourshift == other.ca_avoidcolourshift
        && caautoiterations == other.caautoiterations
        && (ca_autocorrect || (cared == other.cared))
        && (ca_autocorrect || (cablue == other.cablue))
        && expos == other.expos
        && hotPixelFilter == other.hotPixelFilter
        && deadPixelFilter == other.deadPixelFilter
        && hotdeadpix_thresh == other.hotdeadpix_thresh
        && enable_darkframe == other.enable_darkframe
        && enable_flatfield == other.enable_flatfield
        && enable_ca == other.enable_ca
        && enable_hotdeadpix == other.enable_hotdeadpix
        && enable_whitepoint == other.enable_whitepoint;
}

bool RAWParams::operator !=(const RAWParams& other) const
{
    return !(*this == other);
}

const std::vector<const char*>& RAWParams::getFlatFieldBlurTypeStrings()
{
    static const std::vector<const char*> blur_type_strings {
        "Area Flatfield",
        "Vertical Flatfield",
        "Horizontal Flatfield",
        "V+H Flatfield"
    };
    return blur_type_strings;
}

Glib::ustring RAWParams::getFlatFieldBlurTypeString(FlatFieldBlurType type)
{
    return getFlatFieldBlurTypeStrings()[toUnderlying(type)];
}


FilmNegativeParams::FilmNegativeParams() :
    enabled(false),
    redRatio(1.36),
    greenExp(1.5),
    blueRatio(0.86),
    refInput({0.0, 0.0, 0.0}),
    refOutput({0.0, 0.0, 0.0}),
    colorSpace(ColorSpace::WORKING),
    backCompat(BackCompat::CURRENT)
{
}

bool FilmNegativeParams::RGB::operator ==(const FilmNegativeParams::RGB& other) const
{
    return
        r == other.r
        && g == other.g
        && b == other.b;
}

bool FilmNegativeParams::RGB::operator !=(const FilmNegativeParams::RGB& other) const
{
    return !(*this == other);
}

FilmNegativeParams::RGB FilmNegativeParams::RGB::operator *(const FilmNegativeParams::RGB& other) const
{
    return {
        (*this).r * other.r,
        (*this).g * other.g,
        (*this).b * other.b
    };
}

bool FilmNegativeParams::operator ==(const FilmNegativeParams& other) const
{
    return
        enabled == other.enabled
        && redRatio == other.redRatio
        && greenExp == other.greenExp
        && blueRatio == other.blueRatio
        && refInput == other.refInput
        && refOutput == other.refOutput
        && colorSpace == other.colorSpace
        && backCompat == other.backCompat;
}

bool FilmNegativeParams::operator !=(const FilmNegativeParams& other) const
{
    return !(*this == other);
}


namespace {

const std::map<Glib::ustring, Glib::ustring> exif_keys = {
    {"Copyright", "Exif.Image.Copyright"},
    {"Artist", "Exif.Image.Artist"},
    {"ImageDescription", "Exif.Image.ImageDescription"},
    {"Exif.UserComment", "Exif.Photo.UserComment"},
    {"ISOSpeed", "Exif.Photo.ISOSpeedRatings"},
    {"FNumber", "Exif.Photo.FNumber"},
    {"ShutterSpeed", "Exif.Photo.ExposureTime"},
    {"FocalLength", "Exif.Photo.FocalLength"},
    {"ExpComp", "Exif.Photo.ExposureBiasValue"},
    {"Flash", "Exif.Photo.Flash"},
    {"Make", "Exif.Image.Make"},
    {"Model", "Exif.Image.Model"},
    {"Lens", "Exif.Photo.LensModel"},
    {"DateTime", "Exif.Photo.DateTimeOriginal"},
    {"XResolution", "Exif.Image.XResolution"},
    {"YResolution", "Exif.Image.YResolution"}
};

const std::map<Glib::ustring, Glib::ustring> iptc_keys = {
    {"Title", "Iptc.Application2.ObjectName"},
    {"Category", "Iptc.Application2.Category"},
    {"SupplementalCategories", "Iptc.Application2.SuppCategory"},
    {"Keywords", "Iptc.Application2.Keywords"},
    {"Instructions", "Iptc.Application2.SpecialInstructions"},
    {"DateCreated", "Iptc.Application2.DateCreated"},
    {"Creator", "Iptc.Application2.Byline"},
    {"CreatorJobTitle", "Iptc.Application2.BylineTitle"},
    {"City", "Iptc.Application2.City"},
    {"Province", "Iptc.Application2.ProvinceState"},
    {"Country", "Iptc.Application2.CountryName"},
    {"TransReference", "Iptc.Application2.TransmissionReference"},
    {"Headline", "Iptc.Application2.Headline"},
    {"Credit", "Iptc.Application2.Credit"},
    {"Source", "Iptc.Application2.Source"},
    {"Copyright", "Iptc.Application2.Copyright"},
    {"Caption", "Iptc.Application2.Caption"},
    {"CaptionWriter", "Iptc.Application2.Writer"}
};

} // namespace


std::vector<std::string> MetaDataParams::basicExifKeys = {
    "Exif.Image.Copyright",
    "Exif.Image.Artist",
    "Exif.Image.ImageDescription",
    "Exif.Photo.UserComment",
    "Exif.Image.Make",
    "Exif.Image.Model",
    "Exif.Photo.LensModel",
    "Exif.Photo.FNumber",
    "Exif.Photo.ExposureTime",
    "Exif.Photo.FocalLength",
    "Exif.Photo.ISOSpeedRatings",
    "Exif.Photo.ExposureBiasValue",
    "Exif.Photo.Flash",
    "Exif.Photo.DateTimeOriginal",
    "Exif.Image.XResolution",
    "Exif.Image.YResolution"
};


MetaDataParams::MetaDataParams():
    mode(MetaDataParams::EDIT),
    exifKeys{},
    exif{},
    iptc{},
    notes("")
{
    exifKeys = basicExifKeys;
}


bool MetaDataParams::operator==(const MetaDataParams &other) const
{
    return mode == other.mode
        && exifKeys == other.exifKeys
        && exif == other.exif
        && iptc == other.iptc
        && notes == other.notes;
}

bool MetaDataParams::operator!=(const MetaDataParams &other) const
{
    return !(*this == other);
}


ProcParams::ProcParams()
{
    setDefaults();
}


void ProcParams::setDefaults()
{
    exposure = ExposureParams();
    saturation = SaturationParams();
    toneCurve = ToneCurveParams();
    labCurve = LabCurveParams();
    rgbCurves = RGBCurvesParams();
    localContrast = LocalContrastParams();
    sharpening = SharpeningParams();
    prsharpening = SharpeningParams();
    prsharpening.contrast = 25.0;
    prsharpening.method = "usm";
    wb = WBParams();
    defringe = DefringeParams();
    impulseDenoise = ImpulseDenoiseParams();
    denoise = DenoiseParams();
    textureBoost = TextureBoostParams();
    fattal = FattalToneMappingParams();
    logenc = LogEncodingParams();
    toneEqualizer = ToneEqualizerParams();
    crop = CropParams();
    coarse = CoarseTransformParams();
    commonTrans = CommonTransformParams();
    rotate = RotateParams();
    distortion = DistortionParams();
    lensProf = LensProfParams();
    perspective = PerspectiveParams();
    gradient = GradientParams();
    pcvignette = PCVignetteParams();
    vignetting = VignettingParams();
    chmixer = ChannelMixerParams();
    blackwhite = BlackWhiteParams();
    hsl = HSLEqualizerParams();
    cacorrection = CACorrParams();
    resize = ResizeParams();
    icm = ColorManagementParams();
    filmSimulation = FilmSimulationParams();
    softlight = SoftLightParams();
    dehaze = DehazeParams();
    grain = GrainParams();
    smoothing = SmoothingParams();
    colorcorrection = ColorCorrectionParams();
    raw = RAWParams();
    metadata = MetaDataParams();
    filmNegative = FilmNegativeParams();
    // exif.clear();
    // iptc.clear();

    spot = SpotParams();

    rank = -1;
    colorlabel = 0;
    inTrash = false;

    ppVersion = PPVERSION;
}


int ProcParams::save(ProgressListener *pl, const Glib::ustring& fname, const Glib::ustring& fname2, const ParamsEdited *pedited)
{
    if (fname.empty() && fname2.empty()) {
        return 0;
    }

    Glib::ustring sPParams;

    try {
        KeyFile keyFile;
        int ret = save(pl, keyFile, pedited, fname);
        if (ret != 0) {
            return ret;
        }

        sPParams = keyFile.to_data();
    } catch (Glib::KeyFileError &exc) {
        if (pl) {
            pl->error(Glib::ustring::compose(M("PROCPARAMS_SAVE_ERROR"), fname, exc.what()));
        }
    }

    if (sPParams.empty()) {
        return 1;
    }

    int error1, error2;
    error1 = write(pl, fname, sPParams);

    if (!fname2.empty()) {

        error2 = write(pl, fname2, sPParams);
        // If at least one file has been saved, it's a success
        return error1 & error2;
    } else {
        return error1;
    }
}


int ProcParams::saveEmbedded(ProgressListener *pl, const Glib::ustring &fname)
{
    if (fname.empty()) {
        return 0;
    }

    Glib::ustring sPParams;

    try {
        KeyFile keyFile;
        int ret = save(pl, keyFile, nullptr, fname);
        if (ret != 0) {
            return ret;
        }

        sPParams = keyFile.to_data();
    } catch (Glib::KeyFileError &exc) {
        if (pl) {
            pl->error(Glib::ustring::compose(M("PROCPARAMS_SAVE_ERROR"), fname, exc.what()));
        }
    }

    if (sPParams.empty()) {
        return 1;
    }

    try {
        // Exiv2Metadata md(fname, false);
        // md.load();
        // md.xmpData()["Xmp.ART.arp"] = to_xmp(sPParams);
        // md.saveToImage(pl, fname, true);
        Exiv2Metadata::embedProcParamsData(fname, to_xmp(sPParams));
        return 0;
    } catch (std::exception &exc) {
        if (pl) {
            pl->error(Glib::ustring::compose(M("PROCPARAMS_SAVE_ERROR"), fname, exc.what()));
        }
        return 1;
    }
}


namespace {

bool save_lut_params(KeyFile &keyFile, const Glib::ustring &group, const Glib::ustring &name, const CLUTParamValueMap &params)
{
    std::vector<std::string> val;
    for (auto &p : params) {
        std::string v;
        const char *sep = "";
        for (auto f : p.second) {
            v += sep + std::to_string(f);
            sep = ",";
        }
        val.push_back(p.first + "=" + v);
    }
    return saveToKeyfile(group, name, val, keyFile);
}


bool load_lut_params(const KeyFile &keyFile, const Glib::ustring &group, const Glib::ustring &name, CLUTParamValueMap &out)
{
    std::vector<std::string> val;
    if (assignFromKeyfile(keyFile, group, name, val)) {
        out.clear();
        for (auto &v : val) {
            auto p = v.find('=');
            if (p != std::string::npos) {
                auto key = v.substr(0, p);
                for (auto d : Glib::Regex::split_simple(",", Glib::ustring(v.c_str() + (p+1)))) {
                    double val = std::atof(d.c_str());
                    out[key].push_back(val);
                }
            }
        }
        return true;
    }
    return false;
}

} // namespace


int ProcParams::save(ProgressListener *pl, bool save_general,
                     KeyFile &keyFile, const ParamsEdited *pedited,
                     const Glib::ustring &fname) const
{
#define RELEVANT_(n) (!pedited || pedited->n)
    try {
        Glib::ustring basedir = Glib::path_get_dirname(fname);
        
// Version
        if (save_general) {
            keyFile.set_string("Version", "AppVersion", RTVERSION);
            keyFile.set_integer("Version", "Version", PPVERSION);

            if (RELEVANT_(general)) {
                if (rank >= 0) {
                    saveToKeyfile("General", "Rank", rank, keyFile);
                }
                saveToKeyfile("General", "ColorLabel", colorlabel, keyFile);
                saveToKeyfile("General", "InTrash", inTrash, keyFile);
            }
        }

// Exposure
        if (RELEVANT_(exposure)) {
            saveToKeyfile("Exposure", "Enabled", exposure.enabled, keyFile);
            saveToKeyfile("Exposure", "Compensation", exposure.expcomp, keyFile);
            saveToKeyfile("Exposure", "Black", exposure.black, keyFile);
            Glib::ustring hr = "Off";
            switch (exposure.hrmode) {
            case ExposureParams::HR_OFF: hr = "Off"; break;
            case ExposureParams::HR_BLEND: hr = "Blend"; break;
            case ExposureParams::HR_COLOR: hr = "Color"; break;
            case ExposureParams::HR_COLORSOFT: hr = "Balanced"; break;
            }
            saveToKeyfile("Exposure", "HLRecovery", hr, keyFile);
            saveToKeyfile("Exposure", "HLRecoveryBlur", exposure.hrblur, keyFile);
        }

// Brightness, Contrast, Saturation
        if (RELEVANT_(saturation)) {
            saveToKeyfile("Saturation", "Enabled", saturation.enabled, keyFile);
            saveToKeyfile("Saturation", "Saturation", saturation.saturation, keyFile);
            saveToKeyfile("Saturation", "Vibrance", saturation.vibrance, keyFile);
        }

// Tone curve
        if (RELEVANT_(toneCurve)) {
            saveToKeyfile("ToneCurve", "Enabled", toneCurve.enabled, keyFile);
            saveToKeyfile("ToneCurve", "Contrast", toneCurve.contrast, keyFile);
            saveToKeyfile("ToneCurve", "HistogramMatching", toneCurve.histmatching, keyFile);
            saveToKeyfile("ToneCurve", "CurveFromHistogramMatching", toneCurve.fromHistMatching, keyFile);

            const std::map<ToneCurveParams::TcMode, const char*> tc_mapping = {
                {ToneCurveParams::TcMode::STD, "Standard"},
                {ToneCurveParams::TcMode::FILMLIKE, "FilmLike"},
                {ToneCurveParams::TcMode::SATANDVALBLENDING, "SatAndValueBlending"},
                {ToneCurveParams::TcMode::WEIGHTEDSTD, "WeightedStd"},
                {ToneCurveParams::TcMode::LUMINANCE, "Luminance"},
                {ToneCurveParams::TcMode::PERCEPTUAL, "Perceptual"},
                {ToneCurveParams::TcMode::NEUTRAL, "Neutral"}
            };
            const std::map<ToneCurveParams::BcMode, const char*> bc_mapping = {
                {ToneCurveParams::BcMode::LINEAR, "Linear"},
                {ToneCurveParams::BcMode::ROLLOFF, "Rolloff"},
                {ToneCurveParams::BcMode::SCURVE, "SCurve"}
            };

            saveToKeyfile("ToneCurve", "CurveMode", tc_mapping, toneCurve.curveMode, keyFile);
            if (!toneCurve.curve2.empty() && toneCurve.curve2[0] != DCT_Linear && toneCurve.curveMode != toneCurve.curveMode2) {
                saveToKeyfile("ToneCurve", "CurveMode2", tc_mapping, toneCurve.curveMode2, keyFile);
            }

            saveToKeyfile("ToneCurve", "Curve", toneCurve.curve, keyFile);
            saveToKeyfile("ToneCurve", "Curve2", toneCurve.curve2, keyFile);
            saveToKeyfile("ToneCurve", "Saturation", toneCurve.saturation, keyFile);
            saveToKeyfile("ToneCurve", "Saturation2", toneCurve.saturation2, keyFile);
            if (toneCurve.perceptualStrength != 100) {
                saveToKeyfile("ToneCurve", "PerceptualStrength", toneCurve.perceptualStrength, keyFile);
            }
            if (toneCurve.contrastLegacyMode) {
                saveToKeyfile("ToneCurve", "ContrastLegacyMode", toneCurve.contrastLegacyMode, keyFile);
            }
            saveToKeyfile("ToneCurve", "WhitePoint", toneCurve.whitePoint, keyFile);
            saveToKeyfile("ToneCurve", "BaseCurve", bc_mapping, toneCurve.basecurve, keyFile);
        }

// Local contrast
        if (RELEVANT_(localContrast)) {
            saveToKeyfile("Local Contrast", "Enabled", localContrast.enabled, keyFile);
            for (size_t j = 0; j < localContrast.regions.size(); ++j) {
                std::string n = j ? std::string("_") + std::to_string(j) : std::string("");
                auto &r = localContrast.regions[j];
                putToKeyfile("Local Contrast", Glib::ustring("Contrast") + n, r.contrast, keyFile);
                putToKeyfile("Local Contrast", Glib::ustring("Curve") + n, r.curve, keyFile);
                localContrast.masks[j].save(keyFile, basedir, "Local Contrast", "", n);
            }
            saveToKeyfile("Local Contrast", "ShowMask", localContrast.showMask, keyFile);
            saveToKeyfile("Local Contrast", "SelectedRegion", localContrast.selectedRegion, keyFile);
        }


// Channel mixer
        if (RELEVANT_(chmixer)) {
            saveToKeyfile("Channel Mixer", "Enabled", chmixer.enabled, keyFile);
            saveToKeyfile("Channel Mixer", "Mode", int(chmixer.mode), keyFile);
            Glib::ArrayHandle<int> rmix(chmixer.red, 3, Glib::OWNERSHIP_NONE);
            keyFile.set_integer_list("Channel Mixer", "Red", rmix);
            Glib::ArrayHandle<int> gmix(chmixer.green, 3, Glib::OWNERSHIP_NONE);
            keyFile.set_integer_list("Channel Mixer", "Green", gmix);
            Glib::ArrayHandle<int> bmix(chmixer.blue, 3, Glib::OWNERSHIP_NONE);
            keyFile.set_integer_list("Channel Mixer", "Blue", bmix);
            Glib::ArrayHandle<int> h(chmixer.hue_tweak, 3, Glib::OWNERSHIP_NONE);
            keyFile.set_integer_list("Channel Mixer", "HueTweak", h);
            Glib::ArrayHandle<int> s(chmixer.sat_tweak, 3, Glib::OWNERSHIP_NONE);
            keyFile.set_integer_list("Channel Mixer", "SatTweak", s);
        }

// Black & White
        if (RELEVANT_(blackwhite)) {
            saveToKeyfile("Black & White", "Enabled", blackwhite.enabled, keyFile);
            saveToKeyfile("Black & White", "Setting", blackwhite.setting, keyFile);
            saveToKeyfile("Black & White", "Filter", blackwhite.filter, keyFile);
            saveToKeyfile("Black & White", "MixerRed", blackwhite.mixerRed, keyFile);
            saveToKeyfile("Black & White", "MixerGreen", blackwhite.mixerGreen, keyFile);
            saveToKeyfile("Black & White", "MixerBlue", blackwhite.mixerBlue, keyFile);
            saveToKeyfile("Black & White", "GammaRed", blackwhite.gammaRed, keyFile);
            saveToKeyfile("Black & White", "GammaGreen", blackwhite.gammaGreen, keyFile);
            saveToKeyfile("Black & White", "GammaBlue", blackwhite.gammaBlue, keyFile);
            saveToKeyfile("Black & White", "ColorCast", blackwhite.colorCast.toVector(), keyFile);
        }

// HSL equalizer
        if (RELEVANT_(hsl)) {
            saveToKeyfile("HSL Equalizer", "Enabled", hsl.enabled, keyFile);
            saveToKeyfile("HSL Equalizer", "HCurve", hsl.hCurve, keyFile);
            saveToKeyfile("HSL Equalizer", "SCurve", hsl.sCurve, keyFile);
            saveToKeyfile("HSL Equalizer", "LCurve", hsl.lCurve, keyFile);
            saveToKeyfile("HSL Equalizer", "Smoothing", hsl.smoothing, keyFile);
        }

// Luma curve
        if (RELEVANT_(labCurve)) {
            saveToKeyfile("Luminance Curve", "Enabled", labCurve.enabled, keyFile);
            saveToKeyfile("Luminance Curve", "Brightness", labCurve.brightness, keyFile);
            saveToKeyfile("Luminance Curve", "Contrast", labCurve.contrast, keyFile);
            saveToKeyfile("Luminance Curve", "Chromaticity", labCurve.chromaticity, keyFile);
            saveToKeyfile("Luminance Curve", "LCurve", labCurve.lcurve, keyFile);
            saveToKeyfile("Luminance Curve", "aCurve", labCurve.acurve, keyFile);
            saveToKeyfile("Luminance Curve", "bCurve", labCurve.bcurve, keyFile);
        }

// Sharpening
        if (RELEVANT_(sharpening)) {
            saveToKeyfile("Sharpening", "Enabled", sharpening.enabled, keyFile);
            saveToKeyfile("Sharpening", "Contrast", sharpening.contrast, keyFile);
            saveToKeyfile("Sharpening", "Method", sharpening.method, keyFile);
            saveToKeyfile("Sharpening", "Radius", sharpening.radius, keyFile);
            saveToKeyfile("Sharpening", "Amount", sharpening.amount, keyFile);
            saveToKeyfile("Sharpening", "Threshold", sharpening.threshold.toVector(), keyFile);
            saveToKeyfile("Sharpening", "OnlyEdges", sharpening.edgesonly, keyFile);
            saveToKeyfile("Sharpening", "EdgedetectionRadius", sharpening.edges_radius, keyFile);
            saveToKeyfile("Sharpening", "EdgeTolerance", sharpening.edges_tolerance, keyFile);
            saveToKeyfile("Sharpening", "HalocontrolEnabled", sharpening.halocontrol, keyFile);
            saveToKeyfile("Sharpening", "HalocontrolAmount", sharpening.halocontrol_amount, keyFile);
            saveToKeyfile("Sharpening", "DeconvRadius", sharpening.deconvradius, keyFile);
            saveToKeyfile("Sharpening", "DeconvAmount", sharpening.deconvamount, keyFile);
            saveToKeyfile("Sharpening", "DeconvAutoRadius", sharpening.deconvAutoRadius, keyFile);
            saveToKeyfile("Sharpening", "DeconvCornerBoost", sharpening.deconvCornerBoost, keyFile);
            saveToKeyfile("Sharpening", "DeconvCornerLatitude", sharpening.deconvCornerLatitude, keyFile);
            saveToKeyfile("Sharpening", "PSFKernel", sharpening.psf_kernel, keyFile);
            saveToKeyfile("Sharpening", "PSFIterations", sharpening.psf_iterations, keyFile);
        }

// WB
        if (RELEVANT_(wb)) {
            saveToKeyfile("White Balance", "Enabled", wb.enabled, keyFile);
            std::string method = "Camera";
            switch (wb.method) {
            case WBParams::CAMERA:
                method = "Camera";
                break;
            case WBParams::AUTO:
                method = "Auto";
                break;
            case WBParams::CUSTOM_TEMP:
                method = "CustomTemp";
                break;
            case WBParams::CUSTOM_MULT:
                method = "CustomMult";
                break;
            case WBParams::CUSTOM_MULT_LEGACY:
                method = "CustomMultLegacy";
            default:
                break;
            }
            saveToKeyfile("White Balance", "Setting", method, keyFile);
            saveToKeyfile("White Balance", "Temperature", wb.temperature, keyFile);
            saveToKeyfile("White Balance", "Green", wb.green, keyFile);
            if (wb.equal != 1) {
                saveToKeyfile("White Balance", "Equal", wb.equal, keyFile);
            }
            std::vector<double> m(wb.mult.begin(), wb.mult.end());
            saveToKeyfile("White Balance", "Multipliers", m, keyFile);
        }


// Impulse denoise
        if (RELEVANT_(impulseDenoise)) {
            saveToKeyfile("Impulse Denoising", "Enabled", impulseDenoise.enabled, keyFile);
            saveToKeyfile("Impulse Denoising", "Threshold", impulseDenoise.thresh, keyFile);
        }

// Defringe
        if (RELEVANT_(defringe)) {
            saveToKeyfile("Defringing", "Enabled", defringe.enabled, keyFile);
            saveToKeyfile("Defringing", "Radius", defringe.radius, keyFile);
            saveToKeyfile("Defringing", "Threshold", defringe.threshold, keyFile);
            saveToKeyfile("Defringing", "HueCurve", defringe.huecurve, keyFile);
        }

// Dehaze
        if (RELEVANT_(dehaze)) {
            saveToKeyfile("Dehaze", "Enabled", dehaze.enabled, keyFile);
            saveToKeyfile("Dehaze", "Strength", dehaze.strength, keyFile);        
            saveToKeyfile("Dehaze", "Blackpoint", dehaze.blackpoint, keyFile);
            saveToKeyfile("Dehaze", "Luminance", dehaze.luminance, keyFile);
            DehazeParams dp;
            if (dehaze.depth != dp.depth) {
                saveToKeyfile("Dehaze", "Depth", dehaze.depth, keyFile);
            }
            if (dehaze.showDepthMap != dp.showDepthMap) {
                saveToKeyfile("Dehaze", "ShowDepthMap", dehaze.showDepthMap, keyFile);        
            }
        }

// Denoising
        if (RELEVANT_(denoise)) {
            saveToKeyfile("Denoise", "Enabled", denoise.enabled, keyFile);
            saveToKeyfile("Denoise", "ColorSpace", denoise.colorSpace == DenoiseParams::ColorSpace::LAB ? Glib::ustring("LAB") : Glib::ustring("RGB"), keyFile);
            saveToKeyfile("Denoise", "Aggressive", denoise.aggressive, keyFile);
            saveToKeyfile("Denoise", "Gamma", denoise.gamma, keyFile);
            saveToKeyfile("Denoise", "Luminance", denoise.luminance, keyFile);
            saveToKeyfile("Denoise", "LuminanceDetail", denoise.luminanceDetail, keyFile);
            saveToKeyfile("Denoise", "LuminanceDetailThreshold", denoise.luminanceDetailThreshold, keyFile);
            saveToKeyfile("Denoise", "ChrominanceMethod", int(denoise.chrominanceMethod), keyFile);
            saveToKeyfile("Denoise", "ChrominanceAutoFactor", denoise.chrominanceAutoFactor, keyFile);
            saveToKeyfile("Denoise", "Chrominance", denoise.chrominance, keyFile);
            saveToKeyfile("Denoise", "ChrominanceRedGreen", denoise.chrominanceRedGreen, keyFile);
            saveToKeyfile("Denoise", "ChrominanceBlueYellow", denoise.chrominanceBlueYellow, keyFile);
            saveToKeyfile("Denoise", "SmoothingEnabled", denoise.smoothingEnabled, keyFile);
            saveToKeyfile("Denoise", "GuidedChromaRadius", denoise.guidedChromaRadius, keyFile);
            saveToKeyfile("Denoise", "NLDetail", denoise.nlDetail, keyFile);
            saveToKeyfile("Denoise", "NLStrength", denoise.nlStrength, keyFile);
        }

// TextureBoost
        if (RELEVANT_(textureBoost)) {
            saveToKeyfile("TextureBoost", "Enabled", textureBoost.enabled, keyFile);
            for (size_t j = 0; j < textureBoost.regions.size(); ++j) {
                std::string n = j ? std::string("_") + std::to_string(j) : std::string("");
                auto &r = textureBoost.regions[j];
                putToKeyfile("TextureBoost", Glib::ustring("Strength") + n, r.strength, keyFile);
                putToKeyfile("TextureBoost", Glib::ustring("DetailThreshold") + n, r.detailThreshold, keyFile);
                putToKeyfile("TextureBoost", Glib::ustring("Iterations") + n, r.iterations, keyFile);
                textureBoost.masks[j].save(keyFile, basedir, "TextureBoost", "", n);
            }
            saveToKeyfile("TextureBoost", "ShowMask", textureBoost.showMask, keyFile);
            saveToKeyfile("TextureBoost", "SelectedRegion", textureBoost.selectedRegion, keyFile);
        }

// Fattal
        if (RELEVANT_(fattal)) {
            saveToKeyfile("FattalToneMapping", "Enabled", fattal.enabled, keyFile);
            saveToKeyfile("FattalToneMapping", "Threshold", fattal.threshold, keyFile);
            saveToKeyfile("FattalToneMapping", "Amount", fattal.amount, keyFile);
            saveToKeyfile("FattalToneMapping", "SaturationControl", fattal.satcontrol, keyFile);
        }

// Log encoding
        if (RELEVANT_(logenc)) {
            saveToKeyfile("LogEncoding", "Enabled", logenc.enabled, keyFile);
            saveToKeyfile("LogEncoding", "Auto", logenc.autocompute, keyFile);
            saveToKeyfile("LogEncoding", "AutoGain", logenc.autogain, keyFile);
            saveToKeyfile("LogEncoding", "Gain", logenc.gain, keyFile);
            saveToKeyfile("LogEncoding", "TargetGray", logenc.targetGray, keyFile);
            saveToKeyfile("LogEncoding", "BlackEv", logenc.blackEv, keyFile);
            saveToKeyfile("LogEncoding", "WhiteEv", logenc.whiteEv, keyFile);
            saveToKeyfile("LogEncoding", "Regularization", logenc.regularization, keyFile);
            saveToKeyfile("LogEncoding", "SaturationControl", logenc.satcontrol, keyFile);
            saveToKeyfile("LogEncoding", "HighlightCompression", logenc.highlightCompression, keyFile);
        }

// ToneEqualizer
        if (RELEVANT_(toneEqualizer)) {
            saveToKeyfile("ToneEqualizer", "Enabled", toneEqualizer.enabled, keyFile);
            for (size_t i = 0; i < toneEqualizer.bands.size(); ++i) {
                saveToKeyfile("ToneEqualizer", "Band" + std::to_string(i), toneEqualizer.bands[i], keyFile);
            }
            saveToKeyfile("ToneEqualizer", "Regularization", toneEqualizer.regularization, keyFile);
            saveToKeyfile("ToneEqualizer", "Pivot", toneEqualizer.pivot, keyFile);
        }
        
// Crop
        if (RELEVANT_(crop)) {
            saveToKeyfile("Crop", "Enabled", crop.enabled, keyFile);
            saveToKeyfile("Crop", "X", crop.x, keyFile);
            saveToKeyfile("Crop", "Y", crop.y, keyFile);
            saveToKeyfile("Crop", "W", crop.w, keyFile);
            saveToKeyfile("Crop", "H", crop.h, keyFile);
            saveToKeyfile("Crop", "FixedRatio", crop.fixratio, keyFile);
            saveToKeyfile("Crop", "Ratio", crop.ratio, keyFile);
            saveToKeyfile("Crop", "Orientation", crop.orientation, keyFile);
            saveToKeyfile("Crop", "Guide", crop.guide, keyFile);
        }

// Coarse transformation
        if (RELEVANT_(coarse)) {
            saveToKeyfile("Coarse Transformation", "Rotate", coarse.rotate, keyFile);
            saveToKeyfile("Coarse Transformation", "HorizontalFlip", coarse.hflip, keyFile);
            saveToKeyfile("Coarse Transformation", "VerticalFlip", coarse.vflip, keyFile);
        }

// Common properties for transformations
        if (RELEVANT_(commonTrans)) {
            saveToKeyfile("Common Properties for Transformations", "AutoFill", commonTrans.autofill, keyFile);
        }

// Rotation
        if (RELEVANT_(rotate)) {
            saveToKeyfile("Rotation", "Enabled", rotate.enabled, keyFile);
            saveToKeyfile("Rotation", "Degree", rotate.degree, keyFile);
        }

// Distortion
        if (RELEVANT_(distortion)) {
            saveToKeyfile("Distortion", "Enabled", distortion.enabled, keyFile);
            saveToKeyfile("Distortion", "Amount", distortion.amount, keyFile);
            saveToKeyfile("Distortion", "Auto", distortion.autocompute, keyFile);
        }

// Lens profile
        if (RELEVANT_(lensProf)) {
            saveToKeyfile("LensProfile", "LcMode", lensProf.getMethodString(lensProf.lcMode), keyFile);
            saveToKeyfile("LensProfile", "LCPFile", filenameToUri(lensProf.lcpFile, basedir), keyFile);
            saveToKeyfile("LensProfile", "UseDistortion", lensProf.useDist, keyFile);
            saveToKeyfile("LensProfile", "UseVignette", lensProf.useVign, keyFile);
            saveToKeyfile("LensProfile", "UseCA", lensProf.useCA, keyFile);
            saveToKeyfile("LensProfile", "LFCameraMake", lensProf.lfCameraMake, keyFile);
            saveToKeyfile("LensProfile", "LFCameraModel", lensProf.lfCameraModel, keyFile);
            saveToKeyfile("LensProfile", "LFLens", lensProf.lfLens, keyFile);
        }

// Perspective correction
        if (RELEVANT_(perspective)) {
            saveToKeyfile("Perspective", "Enabled", perspective.enabled, keyFile);
            saveToKeyfile("Perspective", "Horizontal", perspective.horizontal, keyFile);
            saveToKeyfile("Perspective", "Vertical", perspective.vertical, keyFile);
            saveToKeyfile("Perspective", "Angle", perspective.angle, keyFile);
            saveToKeyfile("Perspective", "Shear", perspective.shear, keyFile);
            saveToKeyfile("Perspective", "FocalLength", perspective.flength, keyFile);
            saveToKeyfile("Perspective", "CropFactor", perspective.cropfactor, keyFile);
            saveToKeyfile("Perspective", "Aspect", perspective.aspect, keyFile);
            saveToKeyfile("Perspective", "ControlLines", perspective.control_lines, keyFile);
        }

// Gradient
        if (RELEVANT_(gradient)) {
            saveToKeyfile("Gradient", "Enabled", gradient.enabled, keyFile);
            saveToKeyfile("Gradient", "Degree", gradient.degree, keyFile);
            saveToKeyfile("Gradient", "Feather", gradient.feather, keyFile);
            saveToKeyfile("Gradient", "Strength", gradient.strength, keyFile);
            saveToKeyfile("Gradient", "CenterX", gradient.centerX, keyFile);
            saveToKeyfile("Gradient", "CenterY", gradient.centerY, keyFile);
        }

// Post-crop vignette
        if (RELEVANT_(pcvignette)) {
            saveToKeyfile("PCVignette", "Enabled", pcvignette.enabled, keyFile);
            saveToKeyfile("PCVignette", "Strength", pcvignette.strength, keyFile);
            saveToKeyfile("PCVignette", "Feather", pcvignette.feather, keyFile);
            saveToKeyfile("PCVignette", "Roundness", pcvignette.roundness, keyFile);
            saveToKeyfile("PCVignette", "CenterX", pcvignette.centerX, keyFile);
            saveToKeyfile("PCVignette", "CenterY", pcvignette.centerY, keyFile);
        }

// C/A correction
        if (RELEVANT_(cacorrection)) {
            saveToKeyfile("CACorrection", "Enabled", cacorrection.enabled, keyFile);
            saveToKeyfile("CACorrection", "Red", cacorrection.red, keyFile);
            saveToKeyfile("CACorrection", "Blue", cacorrection.blue, keyFile);
        }

// Vignetting correction
        if (RELEVANT_(vignetting)) {
            saveToKeyfile("Vignetting Correction", "Enabled", vignetting.enabled, keyFile);
            saveToKeyfile("Vignetting Correction", "Amount", vignetting.amount, keyFile);
            saveToKeyfile("Vignetting Correction", "Radius", vignetting.radius, keyFile);
            saveToKeyfile("Vignetting Correction", "Strength", vignetting.strength, keyFile);
            saveToKeyfile("Vignetting Correction", "CenterX", vignetting.centerX, keyFile);
            saveToKeyfile("Vignetting Correction", "CenterY", vignetting.centerY, keyFile);
        }

// Resize
        if (RELEVANT_(resize)) {
            saveToKeyfile("Resize", "Enabled", resize.enabled, keyFile);
            saveToKeyfile("Resize", "Scale", resize.scale, keyFile);
            saveToKeyfile("Resize", "AppliesTo", resize.appliesTo, keyFile);
            saveToKeyfile("Resize", "DataSpecified", resize.dataspec, keyFile);
            saveToKeyfile("Resize", "Width", resize.width, keyFile);
            saveToKeyfile("Resize", "Height", resize.height, keyFile);
            saveToKeyfile("Resize", "AllowUpscaling", resize.allowUpscaling, keyFile);
            saveToKeyfile("Resize", "PPI", resize.ppi, keyFile);
            const char *u = "px";
            switch (resize.unit) {
            case ResizeParams::CM: u = "cm"; break;
            case ResizeParams::INCHES: u = "in"; break;
            default: u = "px"; break;
            }
            saveToKeyfile("Resize", "Unit", Glib::ustring(u), keyFile);
        }

// Post resize sharpening
        if (RELEVANT_(prsharpening)) {
            saveToKeyfile("OutputSharpening", "Enabled", prsharpening.enabled, keyFile);
            saveToKeyfile("OutputSharpening", "Contrast", prsharpening.contrast, keyFile);
            saveToKeyfile("OutputSharpening", "Method", prsharpening.method, keyFile);
            saveToKeyfile("OutputSharpening", "Radius", prsharpening.radius, keyFile);
            saveToKeyfile("OutputSharpening", "Amount", prsharpening.amount, keyFile);
            saveToKeyfile("OutputSharpening", "Threshold", prsharpening.threshold.toVector(), keyFile);
            saveToKeyfile("OutputSharpening", "OnlyEdges", prsharpening.edgesonly, keyFile);
            saveToKeyfile("OutputSharpening", "EdgedetectionRadius", prsharpening.edges_radius, keyFile);
            saveToKeyfile("OutputSharpening", "EdgeTolerance", prsharpening.edges_tolerance, keyFile);
            saveToKeyfile("OutputSharpening", "HalocontrolEnabled", prsharpening.halocontrol, keyFile);
            saveToKeyfile("OutputSharpening", "HalocontrolAmount", prsharpening.halocontrol_amount, keyFile);
            saveToKeyfile("OutputSharpening", "DeconvRadius", prsharpening.deconvradius, keyFile);
            saveToKeyfile("OutputSharpening", "DeconvAmount", prsharpening.deconvamount, keyFile);
        }

// Color management
        if (RELEVANT_(icm)) {
            if (icm.inputProfile.substr(0, 5) == "file:") {
                saveToKeyfile("Color Management", "InputProfile", filenameToUri(icm.inputProfile.substr(5), basedir), keyFile);
            } else {
                saveToKeyfile("Color Management", "InputProfile", icm.inputProfile, keyFile);
            }
            saveToKeyfile("Color Management", "ToneCurve", icm.toneCurve, keyFile);
            saveToKeyfile("Color Management", "ApplyLookTable", icm.applyLookTable, keyFile);
            saveToKeyfile("Color Management", "ApplyBaselineExposureOffset", icm.applyBaselineExposureOffset, keyFile);
            saveToKeyfile("Color Management", "ApplyHueSatMap", icm.applyHueSatMap, keyFile);
            saveToKeyfile("Color Management", "DCPIlluminant", icm.dcpIlluminant, keyFile);
            saveToKeyfile("Color Management", "DCPLookEarly", icm.dcp_look_early, keyFile);
            saveToKeyfile("Color Management", "WorkingProfile", icm.workingProfile, keyFile);
            saveToKeyfile("Color Management", "OutputProfile", icm.outputProfile, keyFile);
            saveToKeyfile(
                "Color Management",
                "OutputProfileIntent",
                {
                    {RI_PERCEPTUAL, "Perceptual"},
                    {RI_RELATIVE, "Relative"},
                    {RI_SATURATION, "Saturation"},
                    {RI_ABSOLUTE, "Absolute"}

                },
                icm.outputIntent,
                keyFile
                );
            saveToKeyfile("Color Management", "OutputBPC", icm.outputBPC, keyFile);
            saveToKeyfile("Color Management", "InputProfileCAT", icm.inputProfileCAT, keyFile);
        }


// Soft Light
        if (RELEVANT_(softlight)) {
            saveToKeyfile("SoftLight", "Enabled", softlight.enabled, keyFile);
            saveToKeyfile("SoftLight", "Strength", softlight.strength, keyFile);
        }

// Film simulation
        if (RELEVANT_(filmSimulation)) {
            saveToKeyfile("Film Simulation", "Enabled", filmSimulation.enabled, keyFile);
            auto filename = filenameToUri(filmSimulation.clutFilename, basedir);
            saveToKeyfile("Film Simulation", "ClutFilename", filename, keyFile);
            saveToKeyfile("Film Simulation", "Strength", filmSimulation.strength, keyFile);
            if (filmSimulation.after_tone_curve) {
                saveToKeyfile("Film Simulation", "AfterToneCurve", filmSimulation.after_tone_curve, keyFile);
            }
            //saveToKeyfile("Film Simulation", "ClutParams", filmSimulation.lut_params, keyFile);
            save_lut_params(keyFile, "Film Simulation", "ClutParams", filmSimulation.lut_params);
        }

// RGB curves        
        if (RELEVANT_(rgbCurves)) {
            saveToKeyfile("RGB Curves", "Enabled", rgbCurves.enabled, keyFile);
            saveToKeyfile("RGB Curves", "rCurve", rgbCurves.rcurve, keyFile);
            saveToKeyfile("RGB Curves", "gCurve", rgbCurves.gcurve, keyFile);
            saveToKeyfile("RGB Curves", "bCurve", rgbCurves.bcurve, keyFile);
        }

// Grain
        if (RELEVANT_(grain)) {
            saveToKeyfile("Grain", "Enabled", grain.enabled, keyFile);
            saveToKeyfile("Grain", "ISO", grain.iso, keyFile);
            saveToKeyfile("Grain", "Strength", grain.strength, keyFile);
            saveToKeyfile("Grain", "Color", grain.color, keyFile);
        }


// Smoothing
        if (RELEVANT_(smoothing)) {
            saveToKeyfile("Smoothing", "Enabled", smoothing.enabled, keyFile);
            for (size_t j = 0; j < smoothing.regions.size(); ++j) {
                std::string n = std::to_string(j+1);
                auto &r = smoothing.regions[j];
                putToKeyfile("Smoothing", Glib::ustring("Mode_") + n, int(r.mode), keyFile);
                putToKeyfile("Smoothing", Glib::ustring("Channel_") + n, int(r.channel), keyFile);
                putToKeyfile("Smoothing", Glib::ustring("Radius_") + n, r.radius, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("Sigma_") + n, r.sigma, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("Epsilon_") + n, r.epsilon, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("Iterations_") + n, r.iterations, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("Falloff_") + n, r.falloff, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("NLStrength_") + n, r.nlstrength, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("NLDetail_") + n, r.nldetail, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("NumBlades_") + n, r.numblades, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("Angle_") + n, r.angle, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("Curvature_") + n, r.curvature, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("Offset_") + n, r.offset, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("NoiseStrength_") + n, r.noise_strength, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("NoiseCoarseness_") + n, r.noise_coarseness, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("HalationSize_") + n, r.halation_size, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("HalationColor_") + n, r.halation_color, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("WavStrength_") + n, r.wav_strength, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("WavLevels_") + n, r.wav_levels, keyFile);
                putToKeyfile("Smoothing", Glib::ustring("WavGamma_") + n, r.wav_gamma, keyFile);
                smoothing.masks[j].save(keyFile, basedir, "Smoothing", "", Glib::ustring("_") + n);
            }
            saveToKeyfile("Smoothing", "ShowMask", smoothing.showMask, keyFile);
            saveToKeyfile("Smoothing", "SelectedRegion", smoothing.selectedRegion, keyFile);
        }

// ColorCorrection
        if (RELEVANT_(colorcorrection)) {
            saveToKeyfile("ColorCorrection", "Enabled", colorcorrection.enabled, keyFile);
            for (size_t j = 0; j < colorcorrection.regions.size(); ++j) {
                std::string n = std::to_string(j+1);
                auto &l = colorcorrection.regions[j];
                Glib::ustring mode = "YUV";
                switch (l.mode) {
                case ColorCorrectionParams::Mode::RGB:
                    mode = "RGB";
                    break;
                case ColorCorrectionParams::Mode::JZAZBZ:
                    mode = "Jzazbz";
                    break;
                case ColorCorrectionParams::Mode::HSL:
                    mode = "HSL";
                    break;
                case ColorCorrectionParams::Mode::LUT:
                    mode = "LUT";
                    break;
                default:
                    mode = "YUV";
                    break;
                }
                putToKeyfile("ColorCorrection", Glib::ustring("Mode_") + n, mode, keyFile);
                {
                    const char *chan[3] = { "Slope", "Offset", "Power" };
                    for (int c = 0; c < 3; ++c) {
                        Glib::ustring w = chan[c];
                        putToKeyfile("ColorCorrection", w + "H" + "_" + n, l.hue[c], keyFile);
                        putToKeyfile("ColorCorrection", w + "S" + "_" + n, l.sat[c], keyFile);
                        putToKeyfile("ColorCorrection", w + "L" + "_" + n, l.factor[c], keyFile);
                    }
                }
                {
                    const char *chan[3] = { "R", "G", "B" };
                    for (int c = 0; c < 3; ++c) {
                        putToKeyfile("ColorCorrection", Glib::ustring("Slope") + chan[c] + "_" + n, l.slope[c], keyFile);
                        putToKeyfile("ColorCorrection", Glib::ustring("Offset") + chan[c] + "_" + n, l.offset[c], keyFile);
                        putToKeyfile("ColorCorrection", Glib::ustring("Power") + chan[c] + "_" + n, l.power[c], keyFile);
                        putToKeyfile("ColorCorrection", Glib::ustring("Pivot") + chan[c] + "_" + n, l.pivot[c], keyFile);
                        putToKeyfile("ColorCorrection", Glib::ustring("Compression") + chan[c] + "_" + n, l.compression[c], keyFile);
                    }
                }
                {
                    putToKeyfile("ColorCorrection", Glib::ustring("A_") + n, l.a, keyFile);
                    putToKeyfile("ColorCorrection", Glib::ustring("B_") + n, l.b, keyFile);
                    putToKeyfile("ColorCorrection", Glib::ustring("ABScale_") + n, l.abscale, keyFile);
                    putToKeyfile("ColorCorrection", Glib::ustring("InSaturation_") + n, l.inSaturation, keyFile);
                    putToKeyfile("ColorCorrection", Glib::ustring("OutSaturation_") + n, l.outSaturation, keyFile);
                    putToKeyfile("ColorCorrection", Glib::ustring("Slope_") + n, l.slope[0], keyFile);
                    putToKeyfile("ColorCorrection", Glib::ustring("Offset_") + n, l.offset[0], keyFile);
                    putToKeyfile("ColorCorrection", Glib::ustring("Power_") + n, l.power[0], keyFile);
                    putToKeyfile("ColorCorrection", Glib::ustring("Pivot_") + n, l.pivot[0], keyFile);
                    putToKeyfile("ColorCorrection", Glib::ustring("Compression_") + n, l.compression[0], keyFile);
                    putToKeyfile("ColorCorrection", Glib::ustring("RGBLuminance_") + n, l.rgbluminance, keyFile);
                    putToKeyfile("ColorCorrection", Glib::ustring("HueShift_") + n, l.hueshift, keyFile);
                    putToKeyfile("ColorCorrection", Glib::ustring("LUTFilename_") + n, filenameToUri(l.lutFilename, basedir), keyFile);
                    save_lut_params(keyFile, "ColorCorrection", Glib::ustring("LUTParams_") + n, l.lut_params);
                    putToKeyfile("ColorCorrection", Glib::ustring("HSLGamma_") + n, l.hsl_gamma, keyFile);
                }
                colorcorrection.masks[j].save(keyFile, basedir, "ColorCorrection", "", Glib::ustring("_") + n);
            }
            saveToKeyfile("ColorCorrection", "ShowMask", colorcorrection.showMask, keyFile);
            saveToKeyfile("ColorCorrection", "SelectedRegion", colorcorrection.selectedRegion, keyFile);
        }
        
// Raw
        if (RELEVANT_(darkframe)) {
            saveToKeyfile("RAW", "DarkFrameEnabled", raw.enable_darkframe, keyFile);
            saveToKeyfile("RAW", "DarkFrame", filenameToUri(raw.dark_frame, basedir), keyFile);
            saveToKeyfile("RAW", "DarkFrameAuto", raw.df_autoselect, keyFile);
        }
        if (RELEVANT_(flatfield)) {
            saveToKeyfile("RAW", "FlatFieldEnabled", raw.enable_flatfield, keyFile);
            saveToKeyfile("RAW", "FlatFieldFile", filenameToUri(raw.ff_file, basedir), keyFile);
            saveToKeyfile("RAW", "FlatFieldAutoSelect", raw.ff_AutoSelect, keyFile);
            saveToKeyfile("RAW", "FlatFieldBlurRadius", raw.ff_BlurRadius, keyFile);
            saveToKeyfile("RAW", "FlatFieldBlurType", raw.ff_BlurType, keyFile);
            saveToKeyfile("RAW", "FlatFieldAutoClipControl", raw.ff_AutoClipControl, keyFile);
            saveToKeyfile("RAW", "FlatFieldClipControl", raw.ff_clipControl, keyFile);
            saveToKeyfile("RAW", "FlatFieldUseEmbedded", raw.ff_embedded, keyFile);
        }
        if (RELEVANT_(rawCA)) {
            saveToKeyfile("RAW", "CAEnabled", raw.enable_ca, keyFile);
            saveToKeyfile("RAW", "CA", raw.ca_autocorrect, keyFile);
            saveToKeyfile("RAW", "CAAvoidColourshift", raw.ca_avoidcolourshift, keyFile);
            saveToKeyfile("RAW", "CAAutoIterations", raw.caautoiterations, keyFile);
            saveToKeyfile("RAW", "CARed", raw.cared, keyFile);
            saveToKeyfile("RAW", "CABlue", raw.cablue, keyFile);
        }
        if (RELEVANT_(hotDeadPixelFilter)) {
            saveToKeyfile("RAW", "HotDeadPixelEnabled", raw.enable_hotdeadpix, keyFile);
            saveToKeyfile("RAW", "HotPixelFilter", raw.hotPixelFilter, keyFile);
            saveToKeyfile("RAW", "DeadPixelFilter", raw.deadPixelFilter, keyFile);
            saveToKeyfile("RAW", "HotDeadPixelThresh", raw.hotdeadpix_thresh, keyFile);
        }
        if (RELEVANT_(demosaic)) {
            saveToKeyfile("RAW Bayer", "Method", RAWParams::BayerSensor::getMethodString(raw.bayersensor.method), keyFile);
            saveToKeyfile("RAW Bayer", "Border", raw.bayersensor.border, keyFile);
            saveToKeyfile("RAW Bayer", "ImageNum", raw.bayersensor.imageNum + 1, keyFile);
            saveToKeyfile("RAW Bayer", "CcSteps", raw.bayersensor.ccSteps, keyFile);
        }
        if (RELEVANT_(rawBlack)) {
            saveToKeyfile("RAW Bayer", "PreBlackEnabled", raw.bayersensor.enable_black, keyFile);
            saveToKeyfile("RAW Bayer", "PreBlack0", raw.bayersensor.black0, keyFile);
            saveToKeyfile("RAW Bayer", "PreBlack1", raw.bayersensor.black1, keyFile);
            saveToKeyfile("RAW Bayer", "PreBlack2", raw.bayersensor.black2, keyFile);
            saveToKeyfile("RAW Bayer", "PreBlack3", raw.bayersensor.black3, keyFile);
            saveToKeyfile("RAW Bayer", "PreTwoGreen", raw.bayersensor.twogreen, keyFile);
        }
        if (RELEVANT_(rawPreprocessing)) {
            saveToKeyfile("RAW Bayer", "PreprocessingEnabled", raw.bayersensor.enable_preproc, keyFile);
            saveToKeyfile("RAW Bayer", "LineDenoise", raw.bayersensor.linenoise, keyFile);
            saveToKeyfile("RAW Bayer", "LineDenoiseDirection", toUnderlying(raw.bayersensor.linenoiseDirection), keyFile);
            saveToKeyfile("RAW Bayer", "GreenEqThreshold", raw.bayersensor.greenthresh, keyFile);
        }
        if (RELEVANT_(demosaic)) {
            // saveToKeyfile("RAW Bayer", "DCBIterations", raw.bayersensor.dcb_iterations, keyFile);
            // saveToKeyfile("RAW Bayer", "DCBEnhance", raw.bayersensor.dcb_enhance, keyFile);
            saveToKeyfile("RAW Bayer", "LMMSEIterations", raw.bayersensor.lmmse_iterations, keyFile);
            saveToKeyfile("RAW Bayer", "DualDemosaicAutoContrast", raw.bayersensor.dualDemosaicAutoContrast, keyFile);
            saveToKeyfile("RAW Bayer", "DualDemosaicContrast", raw.bayersensor.dualDemosaicContrast, keyFile);
            saveToKeyfile("RAW Bayer", "PixelShiftMotionCorrectionMethod", toUnderlying(raw.bayersensor.pixelShiftMotionCorrectionMethod), keyFile);
            saveToKeyfile("RAW Bayer", "PixelShiftEperIso", raw.bayersensor.pixelShiftEperIso, keyFile);
            saveToKeyfile("RAW Bayer", "PixelShiftSigma", raw.bayersensor.pixelShiftSigma, keyFile);
            saveToKeyfile("RAW Bayer", "PixelShiftShowMotion", raw.bayersensor.pixelShiftShowMotion, keyFile);
            saveToKeyfile("RAW Bayer", "PixelShiftShowMotionMaskOnly", raw.bayersensor.pixelShiftShowMotionMaskOnly, keyFile);
            saveToKeyfile("RAW Bayer", "pixelShiftHoleFill", raw.bayersensor.pixelShiftHoleFill, keyFile);
            saveToKeyfile("RAW Bayer", "pixelShiftMedian", raw.bayersensor.pixelShiftMedian, keyFile);
            saveToKeyfile("RAW Bayer", "pixelShiftGreen", raw.bayersensor.pixelShiftGreen, keyFile);
            saveToKeyfile("RAW Bayer", "pixelShiftBlur", raw.bayersensor.pixelShiftBlur, keyFile);
            saveToKeyfile("RAW Bayer", "pixelShiftSmoothFactor", raw.bayersensor.pixelShiftSmoothFactor, keyFile);
            saveToKeyfile("RAW Bayer", "pixelShiftEqualBright", raw.bayersensor.pixelShiftEqualBright, keyFile);
            saveToKeyfile("RAW Bayer", "pixelShiftEqualBrightChannel", raw.bayersensor.pixelShiftEqualBrightChannel, keyFile);
            saveToKeyfile("RAW Bayer", "pixelShiftNonGreenCross", raw.bayersensor.pixelShiftNonGreenCross, keyFile);
            saveToKeyfile("RAW Bayer", "pixelShiftDemosaicMethod", raw.bayersensor.pixelShiftDemosaicMethod, keyFile);
        }
        if (RELEVANT_(rawPreprocessing)) {
            saveToKeyfile("RAW Bayer", "PDAFLinesFilter", raw.bayersensor.pdafLinesFilter, keyFile);
            saveToKeyfile("RAW Bayer", "DynamicRowNoiseFilter", raw.bayersensor.dynamicRowNoiseFilter, keyFile);
        }
        if (RELEVANT_(demosaic)) {
            saveToKeyfile("RAW X-Trans", "Method", RAWParams::XTransSensor::getMethodString(raw.xtranssensor.method), keyFile);
            saveToKeyfile("RAW X-Trans", "DualDemosaicAutoContrast", raw.xtranssensor.dualDemosaicAutoContrast, keyFile);
            saveToKeyfile("RAW X-Trans", "DualDemosaicContrast", raw.xtranssensor.dualDemosaicContrast, keyFile);
            saveToKeyfile("RAW X-Trans", "Border", raw.xtranssensor.border, keyFile);
            saveToKeyfile("RAW X-Trans", "CcSteps", raw.xtranssensor.ccSteps, keyFile);
        }
        if (RELEVANT_(rawBlack)) {
            saveToKeyfile("RAW X-Trans", "PreBlackEnabled", raw.xtranssensor.enable_black, keyFile);
            saveToKeyfile("RAW X-Trans", "PreBlackRed", raw.xtranssensor.blackred, keyFile);
            saveToKeyfile("RAW X-Trans", "PreBlackGreen", raw.xtranssensor.blackgreen, keyFile);
            saveToKeyfile("RAW X-Trans", "PreBlackBlue", raw.xtranssensor.blackblue, keyFile);
        }

// Raw exposition
        if (RELEVANT_(rawWhite)) {
            saveToKeyfile("RAW", "PreExposureEnabled", raw.enable_whitepoint, keyFile);
            saveToKeyfile("RAW", "PreExposure", raw.expos, keyFile);
        }

// Film negative
        if (RELEVANT_(filmNegative)) {
            saveToKeyfile("Film Negative", "Enabled", filmNegative.enabled, keyFile);
            saveToKeyfile("Film Negative", "RedRatio", filmNegative.redRatio, keyFile);
            saveToKeyfile("Film Negative", "GreenExponent", filmNegative.greenExp, keyFile);
            saveToKeyfile("Film Negative", "BlueRatio", filmNegative.blueRatio, keyFile);
            if (filmNegative.backCompat == FilmNegativeParams::BackCompat::V2) {
                saveToKeyfile("Film Negative", "RedBase", filmNegative.refInput.r, keyFile);
                saveToKeyfile("Film Negative", "GreenBase", filmNegative.refInput.g, keyFile);
                saveToKeyfile("Film Negative", "BlueBase", filmNegative.refInput.b, keyFile);
            }
            saveToKeyfile("Film Negative", "ColorSpace", toUnderlying(filmNegative.colorSpace), keyFile);
            {
                std::vector<double> v = {
                    filmNegative.refInput.r,
                    filmNegative.refInput.g,
                    filmNegative.refInput.b
                };
                saveToKeyfile("Film Negative", "RefInput", v, keyFile);
                v = {
                    filmNegative.refOutput.r,
                    filmNegative.refOutput.g,
                    filmNegative.refOutput.b
                };
                saveToKeyfile("Film Negative", "RefOutput", v, keyFile);
            }
            if (filmNegative.backCompat != FilmNegativeParams::BackCompat::CURRENT) {
                saveToKeyfile("Film Negative", "BackCompat", toUnderlying(filmNegative.backCompat), keyFile);
            }
        }

// MetaData
        if (RELEVANT_(metadata)) {
            saveToKeyfile("MetaData", "Mode", metadata.mode, keyFile);
            saveToKeyfile("MetaData", "ExifKeys", metadata.exifKeys, keyFile);
            saveToKeyfile("MetaData", "Notes", metadata.notes, keyFile);
        }

// EXIF change list
        if (RELEVANT_(exif)) {
            std::map<Glib::ustring, Glib::ustring> m;
            for (auto &p : exif_keys) {
                m[p.second] = p.first;
            }
            for (auto &p : metadata.exif) {
                auto it = m.find(p.first);
                if (it != m.end()) {
                    keyFile.set_string("Exif", it->second, p.second);
                }
            }
        }

// IPTC change list
        if (RELEVANT_(iptc)) {
            std::map<Glib::ustring, Glib::ustring> m;
            for (auto &p : iptc_keys) {
                m[p.second] = p.first;
            }
            for (auto &p : metadata.iptc) {
                auto it = m.find(p.first);
                if (it != m.end()) {
                    Glib::ArrayHandle<Glib::ustring> values = p.second;
                    keyFile.set_string_list("IPTC", it->second, values);
                }
            }
        }
    //Spot Removal
        if (RELEVANT_(spot)) {
            //Spot removal
            saveToKeyfile("Spot Removal", "Enabled", spot.enabled, keyFile);
            for (size_t i = 0; i < spot.entries.size (); ++i) {
                std::vector<double> entry = {
                    double(spot.entries[i].sourcePos.x),
                    double(spot.entries[i].sourcePos.y),
                    double(spot.entries[i].targetPos.x),
                    double(spot.entries[i].targetPos.y),
                    double(spot.entries[i].radius),
                    double(spot.entries[i].feather),
                    double(spot.entries[i].opacity),
                    double(spot.entries[i].detail)
                };

                std::stringstream ss;
                ss << "Spot" << (i + 1);

                saveToKeyfile("Spot Removal", ss.str(), entry, keyFile);
            }
        }
    } catch (Glib::KeyFileError &exc) {
        if (pl) {
            pl->error(Glib::ustring::compose(M("PROCPARAMS_SAVE_ERROR"), fname, exc.what()));
        }
        return 1;
    }

    return 0;
#undef RELEVANT_
}


int ProcParams::save(ProgressListener *pl,
                     KeyFile &keyFile, const ParamsEdited *pedited,
                     const Glib::ustring &fname) const
{
    return save(pl, true, keyFile, pedited, fname);
}


int ProcParams::load(ProgressListener *pl,
                     const Glib::ustring& fname, const ParamsEdited *pedited)
{
    setlocale(LC_NUMERIC, "C");  // to set decimal point to "."

    if (fname.empty()) {
        return 1;
    }

    KeyFile keyFile;
    keyFile.setProgressListener(pl);

    try {
        if (!Glib::file_test(fname, Glib::FILE_TEST_EXISTS)) {
            return 1;
        }

        if (!keyFile.load_from_file(fname)) {
            // not an error to report
            return 1;
        }

        return load(pl, keyFile, pedited, true, fname);
    } catch (const Glib::Error& e) {
        //printf("-->%s\n", e.what().c_str());
        try {
            Exiv2Metadata md(fname, false);
            md.load();
            std::string xd = md.xmpData()["Xmp.ART.arp"].toString();
            Glib::ustring data = from_xmp(xd);
            if (!keyFile.load_from_data(data)) {
                return 1;
            }
            return load(pl, keyFile, pedited, true, fname);
        } catch (std::exception &exc) {
            if (pl) {
                pl->error(Glib::ustring::compose(M("PROCPARAMS_LOAD_ERROR"), fname, e.what()));
            }
            setDefaults();
            return 1;
        }
    } catch (std::exception &e) {
        if (pl) {
            pl->error(Glib::ustring::compose(M("PROCPARAMS_LOAD_ERROR"), fname, e.what()));
        }
        setDefaults();
        return 1;
    } catch (...) {
        if (pl) {
            pl->error(Glib::ustring::compose(M("PROCPARAMS_LOAD_ERROR"), fname, "unknown exception"));
        }
        //printf("-->unknown exception!\n");
        setDefaults();
        return 1;
    }
}


int ProcParams::load(ProgressListener *pl, bool load_general,
                     const KeyFile &keyFile, const ParamsEdited *pedited,
                     bool resetOnError, const Glib::ustring &fname)
{
#define RELEVANT_(n) (!pedited || pedited->n)
#define APPEND_(n, p) (pedited && pedited->n == ParamsEdited::Undef && (n . regions != p . regions || n . masks != p . masks))
#define DO_APPEND_(l,o) l.insert(l.begin(), o.begin(), o.end())
    
    try {
        Glib::ustring basedir = Glib::path_get_dirname(fname);
        
        if (load_general) {
            ppVersion = PPVERSION;
            appVersion = RTVERSION;

            if (keyFile.has_group("Version")) {
                if (keyFile.has_key("Version", "AppVersion")) {
                    appVersion = keyFile.get_string("Version", "AppVersion");
                }

                if (keyFile.has_key("Version", "Version")) {
                    ppVersion = keyFile.get_integer("Version", "Version");
                }
            }

            if (keyFile.has_group("General") && RELEVANT_(general)) {
                assignFromKeyfile(keyFile, "General", "Rank", rank);
                assignFromKeyfile(keyFile, "General", "ColorLabel", colorlabel);
                assignFromKeyfile(keyFile, "General", "InTrash", inTrash);
            }
        }

        const std::map<std::string, ToneCurveParams::TcMode> tc_mapping = {
            {"Standard", ToneCurveParams::TcMode::STD},
            {"FilmLike", ToneCurveParams::TcMode::FILMLIKE},
            {"SatAndValueBlending", ToneCurveParams::TcMode::SATANDVALBLENDING},
            {"WeightedStd", ToneCurveParams::TcMode::WEIGHTEDSTD},
            {"Luminance", ToneCurveParams::TcMode::LUMINANCE},
            {"Perceptual", ToneCurveParams::TcMode::PERCEPTUAL},
            {"OpenDisplayTransform", ToneCurveParams::TcMode::NEUTRAL},
            {"Neutral", ToneCurveParams::TcMode::NEUTRAL}
        };
        const std::map<std::string, ToneCurveParams::BcMode> bc_mapping = {
            {"Linear", ToneCurveParams::BcMode::LINEAR},
            {"Rolloff", ToneCurveParams::BcMode::ROLLOFF},
            {"SCurve", ToneCurveParams::BcMode::SCURVE}
        };

        if (ppVersion < 350) {
            if (keyFile.has_group("Exposure")) {
                if (RELEVANT_(exposure)) {
                    exposure.enabled = true;
                    assignFromKeyfile(keyFile, "Exposure", "Compensation", exposure.expcomp);
                }

                if (RELEVANT_(toneCurve)) {
                    toneCurve.enabled = true;
                    
                    assignFromKeyfile(keyFile, "Exposure", "CurveMode", tc_mapping, toneCurve.curveMode);
                    assignFromKeyfile(keyFile, "Exposure", "CurveMode2", tc_mapping, toneCurve.curveMode2);

                    if (ppVersion > 200) {
                        assignFromKeyfile(keyFile, "Exposure", "Curve", toneCurve.curve);
                        assignFromKeyfile(keyFile, "Exposure", "Curve2", toneCurve.curve2);
                    }

                    assignFromKeyfile(keyFile, "Exposure", "HistogramMatching", toneCurve.histmatching);
                    if (ppVersion < 340) {
                        toneCurve.fromHistMatching = false;
                    } else {
                        assignFromKeyfile(keyFile, "Exposure", "CurveFromHistogramMatching", toneCurve.fromHistMatching);
                    }
                }
                if (RELEVANT_(saturation)) {
                    saturation.enabled = true;
                    assignFromKeyfile(keyFile, "Exposure", "Saturation", saturation.saturation);
                }
            }
            if (keyFile.has_group("HLRecovery") && RELEVANT_(exposure)) {
                bool en = false;
                Glib::ustring method;
                assignFromKeyfile(keyFile, "HLRecovery", "Enabled", en);
                assignFromKeyfile(keyFile, "HLRecovery", "Method", method);
                if (!en) {
                    exposure.hrmode = ExposureParams::HR_OFF;
                } else if (method == "Blend") {
                    exposure.hrmode = ExposureParams::HR_BLEND;
                } else if (method == "Color") {
                    exposure.hrmode = ExposureParams::HR_COLOR;
                } else {
                    exposure.hrmode = ExposureParams::HR_OFF;
                }
            }
        } else {
            if (keyFile.has_group("Exposure") && RELEVANT_(exposure)) {
                assignFromKeyfile(keyFile, "Exposure", "Enabled", exposure.enabled);
                assignFromKeyfile(keyFile, "Exposure", "Compensation", exposure.expcomp);
                assignFromKeyfile(keyFile, "Exposure", "Black", exposure.black);
                if (ppVersion >= 1000) {
                    Glib::ustring hr;
                    assignFromKeyfile(keyFile, "Exposure", "HLRecovery", hr);
                    if (hr == "Blend") {
                        exposure.hrmode = ExposureParams::HR_BLEND;
                    } else if (hr == "Color") {
                        exposure.hrmode = ExposureParams::HR_COLOR;
                    } else if (hr == "ColorBlend" || hr == "Balanced") {
                        exposure.hrmode = ExposureParams::HR_COLORSOFT;
                    } else {
                        exposure.hrmode = ExposureParams::HR_OFF;
                    }
                } else {
                    bool en = false;
                    Glib::ustring method;
                    assignFromKeyfile(keyFile, "Exposure", "HLRecoveryEnabled", en);
                    assignFromKeyfile(keyFile, "Exposure", "HLRecoveryMethod", method);
                    if (!en) {
                        exposure.hrmode = ExposureParams::HR_OFF;
                    } else if (method == "Blend") {
                        exposure.hrmode = ExposureParams::HR_BLEND;
                    } else if (method == "Color") {
                        exposure.hrmode = ExposureParams::HR_COLOR;
                    } else {
                        exposure.hrmode = ExposureParams::HR_OFF;
                    }
                }
                assignFromKeyfile(keyFile, "Exposure", "HLRecoveryBlur", exposure.hrblur);                
            }
            if (keyFile.has_group("Saturation") && RELEVANT_(saturation)) {
                assignFromKeyfile(keyFile, "Saturation", "Enabled", saturation.enabled);
                assignFromKeyfile(keyFile, "Saturation", "Saturation", saturation.saturation);
                assignFromKeyfile(keyFile, "Saturation", "Vibrance", saturation.vibrance);
            }
            if (keyFile.has_group("ToneCurve") && RELEVANT_(toneCurve)) {
                assignFromKeyfile(keyFile, "ToneCurve", "Enabled", toneCurve.enabled);
                if (assignFromKeyfile(keyFile, "ToneCurve", "Contrast", toneCurve.contrast) && ppVersion < 1034) {
                    double c = std::pow(std::abs(toneCurve.contrast) * 0.125 / 16.0, 1.0/1.5) * 100;
                    toneCurve.contrast = SGN(toneCurve.contrast) * int(c + 0.5);
                }
                if (assignFromKeyfile(keyFile, "ToneCurve", "CurveMode", tc_mapping, toneCurve.curveMode)) {
                    toneCurve.curveMode2 = toneCurve.curveMode;
                }
                assignFromKeyfile(keyFile, "ToneCurve", "CurveMode2", tc_mapping, toneCurve.curveMode2);

                assignFromKeyfile(keyFile, "ToneCurve", "Curve", toneCurve.curve);
                assignFromKeyfile(keyFile, "ToneCurve", "Curve2", toneCurve.curve2);
                assignFromKeyfile(keyFile, "ToneCurve", "HistogramMatching", toneCurve.histmatching);
                assignFromKeyfile(keyFile, "ToneCurve", "CurveFromHistogramMatching", toneCurve.fromHistMatching);
                if (toneCurve.fromHistMatching) {
                    toneCurve.histmatching = false;
                }
                assignFromKeyfile(keyFile, "ToneCurve", "Saturation", toneCurve.saturation);
                assignFromKeyfile(keyFile, "ToneCurve", "Saturation2", toneCurve.saturation2);
                if (!assignFromKeyfile(keyFile, "ToneCurve", "PerceptualStrength", toneCurve.perceptualStrength) && ppVersion >= 1026) {
                    toneCurve.perceptualStrength = 100;
                }
                if (!assignFromKeyfile(keyFile, "ToneCurve", "ContrastLegacyMode", toneCurve.contrastLegacyMode)) {
                    toneCurve.contrastLegacyMode = (ppVersion < 1026);
                }
                assignFromKeyfile(keyFile, "ToneCurve", "WhitePoint", toneCurve.whitePoint);
                assignFromKeyfile(keyFile, "ToneCurve", "BaseCurve", bc_mapping, toneCurve.basecurve);
            }
        }

        if (keyFile.has_group("Channel Mixer") && RELEVANT_(chmixer)) {
            if (ppVersion >= 329) {
                assignFromKeyfile(keyFile, "Channel Mixer", "Enabled", chmixer.enabled);
            } else {
                chmixer.enabled = true;
            }

            int mode = 0;
            if (assignFromKeyfile(keyFile, "Channel Mixer", "Mode", mode)) {
                chmixer.mode = ChannelMixerParams::Mode(mode);
            }

            if (keyFile.has_key("Channel Mixer", "Red") && keyFile.has_key("Channel Mixer", "Green") && keyFile.has_key("Channel Mixer", "Blue")) {
                const std::vector<int> rmix = keyFile.get_integer_list("Channel Mixer", "Red");
                const std::vector<int> gmix = keyFile.get_integer_list("Channel Mixer", "Green");
                const std::vector<int> bmix = keyFile.get_integer_list("Channel Mixer", "Blue");

                if (rmix.size() == 3 && gmix.size() == 3 && bmix.size() == 3) {
                    memcpy(chmixer.red,   rmix.data(), 3 * sizeof(int));
                    memcpy(chmixer.green, gmix.data(), 3 * sizeof(int));
                    memcpy(chmixer.blue,  bmix.data(), 3 * sizeof(int));
                }
                if (ppVersion < 338) {
                    for (int i = 0; i < 3; ++i) {
                        chmixer.red[i] *= 10;
                        chmixer.green[i] *= 10;
                        chmixer.blue[i] *= 10;
                    }
                }
            }

            if (keyFile.has_key("Channel Mixer", "HueTweak")) {
                const std::vector<int> h = keyFile.get_integer_list("Channel Mixer", "HueTweak");
                if (h.size() == 3) {
                    for (int i = 0; i < 3; ++i) {
                        chmixer.hue_tweak[i] = h[i];
                    }
                }
            }
            if (keyFile.has_key("Channel Mixer", "SatTweak")) {
                const std::vector<int> s = keyFile.get_integer_list("Channel Mixer", "SatTweak");
                if (s.size() == 3) {
                    for (int i = 0; i < 3; ++i) {
                        chmixer.sat_tweak[i] = s[i];
                    }
                }
            }
        }

        if (keyFile.has_group("Black & White") && RELEVANT_(blackwhite)) {
            assignFromKeyfile(keyFile, "Black & White", "Enabled", blackwhite.enabled);
            assignFromKeyfile(keyFile, "Black & White", "MixerRed", blackwhite.mixerRed);
            assignFromKeyfile(keyFile, "Black & White", "MixerGreen", blackwhite.mixerGreen);
            assignFromKeyfile(keyFile, "Black & White", "MixerBlue", blackwhite.mixerBlue);
            assignFromKeyfile(keyFile, "Black & White", "GammaRed", blackwhite.gammaRed);
            assignFromKeyfile(keyFile, "Black & White", "GammaGreen", blackwhite.gammaGreen);
            assignFromKeyfile(keyFile, "Black & White", "GammaBlue", blackwhite.gammaBlue);
            assignFromKeyfile(keyFile, "Black & White", "Filter", blackwhite.filter);
            assignFromKeyfile(keyFile, "Black & White", "Setting", blackwhite.setting);
            if (keyFile.has_key("Black & White", "ColorCast")) {
                std::vector<int> ccast = keyFile.get_integer_list("Black & White", "ColorCast");
                if (ccast.size() >= 2) {
                    blackwhite.colorCast.setValues(ccast[0], ccast[1]);
                }
            }
        }

        if (keyFile.has_group("HSL Equalizer") && RELEVANT_(hsl)) {
            assignFromKeyfile(keyFile, "HSL Equalizer", "Enabled", hsl.enabled);
            assignFromKeyfile(keyFile, "HSL Equalizer", "HCurve", hsl.hCurve);
            assignFromKeyfile(keyFile, "HSL Equalizer", "SCurve", hsl.sCurve);
            assignFromKeyfile(keyFile, "HSL Equalizer", "LCurve", hsl.lCurve);
            assignFromKeyfile(keyFile, "HSL Equalizer", "Smoothing", hsl.smoothing);
        }
        
        if (keyFile.has_group("Local Contrast") && RELEVANT_(localContrast)) {
            assignFromKeyfile(keyFile, "Local Contrast", "Enabled", localContrast.enabled);
                
            std::vector<LocalContrastParams::Region> ll;
            std::vector<Mask> lm;
            bool found = false;
            bool done = false;
            for (int i = 0; !done; ++i) {
                LocalContrastParams::Region cur;
                Mask curmask;
                done = true;
                std::string n = i ? std::string("_") + std::to_string(i) : std::string("");
                if (assignFromKeyfile(keyFile, "Local Contrast", Glib::ustring("Contrast") + n, cur.contrast)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, "Local Contrast", Glib::ustring("Curve") + n, cur.curve)) {
                    found = true;
                    done = false;
                }
                if (curmask.load(ppVersion, keyFile, basedir, "Local Contrast", "", n)) {
                    found = true;
                    done = false;
                }
                if (!done) {
                    ll.emplace_back(cur);
                    lm.emplace_back(curmask);
                }
            }
            if (found) {
                if (APPEND_(localContrast, LocalContrastParams())) {
                    DO_APPEND_(ll, localContrast.regions);
                    DO_APPEND_(lm, localContrast.masks);
                }
                localContrast.regions = std::move(ll);
                localContrast.masks = std::move(lm);
            }
            assert(localContrast.regions.size() == localContrast.masks.size());
            assignFromKeyfile(keyFile, "Local Contrast", "ShowMask", localContrast.showMask);
            assignFromKeyfile(keyFile, "Local Contrast", "SelectedRegion", localContrast.selectedRegion);
        }

        if (keyFile.has_group("Luminance Curve") && RELEVANT_(labCurve)) {
            assignFromKeyfile(keyFile, "Luminance Curve", "Enabled", labCurve.enabled);
            assignFromKeyfile(keyFile, "Luminance Curve", "Brightness", labCurve.brightness);
            assignFromKeyfile(keyFile, "Luminance Curve", "Contrast", labCurve.contrast);
            assignFromKeyfile(keyFile, "Luminance Curve", "Chromaticity", labCurve.chromaticity);
            assignFromKeyfile(keyFile, "Luminance Curve", "LCurve", labCurve.lcurve);
            assignFromKeyfile(keyFile, "Luminance Curve", "aCurve", labCurve.acurve);
            assignFromKeyfile(keyFile, "Luminance Curve", "bCurve", labCurve.bcurve);
        }

        if (keyFile.has_group("Sharpening") && RELEVANT_(sharpening)) {
            assignFromKeyfile(keyFile, "Sharpening", "Enabled", sharpening.enabled);
            if (ppVersion >= 334) {
                assignFromKeyfile(keyFile, "Sharpening", "Contrast", sharpening.contrast);
            } else {
                sharpening.contrast = 0;
            }
            assignFromKeyfile(keyFile, "Sharpening", "Radius", sharpening.radius);
            assignFromKeyfile(keyFile, "Sharpening", "Amount", sharpening.amount);

            if (keyFile.has_key("Sharpening", "Threshold")) {
                if (ppVersion < 302) {
                    int thresh = min(keyFile.get_integer("Sharpening", "Threshold"), 2000);
                    sharpening.threshold.setValues(thresh, thresh, 2000, 2000);  // TODO: 2000 is the maximum value and is taken of rtgui/sharpening.cc ; should be changed by the tool modularization
                } else {
                    const std::vector<int> thresh = keyFile.get_integer_list("Sharpening", "Threshold");

                    if (thresh.size() >= 4) {
                        sharpening.threshold.setValues(thresh[0], thresh[1], min(thresh[2], 2000), min(thresh[3], 2000));
                    }
                }
            }

            assignFromKeyfile(keyFile, "Sharpening", "OnlyEdges", sharpening.edgesonly);
            assignFromKeyfile(keyFile, "Sharpening", "EdgedetectionRadius", sharpening.edges_radius);
            assignFromKeyfile(keyFile, "Sharpening", "EdgeTolerance", sharpening.edges_tolerance);
            assignFromKeyfile(keyFile, "Sharpening", "HalocontrolEnabled", sharpening.halocontrol);
            assignFromKeyfile(keyFile, "Sharpening", "HalocontrolAmount", sharpening.halocontrol_amount);
            assignFromKeyfile(keyFile, "Sharpening", "Method", sharpening.method);
            assignFromKeyfile(keyFile, "Sharpening", "DeconvRadius", sharpening.deconvradius);
            assignFromKeyfile(keyFile, "Sharpening", "DeconvAmount", sharpening.deconvamount);
            assignFromKeyfile(keyFile, "Sharpening", "DeconvAutoRadius", sharpening.deconvAutoRadius);
            assignFromKeyfile(keyFile, "Sharpening", "DeconvCornerBoost", sharpening.deconvCornerBoost);
            assignFromKeyfile(keyFile, "Sharpening", "DeconvCornerLatitude", sharpening.deconvCornerLatitude);
            assignFromKeyfile(keyFile, "Sharpening", "PSFKernel", sharpening.psf_kernel);
            assignFromKeyfile(keyFile, "Sharpening", "PSFIterations", sharpening.psf_iterations);
        }

        if (keyFile.has_group("White Balance") && RELEVANT_(wb)) {
            assignFromKeyfile(keyFile, "White Balance", "Enabled", wb.enabled);
            Glib::ustring method = "CustomTemp";
            assignFromKeyfile(keyFile, "White Balance", "Setting", method);
            assignFromKeyfile(keyFile, "White Balance", "Temperature", wb.temperature);
            assignFromKeyfile(keyFile, "White Balance", "Green", wb.green);
            assignFromKeyfile(keyFile, "White Balance", "Equal", wb.equal);
            std::vector<float> m;
            if (assignFromKeyfile(keyFile, "White Balance", "Multipliers", m) && m.size() == 3) {
                for (int i = 0; i < 3; ++i) {
                    wb.mult[i] = m[i];
                }
            }
            if (method == "Camera") {
                wb.method = WBParams::CAMERA;
            } else if (method == "Auto") {
                wb.method = WBParams::AUTO;
            } else if (method == "CustomMult") {
                if (ppVersion < 1027) {
                    wb.method = WBParams::CUSTOM_MULT_LEGACY;
                } else {
                    wb.method = WBParams::CUSTOM_MULT;
                }
            } else if (method == "CustomMultLegacy") {
                wb.method = WBParams::CUSTOM_MULT_LEGACY;
            } else {
                wb.method = WBParams::CUSTOM_TEMP;
            }
        }

        if (keyFile.has_group("Defringing") && RELEVANT_(defringe)) {
            assignFromKeyfile(keyFile, "Defringing", "Enabled", defringe.enabled);
            assignFromKeyfile(keyFile, "Defringing", "Radius", defringe.radius);

            if (keyFile.has_key("Defringing", "Threshold")) {
                defringe.threshold = (float)keyFile.get_integer("Defringing", "Threshold");
            }

            if (ppVersion < 310) {
                defringe.threshold = sqrt(defringe.threshold * 33.f / 5.f);
            }

            assignFromKeyfile(keyFile, "Defringing", "HueCurve", defringe.huecurve);
        }

        if (keyFile.has_group("Impulse Denoising") && RELEVANT_(impulseDenoise)) {
            assignFromKeyfile(keyFile, "Impulse Denoising", "Enabled", impulseDenoise.enabled);
            assignFromKeyfile(keyFile, "Impulse Denoising", "Threshold", impulseDenoise.thresh);
        }

        if (ppVersion < 346) {
            if (keyFile.has_group("Directional Pyramid Denoising") && RELEVANT_(denoise)) { //TODO: No longer an accurate description for FT denoise
                assignFromKeyfile(keyFile, "Directional Pyramid Denoising", "Enabled", denoise.enabled);
                assignFromKeyfile(keyFile, "Directional Pyramid Denoising", "Luma", denoise.luminance);
                assignFromKeyfile(keyFile, "Directional Pyramid Denoising", "Ldetail", denoise.luminanceDetail);
                assignFromKeyfile(keyFile, "Directional Pyramid Denoising", "Chroma", denoise.chrominance);
                Glib::ustring val;
                if (assignFromKeyfile(keyFile, "Directional Pyramid Denoising", "C2Method", val)) {
                    if (val == "MANU") {
                        denoise.chrominanceMethod = DenoiseParams::ChrominanceMethod::MANUAL;
                    } else {
                        denoise.chrominanceMethod = DenoiseParams::ChrominanceMethod::AUTOMATIC;
                    }
                }
                if (assignFromKeyfile(keyFile, "Directional Pyramid Denoising", "SMethod", val)) {
                    denoise.aggressive = (val == "shalbi");
                }
                assignFromKeyfile(keyFile, "Directional Pyramid Denoising", "Redchro", denoise.chrominanceRedGreen);
                assignFromKeyfile(keyFile, "Directional Pyramid Denoising", "Bluechro", denoise.chrominanceBlueYellow);
            }
        } else {
            if (keyFile.has_group("Denoise") && RELEVANT_(denoise)) {
                assignFromKeyfile(keyFile, "Denoise", "Enabled", denoise.enabled);
                Glib::ustring cs = "RGB";
                assignFromKeyfile(keyFile, "Denoise", "ColorSpace", cs);
                if (cs == "LAB") {
                    denoise.colorSpace = DenoiseParams::ColorSpace::LAB;
                } else {
                    denoise.colorSpace = DenoiseParams::ColorSpace::RGB;
                }
                int val;
                assignFromKeyfile(keyFile, "Denoise", "Aggressive", denoise.aggressive);
                assignFromKeyfile(keyFile, "Denoise", "Gamma", denoise.gamma);
                assignFromKeyfile(keyFile, "Denoise", "Luminance", denoise.luminance);
                assignFromKeyfile(keyFile, "Denoise", "LuminanceDetail", denoise.luminanceDetail);
                assignFromKeyfile(keyFile, "Denoise", "LuminanceDetailThreshold", denoise.luminanceDetailThreshold);
                if (assignFromKeyfile(keyFile, "Denoise", "ChrominanceMethod", val)) {
                    denoise.chrominanceMethod = static_cast<DenoiseParams::ChrominanceMethod>(val);
                }
                assignFromKeyfile(keyFile, "Denoise", "ChrominanceAutoFactor", denoise.chrominanceAutoFactor);
                assignFromKeyfile(keyFile, "Denoise", "Chrominance", denoise.chrominance);
                assignFromKeyfile(keyFile, "Denoise", "ChrominanceRedGreen", denoise.chrominanceRedGreen);
                assignFromKeyfile(keyFile, "Denoise", "ChrominanceBlueYellow", denoise.chrominanceBlueYellow);
                assignFromKeyfile(keyFile, "Denoise", "SmoothingEnabled", denoise.smoothingEnabled);
                if (assignFromKeyfile(keyFile, "Denoise", "SmoothingMethod", val) && val != 1) {
                    denoise.smoothingEnabled = false;
                }
                assignFromKeyfile(keyFile, "Denoise", "GuidedChromaRadius", denoise.guidedChromaRadius);
                assignFromKeyfile(keyFile, "Denoise", "NLDetail", denoise.nlDetail);
                assignFromKeyfile(keyFile, "Denoise", "NLStrength", denoise.nlStrength);
            }
        }            

        const Glib::ustring tbgroup = ppVersion < 1000 ? "EPD" : "TextureBoost";
        if (keyFile.has_group(tbgroup) && RELEVANT_(textureBoost)) {
            assignFromKeyfile(keyFile, tbgroup, "Enabled", textureBoost.enabled);
                
            std::vector<TextureBoostParams::Region> ll;
            std::vector<Mask> lm;
            bool found = false;
            bool done = false;
            for (int i = 0; !done; ++i) {
                TextureBoostParams::Region cur;
                Mask curmask;
                done = true;
                std::string n = i ? std::string("_") + std::to_string(i) : std::string("");
                if (assignFromKeyfile(keyFile, tbgroup, Glib::ustring("Strength") + n, cur.strength)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, tbgroup, Glib::ustring(ppVersion < 1009 ? "EdgeStopping" : "DetailThreshold") + n, cur.detailThreshold)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, tbgroup, Glib::ustring("Iterations") + n, cur.iterations)) {
                    found = true;
                    done = false;
                }
                if (curmask.load(ppVersion, keyFile, basedir, tbgroup, "", n)) {
                    found = true;
                    done = false;
                }
                if (!done) {
                    ll.emplace_back(cur);
                    lm.emplace_back(curmask);
                }
            }
            if (found) {
                if (APPEND_(textureBoost, TextureBoostParams())) {
                    DO_APPEND_(ll, textureBoost.regions);
                    DO_APPEND_(lm, textureBoost.masks);
                }
                textureBoost.regions = std::move(ll);
                textureBoost.masks = std::move(lm);
            }
            assert(textureBoost.regions.size() == textureBoost.masks.size());
            assignFromKeyfile(keyFile, tbgroup, "ShowMask", textureBoost.showMask);
            assignFromKeyfile(keyFile, tbgroup, "SelectedRegion", textureBoost.selectedRegion);
        }

        if (keyFile.has_group("FattalToneMapping") && RELEVANT_(fattal)) {
            assignFromKeyfile(keyFile, "FattalToneMapping", "Enabled", fattal.enabled);
            assignFromKeyfile(keyFile, "FattalToneMapping", "Threshold", fattal.threshold);
            assignFromKeyfile(keyFile, "FattalToneMapping", "Amount", fattal.amount);
            assignFromKeyfile(keyFile, "FattalToneMapping", "SaturationControl", fattal.satcontrol);
        }

        if (keyFile.has_group("LogEncoding") && RELEVANT_(logenc)) {
            assignFromKeyfile(keyFile, "LogEncoding", "Enabled", logenc.enabled);
            if (ppVersion >= 1013) {
                assignFromKeyfile(keyFile, "LogEncoding", "Auto", logenc.autocompute);
            } else {
                logenc.autocompute = false;
            }
            assignFromKeyfile(keyFile, "LogEncoding", ppVersion < 1024 ? "AutoGray" : "AutoGain", logenc.autogain);
            if (ppVersion < 349) {
                if (assignFromKeyfile(keyFile, "LogEncoding", "GrayPoint", logenc.gain) && logenc.gain > 0) {
                    logenc.gain = std::log2(18.0/logenc.gain);
                }
            } else {
                if (ppVersion < 1024) {
                    if (assignFromKeyfile(keyFile, "LogEncoding", "SourceGray", logenc.gain) && logenc.gain > 0) {
                        logenc.gain = std::log2(18.0/logenc.gain);
                    }
                } else {
                    assignFromKeyfile(keyFile, "LogEncoding", "Gain", logenc.gain);
                }
                assignFromKeyfile(keyFile, "LogEncoding", "TargetGray", logenc.targetGray);
            }
            assignFromKeyfile(keyFile, "LogEncoding", "BlackEv", logenc.blackEv);
            assignFromKeyfile(keyFile, "LogEncoding", "WhiteEv", logenc.whiteEv);
            if (ppVersion >= 1006) {
                assignFromKeyfile(keyFile, "LogEncoding", "Regularization", logenc.regularization);
            } else {
                logenc.regularization = 0;
            }
            if (ppVersion < 1025) {
                toneCurve.contrast *= 2;
            }
            if (!assignFromKeyfile(keyFile, "LogEncoding", "SaturationControl", logenc.satcontrol) && ppVersion < 1037) {
                logenc.satcontrol = false;
            }
            assignFromKeyfile(keyFile, "LogEncoding", "HighlightCompression", logenc.highlightCompression);
        }

        if (keyFile.has_group("ToneEqualizer") && RELEVANT_(toneEqualizer)) {
            assignFromKeyfile(keyFile, "ToneEqualizer", "Enabled", toneEqualizer.enabled);
            for (size_t i = 0; i < toneEqualizer.bands.size(); ++i) {
                assignFromKeyfile(keyFile, "ToneEqualizer", "Band" + std::to_string(i), toneEqualizer.bands[i]);
            }
            if (ppVersion >= 1020) {
                assignFromKeyfile(keyFile, "ToneEqualizer", "Regularization", toneEqualizer.regularization);
            } else if (ppVersion >= 1002) {
                if (assignFromKeyfile(keyFile, "ToneEqualizer", "Regularization", toneEqualizer.regularization)) {
                    if (toneEqualizer.regularization > 0) {
                        //toneEqualizer.regularization = 6 - std::min(toneEqualizer.regularization, 5);
                        toneEqualizer.regularization = 4;
                    } else if (toneEqualizer.regularization == -5) {
                        toneEqualizer.regularization = 0;
                    } else {
                        toneEqualizer.regularization = 1;
                    }
                }
            } else {
                assignFromKeyfile(keyFile, "ToneEqualizer", "Detail", toneEqualizer.regularization);
            }
            assignFromKeyfile(keyFile, "ToneEqualizer", "Pivot", toneEqualizer.pivot);
        }
        
        if (keyFile.has_group("Crop") && RELEVANT_(crop)) {
            assignFromKeyfile(keyFile, "Crop", "Enabled", crop.enabled);
            assignFromKeyfile(keyFile, "Crop", "X", crop.x);
            assignFromKeyfile(keyFile, "Crop", "Y", crop.y);

            if (keyFile.has_key("Crop", "W")) {
                crop.w = std::max(keyFile.get_integer("Crop", "W"), 1);
            }

            if (keyFile.has_key("Crop", "H")) {
                crop.h = std::max(keyFile.get_integer("Crop", "H"), 1);
            }

            assignFromKeyfile(keyFile, "Crop", "FixedRatio", crop.fixratio);

            if (assignFromKeyfile(keyFile, "Crop", "Ratio", crop.ratio)) {
                //backwards compatibility for crop.ratio
                if (crop.ratio == "DIN" || crop.ratio == "1.414 - DIN EN ISO 216") {
                    crop.ratio = "1.414 - ISO 216 (A4 paper)";
                }

                if (crop.ratio == "8.5:11") {
                    crop.ratio = "8.5:11 - US Letter";
                }

                if (crop.ratio == "11:17") {
                    crop.ratio = "11:17 - Tabloid";
                }
            }

            assignFromKeyfile(keyFile, "Crop", "Orientation", crop.orientation);
            assignFromKeyfile(keyFile, "Crop", "Guide", crop.guide);
        }

        if (keyFile.has_group("Coarse Transformation") && RELEVANT_(coarse)) {
            assignFromKeyfile(keyFile, "Coarse Transformation", "Rotate", coarse.rotate);
            assignFromKeyfile(keyFile, "Coarse Transformation", "HorizontalFlip", coarse.hflip);
            assignFromKeyfile(keyFile, "Coarse Transformation", "VerticalFlip", coarse.vflip);
        }

        if (keyFile.has_group("Rotation") && RELEVANT_(rotate)) {
            assignFromKeyfile(keyFile, "Rotation", "Enabled", rotate.enabled);
            assignFromKeyfile(keyFile, "Rotation", "Degree", rotate.degree);
        }

        if (keyFile.has_group("Common Properties for Transformations") && RELEVANT_(commonTrans)) {
            assignFromKeyfile(keyFile, "Common Properties for Transformations", "AutoFill", commonTrans.autofill);
        }

        if (keyFile.has_group("Distortion") && RELEVANT_(distortion)) {
            assignFromKeyfile(keyFile, "Distortion", "Enabled", distortion.enabled);
            assignFromKeyfile(keyFile, "Distortion", "Amount", distortion.amount);
            assignFromKeyfile(keyFile, "Distortion", "Auto", distortion.autocompute);
        }

        if (keyFile.has_group("LensProfile") && RELEVANT_(lensProf)) {
            if (keyFile.has_key("LensProfile", "LcMode")) {
                lensProf.lcMode = lensProf.getMethodNumber(keyFile.get_string("LensProfile", "LcMode"));
            }

            if (keyFile.has_key("LensProfile", "LCPFile")) {
                if (ppVersion >= 1017) {
                    lensProf.lcpFile = filenameFromUri(keyFile.get_string("LensProfile", "LCPFile"), basedir);
                } else {
                    lensProf.lcpFile = expandRelativePath(fname, "", keyFile.get_string("LensProfile", "LCPFile"));
                }

                if (ppVersion < 327 && !lensProf.lcpFile.empty()) {
                    lensProf.lcMode = LensProfParams::LcMode::LCP;
                }
            }

            assignFromKeyfile(keyFile, "LensProfile", "UseDistortion", lensProf.useDist);
            assignFromKeyfile(keyFile, "LensProfile", "UseVignette", lensProf.useVign);
            assignFromKeyfile(keyFile, "LensProfile", "UseCA", lensProf.useCA);

            if (keyFile.has_key("LensProfile", "LFCameraMake")) {
                lensProf.lfCameraMake = keyFile.get_string("LensProfile", "LFCameraMake");
            }

            if (keyFile.has_key("LensProfile", "LFCameraModel")) {
                lensProf.lfCameraModel = keyFile.get_string("LensProfile", "LFCameraModel");
            }

            if (keyFile.has_key("LensProfile", "LFLens")) {
                lensProf.lfLens = keyFile.get_string("LensProfile", "LFLens");
            }
        }

        if (keyFile.has_group("Perspective") && RELEVANT_(perspective)) {
            assignFromKeyfile(keyFile, "Perspective", "Enabled", perspective.enabled);
            assignFromKeyfile(keyFile, "Perspective", "Horizontal", perspective.horizontal);
            assignFromKeyfile(keyFile, "Perspective", "Vertical", perspective.vertical);
            assignFromKeyfile(keyFile, "Perspective", "Angle", perspective.angle);
            assignFromKeyfile(keyFile, "Perspective", "Shear", perspective.shear);
            assignFromKeyfile(keyFile, "Perspective", "FocalLength", perspective.flength);
            assignFromKeyfile(keyFile, "Perspective", "CropFactor", perspective.cropfactor);
            assignFromKeyfile(keyFile, "Perspective", "Aspect", perspective.aspect); 
            if (keyFile.has_key("Perspective", "ControlLines")) {
                perspective.control_lines = keyFile.get_integer_list("Perspective", "ControlLines");
            }
        }

        if (keyFile.has_group("Gradient") && RELEVANT_(gradient)) {
            assignFromKeyfile(keyFile, "Gradient", "Enabled", gradient.enabled);
            assignFromKeyfile(keyFile, "Gradient", "Degree", gradient.degree);
            assignFromKeyfile(keyFile, "Gradient", "Feather", gradient.feather);
            assignFromKeyfile(keyFile, "Gradient", "Strength", gradient.strength);
            assignFromKeyfile(keyFile, "Gradient", "CenterX", gradient.centerX);
            assignFromKeyfile(keyFile, "Gradient", "CenterY", gradient.centerY);
        }

        if (keyFile.has_group("PCVignette") && RELEVANT_(pcvignette)) {
            assignFromKeyfile(keyFile, "PCVignette", "Enabled", pcvignette.enabled);
            assignFromKeyfile(keyFile, "PCVignette", "Strength", pcvignette.strength);
            assignFromKeyfile(keyFile, "PCVignette", "Feather", pcvignette.feather);
            assignFromKeyfile(keyFile, "PCVignette", "Roundness", pcvignette.roundness);
            assignFromKeyfile(keyFile, "PCVignette", "CenterX", pcvignette.centerX);
            assignFromKeyfile(keyFile, "PCVignette", "CenterY", pcvignette.centerY);
        }

        if (keyFile.has_group("CACorrection") && RELEVANT_(cacorrection)) {
            assignFromKeyfile(keyFile, "CACorrection", "Enabled", cacorrection.enabled);
            assignFromKeyfile(keyFile, "CACorrection", "Red", cacorrection.red);
            assignFromKeyfile(keyFile, "CACorrection", "Blue", cacorrection.blue);
        }

        if (keyFile.has_group("Vignetting Correction") && RELEVANT_(vignetting)) {
            assignFromKeyfile(keyFile, "Vignetting Correction", "Enabled", vignetting.enabled);
            assignFromKeyfile(keyFile, "Vignetting Correction", "Amount", vignetting.amount);
            assignFromKeyfile(keyFile, "Vignetting Correction", "Radius", vignetting.radius);
            assignFromKeyfile(keyFile, "Vignetting Correction", "Strength", vignetting.strength);
            assignFromKeyfile(keyFile, "Vignetting Correction", "CenterX", vignetting.centerX);
            assignFromKeyfile(keyFile, "Vignetting Correction", "CenterY", vignetting.centerY);
        }

        if (keyFile.has_group("Resize") && RELEVANT_(resize)) {
            assignFromKeyfile(keyFile, "Resize", "Enabled", resize.enabled);
            assignFromKeyfile(keyFile, "Resize", "Scale", resize.scale);
            assignFromKeyfile(keyFile, "Resize", "AppliesTo", resize.appliesTo);
            assignFromKeyfile(keyFile, "Resize", "DataSpecified", resize.dataspec);
            assignFromKeyfile(keyFile, "Resize", "Width", resize.width);
            assignFromKeyfile(keyFile, "Resize", "Height", resize.height);
            if (ppVersion >= 339) {
                assignFromKeyfile(keyFile, "Resize", "AllowUpscaling", resize.allowUpscaling);
            } else {
                resize.allowUpscaling = false;
            }
            assignFromKeyfile(keyFile, "Resize", "PPI", resize.ppi);
            if (ppVersion < 1004) {
                resize.unit = ResizeParams::PX;
            } else {
                Glib::ustring u = "px";
                assignFromKeyfile(keyFile, "Resize", "Unit", u);
                if (u == "cm") {
                    resize.unit = ResizeParams::CM;
                } else if (u == "in") {
                    resize.unit = ResizeParams::INCHES;
                } else {
                    resize.unit = ResizeParams::PX;
                }
            }
        }
        
	if (keyFile.has_group ("Spot Removal") && RELEVANT_(spot)) {
            assignFromKeyfile(keyFile, "Spot Removal", "Enabled", spot.enabled);
            int i = 0;
            do {
                std::stringstream ss;
                ss << "Spot" << (i++ + 1);

                if (keyFile.has_key ("Spot Removal", ss.str())) {
                    Glib::ArrayHandle<double> entry = keyFile.get_double_list("Spot Removal", ss.str());
                    const double epsilon = 0.001;  // to circumvent rounding of integer saved as double
                    SpotEntry se;

                    if (entry.size() == 8) {
                        se.sourcePos.set(int(entry.data()[0] + epsilon), int(entry.data()[1] + epsilon));
                        se.targetPos.set(int(entry.data()[2] + epsilon), int(entry.data()[3] + epsilon));
                        se.radius  = LIM<int>(int(entry.data()[4] + epsilon), SpotParams::minRadius, SpotParams::maxRadius);
                        se.feather = float(entry.data()[5]);
                        se.opacity = float(entry.data()[6]);
                        se.detail = entry.data()[7];
                        spot.entries.push_back(se);
                    }
                } else {
                    break;
                }
            } while (1);
        }

        const char *psgrp = ppVersion < 1021 ? "PostResizeSharpening" : "OutputSharpening";
        if (keyFile.has_group(psgrp) && RELEVANT_(prsharpening)) {
            assignFromKeyfile(keyFile, psgrp, "Enabled", prsharpening.enabled);
            assignFromKeyfile(keyFile, psgrp, "Contrast", prsharpening.contrast);
            assignFromKeyfile(keyFile, psgrp, "Radius", prsharpening.radius);
            assignFromKeyfile(keyFile, psgrp, "Amount", prsharpening.amount);

            if (keyFile.has_key(psgrp, "Threshold")) {
                if (ppVersion < 302) {
                    int thresh = min(keyFile.get_integer(psgrp, "Threshold"), 2000);
                    prsharpening.threshold.setValues(thresh, thresh, 2000, 2000);  // TODO: 2000 is the maximum value and is taken of rtgui/sharpening.cc ; should be changed by the tool modularization
                } else {
                    const std::vector<int> thresh = keyFile.get_integer_list(psgrp, "Threshold");

                    if (thresh.size() >= 4) {
                        prsharpening.threshold.setValues(thresh[0], thresh[1], min(thresh[2], 2000), min(thresh[3], 2000));
                    }
                }
            }

            assignFromKeyfile(keyFile, psgrp, "OnlyEdges", prsharpening.edgesonly);
            assignFromKeyfile(keyFile, psgrp, "EdgedetectionRadius", prsharpening.edges_radius);
            assignFromKeyfile(keyFile, psgrp, "EdgeTolerance", prsharpening.edges_tolerance);
            assignFromKeyfile(keyFile, psgrp, "HalocontrolEnabled", prsharpening.halocontrol);
            assignFromKeyfile(keyFile, psgrp, "HalocontrolAmount", prsharpening.halocontrol_amount);
            assignFromKeyfile(keyFile, psgrp, "Method", prsharpening.method);
            assignFromKeyfile(keyFile, psgrp, "DeconvRadius", prsharpening.deconvradius);
            assignFromKeyfile(keyFile, psgrp, "DeconvAmount", prsharpening.deconvamount);
            if (ppVersion < 1021 && !resize.enabled) {
                prsharpening.enabled = false;
            }
        }

        if (keyFile.has_group("Color Management") && RELEVANT_(icm)) {
            if (keyFile.has_key("Color Management", "InputProfile")) {
                icm.inputProfile = keyFile.get_string("Color Management", "InputProfile");
                if (icm.inputProfile.substr(0, 5) == "file:") {
                    if (ppVersion >= 1017) {
                        icm.inputProfile = "file:" + filenameFromUri(icm.inputProfile, basedir);
                    } else {
                        icm.inputProfile = expandRelativePath(fname, "file:", icm.inputProfile);
                    }
                }
            }

            assignFromKeyfile(keyFile, "Color Management", "ToneCurve", icm.toneCurve);
            assignFromKeyfile(keyFile, "Color Management", "ApplyLookTable", icm.applyLookTable);
            assignFromKeyfile(keyFile, "Color Management", "ApplyBaselineExposureOffset", icm.applyBaselineExposureOffset);
            assignFromKeyfile(keyFile, "Color Management", "ApplyHueSatMap", icm.applyHueSatMap);
            assignFromKeyfile(keyFile, "Color Management", "DCPIlluminant", icm.dcpIlluminant);
            if (ppVersion < 1044) {
                icm.dcp_look_early = false;
            } else {
                assignFromKeyfile(keyFile, "Color Management", "DCPLookEarly", icm.dcp_look_early);
            }
            assignFromKeyfile(keyFile, "Color Management", "WorkingProfile", icm.workingProfile);

            assignFromKeyfile(keyFile, "Color Management", "OutputProfile", icm.outputProfile);
            if (ppVersion < 341) {
                if (icm.outputProfile == "RT_Medium_gsRGB") {
                    icm.outputProfile = "RTv4_Medium";
                } else if (icm.outputProfile == "RT_Large_gBT709" || icm.outputProfile == "RT_Large_g10" || icm.outputProfile == "RT_Large_gsRGB") {
                    icm.outputProfile = "RTv4_Large";
                } else if (icm.outputProfile == "WideGamutRGB") {
                    icm.outputProfile = "RTv4_Wide";
                } else if (icm.outputProfile == "RT_sRGB_gBT709" || icm.outputProfile == "RT_sRGB_g10" || icm.outputProfile == "RT_sRGB") {
                    icm.outputProfile = "RTv4_sRGB";
                } else if (icm.outputProfile == "BetaRGB") { // Have we ever provided this profile ? Should we convert this filename ?
                    icm.outputProfile = "RTv4_Beta";
                } else if (icm.outputProfile == "BestRGB") { // Have we ever provided this profile ? Should we convert this filename ?
                    icm.outputProfile = "RTv4_Best";
                } else if (icm.outputProfile == "Rec2020") {
                    icm.outputProfile = "RTv4_Rec2020";
                } else if (icm.outputProfile == "Bruce") { // Have we ever provided this profile ? Should we convert this filename ?
                    icm.outputProfile = "RTv4_Bruce";
                } else if (icm.outputProfile == "ACES") {
                    icm.outputProfile = "RTv4_ACES-AP0";
                }
            }
            if (keyFile.has_key("Color Management", "OutputProfileIntent")) {
                Glib::ustring intent = keyFile.get_string("Color Management", "OutputProfileIntent");

                if (intent == "Perceptual") {
                    icm.outputIntent = RI_PERCEPTUAL;
                } else if (intent == "Relative") {
                    icm.outputIntent = RI_RELATIVE;
                } else if (intent == "Saturation") {
                    icm.outputIntent = RI_SATURATION;
                } else if (intent == "Absolute") {
                    icm.outputIntent = RI_ABSOLUTE;
                }
            }
            assignFromKeyfile(keyFile, "Color Management", "OutputBPC", icm.outputBPC);
            if (!assignFromKeyfile(keyFile, "Color Management", "InputProfileCAT", icm.inputProfileCAT) && ppVersion < 1031) {
                icm.inputProfileCAT = false;
            }
        }

        if (keyFile.has_group("SoftLight") && RELEVANT_(softlight)) {
            assignFromKeyfile(keyFile, "SoftLight", "Enabled", softlight.enabled);
            assignFromKeyfile(keyFile, "SoftLight", "Strength", softlight.strength);
        }

        if (keyFile.has_group("Dehaze") && RELEVANT_(dehaze)) {
            assignFromKeyfile(keyFile, "Dehaze", "Enabled", dehaze.enabled);
            if (ppVersion < 1010) {
                int s = 0;
                if (assignFromKeyfile(keyFile, "Dehaze", "Strength", s)) {
                    float v = 0.5f + LIM((float(s) / 200.f) * 1.38f, -0.5f, 0.5f);
                    dehaze.strength = {
                        FCT_MinMaxCPoints,
                        0.0,
                        v,
                        0.0,
                        0.0,
                        1.0,
                        v,
                        0.0,
                        0.0       
                    };
                }
            } else {
                assignFromKeyfile(keyFile, "Dehaze", "Strength", dehaze.strength);
            }
            assignFromKeyfile(keyFile, "Dehaze", "ShowDepthMap", dehaze.showDepthMap);
            assignFromKeyfile(keyFile, "Dehaze", "Depth", dehaze.depth);
            assignFromKeyfile(keyFile, "Dehaze", "Luminance", dehaze.luminance);
            assignFromKeyfile(keyFile, "Dehaze", "Blackpoint", dehaze.blackpoint);
        }
        
        if (keyFile.has_group("Film Simulation") && RELEVANT_(filmSimulation)) {
            assignFromKeyfile(keyFile, "Film Simulation", "Enabled", filmSimulation.enabled);
            assignFromKeyfile(keyFile, "Film Simulation", "ClutFilename", filmSimulation.clutFilename);
            if (ppVersion >= 1017) {
                filmSimulation.clutFilename = filenameFromUri(filmSimulation.clutFilename, basedir);
            }

            if (keyFile.has_key("Film Simulation", "Strength")) {
                if (ppVersion < 321) {
                    filmSimulation.strength = keyFile.get_double("Film Simulation", "Strength") * 100 + 0.1;
                } else {
                    filmSimulation.strength = keyFile.get_integer("Film Simulation", "Strength");
                }
            }

            if (!assignFromKeyfile(keyFile, "Film Simulation", "AfterToneCurve", filmSimulation.after_tone_curve)) {
                filmSimulation.after_tone_curve = (ppVersion < 1040);
            }

            if (ppVersion < 1041) {
                std::vector<float> tmpparams;
                if (assignFromKeyfile(keyFile, "Film Simulation", "ClutParams", tmpparams)) {
                    filmSimulation.lut_params.clear();
                    CLUTApplication lut(filmSimulation.clutFilename);
                    if (lut) {
                        auto desc = lut.get_param_descriptors();
                        if (desc.size() == tmpparams.size()) {
                            for (size_t i = 0, n = desc.size(); i < n; ++i) {
                                filmSimulation.lut_params[desc[i].name] = { tmpparams[i] };
                            }
                        }
                    }
                }
            } else {
                load_lut_params(keyFile, "Film Simulation", "ClutParams", filmSimulation.lut_params);
            }
        }

        if (keyFile.has_group("RGB Curves") && RELEVANT_(rgbCurves)) {
            if (ppVersion >= 329) {
                assignFromKeyfile(keyFile, "RGB Curves", "Enabled", rgbCurves.enabled);
            } else {
                rgbCurves.enabled = true;
            }

            assignFromKeyfile(keyFile, "RGB Curves", "rCurve", rgbCurves.rcurve);
            assignFromKeyfile(keyFile, "RGB Curves", "gCurve", rgbCurves.gcurve);
            assignFromKeyfile(keyFile, "RGB Curves", "bCurve", rgbCurves.bcurve);
        }

        if (keyFile.has_group("Grain") && RELEVANT_(grain)) {
            assignFromKeyfile(keyFile, "Grain", "Enabled", grain.enabled);
            assignFromKeyfile(keyFile, "Grain", "ISO", grain.iso);
            assignFromKeyfile(keyFile, "Grain", "Strength", grain.strength);
            int gscale = 100;
            if (assignFromKeyfile(keyFile, "Grain", "Scale", gscale)) {
                grain.strength = int(float(grain.strength)/100.f * gscale);
            }
            if (ppVersion < 1043) {
                grain.strength = std::min(grain.strength * 2, 100);
            } else {
                assignFromKeyfile(keyFile, "Grain", "Color", grain.color);
            }
        }

        const char *smoothing_group = ppVersion < 1016 ? "GuidedSmoothing" : "Smoothing";
        if (keyFile.has_group(smoothing_group) && RELEVANT_(smoothing)) {
            assignFromKeyfile(keyFile, smoothing_group, "Enabled", smoothing.enabled);
                
            std::vector<SmoothingParams::Region> ll;
            std::vector<Mask> lm;
            bool found = false;
            bool done = false;
            for (int i = 1; !done; ++i) {
                SmoothingParams::Region cur;
                Mask curmask;
                done = true;
                std::string n = std::to_string(i);
                int c;
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("Channel_") + n, c)) {
                    cur.channel = SmoothingParams::Region::Channel(c);
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("Mode_") + n, c)) {
                    cur.mode = SmoothingParams::Region::Mode(c);
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("Radius_") + n, cur.radius)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("Sigma_") + n, cur.sigma)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("Epsilon_") + n, cur.epsilon)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("Iterations_") + n, cur.iterations)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("Falloff_") + n, cur.falloff)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("NLStrength_") + n, cur.nlstrength)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("NLDetail_") + n, cur.nldetail)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("NumBlades_") + n, cur.numblades)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("Angle_") + n, cur.angle)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("Curvature_") + n, cur.curvature)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("Offset_") + n, cur.offset)) {
                    found = true;
                    done = false;
                }
                if (curmask.load(ppVersion, keyFile, basedir, smoothing_group, "", Glib::ustring("_") + n)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("NoiseStrength_") + n, cur.noise_strength)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("NoiseCoarseness_") + n, cur.noise_coarseness)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("HalationSize_") + n, cur.halation_size)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("HalationColor_") + n, cur.halation_color)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("WavStrength_") + n, cur.wav_strength)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("WavLevels_") + n, cur.wav_levels)) {
                    found = true;
                    done = false;
                }
                if (assignFromKeyfile(keyFile, smoothing_group, Glib::ustring("WavGamma_") + n, cur.wav_gamma)) {
                    found = true;
                    done = false;
                }
                if (!done) {
                    ll.emplace_back(cur);
                    lm.emplace_back(curmask);
                }
            }
            if (found) {
                if (APPEND_(smoothing, SmoothingParams())) {
                    DO_APPEND_(ll, smoothing.regions);
                    DO_APPEND_(lm, smoothing.masks);
                }
                smoothing.regions = std::move(ll);
                smoothing.masks = std::move(lm);
            }
            assert(smoothing.regions.size() == smoothing.masks.size());
            assignFromKeyfile(keyFile, smoothing_group, "ShowMask", smoothing.showMask);
            assignFromKeyfile(keyFile, smoothing_group, "SelectedRegion", smoothing.selectedRegion);
        }

        const char *ccgroup = "ColorCorrection";
        if (keyFile.has_group(ccgroup) && RELEVANT_(colorcorrection)) {
            const Glib::ustring prefix = "";
            assignFromKeyfile(keyFile, ccgroup, "Enabled", colorcorrection.enabled);
            std::vector<ColorCorrectionParams::Region> lg;
            std::vector<Mask> lm;
            bool found = false;
            bool done = false;

            auto ws = ICCStore::getInstance()->workingSpaceMatrix(icm.workingProfile);
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
            
            const auto translate_ab =
                [&](double &a, double &b) -> void
                {
                    float u = SGN(b) * log2lin(std::abs(b), 4.0);
                    float v = SGN(a) * log2lin(std::abs(a), 4.0);
                    translate_uv(u, v);
                    b = SGN(u) * lin2log(std::abs(u), 4.f);
                    a = SGN(v) * lin2log(std::abs(v), 4.f);
                };

            const auto translate_hs =
                [&](double &h, double &s, int c) -> void
                {
                    constexpr float p1[] = { 3.f, 3.f, 3.f };
                    constexpr float p2[] = { 1.f/2.5f, 1.f/2.5f, 1.f/2.5f };
                    s = std::pow(s / 100.0, p1[c]);
                    float u, v;
                    Color::hsl2yuv(h / 180.f * RT_PI_F, s, u, v);
                    translate_uv(u, v);
                    float fh, fs;
                    Color::yuv2hsl(u, v, fh, fs);
                    h = fh * 180.0 / RT_PI;
                    s = std::pow(fs, p2[c]) * 100.0;
                };

            for (int i = 1; !done; ++i) {
                ColorCorrectionParams::Region cur;
                Mask curmask;
                done = true;
                std::string n = std::to_string(i);

                const auto get =
                    [&](const Glib::ustring &key, double &val) -> bool
                    {
                        if (assignFromKeyfile(keyFile, ccgroup, prefix + key + n, val)) {
                            found = true;
                            done = false;
                            return true;
                        } else {
                            return false;
                        }
                    };
            
                
                get("A_", cur.a);
                get("B_", cur.b);
                if (ppVersion < 1028) {
                    translate_ab(cur.a, cur.b);
                }
                get("ABScale_", cur.abscale);
                if (ppVersion < 1005) {
                    int c = -1;
                    if (assignFromKeyfile(keyFile, ccgroup, prefix + Glib::ustring("Channel_") + n, c)) {
                        found = true;
                        done = false;
                    }
                    if (c < 0) {
                        cur.mode = ColorCorrectionParams::Mode::YUV;
                        c = 0;
                    } else {
                        cur.mode = ColorCorrectionParams::Mode::RGB;
                    }
                    get("Slope_", cur.slope[c]);
                    if (get("Offset_", cur.offset[c])) {
                        if (ppVersion <= 1002) {
                            cur.offset[c] *= 2;
                        }
                    }
                    get("Power_", cur.power[c]);
                    get("Pivot_", cur.pivot[c]);
                } else {
                    bool rgb_channels = false;
                    Glib::ustring mode;
                    if (assignFromKeyfile(keyFile, ccgroup, prefix + Glib::ustring("RGBChannels_") + n, rgb_channels)) {
                        found = true;
                        done = false;
                        cur.mode = rgb_channels ? ColorCorrectionParams::Mode::RGB : ColorCorrectionParams::Mode::YUV;
                    } else if (assignFromKeyfile(keyFile, ccgroup, prefix + Glib::ustring("Mode_") + n, mode)) {
                        if (mode == "YUV") {
                            cur.mode = ColorCorrectionParams::Mode::YUV;
                        } else if (mode == "RGB") {
                            cur.mode = ColorCorrectionParams::Mode::RGB;
                        } else if (mode == "HSL") {
                            cur.mode = ColorCorrectionParams::Mode::HSL;
                        } else if (mode == "Jzazbz") {
                            cur.mode = ColorCorrectionParams::Mode::JZAZBZ;
                        } else if (mode == "LUT") {
                            cur.mode = ColorCorrectionParams::Mode::LUT;
                        }
                        found = true;
                        done = false;
                    }
                        
                    if (cur.mode != ColorCorrectionParams::Mode::RGB) {
                        get("Slope_", cur.slope[0]);
                        cur.slope[1] = cur.slope[2] = cur.slope[0];
                        get("Offset_", cur.offset[0]);
                        cur.offset[1] = cur.offset[2] = cur.offset[0];
                        get("Power_", cur.power[0]);
                        if (ppVersion < 1029) {
                            cur.power[0] = 1.0 / cur.power[0];
                        }
                        cur.power[1] = cur.power[2] = cur.power[0];
                        get("Pivot_", cur.pivot[0]);
                        cur.pivot[1] = cur.pivot[2] = cur.pivot[0];
                        if (get("Compression_", cur.compression[0])) {
                            if (ppVersion < 1035) {
                                cur.compression[0] /= 100.0;
                            }
                        }
                        cur.compression[1] = cur.compression[2] = cur.compression[0];
                    } else {
                        const char *chan[3] = { "R", "G", "B" };
                        for (int c = 0; c < 3; ++c) {
                            get(Glib::ustring("Slope") + chan[c] + "_", cur.slope[c]);
                            get(Glib::ustring("Offset") + chan[c] + "_", cur.offset[c]);
                            get(Glib::ustring("Power") + chan[c] + "_", cur.power[c]);
                            if (ppVersion < 1029) {
                                cur.power[c] = 1.0 / cur.power[c];
                            }
                            get(Glib::ustring("Pivot") + chan[c] + "_", cur.pivot[c]);
                            get(Glib::ustring("Compression") + chan[c] + "_", cur.compression[c]);
                        }
                    }
                    {
                        const char *chan[3] = { "Slope", "Offset", "Power" };
                        for (int c = 0; c < 3; ++c) {
                            Glib::ustring w = chan[c];
                            get(w + "H_", cur.hue[c]);
                            get(w + "S_", cur.sat[c]);
                            get(w + "L_", cur.factor[c]);
                            if (ppVersion < 1028) {
                                translate_hs(cur.hue[c], cur.sat[c], c);
                            }
                        }
                    }
                }
                if (ppVersion < 1018) {
                    double sat = 0;
                    get("Saturation_", sat);
                    if (cur.mode == ColorCorrectionParams::Mode::YUV) {
                        cur.inSaturation = sat;
                        cur.outSaturation = 0;
                    } else {
                        cur.inSaturation = 0;
                        cur.outSaturation = sat;
                    }
                } else {
                    get("InSaturation_", cur.inSaturation);
                    get("OutSaturation_", cur.outSaturation);
                }                
                if (assignFromKeyfile(keyFile, ccgroup, prefix + "RGBLuminance_" + n, cur.rgbluminance)) {
                    found = true;
                    done = false;
                }
                get("HueShift_", cur.hueshift);
                if (assignFromKeyfile(keyFile, ccgroup, prefix + "LUTFilename_" + n, cur.lutFilename)) {
                    cur.lutFilename = filenameFromUri(cur.lutFilename, basedir);
                    found = true;
                    done = false;
                }

                if (ppVersion < 1041) {
                    std::vector<float> tmpparams;
                    if (assignFromKeyfile(keyFile, ccgroup, prefix + "LUTParams_" + n, tmpparams)) {
                        cur.lut_params.clear();
                        CLUTApplication lut(cur.lutFilename);
                        if (lut) {
                            auto desc = lut.get_param_descriptors();
                            if (desc.size() == tmpparams.size()) {
                                for (size_t i = 0, n = desc.size(); i < n; ++i) {
                                    cur.lut_params[desc[i].name] = { tmpparams[i] };
                                }
                            }
                        }
                        found = true;
                        done = false;
                    }
                } else {
                    if (load_lut_params(keyFile, ccgroup, prefix + "LUTParams_" + n, cur.lut_params)) {
                        found = true;
                        done = false;
                    }
                }

                if (assignFromKeyfile(keyFile, ccgroup, prefix + "HSLGamma_" + n, cur.hsl_gamma)) {
                    found = true;
                    done = false;
                } else if (ppVersion < 1042) {
                    cur.hsl_gamma = 1.0;
                }
                
                if (curmask.load(ppVersion, keyFile, basedir, ccgroup, prefix, Glib::ustring("_") + n)) {
                    found = true;
                    done = false;
                }
                if (!done) {
                    lg.emplace_back(cur);
                    lm.emplace_back(curmask);
                    if (ppVersion < 1036 && cur.mode == ColorCorrectionParams::Mode::HSL && cur.hueshift != 0) {
                        lg.back().hueshift = 0;
                        lm.emplace_back(curmask);
                        lg.emplace_back();
                        lg.back().hueshift = cur.hueshift;
                        std::swap(lg.back(), lg[lg.size()-2]);
                    }
                }
            }
            if (found) {
                if (APPEND_(colorcorrection, ColorCorrectionParams())) {
                    DO_APPEND_(lg, colorcorrection.regions);
                    DO_APPEND_(lm, colorcorrection.masks);
                }
                colorcorrection.regions = std::move(lg);
                colorcorrection.masks = std::move(lm);
            }
            assert(colorcorrection.regions.size() == colorcorrection.masks.size());
            assignFromKeyfile(keyFile, ccgroup, ppVersion < 348 ? "showMask" : "LabRegionsShowMask", colorcorrection.showMask);
            assignFromKeyfile(keyFile, ccgroup, "SelectedRegion", colorcorrection.selectedRegion);
        }

        if (keyFile.has_group("RAW")) {
            if (RELEVANT_(darkframe)) {
                assignFromKeyfile(keyFile, "RAW", "DarkFrameEnabled", raw.enable_darkframe);
                if (keyFile.has_key("RAW", "DarkFrame")) {
                    if (ppVersion >= 1017) {
                        raw.dark_frame = filenameFromUri(keyFile.get_string("RAW", "DarkFrame"), basedir); 
                    } else {
                        raw.dark_frame = expandRelativePath(fname, "", keyFile.get_string("RAW", "DarkFrame"));
                    }
                }

                assignFromKeyfile(keyFile, "RAW", "DarkFrameAuto", raw.df_autoselect);
            }

            if (RELEVANT_(flatfield)) {
                assignFromKeyfile(keyFile, "RAW", "FlatFieldEnabled", raw.enable_flatfield);
                if (keyFile.has_key("RAW", "FlatFieldFile")) {
                    if (ppVersion >= 1017) {
                        raw.ff_file = filenameFromUri(keyFile.get_string("RAW", "FlatFieldFile"), basedir);
                    } else {
                        raw.ff_file = expandRelativePath(fname, "", keyFile.get_string("RAW", "FlatFieldFile"));
                    }
                }

                assignFromKeyfile(keyFile, "RAW", "FlatFieldAutoSelect", raw.ff_AutoSelect);
                assignFromKeyfile(keyFile, "RAW", "FlatFieldBlurRadius", raw.ff_BlurRadius);
                assignFromKeyfile(keyFile, "RAW", "FlatFieldBlurType", raw.ff_BlurType);
                assignFromKeyfile(keyFile, "RAW", "FlatFieldAutoClipControl", raw.ff_AutoClipControl);

                if (ppVersion < 328) {
                    // With ppversion < 328 this value was stored as a boolean, which is nonsense.
                    // To avoid annoying warnings we skip reading and assume 0.
                    raw.ff_clipControl = 0;
                } else {
                    assignFromKeyfile(keyFile, "RAW", "FlatFieldClipControl", raw.ff_clipControl);
                }
                assignFromKeyfile(keyFile, "RAW", "FlatFieldUseEmbedded", raw.ff_embedded);
            }

            if (RELEVANT_(rawCA)) {
                assignFromKeyfile(keyFile, "RAW", "CAEnabled", raw.enable_ca);
                assignFromKeyfile(keyFile, "RAW", "CA", raw.ca_autocorrect);
                if (ppVersion >= 342) {
                    assignFromKeyfile(keyFile, "RAW", "CAAutoIterations", raw.caautoiterations);
                } else {
                    raw.caautoiterations = 1;
                }

                if (ppVersion >= 343) {
                    assignFromKeyfile(keyFile, "RAW", "CAAvoidColourshift", raw.ca_avoidcolourshift);
                } else {
                    raw.ca_avoidcolourshift = false;
                }
                assignFromKeyfile(keyFile, "RAW", "CARed", raw.cared);
                assignFromKeyfile(keyFile, "RAW", "CABlue", raw.cablue);
            }

            if (RELEVANT_(hotDeadPixelFilter)) {
                assignFromKeyfile(keyFile, "RAW", "HotDeadPixelEnabled", raw.enable_hotdeadpix);
                // For compatibility to elder pp3 versions
                assignFromKeyfile(keyFile, "RAW", "HotDeadPixels", raw.hotPixelFilter);
                raw.deadPixelFilter = raw.hotPixelFilter;

                assignFromKeyfile(keyFile, "RAW", "HotPixelFilter", raw.hotPixelFilter);
                assignFromKeyfile(keyFile, "RAW", "DeadPixelFilter", raw.deadPixelFilter);
                assignFromKeyfile(keyFile, "RAW", "HotDeadPixelThresh", raw.hotdeadpix_thresh);
            }
            if (RELEVANT_(rawWhite)) {
                assignFromKeyfile(keyFile, "RAW", "PreExposureEnabled", raw.enable_whitepoint);
                assignFromKeyfile(keyFile, "RAW", "PreExposure", raw.expos);
            }

            if (ppVersion < 320) {
                if (RELEVANT_(demosaic)) {
                    //assignFromKeyfile(keyFile, "RAW", "Method", raw.bayersensor.method);
                    assignFromKeyfile(keyFile, "RAW", "CcSteps", raw.bayersensor.ccSteps);
                    // assignFromKeyfile(keyFile, "RAW", "DCBIterations", raw.bayersensor.dcb_iterations);
                    // assignFromKeyfile(keyFile, "RAW", "DCBEnhance", raw.bayersensor.dcb_enhance);
                    assignFromKeyfile(keyFile, "RAW", "LMMSEIterations", raw.bayersensor.lmmse_iterations);
                }
                if (RELEVANT_(rawPreprocessing)) {
                    assignFromKeyfile(keyFile, "RAW", "LineDenoise", raw.bayersensor.linenoise);
                    assignFromKeyfile(keyFile, "RAW", "GreenEqThreshold", raw.bayersensor.greenthresh);
                }
                if (RELEVANT_(rawBlack)) {
                    assignFromKeyfile(keyFile, "RAW", "PreBlackzero", raw.bayersensor.black0);
                    assignFromKeyfile(keyFile, "RAW", "PreBlackone", raw.bayersensor.black1);
                    assignFromKeyfile(keyFile, "RAW", "PreBlacktwo", raw.bayersensor.black2);
                    assignFromKeyfile(keyFile, "RAW", "PreBlackthree", raw.bayersensor.black3);
                    assignFromKeyfile(keyFile, "RAW", "PreTwoGreen", raw.bayersensor.twogreen);
                }
            }
        }

        if (keyFile.has_group("RAW Bayer")) {
            if (RELEVANT_(demosaic)) {
                Glib::ustring method;
                if (assignFromKeyfile(keyFile, "RAW Bayer", "Method", method)) {
                    auto &v = raw.bayersensor.getMethodStrings();
                    auto it = std::find(v.begin(), v.end(), method);
                    if (it != v.end()) {
                        raw.bayersensor.method = RAWParams::BayerSensor::Method(it - v.begin());
                    } else if (method == "amazevng4" || method == "dcbvng4") {
                        raw.bayersensor.method = RAWParams::BayerSensor::Method::AMAZEBILINEAR;
                    } else if (method == "rcdvng4") {
                        raw.bayersensor.method = RAWParams::BayerSensor::Method::RCDBILINEAR;
                    } else {
                        raw.bayersensor.method = RAWParams::BayerSensor::Method::AMAZE;
                    }
                }
                assignFromKeyfile(keyFile, "RAW Bayer", "Border", raw.bayersensor.border);

                if (keyFile.has_key("RAW Bayer", "ImageNum")) {
                    raw.bayersensor.imageNum = keyFile.get_integer("RAW Bayer", "ImageNum") - 1;
                }

                assignFromKeyfile(keyFile, "RAW Bayer", "CcSteps", raw.bayersensor.ccSteps);
            }

            if (RELEVANT_(rawBlack)) {
                assignFromKeyfile(keyFile, "RAW Bayer", "PreBlackEnabled", raw.bayersensor.enable_black);
                assignFromKeyfile(keyFile, "RAW Bayer", "PreBlack0", raw.bayersensor.black0);
                assignFromKeyfile(keyFile, "RAW Bayer", "PreBlack1", raw.bayersensor.black1);
                assignFromKeyfile(keyFile, "RAW Bayer", "PreBlack2", raw.bayersensor.black2);
                assignFromKeyfile(keyFile, "RAW Bayer", "PreBlack3", raw.bayersensor.black3);
                assignFromKeyfile(keyFile, "RAW Bayer", "PreTwoGreen", raw.bayersensor.twogreen);
            }

            if (RELEVANT_(rawPreprocessing)) {
                assignFromKeyfile(keyFile, "RAW Bayer", "PreprocessingEnabled", raw.bayersensor.enable_preproc);
                assignFromKeyfile(keyFile, "RAW Bayer", "LineDenoise", raw.bayersensor.linenoise);

                if (keyFile.has_key("RAW Bayer", "LineDenoiseDirection")) {
                    raw.bayersensor.linenoiseDirection = RAWParams::BayerSensor::LineNoiseDirection(keyFile.get_integer("RAW Bayer", "LineDenoiseDirection"));
                }

                assignFromKeyfile(keyFile, "RAW Bayer", "GreenEqThreshold", raw.bayersensor.greenthresh);
            }

            if (RELEVANT_(demosaic)) {
                // assignFromKeyfile(keyFile, "RAW Bayer", "DCBIterations", raw.bayersensor.dcb_iterations);
                // assignFromKeyfile(keyFile, "RAW Bayer", "DCBEnhance", raw.bayersensor.dcb_enhance);
                assignFromKeyfile(keyFile, "RAW Bayer", "LMMSEIterations", raw.bayersensor.lmmse_iterations);
                assignFromKeyfile(keyFile, "RAW Bayer", "DualDemosaicAutoContrast", raw.bayersensor.dualDemosaicAutoContrast);
                if (ppVersion < 345) {
                    raw.bayersensor.dualDemosaicAutoContrast = false;
                }
                assignFromKeyfile(keyFile, "RAW Bayer", "DualDemosaicContrast", raw.bayersensor.dualDemosaicContrast);

                if (keyFile.has_key("RAW Bayer", "PixelShiftMotionCorrectionMethod")) {
                    raw.bayersensor.pixelShiftMotionCorrectionMethod = (RAWParams::BayerSensor::PSMotionCorrectionMethod)keyFile.get_integer("RAW Bayer", "PixelShiftMotionCorrectionMethod");
                }

                assignFromKeyfile(keyFile, "RAW Bayer", "PixelShiftEperIso", raw.bayersensor.pixelShiftEperIso);
                if (ppVersion < 332) {
                    raw.bayersensor.pixelShiftEperIso += 1.0;
                }
                assignFromKeyfile(keyFile, "RAW Bayer", "PixelShiftSigma", raw.bayersensor.pixelShiftSigma);
                assignFromKeyfile(keyFile, "RAW Bayer", "PixelShiftShowMotion", raw.bayersensor.pixelShiftShowMotion);
                assignFromKeyfile(keyFile, "RAW Bayer", "PixelShiftShowMotionMaskOnly", raw.bayersensor.pixelShiftShowMotionMaskOnly);
                assignFromKeyfile(keyFile, "RAW Bayer", "pixelShiftHoleFill", raw.bayersensor.pixelShiftHoleFill);
                assignFromKeyfile(keyFile, "RAW Bayer", "pixelShiftMedian", raw.bayersensor.pixelShiftMedian);
                assignFromKeyfile(keyFile, "RAW Bayer", "pixelShiftGreen", raw.bayersensor.pixelShiftGreen);
                assignFromKeyfile(keyFile, "RAW Bayer", "pixelShiftBlur", raw.bayersensor.pixelShiftBlur);
                assignFromKeyfile(keyFile, "RAW Bayer", "pixelShiftSmoothFactor", raw.bayersensor.pixelShiftSmoothFactor);
                assignFromKeyfile(keyFile, "RAW Bayer", "pixelShiftEqualBright", raw.bayersensor.pixelShiftEqualBright);
                assignFromKeyfile(keyFile, "RAW Bayer", "pixelShiftEqualBrightChannel", raw.bayersensor.pixelShiftEqualBrightChannel);
                assignFromKeyfile(keyFile, "RAW Bayer", "pixelShiftNonGreenCross", raw.bayersensor.pixelShiftNonGreenCross);

                if (ppVersion < 336) {
                    if (keyFile.has_key("RAW Bayer", "pixelShiftLmmse")) {
                        bool useLmmse = keyFile.get_boolean ("RAW Bayer", "pixelShiftLmmse");
                        if (useLmmse) {
                            raw.bayersensor.pixelShiftDemosaicMethod = raw.bayersensor.getPSDemosaicMethodString(RAWParams::BayerSensor::PSDemosaicMethod::LMMSE);
                        } else {
                            raw.bayersensor.pixelShiftDemosaicMethod = raw.bayersensor.getPSDemosaicMethodString(RAWParams::BayerSensor::PSDemosaicMethod::AMAZE);
                        }
                    }
                } else {
                    assignFromKeyfile(keyFile, "RAW Bayer", "pixelShiftDemosaicMethod", raw.bayersensor.pixelShiftDemosaicMethod);
                }
            }

            if (RELEVANT_(rawPreprocessing)) {
                assignFromKeyfile(keyFile, "RAW Bayer", "PDAFLinesFilter", raw.bayersensor.pdafLinesFilter);
                assignFromKeyfile(keyFile, "RAW Bayer", "DynamicRowNoiseFilter", raw.bayersensor.dynamicRowNoiseFilter);
            }
        }

        if (keyFile.has_group("RAW X-Trans")) {
            if (RELEVANT_(demosaic)) {
                Glib::ustring method;
                if (assignFromKeyfile(keyFile, "RAW X-Trans", "Method", method)) {
                    const auto &v = RAWParams::XTransSensor::getMethodStrings();
                    auto it = std::find(v.begin(), v.end(), method);
                    if (it != v.end()) {
                        raw.xtranssensor.method = RAWParams::XTransSensor::Method(it - v.begin());
                    } else {
                        raw.xtranssensor.method = RAWParams::XTransSensor::Method::THREE_PASS;
                    }
                }
                assignFromKeyfile(keyFile, "RAW X-Trans", "DualDemosaicAutoContrast", raw.xtranssensor.dualDemosaicAutoContrast);
                if (ppVersion < 345) {
                    raw.xtranssensor.dualDemosaicAutoContrast = false;
                }
                assignFromKeyfile(keyFile, "RAW X-Trans", "DualDemosaicContrast", raw.xtranssensor.dualDemosaicContrast);
                assignFromKeyfile(keyFile, "RAW X-Trans", "Border", raw.xtranssensor.border);
                assignFromKeyfile(keyFile, "RAW X-Trans", "CcSteps", raw.xtranssensor.ccSteps);
            }
            if (RELEVANT_(rawBlack)) {
                assignFromKeyfile(keyFile, "RAW X-Trans", "PreBlackEnabled", raw.xtranssensor.enable_black);
                assignFromKeyfile(keyFile, "RAW X-Trans", "PreBlackRed", raw.xtranssensor.blackred);
                assignFromKeyfile(keyFile, "RAW X-Trans", "PreBlackGreen", raw.xtranssensor.blackgreen);
                assignFromKeyfile(keyFile, "RAW X-Trans", "PreBlackBlue", raw.xtranssensor.blackblue);
            }
        }

        if (keyFile.has_group("Film Negative") && RELEVANT_(filmNegative)) {
            assignFromKeyfile(keyFile, "Film Negative", "Enabled", filmNegative.enabled);
            assignFromKeyfile(keyFile, "Film Negative", "RedRatio", filmNegative.redRatio);
            assignFromKeyfile(keyFile, "Film Negative", "GreenExponent", filmNegative.greenExp);
            assignFromKeyfile(keyFile, "Film Negative", "BlueRatio", filmNegative.blueRatio);
            if (ppVersion >= 1039) {
                std::vector<float> v;
                if (assignFromKeyfile(keyFile, "Film Negative", "RefInput", v)) {
                    filmNegative.refInput.r = v[0];
                    filmNegative.refInput.g = v[1];
                    filmNegative.refInput.b = v[2];
                }
                if (assignFromKeyfile(keyFile, "Film Negative", "RefOutput", v)) {
                    filmNegative.refOutput.r = v[0];
                    filmNegative.refOutput.g = v[1];
                    filmNegative.refOutput.b = v[2];
                }

                int cs = 0;
                if (assignFromKeyfile(keyFile, "Film Negative", "ColorSpace", cs)) {
                    filmNegative.colorSpace = static_cast<FilmNegativeParams::ColorSpace>(cs);
                }

                if (keyFile.has_key("Film Negative", "BackCompat")) {
                    filmNegative.backCompat = FilmNegativeParams::BackCompat(keyFile.get_integer("Film Negative", "BackCompat"));
                }
            } else if (ppVersion >= 1011) {
                filmNegative.backCompat = FilmNegativeParams::BackCompat::V2;
                filmNegative.colorSpace = FilmNegativeParams::ColorSpace::INPUT;
                double d;
                if (assignFromKeyfile(keyFile, "Film Negative", "RedBase", d)) {
                    filmNegative.refInput.r = d;
                }
                if (assignFromKeyfile(keyFile, "Film Negative", "GreenBase", d)) {
                    filmNegative.refInput.g = d;
                }
                if (assignFromKeyfile(keyFile, "Film Negative", "BlueBase", d)) {
                    filmNegative.refInput.b = d;
                }
            } else {
                filmNegative.backCompat = FilmNegativeParams::BackCompat::V1;
                filmNegative.colorSpace = FilmNegativeParams::ColorSpace::INPUT;
                // Backwards compatibility: use special film base value -1
                filmNegative.refInput.r = -1.f;
                filmNegative.refInput.g = -1.f;
                filmNegative.refInput.b = -1.f;
            }
        }

        if (keyFile.has_group("MetaData") && RELEVANT_(metadata)) {
            int mode = int(ppVersion < 1012 ? MetaDataParams::TUNNEL : MetaDataParams::EDIT);
            assignFromKeyfile(keyFile, "MetaData", "Mode", mode);

            if (mode >= int(MetaDataParams::TUNNEL) && mode <= int(MetaDataParams::STRIP)) {
                metadata.mode = static_cast<MetaDataParams::Mode>(mode);
            }

            if (!assignFromKeyfile(keyFile, "MetaData", "ExifKeys", metadata.exifKeys)) {
                if (ppVersion < 1012) {
                    metadata.exifKeys = { "*" };
                }
            }
            assignFromKeyfile(keyFile, "MetaData", "Notes", metadata.notes);
        }

        if (keyFile.has_group("Exif") && RELEVANT_(exif)) {
            for (const auto& key : keyFile.get_keys("Exif")) {
                auto it = exif_keys.find(key);
                if (it != exif_keys.end()) {
                    metadata.exif[it->second] = keyFile.get_string("Exif", key);
                }
            }
        }

        /*
         * Load iptc change settings
         *
         * Existing values are preserved, and the stored values
         * are added to the list. To reset a field, the user has to
         * save the profile with the field leaved empty, but still
         * terminated by a semi-column ";"
         *
         * Please note that the old Keywords and SupplementalCategories
         * tag content is fully replaced by the new one,
         * i.e. they don't merge
         */
        if (keyFile.has_group("IPTC") && RELEVANT_(iptc)) {
            for (const auto& key : keyFile.get_keys("IPTC")) {
                // does this key already exist?
                auto it = iptc_keys.find(key);
                if (it == iptc_keys.end()) {
                    continue;
                }
                auto kk = it->second;
                const IPTCPairs::iterator element = metadata.iptc.find(kk);

                if (element != metadata.iptc.end()) {
                    // it already exist so we cleanup the values
                    element->second.clear();
                }

                // TODO: look out if merging Keywords and SupplementalCategories from the procparams chain would be interesting
                for (const auto& currLoadedTagValue : keyFile.get_string_list("IPTC", key)) {
                    metadata.iptc[kk].push_back(currLoadedTagValue);
                }
            }
        }

        return 0;
    } catch (const Glib::Error& e) {
        //printf("-->%s\n", e.what().c_str());
        if (pl) {
            pl->error(Glib::ustring::compose(M("PROCPARAMS_LOAD_ERROR"), fname, e.what()));
        }
        if (resetOnError) {
            setDefaults();
        }
        return 1;
    } catch (std::exception &e) {
        if (pl) {
            pl->error(Glib::ustring::compose(M("PROCPARAMS_LOAD_ERROR"), fname, e.what()));
        }            
        if (resetOnError) {
            setDefaults();
        }
        return 1;        
    } catch (...) {
        //printf("-->unknown exception!\n");
        if (pl) {
            pl->error(Glib::ustring::compose(M("PROCPARAMS_LOAD_ERROR"), fname, "unknown exception"));
        }
        if (resetOnError) {
            setDefaults();
        }
        return 1;
    }

    return 0;

#undef DO_APPEND_
#undef APPEND_
#undef RELEVANT_
}


int ProcParams::load(ProgressListener *pl,
                     const KeyFile &keyFile, const ParamsEdited *pedited,
                     bool resetOnError, const Glib::ustring &fname)
{
    return load(pl, true, keyFile, pedited, resetOnError, fname);
}


ProcParams* ProcParams::create()
{
    return new ProcParams();
}

void ProcParams::destroy(ProcParams* pp)
{
    delete pp;
}

bool ProcParams::operator ==(const ProcParams& other) const
{
    const auto d =
        [](const char *name) -> bool
        {
            //std::cout << "ProcParams differ in " << name << std::endl;
            return false;
        };
    
#define ppEQ_(name) (name == other . name || d(#name))
    return
        ppEQ_(exposure)
        && ppEQ_(saturation)
        && ppEQ_(toneCurve)
        && ppEQ_(localContrast)
        && ppEQ_(labCurve)
        && ppEQ_(sharpening)
        && ppEQ_(prsharpening)
        && ppEQ_(wb)
        && ppEQ_(impulseDenoise)
        && ppEQ_(denoise)
        && ppEQ_(textureBoost)
        && ppEQ_(fattal)
        && ppEQ_(logenc)
        && ppEQ_(defringe)
        && ppEQ_(toneEqualizer)
        && ppEQ_(crop)
        && ppEQ_(coarse)
        && ppEQ_(rotate)
        && ppEQ_(commonTrans)
        && ppEQ_(distortion)
        && ppEQ_(lensProf)
        && ppEQ_(perspective)
        && ppEQ_(gradient)
        && ppEQ_(pcvignette)
        && ppEQ_(cacorrection)
        && ppEQ_(vignetting)
        && ppEQ_(chmixer)
        && ppEQ_(blackwhite)
        && ppEQ_(hsl)
        && ppEQ_(resize)
        && ppEQ_(raw)
        && ppEQ_(icm)
        && ppEQ_(filmSimulation)
        && ppEQ_(softlight)
        && ppEQ_(rgbCurves)
        && ppEQ_(metadata)
        && ppEQ_(dehaze)
        && ppEQ_(grain)
        && ppEQ_(smoothing)
        && ppEQ_(colorcorrection)
        && ppEQ_(filmNegative)
        && ppEQ_(spot);
#undef ppEQ_
}

bool ProcParams::operator !=(const ProcParams& other) const
{
    return !(*this == other);
}

void ProcParams::init()
{
}

void ProcParams::cleanup()
{
}

int ProcParams::write(ProgressListener *pl,
                      const Glib::ustring& fname, const Glib::ustring& content) const
{
    int error = 0;

    if (fname.length()) {
        FILE *f;
        f = g_fopen(fname.c_str(), "wt");

        if (f == nullptr) {
            if (pl) {
                pl->error(Glib::ustring::compose(M("PROCPARAMS_SAVE_ERROR"), fname, "write error"));
            }
            error = 1;
        } else {
            fprintf(f, "%s", content.c_str());
            fclose(f);
        }
    }

    return error;
}


bool ProcParams::from_data(const char *data)
{
    setlocale(LC_NUMERIC, "C");  // to set decimal point to "."
    try {
        KeyFile kf;
        if (!kf.load_from_data(data)) {
            return false;
        }

        return load(nullptr, kf, nullptr, true, "") == 0;
    } catch (const Glib::Error& e) {
        return false;
    }
}


std::string ProcParams::to_data() const
{
    try {
        KeyFile kf;
        int ret = save(nullptr, kf, nullptr, "");
        if (ret != 0) {
            return "";
        }

        return kf.to_data();
    } catch (Glib::KeyFileError &exc) {
        return "";
    }
}


std::vector<const MaskableParams *> ProcParams::get_maskable() const
{
    std::vector<const MaskableParams *> ret = {
        &colorcorrection,
        &smoothing,
        &textureBoost,
        &localContrast
    };
    return ret;
}



FullPartialProfile::FullPartialProfile():
    pp_()
{
}


FullPartialProfile::FullPartialProfile(const ProcParams &pp):
    pp_(pp)
{
}


bool FullPartialProfile::applyTo(ProcParams &pp) const
{
    pp = pp_;
    return true;
}


FilePartialProfile::FilePartialProfile(ProgressListener *pl, const Glib::ustring &fname, bool append):
    pl_(pl),
    fname_(fname),
    append_(append)
{
}


bool FilePartialProfile::applyTo(ProcParams &pp) const
{
    ParamsEdited pe(true);
    pe.set_append(append_);
    return !fname_.empty() && (pp.load(pl_, fname_, &pe) == 0);
}


PEditedPartialProfile::PEditedPartialProfile(ProgressListener *pl, const Glib::ustring &fname, const ParamsEdited &pe):
    pl_(pl),
    fname_(fname),
    pp_(),
    pe_(pe)
{
}


PEditedPartialProfile::PEditedPartialProfile(const ProcParams &pp, const ParamsEdited &pe):
    pl_(nullptr),
    fname_(""),
    pp_(pp),
    pe_(pe)
{
}


bool PEditedPartialProfile::applyTo(ProcParams &pp) const
{
    if (!fname_.empty()) {
        KeyFile keyfile;
        try {
            if (!Glib::file_test(fname_, Glib::FILE_TEST_EXISTS) ||
                !keyfile.load_from_file(fname_)) {
                // not an error to repor
                return false;
            }
        } catch (const Glib::Error& e) {
            //printf("-->%s\n", e.what().c_str());
            if (pl_) {
                pl_->error(Glib::ustring::compose(M("PROCPARAMS_LOAD_ERROR"), fname_, e.what()));
            }
            return false;
        }
        return pp.load(pl_, keyfile, &pe_, false) == 0;
    } else {
        KeyFile keyfile;
        if (pp_.save(pl_, keyfile, &pe_) == 0) {
            return pp.load(pl_, keyfile, &pe_, false) == 0;
        }
    }
    return false;
}


void MultiPartialProfile::add(const PartialProfile *p)
{
    profiles_.push_back(p);
}


void MultiPartialProfile::clear()
{
    profiles_.clear();
}


bool MultiPartialProfile::applyTo(ProcParams &pp) const
{
    bool res = false;
    for (auto p : profiles_) {
        if (p->applyTo(pp)) {
            res = true;
        }
    }
    return res;
}


//-----------------------------------------------------------------------------
// ProcParamsWithSnapshots
//-----------------------------------------------------------------------------

int ProcParamsWithSnapshots::load(ProgressListener *pl,
                                  const Glib::ustring &fname)
{
    setlocale(LC_NUMERIC, "C");  // to set decimal point to "."

    if (fname.empty()) {
        return 1;
    }

    KeyFile keyfile;
    keyfile.setProgressListener(pl);
    
    snapshots.clear();

    try {
        if (!Glib::file_test(fname, Glib::FILE_TEST_EXISTS) ||
            !keyfile.load_from_file(fname)) {
            // not an error to report
            return 1;
        }
        if (master.load(pl, true, keyfile, nullptr, true, fname) != 0) {
            return 1;
        }
        const std::string sn = "Snapshot_";
        if (keyfile.has_group("Snapshots")) {
            for (size_t i = 1; ; ++i) {
                Glib::ustring key = sn + std::to_string(i);
                if (keyfile.has_key("Snapshots", key)) {
                    auto name = keyfile.get_string("Snapshots", key);
                    snapshots.push_back(std::make_pair(name, ProcParams()));
                } else {
                    break;
                }
            }
        }

        for (size_t i = 0; i < snapshots.size(); ++i) {
            keyfile.set_prefix(sn + std::to_string(i+1) + " ");
            snapshots[i].second.appVersion = master.appVersion;
            snapshots[i].second.ppVersion = master.ppVersion;
            if (snapshots[i].second.load(pl, false, keyfile, nullptr, true, fname) != 0) {
                snapshots.resize(i);
                break;
            }
        }

        return 0;
    } catch (const Glib::Error &e) {
        if (pl) {
            pl->error(Glib::ustring::compose(M("PROCPARAMS_LOAD_ERROR"), fname, e.what()));
        }
        //printf("-->%s\n", e.what().c_str());
        master.setDefaults();
        snapshots.clear();
        return 1;
    } catch (...) {
        if (pl) {
            pl->error(Glib::ustring::compose(M("PROCPARAMS_LOAD_ERROR"), fname, "unknown exception"));
        }
        //printf("-->unknown exception!\n");
        master.setDefaults();
        snapshots.clear();
        return 1;
    }
}


int ProcParamsWithSnapshots::save(ProgressListener *pl,
                                  const Glib::ustring &fname, const Glib::ustring &fname2)
{
    if (fname.empty() && fname2.empty()) {
        return 0;
    }

    Glib::ustring data;

    try {
        KeyFile keyfile;

        keyfile.set_string("Version", "AppVersion", RTVERSION);
        keyfile.set_integer("Version", "Version", PPVERSION);
        if (master.rank >= 0) {
            saveToKeyfile("General", "Rank", master.rank, keyfile);
        }
        saveToKeyfile("General", "ColorLabel", master.colorlabel, keyfile);
        saveToKeyfile("General", "InTrash", master.inTrash, keyfile);
        
        const std::string sn = "Snapshot_";
        for (size_t i = 0; i < snapshots.size(); ++i) {
            Glib::ustring key = sn + std::to_string(i+1);
            keyfile.set_string("Snapshots", key, snapshots[i].first);
        }

        int ret = master.save(pl, false, keyfile, nullptr, fname);
        if (ret != 0) {
            return ret;
        }

        for (size_t i = 0; i < snapshots.size(); ++i) {
            keyfile.set_prefix(sn + std::to_string(i+1) + " ");
            ret = snapshots[i].second.save(pl, false, keyfile, nullptr, fname);
            if (ret != 0) {
                return ret;
            }
        }
        
        data = keyfile.to_data();
    } catch (Glib::KeyFileError &exc) {
        if (pl) {
            pl->error(Glib::ustring::compose(M("PROCPARAMS_SAVE_ERROR"), fname, exc.what()));
        }
    }

    if (data.empty()) {
        return 1;
    }

    int error1, error2;
    error1 = master.write(pl, fname, data);

    if (!fname2.empty()) {
        error2 = master.write(pl, fname2, data);
        // If at least one file has been saved, it's a success
        return error1 & error2;
    } else {
        return error1;
    }

    return 0;
}

}} // namespace rtengine::procparams

