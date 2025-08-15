/* -*- C++ -*-
 *  
 *  This file is part of RawTherapee.
 *
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
#ifndef _INDCLIPPEDPANEL_
#define _INDCLIPPEDPANEL_

#include <gtkmm.h>
#include <iostream>

class ImageArea;
class IndicateClippedPanel: public Gtk::HBox {

protected:
    Gtk::Image* iFon, *iFoff, *iSon, *iSoff;
    Gtk::ToggleButton* previewSharpMask;
    Gtk::ToggleButton* previewFocusMask;
    Gtk::ToggleButton* indClippedH;
    Gtk::ToggleButton* indClippedS;
    Gtk::ToggleButton *falseColors;
    Gtk::Image *falseColorsOn, *falseColorsOff;
    Gtk::ToggleButton *grid;
    ImageArea* imageArea;

    sigc::connection connFocusMask, connSharpMask, connClippedS, connClippedH;
    sigc::connection connFalseColors;
    sigc::connection connGrid;

public:
    explicit IndicateClippedPanel(ImageArea* ia);
    ~IndicateClippedPanel() override;

    void buttonToggled(Gtk::ToggleButton* tb);
    void toggleClipped(bool highlights);  // inverts a toggle programmatically
    void toggleFocusMask();
    void silentlyDisableSharpMask();  // toggle the button off without throwing a toggle event
    void toggleSharpMask();
    void toggleFalseColors();

    bool showFocusMask ()
    {
        return previewFocusMask->get_active ();
    }
    bool showClippedShadows()
    {
        return indClippedS->get_active();
    }
    bool showClippedHighlights()
    {
        return indClippedH->get_active();
    }

    bool showFalseColors()
    {
        return falseColors->get_active();
    }

    bool showGrid()
    {
        return grid->get_active();
    }
};

#endif
