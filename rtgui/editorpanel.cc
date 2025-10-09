/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>
 *  Copyright (c) 2010 Oliver Duis <www.oliverduis.de>
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
#include "editorpanel.h"

#include <iostream>

#include "../rtengine/imagesource.h"
#include "../rtengine/iccstore.h"
#include "soundman.h"
#include "rtimage.h"
#include "rtwindow.h"
#include "guiutils.h"
#include "popupbutton.h"
#include "options.h"
#include "progressconnector.h"
#include "procparamchangers.h"
#include "placesbrowser.h"
#include "fastexport.h"
#include "../rtengine/imgiomanager.h"
#include "../rtengine/improccoordinator.h"
#include "../rtengine/processingjob.h"

using namespace rtengine::procparams;
using ScopeType = Options::ScopeType;

namespace {

void setprogressStrUI(double val, const Glib::ustring str, MyProgressBar* pProgress)
{
    if (!str.empty()) {
        pProgress->set_text(M(str));
    }

    if (val >= 0.0) {
        pProgress->set_fraction(val);
    }
}

} // namespace


class EditorPanel::ColorManagementToolbar {
private:
    MyComboBoxText profileBox;
    PopUpButton intentBox;
    Gtk::ToggleButton softProof;
    Gtk::ToggleButton spGamutCheck;
    Gtk::ToggleButton spGamutCheckMonitor;
    sigc::connection profileConn, intentConn, softproofConn;
    Glib::ustring defprof;

    std::shared_ptr<rtengine::StagedImageProcessor> &processor;
    EditorPanel *parent;

private:
    void prepareProfileBox()
    {
        if (rtengine::Settings::color_mgmt_mode == rtengine::Settings::ColorManagementMode::APPLICATION) {
            const std::vector<Glib::ustring> profiles = rtengine::ICCStore::getInstance()->getProfilesFromDir(options.rtSettings.monitorIccDirectory);
//        rtengine::ICCStore::getInstance()->setDefaultMonitorProfileName(rtengine::ICCStore::getInstance()->getDefaultMonitorProfileName());

            profileBox.setPreferredWidth (70, 200);
            setExpandAlignProperties (&profileBox, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_FILL);

            profileBox.append (M ("PREFERENCES_PROFILE_NONE"));
            Glib::ustring defprofname;

            //if (find_default_monitor_profile (profileBox.get_root_window()->gobj(), defprof, defprofname)) {
            if (getSystemDefaultMonitorProfile(profileBox.get_root_window()->gobj(), defprof, defprofname)) {
                profileBox.append(M ("MONITOR_PROFILE_SYSTEM") + " (" + defprofname + ")");

                if (options.rtSettings.autoMonitorProfile) {
                    //rtengine::ICCStore::getInstance()->setDefaultMonitorProfileName (defprof);
                    profileBox.set_active (1);
                } else {
                    profileBox.set_active (0);
                }
            } else {
                profileBox.set_active (0);
            }

            for (const auto &profile : profiles) {
                profileBox.append(profile);
            }

            profileBox.set_tooltip_text (profileBox.get_active_text ());
        }
    }

    void prepareIntentBox ()
    {
        if (rtengine::Settings::color_mgmt_mode == rtengine::Settings::ColorManagementMode::APPLICATION) {
            // same order as the enum
            intentBox.addEntry ("intent-perceptual.svg", M ("PREFERENCES_INTENT_PERCEPTUAL"));
            intentBox.addEntry ("intent-relative.svg", M ("PREFERENCES_INTENT_RELATIVE"));
            intentBox.addEntry ("intent-absolute.svg", M ("PREFERENCES_INTENT_ABSOLUTE"));
            setExpandAlignProperties (intentBox.buttonGroup, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_FILL);

            intentBox.setSelected (1);
            intentBox.show ();
        }
    }

    void prepareSoftProofingBox ()
    {
        Gtk::Image *softProofImage = Gtk::manage (new RTImage ("gamut-softproof.svg"));
        softProofImage->set_padding (0, 0);
        softProof.add (*softProofImage);
        softProof.set_relief (Gtk::RELIEF_NONE);
        softProof.set_tooltip_markup (M ("SOFTPROOF_TOOLTIP"));

        softProof.set_active (false);
        softProof.show ();

        Gtk::Image *spGamutCheckImage = Gtk::manage (new RTImage ("gamut-warning.svg"));
        spGamutCheckImage->set_padding (0, 0);
        spGamutCheck.add (*spGamutCheckImage);
        spGamutCheck.set_relief (Gtk::RELIEF_NONE);
        spGamutCheck.set_tooltip_markup (M ("SOFTPROOF_GAMUTCHECK_TOOLTIP"));

        spGamutCheck.set_active (false);
        spGamutCheck.set_sensitive (true);
        spGamutCheck.show ();

        spGamutCheckImage = Gtk::manage(new RTImage("gamut-warning-monitor.svg"));
        spGamutCheckImage->set_padding(0, 0);
        spGamutCheckMonitor.add(*spGamutCheckImage);
        spGamutCheckMonitor.set_relief(Gtk::RELIEF_NONE);
        spGamutCheckMonitor.set_tooltip_markup(M("SOFTPROOF_GAMUTCHECK_MONITOR_TOOLTIP"));

        spGamutCheckMonitor.set_active(false);
        spGamutCheckMonitor.set_sensitive(true);
        spGamutCheckMonitor.show();
    }

    void profileBoxChanged ()
    {
        updateParameters ();
    }

    void intentBoxChanged (int)
    {
        updateParameters ();
    }

    void softProofToggled ()
    {
        updateSoftProofParameters ();
    }

    bool softProofPressed(GdkEventButton *event)
    {
        bool active = softProof.get_active();
        bool show_dialog = (event->state & GDK_CONTROL_MASK);
        if (active && !show_dialog) {
            return false;
        } else {
            if (show_dialog) {
                Gtk::FileChooserDialog dialog(getToplevelWindow(parent), M("PREFERENCES_PRTPROFILE"), Gtk::FILE_CHOOSER_ACTION_OPEN);
                bindCurrentFolder(dialog, options.rtSettings.monitorIccDirectory);
                auto filter_icc = Gtk::FileFilter::create();
                filter_icc->set_name(M("FILECHOOSER_FILTER_COLPROF"));
                filter_icc->add_pattern("*.icc");
                filter_icc->add_pattern("*.icm");
                filter_icc->add_pattern("*.ICC");
                filter_icc->add_pattern("*.ICM");
                dialog.add_filter(filter_icc);
                dialog.add_button(M("GENERAL_CANCEL"), Gtk::RESPONSE_CANCEL);
                dialog.add_button(M("GENERAL_OPEN"), Gtk::RESPONSE_OK);
                if (dialog.run() == Gtk::RESPONSE_OK) {
                    auto fname = dialog.get_filename();
                    options.rtSettings.printerProfile = "file:" + fname;
                } else {
                    return true;
                }
            }
            auto iccs = rtengine::ICCStore::getInstance();
            auto prof = iccs->getProfile(options.rtSettings.printerProfile);
            if (!prof) {
                auto name = options.rtSettings.printerProfile;
                if (name.empty()) {
                    name = "(" + M("PREFERENCES_PROFILE_NONE") + ")";
                } else if (name.find("file:") == 0) {
                    name = name.substr(5);
                }
                parent->error(Glib::ustring::compose(M("ERROR_MSG_INVALID_PROFILE"), name));
                return true;
            } else {
                softProof.set_active(true);
                return true;
            }
        }
    }

    void spGamutCheckToggled ()
    {
        if (spGamutCheck.get_active() && spGamutCheckMonitor.get_active()) {
            spGamutCheckMonitor.set_active(false);
        } else {
            updateSoftProofParameters ();
        }
    }

    void spGamutCheckMonitorToggled ()
    {
        if (spGamutCheck.get_active() && spGamutCheckMonitor.get_active()) {
            spGamutCheck.set_active(false);
        } else {
            updateSoftProofParameters ();
        }
    }

    void updateParameters (bool noEvent = false)
    {
        Glib::ustring profile;
        rtengine::RenderingIntent intent = rtengine::RI_RELATIVE;
        
        if (rtengine::Settings::color_mgmt_mode == rtengine::Settings::ColorManagementMode::APPLICATION) {
            ConnectionBlocker profileBlocker (profileConn);
            ConnectionBlocker intentBlocker (intentConn);


            if (!defprof.empty() && profileBox.get_active_row_number () == 1) {
                profile = defprof;

                if (profile.empty ()) {
                    profile = options.rtSettings.monitorProfile;
                }

                if (profile.empty ()) {
                    profile = "sRGB";
                }
            } else if (profileBox.get_active_row_number () > 0) {
                profile = profileBox.get_active_text ();
            }

            if (profileBox.get_active_row_number () == 0) {

                profile.clear ();

                intentBox.set_sensitive (false);
                intentBox.setSelected (1);
                //softProof.set_sensitive (false);
                //spGamutCheck.set_sensitive (false);
                spGamutCheckMonitor.set_sensitive (false);

                profileBox.set_tooltip_text ("");

            } else {
                const uint8_t supportedIntents = rtengine::ICCStore::getInstance()->getProofIntents (profile);
                const bool supportsRelativeColorimetric = supportedIntents & 1 << INTENT_RELATIVE_COLORIMETRIC;
                const bool supportsPerceptual = supportedIntents & 1 << INTENT_PERCEPTUAL;
                const bool supportsAbsoluteColorimetric = supportedIntents & 1 << INTENT_ABSOLUTE_COLORIMETRIC;

                if (supportsPerceptual || supportsRelativeColorimetric || supportsAbsoluteColorimetric) {
                    intentBox.set_sensitive (true);
                    intentBox.setItemSensitivity (0, supportsPerceptual);
                    intentBox.setItemSensitivity (1, supportsRelativeColorimetric);
                    intentBox.setItemSensitivity (2, supportsAbsoluteColorimetric);
                    //softProof.set_sensitive (true);
                    //spGamutCheck.set_sensitive (true);
                } else {
                    intentBox.setItemSensitivity (0, true);
                    intentBox.setItemSensitivity (1, true);
                    intentBox.setItemSensitivity (2, true);
                    intentBox.set_sensitive (false);
                    intentBox.setSelected (1);
                }
                spGamutCheck.set_sensitive(true);
                spGamutCheckMonitor.set_sensitive(true);

                profileBox.set_tooltip_text (profileBox.get_active_text ());

            }

            switch (intentBox.getSelected ()) {
            default:
            case 0:
                intent = rtengine::RI_PERCEPTUAL;
                break;
            case 1:
                intent = rtengine::RI_RELATIVE;
                break;
            case 2:
                intent = rtengine::RI_ABSOLUTE;
                break;
            }
        }

        if (!processor) {
            return;
        }

        if (!noEvent) {
            processor->beginUpdateParams ();
        }

        if (rtengine::Settings::color_mgmt_mode == rtengine::Settings::ColorManagementMode::APPLICATION) {
            processor->setMonitorProfile(profile, intent);
        }
        
        rtengine::GamutCheck gc = rtengine::GAMUT_CHECK_OFF;
        if (spGamutCheck.get_sensitive() && spGamutCheck.get_active()) {
            gc = rtengine::GAMUT_CHECK_OUTPUT;
        } else if (spGamutCheckMonitor.get_sensitive() && spGamutCheckMonitor.get_active()) {
            gc = rtengine::GAMUT_CHECK_MONITOR;
        }
        processor->setSoftProofing (softProof.get_sensitive() && softProof.get_active(), gc);

        if (!noEvent) {
            processor->endUpdateParams (rtengine::EvMonitorTransform);
        }
    }

