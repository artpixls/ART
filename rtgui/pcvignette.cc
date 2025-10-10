/*
 *  This file is part of RawTherapee.
 */
#include "pcvignette.h"
#include "eventmapper.h"

using namespace rtengine;
using namespace rtengine::procparams;

PCVignette::PCVignette():
    FoldableToolPanel(this, "pcvignette", M("TP_PCVIGNETTE_LABEL"), false, true, true),
    EditSubscriber(ET_OBJECTS),
    lastObject(-1)    
{
    auto m = ProcEventMapper::getInstance();
    EvCenter = m->newEvent(rtengine::LUMINANCECURVE, "HISTORY_MSG_PCVIGNETTE_CENTER");
    EvToolReset.set_action(rtengine::LUMINANCECURVE);

    Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
    edit = Gtk::manage (new Gtk::ToggleButton());
    edit->get_style_context()->add_class("independent");
    edit->add (*Gtk::manage (new RTImage ("crosshair-adjust.svg")));
    edit->set_tooltip_text(M("EDIT_OBJECT_TOOLTIP"));
    editConn = edit->signal_toggled().connect( sigc::mem_fun(*this, &PCVignette::editToggled) );
    hb->pack_start(*edit, Gtk::PACK_SHRINK, 0);

    strength = Gtk::manage (new Adjuster (M("TP_PCVIGNETTE_STRENGTH"), -6, 6, 0.01, 0));
    strength->set_tooltip_text (M("TP_PCVIGNETTE_STRENGTH_TOOLTIP"));
    strength->setAdjusterListener (this);

    feather = Gtk::manage (new Adjuster (M("TP_PCVIGNETTE_FEATHER"), 0, 100, 1, 50));
    feather->set_tooltip_text (M("TP_PCVIGNETTE_FEATHER_TOOLTIP"));
    feather->setAdjusterListener (this);

    roundness = Gtk::manage (new Adjuster (M("TP_PCVIGNETTE_ROUNDNESS"), 0, 100, 1, 50));
    roundness->set_tooltip_text (M("TP_PCVIGNETTE_ROUNDNESS_TOOLTIP"));
    roundness->setAdjusterListener (this);

    centerX = Gtk::manage(new Adjuster(M("TP_PCVIGNETTE_CENTER_X"), -100, 100, 1, 0));
    centerX->setAdjusterListener(this);
    centerY = Gtk::manage(new Adjuster(M("TP_PCVIGNETTE_CENTER_Y"), -100, 100, 1, 0));
    centerY->setAdjusterListener(this);

    pack_start(*hb, Gtk::PACK_SHRINK, 0);
    pack_start(*strength);
    pack_start(*feather);
    pack_start(*roundness);
    pack_start(*centerX);
    pack_start(*centerY);

    // Instantiating the Editing geometry; positions will be initialized later
    Circle *centerCircle = new Circle();
    centerCircle->datum = Geometry::IMAGE;
    centerCircle->radiusInImageSpace = false;
    centerCircle->radius = 15;
    centerCircle->filled = true;

    EditSubscriber::visibleGeometry.push_back(centerCircle);

    // MouseOver geometry
    centerCircle = new Circle();
    centerCircle->datum = Geometry::IMAGE;
    centerCircle->radiusInImageSpace = false;
    centerCircle->radius = 15;
    centerCircle->filled = true;

    EditSubscriber::mouseOverGeometry.push_back(centerCircle);

    show_all();
}


PCVignette::~PCVignette()
{
    for (std::vector<Geometry*>::const_iterator i = visibleGeometry.begin(); i != visibleGeometry.end(); ++i) {
        delete *i;
    }

    for (std::vector<Geometry*>::const_iterator i = mouseOverGeometry.begin(); i != mouseOverGeometry.end(); ++i) {
        delete *i;
    }
}


