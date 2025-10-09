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

#include <gtkmm.h>
#include <iomanip>

#include "inspector.h"
#include "guiutils.h"
#include "cursormanager.h"
#include "guiutils.h"
#include "options.h"
#include "multilangmgr.h"
#include "filecatalog.h"
#include "../rtengine/previewimage.h"
#include "../rtengine/imagedata.h"
#include "focusmask.h"
#include "rtwindow.h"

extern Options options;

//-----------------------------------------------------------------------------
// InspectorBuffer
//-----------------------------------------------------------------------------

class InspectorBuffer {
//private:
//    int infoFromImage (const Glib::ustring& fname);

public:
    BackBuffer imgBuffer;
    Glib::ustring imgPath;
    std::array<LUTu, 3> histogram;

    explicit InspectorBuffer(const Glib::ustring &imgagePath, int width=-1, int height=-1);
    //~InspectorBuffer();
};

InspectorBuffer::InspectorBuffer(const Glib::ustring &imagePath, int width, int height)
{
    if (!imagePath.empty() && Glib::file_test(imagePath, Glib::FILE_TEST_EXISTS) && !Glib::file_test(imagePath, Glib::FILE_TEST_IS_DIR)) {
        imgPath = imagePath;

        // generate thumbnail image
        Glib::ustring ext = getExtension (imagePath);

        if (ext == "") {
            imgPath.clear();
            return;
        }

        rtengine::PreviewImage pi(imagePath, ext, width, height, options.thumbnail_inspector_enable_cms, options.thumbnail_inspector_show_histogram);
        Cairo::RefPtr<Cairo::ImageSurface> imageSurface = pi.getImage();
        pi.getHistogram(histogram[0], histogram[1], histogram[2]);

        if (imageSurface) {
            imgBuffer.setSurface(imageSurface);
        } else {
            imgPath.clear();
        }
    }
}


//-----------------------------------------------------------------------------
// InspectorArea
//-----------------------------------------------------------------------------

InspectorArea::InspectorArea():
    currImage(nullptr),
    active(false),
    first_active_(true),
    highlight_(false),
    has_focus_mask_(false),
    info_text_(""),
    hist_bb_(nullptr, false)
{
    Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
    set_name("Inspector");
    add_events(Gdk::BUTTON_PRESS_MASK|Gdk::BUTTON_RELEASE_MASK|Gdk::POINTER_MOTION_MASK);
    signal_button_press_event().connect(sigc::mem_fun(*this, &InspectorArea::onMousePress), false);
    signal_button_release_event().connect(sigc::mem_fun(*this, &InspectorArea::onMouseRelease), false);
    signal_motion_notify_event().connect(sigc::mem_fun(*this, &InspectorArea::onMouseMove), false);
    prev_point_.set(-1, -1);

    hist_bb_.hide();
    hist_bb_.updateOptions(true, true, true, false, false, 1, Options::ScopeType::HISTOGRAM_RAW, false);
}


InspectorArea::~InspectorArea()
{
    deleteBuffers();
}


namespace {

void show_focus_mask(Cairo::RefPtr<Cairo::ImageSurface> surface)
{
    addFocusMask(surface->get_data(), surface->get_data(), surface->get_width(), surface->get_height(), surface->get_stride(), surface->get_stride(), 1, 1);
}

} // namespace


