/* -*- C++ -*-
*  
*  This file is part of RawTherapee.
*
*  Copyright (c) 2012 Oliver Duis <oduis@oliverduis.de>
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

#include "guiutils.h"
#include "lensgeom.h"
#include "toolpanel.h"

class LensProfilePanel: public ToolParamBlock, public FoldableToolPanel {
public:
    LensProfilePanel();

    void read(const rtengine::procparams::ProcParams* pp) override;
    void write(rtengine::procparams::ProcParams* pp) override;
    void setRawMeta(bool raw, const rtengine::FramesMetaData* pMeta);

    void onLCPFileChanged();
    void onUseDistChanged();
    void onUseVignChanged();
    void onUseCAChanged();

    void onLensfunCameraChanged();
    void onLensfunLensChanged();
    void onCorrModeChanged(const Gtk::RadioButton* rbChanged);

    void setDefaults(const rtengine::procparams::ProcParams *def) override;
    void toolReset(bool to_initial) override;

private:
    class LFDbHelper {
    public:
        class LFModelCam: public Gtk::TreeModel::ColumnRecord {
        public:
            LFModelCam()
            {
                add(make);
                add(model);
            }
            Gtk::TreeModelColumn<Glib::ustring> make;
            Gtk::TreeModelColumn<Glib::ustring> model;
        };

        class LFModelLens: public Gtk::TreeModel::ColumnRecord {
        public:
            LFModelLens()
            {
                add(lens);
                add(prettylens);
            }
            Gtk::TreeModelColumn<Glib::ustring> lens;
            Gtk::TreeModelColumn<Glib::ustring> prettylens;
        };

        LFModelCam lensfunModelCam;
        LFModelLens lensfunModelLens;

        Glib::RefPtr<Gtk::TreeStore> lensfunCameraModel;
        Glib::RefPtr<Gtk::TreeStore> lensfunLensModel;

        LFDbHelper();

        void fillLensfunCameras();
        void fillLensfunLenses();
    };

    void updateDisabled();
    bool setLensfunCamera(const Glib::ustring& make, const Glib::ustring& model);
    bool setLensfunLens(const Glib::ustring& lens);
    bool checkLensfunCanCorrect(bool automatch);
    void setManualParamsVisibility(bool setVisible);
    void updateLensfunWarning();

    bool lcModeChanged;
    bool lcpFileChanged;
    bool useDistChanged;
    bool useVignChanged;
    bool useCAChanged;
    bool useLensfunChanged;
    bool lensfunAutoChanged;
    bool lensfunCameraChanged;
    bool lensfunLensChanged;
    sigc::connection conLCPFile;
    sigc::connection conUseDist;
    sigc::connection conUseVign;
    sigc::connection conUseCA;
    //bool allowFocusDep;
    bool isRaw;
    const rtengine::FramesMetaData* metadata;

    Gtk::Grid* const modesGrid;
    Gtk::Grid* const distGrid;
    Gtk::RadioButton::Group corrGroup;
    Gtk::RadioButton *corrExif;
    Gtk::RadioButton* const corrLensfunAutoRB;
    Gtk::RadioButton* const corrLensfunManualRB;
    Gtk::RadioButton* const corrLcpFileRB;
    MyFileChooserButton* const corrLcpFileChooser;
    Gtk::Label* const lensfunCamerasLbl;
    MyComboBox* const lensfunCameras;
    Gtk::Label* const lensfunLensesLbl;
    MyComboBox* const lensfunLenses;
    Gtk::Image* const warning;
    Gtk::CheckButton* const ckbUseDist;
    Gtk::CheckButton* const ckbUseVign;
    Gtk::CheckButton* const ckbUseCA;

    rtengine::procparams::LensProfParams initial_params;

    static LFDbHelper* lf;
};
