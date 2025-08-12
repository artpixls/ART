/*
 *  This file is part of RawTherapee.
 *
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
#include "multilangmgr.h"
#include "thumbnail.h"
#include <sstream>
#include <iomanip>
#include "options.h"
#include "../rtengine/mytime.h"
#include <cstdio>
#include <cstdlib>
#include <glibmm.h>
#include "../rtengine/imagedata.h"
#include <glib/gstdio.h>

#include "../rtengine/dynamicprofile.h"
#include "guiutils.h"
#include "batchqueue.h"
#include "extprog.h"
#include "profilestorecombobox.h"
#include "procparamchangers.h"
#include "ppversion.h"
#include "version.h"
#include "../rtengine/metadata.h"
#include "thumbimgcache.h"

using namespace rtengine::procparams;

Thumbnail::Thumbnail(CacheManager* cm, const Glib::ustring& fname, CacheImageData* cf)
    : fname(fname), cfs(*cf), cachemgr(cm), ref(1), enqueueNumber(0), tpp(nullptr),
      pparamsValid(false), needsReProcessing(true), imageLoading(false), lastImg(nullptr),
      lastW(0), lastH(0), lastScale(0), initial_(false), first_process_(true)
{
    loadProcParams(false);

    // should be safe to use the unprotected version of loadThumbnail, since we are in the constructor
    _loadThumbnail(true, options.thumb_lazy_caching);
    generateExifDateTimeStrings();

    loadRating();

    delete tpp;
    tpp = nullptr;
}

Thumbnail::Thumbnail (CacheManager* cm, const Glib::ustring& fname, const std::string& md5)
    : fname(fname), cachemgr(cm), ref(1), enqueueNumber(0), tpp(nullptr), pparamsValid(false),
      needsReProcessing(true), imageLoading(false), lastImg(nullptr),
      lastW(0), lastH(0), lastScale(0.0), initial_(true), first_process_(true)
{


    cfs.md5 = md5;
    loadProcParams ();
    //initial_ = !pparamsValid;
    _generateThumbnailImage(true, options.thumb_lazy_caching);
    cfs.recentlySaved = false;

    initial_ = false;
    // if (cfs.thumbImgType == CacheImageData::QUICK_THUMBNAIL && pparamsValid) {
    //     cfs.thumbImgType = CacheImageData::FULL_THUMBNAIL;
    // }

    delete tpp;
    tpp = nullptr;
}

void Thumbnail::_generateThumbnailImage(bool save_in_cache, bool info_only)
{
    //  delete everything loaded into memory
    delete tpp;
    tpp = nullptr;
    delete [] lastImg;
    lastImg = nullptr;
    tw = options.maxThumbnailWidth;
    th = options.maxThumbnailHeight;
    imgRatio = -1.;

    // generate thumbnail image
    Glib::ustring ext = getExtension (fname);

    if (ext == "") {
        return;
    }

    cfs.supported = false;
    cfs.exifValid = false;
    cfs.timeValid = false;

    first_process_ = true;

    if (ext.lowercase() == "jpg" || ext.lowercase() == "jpeg") {
        infoFromImage(fname);
        tpp = rtengine::Thumbnail::loadFromImage (fname, tw, th, -1, pparams.master.wb.equal);

        if (tpp) {
            cfs.format = FT_Jpeg;
        }
    } else if (ext.lowercase() == "png") {
        infoFromImage(fname);
        tpp = rtengine::Thumbnail::loadFromImage (fname, tw, th, -1, pparams.master.wb.equal);

        if (tpp) {
            cfs.format = FT_Png;
        }
    } else {
        // RAW works like this:
        //  1. if we are here it's because we aren't in the cache so load the JPG
        //     image out of the RAW. Mark as "quick".
        //  2. if we don't find that then just grab the real image.
        bool quick = false;

        rtengine::eSensorType sensorType = rtengine::ST_NONE;
        if ( initial_ && options.internalThumbIfUntouched) {
            quick = true;
            tpp = rtengine::Thumbnail::loadQuickFromRaw (fname, sensorType, tw, th, 1, TRUE);
        }

        if ( tpp == nullptr ) {
            quick = false;
            if (!info_only) {
                tpp = rtengine::Thumbnail::loadFromRaw (fname, sensorType, tw, th, 1, pparams.master.wb.equal, TRUE);
            } else {
                tpp = rtengine::Thumbnail::loadInfoFromRaw(fname, sensorType, tw, th, 1);
                save_in_cache = false;
            }
        }

        cfs.sensortype = sensorType;
        if (tpp) {
            cfs.format = FT_Raw;
            cfs.thumbImgType = quick ? CacheImageData::QUICK_THUMBNAIL : CacheImageData::FULL_THUMBNAIL;
            infoFromImage (fname);
            if (!quick) {
                cfs.width = tpp->full_width;
                cfs.height = tpp->full_height;
            }
        }
    }

    if (!tpp && (ext.lowercase() == "tif" || ext.lowercase() == "tiff")) {
        infoFromImage (fname);
        tpp = rtengine::Thumbnail::loadFromImage (fname, tw, th, -1, pparams.master.wb.equal);

        if (tpp) {
            cfs.format = FT_Tiff;
        }
    }
    
    if (!tpp) {
        // try a custom loader
        tpp = rtengine::Thumbnail::loadFromImage(fname, tw, th, -1, pparams.master.wb.equal);
        if (tpp) {
            cfs.format = FT_Custom;
            infoFromImage(fname);
        }
    }

    if (tpp) {
        tpp->getAutoWBMultipliers(cfs.redAWBMul, cfs.greenAWBMul, cfs.blueAWBMul);
        if (save_in_cache) {
            _saveThumbnail();
        }
        cfs.supported = true;
        needsReProcessing = true;

        if (save_in_cache) {
            cfs.save(getCacheFileName("data", ".txt"));
        }

        generateExifDateTimeStrings ();
    }
}

bool Thumbnail::isSupported ()
{
    return cfs.supported;
}

const ProcParams& Thumbnail::getProcParams ()
{
    MyMutex::MyLock lock(mutex);
    return getProcParamsU();
}

// Unprotected version of getProcParams, when
const ProcParams& Thumbnail::getProcParamsU ()
{
    if (pparamsValid) {
        return pparams.master;
    } else {
        auto pp = ProfileStore::getInstance()->getDefaultPartialProfile(getType() == FT_Raw);
        pp->applyTo(pparams.master);
        //pparams = *(ProfileStore::getInstance()->getDefaultProcParams (getType() == FT_Raw));

        // if (pparams.master.wb.method == WBParams::CAMERA) {
        //     double ct;
        //     getCamWB (ct, pparams.master.wb.green);
        //     pparams.master.wb.temperature = ct;
        // } else if (pparams.master.wb.method == WBParams::AUTO) {
        //     double ct;
        //     getAutoWB(ct, pparams.master.wb.green, pparams.master.wb.equal);
        //     pparams.master.wb.temperature = ct;
        // }
    }

    return pparams.master; // there is no valid pp to return, but we have to return something
}


namespace {

bool CPBDump(const Glib::ustring &commFName, const Glib::ustring &imageFName,
             const Glib::ustring &profileFName, const Glib::ustring &defaultPParams,
             const CacheImageData* cfs, const bool flagMode)
{
    const auto kf = new Glib::KeyFile;

    if (!kf) {
        return false;
    }

    FILE *f = nullptr;

    // open the file in write mode
    f = g_fopen (commFName.c_str (), "wt");

    if (f == nullptr) {
        printf ("CPBDump(\"%s\") >>> Error: unable to open file with write access!\n", commFName.c_str());
        delete kf;
        return false;
    }

    try {

        kf->set_string ("RT General", "CachePath", options.cacheBaseDir);
        kf->set_string ("RT General", "AppVersion", RTVERSION);
        kf->set_integer ("RT General", "ProcParamsVersion", PPVERSION);
        kf->set_string ("RT General", "ImageFileName", imageFName);
        kf->set_string ("RT General", "OutputProfileFileName", profileFName);
        kf->set_string ("RT General", "DefaultProcParams", defaultPParams);
        kf->set_boolean ("RT General", "FlaggingMode", flagMode);

        kf->set_integer ("Common Data", "FrameCount", cfs->frameCount);
        kf->set_integer ("Common Data", "SampleFormat", cfs->sampleFormat);
        kf->set_boolean ("Common Data", "IsHDR", cfs->isHDR);
        kf->set_boolean ("Common Data", "IsPixelShift", cfs->isPixelShift);
        kf->set_double ("Common Data", "FNumber", cfs->fnumber);
        kf->set_double ("Common Data", "Shutter", cfs->shutter);
        kf->set_double ("Common Data", "FocalLength", cfs->focalLen);
        kf->set_integer ("Common Data", "ISO", cfs->iso);
        kf->set_string ("Common Data", "Lens", cfs->lens);
        kf->set_string ("Common Data", "Make", cfs->camMake);
        kf->set_string ("Common Data", "Model", cfs->camModel);

    } catch (Glib::KeyFileError&) {}

    try {
        fprintf (f, "%s", kf->to_data().c_str());
    } catch (Glib::KeyFileError&) {}

    fclose (f);
    delete kf;

    return true;
}


} // namespace

/** @brief  Create default params on demand and returns a new updatable object
 *
 *  The loaded profile may be partial, but it return a complete ProcParams (i.e. without ParamsEdited)
 *
 *  @param returnParams Ask to return a pointer to a ProcParams object if true
 *  @param force True if the profile has to be re-generated even if it already exists
 *  @param flaggingMode True if the ProcParams will be created because the file browser is being flagging an image
 *                      (rang, to trash, color labels). This parameter is passed to the CPB.
 *
 *  @return Return a pointer to a ProcPamas structure to be updated if returnParams is true and if everything went fine, NULL otherwise.
 */