bool InspectorArea::on_draw(const ::Cairo::RefPtr< Cairo::Context> &cr)
{
    Glib::RefPtr<Gdk::Window> win = get_window();

    if (!win) {
        return false;
    }

    if (!active) {
        sig_active_.emit();
        active = true;
    }

    // cleanup the region


    if (currImage && currImage->imgBuffer.surfaceCreated()) {
        // this will eventually create/update the off-screen pixmap

        // compute the displayed area
        rtengine::Coord availableSize;
        rtengine::Coord topLeft;
        rtengine::Coord displayedSize;
        rtengine::Coord dest(0, 0);
        availableSize.x = win->get_width();
        availableSize.y = win->get_height();
        int imW = currImage->imgBuffer.getWidth();
        int imH = currImage->imgBuffer.getHeight();

        if (imW < availableSize.x) {
            // center the image in the available space along X
            topLeft.x = 0;
            displayedSize.x = availableSize.x;
            dest.x = (availableSize.x - imW) / 2;
        } else {
            // partial image display
            // double clamp
            topLeft.x = center.x + availableSize.x / 2;
            topLeft.x = rtengine::min<int>(topLeft.x, imW);
            topLeft.x -= availableSize.x;
            topLeft.x = rtengine::max<int>(topLeft.x, 0);
        }

        if (imH < availableSize.y) {
            // center the image in the available space along Y
            topLeft.y = 0;
            displayedSize.y = availableSize.y;
            dest.y = (availableSize.y - imH) / 2;
        } else {
            // partial image display
            // double clamp
            topLeft.y = center.y + availableSize.y / 2;
            topLeft.y = rtengine::min<int>(topLeft.y, imH);
            topLeft.y -= availableSize.y;
            topLeft.y = rtengine::max<int>(topLeft.y, 0);
        }

        //printf("center: %d, %d   (img: %d, %d)  (availableSize: %d, %d)  (topLeft: %d, %d)\n", center.x, center.y, imW, imH, availableSize.x, availableSize.y, topLeft.x, topLeft.y);

        // define the destination area
        auto dw = rtengine::min<int>(availableSize.x - dest.x, imW);
        auto dh = rtengine::min<int>(availableSize.y - dest.y, imH);
        currImage->imgBuffer.setDrawRectangle(win, dest.x, dest.y, dw, dh, false);
        currImage->imgBuffer.setSrcOffset(topLeft.x, topLeft.y);

        if (!currImage->imgBuffer.surfaceCreated()) {
            return false;
        }

        // Draw!

        Gdk::RGBA c;
        Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
        style->render_background(cr, 0, 0, get_width(), get_height());

        if (has_focus_mask_) {
            int sw = std::min(win->get_width(), imW);
            int sh = std::min(win->get_height(), imH);
            BackBuffer surf(sw, sh);//win->get_width(), win->get_height());
            currImage->imgBuffer.setDestPosition(0, 0);
            currImage->imgBuffer.copySurface(&surf);
            show_focus_mask(surf.getSurface());
            surf.setDestPosition(dest.x, dest.y);
            surf.copySurface(win);
        } else {
            currImage->imgBuffer.copySurface(win);
        }

        // draw the frame
        c = highlight_ ? style->get_color(Gtk::STATE_FLAG_SELECTED) : style->get_background_color(Gtk::STATE_FLAG_NORMAL);
        cr->set_source_rgb (c.get_red(), c.get_green(), c.get_blue());
        cr->set_line_width(3);
        cr->rectangle(1.5, 1.5, availableSize.x - 2.5, availableSize.y - 2.5);
        cr->stroke();

        if (options.thumbnail_inspector_show_info && info_text_ != "") {
            info_bb_.copySurface(cr);
        }

        if (options.thumbnail_inspector_show_histogram) {
            auto s = RTScalable::getScale();
            double border = 4 * s;
            Gdk::Rectangle rect(border + 8 * s, availableSize.y - hist_bb_.getHeight() - 8 * s - border, hist_bb_.getWidth(), hist_bb_.getHeight());

            //cr->set_operator(Cairo::OPERATOR_OVER);
            cr->set_source_rgba(0., 0., 0., 0.75);
            cr->rectangle(rect.get_x() - border, rect.get_y() - border, rect.get_width() + border*2, rect.get_height() + border*2);
            cr->fill();
            
            hist_bb_.copySurface(cr, &rect);
        }
    } else {
        Gdk::RGBA c;
        Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
        style->render_background(cr, 0, 0, get_width(), get_height());

        // draw the frame
        c = highlight_ ? style->get_color(Gtk::STATE_FLAG_SELECTED) : style->get_background_color(Gtk::STATE_FLAG_NORMAL);
        cr->set_source_rgb (c.get_red(), c.get_green(), c.get_blue());
        cr->set_line_width(3);
        cr->rectangle(1.5, 1.5, win->get_width() - 2.5, win->get_height() - 2.5);
        cr->stroke();
    }

    if (first_active_) {
        first_active_ = false;
        sig_ready_.emit();
    }

    return true;
}


void InspectorArea::mouseMove(rtengine::Coord2D pos, int transform)
{
    if (!active) {
        return;
    }

    if (currImage) {
        center.set(int(rtengine::LIM01(pos.x)*double(currImage->imgBuffer.getWidth())), int(rtengine::LIM01(pos.y)*double(currImage->imgBuffer.getHeight())));
    } else {
        center.set(0, 0);
    }

    queue_draw();
}


void InspectorArea::switchImage(const Glib::ustring &fullPath, bool recenter, rtengine::Coord2D newcenter)
{
    if (!active) {
        return;
    }

    if (delayconn.connected()) {
        delayconn.disconnect();
    }

    next_image_path = fullPath;
    if (!options.inspectorDelay) {
        doSwitchImage(recenter, newcenter);
    } else {
        delayconn = Glib::signal_timeout().connect(sigc::bind(sigc::mem_fun(*this, &InspectorArea::doSwitchImage), recenter, newcenter), options.inspectorDelay);
    }
}


