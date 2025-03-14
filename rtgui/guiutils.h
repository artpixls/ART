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
#pragma once

#include <functional>
#include <map>

#include <gtkmm.h>

#include <cairomm/cairomm.h>

#include "../rtengine/coord.h"
#include "../rtengine/noncopyable.h"
#include "../rtengine/rtengine.h"

#include "rtimage.h"

// for convenience...
#include "pathutils.h"


Glib::ustring escapeHtmlChars(const Glib::ustring &src);
bool removeIfThere (Gtk::Container* cont, Gtk::Widget* w, bool increference = true);
void thumbInterp (const unsigned char* src, int sw, int sh, unsigned char* dst, int dw, int dh);
bool confirmOverwrite (Gtk::Window& parent, const std::string& filename);
void writeFailed (Gtk::Window& parent, const std::string& filename);
void drawCrop(Glib::RefPtr<Gtk::StyleContext> style, Cairo::RefPtr<Cairo::Context> cr, int imx, int imy, int imw, int imh, int startx, int starty, double scale, const rtengine::procparams::CropParams& cparams, bool drawGuide = true, bool useBgColor = true, bool fullImageVisible = true);
void drawCrop(Cairo::RefPtr<Cairo::Context> cr, int imx, int imy, int imw, int imh, int startx, int starty, double scale, const rtengine::procparams::CropParams& cparams, bool drawGuide = true, bool useBgColor = true, bool fullImageVisible = true);
gboolean acquireGUI(void* data);
void setExpandAlignProperties(Gtk::Widget *widget, bool hExpand, bool vExpand, enum Gtk::Align hAlign, enum Gtk::Align vAlign);
Gtk::Border getPadding(const Glib::RefPtr<Gtk::StyleContext> style);

class IdleRegister final: public rtengine::NonCopyable {
public:
    ~IdleRegister();

    void add(std::function<bool ()> function, gint priority = G_PRIORITY_DEFAULT_IDLE);
    void destroy();

private:
    struct DataWrapper {
        IdleRegister* const self;
        std::function<bool ()> function;
    };

    std::map<const DataWrapper*, guint> ids;
    MyMutex mutex;
};

// TODO: The documentation says gdk_threads_enter and gdk_threads_leave should be replaced
// by g_main_context_invoke(), g_idle_add() and related functions, but this will require more extensive changes.
// We silence those warnings until then so that we notice the others.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

/**
 * @brief Lock GTK for critical section.
 *
 * Will unlock on destruction. To use:
 *
 *   <code>
 *     {
 *       GThreadLock lock;
 *       // critical code
 *     }
 *   </code>
 */
class GThreadLock {
public:
    GThreadLock()
    {
        gdk_threads_enter();
    }
    ~GThreadLock()
    {
        gdk_threads_leave();
    }
};

/**
 * @brief Unlock GTK critical section.
 *
 * Will relock on destruction.
 */
class GThreadUnLock {
public:
    GThreadUnLock()
    {
        gdk_threads_leave();
    }
    ~GThreadUnLock()
    {
        gdk_threads_enter();
    }
};

#pragma GCC diagnostic pop

class ConnectionBlocker {
public:
    explicit ConnectionBlocker (Gtk::Widget *associatedWidget, sigc::connection& connection) : connection (associatedWidget ? &connection : nullptr), wasBlocked(false)
    {
        if (this->connection) {
            wasBlocked = connection.block();
        }
    }
    explicit ConnectionBlocker (sigc::connection& connection) : connection (&connection)
    {
            wasBlocked = connection.block();
    }
    ~ConnectionBlocker ()
    {
        if (connection) {
            connection->block(wasBlocked);
        }
    }
private:
    sigc::connection *connection;
    bool wasBlocked;
};

/**
 * @brief Glue box to control visibility of the MyExpender's content ; also handle the frame around it
 */
class ExpanderBox: public Gtk::EventBox {
private:
    Gtk::Container *pC;

public:
    explicit ExpanderBox( Gtk::Container *p);
    ~ExpanderBox( ) override
    {
        delete pC;
    }

    void setLevel(int level);

    void show() {}
    void show_all();
    void hide() {}
    void set_visible(bool isVisible = true) {}

    void showBox();
    void hideBox();

//  bool on_draw(const ::Cairo::RefPtr< Cairo::Context> &cr);
};

/**
 * @brief A custom Expander class, that can handle widgets in the title bar
 *
 * Custom made expander for responsive widgets in the header. It also handle a "enabled/disabled" property that display
 * a different arrow depending on this boolean value.
 *
 * Warning: once you've instantiated this class with a text label or a widget label, you won't be able to revert to the other solution.
 */