rtengine::procparams::ProcParams* Thumbnail::createProcParamsForUpdate(bool returnParams, bool force, bool flaggingMode)
{

    static int index = 0; // Will act as unique identifier during the session

    // try to load the last saved parameters from the cache or from the paramfile file
    ProcParams* ldprof = nullptr;

    Glib::ustring defProf = getType() == FT_Raw ? options.defProfRaw : options.defProfImg;

    const CacheImageData* cfs = getCacheImageData();
    Glib::ustring defaultPparamsPath = options.findProfilePath(defProf);
    const bool create = (!hasProcParams() || force);
    const bool run_cpb = !options.CPBPath.empty() && !defaultPparamsPath.empty() && cfs && cfs->exifValid && create;

    const Glib::ustring outFName =
        (options.paramsLoadLocation == PLL_Input && options.saveParamsFile) ?
        options.getParamFile(fname) :
        getCacheFileName("profiles", paramFileExtension);

    if (!run_cpb) {
        if (defProf == Options::DEFPROFILE_DYNAMIC && create && cfs && cfs->exifValid) {
            auto imageMetaData = getMetaData();
            auto pp = ProfileStore::getInstance()->loadDynamicProfile(imageMetaData.get());
            ProcParams params;
            if (pp->applyTo(params) && params.save(cachemgr->getProgressListener(), outFName) == 0) {
                loadProcParams();
            }
        } else if (create && defProf != Options::DEFPROFILE_DYNAMIC) {
            const PartialProfile *p = ProfileStore::getInstance()->getProfile(defProf);
            ProcParams params;
            if (p && p->applyTo(params) && params.save(cachemgr->getProgressListener(), outFName) == 0) {
                loadProcParams();
            }
        }
    } else {
        // First generate the communication file, with general values and EXIF metadata
        Glib::ustring tmpFileName( Glib::build_filename(options.cacheBaseDir, Glib::ustring::compose("CPB_temp_%1.txt", index++)) );

        CPBDump(tmpFileName, fname, outFName,
                defaultPparamsPath == Options::DEFPROFILE_INTERNAL ? Options::DEFPROFILE_INTERNAL : Glib::build_filename(defaultPparamsPath, Glib::path_get_basename(defProf) + paramFileExtension), cfs, flaggingMode);
        
        // For the filename etc. do NOT use streams, since they are not UTF8 safe
        Glib::ustring cmdLine = options.CPBPath + Glib::ustring(" \"") + tmpFileName + Glib::ustring("\"");

        if (options.rtSettings.verbose > 1) {
            printf("Custom profile builder's command line: %s\n", Glib::ustring(cmdLine).c_str());
        }

        bool success = ExtProg::spawnCommandSync(cmdLine);

        // Now they SHOULD be there (and potentially "partial"), so try to load them and store it as a full procparam
        if (success) {
            loadProcParams();
        }

        g_remove (tmpFileName.c_str ());
    }

    if (returnParams && hasProcParams()) {
        ldprof = new ProcParams ();
        *ldprof = getProcParams ();
    }

    return ldprof;
}