bool InspectorArea::doSwitchImage(bool recenter, rtengine::Coord2D newcenter)
{
    Glib::ustring fullPath = next_image_path;
    
    // we first check the size of the list, it may have been changed in Preference
    if (images.size() > size_t(options.maxInspectorBuffers)) {
        // deleting the last entries
        for (size_t i = images.size() - 1; i > size_t(options.maxInspectorBuffers - 1); --i) {
            delete images.at(i);
            images.at(i) = nullptr;
        }

        // resizing down
        images.resize(options.maxInspectorBuffers);
    }

    if (fullPath.empty()) {
        currImage = nullptr;
    } else {
        bool found = false;

        for (size_t i = 0; i < images.size(); ++i) {
            if (images.at(i) != nullptr && images.at(i)->imgPath == fullPath) {
                currImage = images.at(i);

                // rolling the list 1 step to the beginning
                for (size_t j = i; j < images.size() - 1; ++j) {
                    images.at(j) = images.at(j + 1);
                }

                images.at(images.size() - 1) = currImage; // move the last used image to the tail
                found = true;
                break;
            }
        }

        if (!found) {
            if (images.size() == size_t(options.maxInspectorBuffers)) {
                // The list is full, delete the first entry
                delete images.at(0);
                images.erase(images.begin());
            }

            Glib::RefPtr<Gdk::Window> win = get_window();
            int width = -1, height = -1;
            if (win && options.thumbnail_inspector_zoom_fit) {
                width = win->get_width();
                height = win->get_height();
            }
            
            // Loading a new image
            InspectorBuffer *iBuffer = new InspectorBuffer(fullPath, width, height);

            // and add it to the tail
            if (!iBuffer->imgPath.empty()) {
                images.push_back(iBuffer);
                currImage = images.at(images.size() - 1);
            } else {
                delete iBuffer;
                currImage = nullptr;
            }
        }
    }

    if (currImage && recenter) {
        if (newcenter.x >= 0 && newcenter.y >= 0) {
            center.set(rtengine::LIM01(newcenter.x) * currImage->imgBuffer.getWidth(), rtengine::LIM01(newcenter.y) * currImage->imgBuffer.getHeight());
        } else {
            center.set(currImage->imgBuffer.getWidth()/2, currImage->imgBuffer.getHeight()/2);
        }
    }

    if (currImage && options.thumbnail_inspector_show_histogram) {
        updateHistogram();
    }

    queue_draw();

    return true;
}

void InspectorArea::deleteBuffers ()
{
    for (size_t i = 0; i < images.size(); ++i) {
        if (images.at(i) != nullptr) {
            delete images.at(i);
            images.at(i) = nullptr;
        }
    }

    images.resize(0);
    currImage = nullptr;
}

void InspectorArea::flushBuffers ()
{
    if (!active) {
        return;
    }

    deleteBuffers();
}

void InspectorArea::setActive(bool state)
{
    if (!state) {
        flushBuffers();
    }

    active = state;
    if (!active) {
        first_active_ = true;
    }
}


Gtk::SizeRequestMode InspectorArea::get_request_mode_vfunc () const
{
    return Gtk::SIZE_REQUEST_CONSTANT_SIZE;
}

void InspectorArea::get_preferred_height_vfunc (int &minimum_height, int &natural_height) const
{
    minimum_height= 50 * RTScalable::getScale();
    natural_height = 300 * RTScalable::getScale();
}

void InspectorArea::get_preferred_width_vfunc (int &minimum_width, int &natural_width) const
{
    minimum_width = 50 * RTScalable::getScale();
    natural_width = 200 * RTScalable::getScale();
}

void InspectorArea::get_preferred_height_for_width_vfunc (int width, int &minimum_height, int &natural_height) const
{
    get_preferred_height_vfunc(minimum_height, natural_height);
}

void InspectorArea::get_preferred_width_for_height_vfunc (int height, int &minimum_width, int &natural_width) const
{
    get_preferred_width_vfunc (minimum_width, natural_width);
}


void InspectorArea::setInfoText(const Glib::ustring &text)
{
    info_text_ = text;

    Glib::RefPtr<Pango::Context> context = get_pango_context();
    Pango::FontDescription fontd(get_style_context()->get_font());

    // update font
    fontd.set_weight(Pango::WEIGHT_BOLD);
    fontd.set_size(options.fontSize * Pango::SCALE);
    context->set_font_description(fontd);

    // create text layout
    Glib::RefPtr<Pango::Layout> ilayout = create_pango_layout("");
    ilayout->set_markup(text);

    // get size of the text block
    int iw, ih;
    ilayout->get_pixel_size(iw, ih);

    // create BackBuffer
    int scale = RTScalable::getDeviceScale();
    info_bb_.setDrawRectangle(Cairo::FORMAT_ARGB32, 0, 0, (iw + 16) * scale, (ih + 16) * scale, true);
    info_bb_.setDestPosition(8, 8);
    RTScalable::setDeviceScale(info_bb_.getSurface(), scale);

    Cairo::RefPtr<Cairo::Context> cr = info_bb_.getContext();

    // cleaning the back buffer (make it full transparent)
    cr->set_source_rgba(0., 0., 0., 0.);
    cr->set_operator(Cairo::OPERATOR_CLEAR);
    cr->paint();
    cr->set_operator(Cairo::OPERATOR_OVER);

    // paint transparent black background
    cr->set_source_rgba(0., 0., 0., 0.5);
    cr->paint ();

    // paint text
    cr->set_source_rgb(1.0, 1.0, 1.0);
    cr->move_to(8, 8);
    ilayout->add_to_cairo_context(cr);
    cr->fill();
}


