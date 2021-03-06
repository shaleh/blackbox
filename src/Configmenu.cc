// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Configmenu.cc for Blackbox - An X11 Window Manager
// Copyright (c) 2001 - 2005 Sean 'Shaleh' Perry <shaleh@debian.org>
// Copyright (c) 1997 - 2000, 2002 - 2005
//         Bradley T Hughes <bhughes at trolltech.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Configmenu.hh"
#include "Screen.hh"
#include "Slitmenu.hh"
#include "Toolbarmenu.hh"

#include <Image.hh>
#include <Unicode.hh>


class ConfigFocusmenu : public bt::Menu {
public:
  ConfigFocusmenu(bt::Application &app, unsigned int screen,
                  BScreen *bscreen);

  void refresh(void);

protected:
  void itemClicked(unsigned int id, unsigned int);

private:
  BScreen *_bscreen;
};


class ConfigPlacementmenu : public bt::Menu {
public:
  ConfigPlacementmenu(bt::Application &app, unsigned int screen,
                      BScreen *bscreen);

  void refresh(void);

protected:
  void itemClicked(unsigned int id, unsigned int);

private:
  BScreen *_bscreen;
};


class ConfigDithermenu : public bt::Menu {
public:
  ConfigDithermenu(bt::Application &app, unsigned int screen,
                   BScreen *bscreen);

  void refresh(void);

protected:
  void itemClicked(unsigned int id, unsigned int);

private:
  BScreen *_bscreen;
};


enum {
  FocusModel,
  WindowPlacement,
  ImageDithering,
  OpaqueWindowMoving,
  OpaqueWindowResizing,
  FullMaximization,
  FocusNewWindows,
  FocusLastWindowOnWorkspace,
  ChangeWorkspaceWithMouseWheel,
  ShadeWindowWithMouseWheel,
  ToolbarActionsWithMouseWheel,
  DisableBindings,
  ToolbarOptions,
  SlitOptions,
  ClickToFocus,
  SloppyFocus,
  AutoRaise,
  ClickRaise,
  IgnoreShadedWindows
};

Configmenu::Configmenu(bt::Application &app, unsigned int screen,
                       BScreen *bscreen)
  : bt::Menu(app, screen), _bscreen(bscreen)
{
  setAutoDelete(false);
  setTitle(bt::toUnicode("Configuration Options"));
  showTitle();

  ConfigFocusmenu *focusmenu =
    new ConfigFocusmenu(app, screen, bscreen);
  ConfigPlacementmenu *placementmenu =
    new ConfigPlacementmenu(app, screen, bscreen);
  ConfigDithermenu *dithermenu =
    new ConfigDithermenu(app, screen, bscreen);

  insertItem(bt::toUnicode("Focus Model"), focusmenu, FocusModel);
  insertItem(bt::toUnicode("Window Placement"),
             placementmenu, WindowPlacement);
  insertItem(bt::toUnicode("Image Dithering"), dithermenu, ImageDithering);
  insertSeparator();
  insertItem(bt::toUnicode("Opaque Window Moving"), OpaqueWindowMoving);
  insertItem(bt::toUnicode("Opaque Window Resizing"), OpaqueWindowResizing);
  insertItem(bt::toUnicode("Full Maximization"), FullMaximization);
  insertItem(bt::toUnicode("Focus New Windows"), FocusNewWindows);
  insertItem(bt::toUnicode("Focus Last Window on Workspace"),
             FocusLastWindowOnWorkspace);
  insertItem(bt::toUnicode("Change Workspace with Mouse Wheel"),
             ChangeWorkspaceWithMouseWheel);
  insertItem(bt::toUnicode("Shade Windows with Mouse Wheel"),
             ShadeWindowWithMouseWheel);
  insertItem(bt::toUnicode("Toolbar Actions with Mouse Wheel"),
             ToolbarActionsWithMouseWheel);
  insertItem(bt::toUnicode("Disable Bindings with Scroll Lock"),
             DisableBindings);
  insertSeparator();
  insertItem(bt::toUnicode("Toolbar Options"),
             bscreen->toolbarmenu(), ToolbarOptions);
  insertItem(bt::toUnicode("Slit Options"), bscreen->slitmenu(), SlitOptions);
}