class MyExpander : public Gtk::VBox {
public:
    typedef sigc::signal<void> type_signal_enabled_toggled;
private:
    type_signal_enabled_toggled message;
    static Glib::RefPtr<RTImage> inconsistentImage; /// "inconsistent" image, displayed when useEnabled is true ; in this case, nothing will tell that an expander is opened/closed
    static Glib::RefPtr<RTImage> enabledImage;      ///      "enabled" image, displayed when useEnabled is true ; in this case, nothing will tell that an expander is opened/closed
    static Glib::RefPtr<RTImage> disabledImage;     ///     "disabled" image, displayed when useEnabled is true ; in this case, nothing will tell that an expander is opened/closed
    static Glib::RefPtr<RTImage> openedImage;       ///       "opened" image, displayed when useEnabled is false
    static Glib::RefPtr<RTImage> closedImage;       ///       "closed" image, displayed when useEnabled is false
    bool enabled;               /// Enabled feature (default to true)
    bool inconsistent;          /// True if the enabled button is inconsistent
    Gtk::EventBox *titleEvBox;  /// EventBox of the title, to get a connector from it
    Gtk::HBox *headerHBox;
    bool flushEvent;            /// Flag to control the weird event mechanism of Gtk (please prove me wrong!)
    ExpanderBox* expBox;        /// Frame that includes the child and control its visibility
    Gtk::EventBox *imageEvBox;  /// Enable/Disable or Open/Close arrow event box

    using Gtk::Container::add;

    /// Triggered on opened/closed event
    bool on_toggle(GdkEventButton* event);
    /// Triggered on enabled/disabled change -> will emit a toggle event to the connected objects
    bool on_enabled_change(GdkEventButton* event);
    /// Used to handle the colored background for the whole Title
    bool on_enter_leave_title (GdkEventCrossing* event);
    /// Used to handle the colored background for the Enable button
    bool on_enter_leave_enable (GdkEventCrossing* event);

    void updateStyle();

protected:
    Gtk::Container* child;      /// Gtk::Contained to display below the expander's title
    Gtk::Widget* headerWidget;  /// Widget to display in the header, next to the arrow image ; can be NULL if the "string" version of the ctor has been used
    RTImage* statusImage;       /// Image to display the opened/closed status (if useEnabled is false) of the enabled/disabled status (if useEnabled is true)
    Gtk::Label* label;          /// Text to display in the header, next to the arrow image ; can be NULL if the "widget" version of the ctor has been used
    bool useEnabled;            /// Set whether to handle an enabled/disabled feature and display the appropriate images

    Gtk::Overlay *overlay_;
    Gtk::Widget *overlay_label_;

public:

    /** @brief Create a custom expander with a simple header made of a label.
     * @param useEnabled Set whether to handle an enabled/disabled toggle button and display the appropriate image
     * @param titleLabel A string to display in the header. Warning: you won't be able to switch to a widget label.
     */
    MyExpander(bool useEnabled, Glib::ustring titleLabel);

    /** Create a custom expander with a a custom - and responsive - widget
     * @param useEnabled Set whether to handle an enabled/disabled toggle button and display the appropriate image
     * @param titleWidget A widget to display in the header. Warning: you won't be able to switch to a string label.
     */
    MyExpander(bool useEnabled, Gtk::Widget* titleWidget);

    /// Initialize the class by loading the images
    static void init();
    static void cleanup();

    Glib::SignalProxy1< bool, GdkEventButton* > signal_button_release_event()
    {
        return titleEvBox->signal_button_release_event();
    };
    type_signal_enabled_toggled signal_enabled_toggled();

    /// Set the nesting level of the Expander to adapt its style accordingly
    void setLevel(int level);

    /// Set a new label string. If it has been instantiated with a Gtk::Widget, this method will do nothing
    void setLabel (Glib::ustring newLabel);
    /// Set a new label string. If it has been instantiated with a Gtk::Widget, this method will do nothing
    void setLabel (Gtk::Widget *newWidget);

    /// Get whether the enabled option is set (to true or false) or unset (i.e. undefined)
    bool get_inconsistent();
    /// Set whether the enabled option is set (to true or false) or unset (i.e. undefined)
    void set_inconsistent(bool isInconsistent);

    /// Get whether the enabled button is used or not
    bool getUseEnabled();
    /// Get whether the enabled button is on or off
    bool getEnabled();
    /// If not inconsistent, set the enabled button to true or false and emit the message if the state is different
    /// If inconsistent, set the internal value to true or false, but do not update the image and do not emit the message
    void setEnabled(bool isEnabled);
    /// Adds a Tooltip to the Enabled button, if it exist ; do nothing otherwise
    void setEnabledTooltipMarkup(Glib::ustring tooltipMarkup);
    void setEnabledTooltipText(Glib::ustring tooltipText);