void InspectorArea::infoEnabled(bool yes)
{
    if (options.thumbnail_inspector_show_info != yes) {
        options.thumbnail_inspector_show_info = yes;
        queue_draw();
    }
}


void InspectorArea::setFocusMask(bool yes)
{
    if (has_focus_mask_ != yes) {
        has_focus_mask_ = yes;
        queue_draw();
    }
}


void InspectorArea::updateHistogram()
{
    Glib::RefPtr<Gdk::Window> win = get_window();
    if (!win || !currImage) {
        return;
    }
    
    LUTu dummy_lut(1);
    array2D<int> dummy_arr;
    hist_bb_.update(dummy_lut, dummy_lut, dummy_lut,
                    dummy_lut, dummy_lut,
                    currImage->histogram[0],
                    currImage->histogram[1],
                    currImage->histogram[2],
                    1,
                    dummy_arr, dummy_arr,
                    1,
                    dummy_arr, dummy_arr, dummy_arr, dummy_arr);

    int hist_w = RTScalable::getScale() * 300;
    int hist_h = RTScalable::getScale() * 200;
    
    hist_bb_.updateBackBuffer(hist_w, hist_h);
}


bool InspectorArea::onMouseMove(GdkEventMotion *evt)
{
    if (active && currImage && prev_point_.x >= 0) {
        double w = currImage->imgBuffer.getWidth();
        double h = currImage->imgBuffer.getHeight();
        if (w > 0 && h > 0) {
            constexpr double gain = 4.0;
            double dx = center.x - (evt->x - prev_point_.x) * gain;
            double dy = center.y - (evt->y - prev_point_.y) * gain;
            sig_moved_.emit(rtengine::Coord2D(dx/w, dy/h));
        }
        prev_point_.set(evt->x, evt->y);
    }
    return false;
}

bool InspectorArea::onMousePress(GdkEventButton *evt)
{
    if (active && evt->button == 1) {
        prev_point_.set(evt->x, evt->y);
        CursorManager::setWidgetCursor(get_window(), CSHandClosed);
        if (currImage) {
            double w = currImage->imgBuffer.getWidth();
            double h = currImage->imgBuffer.getHeight();
            auto win = get_window();
            if (w > 0 && h > 0) {
                int ww = win->get_width();
                int hh = win->get_height();
                int ox = w/2 - ww/2;
                int oy = h/2 - hh/2;
                double x = (evt->x + ox) / w;
                double y = (evt->y + oy) / h;
                sig_pressed_.emit(rtengine::Coord2D(x, y));
            }
        }
    } else {
        prev_point_.set(-1, -1);
        CursorManager::setWidgetCursor(get_window(), CSArrow);
    }
    return false;
}

bool InspectorArea::onMouseRelease(GdkEventButton *evt)
{
    prev_point_.set(-1, -1);
    CursorManager::setWidgetCursor(get_window(), CSArrow);
    sig_released_.emit();
    return false;
}


//-----------------------------------------------------------------------------
// Inspector
//-----------------------------------------------------------------------------

Inspector::Inspector(FileCatalog *filecatalog):
    filecatalog_(filecatalog),
    focusmask_on_("focusscreen-on.svg"),
    focusmask_off_("focusscreen-off.svg")
{
    ibox_.pack_start(ins_[0], Gtk::PACK_EXPAND_WIDGET, 3);
    ibox_.pack_start(ins_[1], Gtk::PACK_EXPAND_WIDGET, 3);
    pack_start(ibox_);
    pack_start(*get_toolbar(), Gtk::PACK_SHRINK, 2);
    removeIfThere(&ibox_, &ins_[1]);
    show_all_children();

    signal_key_press_event().connect(sigc::mem_fun(*this, &Inspector::keyPressed));

    active_ = 0;
    num_active_ = 1;
    temp_zoom_11_ = false;
    for (size_t i = 0; i < 2; ++i) {
        ins_[i].set_can_focus(true);
        ins_[i].add_events(Gdk::BUTTON_PRESS_MASK);
        ins_[i].signal_button_press_event().connect_notify(sigc::bind(sigc::mem_fun(*this, &Inspector::onGrabFocus), i));
//        ins_[i].signal_size_allocate().connect(sigc::mem_fun(*this, &Inspector::onInspectorResized));
        ins_[i].signal_active().connect(sigc::bind(sigc::mem_fun(*this, &Inspector::setActive), true));
        ins_[i].signal_moved().connect(sigc::mem_fun(*this, &Inspector::on_moved));
        ins_[i].signal_pressed().connect(sigc::mem_fun(*this, &Inspector::on_pressed));
        ins_[i].signal_released().connect(sigc::mem_fun(*this, &Inspector::on_released));
    }
    signal_size_allocate().connect(sigc::mem_fun(*this, &Inspector::onInspectorResized));    
}


