/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2019 Jean-Christophe FRISCH <natureh.510@gmail.com>
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

#include "edit.h"
#include "spot.h"
#include "rtimage.h"
#include <iomanip>
#include "../rtengine/rt_math.h"
#include "guiutils.h"
#include "eventmapper.h"
#include "../rtengine/refreshmap.h"

using namespace rtengine;
using namespace rtengine::procparams;

namespace {

constexpr int STATIC_VISIBLE_OBJ_NBR = 6;
constexpr int STATIC_MO_OBJ_NBR = 7;

constexpr int SOURCE_DISC = 2;
constexpr int TARGET_DISC = 1;

} // namespace


Spot::Spot() :
    FoldableToolPanel(this, "spot", M ("TP_SPOT_LABEL"), false, true, true),
    EditSubscriber(ET_OBJECTS),
    draggedSide(DraggedSide::NONE),
    lastObject(-1),
    activeSpot(-1),
    sourceIcon("spot-normal.png", "spot-active.png", "spot-prelight.png", "", "", Geometry::DP_CENTERCENTER),
    targetIcon("spot-normal-target.png", "spot-active-target.png", "spot-prelight-target.png", "", "", Geometry::DP_CENTERCENTER),
	lightPipelineOnImage("light_pipeline_on.png"),
	lightPipelineOffImage("light_pipeline_off.png"),
    editedCheckBox(nullptr)
{
    countLabel = Gtk::manage (new Gtk::Label (Glib::ustring::compose (M ("TP_SPOT_COUNTLABEL"), 0)));

    edit = Gtk::manage (new Gtk::ToggleButton());
    edit->add (*Gtk::manage (new RTImage ("edit-point.png")));
    editConn = edit->signal_toggled().connect ( sigc::mem_fun (*this, &Spot::editToggled) );
    edit->set_tooltip_text(M("TP_SPOT_HINT"));

    lightPipeline = Gtk::manage (new Gtk::ToggleButton());
    lightPipeline->add (*Gtk::manage (new RTImage (lightPipelineOffImage)));
    lightPipelineConn = lightPipeline->signal_toggled().connect ( sigc::mem_fun (*this, &Spot::lightPipelineToggled) );
    lightPipeline->set_tooltip_text(M("TP_SPOT_LIGHTPIPELINE_HINT"));

    reset = Gtk::manage (new Gtk::Button ());
    reset->add (*Gtk::manage (new RTImage ("undo-small.png")));
    reset->set_relief (Gtk::RELIEF_NONE);
    reset->set_border_width (0);
    reset->signal_clicked().connect ( sigc::mem_fun (*this, &Spot::resetPressed) );

    labelBox = Gtk::manage (new Gtk::HBox());
    labelBox->set_spacing (2);
    labelBox->pack_start (*countLabel, false, false, 0);
    labelBox->pack_end (*lightPipeline, false, false, 0);
    labelBox->pack_end (*edit, false, false, 0);
    labelBox->pack_end (*reset, false, false, 0);
    pack_start (*labelBox);

    sourceIcon.datum = Geometry::IMAGE;
    sourceIcon.setActive (false);
    sourceIcon.state = Geometry::ACTIVE;
    sourceCircle.datum = Geometry::IMAGE;
    sourceCircle.setActive (false);
    sourceCircle.radiusInImageSpace = true;
    sourceCircle.setDashed(true);
    sourceMODisc.datum = Geometry::IMAGE;
    sourceMODisc.setActive (false);
    sourceMODisc.radiusInImageSpace = true;
    sourceMODisc.filled = true;
    sourceMODisc.innerLineWidth = 0.;
    targetCircle.datum = Geometry::IMAGE;
    targetCircle.setActive (false);
    targetCircle.radiusInImageSpace = true;
    targetMODisc.datum = Geometry::IMAGE;
    targetMODisc.setActive (false);
    targetMODisc.radiusInImageSpace = true;
    targetMODisc.filled = true;
    targetMODisc.innerLineWidth = 0.;
    sourceFeatherCircle.datum = Geometry::IMAGE;
    sourceFeatherCircle.setActive (false);
    sourceFeatherCircle.radiusInImageSpace = true;
    sourceFeatherCircle.setDashed(true);
    sourceFeatherCircle.innerLineWidth = 0.7;
    targetFeatherCircle.datum = Geometry::IMAGE;
    targetFeatherCircle.setActive (false);
    targetFeatherCircle.radiusInImageSpace = true;
    targetFeatherCircle.innerLineWidth = 0.7;
    link.datum = Geometry::IMAGE;
    link.setActive (false);

    Rectangle *rect = new Rectangle();
    whole_area_rectangle.reset(rect);
    rect->filled = true;
    rect->setActive(true);
    rect->datum = Geometry::IMAGE;

    auto m = ProcEventMapper::getInstance();
    EvSpotEnabled = m->newEvent(rtengine::ALLNORAW, "TP_SPOT_LABEL");
    EvSpotEnabledOPA = m->newAnonEvent(rtengine::SPOTADJUST);
    EvSpotEntry = m->newEvent(rtengine::SPOTADJUST, "HISTORY_MSG_SPOT_ENTRY");
    EvSpotEntryOPA = m->newEvent(rtengine::SPOTADJUST, "HISTORY_MSG_SPOT_ENTRY");
    EvToolReset.set_action(rtengine::SPOTADJUST);

    spot_frame = Gtk::manage(new Gtk::Frame(M("TP_SPOT_CUR_SPOT_LABEL")));
    Gtk::VBox *vb = Gtk::manage(new Gtk::VBox());
    const auto mkadj =
        [](const Glib::ustring &lbl, double vmin, double vmax, double vstep, double vdefault) -> Adjuster *
        {
            return Gtk::manage(new Adjuster(lbl, vmin, vmax, vstep, vdefault, nullptr, nullptr, nullptr, nullptr, false, true));
        };
    source_x = mkadj(M("TP_SPOT_SOURCE_X") + " ", 0, 10000, 1, 0);
    source_y = mkadj(M("TP_SPOT_SOURCE_Y") + " ", 0, 10000, 1, 0);
    target_y = mkadj(M("TP_SPOT_TARGET_Y") + " ", 0, 10000, 1, 0);
    target_x = mkadj(M("TP_SPOT_TARGET_X") + " ", 0, 10000, 1, 0);
    radius = mkadj(M("TP_SPOT_RADIUS") + " ", SpotParams::minRadius, SpotParams::maxRadius, 1, SpotParams::minRadius);
    feather = mkadj(M("TP_SPOT_FEATHER") + " ", 0, 1, 0.01, 0);
    opacity = mkadj(M("TP_SPOT_OPACITY") + " ", 0, 1, 0.01, 0);
    detail = mkadj(M("TP_SPOT_DETAIL") + " ", 0, 5, 1, 0);

    spot_adjusters = {
        source_x,
        source_y,
        target_x,
        target_y,
        radius,
        feather,
        opacity,
        detail
    };

    for (auto a : spot_adjusters) {
        a->setAdjusterListener(this);
        vb->pack_start(*a, Gtk::PACK_SHRINK, 2);
    }

    spot_frame->add(*vb);
    pack_start(*spot_frame);

    getExpander()->signal_button_release_event().connect_notify(sigc::mem_fun(this, &Spot::on_fold));
    signal_unmap().connect(sigc::mem_fun(*this, &Spot::on_hide));
    
    show_all();
}

