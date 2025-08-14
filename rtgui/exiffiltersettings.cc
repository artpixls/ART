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
#include "exiffiltersettings.h"

ExifFilterSettings::ExifFilterSettings()
{
    clear();
}


void ExifFilterSettings::clear()
{
    enabled = false;
    fnumberFrom = 100;
    fnumberTo = 0;
    shutterFrom = 100;
    shutterTo = 0;
    isoFrom = 100000000;
    isoTo = 0;
    focalFrom = 1e8;
    focalTo = 0;
    dateFrom = Glib::Date(31, Glib::Date::DECEMBER, 2100);
    dateTo = Glib::Date(1, Glib::Date::JANUARY, 1900);
    lenses.clear();
    orientations.clear();
    cameras.clear();
    expcomp.clear();
    filetypes.clear();

    filterFNumber = false;
    filterShutter = false;
    filterFocalLen = false;
    filterISO = false;
    filterExpComp = false;
    filterCamera = false;
    filterLens = false;
    filterOrientation = false;
    filterFiletype = false;
    filterDate = false;
}


void ExifFilterSettings::load(const Glib::KeyFile &kf, const Glib::ustring &group)
{
    clear();
    
    const auto get_bool =
        [&](bool &val, const Glib::ustring &key) -> void
        {
            if (kf.has_key(group, key)) {
                val = kf.get_boolean(group, key);
            }
        };

    const auto get_int =
        [&](unsigned &val, const Glib::ustring &key) -> void
        {
            if (kf.has_key(group, key)) {
                val = kf.get_integer(group, key);
            }
        };

    const auto get_double =
        [&](double &val, const Glib::ustring &key) -> void
        {
            if (kf.has_key(group, key)) {
                val = kf.get_double(group, key);
            }
        };
    
    get_bool(enabled, "Enabled");
    get_bool(filterFNumber, "FilterFNumber");
    get_bool(filterShutter, "FilterShutter");
    get_bool(filterFocalLen, "FilterFocalLen");
    get_bool(filterISO, "FilterISO");
    get_bool(filterExpComp, "FilterExpComp");
    get_bool(filterCamera, "FilterCamera");
    get_bool(filterLens, "FilterLens");
    get_bool(filterOrientation, "FilterOrientation");
    get_bool(filterFiletype, "FilterFiletype");
    get_bool(filterDate, "FilterDate");

    const auto get_set =
        [&](const Glib::ustring &name, std::set<std::string> &s) -> void
        {
            if (kf.has_key(group, name)) {
                auto l = kf.get_string_list(group, name);
                s.clear();
                s.insert(l.begin(), l.end());
            }
        };

    get_set("Filetypes", filetypes);
    get_set("Cameras", cameras);
    get_set("Lenses", lenses);
    get_set("Orientations", orientations);
    get_set("Expcomp", expcomp);

    get_double(fnumberFrom, "FNumberFrom");
    get_double(fnumberTo, "FNumberTo");
    get_double(shutterFrom, "ShutterFrom");
    get_double(shutterTo, "ShutterTo");
    get_double(focalFrom, "FocalFrom");
    get_double(focalTo, "FocalTo");

    get_int(isoFrom, "ISOFrom");
    get_int(isoTo, "ISOTo");

    const auto get_date =
        [&](const Glib::ustring &name, Glib::Date &d) -> void
        {
            if (!kf.has_key(group, name)) {
                return;
            }
            
            d = Glib::Date();
            
            auto s = kf.get_string(group, name);

            Glib::KeyFileError err(Glib::KeyFileError::INVALID_VALUE,
                                   Glib::ustring::format("bad date: %1", s));
            
            Glib::ustring::size_type prev = 0;
            auto pos = s.find_first_of('/', prev);
            if (pos == Glib::ustring::npos) {
                throw err;
            }
            auto t = s.substr(prev, pos - prev);
            auto year = atoi(t.c_str());
            if (year < 1900) {
                throw err;
            }
            d.set_year(year);
            
            prev = pos+1;
            pos = s.find_first_of('/', prev);
            if (pos == Glib::ustring::npos) {
                throw err;
            }
            t = s.substr(prev, pos - prev);
            auto month = atoi(t.c_str());
            if (month < 1 || month > 12) {
                throw err;
            }
            d.set_month(Glib::Date::Month(month));
            
            prev = pos+1;
            t = s.substr(prev);
            auto day = atoi(t.c_str());
            if (day < 1 || day > 31) {
                throw err;
            }
            d.set_day(day);
        };

    get_date("DateFrom", dateFrom);
    get_date("DateTo", dateTo);
}


void ExifFilterSettings::save(Glib::KeyFile &kf, const Glib::ustring &group) const
{
    kf.set_boolean(group, "Enabled", enabled);
    kf.set_boolean(group, "FilterFNumber", filterFNumber);
    kf.set_boolean(group, "FilterShutter", filterShutter);
    kf.set_boolean(group, "FilterFocalLen", filterFocalLen);
    kf.set_boolean(group, "FilterISO", filterISO);
    kf.set_boolean(group, "FilterExpComp", filterExpComp);
    kf.set_boolean(group, "FilterCamera", filterCamera);
    kf.set_boolean(group, "FilterLens", filterLens);
    kf.set_boolean(group, "FilterOrientation", filterOrientation);
    kf.set_boolean(group, "FilterFiletype", filterFiletype);
    kf.set_boolean(group, "FilterDate", filterDate);

    const auto set_set =
        [&](const Glib::ustring &name, const std::set<std::string> &s) -> void
        {
            kf.set_string_list(group, name, s);
        };

    set_set("Filetypes", filetypes);
    set_set("Cameras", cameras);
    set_set("Lenses", lenses);
    set_set("Orientations", orientations);
    set_set("Expcomp", expcomp);

    kf.set_double(group, "FNumberFrom", fnumberFrom);
    kf.set_double(group, "FNumberTo", fnumberTo);
    kf.set_double(group, "ShutterFrom", shutterFrom);
    kf.set_double(group, "ShutterTo", shutterTo);
    kf.set_double(group, "FocalFrom", focalFrom);
    kf.set_double(group, "FocalTo", focalTo);
    kf.set_integer(group, "ISOFrom", isoFrom);
    kf.set_integer(group, "ISOTo", isoTo);

    const auto set_date =
        [&](const Glib::ustring &name, const Glib::Date &d) -> void
        {
            auto s = d.format_string("%Y/%m/%d");
            kf.set_string(group, name, s);
        };

    set_date("DateFrom", dateFrom);
    set_date("DateTo", dateTo);
}