void Inspector::mouseMove(rtengine::Coord2D pos, int transform)
{
    for (size_t i = 0; i < num_active_; ++i) {
        ins_[i].mouseMove(pos, transform);
    }
}


void Inspector::on_moved(rtengine::Coord2D pos)
{
    mouseMove(pos, 0);
}


void Inspector::on_pressed(rtengine::Coord2D pos)
{
    if (options.thumbnail_inspector_zoom_fit) {
        temp_zoom_11_ = true;
        ConnectionBlocker block1(zoom11conn_);
        zoom11_->set_active(true);
        do_toggle_zoom(zoom11_, pos);
    }
}


void Inspector::on_released()
{
    if (temp_zoom_11_) {
        temp_zoom_11_ = false;
        ConnectionBlocker block1(zoomfitconn_);
        zoomfit_->set_active(true);
        do_toggle_zoom(zoomfit_);
    }
}


void Inspector::flushBuffers()
{
    for (size_t i = 0; i < 2; ++i) {
        ins_[i].flushBuffers();
    }
}
    

void Inspector::setActive(bool state)
{
    if (!state) {
        toolbar_->hide();
    } else {
        toolbar_->show();
    }
    for (size_t i = 0; i < num_active_; ++i) {
        ins_[i].setActive(state);
    }
}


bool Inspector::isActive() const
{
    return ins_[0].isActive();
}


sigc::signal<void> Inspector::signal_ready()
{
    return ins_[active_].signal_ready();
}


bool Inspector::keyPressed(GdkEventKey *evt)
{
    if (filecatalog_) {
        return filecatalog_->handleShortcutKey(evt);
    }
    return false;    
}


void Inspector::switchImage(const Glib::ustring &fullPath)
{
    cur_image_[active_] = fullPath;
    if (info_->get_active()) {
        ins_[active_].setInfoText(get_info_text(active_));
    }
    ins_[active_].switchImage(fullPath);
    auto &root = getToplevelWindow(this);
    if (RTWindow *w = dynamic_cast<RTWindow *>(&root)) {
        w->set_title_decorated(fullPath);
    }
}


