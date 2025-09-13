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

#pragma once

#include <vector>
#include <glibmm/ustring.h>
#include <glibmm/regex.h>
#include "thumbnail.h"


class UserCommand {
public:
    Glib::ustring command;
    Glib::ustring label;
    
    std::string camera;
    std::vector<std::string> extensions;
    size_t min_args;
    size_t max_args;
    enum FileType {
        RAW,
        NONRAW,
        ANY
    };
    FileType filetype;
    
    bool match_camera;
    bool match_lens;
    bool match_shutter;
    bool match_iso;
    bool match_aperture;
    bool match_focallen;
    bool match_dimensions;

    UserCommand();
    bool matches(const std::vector<Thumbnail *> &args) const;
    void execute(const std::vector<Thumbnail *> &args) const;
};


class UserCommandStore {
public:
    static UserCommandStore *getInstance();
    void init(const Glib::ustring &dir);

    std::vector<UserCommand> getCommands(const std::vector<Thumbnail *> &sel) const;
    const std::vector<UserCommand> &getAllCommands() const { return commands_; }
    const std::string &dir() const { return dir_; }
    const std::string &getPathEnvVar(bool for_user_command) const { return for_user_command ? ucpath_ : origpath_; }

private:
    std::string dir_;
    std::string origpath_;
    std::string ucpath_;
    std::vector<UserCommand> commands_;
};


namespace ExtProg {

bool spawnCommandAsync(const Glib::ustring &cmd);
bool spawnCommandSync(const Glib::ustring &cmd);

bool openInGimp(const Glib::ustring &fileName);
bool openInPhotoshop(const Glib::ustring &fileName);
bool openInCustomEditor(const Glib::ustring &fileName);

} // namespace ExtProg
