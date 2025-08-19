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
#include <fftw3.h>
#include "../rtgui/profilestorecombobox.h"
#include "rtengine.h"
#include "iccstore.h"
#include "dcp.h"
#include "camconst.h"
#include "curves.h"
#include "rawimagesource.h"
#include "improcfun.h"
#include "improccoordinator.h"
#include "dfmanager.h"
#include "ffmanager.h"
#include "rtthumbnail.h"
#include "profilestore.h"
#include "../rtgui/threadutils.h"
#include "rtlensfun.h"
#include "metadata.h"
#include "imgiomanager.h"
#include "threadpool.h"

#ifdef _OPENMP
# include <omp.h>
#endif

namespace rtengine {

std::unique_ptr<ThreadPool> ThreadPool::instance_;


const Settings* settings;

MyMutex *lcmsMutex = nullptr;
MyMutex *fftwMutex = nullptr;
MyMutex *librawMutex = nullptr;

int init (const Settings* s, Glib::ustring baseDir, Glib::ustring userSettingsDir, bool loadAll)
{
    settings = s;
    ProcParams::init();
    PerceptualToneCurve::init();
    RawImageSource::init();

    int num_threads = settings->thread_pool_size;
    if (num_threads <= 0) {
        num_threads = 1;
#ifdef _OPENMP
        num_threads = std::max(omp_get_num_procs()-1, num_threads);
#endif
    }
    ThreadPool::init(num_threads);

#ifdef _OPENMP
#pragma omp parallel sections if (!settings->verbose)
#endif
{
#ifdef _OPENMP
#pragma omp section
#endif
{
    if (s->lensfunDbDirectory.empty()) {
        if (!LFDatabase::init(s->lensfunDbDirectory)) {
            LFDatabase::init(Glib::build_filename(baseDir, "share", "lensfun"));
        }
    } else if (Glib::path_is_absolute(s->lensfunDbDirectory)) {
        LFDatabase::init(s->lensfunDbDirectory);
    } else {
        LFDatabase::init(Glib::build_filename(baseDir, s->lensfunDbDirectory));
    }
}
#ifdef _OPENMP
#pragma omp section
#endif
{
    ProfileStore::getInstance()->init(loadAll);
}
#ifdef _OPENMP
#pragma omp section
#endif
{
    ICCStore::getInstance()->init(s->iccDirectory, Glib::build_filename (baseDir, "iccprofiles"), loadAll);
}
#ifdef _OPENMP
#pragma omp section
#endif
{
    DCPStore::getInstance()->init(Glib::build_filename (baseDir, "dcpprofiles"), loadAll);
}
#ifdef _OPENMP
#pragma omp section
#endif
{
    CameraConstantsStore::getInstance()->init(baseDir, userSettingsDir);
}
#ifdef _OPENMP
#pragma omp section
#endif
{
    dfm.init(s->darkFramesPath);
}
#ifdef _OPENMP
#pragma omp section
#endif
{
    ffm.init(s->flatFieldsPath);
}
}

    Color::init ();
    Exiv2Metadata::init(baseDir, userSettingsDir);

    DynamicProfileRules::init(baseDir);
    ImageIOManager::getInstance()->init(baseDir, userSettingsDir);
    
    delete lcmsMutex;
    lcmsMutex = new MyMutex;
    fftwMutex = new MyMutex;
#ifdef ART_USE_LIBRAW
    librawMutex = new MyMutex;
#endif

    return 0;
}

void cleanup ()
{
    Exiv2Metadata::cleanup();
    ProcParams::cleanup ();
    Color::cleanup ();
    RawImageSource::cleanup ();

#ifdef RT_FFTW3F_OMP
    fftwf_cleanup_threads();
#else
    fftwf_cleanup();
#endif

}

StagedImageProcessor* StagedImageProcessor::create (InitialImage* initialImage)
{

    ImProcCoordinator* ipc = new ImProcCoordinator ();
    ipc->assign (initialImage->getImageSource ());
    return ipc;
}

void StagedImageProcessor::destroy (StagedImageProcessor* sip)
{

    delete sip;
}


Settings::Settings():
    printerBPC(false),
    monitorIntent(RI_RELATIVE),
    monitorBPC(false),
    autoMonitorProfile(false),
    verbose(0),
    HistogramWorking(false),
    thumbnail_inspector_mode(ThumbnailInspectorMode::JPEG),
    thumbnail_inspector_raw_curve(ThumbnailInspectorRawCurve::LINEAR),
    xmp_sidecar_style(XmpSidecarStyle::STD),
    metadata_xmp_sync(MetadataXmpSync::NONE),
    thread_pool_size(0),
    ctl_scripts_fast_preview(false),
    os_monitor_profile(StdMonitorProfile::SRGB),
    imgio_raw_cache_size(10)
{
}

Settings::ColorManagementMode Settings::color_mgmt_mode(Settings::ColorManagementMode::APPLICATION);

} // namespace rtengine