Gtk::HBox *Inspector::get_toolbar()
{
    Gtk::HBox *tb = Gtk::manage(new Gtk::HBox());
    toolbar_ = tb;
    tb->pack_start(*Gtk::manage(new Gtk::Label("")), Gtk::PACK_EXPAND_WIDGET, 2);

    const auto add_tool =
        [&](const char *icon, const char *tip=nullptr) -> Gtk::ToggleButton *
        {
            Gtk::ToggleButton *ret = Gtk::manage(new Gtk::ToggleButton());
            ret->set_image(*Gtk::manage(new RTImage(icon)));
            ret->set_relief(Gtk::RELIEF_NONE);
            if (tip) {
                ret->set_tooltip_markup(M(tip));
            }
            tb->pack_start(*ret, Gtk::PACK_SHRINK, 2);
            return ret;
        };

    split_ = add_tool("beforeafter.svg", "INSPECTOR_SPLIT");
    tb->pack_start(*Gtk::manage(new Gtk::VSeparator()), Gtk::PACK_SHRINK, 4);
       
    info_ = add_tool("info.svg", "INSPECTOR_INFO");
    histogram_ = add_tool("histogram.svg", "INSPECTOR_HISTOGRAM");
    focusmask_ = add_tool("focusscreen-off.svg", "INSPECTOR_FOCUS_MASK");
    tb->pack_start(*Gtk::manage(new Gtk::VSeparator()), Gtk::PACK_SHRINK, 4);

    jpg_ = add_tool("wb-camera.svg", "INSPECTOR_PREVIEW");
    rawlinear_ = add_tool("raw-linear-curve.svg", "INSPECTOR_RAW_LINEAR");
    rawfilm_ = add_tool("raw-film-curve.svg", "INSPECTOR_RAW_FILM");
    rawshadow_ = add_tool("raw-shadow-curve.svg", "INSPECTOR_RAW_SHADOW");
    rawclip_ = add_tool("raw-clip-curve.svg", "INSPECTOR_RAW_CLIP");

    tb->pack_start(*Gtk::manage(new Gtk::VSeparator()), Gtk::PACK_SHRINK, 4);

    zoomfit_ = add_tool("magnifier-fit.svg", "INSPECTOR_ZOOM_FIT");
    zoom11_ = add_tool("magnifier-1to1.svg", "INSPECTOR_ZOOM_11");

    tb->pack_start(*Gtk::manage(new Gtk::VSeparator()), Gtk::PACK_SHRINK, 4);

    cms_ = add_tool("gamut-softproof.svg", "INSPECTOR_ENABLE_CMS");

    //------------------------------------------------------------------------

    split_->signal_toggled().connect(sigc::mem_fun(*this, &Inspector::split_toggled));
    
    info_->set_active(options.thumbnail_inspector_show_info);
    info_->signal_toggled().connect(sigc::mem_fun(*this, &Inspector::info_toggled));

    histogram_->set_active(options.thumbnail_inspector_show_histogram);
    histogram_->signal_toggled().connect(sigc::mem_fun(*this, &Inspector::histogram_toggled));

    focusmask_->signal_toggled().connect(sigc::mem_fun(*this, &Inspector::focus_mask_toggled));
    
    bool use_jpg = options.rtSettings.thumbnail_inspector_mode == rtengine::Settings::ThumbnailInspectorMode::JPEG;
    jpg_->set_active(use_jpg);
    rawlinear_->set_active(!use_jpg && options.rtSettings.thumbnail_inspector_raw_curve == rtengine::Settings::ThumbnailInspectorRawCurve::LINEAR);
    rawfilm_->set_active(!use_jpg && options.rtSettings.thumbnail_inspector_raw_curve == rtengine::Settings::ThumbnailInspectorRawCurve::FILM);
    rawshadow_->set_active(!use_jpg && options.rtSettings.thumbnail_inspector_raw_curve == rtengine::Settings::ThumbnailInspectorRawCurve::SHADOW_BOOST);
    rawclip_->set_active(!use_jpg && options.rtSettings.thumbnail_inspector_raw_curve == rtengine::Settings::ThumbnailInspectorRawCurve::RAW_CLIPPING);

    zoomfit_->set_active(options.thumbnail_inspector_zoom_fit);
    zoom11_->set_active(!options.thumbnail_inspector_zoom_fit);

    cms_->set_active(options.thumbnail_inspector_enable_cms);

    jpgconn_ = jpg_->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &Inspector::mode_toggled), jpg_));
    rawlinearconn_ = rawlinear_->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &Inspector::mode_toggled), rawlinear_));
    rawfilmconn_ = rawfilm_->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &Inspector::mode_toggled), rawfilm_));
    rawshadowconn_ = rawshadow_->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &Inspector::mode_toggled), rawshadow_));
    rawclipconn_ = rawclip_->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &Inspector::mode_toggled), rawclip_));

    zoomfitconn_ = zoomfit_->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &Inspector::zoom_toggled), zoomfit_));
    zoom11conn_ = zoom11_->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &Inspector::zoom_toggled), zoom11_));

    cms_->signal_toggled().connect(sigc::mem_fun(*this, &Inspector::cms_toggled));

    return tb;
}


void Inspector::info_toggled()
{
    if (!info_->get_active()) {
        for (size_t i = 0; i < num_active_; ++i) {
            ins_[i].infoEnabled(false);
        }
    } else {
        for (size_t i = 0; i < num_active_; ++i) {
            ins_[i].setInfoText(get_info_text(i));
            ins_[i].infoEnabled(true);
        }
    }
}


Glib::ustring Inspector::get_info_text(size_t i)
{    
    rtengine::FramesData meta(cur_image_[i]);

    Glib::ustring infoString;
    Glib::ustring expcomp;

    if (meta.hasExif()) {
        infoString = Glib::ustring::compose ("%1 + %2\n<span size=\"small\">f/</span><span size=\"large\">%3</span>  <span size=\"large\">%4</span><span size=\"small\">s</span>  <span size=\"small\">%5</span><span size=\"large\">%6</span>  <span size=\"large\">%7</span><span size=\"small\">mm</span>",
                                              Glib::ustring(meta.getMake() + " " + meta.getModel()),
                                              Glib::ustring(meta.getLens()),
                                              Glib::ustring(meta.apertureToString (meta.getFNumber())),
                                              Glib::ustring(meta.shutterToString (meta.getShutterSpeed())),
                                              M("QINFO_ISO"), meta.getISOSpeed(),
                                              Glib::ustring::format(std::setw (3), std::fixed, std::setprecision (2), meta.getFocalLen()));

        expcomp = Glib::ustring(meta.expcompToString(meta.getExpComp(), true));

        if (!expcomp.empty ()) {
            infoString = Glib::ustring::compose ("%1  <span size=\"large\">%2</span><span size=\"small\">EV</span>",
                                                  infoString,
                                                  expcomp);
        }

        infoString = Glib::ustring::compose("%1\n<span size=\"small\">%2</span><span>%3</span>",
                                              infoString,
                                              escapeHtmlChars(Glib::path_get_dirname(cur_image_[i])) + G_DIR_SEPARATOR_S,
                                              escapeHtmlChars(Glib::path_get_basename(cur_image_[i])));

        int ww = -1, hh = -1;
        meta.getDimensions(ww, hh);
        if (ww > 0 && hh > 0) {
            //megapixels
            infoString = Glib::ustring::compose ("%1\n<span size=\"small\">%2 MP (%3x%4)</span>",
                                                 infoString,
                                                 Glib::ustring::format (std::setw (4), std::fixed, std::setprecision (1), (float)ww * hh / 1000000),
                                                 ww, hh);
        }
    } else {
        infoString = M("QINFO_NOEXIF");
    }
    return infoString;
}


