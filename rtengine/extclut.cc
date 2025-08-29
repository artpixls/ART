/* -*- C++ -*-
 *
 *  This file is part of ART.
 *
 *  Copyright 2025 Alberto Griggio <alberto.griggio@gmail.com>
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
#include "extclut.h"
#include "cJSON.h"
#include "settings.h"
#include "compress.h"

#include "../rtgui/multilangmgr.h"
#include "../rtgui/pathutils.h"
#include "../rtgui/options.h"

#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <glib/gstdio.h>
#include <unistd.h>
#include <giomm.h>
#include <algorithm>
#include <locale.h>

namespace rtengine {

extern const Settings *settings;

namespace {

bool add_param(std::vector<CLUTParamDescriptor> &params, cJSON *elem)
{
    params.emplace_back();
    auto &p = params.back();
    static CLUTParamType pt[4] = {
        CLUTParamType::PT_INT,
        CLUTParamType::PT_FLOAT,
        CLUTParamType::PT_BOOL,
        CLUTParamType::PT_CURVE
    };
    for (int i = 0; i < 4; ++i) {
        p.type = pt[i];
        if (p.fill_from_json(elem)) {
            return true;
        }
    }
    return false;
}


std::string get_params_json(const std::vector<CLUTParamDescriptor> &params,
                            const CLUTParamValueMap &values)
{
    setlocale(LC_NUMERIC, "C");
    
    cJSON *root = cJSON_CreateObject();
    const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> del_root(root, cJSON_Delete);

    for (const auto &p : params) {
        auto it = values.find(p.name);
        const auto &vv = (it != values.end()) ? it->second : p.value_default;
        auto v = vv[0];
        
        cJSON *val = nullptr;
        switch (p.type) {
        case CLUTParamType::PT_FLOAT:
        case CLUTParamType::PT_INT:
        case CLUTParamType::PT_CHOICE:
            val = cJSON_CreateNumber(v);
            break;
        case CLUTParamType::PT_BOOL:
            val = cJSON_CreateBool(bool(v));
            break;
        default:
            return ""; // TODO
        }
        if (val) {
            cJSON_AddItemToObject(root, p.name.c_str(), val);
        } else {
            return "";
        }
    }

    char *data = cJSON_PrintUnformatted(root);
    std::string ret = data;
    free(data);
    return ret;
}


std::string generate_params(const std::vector<CLUTParamDescriptor> &params,
                            const CLUTParamValueMap &values)
{
    std::string fn = "";
    std::string json = get_params_json(params, values);
    if (!json.empty()) {
        fn = Glib::build_filename(Glib::get_tmp_dir(), "ART-extclut-params-XXXXXX");
        int fd = Glib::mkstemp(fn);
        if (fd < 0) {
            return "";
        }
        close(fd);

        FILE *out = g_fopen(fn.c_str(), "wb");
        fputs(json.c_str(), out);
        fclose(out);
    }
    
    return fn;
}


std::pair<std::string, std::string> get_cache_keys(const Glib::ustring &filename, const std::vector<CLUTParamDescriptor> &params, const CLUTParamValueMap &values)
{
    auto md5 = getMD5(filename, true);
    auto json = get_params_json(params, values);
    auto csum = Glib::Checksum::compute_checksum(Glib::Checksum::CHECKSUM_SHA256, Glib::filename_from_utf8(filename) + "\n" + md5 + "\n" + json);
    return std::make_pair(csum, csum + ".clfz");
}


std::string find_in_cache(MyMutex &mtx, const std::string &key)
{
    auto name = Glib::build_filename(options.cacheBaseDir, "extlut", key);
    if (Glib::file_test(name, Glib::FILE_TEST_EXISTS)) {
        std::string templ = Glib::build_filename(Glib::get_tmp_dir(), Glib::ustring::compose("ART-ocio-clf-%1-XXXXXX", key));
        int fd = Glib::mkstemp(templ);
        if (fd < 0) {
            return "";
        }
        close(fd);
        {
            MyMutex::MyLock lck(mtx);
            if (decompress_to(name, templ)) {
                if (settings->verbose > 1) {
                    std::cout << "extlut cache hit: " << key << std::endl;
                }
                return templ;
            }
        }
    }
    if (settings->verbose > 1) {
        std::cout << "extlut cache miss: " << key << std::endl;
    }
    return "";
}


void store_in_cache(MyMutex &mtx, const std::string &key, const std::string &fn)
{
    auto dir = Glib::build_filename(options.cacheBaseDir, "extlut");
    auto error = g_mkdir_with_parents(dir.c_str(), 0777);
    if (!error) {
        auto name = Glib::build_filename(dir, key);
        MyMutex::MyLock lck(mtx);
        if (compress_to(fn, name)) {
            if (settings->verbose > 1) {
                std::cout << "extlut cache store: " << key << std::endl;
            }
        }
    }
}


bool skip_line(subprocess::SubprocessInfo *p, std::vector<char> *buf)
{
    while (true) {
        int c = p->read();
        if (c < 0) {
            return false;
        } else {
            if (buf) {
                buf->push_back(c);
            }
            if (c == '\n') {
                return true;
            }
        }
    }
}

} // namespace


//-----------------------------------------------------------------------------
// ExternalLUT3D::SubprocessManager
//-----------------------------------------------------------------------------

ExternalLUT3D::SubprocessManager::~SubprocessManager()
{
    for (auto &p : procs_) {
        if (p.second->live()) {
            if (settings->verbose > 1) {
                std::cout << "extlut - terminating process with id: " << p.second->id() << ", key: " << p.first << std::endl;
            }
            p.second->kill();
        }
    }
}


bool ExternalLUT3D::SubprocessManager::process(const Glib::ustring &filename, const Glib::ustring &workdir, const std::vector<Glib::ustring> &argv, const std::string &params, const std::string &outname)
{
    auto key = getMD5(filename, true);
    auto it = procs_.find(key);
    subprocess::SubprocessInfo *p = nullptr;
    if (it == procs_.end() || !it->second->live()) {
        if (settings->verbose > 1) {
            std::cout << "extlut - executing server:";
            for (auto &a : argv) {
                std::cout << " " << a;
            }
            std::cout << "\nworkdir: " << workdir << std::endl;
        }
        auto pp = subprocess::popen(workdir, argv, true, true, true);
        if (!pp) {
            return false;
        }
        p = pp.get();
        procs_[key] = std::move(pp);
        if (settings->verbose > 1) {
            std::cout << "extlut - started server for filename: " << filename << ", key: " << key << ", id: " << p->id() << std::endl;
        }
    } else {
        p = it->second.get();
    }
    bool err = false;
    if (p) {
        if (!p->write(params.c_str(), params.size()) || !p->write("\n", 1) ||
            !p->write(outname.c_str(), outname.size()) || !p->write("\n", 1)) {
            err = true;
        }
        while (true) {
            int c = p->read();
            if (c < 0) {
                err = true;
                break;
            } else if (c == 'Y' || c == 'N') {
                bool success = (c == 'Y');
                std::vector<char> buf;
                while (true) {
                    c = p->read();
                    if (c < 0) {
                        err = true;
                        break;
                    } else if (c == '\n') {
                        buf.push_back(0);
                        break;
                    } else {
                        buf.push_back(c);
                    }
                }
                if (!err) {
                    int n = atoi(&buf[0]);
                    buf.clear();
                    while (n > 0) {
                        if (!skip_line(p, settings->verbose > 1 ? &buf : nullptr)) {
                            err = true;
                            break;
                        }
                        --n;
                    }
                    if (settings->verbose > 1) {
                        buf.push_back(0);
                        std::cout << "subprocess output:\n" << (&buf[0]) << std::endl;
                    }
                }
                if (!err) {
                    return success;
                }
            } else {
                if (settings->verbose) {
                    std::cout << "unexpected output from subprocess: " << char(c);
                    const char *dbg = g_getenv("ART_DEBUG_EXTCLUT");
                    if (dbg && atoi(dbg)) {
                        while ((c = p->read()) > 0) {
                            std::cout << char(c);
                        }
                    }
                    std::cout << std::endl;
                }
                err = true;
                break;
            }
        }
    } else {
        err = true;
    }

    if (err) {
        if (p) {
            p->kill();
        }
        procs_.erase(key);
    }
    
    return false;
}

//-----------------------------------------------------------------------------
// ExternalLUT3D
//-----------------------------------------------------------------------------


std::unique_ptr<Cache<std::string, OCIO::ConstProcessorRcPtr>> ExternalLUT3D::cache_;
MyMutex ExternalLUT3D::disk_cache_mutex_;
ExternalLUT3D::SubprocessManager ExternalLUT3D::smgr_;


void ExternalLUT3D::init()
{
    cache_.reset(new Cache<std::string, OCIO::ConstProcessorRcPtr>(options.clutCacheSize * 4));
}


void ExternalLUT3D::cleanup()
{
    cache_.reset(nullptr);
}


ExternalLUT3D::ExternalLUT3D():
    ok_(false),
    is_server_(false)
{
}


ExternalLUT3D::ExternalLUT3D(const Glib::ustring &filename):
    ok_(false),
    is_server_(false),
    filename_(filename)
{
    init(filename);
}


bool ExternalLUT3D::init(const Glib::ustring &filename)
{
    ok_ = false;
    is_server_ = false;
    filename_ = filename;
    
    const std::unique_ptr<FILE, std::function<void (FILE*)>> file(
        g_fopen(filename.c_str(), "rb"),
        [](FILE *file) { fclose(file); }
        );

    if (!file) {
        return false;
    }

    fseek(file.get(), 0, SEEK_END);
    const long length = ftell(file.get());
    if (length <= 0) {
        return false;
    }

    std::unique_ptr<char[]> buffer(new char[length + 1]);
    fseek(file.get(), 0, SEEK_SET);
    const size_t rd = fread(buffer.get(), 1, length, file.get());
    buffer[rd] = 0;

    setlocale(LC_NUMERIC, "C");
    cJSON_Minify(buffer.get());
    
    const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root_p(cJSON_Parse(buffer.get()), cJSON_Delete);
    cJSON *root = root_p.get();
    if (!root || !cJSON_IsObject(root)) {
        return false;
    }

    root = cJSON_GetObjectItem(root, "ART-lut3d");
    if (!root || !cJSON_IsObject(root)) {
        return false;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "command");
    if (!cmd || !cJSON_IsString(cmd)) {
        return false;
    }
    auto cmdline = cJSON_GetStringValue(cmd);
    workdir_ = Glib::path_get_dirname(filename);
    argv_ = subprocess::split_command_line(cmdline);

    cJSON *params = cJSON_GetObjectItem(root, "params");
    if (params) {
        if (!cJSON_IsArray(params)) {
            return false;
        }
        for (size_t i = 0, n = cJSON_GetArraySize(params); i < n; ++i) {
            if (!add_param(params_, cJSON_GetArrayItem(params, i))) {
                return false;
            }
        }
    }

    cJSON *label = cJSON_GetObjectItem(root, "label");
    if (label) {
        if (!cJSON_IsString(label)) {
            return false;
        }
        gui_name_ = cJSON_GetStringValue(label);
        if (!gui_name_.empty() && gui_name_[0] == '$') {
            auto pos = gui_name_.find(';');
            if (pos != Glib::ustring::npos) {
                auto key = gui_name_.substr(1, pos-1);
                auto dflt = gui_name_.substr(pos+1);
                auto res = M(key);
                gui_name_ = (res == key) ? dflt : res;
            } else {
                gui_name_ = M(gui_name_.c_str()+1);
            }
        }
    }

    cJSON *is_server = cJSON_GetObjectItem(root, "server");
    if (is_server) {
        if (!cJSON_IsBool(is_server)) {
            return false;
        }
        is_server_ = cJSON_IsTrue(is_server);
    }
    
    if (gui_name_.empty()) {
        gui_name_ = removeExtension(Glib::path_get_basename(filename_));
    }

    ok_ = true;
    return true;
}


bool ExternalLUT3D::set_param_values(const CLUTParamValueMap &values)
{
    if (!ok_) {
        return false;
    }
    bool success = true;
    OCIO::ConstProcessorRcPtr lut;
    std::pair<std::string, std::string> key = get_cache_keys(filename_, params_, values);
    bool found = cache_->get(key.first, lut);
    if (!found) {
        if (settings->verbose) {
            std::cout << "computing 3dlut for " << filename_ << std::endl;
        }
        std::string pn = generate_params(params_, values);
        std::string fn = find_in_cache(disk_cache_mutex_, key.second);
        if (fn.empty()) {
            fn = recompute_lut(pn);
            if (!fn.empty()) {
                store_in_cache(disk_cache_mutex_, key.second, fn);
            }
        }

        try {
            OCIO::ConstConfigRcPtr config = OCIO::Config::CreateRaw();
            OCIO::FileTransformRcPtr t = OCIO::FileTransform::Create();
            t->setSrc(fn.c_str());
            t->setInterpolation(OCIO::INTERP_BEST);
            lut = config->getProcessor(t);
            cache_->set(key.first, lut);
        } catch (...) {
            ok_ = false;
            success = false;
        }
        
        if (!pn.empty()) {
            g_remove(pn.c_str());
        }
        if (!fn.empty()) {
            g_remove(fn.c_str());
        }
    }
    if (lut) {
        try {
            proc_ = lut->getOptimizedCPUProcessor(OCIO::BIT_DEPTH_F32, 
                                                  OCIO::BIT_DEPTH_F32,
                                                  OCIO::OPTIMIZATION_DEFAULT);
        } catch (...) {
            ok_ = false;
            success = false;
        }
    } else {
        ok_ = success = false;
    }
    return success;
}


void ExternalLUT3D::trim_cache()
{
    MyMutex::MyLock lck(disk_cache_mutex_);
    
    size_t num_files = 0;
    const size_t max_num_files = std::min(size_t(options.clutCacheSize) * 100, options.maxCacheEntries);
    const auto dir_name = Glib::build_filename(options.cacheBaseDir, "extlut");
    const auto dir = Gio::File::create_for_path(dir_name);
    
    try {
        auto enumerator = dir->enumerate_children("");

        while (num_files <= max_num_files && enumerator->next_file()) {
            ++num_files;
        }
    } catch (Glib::Exception&) {}

    if (num_files <= max_num_files) {
        return;
    }

    using FNameMTime = std::pair<Glib::ustring, Glib::TimeVal>;
    std::vector<FNameMTime> files;

    try {
        auto enumerator = dir->enumerate_children("standard::name,time::modified");
        while (auto file = enumerator->next_file()) {
            files.emplace_back(file->get_name(), file->modification_time());
        }
    } catch (Glib::Exception&) {}

    if (files.size() <= max_num_files) {
        return;
    }

    std::sort(files.begin(), files.end(), [](const FNameMTime& lhs, const FNameMTime& rhs)
    {
        return lhs.second < rhs.second;
    });

    auto cache_entries = files.size();
    size_t num_removed = 0;
    for (auto entry = files.begin(); cache_entries-- > max_num_files; ++entry) {
        const auto &name = entry->first;
        auto pth = Glib::build_filename(dir_name, name);
        auto error = g_remove(pth.c_str());
        if (error && settings->verbose) {
            std::cerr << "extlut - error removing cache file: " << name << std::endl;
        } else {
            ++num_removed;
        }
    }

    if (settings->verbose > 1) {
        std::cout << "extlut - removed " << num_removed << " cache files" << std::endl;
    }
}


void ExternalLUT3D::clear_cache()
{
    MyMutex::MyLock lck(disk_cache_mutex_);

    try {
        auto dirname = Glib::build_filename(options.cacheBaseDir, "extlut");
        Glib::Dir dir(dirname);

        bool error = false;
        size_t num_removed = 0;
        for (auto entry = dir.begin(); entry != dir.end(); ++entry) {
            auto name = Glib::build_filename(dirname, *entry);
            if (g_remove(name.c_str()) != 0) {
                error = true;
            } else {
                ++num_removed;
            }
        }

        if (error && settings->verbose) {
            std::cerr << "extlut - failed to delete all entries in cache directory '" << dirname << "': " << g_strerror(errno) << std::endl;
        } else if (settings->verbose > 1) {
            std::cout << "extlut - removed " << num_removed << " cache files" << std::endl;
        }
    } catch (Glib::Error&) {}
}


std::string ExternalLUT3D::recompute_lut(const std::string &params)
{
    const auto &workdir = workdir_;
    const auto &argv = argv_;
    
    if (params.empty()) {
        return "";
    }
    
    std::string fn = Glib::build_filename(Glib::get_tmp_dir(), "ART-extclut-params-XXXXXX");
    int fd = Glib::mkstemp(fn);
    if (fd < 0) {
        return "";
    }
    close(fd);

    try {
        if (is_server_) {
            if (settings->verbose > 1) {
                std::cout << "extlut - executing server for " << filename_ << ": " << params << " " << fn << std::endl;
            }
            MyMutex::MyLock lck(disk_cache_mutex_);
            if (!smgr_.process(filename_, workdir_, argv_, params, fn)) {
                if (settings->verbose) {
                    std::cout << "extlut - exec server error for " << filename_ << std::endl;
                }
                if (!fn.empty()) {
                    g_remove(fn.c_str());
                }
                fn = "";
            }
        } else {
            std::vector<Glib::ustring> args = argv;
            args.push_back(params);
            args.push_back(fn);
            std::string sout, serr;
            if (settings->verbose > 1) {
                std::cout << "extlut - executing:";
                for (auto &a : args) {
                    std::cout << " " << a;
                }
                std::cout << "\nworkdir: " << workdir << std::endl;
            }
            subprocess::exec_sync(workdir, args, true, &sout, &serr);
            if (settings->verbose > 1) {
                std::cout << "  stdout: " << sout << "\n  stderr: " << serr << std::endl;
            }
        }
    } catch (subprocess::error &err) {
        if (settings->verbose) {
            std::cout << "  exec error: " << err.what() << std::endl;
        }
        if (!fn.empty()) {
            g_remove(fn.c_str());
        }
        fn = "";
    }

    return fn;
}

} // namespace rtengine