void PCVignette::read (const ProcParams* pp)
{
    disableListener();
    crop_ = pp->crop;

    setEnabled(pp->pcvignette.enabled);
    strength->setValue(pp->pcvignette.strength);
    feather->setValue(pp->pcvignette.feather);
    roundness->setValue(pp->pcvignette.roundness);
    centerX->setValue(pp->pcvignette.centerX);
    centerY->setValue(pp->pcvignette.centerY);

    updateGeometry(pp->pcvignette.centerX, pp->pcvignette.centerY);
    
    enableListener ();
}

void PCVignette::write(ProcParams* pp)
{
    pp->pcvignette.strength = strength->getValue ();
    pp->pcvignette.feather = feather->getIntValue ();
    pp->pcvignette.roundness = roundness->getIntValue ();
    pp->pcvignette.enabled = getEnabled();
    pp->pcvignette.centerX = centerX->getIntValue();
    pp->pcvignette.centerY = centerY->getIntValue();
}


void PCVignette::setDefaults(const ProcParams* defParams)
{
    strength->setDefault (defParams->pcvignette.strength);
    feather->setDefault (defParams->pcvignette.feather);
    roundness->setDefault (defParams->pcvignette.roundness);
    centerX->setDefault(defParams->pcvignette.centerX);
    centerY->setDefault(defParams->pcvignette.centerY);
    initial_params = defParams->pcvignette;
}

void PCVignette::adjusterChanged(Adjuster* a, double newval)
{
    updateGeometry(int(centerX->getValue()), int(centerY->getValue()));

    if (listener && getEnabled()) {
        if (a == strength) {
            listener->panelChanged (EvPCVignetteStrength, strength->getTextValue());
        } else if (a == feather) {
            listener->panelChanged (EvPCVignetteFeather, feather->getTextValue());
        } else if (a == roundness) {
            listener->panelChanged (EvPCVignetteRoundness, roundness->getTextValue());
        } else if (a == centerX || a == centerY) {
            listener->panelChanged(EvCenter, Glib::ustring::compose("X=%1 Y=%2", centerX->getTextValue(), centerY->getTextValue()));
        }
    }
}

void PCVignette::adjusterAutoToggled(Adjuster* a, bool newval)
{
}

void PCVignette::enabledChanged ()
{

    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged (EvPCVignetteEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged (EvPCVignetteEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged (EvPCVignetteEnabled, M("GENERAL_DISABLED"));
        }
    }
}

void PCVignette::trimValues (rtengine::procparams::ProcParams* pp)
{
    strength->trimValue(pp->pcvignette.strength);
    feather->trimValue(pp->pcvignette.feather);
    roundness->trimValue(pp->pcvignette.roundness);
    centerX->trimValue(pp->pcvignette.centerX);
    centerY->trimValue(pp->pcvignette.centerY);
}


void PCVignette::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.pcvignette = initial_params;
    }
    pp.pcvignette.enabled = getEnabled();
    read(&pp);
}


void PCVignette::updateGeometry(const int centerX, const int centerY)
{
    EditDataProvider* dataProvider = getEditProvider();

    if (!dataProvider) {
        return;
    }

    int im_x = 0, im_y = 0;
    int imW = 0, imH = 0;
    getDimensions(im_x, im_y, imW, imH);
    // dataProvider->getImageSize(imW, imH);
    if (!imW || !imH) {
        return;
    }

    rtengine::Coord origin (im_x + imW / 2 + centerX * imW / 200, im_y + imH / 2 + centerY * imH / 200);

    const auto updateCircle = [&](Geometry* geometry)
    {
        const auto circle = static_cast<Circle*>(geometry);
        circle->center = origin;
    };

    // update circle's position
    updateCircle(visibleGeometry[0]);
    updateCircle(mouseOverGeometry[0]);
}


void PCVignette::setEditProvider(EditDataProvider *provider)
{
    EditSubscriber::setEditProvider(provider);
}

void PCVignette::editToggled()
{
    if (edit->get_active()) {
        subscribe();
    } else {
        unsubscribe();
    }
}

CursorShape PCVignette::getCursor(int objectID)
{
    switch (objectID) {
    case 0:
        return CSMove2D;

    default:
        return CSArrow;
    }
}

