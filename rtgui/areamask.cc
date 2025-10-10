/** -*- C++ -*-
 *  
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2018 Alberto Griggio <alberto.griggio@gmail.com>
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
#include "areamask.h"

using rtengine::Coord;
using rtengine::PolarCoord;

namespace {

// z level of the MouseOver geometry for polygons
// constexpr int CURVE_POLY  = 0;
// constexpr int CAGE_POLY   = 1;
// constexpr int INSERT_LINE = 2;
constexpr int SEL_DISC    = 1;
constexpr int PREV_DISC   = 2;
//constexpr int NEXT_DISC   = 3;

} // namespace

AreaMask::AreaMask():
    EditSubscriber(ET_OBJECTS),
    last_object_(-1),
    dragged_point_old_angle_(-1000),
    dragged_point_adjuster_angle_(-1000),
    dragged_feather_offset_(0),
    dragged_center_(0, 0),
    center_x_(0),
    center_y_(0),
    width_(100),
    height_(100),
    strength_start_(100),
    strength_end_(100.),
    feather_(0.),
    angle_(0),
    top_id_(0),
    bottom_id_(0),
    left_id_(0),
    right_id_(0),
    rotate_w_id_(0),
    rotate_h_id_(0),
    center_id_(0),
    insertion_line(nullptr),
    curve(nullptr),
    cage(nullptr),
    sel_knot(nullptr),
    sel_knot_bg_(nullptr),
    prev_knot(nullptr),
    next_knot(nullptr),
    hovered_line_id_(-1),
    sel_poly_knot_id_(-1),
    prev_poly_knot_id_(-1),
    next_poly_knot_id_(-1),
    dragged_element_(DraggedElement::NONE),
    h_line_id_(0),
    v_line_id_(0),
    feather_line1_id_(0),
    feather_line2_id_(0),
    center_circle_id_(0),
    geomType (rteMaskShape::Type::RECTANGLE)
{
}

AreaMask::~AreaMask()
{
    deleteGeometry();
}

void AreaMask::deleteGeometry()
{
    for (auto geometry : visibleGeometry) {
        delete geometry;
    }
    visibleGeometry.clear();

    if (geomType == rteMaskShape::Type::RECTANGLE || geomType == rteMaskShape::Type::GRADIENT) {
        for (auto geometry : mouseOverGeometry) {
            delete geometry;
        }
    }
    else {  // geomType == rteMaskShape::Type::POLYGON
        // Some shapes are in visible and mouseOver geometry, so need
        // to be deleted differently
        for (auto geom : segments_MO) {
            delete geom;
        }
        segments_MO.clear();
        dragged_points_.clear();

        // already deleted in visibleGeometry...
        insertion_line = nullptr;
        curve = nullptr;
        cage = nullptr;
        sel_knot = nullptr;
        if (sel_knot_bg_) {
            delete sel_knot_bg_;
            sel_knot_bg_ = nullptr;
        }
        prev_knot = nullptr;
        next_knot = nullptr;
    }
    mouseOverGeometry.clear();
}

void AreaMask::createRectangleGeometry()
{
    const auto mkline = [](std::vector<Geometry *> &geom) -> int
                        {
                            Line *ret = new Line();
                            ret->innerLineWidth = 2;
                            ret->datum = Geometry::IMAGE;
                            geom.push_back(ret);
                            return geom.size() - 1;
                        };

    const auto mkcircle = [](std::vector<Geometry *> &geom) -> int
                          {
                              Circle *ret = new Circle();
                              ret->datum = Geometry::IMAGE;
                              ret->radiusInImageSpace = false;
                              ret->radius = 6;
                              ret->filled = true;
                              geom.push_back(ret);
                              return geom.size() - 1;
                          };
    
    top_id_ = mkline(visibleGeometry); // top
    bottom_id_ = mkline(visibleGeometry); // bottom
    left_id_ = mkline(visibleGeometry); // left
    right_id_ = mkline(visibleGeometry); // right
    rotate_w_id_ = mkline(visibleGeometry); // rotate_w
    rotate_h_id_ = mkline(visibleGeometry); // rotate_h
    center_id_ = mkcircle(visibleGeometry); // center

    // MouseOver geometry
    mkline(mouseOverGeometry); // top
    mkline(mouseOverGeometry); // bottom
    mkline(mouseOverGeometry); // left
    mkline(mouseOverGeometry); // right
    mkline(mouseOverGeometry); // rotate_w
    mkline(mouseOverGeometry); // rotate_h
    mkcircle(mouseOverGeometry); // center
}

void AreaMask::createPolygonGeometry()
{
    insertion_line = new Line();
    insertion_line->innerLineWidth = 6;
    insertion_line->setFrame(true);
    insertion_line->datum = Geometry::IMAGE;
    insertion_line->setInnerLineColor(1., 0.5, 0);
    visibleGeometry.push_back(insertion_line);

    curve = new PolyLine();
    curve->innerLineWidth = 2;
    curve->datum = Geometry::IMAGE;
    visibleGeometry.push_back(curve);

    cage = new PolyLine();
    cage->innerLineWidth = 1;
    cage->datum = Geometry::IMAGE;
    cage->setFrame(true);
    cage->setDashed(true);
    visibleGeometry.push_back(cage);

    const auto mkcircle = [this](Circle* &circle) -> void
                          {
                              circle = new Circle();
                              circle->datum = Geometry::IMAGE;
                              circle->radiusInImageSpace = false;
                              circle->radius = 3;
                              circle->filled = true;
                              circle->setVisible(false);
                              circle->setHoverable(false);
                              visibleGeometry.push_back(circle);
                              //have to be added after the segments,
                              //so it's added to the vectors in setPolylineSize
                              //mouseOverGeometry.push_back(circle);
                          };

    mkcircle(sel_knot_bg_);
    visibleGeometry.pop_back(); // the bg circle is only for mouse wheel
    
    mkcircle(sel_knot);
    mkcircle(prev_knot);
    mkcircle(next_knot);

    sel_knot->state = Geometry::PRELIGHT;  // always prelight, but either visible or invisible
    sel_knot_bg_->radius = 7;
    sel_knot_bg_->setHoverable(true);

    hovered_line_id_ = -1;
    sel_poly_knot_id_ = -1;
    prev_poly_knot_id_ = -1;
    next_poly_knot_id_ = -1;
}

void AreaMask::createGradientGeometry()
{
    const auto mkline = [](std::vector<Geometry *> &geom) -> int
                        {
                            Line *ret = new Line();
                            ret->innerLineWidth = 2;
                            ret->datum = Geometry::IMAGE;
                            geom.push_back(ret);
                            return geom.size() - 1;
                        };

    const auto mkcircle = [](std::vector<Geometry *> &geom, int radius) -> int
                          {
                              Circle *ret = new Circle();
                              ret->datum = Geometry::IMAGE;
                              ret->radiusInImageSpace = false;
                              ret->radius = radius;
                              ret->filled = true;
                              geom.push_back(ret);
                              return geom.size() - 1;
                          };

    h_line_id_ = mkline(visibleGeometry);
    v_line_id_ = mkline(visibleGeometry);
    feather_line1_id_ = mkline(visibleGeometry);
    feather_line2_id_ = mkline(visibleGeometry);
    center_circle_id_ = mkcircle(visibleGeometry, 6);

    mkline(mouseOverGeometry);
    mkline(mouseOverGeometry);
    mkline(mouseOverGeometry);
    mkline(mouseOverGeometry);
    mkcircle(mouseOverGeometry, 20);
}

size_t AreaMask::getPolygonSize()
{
    return poly_knots_.size();
}

void AreaMask::setPolygon(const std::vector<rteMaskPoly::Knot> &new_poly)
{
    if (new_poly.empty()) {
        poly_knots_.clear();
    }
    else {
        poly_knots_ = new_poly;
    }
    if (geomType == rteMaskShape::Type::POLYGON) {
        initHoverGeometry();
        updateGeometry();
    }
}

std::vector<rteMaskPoly::Knot> AreaMask::getPolygon()
{
    return poly_knots_;
}

void AreaMask::clearPolygon()
{
    poly_knots_.clear();
    if (geomType == rteMaskShape::Type::POLYGON) {
        initHoverGeometry();
        updateGeometry();
    }
}

void AreaMask::initHoverGeometry()
{
    if (geomType == rteMaskShape::Type::POLYGON) {
        hovered_line_id_ = poly_knots_.size() > 1 ? poly_knots_.size() - 1 : -1;
        sel_poly_knot_id_ = -1;
        prev_poly_knot_id_ = -1;
        next_poly_knot_id_ = -1;

        if (poly_knots_.size() == 1) {
            prev_poly_knot_id_ = 0;
        }
        else if (poly_knots_.size() == 2) {
            prev_poly_knot_id_ = 0;
            next_poly_knot_id_ = 1;
        }
        else if (poly_knots_.size() > 2) {
            prev_poly_knot_id_ = hovered_line_id_;
            next_poly_knot_id_ = hovered_line_id_ == (int)poly_knots_.size() - 1 ? 0 : sel_poly_knot_id_ + 1;
        }
    }
}

void AreaMask::setGeometryType(rteMaskShape::Type newType)
{
    if (newType != geomType || visibleGeometry.empty()) {
        deleteGeometry();
        geomType = newType;
        switch (newType) {
        case rteMaskShape::Type::RECTANGLE:
            createRectangleGeometry();
            break;
        case rteMaskShape::Type::GRADIENT:
            createGradientGeometry();
            break;
        case rteMaskShape::Type::POLYGON:
        default:
            createPolygonGeometry();
        }
    }
}

rteMaskShape::Type AreaMask::getGeometryType()
{
    return geomType;
}

CursorShape AreaMask::getCursor(int objectID)
{
    switch (geomType) {
    case rteMaskShape::Type::RECTANGLE:
        if (objectID == top_id_ || objectID == bottom_id_) {
            if (angle_ < -135 || (angle_ >= -45 && angle_ <= 45) || angle_ > 135) {
                return CSMove1DV;
            } else {
                return CSMove1DH;
            }
        } else if (objectID == left_id_ || objectID == right_id_) {
            if (angle_ < -135 || (angle_ >= -45 && angle_ <= 45) || angle_ > 135) {
                return CSMove1DH;
            } else {
                return CSMove1DV;
            }
        } else if (objectID == rotate_w_id_ || objectID == rotate_h_id_) {
            return CSMoveRotate;
        } else if (objectID == center_id_) {
            return CSMove2D;
        }
        break;
    case rteMaskShape::Type::GRADIENT:
        if (objectID == h_line_id_ || objectID == v_line_id_) {
            return CSMoveRotate;
        } else if (objectID == feather_line1_id_ || objectID == feather_line2_id_) {
            if (angle_ < -135. || (angle_ >= -45. && angle_ <= 45.) || angle_ > 135.) {
                return CSMove1DV;
            }

            return CSMove1DH;
        } else if (objectID == center_circle_id_) {
            return CSMove2D;
        }
        else {
            return CSArrow;
        }
        break;
    case rteMaskShape::Type::POLYGON:
        if (dragged_element_ != DraggedElement::NONE) {
            return CSEmpty;
        } else if (objectID >= 0) {
            return CSMove2D;
        } else {
            return CSCrosshair;
        }
        break;
    default:
        break;
    }

    return CSArrow;
}


bool AreaMask::mouseOver(int modifierKey)
{
    EditDataProvider *provider = getEditProvider();

    if (provider && provider->object != last_object_) {
        if (geomType == rteMaskShape::Type::RECTANGLE) {
            if (last_object_ > -1) {
                for (int i = top_id_; i <= center_id_; ++i) {
                    visibleGeometry[i]->state = Geometry::NORMAL;
                }
            }

            if (provider->object > -1) {
                for (int i = top_id_; i <= center_id_; ++i) {
                    visibleGeometry[i]->state = Geometry::PRELIGHT;
                }
            }
        } else if (geomType == rteMaskShape::Type::GRADIENT) {
            if (last_object_ > -1) {
                if (last_object_ == 2 || last_object_ == 3) {
                    visibleGeometry[2]->state = Geometry::NORMAL;
                    visibleGeometry[3]->state = Geometry::NORMAL;
                } else {
                    visibleGeometry[last_object_]->state = Geometry::NORMAL;
                }
            }

            if (provider->object > -1) {
                if (provider->object == 2 || provider->object == 3) {
                    visibleGeometry[2]->state = Geometry::PRELIGHT;
                    visibleGeometry[3]->state = Geometry::PRELIGHT;
                } else {
                    visibleGeometry[provider->object]->state = Geometry::PRELIGHT;
                }
            }
        } else if (geomType == rteMaskShape::Type::POLYGON) {
            int imW = 0;
            int imH = 0;
            provider->getImageSize(imW, imH);
            if (!imW || !imH) {
                last_object_ = provider->object;
                return false;
            }

            int discsPos = poly_knots_.size();
            if (provider->object >= discsPos) {
                hovered_line_id_ = -1;
                // if over a knot, the knots might be updated
                if (provider->object >= discsPos + PREV_DISC) {
                    // if over a disc, 3 disc to be shown : selected (= hovered) + previous + next
                    sel_poly_knot_id_ = provider->object == discsPos + PREV_DISC ? prev_poly_knot_id_ : next_poly_knot_id_;
                    if (poly_knots_.size() == 1) {
                        prev_poly_knot_id_ = -1;
                        next_poly_knot_id_ = -1;
                    }
                    if (poly_knots_.size() == 2) {
                        if (sel_poly_knot_id_ == 0) {
                            prev_poly_knot_id_ = -1;
                            next_poly_knot_id_ = 1;
                        }
                        else if (sel_poly_knot_id_ == 1) {
                            prev_poly_knot_id_ = 0;
                            next_poly_knot_id_ = -1;
                        }
                    }
                    else { // poly_knots_.size() >= 3
                        prev_poly_knot_id_ = !sel_poly_knot_id_ ? poly_knots_.size() - 1 : sel_poly_knot_id_ - 1;
                        next_poly_knot_id_ = sel_poly_knot_id_ == (int)poly_knots_.size() - 1 ? 0 : sel_poly_knot_id_ + 1;
                    }
                }
            } else if (provider->object >= 0) {
                // looking at segments
                if (poly_knots_.size() == 2) {
                    hovered_line_id_ = 1;
                    prev_poly_knot_id_ = 1;
                    next_poly_knot_id_ = 0;
                } else {
                    hovered_line_id_ = provider->object;
                    prev_poly_knot_id_ = hovered_line_id_;
                    next_poly_knot_id_ = hovered_line_id_ == (int)poly_knots_.size() - 1 ? 0 : prev_poly_knot_id_ + 1;
                }
                sel_poly_knot_id_ = -1;
            } else if (provider->object == -1) {
                // cursor not over geometry
                if (poly_knots_.empty()) {
                    // not sure those are necessary...
                    sel_poly_knot_id_ = -1;
                    hovered_line_id_ = -1;
                    prev_poly_knot_id_ = -1;
                    next_poly_knot_id_ = -1;
                    return false;
                }
                else if (poly_knots_.size() == 1) {
                    sel_poly_knot_id_ = -1;
                    hovered_line_id_ = -1;
                    prev_poly_knot_id_ = 0;
                    next_poly_knot_id_ = -1;
                }
                else if (poly_knots_.size() == 2) {
                    sel_poly_knot_id_ = -1;
                    hovered_line_id_ = 1;
                    prev_poly_knot_id_ = 0;
                    next_poly_knot_id_ = 1;
                }
                else { // poly_knots_.size() >= 3
                    sel_poly_knot_id_ = -1;
                    hovered_line_id_ = poly_knots_.size() - 1;
                    prev_poly_knot_id_ = hovered_line_id_;
                    next_poly_knot_id_ = hovered_line_id_ == (int)poly_knots_.size() - 1 ? 0 : prev_poly_knot_id_ + 1;
                }
            }
            updateGeometry();
        }
        last_object_ = provider->object;
        return true;
    }

    return false;
}


bool AreaMask::button1Pressed(int modifierKey)
{
    EditDataProvider *provider = getEditProvider();
    int imW, imH;
    provider->getImageSize(imW, imH);

    switch (geomType) {
    case rteMaskShape::Type::RECTANGLE:
        if (last_object_ < 0) {
            return false;
        }

        if (!(modifierKey & GDK_CONTROL_MASK)) {
            // button press is valid (no modifier key)
            PolarCoord pCoord;
            double halfSizeW = imW / 2.;
            double halfSizeH = imH / 2.;
            dragged_center_.set(halfSizeW + halfSizeW * (center_x_ / 100), halfSizeH + halfSizeH * center_y_ / 100);

            // trick to get the correct angle (clockwise/counter-clockwise)
            rtengine::Coord p1 = dragged_center_;
            rtengine::Coord p2 = provider->posImage;
            int p = p1.y;
            p1.y = p2.y;
            p2.y = p;
            pCoord = p2 - p1;
            dragged_point_old_angle_ = pCoord.angle;
            dragged_point_adjuster_angle_ = angle_;

            EditSubscriber::action = ES_ACTION_DRAGGING;
            return false;
        } else { // should theoretically always be true
            // this will let this class ignore further drag events
            for (int i = top_id_; i <= center_id_; ++i) {
                visibleGeometry[i]->state = Geometry::NORMAL;
            }

            last_object_ = -1;
            return true;
        }
        break;
    case rteMaskShape::Type::GRADIENT: {
        if (!(modifierKey & GDK_CONTROL_MASK)) {
            // button press is valid (no modifier key)
            PolarCoord pCoord;
            int imW, imH;
            provider->getImageSize(imW, imH);
            double halfSizeW = imW / 2.;
            double halfSizeH = imH / 2.;
            dragged_center_.set(int(halfSizeW + halfSizeW * (center_x_ / 100.)), int(halfSizeH + halfSizeH * (center_y_ / 100.)));

            // trick to get the correct angle (clockwise/counter-clockwise)
            rtengine::Coord p1 = dragged_center_;
            rtengine::Coord p2 = provider->posImage;
            int p = p1.y;
            p1.y = p2.y;
            p2.y = p;

            pCoord = p2 - p1;
            dragged_point_old_angle_ = pCoord.angle;
            //printf("\ndraggedPointOldAngle=%.3f\n\n", dragged_point_old_angle_);
            dragged_point_adjuster_angle_ = angle_;

            if (last_object_ == 2 || last_object_ == 3) {
                // Dragging a line to change the angle
                PolarCoord draggedPoint;
                rtengine::Coord currPos;
                currPos = provider->posImage;
                rtengine::Coord centerPos = dragged_center_;

                double diagonal = sqrt(double(imW) * double(imW) + double(imH) * double(imH));

                // trick to get the correct angle (clockwise/counter-clockwise)
                int p = centerPos.y;
                centerPos.y = currPos.y;
                currPos.y = p;

                draggedPoint = currPos - centerPos;
                // compute the projected value of the dragged point
                dragged_feather_offset_ = draggedPoint.radius * sin((draggedPoint.angle - angle_) / 180.*rtengine::RT_PI);

                if (last_object_ == 3) {
                    dragged_feather_offset_ = -dragged_feather_offset_;
                }

                dragged_feather_offset_ -= (feather_ / 200. * diagonal);
            }

            EditSubscriber::action = ES_ACTION_DRAGGING;
            return false;
        } else { // should theoretically always be true
            // this will let this class ignore further drag events
            if (last_object_ == 2 || last_object_ == 3) {
                EditSubscriber::visibleGeometry.at(2)->state = Geometry::NORMAL;
                EditSubscriber::visibleGeometry.at(3)->state = Geometry::NORMAL;
            } else {
                EditSubscriber::visibleGeometry.at(last_object_)->state = Geometry::NORMAL;
            }

            last_object_ = -1;
            return true;
        }
    }
    case rteMaskShape::Type::POLYGON:
        if ((modifierKey & GDK_CONTROL_MASK)
             && sel_poly_knot_id_ == -1)
        {
            // add a new point
            rteMaskPoly::Knot newKnot;
            newKnot.x = rteMaskShape::toParamRange(provider->posImage.x, imW);
            newKnot.y = rteMaskShape::toParamRange(provider->posImage.y, imH);
            newKnot.roundness = modifierKey & GDK_SHIFT_MASK ? 100. : 0.;
            if (hovered_line_id_ == -1) {
                // no selected knot & no hovered line = one of the 1 first point creation
                poly_knots_.push_back(newKnot);
                sel_poly_knot_id_ = (int)poly_knots_.size() - 1;
                if (sel_poly_knot_id_ == 0) {
                    prev_poly_knot_id_ = -1;
                }
                else if (sel_poly_knot_id_ == 1) {
                    prev_poly_knot_id_ = 0;
                }
                next_poly_knot_id_ = -1;
            }
            else {

                if (hovered_line_id_ == (int)poly_knots_.size() - 1) {
                    poly_knots_.push_back(newKnot);
                }
                else {
                    poly_knots_.insert(poly_knots_.begin() + (hovered_line_id_ + 1), newKnot);
                }
                sel_poly_knot_id_ = hovered_line_id_ + 1;
                prev_poly_knot_id_ = sel_poly_knot_id_ - 1;
                next_poly_knot_id_ = sel_poly_knot_id_ == (int)poly_knots_.size() - 1 ? 0 : sel_poly_knot_id_ + 1;
                hovered_line_id_ = -1;
            }
            rtengine::CoordD pt(newKnot.x, newKnot.y);
            dragged_points_.emplace_back(pt);
            dragged_element_ = DraggedElement::POINT;
            EditSubscriber::action = ES_ACTION_DRAGGING;
            updateGeometry(imW, imH);
            last_object_ = segments_MO.size() + SEL_DISC;
        }
        else if(sel_poly_knot_id_ >= 0) {
            if (modifierKey & GDK_SHIFT_MASK) {
                if (poly_knots_.size() > 2) {
                    dragged_element_ = DraggedElement::ROUNDNESS;
                    EditSubscriber::action = ES_ACTION_DRAGGING;
                }
            }
            else {
                dragged_element_ = DraggedElement::POINT;
                rtengine::CoordD pt(poly_knots_.at(sel_poly_knot_id_).x,
                                    poly_knots_.at(sel_poly_knot_id_).y);
                dragged_points_.emplace_back(pt);
                EditSubscriber::action = ES_ACTION_DRAGGING;
            }
        }
        else if(hovered_line_id_ >= 0 && last_object_ != -1) {
            if ((modifierKey & GDK_SHIFT_MASK) && !(modifierKey & GDK_CONTROL_MASK) ) {
                dragged_element_ = DraggedElement::WHOLE;
                rtengine::CoordD pt;
                for (auto knot : poly_knots_) {
                    pt.x = knot.x;
                    pt.y = knot.y;
                    dragged_points_.emplace_back(pt);
                }
                EditSubscriber::action = ES_ACTION_DRAGGING;
            }
            else {
                dragged_element_ = DraggedElement::SEGMENT;
                rtengine::CoordD pt;
                pt.x = poly_knots_.at(hovered_line_id_).x;
                pt.y = poly_knots_.at(hovered_line_id_).y;
                dragged_points_.emplace_back(pt);
                int i = hovered_line_id_ == (int)poly_knots_.size() - 1 ? 0 : hovered_line_id_ + 1;
                pt.x = poly_knots_.at(i).x;
                pt.y = poly_knots_.at(i).y;
                dragged_points_.emplace_back(pt);
                EditSubscriber::action = ES_ACTION_DRAGGING;
            }
        }
        break;
    default:
        break;
    }

    return EditSubscriber::action == ES_ACTION_DRAGGING;
}


bool AreaMask::button1Released()
{
    switch (geomType) {
    case rteMaskShape::Type::RECTANGLE:
        dragged_point_old_angle_ = -1000.;
        for (int i = top_id_; i <= center_id_; ++i) {
            visibleGeometry[i]->state = Geometry::NORMAL;
        }
        last_object_ = -1;
        break;
    case rteMaskShape::Type::GRADIENT:
        dragged_point_old_angle_ = -1000.;
        for (int i = h_line_id_; i <= center_circle_id_; ++i) {
            visibleGeometry[i]->state = Geometry::NORMAL;
        }
        last_object_ = -1;
        break;
    case rteMaskShape::Type::POLYGON:
        // keep the point selected after dragging
        dragged_element_ = DraggedElement::NONE;
        dragged_points_.clear();
        initHoverGeometry();
        break;
    default:
        break;
    }
    EditSubscriber::action = ES_ACTION_NONE;
    return true;
}


bool AreaMask::drag1(int modifierKey)
{
    // compute the polar coordinate of the mouse position
    EditDataProvider *provider = getEditProvider();
    int imW, imH;
    provider->getImageSize(imW, imH);

    switch (geomType) {
    case rteMaskShape::Type::RECTANGLE:
    {
        double halfSizeW = imW / 2.;
        double halfSizeH = imH / 2.;

        if (last_object_ == rotate_w_id_ || last_object_ == rotate_h_id_) {
            // Dragging a line to change the angle
            rtengine::Coord currPos = provider->posImage + provider->deltaImage;
            rtengine::Coord centerPos = dragged_center_;

            // trick to get the correct angle (clockwise/counter-clockwise)
            std::swap(centerPos.y, currPos.y);

            PolarCoord draggedPoint(currPos - centerPos);
            double deltaAngle = draggedPoint.angle - dragged_point_old_angle_;

            if (deltaAngle > 180.) { // crossing the boundary (0->360)
                deltaAngle -= 360.;
            } else if (deltaAngle < -180.) { // crossing the boundary (360->0)
                deltaAngle += 360.;
            }

            dragged_point_old_angle_ = draggedPoint.angle;
            dragged_point_adjuster_angle_ += deltaAngle;

            if (dragged_point_adjuster_angle_ > 180.) {
                dragged_point_adjuster_angle_ = -360. + dragged_point_adjuster_angle_;
            } else if (dragged_point_adjuster_angle_ < -180.) {
                dragged_point_adjuster_angle_ = 360. - dragged_point_adjuster_angle_;
            }

            if (dragged_point_adjuster_angle_ != angle_) {
                if (dragged_point_adjuster_angle_ < 0) {
                    angle_ = 180.0 + dragged_point_adjuster_angle_;
                } else {
                    angle_ = dragged_point_adjuster_angle_;
                }
                updateGeometry();
                return true;
            }
        } else if (last_object_ >= top_id_ && last_object_ <= right_id_) {
            // Dragging to resize
            rtengine::Coord currPos = provider->posImage + provider->deltaImage;
            rtengine::Coord centerPos = dragged_center_;

            // trick to get the correct angle (clockwise/counter-clockwise)
            std::swap(centerPos.y, currPos.y);

            PolarCoord draggedPoint(currPos - centerPos);
            double cur_offset_h = draggedPoint.radius * sin((draggedPoint.angle - angle_) / 180.*rtengine::RT_PI);
            double cur_offset_w = draggedPoint.radius * cos((draggedPoint.angle - angle_) / 180.*rtengine::RT_PI);

            double ww = width_, hh = height_;

            if (last_object_ == top_id_ || last_object_ == bottom_id_) {
                double ch = (cur_offset_h - height_ / 2) * (last_object_ == top_id_ ? -1 : 1);
                hh = std::max(ch * 200 / imH, 0.1);
            } else { // left_id_ || right_id_
                double cw = (cur_offset_w - width_ / 2) * (last_object_ == left_id_ ? -1 : 1);
                ww = std::max(cw * 200 / imW, 0.1);
            }

            if (ww != width_ || hh != height_) {
                width_ = ww;
                height_ = hh;
                updateGeometry();
                return true;
            }
        } else if (last_object_ == center_id_) {
            // Dragging the circle to change the center
            dragged_center_ += provider->deltaPrevImage;
            rtengine::Coord currPos = dragged_center_;
            currPos.clip(imW, imH);
            double cx = (double(currPos.x) - halfSizeW) / halfSizeW * 100.;
            double cy = (double(currPos.y) - halfSizeH) / halfSizeH * 100.;

            if (cx != center_x_ || cy != center_y_) {
                center_x_ = cx;
                center_y_ = cy;
                updateGeometry();
                return true;
            }
        }
        break;
    }
    case rteMaskShape::Type::GRADIENT:
    {
        double halfSizeW = imW / 2.;
        double halfSizeH = imH / 2.;

        if (last_object_ == 0 || last_object_ == 1) {

            // Dragging a line to change the angle
            PolarCoord draggedPoint;
            rtengine::Coord currPos;
            currPos = provider->posImage + provider->deltaImage;
            rtengine::Coord centerPos = dragged_center_;

            // trick to get the correct angle (clockwise/counter-clockwise)
            std::swap(centerPos.y, currPos.y);

            draggedPoint = currPos - centerPos;
            double deltaAngle = draggedPoint.angle - dragged_point_old_angle_;

            if (deltaAngle > 180.) { // crossing the boundary (0->360)
                deltaAngle -= 360.;
            } else if (deltaAngle < -180.) { // crossing the boundary (360->0)
                deltaAngle += 360.;
            }

            dragged_point_old_angle_ = draggedPoint.angle;

            dragged_point_adjuster_angle_ += deltaAngle;

            if (dragged_point_adjuster_angle_ > 180.) {
                dragged_point_adjuster_angle_ = -360. + dragged_point_adjuster_angle_;
            } else if (dragged_point_adjuster_angle_ < -180.) {
                dragged_point_adjuster_angle_ = 360. - dragged_point_adjuster_angle_;
            }

            //printf("dragged_point_old_angle_: %.3f /  From %d,%d to %d,%d -> angle = %.3f  /  ", dragged_point_adjuster_angle_, centerPos.x, centerPos.y, currPos.x, currPos.y, draggedPoint.angle);
            //printf("currAngle: %.3f = degree: %.3f + deltaAngle: %.3f %s /  dragged_point_old_angle_: %.3f\n", dragged_point_adjuster_angle_, degree->getValue(), deltaAngle, degree->getValue()>180.?">180":degree->getValue()<180.?"<180":"", dragged_point_old_angle_);
            if (dragged_point_adjuster_angle_ != angle_) {
                angle_ = dragged_point_adjuster_angle_;
                updateGeometry();
                return true;
            }
        } else if (last_object_ == 2 || last_object_ == 3) {
            // Dragging the upper or lower feather bar
            PolarCoord draggedPoint;
            rtengine::Coord currPos;
            currPos = provider->posImage + provider->deltaImage;
            rtengine::Coord centerPos = dragged_center_;

            double diagonal = sqrt(double(imW) * double(imW) + double(imH) * double(imH));

            // trick to get the correct angle (clockwise/counter-clockwise)
            int p = centerPos.y;
            centerPos.y = currPos.y;
            currPos.y = p;

            draggedPoint = currPos - centerPos;
            double curr_dragged_feather_offset = draggedPoint.radius * sin((draggedPoint.angle - angle_) / 180.*rtengine::RT_PI);

            if (last_object_ == 2)
                // Dragging the upper feather bar
            {
                curr_dragged_feather_offset -= dragged_feather_offset_;
            } else if (last_object_ == 3)
                // Dragging the lower feather bar
            {
                curr_dragged_feather_offset = -curr_dragged_feather_offset + dragged_feather_offset_;
            }

            curr_dragged_feather_offset = curr_dragged_feather_offset * 200. / diagonal;

            if (curr_dragged_feather_offset != feather_) {
                feather_ = curr_dragged_feather_offset;
                updateGeometry ();
                return true;
            }
        } else if (last_object_ == 4) {
            // Dragging the circle to change the center
            rtengine::Coord currPos;
            dragged_center_ += provider->deltaPrevImage;
            currPos = dragged_center_;
            currPos.clip(imW, imH);
            double cx = (double(currPos.x) - halfSizeW) / halfSizeW * 100.;
            double cy = (double(currPos.y) - halfSizeH) / halfSizeH * 100.;

            if (cx != center_x_ || cy != center_y_) {
                center_x_ = cx;
                center_y_ = cy;
                updateGeometry();
                return true;
            }
        }
        break;
    }
    case rteMaskShape::Type::POLYGON:
    {
        rtengine::CoordD d;
        bool moved = false;
        const auto clipDelta = [this, &d]() -> bool
                    {
                        for (auto point : dragged_points_) {
                            double v = point.x + d.x;
                            if      (v >  200.) d.x =  200. - point.x;
                            else if (v < -200.) d.x = -200. - point.x;
                            v = point.y + d.y;
                            if      (v >  200.) d.y =  200. - point.y;
                            else if (v < -200.) d.y = -200. - point.y;
                        }
                        return d.x != 0. || d.y != 0.;
                    };
        switch (dragged_element_) {
        case DraggedElement::POINT:
        {
            d.x = rteMaskShape::toParamRange(provider->deltaImage.x + imW / 2, imW);
            d.y = rteMaskShape::toParamRange(provider->deltaImage.y + imH / 2, imH);
            moved = clipDelta();
            poly_knots_.at(sel_poly_knot_id_).x = dragged_points_.at(0).x + d.x;
            poly_knots_.at(sel_poly_knot_id_).y = dragged_points_.at(0).y + d.y;
            break;
        }
        case DraggedElement::SEGMENT:
        {
            d.x = rteMaskShape::toParamRange(provider->deltaImage.x + imW / 2, imW);
            d.y = rteMaskShape::toParamRange(provider->deltaImage.y + imH / 2, imH);
            moved = clipDelta();
            poly_knots_.at(hovered_line_id_).x = dragged_points_.at(0).x + d.x;
            poly_knots_.at(hovered_line_id_).y = dragged_points_.at(0).y + d.y;
            int i = hovered_line_id_ == (int)poly_knots_.size() - 1 ? 0 : hovered_line_id_ + 1;
            poly_knots_.at(i).x = dragged_points_.at(1).x + d.x;
            poly_knots_.at(i).y = dragged_points_.at(1).y + d.y;
            break;
        }
        case DraggedElement::ROUNDNESS:
        {
            // 5px = 1.  (range = [0. ; 100.])
            double old_val = poly_knots_.at(sel_poly_knot_id_).roundness;
            double new_val = old_val + provider->deltaPrevScreen.x / (modifierKey & GDK_CONTROL_MASK ? 10. : 2.);
            poly_knots_.at(sel_poly_knot_id_).roundness = new_val = rtengine::LIM<double>( new_val, 0., 100.);
            moved = old_val != new_val;
            break;
        }
        case DraggedElement::WHOLE:
            d.x = rteMaskShape::toParamRange(provider->deltaImage.x + imW / 2, imW);
            d.y = rteMaskShape::toParamRange(provider->deltaImage.y + imH / 2, imH);
            moved = clipDelta();
            for (size_t i = 0 ; i < poly_knots_.size(); ++i) {
                poly_knots_.at(i).x = dragged_points_.at(i).x + d.x;
                poly_knots_.at(i).y = dragged_points_.at(i).y + d.y;
            }
            break;
        case DraggedElement::NONE:
        default:
            break;
        }
        if (moved) {
            updateGeometry();
        }
        return moved;
    }
    default:
        break;
    }
    return false;
}

bool AreaMask::button3Pressed(int modifierKey)
{
    if (geomType == rteMaskShape::Type::POLYGON
        && (modifierKey & GDK_CONTROL_MASK)
        && sel_poly_knot_id_ >= 0
        && (int)poly_knots_.size() > 3)
    {
        action = ES_ACTION_PICKING;
    }
    return false;
}

bool AreaMask::pick3(bool picked)
{
    action = ES_ACTION_NONE;
    if (picked) {
        // remove the selected knot
        poly_knots_.erase(poly_knots_.begin() + sel_poly_knot_id_);
        initHoverGeometry();
        updateGeometry();
        return true;
    }
    return false;
}

bool AreaMask::scroll(int modifierKey, GdkScrollDirection direction, double deltaX, double deltaY, bool &propagateEvent)
{
    EditDataProvider *provider = getEditProvider();
    if (!provider
        || geomType != rteMaskShape::Type::POLYGON
        || sel_poly_knot_id_ == -1
        || (direction == GDK_SCROLL_SMOOTH && deltaY == 0.)
        || (direction != GDK_SCROLL_SMOOTH && direction != GDK_SCROLL_UP && direction != GDK_SCROLL_DOWN))
    {
        propagateEvent = true;
        return false;
    }

    // scroll event is catched!
    propagateEvent = false;

    int imW, imH;
    provider->getImageSize(imW, imH);

    // Since 'ImageArea::on_realize()' sets 'SMOOTH_SCROLL_MASK' to 'add_events' on Linux and Window system,
    // they'll always receive 'GDK_SCROLL_SMOOTH events', even with standard mouses. Only MacOS will receive discrete
    // scroll values ('GDK_SCROLL_UP' / 'GDK_SCROLL_DOWN')
    double delta = (direction == GDK_SCROLL_SMOOTH ?
                   deltaY
                 : (direction == GDK_SCROLL_UP ? 3. : -3.)) / (modifierKey & GDK_CONTROL_MASK ? 8. : 0.1);
    double old_val = poly_knots_.at(sel_poly_knot_id_).roundness;
    double new_val = old_val - delta;
    poly_knots_.at(sel_poly_knot_id_).roundness = new_val = rtengine::LIM<double>( new_val, 0., 100.);
    if (old_val != new_val) {
        updateGeometry();
        return true;
    }
    return false;
}

void AreaMask::setPolylineSize(size_t newSize)
{
    if (newSize == segments_MO.size()) {
        return;
    }
    // the 3 knots must remain after the segments in the mouseOver vector
    if (newSize > segments_MO.size()) {
        // needs to add new Lines
        size_t oldSize = segments_MO.size();
        segments_MO.resize(newSize);
        mouseOverGeometry.resize(newSize + 4);
        for (size_t i = oldSize; i < newSize; ++i) {
            segments_MO.at(i) = new Line();
            segments_MO.at(i)->innerLineWidth = 3;
            segments_MO.at(i)->datum = Geometry::IMAGE;
            mouseOverGeometry.at(i) = segments_MO.at(i);
        }
    }
    else /*if (segments_MO.size() > newSize)*/ {
        // needs to delete unused Lines
        for (int i = (int)segments_MO.size() - 1; i >= (int)newSize; --i) {
            delete segments_MO.at(i);
        }
        segments_MO.resize(newSize);
        mouseOverGeometry.resize(newSize + 4);
    }
    mouseOverGeometry.at(mouseOverGeometry.size() - 4) = sel_knot_bg_;
    mouseOverGeometry.at(mouseOverGeometry.size() - 3) = sel_knot;
    mouseOverGeometry.at(mouseOverGeometry.size() - 2) = prev_knot;
    mouseOverGeometry.at(mouseOverGeometry.size() - 1) = next_knot;
}