    void updateSoftProofParameters (bool noEvent = false)
    {
        bool profile_active = profileBox.get_active_row_number() > 0;
        bool check_is_active = rtengine::Settings::color_mgmt_mode != rtengine::Settings::ColorManagementMode::APPLICATION || profile_active;
        spGamutCheck.set_sensitive(check_is_active);

        if (check_is_active) {
            if (processor) {
                if (!noEvent) {
                    processor->beginUpdateParams ();
                }

                rtengine::GamutCheck gc = rtengine::GAMUT_CHECK_OFF;
                if (spGamutCheck.get_sensitive() && spGamutCheck.get_active()) {
                    gc = rtengine::GAMUT_CHECK_OUTPUT;
                } else if (spGamutCheckMonitor.get_sensitive() && spGamutCheckMonitor.get_active()) {
                    gc = rtengine::GAMUT_CHECK_MONITOR;
                }
                processor->setSoftProofing(softProof.get_sensitive() && softProof.get_active(), gc);

                if (!noEvent) {
                    processor->endUpdateParams (rtengine::EvMonitorTransform);
                }
            }
        }
    }

public:
    explicit ColorManagementToolbar(EditorPanel *p, std::shared_ptr<rtengine::StagedImageProcessor> &ipc):
        intentBox(Glib::ustring (), true),
        processor(ipc),
        parent(p)
    {
        prepareProfileBox ();
        prepareIntentBox ();
        prepareSoftProofingBox ();

        reset ();

        //softproofConn =
        softProof.signal_toggled().connect(sigc::mem_fun (this, &ColorManagementToolbar::softProofToggled));
        softproofConn = softProof.signal_button_release_event().connect(sigc::mem_fun(*this, &ColorManagementToolbar::softProofPressed), false);

        spGamutCheck.signal_toggled().connect (sigc::mem_fun (this, &ColorManagementToolbar::spGamutCheckToggled));
        spGamutCheckMonitor.signal_toggled().connect (sigc::mem_fun (this, &ColorManagementToolbar::spGamutCheckMonitorToggled));

        if (rtengine::Settings::color_mgmt_mode == rtengine::Settings::ColorManagementMode::APPLICATION) {
            profileConn = profileBox.signal_changed ().connect (sigc::mem_fun (this, &ColorManagementToolbar::profileBoxChanged));
            intentConn = intentBox.signal_changed ().connect (sigc::mem_fun (this, &ColorManagementToolbar::intentBoxChanged));
        }
    }

    void pack_right_in (Gtk::Grid* grid)
    {
        if (rtengine::Settings::color_mgmt_mode == rtengine::Settings::ColorManagementMode::APPLICATION) {
            grid->attach_next_to (profileBox, Gtk::POS_RIGHT, 1, 1);
            grid->attach_next_to (*intentBox.buttonGroup, Gtk::POS_RIGHT, 1, 1);
        }
        grid->attach_next_to (softProof, Gtk::POS_RIGHT, 1, 1);
        grid->attach_next_to (spGamutCheck, Gtk::POS_RIGHT, 1, 1);
        grid->attach_next_to(spGamutCheckMonitor, Gtk::POS_RIGHT, 1, 1);
    }

    void updateProcessor()
    {
        if (processor) {
            updateParameters (true);
        }
    }

    void reset ()
    {
        if (rtengine::Settings::color_mgmt_mode == rtengine::Settings::ColorManagementMode::APPLICATION) {
            ConnectionBlocker intentBlocker (intentConn);
            ConnectionBlocker profileBlocker (profileConn);

            if (!defprof.empty() && options.rtSettings.autoMonitorProfile) {
                profileBox.set_active (1);
            } else {
                setActiveTextOrIndex (profileBox, options.rtSettings.monitorProfile, 0);
            }

            switch (options.rtSettings.monitorIntent) {
            default:
            case rtengine::RI_PERCEPTUAL:
                intentBox.setSelected (0);
                break;

            case rtengine::RI_RELATIVE:
                intentBox.setSelected (1);
                break;

            case rtengine::RI_ABSOLUTE:
                intentBox.setSelected (2);
                break;
            }
        }

        updateParameters();
    }

    void updateHistogram()
    {
        updateParameters();
    }


    void defaultMonitorProfileChanged (const Glib::ustring &profile_name, bool auto_monitor_profile)
    {
        if (rtengine::Settings::color_mgmt_mode == rtengine::Settings::ColorManagementMode::APPLICATION) {
            ConnectionBlocker profileBlocker (profileConn);

            if (auto_monitor_profile && !defprof.empty()) {
                rtengine::ICCStore::getInstance()->setDefaultMonitorProfileName (defprof);
                profileBox.set_active (1);
            } else {
                rtengine::ICCStore::getInstance()->setDefaultMonitorProfileName (profile_name);
                setActiveTextOrIndex (profileBox, profile_name, 0);
            }
        }
    }
};

