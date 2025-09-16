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
#include "flatfield.h"
#include "options.h"
#include "guiutils.h"
#include <sstream>
#include "rtimage.h"
#include "../rtengine/refreshmap.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;

FlatField::FlatField () : FoldableToolPanel(this, "flatfield", M("TP_FLATFIELD_LABEL"), false, true, true)
{
    auto m = ProcEventMapper::getInstance();
    EvToolEnabled.set_action(rtengine::DARKFRAME);
    EvToolReset.set_action(rtengine::DARKFRAME);
    EvEmbedded = m->newEvent(rtengine::DARKFRAME, "HISTORY_MSG_FLATFIELD_EMBEDDED");
    
    hbff = Gtk::manage(new Gtk::HBox());
    hbff->set_spacing(2);
    flatFieldFile = Gtk::manage(new MyFileChooserButton(M("TP_FLATFIELD_LABEL"), Gtk::FILE_CHOOSER_ACTION_OPEN));
    bindCurrentFolder (*flatFieldFile, options.lastFlatfieldDir);
    ffLabel = Gtk::manage(new Gtk::Label(M("GENERAL_FILE")));
    flatFieldFileReset = Gtk::manage(new Gtk::Button());
    flatFieldFileReset->set_image (*Gtk::manage(new RTImage ("cancel-small.png")));
    hbff->pack_start(*ffLabel, Gtk::PACK_SHRINK, 0);
    hbff->pack_start(*flatFieldFile);
    hbff->pack_start(*flatFieldFileReset, Gtk::PACK_SHRINK, 0);
    flatFieldAutoSelect = Gtk::manage(new Gtk::CheckButton((M("TP_FLATFIELD_AUTOSELECT"))));
    ffInfo = Gtk::manage(new Gtk::Label(""));
    ffInfo->set_alignment(0, 0); //left align
    flatFieldBlurRadius = Gtk::manage(new Adjuster (M("TP_FLATFIELD_BLURRADIUS"), 0, 200, 2, 32));
    flatFieldBlurRadius->setAdjusterListener (this);

    if (flatFieldBlurRadius->delay < options.adjusterMaxDelay) {
        flatFieldBlurRadius->delay = options.adjusterMaxDelay;
    }

    flatFieldBlurRadius->show();

    Gtk::HBox* hbffbt = Gtk::manage (new Gtk::HBox ());
    hbffbt->pack_start (*Gtk::manage (new Gtk::Label ( M("TP_FLATFIELD_BLURTYPE") + ": ")), Gtk::PACK_SHRINK, 0);
    //hbffbt->set_spacing(4);
    flatFieldBlurType = Gtk::manage (new MyComboBoxText ());
    flatFieldBlurType->append(M("TP_FLATFIELD_BT_AREA"));
    flatFieldBlurType->append(M("TP_FLATFIELD_BT_VERTICAL"));
    flatFieldBlurType->append(M("TP_FLATFIELD_BT_HORIZONTAL"));
    flatFieldBlurType->append(M("TP_FLATFIELD_BT_VERTHORIZ"));
    flatFieldBlurType->set_active(0);
    hbffbt->pack_start(*flatFieldBlurType, Gtk::PACK_EXPAND_WIDGET, 0);

    flatFieldClipControl = Gtk::manage (new Adjuster(M("TP_FLATFIELD_CLIPCONTROL"), 0., 100., 1., 0.));
    flatFieldClipControl->setAdjusterListener(this);
    flatFieldClipControl->addAutoButton("");

    if (flatFieldClipControl->delay < options.adjusterMaxDelay) {
        flatFieldClipControl->delay = options.adjusterMaxDelay;
    }

    flatFieldClipControl->show();
    flatFieldClipControl->set_tooltip_markup (M("TP_FLATFIELD_CLIPCONTROL_TOOLTIP"));

    embedded = Gtk::manage(new Gtk::CheckButton(M("TP_FLATFIELD_EMBEDDED")));

    pack_start( *hbff, Gtk::PACK_SHRINK, 0);
    Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
    hb->pack_start( *flatFieldAutoSelect, Gtk::PACK_SHRINK, 0);
    hb->pack_start(*embedded, Gtk::PACK_SHRINK, 4);
    pack_start(*hb, Gtk::PACK_EXPAND_WIDGET, 0);

    vbff = Gtk::manage(new Gtk::VBox());
    pack_start( *ffInfo, Gtk::PACK_SHRINK, 0);
    vbff->pack_start( *hbffbt, Gtk::PACK_SHRINK, 2);
    vbff->pack_start( *flatFieldBlurRadius, Gtk::PACK_SHRINK, 0);
    //vbff->pack_start( *flatFieldClipControl, Gtk::PACK_SHRINK, 0);
    pack_start(*vbff);
    pack_start( *flatFieldClipControl, Gtk::PACK_SHRINK, 0);

    flatFieldFileconn = flatFieldFile->signal_file_set().connect ( sigc::mem_fun(*this, &FlatField::flatFieldFileChanged)); //, true);
    flatFieldFileReset->signal_clicked().connect( sigc::mem_fun(*this, &FlatField::flatFieldFile_Reset), true );
    flatFieldAutoSelectconn = flatFieldAutoSelect->signal_toggled().connect ( sigc::mem_fun(*this, &FlatField::flatFieldAutoSelectChanged), true);
    flatFieldBlurTypeconn = flatFieldBlurType->signal_changed().connect( sigc::mem_fun(*this, &FlatField::flatFieldBlurTypeChanged) );
    lastShortcutPath = "";

    embedded->signal_toggled().connect(sigc::mem_fun(*this, &FlatField::embeddedToggled), true);

    // Set filename filters
    b_filter_asCurrent = false;
    Glib::RefPtr<Gtk::FileFilter> filter_any = Gtk::FileFilter::create();
    filter_any->add_pattern("*");
    filter_any->set_name(M("FILECHOOSER_FILTER_ANY"));
    flatFieldFile->add_filter (filter_any);

    // filters for all supported non-raw extensions
    for (size_t i = 0; i < options.parseExtensions.size(); i++) {
        if (options.parseExtensionsEnabled[i] && options.parseExtensions[i].uppercase() != "JPG" && options.parseExtensions[i].uppercase() != "JPEG" && options.parseExtensions[i].uppercase() != "PNG" && options.parseExtensions[i].uppercase() != "TIF" && options.parseExtensions[i].uppercase() != "TIFF"  ) {
            Glib::RefPtr<Gtk::FileFilter> filter_ff = Gtk::FileFilter::create();
            filter_ff->add_pattern("*." + options.parseExtensions[i]);
            filter_ff->add_pattern("*." + options.parseExtensions[i].uppercase());
            filter_ff->set_name(options.parseExtensions[i].uppercase());
            flatFieldFile->add_filter (filter_ff);
            //printf("adding filter %s \n",options.parseExtensions[i].uppercase().c_str());
        }
    }
}

