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
#pragma once

#include <glibmm.h>
#include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;

#include "clutparams.h"
#include "cache.h"
#include "utils.h"
#include "subprocess.h"

namespace rtengine {

class ExternalLUT3D {
public:
    ExternalLUT3D();
    explicit ExternalLUT3D(const Glib::ustring &filename);
    bool init(const Glib::ustring &filename);
    CLUTParamDescriptorList get_param_descriptors() const { return params_; }
    bool set_param_values(const CLUTParamValueMap &values);
    OCIO::ConstCPUProcessorRcPtr get_processor() const { return proc_; }
    bool ok() const { return ok_; }
    Glib::ustring get_display_name() const { return gui_name_; }

    static void clear_cache();
    static void trim_cache();

    static void init();
    static void cleanup();
    
private:
    class SubprocessManager {
    public:
        ~SubprocessManager();
        bool process(const Glib::ustring &filename, const Glib::ustring &workdir, const std::vector<Glib::ustring> &argv, const std::string &params, const std::string &outname);

    private:
        std::unordered_map<std::string, std::unique_ptr<subprocess::SubprocessInfo>> procs_;
    };
    static std::unique_ptr<Cache<std::string, OCIO::ConstProcessorRcPtr>> cache_;
    static MyMutex disk_cache_mutex_;
    static SubprocessManager smgr_;

    std::string recompute_lut(const std::string &params);
    
    bool ok_;
    bool is_server_;
    Glib::ustring filename_;
    CLUTParamDescriptorList params_;
    OCIO::ConstCPUProcessorRcPtr proc_;
    Glib::ustring workdir_;
    std::vector<Glib::ustring> argv_;
    Glib::ustring gui_name_;
};

} // namespace rtengine
