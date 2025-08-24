/* -*- C++ -*-
 *
 *  This file is part of RawTherapee.
 *
 *  Copyright 2018 Alberto Griggio <alberto.griggio@gmail.com>
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
#include "adjuster.h"
#include "toolpanel.h"
#include "areamask.h"
#include "colorprovider.h"
#include "curvelistener.h"
#include "curveeditorgroup.h"
#include "curveeditor.h"
#include "clipboard.h"
#include "thresholdadjuster.h"
#include <unordered_set>


class LabMasksContentProvider {
public:
    virtual ~LabMasksContentProvider() {}

    virtual Gtk::Widget *getWidget() = 0;
    virtual void getEvents(rtengine::ProcEvent &mask_list, rtengine::ProcEvent &parametric_mask, rtengine::ProcEvent &h_mask, rtengine::ProcEvent &c_mask, rtengine::ProcEvent &l_mask, rtengine::ProcEvent &blur, rtengine::ProcEvent &show, rtengine::ProcEvent &area_mask, rtengine::ProcEvent &deltaE_mask, rtengine::ProcEvent &contrastThreshold_mask, rtengine::ProcEvent &drawn_mask, rtengine::ProcEvent &mask_postprocess, rtengine::ProcEvent &raster_mask) = 0;

    virtual ToolPanelListener *listener() = 0;

    virtual void selectionChanging(int idx) = 0;
    virtual void selectionChanged(int idx) = 0;
    virtual bool addPressed(int idx) = 0;
    virtual bool removePressed(int idx) = 0;
    virtual bool copyPressed(int idx) = 0;
    virtual bool moveUpPressed(int idx) = 0;
    virtual bool moveDownPressed(int idx) = 0;
    virtual bool resetPressed(int idx) = 0;

    virtual int getColumnCount() = 0;
    virtual Glib::ustring getColumnHeader(int col) = 0;
    virtual Glib::ustring getColumnContent(int col, int row) = 0;

    virtual void getEditIDs(EditUniqueID &hcurve, EditUniqueID &ccurve, EditUniqueID &lcurve, EditUniqueID &deltaE) = 0;

    virtual Glib::ustring getToolName() = 0;
};


class AreaDrawUpdater {
public:
    virtual ~AreaDrawUpdater() = default;
    enum Phase {
        BEGIN,
        UPDATE,
        END
    };
    virtual void updateRectangleArea(Phase phase, int x1, int y1, int x2, int y2) = 0;
    virtual void cancelUpdateRectangleArea() = 0;
};


class AreaDrawListener {
public:
    virtual ~AreaDrawListener() = default;
    virtual void startRectangleDrawingArea(AreaDrawUpdater *updater) = 0;
    virtual void stopRectangleDrawingArea() = 0;
};


class AreaDrawListenerProvider {
public:
    virtual ~AreaDrawListenerProvider() = default;
    virtual void setAreaDrawListener(AreaDrawListener *listener) = 0;
};


class DeltaEColorProvider {
public:
    virtual ~DeltaEColorProvider() = default;
    virtual bool getDeltaELCH(EditUniqueID id, rtengine::Coord pos, float &L, float &C, float &H) = 0;
};


class LabMasksPanel:
    public Gtk::VBox,
    public AdjusterListener,
    public CurveListener,
    public ColorProvider,
    public AreaMask,
    public AreaDrawUpdater,
    public ThresholdAdjusterListener {
public:
    LabMasksPanel(LabMasksContentProvider *cp);
    ~LabMasksPanel();

    void setMasks(const std::vector<rtengine::procparams::Mask> &masks, int selected_idx, bool show_mask);
    void getMasks(std::vector<rtengine::procparams::Mask> &masks, int &show_mask_idx);
    int getSelected();

    void adjusterChanged(Adjuster *a, double newval) override;
    void adjusterAutoToggled(Adjuster *a, bool newval) override;
    void curveChanged(CurveEditor* ce) override;
    float blendPipetteValues(CurveEditor *ce, float chan1, float chan2, float chan3) override;

    bool button1Released() override;
    bool pick3(bool picked) override;
    bool scroll(int modifierKey, GdkScrollDirection direction, double deltaX, double deltaY, bool &propagateEvent) override;
    void switchOffEditMode() override;
    void setEditProvider(EditDataProvider *provider);
    void colorForValue(double valX, double valY, enum ColorCaller::ElemType elemType, int callerId, ColorCaller *caller) override;

    void setEdited(bool yes);
    bool getEdited();

    void updateSelected();

    void updateRectangleArea(AreaDrawUpdater::Phase phase, int x1, int y1, int x2, int y2) override;
    void cancelUpdateRectangleArea() override;
    void setAreaDrawListener(AreaDrawListener *l);

    void setDeltaEColorProvider(DeltaEColorProvider *provider);

    void adjusterChanged(ThresholdAdjuster *a, double newBottom, double newTop) override;
    void adjusterChanged(ThresholdAdjuster *a, double newBottomLeft, double newTopLeft, double newBottomRight, double newTopRight) override {}
    void adjusterChanged(ThresholdAdjuster *a, int newBottom, int newTop) override {}
    void adjusterChanged(ThresholdAdjuster *a, int newBottomLeft, int newTopLeft, int newBottomRight, int newTopRight) override {}
    void adjusterChanged2(ThresholdAdjuster *a, int newBottomL, int newTopL, int newBottomR, int newTopR) override {}

    void updateRasterMaskList(const rtengine::procparams::ProcParams *params);

private:
    void on_map() override;
    void onMaskFold(GdkEventButton *evt);
    
    ToolPanelListener *getListener();
    void populateList();
    void onSelectionChanged();
    void onAddPressed();
    void onRemovePressed();
    void onUpPressed();
    void onDownPressed();
    void onCopyPressed();
    void onResetPressed();
    void onAreaMaskEnableToggled();
    void onMaskInvertedChanged();
    void onAreaMaskToggleChanged();
    void onShowMaskChanged();
    void onAreaShapeSelectionChanged();
    void onAreaShapeResetPressed();
    void onAreaShapeAddPressed();
    void onAreaShapeRemovePressed();
    void onAreaShapeUpDownPressed(bool up);
    void onAreaMaskCopyPressed();
    void onAreaMaskPastePressed();
    void onAreaShapeModeChanged(int i);
    // void onRectangleAreaMaskDrawChanged();
    void onAreaMaskDrawRectangleAddPressed();
    void onAreaMaskDrawPolygonAddPressed();
    void onAreaMaskDrawGradientAddPressed();
    bool onKnotRoundnessUpdated();
    void onDeltaEMaskEnableToggled();
    void onParametricMaskEnableToggled();
    void onListEnabledToggled(const Glib::ustring &path);
    void setListEnabled(Gtk::CellRenderer *renderer, const Gtk::TreeModel::iterator &it);
    
    void shapeAddPressed(rtengine::procparams::AreaMask::Shape::Type type, bool list_only);
    void setAdjustersVisibility(bool visible, rtengine::procparams::AreaMask::Shape::Type shape_type);
    void updateRectangleAreaMask(bool from_mask);
    void updateGradientAreaMask(bool from_mask);
    void maskGet(int idx);
    void maskShow(int idx, bool list_only=false, bool unsub=true);
    void populateShapeList(int idx, int sel);
    void areaShapeSelect(int idx, bool update_list);

    void toggleAreaShapeMode(int i);
    void updateShapeButtonsSensitivity();
    int getAreaShapeMode();

    void disableListener();
    void enableListener();

    void onMaskCopyPressed();
    void onMaskPastePressed();

    const rtengine::ProcEvent &areaMaskEvent() const;
    const rtengine::ProcEvent &deltaEMaskEvent() const;

    void onDeltaESpotRequested(rtengine::Coord pos);
    void onDeltaEPickClicked();
    void onDrawnMaskUpdated();

    bool onMaskNameFocusOut(GdkEventFocus *e);
    void on_hide() override;

    void onMaskExpanded(GdkEventButton *evt, MyExpander *exp);

    void onRasterMaskChanged();

    LabMasksContentProvider *cp_;
    std::vector<rtengine::procparams::Mask> masks_;
    unsigned int selected_;

    rtengine::ProcEvent EvMaskList;
    rtengine::ProcEvent EvParametricMask;
    rtengine::ProcEvent EvHMask;
    rtengine::ProcEvent EvCMask;
    rtengine::ProcEvent EvLMask;
    rtengine::ProcEvent EvMaskBlur;
    rtengine::ProcEvent EvShowMask;
    rtengine::ProcEvent EvAreaMask;
    rtengine::ProcEvent EvAreaMaskVoid;
    rtengine::ProcEvent EvDeltaEMask;
    rtengine::ProcEvent EvDeltaEMaskVoid;
    rtengine::ProcEvent EvContrastThresholdMask;
    rtengine::ProcEvent EvDrawnMask;
    rtengine::ProcEvent EvMaskName;
    rtengine::ProcEvent EvMaskPostprocess;
    rtengine::ProcEvent EvRasterMask;

    class ListColumns: public Gtk::TreeModel::ColumnRecord {
    public:
        ListColumns(int n)
        {
            add(enabled);
            add(id);
            for (int i = 0; i < n; ++i) {
                cols.push_back(Gtk::TreeModelColumn<Glib::ustring>());
                add(cols.back());
            }
            add(mask);
        }

        Gtk::TreeModelColumn<bool> enabled;
        Gtk::TreeModelColumn<int> id;
        std::vector<Gtk::TreeModelColumn<Glib::ustring>> cols;
        Gtk::TreeModelColumn<Glib::ustring> mask;
    };

    MyExpander *mask_exp_;
    bool first_mask_exp_;
    //Gtk::ListViewText *list;
    Gtk::CellRendererToggle list_enabled_renderer_;
    Gtk::TreeView::Column list_enabled_column_;
    std::unique_ptr<ListColumns> list_model_columns_;
    Glib::RefPtr<Gtk::ListStore> list_model_;
    Gtk::TreeView *list;
    
    Gtk::Button *reset;
    Gtk::Button *add;
    Gtk::Button *remove;
    Gtk::Button *up;
    Gtk::Button *down;
    Gtk::Button *copy;
    Gtk::Button *maskCopy;
    Gtk::Button *maskPaste;
    MyExpander *parametricMask;
    CurveEditorGroup *maskEditorGroup;
    FlatCurveEditor *hueMask;
    FlatCurveEditor *chromaticityMask;
    FlatCurveEditor *lightnessMask;
    Adjuster *lightnessMaskDetail;
    Adjuster *maskBlur;
    Gtk::CheckButton *showMask;
    sigc::connection selectionConn;
    Gtk::CheckButton *maskInverted;
    MyExpander *areaMask;
    Gtk::HBox *areaMaskButtonsHb;
    Gtk::ListViewText *areaMaskShapes;
    sigc::connection shapeSelectionConn;
    Gtk::Button *areaMaskReset;
    Gtk::Button *areaMaskAdd;
    Gtk::Button *areaMaskRemove;
    Gtk::Button *areaMaskUp;
    Gtk::Button *areaMaskDown;
    unsigned int area_shape_index_;
    Gtk::ToggleButton *areaMaskToggle;
    Gtk::Button *areaMaskDrawGradientAdd;
    Gtk::Button *areaMaskDrawPolygonAdd;
    Gtk::Button *areaMaskDrawRectangleAdd;
    sigc::connection areaMaskDrawConn;
    Gtk::Button *areaMaskCopy;
    Gtk::Button *areaMaskPaste;
    Adjuster *areaMaskFeather;
    Adjuster *areaMaskBlur;
    DiagonalCurveEditor *areaMaskContrast;
    Gtk::ToggleButton *areaMaskMode[3];
    sigc::connection areaMaskModeConn[3];
    Adjuster *areaMaskShapeFeather;
    Adjuster *areaMaskShapeBlur;
    Adjuster *areaMaskX;
    Adjuster *areaMaskY;
    Adjuster *areaMaskWidth;
    Adjuster *areaMaskHeight;
    Adjuster *areaMaskAngle180;
    Adjuster *areaMaskRoundness;
    Adjuster *areaMaskStrengthStart;
    Adjuster *areaMaskStrengthEnd;
    Adjuster *areaMaskAngle360;
    Adjuster *areaMaskGradFeather;
    std::vector<Adjuster *> areaMaskAdjusters;
    std::vector<bool> listenerDisabled;
    rtengine::procparams::AreaMask::Rectangle defaultAreaShape;
    bool listEdited;
    AreaDrawListener *adl_;
    sigc::connection scrollDelayConn;

    MyExpander *deltaEMask;
    Gtk::DrawingArea *deltaEColor;
    ThresholdAdjuster *deltaEL;
    ThresholdAdjuster *deltaEC;
    ThresholdAdjuster *deltaEH;
    Adjuster *deltaERange;
    Adjuster *deltaEDecay;
    Adjuster *deltaEStrength;
    Gtk::CheckButton *deltaEInverted;
    Gtk::Button *deltaEPick;

    Adjuster *contrastThreshold;
    DeltaEColorProvider *deltaE_provider_;
    MyExpander *drawnMask;

    std::vector<MyExpander *> mask_expanders_;

    Gtk::Entry *maskName;
    DiagonalCurveEditor *maskCurve;
    Adjuster *maskPosterization;
    Adjuster *maskSmoothing;
    Adjuster *maskOpacity;

    MyExpander *raster_mask_;
    MyComboBoxText *raster_mask_value_;
    Gtk::CheckButton *raster_mask_inverted_;
    struct RasterMaskInfo {
        Glib::ustring toolname;
        Glib::ustring name;
        unsigned int idx;
        RasterMaskInfo(const Glib::ustring &t="", const Glib::ustring &n="", unsigned int i=0): toolname{t}, name{n}, idx{i} {}
    };
    std::vector<RasterMaskInfo> available_raster_masks_;
    std::unordered_set<std::string> used_raster_masks_;
};
