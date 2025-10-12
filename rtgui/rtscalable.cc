/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2018 Jean-Christophe FRISCH <natureh.510@gmail.com>
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

#include "rtscalable.h"
#include <glib/gstdio.h>
#include <glibmm/regex.h>
#include <glibmm.h>
#include <iostream>
#include <librsvg/rsvg.h>
#include "options.h"
#include "pathutils.h"
#include <unordered_map>

double RTScalable::dpi = 0.;
int RTScalable::scale = 0;
int RTScalable::device_scale = 1;

extern Options options;
extern unsigned char initialGdkScale;
extern float fontScale;
Gtk::TextDirection RTScalable::direction = Gtk::TextDirection::TEXT_DIR_NONE;


void RTScalable::setDPInScale(const double newDPI, const int newScale)
{
    if (!options.pseudoHiDPISupport) {
    	scale = 1;
    	dpi = baseDPI;
    	return;
    }

    if (scale != newScale || (scale == 1 && dpi != newDPI)) {
        // reload all images
        scale = newScale;
        // HOMBRE: On windows, if scale = 2, the dpi is non significant, i.e. should be considered = 192 ; don't know for linux/macos
        dpi = newDPI;
        if (scale == 1) {
            if (dpi >= baseHiDPI) {
                scale = 2;
            }
        }
        else if (scale == 2) {
            if (dpi < baseHiDPI) {
                dpi *= 2.;
            }
        }
    }
}

double RTScalable::getDPI ()
{
    return dpi;
}

double RTScalable::getTweakedDPI ()
{
    return dpi * fontScale;
}

int RTScalable::getScale ()
{
    return scale;
}

int RTScalable::getDeviceScale()
{
    return device_scale;
}

Gtk::TextDirection RTScalable::getDirection()
{
    return direction;
}

void RTScalable::init(Gtk::Window *window)
{
    dpi = 0.;
    scale = 0;
    device_scale = 1;

    setDPInScale(window->get_screen()->get_resolution(), rtengine::max((int)initialGdkScale, window->get_scale_factor()));
    direction = window->get_direction();
#ifdef __APPLE__
    device_scale = window->get_scale_factor();
#endif
}

void RTScalable::deleteDir(const Glib::ustring& path)
{
    if (options.rtSettings.verbose > 1) {
        std::cout << "RTScalable::deleteDir(" << path << ")" << std::endl;
    }
    
    int error = 0;
    try {

        Glib::Dir dir (path);

        // Removing the directory content
        for (auto entry = dir.begin(); entry != dir.end(); ++entry) {
            error |= g_remove (Glib::build_filename (path, *entry).c_str());
        }

        if (error != 0 && options.rtSettings.verbose) {
            std::cerr << "Failed to delete all entries in '" << path << "': " << g_strerror(errno) << std::endl;
        }

    } catch (Glib::Error &exc) {
        if (options.rtSettings.verbose) {
            std::cerr << "Error in deleteDir(" << path << "): " << exc.what() << std::endl;
        }
        error = 1;
    }

    // Removing the directory itself
    if (!error) {
        try {

            error = g_remove (path.c_str());

        } catch (Glib::Error &exc) {
            if (options.rtSettings.verbose) {
                std::cerr << "Error in deleteDir(" << path << "): " << exc.what() << std::endl;
            }    
        }
    }
}

void RTScalable::cleanup(bool all)
{
    Glib::ustring imagesCacheFolder = Glib::build_filename (options.cacheBaseDir, "svg2png");
    Glib::ustring sDPI = Glib::ustring::compose("%1", (int)getTweakedDPI());

    try {
        Glib::Dir dir(imagesCacheFolder);

        for (Glib::DirIterator entry = dir.begin(); entry != dir.end(); ++entry) {
            const Glib::ustring fileName = *entry;
            const Glib::ustring filePath = Glib::build_filename(imagesCacheFolder, fileName);
            if (fileName == "." || fileName == ".." || !Glib::file_test(filePath, Glib::FILE_TEST_IS_DIR)) {
                continue;
            }

            if (all || fileName != sDPI) {
                deleteDir(filePath);
            }
        }
    } catch (Glib::Exception &exc) {
        if (options.rtSettings.verbose) {
            std::cerr << "Error in RTScalable::cleanup: " << exc.what() << std::endl;
        }
    }

}


namespace {

std::unordered_map<std::string, Cairo::RefPtr<Cairo::ImageSurface>> image_cache;

Cairo::RefPtr<Cairo::ImageSurface> cache_put(const Glib::ustring &fname, Cairo::RefPtr<Cairo::ImageSurface> surf)
{
    image_cache[fname] = surf;
    return surf;
}

} // namespace


Cairo::RefPtr<Cairo::ImageSurface> RTScalable::loadImage(const Glib::ustring &fname, double dpi)
{
    Glib::ustring imagesFolder = Glib::build_filename (options.ART_base_dir, "images");

    auto it = image_cache.find(fname);
    if (it != image_cache.end()) {
        return it->second;
    }

    auto path = Glib::build_filename(imagesFolder, fname);

    std::string svgFile;

    if (getExtension(fname) == "png") {
        return cache_put(fname, Cairo::ImageSurface::create_from_png(path));
    }

    // -------------------- Loading the SVG file --------------------

    try {
        svgFile = Glib::file_get_contents(path);
    } catch (Glib::FileError &err) {
        std::cerr << "ERROR: " << err.what() << std::endl;
        Cairo::RefPtr<Cairo::ImageSurface> surf = Cairo::ImageSurface::create(Cairo::FORMAT_RGB24, 10, 10);
        return surf;
    }

    // -------------------- Updating the the magic color --------------------
    // Magic color       : #2a7fff

    std::string updatedSVG = Glib::Regex::create("#2a7fff")->replace(svgFile, 0, options.svg_color, Glib::RegexMatchFlags());

    // -------------------- Creating the rsvg handle --------------------

    GError **error = nullptr;
    RsvgHandle *handle = rsvg_handle_new_from_data((unsigned const char*)updatedSVG.c_str(), updatedSVG.length(), error);

    if (handle == nullptr) {
        std::cerr << "ERROR: Can't use the provided data for \"" << fname << "\" to create a RsvgHandle:" << std::endl
                  << Glib::ustring((*error)->message) << std::endl;
        Cairo::RefPtr<Cairo::ImageSurface> surf = Cairo::ImageSurface::create(Cairo::FORMAT_RGB24, 10, 10);
        return surf;
    }

    // -------------------- Drawing the image to a Cairo::ImageSurface --------------------

    RsvgDimensionData dim;
    rsvg_handle_get_dimensions(handle, &dim);
    int scale = getDeviceScale();
    double r = dpi / baseDPI * scale;
    Cairo::RefPtr<Cairo::ImageSurface> surf = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, (int)(dim.width * r + 0.499), (int)(dim.height * r + 0.499));
    Cairo::RefPtr<Cairo::Context> c = Cairo::Context::create(surf);
    c->set_source_rgba (0., 0., 0., 0.);
    c->set_operator (Cairo::OPERATOR_CLEAR);
    c->paint ();
    c->set_operator (Cairo::OPERATOR_OVER);
    c->scale(r, r);
    rsvg_handle_render_cairo(handle, c->cobj());
    rsvg_handle_free(handle);

    setDeviceScale(surf, scale);

    return cache_put(fname, surf);
}


void RTScalable::setDeviceScale(const Cairo::RefPtr<Cairo::Surface>& surface, int scale)
{
    cairo_surface_t* cobj = surface->cobj();
    cairo_surface_set_device_scale(cobj, scale, scale);
}