EditorPanel::EditorPanel (FilePanel* filePanel)
    : catalogPane (nullptr), realized (false), tbBeforeLock (nullptr), iHistoryShow (nullptr), iHistoryHide (nullptr),
      iTopPanel_1_Show (nullptr), iTopPanel_1_Hide (nullptr), iRightPanel_1_Show (nullptr), iRightPanel_1_Hide (nullptr),
      iBeforeLockON (nullptr), iBeforeLockOFF (nullptr), previewHandler (nullptr), beforePreviewHandler (nullptr),
      beforeIarea (nullptr), beforeBox (nullptr), afterBox (nullptr), beforeLabel (nullptr), afterLabel (nullptr),
      beforeHeaderBox (nullptr), afterHeaderBox (nullptr), parent (nullptr), parentWindow (nullptr), openThm (nullptr),
      selectedFrame(0), isrc (nullptr), ipc (nullptr), beforeIpc (nullptr), err (0), isProcessing (false),
      histogram_observable(nullptr), histogram_scope_type(ScopeType::NONE)      
{

    epih = new EditorPanelIdleHelper;
    epih->epanel = this;
    epih->destroyed = false;
    epih->pending = 0;
    //rtengine::befaf=true;
    processingStartedTime = 0;
    firstProcessingDone = false;

    // construct toolpanelcoordinator
    tpc = new ToolPanelCoordinator();
    tpc->setProgressListener(this);

    // build GUI

    // build left side panel
    leftbox = new Gtk::Paned (Gtk::ORIENTATION_VERTICAL);

    // make a subbox to allow resizing of the histogram (if it's on the left)
    leftsubbox = new Gtk::Box (Gtk::ORIENTATION_VERTICAL);
    leftsubbox->set_size_request (230, 250);

    histogramPanel = nullptr;

    profilep = Gtk::manage (new ProfilePanel ());
    ppframe = new Gtk::Frame ();
    ppframe->set_name ("ProfilePanel");
    ppframe->add (*profilep);
    ppframe->set_label (M ("PROFILEPANEL_LABEL"));
    //leftsubbox->pack_start (*ppframe, Gtk::PACK_SHRINK, 4);

    navigator = Gtk::manage (new Navigator ());
    navigator->previewWindow->set_size_request (-1, 150 * RTScalable::getScale());
    leftsubbox->pack_start (*navigator, Gtk::PACK_SHRINK, 2);

    history = Gtk::manage (new History ());
    leftsubbox->pack_start (*history);

    leftsubbox->show_all ();

    leftbox->pack2 (*leftsubbox, true, true);
    leftbox->show_all ();

    // build the middle of the screen
    Gtk::Box* editbox = Gtk::manage (new Gtk::Box (Gtk::ORIENTATION_VERTICAL));

    info = Gtk::manage (new Gtk::ToggleButton ());
    Gtk::Image* infoimg = Gtk::manage (new RTImage ("info.svg"));
    info->add (*infoimg);
    info->set_relief (Gtk::RELIEF_NONE);
    info->set_tooltip_markup (M ("MAIN_TOOLTIP_QINFO"));

    beforeAfter = Gtk::manage (new Gtk::ToggleButton ());
    Gtk::Image* beforeAfterIcon = Gtk::manage (new RTImage ("beforeafter.svg"));
    beforeAfter->add (*beforeAfterIcon);
    beforeAfter->set_relief (Gtk::RELIEF_NONE);
    beforeAfter->set_tooltip_markup (M ("MAIN_TOOLTIP_TOGGLE"));

    iBeforeLockON = new RTImage ("padlock-locked-small.svg");
    iBeforeLockOFF = new RTImage ("padlock-unlocked-small.svg");

    Gtk::VSeparator* vsept = Gtk::manage (new Gtk::VSeparator ());
    Gtk::VSeparator* vsepz = Gtk::manage (new Gtk::VSeparator ());
    Gtk::VSeparator* vsepi = Gtk::manage (new Gtk::VSeparator ());
    Gtk::VSeparator* vseph = Gtk::manage (new Gtk::VSeparator ());

    hidehp = Gtk::manage (new Gtk::ToggleButton ());

    iHistoryShow = new RTImage ("panel-to-right.svg");
    iHistoryHide = new RTImage ("panel-to-left.svg");

    hidehp->set_relief (Gtk::RELIEF_NONE);
    hidehp->set_active (options.showHistory);
    hidehp->set_tooltip_markup (M ("MAIN_TOOLTIP_HIDEHP"));

    if (options.showHistory) {
        hidehp->set_image (*iHistoryHide);
    } else {
        hidehp->set_image (*iHistoryShow);
    }

    tbTopPanel_1 = nullptr;

    if (!simpleEditor && filePanel) {
        tbTopPanel_1 = new Gtk::ToggleButton ();
        iTopPanel_1_Show = new RTImage ("panel-to-bottom.svg");
        iTopPanel_1_Hide = new RTImage ("panel-to-top.svg");
        if (options.filmstripBottom) {
            std::swap(iTopPanel_1_Show, iTopPanel_1_Hide);
        }
        tbTopPanel_1->set_relief (Gtk::RELIEF_NONE);
        tbTopPanel_1->set_active (true);
        tbTopPanel_1->set_tooltip_markup (M ("MAIN_TOOLTIP_SHOWHIDETP1"));
        tbTopPanel_1->set_image (*iTopPanel_1_Hide);
    }

    Gtk::VSeparator* vsepcl = Gtk::manage (new Gtk::VSeparator ());
    Gtk::VSeparator* vsepz2 = Gtk::manage (new Gtk::VSeparator ());
    Gtk::VSeparator* vsepz3 = Gtk::manage (new Gtk::VSeparator ());
    Gtk::VSeparator* vsepz4 = Gtk::manage (new Gtk::VSeparator ());

    Gtk::VSeparator* vsep1 = Gtk::manage (new Gtk::VSeparator ());
    Gtk::VSeparator* vsep2 = Gtk::manage (new Gtk::VSeparator ());

    // Histogram profile toggle controls
    toggleHistogramProfile = Gtk::manage (new Gtk::ToggleButton ());
    Gtk::Image* histProfImg = Gtk::manage (new RTImage ("gamut-hist.svg"));
    toggleHistogramProfile->add (*histProfImg);
    toggleHistogramProfile->set_relief (Gtk::RELIEF_NONE);
    toggleHistogramProfile->set_active (options.rtSettings.HistogramWorking);
    toggleHistogramProfile->set_tooltip_markup ( (M ("PREFERENCES_HISTOGRAM_TOOLTIP")));

    Gtk::VSeparator* vsep3 = Gtk::manage (new Gtk::VSeparator ());

    iareapanel = new ImageAreaPanel ();
    tpc->setEditProvider (iareapanel->imageArea);
    tpc->getToolBar()->setLockablePickerToolListener (iareapanel->imageArea);

    Gtk::Box* toolBarPanel = Gtk::manage (new Gtk::Box (Gtk::ORIENTATION_HORIZONTAL));
    toolBarPanel->set_name ("EditorTopPanel");
    toolBarPanel->pack_start (*hidehp, Gtk::PACK_SHRINK, 1);
    toolBarPanel->pack_start (*vseph, Gtk::PACK_SHRINK, 2);
    toolBarPanel->pack_start (*info, Gtk::PACK_SHRINK, 1);
    toolBarPanel->pack_start (*beforeAfter, Gtk::PACK_SHRINK, 1);
    toolBarPanel->pack_start (*vsepi, Gtk::PACK_SHRINK, 2);
    toolBarPanel->pack_start (*tpc->getToolBar(), Gtk::PACK_SHRINK, 1);
    toolBarPanel->pack_start (*vsept, Gtk::PACK_SHRINK, 2);

    if (tbTopPanel_1) {
        Gtk::VSeparator* vsep = Gtk::manage (new Gtk::VSeparator ());
        toolBarPanel->pack_end   (*tbTopPanel_1, Gtk::PACK_SHRINK, 1);
        toolBarPanel->pack_end   (*vsep, Gtk::PACK_SHRINK, 2);
    }

    toolBarPanel->pack_end   (*tpc->coarse, Gtk::PACK_SHRINK, 2);
    toolBarPanel->pack_end   (*vsepcl, Gtk::PACK_SHRINK, 2);
    // Histogram profile toggle
    toolBarPanel->pack_end (*toggleHistogramProfile, Gtk::PACK_SHRINK, 1);
    toolBarPanel->pack_end (*vsep3, Gtk::PACK_SHRINK, 2);

    toolBarPanel->pack_end   (*iareapanel->imageArea->indClippedPanel, Gtk::PACK_SHRINK, 0);
    toolBarPanel->pack_end   (*vsepz, Gtk::PACK_SHRINK, 2);
    toolBarPanel->pack_end   (*iareapanel->imageArea->previewModePanel, Gtk::PACK_SHRINK, 0);
    toolBarPanel->pack_end   (*vsepz4, Gtk::PACK_SHRINK, 2);

    afterBox = Gtk::manage (new Gtk::Box (Gtk::ORIENTATION_VERTICAL));
    afterBox->pack_start (*iareapanel);

    beforeAfterBox = Gtk::manage (new Gtk::Box (Gtk::ORIENTATION_HORIZONTAL));
    beforeAfterBox->set_name ("BeforeAfterContainer");
    beforeAfterBox->pack_start (*afterBox);

    MyScrolledToolbar *stb1 = Gtk::manage(new MyScrolledToolbar());
    stb1->set_name("EditorToolbarTop");
    stb1->add(*toolBarPanel);
    editbox->pack_start (*stb1, Gtk::PACK_SHRINK, 2);
    editbox->pack_start (*beforeAfterBox);

    // build right side panel
    vboxright = new Gtk::Paned (Gtk::ORIENTATION_VERTICAL);

    vsubboxright = new Gtk::Box (Gtk::ORIENTATION_VERTICAL, 0);
    vsubboxright->set_size_request (300, 250);

    vsubboxright->pack_start (*ppframe, Gtk::PACK_SHRINK, 2);
    // main notebook
    vsubboxright->pack_start (*tpc->toolPanelNotebook);

    vboxright->pack2 (*vsubboxright, true, true);

    // Save buttons
    Gtk::Grid *iops = new Gtk::Grid ();
    iops->set_name ("IopsPanel");
    iops->set_orientation (Gtk::ORIENTATION_HORIZONTAL);
    iops->set_row_spacing (2);
    iops->set_column_spacing (2);

    Gtk::Image *saveButtonImage =  Gtk::manage (new RTImage ("save.svg"));
    saveimgas = Gtk::manage (new Gtk::Button ());
    saveimgas->set_relief(Gtk::RELIEF_NONE);
    saveimgas->add (*saveButtonImage);
    saveimgas->set_tooltip_markup (M ("MAIN_BUTTON_SAVE_TOOLTIP"));
    setExpandAlignProperties (saveimgas, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_FILL);

    Gtk::Image *queueButtonImage = Gtk::manage (new RTImage ("gears.svg"));
    queueimg = Gtk::manage (new Gtk::Button ());
    queueimg->set_relief(Gtk::RELIEF_NONE);
    queueimg->add (*queueButtonImage);
    queueimg->set_tooltip_markup (M ("MAIN_BUTTON_PUTTOQUEUE_TOOLTIP"));
    setExpandAlignProperties (queueimg, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_FILL);

    Gtk::Image *sendToEditorButtonImage = Gtk::manage (new RTImage ("palette-brush.svg"));
    sendtogimp = Gtk::manage (new Gtk::Button ());
    sendtogimp->set_relief(Gtk::RELIEF_NONE);
    sendtogimp->add (*sendToEditorButtonImage);
    sendtogimp->set_tooltip_markup (M ("MAIN_BUTTON_SENDTOEDITOR_TOOLTIP"));
    setExpandAlignProperties (sendtogimp, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_FILL);

    // Status box
    progressLabel = Gtk::manage (new MyProgressBar (300));
    progressLabel->set_show_text (true);
    setExpandAlignProperties (progressLabel, true, false, Gtk::ALIGN_START, Gtk::ALIGN_FILL);
    progressLabel->set_fraction (0.0);

    // tbRightPanel_1
    tbRightPanel_1 = new Gtk::ToggleButton ();
    iRightPanel_1_Show = new RTImage ("panel-to-left.svg");
    iRightPanel_1_Hide = new RTImage ("panel-to-right.svg");
    tbRightPanel_1->set_relief (Gtk::RELIEF_NONE);
    tbRightPanel_1->set_active (true);
    tbRightPanel_1->set_tooltip_markup (M ("MAIN_TOOLTIP_SHOWHIDERP1"));
    tbRightPanel_1->set_image (*iRightPanel_1_Hide);
    setExpandAlignProperties (tbRightPanel_1, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_FILL);

    // ShowHideSidePanels
    tbShowHideSidePanels = new Gtk::ToggleButton ();
    iShowHideSidePanels = new RTImage ("crossed-arrows-out.svg");
    iShowHideSidePanels_exit = new RTImage ("crossed-arrows-in.svg");
    tbShowHideSidePanels->set_relief (Gtk::RELIEF_NONE);
    tbShowHideSidePanels->set_active (false);
    tbShowHideSidePanels->set_tooltip_markup (M ("MAIN_BUTTON_SHOWHIDESIDEPANELS_TOOLTIP"));
    tbShowHideSidePanels->set_image (*iShowHideSidePanels);
    setExpandAlignProperties (tbShowHideSidePanels, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_FILL);

    navPrev = navNext = navSync = nullptr;

    if (!simpleEditor && !options.tabbedUI) {
        // Navigation buttons
        Gtk::Image *navPrevImage = Gtk::manage (new RTImage ("arrow2-left.svg"));
        navPrevImage->set_padding (0, 0);
        navPrev = Gtk::manage (new Gtk::Button ());
        navPrev->add (*navPrevImage);
        navPrev->set_relief (Gtk::RELIEF_NONE);
        navPrev->set_tooltip_markup (M ("MAIN_BUTTON_NAVPREV_TOOLTIP"));
        setExpandAlignProperties (navPrev, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_FILL);

        Gtk::Image *navNextImage = Gtk::manage (new RTImage ("arrow2-right.svg"));
        navNextImage->set_padding (0, 0);
        navNext = Gtk::manage (new Gtk::Button ());
        navNext->add (*navNextImage);
        navNext->set_relief (Gtk::RELIEF_NONE);
        navNext->set_tooltip_markup (M ("MAIN_BUTTON_NAVNEXT_TOOLTIP"));
        setExpandAlignProperties (navNext, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_FILL);

        Gtk::Image *navSyncImage = Gtk::manage (new RTImage ("arrow-updown.svg"));
        navSyncImage->set_padding (0, 0);
        navSync = Gtk::manage (new Gtk::Button ());
        navSync->add (*navSyncImage);
        navSync->set_relief (Gtk::RELIEF_NONE);
        navSync->set_tooltip_markup (M ("MAIN_BUTTON_NAVSYNC_TOOLTIP"));
        setExpandAlignProperties (navSync, false, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_FILL);
    }

    // ==================  PACKING THE BOTTOM WIDGETS =================

    // Adding widgets from center to the left, on the left side (using Gtk::POS_LEFT)
    iops->attach_next_to (*vsep2, Gtk::POS_LEFT, 1, 1);
    iops->attach_next_to (*progressLabel, Gtk::POS_LEFT, 1, 1);
    iops->attach_next_to (*vsep1, Gtk::POS_LEFT, 1, 1);

    if (!gimpPlugin) {
        iops->attach_next_to (*sendtogimp, Gtk::POS_LEFT, 1, 1);
    }

    if (!gimpPlugin && !simpleEditor) {
        iops->attach_next_to (*queueimg, Gtk::POS_LEFT, 1, 1);
    }

    if (!gimpPlugin) {
        iops->attach_next_to (*saveimgas, Gtk::POS_LEFT, 1, 1);
    }


    // Color management toolbar
    colorMgmtToolBar.reset(new ColorManagementToolbar(this, ipc));
    colorMgmtToolBar->pack_right_in (iops);

    if (!simpleEditor && !options.tabbedUI) {
        Gtk::VSeparator* vsep3 = Gtk::manage (new Gtk::VSeparator ());
        iops->attach_next_to (*vsep3, Gtk::POS_RIGHT, 1, 1);
        iops->attach_next_to (*navPrev, Gtk::POS_RIGHT, 1, 1);
        iops->attach_next_to (*navSync, Gtk::POS_RIGHT, 1, 1);
        iops->attach_next_to (*navNext, Gtk::POS_RIGHT, 1, 1);
    }

    iops->attach_next_to (*vsepz2, Gtk::POS_RIGHT, 1, 1);
    iops->attach_next_to (*iareapanel->imageArea->zoomPanel, Gtk::POS_RIGHT, 1, 1);
    iops->attach_next_to (*vsepz3, Gtk::POS_RIGHT, 1, 1);
    iops->attach_next_to (*tbShowHideSidePanels, Gtk::POS_RIGHT, 1, 1);
    iops->attach_next_to (*tbRightPanel_1, Gtk::POS_RIGHT, 1, 1);

    MyScrolledToolbar *stb2 = Gtk::manage(new MyScrolledToolbar());
    stb2->set_name("EditorToolbarBottom");
    stb2->add(*iops);

    editbox->show_all ();

    Gtk::VBox *vb = Gtk::manage(new Gtk::VBox());
    stb2->show_all();

    // build screen
    hpanedl = Gtk::manage (new Gtk::Paned (Gtk::ORIENTATION_HORIZONTAL));
    hpanedl->set_name ("EditorLeftPaned");
    hpanedr = Gtk::manage (new Gtk::Paned (Gtk::ORIENTATION_HORIZONTAL));
    hpanedr->set_name ("EditorRightPaned");
    leftbox->reference ();
    vboxright->reference ();

    if (options.showHistory) {
        hpanedl->pack1 (*leftbox, false, false);
        hpanedl->set_position (options.historyPanelWidth);
    }

    Gtk::Paned *viewpaned = Gtk::manage (new Gtk::Paned (Gtk::ORIENTATION_VERTICAL));
    fPanel = filePanel;

    if (filePanel) {
        catalogPane = new Gtk::Paned();
        if (options.filmstripBottom) {
            viewpaned->pack2(*catalogPane, false, true);
        } else {
            viewpaned->pack1(*catalogPane, false, true);
        }
    }

    if (options.filmstripBottom) {
        viewpaned->pack1(*editbox, true, true);
    } else {
        viewpaned->pack2(*editbox, true, true);
    }

    vb->pack_start(*viewpaned, true, true);
    vb->pack_start(*stb2, Gtk::PACK_SHRINK, 0);
    hpanedl->pack2(*vb, true, true);

    hpanedr->pack1 (*hpanedl, true, false);
    hpanedr->pack2 (*vboxright, false, false);
    hpanedl->signal_button_release_event().connect_notify ( sigc::mem_fun (*this, &EditorPanel::leftPaneButtonReleased) );
    hpanedr->signal_button_release_event().connect_notify ( sigc::mem_fun (*this, &EditorPanel::rightPaneButtonReleased) );

    pack_start (*hpanedr);

    updateHistogramPosition (0, options.histogramPosition);

    show_all();
    // connect listeners
    profilep->setProfileChangeListener (tpc);
    history->setProfileChangeListener (tpc);
    history->setHistoryBeforeAfterListener (this);
    tpc->addPParamsChangeListener (profilep);
    tpc->addPParamsChangeListener (history);
    tpc->addPParamsChangeListener (this);
    iareapanel->imageArea->setCropGUIListener (tpc->getCropGUIListener());
    iareapanel->imageArea->setPointerMotionListener (navigator);
    iareapanel->imageArea->setImageAreaToolListener (tpc);
    iareapanel->imageArea->setAreaDrawListenerProvider(tpc);

    // initialize components
    info->set_active (options.showInfo);
    tpc->readOptions ();

    // connect event handlers
    info->signal_toggled().connect ( sigc::mem_fun (*this, &EditorPanel::info_toggled) );
    beforeAfter->signal_toggled().connect ( sigc::mem_fun (*this, &EditorPanel::beforeAfterToggled) );
    hidehp->signal_toggled().connect ( sigc::mem_fun (*this, &EditorPanel::hideHistoryActivated) );
    tbRightPanel_1->signal_toggled().connect ( sigc::mem_fun (*this, &EditorPanel::tbRightPanel_1_toggled) );
    saveimgas->signal_button_release_event().connect_notify(sigc::mem_fun(*this, &EditorPanel::saveAsPressed));
    queueimg->signal_button_release_event().connect_notify(sigc::mem_fun(*this, &EditorPanel::queueImgPressed));
    sendtogimp->signal_button_release_event().connect_notify(sigc::mem_fun(*this, &EditorPanel::sendToGimpPressed));
    toggleHistogramProfile->signal_toggled().connect( sigc::mem_fun (*this, &EditorPanel::histogramProfile_toggled) );

    if (navPrev) {
        navPrev->signal_pressed().connect ( sigc::mem_fun (*this, &EditorPanel::openPreviousEditorImage) );
    }

    if (navNext) {
        navNext->signal_pressed().connect ( sigc::mem_fun (*this, &EditorPanel::openNextEditorImage) );
    }

    if (navSync) {
        navSync->signal_pressed().connect ( sigc::mem_fun (*this, &EditorPanel::syncFileBrowser) );
    }

    ShowHideSidePanelsconn = tbShowHideSidePanels->signal_toggled().connect ( sigc::mem_fun (*this, &EditorPanel::toggleSidePanels), true);

    if (tbTopPanel_1) {
        tbTopPanel_1->signal_toggled().connect ( sigc::mem_fun (*this, &EditorPanel::tbTopPanel_1_toggled) );
    }
}


