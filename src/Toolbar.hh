// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Toolbar.hh for Blackbox - an X11 Window manager
// Copyright (c) 2001 - 2002 Sean 'Shaleh' Perry <shaleh at debian.org>
// Copyright (c) 1997 - 2000, 2002 Bradley T Hughes <bhughes at trolltech.com>
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

#ifndef   __Toolbar_hh
#define   __Toolbar_hh

#include <X11/Xlib.h>

#include "Screen.hh"
#include "Basemenu.hh"
#include "Timer.hh"
#include "Widget.hh"

// forward declaration
class Toolbar;
class Toolbarmenu;
class ToolbarClock;

class Toolbar2 : public Widget
{
public:
  enum Placement { TopLeft , BottomLeft, TopCenter,
                   BottomCenter, TopRight, BottomRight };
  Toolbar2(BScreen *);
  ~Toolbar2();

  void reconfigure();

  void setPlacement(Placement);
  Placement placement() const { return _placement; }

protected:
  void buttonPressEvent(XEvent *);
  void buttonReleaseEvent(XEvent *);
  void enterEvent(XEvent *);
  void leaveEvent(XEvent *);
  void exposeEvent(XEvent *);

private:
  void updateLayout();
  void updatePlacement();
  void updatePixmap();

  BTimer *clock_timer, *hide_timer;
  Pixmap pixmap, texture_pixmap;
  Rect texture_rect;
  Toolbarmenu *toolbarmenu;
  NETStrut strut;
  bool always_on_top, editing, hidden, auto_hide;
  Placement _placement;

  ToolbarClock *clock;

  class HideHandler : public TimeoutHandler
  {
  public:
    Toolbar *toolbar;
    void timeout();
  } hide_handler;

  class ClockHandler : public TimeoutHandler
  {
  public:
    Toolbar *toolbar;
    void timeout();
  } clock_handler;

  friend class HideHandler;
  friend class ClockHandler;
};


class Toolbar : public TimeoutHandler {
private:
  bool on_top, editing, hidden, auto_hide;

  struct ToolbarFrame {
    unsigned long button_pixel, pbutton_pixel;
    Pixmap base, label, wlabel, clk, button, pbutton;
    Window window, workspace_label, window_label, clock, psbutton, nsbutton,
      pwbutton, nwbutton;

    int x, y, x_hidden, y_hidden, hour, minute, grab_x, grab_y;
    unsigned int width, height, window_label_w, workspace_label_w, clock_w,
      button_w, bevel_w, label_h;
  } frame;

  class HideHandler : public TimeoutHandler {
  public:
    Toolbar *toolbar;

    virtual void timeout(void);
  } hide_handler;

  Blackbox *blackbox;
  BImageControl *image_ctrl;
  BScreen *screen;
  BTimer *clock_timer, *hide_timer;
  Toolbarmenu *toolbarmenu;
  NETStrut strut;

  char *new_workspace_name;
  size_t new_name_pos;

  friend class HideHandler;
  friend class Toolbarmenu;

  Toolbar(const Toolbar&);
  Toolbar& operator=(const Toolbar&);

public:
  Toolbar(BScreen *scrn);
  virtual ~Toolbar(void);

  inline const Bool isEditing(void) const { return editing; }
  void setOnTop(bool t);
  inline const Bool isOnTop(void) const { return on_top; }
  inline const Bool isHidden(void) const { return hidden; }
  inline const Bool autoHide(void) const { return auto_hide; }
  void setAutoHide(bool h);

  inline const Window windowID(void) const { return frame.window; }

  inline const unsigned int width() const { return frame.width; }
  inline const unsigned int height() const { return frame.height; }
  inline const unsigned int &getExposedHeight(void) const
    { return ((auto_hide) ? frame.bevel_w : frame.height); }
  inline const int &getX(void) const
    { return ((hidden) ? frame.x_hidden : frame.x); }
  inline const int getY(void) const
    { return ((hidden) ? frame.y_hidden : frame.y); }

  void buttonPressEvent(XButtonEvent *be);
  void buttonReleaseEvent(XButtonEvent *re);
  void enterNotifyEvent(XCrossingEvent * /*unused*/);
  void leaveNotifyEvent(XCrossingEvent * /*unused*/);
  void exposeEvent(XExposeEvent *ee);
  void keyPressEvent(XKeyEvent *ke);

  void redrawWindowLabel(Bool redraw = False);
  void redrawWorkspaceLabel(Bool redraw = False);
  void redrawPrevWorkspaceButton(Bool pressed = False, Bool redraw = False);
  void redrawNextWorkspaceButton(Bool pressed = False, Bool redraw = False);
  void redrawPrevWindowButton(Bool preseed = False, Bool redraw = False);
  void redrawNextWindowButton(Bool preseed = False, Bool redraw = False);
  void edit(void);
  void reconfigure(void);

#ifdef    HAVE_STRFTIME
  void checkClock(Bool redraw = False);
#else //  HAVE_STRFTIME
  void checkClock(Bool redraw = False, Bool date = False);
#endif // HAVE_STRFTIME

  virtual void timeout(void);

  enum Placement { TopLeft = 1, BottomLeft, TopCenter,
                   BottomCenter, TopRight, BottomRight };
  void setPlacement(Placement);
};


#endif // __Toolbar_hh