FlatField::~FlatField ()
{
    idle_register.destroy();
}

void FlatField::read(const rtengine::procparams::ProcParams* pp)
{
    disableListener ();
    setEnabled(pp->raw.enable_flatfield);
    
    flatFieldAutoSelectconn.block (true);
    flatFieldBlurTypeconn.block (true);

    //flatFieldBlurType
    for (size_t i = 0; i < procparams::RAWParams::getFlatFieldBlurTypeStrings().size(); ++i) {
        if (pp->raw.ff_BlurType == procparams::RAWParams::getFlatFieldBlurTypeStrings()[i]) {
            flatFieldBlurType->set_active(i);
            break;
        }
    }

    if (pp->raw.ff_BlurType == procparams::RAWParams::getFlatFieldBlurTypeString(procparams::RAWParams::FlatFieldBlurType::AREA)) {
        flatFieldClipControl->show();
    } else {
        flatFieldClipControl->hide();
    }

    flatFieldAutoSelect->set_active (pp->raw.ff_AutoSelect);
    flatFieldBlurRadius->setValue (pp->raw.ff_BlurRadius);
    flatFieldClipControl->setValue (pp->raw.ff_clipControl);
    flatFieldClipControl->setAutoValue (pp->raw.ff_AutoClipControl);

    if (Glib::file_test (pp->raw.ff_file, Glib::FILE_TEST_EXISTS)) {
        flatFieldFile->set_filename (pp->raw.ff_file);
    } else {
        flatFieldFile_Reset();
    }

    hbff->set_sensitive( !pp->raw.ff_AutoSelect );

    lastFFAutoSelect = pp->raw.ff_AutoSelect;
    lastFFAutoClipCtrl = pp->raw.ff_AutoClipControl;

    if( pp->raw.ff_AutoSelect  && ffp) {
        // retrieve the auto-selected ff filename
        rtengine::RawImage *img = ffp->getFF();

        if( img ) {
            ffInfo->set_text( Glib::ustring::compose("%1: f/%2", Glib::path_get_basename(img->get_filename()), img->get_aperture()) ); // !!! need to add focallength in mm and format aperture to ##.#
        } else {
            ffInfo->set_text(Glib::ustring(M("TP_PREPROCESS_NO_FOUND")));
        }
    } else {
        ffInfo->set_text("");
    }

    ffChanged = false;

    flatFieldAutoSelectconn.block (false);
    flatFieldBlurTypeconn.block (false);
    enableListener ();

    // Add filter with the current file extension if the current file is raw
    if (ffp) {

        if (b_filter_asCurrent) {
            //First, remove last filter_asCurrent if it was set for a raw file
            std::vector< Glib::RefPtr<Gtk::FileFilter> > filters = flatFieldFile->list_filters();
            flatFieldFile->remove_filter(*(filters.end() - 1));
            b_filter_asCurrent = false;
        }

        Glib::ustring fname = Glib::path_get_basename(ffp->GetCurrentImageFilePath());
        Glib::ustring filetype;

        if (fname != "") {
            // get image filetype, set filter to the same as current image's filetype
            std::string::size_type idx;
            idx = fname.rfind('.');

            if(idx != std::string::npos) {
                filetype = fname.substr(idx + 1);
                //exclude non-raw
                israw = filetype.uppercase() != "JPG" && filetype.uppercase() != "JPEG" && filetype.uppercase() != "PNG" && filetype.uppercase() != "TIF" && filetype.uppercase() != "TIFF";

                if (israw) {
                    b_filter_asCurrent = true; //prevent re-adding this filter on every pp3 file read
                    Glib::RefPtr<Gtk::FileFilter> filter_asCurrent = Gtk::FileFilter::create();
                    filter_asCurrent->add_pattern("*." + filetype);
                    filter_asCurrent->set_name(M("FILECHOOSER_FILTER_SAME") + " (" + filetype + ")");
                    flatFieldFile->add_filter (filter_asCurrent);
                    flatFieldFile->set_filter (filter_asCurrent);
                }
            }
        }
    }

    disableListener();
    embedded->set_active(pp->raw.ff_embedded);
    embedded->set_sensitive(true);
    if (ffp && !ffp->hasEmbeddedFF()) {
        embedded->set_sensitive(false);
        embedded->set_active(false);
    }
    embeddedToggled();
    enableListener();
}


