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
#pragma once

#include <gtkmm.h>
#include "toolpanel.h"
#include "exifpanel.h"
#include "iptcpanel.h"

class MetaDataPanel: public Gtk::VBox, public ToolPanel {
public:
    MetaDataPanel();
    ~MetaDataPanel() override;
    
    void read(const rtengine::procparams::ProcParams* pp) override;
    void write(rtengine::procparams::ProcParams* pp) override;
    void setDefaults(const rtengine::procparams::ProcParams* defParams) override;

    void setImageData(const rtengine::FramesMetaData* id);
    void setListener(ToolPanelListener *tpl) override;

    void setProgressListener(rtengine::ProgressListener *pl);

private:
    rtengine::ProcEvent EvMetaDataMode;
    rtengine::ProcEvent EvNotes;
    
    MyComboBoxText *metadataMode;
    Gtk::Notebook *tagsNotebook;
    ExifPanel *exifpanel;
    IPTCPanel *iptcpanel;
    Gtk::TextView *notes_view_;
    Glib::RefPtr<Gtk::TextBuffer> notes_;

    void metaDataModeChanged();
};

