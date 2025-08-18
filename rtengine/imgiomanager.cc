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

#include "imgiomanager.h"
#include "subprocess.h"
#include "utils.h"
#include "settings.h"
#include "imagefloat.h"
#include "image8.h"
#include "image16.h"
#include "profilestore.h"
#include "../rtgui/pathutils.h"
#include "../rtgui/config.h"
#include <iostream>
#include <glib/gstdio.h>
#include <unistd.h>

namespace rtengine {

extern const Settings *settings;

namespace {

ImageIOManager instance;

class S: public Glib::ustring {
public:
    explicit S(const Glib::ustring &s): Glib::ustring(s) {}
};

std::ostream &operator<<(std::ostream &out, const S &s)
{
    try {
        return out << std::string(s);
    } catch (Glib::ConvertError &e) {
        return out << s.raw();
    }
}


inline void exec_sync(const Glib::ustring &usrdir, const Glib::ustring &sysdir, const Glib::ustring &workdir, const std::vector<Glib::ustring> &argv, bool search_in_path, std::string *out, std::string *err)
{
    auto pth = Glib::getenv("PATH");
    auto extrapath = Glib::build_filename(usrdir, "bin") + G_SEARCHPATH_SEPARATOR_S + Glib::build_filename(sysdir, "bin");
#ifdef BUILD_BUNDLE
    extrapath += G_SEARCHPATH_SEPARATOR_S + options.ART_base_dir;
#endif // BUILD_BUNDLE
    auto epth = Glib::getenv("ART_EXIFTOOL_BASE_DIR");
    if (!epth.empty()) {
        extrapath += G_SEARCHPATH_SEPARATOR_S + epth;
    }
    Glib::setenv("PATH", extrapath + G_SEARCHPATH_SEPARATOR_S + pth);
    subprocess::exec_sync(workdir, argv, search_in_path, out, err);
    Glib::setenv("PATH", pth);
}

} // namespace


ImageIOManager *ImageIOManager::getInstance()
{
    return &instance;
}


void ImageIOManager::init(const Glib::ustring &base_dir, const Glib::ustring &user_dir)
{
    sysdir_ = Glib::build_filename(base_dir, "imageio");
    usrdir_ = Glib::build_filename(user_dir, "imageio");
    do_init(sysdir_);
    do_init(usrdir_);
}


void ImageIOManager::do_init(const Glib::ustring &dirname)
{
    if (!Glib::file_test(dirname, Glib::FILE_TEST_IS_DIR)) {
        return;
    }

    try {
        Glib::Dir dir(dirname);
        std::vector<std::string> dirlist(dir.begin(), dir.end());
        std::sort(dirlist.begin(), dirlist.end());

        for (auto &filename : dirlist) {
            auto ext = getFileExtension(filename).lowercase();
            if (filename[0] == '.' || ext != "txt") {
                continue;
            }
            
            const Glib::ustring pth = Glib::build_filename(dirname, filename);

            if (!Glib::file_test(pth, Glib::FILE_TEST_IS_REGULAR)) {
                continue;
            }

            try {
                Glib::KeyFile kf;
                if (!kf.load_from_file(pth)) {
                    continue;
                }

                const Glib::ustring raw_group = "ART RAWImageIO";

                if (kf.has_group(raw_group)) {
                    RawKey k;

                    if (!kf.has_key(raw_group, "ReadCommand")) {
                        continue;
                    }
                    Glib::ustring cmd = kf.get_string(raw_group, "ReadCommand");
                    
                    if (kf.has_key(raw_group, "Extension")) {
                        k.ext = kf.get_string(raw_group, "Extension").lowercase();
                    } else {
                        continue;
                    }

                    if (kf.has_key(raw_group, "Make")) {
                        k.make = kf.get_string(raw_group, "Make").lowercase();
                    }

                    if (kf.has_key(raw_group, "Model")) {
                        k.model = kf.get_string(raw_group, "Model").lowercase();
                    }

                    raw_loaders_[k] = Pair(dirname, cmd);

                    if (settings->verbose > 1) {
                        std::cout << "Found RAW loader for (extension, make, model) \"" << k.ext << ", " << k.make << ", " << k.model << "\": " << S(cmd) << std::endl;
                    }
                    
                    continue;
                }
                
                const Glib::ustring group = "ART ImageIO";
                
                Format fmt = FMT_TIFF_FLOAT;

                std::string ext;
                if (kf.has_key(group, "Extension")) {
                    ext = kf.get_string(group, "Extension").lowercase();
                } else {
                    continue;
                }

                std::string savefmt = ext;
                if (kf.has_key(group, "SaveFormat")) {
                    savefmt = kf.get_string(group, "SaveFormat").lowercase();
                }

                Glib::ustring cmd;
                if (kf.has_key(group, "ReadCommand")) {
                    cmd = kf.get_string(group, "ReadCommand");
                    loaders_[ext] = Pair(dirname, cmd);

                    if (settings->verbose > 1) {
                        std::cout << "Found loader for extension \"" << ext << "\": " << S(cmd) << std::endl;
                    }
                }

                if (kf.has_key(group, "WriteCommand")) {
                    cmd = kf.get_string(group, "WriteCommand");
                    savers_[savefmt] = Pair(dirname, cmd);
                    Glib::ustring lbl;
                    if (kf.has_key(group, "Label")) {
                        lbl = kf.get_string(group, "Label");
                    } else {
                        lbl = Glib::ustring(ext).uppercase();
                    }

                    savelbls_[savefmt] = SaveFormatInfo(ext, lbl);
                    
                    if (settings->verbose > 1) {
                        std::cout << "Found saver for format \"" << savefmt << "\" with extension \"" << ext << "\": " << S(cmd) << std::endl;
                    }
                }

                if (kf.has_key(group, "Format")) {
                    auto f = kf.get_string(group, "Format").lowercase();
                    if (f == "jpg") {
                        fmt = FMT_JPG;
                    } else if (f == "png") {
                        fmt = FMT_PNG;
                    } else if (f == "png16") {
                        fmt = FMT_PNG16;
                    } else if (f == "tiff") {
                        fmt = FMT_TIFF;
                    } else if (f == "float") {
                        fmt = FMT_TIFF_FLOAT;
                    } else if (f == "half") {
                        fmt = FMT_TIFF_FLOAT16;
                    }
                }
                fmts_[savefmt] = fmt;

                if (kf.has_key(group, "SaveProfile")) {
                    auto f = kf.get_string(group, "SaveProfile");
                    if (!Glib::path_is_absolute(f)) {
                        f = Glib::build_filename(dirname, f);
                    }
                    procparams::FilePartialProfile p(nullptr, f, false);
                    saveprofiles_[savefmt] = std::move(p);
                }
            } catch (Glib::Exception &exc) {
                if (settings->verbose) {
                    std::cout << "ERROR loading " << S(pth) << ": " << S(exc.what()) << std::endl;
                }
            }
        }
    } catch (Glib::Exception &exc) {
        if (settings->verbose) {
            std::cout << "ERROR scanning " << S(dirname) << ": " << S(exc.what()) << std::endl;
        }
    }

    if (settings->verbose) {
        std::cout << "Loaded " << loaders_.size() << " custom loaders"
                  << std::endl;
    }
}


Glib::ustring ImageIOManager::get_ext(Format f)
{
    switch (f) {
    case FMT_JPG: return ".jpg";
    case FMT_PNG: case FMT_PNG16: return ".png";
    default: return ".tif";
    }
}


bool ImageIOManager::load(const Glib::ustring &fileName, ProgressListener *plistener, ImageIO *&img, int maxw_hint, int maxh_hint)
{
    auto ext = std::string(getFileExtension(fileName).lowercase());
    auto it = loaders_.find(ext);
    if (it == loaders_.end()) {
        return false;
    }
    if (plistener) {
        plistener->setProgressStr("PROGRESSBAR_LOADING");
        plistener->setProgress(0.0);
    }

    std::string templ = Glib::build_filename(Glib::get_tmp_dir(), Glib::ustring::compose("ART-load-%1-XXXXXX", Glib::path_get_basename(fileName)));
    int fd = Glib::mkstemp(templ);
    if (fd < 0) {
        return false;
    }
    auto fmt = fmts_[ext];
    Glib::ustring outname = fname_to_utf8(templ) + get_ext(fmt);
    // int exit_status = -1;
    auto &dir = it->second.first;
    auto &cmd = it->second.second;
    std::vector<Glib::ustring> argv = subprocess::split_command_line(cmd);
    argv.push_back(fileName);
    argv.push_back(outname);
    argv.push_back(std::to_string(maxw_hint));
    argv.push_back(std::to_string(maxh_hint));
    std::string sout, serr;
    bool ok = true;
    if (settings->verbose) {
        std::cout << "loading " << fileName << " with " << cmd << std::endl;
    }
    try {
        exec_sync(usrdir_, sysdir_, dir, argv, true, &sout, &serr);
    } catch (subprocess::error &err) {
        if (settings->verbose) {
            std::cout << "  exec error: " << err.what() << std::endl;
        }
        ok = false;
    }
    close(fd);
    g_remove(templ.c_str());
    if (settings->verbose > 1) {
        if (!sout.empty()) {
            std::cout << "  stdout: " << sout << std::flush;
        }
        if (!serr.empty()) {
            std::cout << "  stderr: " << serr << std::flush;
        }
    }
    if (!ok) {
        if (Glib::file_test(outname, Glib::FILE_TEST_EXISTS)) {
            g_remove(outname.c_str());
        }
        return false;
    }

    IIOSampleFormat sFormat;
    IIOSampleArrangement sArrangement;
    bool err = false;

    switch (fmt) {
    case FMT_UNKNOWN:
        err = true;
        break;
    case FMT_JPG:
        sFormat = IIOSF_UNSIGNED_CHAR;
        sArrangement = IIOSA_CHUNKY;
        break;
    case FMT_PNG:
    case FMT_PNG16:
        err = ImageIO::getPNGSampleFormat(outname, sFormat, sArrangement) != IMIO_SUCCESS;
        break;
    default:
        err = ImageIO::getTIFFSampleFormat(outname, sFormat, sArrangement) != IMIO_SUCCESS;
        break;
    }
        
    if (err) {
        if (Glib::file_test(outname, Glib::FILE_TEST_EXISTS)) {
            g_remove(outname.c_str());
        }
        return false;
    }

    bool ret = true;
    ImageIO *fimg = nullptr;
    
    switch (sFormat) {
    case IIOSF_UNSIGNED_CHAR:
        fimg = new Image8();
        break;
    case IIOSF_UNSIGNED_SHORT:
        fimg = new Image16();
        break;
    case IIOSF_LOGLUV24:
    case IIOSF_LOGLUV32:
    case IIOSF_FLOAT16:
    case IIOSF_FLOAT24:
    case IIOSF_FLOAT32:
        fimg = new Imagefloat();
        break;
    default:
        ret = false;
    }

    if (ret) {
        fimg->setProgressListener(plistener);
        fimg->setSampleFormat(sFormat);
        fimg->setSampleArrangement(sArrangement);

        if (fimg->load(outname)) {
            delete fimg;
            ret = false;
        } else {
            img = fimg;
        }
    } else {
        if (fimg) {
            delete fimg;
        }
    }

    if (Glib::file_test(outname, Glib::FILE_TEST_EXISTS)) {
        g_remove(outname.c_str());
    }
    return ret;
}


bool ImageIOManager::save(IImagefloat *img, const std::string &ext, const Glib::ustring &fileName, ProgressListener *plistener)
{
    auto it = savers_.find(ext);
    if (it == savers_.end()) {
        return false;
    }
    if (plistener) {
        plistener->setProgressStr("PROGRESSBAR_SAVING");
        plistener->setProgress(0.0);
    }

    std::string templ = Glib::build_filename(Glib::get_tmp_dir(), Glib::ustring::compose("ART-save-%1-XXXXXX", Glib::path_get_basename(fileName)));
    int fd = Glib::mkstemp(templ);
    if (fd < 0) {
        return false;
    }
    auto fmt = fmts_[ext];
    Glib::ustring tmpname = fname_to_utf8(templ) + get_ext(fmt);

    bool ok = false;

    switch (fmt) {
    case FMT_UNKNOWN:
        ok = false;
        break;
    case FMT_JPG:
        ok = (img->saveAsJPEG(tmpname) == 0);
        break;
    case FMT_PNG:
        ok = (img->saveAsPNG(tmpname, 8, true) == 0);
        break;
    case FMT_PNG16:
        ok = (img->saveAsPNG(tmpname, 16, true) == 0);
        break;
    case FMT_TIFF:
        ok = (img->saveAsTIFF(tmpname, 16, false, true) == 0);
        break;
    case FMT_TIFF_FLOAT16:
        ok = (img->saveAsTIFF(tmpname, 16, true, true) == 0);
        break;
    case FMT_TIFF_FLOAT:
    default:
        ok = (img->saveAsTIFF(tmpname, 32, true, true) == 0);
        break;
    }

    if (plistener) {
        plistener->setProgress(0.5);
    }
        
    if (ok) {
        auto &dir = it->second.first;
        auto &cmd = it->second.second;
        std::vector<Glib::ustring> argv = subprocess::split_command_line(cmd);
        argv.push_back(tmpname);
        argv.push_back(fileName);
        std::string sout, serr;
        if (settings->verbose) {
            std::cout << "saving " << fileName << " with " << cmd << std::endl;
        }
        try {
            exec_sync(usrdir_, sysdir_, dir, argv, true, &sout, &serr);
        } catch (subprocess::error &err) {
            if (settings->verbose) {
                std::cout << "  exec error: " << err.what() << std::endl;
            }
            ok = false;
        }
        if (settings->verbose > 1) {
            if (!sout.empty()) {
                std::cout << "  stdout: " << sout << std::flush;
            }
            if (!serr.empty()) {
                std::cout << "  stderr: " << serr << std::flush;
            }
        }
    }
    
    if (plistener) {
        plistener->setProgress(1.0);
    }

    close(fd);
    g_remove(templ.c_str());
    if (Glib::file_test(tmpname, Glib::FILE_TEST_EXISTS)) {
        g_remove(tmpname.c_str());
    }

    return ok;
}


ImageIOManager::Format ImageIOManager::getFormat(const Glib::ustring &fname)
{
    auto ext = std::string(getFileExtension(fname).lowercase());
    auto it = fmts_.find(ext);
    if (it == fmts_.end()) {
        return FMT_UNKNOWN;
    } else {
        return it->second;
    }
}


std::vector<std::pair<std::string, ImageIOManager::SaveFormatInfo>> ImageIOManager::getSaveFormats() const
{
    std::vector<std::pair<std::string, ImageIOManager::SaveFormatInfo>> ret(savelbls_.begin(), savelbls_.end());
    return ret;
}


const procparams::PartialProfile *ImageIOManager::getSaveProfile(const std::string &ext) const
{
    auto it = saveprofiles_.find(ext);
    if (it != saveprofiles_.end()) {
        return &(it->second);
    }
    return nullptr;
}


bool ImageIOManager::loadRaw(const Glib::ustring &fname, const std::string &make, const std::string &model, Glib::ustring &out_dng_name)
{
    auto ext = std::string(getFileExtension(fname).lowercase());
    RawKey k(ext, make, model);
    auto it = raw_loaders_.lower_bound(k);
    if (it == raw_loaders_.end()) {
        return false;
    }
    auto &o = it->first;
    if (o.ext != k.ext) {
        return false;
    }
    if (!o.make.empty() && o.make != k.make) {
        return false;
    }
    if (!o.model.empty() && o.model != k.model) {
        return false;
    }
    return do_loadRaw(it->second, fname, out_dng_name);
}


bool ImageIOManager::do_loadRaw(const Pair &p, const Glib::ustring &fname, Glib::ustring &out_dng_name)
{
    std::string templ = Glib::build_filename(Glib::get_tmp_dir(), Glib::ustring::compose("ART-load_raw-%1-XXXXXX", Glib::path_get_basename(fname)));
    int fd = Glib::mkstemp(templ);
    if (fd < 0) {
        return false;
    }
    Glib::ustring outname = fname_to_utf8(templ) + ".dng";

    auto &dir = p.first;
    auto &cmd = p.second;
    std::vector<Glib::ustring> argv = subprocess::split_command_line(cmd);
    argv.push_back(fname);
    argv.push_back(outname);

    std::string sout, serr;
    bool ok = true;
    if (settings->verbose) {
        std::cout << "loading RAW " << fname << " with " << cmd << std::endl;
    }
    try {
        exec_sync(usrdir_, sysdir_, dir, argv, true, &sout, &serr);
    } catch (subprocess::error &err) {
        if (settings->verbose) {
            std::cout << "  exec error: " << err.what() << std::endl;
        }
        ok = false;
    }
    close(fd);
    g_remove(templ.c_str());
    if (settings->verbose > 1) {
        if (!sout.empty()) {
            std::cout << "  stdout: " << sout << std::flush;
        }
        if (!serr.empty()) {
            std::cout << "  stderr: " << serr << std::flush;
        }
    }
    if (!ok) {
        if (Glib::file_test(outname, Glib::FILE_TEST_EXISTS)) {
            g_remove(outname.c_str());
        }
        return false;
    }

    out_dng_name = outname;
    return true;
}


} // namespace rtengine