Spot::~Spot()
{
    // delete all dynamically allocated geometry
    if (EditSubscriber::visibleGeometry.size()) {
        for (size_t i = 0; i < EditSubscriber::visibleGeometry.size() - STATIC_VISIBLE_OBJ_NBR; ++i) { // static visible geometry at the end if the list
            delete EditSubscriber::visibleGeometry.at (i);
        }
    }

    // We do not delete the mouseOverGeometry, because the referenced objects are either
    // shared with visibleGeometry or instantiated by the class's ctor
}


void Spot::read (const ProcParams *pp)
{
    disableListener ();

    spots = pp->spot.entries;
    setEnabled (pp->spot.enabled);
    lastEnabled = pp->spot.enabled;
    activeSpot = -1;
    lastObject = -1;

    createGeometry();
    updateGeometry();

    enableListener ();
}


void Spot::write(ProcParams *pp)
{
    pp->spot.enabled = getEnabled();
    pp->spot.entries = spots;
}


void Spot::resetPressed()
{
    if (!spots.empty()) {
        spots.clear();
        activeSpot = -1;
        lastObject = -1;
        createGeometry();
        updateGeometry();

        if (listener) {
            listener->panelChanged (edit->get_active() ? EvSpotEntryOPA : EvSpotEntry, Glib::ustring::compose (M ("TP_SPOT_COUNTLABEL"), 0));
        }
    }
}


void Spot::editedToggled ()
{
    if (listener) {
        listener->panelChanged (EvSpotEntry, !editedCheckBox->get_active() ? M ("GENERAL_UNCHANGED") : Glib::ustring::compose (M ("TP_SPOT_COUNTLABEL"), spots.size()));
    }
}

