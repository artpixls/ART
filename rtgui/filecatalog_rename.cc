/** -*- C++ -*-
 *  
 *  This file is part of ART.
 *
 *  Copyright (c) 2020 Alberto Griggio <alberto.griggio@gmail.com>
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
#include <gtkmm.h>
#include "filecatalog.h"
#include "options.h"
#include "multilangmgr.h"
#include "../rtengine/imagedata.h"
#include "../rtengine/metadata.h"
#include "pathutils.h"
#include "filepanel.h"
#include "rtwindow.h"
#include "session.h"
#include <time.h>
#include <ctype.h>
#include <sstream>
#include <iomanip>
#include <glibmm/regex.h>
#include <errno.h>


namespace {

using rtengine::FramesMetaData;
using rtengine::Exiv2Metadata;


class FastMetadata: public FramesMetaData {
public:
    FastMetadata(const Glib::ustring &fname, const CacheImageData *cd):
        fname_(fname), cd_(cd) {}

    Glib::ustring getFileName() const override { return fname_; }
    tm getDateTime() const override
    {
        if (fd_) {
            return fd_->getDateTime();
        } else if (cd_->timeValid) {
            tm res;
            res.tm_sec = cd_->sec;
            res.tm_min = cd_->min;
            res.tm_hour = cd_->hour;
            res.tm_year = cd_->year - 1900;
            res.tm_mday = cd_->day;
            res.tm_mon = cd_->month - 1;
            return res;
        } else {
            fd_.reset(FramesMetaData::fromFile(fname_));
            return fd_->getDateTime();
        }
    }

    unsigned int getFrameCount () const override { return f()->getFrameCount(); }
    bool hasExif() const override  { return f()->hasExif(); }
    time_t getDateTimeAsTS() const override { return f()->getDateTimeAsTS(); }
    int getISOSpeed() const override { return f()->getISOSpeed(); }
    double getFNumber() const override { return f()->getFNumber(); }
    double getFocalLen() const override { return f()->getFocalLen(); }
    double getFocalLen35mm() const override { return f()->getFocalLen35mm(); }
    float getFocusDist() const override { return f()->getFocusDist(); }
    double getShutterSpeed() const override { return f()->getShutterSpeed(); }
    double getExpComp() const override { return f()->getExpComp(); }
    std::string getMake() const override { return f()->getMake(); }
    std::string getModel() const override { return f()->getModel(); }
    std::string getLens() const override { return f()->getLens(); }
    std::string getOrientation() const override { return f()->getOrientation(); }
    bool getPixelShift () const override { return f()->getPixelShift(); }
    bool getHDR() const override { return f()->getHDR(); }
    std::string getImageType() const override { return f()->getImageType(); }
    std::string getSoftware() const override { return f()->getSoftware(); }
    rtengine::IIOSampleFormat getSampleFormat() const override { return f()->getSampleFormat(); }
    int getRating() const override { return f()->getRating(); }
    int getColorLabel() const override { return f()->getColorLabel(); }
    std::vector<rtengine::GainMap> getGainMaps() const override { return f()->getGainMaps(); }
    void getDimensions(int &w, int &h) const override { f()->getDimensions(w, h); }
    
private:
    const FramesMetaData *f() const
    {
        if (fd_) {
            return fd_.get();
        } else if (!cd_->exifValid) {
            fd_.reset(FramesMetaData::fromFile(fname_));
            return fd_.get();
        } else {
            return cd_;
        }
    }
    
    Glib::ustring fname_;
    const CacheImageData *cd_;
    mutable std::unique_ptr<FramesMetaData> fd_;
};


bool is_valid_char(gunichar c, bool allow_sep)
{
    if (G_IS_DIR_SEPARATOR(c)) {
        return allow_sep;
    }
    
#ifdef __WIN32__
    switch (c) {
    case '<':
    case '>':
    case ':':
    case '"':
    case '/':
    case '\\':
    case '|':
    case '?':
    case '*':
        return false;
    default:
        return true;
    }
#else
    return c != '/';
#endif
}


Glib::ustring make_valid(const Glib::ustring &s, bool allow_sep=false)
{
    Glib::ustring ret;
    for (auto c : s) {
        if (!is_valid_char(c, allow_sep)) {
            if (c == '/') {
                ret.push_back(gunichar(gunichar(0x2215))); // unicode "division slash" ∕
            } else {
                ret.push_back('_');
            }
        } else {
            ret.push_back(c);
        }
    }
    return ret;
}


class Pattern {
public:
    virtual ~Pattern() {}
    virtual Glib::ustring operator()(const FramesMetaData *fd) = 0;
};


class FixedPattern: public Pattern {
public:
    FixedPattern(const Glib::ustring &s): s_(make_valid(s, true)) {}
    Glib::ustring operator()(const FramesMetaData *fd) override
    {
        return s_;
    }
private:
    Glib::ustring s_;
};


class ProgressivePattern: public Pattern {
public:
    ProgressivePattern(int &idx, size_t pad): idx_(idx), pad_(pad) {}
    
    Glib::ustring operator()(const FramesMetaData *fd) override
    {
        auto s = std::to_string(idx_++);
        if (s.length() < pad_) {
            auto s2 = std::string(pad_ - s.length(), '0');
            s = s2 + s;
        }
        return s;
    }

private:
    int &idx_;
    size_t pad_;
};


template <class F>
class FramesDataPattern: public Pattern {
public:
    FramesDataPattern(F func): func_(func) {}
    Glib::ustring operator()(const FramesMetaData *fd) override
    {
        return make_valid(func_(fd));
    }
private:
    F func_;
};


template <class F>
std::unique_ptr<Pattern> make_pattern(F func)
{
    return std::unique_ptr<Pattern>(new FramesDataPattern<F>(func));
}


class TagPattern: public Pattern {
public:
    TagPattern(const std::string &tag): tag_(tag) {}
    
    Glib::ustring operator()(const FramesMetaData *fd) override
    {
        try {
            Exiv2Metadata md(fd->getFileName());
            md.load();
            if (strncmp(tag_.c_str(), "Exif.", 5) == 0) {
                auto it = md.exifData().findKey(Exiv2::ExifKey(tag_));
                if (it != md.exifData().end()) {
                    return make_valid(it->toString());
                }
            } else if (strncmp(tag_.c_str(), "Iptc.", 5) == 0) {
                auto it = md.iptcData().findKey(Exiv2::IptcKey(tag_));
                if (it != md.iptcData().end()) {
                    return make_valid(it->toString());
                }
            } else if (strncmp(tag_.c_str(), "Xmp.", 4) == 0) {
                auto it = md.xmpData().findKey(Exiv2::XmpKey(tag_));
                if (it != md.xmpData().end()) {
                    return make_valid(it->toString());
                }
            }
        } catch (std::exception &e) {
            if (options.rtSettings.verbose) {
                std::cout << "TagPattern error: " << e.what() << std::endl;
            }
        }
        return "";
    }

private:
    std::string tag_;
};


struct Params {
    std::string basedir;
    std::vector<std::unique_ptr<Pattern>> pattern;
    std::vector<Glib::ustring> sidecars;
    enum class Normalization {
        OFF,
        UPPERCASE,
        LOWERCASE
    };
    Normalization name_norm;
    Normalization ext_norm;
    bool allow_whitespace;
    enum class OnExistingAction {
        SKIP,
        RENAME
    };
    OnExistingAction on_existing;
    int progressive_number;

    Params() = default;
};


template <class T>
std::string tostr(T n, int digits)
{
    std::ostringstream buf;
    buf << std::setprecision(digits) << std::fixed << n;
    return buf.str();
}

/*
 * pattern syntax:
 * %f : FileNamePattern
 * %e : FileExtPattern
 * %# : FileNumberPattern
 * %a : DatePattern(day name abbreviated)
 * %A : DatePattern(day name full)
 * %b : DatePattern(month name abbreviated)
 * %B : DatePattern(month name full)
 * %m : DatePattern(month)
 * %Y : DatePattern(year)
 * %y : DatePattern(year 2-digits)
 * %d : DatePattern(day)
 * %C : CameraPattern
 * %M : MakePattern
 * %N : ModelPattern
 * %r : FramesDataPattern<rating>
 * %I : FramesDataPattern<ISO>
 * %F : FramesDataPattern<FNumber>
 * %L : FramesDataPattern<Lens>
 * %l : FramesDataPattern<FocalLength>
 * %E : FramesDataPattern<ExpComp>
 * %s : FramesDataPattern<ShutterSpeed>
 * %n[0-9] : ProgressivePattern
 * %T[tag] : TagPattern
 * %% : % character
 */ 
