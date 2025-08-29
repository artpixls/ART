/** -*- C++ -*-
 *  
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2017 Alberto Griggio <alberto.griggio@gmail.com>
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

#include "metadatapanel.h"
#include "eventmapper.h"
#include "../rtengine/procparams.h"

using namespace rtengine;
using namespace rtengine::procparams;


MetaDataPanel::MetaDataPanel()
{
    EvMetaDataMode = ProcEventMapper::getInstance()->newEvent(rtengine::M_VOID, "HISTORY_MSG_METADATA_MODE");
    EvNotes = ProcEventMapper::getInstance()->newEvent(rtengine::M_VOID, "HISTORY_MSG_METADATA_NOTES");

    Gtk::HBox *box = Gtk::manage(new Gtk::HBox());
    box->pack_start(*Gtk::manage(new Gtk::Label(M("TP_METADATA_MODE") + ": ")), Gtk::PACK_SHRINK, 4);
    metadataMode = Gtk::manage(new MyComboBoxText());
    metadataMode->append(M("TP_METADATA_TUNNEL"));
    metadataMode->append(M("TP_METADATA_EDIT"));
    metadataMode->append(M("TP_METADATA_STRIP"));
    metadataMode->set_active(0);
    box->pack_end(*metadataMode, Gtk::PACK_EXPAND_WIDGET, 4);
    pack_start(*box, Gtk::PACK_SHRINK, 4);

    metadataMode->signal_changed().connect(sigc::mem_fun(*this, &MetaDataPanel::metaDataModeChanged));

    tagsNotebook = Gtk::manage(new Gtk::Notebook());
    exifpanel = new ExifPanel();
    iptcpanel = new IPTCPanel();
    tagsNotebook->set_name("MetaPanelNotebook");
    tagsNotebook->append_page(*exifpanel, M("MAIN_TAB_EXIF"));
    tagsNotebook->append_page(*iptcpanel, M("MAIN_TAB_IPTC"));

    notes_ = Gtk::TextBuffer::create();
    notes_view_ = Gtk::manage(new Gtk::TextView(notes_));
    notes_view_->set_wrap_mode(Gtk::WRAP_WORD);
    setExpandAlignProperties(notes_view_, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);
    Gtk::ScrolledWindow *sw = Gtk::manage(new Gtk::ScrolledWindow());
    setExpandAlignProperties(notes_view_, true, true, Gtk::ALIGN_FILL, Gtk::ALIGN_FILL);
    sw->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_ALWAYS);
    sw->add(*notes_view_);
    Gtk::VBox *vb = Gtk::manage(new Gtk::VBox());
    vb->pack_start(*sw, Gtk::PACK_EXPAND_WIDGET, 4);
    vb->set_spacing(4);
    tagsNotebook->append_page(*vb, M("TP_METADATA_NOTES"));
    const auto update_notes =
        [&]() -> void
        {
            if (listener) {
                listener->panelChanged(EvNotes, M("HISTORY_CHANGED"));
            }
        };
    notes_->signal_changed().connect(sigc::slot<void>(update_notes));

    pack_end(*tagsNotebook);
}


MetaDataPanel::~MetaDataPanel()
{
    delete iptcpanel;
    delete exifpanel;
}


void MetaDataPanel::read(const rtengine::procparams::ProcParams* pp)
{
    disableListener();
    metadataMode->set_active(int(pp->metadata.mode));

    exifpanel->read(pp);
    iptcpanel->read(pp);
    notes_->set_text(pp->metadata.notes);
    
    enableListener();
}


void MetaDataPanel::write(rtengine::procparams::ProcParams* pp)
{
    pp->metadata.mode = static_cast<MetaDataParams::Mode>(min(metadataMode->get_active_row_number(), 2));
    pp->metadata.notes = notes_->get_text();
    
    exifpanel->write(pp);
    iptcpanel->write(pp);
}


void MetaDataPanel::setDefaults(const rtengine::procparams::ProcParams* defParams)
{
    exifpanel->setDefaults(defParams);
    iptcpanel->setDefaults(defParams);
}


void MetaDataPanel::setImageData(const rtengine::FramesMetaData* id)
{
    exifpanel->setImageData(id);
    iptcpanel->setImageData(id);
}


void MetaDataPanel::setListener(ToolPanelListener *tpl)
{
    ToolPanel::setListener(tpl);
    exifpanel->setListener(tpl);
    iptcpanel->setListener(tpl);
}


void MetaDataPanel::metaDataModeChanged()
{
    if (listener) {
        listener->panelChanged(EvMetaDataMode, M("HISTORY_CHANGED"));
    }
}


void MetaDataPanel::setProgressListener(rtengine::ProgressListener *pl)
{
    exifpanel->setProgressListener(pl);
}
