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
#include "cachemanager.h"

#include <memory>
#include <iostream>

#include <glib/gstdio.h>
#include <giomm.h>

#ifdef WIN32
#  include <windows.h>
#endif

#include "guiutils.h"
#include "options.h"
#include "procparamchangers.h"
#include "thumbnail.h"
#include "../rtengine/utils.h"
#ifdef ART_USE_OCIO
# include "../rtengine/extclut.h"
#endif

namespace {

constexpr int cacheDirMode = 0777;
constexpr const char* cacheDirs[] = {
    "profiles",
    "images",
    "embprofiles",
    "data"
};

} // namespace

CacheManager::CacheManager():
    pl_(nullptr)
{
}


CacheManager* CacheManager::getInstance ()
{
    static CacheManager instance;
    return &instance;
}


void CacheManager::init()
{
    MyMutex::MyLock lock(mutex);

    openEntries.clear();
    baseDir = options.cacheBaseDir;

    auto error = g_mkdir_with_parents(baseDir.c_str(), cacheDirMode);

    for (const auto& cacheDir : cacheDirs) {
        error |= g_mkdir_with_parents(Glib::build_filename(baseDir, cacheDir).c_str(), cacheDirMode);
    }

    if (error != 0 && options.rtSettings.verbose) {
        std::cerr << "Failed to create all cache directories: " << g_strerror(errno) << std::endl;
    }
}


Thumbnail* CacheManager::getEntry(const Glib::ustring& fname)
{
    std::unique_ptr<Thumbnail> thumbnail;

    // take manager lock and search for entry,
    // if found return it,
    // else release lock and create it
    {
        MyMutex::MyLock lock(mutex);

        // if it is open, return it
        const auto iterator = openEntries.find(fname);
        if (iterator != openEntries.end()) {

            auto cachedThumbnail = iterator->second;

            cachedThumbnail->increaseRef();
            return cachedThumbnail;
        }
    }

    // build path name
    const auto md5 = getMD5(fname);

    if (md5.empty()) {
        return nullptr;
    }

    const auto cacheName = getCacheFileName("data", fname, ".txt", md5);

    // let's see if we have it in the cache
    {
        CacheImageData imageData;

        const auto error = imageData.load(cacheName);
        if (error == 0 && imageData.supported) {

            thumbnail.reset(new Thumbnail(this, fname, &imageData));
            if (!thumbnail->isSupported()) {
                thumbnail.reset();
            }
        }
    }

    // if not, create a new one
    if (!thumbnail) {

        thumbnail.reset(new Thumbnail(this, fname, md5));
        if (!thumbnail->isSupported()) {
            thumbnail.reset();
        }
    }

    // retake the lock and see if it was added while we we're unlocked, if it
    // was use it over our version. if not added we create the cache entry
    if (thumbnail) {
        MyMutex::MyLock lock(mutex);

        const auto iterator = openEntries.find(fname);
        if (iterator != openEntries.end()) {

            auto cachedThumbnail = iterator->second;

            cachedThumbnail->increaseRef();
            return cachedThumbnail;
        }

        // it wasn't, create a new entry
        openEntries.emplace(fname, thumbnail.get());
    }

    return thumbnail.release();
}


void CacheManager::deleteEntry(const Glib::ustring& fname)
{
    MyMutex::MyLock lock(mutex);

    // check if it is opened
    auto iterator = openEntries.find(fname);
    if (iterator == openEntries.end()) {
        deleteFiles(fname, getMD5(fname), true, true);
        return;
    }

    auto thumbnail = iterator->second;

    // decrease reference count;
    // this will call back into CacheManager,
    // so we release the lock for it
    {
        lock.release();
        thumbnail->decreaseRef();
        lock.acquire();
    }

    // check again if in the editor,
    // the thumbnail still exists,
    // if not, delete it
    if (openEntries.count(fname) == 0) {
        deleteFiles(fname, thumbnail->getMD5(), true, true);
    }
}


void CacheManager::clearFromCache(const Glib::ustring& fname, bool purge) const
{
    deleteFiles(fname, getMD5(fname), true, purge);
}


void CacheManager::renameEntry(const std::string& oldfilename, const std::string& oldmd5, const std::string& newfilename)
{
    MyMutex::MyLock lock(mutex);

    const auto newmd5 = getMD5(newfilename);

    auto error = g_rename(getCacheFileName("profiles", oldfilename, paramFileExtension, oldmd5).c_str(), getCacheFileName("profiles", newfilename, paramFileExtension, newmd5).c_str());
    error |= g_rename(getCacheFileName("images", oldfilename, ".rtti", oldmd5).c_str(), getCacheFileName("images", newfilename, ".rtti", newmd5).c_str());
    error |= g_rename(getCacheFileName("embprofiles", oldfilename, ".icc", oldmd5).c_str(), getCacheFileName("embprofiles", newfilename, ".icc", newmd5).c_str());
    error |= g_rename(getCacheFileName("data", oldfilename, ".txt", oldmd5).c_str(), getCacheFileName("data", newfilename, ".txt", newmd5).c_str());
    error |= g_rename(getCacheFileName("images", oldfilename, ".artt", oldmd5).c_str(), getCacheFileName("images", newfilename, ".artt", newmd5).c_str());
    
    if (error != 0 && options.rtSettings.verbose) {
        std::cerr << "Failed to rename all files for cache entry '" << oldfilename << "': " << g_strerror(errno) << std::endl;
    }

    // check if it is opened
    // if it is open, update md5
    const auto iterator = openEntries.find(oldfilename);
    if (iterator == openEntries.end()) {
        return;
    }

    auto thumbnail = iterator->second;
    openEntries.erase(iterator);
    openEntries.emplace(newfilename, thumbnail);

    thumbnail->setFileName(newfilename);
    thumbnail->updateCache();
    thumbnail->saveThumbnail();
}