void Thumbnail::notifylisterners_procParamsChanged(int whoChangedIt)
{
    for (size_t i = 0; i < listeners.size(); i++) {
        listeners[i]->procParamsChanged (this, whoChangedIt);
    }
}

/*
 * Load the procparams from the cache or from the sidecar file (priority set in
 * the Preferences).
 *
 * The result is a complete ProcParams with default values merged with the values
 * from the default Raw or Image ProcParams, then with the values from the loaded
 * ProcParams (sidecar or cache file).
 */
void Thumbnail::loadProcParams(bool load_rating)
{
    MyMutex::MyLock lock(mutex);

    pparamsValid = false;
    pparams.master.setDefaults();
    const PartialProfile *defaultPP = ProfileStore::getInstance()->getDefaultPartialProfile(getType() == FT_Raw);
    defaultPP->applyTo(pparams.master);

    if (options.paramsLoadLocation == PLL_Input) {
        // try to load it from params file next to the image file
        int ppres = pparams.load(cachemgr->getProgressListener(), options.getParamFile(fname));
        pparamsValid = !ppres && pparams.master.ppVersion >= 220;

        // if no success, try to load the cached version of the procparams
        if (!pparamsValid) {
            pparamsValid = !pparams.load(cachemgr->getProgressListener(), getCacheFileName ("profiles", paramFileExtension));
        }
    } else {
        // try to load it from cache
        pparamsValid = !pparams.load(cachemgr->getProgressListener(), getCacheFileName ("profiles", paramFileExtension));

        // if no success, try to load it from params file next to the image file
        if (!pparamsValid) {
            int ppres = pparams.load(cachemgr->getProgressListener(), options.getParamFile(fname));
            pparamsValid = !ppres && pparams.master.ppVersion >= 220;
        }
    }

    if (load_rating) {
        loadRating();
    }
}

