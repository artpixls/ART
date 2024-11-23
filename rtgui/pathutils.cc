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

#include "pathutils.h"

#ifndef WIN32
# include <glibmm/fileutils.h>
# include <glib.h>
# include <glib/gstdio.h>
# include <glibmm/threads.h>
# include <unistd.h>
#else // WIN32
# include <glibmm/thread.h>
# include "conio.h"
#endif

#ifdef __APPLE__
# include <mach-o/dyld.h>
#endif // __APPLE__


Glib::ustring removeExtension (const Glib::ustring& filename)
{

    Glib::ustring bname = Glib::path_get_basename(filename);
    size_t lastdot = bname.find_last_of ('.');
    size_t lastwhitespace = bname.find_last_of (" \t\f\v\n\r");

    if (lastdot != bname.npos && (lastwhitespace == bname.npos || lastdot > lastwhitespace)) {
        return filename.substr (0, filename.size() - (bname.size() - lastdot));
    } else {
        return filename;
    }
}

Glib::ustring getExtension (const Glib::ustring& filename)
{

    Glib::ustring bname = Glib::path_get_basename(filename);
    size_t lastdot = bname.find_last_of ('.');
    size_t lastwhitespace = bname.find_last_of (" \t\f\v\n\r");

    if (lastdot != bname.npos && (lastwhitespace == bname.npos || lastdot > lastwhitespace)) {
        return filename.substr (filename.size() - (bname.size() - lastdot) + 1, filename.npos);
    } else {
        return "";
    }
}


// For an unknown reason, Glib::filename_to_utf8 doesn't work on reliably Windows,
// so we're using Glib::filename_to_utf8 for Linux/Apple and Glib::locale_to_utf8 for Windows.
Glib::ustring fname_to_utf8(const std::string &fname)
{
#ifdef WIN32
    try {
        return Glib::locale_to_utf8(fname);
    } catch (Glib::Error&) {
        return Glib::convert_with_fallback(fname, "UTF-8", "ISO-8859-1", "?");
    }

#else

    return Glib::filename_to_utf8(fname);

#endif
}


Glib::ustring getExecutablePath(char *argv0)
{
    char exname[512] = {0};
    
#ifdef WIN32
    WCHAR exnameU[512] = {0};
    GetModuleFileNameW (NULL, exnameU, 511);
    WideCharToMultiByte (CP_UTF8, 0, exnameU, -1, exname, 511, 0, 0 );
    
#elif defined __APPLE__
    uint32_t bufsz = 512;
    
    if (_NSGetExecutablePath(exname, &bufsz) != 0) {
        g_strlcpy(exname, argv0, 512);
    }
    {
        gchar *a = g_canonicalize_filename(exname, NULL);
        g_strlcpy(exname, a, 512);
        g_free(a);
    }
    
#else

    if (readlink("/proc/self/exe", exname, 511) < 0) {
        g_strlcpy(exname, argv0, 512);
        gchar *a = g_canonicalize_filename(exname, NULL);
        g_strlcpy(exname, a, 512);
        g_free(a);
    }

#endif
    
    Glib::ustring exePath = Glib::path_get_dirname(exname);
    return exePath;
}