bool parse_pattern(const Glib::ustring &s, Params &out)
{
    typedef const FramesMetaData *FD;
    out.pattern.clear();
    size_t prev = 0;
    size_t n = s.length();
    for (size_t i = 0; i < n; ) {
        auto c = s[i];
        if (c == '%') {
            if (prev != i) {
                auto f = s.substr(prev, i-prev);
                out.pattern.emplace_back(new FixedPattern(f));
            }
            if (i+1 < n) {
                i += 2;
                c = s[i-1];
                switch (c) {
                case 'f':
                    out.pattern.push_back(
                        make_pattern(
                            [](FD fd) {
                                return removeExtension(
                                    Glib::path_get_basename(fd->getFileName()));
                            }));
                    break;
                case 'e':
                    out.pattern.push_back(
                        make_pattern(
                            [](FD fd) {
                                return getExtension(fd->getFileName());
                            }));
                    break;
                case '#':
                    out.pattern.push_back(
                        make_pattern(
                            [](FD fd) {
                                auto re = Glib::Regex::create("^.*?([0-9]+)$");
                                Glib::MatchInfo m;
                                auto name = removeExtension(
                                    Glib::path_get_basename(fd->getFileName()));
                                if (re->match(name, m)) {
                                    return m.fetch(1);
                                }
                                return Glib::ustring("");
                            }));
                    break;
                case 'm':
                case 'd':
                case 'Y':
                case 'y':
                case 'a':
                case 'A':
                case 'b':
                case 'B':
                    out.pattern.push_back(
                        make_pattern(
                            [c](FD fd) {
                                char buf[256];
                                char fmt[3] = { '%', char(c), 0 };
                                auto t = fd->getDateTime();
                                strftime(buf, 255, fmt, &t);
                                return std::string(buf);
                            }));
                    break;
                case 'C':
                    out.pattern.push_back(
                        make_pattern(
                            [](FD fd) {
                                return fd->getMake() + " " + fd->getModel();
                            }));
                    break;
                case 'M':
                    out.pattern.push_back(
                        make_pattern(
                            [](FD fd) {
                                return fd->getMake();
                            }));
                    break;
                case 'N':
                    out.pattern.push_back(
                        make_pattern(
                            [](FD fd) {
                                return fd->getModel();
                            }));
                    break;
                case 'n': 
                    if (i < n && isdigit(s[i])) {
                        out.pattern.emplace_back(
                            new ProgressivePattern(out.progressive_number,
                                                   int(s[i]) - int('0')));
                        ++i;
                    } else {
                        out.pattern.emplace_back(
                            new ProgressivePattern(out.progressive_number, 0));
                    }
                    break;
                case 'T':
                    if (i < n && s[i] == '[') {
                        size_t j = i+1;
                        while (j < n && s[j] != ']') {
                            ++j;
                        }
                        if (j < n) {
                            out.pattern.emplace_back(
                                new TagPattern(s.substr(i+1, j-i-1)));
                            i = j+1;
                        } else {
                            return false;
                        }
                    } else {
                        return false;
                    }
                    break;
                case 'r':
                    out.pattern.push_back(
                        make_pattern(
                            [](FD fd) {
                                return tostr(fd->getRating(), 0);
                            }));
                    break;
                case 'I':
                    out.pattern.push_back(
                        make_pattern(
                            [](FD fd) {
                                return tostr(fd->getISOSpeed(), 0);
                            }));
                    break;
                case 'F':
                    out.pattern.push_back(
                        make_pattern(
                            [](FD fd) {
                                return fd->apertureToString(fd->getFNumber());
                            }));
                    break;
                case 'L':
                    out.pattern.push_back(
                        make_pattern(
                            [](FD fd) {
                                return fd->getLens();
                            }));
                    break;
                case 'l':
                    out.pattern.push_back(
                        make_pattern(
                            [](FD fd) {
                                return tostr(fd->getFocalLen(), 0);
                            }));
                    break;
                case 'E':
                    out.pattern.push_back(
                        make_pattern(
                            [](FD fd) {
                                return fd->expcompToString(fd->getExpComp(), false);
                            }));
                    break;
                case 's':
                    out.pattern.push_back(
                        make_pattern(
                            [](FD fd) {
                                return fd->shutterToString(fd->getShutterSpeed());
                            }));
                    break;
                case '%':
                    out.pattern.push_back(
                        make_pattern([](FD fd) { return "%"; }));
                    break;
                default:
                    return false;
                }
                prev = i;
            } else {
                return false;
            }
        } else {
            if (!is_valid_char(c, true)) {
                return false;
            }
            ++i;
        }
    }
    if (prev < n) {
        auto f = s.substr(prev);
        out.pattern.emplace_back(new FixedPattern(f));
    }
    if (out.pattern.empty()) {
        return false;
    } else if (dynamic_cast<FixedPattern *>(out.pattern[0].get())) {
        auto s = (*out.pattern[0])(nullptr);
        if (g_path_is_absolute(s.c_str())) {
            return false;
        }
    }
    return true;
}