void Thumbnail::clearProcParams (int whoClearedIt)
{

    /*  Clarification on current "clear profile" functionality:
        a. if rank/colorlabel/inTrash are NOT set,
        the "clear profile" will delete the pp3 file (as before).

        b. if any of the rank/colorlabel/inTrash ARE set,
        the "clear profile" will lead to execution of ProcParams::setDefaults
        (the CPB is NOT called) to set the params values and will preserve
        rank/colorlabel/inTrash in the param file. */

    {
        MyMutex::MyLock lock(mutex);

        // this preserves rank, colorlabel and inTrash across clear
        // (nothing do to)
        cfs.recentlySaved = false;
        pparamsValid = false;
        needsReProcessing = true;

        //TODO: run though customprofilebuilder?
        // probably not as this is the only option to set param values to default

        // reset the params to defaults
        pparams.master.setDefaults();
        pparams.snapshots.clear();

        // and restore rank and inTrash
        if (options.thumbnail_rating_mode == Options::ThumbnailRatingMode::PROCPARAMS) {
            saveRating();
        }

        // params could get validated by rank/inTrash values restored above
        if (pparamsValid) {
            updateCache();
        } else {
            // remove param file from cache
            Glib::ustring fname_ = getCacheFileName ("profiles", paramFileExtension);
            g_remove (fname_.c_str ());

            // remove param file located next to the file
            // fname_ = fname + paramFileExtension;
            // g_remove (fname_.c_str ());

            // fname_ = removeExtension(fname) + paramFileExtension;
            fname_ = options.getParamFile(fname);
            g_remove (fname_.c_str ());

            if (cfs.format == FT_Raw && options.internalThumbIfUntouched && cfs.thumbImgType != CacheImageData::QUICK_THUMBNAIL) {
                // regenerate thumbnail, ie load the quick thumb again. For the rare formats not supporting quick thumbs this will
                // be a bit slow as a new full thumbnail will be generated unnecessarily, but currently there is no way to pre-check
                // if the format supports quick thumbs.
                initial_ = true;
                _generateThumbnailImage();
                initial_ = false;
            }
        }

    } // end of mutex lock

    for (size_t i = 0; i < listeners.size(); i++) {
        listeners[i]->procParamsChanged (this, whoClearedIt);
    }
}

bool Thumbnail::hasProcParams ()
{

    return pparamsValid;
}

void Thumbnail::setProcParams(const PartialProfile &pp, int whoChangedIt, bool updateCacheNow, bool resetToDefault)
{
    {
        MyMutex::MyLock lock(mutex);
        ProcParams tmp = pparams.master;
        pp.applyTo(pparams.master);

        if (pparams.master != tmp) {
            cfs.recentlySaved = false;
        } else if (pparamsValid && !updateCacheNow) {
            // nothing to do
            return;
        }

        // do not update rank, colorlabel and inTrash
        pparamsValid = true;
        if (options.thumbnail_rating_mode == Options::ThumbnailRatingMode::PROCPARAMS) {
            saveRating();
        }

        if (updateCacheNow) {
            updateCache();
        }
    } // end of mutex lock

    for (size_t i = 0; i < listeners.size(); i++) {
        listeners[i]->procParamsChanged (this, whoChangedIt);
    }
}


void Thumbnail::setProcParams(const ProcParams &pp, int whoChangedIt, bool updateCacheNow, bool resetToDefault)
{
    setProcParams(FullPartialProfile(pp), whoChangedIt, updateCacheNow, resetToDefault);
}


bool Thumbnail::isRecentlySaved ()
{

    return cfs.recentlySaved;
}

void Thumbnail::imageDeveloped ()
{

    cfs.recentlySaved = true;
    cfs.save (getCacheFileName ("data", ".txt"));

    if (options.saveParamsCache) {
        pparams.save (cachemgr->getProgressListener(), getCacheFileName ("profiles", paramFileExtension));
    }
}

void Thumbnail::imageEnqueued ()
{

    enqueueNumber++;
}

void Thumbnail::imageRemovedFromQueue ()
{

    enqueueNumber--;
}

bool Thumbnail::isEnqueued ()
{

    return enqueueNumber > 0;
}

bool Thumbnail::isPixelShift ()
{
    return cfs.isPixelShift;
}
bool Thumbnail::isHDR ()
{
    return cfs.isHDR;
}

void Thumbnail::increaseRef ()
{
    MyMutex::MyLock lock(mutex);
    ++ref;
}

void Thumbnail::decreaseRef ()
{
    {
        MyMutex::MyLock lock(mutex);

        if ( ref == 0 ) {
            return;
        }

        if ( --ref != 0 ) {
            return;
        }
    }
    cachemgr->closeThumbnail (this);
}

void Thumbnail::getThumbnailSize (int &w, int &h, const rtengine::procparams::ProcParams *pparams)
{
    MyMutex::MyLock lock(mutex);

    int tw_ = tw;
    int th_ = th;
    float imgRatio_ = imgRatio;

    if (pparams) {
        int ppCoarse = pparams->coarse.rotate;

        if (ppCoarse >= 180) {
            ppCoarse -= 180;
        }

        int thisCoarse = this->pparams.master.coarse.rotate;

        if (thisCoarse >= 180) {
            thisCoarse -= 180;
        }

        if (thisCoarse != ppCoarse) {
            // different orientation -> swapping width & height
            int tmp = th_;
            th_ = tw_;
            tw_ = tmp;

            if (imgRatio_ >= 0.0001f) {
                imgRatio_ = 1.f / imgRatio_;
            }
        }
    }

    if (imgRatio_ > 0.) {
        w = (int)(imgRatio_ * (float)h);
    } else {
        w = tw_ * h / th_;
    }

    if (w > options.maxThumbnailWidth) {
        float s = float(options.maxThumbnailWidth)/w;
        w = options.maxThumbnailWidth;
        h = std::max(int(h * s), 1);
    }
}