EditorPanel::~EditorPanel ()
{
    if (autosave_conn_.connected()) {
        autosave_conn_.disconnect();
    }
    
    idle_register.destroy();

    history->setHistoryBeforeAfterListener (nullptr);
    // the order is important!
    
    iareapanel->setBeforeAfterViews (nullptr, iareapanel);
    delete iareapanel;
    iareapanel = nullptr;

    if (beforeIpc) {
        beforeIpc->stopProcessing ();
    }

    delete beforeIarea;
    beforeIarea = nullptr;

    if (beforeIpc) {
        beforeIpc->setPreviewImageListener (nullptr);
    }

    delete beforePreviewHandler;
    beforePreviewHandler = nullptr;

    // if (beforeIpc) {
    //     rtengine::StagedImageProcessor::destroy (beforeIpc);
    // }
    // beforeIpc = nullptr;
    beforeIpc.reset();

    close ();

    if (epih->pending) {
        epih->destroyed = true;
    } else {
        delete epih;
    }

    delete tpc;

    delete ppframe;
    delete leftsubbox;
    delete leftbox;
    delete vsubboxright;
    delete vboxright;

    //delete saveAsDialog;
    if (catalogPane) {
        delete catalogPane;
    }

    if (iTopPanel_1_Show) {
        delete iTopPanel_1_Show;
    }

    if (iTopPanel_1_Hide) {
        delete iTopPanel_1_Hide;
    }

    if (iHistoryShow) {
        delete iHistoryShow;
    }

    if (iHistoryHide) {
        delete iHistoryHide;
    }

    if (iBeforeLockON) {
        delete iBeforeLockON;
    }

    if (iBeforeLockOFF) {
        delete iBeforeLockOFF;
    }

    if (iRightPanel_1_Show) {
        delete iRightPanel_1_Show;
    }

    if (iRightPanel_1_Hide) {
        delete iRightPanel_1_Hide;
    }
}


void EditorPanel::cleanup()
{
    if (tpc) {
        tpc->setEditProvider(nullptr);
    }
}   


void EditorPanel::leftPaneButtonReleased (GdkEventButton *event)
{
    if (event->button == 1) {
        // Button 1 released : it's a resize
        options.historyPanelWidth = hpanedl->get_position();
    }

    /*else if (event->button == 3) {
    }*/
}

void EditorPanel::rightPaneButtonReleased (GdkEventButton *event)
{
    if (event->button == 1) {
        int winW, winH;
        parentWindow->get_size (winW, winH);
        // Button 1 released : it's a resize
        options.toolPanelWidth = winW - hpanedr->get_position();
    }

    /*else if (event->button == 3) {
    }*/
}

void EditorPanel::writeOptions()
{
    if (profilep) {
        profilep->writeOptions();
    }

    if (tpc) {
        tpc->writeOptions();
    }
}


void EditorPanel::writeToolExpandedStatus (std::vector<int> &tpOpen)
{
    if (tpc) {
        tpc->writeToolExpandedStatus (tpOpen);
    }
}

void EditorPanel::showTopPanel (bool show)
{
    if (tbTopPanel_1->get_active() != show) {
        tbTopPanel_1->set_active (show);
    }
}

void EditorPanel::setAspect ()
{
    int winW, winH;
    parentWindow->get_size (winW, winH);
    hpanedl->set_position (options.historyPanelWidth);
    hpanedr->set_position (winW - options.toolPanelWidth);

    // initialize components
    if (info->get_active() != options.showInfo) {
        info->set_active (options.showInfo);
    }
}

void EditorPanel::on_realize ()
{
    realized = true;
    Gtk::VBox::on_realize ();
    // This line is needed to avoid autoexpansion of the window :-/
    //vboxright->set_size_request (options.toolPanelWidth, -1);
    tpc->updateToolState();
}


bool EditorPanel::can_open_now() const
{
    if (!ipc) {
        return true;
    }
    return !static_cast<const rtengine::ImProcCoordinator *>(ipc.get())->is_running();
}


void EditorPanel::open(Thumbnail* tmb, rtengine::InitialImage* isrc)
{
    close();

    isProcessing = true; // prevents closing-on-init

    // initialize everything
    openThm = tmb;
    openThm->increaseRef();

    fname = openThm->getFileName();
    lastSaveAsFileName = Glib::path_get_basename(fname);

    previewHandler = new PreviewHandler();

    this->isrc = isrc;
    ipc.reset(rtengine::StagedImageProcessor::create(isrc));
    ipc->setProgressListener (this);
    colorMgmtToolBar->updateProcessor();
    ipc->setPreviewImageListener (previewHandler);
    ipc->setPreviewScale (10);  // Important
    tpc->initImage(ipc.get(), tmb->getType() == FT_Raw);
    ipc->setHistogramListener (this);
    iareapanel->imageArea->indClippedPanel->silentlyDisableSharpMask();
    ipc->setSizeListener(this);

//    iarea->fitZoom ();   // tell to the editorPanel that the next image has to be fitted to the screen
    iareapanel->imageArea->setPreviewHandler (previewHandler);
    iareapanel->imageArea->setImProcCoordinator (ipc);
    navigator->previewWindow->setPreviewHandler (previewHandler);
    navigator->previewWindow->setImageArea (iareapanel->imageArea);

    rtengine::ImageSource* is = isrc->getImageSource();
    is->setProgressListener ( this );

    // try to load the last saved parameters from the cache or from the paramfile file
    std::unique_ptr<ProcParams> ldprof(openThm->createProcParamsForUpdate (true, false)); // will be freed by initProfile

    // initialize profile
    Glib::ustring defProf = openThm->getType() == FT_Raw ? options.defProfRaw : options.defProfImg;
    auto metadata = openThm->getMetaData();
    profilep->initProfile (defProf, ldprof.get(), metadata.get());
    profilep->setInitialFileName (fname);

    openThm->addThumbnailListener (this);
    info_toggled ();

    if (beforeIarea) {
        beforeAfterToggled();
        beforeAfterToggled();
    }

    if (iareapanel->imageArea->mainCropWindow) {
        iareapanel->imageArea->mainCropWindow->cropHandler.newImage(ipc, false);
    }
    { // if there is no crop window, it will be constructed in on_resized
        Gtk::Allocation alloc;
        iareapanel->imageArea->on_resized(alloc);
    }

    history->resetSnapShotNumber();
    navigator->setMetaInfo(isrc->getMetaData());
    navigator->setInvalid(ipc->getFullWidth(),ipc->getFullHeight());

    history->setPParamsSnapshotListener(openThm);
    history->setSnapshots(openThm->getProcParamsSnapshots());
    history->enableSnapshots(false);

    // When passing a photo as an argument to the RawTherapee executable, the user wants
    // this auto-loaded photo's thumbnail to be selected and visible in the Filmstrip.
    syncFileBrowser();

    if (options.sidecar_autosave_interval > 0) {
        autosave_conn_ = Glib::signal_timeout().connect(sigc::mem_fun(*this, &EditorPanel::autosave), options.sidecar_autosave_interval * 60000);
    }
}

void EditorPanel::close ()
{
    if (ipc) {
        saveProfile ();
        // close image processor and the current thumbnail
        tpc->closeImage ();    // this call stops image processing
        tpc->writeOptions ();
        rtengine::ImageSource* is = isrc->getImageSource();
        is->setProgressListener ( nullptr );

        if (ipc) {
            ipc->setPreviewImageListener (nullptr);
        }

        if (beforeIpc) {
            beforeIpc->setPreviewImageListener (nullptr);
        }

        delete previewHandler;
        previewHandler = nullptr;

        if (iareapanel) {
            if (iareapanel->imageArea->mainCropWindow) {
                iareapanel->imageArea->mainCropWindow->cropHandler.newImage(nullptr, false);
            }
            iareapanel->imageArea->setPreviewHandler (nullptr);
            iareapanel->imageArea->setImProcCoordinator (nullptr);
            iareapanel->imageArea->unsubscribe();
        }

        // rtengine::StagedImageProcessor::destroy (ipc);
        // ipc = nullptr;
        ipc.reset();
        navigator->previewWindow->setPreviewHandler (nullptr);

        // If the file was deleted somewhere, the openThm.descreaseRef delete the object, but we don't know here
        if (Glib::file_test (fname, Glib::FILE_TEST_EXISTS)) {
            openThm->removeThumbnailListener (this);
            openThm->decreaseRef ();
        }
    }
    openThm = nullptr;
}

void EditorPanel::saveProfile ()
{
    if (!ipc || !openThm) {
        return;
    }

    if (autosave_conn_.connected()) {
        autosave_conn_.disconnect();
    }
    
    // If the file was deleted, do not generate ghost entries
    if (Glib::file_test (fname, Glib::FILE_TEST_EXISTS)) {
        ProcParams params;
        ipc->getParams (&params);

        // Will call updateCache, which will update both the cached and sidecar files if necessary
        openThm->setProcParams(FullPartialProfile(params), EDITOR);
    }

    if (options.sidecar_autosave_interval > 0) {
        autosave_conn_ = Glib::signal_timeout().connect(sigc::mem_fun(*this, &EditorPanel::autosave), options.sidecar_autosave_interval * 60000);
    }
}

Glib::ustring EditorPanel::getShortName ()
{
    if (openThm) {
        return Glib::path_get_basename (openThm->getFileName ());
    } else {
        return "";
    }
}

Glib::ustring EditorPanel::getFileName ()
{
    if (openThm) {
        return openThm->getFileName ();
    } else {
        return "";
    }
}

// TODO!!!
void EditorPanel::procParamsChanged(
    const rtengine::procparams::ProcParams* params,
    const rtengine::ProcEvent& ev,
    const Glib::ustring& descr,
    const ParamsEdited* paramsEdited
)
{

//    if (ev!=EvPhotoLoaded)
//        saveLabel->set_markup (Glib::ustring("<span foreground=\"#AA0000\" weight=\"bold\">") + M("MAIN_BUTTON_SAVE") + "</span>");

    rtengine::eSensorType sensorType = isrc->getImageSource()->getSensorType();

    selectedFrame = 0;
    if (sensorType == rtengine::ST_BAYER) {
        selectedFrame = params->raw.bayersensor.imageNum;
    //} else if (sensorType == rtengine::ST_FUJI_XTRANS) {
    //    selectedFrame = params->raw.xtranssensor.imageNum;
    }
    selectedFrame = rtengine::LIM<int>(selectedFrame, 0, isrc->getImageSource()->getMetaData()->getFrameCount() - 1);

    info_toggled();
}

void EditorPanel::clearParamChanges()
{
}

void EditorPanel::setProgress(double p)
{
    MyProgressBar* const pl = progressLabel;

    idle_register.add(
        [p, pl]() -> bool
        {
            setprogressStrUI(p, {}, pl);
            return false;
        }
    );
}

void EditorPanel::setProgressStr(const Glib::ustring& str)
{
    MyProgressBar* const pl = progressLabel;

    idle_register.add(
        [str, pl]() -> bool
        {
            setprogressStrUI(-1.0, str, pl);
            return false;
        }, G_PRIORITY_LOW
    );
}

void EditorPanel::setProgressState(bool inProcessing)
{
    epih->pending++;

    idle_register.add(
        [this, inProcessing]() -> bool
        {
            if (epih->destroyed)
            {
                if (epih->pending == 1) {
                    delete epih;
                } else {
                    --epih->pending;
                }

                return false;
            }

            epih->epanel->refreshProcessingState(inProcessing);
            --epih->pending;

            return false;
        }
    );
}

void EditorPanel::error(const Glib::ustring& descr)
{
    parent->error(descr);
}