Glib::ustring trim(const Glib::ustring &s, bool start=true, bool end=true)
{
    size_t i = 0;
    size_t n = s.length();
    while (start && isspace(s[i])) {
        ++i;
    }
    while (end && n > 0 && isspace(s[n-1])) {
        --n;
    }
    return s.substr(i, n-i);
}


bool parse_sidecars(const Glib::ustring &s, Params &out)
{
    out.sidecars.clear();
    size_t prev = 0;
    size_t n = s.length();
    for (size_t i = 0; i < n; ++i) {
        if (s[i] == ';') {
            auto e = trim(s.substr(prev, i-prev));
            if (!e.empty()) {
                out.sidecars.push_back(e);
            }
            ++i;
            prev = i;
        }
    }
    if (prev < n) {
        auto e = trim(s.substr(prev));
        if (!e.empty()) {
            out.sidecars.push_back(e);
        }
    }
    return true;
}


Glib::ustring get_new_name(Params &params, FileBrowserEntry *entry)
{
    std::unique_ptr<FramesMetaData> fd(new FastMetadata(entry->thumbnail->getFileName(), entry->thumbnail->getCacheImageData()));

    Glib::ustring name;
    for (auto &p : params.pattern) {
        name += (*p)(fd.get());
    }
    Glib::ustring ext = getExtension(name);
    if (!ext.empty()) {
        ext = "." + ext;
        name = removeExtension(name);
    }
    Glib::ustring ret;
    
    for (auto c : name) {
        if (!params.allow_whitespace && isspace(c)) {
            c = '_';
        }
        switch (params.name_norm) {
        case Params::Normalization::UPPERCASE:
            c = toupper(c);
            break;
        case Params::Normalization::LOWERCASE:
            c = tolower(c);
            break;
        default:
            break;
        }
        ret.push_back(c);
    }
    for (auto c : ext) {
        if (!params.allow_whitespace && isspace(c)) {
            c = '_';
        }
        switch (params.ext_norm) {
        case Params::Normalization::UPPERCASE:
            c = toupper(c);
            break;
        case Params::Normalization::LOWERCASE:
            c = tolower(c);
            break;
        default:
            break;
        }
        ret.push_back(c);
    }

    if (!params.basedir.empty() && params.basedir != ".") {
        ret = Glib::build_filename(params.basedir, ret);
    }
    
    return ret;
}