void Thumbnail::getFinalSize (const rtengine::procparams::ProcParams& pparams, int& w, int& h)
{
    MyMutex::MyLock lock(mutex);

    // WARNING: When downscaled, the ratio have loosed a lot of precision, so we can't get back the exact initial dimensions
    double fw = lastW * lastScale;
    double fh = lastH * lastScale;

    if (pparams.coarse.rotate == 90 || pparams.coarse.rotate == 270) {
        fh = lastW * lastScale;
        fw = lastH * lastScale;
    }

    if (!pparams.resize.enabled) {
        w = fw;
        h = fh;
    } else {
        w = (int)(fw + 0.5);
        h = (int)(fh + 0.5);
    }
}

void Thumbnail::getOriginalSize(int &w, int &h, bool consider_coarse)
{
    if (cfs.width < 0) {
        try {
            rtengine::Exiv2Metadata meta(fname);
            meta.load();
            meta.getDimensions(cfs.width, cfs.height);
        } catch (std::exception &) {}
    }
    w = cfs.width;
    h = cfs.height;
    if (consider_coarse && pparamsValid) {
        if (pparams.master.coarse.rotate == 90 || pparams.master.coarse.rotate == 270) {
            std::swap(w, h);
        }
    }
}

rtengine::IImage8* Thumbnail::processThumbImage (const rtengine::procparams::ProcParams& pparams, int h, double& scale)
{

    MyMutex::MyLock lock(mutex);

    if ( tpp == nullptr ) {
        _loadThumbnail();

        if ( tpp == nullptr ) {
            return nullptr;
        } else if (options.thumb_lazy_caching) {
            _saveThumbnail();
        }
    }

    rtengine::IImage8* image = nullptr;

    if ( cfs.thumbImgType == CacheImageData::QUICK_THUMBNAIL ) {
        // RAW internal thumbnail, no profile yet: just do some rotation etc.
        image = tpp->quickProcessImage (pparams, h, rtengine::TI_Nearest);
    } else {
        auto fn = getCacheFileName("images", "");
        if (first_process_) {
            image = art::thumbimgcache::load(fn, pparams, h);
            if (!image) {
                first_process_ = false;
            }
        }
        if (!image) {
            if (options.rtSettings.verbose) {
                std::cout << "full thumb processing: " << fname << std::endl;
            }
            // Full thumbnail: apply profile
            image = tpp->processImage(pparams, static_cast<rtengine::eSensorType>(cfs.sensortype), h, rtengine::TI_Bilinear, &cfs, scale );
            art::thumbimgcache::store(fn, pparams, image);
        } else if (options.rtSettings.verbose) {
            std::cout << "cached thumb image: " << fname << std::endl;
        }
    }

    tpp->getDimensions(lastW, lastH, lastScale);

    delete tpp;
    tpp = nullptr;
    return image;
}

rtengine::IImage8* Thumbnail::upgradeThumbImage (const rtengine::procparams::ProcParams& pparams, int h, double& scale)
{

    MyMutex::MyLock lock(mutex);

    if ( cfs.thumbImgType != CacheImageData::QUICK_THUMBNAIL ) {
        return nullptr;
    }

    _generateThumbnailImage();

    if ( tpp == nullptr ) {
        return nullptr;
    }

    // rtengine::IImage8* image = tpp->processImage (pparams, h, rtengine::TI_Bilinear, cfs.getCamera(), cfs.focalLen, cfs.focalLen35mm, cfs.focusDist, cfs.shutter, cfs.fnumber, cfs.iso, cfs.expcomp,  scale );
    rtengine::IImage8* image = tpp->processImage (pparams, static_cast<rtengine::eSensorType>(cfs.sensortype), h, rtengine::TI_Bilinear, &cfs, scale );
    tpp->getDimensions(lastW, lastH, lastScale);
    art::thumbimgcache::store(getCacheFileName("images", ""), pparams, image);

    delete tpp;
    tpp = nullptr;
    return image;
}

void Thumbnail::generateExifDateTimeStrings ()
{
    exifString = "";
    dateTimeString = "";

    if (!cfs.exifValid) {
        return;
    }

    exifString = Glib::ustring::compose ("f/%1 %2s %3%4 %5mm", Glib::ustring(rtengine::FramesData::apertureToString(cfs.fnumber)), Glib::ustring(rtengine::FramesData::shutterToString(cfs.shutter)), M("QINFO_ISO"), cfs.iso, Glib::ustring::format(std::setw(3), std::fixed, std::setprecision(2), cfs.focalLen));

    if (options.fbShowExpComp && cfs.expcomp != "0.00" && cfs.expcomp != "") {
        exifString = Glib::ustring::compose ("%1 %2EV", exifString, cfs.expcomp);
    }

    std::ostringstream ostr;

    if (g_date_valid_dmy(int(cfs.day), GDateMonth(cfs.month), cfs.year)) {
        Glib::Date d(cfs.day, Glib::Date::Month(cfs.month), cfs.year);
        ostr << std::string(d.format_string(options.dateFormat));    
        ostr << " " << std::setw(2) << std::setfill('0') << int(cfs.hour);
        ostr << ":" << std::setw(2) << std::setfill('0') << int(cfs.min);
        ostr << ":" << std::setw(2) << std::setfill('0') << int(cfs.sec);
    }
    dateTimeString = ostr.str();
}