    /// Get the header widget. It'll send back the Gtk::Label* if it has been instantiated with a simple text
    Gtk::Widget* getLabelWidget() const
    {
        return headerWidget ? headerWidget : label;
    }

    /// Set the collapsed/expanded state of the expander
    void set_expanded( bool expanded );

    /// Get the collapsed/expanded state of the expander
    bool get_expanded();

    /// Add a Gtk::Container for the content of the expander
    /// Warning: do not manually Show/Hide the widget, because this parameter is handled by the click on the Expander's title
    void add  (Gtk::Container& widget, bool setChild = true);

    void updateVScrollbars(bool hide);
};


/**
 * @brief subclass of Gtk::ScrolledWindow in order to handle the scrollwheel
 */
class MyScrolledWindow: public Gtk::ScrolledWindow {
public:
    bool on_scroll_event (GdkEventScroll* event) override;
    void get_preferred_width_vfunc (int& minimum_width, int& natural_width) const override;
    void get_preferred_height_vfunc (int& minimum_height, int& natural_height) const override;
    void get_preferred_height_for_width_vfunc (int width, int &minimum_height, int &natural_height) const override;

public:
    MyScrolledWindow();
};

/**
 * @brief subclass of Gtk::ScrolledWindow in order to handle the large toolbars (wider than available space)
 */
class MyScrolledToolbar: public Gtk::ScrolledWindow {

    bool on_scroll_event (GdkEventScroll* event) override;
    void get_preferred_height_vfunc (int& minimum_height, int& natural_height) const override;

public:
    MyScrolledToolbar();
};

/**
 * @brief subclass of Gtk::ComboBox in order to handle the scrollwheel
 */
class MyComboBox: public Gtk::ComboBox {
    int naturalWidth, minimumWidth;

    bool on_scroll_event (GdkEventScroll* event) override;
    void get_preferred_width_vfunc (int &minimum_width, int &natural_width) const override;
    void get_preferred_width_for_height_vfunc (int height, int &minimum_width, int &natural_width) const override;

public:
    MyComboBox ();

    void setPreferredWidth (int minimum_width, int natural_width);
};

/**
 * @brief subclass of Gtk::ComboBoxText in order to handle the scrollwheel
 */
class MyComboBoxText: public Gtk::ComboBoxText {
    int naturalWidth, minimumWidth;
    sigc::connection myConnection;

    bool on_scroll_event (GdkEventScroll* event) override;
    void get_preferred_width_vfunc (int &minimum_width, int &natural_width) const override;
    void get_preferred_width_for_height_vfunc (int height, int &minimum_width, int &natural_width) const override;

public:
    explicit MyComboBoxText (bool has_entry = false);

    void setPreferredWidth (int minimum_width, int natural_width);
    void connect(const sigc::connection &connection) { myConnection = connection; }
    void block(bool blocked) { myConnection.block(blocked); }
};

/**
 * @brief subclass of Gtk::SpinButton in order to handle the scrollwheel
 */
class MySpinButton: public Gtk::SpinButton {

protected:
    bool on_scroll_event (GdkEventScroll* event) override;
    bool on_key_press_event (GdkEventKey* event) override;

public:
    MySpinButton ();
    void updateSize();
};

/**
 * @brief subclass of Gtk::HScale in order to handle the scrollwheel
 */
class MyHScale: public Gtk::HScale {

    bool on_scroll_event (GdkEventScroll* event) override;
    bool on_key_press_event (GdkEventKey* event) override;
};

/**
 * @brief subclass of Gtk::FileChooserButton in order to handle the scrollwheel
 */
class MyFileChooserButton: public Gtk::Button {
private:
    void show_chooser();

    Glib::ustring title_;
    Gtk::FileChooserAction action_;
    Gtk::HBox box_;
    Gtk::Label lbl_;
    std::string filename_;
    std::string current_folder_;
    std::vector<Glib::RefPtr<Gtk::FileFilter>> file_filters_;
    Glib::RefPtr<Gtk::FileFilter> cur_filter_;
    std::vector<std::string> shortcut_folders_;
    bool show_hidden_;
    sigc::signal<void> selection_changed_;

protected:
    bool on_scroll_event (GdkEventScroll* event) override;
    void get_preferred_width_vfunc (int &minimum_width, int &natural_width) const override;
    void get_preferred_width_for_height_vfunc (int height, int &minimum_width, int &natural_width) const override;