void EditorPanel::error(const Glib::ustring& title, const Glib::ustring& descr)
{
    epih->pending++;

    idle_register.add(
        [this, descr, title]() -> bool
        {
            if (epih->destroyed) {
                if (epih->pending == 1) {
                    delete epih;
                } else {
                    --epih->pending;
                }

                return false;
            }

            epih->epanel->displayError(title, descr);
            --epih->pending;

            return false;
        }
    );
}

void EditorPanel::displayError(const Glib::ustring& title, const Glib::ustring& descr)
{
    parent->error(title + ": " + descr);
    // GtkWidget* msgd = gtk_message_dialog_new_with_markup (nullptr,
    //                   GTK_DIALOG_DESTROY_WITH_PARENT,
    //                   GTK_MESSAGE_ERROR,
    //                   GTK_BUTTONS_OK,
    //                   "<b>%s</b>",
    //                   descr.data());
    // gtk_window_set_title ((GtkWindow*)msgd, title.data());
    // g_signal_connect_swapped (msgd, "response",
    //                           G_CALLBACK (gtk_widget_destroy),
    //                           msgd);
    // gtk_widget_show_all (msgd);
}

// This is only called from the ThreadUI, so within the gtk thread
void EditorPanel::refreshProcessingState (bool inProcessingP)
{
    double val;
    Glib::ustring str;

    if (inProcessingP) {
        if (processingStartedTime == 0) {
            processingStartedTime = ::time (nullptr);
        }

        val = 1.0;
        str = "PROGRESSBAR_PROCESSING";
    } else {
        // Set proc params of thumbnail. It saves it into the cache and updates the file browser.
        if (ipc && openThm && tpc->getChangedState()) {
            rtengine::procparams::ProcParams pparams;
            ipc->getParams (&pparams);
            openThm->setProcParams(pparams, EDITOR, false);
        }

        // Ring a sound if it was a long event
        if (processingStartedTime != 0) {
            time_t curTime = ::time (nullptr);

            if (::difftime (curTime, processingStartedTime) > options.sndLngEditProcDoneSecs) {
                SoundManager::playSoundAsync (options.sndLngEditProcDone);
            }

            processingStartedTime = 0;
        }

        // Set progress bar "done"
        val = 0.0;
        str = "PROGRESSBAR_READY";

#ifdef WIN32

        // Maybe accessing "parent", which is a Gtk object, can justify to get the Gtk lock...
        if (!firstProcessingDone && static_cast<RTWindow*> (parent)->getIsFullscreen()) {
            parent->fullscreen();
        }

#endif
        firstProcessingDone = true;

        history->enableSnapshots(true);
    }

    isProcessing = inProcessingP;

    setprogressStrUI(val, str, progressLabel);
}

void EditorPanel::info_toggled ()
{

    Glib::ustring infoString;
    Glib::ustring expcomp;

    if (!ipc || !openThm) {
        return;
    }

    const rtengine::FramesMetaData* idata = ipc->getInitialImage()->getMetaData();

    if (idata && idata->hasExif()) {
        infoString = Glib::ustring::compose ("%1 + %2\n<span size=\"small\">f/</span><span size=\"large\">%3</span>  <span size=\"large\">%4</span><span size=\"small\">s</span>  <span size=\"small\">%5</span><span size=\"large\">%6</span>  <span size=\"large\">%7</span><span size=\"small\">mm</span>",
                                              Glib::ustring (idata->getMake() + " " + idata->getModel()),
                                              Glib::ustring (idata->getLens()),
                                              Glib::ustring (idata->apertureToString (idata->getFNumber())),
                                              Glib::ustring (idata->shutterToString (idata->getShutterSpeed())),
                                              M ("QINFO_ISO"), idata->getISOSpeed(),
                                              Glib::ustring::format (std::setw (3), std::fixed, std::setprecision (2), idata->getFocalLen()));

        expcomp = Glib::ustring (idata->expcompToString (idata->getExpComp(), true)); // maskZeroexpcomp

        if (!expcomp.empty ()) {
            infoString = Glib::ustring::compose ("%1  <span size=\"large\">%2</span><span size=\"small\">EV</span>",
                                                  infoString,
                                                  expcomp /*Glib::ustring(idata->expcompToString(idata->getExpComp()))*/);
        }

        infoString = Glib::ustring::compose ("%1\n<span size=\"small\">%2</span><span>%3</span>",
                                              infoString,
                                              escapeHtmlChars (Glib::path_get_dirname (openThm->getFileName())) + G_DIR_SEPARATOR_S,
                                              escapeHtmlChars (Glib::path_get_basename (openThm->getFileName()))  );

        int ww = -1, hh = -1;
        //idata->getDimensions(ww, hh);
        if (ww <= 0) {
            ww = ipc->getFullWidth();
            hh = ipc->getFullHeight();
        }
        
        //megapixels
        infoString = Glib::ustring::compose ("%1\n<span size=\"small\">%2 MP (%3x%4)</span>",
                                             infoString,
                                             Glib::ustring::format (std::setw (4), std::fixed, std::setprecision (1), (float)ww * hh / 1000000),
                                             ww, hh);

        //adding special characteristics
        bool isHDR = idata->getHDR();
        bool isPixelShift = idata->getPixelShift();
        unsigned int numFrames = idata->getFrameCount();
        if (isHDR) {
            infoString = Glib::ustring::compose ("%1\n" + M("QINFO_HDR"), infoString, numFrames);
            if (numFrames == 1) {
                int sampleFormat = idata->getSampleFormat();
                infoString = Glib::ustring::compose ("%1 / %2", infoString, M(Glib::ustring::compose("SAMPLEFORMAT_%1", sampleFormat)));
            }
        } else if (isPixelShift) {
            infoString = Glib::ustring::compose ("%1\n" + M("QINFO_PIXELSHIFT"), infoString, numFrames);
        } else if (numFrames > 1) {
            infoString = Glib::ustring::compose ("%1\n" + M("QINFO_FRAMECOUNT"), infoString, numFrames);
        }
    } else {
        infoString = M ("QINFO_NOEXIF");
    }

    iareapanel->imageArea->setInfoText (infoString);
    iareapanel->imageArea->infoEnabled (info->get_active ());
}

void EditorPanel::hideHistoryActivated ()
{

    removeIfThere (hpanedl, leftbox, false);

    if (hidehp->get_active()) {
        hpanedl->pack1 (*leftbox, false, false);
    }

    options.showHistory = hidehp->get_active();

    if (options.showHistory) {
        hidehp->set_image (*iHistoryHide);
    } else {
        hidehp->set_image (*iHistoryShow);
    }

    tbShowHideSidePanels_managestate();
}


void EditorPanel::tbRightPanel_1_toggled ()
{
    /*
        removeIfThere (hpanedr, vboxright, false);
        if (tbRightPanel_1->get_active()){
            hpanedr->pack2(*vboxright, false, true);
            tbRightPanel_1->set_image (*iRightPanel_1_Hide);
        }
        else {
            tbRightPanel_1->set_image (*iRightPanel_1_Show);
        }
        tbShowHideSidePanels_managestate();
        */
    if (vboxright) {
        if (tbRightPanel_1->get_active()) {
            vboxright->show();
            tbRightPanel_1->set_image (*iRightPanel_1_Hide);
        } else {
            vboxright->hide();
            tbRightPanel_1->set_image (*iRightPanel_1_Show);
        }

        tbShowHideSidePanels_managestate();
    }
}

void EditorPanel::tbTopPanel_1_visible (bool visible)
{
    if (!tbTopPanel_1) {
        return;
    }

    if (visible) {
        tbTopPanel_1->show();
    } else {
        tbTopPanel_1->hide();
    }
}

void EditorPanel::tbTopPanel_1_toggled ()
{

    if (catalogPane) { // catalogPane does not exist in multitab mode

        if (tbTopPanel_1->get_active()) {
            catalogPane->show();
            tbTopPanel_1->set_image (*iTopPanel_1_Hide);
            options.editorFilmStripOpened = true;
        } else {
            catalogPane->hide();
            tbTopPanel_1->set_image (*iTopPanel_1_Show);
            options.editorFilmStripOpened = false;
        }

        tbShowHideSidePanels_managestate();
    }
}

/*
 * WARNING: Take care of the simpleEditor value when adding or modifying shortcut keys,
 *          since handleShortcutKey is now also triggered in simple editor mode
 */
bool EditorPanel::handleShortcutKey (GdkEventKey* event)
{

    bool ctrl = event->state & GDK_CONTROL_MASK;
    bool shift = event->state & GDK_SHIFT_MASK;
    bool alt = event->state & GDK_MOD1_MASK;
#ifdef __WIN32__
    bool altgr = event->state & GDK_MOD2_MASK;
#else
    bool altgr = event->state & GDK_MOD5_MASK;
#endif

    if (shortcut_mgr_ && shortcut_mgr_->keyPressed(event)) {
        return true;
    }

    // Editor Layout
    switch (getKeyval(event)) {
        case GDK_KEY_L:
            if (tbTopPanel_1) {
                tbTopPanel_1->set_active (!tbTopPanel_1->get_active());    // toggle top panel
            }

            return true;
            break;

        case GDK_KEY_l:
            if (alt) { // toggle left and right panels
                hidehp->set_active (!hidehp->get_active());
                tbRightPanel_1->set_active (!tbRightPanel_1->get_active());
            } else if (ctrl) { // toggle right panel
                tbRightPanel_1->set_active (!tbRightPanel_1->get_active());
            } else {
                hidehp->set_active (!hidehp->get_active()); // toggle History (left panel)
            }
            return true;
            
            break;

        case GDK_KEY_m: // Maximize preview panel: hide top AND right AND history panels
            if (!ctrl && !alt) {
                toggleSidePanels();
                return true;
            }

            break;

        case GDK_KEY_M: // Maximize preview panel: hide top AND right AND history panels AND (fit image preview)
            if (!ctrl && !alt) {
                toggleSidePanelsZoomFit();
                return true;
            }

            break;
    }


    if (!alt && !ctrl && !altgr && getKeyval(event, false) == GDK_KEY_9) {
        iareapanel->imageArea->previewModePanel->togglebackColor();
        return true;
    }

    if (!alt) {
        if (!ctrl) {
            // Normal
            switch (getKeyval(event)) {
                case GDK_KEY_bracketright:
                    tpc->coarse->rotateRight();
                    return true;

                case GDK_KEY_bracketleft:
                    tpc->coarse->rotateLeft();
                    return true;

                //case GDK_KEY_i:
                case GDK_KEY_I:
                    info->set_active (!info->get_active());
                    return true;

                //case GDK_KEY_B:
                case GDK_KEY_A:
                    beforeAfter->set_active (!beforeAfter->get_active());
                    return true;

                case GDK_KEY_plus:
                case GDK_KEY_equal:
                case GDK_KEY_KP_Add:
                    iareapanel->imageArea->zoomPanel->zoomInClicked();
                    return true;

                case GDK_KEY_minus:
                case GDK_KEY_underscore:
                case GDK_KEY_KP_Subtract:
                    iareapanel->imageArea->zoomPanel->zoomOutClicked();
                    return true;

                case GDK_KEY_z://GDK_1
                    iareapanel->imageArea->zoomPanel->zoom11Clicked();
                    return true;

                /*
                #ifndef __WIN32__
                                case GDK_KEY_9: // toggle background color of the preview
                                    iareapanel->imageArea->previewModePanel->togglebackColor();
                                    return true;
                #endif
                */
                case GDK_KEY_R: //preview mode Red
                    iareapanel->imageArea->previewModePanel->toggleR();
                    return true;

                case GDK_KEY_G: //preview mode Green
                    iareapanel->imageArea->previewModePanel->toggleG();
                    return true;

                case GDK_KEY_B: //preview mode Blue
                    iareapanel->imageArea->previewModePanel->toggleB();
                    return true;

                case GDK_KEY_O: //preview mode Sharpening Contrast mask
                    iareapanel->imageArea->indClippedPanel->toggleSharpMask();
                    return true;

                case GDK_KEY_V: //preview mode Luminosity
                    iareapanel->imageArea->previewModePanel->toggleL();
                    return true;

                case GDK_KEY_F: //preview mode Focus Mask
                    iareapanel->imageArea->indClippedPanel->toggleFocusMask();
                    return true;

                case GDK_KEY_E: // preview mode false colors
                    iareapanel->imageArea->indClippedPanel->toggleFalseColors();
                    return true;

                case GDK_KEY_less:
                    iareapanel->imageArea->indClippedPanel->toggleClipped (false);
                    return true;

                case GDK_KEY_greater:
                    iareapanel->imageArea->indClippedPanel->toggleClipped (true);
                    return true;

                case GDK_KEY_f:
                    iareapanel->imageArea->zoomPanel->zoomFitClicked();
                    return true;

                case GDK_KEY_y: // synchronize filebrowser with image in Editor
                    if (!simpleEditor && fPanel && !fname.empty()) {
                        fPanel->fileCatalog->selectImage (fname, false);
                        return true;
                    }

                    break; // to avoid gcc complain

                case GDK_KEY_x: // clear filters and synchronize filebrowser with image in Editor
                    if (!simpleEditor && fPanel && !fname.empty()) {
                        fPanel->fileCatalog->selectImage (fname, true);
                        return true;
                    }

                    break; // to avoid gcc complain
            }
        } else {
            // With control
            switch (getKeyval(event)) {
                case GDK_KEY_S:
                    if (!gimpPlugin) {
                        do_save_image(true);
                    }
                    // saveProfile();
                    // setProgressStr (M ("PROGRESSBAR_PROCESSING_PROFILESAVED"));
                    return true;

                case GDK_KEY_s:
                    if (!gimpPlugin) {
                        do_save_image(false);
                    }

                    return true;

                case GDK_KEY_b:
                case GDK_KEY_B:
                    if (!simpleEditor && catalogPane && catalogPane->is_visible() && fPanel->fileCatalog->isSelected(fname)) {
                        // propagate this to fPanel, so that if there is a
                        // multiple selection all the selected thumbs get
                        // enqueued
                    } else if (!gimpPlugin && !simpleEditor) {
                        do_queue_image(getKeyval(event) == GDK_KEY_B);
                        return true;
                    }
                    break;
                    
                case GDK_KEY_e:
                case GDK_KEY_E:
                    if (!gimpPlugin) {
                        do_send_to_gimp(getKeyval(event) == GDK_KEY_E);
                    }

                    return true;

                case GDK_KEY_z:
                    history->undo ();
                    return true;

                case GDK_KEY_Z:
                case GDK_KEY_y:
                    history->redo ();
                    return true;

                case GDK_KEY_comma:
                    history->activatePrevSnapshot();
                    return true;
    
                case GDK_KEY_period:
                    history->activateNextSnapshot();
                    return true;

                case GDK_KEY_g:
                    iareapanel->imageArea->indClippedPanel->toggleGrid();
                    return true;
            }
        } //if (!ctrl)
    } //if (!alt)

    if (alt) {
        switch (getKeyval(event)) {
            case GDK_KEY_s:
                history->addBookmarkPressed ();
                setProgressStr (M ("PROGRESSBAR_SNAPSHOT_ADDED"));
                return true;

            // case GDK_KEY_f:
            //     iareapanel->imageArea->zoomPanel->zoomFitClicked();
            //     return true;
        }
    }

    if (shift) {
        switch (getKeyval(event)) {
            case GDK_KEY_F3: // open Previous image from Editor's perspective
                if (!simpleEditor && fPanel && !fname.empty()) {
                    EditorPanel::openPreviousEditorImage();
                    return true;
                }

                break; // to avoid gcc complain

            case GDK_KEY_F4: // open next image from Editor's perspective
                if (!simpleEditor && fPanel && !fname.empty()) {
                    EditorPanel::openNextEditorImage();
                    return true;
                }

                break; // to avoid gcc complain
        }
    }

    if (tpc->getToolBar() && tpc->getToolBar()->handleShortcutKey (event)) {
        return true;
    }

    if (tpc->handleShortcutKey (event)) {
        return true;
    }

    if (!simpleEditor && fPanel) {
        if (fPanel->handleShortcutKey (event)) {
            return true;
        }
    }

    return false;
}