bool get_params(Gtk::Window &parent, const std::vector<FileBrowserEntry *> &args, Params &out, bool move)
{
    Gtk::Dialog dialog(M(move ? "FILEBROWSER_RENAMEDLGLABEL" : "FILEBROWSER_RENAME_DIALOG_COPY_LABEL"), parent);
    Gtk::Label lbl(M("RENAME_DIALOG_PATTERN"));
    MyFileChooserButton basedir(M("RENAME_DIALOG_BASEDIR"), Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);
    Gtk::Entry pattern;
    Gtk::VBox vb;
    Gtk::HBox hb;
    Gtk::HBox mainhb;
    Gtk::VBox mainvb;
    double s = RTScalable::getScale();
    int pad = 4 * s;

    Gtk::Label lbld(M("RENAME_DIALOG_BASEDIR"));
    Gtk::HBox hbd;
    hbd.pack_start(lbld, Gtk::PACK_SHRINK, pad);
    hbd.pack_start(basedir, Gtk::PACK_EXPAND_WIDGET, pad);
    mainvb.pack_start(hbd, Gtk::PACK_SHRINK, pad);
    
    hb.pack_start(lbl, Gtk::PACK_SHRINK, pad);
    hb.pack_start(pattern, Gtk::PACK_EXPAND_WIDGET, pad);
    vb.pack_start(hb, Gtk::PACK_SHRINK, pad);
    pattern.set_tooltip_markup(M("RENAME_DIALOG_PATTERN_TIP"));

    Gtk::HBox hb2;
    Gtk::Label lbl7(M("RENAME_DIALOG_ALLOW_WHITESPACE"));
    Gtk::CheckButton allow_whitespace("");
    hb2.pack_start(lbl7, Gtk::PACK_SHRINK, pad);
    hb2.pack_start(allow_whitespace, Gtk::PACK_SHRINK, pad);
    mainvb.pack_start(hb2, Gtk::PACK_SHRINK, pad);

    Gtk::HBox hb3;
    Gtk::Label lbl2(M("RENAME_DIALOG_NAME_NORMALIZATION"));
    hb3.pack_start(lbl2, Gtk::PACK_SHRINK, pad);
    Gtk::ComboBoxText name_norm;
    name_norm.append(M("RENAME_DIALOG_NORM_OFF"));
    name_norm.append(M("RENAME_DIALOG_NORM_UPPERCASE"));
    name_norm.append(M("RENAME_DIALOG_NORM_LOWERCASE"));
    hb3.pack_start(name_norm, Gtk::PACK_EXPAND_WIDGET, pad);
    mainvb.pack_start(hb3, Gtk::PACK_SHRINK, pad);

    Gtk::Box hb4;
    Gtk::Label lbl3(M("RENAME_DIALOG_EXT_NORMALIZATION"));
    hb4.pack_start(lbl3, Gtk::PACK_SHRINK, pad);
    Gtk::ComboBoxText ext_norm;
    ext_norm.append(M("RENAME_DIALOG_NORM_OFF"));
    ext_norm.append(M("RENAME_DIALOG_NORM_UPPERCASE"));
    ext_norm.append(M("RENAME_DIALOG_NORM_LOWERCASE"));
    hb4.pack_start(ext_norm, Gtk::PACK_EXPAND_WIDGET, pad);
    mainvb.pack_start(hb4, Gtk::PACK_SHRINK, pad);
    
    Gtk::HBox hb6;
    Gtk::Label lbl5(M("RENAME_DIALOG_ON_EXISTING"));
    hb6.pack_start(lbl5, Gtk::PACK_SHRINK, pad);
    Gtk::ComboBoxText on_existing;
    on_existing.append(M("RENAME_DIALOG_SKIP"));
    on_existing.append(M("RENAME_DIALOG_RENAME"));
    hb6.pack_start(on_existing, Gtk::PACK_EXPAND_WIDGET, pad);
    mainvb.pack_start(hb6, Gtk::PACK_SHRINK, pad);

    Gtk::HBox hb7;
    Gtk::Label lbl6(M("RENAME_DIALOG_PROGRESSIVE"));
    hb7.pack_start(lbl6, Gtk::PACK_SHRINK, pad);
    Gtk::SpinButton progressive_number;
    progressive_number.set_range(1, 1000000);
    progressive_number.set_increments(1, 1);
    progressive_number.set_value(1);
    hb7.pack_start(progressive_number, Gtk::PACK_EXPAND_WIDGET, pad);
    mainvb.pack_start(hb7, Gtk::PACK_SHRINK, pad);

    Gtk::HBox hb5;
    Gtk::Label lbl4(M("RENAME_DIALOG_SIDECARS"));
    hb5.pack_start(lbl4, Gtk::PACK_SHRINK, pad);
    Gtk::Entry sidecars;
    sidecars.set_tooltip_markup(M("RENAME_DIALOG_SIDECARS_TIP"));
    hb5.pack_start(sidecars, Gtk::PACK_EXPAND_WIDGET, pad);
    mainvb.pack_start(hb5, Gtk::PACK_SHRINK, pad);
    
    Gtk::ListViewText filelist(1);
    filelist.set_column_title(0, M(move ? "RENAME_DIALOG_FILENAMES" : "RENAME_DIALOG_FILENAMES_COPY") + " (" + std::to_string(args.size()) + ")");
    filelist.set_activate_on_single_click(true);
    for (auto &e : args) {
        filelist.append(Glib::path_get_basename(e->thumbnail->getFileName()));
    }
    if (!args.empty()) {
        Gtk::TreePath pth;
        pth.push_back(0);
        filelist.get_selection()->select(pth);
    }
    Gtk::ScrolledWindow sw;
    sw.add(filelist);
    sw.set_size_request(200 * s, -1);
    mainhb.pack_start(sw, Gtk::PACK_SHRINK, pad);
    mainhb.pack_start(mainvb, Gtk::PACK_EXPAND_WIDGET);
    vb.pack_start(mainhb, Gtk::PACK_SHRINK);

    Gtk::HBox hb8;
    Gtk::Label info;
    info.set_markup("<span size=\"large\"><b>" + M("RENAME_DIALOG_PREVIEW") + ": </b></span>");
    hb8.pack_start(info, Gtk::PACK_SHRINK, 2*pad);
    Gtk::Label empty;
    hb8.pack_start(empty, Gtk::PACK_EXPAND_WIDGET);
    vb.pack_start(hb8, Gtk::PACK_SHRINK);
    
    dialog.get_content_area()->pack_start(vb, Gtk::PACK_SHRINK, 2*pad);
    auto okbtn = dialog.add_button(M("GENERAL_OK"), 1);
    dialog.add_button(M("GENERAL_CANCEL"), 0);
    dialog.set_size_request(600, -1);
    dialog.show_all_children();


    const auto getparams =
        [&]() -> bool
        {
            bool err = false;
            Glib::ustring errmsg = "";
            Glib::ustring patternstr = pattern.get_text();
            if (!parse_pattern(patternstr, out)) {
                errmsg = M("RENAME_DIALOG_INVALID_PATTERN");
                err = true;
            } else if (!parse_sidecars(sidecars.get_text(), out)) {
                errmsg = M("RENAME_DIALOG_INVALID_SIDECARS");
                err = true;
            }
            out.basedir = basedir.get_filename();

            info.set_markup("<span size=\"large\"><b>" + M("RENAME_DIALOG_PREVIEW") + ": <span foreground=\"#ff0000\">" + errmsg + "</span></b></span>");
            
            if (err) {
                okbtn->set_sensitive(false);
                return false;
            } 

            okbtn->set_sensitive(true);
            
            out.name_norm = Params::Normalization(name_norm.get_active_row_number());
            out.ext_norm = Params::Normalization(ext_norm.get_active_row_number());
            out.allow_whitespace = allow_whitespace.get_active();
            out.on_existing = Params::OnExistingAction(on_existing.get_active_row_number());
            out.progressive_number = progressive_number.get_value_as_int();

            if (options.renaming.remember) {
                options.renaming.pattern = patternstr;
                options.renaming.sidecars = sidecars.get_text();
                options.renaming.name_norm = name_norm.get_active_row_number();
                options.renaming.ext_norm = ext_norm.get_active_row_number();
                options.renaming.allow_whitespace = allow_whitespace.get_active();
                options.renaming.on_existing = on_existing.get_active_row_number();
                options.renaming.progressive_number = progressive_number.get_value_as_int();
                options.lastCopyMovePath = out.basedir;
            }

            return true;
        };

    const auto on_pattern_change =
        [&]() -> void
        {
            if (getparams()) {
                auto sel = filelist.get_selected();
                if (!sel.empty()) {
                    auto entry = args[sel[0]];
                    Glib::ustring newname = get_new_name(out, entry);
                    Glib::ustring show;
                    for (auto c : newname) {
                        if (isspace(c)) {
                            show += "<span foreground=\"#E59836\">";
                            show.push_back(gunichar(9141));
                            show += "</span>";
                        } else if (G_IS_DIR_SEPARATOR(c)) {
                            show += "<span foreground=\"#E59836\">";
                            show.push_back(c);
                            show += "</span>";                               
                        } else {
                            show.push_back(c);
                        }
                    }
                    info.set_markup("<span size=\"large\"><b>" + M("RENAME_DIALOG_PREVIEW") + ": " + show + "</b></span>");
                }
            }
        };

    const auto on_file_select =
        [&](const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* column) -> void
        {
            on_pattern_change();
        };

    if (!Glib::file_test(options.lastCopyMovePath, Glib::FILE_TEST_IS_DIR)) {
        options.lastCopyMovePath = ".";
    }
    if (options.renaming.remember) {
        basedir.set_filename(options.lastCopyMovePath);
        pattern.set_text(options.renaming.pattern);
        sidecars.set_text(options.renaming.sidecars);
        name_norm.set_active(options.renaming.name_norm);
        ext_norm.set_active(options.renaming.ext_norm);
        on_existing.set_active(options.renaming.on_existing);
        allow_whitespace.set_active(options.renaming.allow_whitespace);
        progressive_number.set_value(options.renaming.progressive_number);
    } else {
        auto r = Options::RenameOptions();
        basedir.set_filename(".");
        pattern.set_text(r.pattern);
        sidecars.set_text(r.sidecars);
        name_norm.set_active(r.name_norm);
        ext_norm.set_active(r.ext_norm);
        on_existing.set_active(r.on_existing);
        allow_whitespace.set_active(r.allow_whitespace);
        progressive_number.set_value(r.progressive_number);
    }

    pattern.signal_changed().connect(sigc::slot<void>(on_pattern_change));
    name_norm.signal_changed().connect(sigc::slot<void>(on_pattern_change));
    ext_norm.signal_changed().connect(sigc::slot<void>(on_pattern_change));
    on_existing.signal_changed().connect(sigc::slot<void>(on_pattern_change));
    allow_whitespace.signal_toggled().connect(sigc::slot<void>(on_pattern_change));
    progressive_number.signal_value_changed().connect(sigc::slot<void>(on_pattern_change));
    filelist.signal_row_activated().connect(sigc::slot<void, const Gtk::TreeModel::Path &, Gtk::TreeViewColumn *>(on_file_select));
    basedir.signal_file_set().connect(sigc::slot<void>(on_pattern_change));

    on_pattern_change();

    return (dialog.run() == 1) && getparams();
}