void FlatField::write( rtengine::procparams::ProcParams* pp)
{
    pp->raw.enable_flatfield = getEnabled();
    pp->raw.ff_file = flatFieldFile->get_filename();
    if (!Glib::file_test(pp->raw.ff_file, Glib::FILE_TEST_EXISTS)) {
        pp->raw.ff_file = "";
    }    
    pp->raw.ff_AutoSelect = flatFieldAutoSelect->get_active();
    pp->raw.ff_BlurRadius = flatFieldBlurRadius->getIntValue();
    pp->raw.ff_clipControl = flatFieldClipControl->getIntValue();
    pp->raw.ff_AutoClipControl = flatFieldClipControl->getAutoValue();

    int currentRow = flatFieldBlurType->get_active_row_number();

    if( currentRow >= 0 && flatFieldBlurType->get_active_text() != M("GENERAL_UNCHANGED")) {
        pp->raw.ff_BlurType = procparams::RAWParams::getFlatFieldBlurTypeStrings()[currentRow];
    }

    pp->raw.ff_embedded = embedded->get_active();
}

void FlatField::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        const Glib::ustring value = a->getTextValue();

        if (a == flatFieldBlurRadius) {
            listener->panelChanged (EvFlatFieldBlurRadius,  value);
        } else if (a == flatFieldClipControl) {
            listener->panelChanged (EvFlatFieldClipControl,  value);
        }
    }
}