const Glib::ustring& Thumbnail::getExifString()
{
    return exifString;
}

const Glib::ustring& Thumbnail::getDateTimeString ()
{

    return dateTimeString;
}

// void Thumbnail::getAutoWB (double& temp, double& green, double equal)
// {
//     if (cfs.redAWBMul != -1.0) {
//         rtengine::ColorTemp ct(cfs.redAWBMul, cfs.greenAWBMul, cfs.blueAWBMul, equal);
//         temp = ct.getTemp();
//         green = ct.getGreen();
//     } else {
//         temp = green = -1.0;
//     }
// }


ThFileType Thumbnail::getType ()
{

    return (ThFileType) cfs.format;
}

int Thumbnail::infoFromImage (const Glib::ustring& fname)
{
    rtengine::FramesMetaData* idata = rtengine::FramesMetaData::fromFile (fname);

    if (!idata) {
        return 0;
    }

    int deg = 0;
    cfs.timeValid = false;
    cfs.exifValid = false;

    if (idata->hasExif()) {
        cfs.shutter      = idata->getShutterSpeed ();
        cfs.fnumber      = idata->getFNumber ();
        cfs.focalLen     = idata->getFocalLen ();
        cfs.focalLen35mm = idata->getFocalLen35mm ();
        cfs.focusDist    = idata->getFocusDist ();
        cfs.iso          = idata->getISOSpeed ();
        cfs.expcomp      = idata->expcompToString (idata->getExpComp(), false); // do not mask Zero expcomp
        cfs.isHDR        = idata->getHDR ();
        cfs.isPixelShift = idata->getPixelShift ();
        cfs.frameCount   = idata->getFrameCount ();
        cfs.sampleFormat = idata->getSampleFormat ();
        cfs.year         = 1900 + idata->getDateTime().tm_year;
        cfs.month        = idata->getDateTime().tm_mon + 1;
        cfs.day          = idata->getDateTime().tm_mday;
        cfs.hour         = idata->getDateTime().tm_hour;
        cfs.min          = idata->getDateTime().tm_min;
        cfs.sec          = idata->getDateTime().tm_sec;
        cfs.timeValid    = true;
        cfs.exifValid    = true;
        cfs.lens         = idata->getLens();
        cfs.orientation         = idata->getOrientation();
        cfs.camMake      = idata->getMake();
        cfs.camModel     = idata->getModel();
        cfs.rating = idata->getRating();
        cfs.colorLabel = idata->getColorLabel();
        cfs.timestamp = idata->getDateTimeAsTS();

         if (idata->getOrientation() == "") {
            cfs.orientation = "Unknown";
        }

        if (idata->getOrientation() == "Rotate 90 CW") {
            deg = 90;
        } else if (idata->getOrientation() == "Rotate 180") {
            deg = 180;
        } else if (idata->getOrientation() == "Rotate 270 CW") {
            deg = 270;
        }
    } else {
        cfs.lens     = "Unknown";
        cfs.camMake  = "Unknown";
        cfs.camModel = "Unknown";
    }

    // get image filetype
    std::string::size_type idx;
    idx = fname.rfind('.');

    if(idx != std::string::npos) {
        cfs.filetype = fname.substr(idx + 1).uppercase();
    } else {
        cfs.filetype = "";
    }

    idata->getDimensions(cfs.width, cfs.height);

    delete idata;
    return deg;
}

/*
 * Read all thumbnail's data from the cache; build and save them if doesn't exist - NON PROTECTED
 * This includes:
 *  - image's bitmap (*.rtti)
 *  - auto exposure's histogram (full thumbnail only)
 *  - embedded profile (full thumbnail only)
 *  - LiveThumbData section of the data file
 */
void Thumbnail::_loadThumbnail(bool firstTrial, bool info_only)
{

    needsReProcessing = true;
    tw = -1;
    th = options.maxThumbnailHeight;
    delete tpp;
    tpp = new rtengine::Thumbnail ();
    tpp->isRaw = (cfs.format == (int) FT_Raw);

    // load supplementary data
    bool succ = tpp->readData (getCacheFileName ("data", ".txt"));

    if (succ) {
        tpp->getAutoWBMultipliers(cfs.redAWBMul, cfs.greenAWBMul, cfs.blueAWBMul);
    }

    // thumbnail image
    succ = succ && tpp->readImage (getCacheFileName ("images", ""));

    if (!succ && firstTrial) {
        _generateThumbnailImage(false, info_only);
        return;

        // if (cfs.supported && firstTrial) {
        //     _loadThumbnail (false);
        // }

        // if (tpp == nullptr) {
        //     return;
        // }
    } else if (!succ) {
        delete tpp;
        tpp = nullptr;
        return;
    }

    if ( cfs.thumbImgType == CacheImageData::FULL_THUMBNAIL ) {
        // load embedded profile
        tpp->readEmbProfile (getCacheFileName ("embprofiles", ".icc"));

        tpp->init ();
    }

    if (!initial_) {
        tw = tpp->getImageWidth (getProcParamsU(), th, imgRatio);    // this might return 0 if image was just building
    }
}

