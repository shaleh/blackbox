// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Widget.cc for Blackbox - an X11 Window manager
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

#ifdef HAVE_CONFIG_H
#  include "../config.h"
#endif // HAVE_CONFIG_H

#include "Widget.hh"
#include "BaseDisplay.hh"
#include "Color.hh"

#include <stdio.h>

WidgetMapper Widget::mapper;


Widget::Widget(int s, Type t)
  : _parent(0), _type(t),
    visible(false), focused(false),
    grabbedMouse(false) , grabbedKeyboard(false),
    _screen(s)
{
  create();
}

Widget::Widget(Widget *p)
  : _parent(p), _type(Normal),
    visible(false), focused(false),
    grabbedMouse(false) , grabbedKeyboard(false),
    _screen(p->screenNumber())
{
  parent()->insertChild(this);
  create();
}

Widget::~Widget()
{
  if (isVisible())
    hide();
  // delete children
  WidgetList::iterator it = _children.begin();
  while (it != _children.end()) {
    Widget *child = *it;
    // we don't remove the child from the list, the widget destructor
    // does this for us... we also don't set the item to zero, since
    // this will cause removeChild to fail...
    ++it;
    delete child;
  }
  if (parent())
    parent()->removeChild(this);
  mapper.erase(win);
  XDestroyWindow(*BaseDisplay::instance(), win);
}

void Widget::create()
{
  BaseDisplay *display = BaseDisplay::instance();
  ScreenInfo *screeninfo = display->screenInfo(screenNumber());
  Window p = parent() ? parent()->win : screeninfo->rootWindow();

  // set the initial geometry
  _rect.setRect(screeninfo->width() / 4, screeninfo->height() / 4,
                screeninfo->width() / 2, screeninfo->height() / 2);

  // create the window
  XSetWindowAttributes attrib;
  unsigned long mask = CWBackPixmap | CWColormap |
                       CWOverrideRedirect | CWEventMask;
  attrib.background_pixmap = None;
  attrib.colormap = screeninfo->colormap();
  attrib.override_redirect = False;
  attrib.event_mask = ButtonPressMask | ButtonReleaseMask |
                      ButtonMotionMask |
                      KeyPressMask | KeyReleaseMask |
                      EnterWindowMask | LeaveWindowMask |
                      FocusChangeMask |
                      ExposureMask |
                      StructureNotifyMask;

  // handle the different window type parameters
  switch(type()) {
  case Normal:
    attrib.override_redirect = False;
    break;

  case Popup:
    attrib.override_redirect = True;
    attrib.event_mask |= PointerMotionMask;
    break;

  case OverrideRedirect:
    attrib.override_redirect = True;
    break;
  }

  win = XCreateWindow(*display, p, x(), y(), width(), height(), 0,
                      screeninfo->depth(), InputOutput,
                      screeninfo->visual(), mask, &attrib);

  setTitle("Untitled");

  mapper.insert(std::pair<Window,Widget*>(win, this));
}

void Widget::insertChild(Widget *child)
{
  _children.insert(_children.end(), child);
}

void Widget::removeChild(Widget *child)
{
  WidgetList::iterator it = _children.begin();
  while (it != _children.end()) {
    if ((*it) == child)
      break;
    ++it;
  }

  assert(it != _children.end());
  _children.erase(it);
}

void Widget::move(int x, int y)
{
    _rect.setPos(x, y);
    XMoveWindow(*BaseDisplay::instance(), win, x, y);
}

void Widget::move(const Point &p)
{
    move(p.x(), p.y());
}

void Widget::resize(int w, int h)
{
    _rect.setSize(w, h);
    XResizeWindow(*BaseDisplay::instance(), win, w, h);
}

void Widget::resize(const Size &s)
{
    resize(s.width(), s.height());
}