bool EditorPanel::keyPressedBefore(GdkEventKey *event)
{
    bool ctrl = event->state & GDK_CONTROL_MASK;
    int dx = 0, dy = 0;
    const int step = options.editor_keyboard_scroll_step;
    switch (getKeyval(event)) {
    case GDK_KEY_KP_Up: case GDK_KEY_Up: dy = -step; break;
    case GDK_KEY_KP_Down: case GDK_KEY_Down: dy = step; break;
    case GDK_KEY_KP_Left: case GDK_KEY_Left: dx = -step; break;
    case GDK_KEY_KP_Right: case GDK_KEY_Right: dx = step; break;
    }
    if (dx || dy) {
        if (ctrl && iareapanel->imageArea->getMainCropWindow()) {
            iareapanel->imageArea->getMainCropWindow()->remoteMove(dx, dy);
            return true;
        }
    }
    return false;
}


bool EditorPanel::keyReleased(GdkEventKey *event)
{
    if (shortcut_mgr_ && shortcut_mgr_->keyReleased(event)) {
        return true;
    }

    switch (getKeyval(event)) {
    case GDK_KEY_KP_Up: case GDK_KEY_Up: 
    case GDK_KEY_KP_Down: case GDK_KEY_Down:
    case GDK_KEY_KP_Left: case GDK_KEY_Left:
    case GDK_KEY_KP_Right: case GDK_KEY_Right:
        if (iareapanel->imageArea->getMainCropWindow()) {
            iareapanel->imageArea->getMainCropWindow()->remoteMoveReady();
        }
        break;
    }
    
    return false;
}


bool EditorPanel::scrollPressed(GdkEventScroll *event)
{
    if (shortcut_mgr_ && shortcut_mgr_->scrollPressed(event)) {
        return true;
    }
    return false;
}


void EditorPanel::procParamsChanged (Thumbnail* thm, int whoChangedIt)
{

    if (whoChangedIt != EDITOR) {
        const auto &pp = openThm->getProcParams();
        rtengine::procparams::FullPartialProfile fp(pp);
        tpc->profileChange (&fp, rtengine::EvProfileChangeNotification, M ("PROGRESSDLG_PROFILECHANGEDINBROWSER"));
    }
}

