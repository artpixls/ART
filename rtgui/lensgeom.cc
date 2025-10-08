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
#include "lensgeom.h"
#include "guiutils.h"
#include "rtimage.h"

using namespace rtengine;
using namespace rtengine::procparams;

GeometryPanel::GeometryPanel () : FoldableToolPanel(this, "lensgeom", M("TP_GEOM_LABEL")), rlistener(nullptr), lastFill(false)
{

    fill = Gtk::manage (new Gtk::CheckButton (M("TP_LENSGEOM_FILL")));
    pack_start (*fill);

    autoCrop = Gtk::manage (new Gtk::Button (M("TP_LENSGEOM_AUTOCROP")));
    autoCrop->set_image (*Gtk::manage (new RTImage ("crop-auto-small.svg")));
    autoCrop->get_style_context()->add_class("independent");
    pack_start (*autoCrop, Gtk::PACK_SHRINK, 2);

    packBox = Gtk::manage (new ToolParamBlock ());
    pack_start (*packBox);

    autoCrop->signal_pressed().connect( sigc::mem_fun(*this, &GeometryPanel::autoCropPressed) );
    fillConn = fill->signal_toggled().connect( sigc::mem_fun(*this, &GeometryPanel::fillPressed) );

    fill->set_active (true);
    show_all ();
}

GeometryPanel::~GeometryPanel ()
{
    idle_register.destroy();
}

void GeometryPanel::read(const ProcParams* pp)
{

    disableListener ();

    fillConn.block (true);
    fill->set_active (pp->commonTrans.autofill);
    fillConn.block (false);
    autoCrop->set_sensitive (!pp->commonTrans.autofill);

    lastFill = pp->commonTrans.autofill;

    enableListener ();
}

void GeometryPanel::write(ProcParams* pp)
{
    pp->commonTrans.autofill   = fill->get_active ();
}

void GeometryPanel::autoCropPressed ()
{

    if (rlistener) {
        rlistener->autoCropRequested ();
    }
}

void GeometryPanel::fillPressed ()
{
    autoCrop->set_sensitive (!fill->get_active());

    if (listener) {
        if (fill->get_active ()) {
            listener->panelChanged (EvTransAutoFill, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged (EvTransAutoFill, M("GENERAL_DISABLED"));
        }
    }
}


LensPanel::LensPanel():
    FoldableToolPanel(this, "lensaberr", M("TP_LENS_LABEL"))
{
    packBox = Gtk::manage (new ToolParamBlock ());
    pack_start (*packBox);

    show_all ();
}