void FlatField::adjusterAutoToggled (Adjuster* a, bool newval)
{
    if (listener && getEnabled()) {
        if(a == flatFieldClipControl) {
            if (flatFieldClipControl->getAutoInconsistent()) {
                listener->panelChanged (EvFlatFieldAutoClipControl, M("GENERAL_UNCHANGED"));
            } else if (flatFieldClipControl->getAutoValue()) {
                listener->panelChanged (EvFlatFieldAutoClipControl, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged (EvFlatFieldAutoClipControl, M("GENERAL_DISABLED"));
            }
        }
    }
}


void FlatField::trimValues (rtengine::procparams::ProcParams* pp)
{
    flatFieldClipControl->trimValue(pp->raw.ff_clipControl);
}

void FlatField::setDefaults(const rtengine::procparams::ProcParams* defParams)
{
    flatFieldBlurRadius->setDefault( defParams->raw.ff_BlurRadius);
    flatFieldClipControl->setDefault( defParams->raw.ff_clipControl);

    initial_params = defParams->raw;
}

void FlatField::flatFieldFileChanged()
{
    ffChanged = true;

    if (listener && getEnabled()) {
        listener->panelChanged (EvFlatFieldFile, Glib::path_get_basename(flatFieldFile->get_filename()));
    }
}

void FlatField::flatFieldFile_Reset()
{
    ffChanged = true;

// caution: I had to make this hack, because set_current_folder() doesn't work correctly!
//          Because szeva doesn't exist since he was committed to happy hunting ground in Issue 316
//          we can use him now for this hack
    flatFieldFile->set_filename (options.lastFlatfieldDir + "/szeva");
// end of the hack

    if (!options.lastFlatfieldDir.empty()) {
        flatFieldFile->set_current_folder(options.lastFlatfieldDir);
    }

    ffInfo->set_text("");

    if (listener && getEnabled()) {
        listener->panelChanged (EvFlatFieldFile, M("GENERAL_NONE") );
    }
}

void FlatField::flatFieldBlurTypeChanged ()
{
    const int curSelection = flatFieldBlurType->get_active_row_number();
    const RAWParams::FlatFieldBlurType blur_type = RAWParams::FlatFieldBlurType(curSelection);

    if (blur_type == procparams::RAWParams::FlatFieldBlurType::AREA) {
        flatFieldClipControl->show();
    } else {
        flatFieldClipControl->hide();
    }

    if (listener && curSelection >= 0 && getEnabled()) {
        listener->panelChanged (EvFlatFieldBlurType, flatFieldBlurType->get_active_text());
    }
}

void FlatField::flatFieldAutoSelectChanged()
{
    hbff->set_sensitive( !flatFieldAutoSelect->get_active() );

    if( flatFieldAutoSelect->get_active()  && ffp) {
        // retrieve the auto-selected ff filename
        rtengine::RawImage *img = ffp->getFF();

        if( img ) {
            ffInfo->set_text( Glib::ustring::compose("%1: f/%2", Glib::path_get_basename(img->get_filename()), img->get_aperture()) ); // !!! need to add focallength in mm and format aperture to ##.#
        } else {
            ffInfo->set_text(Glib::ustring(M("TP_PREPROCESS_NO_FOUND")));
        }
    } else {
        ffInfo->set_text("");
    }

    if (listener && getEnabled()) {
        listener->panelChanged (EvFlatFieldAutoSelect, flatFieldAutoSelect->get_active() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }

}

void FlatField::setShortcutPath(const Glib::ustring& path)
{
    if (path.empty ()) {
        return;
    }

    try {

        if (!lastShortcutPath.empty ()) {
            flatFieldFile->remove_shortcut_folder (lastShortcutPath);
        }

        flatFieldFile->add_shortcut_folder (path);

        lastShortcutPath = path;

    } catch (Glib::Error&) {}
}

void FlatField::flatFieldAutoClipValueChanged(int n)
{
    idle_register.add(
        [this, n]() -> bool
        {
            disableListener();
            flatFieldClipControl->setValue(n);
            enableListener();
            return false;
        }
    );
}


void FlatField::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.raw = initial_params;
    }
    pp.raw.enable_flatfield = getEnabled();
    read(&pp);
}


void FlatField::embeddedToggled()
{
    bool active = embedded->get_active();
    flatFieldAutoSelect->set_sensitive(!active);
    hbff->set_sensitive(!active);
    vbff->set_sensitive(!active);
    bool ena = disableListener();
    flatFieldBlurType->set_active(0);
    if (active) {
        flatFieldClipControl->delAutoButton();
    } else {
        flatFieldClipControl->addAutoButton("");
    }
    if (ena) {
        enableListener();
    }

    if (listener && getEnabled()) {
        listener->panelChanged(EvEmbedded, active ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}