void Inspector::mode_toggled(Gtk::ToggleButton *b)
{
    ConnectionBlocker blockj(jpgconn_);
    ConnectionBlocker blockl(rawlinearconn_);
    ConnectionBlocker blockf(rawfilmconn_);
    ConnectionBlocker blocks(rawshadowconn_);
    ConnectionBlocker blockc(rawclipconn_);

    if (!b->get_active()) {
        b->set_active(true);
    } else {
        jpg_->set_active(false);
        rawlinear_->set_active(false);
        rawfilm_->set_active(false);
        rawshadow_->set_active(false);
        rawclip_->set_active(false);
        b->set_active(true);

        if (jpg_->get_active()) {
            options.rtSettings.thumbnail_inspector_mode = rtengine::Settings::ThumbnailInspectorMode::JPEG;
        } else if (rawlinear_->get_active()) {
            options.rtSettings.thumbnail_inspector_mode = rtengine::Settings::ThumbnailInspectorMode::RAW;
            options.rtSettings.thumbnail_inspector_raw_curve = rtengine::Settings::ThumbnailInspectorRawCurve::LINEAR;
        } else if (rawfilm_->get_active()) {
            options.rtSettings.thumbnail_inspector_mode = rtengine::Settings::ThumbnailInspectorMode::RAW;
            options.rtSettings.thumbnail_inspector_raw_curve = rtengine::Settings::ThumbnailInspectorRawCurve::FILM;
        } else if (rawshadow_->get_active()) {
            options.rtSettings.thumbnail_inspector_mode = rtengine::Settings::ThumbnailInspectorMode::RAW;
            options.rtSettings.thumbnail_inspector_raw_curve = rtengine::Settings::ThumbnailInspectorRawCurve::SHADOW_BOOST;
        } else if (rawclip_->get_active()) {
            options.rtSettings.thumbnail_inspector_mode = rtengine::Settings::ThumbnailInspectorMode::RAW;
            options.rtSettings.thumbnail_inspector_raw_curve = rtengine::Settings::ThumbnailInspectorRawCurve::RAW_CLIPPING;
        }

        for (size_t i = 0; i < num_active_; ++i) {
            ins_[i].flushBuffers();
            ins_[i].switchImage(cur_image_[i]);
        }
    }
}


void Inspector::zoom_toggled(Gtk::ToggleButton *b)
{
    do_toggle_zoom(b);
}


void Inspector::do_toggle_zoom(Gtk::ToggleButton *b, rtengine::Coord2D pos)
{
    ConnectionBlocker blockf(zoomfitconn_);
    ConnectionBlocker block1(zoom11conn_);

    if (!b->get_active()) {
        b->set_active(true);
    } else {
        zoomfit_->set_active(false);
        zoom11_->set_active(false);
        b->set_active(true);

        options.thumbnail_inspector_zoom_fit = zoomfit_->get_active();

        for (size_t i = 0; i < num_active_; ++i) {
            ins_[i].flushBuffers();
            ins_[i].switchImage(cur_image_[i], true, pos);
        }
    }
}


void Inspector::cms_toggled()
{
    options.thumbnail_inspector_enable_cms = cms_->get_active();
    for (size_t i = 0; i < num_active_; ++i) {
        ins_[i].flushBuffers();
        ins_[i].switchImage(cur_image_[i]);
    }
}


void Inspector::toggleShowInfo()
{
    info_->set_active(!info_->get_active());
}


void Inspector::toggleUseCms()
{
    cms_->set_active(!cms_->get_active());
}


void Inspector::toggleShowHistogram()
{
    histogram_->set_active(!histogram_->get_active());
}

enum class DisplayMode {
    JPG,
    RAW_LINEAR,
    RAW_FILM_CURVE,
    RAW_SHADOW_BOOST,
    RAW_CLIP_WARNING
};