    void set_none();

public:
    MyFileChooserButton(const Glib::ustring &title, Gtk::FileChooserAction action=Gtk::FILE_CHOOSER_ACTION_OPEN);

    sigc::signal<void> &signal_selection_changed();
    sigc::signal<void> &signal_file_set();

    std::string get_filename() const;
    bool set_filename(const std::string &filename);

    void add_filter(const Glib::RefPtr<Gtk::FileFilter> &filter);
    void remove_filter(const Glib::RefPtr<Gtk::FileFilter> &filter);
    void set_filter(const Glib::RefPtr<Gtk::FileFilter> &filter);
    std::vector<Glib::RefPtr<Gtk::FileFilter>> list_filters();

    bool set_current_folder(const std::string &filename);
    std::string get_current_folder() const;

    bool add_shortcut_folder(const std::string &folder);
    bool remove_shortcut_folder(const std::string &folder);

    void unselect_all();
    void unselect_filename(const std::string &filename);

    void set_show_hidden(bool yes);
};

/**
 * @brief A helper method to connect the current folder property of a file chooser to an arbitrary variable.
 */
template <class FileChooser>
void bindCurrentFolder (FileChooser& chooser, Glib::ustring& variable)
{
    chooser.signal_selection_changed ().connect ([&]()
    {
        const auto current_folder = chooser.get_current_folder ();

        if (!current_folder.empty ())
            variable = current_folder;
    });

    if (!variable.empty ())
        chooser.set_current_folder (variable);
}

typedef enum RTUpdatePolicy {
    RTUP_STATIC,
    RTUP_DYNAMIC
} eUpdatePolicy;

typedef enum RTOrientation {
    RTO_Left2Right,
    RTO_Bottom2Top,
    RTO_Right2Left,
    RTO_Top2Bottom
} eRTOrientation;

typedef enum RTNav {
    NAV_NONE,
    NAV_NEXT,
    NAV_PREVIOUS
} eRTNav;

/**
 * @brief Handle the switch between text and image to be displayed in the HBox (to be used in a button/toolpanel)
 */
class TextOrIcon: public Gtk::HBox {

public:
    TextOrIcon (const Glib::ustring &filename, const Glib::ustring &labelTx, const Glib::ustring &tooltipTx);
};

class MyImageMenuItem: public Gtk::MenuItem {
private:
    Gtk::Grid *box;
    RTImage *image;
    Gtk::Label *label;

public:
    MyImageMenuItem (Glib::ustring label, Glib::ustring imageFileName);
    const RTImage *getImage () const;
    const Gtk::Label* getLabel () const;
};

class MyProgressBar: public Gtk::ProgressBar {
private:
    int w;

    void get_preferred_width_vfunc (int &minimum_width, int &natural_width) const override;
    void get_preferred_width_for_height_vfunc (int height, int &minimum_width, int &natural_width) const override;

public:
    explicit MyProgressBar(int width);
    MyProgressBar();

    void setPreferredWidth(int width);
};


/**
 * @brief Define a gradient milestone
 */
class GradientMilestone {
public:
    double position;
    double r;
    double g;
    double b;
    double a;

    GradientMilestone(double _p = 0., double _r = 0., double _g = 0., double _b = 0., double _a = 0.)
    {
        position = _p;
        r = _r;
        g = _g;
        b = _b;
        a = _a;
    }
};

class RefCount {
private:
    int refCount;
public:
    RefCount() : refCount(1) {}
    virtual ~RefCount() {}

    void reference()
    {
        ++refCount;
    }
    void unreference()
    {
        --refCount;

        if (!refCount) {
            delete this;
        }
    }
};

/**
 * @brief Handle back buffers as automatically as possible, and suitable to be used with Glib::RefPtr
 */
class BackBuffer: public RefCount {

protected:
    int x, y, w, h;  // Rectangle where the colored bar has to be drawn
    rtengine::Coord offset;  // Offset of the source region to draw, relative to the top left corner
    Cairo::RefPtr<Cairo::ImageSurface> surface;
    bool dirty;  // mean that the Surface has to be (re)allocated

public:
    BackBuffer();
    BackBuffer(int w, int h, Cairo::Format format = Cairo::FORMAT_RGB24);

