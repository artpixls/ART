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
#include <iomanip>
#include <tuple>
#include "icmpanel.h"
#include "options.h"
#include "eventmapper.h"

#include "guiutils.h"
#include "../rtengine/iccstore.h"
#include "../rtengine/dcp.h"
#include "rtimage.h"

using namespace rtengine;
using namespace rtengine::procparams;

extern Options options;

ICMPanel::ICMPanel():
    FoldableToolPanel(this, "icm", M("TP_ICM_LABEL"), false, false, true),
    iunchanged(nullptr),
    icmplistener(nullptr),
    lastRefFilename(""),
    camName(""),
    filename("")
{
    auto m = ProcEventMapper::getInstance();
    EvUseCAT = m->newEvent(rtengine::ALLNORAW, "HISTORY_MSG_ICM_INPUT_CAT");
    EvToolReset.set_action(rtengine::DEMOSAIC);
    EvDCPApplyLookEarly = m->newEvent(rtengine::RGBCURVE, "HISTORY_MSG_ICM_DCP_LOOK_PIPELINE_POS");

    ipDialog = Gtk::manage(new MyFileChooserButton(M("TP_ICM_INPUTDLGLABEL"), Gtk::FILE_CHOOSER_ACTION_OPEN));
    ipDialog->set_tooltip_text(M("TP_ICM_INPUTCUSTOM_TOOLTIP"));
    bindCurrentFolder(*ipDialog, options.lastIccDir);


    // ------------------------------- Input profile

    auto iVBox = Gtk::manage(new Gtk::VBox());
    auto exp = Gtk::manage(new MyExpander(false, M("TP_ICM_INPUTPROFILE")));
    exp->add(*iVBox, false);

    inone = Gtk::manage(new Gtk::RadioButton(M("TP_ICM_INPUTNONE")));
    inone->set_tooltip_text(M("TP_ICM_INPUTNONE_TOOLTIP"));
    iVBox->pack_start(*inone, Gtk::PACK_SHRINK);

    iembedded = Gtk::manage(new Gtk::RadioButton(M("TP_ICM_INPUTEMBEDDED")));
    iembedded->set_tooltip_text(M("TP_ICM_INPUTEMBEDDED_TOOLTIP"));
    iVBox->pack_start(*iembedded, Gtk::PACK_SHRINK);

    icamera = Gtk::manage(new Gtk::RadioButton(M("TP_ICM_INPUTCAMERA")));
    icamera->set_tooltip_text(M("TP_ICM_INPUTCAMERA_TOOLTIP"));
    iVBox->pack_start(*icamera, Gtk::PACK_SHRINK);

    icameraICC = Gtk::manage(new Gtk::RadioButton(M("TP_ICM_INPUTCAMERAICC")));
    icameraICC->set_tooltip_text(M("TP_ICM_INPUTCAMERAICC_TOOLTIP"));
    iVBox->pack_start(*icameraICC, Gtk::PACK_SHRINK);

    ifromfile = Gtk::manage(new Gtk::RadioButton(M("TP_ICM_INPUTCUSTOM") + ":"));
    Gtk::HBox* ffbox = Gtk::manage(new Gtk::HBox());
    ifromfile->set_tooltip_text(M("TP_ICM_INPUTCUSTOM_TOOLTIP"));
    ffbox->pack_start(*ifromfile, Gtk::PACK_SHRINK);
    ffbox->pack_start(*ipDialog);

    iVBox->pack_start(*ffbox, Gtk::PACK_SHRINK);

    opts = icamera->get_group();
    icameraICC->set_group(opts);
    iembedded->set_group(opts);
    ifromfile->set_group(opts);
    inone->set_group(opts);

    dcpFrame = Gtk::manage(new Gtk::Frame("DCP"));
    dcpFrame->set_name("ExpanderBox2");

    auto dcpGrid = Gtk::manage(new Gtk::VBox());

    Gtk::Grid* dcpIllGrid = Gtk::manage(new Gtk::Grid());
    dcpIllGrid->set_column_homogeneous(false);
    dcpIllGrid->set_row_homogeneous(false);
    dcpIllGrid->set_column_spacing(2);
    dcpIllGrid->set_row_spacing(2);

    dcpIllLabel = Gtk::manage(new Gtk::Label(M("TP_ICM_DCPILLUMINANT") + ":"));
    setExpandAlignProperties(dcpIllLabel, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    dcpIllLabel->set_tooltip_text(M("TP_ICM_DCPILLUMINANT_TOOLTIP"));
    dcpIllLabel->show();
    dcpIll = Gtk::manage(new MyComboBoxText());
    setExpandAlignProperties(dcpIll, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);
    dcpIll->set_tooltip_text(M("TP_ICM_DCPILLUMINANT_TOOLTIP"));
    dcpIll->append(M("TP_ICM_DCPILLUMINANT_INTERPOLATED"));
    dcpIll->append(M("TP_ICM_DCPILLUMINANT") + " 1");
    dcpIll->append(M("TP_ICM_DCPILLUMINANT") + " 2");
    dcpIll->show();
    dcpTemperatures[0] = 0;
    dcpTemperatures[1] = 0;
    dcpIllGrid->attach_next_to(*dcpIllLabel, Gtk::POS_LEFT, 1, 1);
    dcpIllGrid->attach_next_to(*dcpIll, *dcpIllLabel, Gtk::POS_RIGHT, 1, 1);

    ckbToneCurve = Gtk::manage(new Gtk::CheckButton(M("TP_ICM_TONECURVE")));
    ckbToneCurve->set_sensitive(false);
    ckbToneCurve->set_tooltip_text(M("TP_ICM_TONECURVE_TOOLTIP"));
    setExpandAlignProperties(ckbToneCurve, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);

    ckbApplyLookTable = Gtk::manage(new Gtk::CheckButton(M("TP_ICM_APPLYLOOKTABLE")));
    ckbApplyLookTable->set_sensitive(false);
    ckbApplyLookTable->set_tooltip_text(M("TP_ICM_APPLYLOOKTABLE_TOOLTIP"));
    setExpandAlignProperties(ckbApplyLookTable, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);

    ckbApplyHueSatMap = Gtk::manage(new Gtk::CheckButton(M("TP_ICM_APPLYHUESATMAP")));
    ckbApplyHueSatMap->set_sensitive(false);
    ckbApplyHueSatMap->set_tooltip_text(M("TP_ICM_APPLYHUESATMAP_TOOLTIP"));
    setExpandAlignProperties(ckbApplyHueSatMap, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);

    ckbApplyBaselineExposureOffset = Gtk::manage(new Gtk::CheckButton(M("TP_ICM_APPLYBASELINEEXPOSUREOFFSET")));
    ckbApplyBaselineExposureOffset->set_sensitive(false);
    ckbApplyBaselineExposureOffset->set_tooltip_text(M("TP_ICM_APPLYBASELINEEXPOSUREOFFSET_TOOLTIP"));
    setExpandAlignProperties(ckbApplyBaselineExposureOffset, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);

    auto dcp_look_early_hbox = Gtk::manage(new Gtk::HBox());
    auto dcp_look_early_lbl = Gtk::manage(new Gtk::Label(M("TP_ICM_DCP_LOOK_PIPELINE_POS") + ": "));
    dcp_look_early_lbl->set_tooltip_text(M("TP_ICM_DCP_LOOK_PIPELINE_POS_TOOLTIP"));
    dcp_look_early_hbox->pack_start(*dcp_look_early_lbl, Gtk::PACK_SHRINK);
    dcp_look_early_ = Gtk::manage(new MyComboBoxText());
    dcp_look_early_->append(M("TP_ICM_DCP_LOOK_PIPELINE_POS_EARLY"));
    dcp_look_early_->append(M("TP_ICM_DCP_LOOK_PIPELINE_POS_LATE"));
    dcp_look_early_hbox->pack_start(*dcp_look_early_, Gtk::PACK_EXPAND_WIDGET, 4);
    setExpandAlignProperties(dcp_look_early_lbl, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    setExpandAlignProperties(dcp_look_early_, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);

    dcpGrid->pack_start(*dcpIllGrid);
    dcpGrid->pack_start(*ckbApplyHueSatMap);
    dcpGrid->pack_start(*ckbApplyBaselineExposureOffset);
    dcpGrid->pack_start(*ckbApplyLookTable);
    dcpGrid->pack_start(*ckbToneCurve);
    dcpGrid->pack_start(*dcp_look_early_hbox);

    dcpFrame->add(*dcpGrid);
    //dcpFrame->set_sensitive(false);
    iVBox->pack_start(*dcpFrame);

    use_CAT_ = Gtk::manage(new Gtk::CheckButton(M("TP_ICM_INPUT_CAT")));
    iVBox->pack_start(*use_CAT_, Gtk::PACK_SHRINK);
    use_CAT_->signal_toggled().connect(
        sigc::slot<void>(
            [this]() -> void
            {
                if (listener) {
                    if (use_CAT_->get_active()) {
                        listener->panelChanged(EvUseCAT, M("GENERAL_ENABLED"));
                    } else {
                        listener->panelChanged(EvUseCAT, M("GENERAL_DISABLED"));
                    }
                }
            }));

    saveRef = Gtk::manage(new Gtk::Button(M("TP_ICM_SAVEREFERENCE")));
    saveRef->set_image(*Gtk::manage(new RTImage("save-small.svg")));
    saveRef->set_alignment(0.5f, 0.5f);
    saveRef->set_tooltip_markup(M("TP_ICM_SAVEREFERENCE_TOOLTIP"));
    iVBox->pack_start(*saveRef, Gtk::PACK_SHRINK);

    //pack_start(*iVBox, Gtk::PACK_EXPAND_WIDGET);
    pack_start(*exp, Gtk::PACK_EXPAND_WIDGET);


    // ---------------------------- Working profile
    Gtk::VBox *wProfVBox = Gtk::manage(new Gtk::VBox());
    exp = Gtk::manage(new MyExpander(false, M("TP_ICM_WORKINGPROFILE")));
    exp->add(*wProfVBox, false);

    wProfNames = Gtk::manage(new MyComboBoxText());
    wProfVBox->pack_start(*wProfNames, Gtk::PACK_SHRINK);

    std::vector<Glib::ustring> wpnames = rtengine::ICCStore::getInstance()->getWorkingProfiles();

    for (size_t i = 0; i < wpnames.size(); i++) {
        wProfNames->append(wpnames[i]);
    }

    wProfNames->set_active(0);

    pack_start(*exp, Gtk::PACK_EXPAND_WIDGET);


    // ---------------------------- Output profile

    Gtk::VBox *oProfVBox = Gtk::manage(new Gtk::VBox());
    exp = Gtk::manage(new MyExpander(false, M("TP_ICM_OUTPUTPROFILE")));
    exp->add(*oProfVBox, false);

    oProfNames = Gtk::manage(new MyComboBoxText());
    oProfVBox->pack_start(*oProfNames, Gtk::PACK_SHRINK);

    oProfNames->append(M("TP_ICM_NOICM"));
    oProfNames->append(M("TP_ICM_NOPROFILE"));
    oProfNames->set_active(0);

    out_profiles_ = {
        ColorManagementParams::NoICMString,
        ColorManagementParams::NoProfileString
    };

    std::vector<Glib::ustring> opnames = ICCStore::getInstance()->getProfiles(rtengine::ICCStore::ProfileType::OUTPUT);

    const auto is_ART_profile =
        [](cmsHPROFILE p) -> bool
        {
            return ICCStore::getProfileTag(p, cmsSigDeviceMfgDescTag) == "ART";
        };

    std::vector<std::tuple<int, Glib::ustring, Glib::ustring>> opl;
    for (size_t i = 0; i < opnames.size(); i++) {
        auto lbl = opnames[i];
        auto p = ICCStore::getInstance()->getProfile(opnames[i]);
        int key = 1;
        if (p) { 
            auto s = ICCStore::getProfileTag(p, cmsSigProfileDescriptionTag);
            if (is_ART_profile(p)) {
                lbl = s;
                key = 0;
            }
        }
        opl.push_back({key, lbl, opnames[i]});
    }
    std::sort(opl.begin(), opl.end());
    for (auto &t : opl) {
        out_profiles_.push_back(std::get<2>(t));
        oProfNames->append(std::get<1>(t));
    }
    
    oProfNames->set_active(0);

    // Rendering intent
    Gtk::HBox *riHBox = Gtk::manage(new Gtk::HBox());
    Gtk::Label* outputIntentLbl = Gtk::manage(new Gtk::Label(M("TP_ICM_PROFILEINTENT")));
    riHBox->pack_start(*outputIntentLbl, Gtk::PACK_SHRINK);
    oRendIntent.reset(new PopUpButton());
    oRendIntent->addEntry("intent-perceptual.svg", M("PREFERENCES_INTENT_PERCEPTUAL"));
    oRendIntent->addEntry("intent-relative.svg", M("PREFERENCES_INTENT_RELATIVE"));
    oRendIntent->addEntry("intent-saturation.svg", M("PREFERENCES_INTENT_SATURATION"));
    oRendIntent->addEntry("intent-absolute.svg", M("PREFERENCES_INTENT_ABSOLUTE"));
    oRendIntent->setSelected(1);
    oRendIntent->show();
    riHBox->pack_start(*oRendIntent->buttonGroup, Gtk::PACK_EXPAND_PADDING);
    oProfVBox->pack_start(*riHBox, Gtk::PACK_SHRINK);

    // Black Point Compensation
    obpc = Gtk::manage(new Gtk::CheckButton((M("TP_ICM_BPC"))));
    obpc->set_active(true);
    oProfVBox->pack_start(*obpc, Gtk::PACK_SHRINK);

    //oFrame->add(*oProfVBox);

    //pack_start(*oProfVBox, Gtk::PACK_EXPAND_WIDGET);
    pack_start(*exp, Gtk::PACK_EXPAND_WIDGET);

    // ---------------------------- Output gamma list entries

    Glib::RefPtr<Gtk::FileFilter> filter_icc = Gtk::FileFilter::create();
    filter_icc->set_name(M("FILECHOOSER_FILTER_COLPROF"));
    filter_icc->add_pattern("*.dcp");
    filter_icc->add_pattern("*.DCP");
    filter_icc->add_pattern("*.icc");
    filter_icc->add_pattern("*.icm");
    filter_icc->add_pattern("*.ICC");
    filter_icc->add_pattern("*.ICM");
    Glib::RefPtr<Gtk::FileFilter> filter_iccdng = Gtk::FileFilter::create();
    filter_iccdng->set_name(M("FILECHOOSER_FILTER_COLPROF") + " + DNG");
    filter_iccdng->add_pattern("*.dcp");
    filter_iccdng->add_pattern("*.DCP");
    filter_iccdng->add_pattern("*.dng");
    filter_iccdng->add_pattern("*.DNG");
    filter_iccdng->add_pattern("*.icc");
    filter_iccdng->add_pattern("*.icm");
    filter_iccdng->add_pattern("*.ICC");
    filter_iccdng->add_pattern("*.ICM");
#ifdef ART_USE_OCIO
    Glib::RefPtr<Gtk::FileFilter> filter_lut = Gtk::FileFilter::create();
    filter_lut->set_name(M("FILECHOOSER_FILTER_LUT"));
    filter_lut->add_pattern("*.clf");
    filter_lut->add_pattern("*.CLF");
    filter_lut->add_pattern("*.clfz");
    filter_lut->add_pattern("*.CLFZ");
#endif // ART_USE_OCIO
    Glib::RefPtr<Gtk::FileFilter> filter_any = Gtk::FileFilter::create();
    filter_any->set_name(M("FILECHOOSER_FILTER_ANY"));
    filter_any->add_pattern("*");

    ipDialog->add_filter(filter_icc);
    ipDialog->add_filter(filter_iccdng);
#ifdef ART_USE_OCIO
    ipDialog->add_filter(filter_lut);
#endif // ART_USE_OCIO
    ipDialog->add_filter(filter_any);
#ifdef WIN32
    ipDialog->set_show_hidden(true);  // ProgramData is hidden on Windows
#endif

    oldip = "";

    wprofnamesconn = wProfNames->signal_changed().connect(sigc::mem_fun(*this, &ICMPanel::wpChanged));
    oprofnamesconn = oProfNames->signal_changed().connect(sigc::mem_fun(*this, &ICMPanel::opChanged));
    orendintentconn = oRendIntent->signal_changed().connect(sigc::mem_fun(*this, &ICMPanel::oiChanged));
    dcpillconn = dcpIll->signal_changed().connect(sigc::mem_fun(*this, &ICMPanel::dcpIlluminantChanged));

    obpcconn = obpc->signal_toggled().connect(sigc::mem_fun(*this, &ICMPanel::oBPCChanged));
    tcurveconn = ckbToneCurve->signal_toggled().connect(sigc::mem_fun(*this, &ICMPanel::toneCurveChanged));
    ltableconn = ckbApplyLookTable->signal_toggled().connect(sigc::mem_fun(*this, &ICMPanel::applyLookTableChanged));
    beoconn = ckbApplyBaselineExposureOffset->signal_toggled().connect(sigc::mem_fun(*this, &ICMPanel::applyBaselineExposureOffsetChanged));
    hsmconn = ckbApplyHueSatMap->signal_toggled().connect(sigc::mem_fun(*this, &ICMPanel::applyHueSatMapChanged));
    dcp_look_early_->signal_changed().connect(sigc::slot<void>([&]() {
        if (listener) {
            listener->panelChanged(EvDCPApplyLookEarly, dcp_look_early_->get_active_text());
        }
    }));

    icamera->signal_toggled().connect(sigc::mem_fun(*this, &ICMPanel::ipChanged));
    icameraICC->signal_toggled().connect(sigc::mem_fun(*this, &ICMPanel::ipChanged));
    iembedded->signal_toggled().connect(sigc::mem_fun(*this, &ICMPanel::ipChanged));
    ifromfile->signal_toggled().connect(sigc::mem_fun(*this, &ICMPanel::ipChanged));

    ipc = ipDialog->signal_selection_changed().connect(sigc::mem_fun(*this, &ICMPanel::ipSelectionChanged));
    saveRef->signal_pressed().connect(sigc::mem_fun(*this, &ICMPanel::saveReferencePressed));

    show_all();
}

void ICMPanel::updateRenderingIntent(const Glib::ustring &profile)
{
    const uint8_t supportedIntents = rtengine::ICCStore::getInstance()->getOutputIntents(profile);
    const bool supportsPerceptual = supportedIntents & 1 << INTENT_PERCEPTUAL;
    const bool supportsRelative   = supportedIntents & 1 << INTENT_RELATIVE_COLORIMETRIC;
    const bool supportsSaturation = supportedIntents & 1 << INTENT_SATURATION;
    const bool supportsAbsolute   = supportedIntents & 1 << INTENT_ABSOLUTE_COLORIMETRIC;

    //printf("Intents: %d / Perceptual: %d  Relative: %d  Saturation: %d  Absolute: %d\n", supportedIntents, supportsPerceptual, supportsRelative, supportsSaturation, supportsAbsolute);

    if (!profile.empty() && (supportsPerceptual || supportsRelative || supportsSaturation || supportsAbsolute)) {
        oRendIntent->set_sensitive(true);
        oRendIntent->setItemSensitivity(0, supportsPerceptual);
        oRendIntent->setItemSensitivity(1, supportsRelative);
        oRendIntent->setItemSensitivity(2, supportsSaturation);
        oRendIntent->setItemSensitivity(3, supportsAbsolute);
    } else {
        oRendIntent->setItemSensitivity(0, true);
        oRendIntent->setItemSensitivity(1, true);
        oRendIntent->setItemSensitivity(2, true);
        oRendIntent->setItemSensitivity(3, true);
        oRendIntent->set_sensitive(false);
        oRendIntent->setSelected(1);
    }
}

void ICMPanel::updateDCP(int dcpIlluminant, Glib::ustring dcp_name)
{
    ConnectionBlocker dcpillconn_(dcpillconn);

    ckbToneCurve->set_sensitive(false);
    ckbApplyLookTable->set_sensitive(false);
    ckbApplyBaselineExposureOffset->set_sensitive(false);
    ckbApplyHueSatMap->set_sensitive(false);
    dcpIllLabel->set_sensitive(false);
    dcpIll->set_sensitive(false);
    //dcpFrame->set_sensitive(false);

    DCPProfile* dcp = nullptr;

    if (dcp_name == "(cameraICC)") {
        dcp = DCPStore::getInstance()->getCameraProfile(camName);
    } else if (dcp_name == "(embedded)") {
        dcp = DCPStore::getInstance()->getProfile(filename);
    } else if (ifromfile->get_active() && DCPStore::getInstance()->isValidDCPFileName(dcp_name)) {
        dcp = DCPStore::getInstance()->getProfile(dcp_name);
    }

    if (dcp) {
        //dcpFrame->set_sensitive(true);
        dcpFrame->set_visible(true);

        if (dcp->getHasToneCurve()) {
            ckbToneCurve->set_sensitive(true);
        }

        if (dcp->getHasLookTable()) {
            ckbApplyLookTable->set_sensitive(true);
        }

        if (dcp->getHasBaselineExposureOffset()) {
            ckbApplyBaselineExposureOffset->set_sensitive(true);
        }

        if (dcp->getHasHueSatMap()) {
            ckbApplyHueSatMap->set_sensitive(true);
        }

        const DCPProfile::Illuminants illuminants = dcp->getIlluminants();

        if (illuminants.will_interpolate) {
            if (dcpTemperatures[0] != illuminants.temperature_1 || dcpTemperatures[1] != illuminants.temperature_2) {
                char tempstr1[64], tempstr2[64];
                sprintf(tempstr1, "%.0fK", illuminants.temperature_1);
                sprintf(tempstr2, "%.0fK", illuminants.temperature_2);
                int curr_active = dcpIll->get_active_row_number();
                dcpIll->remove_all();
                dcpIll->append(M("TP_ICM_DCPILLUMINANT_INTERPOLATED"));
                dcpIll->append(tempstr1);
                dcpIll->append(tempstr2);
                dcpTemperatures[0] = illuminants.temperature_1;
                dcpTemperatures[1] = illuminants.temperature_2;
                dcpIll->set_active(curr_active);
            }

            if (dcpIlluminant > 2) {
                dcpIlluminant = 0;
            }

            if (dcpIll->get_active_row_number() == -1 && dcpIlluminant == -1) {
                dcpIll->set_active(0);
            } else if (dcpIlluminant >= 0 && dcpIlluminant != dcpIll->get_active_row_number()) {
                dcpIll->set_active(dcpIlluminant);
            }

            dcpIll->set_sensitive(true);
            dcpIllLabel->set_sensitive(true);
        } else {
            if (dcpIll->get_active_row_number() != -1) {
                dcpIll->set_active(-1);
            }
        }
    } else {
        dcpFrame->set_visible(false);
    }

    if (!dcpIllLabel->get_sensitive() && dcpIll->get_active_row_number() != 0) {
        if (dcpTemperatures[0] != 0 || dcpTemperatures[1] != 0) {
            int curr_active = dcpIll->get_active_row_number();
            dcpIll->remove_all();
            dcpIll->append(M("TP_ICM_DCPILLUMINANT_INTERPOLATED"));
            dcpIll->append(M("TP_ICM_DCPILLUMINANT") + " 1");
            dcpIll->append(M("TP_ICM_DCPILLUMINANT") + " 2");

            dcpTemperatures[0] = 0;
            dcpTemperatures[1] = 0;
            dcpIll->set_active(curr_active);
        }
    }
}

void ICMPanel::read(const ProcParams* pp)
{

    disableListener();

    ConnectionBlocker obpcconn_(obpcconn);
    ConnectionBlocker ipc_(ipc);
    ConnectionBlocker tcurveconn_(tcurveconn);
    ConnectionBlocker ltableconn_(ltableconn);
    ConnectionBlocker beoconn_(beoconn);
    ConnectionBlocker hsmconn_(hsmconn);
    ConnectionBlocker wprofnamesconn_(wprofnamesconn);
    ConnectionBlocker oprofnamesconn_(oprofnamesconn);
    ConnectionBlocker orendintentconn_(orendintentconn);
    ConnectionBlocker dcpillconn_(dcpillconn);

    if (pp->icm.inputProfile.substr(0, 5) != "file:" && !ipDialog->get_filename().empty()) {
        ipDialog->set_filename(pp->icm.inputProfile);
    }

    if (pp->icm.inputProfile == "(none)") {
        inone->set_active(true);
        updateDCP(pp->icm.dcpIlluminant, "");
    } else if (pp->icm.inputProfile == "(embedded)" || ((pp->icm.inputProfile == "(camera)" || pp->icm.inputProfile == "") && icamera->get_state() == Gtk::STATE_INSENSITIVE)) {
        iembedded->set_active(true);
        updateDCP(pp->icm.dcpIlluminant, "(embedded)");
    } else if ((pp->icm.inputProfile == "(cameraICC)") && icameraICC->get_state() != Gtk::STATE_INSENSITIVE) {
        icameraICC->set_active(true);
        updateDCP(pp->icm.dcpIlluminant, "(cameraICC)");
    } else if ((pp->icm.inputProfile == "(cameraICC)") && icamera->get_state() != Gtk::STATE_INSENSITIVE && icameraICC->get_state() == Gtk::STATE_INSENSITIVE) {
        // this is the case when (cameraICC) is instructed by packaged profiles, but ICC file is not found
        // therefore falling back UI to explicitly reflect the (camera) option
        icamera->set_active(true);
        updateDCP(pp->icm.dcpIlluminant, "");
    } else if ((pp->icm.inputProfile == "(cameraICC)") && icamera->get_state() == Gtk::STATE_INSENSITIVE && icameraICC->get_state() == Gtk::STATE_INSENSITIVE) {
        // If neither (camera) nor (cameraICC) are available, as is the case when loading a non-raw, activate (embedded).
        iembedded->set_active(true);
        updateDCP(pp->icm.dcpIlluminant, "(cameraICC)");
    } else if ((pp->icm.inputProfile == "(camera)" || pp->icm.inputProfile == "") && icamera->get_state() != Gtk::STATE_INSENSITIVE) {
        icamera->set_active(true);
        updateDCP(pp->icm.dcpIlluminant, "");
    } else {
        ifromfile->set_active(true);
        oldip = pp->icm.inputProfile.substr(5);  // cut of "file:"
        ipDialog->set_filename(pp->icm.inputProfile.substr(5));
        updateDCP(pp->icm.dcpIlluminant, pp->icm.inputProfile.substr(5));
    }

    wProfNames->set_active_text(pp->icm.workingProfile);

    oProfNames->set_active(0);
    for (size_t i = 0; i < out_profiles_.size(); ++i) {
        if (out_profiles_[i] == pp->icm.outputProfile) {
            oProfNames->set_active(i);
            break;
        }
    }

    oRendIntent->setSelected(pp->icm.outputIntent);

    obpc->set_active(pp->icm.outputBPC);
    ckbToneCurve->set_active(pp->icm.toneCurve);
    ckbApplyLookTable->set_active(pp->icm.applyLookTable);
    ckbApplyBaselineExposureOffset->set_active(pp->icm.applyBaselineExposureOffset);
    ckbApplyHueSatMap->set_active(pp->icm.applyHueSatMap);

    use_CAT_->set_active(pp->icm.inputProfileCAT);
    use_CAT_->set_visible(icamera->get_active());

    dcp_look_early_->set_active(pp->icm.dcp_look_early ? 0 : 1);

    enableListener();
}

void ICMPanel::write(ProcParams* pp)
{

    if (inone->get_active()) {
        pp->icm.inputProfile = "(none)";
    } else if (iembedded->get_active()) {
        pp->icm.inputProfile = "(embedded)";
    } else if (icamera->get_active()) {
        pp->icm.inputProfile = "(camera)";
    } else if (icameraICC->get_active()) {
        pp->icm.inputProfile = "(cameraICC)";
    } else {
        if (Glib::file_test(ipDialog->get_filename(), Glib::FILE_TEST_EXISTS) && !Glib::file_test(ipDialog->get_filename(), Glib::FILE_TEST_IS_DIR)) {
            pp->icm.inputProfile = "file:" + ipDialog->get_filename();
        } else {
            pp->icm.inputProfile = "";    // just a directory
        }

        Glib::ustring p = Glib::path_get_dirname(ipDialog->get_filename());
    }

    pp->icm.workingProfile = wProfNames->get_active_text();
    pp->icm.dcpIlluminant = rtengine::max<int>(dcpIll->get_active_row_number(), 0);
    // if (oProfNames->get_active_row_number() == 0) {//M("TP_ICM_NOICM")) {
    //     pp->icm.outputProfile  = ColorManagementParams::NoICMString;
    // } else if (oProfNames->get_active_row_number() == 1) {
    //     pp->icm.outputProfile  = ColorManagementParams::NoProfileString;
    // } else {
    //     pp->icm.outputProfile  = oProfNames->get_active_text();
    // }
    pp->icm.outputProfile = out_profiles_[oProfNames->get_active_row_number()];

    int ointentVal = oRendIntent->getSelected();

    if (ointentVal >= 0 && ointentVal < RI__COUNT) {
        pp->icm.outputIntent  = static_cast<RenderingIntent>(ointentVal);
    } else {
        pp->icm.outputIntent  = rtengine::RI_RELATIVE;
    }

    pp->icm.toneCurve = ckbToneCurve->get_active();
    pp->icm.applyLookTable = ckbApplyLookTable->get_active();
    pp->icm.applyBaselineExposureOffset = ckbApplyBaselineExposureOffset->get_active();
    pp->icm.applyHueSatMap = ckbApplyHueSatMap->get_active();
    pp->icm.outputBPC = obpc->get_active();
    pp->toneCurve.fromHistMatching = false;

    pp->icm.inputProfileCAT = use_CAT_->get_active();

    pp->icm.dcp_look_early = dcp_look_early_->get_active_row_number() == 0;
}

void ICMPanel::setDefaults(const ProcParams* defParams)
{
    initial_params = defParams->icm;
}

void ICMPanel::adjusterChanged(Adjuster* a, double newval)
{
}

void ICMPanel::adjusterAutoToggled(Adjuster* a, bool newval)
{
}

void ICMPanel::wpChanged()
{
    if (listener) {
        listener->panelChanged(EvWProfile, wProfNames->get_active_text());
    }
}

void ICMPanel::dcpIlluminantChanged()
{
    if (listener) {
        listener->panelChanged(EvDCPIlluminant, dcpIll->get_active_text());
    }
}

void ICMPanel::toneCurveChanged()
{
    if (listener) {
        if (ckbToneCurve->get_inconsistent()) {
            listener->panelChanged(EvDCPToneCurve, M("GENERAL_UNCHANGED"));
        } else if (ckbToneCurve->get_active()) {
            listener->panelChanged(EvDCPToneCurve, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvDCPToneCurve, M("GENERAL_DISABLED"));
        }
    }
}

void ICMPanel::applyLookTableChanged()
{
    if (listener) {
        if (ckbApplyLookTable->get_inconsistent()) {
            listener->panelChanged(EvDCPApplyLookTable, M("GENERAL_UNCHANGED"));
        } else if (ckbApplyLookTable->get_active()) {
            listener->panelChanged(EvDCPApplyLookTable, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvDCPApplyLookTable, M("GENERAL_DISABLED"));
        }
    }
}

void ICMPanel::applyBaselineExposureOffsetChanged()
{
    if (listener) {
        if (ckbApplyBaselineExposureOffset->get_inconsistent()) {
            listener->panelChanged(EvDCPApplyBaselineExposureOffset, M("GENERAL_UNCHANGED"));
        } else if (ckbApplyBaselineExposureOffset->get_active()) {
            listener->panelChanged(EvDCPApplyBaselineExposureOffset, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvDCPApplyBaselineExposureOffset, M("GENERAL_DISABLED"));
        }
    }
}

void ICMPanel::applyHueSatMapChanged()
{
    if (listener) {
        if (ckbApplyHueSatMap->get_inconsistent()) {
            listener->panelChanged(EvDCPApplyHueSatMap, M("GENERAL_UNCHANGED"));
        } else if (ckbApplyHueSatMap->get_active()) {
            listener->panelChanged(EvDCPApplyHueSatMap, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvDCPApplyHueSatMap, M("GENERAL_DISABLED"));
        }
    }
}

void ICMPanel::ipChanged()
{

    Glib::ustring profname;

    if (inone->get_active()) {
        profname = "(none)";
    } else if (iembedded->get_active()) {
        profname = "(embedded)";
    } else if (icamera->get_active()) {
        profname = "(camera)";
    } else if (icameraICC->get_active()) {
        profname = "(cameraICC)";
    } else {
        profname = ipDialog->get_filename();
    }

    updateDCP(-1, profname);

    use_CAT_->set_visible(icamera->get_active());

    if (listener && profname != oldip) {
        listener->panelChanged(EvIProfile, profname);
    }

    oldip = profname;
}

void ICMPanel::opChanged()
{
    updateRenderingIntent(out_profiles_[oProfNames->get_active_row_number()]);

    if (listener) {
        listener->panelChanged(EvOProfile, oProfNames->get_active_text());
    }
}

void ICMPanel::oiChanged(int n)
{

    if (listener) {
        Glib::ustring str;

        switch (n) {
            case 0:
                str = M("PREFERENCES_INTENT_PERCEPTUAL");
                break;

            case 1:
                str = M("PREFERENCES_INTENT_RELATIVE");
                break;

            case 2:
                str = M("PREFERENCES_INTENT_SATURATION");
                break;

            case 3:
                str = M("PREFERENCES_INTENT_ABSOLUTE");
                break;

            case 4:
            default:
                str = M("GENERAL_UNCHANGED");
                break;
        }

        listener->panelChanged(EvOIntent, str);
    }
}

void ICMPanel::oBPCChanged()
{
    if (listener) {
        if (obpc->get_inconsistent()) {
            listener->panelChanged(EvOBPCompens, M("GENERAL_UNCHANGED"));
        } else if (obpc->get_active()) {
            listener->panelChanged(EvOBPCompens, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvOBPCompens, M("GENERAL_DISABLED"));
        }
    }
}

void ICMPanel::setRawMeta(bool raw, const rtengine::FramesData* pMeta)
{

    disableListener();

    icamera->set_active(raw);
    iembedded->set_active(!raw);
    icamera->set_sensitive(raw);
    camName = pMeta->getCamera();
    filename = pMeta->getFileName();
    icameraICC->set_sensitive(raw && (ICCStore::getInstance()->getCameraProfile(pMeta->getCamera()) != nullptr || DCPStore::getInstance()->getCameraProfile(pMeta->getCamera()) != nullptr));
    iembedded->set_sensitive(!raw || DCPStore::getInstance()->getProfile(filename));

    enableListener();
}

void ICMPanel::ipSelectionChanged()
{

    if (ipDialog->get_filename() == "") {
        return;
    }

    ipChanged();
}

void ICMPanel::saveReferencePressed()
{

    if (!icmplistener) {
        return;
    }

    Gtk::FileChooserDialog dialog(getToplevelWindow(this), M("TP_ICM_SAVEREFERENCE"), Gtk::FILE_CHOOSER_ACTION_SAVE);
    bindCurrentFolder(dialog, options.lastProfilingReferenceDir);
    dialog.set_current_name(lastRefFilename);

    dialog.add_button(M("GENERAL_CANCEL"), Gtk::RESPONSE_CANCEL);
    dialog.add_button(M("GENERAL_SAVE"), Gtk::RESPONSE_OK);

    Gtk::CheckButton applyWB(M("TP_ICM_SAVEREFERENCE_APPLYWB"));
    applyWB.set_tooltip_text(M("TP_ICM_SAVEREFERENCE_APPLYWB_TOOLTIP"));
    Gtk::HBox* hbox = Gtk::manage(new Gtk::HBox());
    hbox->pack_end(applyWB, Gtk::PACK_SHRINK, 2);
    Gtk::Box *box = dialog.get_content_area();
    box->pack_end(*hbox, Gtk::PACK_SHRINK, 2);

    Glib::RefPtr<Gtk::FileFilter> filter_tif = Gtk::FileFilter::create();
    filter_tif->set_name(M("FILECHOOSER_FILTER_TIFF"));
    filter_tif->add_pattern("*.tif");
    filter_tif->add_pattern("*.tiff");
    dialog.add_filter(filter_tif);

    Glib::RefPtr<Gtk::FileFilter> filter_any = Gtk::FileFilter::create();
    filter_any->set_name(M("FILECHOOSER_FILTER_ANY"));
    filter_any->add_pattern("*");
    dialog.add_filter(filter_any);

    dialog.show_all_children();
    //dialog.set_do_overwrite_confirmation (true);

    bool done = false;

    do {
        int result = dialog.run();

        if (result != Gtk::RESPONSE_OK) {
            done = true;
        } else {
            std::string fname = dialog.get_filename();
            Glib::ustring ext = getExtension(fname);

            if (ext != "tif" && ext != "tiff") {
                fname += ".tif";
            }

            if (confirmOverwrite(dialog, fname)) {
                icmplistener->saveInputICCReference(fname, applyWB.get_active());
                lastRefFilename = Glib::path_get_basename(fname);
                done = true;
            }
        }
    } while (!done);

    return;
}


void ICMPanel::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.icm = initial_params;
    }
    read(&pp);
}