void Widget::setGeometry(int x, int y, int w, int h)
{
    _rect = Rect(x, y, w, h);
    XMoveResizeWindow(*BaseDisplay::instance(), win, x, y, w, h);
}

void Widget::setGeometry(const Point &p, const Size &s)
{
    setGeometry(p.x(), p.y(), s.width(), s.height());
}

void Widget::setGeometry(const Rect &r)
{
    setGeometry(r.x(), r.y(), r.width(), r.height());
}

void Widget::show()
{
  if (isVisible())
    return;
  WidgetList::iterator it = _children.begin();
  while (it != _children.end()) {
    (*it)->show();
    ++it;
  }
  if (type() == Popup) {
    XMapRaised(*BaseDisplay::instance(), win);
    BaseDisplay::instance()->popup(this);
  } else
    XMapWindow(*BaseDisplay::instance(), win);
  visible = true;
}

void Widget::hide()
{
    if (! isVisible())
	return;
    if (type() == Popup)
	BaseDisplay::instance()->popdown(this);
    XUnmapWindow(*BaseDisplay::instance(), win);
    visible = false;
}

void Widget::setFocus()
{
    if (! isVisible())
	return;
    XSetInputFocus(*BaseDisplay::instance(), win,
		    RevertToPointerRoot, CurrentTime);
}

void Widget::setTitle(const string &t)
{
    _title = t;
    if (! parent() && _type == Normal) {
      // we have no parent and are a normal window. this means we
      // are toplevel window, so we should set the WM_NAME property
      // so that the window manager knows who we are
      fprintf(stderr, "Widget::setTitle: TODO - set WM_NAME\n");
    }
}

bool Widget::grabMouse()
{
    int ret = XGrabPointer(*BaseDisplay::instance(), win, True,
			    (ButtonPressMask | ButtonReleaseMask |
			      ButtonMotionMask | EnterWindowMask |
			      LeaveWindowMask | PointerMotionMask),
			    GrabModeSync, GrabModeAsync, None, None,
			    CurrentTime);
    grabbedMouse = (ret == GrabSuccess);
    return grabbedMouse;
}

void Widget::ungrabMouse()
{
    if (! grabbedMouse)
	return;

    XUngrabPointer(*BaseDisplay::instance(), CurrentTime);
    grabbedMouse = false;
}

bool Widget::grabKeyboard()
{
    int ret = XGrabKeyboard(*BaseDisplay::instance(), win, True,
			     GrabModeSync, GrabModeAsync, CurrentTime);
    grabbedKeyboard = (ret == GrabSuccess);
    return grabbedKeyboard;
}

void Widget::ungrabKeyboard()
{
    if (! grabbedKeyboard)
	return;

    XUngrabKeyboard(*BaseDisplay::instance(), CurrentTime);
    grabbedKeyboard = false;
}

void Widget::setBackgroundColor(const BColor &color)
{
    XSetWindowBackground(*BaseDisplay::instance(), win, color.pixel());
}

void Widget::buttonPressEvent(XEvent *)
{
}

void Widget::buttonReleaseEvent(XEvent *)
{
}

void Widget::pointerMotionEvent(XEvent *)
{
}

void Widget::keyPressEvent(XEvent *)
{
}

void Widget::keyReleaseEvent(XEvent *)
{
}

void Widget::configureEvent(XEvent *e)
{
    _rect.setRect(e->xconfigure.x,
		   e->xconfigure.y,
		   e->xconfigure.width,
		   e->xconfigure.height);
}

void Widget::mapEvent(XEvent *)
{
    visible = true;
}

void Widget::unmapEvent(XEvent *)
{
    visible = false;
}

void Widget::focusInEvent(XEvent *)
{
    focused = true;
}

void Widget::focusOutEvent(XEvent *)
{
    focused = false;
}

void Widget::exposeEvent(XEvent *)
{
}

void Widget::enterEvent(XEvent *)
{
}

void Widget::leaveEvent(XEvent *)
{
}