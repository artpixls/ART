/* -*- C++ -*-
 *  
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
#ifndef _FILEBROWSERENTRY_
#define _FILEBROWSERENTRY_

#include <atomic>

#include <gtkmm.h>

#include "../rtengine/rtengine.h"

#include "crophandler.h"
#include "editenums.h"
#include "filethumbnailbuttonset.h"
#include "imageareatoollistener.h"
#include "thumbbrowserentrybase.h"
#include "thumbimageupdater.h"
#include "thumbnail.h"
#include "thumbnaillistener.h"


class FileBrowserEntry;
struct FileBrowserEntryIdleHelper {
    FileBrowserEntry* fbentry;
    bool destroyed;
    std::atomic<int> pending;
};

class FileThumbnailButtonSet;
class FileBrowserEntry : public ThumbBrowserEntryBase,
    public ThumbnailListener,
    public ThumbImageUpdateListener
{

    double scale;
    bool wasInside;
    int press_x, press_y, action_x, action_y;
    double rot_deg;
    int coarse_rotate;
    rtengine::procparams::CropParams cropParams;
    CropGUIListener* cropgl;
    FileBrowserEntryIdleHelper* feih;

    ImgEditState state;
    float crop_custom_ratio;

    IdleRegister idle_register;

    // bool onArea (CursorArea a, int x, int y);
    // void updateCursor (int x, int y);
    void drawStraightenGuide (Cairo::RefPtr<Cairo::Context> c);
    void customBackBufferUpdate (Cairo::RefPtr<Cairo::Context> c) override;

    enum class RefreshStatus { READY, PENDING, QUICK, FULL };
    RefreshStatus refresh_status_;
    bool refresh_disabled_;
    void update_refresh_status();
    
public:

    static Glib::RefPtr<Gdk::Pixbuf> editedIcon;
    static Glib::RefPtr<Gdk::Pixbuf> recentlySavedIcon;
    static Glib::RefPtr<Gdk::Pixbuf> enqueuedIcon;
    static Glib::RefPtr<Gdk::Pixbuf> hdr;
    static Glib::RefPtr<Gdk::Pixbuf> ps;

    FileBrowserEntry (Thumbnail* thm, const Glib::ustring& fname);
    ~FileBrowserEntry () override;
    static void init ();
    void draw (Cairo::RefPtr<Cairo::Context> cc) override;

    FileThumbnailButtonSet* getThumbButtonSet ();

    void refreshThumbnailImage () override;
    void refreshQuickThumbnailImage () override;
    void calcThumbnailSize () override;

    std::vector<Glib::RefPtr<Gdk::Pixbuf>> getIconsOnImageArea () override;
    std::vector<Glib::RefPtr<Gdk::Pixbuf>> getSpecificityIconsOnImageArea () override;
    void getIconSize (int& w, int& h) const override;

    // thumbnaillistener interface
    void procParamsChanged (Thumbnail* thm, int whoChangedIt) override;
    // thumbimageupdatelistener interface
    void updateImage(rtengine::IImage8* img, double scale, const rtengine::procparams::CropParams& cropParams) override;
    void _updateImage(rtengine::IImage8* img, double scale, const rtengine::procparams::CropParams& cropParams); // inside gtk thread

    bool    motionNotify  (int x, int y) override;
    bool    pressNotify   (int button, int type, int bstate, int x, int y) override;
    bool    releaseNotify (int button, int type, int bstate, int x, int y) override;

    void enableThumbRefresh();
};

#endif
