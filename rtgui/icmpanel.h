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

#include <memory>
#include <gtkmm.h>
#include "adjuster.h"
#include "guiutils.h"

#include "toolpanel.h"
#include "popupbutton.h"
#include "../rtengine/imagedata.h"

class ICMPanelListener
{
public:
    virtual ~ICMPanelListener() = default;
    virtual void saveInputICCReference(const Glib::ustring& fname, bool apply_wb) = 0;
};

class ICMPanel :
    public ToolParamBlock,
    public AdjusterListener,
    public FoldableToolPanel
{

protected:
    Gtk::Frame* dcpFrame;
    Gtk::Frame* coipFrame;

    Gtk::Label* labmga;
    Gtk::HBox* gabox;


    //bool freegamma;
    sigc::connection tcurveconn;
    sigc::connection ltableconn;
    sigc::connection beoconn;
    sigc::connection hsmconn;
    sigc::connection obpcconn;

    rtengine::procparams::ColorManagementParams initial_params;

private:
    rtengine::ProcEvent EvUseCAT;
    rtengine::ProcEvent EvDCPApplyLookEarly;

    Gtk::CheckButton* obpc;
    Gtk::RadioButton* inone;

    Gtk::RadioButton* iembedded;
    Gtk::RadioButton* icamera;
    Gtk::RadioButton* icameraICC;
    Gtk::RadioButton* ifromfile;
    Gtk::Label* dcpIllLabel;
    MyComboBoxText* dcpIll;
    sigc::connection dcpillconn;
    Gtk::CheckButton* ckbToneCurve;
    Gtk::CheckButton* ckbApplyLookTable;
    Gtk::CheckButton* ckbApplyBaselineExposureOffset;
    Gtk::CheckButton* ckbApplyHueSatMap;
    MyComboBoxText *dcp_look_early_;
    
    MyComboBoxText* wProfNames;
    sigc::connection wprofnamesconn;

    std::vector<Glib::ustring> out_profiles_;

    MyComboBoxText* oProfNames;
    sigc::connection oprofnamesconn;
    std::unique_ptr<PopUpButton> oRendIntent;
    sigc::connection orendintentconn;
    Gtk::RadioButton* iunchanged;
    MyFileChooserButton* ipDialog;
    Gtk::RadioButton::Group opts;
    Gtk::Button* saveRef;
    sigc::connection ipc;
    Glib::ustring oldip;
    ICMPanelListener* icmplistener;

    double dcpTemperatures[2];
    Glib::ustring lastRefFilename;
    Glib::ustring camName;
    Glib::ustring filename;

    Gtk::CheckButton *use_CAT_;
    
    void updateDCP(int dcpIlluminant, Glib::ustring dcp_name);
    void updateRenderingIntent(const Glib::ustring &profile);
public:
    ICMPanel();

    void read(const rtengine::procparams::ProcParams* pp) override;
    void write(rtengine::procparams::ProcParams* pp) override;
    void setDefaults(const rtengine::procparams::ProcParams* defParams) override;
    void adjusterChanged(Adjuster* a, double newval) override;
    void adjusterAutoToggled(Adjuster* a, bool newval) override;

    void wpChanged();
    void wtrcinChanged();
    void opChanged();
    void oiChanged(int n);
    void oBPCChanged();
    void ipChanged();
    void ipSelectionChanged();
    void dcpIlluminantChanged();
    void toneCurveChanged();
    void applyLookTableChanged();
    void applyBaselineExposureOffsetChanged();
    void applyHueSatMapChanged();

    void setRawMeta(bool raw, const rtengine::FramesData* pMeta);
    void saveReferencePressed();

    void setICMPanelListener(ICMPanelListener* ipl)
    {
        icmplistener = ipl;
    }

    void toolReset(bool to_initial) override;
};

