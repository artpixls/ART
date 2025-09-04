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

#pragma once

#include "procparams.h"
#include "array2D.h"
#include "labimage.h"
#include "imagefloat.h"
#include "cache.h"
#include <unordered_map>
#include <unordered_set>


namespace rtengine {

class LinkedMaskManager {
public:
    LinkedMaskManager();
    void init(const rtengine::ProcParams &pparams);
    bool store_mask(const Glib::ustring &toolname, const Glib::ustring &name, const array2D<float> *mask1, const array2D<float> *mask2, bool multithread);
    bool apply_mask(const Glib::ustring &toolname, const Glib::ustring &name, bool inverted, array2D<float> *out1, array2D<float> *out2, bool multithread, ProgressListener *plistener);
    bool is_needed(const Glib::ustring &toolname, const Glib::ustring &name);

private:
    std::string key(const Glib::ustring &toolname, const Glib::ustring &name);
    std::unordered_map<std::string, array2D<uint32_t>> masks_;
    std::unordered_set<std::string> needed_;
};


class ExternalMaskManager: public NonCopyable {
public:
    static ExternalMaskManager *getInstance();
    bool apply_mask(const Glib::ustring &filename, bool inverted, double feather, int offset_x, int offset_y, int full_width, int full_height, const array2D<float> &guide, array2D<float> *out, bool multithread, ProgressListener *plistener);

    static void init();
    static void cleanup();

private:
    ExternalMaskManager();

    Cache<std::string, std::shared_ptr<array2D<float>>> cache_;
    static std::unique_ptr<ExternalMaskManager> instance_;
};


bool generateMasks(Imagefloat *rgb, const Glib::ustring &toolname, LinkedMaskManager &mmgr, const std::vector<procparams::Mask> &masks, int offset_x, int offset_y, int full_width, int full_height, double scale, bool multithread, int show_mask_idx, std::vector<array2D<float>> *Lmask, std::vector<array2D<float>> *abmask, ProgressListener *pl);

enum class MasksEditID { H = 0, C, L };
void fillPipetteMasks(Imagefloat *rgb, PlanarWhateverData<float> *editWhatever, MasksEditID id, bool multithread);

bool getDeltaEColor(Imagefloat *rgb, int x, int y, int offset_x, int offset_y, int full_width, int full_height, double scale, float &L, float &C, float &H);

} // namespace rtengine