void get_targets(Params &params, FileBrowserEntry *entry,
                 std::vector<std::pair<Glib::ustring, Glib::ustring>> &out)
{
    out.clear();
    
    auto fn = entry->thumbnail->getFileName();
    auto dir = Glib::path_get_dirname(fn);
    auto name = Glib::path_get_basename(fn);
    auto newname = get_new_name(params, entry);
    auto newpath = g_path_is_absolute(newname.c_str()) ? newname : Glib::ustring(Glib::build_filename(dir, newname));
    if (Glib::file_test(newpath, Glib::FILE_TEST_EXISTS)) {
        if (params.on_existing == Params::OnExistingAction::RENAME) {
            auto bn = removeExtension(newname);
            auto ext = getExtension(newname);
            if (!ext.empty()) {
                ext = "." + ext;
            }
            for (int i = 1; ; ++i) {
                auto nn = bn + "_" + std::to_string(i) + ext;
                newpath = Glib::build_filename(dir, nn);
                if (!Glib::file_test(newpath, Glib::FILE_TEST_EXISTS)) {
                    newname = nn;
                    break;
                }
            }
        } else {
            return; // skip
        }
    }
    out.push_back(std::make_pair(fn, newpath));
    auto pf = options.getParamFile(fn);
    if (Glib::file_test(pf, Glib::FILE_TEST_EXISTS)) {
        out.push_back(std::make_pair(pf, options.getParamFile(newpath)));
    }
    auto xmp = options.getXmpSidecarFile(fn);
    if (Glib::file_test(xmp, Glib::FILE_TEST_EXISTS)) {
        out.push_back(std::make_pair(xmp, options.getXmpSidecarFile(newpath)));
    }

    const auto ok =
        [&](const Glib::ustring &name) -> bool
        {
            if (!Glib::file_test(name, Glib::FILE_TEST_EXISTS)) {
                return false;
            }
            for (auto &p : out) {
                if (p.first == name) {
                    return false;
                }
            }
            return true;
        };
    
    if (!params.sidecars.empty()) {
        auto base_fn = removeExtension(fn);
        auto base_newpath = removeExtension(newpath);
        for (auto &s : params.sidecars) {
            Glib::ustring orig_sidename;
            Glib::ustring new_sidename;
            if (s[0] == '+') {
                orig_sidename = fn + "." + s.substr(1);
                new_sidename = newpath + "." + s.substr(1);
            } else {
                orig_sidename = base_fn + "." + s;
                new_sidename = base_newpath + "." + s;
            }
            if (ok(orig_sidename)) {
                out.push_back(std::make_pair(orig_sidename, new_sidename));
            }
        }
    }
}


