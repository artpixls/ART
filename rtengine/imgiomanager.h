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

#pragma once

#include "noncopyable.h"
#include "rtengine.h"
#include "imageio.h"
#include "procparams.h"
#include <glibmm/ustring.h>
#include <unordered_map>
#include <map>
#include <cctype>

namespace rtengine {

class ImageIOManager: public NonCopyable {
public:
    enum Format {
        FMT_UNKNOWN = 0,
        FMT_JPG,
        FMT_PNG,
        FMT_PNG16,
        FMT_TIFF,
        FMT_TIFF_FLOAT,
        FMT_TIFF_FLOAT16
    };

    struct SaveFormatInfo {
        std::string extension;
        Glib::ustring label;

        SaveFormatInfo(const std::string &ext="", const Glib::ustring &lbl=""):
            extension(ext), label(lbl) {}
    };

    static ImageIOManager *getInstance();

    void init(const Glib::ustring &base_dir, const Glib::ustring &user_dir);
    
    bool load(const Glib::ustring &fileName, ProgressListener *plistener, ImageIO *&img, int maxw_hint, int maxh_hint);
    bool save(IImagefloat *img, const std::string &ext, const Glib::ustring &fileName, ProgressListener *plistener);
    std::vector<std::pair<std::string, SaveFormatInfo>> getSaveFormats() const;

    bool canLoad(const std::string &ext) const
    {
        return loaders_.find(ext) != loaders_.end();
    }

    Format getFormat(const Glib::ustring &fileName);

    const procparams::PartialProfile *getSaveProfile(const std::string &ext) const;

    bool loadRaw(const Glib::ustring &fname, const std::string &make, const std::string &model, Glib::ustring &out_dng_name);

private:
    typedef std::pair<Glib::ustring, Glib::ustring> Pair;

    void do_init(const Glib::ustring &dir);
    static Glib::ustring get_ext(Format f);

    bool do_loadRaw(const Pair &p, const Glib::ustring &fname, Glib::ustring &out_dng_name);
    

    Glib::ustring sysdir_;
    Glib::ustring usrdir_;
    
    std::unordered_map<std::string, Pair> loaders_;
    std::unordered_map<std::string, Pair> savers_;
    std::unordered_map<std::string, Format> fmts_;
    std::map<std::string, SaveFormatInfo> savelbls_;
    std::unordered_map<std::string, procparams::FilePartialProfile> saveprofiles_;
    class RawKey {
    public:
        std::string ext;
        std::string make;
        std::string model;

        RawKey(const std::string &e="", const std::string &ma="", const std::string &mo=""): ext(e), make(), model()
        {
            make.reserve(ma.size());
            for (auto &c : ma) {
                make.push_back(std::tolower(c));
            }
            model.reserve(mo.size());
            for (auto &c : mo) {
                model.push_back(std::tolower(c));
            }
        }

        bool operator<(const RawKey &other) const
        {
            int r = ext.compare(other.ext);
            if (r) {
                return r > 0;
            }
            r = make.compare(other.make);
            if (r) {
                return r > 0;
            }
            r = model.compare(other.model);
            return r > 0;
        }
    };
    std::map<RawKey, Pair> raw_loaders_;
};

} // namespace rtengine