bool EditorPanel::idle_saveImage (ProgressConnector<rtengine::IImagefloat*> *pc, Glib::ustring fname, SaveFormat sf, rtengine::procparams::ProcParams &pparams)
{
    rtengine::IImagefloat* img = pc->returnValue();
    //delete pc;
    pc->destroy();

    if (img) {
        setProgressStr (M ("GENERAL_SAVE"));
        setProgress (0.9f);

        ProgressConnector<int> *ld = new ProgressConnector<int>();
        img->setSaveProgressListener (parent->getProgressListener());

        if (sf.format == "tif") {
            ld->startFunc (sigc::bind (sigc::mem_fun (img, &rtengine::IImagefloat::saveAsTIFF), fname, sf.tiffBits, sf.tiffFloat, sf.tiffUncompressed),
                           sigc::bind (sigc::mem_fun (*this, &EditorPanel::idle_imageSaved), ld, img, fname, sf, pparams));
        } else if (sf.format == "png") {
            ld->startFunc (sigc::bind (sigc::mem_fun (img, &rtengine::IImagefloat::saveAsPNG), fname, sf.pngBits, false),
                           sigc::bind (sigc::mem_fun (*this, &EditorPanel::idle_imageSaved), ld, img, fname, sf, pparams));
        } else if (sf.format == "jpg") {
            ld->startFunc (sigc::bind (sigc::mem_fun (img, &rtengine::IImagefloat::saveAsJPEG), fname, sf.jpegQuality, sf.jpegSubSamp),
                           sigc::bind (sigc::mem_fun (*this, &EditorPanel::idle_imageSaved), ld, img, fname, sf, pparams));
        } else {
            //delete ld;
            const auto do_save =
                [=]() -> int
                {
                    return rtengine::ImageIOManager::getInstance()->save(img, sf.format, fname, this) ? 0 : 1;
                };
            ld->startFunc(sigc::slot0<int>(do_save),
                          sigc::bind(sigc::mem_fun(*this, &EditorPanel::idle_imageSaved), ld, img, fname, sf, pparams));
        }
    } else {
        Glib::ustring msg_ = Glib::ustring ("<b>") + fname + ": Error during image processing\n</b>";
        Gtk::MessageDialog msgd (*parent, msg_, true, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        msgd.run ();

        saveimgas->set_sensitive (true);
        sendtogimp->set_sensitive (true);
        isProcessing = false;

    }

    rtengine::ImageSource* imgsrc = isrc->getImageSource ();
    imgsrc->setProgressListener (this);
    return false;
}

bool EditorPanel::idle_imageSaved (ProgressConnector<int> *pc, rtengine::IImagefloat* img, Glib::ustring fname, SaveFormat sf, rtengine::procparams::ProcParams &pparams)
{
    img->free ();

    if (! pc->returnValue() ) {
        openThm->imageDeveloped ();

        // save processing parameters, if needed
        if (sf.saveParams) {
            // We keep the extension to avoid overwriting the profile when we have
            // the same output filename with different extension
            auto sidecar = fname + ".out" + paramFileExtension;
            if (!options.params_out_embed) {
                pparams.save(parent, sidecar);
            } else if (pparams.saveEmbedded(parent, fname) != 0) { 
                parent->error(Glib::ustring::compose(M("PROCPARAMS_EMBEDDED_SAVE_WARNING"), fname, sidecar));
                pparams.save(parent, sidecar);
            }
        }
    } else {
        error (M ("MAIN_MSG_CANNOTSAVE"), fname);
    }

    saveimgas->set_sensitive (true);
    sendtogimp->set_sensitive (true);

    parent->setProgressStr ("");
    parent->setProgress (0.);

    setProgressState (false);

    //delete pc;
    pc->destroy();
    SoundManager::playSoundAsync (options.sndBatchQueueDone);
    isProcessing = false;
    return false;
}


BatchQueueEntry* EditorPanel::createBatchQueueEntry(bool fast_export, bool use_batch_queue_profile, const rtengine::procparams::PartialProfile *export_profile)
{
    rtengine::procparams::ProcParams pparams;
    ipc->getParams (&pparams);
    if (export_profile) {
        export_profile->applyTo(pparams);
    }
    // rtengine::ProcessingJob* job = rtengine::ProcessingJob::create (openThm->getFileName (), openThm->getType() == FT_Raw, pparams);
    rtengine::ProcessingJob* job = create_processing_job(openThm->getFileName(), openThm->getType() == FT_Raw, pparams, fast_export);
    static_cast<rtengine::ProcessingJobImpl *>(job)->use_batch_profile = use_batch_queue_profile;
    int fullW = 0, fullH = 0;
    isrc->getImageSource()->getFullSize (fullW, fullH, pparams.coarse.rotate == 90 || pparams.coarse.rotate == 270 ? TR_R90 : TR_NONE);
    int prevh = BatchQueue::calcMaxThumbnailHeight();
    int prevw = int ((size_t)fullW * (size_t)prevh / (size_t)fullH);
    return new BatchQueueEntry (job, pparams, openThm->getFileName(), prevw, prevh, openThm);
}


void EditorPanel::saveAsPressed(GdkEventButton *event)
{
    do_save_image(event->state & GDK_CONTROL_MASK);
}


void EditorPanel::do_save_image(bool fast_export)
{
    if (!ipc || !openThm) {
        return;
    }

    bool fnameOK = false;
    Glib::ustring fnameOut;

    SaveAsDialog* saveAsDialog;
    auto toplevel = static_cast<Gtk::Window*> (get_toplevel ());

    if (Glib::file_test (options.lastSaveAsPath, Glib::FILE_TEST_IS_DIR)) {
        saveAsDialog = new SaveAsDialog(options.lastSaveAsPath, toplevel);
    } else {
        saveAsDialog = new SaveAsDialog(PlacesBrowser::userPicturesDir (), toplevel);
    }

    saveAsDialog->set_default_size (options.saveAsDialogWidth, options.saveAsDialogHeight);
    saveAsDialog->setInitialFileName (lastSaveAsFileName);
    saveAsDialog->setImagePath (fname);

    do {
        int result = saveAsDialog->run ();

        // The SaveAsDialog ensure that a filename has been specified
        fnameOut = saveAsDialog->getFileName ();

        options.lastSaveAsPath = saveAsDialog->getDirectory ();
        saveAsDialog->get_size (options.saveAsDialogWidth, options.saveAsDialogHeight);
        options.autoSuffix = saveAsDialog->getAutoSuffix ();
        options.saveMethodNum = saveAsDialog->getSaveMethodNum ();
        lastSaveAsFileName = Glib::path_get_basename (removeExtension (fnameOut));
        SaveFormat sf = saveAsDialog->getFormat ();
        options.saveFormat = sf;
        options.forceFormatOpts = saveAsDialog->getForceFormatOpts ();

        if (result != Gtk::RESPONSE_OK) {
            break;
        }

        if (saveAsDialog->getImmediately()) {
            // separate filename and the path to the destination directory
            Glib::ustring dstdir = Glib::path_get_dirname (fnameOut);
            Glib::ustring dstfname = Glib::path_get_basename (removeExtension (fnameOut));
            Glib::ustring dstext = getExtension (fnameOut);

            if (saveAsDialog->getAutoSuffix()) {

                Glib::ustring fnameTemp;

                for (int tries = 0; tries < 100; tries++) {
                    if (tries == 0) {
                        fnameTemp = Glib::ustring::compose ("%1.%2", Glib::build_filename (dstdir,  dstfname), dstext);
                    } else {
                        fnameTemp = Glib::ustring::compose ("%1-%2.%3", Glib::build_filename (dstdir,  dstfname), tries, dstext);
                    }

                    if (!Glib::file_test (fnameTemp, Glib::FILE_TEST_EXISTS)) {
                        fnameOut = fnameTemp;
                        fnameOK = true;
                        break;
                    }
                }
            }

            // check if it exists
            if (!fnameOK) {
                fnameOK = confirmOverwrite (*saveAsDialog, fnameOut);
            }

            if (fnameOK) {
                isProcessing = true;
                // save image
                rtengine::procparams::ProcParams pparams;
                ipc->getParams (&pparams);
                auto ep = saveAsDialog->getExportProfile();
                if (ep) {
                    ep->applyTo(pparams);
                }
                ep = rtengine::ImageIOManager::getInstance()->getSaveProfile(sf.format);
                if (ep) {
                    ep->applyTo(pparams);
                }
                
                rtengine::ProcessingJob *job = create_processing_job(ipc->getInitialImage(), pparams, fast_export);
                static_cast<rtengine::ProcessingJobImpl *>(job)->use_batch_profile = false;

                ProgressConnector<rtengine::IImagefloat*> *ld = new ProgressConnector<rtengine::IImagefloat*>();
                ld->startFunc (sigc::bind (sigc::ptr_fun (&rtengine::processImage), job, err, parent->getProgressListener(), false ),
                               sigc::bind (sigc::mem_fun ( *this, &EditorPanel::idle_saveImage ), ld, fnameOut, sf, pparams));
                saveimgas->set_sensitive (false);
                sendtogimp->set_sensitive (false);
            }
        } else {
            BatchQueueEntry* bqe = createBatchQueueEntry(fast_export, false, saveAsDialog->getExportProfile());
            bqe->outFileName = fnameOut;
            bqe->saveFormat = saveAsDialog->getFormat ();
            bqe->forceFormatOpts = saveAsDialog->getForceFormatOpts ();
            parent->addBatchQueueJob (bqe, saveAsDialog->getToHeadOfQueue ());
            fnameOK = true;
        }

        // ask parent to redraw file browser
        // ... or does it automatically when the tab is switched to it
    } while (!fnameOK);

    saveAsDialog->hide();

    delete saveAsDialog;
}


void EditorPanel::queueImgPressed(GdkEventButton *event)
{
    do_queue_image(event->state & GDK_CONTROL_MASK);
}


void EditorPanel::do_queue_image(bool fast_export)
{
    if (!ipc || !openThm) {
        return;
    }

    saveProfile();
    parent->addBatchQueueJob(createBatchQueueEntry(fast_export, true, nullptr));
}

void EditorPanel::sendToGimpPressed(GdkEventButton *event)
{
    do_send_to_gimp(event->state & GDK_CONTROL_MASK);
}


void EditorPanel::do_send_to_gimp(bool fast_export)
{
    if (!ipc || !openThm) {
        return;
    }

    // develop image
    rtengine::procparams::ProcParams pparams;
    ipc->getParams (&pparams);
    if (options.editor_bypass_output_profile) {
        pparams.icm.outputProfile = rtengine::procparams::ColorManagementParams::NoProfileString;
    }
    // rtengine::ProcessingJob* job = rtengine::ProcessingJob::create (ipc->getInitialImage(), pparams);
    rtengine::ProcessingJob* job = create_processing_job(ipc->getInitialImage(), pparams, fast_export);
    ProgressConnector<rtengine::IImagefloat*> *ld = new ProgressConnector<rtengine::IImagefloat*>();
    ld->startFunc (sigc::bind (sigc::ptr_fun (&rtengine::processImage), job, err, parent->getProgressListener(), false ),
                   sigc::bind (sigc::mem_fun ( *this, &EditorPanel::idle_sendToGimp ), ld, openThm->getFileName() ));
    saveimgas->set_sensitive (false);
    sendtogimp->set_sensitive (false);
}


bool EditorPanel::saveImmediately (const Glib::ustring &filename, const SaveFormat &sf)
{
    rtengine::procparams::ProcParams pparams;
    ipc->getParams (&pparams);

    rtengine::ProcessingJob *job = rtengine::ProcessingJob::create (ipc->getInitialImage(), pparams);

    // save immediately
    rtengine::IImagefloat *img = rtengine::processImage (job, err, nullptr, false);
    if (img) {
        img->setSaveProgressListener(parent);
    }

    int err = 0;

    if (gimpPlugin) {
        err = img->saveAsTIFF (filename, 32, true, true);
    } else if (sf.format == "tif") {
        err = img->saveAsTIFF (filename, sf.tiffBits, sf.tiffFloat, sf.tiffUncompressed);
    } else if (sf.format == "png") {
        err = img->saveAsPNG (filename, sf.pngBits);
    } else if (sf.format == "jpg") {
        err = img->saveAsJPEG (filename, sf.jpegQuality, sf.jpegSubSamp);
    } else {
        err = rtengine::ImageIOManager::getInstance()->save(img, sf.format, filename, this) ? 0 : 1;
    }

    img->free();
    return !err;
}


void EditorPanel::openPreviousEditorImage()
{
    if (!simpleEditor && fPanel && !fname.empty()) {
        fPanel->fileCatalog->openNextPreviousEditorImage (fname, false, NAV_PREVIOUS);
    }
}

void EditorPanel::openNextEditorImage()
{
    if (!simpleEditor && fPanel && !fname.empty()) {
        fPanel->fileCatalog->openNextPreviousEditorImage (fname, false, NAV_NEXT);
    }
}

void EditorPanel::syncFileBrowser()   // synchronize filebrowser with image in Editor
{
    if (!simpleEditor && fPanel && !fname.empty()) {
        fPanel->fileCatalog->selectImage (fname, false);
    }
}

void EditorPanel::histogramProfile_toggled()
{
    options.rtSettings.HistogramWorking = toggleHistogramProfile->get_active();
    colorMgmtToolBar->updateHistogram();
}

bool EditorPanel::idle_sendToGimp ( ProgressConnector<rtengine::IImagefloat*> *pc, Glib::ustring fname)
{

    rtengine::IImagefloat* img = pc->returnValue();
    //delete pc;
    pc->destroy();

    if (img) {
        // get file name base
        Glib::ustring shortname = removeExtension(Glib::path_get_basename(fname));
        Glib::ustring dirname;
        switch (options.editor_out_dir) {
        case Options::EDITOR_OUT_DIR_CURRENT:
            dirname = Glib::path_get_dirname(fname);
            break;
        case Options::EDITOR_OUT_DIR_CUSTOM:
            dirname = options.editor_custom_out_dir;
            break;
        default: // Options::EDITOR_OUT_DIR_TEMP
            dirname = Glib::get_tmp_dir();
            break;
        }
        Glib::ustring fname = Glib::build_filename(dirname, shortname);

        SaveFormat sf;
        sf.format = "tif";
        if (options.editor_float32) {
            sf.tiffBits = 32;
            sf.tiffFloat = true;
        } else {
            sf.tiffBits = 16;
            sf.tiffFloat = false;
        }
        sf.tiffUncompressed = true;
        sf.saveParams = true;

        Glib::ustring fileName = Glib::ustring::compose ("%1.%2", fname, sf.format);

        // TODO: Just list all file with a suitable name instead of brute force...
        int tries = 1;

        while (Glib::file_test (fileName, Glib::FILE_TEST_EXISTS) && tries < 1000) {
            fileName = Glib::ustring::compose ("%1-%2.%3", fname, tries, sf.format);
            tries++;
        }

        if (tries == 1000) {
            img->free ();
            return false;
        }

        ProgressConnector<int> *ld = new ProgressConnector<int>();
        img->setSaveProgressListener (parent->getProgressListener());
        ld->startFunc (sigc::bind (sigc::mem_fun (img, &rtengine::IImagefloat::saveAsTIFF), fileName, sf.tiffBits, sf.tiffFloat, sf.tiffUncompressed),
                       sigc::bind (sigc::mem_fun (*this, &EditorPanel::idle_sentToGimp), ld, img, fileName));
    } else {
        Glib::ustring msg_ = Glib::ustring ("<b> Error during image processing\n</b>");
        Gtk::MessageDialog msgd (*parent, msg_, true, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        msgd.run ();
        saveimgas->set_sensitive (true);
        sendtogimp->set_sensitive (true);
    }

    return false;
}

bool EditorPanel::idle_sentToGimp (ProgressConnector<int> *pc, rtengine::IImagefloat* img, Glib::ustring filename)
{
    img->free ();
    int errore = pc->returnValue();
    setProgressState(false);
    //delete pc;
    pc->destroy();

    if (!errore) {
        saveimgas->set_sensitive (true);
        sendtogimp->set_sensitive (true);
        parent->setProgressStr ("");
        parent->setProgress (0.);
        bool success = false;

        if (options.editorToSendTo == 1) {
            success = ExtProg::openInGimp (filename);
        } else if (options.editorToSendTo == 2) {
            success = ExtProg::openInPhotoshop (filename);
        } else if (options.editorToSendTo == 3) {
            success = ExtProg::openInCustomEditor (filename);
        }

        if (!success) {
            Gtk::MessageDialog msgd (*parent, M ("MAIN_MSG_CANNOTSTARTEDITOR"), false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
            msgd.set_secondary_text (M ("MAIN_MSG_CANNOTSTARTEDITOR_SECONDARY"));
            msgd.set_title (M ("MAIN_BUTTON_SENDTOEDITOR"));
            msgd.run ();
        }
    }

    return false;
}

void EditorPanel::historyBeforeAfterChanged(const rtengine::procparams::ProcParams& params)
{

    if (beforeIpc) {
        ProcParams* pparams = beforeIpc->beginUpdateParams ();
        *pparams = params;
        beforeIpc->endUpdateParams (rtengine::EvProfileChanged);  // starts the IPC processing
    }
}

void EditorPanel::beforeAfterToggled ()
{

    if (!ipc) {
        return;
    }

    removeIfThere (beforeAfterBox,  beforeBox, false);
    removeIfThere (afterBox,  afterHeaderBox, false);

    if (beforeIarea) {
        if (beforeIpc) {
            beforeIpc->stopProcessing ();
        }

        iareapanel->setBeforeAfterViews (nullptr, iareapanel);
        iareapanel->imageArea->iLinkedImageArea = nullptr;
        delete beforeIarea;
        beforeIarea = nullptr;

        if (beforeIpc) {
            beforeIpc->setPreviewImageListener (nullptr);
        }

        delete beforePreviewHandler;
        beforePreviewHandler = nullptr;

        // if (beforeIpc) {
        //     rtengine::StagedImageProcessor::destroy (beforeIpc);
        // }
        // beforeIpc = nullptr;
        beforeIpc.reset();
    }

    if (beforeAfter->get_active ()) {

        int errorCode = 0;
        rtengine::InitialImage *beforeImg = rtengine::InitialImage::load ( isrc->getImageSource ()->getFileName(),  openThm->getType() == FT_Raw, &errorCode, nullptr);

        if ( !beforeImg || errorCode ) {
            return;
        }

        beforeIarea = new ImageAreaPanel ();
        if (shortcut_mgr_) {
            beforeIarea->imageArea->setToolShortcutManager(shortcut_mgr_.get());
        }

        int HeaderBoxHeight = 17;

        beforeLabel = Gtk::manage (new Gtk::Label ());
        beforeLabel->set_markup (Glib::ustring ("<b>") + M ("GENERAL_BEFORE") + "</b>");
        tbBeforeLock = Gtk::manage (new Gtk::ToggleButton ());
        tbBeforeLock->set_relief(Gtk::RELIEF_NONE);
        tbBeforeLock->set_tooltip_markup (M ("MAIN_TOOLTIP_BEFOREAFTERLOCK"));
        tbBeforeLock->signal_toggled().connect ( sigc::mem_fun (*this, &EditorPanel::tbBeforeLock_toggled) );
        beforeHeaderBox = Gtk::manage (new Gtk::Box (Gtk::ORIENTATION_HORIZONTAL));
        beforeHeaderBox->get_style_context()->add_class("smallbuttonbox");
        beforeHeaderBox->pack_end (*tbBeforeLock, Gtk::PACK_SHRINK, 2);
        beforeHeaderBox->pack_end (*beforeLabel, Gtk::PACK_SHRINK, 2);
        beforeHeaderBox->set_size_request (0, HeaderBoxHeight);

        history->getBeforeAfterLock() ? tbBeforeLock->set_image (*iBeforeLockON) : tbBeforeLock->set_image (*iBeforeLockOFF);
        tbBeforeLock->set_active(history->getBeforeAfterLock());

        beforeBox = Gtk::manage (new Gtk::VBox ());
        beforeBox->pack_start (*beforeHeaderBox, Gtk::PACK_SHRINK, 2);
        beforeBox->pack_start (*beforeIarea);

        afterLabel = Gtk::manage (new Gtk::Label ());
        afterLabel->set_markup (Glib::ustring ("<b>") + M ("GENERAL_AFTER") + "</b>");
        afterHeaderBox = Gtk::manage (new Gtk::Box (Gtk::ORIENTATION_HORIZONTAL));
        afterHeaderBox->set_size_request (0, HeaderBoxHeight);
        afterHeaderBox->pack_end (*afterLabel, Gtk::PACK_SHRINK, 2);
        afterBox->pack_start (*afterHeaderBox, Gtk::PACK_SHRINK, 2);
        afterBox->reorder_child (*afterHeaderBox, 0);

        beforeAfterBox->pack_start (*beforeBox);
        beforeAfterBox->reorder_child (*beforeBox, 0);
        beforeAfterBox->show_all ();

        beforePreviewHandler = new PreviewHandler ();

        beforeIpc.reset(rtengine::StagedImageProcessor::create(beforeImg));
        beforeIpc->setPreviewScale (10);
        beforeIpc->setPreviewImageListener (beforePreviewHandler);
        Glib::ustring monitorProfile;
        rtengine::RenderingIntent intent;
        ipc->getMonitorProfile(monitorProfile, intent);
        beforeIpc->setMonitorProfile(monitorProfile, intent);

        beforeIarea->imageArea->setPreviewHandler (beforePreviewHandler);
        beforeIarea->imageArea->setImProcCoordinator (beforeIpc);

        beforeIarea->imageArea->setPreviewModePanel (iareapanel->imageArea->previewModePanel);
        beforeIarea->imageArea->setIndicateClippedPanel (iareapanel->imageArea->indClippedPanel);
        iareapanel->imageArea->iLinkedImageArea = beforeIarea->imageArea;

        iareapanel->setBeforeAfterViews (beforeIarea, iareapanel);
        beforeIarea->setBeforeAfterViews (beforeIarea, iareapanel);

        {
            auto cw = new CropWindow(beforeIarea->imageArea, false, false);
            cw->setDecorated(false);
            cw->setFitZoomEnabled(true);
            cw->addCropWindowListener(beforeIarea->imageArea);
            cw->setPosition(0, 0);
            cw->enable(false);
            cw->cropHandler.cropParams = iareapanel->imageArea->mainCropWindow->cropHandler.cropParams;
            beforeIarea->imageArea->mainCropWindow = cw;
        }
        
        rtengine::procparams::ProcParams params;

        if (history->getBeforeAfterParams(params)) {
            historyBeforeAfterChanged(params);
        }
    }
}

void EditorPanel::tbBeforeLock_toggled ()
{
    history->setBeforeAfterLock(tbBeforeLock->get_active());
    tbBeforeLock->get_active() ? tbBeforeLock->set_image (*iBeforeLockON) : tbBeforeLock->set_image (*iBeforeLockOFF);
}

void EditorPanel::histogramChanged(
    const LUTu& histRed,
    const LUTu& histGreen,
    const LUTu& histBlue,
    const LUTu& histLuma,
    const LUTu& histToneCurve,
    const LUTu& histLCurve,
    const LUTu& histCCurve,
    const LUTu& histLCAM,
    const LUTu& histCCAM,
    const LUTu& histRedRaw,
    const LUTu& histGreenRaw,
    const LUTu& histBlueRaw,
    const LUTu& histChroma,
    const LUTu& histLRETI,
    int vectorscopeScale,
    const array2D<int>& vectorscopeHC,
    const array2D<int>& vectorscopeHS,
    int waveformScale,
    const array2D<int>& waveformRed,
    const array2D<int>& waveformGreen,
    const array2D<int>& waveformBlue,
    const array2D<int>& waveformLuma
)
{
    if (histogramPanel) {
        histogramPanel->histogramChanged(histRed, histGreen, histBlue, histLuma, histChroma, histRedRaw, histGreenRaw, histBlueRaw, vectorscopeScale, vectorscopeHC, vectorscopeHS, waveformScale, waveformRed, waveformGreen, waveformBlue, waveformLuma);
    }

    tpc->updateCurveBackgroundHistogram(histToneCurve, histLCurve, histCCurve, histLCAM, histCCAM, histRed, histGreen, histBlue, histLuma, histLRETI);
}

void EditorPanel::setObservable(rtengine::HistogramObservable* observable)
{
    histogram_observable = observable;
}

bool EditorPanel::updateHistogram() const
{
    return histogram_scope_type == ScopeType::HISTOGRAM
        || histogram_scope_type == ScopeType::NONE;
}

bool EditorPanel::updateHistogramRaw() const
{
    return histogram_scope_type == ScopeType::HISTOGRAM_RAW
        || histogram_scope_type == ScopeType::NONE;
}

bool EditorPanel::updateVectorscopeHC() const
{
    return
        histogram_scope_type == ScopeType::VECTORSCOPE_HC
        || histogram_scope_type == ScopeType::NONE;
}

bool EditorPanel::updateVectorscopeHS() const
{
    return
        histogram_scope_type == ScopeType::VECTORSCOPE_HS
        || histogram_scope_type == ScopeType::NONE;
}

bool EditorPanel::updateWaveform() const
{
    return histogram_scope_type == ScopeType::WAVEFORM
        || histogram_scope_type == ScopeType::PARADE
        || histogram_scope_type == ScopeType::NONE;
}


void EditorPanel::scopeTypeChanged(ScopeType new_type)
{
    histogram_scope_type = new_type;

    if (!histogram_observable) {
        return;
    }

    // Make sure the new scope is updated since we only actively update the
    // current scope.
    switch (new_type) {
        case ScopeType::HISTOGRAM:
            histogram_observable->requestUpdateHistogram();
            break;
        case ScopeType::HISTOGRAM_RAW:
            histogram_observable->requestUpdateHistogramRaw();
            break;
        case ScopeType::VECTORSCOPE_HC:
            histogram_observable->requestUpdateVectorscopeHC();
            break;
        case ScopeType::VECTORSCOPE_HS:
            histogram_observable->requestUpdateVectorscopeHS();
            break;
        case ScopeType::PARADE:
        case ScopeType::WAVEFORM:
            histogram_observable->requestUpdateWaveform();
            break;
        case ScopeType::NONE:
            break;
    }
}


bool EditorPanel::CheckSidePanelsVisibility()
{
    if (tbTopPanel_1) {
        return tbTopPanel_1->get_active() || tbRightPanel_1->get_active() || hidehp->get_active();
    }

    return tbRightPanel_1->get_active() || hidehp->get_active();
}

void EditorPanel::toggleSidePanels()
{
    // Maximize preview panel:
    // toggle top AND right AND history panels

    bool bAllSidePanelsVisible;
    bAllSidePanelsVisible = CheckSidePanelsVisibility();

    if (tbTopPanel_1) {
        tbTopPanel_1->set_active (!bAllSidePanelsVisible);
    }

    tbRightPanel_1->set_active (!bAllSidePanelsVisible);
    hidehp->set_active (!bAllSidePanelsVisible);

    if (!bAllSidePanelsVisible) {
        tbShowHideSidePanels->set_image (*iShowHideSidePanels);
    } else {
        tbShowHideSidePanels->set_image (*iShowHideSidePanels_exit);
    }
}

void EditorPanel::toggleSidePanelsZoomFit()
{
    toggleSidePanels();

    // fit image preview
    // !!! TODO this does not want to work... seems to have an effect on a subsequent key press
    // iarea->imageArea->zoomPanel->zoomFitClicked();
}

void EditorPanel::tbShowHideSidePanels_managestate()
{
    bool bAllSidePanelsVisible;
    bAllSidePanelsVisible = CheckSidePanelsVisibility();
    ShowHideSidePanelsconn.block (true);

    tbShowHideSidePanels->set_active (!bAllSidePanelsVisible);

    ShowHideSidePanelsconn.block (false);
}

void EditorPanel::updateProfiles (const Glib::ustring &printerProfile, rtengine::RenderingIntent printerIntent, bool printerBPC)
{
}

void EditorPanel::updateTPVScrollbar (bool hide)
{
    tpc->updateTPVScrollbar (hide);
}

void EditorPanel::updateHistogramPosition (int oldPosition, int newPosition)
{

    switch (newPosition) {
        case 0:

            // No histogram
            if (!oldPosition) {
                // An histogram actually exist, we delete it
                delete histogramPanel;
                histogramPanel = nullptr;
            }

            // else no need to create it
            break;

        case 1:

            // Histogram on the left pane
            if (oldPosition == 0) {
                // There was no Histogram before, so we create it
                histogramPanel = Gtk::manage (new HistogramPanel ());
                leftbox->pack1(*histogramPanel, false, false);
            } else if (oldPosition == 2) {
                // The histogram was on the right side, so we move it to the left
                histogramPanel->reference();
                removeIfThere (vboxright, histogramPanel, false);
                leftbox->pack1(*histogramPanel, false, false);
                histogramPanel->unreference();
            }

            leftbox->set_position(options.histogramHeight);
            histogramPanel->reorder (Gtk::POS_LEFT);
            break;

        case 2:
        default:

            // Histogram on the right pane
            if (oldPosition == 0) {
                // There was no Histogram before, so we create it
                histogramPanel = Gtk::manage (new HistogramPanel ());
                vboxright->pack1 (*histogramPanel, false, false);
            } else if (oldPosition == 1) {
                // The histogram was on the left side, so we move it to the right
                histogramPanel->reference();
                removeIfThere (leftbox, histogramPanel, false);
                vboxright->pack1 (*histogramPanel, false, false);
                histogramPanel->unreference();
            }

            vboxright->set_position(options.histogramHeight);
            histogramPanel->reorder (Gtk::POS_RIGHT);
            break;
    }
 
    if (histogramPanel) {
        histogramPanel->setPanelListener(this);
    }

    iareapanel->imageArea->setPointerMotionHListener (histogramPanel);

}


void EditorPanel::defaultMonitorProfileChanged (const Glib::ustring &profile_name, bool auto_monitor_profile)
{
    colorMgmtToolBar->defaultMonitorProfileChanged (profile_name, auto_monitor_profile);
}


bool EditorPanel::autosave()
{
    MyProgressBar* const pl = progressLabel;
    auto prev = pl->get_text();
    pl->set_text(M("MAIN_MSG_AUTOSAVING"));
    const auto doit =
        [pl, prev]() -> bool
        {
            pl->set_text(prev);
            return false;
        };
    Glib::signal_timeout().connect(sigc::slot<bool>(doit), 1000);
    saveProfile();
    return false;
}


void EditorPanel::sizeChanged(int w, int h, int ow, int oh)
{
    if (ipc) {
        idle_register.add(
            [this]() -> bool
            {
                if (ipc) {
                    info_toggled();
                    navigator->setInvalid(ipc->getFullWidth(), ipc->getFullHeight());
                }
                return false;
            });
    }    
}


void EditorPanel::setParent(RTWindow *p)
{
    parent = p;
    shortcut_mgr_.reset(new ToolShortcutManager(p));
    tpc->setToolShortcutManager(shortcut_mgr_.get());
    iareapanel->imageArea->setToolShortcutManager(shortcut_mgr_.get());
}