void CacheManager::closeThumbnail(Thumbnail* thumbnail)
{
    MyMutex::MyLock lock(mutex);

    openEntries.erase(thumbnail->getFileName());
    delete thumbnail;
}


void CacheManager::closeCache() const
{
    MyMutex::MyLock lock(mutex);

    applyCacheSizeLimitation();
#ifdef ART_USE_OCIO
    rtengine::ExternalLUT3D::trim_cache();
#endif
}


void CacheManager::clearAll() const
{
    MyMutex::MyLock lock(mutex);

    for (const auto& cacheDir : cacheDirs) {
        deleteDir(cacheDir);
    }

#ifdef ART_USE_OCIO
    rtengine::ExternalLUT3D::clear_cache();
#endif
}


void CacheManager::clearImages() const
{
    MyMutex::MyLock lock(mutex);

    deleteDir("data");
    deleteDir("images");
    deleteDir("aehistograms");
}


void CacheManager::clearProfiles() const
{
    MyMutex::MyLock lock(mutex);

    deleteDir("profiles");
}


void CacheManager::deleteDir(const Glib::ustring& dirName) const
{
    try {

        Glib::Dir dir(Glib::build_filename(baseDir, dirName));

        auto error = 0;
        for (auto entry = dir.begin(); entry != dir.end(); ++entry) {
            error |= g_remove(Glib::build_filename(baseDir, dirName, *entry).c_str());
        }

        if (error != 0 && options.rtSettings.verbose) {
            std::cerr << "Failed to delete all entries in cache directory '" << dirName << "': " << g_strerror(errno) << std::endl;
        }

    } catch (Glib::Error&) {}
}


void CacheManager::deleteFiles(const Glib::ustring& fname, const std::string& md5, bool purgeData, bool purgeProfile) const
{
    if (md5.empty()) {
        return;
    }

    auto error = g_remove(getCacheFileName("images", fname, ".rtti", md5).c_str());
    error |= g_remove(getCacheFileName("embprofiles", fname, ".icc", md5).c_str());
    error |= g_remove(getCacheFileName("images", fname, ".artt", md5).c_str());

    if (purgeData) {
        error |= g_remove(getCacheFileName("data", fname, ".txt", md5).c_str());
    }

    if (purgeProfile) {
        error |= g_remove(getCacheFileName("profiles", fname, paramFileExtension, md5).c_str());
    }

    if (error != 0 && options.rtSettings.verbose) {
        std::cerr << "Failed to delete all files for cache entry '" << fname << "': " << g_strerror(errno) << std::endl;
    }
}


std::string CacheManager::getMD5(const Glib::ustring& fname)
{
    return rtengine::getMD5(fname);
}


Glib::ustring CacheManager::getCacheFileName(const Glib::ustring& subDir,
                                             const Glib::ustring& fname,
                                             const Glib::ustring& fext,
                                             const Glib::ustring& md5) const
{
    const auto dirName = Glib::build_filename(baseDir, subDir);
    const auto baseName = Glib::path_get_basename(fname) + "." + md5;
    return Glib::build_filename(dirName, baseName + fext);
}


void CacheManager::applyCacheSizeLimitation() const
{
    // first count files without fetching file name and timestamp.
    std::size_t numFiles = 0;
    try {

        const auto dirName = Glib::build_filename(baseDir, "data");
        const auto dir = Gio::File::create_for_path(dirName);

        auto enumerator = dir->enumerate_children("");

        while (numFiles <= options.maxCacheEntries && enumerator->next_file()) {
            ++numFiles;
        }

    } catch (Glib::Exception&) {}

    if (numFiles <= options.maxCacheEntries) {
        return;
    }

    using FNameMTime = std::pair<Glib::ustring, Glib::TimeVal>;
    std::vector<FNameMTime> files;

    try {

        const auto dirName = Glib::build_filename(baseDir, "data");
        const auto dir = Gio::File::create_for_path(dirName);

        auto enumerator = dir->enumerate_children("standard::name,time::modified");

        while (auto file = enumerator->next_file()) {
            files.emplace_back(file->get_name(), file->modification_time());
        }

    } catch (Glib::Exception&) {}

    if (files.size() <= options.maxCacheEntries) {
        return;
    }

    std::sort(files.begin(), files.end(), [](const FNameMTime& lhs, const FNameMTime& rhs)
    {
        return lhs.second < rhs.second;
    });

    auto cacheEntries = files.size();

    for (auto entry = files.begin(); cacheEntries-- > options.maxCacheEntries; ++entry) {

        const auto& name = entry->first;

        constexpr auto md5_size = 32;
        const auto name_size = name.size();

        if (name_size < md5_size + 5) {
            continue;
        }

        const auto fname = name.substr(0, name_size - md5_size - 5);
        const auto md5 = name.substr(name_size - md5_size - 4, md5_size);

        deleteFiles(fname, md5, true, false);
    }
}


bool CacheManager::getImageData(const Glib::ustring &fname, CacheImageData &out)
{
    const auto md5 = getMD5(fname);

    if (!md5.empty()) {
        const auto cacheName = getCacheFileName("data", fname, ".txt", md5);

        const auto error = out.load(cacheName);
        if (error != 0) {
            return false;
        }
    } else {
        return false;
    }

    return true;
}