template <class Func>
void run_with_progress(const std::vector<FileBrowserEntry *> &args, Func func, const Glib::ustring &msg, Gtk::Window &parent)
{
    if (args.empty()) {
        return;
    }
    
    Gtk::MessageDialog dlg(parent, Glib::ustring::compose("%1 (0/%2)", msg, std::to_string(args.size())), false, Gtk::MESSAGE_INFO, Gtk::BUTTONS_CANCEL, true);
    Gtk::ProgressBar progress;
    progress.set_fraction(0);
    dlg.set_title(msg);
    dlg.get_message_area()->pack_start(progress, Gtk::PACK_SHRINK, 4);
    dlg.show_all_children();

    unsigned int i = 0;
    bool done = false;

    IdleRegister reg;
    reg.add([&]() -> bool
            {
                if (done || i == args.size()) {
                    dlg.response(Gtk::RESPONSE_CANCEL);
                    return false;
                } else {
                    func(args[i]);
                    ++i;
                    progress.set_fraction(i / double(args.size()));
                    dlg.set_message(Glib::ustring::compose("%1 (%2/%3)", msg, std::to_string(i), std::to_string(args.size())));
                    return true;
                }
            });
    
    dlg.run();
    done = true;
}

} // namespace


void FileCatalog::copyMoveRequested(const std::vector<FileBrowserEntry *> &args, bool move)
{
    const auto doit =
        [&](const Glib::ustring &s, const Glib::ustring &d) -> bool
        {
            if (move) {
                errno = 0;
                if (::g_rename(s.c_str(), d.c_str()) == 0) {
                    return true;
                } else if (errno == EXDEV) {
                    try {
                        auto fs = Gio::File::create_for_path(s);
                        auto fd = Gio::File::create_for_path(d);
                        fs->copy(fd);
                        return ::g_remove(s.c_str()) == 0;
                    } catch (Glib::Error &exc) {
                        if (options.rtSettings.verbose) {
                            std::cout << "copy error: " << exc.what() << std::endl;
                        }
                        return false;
                    }
                } else {
                    return false;
                }
            } else {
                try {
                    auto fs = Gio::File::create_for_path(s);
                    auto fd = Gio::File::create_for_path(d);
                    fs->copy(fd);
                    return true;
                } catch (Glib::Error &exc) {
                    if (options.rtSettings.verbose) {
                        std::cout << "copy error: " << exc.what() << std::endl;
                    }
                    return false;
                }
            }
        };
    
    Params params;
    if (get_params(getToplevelWindow(this), args, params, move)) {
        const bool is_session = art::session::check(selectedDirectory);
        
        if (!move) {
            removeFromBatchQueue(args);
        }
        std::vector<Glib::ustring> session_add, session_rem;
        
        std::vector<std::pair<Glib::ustring, Glib::ustring>> torename;

        const auto func =
            [&](FileBrowserEntry *e) -> void
            {
                get_targets(params, e, torename);
                bool first = true;
                for (auto &p : torename) {
                    auto destdir = Glib::path_get_dirname(p.second);
                    if (::g_mkdir_with_parents(destdir.c_str(), 0755) == 0 &&
                        doit(p.first, p.second)) {
                        if (first && move) {
                            cacheMgr->renameEntry(p.first, e->thumbnail->getMD5(), p.second);
                            if (is_session) {
                                session_add.push_back(p.second);
                                session_rem.push_back(p.first);
                            }
                        }
                    } else {
                        filepanel->getParent()->error(Glib::ustring::compose(M("RENAME_DIALOG_ERROR"), p.first, p.second));
                    }
                    first = false;
                }
            };

        {
            ConnectionBlocker blocker(dir_refresh_conn_);
            run_with_progress(args, func, M(move ? "PROGRESSBAR_FILE_RENAME" : "PROGRESSBAR_FILE_COPY"), getToplevelWindow(this));
        }

        if (is_session && session_add.size() + session_rem.size() > 0) {
            art::session::remove(session_rem);
            art::session::add(session_add);
        } else {
            reparseDirectory();
        }
    }
}
    