void AreaMask::updateGeometry(const int fullWidth, const int fullHeight)
{
    EditDataProvider* provider = getEditProvider();

    if (!provider) {
        return;
    }

    int imW = 0;
    int imH = 0;
    if (fullWidth != -1 && fullHeight != -1) {
        imW = fullWidth;
        imH = fullHeight;
    } else {
        provider->getImageSize(imW, imH);
        if (!imW || !imH) {
            return;
        }
    }

    const auto width = width_ / 100 * imW;
    const auto height = height_ / 100 * imH;

    switch (geomType) {
    case rteMaskShape::Type::RECTANGLE:
    {
        if (mouseOverGeometry.empty() || visibleGeometry.empty()) {
            return;
        }
        
        rtengine::Coord origin(rteMaskShape::toImgSpace(center_x_, imW),
                               rteMaskShape::toImgSpace(center_y_, imH));

        const auto update_border =
            [&](Geometry *geometry, int direction)
            {
                const auto line = static_cast<Line *>(geometry);
                float w = width / 2;
                float h = height / 2;
                int sx, sy;
                if (direction == top_id_) {
                    sx = sy = 1;
                } else if (direction == bottom_id_) {
                    sx = 1;
                    sy = -1;
                } else if (direction == left_id_) {
                    sx = -1;
                    sy = 1;
                } else {
                    sx = 1;
                    sy = 1;
                }
                PolarCoord begin, end;
                if (direction == top_id_ || direction == bottom_id_) {
                    begin = Coord(-sx * w, sy * h);
                    end = Coord(sx * w, sy * h);
                } else {
                    begin = Coord(sx * w, -sy * h);
                    end = Coord(sx * w, sy * h);
                }
                double r, a;
                begin.get(r, a);
                begin.set(r, a - angle_);
                end.get(r, a);
                end.set(r, a - angle_);

                line->begin = begin;
                line->end = end;
                line->begin += origin;
                line->end += origin;
            };

        const auto update_inner =
            [&](Geometry *geometry, int direction)
            {
                const auto line = static_cast<Line *>(geometry);
                PolarCoord begin, end;
                float w = width / 4;
                float h = height / 4;
                if (direction == rotate_w_id_) {
                    begin = Coord(-w, 0);
                    end = Coord(w, 0);
                } else {
                    begin = Coord(0, -h);
                    end = Coord(0, h);
                }
                double r, a;
                begin.get(r, a);
                begin.set(r, a - angle_);
                end.get(r, a);
                end.set(r, a - angle_);

                line->begin = begin;
                line->end = end;
                line->begin += origin;
                line->end += origin;
            };

        const auto update_center =
            [&](Geometry *geometry)
            {
                const auto circle = static_cast<Circle *>(geometry);
                circle->center = origin;
            };

        for (int dir = top_id_; dir <= right_id_; ++dir) {
            update_border(visibleGeometry[dir], dir);
            update_border(mouseOverGeometry[dir], dir);
        }

        for (int dir = rotate_w_id_; dir <= rotate_h_id_; ++dir) {
            update_inner(visibleGeometry[dir], dir);
            update_inner(mouseOverGeometry[dir], dir);
        }

        update_center(visibleGeometry[center_id_]);
        update_center(mouseOverGeometry[center_id_]);
        break;
    }
    case rteMaskShape::Type::POLYGON:
    {
        std::vector<rteMaskPoly::Knot> imgSpacePoly(poly_knots_.size());
        const size_t k_size = poly_knots_.size();

        setPolylineSize(poly_knots_.size() == 1 ? 0 : k_size);

        // update cage and curve polyline
        cage->points.resize(poly_knots_.size());
        cage->closed = poly_knots_.size() > 2;
        cage->setVisible(poly_knots_.size() > 1);

        if (poly_knots_.size() > 1) {
            for (size_t i = 0; i < poly_knots_.size(); ++i) {
                rteMaskPoly::Knot* currKnot = &poly_knots_.at(i);
                rteMaskPoly::Knot* nextKnot = nullptr;
                int currX = rteMaskShape::toImgSpace(currKnot->x, imW);
                int currY = rteMaskShape::toImgSpace(currKnot->y, imH);

                cage->points.at(i).set(currX, currY);
                imgSpacePoly.at(i).x = currX;
                imgSpacePoly.at(i).y = currY;
                imgSpacePoly.at(i).roundness = poly_knots_.at(i).roundness;

                if (i == poly_knots_.size() - 1) {
                    nextKnot = &poly_knots_.at(0);
                } else {
                    nextKnot = &poly_knots_.at(i + 1);
                }
                segments_MO.at(i)->begin.set(currX, currY);
                segments_MO.at(i)->end.set(rteMaskShape::toImgSpace(nextKnot->x, imW),
                                           rteMaskShape::toImgSpace(nextKnot->y, imH));
                if(hovered_line_id_ == (int)i) {
                    insertion_line->begin.set(currX, currY);
                    int endKnot = hovered_line_id_ == (int)poly_knots_.size() - 1 ? 0 : i + 1;
                    insertion_line->end.set(rteMaskShape::toImgSpace(poly_knots_.at(endKnot).x, imW),
                                            rteMaskShape::toImgSpace(poly_knots_.at(endKnot).y, imH));
                }
            }
        }
        insertion_line->setVisible(hovered_line_id_ >= 0 && dragged_element_ == DraggedElement::NONE);

        const auto updateKnot = [this, imW, imH](int knot_id, Circle *knot)
                {
                    if (knot_id >= 0) {
                        knot->center.set(rteMaskShape::toImgSpace(poly_knots_.at(knot_id).x, imW),
                                         rteMaskShape::toImgSpace(poly_knots_.at(knot_id).y, imH));
                    }
                    knot->setVisible(knot_id >= 0 && (dragged_element_ == DraggedElement::NONE || poly_knots_.size() <= 2));
                    knot->setHoverable(knot_id >= 0 && dragged_element_ == DraggedElement::NONE);
                };
        updateKnot(sel_poly_knot_id_, sel_knot_bg_);
        sel_knot_bg_->setVisible(false);
        updateKnot(sel_poly_knot_id_, sel_knot);
        updateKnot(prev_poly_knot_id_, prev_knot);
        updateKnot(next_poly_knot_id_, next_knot);

        curve->points = rtengine::procparams::AreaMask::Polygon::get_tessellation(imgSpacePoly);
        curve->setVisible(poly_knots_.size() > 1);
        break;
    }
    case rteMaskShape::Type::GRADIENT:
    {
        const auto decay = feather_ * rtengine::norm2<double> (imW, imH) / 200.0;
        rtengine::Coord origin (imW / 2 + center_x_ * imW / 200, imH / 2 + center_y_ * imH / 200);

        const auto updateLine = [&](Geometry* geometry, const float radius, const float begin, const float end)
        {
            const auto line = static_cast<Line*>(geometry);
            line->begin = PolarCoord(radius, -angle_ + begin);
            line->begin += origin;
            line->end = PolarCoord(radius, -angle_ + end);
            line->end += origin;
        };

        const auto updateLineWithDecay = [&](Geometry* geometry, const float radius, const float offSetAngle)
        {
            const auto line = static_cast<Line*>(geometry);
            line->begin = PolarCoord (radius, -angle_ + 180.) + PolarCoord (decay, -angle_ + offSetAngle);
            line->begin += origin;
            line->end = PolarCoord (radius, -angle_) + PolarCoord (decay, -angle_ + offSetAngle);
            line->end += origin;
        };

        const auto updateCircle = [&](Geometry* geometry)
        {
            const auto circle = static_cast<Circle*>(geometry);
            circle->center = origin;
        };

        // update horizontal line
        updateLine (visibleGeometry.at(h_line_id_), 1500., 0., 180.);
        updateLine (mouseOverGeometry.at(h_line_id_), 1500., 0., 180.);

        // update vertical line
        updateLine (visibleGeometry.at(v_line_id_), 700., 90., 270.);
        updateLine (mouseOverGeometry.at(v_line_id_), 700., 90., 270.);

        // update upper feather line
        updateLineWithDecay (visibleGeometry.at(feather_line1_id_), 350., 270.);
        updateLineWithDecay (mouseOverGeometry.at(feather_line1_id_), 350., 270.);

        // update lower feather line
        updateLineWithDecay (visibleGeometry.at(feather_line2_id_), 350., 90.);
        updateLineWithDecay (mouseOverGeometry.at(feather_line2_id_), 350., 90.);

        // update circle's position
        updateCircle (visibleGeometry.at(center_circle_id_));
        updateCircle (mouseOverGeometry.at(center_circle_id_));

        break;
    }
    default:
        break;
    }
}