/*
 * Read all thumbnail's data from the cache; build and save them if doesn't exist - MUTEX PROTECTED
 * This includes:
 *  - image's bitmap (*.rtti)
 *  - auto exposure's histogram (full thumbnail only)
 *  - embedded profile (full thumbnail only)
 *  - LiveThumbData section of the data file
 */
void Thumbnail::loadThumbnail (bool firstTrial)
{
    MyMutex::MyLock lock(mutex);
    _loadThumbnail(firstTrial);
}

/*
 * Save thumbnail's data to the cache - NON PROTECTED
 * This includes:
 *  - image's bitmap (*.rtti)
 *  - auto exposure's histogram (full thumbnail only)
 *  - embedded profile (full thumbnail only)
 *  - LiveThumbData section of the data file
 */
void Thumbnail::_saveThumbnail ()
{

    if (!tpp) {
        return;
    }

    g_remove (getCacheFileName ("images", ".rtti").c_str ());

    // save thumbnail image
    tpp->writeImage (getCacheFileName ("images", ""));

    // save embedded profile
    tpp->writeEmbProfile (getCacheFileName ("embprofiles", ".icc"));

    // save supplementary data
    tpp->writeData (getCacheFileName ("data", ".txt"));
}

/*
 * Save thumbnail's data to the cache - MUTEX PROTECTED
 * This includes:
 *  - image's bitmap (*.rtti)
 *  - auto exposure's histogram (full thumbnail only)
 *  - embedded profile (full thumbnail only)
 *  - LiveThumbData section of the data file
 */
void Thumbnail::saveThumbnail ()
{
    MyMutex::MyLock lock(mutex);
    _saveThumbnail();
}

/*
 * Update the cached files
 *  - updatePParams==true (default)        : write the procparams file (sidecar or cache, depending on the options)
 *  - updateCacheImageData==true (default) : write the CacheImageData values in the cache folder,
 *                                           i.e. some General, DateTime, ExifInfo, File info and ExtraRawInfo,
 */
void Thumbnail::updateCache (bool updatePParams, bool updateCacheImageData)
{
    saveRating();

    if (updatePParams && pparamsValid) {
        pparams.save (
            cachemgr->getProgressListener(),
            options.saveParamsFile  ? options.getParamFile(fname) : "",
            options.saveParamsCache ? getCacheFileName ("profiles", paramFileExtension) : "");
    }

    if (updateCacheImageData) {
        cfs.save (getCacheFileName ("data", ".txt"));
    }

    if (updatePParams && pparamsValid) {
        saveMetadata();
    }
}

Thumbnail::~Thumbnail ()
{
    mutex.lock();

    delete [] lastImg;
    delete tpp;
    mutex.unlock();
}

Glib::ustring Thumbnail::getCacheFileName (const Glib::ustring& subdir, const Glib::ustring& fext) const
{
    return cachemgr->getCacheFileName (subdir, fname, fext, cfs.md5);
}

void Thumbnail::setFileName (const Glib::ustring &fn)
{

    fname = fn;
    cfs.md5 = cachemgr->getMD5 (fname);
}

void Thumbnail::addThumbnailListener (ThumbnailListener* tnl)
{

    increaseRef();
    listeners.push_back (tnl);
}

void Thumbnail::removeThumbnailListener (ThumbnailListener* tnl)
{

    std::vector<ThumbnailListener*>::iterator f = std::find (listeners.begin(), listeners.end(), tnl);

    if (f != listeners.end()) {
        listeners.erase (f);
        decreaseRef();
    }
}


bool Thumbnail::imageLoad(bool loading)
{
    MyMutex::MyLock lock(mutex);
    bool previous = imageLoading;

    if( loading && !previous ) {
        imageLoading = true;
        return true;
    } else if( !loading ) {
        imageLoading = false;
    }

    return false;
}


void Thumbnail::saveRating()
{
    if (!rating_.edited()) {
        return;
    }
    
    if (options.thumbnail_rating_mode == Options::ThumbnailRatingMode::PROCPARAMS) {
        if (rating_.rank.edited && rating_.rank != pparams.master.rank) {
            pparams.master.rank = rating_.rank;
            pparamsValid = true;
        }
        if (rating_.color.edited && rating_.color != pparams.master.colorlabel) {
            pparams.master.colorlabel = rating_.color;
            pparamsValid = true;
        }
        if (rating_.trash.edited && rating_.trash != pparams.master.inTrash) {
            pparams.master.inTrash = rating_.trash;
            pparamsValid = true;
        }
    } else if (rating_.edited()) {
        auto fn = rtengine::Exiv2Metadata::xmpSidecarPath(fname);
        try {
            auto xmp = rtengine::Exiv2Metadata::getXmpSidecar(fname);
            if (rating_.color.edited) {
                xmp["Xmp.xmp.Label"] = rtengine::FramesData::xmp_color2label(rating_.color);
            }
            if (rating_.trash.edited || rating_.rank.edited) {
                xmp["Xmp.xmp.Rating"] = (rating_.trash ? "-1" : std::to_string(rating_.rank));
            }
            rtengine::Exiv2Metadata meta;
            meta.xmpData() = std::move(xmp);
            meta.saveToXmp(fn);
        } catch (std::exception &exc) {
            std::cerr << "ERROR saving thumbnail rating data to " << fn
                      << ": " << exc.what() << std::endl;
            if (cachemgr->getProgressListener()) {
                cachemgr->getProgressListener()->error(Glib::ustring::compose(M("METADATA_SAVE_ERROR"), fn, exc.what()));
            }
        }
    }
}


