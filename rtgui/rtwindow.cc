/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>, Oliver Duis <www.oliverduis.de>
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
#include "rtwindow.h"
#include "version.h"
#include "options.h"
#include "preferences.h"
#include "cursormanager.h"
#include "rtimage.h"
#include "whitebalance.h"
#include "threadutils.h"
#include "editwindow.h"
#include "gdkcolormgmt.h"
#include "../rtengine/profilestore.h"

float fontScale = 1.f;
Glib::RefPtr<Gtk::CssProvider> cssForced;
// Glib::RefPtr<Gtk::CssProvider> cssRT;

extern unsigned char initialGdkScale;

//-----------------------------------------------------------------------------
// MessageWindow
//-----------------------------------------------------------------------------

MessageWindow::MessageWindow():
    main_overlay_(nullptr),
    msg_revealer_(nullptr),
    info_label_(nullptr),
    info_msg_num_(0)
{
}


MessageWindow::~MessageWindow()
{
}


void MessageWindow::init(Gtk::Widget *main_widget)
{
    main_overlay_ = Gtk::manage(new Gtk::Overlay());
    add(*main_overlay_);
    main_overlay_->add(*main_widget);
    show_all();
    
    msg_revealer_ = Gtk::manage(new Gtk::Revealer());
    {
        info_label_ = Gtk::manage(new Gtk::Label());
        Gtk::HBox *box = Gtk::manage(new Gtk::HBox());
        info_box_ = box;
        box->set_spacing(4);

        info_label_->set_can_focus(false);
        info_box_->set_can_focus(false);

        box->get_style_context()->add_class("app-notification");
        RTImage *icon = Gtk::manage(new RTImage("warning.svg"));
        info_image_ = icon;
        setExpandAlignProperties(icon, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_START);
        icon->set_can_focus(false);
        
        box->pack_start(*icon, false, false, 20);
        box->pack_start(*info_label_, false, true, 10);
        msg_revealer_->add(*box);
        msg_revealer_->set_transition_type(Gtk::REVEALER_TRANSITION_TYPE_CROSSFADE);
        msg_revealer_->property_halign() = Gtk::ALIGN_CENTER;
        msg_revealer_->property_valign() = Gtk::ALIGN_END;
        msg_revealer_->property_margin_bottom() = 70;
    }
    main_overlay_->add_overlay(*msg_revealer_);
    msg_revealer_->show_all();
    msg_revealer_->set_reveal_child(false);
}


bool MessageWindow::hide_info_msg()
{
    msg_revealer_->set_reveal_child(false);
    const auto hidebox =
        [this]() -> bool
        {
            info_box_->hide();
            return false;
        };
    Glib::signal_timeout().connect(sigc::slot<bool>(hidebox), msg_revealer_->get_transition_duration());
    info_msg_.clear();
    unique_info_msg_.clear();
    info_msg_num_ = 0;
    return false;
}


void MessageWindow::showInfo(const Glib::ustring &msg, double duration)
{
    show_info_msg(msg, false, duration, 0);
}


void MessageWindow::showError(const Glib::ustring& descr)
{
    show_info_msg(descr, true, 0, 120);
}


void MessageWindow::show_info_msg(const Glib::ustring &descr, bool is_error, double duration, size_t padding)
{
    GThreadLock lock;
    
    const auto wrap =
        [padding](Glib::ustring s) -> Glib::ustring
        {
            Glib::ustring ret;
            const size_t pad = padding;
            while (s.size() > pad) {
                if (!ret.empty()) {
                    ret += "\n";
                }
                auto p1 = s.find_first_of(' ', pad);
                auto p2 = s.find_last_of(' ', pad+1);
                if (p1 != Glib::ustring::npos && p2 != Glib::ustring::npos) {
                    if (p1 - pad > pad - p2) {
                        ret += s.substr(0, p2);
                        s = s.substr(p2+1);
                    } else {
                        ret += s.substr(0, p1);
                        s = s.substr(p1+1);
                    }
                } else if (p1 != Glib::ustring::npos) {
                    ret += s.substr(0, p1);
                    s = s.substr(p1+1);
                } else if (p2 != Glib::ustring::npos) {
                    ret += s.substr(0, p2);
                    s = s.substr(p2+1);
                } else {
                    break;
                }
            }
            if (!ret.empty()) {
                ret += "\n";
            }
            ret += s;
            return ret;
        };

    auto m = escapeHtmlChars(padding > 0 ? wrap(descr) : descr);
    if (is_error) {
        if (!unique_info_msg_.insert(descr).second) {
            return;
        }
        info_msg_.push_back(m);
        ++info_msg_num_;
    } else {
        info_msg_.clear();
        info_msg_.push_back(m);
    }
    Glib::ustring msg;
    const char *sep = "";
    if (reveal_conn_.connected()) {
        reveal_conn_.disconnect();
        if (is_error) {
            int n = int(info_msg_.size()) - options.max_error_messages;
            if (options.max_error_messages > 0 && n > 0) {
                info_msg_.assign(info_msg_.begin() + n, info_msg_.end());
            }
            n = info_msg_num_ - options.max_error_messages;
            if (n > 0) {
                msg = Glib::ustring::compose(M("ERROR_MSG_MAXERRORS"), n) + "\n";
            }
        }
    }
    for (auto &s : info_msg_) {
        msg += sep + s;
        sep = "\n";
    }
    info_label_->set_markup(Glib::ustring::compose("<span size=\"large\"><b>%1</b></span>", msg));
    info_box_->show();
    info_image_->set_visible(is_error);
    msg_revealer_->set_reveal_child(true);
    reveal_conn_ = Glib::signal_timeout().connect(sigc::mem_fun(*this, &RTWindow::hide_info_msg), duration > 0 ? duration : options.error_message_duration * (is_error ? 1.0 : 0.25));
}


//-----------------------------------------------------------------------------
// RTWindow
//-----------------------------------------------------------------------------