void Configmenu::refresh(void) {
  const BlackboxResource &res = _bscreen->blackbox()->resource();
  setItemChecked(OpaqueWindowMoving, res.opaqueMove());
  setItemChecked(OpaqueWindowResizing, res.opaqueResize());
  setItemChecked(FullMaximization, res.fullMaximization());
  setItemChecked(FocusNewWindows, res.focusNewWindows());
  setItemChecked(FocusLastWindowOnWorkspace, res.focusLastWindowOnWorkspace());
  setItemChecked(ChangeWorkspaceWithMouseWheel,
                 res.changeWorkspaceWithMouseWheel());
  setItemChecked(ShadeWindowWithMouseWheel,
                 res.shadeWindowWithMouseWheel());
  setItemChecked(ToolbarActionsWithMouseWheel,
                 res.toolbarActionsWithMouseWheel());
  setItemChecked(DisableBindings, res.allowScrollLock());
}


void Configmenu::itemClicked(unsigned int id, unsigned int) {
  BlackboxResource &res = _bscreen->blackbox()->resource();
  switch (id) {
  case OpaqueWindowMoving: // opaque move
    res.setOpaqueMove(!res.opaqueMove());
    break;

  case OpaqueWindowResizing:
    res.setOpaqueResize(!res.opaqueResize());
    break;

  case FullMaximization: // full maximization
    res.setFullMaximization(!res.fullMaximization());
    break;

  case FocusNewWindows: // focus new windows
    res.setFocusNewWindows(!res.focusNewWindows());
    break;

  case FocusLastWindowOnWorkspace: // focus last window on workspace
    res.setFocusLastWindowOnWorkspace(!res.focusLastWindowOnWorkspace());
    break;

  case ChangeWorkspaceWithMouseWheel:
    res.setChangeWorkspaceWithMouseWheel(!res.changeWorkspaceWithMouseWheel());
    break;

  case ShadeWindowWithMouseWheel:
    res.setShadeWindowWithMouseWheel(!res.shadeWindowWithMouseWheel());
    break;

  case ToolbarActionsWithMouseWheel:
    res.setToolbarActionsWithMouseWheel(!res.toolbarActionsWithMouseWheel());
    break;

  case DisableBindings: // disable keybindings with Scroll Lock
    res.setAllowScrollLock(!res.allowScrollLock());
    _bscreen->blackbox()->reconfigure();
    break;

  default:
    return;
  } // switch

  res.save(*_bscreen->blackbox());
}


ConfigFocusmenu::ConfigFocusmenu(bt::Application &app, unsigned int screen,
                                 BScreen *bscreen)
  : bt::Menu(app, screen), _bscreen(bscreen)
{
  setTitle(bt::toUnicode("Focus Model"));
  showTitle();

  insertItem(bt::toUnicode("Click to Focus"), ClickToFocus);
  insertItem(bt::toUnicode("Sloppy Focus"), SloppyFocus);
  insertItem(bt::toUnicode("Auto Raise"), AutoRaise);
  insertItem(bt::toUnicode("Click Raise"), ClickRaise);
}


void ConfigFocusmenu::refresh(void) {
  const BlackboxResource &res = _bscreen->blackbox()->resource();

  setItemChecked(ClickToFocus, res.focusModel() == ClickToFocusModel);
  setItemChecked(SloppyFocus, res.focusModel() == SloppyFocusModel);

  setItemEnabled(AutoRaise, res.focusModel() == SloppyFocusModel);
  setItemChecked(AutoRaise, res.autoRaise());

  setItemEnabled(ClickRaise, res.focusModel() == SloppyFocusModel);
  setItemChecked(ClickRaise, res.clickRaise());
}


void ConfigFocusmenu::itemClicked(unsigned int id, unsigned int) {
  BlackboxResource &res = _bscreen->blackbox()->resource();
  switch (id) {
  case ClickToFocus:
    _bscreen->toggleFocusModel(ClickToFocusModel);
    break;

  case SloppyFocus:
    _bscreen->toggleFocusModel(SloppyFocusModel);
    break;

  case AutoRaise: // auto raise with sloppy focus
    res.setAutoRaise(!res.autoRaise());
    break;

  case ClickRaise: // click raise with sloppy focus
    res.setClickRaise(!res.clickRaise());
    // make sure the appropriate mouse buttons are grabbed on the windows
    _bscreen->toggleFocusModel(SloppyFocusModel);
    break;

  default: return;
  } // switch
  res.save(*_bscreen->blackbox());
}