void Inspector::setDisplayMode(DisplayMode m)
{
    switch (m) {
    case DisplayMode::JPG:
        jpg_->set_active(true);
        break;
    case DisplayMode::RAW_LINEAR:
        rawlinear_->set_active(true);
        break;
    case DisplayMode::RAW_FILM_CURVE:
        rawfilm_->set_active(true);
        break;
    case DisplayMode::RAW_SHADOW_BOOST:
        rawshadow_->set_active(true);
        break;
    case DisplayMode::RAW_CLIP_WARNING:
        rawclip_->set_active(true);
        break;
    }
}


void Inspector::setZoomFit(bool yes)
{
    if (yes) {
        zoomfit_->set_active(true);
    } else {
        zoom11_->set_active(true);
    }
}


// void Inspector::setFocusMask(bool yes)
// {
//     focusmask_->set_active(yes);
// }


void Inspector::onGrabFocus(GdkEventButton *evt, size_t i)
{
    if (evt->button == 1) {
        ins_[active_].setHighlight(false);
        ins_[i].setHighlight(true);
        active_ = i;
        queue_draw();
    }
}


void Inspector::split_toggled()
{
    if (split_->get_active()) {
        active_ = 1;
        ibox_.pack_start(ins_[1], Gtk::PACK_EXPAND_WIDGET, 3);
        ins_[1].show();
        ins_[1].setActive(false);
        num_active_ = 2;
        ins_[0].setHighlight(false);
        ins_[1].setHighlight(true);
    } else {
        active_ = 0;
        ins_[1].setActive(false);
        removeIfThere(&ibox_, &ins_[1]);
        num_active_ = 1;
        ins_[0].setHighlight(false);
        ins_[1].setHighlight(false);
    }
    queue_draw();
}


void Inspector::histogram_toggled()
{
    options.thumbnail_inspector_show_histogram = histogram_->get_active();
    for (size_t i = 0; i < num_active_; ++i) {
        ins_[i].flushBuffers();
        ins_[i].switchImage(cur_image_[i]);
    }
}


void Inspector::focus_mask_toggled()
{
    if (focusmask_->get_active()) {
        focusmask_->set_image(focusmask_on_);
    } else {
        focusmask_->set_image(focusmask_off_);
    }
    for (size_t i = 0; i < num_active_; ++i) {
        ins_[i].setFocusMask(focusmask_->get_active());
    }
}


bool Inspector::handleShortcutKey(GdkEventKey *event)
{
    bool ctrl  = event->state & GDK_CONTROL_MASK;
    bool shift = event->state & GDK_SHIFT_MASK;
    bool alt   = event->state & GDK_MOD1_MASK;
#ifdef __WIN32__
    bool altgr = event->state & GDK_MOD2_MASK;
#else
    bool altgr = false;
#endif

    if (!ctrl && !shift && !alt && !altgr) {
        switch (getKeyval(event)) {
        case GDK_KEY_h:
            toggleShowHistogram();
            return true;
        case GDK_KEY_c:
            toggleUseCms();
            return true;
        case GDK_KEY_z:
            setZoomFit(false);
            return true;
        case GDK_KEY_x:
            setZoomFit(true);
            return true;
        case GDK_KEY_j:
            setDisplayMode(Inspector::DisplayMode::JPG);
            return true;
        case GDK_KEY_r:
            setDisplayMode(Inspector::DisplayMode::RAW_LINEAR);
            return true;
        case GDK_KEY_f:
            setDisplayMode(Inspector::DisplayMode::RAW_FILM_CURVE);
            return true;
        case GDK_KEY_s:
            setDisplayMode(Inspector::DisplayMode::RAW_SHADOW_BOOST);
            return true;
        case GDK_KEY_w:
            setDisplayMode(Inspector::DisplayMode::RAW_CLIP_WARNING);
            return true;
        case GDK_KEY_y:
            split_->set_active(!split_->get_active());
            return true;
        case GDK_KEY_Tab:
            if (split_->get_active()) {
                ins_[active_].setHighlight(false);
                active_ = 1 - active_;
                ins_[active_].setHighlight(true);
                queue_draw();
                return true;
            }
            break;
        }
    }
    if (!ctrl && shift && !alt && !altgr) {
        switch (getKeyval(event)) {
        case GDK_KEY_F:
            focusmask_->set_active(!focusmask_->get_active());
            return true;
        case GDK_KEY_I:
            toggleShowInfo(); 
            return true;
        }
    }

    return false;
}


void Inspector::onInspectorResized(Gtk::Allocation &a)
{
    if (zoomfit_->get_active()) {
        if (delayconn_.connected()) {
            delayconn_.disconnect();
        }

        const auto doit =
            [this]() -> bool
            {
                for (size_t i = 0; i < num_active_; ++i) {
                    ins_[i].flushBuffers();
                    ins_[i].switchImage(cur_image_[i]);
                }
                return false;
            };
        
        delayconn_ = Glib::signal_timeout().connect(sigc::slot<bool>(doit), options.adjusterMaxDelay);
    }
}