RTWindow::RTWindow():
    MessageWindow(),
    epanel(nullptr),
    fpanel(nullptr),
    // main_overlay_(nullptr),
    // msg_revealer_(nullptr),
    // info_label_(nullptr),
    // info_msg_num_(0),
    mainNB(nullptr),
    bpanel(nullptr),
    splash(nullptr),
    btn_fullscreen(nullptr),
    browser_tab_label_(nullptr),
    is_application_(false)
{

    if (options.is_new_version()) {
        RTImage::cleanup(true);
    }
    cacheMgr->init ();
    ProfilePanel::init (this);

    // ------- loading theme files

    Glib::RefPtr<Gdk::Screen> screen = Gdk::Screen::get_default();

    if (screen) {
        Gtk::Settings::get_for_screen (screen)->property_gtk_theme_name() = "Adwaita";
        Gtk::Settings::get_for_screen (screen)->property_gtk_application_prefer_dark_theme() = true;

        if (GTK_MINOR_VERSION < 20 || options.theme != Options::DEFAULT_THEME) {
            Glib::RefPtr<Glib::Regex> regex = Glib::Regex::create(Options::THEMEREGEXSTR, Glib::RegexCompileFlags::REGEX_CASELESS);
            Glib::ustring filename;
            Glib::MatchInfo mInfo;
            bool match = regex->match(options.theme + ".css", mInfo);
            if (match) {
                // save old theme (name + version)
                Glib::ustring initialTheme(options.theme);
                bool deprecated = !(mInfo.fetch(4).empty());

                // update version
                auto pos = options.theme.find("-GTK3-");
                Glib::ustring themeRootName(options.theme.substr(0, pos));
                if (GTK_MINOR_VERSION < 20) {
                    options.theme = themeRootName + "-GTK3-_19";
                } else {
                    options.theme = themeRootName + "-GTK3-20_";
                }
                // check if this version exist
                bool reset = false;
                if (!Glib::file_test(Glib::build_filename(options.ART_base_dir, "themes", options.theme + ".css"), Glib::FILE_TEST_EXISTS)) {
                    if (!deprecated) {
                        options.theme += "-DEPRECATED";
                        if (!Glib::file_test(Glib::build_filename(options.ART_base_dir, "themes", options.theme + ".css"), Glib::FILE_TEST_EXISTS)) {
                            reset = true;
                        }
                        
                        Glib::signal_timeout().connect(
                            sigc::slot<bool>(
                                [this,themeRootName]() -> bool
                                {
                                    error(Glib::ustring::compose("%1: %2 - %3: %4", M("GENERAL_WARNING"), M("PREFERENCES_APPEARANCE_THEME"), themeRootName, M("GENERAL_DEPRECATED_TOOLTIP")));
                                    return false;
                                }), options.error_message_duration);
                    } else {
                        reset = true;
                    }
                }
                if (reset) {
                    options.theme = initialTheme;
                }
            }
            filename = Glib::build_filename(options.ART_base_dir, "themes", options.theme + ".css");

            if (!match || !Glib::file_test(filename, Glib::FILE_TEST_EXISTS)) {
                options.theme = "Default";
                if (GTK_MINOR_VERSION < 20) {
                    options.theme = options.theme + "-GTK3-_19";
                }
            }
        }

        Preferences::switchThemeTo(options.theme);

        // Set the font face and size
        Glib::ustring css;
        if (options.fontFamily != "default") {
#if GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION < 20 //GTK318
            css = Glib::ustring::compose ("* { font-family: %1; font-size: %2px}", options.fontFamily, options.fontSize * (int)initialGdkScale);
#else
            css = Glib::ustring::compose ("* { font-family: %1; font-size: %2pt}", options.fontFamily, options.fontSize * (int)initialGdkScale);
#endif //GTK318
            if (options.pseudoHiDPISupport) {
            	fontScale = options.fontSize / (float)RTScalable::baseFontSize;
            }
            if (options.rtSettings.verbose > 1) {
                printf("\"Non-Default\" font size(%d) * scale(%d) / fontScale(%.3f)\n", options.fontSize, (int)initialGdkScale, fontScale);
            }
        } else {
            Glib::RefPtr<Gtk::StyleContext> style = Gtk::StyleContext::create();
            Pango::FontDescription pfd = style->get_font(Gtk::STATE_FLAG_NORMAL);
            int pt;
            if (pfd.get_set_fields() & Pango::FONT_MASK_SIZE) {
                int fontSize = pfd.get_size();
                bool isPix = pfd.get_size_is_absolute();
                int resolution = (int)style->get_screen()->get_resolution();
                if (isPix) {
                    // HOMBRE: guessing here...
                    // if resolution is lower than baseHiDPI, we're supposing that it's already expressed in a scale==1 scenario
                    if (resolution >= int(RTScalable::baseHiDPI)) {
                        // converting the resolution to a scale==1 scenario
                        resolution /= 2;
                    }
                    // 1pt =  1/72in @ 96 ppi
                    // HOMBRE: If the font unit is px, is it alredy scaled up to match the resolution ?
                    //                 px         >inch                 >pt    >"scaled pt"
                    pt = (int)(double(fontSize) / RTScalable::baseDPI * 72. * (96. / (double)resolution) + 0.49);
                } else {
                    pt = fontSize / Pango::SCALE;
                }
                if (options.pseudoHiDPISupport) {
                	fontScale = (float)pt / (float)RTScalable::baseFontSize;
                }
                if ((int)initialGdkScale > 1 || pt != RTScalable::baseFontSize) {
                    css = Glib::ustring::compose ("* { font-size: %1pt}", pt * (int)initialGdkScale);
                    if (options.rtSettings.verbose > 1) {
                        printf("\"Default\" font size(%d) * scale(%d) / fontScale(%.3f)\n", pt, (int)initialGdkScale, fontScale);
                    }
                }
            }
        }
        if (!css.empty()) {
            if (options.rtSettings.verbose > 1) {
                printf("CSS:\n%s\n\n", css.c_str());
            }
            try {
                cssForced = Gtk::CssProvider::create();
                cssForced->load_from_data (css);

                Gtk::StyleContext::add_provider_for_screen (screen, cssForced, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

            } catch (Glib::Error &err) {
                printf ("Error: \"%s\"\n", err.what().c_str());
            } catch (...) {
                printf ("Error: Can't find the font named \"%s\"\n", options.fontFamily.c_str());
            }
        }
    }

#ifndef NDEBUG
    else if (!screen) {
        printf ("ERROR: Can't get default screen!\n");
    }

#endif

    // ------- end loading theme files

    RTScalable::init(this);
    RTSurface::init();
    RTImage::init();
    WhiteBalance::init();
    MyExpander::init();
    FileBrowserEntry::init();

#ifndef WIN32
    const std::vector<Glib::RefPtr<Gdk::Pixbuf>> appIcons = {
        RTImage::createPixbufFromFile("ART-logo-16.png"),
        RTImage::createPixbufFromFile("ART-logo-24.png"),
        RTImage::createPixbufFromFile("ART-logo-48.png"),
        RTImage::createPixbufFromFile("ART-logo-128.png"),
        RTImage::createPixbufFromFile("ART-logo-256.png")
    };
    try {
        set_default_icon_list(appIcons);
    } catch (Glib::Exception& ex) {
        printf ("%s\n", ex.what().c_str());
    }
#endif

    versionStr = Glib::ustring(RTNAME " ") + versionString;

    set_title_decorated ("");
    set_resizable (true);
    set_decorated (true);
    set_default_size (options.windowWidth, options.windowHeight);
    set_modal (false);

    Gdk::Rectangle lMonitorRect;
    get_screen()->get_monitor_geometry (std::min (options.windowMonitor, Gdk::Screen::get_default()->get_n_monitors() - 1), lMonitorRect);

    if (options.windowMaximized) {
        move (lMonitorRect.get_x(), lMonitorRect.get_y());
        maximize();
#ifdef __APPLE__
        set_default_size(lMonitorRect.get_width(), lMonitorRect.get_height());
#endif
    } else {
        unmaximize();
        resize (options.windowWidth, options.windowHeight);

        if (options.windowX <= lMonitorRect.get_x() + lMonitorRect.get_width() && options.windowY <= lMonitorRect.get_y() + lMonitorRect.get_height()) {
            move (options.windowX, options.windowY);
        } else {
            move (lMonitorRect.get_x(), lMonitorRect.get_y());
        }
    }

    on_delete_has_run = false;
    is_fullscreen = false;
    property_destroy_with_parent().set_value (false);

    add_events(Gdk::KEY_PRESS_MASK | Gdk::SCROLL_MASK);
    signal_window_state_event().connect ( sigc::mem_fun (*this, &RTWindow::on_window_state_event) );
    signal_key_press_event().connect ( sigc::mem_fun (*this, &RTWindow::keyPressed) );
    signal_key_press_event().connect(sigc::mem_fun(*this, &RTWindow::keyPressedBefore), false);
    signal_key_release_event().connect(sigc::mem_fun(*this, &RTWindow::keyReleased));
    signal_scroll_event().connect(sigc::mem_fun(*this, &RTWindow::scrollPressed), false);

    Gtk::Widget *main_widget = nullptr;
    if (simpleEditor) {
        epanel = Gtk::manage ( new EditorPanel (nullptr) );
        epanel->setParent (this);
        epanel->setParentWindow (this);
        main_widget = epanel;

        pldBridge = nullptr; // No progress listener

        CacheManager* cm = CacheManager::getInstance();
        Thumbnail* thm = cm->getEntry ( argv1 );

        if (thm) {
            int error;
            rtengine::InitialImage *ii = rtengine::InitialImage::load (argv1, thm->getType() == FT_Raw, &error, nullptr);
            epanel->open ( thm, ii );
        }
    } else {
        mainNB = Gtk::manage (new Gtk::Notebook ());
        mainNB->set_name ("MainNotebook");
        mainNB->set_scrollable (true);
        mainNB->signal_switch_page().connect_notify ( sigc::mem_fun (*this, &RTWindow::on_mainNB_switch_page) );

        // Editor panel
        fpanel =  new FilePanel () ;
        fpanel->setParent (this);

        // decorate tab
        Gtk::Grid* fpanelLabelGrid = Gtk::manage (new Gtk::Grid ());
        setExpandAlignProperties (fpanelLabelGrid, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_CENTER);
        Gtk::Label* fpl = Gtk::manage (new Gtk::Label ( Glib::ustring (" ") + M ("MAIN_FRAME_EDITOR") ));
        browser_tab_label_ = fpl;

        if (!options.tabbedUI) {
            mainNB->set_tab_pos (Gtk::POS_LEFT);
            fpl->set_angle (90);
            RTImage* folderIcon = Gtk::manage (new RTImage ("folder-closed.svg"));
            fpanelLabelGrid->attach_next_to (*folderIcon, Gtk::POS_TOP, 1, 1);
            fpanelLabelGrid->attach_next_to (*fpl, Gtk::POS_TOP, 1, 1);
        } else {
            RTImage* folderIcon = Gtk::manage (new RTImage ("folder-closed.svg"));
            fpanelLabelGrid->attach_next_to (*folderIcon, Gtk::POS_RIGHT, 1, 1);
            fpanelLabelGrid->attach_next_to (*fpl, Gtk::POS_RIGHT, 1, 1);
        }

        fpanelLabelGrid->set_tooltip_markup (M ("MAIN_FRAME_FILEBROWSER_TOOLTIP"));
        fpanelLabelGrid->show_all ();
        mainNB->append_page (*fpanel, *fpanelLabelGrid);


        // Batch Queue panel
        bpanel = Gtk::manage ( new BatchQueuePanel (fpanel->fileCatalog) );

        // decorate tab, the label is unimportant since its updated in batchqueuepanel anyway
        Gtk::Label* lbq = Gtk::manage ( new Gtk::Label (M ("MAIN_FRAME_QUEUE")) );

        if (!options.tabbedUI) {
            lbq->set_angle(90);
        }
        mainNB->append_page (*bpanel, *lbq);


        if (isSingleTabMode()) {
            createSetmEditor();
        }

        mainNB->set_current_page (mainNB->page_num (*fpanel));

        // filling bottom box
        iFullscreen = new RTImage ("fullscreen-enter.svg");
        iFullscreen_exit = new RTImage ("fullscreen-leave.svg");

        Gtk::Button* preferences = Gtk::manage (new Gtk::Button ());
        setExpandAlignProperties (preferences, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_CENTER);
        preferences->set_relief(Gtk::RELIEF_NONE);
        preferences->set_image (*Gtk::manage (new RTImage ("preferences.svg")));
        preferences->set_tooltip_markup (M ("MAIN_BUTTON_PREFERENCES"));
        preferences->signal_clicked().connect ( sigc::mem_fun (*this, &RTWindow::showPreferences) );

        btn_fullscreen = Gtk::manage ( new Gtk::Button());
        setExpandAlignProperties (btn_fullscreen, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_CENTER);
        btn_fullscreen->set_relief(Gtk::RELIEF_NONE);
        btn_fullscreen->set_tooltip_markup (M ("MAIN_BUTTON_FULLSCREEN"));
        btn_fullscreen->set_image (*iFullscreen);
        btn_fullscreen->signal_clicked().connect ( sigc::mem_fun (*this, &RTWindow::toggle_fullscreen) );
        setExpandAlignProperties (&prProgBar, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_CENTER);
        prProgBar.set_show_text (true);

        Gtk::Grid* actionGrid = Gtk::manage (new Gtk::Grid ());
        actionGrid->set_row_spacing (2);
        actionGrid->set_column_spacing (2);

        setExpandAlignProperties (actionGrid, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_CENTER);

        if (!options.tabbedUI) {
            prProgBar.set_orientation (Gtk::ORIENTATION_VERTICAL);
            prProgBar.set_inverted (true);
            actionGrid->set_orientation (Gtk::ORIENTATION_VERTICAL);
            actionGrid->attach_next_to (prProgBar, Gtk::POS_BOTTOM, 1, 1);
            actionGrid->attach_next_to (*preferences, Gtk::POS_BOTTOM, 1, 1);
            actionGrid->attach_next_to (*btn_fullscreen, Gtk::POS_BOTTOM, 1, 1);
            mainNB->set_action_widget (actionGrid, Gtk::PACK_END);
        } else {
            prProgBar.set_orientation (Gtk::ORIENTATION_HORIZONTAL);
            actionGrid->set_orientation (Gtk::ORIENTATION_HORIZONTAL);
            actionGrid->attach_next_to (prProgBar, Gtk::POS_RIGHT, 1, 1);
            actionGrid->attach_next_to (*preferences, Gtk::POS_RIGHT, 1, 1);
            actionGrid->attach_next_to (*btn_fullscreen, Gtk::POS_RIGHT, 1, 1);
            mainNB->set_action_widget (actionGrid, Gtk::PACK_END);
        }

        actionGrid->show_all();

        pldBridge = new PLDBridge (static_cast<rtengine::ProgressListener*> (this));

        main_widget = mainNB;

        bpanel->init (this);

        if (!argv1.empty() && !remote) {
            Thumbnail* thm = cacheMgr->getEntry (argv1);

            if (thm) {
                fpanel->fileCatalog->openRequested ({thm});
            }
        }
    }

    init(main_widget);

    cacheMgr->setProgressListener(this);
    ProfileStore::getInstance()->setProgressListener(this);

    const auto on_show =
        [this](GdkEventAny *e) -> bool
        {
            static bool first_draw = true;
            if (first_draw) {
                first_draw = false;

                // this is a ugly hack to force the right panel to get the
                // exact size as specified in options.browserToolPanelWidth I
                // don't know why it's needed, but I couldn't find another
                // solution...
                const auto doit =
                    [this]() -> bool
                    {
                        static int count = 10;
                        if (fpanel) {
                            fpanel->setAspect();
                        }
                        
                        if (simpleEditor) {
                            epanel->setAspect();
                        }
                        return --count;
                    };
                doit();
                idle_register.add(doit);
            }
            return false;
        };
    signal_map_event().connect(sigc::slot<bool,GdkEventAny*>(on_show));
}


bool RTWindow::on_draw(const ::Cairo::RefPtr<::Cairo::Context> &cr)
{
    return Gtk::Window::on_draw(cr);
}


RTWindow::~RTWindow()
{
    idle_register.destroy();
    
    cacheMgr->setProgressListener(nullptr);
    ProfileStore::getInstance()->setProgressListener(nullptr);
    
    if (!simpleEditor) {
        delete pldBridge;
    }

    pldBridge = nullptr;

    if (fpanel) {
        delete fpanel;
    }

    RTImage::cleanup();
}

void RTWindow::on_realize()
{
    Gtk::Window::on_realize();

    art::gdk_set_monitor_profile(get_window()->gobj(), options.rtSettings.os_monitor_profile);
    mainWindowCursorManager.init(get_window());

    // Display release notes only if new major version.
    bool waitForSplash = false;
    if (options.is_new_version()) {
        // Update the version parameter with the right value
        options.version = versionString;

        splash = new Splash (*this);
        splash->set_transient_for (*this);
        splash->signal_delete_event().connect ( sigc::mem_fun (*this, &RTWindow::splashClosed) );

        waitForSplash = true;
        splash->show ();
    }

    if (!waitForSplash) {
        showErrors();
    }
}

void RTWindow::showErrors()
{
    // alerting users if the default raw and image profiles are missing
    if (options.is_defProfRawMissing()) {
        options.defProfRaw = Options::DEFPROFILE_RAW;
        Gtk::MessageDialog msgd (*this, Glib::ustring::compose (M ("OPTIONS_DEFRAW_MISSING"), options.defProfRaw), true, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        msgd.run ();
    }
    if (options.is_bundledDefProfRawMissing()) {
        Gtk::MessageDialog msgd (*this, Glib::ustring::compose (M ("OPTIONS_BUNDLED_MISSING"), options.defProfRaw), true, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        msgd.run ();
        options.defProfRaw = Options::DEFPROFILE_INTERNAL;
    }

    if (options.is_defProfImgMissing()) {
        options.defProfImg = Options::DEFPROFILE_IMG;
        Gtk::MessageDialog msgd (*this, Glib::ustring::compose (M ("OPTIONS_DEFIMG_MISSING"), options.defProfImg), true, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        msgd.run ();
    }
    if (options.is_bundledDefProfImgMissing()) {
        Gtk::MessageDialog msgd (*this, Glib::ustring::compose (M ("OPTIONS_BUNDLED_MISSING"), options.defProfImg), true, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        msgd.run ();
        options.defProfImg = Options::DEFPROFILE_INTERNAL;
    }
}

bool RTWindow::on_configure_event (GdkEventConfigure* event)
{
    if (!is_maximized() && is_visible()) {
        get_size (options.windowWidth, options.windowHeight);
        get_position (options.windowX, options.windowY);
    }

    RTImage::setDPInScale(RTScalable::getDPI(), RTScalable::getScale());   // will update the RTImage   on scale/resolution change
    RTSurface::setDPInScale(RTScalable::getDPI(), RTScalable::getScale()); // will update the RTSurface on scale/resolution change

    return Gtk::Widget::on_configure_event (event);
}

bool RTWindow::on_window_state_event (GdkEventWindowState* event)
{
    if (event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED) {
        options.windowMaximized = event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED;
    }

    return Gtk::Widget::on_window_state_event (event);
}

void RTWindow::on_mainNB_switch_page (Gtk::Widget* widget, guint page_num)
{
    if (!on_delete_has_run) {
        if (isEditorPanel (page_num)) {
            if (isSingleTabMode() && epanel) {
                MoveFileBrowserToEditor();
            }

            EditorPanel *ep = static_cast<EditorPanel*> (mainNB->get_nth_page (page_num));
            ep->setAspect();

            auto fn = ep->getFileName();
            if (!fn.empty()) {
                set_title_decorated(fn);
                if (isSingleTabMode() && fpanel) {
                    if (!fpanel->fileCatalog->isSelected(fn)) {
                        fpanel->fileCatalog->selectImage(fn, false);
                    }
                }
            }
        } else {
            if (mainNB->get_nth_page(page_num) == bpanel) {
                bpanel->refreshProfiles();
            }
            
            // in single tab mode with command line filename epanel does not exist yet
            if (isSingleTabMode() && epanel) {
                // Save profile on leaving the editor panel
                epanel->saveProfile();

                // Moving the FileBrowser only if the user has switched to the FileBrowser tab
                if (mainNB->get_nth_page (page_num) == fpanel) {
                    MoveFileBrowserToMain();
                }
            }
        }
    }
}

void RTWindow::addEditorPanel (EditorPanel* ep, const std::string &name)
{
    if (options.multiDisplayMode > 0) {
        EditWindow * wndEdit = EditWindow::getInstance (this);
        wndEdit->show();
        wndEdit->addEditorPanel (ep, name);
        wndEdit->toFront();
    } else {
        ep->setParent (this);
        ep->setParentWindow (this);

        // construct closeable tab for the image
        Gtk::Grid* titleGrid = Gtk::manage (new Gtk::Grid ());
        titleGrid->set_tooltip_markup (name);
        RTImage *closebimg = Gtk::manage (new RTImage ("cancel-small.svg"));
        Gtk::Button* closeb = Gtk::manage (new Gtk::Button ());
        closeb->set_name ("CloseButton");
        closeb->add (*closebimg);
        closeb->set_relief (Gtk::RELIEF_NONE);
        closeb->set_focus_on_click (false);
        closeb->signal_clicked().connect ( sigc::bind (sigc::mem_fun (*this, &RTWindow::remEditorPanel), ep));

        if (!EditWindow::isMultiDisplayEnabled()) {
            titleGrid->attach_next_to (*Gtk::manage (new RTImage ("aperture.svg")), Gtk::POS_RIGHT, 1, 1);
        }
        titleGrid->attach_next_to (*Gtk::manage (new Gtk::Label (Glib::path_get_basename (name))), Gtk::POS_RIGHT, 1, 1);
        titleGrid->attach_next_to (*closeb, Gtk::POS_RIGHT, 1, 1);
        titleGrid->show_all ();
//GTK318
#if GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION < 20
        titleGrid->set_column_spacing (2);
#endif
//GTK318

        mainNB->append_page (*ep, *titleGrid);
        //ep->setAspect ();
        mainNB->set_current_page (mainNB->page_num (*ep));
        mainNB->set_tab_reorderable (*ep, true);

        set_title_decorated (name);
        epanels[ name ] = ep;
        filesEdited.insert ( name );
        fpanel->refreshEditedState (filesEdited);
        ep->tbTopPanel_1_visible (false); //hide the toggle Top Panel button
    }
}

void RTWindow::remEditorPanel (EditorPanel* ep)
{
    if (ep->getIsProcessing()) {
        return;    // Will crash if destroyed while loading
    }

    if (options.multiDisplayMode > 0) {
        EditWindow * wndEdit = EditWindow::getInstance (this);
        wndEdit->remEditorPanel (ep);
    } else {
        bool queueHadFocus = (mainNB->get_current_page() == mainNB->page_num (*bpanel));
        epanels.erase (ep->getFileName());
        filesEdited.erase (ep->getFileName ());
        fpanel->refreshEditedState (filesEdited);

        mainNB->remove_page (*ep);

        if (!isEditorPanel (mainNB->get_current_page())) {
            if (!queueHadFocus) {
                mainNB->set_current_page (mainNB->page_num (*fpanel));
            }

            set_title_decorated ("");
        } else {
            EditorPanel* ep = static_cast<EditorPanel*> (mainNB->get_nth_page (mainNB->get_current_page()));
            set_title_decorated (ep->getFileName());
        }

        // TODO: ask what to do: close & apply, close & apply selection, close & revert, cancel
    }
}

bool RTWindow::selectEditorPanel (const std::string &name)
{
    if (options.multiDisplayMode > 0) {
        EditWindow * wndEdit = EditWindow::getInstance (this);

        if (wndEdit->selectEditorPanel (name)) {
            set_title_decorated (name);
            wndEdit->toFront();
            return true;
        }
    } else {
        std::map<Glib::ustring, EditorPanel*>::iterator iep = epanels.find (name);

        if (iep != epanels.end()) {
            mainNB->set_current_page (mainNB->page_num (*iep->second));
            set_title_decorated (name);
            return true;
        } else {
            //set_title_decorated(name);
            //printf("RTWindow::selectEditorPanel - plain set\n");
        }
    }

    return false;
}


void RTWindow::quit()
{
    if (!on_delete_event (nullptr)) {
        if (isApplication()) {
            unset_application();    
        } else {
            gtk_main_quit();
        }
    }
}


bool RTWindow::keyPressed (GdkEventKey* event)
{
    bool ctrl = event->state & GDK_CONTROL_MASK;
    //bool shift = event->state & GDK_SHIFT_MASK;

    auto keyval = getKeyval(event);

    bool try_quit = false;
#if defined(__APPLE__)
    bool apple_cmd = event->state & GDK_MOD2_MASK;

    if (keyval == GDK_KEY_q && apple_cmd) {
        try_quit = true;
    }

#else

    if (keyval == GDK_KEY_q && ctrl) {
        try_quit = true;
    }

#endif

    if (try_quit) {
        quit();
    }

    if (keyval == GDK_KEY_F11) {
        toggle_fullscreen();
    }

    if (simpleEditor)
        // in simpleEditor mode, there's no other tab that can handle pressed keys, so we can send the event to editor panel then return
    {
        return epanel->handleShortcutKey (event);
    };

    if (ctrl) {
        switch (keyval) {
            case GDK_KEY_F2: // file browser panel
                mainNB->set_current_page (mainNB->page_num (*fpanel));
                return true;

            case GDK_KEY_F3: // batch queue panel
                mainNB->set_current_page (mainNB->page_num (*bpanel));
                return true;

            case GDK_KEY_F4: //single tab mode, editor panel
                if (isSingleTabMode() && epanel) {
                    mainNB->set_current_page (mainNB->page_num (*epanel));
                }

                return true;

            case GDK_KEY_w: //multi-tab mode, close editor panel
                if (!isSingleTabMode() &&
                        mainNB->get_current_page() != mainNB->page_num (*fpanel) &&
                        mainNB->get_current_page() != mainNB->page_num (*bpanel)) {

                    EditorPanel* ep = static_cast<EditorPanel*> (mainNB->get_nth_page (mainNB->get_current_page()));
                    remEditorPanel (ep);
                    return true;
                }
        }
    }

    if (mainNB->get_current_page() == mainNB->page_num (*fpanel)) {
        return fpanel->handleShortcutKey (event);
    } else if (mainNB->get_current_page() == mainNB->page_num (*bpanel)) {
        return bpanel->handleShortcutKey (event);
    } else {
        EditorPanel* ep = static_cast<EditorPanel*> (mainNB->get_nth_page (mainNB->get_current_page()));
        return ep->handleShortcutKey (event);
    }

    return false;
}


bool RTWindow::keyPressedBefore(GdkEventKey* event)
{
    if (simpleEditor) {
        return epanel->keyPressedBefore(event);
    };

    if (mainNB->get_current_page() == mainNB->page_num(*fpanel)) {
        return false;
    } else if (mainNB->get_current_page() == mainNB->page_num (*bpanel)) {
        return false;
    } else {
        EditorPanel *ep = static_cast<EditorPanel *>(mainNB->get_nth_page (mainNB->get_current_page()));
        return ep->keyPressedBefore(event);
    }

    return false;
}


bool RTWindow::keyReleased(GdkEventKey* event)
{
    if (simpleEditor) {
        return epanel->keyReleased(event);
    } else if (mainNB->get_current_page() == mainNB->page_num (*fpanel)) {
        return false;
    } else if (mainNB->get_current_page() == mainNB->page_num (*bpanel)) {
        return false;
    } else {
        EditorPanel* ep = static_cast<EditorPanel*> (mainNB->get_nth_page (mainNB->get_current_page()));
        return ep->keyReleased(event);
    }
}


bool RTWindow::scrollPressed(GdkEventScroll *event)
{
    if (simpleEditor) {
        return epanel->scrollPressed(event);
    } else if (mainNB->get_current_page() == mainNB->page_num (*fpanel)) {
        return false;
    } else if (mainNB->get_current_page() == mainNB->page_num (*bpanel)) {
        return false;
    } else {
        EditorPanel* ep = static_cast<EditorPanel*> (mainNB->get_nth_page (mainNB->get_current_page()));
        return ep->scrollPressed(event);
    }
}

void RTWindow::addBatchQueueJob (BatchQueueEntry* bqe, bool head)
{

    std::vector<BatchQueueEntry*> entries;
    entries.push_back (bqe);
    bpanel->addBatchQueueJobs (entries, head);
    fpanel->queue_draw ();
}

void RTWindow::addBatchQueueJobs(const std::vector<BatchQueueEntry*>& entries)
{
    bpanel->addBatchQueueJobs (entries, false);
    fpanel->queue_draw ();
}

bool RTWindow::on_delete_event (GdkEventAny* event)
{

    if (on_delete_has_run) {
        // on Mac OSX we can get multiple events
        return false;
    }

    // Check if any editor is still processing, and do NOT quit if so. Otherwise crashes and inconsistent caches
    bool isProcessing = false;
    EditWindow* editWindow = nullptr;

    if (isSingleTabMode() || simpleEditor) {
        isProcessing = epanel->getIsProcessing();
    } else if (options.multiDisplayMode > 0) {
        editWindow = EditWindow::getInstance (this, false);
        isProcessing = editWindow->isProcessing();
    } else {
        int pageCount = mainNB->get_n_pages();

        for (int i = 0; i < pageCount && !isProcessing; i++) {
            if (isEditorPanel (i)) {
                isProcessing |= (static_cast<EditorPanel*> (mainNB->get_nth_page (i)))->getIsProcessing();
            }
        }
    }

    if (isProcessing) {
        return true;
    }

    if ( fpanel ) {
        fpanel->saveOptions ();
    }

    if ( bpanel ) {
        bpanel->saveOptions ();
    }

    if (epanel) {
        epanel->cleanup();
    }
    for (auto &p : epanels) {
        p.second->cleanup();
    }

    if ((isSingleTabMode() || simpleEditor) && epanel->isRealized()) {
        epanel->saveProfile();
        epanel->writeOptions ();
    } else {
        if (options.multiDisplayMode > 0 && editWindow) {
            editWindow->closeOpenEditors();
            editWindow->writeOptions();
        } else if (epanels.size()) {
            // Storing the options of the last EditorPanel before Gtk destroys everything
            // Look at the active panel first, if any, otherwise look at the first one (sorted on the filename)

            int page = mainNB->get_current_page();
            Gtk::Widget *w = mainNB->get_nth_page (page);
            bool optionsWritten = false;

            for (std::map<Glib::ustring, EditorPanel*>::iterator i = epanels.begin(); i != epanels.end(); ++i) {
                if (i->second == w) {
                    i->second->writeOptions();
                    optionsWritten = true;
                }
            }

            if (!optionsWritten) {
                // fallback solution: save the options of the first editor panel
                std::map<Glib::ustring, EditorPanel*>::iterator i = epanels.begin();
                i->second->writeOptions();
            }
        }
    }

    cacheMgr->closeCache ();  // also makes cleanup if too large
    WhiteBalance::cleanup();
    ProfilePanel::cleanup();
    ClutComboBox::cleanup();
    MyExpander::cleanup();
    mainWindowCursorManager.cleanup();
    editWindowCursorManager.cleanup();
    BatchQueueEntry::savedAsIcon.reset();
    FileBrowserEntry::editedIcon.reset();
    FileBrowserEntry::recentlySavedIcon.reset();
    FileBrowserEntry::enqueuedIcon.reset();
    FileBrowserEntry::hdr.reset();
    FileBrowserEntry::ps.reset();

    if (!options.windowMaximized) {
        get_size (options.windowWidth, options.windowHeight);
        get_position (options.windowX, options.windowY);
    }

    options.windowMonitor = get_screen()->get_monitor_at_window (get_window());

    try {
        Options::save ();
    } catch (Options::Error &e) {
        Gtk::MessageDialog msgd (getToplevelWindow (this), e.get_msg(), true, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_CLOSE, true);
        msgd.run();
    }

    hide();

    on_delete_has_run = true;
    return false;
}


void RTWindow::writeToolExpandedStatus (std::vector<int> &tpOpen)
{
    if ((isSingleTabMode() || gimpPlugin) && epanel->isRealized()) {
        epanel->writeToolExpandedStatus (tpOpen);
    } else {
        // Storing the options of the last EditorPanel before Gtk destroys everything
        // Look at the active panel first, if any, otherwise look at the first one (sorted on the filename)
        if (epanels.size()) {
            int page = mainNB->get_current_page();
            Gtk::Widget *w = mainNB->get_nth_page (page);
            bool optionsWritten = false;

            for (std::map<Glib::ustring, EditorPanel*>::iterator i = epanels.begin(); i != epanels.end(); ++i) {
                if (i->second == w) {
                    i->second->writeToolExpandedStatus (tpOpen);
                    optionsWritten = true;
                }
            }

            if (!optionsWritten) {
                // fallback solution: save the options of the first editor panel
                std::map<Glib::ustring, EditorPanel*>::iterator i = epanels.begin();
                i->second->writeToolExpandedStatus (tpOpen);
            }
        }
    }
}


void RTWindow::showPreferences ()
{
    Preferences *pref = new Preferences (this);
    pref->run ();
    delete pref;

    fpanel->optionsChanged ();

    if (epanel) {
        epanel->defaultMonitorProfileChanged (options.rtSettings.monitorProfile, options.rtSettings.autoMonitorProfile);
    }

    for (const auto &p : epanels) {
        p.second->defaultMonitorProfileChanged (options.rtSettings.monitorProfile, options.rtSettings.autoMonitorProfile);
    }
}

void RTWindow::setProgress(double p)
{
    prProgBar.set_fraction(p);
}

void RTWindow::setProgressStr(const Glib::ustring& str)
{
    if (options.tabbedUI) {
        prProgBar.set_text(str);
    }
}

void RTWindow::setProgressState(bool inProcessing)
{
    if (inProcessing) {
        prProgBar.show();
    } else {
        prProgBar.hide();
    }
}

void RTWindow::error(const Glib::ustring& descr)
{
    showError(descr);
}


void RTWindow::showError(const Glib::ustring& descr)
{
    if (options.multiDisplayMode > 0) {
        EditWindow::getInstance(this)->showError(descr);
    } else {
        MessageWindow::showError(descr);
    }
}



void RTWindow::showInfo(const Glib::ustring &msg, double duration=0.0)
{
    if (options.multiDisplayMode > 0) {
        EditWindow::getInstance(this)->showInfo(msg, duration);
    } else {
        MessageWindow::showInfo(msg, duration);
    }
}


void RTWindow::toggle_fullscreen ()
{
    if (is_fullscreen) {
        unfullscreen();
        is_fullscreen = false;

        if (btn_fullscreen) {
            //btn_fullscreen->set_label(M("MAIN_BUTTON_FULLSCREEN"));
            btn_fullscreen->set_tooltip_markup (M ("MAIN_BUTTON_FULLSCREEN"));
            btn_fullscreen->set_image (*iFullscreen);
        }
    } else {
        fullscreen();
        is_fullscreen = true;

        if (btn_fullscreen) {
            //btn_fullscreen->set_label(M("MAIN_BUTTON_UNFULLSCREEN"));
            btn_fullscreen->set_tooltip_markup (M ("MAIN_BUTTON_UNFULLSCREEN"));
            btn_fullscreen->set_image (*iFullscreen_exit);
        }
    }
}

void RTWindow::SetEditorCurrent()
{
    mainNB->set_current_page (mainNB->page_num (*epanel));
}

void RTWindow::SetMainCurrent()
{
    mainNB->set_current_page (mainNB->page_num (*fpanel));
}

void RTWindow::MoveFileBrowserToMain()
{
    if ( fpanel->ribbonPane->get_children().empty()) {
        FileCatalog *fCatalog = fpanel->fileCatalog;
        epanel->catalogPane->remove (*fCatalog);
        fpanel->ribbonPane->add (*fCatalog);
        fCatalog->enableTabMode (false);
        fCatalog->tbLeftPanel_1_visible (true);
        fCatalog->tbRightPanel_1_visible (true);
        if (fpanel->isInspectorVisible()) {
            fCatalog->enableInspector();
        }
    }
}

void RTWindow::MoveFileBrowserToEditor()
{
    if (epanel->catalogPane->get_children().empty() ) {
        FileCatalog *fCatalog = fpanel->fileCatalog;
        fpanel->ribbonPane->remove (*fCatalog);
        fCatalog->disableInspector();
        epanel->catalogPane->add (*fCatalog);
        epanel->showTopPanel (options.editorFilmStripOpened);
        fCatalog->enableTabMode (true);
        fCatalog->refreshHeight();
        fCatalog->tbLeftPanel_1_visible (false);
        fCatalog->tbRightPanel_1_visible (false);
    }
}

void RTWindow::updateProfiles (const Glib::ustring &printerProfile, rtengine::RenderingIntent printerIntent, bool printerBPC)
{
    if (epanel) {
        epanel->updateProfiles (printerProfile, printerIntent, printerBPC);
    }

    for (auto panel : epanels) {
        panel.second->updateProfiles (printerProfile, printerIntent, printerBPC);
    }
}

void RTWindow::updateTPVScrollbar (bool hide)
{
    fpanel->updateTPVScrollbar (hide);

    if (epanel) {
        epanel->updateTPVScrollbar (hide);
    }

    for (auto panel : epanels) {
        panel.second->updateTPVScrollbar (hide);
    }
}

void RTWindow::updateFBQueryTB (bool singleRow)
{
    fpanel->fileCatalog->updateFBQueryTB (singleRow);
}

void RTWindow::updateFBToolBarVisibility (bool showFilmStripToolBar)
{
    fpanel->fileCatalog->updateFBToolBarVisibility (showFilmStripToolBar);
}

void RTWindow::updateHistogramPosition (int oldPosition, int newPosition)
{
    if (epanel) {
        epanel->updateHistogramPosition (oldPosition, newPosition);
    }

    for (auto panel : epanels) {
        panel.second->updateHistogramPosition (oldPosition, newPosition);
    }
}

bool RTWindow::splashClosed (GdkEventAny* event)
{
    delete splash;
    splash = nullptr;
    showErrors();
    return true;
}

void RTWindow::set_title_decorated (Glib::ustring fname)
{
    Glib::ustring subtitle;

    if (!fname.empty()) {
        subtitle = " - " + fname;
    }

    set_title (versionStr + subtitle);
}

void RTWindow::closeOpenEditors()
{
    std::map<Glib::ustring, EditorPanel*>::const_iterator itr;
    itr = epanels.begin();

    while (itr != epanels.end()) {
        remEditorPanel ((*itr).second);
        itr = epanels.begin();
    }
}

bool RTWindow::isEditorPanel (Widget* panel)
{
    return (panel != bpanel) && (panel != fpanel);
}

bool RTWindow::isEditorPanel (guint pageNum)
{
    return isEditorPanel (mainNB->get_nth_page (pageNum));
}

void RTWindow::setEditorMode (bool tabbedUI)
{
    MoveFileBrowserToMain();
    closeOpenEditors();
    SetMainCurrent();

    if (tabbedUI) {
        mainNB->remove_page (*epanel);
        epanel = nullptr;
        set_title_decorated ("");
    } else {
        createSetmEditor();
        epanel->show_all();
        set_title_decorated ("");
    }
}

void RTWindow::createSetmEditor()
{
    // Editor panel, single-tab mode only
    epanel = Gtk::manage ( new EditorPanel (fpanel) );
    epanel->setParent (this);
    epanel->setParentWindow (this);

    // decorate tab
    Gtk::Grid* const editorLabelGrid = Gtk::manage (new Gtk::Grid ());
    setExpandAlignProperties (editorLabelGrid, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_CENTER);
    Gtk::Label* const el = Gtk::manage (new Gtk::Label ( Glib::ustring (" ") + M ("MAIN_FRAME_EDITOR") ));

    const auto pos = !options.tabbedUI ? Gtk::POS_TOP : Gtk::POS_RIGHT;
    if (!options.tabbedUI) {
        el->set_angle(90);
    }

    editorLabelGrid->attach_next_to (*Gtk::manage (new RTImage ("aperture.svg")), pos, 1, 1);
    editorLabelGrid->attach_next_to (*el, pos, 1, 1);

    editorLabelGrid->set_tooltip_markup (M ("MAIN_FRAME_EDITOR_TOOLTIP"));
    editorLabelGrid->show_all ();
    epanel->tbTopPanel_1_visible (true); //show the toggle Top Panel button
    mainNB->append_page (*epanel, *editorLabelGrid);

}