void Spot::enabledChanged ()
{
    if (listener) {
        listener->panelChanged(EvSpotEnabled, getEnabled() ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
        // if (get_inconsistent()) {
        //     listener->panelChanged (edit->get_active() ? EvSpotEnabledOPA : EvSpotEnabled, M ("GENERAL_UNCHANGED"));
        // } else if (getEnabled()) {
        //     listener->panelChanged (edit->get_active() ? EvSpotEnabledOPA : EvSpotEnabled, M ("GENERAL_ENABLED"));
        // } else {
        //     listener->panelChanged (edit->get_active() ? EvSpotEnabledOPA : EvSpotEnabled, M ("GENERAL_DISABLED"));
        // }
    }
}

void Spot::setEditProvider(EditDataProvider* provider)
{
    EditSubscriber::setEditProvider(provider);
    if (provider) {
        int imW, imH;
        provider->getImageSize(imW, imH);

        static_cast<Rectangle *>(whole_area_rectangle.get())->bottomRight.x = imW;
        static_cast<Rectangle *>(whole_area_rectangle.get())->bottomRight.y = imH;
    }
}


void Spot::editToggled()
{
    if (listener) {
        if (edit->get_active()) {
            listener->setTweakOperator(this);
            listener->refreshPreview(EvSpotEnabledOPA); // reprocess the preview w/o creating History entry
            subscribe();
        } else {
            reset_adjusters();
            unsubscribe();
            listener->unsetTweakOperator(this);
            listener->refreshPreview(EvSpotEnabled); // reprocess the preview w/o creating History entry
        }
    }
}


void Spot::lightPipelineToggled()
{
    if (listener && edit->get_active()) {
        listener->refreshPreview(EvSpotEnabledOPA); // reprocess the preview w/o creating History entry
        //NOTE: an option need to be created if we want to make this button state persistent across session
    }
    lightPipeline->set_image(lightPipeline->get_active() ? lightPipelineOnImage : lightPipelineOffImage);
}


Geometry* Spot::getVisibleGeometryFromMO(int MOID)
{
    if (MOID == -1) {
        return nullptr;
    }

    if (MOID == 1) {
        return getActiveSpotIcon();
    }

    if (MOID == SOURCE_DISC) { // sourceMODisc
        return &sourceIcon;
    }

    if (MOID > STATIC_MO_OBJ_NBR) {
        return EditSubscriber::visibleGeometry.at(MOID - STATIC_MO_OBJ_NBR);
    }

    return EditSubscriber::mouseOverGeometry.at(MOID);
}

void Spot::createGeometry ()
{
    int nbrEntry = spots.size();
    countLabel->set_text (Glib::ustring::compose (M ("TP_SPOT_COUNTLABEL"), nbrEntry));

    //printf("CreateGeometry(%d)\n", nbrEntry);
    // delete all dynamically allocated geometry
    if (EditSubscriber::visibleGeometry.size() > STATIC_VISIBLE_OBJ_NBR)
        for (size_t i = 0; i < EditSubscriber::visibleGeometry.size() - STATIC_VISIBLE_OBJ_NBR; ++i) { // static visible geometry at the end if the list
            delete EditSubscriber::visibleGeometry.at (i);
        }

    // mouse over geometry starts with the static geometry, then the spot's icon geometry
    EditSubscriber::mouseOverGeometry.resize (STATIC_MO_OBJ_NBR + nbrEntry);
    // visible geometry starts with the spot's icon geometry, then the static geometry
    EditSubscriber::visibleGeometry.resize (nbrEntry + STATIC_VISIBLE_OBJ_NBR);

    size_t i = 0, j = 0;
    mouseOverGeometry[i++] = whole_area_rectangle.get();
    mouseOverGeometry[i++] = &targetMODisc;
    mouseOverGeometry[i++] = &sourceMODisc;
    mouseOverGeometry[i++] = &targetCircle;
    mouseOverGeometry[i++] = &sourceCircle;
    mouseOverGeometry[i++] = &targetFeatherCircle;
    mouseOverGeometry[i++] = &sourceFeatherCircle;

    // recreate all spots geometry
    Cairo::RefPtr<RTSurface> normalImg   = targetIcon.getNormalImg();
    Cairo::RefPtr<RTSurface> prelightImg = targetIcon.getPrelightImg();
    Cairo::RefPtr<RTSurface> activeImg   = targetIcon.getActiveImg();

    for (; j < EditSubscriber::visibleGeometry.size() - STATIC_VISIBLE_OBJ_NBR; ++i, ++j) {
        EditSubscriber::mouseOverGeometry.at (i) = EditSubscriber::visibleGeometry.at (j) = new OPIcon (normalImg, activeImg, prelightImg, Cairo::RefPtr<RTSurface> (nullptr), Cairo::RefPtr<RTSurface> (nullptr), Geometry::DP_CENTERCENTER);
        EditSubscriber::visibleGeometry.at (j)->setActive (true);
        EditSubscriber::visibleGeometry.at (j)->datum = Geometry::IMAGE;
        EditSubscriber::visibleGeometry.at (j)->state = Geometry::NORMAL;
        //printf("mouseOverGeometry.at(%d) = %p\n", (unsigned int)i, (void*)EditSubscriber::mouseOverGeometry.at(i));
    }

    visibleGeometry[j++] = &sourceIcon;
    visibleGeometry[j++] = &sourceFeatherCircle;
    visibleGeometry[j++] = &link;
    visibleGeometry[j++] = &sourceCircle;
    visibleGeometry[j++] = &targetFeatherCircle;
    visibleGeometry[j++] = &targetCircle;
}

void Spot::updateGeometry()
{
    EditDataProvider* dataProvider = getEditProvider();

    if (dataProvider) {
        int imW, imH;
        dataProvider->getImageSize (imW, imH);

        static_cast<Rectangle *>(whole_area_rectangle.get())->bottomRight.x = imW;
        static_cast<Rectangle *>(whole_area_rectangle.get())->bottomRight.y = imH;
        source_x->setLimits(0, imW, 1, 0);
        target_x->setLimits(0, imW, 1, 0);
        source_y->setLimits(0, imH, 1, 0);
        target_y->setLimits(0, imH, 1, 0);

        if (activeSpot > -1) {
            // Target point circle
            targetCircle.center = spots.at (activeSpot).targetPos;
            targetCircle.radius = spots.at (activeSpot).radius;
            targetCircle.setActive (true);

            // Target point Mouse Over disc
            targetMODisc.center = targetCircle.center;
            targetMODisc.radius = targetCircle.radius;
            targetMODisc.setActive (true);

            // Source point Icon
            sourceIcon.position = spots.at (activeSpot).sourcePos;
            sourceIcon.setActive (true);

            // Source point circle
            sourceCircle.center = spots.at (activeSpot).sourcePos;
            sourceCircle.radius = spots.at (activeSpot).radius;
            sourceCircle.setActive (true);

            // Source point Mouse Over disc
            sourceMODisc.center = sourceCircle.center;
            sourceMODisc.radius = sourceCircle.radius;
            sourceMODisc.setActive (true);

            // Target point feather circle
            targetFeatherCircle.center = spots.at (activeSpot).targetPos;
            targetFeatherCircle.radius = float (spots.at (activeSpot).radius) * (1.f + spots.at (activeSpot).feather);
            targetFeatherCircle.radiusInImageSpace = true;
            targetFeatherCircle.setActive (true);

            // Source point feather circle
            sourceFeatherCircle.center = spots.at (activeSpot).sourcePos;
            sourceFeatherCircle.radius = targetFeatherCircle.radius;
            sourceFeatherCircle.setActive (true);

            // Link line
            PolarCoord p;
            p = targetCircle.center - sourceCircle.center;

            if (p.radius > sourceCircle.radius + targetCircle.radius) {
                PolarCoord p2 (sourceCircle.radius, p.angle);
                Coord p3;
                p3 = p2;
                link.begin = sourceCircle.center + p3;
                p2.set (targetCircle.radius, p.angle + 180);
                p3 = p2;
                link.end = targetCircle.center + p3;
                link.setActive (true);
            } else {
                link.setActive (false);
            }

            sourceCircle.setVisible(draggedSide != DraggedSide::SOURCE);
            targetCircle.setVisible(draggedSide != DraggedSide::TARGET);
            link.setVisible(draggedSide == DraggedSide::NONE);

            {
                auto &s = spots[activeSpot];
                spot_frame->set_sensitive(true);
                source_x->setValue(s.sourcePos.x);
                source_y->setValue(s.sourcePos.y);
                target_x->setValue(s.targetPos.x);
                target_y->setValue(s.targetPos.y);
                radius->setValue(s.radius);
                feather->setValue(s.feather);
                opacity->setValue(s.opacity);
                detail->setValue(s.detail);
            }
        } else {
            targetCircle.setActive (false);
            targetMODisc.setActive (false);
            sourceIcon.setActive (false);
            sourceCircle.setActive (false);
            sourceMODisc.setActive (false);
            targetFeatherCircle.setActive (false);
            sourceFeatherCircle.setActive (false);
            link.setActive (false);

            reset_adjusters();
        }

        for (size_t i = 0; i < spots.size(); ++i) {
            // Target point icon
            OPIcon* geom = static_cast<OPIcon*> (EditSubscriber::visibleGeometry.at (i));
            geom->position = spots.at (i).targetPos;
            geom->setActive (true);

            if (int (i) == activeSpot) {
                geom->setHoverable (false);
            }
        }
    } else {
        reset_adjusters();
    }
}


OPIcon *Spot::getActiveSpotIcon()
{
    if (activeSpot > -1) {
        return static_cast<OPIcon*> (EditSubscriber::visibleGeometry.at (activeSpot));
    }

    return nullptr;
}

void Spot::addNewEntry()
{
    EditDataProvider* editProvider = getEditProvider();
    // we create a new entry
    SpotEntry se;
    se.targetPos = editProvider->posImage;
    se.sourcePos = se.targetPos;
    spots.push_back (se); // this make a copy of se ...
    activeSpot = spots.size() - 1;
    lastObject = 1;

    //printf("ActiveSpot = %d\n", activeSpot);

    createGeometry();
    updateGeometry();
    EditSubscriber::visibleGeometry.at (activeSpot)->state = Geometry::ACTIVE;
    sourceIcon.state = Geometry::DRAGGED;
    // TODO: find a way to disable the active spot's Mouse Over geometry but still displaying its location...

    if (listener) {
        listener->panelChanged (EvSpotEntryOPA, M ("TP_SPOT_ENTRYCHANGED"));
    }
}

void Spot::deleteSelectedEntry()
{
    // delete the activeSpot
    if (activeSpot > -1) {
        std::vector<rtengine::procparams::SpotEntry>::iterator i = spots.begin();

        for (int j = 0; j < activeSpot; ++j) {
            ++i;
        }

        spots.erase (i);
    }

    lastObject = -1;
    activeSpot = -1;

    createGeometry();
    updateGeometry();

    if (listener) {
        listener->panelChanged (EvSpotEntry, M ("TP_SPOT_ENTRYCHANGED"));
    }
}

CursorShape Spot::getCursor(int objectID, int xPos, int yPos)
{
    EditDataProvider* editProvider = getEditProvider();
    if (editProvider) {
        if (draggedSide != DraggedSide::NONE) {
            return CSEmpty;
        }

        if (objectID == TARGET_DISC || objectID == SOURCE_DISC) {
            return CSMove2D;
        }
        if (objectID >= 3 && objectID <= 6 && activeSpot > -1) {
            Coord delta(Coord(xPos, yPos) - ((objectID == 4 || objectID == 6) ? spots.at(activeSpot).sourcePos : spots.at(activeSpot).targetPos));
            PolarCoord polarPos(delta);
            if (polarPos.angle < 0.) {
                polarPos.angle += 180.;
            }
            if (polarPos.angle < 22.5 || polarPos.angle >= 157.5) {
                return CSMove1DH;
            }
            if (polarPos.angle < 67.5) {
                return CSResizeBottomRight;
            }
            if (polarPos.angle < 112.5) {
                return CSMove1DV;
            }
            return CSResizeBottomLeft;
        }
    }
    return CSCrosshair;
}

bool Spot::mouseOver (int modifierKey)
{
    EditDataProvider* editProvider = getEditProvider();

    if (editProvider && editProvider->getObject() != lastObject) {
        if (lastObject > -1) {
            if (EditSubscriber::mouseOverGeometry.at (lastObject) == &targetMODisc) {
                getVisibleGeometryFromMO (lastObject)->state = Geometry::ACTIVE;
            } else {
                getVisibleGeometryFromMO (lastObject)->state = Geometry::NORMAL;
            }

            sourceIcon.state = Geometry::ACTIVE;
        }

        if (editProvider->getObject() > -1) {
            getVisibleGeometryFromMO (editProvider->getObject())->state = Geometry::PRELIGHT;

            if (editProvider->getObject() >= STATIC_MO_OBJ_NBR) {
                // a Spot is being edited
                int oldActiveSpot = activeSpot;
                activeSpot = editProvider->getObject() - STATIC_MO_OBJ_NBR;

                if (activeSpot != oldActiveSpot) {
                    if (oldActiveSpot > -1) {
                        EditSubscriber::visibleGeometry.at (oldActiveSpot)->state = Geometry::NORMAL;
                        EditSubscriber::mouseOverGeometry.at (oldActiveSpot + STATIC_MO_OBJ_NBR)->state = Geometry::NORMAL;
                    }

                    EditSubscriber::visibleGeometry.at (activeSpot)->state = Geometry::PRELIGHT;
                    EditSubscriber::mouseOverGeometry.at (activeSpot + STATIC_MO_OBJ_NBR)->state = Geometry::PRELIGHT;
                    //printf("ActiveSpot = %d (was %d before)\n", activeSpot, oldActiveSpot);
                }
            }
        }

        lastObject = editProvider->getObject();
        if (lastObject == 0) {
            lastObject = -1;
        }

        if (lastObject > -1 && EditSubscriber::mouseOverGeometry.at (lastObject) == getActiveSpotIcon()) {
            lastObject = TARGET_DISC;
        }

        updateGeometry();
        return true;
    }

    return false;
}

// Create a new Target and Source point or start the drag of a Target point under the cursor
bool Spot::button1Pressed (int modifierKey)
{
    EditDataProvider* editProvider = getEditProvider();

    if (editProvider) {
        if (lastObject == -1 && (modifierKey & GDK_CONTROL_MASK)) {
            draggedSide = DraggedSide::SOURCE;
            addNewEntry();
            EditSubscriber::action = ES_ACTION_DRAGGING;
            return true;
        } else if (lastObject > -1) {
            draggedSide = lastObject == TARGET_DISC ? DraggedSide::TARGET : lastObject == SOURCE_DISC ? DraggedSide::SOURCE : DraggedSide::NONE;
            getVisibleGeometryFromMO (lastObject)->state = Geometry::DRAGGED;
            EditSubscriber::action = ES_ACTION_DRAGGING;
            return true;
        }
    }

    return false;
}

// End the drag of a Target point
bool Spot::button1Released()
{
    Geometry *loGeom = getVisibleGeometryFromMO (lastObject);

    if (!loGeom) {
        EditSubscriber::action = ES_ACTION_NONE;
        return false;
    }

    loGeom->state = Geometry::PRELIGHT;
    EditSubscriber::action = ES_ACTION_NONE;
    draggedSide = DraggedSide::NONE;
    updateGeometry();
    return true;
}

// Delete a point
bool Spot::button2Pressed (int modifierKey)
{
    EditDataProvider* editProvider = getEditProvider();

    if (!editProvider || lastObject == -1 || activeSpot == -1) {
        return false;
    }

    if (! (modifierKey & (GDK_SHIFT_MASK | GDK_SHIFT_MASK))) {
        EditSubscriber::action = ES_ACTION_PICKING;
    }

    return false;
}

// Create a new Target and Source point or start the drag of a Target point under the cursor
bool Spot::button3Pressed (int modifierKey)
{
    EditDataProvider* editProvider = getEditProvider();

    if (!editProvider || lastObject == -1 || activeSpot == -1) {
        return false;
    }

    if ((modifierKey & GDK_CONTROL_MASK) && (EditSubscriber::mouseOverGeometry.at (lastObject) == &targetMODisc || lastObject >= STATIC_MO_OBJ_NBR)) {
        lastObject = SOURCE_DISC;
        sourceIcon.state = Geometry::DRAGGED;
        EditSubscriber::action = ES_ACTION_DRAGGING;
        draggedSide = DraggedSide::SOURCE;
        return true;
    } else if (! (modifierKey & (GDK_SHIFT_MASK | GDK_SHIFT_MASK))) {
        EditSubscriber::action = ES_ACTION_PICKING;
    }

    return false;
}

bool Spot::button3Released()
{
    Geometry *loGeom = getVisibleGeometryFromMO (lastObject);

    if (!loGeom) {
        EditSubscriber::action = ES_ACTION_NONE;
        return false;
    }

    lastObject = -1;
    sourceIcon.state = Geometry::ACTIVE;
    draggedSide = DraggedSide::NONE;
    updateGeometry();
    EditSubscriber::action = ES_ACTION_NONE;
    return true;

    return false;
}

bool Spot::drag1 (int modifierKey)
{
    EditDataProvider *editProvider = getEditProvider();
    int imW, imH;
    editProvider->getImageSize (imW, imH);
    bool modified = false;

    //printf("Drag1 / LastObject=%d\n", lastObject);

    Geometry *loGeom = EditSubscriber::mouseOverGeometry.at (lastObject);

    if (loGeom == &sourceMODisc) {
        //printf("sourceMODisc / deltaPrevImage = %d / %d\n", editProvider->deltaPrevImage.x, editProvider->deltaPrevImage.y);
        rtengine::Coord currPos = spots.at (activeSpot).sourcePos;
        spots.at (activeSpot).sourcePos += editProvider->deltaPrevImage;
        spots.at (activeSpot).sourcePos.clip (imW, imH);

        if (spots.at (activeSpot).sourcePos != currPos) {
            modified = true;
        }

        EditSubscriber::mouseOverGeometry.at (activeSpot + STATIC_MO_OBJ_NBR)->state = Geometry::DRAGGED;
    } else if (loGeom == &targetMODisc || lastObject >= STATIC_MO_OBJ_NBR) {
        //printf("targetMODisc / deltaPrevImage = %d / %d\n", editProvider->deltaPrevImage.x, editProvider->deltaPrevImage.y);
        rtengine::Coord currPos = spots.at (activeSpot).targetPos;
        spots.at (activeSpot).targetPos += editProvider->deltaPrevImage;
        spots.at (activeSpot).targetPos.clip (imW, imH);

        if (spots.at (activeSpot).targetPos != currPos) {
            modified = true;
        }
    } else if (loGeom == &sourceCircle) {
        //printf("sourceCircle / deltaPrevImage = %d / %d\n", editProvider->deltaImage.x, editProvider->deltaImage.y);
        int lastRadius = spots.at (activeSpot).radius;
        rtengine::Coord currPos = editProvider->posImage + editProvider->deltaImage;
        rtengine::PolarCoord currPolar (currPos - spots.at (activeSpot).sourcePos);
        spots.at (activeSpot).radius = LIM<int> (int (currPolar.radius), SpotParams::minRadius, SpotParams::maxRadius);

        if (spots.at (activeSpot).radius != lastRadius) {
            modified = true;
        }
    } else if (loGeom == &targetCircle) {
        //printf("targetCircle / deltaPrevImage = %d / %d\n", editProvider->deltaImage.x, editProvider->deltaImage.y);
        int lastRadius = spots.at (activeSpot).radius;
        rtengine::Coord currPos = editProvider->posImage + editProvider->deltaImage;
        rtengine::PolarCoord currPolar (currPos - spots.at (activeSpot).targetPos);
        spots.at (activeSpot).radius = LIM<int> (int (currPolar.radius), SpotParams::minRadius, SpotParams::maxRadius);

        if (spots.at (activeSpot).radius != lastRadius) {
            modified = true;
        }
    } else if (loGeom == &sourceFeatherCircle) {
        //printf("sourceFeatherCircle / deltaPrevImage = %d / %d\n", editProvider->deltaImage.x, editProvider->deltaImage.y);
        float currFeather = spots.at (activeSpot).feather;
        rtengine::Coord currPos = editProvider->posImage + editProvider->deltaImage;
        rtengine::PolarCoord currPolar (currPos - spots.at (activeSpot).sourcePos);
        spots.at (activeSpot).feather = LIM01<float> ((currPolar.radius - double (spots.at (activeSpot).radius)) / double (spots.at (activeSpot).radius));

        if (spots.at (activeSpot).feather != currFeather) {
            modified = true;
        }
    } else if (loGeom == &targetFeatherCircle) {
        //printf("targetFeatherCircle / deltaPrevImage = %d / %d\n", editProvider->deltaImage.x, editProvider->deltaImage.y);
        float currFeather = spots.at (activeSpot).feather;
        rtengine::Coord currPos = editProvider->posImage + editProvider->deltaImage;
        rtengine::PolarCoord currPolar (currPos - spots.at (activeSpot).targetPos);
        spots.at (activeSpot).feather = LIM01<float> ((currPolar.radius - double (spots.at (activeSpot).radius)) / double (spots.at (activeSpot).radius));

        if (spots.at (activeSpot).feather != currFeather) {
            modified = true;
        }
    }

    if (listener && modified) {
        updateGeometry();
        listener->panelChanged (EvSpotEntry, M ("TP_SPOT_ENTRYCHANGED"));
    }

    return modified;
}

bool Spot::drag3 (int modifierKey)
{
    EditDataProvider *editProvider = getEditProvider();
    int imW, imH;
    editProvider->getImageSize (imW, imH);
    bool modified = false;

    Geometry *loGeom = EditSubscriber::mouseOverGeometry.at (lastObject);

    if (loGeom == &sourceMODisc) {
        rtengine::Coord currPos = spots.at (activeSpot).sourcePos;
        spots.at (activeSpot).sourcePos += editProvider->deltaPrevImage;
        spots.at (activeSpot).sourcePos.clip (imW, imH);

        if (spots.at (activeSpot).sourcePos != currPos) {
            modified = true;
        }
    }

    if (listener) {
        updateGeometry();
        listener->panelChanged (EvSpotEntry, M ("TP_SPOT_ENTRYCHANGED"));
    }

    return modified;
}

bool Spot::pick2 (bool picked)
{
    return pick3 (picked);
}

bool Spot::pick3 (bool picked)
{
    EditDataProvider* editProvider = getEditProvider();

    if (!picked) {
        if (editProvider->getObject() != lastObject) {
            return false;
        }
    }

    // Object is picked, we delete it
    deleteSelectedEntry();
    EditSubscriber::action = ES_ACTION_NONE;
    updateGeometry();
    return true;
}


void Spot::switchOffEditMode ()
{
    if (edit->get_active()) {
        // switching off the toggle button
        bool wasBlocked = editConn.block (true);
        edit->set_active (false);

        if (!wasBlocked) {
            editConn.block (false);
        }
    }

    reset_adjusters();

    EditSubscriber::switchOffEditMode();  // disconnect
    listener->unsetTweakOperator(this);
    listener->refreshPreview(EvSpotEnabled); // reprocess the preview w/o creating History entry
}


void Spot::tweakParams(procparams::ProcParams& pparams)
{
    //params->raw.bayersensor.method = RAWParams::BayerSensor::getMethodString(RAWParams::BayerSensor::Method::FAST);
    //params->raw.xtranssensor.method = RAWParams::XTransSensor::getMethodString(RAWParams::XTransSensor::Method::FAST);

    // -> disabling all transform
    //pparams.coarse = CoarseTransformParams();
    pparams.lensProf = LensProfParams();
    pparams.cacorrection = CACorrParams();
    pparams.distortion = DistortionParams();
    pparams.rotate = RotateParams();
    pparams.perspective = PerspectiveParams();
    pparams.vignetting = VignettingParams();

    // -> disabling standard crop
    pparams.crop.enabled = false;

    // -> disabling time consuming and unnecessary tool
    if (lightPipeline->get_active()) {
        pparams.colorcorrection.enabled = false;
        pparams.localContrast.enabled = false;
        pparams.smoothing.enabled = false;
        pparams.textureBoost.enabled = false;
        pparams.toneEqualizer.enabled = false;
        pparams.fattal.enabled = false;
        pparams.dehaze.enabled = false;
        pparams.filmSimulation.enabled = false;
        pparams.blackwhite.enabled = false;
        pparams.defringe.enabled = false;
        pparams.sharpening.enabled = false;
        pparams.impulseDenoise.enabled = false;
        pparams.softlight.enabled = false;
        pparams.gradient.enabled = false;
        pparams.pcvignette.enabled = false;

        //users might want to edit on a denoised image, no problem to preview it since it doesn't seem to slow things down
        //pparams.denoise = false;
    }
}


void Spot::adjusterChanged(Adjuster *a, double newval)
{
    if (activeSpot > -1) {
        auto &s = spots[activeSpot];
        s.sourcePos.x = source_x->getValue();
        s.sourcePos.y = source_y->getValue();
        s.targetPos.x = target_x->getValue();
        s.targetPos.y = target_y->getValue();
        s.radius = radius->getValue();
        s.feather = feather->getValue();
        s.opacity = opacity->getValue();
        s.detail = detail->getValue();
    }
    
    if (listener && getEnabled()) {
        disableListener();
        updateGeometry();
        enableListener();

        listener->panelChanged(EvSpotEntry, M("TP_SPOT_ENTRYCHANGED"));
    }
}


void Spot::reset_adjusters()
{
    for (auto a : spot_adjusters) {
        a->block(true);
        a->resetValue(true);
        a->block(false);
    }
    spot_frame->set_sensitive(false);
}


void Spot::setDefaults(const ProcParams *def)
{
    initial_params = def->spot;
}


void Spot::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.spot = initial_params;
    }
    pp.spot.enabled = getEnabled();
    read(&pp);
}


void Spot::on_fold(GdkEventButton *evt)
{
    if (isCurrentSubscriber()) {
        switchOffEditMode();
    }
}


void Spot::on_hide()
{
    if (isCurrentSubscriber()) {
        switchOffEditMode();
    }
}