void FileCatalog::deleteRequested(const std::vector<FileBrowserEntry*>& tbe, bool onlySelected)
{
    if (tbe.empty()) {
        return;
    }

    Gtk::MessageDialog msd(getToplevelWindow(this), M("FILEBROWSER_DELETEDIALOG_HEADER"), true, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO, true);

    Gtk::HBox hb5;
    Gtk::Label lbl4(M("RENAME_DIALOG_SIDECARS"));
    constexpr int pad = 4;
    hb5.pack_start(lbl4, Gtk::PACK_SHRINK, pad);
    Gtk::Entry sidecars;
    sidecars.set_text(options.renaming.sidecars);
    sidecars.set_tooltip_markup(M("RENAME_DIALOG_SIDECARS_TIP"));
    hb5.pack_start(sidecars, Gtk::PACK_EXPAND_WIDGET, pad);
    msd.get_message_area()->pack_start(hb5, Gtk::PACK_SHRINK, pad);
    msd.set_default_response(Gtk::RESPONSE_YES);
    sidecars.set_activates_default(true);
    msd.show_all_children();

    if (onlySelected) {
        msd.set_secondary_text(Glib::ustring::compose (M("FILEBROWSER_DELETEDIALOG_SELECTED"), tbe.size()), true);
    } else {
        msd.set_secondary_text(Glib::ustring::compose (M("FILEBROWSER_DELETEDIALOG_ALL"), tbe.size()), true);
    }

    if (msd.run() == Gtk::RESPONSE_YES) {
        removeFromBatchQueue(tbe);
        Params params;
        bool err = false;
        Glib::ustring errmsg;
        if (!parse_sidecars(sidecars.get_text(), params)) {
            errmsg = M("RENAME_DIALOG_INVALID_SIDECARS");
            err = true;
        }

        if (!err) {
            const bool is_session = art::session::check(selectedDirectory);
            std::vector<Glib::ustring> session_rem;
            
            options.renaming.sidecars = sidecars.get_text();
            
            // for (unsigned int i = 0; i < tbe.size(); i++) {
            const auto func =
                [&](FileBrowserEntry *e) -> void
                {
                    //const auto fname = tbe[i]->filename;
                    const auto fname = e->filename;
                    // remove from browser
                    delete fileBrowser->delEntry(fname);
                    // remove from cache
                    cacheMgr->deleteEntry(fname);
                    // delete from file system
                    ::g_remove(fname.c_str());
                    // delete also the arp sidecar
                    ::g_remove(options.getParamFile(fname).c_str());

                    if (!params.sidecars.empty()) {
                        auto base_fn = removeExtension(fname);
                        for (auto &s : params.sidecars) {
                            Glib::ustring sidename;
                            if (s[0] == '+') {
                                sidename = fname + "." + s.substr(1);
                            } else {
                                sidename = base_fn + "." + s;
                            }
                            if (Glib::file_test(sidename, Glib::FILE_TEST_EXISTS)) {
                                ::g_remove(sidename.c_str());
                            }
                        }
                    }

                    previewsLoaded--;

                    if (is_session) {
                        session_rem.push_back(fname);
                    }
                };

            {
                ConnectionBlocker blocker(dir_refresh_conn_);
                run_with_progress(tbe, func, M("PROGRESSBAR_FILE_DELETE"), getToplevelWindow(this));
            }

            _refreshProgressBar();
            if (is_session) {
                art::session::remove(session_rem);
            } else {
                redrawAll();
            }
        } else {
            Gtk::MessageDialog errd(getToplevelWindow(this), errmsg, true, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
            errd.run();
        }
    }
}
