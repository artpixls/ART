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

#include "shortcutmanager.h"
#include "rtwindow.h"
#include "toolpanel.h"
#include "options.h"
#include "multilangmgr.h"

#include <map>
#include <vector>

extern Options options;


ToolShortcutManager::ToolShortcutManager(RTWindow *parent):
    parent_(parent),
    cur_key_(0),
    cur_tool_(nullptr),
    cur_adjuster_(nullptr)
{
}


void ToolShortcutManager::reset(bool clear)
{
    cur_key_ = 0;
    cur_tool_ = nullptr;
    cur_adjuster_ = nullptr;
    if (conn_.connected()) {
        conn_.disconnect();
    }
    if (clear) {
        action_map_.clear();
    }
}


void ToolShortcutManager::addShortcut(guint keyval, FoldableToolPanel *tool, Adjuster *adjuster)
{
    action_map_[keyval] = std::make_pair(tool, adjuster);
}


namespace {

bool has_modifiers(GdkEventKey *event)
{
#if defined(__APPLE__)
    return event->state & (GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_MOD2_MASK);
#else
    return event->state & (GDK_CONTROL_MASK|GDK_MOD1_MASK);
#endif
}

enum {
    DIRECTION_ZERO = 0,
    DIRECTION_DOWN = -1,
    DIRECTION_UP = 1,
    DIRECTION_INITIAL = 2
};

} // namespace


bool ToolShortcutManager::keyPressed(GdkEventKey *event)
{
    auto keyval = getKeyval(event);
    if (keyval == GDK_KEY_F1 && !has_modifiers(event)) {
        showHelp();
        return true;
    }

    if (shouldHandleScroll()) {
        switch (keyval) {
        case GDK_KEY_plus:
        case GDK_KEY_equal:
        case GDK_KEY_KP_Add:
            doit(DIRECTION_UP);
            return true;
        case GDK_KEY_asterisk:
        case GDK_KEY_KP_Multiply:
        case GDK_KEY_0:
            doit(DIRECTION_ZERO);
            return true;
        case GDK_KEY_slash:
        case GDK_KEY_KP_Divide:
            doit(DIRECTION_INITIAL);
            return true;
        case GDK_KEY_minus:
        case GDK_KEY_KP_Subtract:
            doit(DIRECTION_DOWN);
            return true;
        default:
            break;
        }
    }
    
    if (has_modifiers(event)) {
        return false;
    }

    if (cur_key_ == keyval) {
        return true;
    }

    auto it = action_map_.find(keyval);
    if (it != action_map_.end()) {
        cur_key_ = keyval;
        cur_tool_ = it->second.first;
        cur_adjuster_ = it->second.second;
        return true;
    } else {
        return keyReleased(event);
    }
}


bool ToolShortcutManager::keyReleased(GdkEventKey *event)
{
    if (shouldHandleScroll()) {
        switch (getKeyval(event)) {
        case GDK_KEY_plus:
        case GDK_KEY_equal:
        case GDK_KEY_KP_Add:
        case GDK_KEY_asterisk:
        case GDK_KEY_KP_Multiply:
        case GDK_KEY_slash:
        case GDK_KEY_KP_Divide:
        case GDK_KEY_minus:
        case GDK_KEY_KP_Subtract:
            return true;
        default:
            break;
        }
    }

    cur_key_ = 0;
    cur_tool_ = nullptr;
    cur_adjuster_ = nullptr;

    return false;
}


bool ToolShortcutManager::scrollPressed(GdkEventScroll *event)
{
    if (shouldHandleScroll()) {
        if (conn_.connected()) {
            conn_.disconnect();
        }

        int dir = event->direction == GDK_SCROLL_DOWN ? DIRECTION_DOWN : DIRECTION_UP;
        doit(dir, options.adjuster_shortcut_scrollwheel_factor);
        return true;
    }
    return false;
}


bool ToolShortcutManager::shouldHandleScroll() const
{
    return (cur_key_ != 0 && cur_tool_ != nullptr && cur_adjuster_ != nullptr);
}


void ToolShortcutManager::doit(int direction, int speed)
{
    cur_tool_->disableListener();
    cur_tool_->setEnabled(true);
    if (direction == DIRECTION_ZERO) {
        cur_adjuster_->resetValue(false);
    } else if (direction == DIRECTION_INITIAL) {
        cur_adjuster_->resetValue(true);
    } else {
        cur_adjuster_->setValue(cur_adjuster_->getValue() + direction * cur_adjuster_->getStepValue() * speed);
    }
    Glib::ustring msg = cur_tool_->getUILabel() + ": " + cur_adjuster_->getLabel() + " = " + cur_adjuster_->getTextValue();
    parent_->showInfo(msg, 0.0);

    FoldableToolPanel *tool = cur_tool_;
    Adjuster *adjuster = cur_adjuster_;        
    const auto doit =
        [tool,adjuster]() -> bool
        {
            tool->enableListener();
            adjuster->forceNotifyListener();
            return false;
        };
    conn_ = Glib::signal_timeout().connect(sigc::slot<bool>(doit), options.adjusterMaxDelay);    
}


void ToolShortcutManager::showHelp()
{
    Glib::ustring msg = M("TOOL_SHORTCUT_HELP_HEADER");
    std::map<Glib::ustring, std::vector<Glib::ustring>> helpmap;
    for (auto &p : action_map_) {
        auto header = p.second.first->getUILabel();
        auto value = Glib::ustring(1, char(p.first)) + ": " + p.second.second->getLabel();
        helpmap[header].push_back(value);
    }

    Glib::ustring pad(10, ' ');
    for (auto &p : helpmap) {
        msg += "\n";
        msg += p.first;
        Glib::ustring headpad(p.first.size()+2, ' ');
        auto &v = p.second;
        std::sort(v.begin(), v.end());
        for (size_t i = 0; i < v.size(); ++i) {
            msg += "\n" + pad + v[i];
        }
    }

    parent_->showInfo(msg, options.error_message_duration);
}
