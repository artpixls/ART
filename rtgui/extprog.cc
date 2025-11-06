/* -*- C++ -*-
*  
*  This file is part of ART.
*
*  Copyright (c) 2021 Alberto Griggio <alberto.griggio@gmail.com>
*  Copyright (c) 2012 Oliver Duis <www.oliverduis.de>
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
#include "extprog.h"

#include <cstring>
#include <iostream>
#include <limits>
#include <thread>

#ifdef WIN32
#include <windows.h>
#include <shlobj.h>
#endif

#include "config.h"
#include "options.h"
#include "multilangmgr.h"
#include "../rtengine/utils.h"
#include "../rtengine/subprocess.h"


namespace {

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

class PathSetter {
public:
    PathSetter()
    {
        auto uc = UserCommandStore::getInstance();
        pth_ = uc->getPathEnvVar(false);
        auto p = uc->getPathEnvVar(true);
        if (options.rtSettings.verbose > 1) {
            std::cout << "setting PATH for user command to: " << p << std::endl;
        }
        Glib::setenv("PATH", p);
    }

    ~PathSetter()
    {
        Glib::setenv("PATH", pth_);
    }

private:
    std::string pth_;
};

} // namespace


UserCommand::UserCommand():
    command(""),
    label(""),
    camera("^.*$"),
    extensions(),
    min_args(1),
    max_args(std::numeric_limits<size_t>::max()),
    filetype(ANY),
    match_camera(false),
    match_lens(false),
    match_shutter(false),
    match_iso(false),
    match_aperture(false),
    match_focallen(false),
    match_dimensions(false)
{
}


bool UserCommand::matches(const std::vector<Thumbnail *> &args) const
{
    size_t n = args.size();
    if (!n || n < min_args || n > max_args) {
        return false;
    }

    auto md = args[0]->getMetaData();
    int ow = 0, oh = 0;
    if (match_dimensions) {
        args[0]->getOriginalSize(ow, oh);
    }

    for (size_t i = 0; i < n; ++i) {
        auto mdi = args[i]->getCacheImageData();
        if (i > 0) {
            if (match_camera && (md->getMake() != mdi->getMake() ||
                                 md->getModel() != mdi->getModel())) {
                return false;
            }
            if (match_lens && md->getLens() != mdi->getLens()) {
                return false;
            }
            if (match_shutter && md->getShutterSpeed() != mdi->getShutterSpeed()) {
                return false;
            }
            if (match_iso && md->getISOSpeed() != mdi->getISOSpeed()) {
                return false;
            }
            if (match_aperture && md->getFNumber() != mdi->getFNumber()) {
                return false;
            }
            if (match_focallen && md->getFocalLen() != mdi->getFocalLen()) {
                return false;
            }
            if (match_dimensions) {
                int w = 0, h = 0;
                args[i]->getOriginalSize(w, h);
                if (w != ow || h != oh) {
                    return false;
                }
            }
        }

        if (!Glib::Regex::match_simple(camera, mdi->getMake() + " " + mdi->getModel(), Glib::REGEX_CASELESS)) {
            return false;
        }
        if (filetype != ANY && (args[i]->getType() == FT_Raw) != (filetype == RAW)) {
            return false;
        }
        if (!extensions.empty()) {
            auto ext = std::string(rtengine::getFileExtension(args[i]->getFileName()).lowercase());
            if (std::find(extensions.begin(), extensions.end(), ext) == extensions.end()) {
                return false;
            }
        }
    }

    return true;
}


void UserCommand::execute(const std::vector<Thumbnail *> &args) const
{
    if (args.empty()) {
        return;
    }

    std::vector<Glib::ustring> argv = rtengine::subprocess::split_command_line(command);
    
    for (auto &t : args) {
        t->updateCache(true, false);
        argv.push_back(t->getFileName());
    }

    const auto doit =
        [=](bool verb) -> void
        {
            try {
                PathSetter ps;
                rtengine::subprocess::exec_sync(UserCommandStore::getInstance()->dir(), argv, true, nullptr, nullptr);
            } catch (rtengine::subprocess::error &exc) {
                if (verb) {
                    std::cerr << "Failed to execute \"" << command << "\": " << exc.what() << std::endl;
                }
            }
        };

    std::thread(doit, options.rtSettings.verbose).detach();
}


UserCommandStore *UserCommandStore::getInstance()
{
    static UserCommandStore instance;
    return &instance;
}


void UserCommandStore::init(const Glib::ustring &dirname)
{
    origpath_ = Glib::getenv("PATH");
    std::string extrapath;
#ifdef BUILD_BUNDLE
# ifdef __APPLE__
    extrapath = Glib::build_filename(options.ART_base_dir, "../MacOS") + G_SEARCHPATH_SEPARATOR_S + options.ART_base_dir;
# else // __APPLE__
    extrapath = options.ART_base_dir;
# endif // __APPLE__
#endif // BUILD_BUNDLE
    auto epth = Glib::getenv("ART_EXIFTOOL_BASE_DIR");
    if (!epth.empty()) {
        extrapath += G_SEARCHPATH_SEPARATOR_S + epth;
    }
    ucpath_ = extrapath + G_SEARCHPATH_SEPARATOR_S + origpath_;
    
    commands_.clear();

    if (!Glib::file_test(dirname, Glib::FILE_TEST_IS_DIR)) {
        return;
    }
    dir_ = Glib::filename_from_utf8(dirname);

    try {
        Glib::Dir dir(dirname);
        std::vector<std::string> dirlist(dir.begin(), dir.end());
        std::sort(dirlist.begin(), dirlist.end());

        for (auto &filename : dirlist) {
            auto ext = rtengine::getFileExtension(filename).lowercase();
            if (ext != "txt") {
                continue;
            }
            
            const Glib::ustring pth = Glib::build_filename(dirname, filename);

            if (!Glib::file_test(pth, Glib::FILE_TEST_IS_REGULAR)) {
                continue;
            }

            try {
                const Glib::ustring group = "ART UserCommand";
                Glib::KeyFile kf;
                if (!kf.load_from_file(pth)) {
                    continue;
                }

                UserCommand cmd;
                if (kf.has_key(group, "Command")) {
                    cmd.command = kf.get_string(group, "Command");
                } else {
                    continue;
                }

                if (kf.has_key(group, "Label")) {
                    cmd.label = kf.get_string(group, "Label");
                } else {
                    continue;
                }

                if (kf.has_key(group, "Camera")) {
                    cmd.camera = kf.get_string(group, "Camera");
                }

                if (kf.has_key(group, "Extension")) {
                    cmd.extensions.clear();
                    try {
                        auto el = kf.get_string_list(group, "Extension");
                        for (const auto &e : el) {
                            cmd.extensions.emplace_back(e.lowercase());
                        }
                    } catch (Glib::KeyFileError &) {
                        cmd.extensions.emplace_back(kf.get_string(group, "Extension").lowercase());
                    }
                }

                if (kf.has_key(group, "MinArgs")) {
                    cmd.min_args = kf.get_integer(group, "MinArgs");
                }

                if (kf.has_key(group, "MaxArgs")) {
                    cmd.max_args = kf.get_integer(group, "MaxArgs");
                }

                if (kf.has_key(group, "NumArgs")) {
                    cmd.min_args = cmd.max_args = kf.get_integer(group, "NumArgs");
                }

                if (kf.has_key(group, "FileType")) {
                    auto tp = kf.get_string(group, "FileType").lowercase();
                    if (tp == "raw") {
                        cmd.filetype = UserCommand::RAW;
                    } else if (tp == "nonraw") {
                        cmd.filetype = UserCommand::NONRAW;
                    } else {
                        cmd.filetype = UserCommand::ANY;
                    }
                }

                const auto getbool =
                    [&](const char *k) -> bool
                    {
                        return kf.has_key(group, k) && kf.get_boolean(group, k);
                    };
                cmd.match_camera = getbool("MatchCamera");
                cmd.match_lens = getbool("MatchLens");
                cmd.match_shutter = getbool("MatchShutter");
                cmd.match_iso = getbool("MatchISO");
                cmd.match_aperture = getbool("MatchAperture");
                cmd.match_focallen = getbool("MatchFocalLen");
                cmd.match_dimensions = getbool("MatchDimensions");
            
                commands_.push_back(cmd);

                if (options.rtSettings.verbose > 1) {
                    std::cout << "Found user command \"" << S(cmd.label)
                              << "\": " << S(cmd.command) << std::endl;
                }
            } catch (Glib::Exception &exc) {
                std::cout << "ERROR loading " << S(pth) << ": " << S(exc.what())
                          << std::endl;
            }
        }
    } catch (Glib::Exception &exc) {
        std::cout << "ERROR scanning " << S(dirname) << ": " << S(exc.what()) << std::endl;
    }

    if (options.rtSettings.verbose) {
        std::cout << "Loaded " << commands_.size() << " user commands"
                  << std::endl;
    }
}


std::vector<UserCommand> UserCommandStore::getCommands(const std::vector<Thumbnail *> &sel) const
{
    std::vector<UserCommand> ret;
    for (auto &c : commands_) {
        if (c.matches(sel)) {
            ret.push_back(c);
        }
    }
    return ret;
}


namespace ExtProg {

bool spawnCommandAsync(const Glib::ustring &cmd)
{
    try {
        Glib::spawn_async("", Glib::shell_parse_argv(cmd), rtengine::subprocess::get_env(), Glib::SPAWN_SEARCH_PATH_FROM_ENVP);

        return true;

    } catch (const Glib::Exception& exception) {

        if (options.rtSettings.verbose) {
            std::cerr << "Failed to execute \"" << cmd << "\": " << exception.what() << std::endl;
        }

        return false;

    }
}


bool spawnCommandSync(const Glib::ustring &cmd)
{
    auto exitStatus = -1;

    try {
        Glib::spawn_sync("", Glib::shell_parse_argv(cmd), rtengine::subprocess::get_env(), Glib::SPAWN_SEARCH_PATH_FROM_ENVP, {}, nullptr, nullptr, &exitStatus);
    } catch (const Glib::Exception& exception) {

        if (options.rtSettings.verbose) {
            std::cerr << "Failed to execute \"" << cmd << "\": " << exception.what() << std::endl;
        }

    }

    return exitStatus == 0;
}


bool openInGimp(const Glib::ustring &fileName)
{
#if defined WIN32

    auto executable = rtengine::subprocess::to_wstr(Glib::build_filename (options.gimpDir, "bin", "gimp-win-remote"));
    auto fn = rtengine::subprocess::quote(rtengine::subprocess::to_wstr(fileName)); //'"' + fileName + '"';
    auto open = rtengine::subprocess::to_wstr("open");
    auto success = ShellExecuteW( NULL, open.c_str(), executable.c_str(), fn.c_str(), NULL, SW_SHOWNORMAL );

#elif defined __APPLE__

    auto cmdLine = Glib::ustring("open -b \'org.gimp.gimp\' \'") + fileName + Glib::ustring("\'");
    auto success = spawnCommandAsync (cmdLine);

#else

    auto cmdLine = Glib::ustring("gimp \"") + fileName + Glib::ustring("\"");
    auto success = spawnCommandAsync (cmdLine);

#endif

#ifdef WIN32
    if ((uintptr_t)success > 32) {
        return true;
    }
#else
    if (success) {
        return true;
    }

#endif

#ifdef WIN32

    for (auto major = 3; major >= 2; --major) {
        for (auto minor = (major == 3 ? 1 : 12); minor >= 0; --minor) {

            executable = rtengine::subprocess::to_wstr(Glib::build_filename (options.gimpDir, "bin", Glib::ustring::compose (Glib::ustring("gimp-%1.%2.exe"), major, minor)));
            auto success = ShellExecuteW( NULL, open.c_str(), executable.c_str(), fn.c_str(), NULL, SW_SHOWNORMAL );

            if ((uintptr_t)success > 32) {
                return true;
            }
        }
    }

#elif defined __APPLE__

    cmdLine = Glib::ustring("open -a GIMP-dev \'") + fileName + Glib::ustring("\'");
    success = spawnCommandAsync(cmdLine);

#else

    cmdLine = Glib::ustring("gimp-remote \"") + fileName + Glib::ustring("\"");
    success = spawnCommandAsync(cmdLine);

#endif

    return success;
}


bool openInPhotoshop(const Glib::ustring& fileName)
{
#if defined WIN32

    const auto executable = rtengine::subprocess::to_wstr(Glib::build_filename(options.psDir, "Photoshop.exe"));
    const auto fn = rtengine::subprocess::quote(rtengine::subprocess::to_wstr(fileName));
    auto open = rtengine::subprocess::to_wstr("open");
    auto success = ShellExecuteW(NULL, open.c_str(), executable.c_str(), fn.c_str(), NULL, SW_SHOWNORMAL);
    return (uintptr_t)success > 32;

#elif defined __APPLE__

    const auto cmdLine = Glib::ustring("open -b \'com.adobe.Photoshop\' \'") + fileName + Glib::ustring("\'");
    return spawnCommandAsync (cmdLine);

#else

    const auto cmdLine = Glib::ustring("\"") + Glib::build_filename(options.psDir, "Photoshop.exe") + Glib::ustring("\" \"") + fileName + Glib::ustring("\"");
    return spawnCommandAsync (cmdLine);
    
#endif
}


bool openInCustomEditor(const Glib::ustring& fileName)
{
#if defined WIN32

    const auto cmdLine = rtengine::subprocess::to_wstr(options.customEditorProg);
    auto fn = rtengine::subprocess::quote(rtengine::subprocess::to_wstr(fileName));
    auto open = rtengine::subprocess::to_wstr("open");
    auto success = ShellExecuteW( NULL, open.c_str(), cmdLine.c_str(), fn.c_str(), NULL, SW_SHOWNORMAL );
    return (uintptr_t)success > 32;

#elif defined __APPLE__

    const auto cmdLine = options.customEditorProg + Glib::ustring(" \"") + fileName + Glib::ustring("\"");
    return spawnCommandAsync (cmdLine);

#else

    const auto cmdLine = Glib::ustring("\"") + options.customEditorProg + Glib::ustring("\" \"") + fileName + Glib::ustring("\"");
    return spawnCommandAsync (cmdLine);

#endif
}

} // namespace ExtProg
