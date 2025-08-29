/*
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
#include <glibmm.h>
#include <iostream>
#include "lensprofile.h"
#include "guiutils.h"
#include "../rtengine/lcp.h"
#include <sstream>
#include "rtimage.h"
#include "../rtengine/rtlensfun.h"
#include <map>
#include <set>
#include "../rtengine/lensexif.h"
#include "eventmapper.h"
#ifdef EXIF
#  undef EXIF
#endif

using namespace rtengine;
using namespace rtengine::procparams;

LensProfilePanel::LensProfilePanel() :
    FoldableToolPanel(this, "lensprof", M("TP_LENSPROFILE_LABEL"), false, true, true),
    lcModeChanged(false),
    lcpFileChanged(false),
    useDistChanged(false),
    useVignChanged(false),
    useCAChanged(false),
    useLensfunChanged(false),
    lensfunAutoChanged(false),
    lensfunCameraChanged(false),
    lensfunLensChanged(false),
    allowFocusDep(true),
    isRaw(true),
    metadata(nullptr),
    modesGrid(Gtk::manage(new Gtk::Grid())),
    distGrid(Gtk::manage((new Gtk::Grid()))),
    corrExif(Gtk::manage((new Gtk::RadioButton(corrGroup, M("TP_LENSPROFILE_CORRECTION_EXIF"))))),
    corrLensfunAutoRB(Gtk::manage((new Gtk::RadioButton(corrGroup, M("TP_LENSPROFILE_CORRECTION_AUTOMATCH"))))),
    corrLensfunManualRB(Gtk::manage((new Gtk::RadioButton(corrGroup, M("TP_LENSPROFILE_CORRECTION_MANUAL"))))),
    corrLcpFileRB(Gtk::manage((new Gtk::RadioButton(corrGroup, M("TP_LENSPROFILE_CORRECTION_LCPFILE"))))),
    corrLcpFileChooser(Gtk::manage((new MyFileChooserButton(M("TP_LENSPROFILE_LABEL"), Gtk::FILE_CHOOSER_ACTION_OPEN)))),
    lensfunCamerasLbl(Gtk::manage((new Gtk::Label(M("EXIFFILTER_CAMERA"))))),
    lensfunCameras(Gtk::manage((new MyComboBox()))),
    lensfunLensesLbl(Gtk::manage((new Gtk::Label(M("EXIFFILTER_LENS"))))),
    lensfunLenses(Gtk::manage((new MyComboBox()))),
    warning(Gtk::manage(new RTImage("warning.png"))),
    ckbUseDist(Gtk::manage((new Gtk::CheckButton(M("TP_LENSPROFILE_USE_GEOMETRIC"))))),
    ckbUseVign(Gtk::manage((new Gtk::CheckButton(M("TP_LENSPROFILE_USE_VIGNETTING"))))),
    ckbUseCA(Gtk::manage((new Gtk::CheckButton(M("TP_LENSPROFILE_USE_CA")))))
{
    EvToolEnabled.set_action(rtengine::DARKFRAME); // might need to redo vignetting
    EvToolReset.set_action(rtengine::DARKFRAME);
    
    if (!lf) {
        lf = new LFDbHelper();
    }

    // Main containers:

    Gtk::Frame *nodesFrame = Gtk::manage(new Gtk::Frame(M("TP_LENSPROFILE_MODE_HEADER")));

    modesGrid->get_style_context()->add_class("grid-spacing");
    setExpandAlignProperties(modesGrid, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);

    Gtk::Frame *distFrame = Gtk::manage(new Gtk::Frame(M("TP_LENSPROFILE_USE_HEADER")));

    distGrid->get_style_context()->add_class("grid-spacing");
    setExpandAlignProperties(distGrid, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);

    // Mode choice widgets:

    setExpandAlignProperties(corrLcpFileChooser, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);

    // Manually-selected profile widgets:

    setExpandAlignProperties(lensfunCamerasLbl, false, false, Gtk::ALIGN_END, Gtk::ALIGN_CENTER);

    lensfunCameras->set_model(lf->lensfunCameraModel);
    lensfunCameras->pack_start(lf->lensfunModelCam.model);
    lensfunCameras->setPreferredWidth(50, 120);
    setExpandAlignProperties(lensfunCameras, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);

    Gtk::CellRendererText* const camerasCellRenderer = static_cast<Gtk::CellRendererText*>(lensfunCameras->get_first_cell());
    camerasCellRenderer->property_ellipsize() = Pango::ELLIPSIZE_MIDDLE;
    camerasCellRenderer->property_ellipsize_set() = true;

    setExpandAlignProperties(lensfunLensesLbl, false, false, Gtk::ALIGN_END, Gtk::ALIGN_CENTER);

    lensfunLenses->set_model(lf->lensfunLensModel);
    lensfunLenses->pack_start(lf->lensfunModelLens.prettylens);
    lensfunLenses->setPreferredWidth(50, 120);
    setExpandAlignProperties(lensfunLenses, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);

    Gtk::CellRendererText* const lensesCellRenderer = static_cast<Gtk::CellRendererText*>(lensfunLenses->get_first_cell());
    lensesCellRenderer->property_ellipsize() = Pango::ELLIPSIZE_MIDDLE;
    lensesCellRenderer->property_ellipsize_set() = true;

    warning->set_tooltip_text(M("TP_LENSPROFILE_LENS_WARNING"));
    warning->hide();

    // LCP file filter config:

    const Glib::RefPtr<Gtk::FileFilter> filterLCP = Gtk::FileFilter::create();
    filterLCP->set_name(M("FILECHOOSER_FILTER_LCP"));
    filterLCP->add_pattern("*.lcp");
    filterLCP->add_pattern("*.LCP");
    corrLcpFileChooser->add_filter(filterLCP);

    const Glib::ustring defDir = LCPStore::getInstance()->getDefaultCommonDirectory();

    if (!defDir.empty()) {
#ifdef WIN32
        corrLcpFileChooser->set_show_hidden(true);  // ProgramData is hidden on Windows
#endif
        corrLcpFileChooser->set_current_folder(defDir);
    } else if (!options.lastLensProfileDir.empty()) {
        corrLcpFileChooser->set_current_folder(options.lastLensProfileDir);
    }

    bindCurrentFolder(*corrLcpFileChooser, options.lastLensProfileDir);

    // Choice of properties to correct, applicable to all modes:

    // Populate modes grid:

    modesGrid->attach(*corrExif, 0, 0, 3, 1);
    modesGrid->attach(*corrLensfunAutoRB, 0, 1, 3, 1);
    modesGrid->attach(*corrLensfunManualRB, 0, 2, 3, 1);

    modesGrid->attach(*lensfunCamerasLbl, 0, 3, 1, 1);
    modesGrid->attach(*lensfunCameras, 1, 3, 1, 1);
    modesGrid->attach(*lensfunLensesLbl, 0, 4, 1, 1);
    modesGrid->attach(*lensfunLenses, 1, 4, 1, 1);
    modesGrid->attach(*warning, 2, 3, 1, 2);

    modesGrid->attach(*corrLcpFileRB, 0, 5, 1, 1);
    modesGrid->attach(*corrLcpFileChooser, 1, 5, 1, 1);

#if GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION < 20
    modesGrid->set_row_spacing(2);
    modesGrid->set_column_spacing(4);
#endif

    // Populate distortions grid:

    distGrid->attach(*ckbUseDist, 0, 0, 1, 1);
    distGrid->attach(*ckbUseVign, 0, 1, 1, 1);
    distGrid->attach(*ckbUseCA, 0, 2, 1, 1);

    // Attach grids:
    nodesFrame->add(*modesGrid);
    distFrame->add(*distGrid);
    
    pack_start(*nodesFrame, Gtk::PACK_EXPAND_WIDGET);
    pack_start(*distFrame, Gtk::PACK_EXPAND_WIDGET);

    // Signals:

    conLCPFile = corrLcpFileChooser->signal_file_set().connect(sigc::mem_fun(*this, &LensProfilePanel::onLCPFileChanged));
    conUseDist = ckbUseDist->signal_toggled().connect(sigc::mem_fun(*this, &LensProfilePanel::onUseDistChanged));
    ckbUseVign->signal_toggled().connect(sigc::mem_fun(*this, &LensProfilePanel::onUseVignChanged));
    ckbUseCA->signal_toggled().connect(sigc::mem_fun(*this, &LensProfilePanel::onUseCAChanged));

    lensfunCameras->signal_changed().connect(sigc::mem_fun(*this, &LensProfilePanel::onLensfunCameraChanged));
    lensfunLenses->signal_changed().connect(sigc::mem_fun(*this, &LensProfilePanel::onLensfunLensChanged));
    corrExif->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &LensProfilePanel::onCorrModeChanged), corrExif));
    corrLensfunAutoRB->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &LensProfilePanel::onCorrModeChanged), corrLensfunAutoRB));
    corrLensfunManualRB->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &LensProfilePanel::onCorrModeChanged), corrLensfunManualRB));
    corrLcpFileRB->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &LensProfilePanel::onCorrModeChanged), corrLcpFileRB));
}

void LensProfilePanel::read(const rtengine::procparams::ProcParams* pp)
{
    disableListener();
    conUseDist.block(true);

    setEnabled(true);

    switch (pp->lensProf.lcMode) {
        case procparams::LensProfParams::LcMode::LCP: {
            corrLcpFileRB->set_active(true);
            break;
        }

        case procparams::LensProfParams::LcMode::LENSFUNAUTOMATCH: {
            corrLensfunAutoRB->set_active(true);
            break;
        }

        case procparams::LensProfParams::LcMode::LENSFUNMANUAL: {
            corrLensfunManualRB->set_active(true);
            break;
        }

        case procparams::LensProfParams::LcMode::EXIF: {
            if (metadata) {
                if (rtengine::ExifLensCorrection::ok(metadata)) {
                    corrExif->set_active(true);
                    corrExif->set_sensitive(true);
                    ckbUseCA->set_sensitive(true);
                } else {
                    corrExif->set_sensitive(false);
                    corrLensfunAutoRB->set_active(true);
                }
            } else {
                corrExif->set_sensitive(false);
                setEnabled(false);
            }
            break;
        }
            
        case procparams::LensProfParams::LcMode::NONE: {
            setEnabled(false);
            break;
        }
    }

    if (pp->lensProf.lcpFile.empty()) {
        const Glib::ustring lastFolder = corrLcpFileChooser->get_current_folder();
        corrLcpFileChooser->set_current_folder(lastFolder);
        corrLcpFileChooser->unselect_all();
        bindCurrentFolder(*corrLcpFileChooser, options.lastLensProfileDir);
        updateDisabled();
    } else if (LCPStore::getInstance()->isValidLCPFileName(pp->lensProf.lcpFile)) {
        corrLcpFileChooser->set_filename(pp->lensProf.lcpFile);

        if (corrLcpFileRB->get_active()) {
            updateDisabled();
        }
    } else {
        corrLcpFileChooser->unselect_filename(corrLcpFileChooser->get_filename());
        updateDisabled();
    }

    setManualParamsVisibility(corrLensfunAutoRB->get_active() || corrLensfunManualRB->get_active());
    
    const LFDatabase* const db = LFDatabase::getInstance();
    LFCamera c;

    if (pp->lensProf.lfAutoMatch()) {
        if (metadata) {
            c = db->findCamera(metadata->getMake(), metadata->getModel());
            setLensfunCamera(c.getMake(), c.getModel());
        }
    } else if (pp->lensProf.lfManual()) {
        setLensfunCamera(pp->lensProf.lfCameraMake, pp->lensProf.lfCameraModel);
    }

    if (pp->lensProf.lfAutoMatch()) {
        if (metadata) {
            const LFLens l = db->findLens(c, metadata->getLens());
            setLensfunLens(l.getLens());
        }
    } else if (pp->lensProf.lfManual()) {
        setLensfunLens(pp->lensProf.lfLens);
    }


    ckbUseDist->set_active(pp->lensProf.useDist);
    ckbUseVign->set_active(pp->lensProf.useVign);
    ckbUseCA->set_active(pp->lensProf.useCA);
    
    lcModeChanged = lcpFileChanged = useDistChanged = useVignChanged = useCAChanged = false;
    useLensfunChanged = lensfunAutoChanged = lensfunCameraChanged = lensfunLensChanged = false;
    
    updateLensfunWarning();
    enableListener();
    conUseDist.block(false);
}

void LensProfilePanel::write(rtengine::procparams::ProcParams* pp)
{
    if (!getEnabled()) {
        pp->lensProf.lcMode = procparams::LensProfParams::LcMode::NONE;
    } else if (corrLcpFileRB->get_active()) {
        pp->lensProf.lcMode = procparams::LensProfParams::LcMode::LCP;
    }
    else if (corrLensfunManualRB->get_active()) {
        pp->lensProf.lcMode = procparams::LensProfParams::LcMode::LENSFUNMANUAL;
    }
    else if (corrLensfunAutoRB->get_active()) {
        pp->lensProf.lcMode = procparams::LensProfParams::LcMode::LENSFUNAUTOMATCH;
    }
    else if (corrExif->get_active()) {
        pp->lensProf.lcMode = procparams::LensProfParams::LcMode::EXIF;
    }

    if (LCPStore::getInstance()->isValidLCPFileName(corrLcpFileChooser->get_filename())) {
        pp->lensProf.lcpFile = corrLcpFileChooser->get_filename();
    } else {
        pp->lensProf.lcpFile = "";
    }

    pp->lensProf.useDist = ckbUseDist->get_active();
    pp->lensProf.useVign = ckbUseVign->get_active();
    pp->lensProf.useCA   = ckbUseCA->get_active();

    const auto itc = lensfunCameras->get_active();

    if (itc && !corrLensfunAutoRB->get_active()) {
        pp->lensProf.lfCameraMake = (*itc)[lf->lensfunModelCam.make];
        pp->lensProf.lfCameraModel = (*itc)[lf->lensfunModelCam.model];
    } else {
        pp->lensProf.lfCameraMake = "";
        pp->lensProf.lfCameraModel = "";
    }

    const auto itl = lensfunLenses->get_active();

    if (itl && !corrLensfunAutoRB->get_active()) {
        pp->lensProf.lfLens = (*itl)[lf->lensfunModelLens.lens];
    } else {
        pp->lensProf.lfLens = "";
    }
}

void LensProfilePanel::setRawMeta(bool raw, const rtengine::FramesMetaData* pMeta)
{
    disableListener();
    // if ((!raw || pMeta->getFocusDist() <= 0)) {

    //     // CA is very focus layer dependent, otherwise it might even worsen things
    //     allowFocusDep = false;
    //     ckbUseCA->set_active(false);
    //     ckbUseCA->set_sensitive(false);
    // }

    isRaw = raw;
    metadata = pMeta;

    if (metadata) {
        if (!rtengine::ExifLensCorrection::ok(metadata)) {
            corrExif->set_sensitive(false);
        } else {
            ckbUseCA->set_sensitive(true);
        }        
    } else {
        corrExif->set_sensitive(false);
    }
    enableListener();
}

void LensProfilePanel::onLCPFileChanged()
{
    lcpFileChanged = true;
    const bool valid = LCPStore::getInstance()->isValidLCPFileName(corrLcpFileChooser->get_filename());
    updateDisabled();

    if (listener) {
        if (valid) {
            disableListener();
            corrLcpFileRB->set_active(true);
            enableListener();
        }

        listener->panelChanged(EvLCPFile, Glib::path_get_basename(corrLcpFileChooser->get_filename()));
    }
}

void LensProfilePanel::onUseDistChanged()
{
    useDistChanged = true;
    if (ckbUseDist->get_inconsistent()) {
        ckbUseDist->set_inconsistent(false);
        ckbUseDist->set_active(false);
    }

    if (listener) {
        listener->panelChanged(EvLCPUseDist, ckbUseDist->get_active() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}

void LensProfilePanel::onUseVignChanged()
{
    useVignChanged = true;
    if (ckbUseVign->get_inconsistent()) {
        ckbUseVign->set_inconsistent(false);
        ckbUseVign->set_active(false);
    }

    if (listener) {
        listener->panelChanged(EvLCPUseVign, ckbUseVign->get_active() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}

void LensProfilePanel::onUseCAChanged()
{
    useCAChanged = true;
    if (ckbUseCA->get_inconsistent()) {
        ckbUseCA->set_inconsistent(false);
        ckbUseCA->set_active(false);
    }

    if (listener) {
        listener->panelChanged(EvLCPUseCA, ckbUseCA->get_active() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}


void LensProfilePanel::onLensfunCameraChanged()
{
    const auto iter = lensfunCameras->get_active();

    if (iter) {
        lensfunCameraChanged = true;

        if (listener) {
            disableListener();
            corrLensfunManualRB->set_active(true);
            enableListener();

            const Glib::ustring name = (*iter)[lf->lensfunModelCam.model];
            listener->panelChanged(EvLensCorrLensfunCamera, name);
        }
    }

    updateLensfunWarning();
}

void LensProfilePanel::onLensfunLensChanged()
{
    const auto iter = lensfunLenses->get_active();

    if (iter) {
        lensfunLensChanged = true;

        if (listener) {
            disableListener();
            corrLensfunManualRB->set_active(true);
            enableListener();

            const Glib::ustring name = (*iter)[lf->lensfunModelLens.prettylens];
            listener->panelChanged(EvLensCorrLensfunLens, name);
        }
    }

    updateLensfunWarning();
}

void LensProfilePanel::onCorrModeChanged(const Gtk::RadioButton* rbChanged)
{
    if (rbChanged->get_active()) {
        Glib::ustring mode;
        if (rbChanged == corrLensfunAutoRB) {
            lcModeChanged = true;
            useLensfunChanged = true;
            lensfunAutoChanged = true;
            lensfunCameraChanged = true;
            lensfunLensChanged = true;
            lcpFileChanged = false;

            const bool disabled = disableListener();
            if (metadata) {
                const LFDatabase* const db = LFDatabase::getInstance();
                const LFCamera c = db->findCamera(metadata->getMake(), metadata->getModel());
                const LFLens l = db->findLens(c, metadata->getLens());
                setLensfunCamera(c.getMake(), c.getModel());
                setLensfunLens(l.getLens());
            }
            if (disabled) {
                enableListener();
            }
            
            mode = M("TP_LENSPROFILE_CORRECTION_AUTOMATCH");
            
        } else if (rbChanged == corrLensfunManualRB) {
            lcModeChanged = true;
            useLensfunChanged = true;
            lensfunAutoChanged = true;
            lensfunCameraChanged = true;
            lensfunLensChanged = true;
            lcpFileChanged = false;

            mode = M("TP_LENSPROFILE_CORRECTION_MANUAL");
            
        } else if (rbChanged == corrLcpFileRB || rbChanged == corrExif) {
            lcModeChanged = true;
            useLensfunChanged = true;
            lensfunAutoChanged = true;
            lcpFileChanged = true;


            mode = M("TP_LENSPROFILE_CORRECTION_LCPFILE");            
        }

        updateLensfunWarning();
        updateDisabled();

        if (rbChanged == corrLensfunManualRB || (rbChanged == corrLensfunAutoRB)) {
            setManualParamsVisibility(true);
        } else {
            setManualParamsVisibility(false);
        }

        if (listener) {
            listener->panelChanged(EvLensCorrMode, mode);
        }
    }
}

//-----------------------------------------------------------------------------
// LFDbHelper
//-----------------------------------------------------------------------------

LensProfilePanel::LFDbHelper::LFDbHelper()
{
    lensfunCameraModel = Gtk::TreeStore::create(lensfunModelCam);
    lensfunLensModel = Gtk::TreeStore::create(lensfunModelLens);

#ifdef _OPENMP
#pragma omp parallel sections if (!options.rtSettings.verbose)
#endif
    {
#ifdef _OPENMP
#pragma omp section
#endif
        {
            fillLensfunCameras();
        }
#ifdef _OPENMP
#pragma omp section
#endif
        {
            fillLensfunLenses();
        }
    }
}

void LensProfilePanel::LFDbHelper::fillLensfunCameras()
{
    if (options.rtSettings.verbose > 1) {
        std::cout << "LENSFUN, scanning cameras:" << std::endl;
    }

    std::map<Glib::ustring, std::set<Glib::ustring>> camnames;
    const auto camlist = LFDatabase::getInstance()->getCameras();

    for (const auto& c : camlist) {
        camnames[c.getMake()].insert(c.getModel());

        if (options.rtSettings.verbose > 1) {
            std::cout << "  found: " << c.getDisplayString().c_str() << std::endl;
        }
    }

    for (const auto& p : camnames) {
        Gtk::TreeModel::Row row = *(lensfunCameraModel->append());
        row[lensfunModelCam.make] = p.first;
        row[lensfunModelCam.model] = p.first;

        for (const auto& c : p.second) {
            Gtk::TreeModel::Row child = *(lensfunCameraModel->append(row.children()));
            child[lensfunModelCam.make] = p.first;
            child[lensfunModelCam.model] = c;
        }
    }
}

void LensProfilePanel::LFDbHelper::fillLensfunLenses()
{
    if (options.rtSettings.verbose > 1) {
        std::cout << "LENSFUN, scanning lenses:" << std::endl;
    }

    std::map<Glib::ustring, std::set<Glib::ustring>> lenses;
    const auto lenslist = LFDatabase::getInstance()->getLenses();

    for (const auto& l : lenslist) {
        const auto& name = l.getLens();
        const auto& make = l.getMake();
        lenses[make].insert(name);

        if (options.rtSettings.verbose > 1) {
            std::cout << "  found: " << l.getDisplayString().c_str() << std::endl;
        }
    }

    for (const auto& p : lenses) {
        Gtk::TreeModel::Row row = *(lensfunLensModel->append());
        row[lensfunModelLens.lens] = p.first;
        row[lensfunModelLens.prettylens] = p.first;

        for (auto &c : p.second) {
            Gtk::TreeModel::Row child = *(lensfunLensModel->append(row.children()));
            child[lensfunModelLens.lens] = c;

            if (c.find(p.first, p.first.size() + 1) == p.first.size() + 1) {
                child[lensfunModelLens.prettylens] = c.substr(p.first.size() + 1);
            } else {
                child[lensfunModelLens.prettylens] = c;
            }
        }
    }
}

void LensProfilePanel::updateDisabled()
{
    ckbUseDist->set_sensitive(true);
    ckbUseVign->set_sensitive(true);
    ckbUseCA->set_sensitive(true);
    
    ckbUseVign->set_sensitive(isRaw);
    // ckbUseCA->set_sensitive(allowFocusDep || corrExif->get_active());
}

bool LensProfilePanel::setLensfunCamera(const Glib::ustring& make, const Glib::ustring& model)
{
    if (!make.empty() && !model.empty()) {
        const auto camera_it = lensfunCameras->get_active();

        if (camera_it && (*camera_it)[lf->lensfunModelCam.make] == make && (*camera_it)[lf->lensfunModelCam.model] == model) {
            return true;
        }

        // search for the active row
        for (const auto& row : lf->lensfunCameraModel->children()) {
            if (row[lf->lensfunModelCam.make] == make) {
                const auto& c = row.children();

                for (auto model_it = c.begin(), end = c.end(); model_it != end; ++model_it) {
                    const auto& childrow = *model_it;

                    if (childrow[lf->lensfunModelCam.model] == model) {
                        lensfunCameras->set_active(model_it);
                        return true;
                    }
                }

                break;
            }
        }
    }

    lensfunCameras->set_active(-1);
    return false;
}

bool LensProfilePanel::setLensfunLens(const Glib::ustring& lens)
{
    if (!lens.empty()) {
        const auto lens_it = lensfunLenses->get_active();

        if (lens_it && (*lens_it)[lf->lensfunModelLens.lens] == lens) {
            return true;
        }

        bool first_maker_found = false;

        for (const auto& row : lf->lensfunLensModel->children()) {
            if (lens.find(row[lf->lensfunModelLens.lens]) == 0) {
                const auto& c = row.children();

                for (auto model_it = c.begin(), end = c.end(); model_it != end; ++model_it) {
                    const auto& childrow = *model_it;

                    if (childrow[lf->lensfunModelLens.lens] == lens) {
                        lensfunLenses->set_active(model_it);
                        return true;
                    }
                }

                // we do not break immediately here, because there might be multiple makers
                // sharing the same prefix (e.g. "Leica" and "Leica Camera AG").
                // therefore, we break below when the lens doesn't match any of them
                first_maker_found = true;
            } else if (first_maker_found) {
                break;
            }
        }
    }

    lensfunLenses->set_active(-1);
    return false;
}

bool LensProfilePanel::checkLensfunCanCorrect(bool automatch)
{
    if (!metadata) {
        return false;
    }

    rtengine::procparams::ProcParams lpp;
    write(&lpp);
    const std::unique_ptr<LFModifier> mod(LFDatabase::getInstance()->findModifier(lpp.lensProf, metadata, 100, 100, lpp.coarse, -1));
    return static_cast<bool>(mod);
}

void LensProfilePanel::setManualParamsVisibility(bool setVisible)
{
    if (setVisible) {
        lensfunCamerasLbl->show();
        lensfunCameras->show();
        lensfunLensesLbl->show();
        lensfunLenses->show();
        updateLensfunWarning();
    } else {
        lensfunCamerasLbl->hide();
        lensfunCameras->hide();
        lensfunLensesLbl->hide();
        lensfunLenses->hide();
        warning->hide();
    }
}

void LensProfilePanel::updateLensfunWarning()
{
    warning->hide();

    ckbUseVign->set_sensitive(isRaw);
    ckbUseDist->set_sensitive(true);
    ckbUseCA->set_sensitive(true);
        
    if (corrLensfunManualRB->get_active() || corrLensfunAutoRB->get_active()) {
        const LFDatabase* const db = LFDatabase::getInstance();

        const auto itc = lensfunCameras->get_active();

        if (!itc) {
            return;
        }

        const LFCamera c = db->findCamera((*itc)[lf->lensfunModelCam.make], (*itc)[lf->lensfunModelCam.model]);
        const auto itl = lensfunLenses->get_active();

        if (!itl) {
            return;
        }

        const LFLens l = db->findLens(LFCamera(), (*itl)[lf->lensfunModelLens.lens]);
        const float lenscrop = l.getCropFactor();
        const float camcrop = c.getCropFactor();

        if (lenscrop <= 0 || camcrop <= 0 || lenscrop / camcrop >= 1.01f) {
            warning->show();
        }

        ckbUseVign->set_sensitive(l.hasVignettingCorrection());
        ckbUseDist->set_sensitive(l.hasDistortionCorrection());
        ckbUseCA->set_sensitive(l.hasCACorrection()); // && allowFocusDep);

        if (!isRaw || !l.hasVignettingCorrection()) {
            ckbUseVign->set_active(false);
        }

        if (!l.hasDistortionCorrection()) {
            ckbUseDist->set_active(false);
        }

        if (!l.hasCACorrection()) {
            ckbUseCA->set_active(false);
        }
    }
}

LensProfilePanel::LFDbHelper* LensProfilePanel::lf(nullptr);


void LensProfilePanel::setDefaults(const ProcParams *def)
{
    initial_params = def->lensProf;
}


void LensProfilePanel::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.lensProf = initial_params;
    }
    read(&pp);
    if (listener && !getEnabled()) {
        listener->panelChanged(EvToolReset, M("GENERAL_RESET"));
    }
}
