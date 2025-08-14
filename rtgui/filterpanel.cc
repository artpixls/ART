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
#include "filterpanel.h"
#include "multilangmgr.h"
#include "../rtengine/rtengine.h"
#include "rtimage.h"

using namespace rtengine;

FilterPanel::FilterPanel () : listener (nullptr)
{
    enabled = Gtk::manage (new Gtk::CheckButton (M("EXIFFILTER_METADATAFILTER")));
    pack_start (*enabled, Gtk::PACK_SHRINK, 2);
    pack_start (*Gtk::manage(new Gtk::HSeparator ()), Gtk::PACK_SHRINK, 2);

    enaFiletype = Gtk::manage(new Gtk::CheckButton(M("EXIFFILTER_FILETYPE") + ":"));
    Gtk::VBox* ftvb = Gtk::manage(new Gtk::VBox ());
    ftvb->pack_start (*enaFiletype, Gtk::PACK_SHRINK, 0);
    filetype = Gtk::manage(new Gtk::ListViewText (1, false, Gtk::SELECTION_MULTIPLE));
    filetype->set_headers_visible (false);
    Gtk::ScrolledWindow* sfiletype = Gtk::manage(new Gtk::ScrolledWindow());
    sfiletype->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_ALWAYS);
    sfiletype->set_size_request(-1, 80);
    sfiletype->add(*filetype);
    ftvb->pack_start (*sfiletype, Gtk::PACK_EXPAND_WIDGET, 0);
    pack_start (*ftvb, Gtk::PACK_EXPAND_WIDGET, 4);

    
    enaFNumber = Gtk::manage (new Gtk::CheckButton (M("EXIFFILTER_APERTURE") + ":"));
    Gtk::VBox* fnvb = Gtk::manage(new Gtk::VBox ());
    Gtk::HBox* fnhb = Gtk::manage(new Gtk::HBox ());
    fnvb->pack_start (*enaFNumber, Gtk::PACK_SHRINK, 0);
    fnumberFrom = Gtk::manage(new Gtk::Entry ());
    fnumberFrom->set_width_chars(1);
    fnumberTo = Gtk::manage(new Gtk::Entry ());
    fnumberTo->set_width_chars(1);
    fnhb->pack_start (*fnumberFrom, true, true, 2);
    fnhb->pack_start (*Gtk::manage(new Gtk::Label(" - ")), false, false, 4);
    fnhb->pack_start (*fnumberTo, true, true, 2);
    fnvb->pack_start (*fnhb, Gtk::PACK_SHRINK, 0);
    pack_start (*fnvb, Gtk::PACK_SHRINK, 4);

    enaShutter = Gtk::manage(new Gtk::CheckButton(M("EXIFFILTER_SHUTTER") + ":"));
    Gtk::VBox* svb = Gtk::manage(new Gtk::VBox ());
    Gtk::HBox* shb = Gtk::manage(new Gtk::HBox ());
    svb->pack_start (*enaShutter, Gtk::PACK_SHRINK, 0);
    shutterFrom = Gtk::manage(new Gtk::Entry ());
    shutterFrom->set_width_chars(1);
    shutterTo = Gtk::manage(new Gtk::Entry ());
    shutterTo->set_width_chars(1);
    shb->pack_start (*shutterFrom, true, true, 2);
    shb->pack_start (*Gtk::manage(new Gtk::Label(" - ")), false, false, 4);
    shb->pack_start (*shutterTo, true, true, 2);
    svb->pack_start (*shb, Gtk::PACK_SHRINK, 0);
    pack_start (*svb, Gtk::PACK_SHRINK, 4);

    enaISO = Gtk::manage(new Gtk::CheckButton(M("EXIFFILTER_ISO") + ":"));
    Gtk::VBox* ivb = Gtk::manage(new Gtk::VBox ());
    Gtk::HBox* ihb = Gtk::manage(new Gtk::HBox ());
    ivb->pack_start (*enaISO, Gtk::PACK_SHRINK, 0);
    isoFrom = Gtk::manage(new Gtk::Entry ());
    isoFrom->set_width_chars(1);
    isoTo = Gtk::manage(new Gtk::Entry ());
    isoTo->set_width_chars(1);
    ihb->pack_start (*isoFrom, true, true, 2);
    ihb->pack_start (*Gtk::manage(new Gtk::Label(" - ")), false, false, 4);
    ihb->pack_start (*isoTo, true, true, 2);
    ivb->pack_start (*ihb, Gtk::PACK_SHRINK, 0);
    pack_start (*ivb, Gtk::PACK_SHRINK, 4);

    enaFocalLen = Gtk::manage(new Gtk::CheckButton(M("EXIFFILTER_FOCALLEN") + ":"));
    Gtk::VBox* fvb = Gtk::manage(new Gtk::VBox ());
    Gtk::HBox* fhb = Gtk::manage(new Gtk::HBox ());
    fvb->pack_start (*enaFocalLen, Gtk::PACK_SHRINK, 0);
    focalFrom = Gtk::manage(new Gtk::Entry ());
    focalFrom->set_width_chars(1);
    focalTo = Gtk::manage(new Gtk::Entry ());
    focalTo->set_width_chars(1);
    fhb->pack_start (*focalFrom, true, true, 2);
    fhb->pack_start (*Gtk::manage(new Gtk::Label(" - ")), false, false, 4);
    fhb->pack_start (*focalTo, true, true, 2);
    fvb->pack_start (*fhb, Gtk::PACK_SHRINK, 0);
    pack_start (*fvb, Gtk::PACK_SHRINK, 4);

    enaDate = Gtk::manage(new Gtk::CheckButton(M("EXIFFILTER_DATE") + ":"));
    {
        Gtk::VBox* fvb = Gtk::manage(new Gtk::VBox ());
        Gtk::HBox* fhb = Gtk::manage(new Gtk::HBox ());
        fvb->pack_start(*enaDate, Gtk::PACK_SHRINK, 0);
        dateFrom = Gtk::manage(new DateEntry());
        dateTo = Gtk::manage(new DateEntry());
        fhb->pack_start(*dateFrom, true, true, 2);
        fhb->pack_start(*Gtk::manage(new Gtk::Label(" - ")), false, false, 4);
        fhb->pack_start(*dateTo, true, true, 2);
        fvb->pack_start(*fhb, Gtk::PACK_SHRINK, 0);
        pack_start(*fvb, Gtk::PACK_SHRINK, 4);
    }
    
    enaExpComp = Gtk::manage(new Gtk::CheckButton(M("EXIFFILTER_EXPOSURECOMPENSATION") + ":"));
    Gtk::VBox* evb = Gtk::manage(new Gtk::VBox ());
    evb->pack_start (*enaExpComp, Gtk::PACK_SHRINK, 0);
    expcomp = Gtk::manage(new Gtk::ListViewText (1, false, Gtk::SELECTION_MULTIPLE));
    expcomp->set_headers_visible (false);
    Gtk::ScrolledWindow* sexpcomp = Gtk::manage(new Gtk::ScrolledWindow());
    sexpcomp->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_ALWAYS);
    sexpcomp->set_size_request(-1, 80);
    sexpcomp->add(*expcomp);
    evb->pack_start (*sexpcomp, Gtk::PACK_SHRINK, 0);
    pack_start (*evb, Gtk::PACK_SHRINK, 4);

    enaCamera = Gtk::manage(new Gtk::CheckButton(M("EXIFFILTER_CAMERA") + ":"));
    Gtk::VBox* cvb = Gtk::manage(new Gtk::VBox ());
    cvb->pack_start (*enaCamera, Gtk::PACK_SHRINK, 0);
    camera = Gtk::manage(new Gtk::ListViewText (1, false, Gtk::SELECTION_MULTIPLE));
    camera->set_headers_visible (false);
    Gtk::ScrolledWindow* scamera = Gtk::manage(new Gtk::ScrolledWindow());
    scamera->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_ALWAYS);
    scamera->set_size_request(-1, 80);
    scamera->add(*camera);
    cvb->pack_start (*scamera, Gtk::PACK_EXPAND_WIDGET, 0);
    pack_start (*cvb, Gtk::PACK_EXPAND_WIDGET, 4);

    enaLens = Gtk::manage(new Gtk::CheckButton(M("EXIFFILTER_LENS") + ":"));
    Gtk::VBox* lvb = Gtk::manage(new Gtk::VBox ());
    lvb->pack_start (*enaLens, Gtk::PACK_SHRINK, 0);
    lens = Gtk::manage(new Gtk::ListViewText (1, false, Gtk::SELECTION_MULTIPLE));
    lens->set_headers_visible (false);
    Gtk::ScrolledWindow* slens = Gtk::manage(new Gtk::ScrolledWindow());
    slens->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_ALWAYS);
    slens->set_size_request(-1, 80);
    slens->add(*lens);
    lvb->pack_start (*slens, Gtk::PACK_EXPAND_WIDGET, 0);
    pack_start (*lvb, Gtk::PACK_EXPAND_WIDGET, 4);

    enaOrientation = Gtk::manage(new Gtk::CheckButton(M("EXIFFILTER_ORIENTATION") + ":"));
    Gtk::VBox* ovb = Gtk::manage(new Gtk::VBox ());
    ovb->pack_start (*enaOrientation, Gtk::PACK_SHRINK, 0);
    orientation = Gtk::manage(new Gtk::ListViewText (1, false, Gtk::SELECTION_MULTIPLE));
    orientation->set_headers_visible (false);
    Gtk::ScrolledWindow* sorientation = Gtk::manage(new Gtk::ScrolledWindow());
    sorientation->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_ALWAYS);
    sorientation->set_size_request(-1, 80);
    sorientation->add(*orientation);
    ovb->pack_start (*sorientation, Gtk::PACK_EXPAND_WIDGET, 0);
    pack_start (*ovb, Gtk::PACK_EXPAND_WIDGET, 4);
    
    // add panel ending
    Gtk::VBox* vboxpe = Gtk::manage (new Gtk::VBox ());
    Gtk::HSeparator* hseptpe = Gtk::manage (new Gtk::HSeparator ());
    Gtk::Image* peImg = Gtk::manage (new RTImage("ornament1.png"));
    vboxpe->pack_start(*hseptpe, Gtk::PACK_SHRINK, 4);
    vboxpe->pack_start(*peImg);
    pack_start(*vboxpe, Gtk::PACK_SHRINK, 0);

    sChange.push_back(fnumberFrom->signal_changed().connect (sigc::mem_fun(*this, &FilterPanel::valueChanged)));
    sChange.push_back(fnumberTo->signal_changed().connect (sigc::mem_fun(*this, &FilterPanel::valueChanged)));
    sChange.push_back(shutterFrom->signal_changed().connect (sigc::mem_fun(*this, &FilterPanel::valueChanged)));
    sChange.push_back(shutterTo->signal_changed().connect (sigc::mem_fun(*this, &FilterPanel::valueChanged)));
    sChange.push_back(isoFrom->signal_changed().connect (sigc::mem_fun(*this, &FilterPanel::valueChanged)));
    sChange.push_back(isoTo->signal_changed().connect (sigc::mem_fun(*this, &FilterPanel::valueChanged)));
    sChange.push_back(focalFrom->signal_changed().connect (sigc::mem_fun(*this, &FilterPanel::valueChanged)));
    sChange.push_back(focalTo->signal_changed().connect (sigc::mem_fun(*this, &FilterPanel::valueChanged)));
    sChange.push_back(dateFrom->signal_date_changed().connect(sigc::mem_fun(*this, &FilterPanel::valueChanged)));
    sChange.push_back(dateTo->signal_date_changed().connect(sigc::mem_fun(*this, &FilterPanel::valueChanged)));
    sChange.push_back(expcomp->get_selection()->signal_changed().connect(sigc::mem_fun(*this, &FilterPanel::valueChanged)));
    sChange.push_back(filetype->get_selection()->signal_changed().connect(sigc::mem_fun(*this, &FilterPanel::valueChanged)));
    sChange.push_back(camera->get_selection()->signal_changed().connect(sigc::mem_fun(*this, &FilterPanel::valueChanged)));
    sChange.push_back(lens->get_selection()->signal_changed().connect(sigc::mem_fun(*this, &FilterPanel::valueChanged)));
    sChange.push_back(orientation->get_selection()->signal_changed().connect(sigc::mem_fun(*this, &FilterPanel::valueChanged)));
    sChange.push_back(enaFNumber->signal_toggled().connect( sigc::mem_fun(*this, &FilterPanel::valueChanged) ));
    sChange.push_back(enaShutter->signal_toggled().connect( sigc::mem_fun(*this, &FilterPanel::valueChanged) ));
    sChange.push_back(enaFocalLen->signal_toggled().connect( sigc::mem_fun(*this, &FilterPanel::valueChanged) ));
    sChange.push_back(enaISO->signal_toggled().connect( sigc::mem_fun(*this, &FilterPanel::valueChanged) ));
    sChange.push_back(enaExpComp->signal_toggled().connect( sigc::mem_fun(*this, &FilterPanel::valueChanged) ));
    sChange.push_back(enaCamera->signal_toggled().connect( sigc::mem_fun(*this, &FilterPanel::valueChanged) ));
    sChange.push_back(enaLens->signal_toggled().connect( sigc::mem_fun(*this, &FilterPanel::valueChanged) ));
    sChange.push_back(enaOrientation->signal_toggled().connect( sigc::mem_fun(*this, &FilterPanel::valueChanged) ));
    sChange.push_back(enabled->signal_toggled().connect( sigc::mem_fun(*this, &FilterPanel::valueChanged) ));
    sChange.push_back(enaFiletype->signal_toggled().connect( sigc::mem_fun(*this, &FilterPanel::valueChanged) ));

    show_all ();
}