bool PCVignette::mouseOver(int modifierKey)
{
    EditDataProvider *editProvider = getEditProvider();

    if (editProvider && editProvider->object != lastObject) {
        if (lastObject > -1) {
            EditSubscriber::visibleGeometry.at(lastObject)->state = Geometry::NORMAL;
        }

        if (editProvider->object > -1) {
            EditSubscriber::visibleGeometry.at(editProvider->object)->state = Geometry::PRELIGHT;
        }

        lastObject = editProvider->object;
        return true;
    }

    return false;
}

bool PCVignette::button1Pressed(int modifierKey)
{
    if (lastObject < 0) {
        return false;
    }

    // EditDataProvider *provider = getEditProvider();

    if (!(modifierKey & GDK_CONTROL_MASK)) {
        // button press is valid (no modifier key)
        PolarCoord pCoord;
        int im_x, im_y;
        int imW, imH;
        getDimensions(im_x, im_y, imW, imH);
        // provider->getImageSize(imW, imH);
        double halfSizeW = imW / 2.;
        double halfSizeH = imH / 2.;
        draggedCenter.set(int(halfSizeW + halfSizeW * (centerX->getValue() / 100.)), int(halfSizeH + halfSizeH * (centerY->getValue() / 100.)));

        EditSubscriber::action = ES_ACTION_DRAGGING;
        return false;
    } else { // should theoretically always be true
        // this will let this class ignore further drag events
        EditSubscriber::visibleGeometry.at(lastObject)->state = Geometry::NORMAL;
        lastObject = -1;
        return true;
    }

    return false;
}


bool PCVignette::button1Released()
{
    EditSubscriber::action = ES_ACTION_NONE;
    return true;
}


bool PCVignette::drag1(int modifierKey)
{
    // compute the polar coordinate of the mouse position
    EditDataProvider *provider = getEditProvider();
    int im_x, im_y;
    int imW, imH;
    getDimensions(im_x, im_y, imW, imH);
    // provider->getImageSize(imW, imH);
    double halfSizeW = imW / 2.;
    double halfSizeH = imH / 2.;

    if (lastObject == 0) {
        // Dragging the circle to change the center
        rtengine::Coord currPos;
        draggedCenter += provider->deltaPrevImage;
        currPos = draggedCenter;
        currPos.clip(imW, imH);
        int newCenterX = int((double(currPos.x) - halfSizeW) / halfSizeW * 100.);
        int newCenterY = int((double(currPos.y) - halfSizeH) / halfSizeH * 100.);

        if (newCenterX != centerX->getIntValue() || newCenterY != centerY->getIntValue()) {
            centerX->setValue(newCenterX);
            centerY->setValue(newCenterY);
            updateGeometry(newCenterX, newCenterY);

            if (listener) {
                listener->panelChanged(EvCenter, Glib::ustring::compose("X=%1\nY=%2", centerX->getTextValue(), centerY->getTextValue()));
            }

            return true;
        }
    }

    return false;
}

void PCVignette::switchOffEditMode ()
{
    if (edit->get_active()) {
        // switching off the toggle button
        bool wasBlocked = editConn.block(true);
        edit->set_active(false);

        if (!wasBlocked) {
            editConn.block(false);
        }
    }

    EditSubscriber::switchOffEditMode();  // disconnect
}


void PCVignette::procParamsChanged(
    const rtengine::procparams::ProcParams* params,
    const rtengine::ProcEvent& ev,
    const Glib::ustring& descr,
    const ParamsEdited* paramsEdited)
{
    crop_ = params->crop;
    updateGeometry(params->pcvignette.centerX, params->pcvignette.centerY);
}


void PCVignette::getDimensions(int &x, int &y, int &w, int &h)
{
    x = y = w = h = 0;
    EditDataProvider *p = getEditProvider();
    if (!p) {
        return;
    }
    
    if (crop_.enabled) {
        w = crop_.w;
        h = crop_.h;
        x = crop_.x;
        y = crop_.y;
    } else {
        p->getImageSize(w, h);
    }
}