void Thumbnail::loadRating()
{
    bool update_rating_xmp = false;
    bool update_color_xmp = false;
    rating_ = Rating();
    if (cfs.exifValid) {
        if (cfs.getRating() < 0) {
            rating_.trash.value = true;
        } else {
            rating_.rank.value = rtengine::LIM(cfs.getRating(), 0, 5);
        }
        rating_.color.value = rtengine::LIM(cfs.getColorLabel(), -1, 5);
    } else {
        auto md = getMetaData();
        if (md && md->hasExif()) {
            if (md->getRating() < 0) {
                rating_.trash.value = true;
            } else {
                rating_.rank.value = rtengine::LIM(md->getRating(), 0, 5);
            }
            update_rating_xmp = md->getRating() != 0;
            auto color = md->getColorLabel();
            if (color >= 1 && color <= 5) {
                rating_.color.value = color;
                update_color_xmp = true;
            }
        }
    }
    if (options.thumbnail_rating_mode == Options::ThumbnailRatingMode::PROCPARAMS) {
        if (pparamsValid) {
            if (pparams.master.rank >= 0) {
                rating_.rank.value = pparams.master.rank;
            }
            rating_.color.value = pparams.master.colorlabel;
            rating_.trash.value = pparams.master.inTrash;
        }
    } else {
        try {
            auto xmp = rtengine::Exiv2Metadata::getXmpSidecar(fname);
            auto pos = xmp.findKey(Exiv2::XmpKey("Xmp.xmp.Rating"));
            if (pos != xmp.end()) {
                int r = rtengine::exiv2_to_long(*pos);
                if (r < 0) {
                    rating_.trash.value = true;
                } else {
                    rating_.rank.value = rtengine::LIM(r, 0, 5);
                }
            } else if (update_rating_xmp) {
                rating_.trash.edited = true;
                rating_.rank.edited = true;
            }
            pos = xmp.findKey(Exiv2::XmpKey("Xmp.xmp.Label"));
            if (pos != xmp.end()) {
                rating_.color.value = rtengine::FramesData::xmp_label2color(pos->toString());
            } else if (update_color_xmp) {
                rating_.color.edited = true;
            }
        } catch (std::exception &exc) {
            std::cerr << "ERROR loading thumbnail rating data from "
                      << getXmpSidecarPath(fname)
                      << ": " << exc.what() << std::endl;
            if (cachemgr->getProgressListener()) {
                cachemgr->getProgressListener()->error(Glib::ustring::compose(M("METADATA_LOAD_ERROR"), getXmpSidecarPath(fname), exc.what()));
            }
        }    
    }
}


void Thumbnail::saveMetadata()
{
    if (options.rtSettings.metadata_xmp_sync != rtengine::Settings::MetadataXmpSync::READ_WRITE) {
        return;
    }

    if (pparams.master.metadata.exif.empty() && pparams.master.metadata.iptc.empty()) {
        return;
    }

    auto fn = rtengine::Exiv2Metadata::xmpSidecarPath(fname);
    try {
        auto xmp = rtengine::Exiv2Metadata::getXmpSidecar(fname);
        rtengine::Exiv2Metadata meta;
        meta.xmpData() = std::move(xmp);
        meta.setExif(pparams.master.metadata.exif);
        meta.setIptc(pparams.master.metadata.iptc);
        meta.saveToXmp(fn);
        if (options.rtSettings.verbose > 1) {
            std::cout << "saved edited metadata for " << fname << " to "
                      << fn << std::endl;
        }
    } catch (std::exception &exc) {
        std::cerr << "ERROR saving metadata for " << fname << " to " << fn
                  << ": " << exc.what() << std::endl;
        if (cachemgr->getProgressListener()) {
            cachemgr->getProgressListener()->error(Glib::ustring::compose(M("METADATA_SAVE_ERROR"), fn, exc.what()));
        }
    }
}


std::shared_ptr<rtengine::FramesMetaData> Thumbnail::getMetaData()
{
    rtengine::FramesMetaData* imageMetaData = rtengine::FramesMetaData::fromFile(fname);
    static_cast<rtengine::FramesData *>(imageMetaData)->setRAW(getType() == FT_Raw);
    return std::shared_ptr<rtengine::FramesMetaData>(imageMetaData);
}


Glib::ustring Thumbnail::getXmpSidecarPath(const Glib::ustring &path)
{
    return rtengine::Exiv2Metadata::xmpSidecarPath(path);
}


void Thumbnail::snapshotsChanged(const std::vector<std::pair<Glib::ustring, rtengine::procparams::ProcParams>> &snapshots)
{
    if (pparamsValid || !snapshots.empty()) {
        pparamsValid = true;
        pparams.snapshots = snapshots;
        updateCache(true, false);
    }
}


const std::vector<std::pair<Glib::ustring, rtengine::procparams::ProcParams>> &Thumbnail::getProcParamsSnapshots()
{
    return pparams.snapshots;
}