void FilterPanel::setFilter(ExifFilterSettings& defefs, bool update)
{
    for (size_t i = 0; i < sChange.size(); i++) {
        sChange[i].block(true);
    }

    if (!!update) {
        enabled->set_active(defefs.enabled);
    }

    if (!!update) {
        enaFNumber->set_active(defefs.filterFNumber);
    }
    if (!update || defefs.filterFNumber) {
        fnumberFrom->set_text (FramesMetaData::apertureToString (defefs.fnumberFrom));
        curefs.fnumberFrom = defefs.fnumberFrom;
        fnumberTo->set_text (FramesMetaData::apertureToString (defefs.fnumberTo));
        curefs.fnumberTo = defefs.fnumberTo;
    }

    if (!!update) {
        enaShutter->set_active(defefs.filterShutter);
    }
    if (!update || defefs.filterShutter) {
        shutterFrom->set_text (FramesMetaData::shutterToString (defefs.shutterFrom));
        curefs.shutterFrom = defefs.shutterFrom;
        shutterTo->set_text (FramesMetaData::shutterToString (defefs.shutterTo));
        curefs.shutterTo = defefs.shutterTo;
    }

    if (!!update) {
        enaISO->set_active(defefs.filterISO);
    }
    if (!update || defefs.filterISO) {
        isoFrom->set_text (Glib::ustring::format (defefs.isoFrom));
        curefs.isoFrom = defefs.isoFrom;
        isoTo->set_text (Glib::ustring::format (defefs.isoTo));
        curefs.isoTo = defefs.isoTo;
    }

    if (!!update) {
        enaFocalLen->set_active(defefs.filterFocalLen);
    }
    if (!update || defefs.filterFocalLen) {
        focalFrom->set_text (Glib::ustring::format (defefs.focalFrom));
        curefs.focalFrom = defefs.focalFrom;
        focalTo->set_text (Glib::ustring::format (defefs.focalTo));
        curefs.focalTo = defefs.focalTo;
    }

    if (!!update) {
        enaDate->set_active(defefs.filterDate);
    }
    if (!update || defefs.filterDate) {
        dateFrom->set_date(defefs.dateFrom);
        dateTo->set_date(defefs.dateTo);
        curefs.dateFrom = defefs.dateFrom;
        curefs.dateTo = defefs.dateTo;
    }

    if (!!update) {
        enaExpComp->set_active(defefs.filterExpComp);
    }
    Glib::RefPtr<Gtk::TreeSelection> eselection = expcomp->get_selection ();

    if (!!update) {
        enaFiletype->set_active(defefs.filterFiletype);
    }
    Glib::RefPtr<Gtk::TreeSelection> ftselection = filetype->get_selection ();

    if (!!update) {
        enaCamera->set_active(defefs.filterCamera);
    }
    Glib::RefPtr<Gtk::TreeSelection> cselection = camera->get_selection ();

    if (!!update) {
        enaLens->set_active(defefs.filterLens);
    }
    Glib::RefPtr<Gtk::TreeSelection> lselection = lens->get_selection ();

    if (!!update) {
        enaOrientation->set_active(defefs.filterOrientation);
    }
    Glib::RefPtr<Gtk::TreeSelection> oselection = orientation->get_selection ();

     if (!update) {
        expcomp->clear_items();
        curefs.expcomp.clear();

        for (std::set<std::string>::iterator i = defefs.expcomp.begin(); i != defefs.expcomp.end(); ++i) {
            expcomp->append (*i);
            curefs.expcomp.insert(*i);
        }

        eselection->select_all();

        lens->clear_items();
        curefs.lenses.clear();

        for (std::set<std::string>::iterator i = defefs.lenses.begin(); i != defefs.lenses.end(); ++i) {
            lens->append (*i);
            curefs.lenses.insert(*i);
        }

        oselection->select_all();

        orientation->clear_items();
        curefs.orientations.clear();

        for (std::set<std::string>::iterator i = defefs.orientations.begin(); i != defefs.orientations.end(); ++i) {
            orientation->append (*i);
            curefs.orientations.insert(*i);
        }

        oselection->select_all();
        
        camera->clear_items();
        curefs.cameras.clear();

        for (std::set<std::string>::iterator i = defefs.cameras.begin(); i != defefs.cameras.end(); ++i) {
            camera->append(*i);
            curefs.cameras.insert(*i);
        }

        cselection->select_all();

        filetype->clear_items();
        curefs.filetypes.clear();

        for (std::set<std::string>::iterator i = defefs.filetypes.begin(); i != defefs.filetypes.end(); ++i) {
            filetype->append(*i);
            curefs.filetypes.insert(*i);
        }

        ftselection->select_all();
    } else {
        if (defefs.filterExpComp) {
            for( Gtk::TreeModel::Children::iterator iter = expcomp->get_model()->children().begin(); iter != expcomp->get_model()->children().end(); ++iter) {
                Glib::ustring v;
                iter->get_value(0, v);

                if( defefs.expcomp.find( v ) != defefs.expcomp.end() ) {
                    eselection->select( iter );
                } else {
                    eselection->unselect( iter );
                }
            }
        }

        if (defefs.filterLens) {
            for( Gtk::TreeModel::Children::iterator iter = lens->get_model()->children().begin(); iter != lens->get_model()->children().end(); ++iter) {
                Glib::ustring v;
                iter->get_value(0, v);

                if( defefs.lenses.find( v ) != defefs.lenses.end() ) {
                    lselection->select( iter );
                } else {
                    lselection->unselect( iter );
                }
            }
        }

        if (defefs.filterOrientation) {
            for( Gtk::TreeModel::Children::iterator iter = orientation->get_model()->children().begin(); iter != orientation->get_model()->children().end(); ++iter) {
                Glib::ustring v;
                iter->get_value(0, v);

                if( defefs.orientations.find( v ) != defefs.orientations.end() ) {
                    oselection->select( iter );
                } else {
                    oselection->unselect( iter );
                }
            }
        }
        
        if (defefs.filterCamera) {
            for( Gtk::TreeModel::Children::iterator iter = camera->get_model()->children().begin(); iter != camera->get_model()->children().end(); ++iter) {
                Glib::ustring v;
                iter->get_value(0, v);

                if( defefs.cameras.find( v ) != defefs.cameras.end() ) {
                    cselection->select(iter);
                } else {
                    cselection->unselect(iter);
                }
            }
        }

        if (defefs.filterFiletype) {
            for( Gtk::TreeModel::Children::iterator iter = filetype->get_model()->children().begin(); iter != filetype->get_model()->children().end(); ++iter) {
                Glib::ustring v;
                iter->get_value(0, v);

                if( defefs.filetypes.find( v ) != defefs.filetypes.end() ) {
                    ftselection->select(iter);
                } else {
                    ftselection->unselect(iter);
                }
            }
        }
    }

    curefs = defefs;

    for (size_t i = 0; i < sChange.size(); i++) {
        sChange[i].block (false);
    }
}