    // set the destination drawing rectangle; return true if the dimensions are different
    // Note: newW & newH must be > 0
    bool setDrawRectangle(Glib::RefPtr<Gdk::Window> window, Gdk::Rectangle &rectangle, bool updateBackBufferSize = true);
    bool setDrawRectangle(Glib::RefPtr<Gdk::Window> window, int newX, int newY, int newW, int newH, bool updateBackBufferSize = true);
    bool setDrawRectangle(Cairo::Format format, Gdk::Rectangle &rectangle, bool updateBackBufferSize = true);
    bool setDrawRectangle(Cairo::Format format, int newX, int newY, int newW, int newH, bool updateBackBufferSize = true);
    // set the destination drawing location, do not modify other parameters like size and offset. Use setDrawRectangle to set all parameters at the same time
    void setDestPosition(int x, int y);
    void setSrcOffset(int x, int y);
    void setSrcOffset(const rtengine::Coord &newOffset);
    void getSrcOffset(int &x, int &y);
    void getSrcOffset(rtengine::Coord &offset);

    void copyRGBCharData(const unsigned char *srcData, int srcX, int srcY, int srcW, int srcH, int srcRowStride, int dstX, int dstY);
    void copySurface(Glib::RefPtr<Gdk::Window> window, Gdk::Rectangle *rectangle = nullptr);
    void copySurface(BackBuffer *destBackBuffer, Gdk::Rectangle *rectangle = nullptr);
    void copySurface(Cairo::RefPtr<Cairo::ImageSurface> destSurface, Gdk::Rectangle *rectangle = nullptr);
    void copySurface(Cairo::RefPtr<Cairo::Context> crDest, Gdk::Rectangle *destRectangle = nullptr);

    void setDirty(bool isDirty)
    {
        dirty = isDirty;

        if (!dirty && !surface) {
            dirty = true;
        }
    }
    bool isDirty()
    {
        return dirty;
    }
    // you have to check if the surface is created thanks to surfaceCreated before starting to draw on it
    bool surfaceCreated()
    {
        return static_cast<bool>(surface);
    }
    Cairo::RefPtr<Cairo::ImageSurface> getSurface()
    {
        return surface;
    }
    void setSurface(Cairo::RefPtr<Cairo::ImageSurface> surf)
    {
        surface = surf;
    }
    void deleteSurface()
    {
        if (surface) {
            surface.clear();
        }

        dirty = true;
    }
    // will let you get a Cairo::Context for Cairo drawing operations
    Cairo::RefPtr<Cairo::Context> getContext()
    {
        return Cairo::Context::create(surface);
    }
    int getWidth()
    {
        return surface ? surface->get_width() : 0;    // sending back the allocated width
    }
    int getHeight()
    {
        return surface ? surface->get_height() : 0;    // sending back the allocated height
    }
};

inline void setActiveTextOrIndex (Gtk::ComboBoxText& comboBox, const Glib::ustring& text, int index)
{
    bool valueSet = false;
    if (!text.empty()) {
        comboBox.set_active_text (text);
        valueSet = true;
    }

    if (!valueSet || comboBox.get_active_row_number () < 0) {
        comboBox.set_active (index);
    }
}

inline Gtk::Window& getToplevelWindow (Gtk::Widget* widget)
{
    return *static_cast<Gtk::Window*> (widget->get_toplevel ());
}


void setTreeViewCssProvider(Gtk::TreeView *tree);


namespace HWKeyCode {
#ifdef __WIN32__
enum {
    KEY_0 = 0x30,
    KEY_1 = 0x31,
    KEY_2 = 0x32,
    KEY_3 = 0x33,
    KEY_4 = 0x34,
    KEY_5 = 0x35,
    KEY_6 = 0x36,
    KEY_7 = 0x37,
    KEY_8 = 0x38,
    KEY_9 = 0x39
};
#elif defined __APPLE__
enum {
    KEY_0 = 29,
    KEY_1 = 18,
    KEY_2 = 19,
    KEY_3 = 20,
    KEY_4 = 21,
    KEY_5 = 23,
    KEY_6 = 22,
    KEY_7 = 26,
    KEY_8 = 28,
    KEY_9 = 25
};
#else
enum {
    KEY_0 = 0x13,
    KEY_1 = 0x0a,
    KEY_2 = 0x0b,
    KEY_3 = 0x0c,
    KEY_4 = 0x0d,
    KEY_5 = 0x0e,
    KEY_6 = 0x0f,
    KEY_7 = 0x10,
    KEY_8 = 0x11,
    KEY_9 = 0x12
};
#endif

} // namespace HWKeyCode

bool getSystemDefaultMonitorProfile(GdkWindow *rootwin, Glib::ustring &defprof, Glib::ustring &defprofname);
void initGUIColorManagement();
void getGUIColor(int &r, int &g, int &b);
void getGUIColor(float &r, float &g, float &b);
void getGUIColor(double &r, double &g, double &b);
    