ConfigPlacementmenu::ConfigPlacementmenu(bt::Application &app,
                                         unsigned int screen,
                                         BScreen *bscreen)
  : bt::Menu(app, screen), _bscreen(bscreen)
{
  setTitle(bt::toUnicode("Window Placement"));
  showTitle();

  insertItem(bt::toUnicode("Smart Placement (Rows)"), RowSmartPlacement);
  insertItem(bt::toUnicode("Smart Placement (Columns)"), ColSmartPlacement);
  insertItem(bt::toUnicode("Center Placement"), CenterPlacement);
  insertItem(bt::toUnicode("Cascade Placement"), CascadePlacement);

  insertSeparator();

  insertItem(bt::toUnicode("Left to Right"), LeftRight);
  insertItem(bt::toUnicode("Right to Left"), RightLeft);
  insertItem(bt::toUnicode("Top to Bottom"), TopBottom);
  insertItem(bt::toUnicode("Bottom to Top"), BottomTop);

  insertSeparator();

  insertItem(bt::toUnicode("Ignore Shaded Windows"), IgnoreShadedWindows);
}


void ConfigPlacementmenu::refresh(void) {
  const BlackboxResource &res = _bscreen->blackbox()->resource();
  bool rowsmart = res.windowPlacementPolicy() == RowSmartPlacement,
       colsmart = res.windowPlacementPolicy() == ColSmartPlacement,
         center = res.windowPlacementPolicy() == CenterPlacement,
        cascade = res.windowPlacementPolicy() == CascadePlacement,
             rl = res.rowPlacementDirection() == LeftRight,
             tb = res.colPlacementDirection() == TopBottom;

  setItemChecked(RowSmartPlacement, rowsmart);
  setItemChecked(ColSmartPlacement, colsmart);
  setItemChecked(CenterPlacement, center);
  setItemChecked(CascadePlacement, cascade);

  setItemEnabled(LeftRight, !center && !cascade);
  setItemChecked(LeftRight, !center && (cascade || rl));

  setItemEnabled(RightLeft, !center && !cascade);
  setItemChecked(RightLeft, !center && (!cascade && !rl));

  setItemEnabled(TopBottom, !center && !cascade);
  setItemChecked(TopBottom, !center && (cascade || tb));

  setItemEnabled(BottomTop, !center && !cascade);
  setItemChecked(BottomTop, !center && (!cascade && !tb));

  setItemEnabled(IgnoreShadedWindows, !center);
  setItemChecked(IgnoreShadedWindows, !center && res.placementIgnoresShaded());
}


void ConfigPlacementmenu::itemClicked(unsigned int id, unsigned int) {
  BlackboxResource &res = _bscreen->blackbox()->resource();
  switch (id) {
  case RowSmartPlacement:
  case ColSmartPlacement:
  case CenterPlacement:
  case CascadePlacement:
    res.setWindowPlacementPolicy(id);
    break;

  case LeftRight:
  case RightLeft:
    res.setRowPlacementDirection(id);
    break;

  case TopBottom:
  case BottomTop:
    res.setColPlacementDirection(id);
    break;

  case IgnoreShadedWindows:
    res.setPlacementIgnoresShaded(! res.placementIgnoresShaded());
    break;

  default:
    return;
  } // switch
  res.save(*_bscreen->blackbox());
}


ConfigDithermenu::ConfigDithermenu(bt::Application &app, unsigned int screen,
                                   BScreen *bscreen)
  : bt::Menu(app, screen), _bscreen(bscreen)
{
  setTitle(bt::toUnicode("Image Dithering"));
  showTitle();

  insertItem(bt::toUnicode("Do not dither images"), bt::NoDither);
  insertItem(bt::toUnicode("Use fast dither"), bt::OrderedDither);
  insertItem(bt::toUnicode("Use high-quality dither"), bt::FloydSteinbergDither);
}


void ConfigDithermenu::refresh(void) {
  setItemChecked(bt::NoDither,
                 bt::Image::ditherMode() == bt::NoDither);
  setItemChecked(bt::OrderedDither,
                 bt::Image::ditherMode() == bt::OrderedDither);
  setItemChecked(bt::FloydSteinbergDither,
                 bt::Image::ditherMode() == bt::FloydSteinbergDither);
}


void ConfigDithermenu::itemClicked(unsigned int id, unsigned int) {
  bt::Image::setDitherMode((bt::DitherMode) id);
  _bscreen->blackbox()->resource().save(*_bscreen->blackbox());
}