bool FilterPanel::isEnabled ()
{

    return enabled->get_active () && is_sensitive();
}


ExifFilterSettings FilterPanel::getFilter(bool full_data)
{
    ExifFilterSettings efs;

    efs.enabled = enabled->get_active();
    
    efs.filterFNumber  = enaFNumber->get_active ();
    efs.filterShutter  = enaShutter->get_active ();
    efs.filterFocalLen = enaFocalLen->get_active ();
    efs.filterISO      = enaISO->get_active ();
    efs.filterExpComp  = enaExpComp->get_active ();
    efs.filterCamera   = enaCamera->get_active ();
    efs.filterLens     = enaLens->get_active ();
    efs.filterOrientation     = enaOrientation->get_active ();
    efs.filterFiletype = enaFiletype->get_active ();
    efs.filterDate = enaDate->get_active();

    if (efs.filterFNumber || full_data) {
        efs.fnumberFrom = atof(fnumberFrom->get_text().c_str());
        efs.fnumberTo = atof (fnumberTo->get_text().c_str());
    }
    if (efs.filterFocalLen || full_data) {
        efs.focalFrom = atof (focalFrom->get_text().c_str());
        efs.focalTo = atof (focalTo->get_text().c_str());
    }
    if (efs.filterISO || full_data) {
        efs.isoFrom = atoi (isoFrom->get_text().c_str());
        efs.isoTo = atoi (isoTo->get_text().c_str());
    }
    if (efs.filterShutter || full_data) {
        efs.shutterFrom = FramesMetaData::shutterFromString (shutterFrom->get_text());
        efs.shutterTo = FramesMetaData::shutterFromString (shutterTo->get_text());
    }
    if (efs.filterDate || full_data) {
        efs.dateFrom = dateFrom->get_date();
        efs.dateTo = dateTo->get_date();
    }
    
    std::vector<int> sel = camera->get_selected ();

    if (efs.filterCamera || full_data) {
        for (size_t i = 0; i < sel.size(); i++) {
            efs.cameras.insert (camera->get_text (sel[i]));
        }
    }

    sel = expcomp->get_selected ();

    if (efs.filterExpComp || full_data) {
        for (size_t i = 0; i < sel.size(); i++) {
            efs.expcomp.insert (expcomp->get_text (sel[i]));
        }
    }

    sel = lens->get_selected ();

    if (efs.filterLens || full_data) {
        for (size_t i = 0; i < sel.size(); i++) {
            efs.lenses.insert (lens->get_text (sel[i]));
        }
    }

    sel = orientation->get_selected ();

    if (efs.filterOrientation || full_data) {
        for (size_t i = 0; i < sel.size(); i++) {
            efs.orientations.insert (orientation->get_text (sel[i]));
        }
    }
    
    sel = filetype->get_selected ();

    if (efs.filterFiletype || full_data) {
        for (size_t i = 0; i < sel.size(); i++) {
            efs.filetypes.insert (filetype->get_text (sel[i]));
        }
    }

    return efs;
}


// Called within GTK UI thread
void FilterPanel::valueChanged()
{
    if (listener) {
        listener->exifFilterChanged ();
    }
}
