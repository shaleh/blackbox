// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Workspace.cc for Blackbox - an X11 Window manager
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

#ifdef    HAVE_CONFIG_H
#  include "../config.h"
#endif // HAVE_CONFIG_H

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "i18n.hh"
#include "blackbox.hh"
#include "Clientmenu.hh"
#include "Screen.hh"
#include "Toolbar.hh"
#include "Util.hh"
#include "Window.hh"
#include "Workspace.hh"
#include "Windowmenu.hh"

#ifdef    HAVE_STDIO_H
#  include <stdio.h>
#endif // HAVE_STDIO_H

#ifdef    STDC_HEADERS
#  include <string.h>
#endif // STDC_HEADERS


Workspace::Workspace(BScreen *scrn, int i)
  : screen(scrn)
{
  screen = scrn;

  cascade_x = cascade_y = 0;

  id = i;

  stackingList = new LinkedList<BlackboxWindow>;
  windowList = new LinkedList<BlackboxWindow>;
  clientmenu = new Clientmenu(this, screen->screenNumber());

  lastfocus = (BlackboxWindow *) 0;

  name = (char *) 0;
  char *tmp = screen->getNameOfWorkspace(id);
  setName(tmp);
}


Workspace::~Workspace(void) {
  delete stackingList;
  delete windowList;
  delete clientmenu;

  if (name)
    delete [] name;
}


int Workspace::addWindow(BlackboxWindow *w, Bool place)
{
  if (! w)
    return -1;

  if (place)
    placeWindow(w);

  w->setWorkspace(id);
  w->setWindowNumber(windowList->count());

  stackingList->insert(w, 0);
  windowList->insert(w);

  clientmenu->insert(*w->getTitle());

  screen->updateNetizenWindowAdd(w->getClientWindow(), id);

  raiseWindow(w);

  return w->getWindowNumber();
}

int Workspace::removeWindow(BlackboxWindow *w)
{
  if (! w)
    return -1;

  stackingList->remove(w);

  if (w->isFocused()) {
    BlackboxWindow *newfocus = 0;
    if (w->isTransient()) newfocus = w->getTransientFor();
    if (! newfocus) newfocus = stackingList->first();
    if (! newfocus || ! newfocus->setInputFocus()) {
      Blackbox::instance()->setFocusedWindow(0);
    }
  }

  if (lastfocus == w)
    lastfocus = (BlackboxWindow *) 0;

  windowList->remove(w->getWindowNumber());
  clientmenu->remove(w->getWindowNumber());

  screen->updateNetizenWindowDel(w->getClientWindow());

  LinkedListIterator<BlackboxWindow> it(windowList);
  BlackboxWindow *bw = it.current();
  for (int i = 0; bw; it++, i++, bw = it.current())
    bw->setWindowNumber(i);

  return windowList->count();
}


void Workspace::showAll(void) {
  LinkedListIterator<BlackboxWindow> it(stackingList);
  for (BlackboxWindow *bw = it.current(); bw; it++, bw = it.current())
    bw->deiconify(False, False);
}


void Workspace::hideAll(void) {
  LinkedList<BlackboxWindow> lst;

  LinkedListIterator<BlackboxWindow> it(stackingList);
  for (BlackboxWindow *bw = it.current(); bw; it++, bw = it.current())
    lst.insert(bw, 0);

  LinkedListIterator<BlackboxWindow> it2(&lst);
  for (BlackboxWindow *bw = it2.current(); bw; it2++, bw = it2.current())
    if (! bw->isStuck())
      bw->withdraw();
}


void Workspace::removeAll(void) {
  LinkedListIterator<BlackboxWindow> it(windowList);
  for (BlackboxWindow *bw = it.current(); bw; it++, bw = it.current())
    bw->iconify();
}


void Workspace::raiseWindow(BlackboxWindow *w) {
  BlackboxWindow *win = (BlackboxWindow *) 0, *bottom = w;

  while (bottom->isTransient()) {
    BlackboxWindow *bw = bottom->getTransientFor();
    if (! bw) break;
    bottom = bw;
  }

  int i = 1;
  win = bottom;
  while (win->hasTransient() && win->getTransient()) {
    win = win->getTransient();
    i++;
  }

  Window *nstack = new Window[i], *curr = nstack;
  Workspace *wkspc;

  win = bottom;
  while (True) {
    *(curr++) = win->getFrameWindow();
    screen->updateNetizenWindowRaise(win->getClientWindow());

    if (! win->isIconic()) {
      wkspc = screen->getWorkspace(win->getWorkspaceNumber());
      wkspc->stackingList->remove(win);
      wkspc->stackingList->insert(win, 0);
    }

    if (! win->hasTransient() || ! win->getTransient())
      break;
    win = win->getTransient();
  }

  screen->raiseWindows(nstack, i);

  delete [] nstack;
}


void Workspace::lowerWindow(BlackboxWindow *w) {
  BlackboxWindow *win = (BlackboxWindow *) 0, *bottom = w;

  while (bottom->isTransient()) {
    BlackboxWindow *bw = bottom->getTransientFor();
    if (! bw) break;
    bottom = bw;
  }

  int i = 1;
  win = bottom;
  while (win->hasTransient() && win->getTransient()) {
    win = win->getTransient();

    i++;
  }

  Window *nstack = new Window[i], *curr = nstack;
  Workspace *wkspc;

  while (True) {
    *(curr++) = win->getFrameWindow();
    screen->updateNetizenWindowLower(win->getClientWindow());

    if (! win->isIconic()) {
      wkspc = screen->getWorkspace(win->getWorkspaceNumber());
      wkspc->stackingList->remove(win);
      wkspc->stackingList->insert(win);
    }

    win = win->getTransientFor();
    if (! win)
      break;
  }

  XLowerWindow(*BaseDisplay::instance(), *nstack);
  XRestackWindows(*BaseDisplay::instance(), nstack, i);

  delete [] nstack;
}


void Workspace::reconfigure(void) {
  clientmenu->reconfigure();

  LinkedListIterator<BlackboxWindow> it(windowList);
  for (BlackboxWindow *bw = it.current(); bw; it++, bw = it.current()) {
    if (bw->validateClient())
      bw->reconfigure();
  }
}


BlackboxWindow *Workspace::getWindow(int index) {
  if ((index >= 0) && (index < windowList->count()))
    return windowList->find(index);
  else
    return 0;
}

void Workspace::changeName(BlackboxWindow *window)
{
  clientmenu->change(window->getWindowNumber(), *window->getTitle());
  screen->getToolbar()->redrawWindowLabel(True);
}

Bool Workspace::isCurrent(void) {
  return (id == screen->getCurrentWorkspaceID());
}


Bool Workspace::isLastWindow(BlackboxWindow *w) {
  return (w == windowList->last());
}

void Workspace::setCurrent(void) {
  screen->changeWorkspaceID(id);
}


void Workspace::setName(char *new_name) {
  if (name)
    delete [] name;

  if (new_name) {
    name = bstrdup(new_name);
  } else {
    name = new char[128];
    sprintf(name, i18n(WorkspaceSet, WorkspaceDefaultNameFormat, "Workspace %d"),
            id + 1);
  }

  clientmenu->setTitle(name);
  clientmenu->showTitle();
}


void Workspace::shutdown(void) {
  while (windowList->count()) {
    windowList->first()->restore();
    delete windowList->first();
  }
}

#define CALC_POSSIBLE_X() \
  { \
    if (r.right() > x) \
      possible = possible < r.right() ? possible : r.right(); \
    if (r.left() - width > x) \
      possible = possible < r.left() - width ? possible : r.left() - width; \
  }

#define CALC_POSSIBLE_Y() \
  { \
    if (r.bottom() > y) \
      possible = possible < r.bottom() ? possible : r.bottom(); \
    if (r.top() - height > y) \
      possible = possible < r.top() - height ? possible : r.top() - height; \
  }

#define CALC_OVERLAP() \
  { \
    overlap = 0; \
    Rect one(x, y, width, height); \
    BlackboxWindow *c; \
    LinkedListIterator<BlackboxWindow> it(windowList); \
    while ( it.current() ) { \
      c = it.current(); \
      it++; \
      if (c != win) { \
        int h = c->isShaded() ? c->getTitleHeight() : c->getHeight(); \
        Rect two(c->getXFrame(), c->getYFrame(), \
                 c->getWidth() + screen->style()->borderWidth() * 2, \
                 h + screen->style()->borderWidth() * 2); \
        if (two.intersects(one)) { \
          two &= one; \
          overlap += (two.right() - two.left()) * \
                     (two.bottom() - two.top()); \
        } \
      } \
    } \
  }

void Workspace::placeWindow(BlackboxWindow *win) {
  Rect avail = screen->availableArea();
  int x = avail.x(), y = avail.y(), place_x = x, place_y = y;
  int width = win->getWidth() + screen->style()->borderWidth() * 2,
     height = win->getHeight() + screen->style()->borderWidth() * 2;

  switch (screen->windowPlacement()) {
  case BScreen::SmartRow:
    {
      // when smart placing windows bigger than the available area, constrain them
      // to the size of the available area
      if (width >= avail.width())
        width = avail.width() - 1;
      if (height >= avail.height())
        height = avail.height() - 1;

      int overlap = 0, min = INT_MAX, possible = 0;

      do {
        if (y + height > avail.bottom()) {
          overlap = -1;
        } else if (x + width > avail.right()) {
          overlap = -2;
        } else {
          CALC_OVERLAP();
        }

        if (overlap == 0) {
          place_x = x;
          place_y = y;
          break;
        }

        if (overlap >= 0 && overlap < min) {
          min = overlap;
          place_x = x;
          place_y = y;
        }

        if (overlap > 0) {
          possible = avail.right();
          if (possible - width > x)
            possible -= width;

          BlackboxWindow *c;
          LinkedListIterator<BlackboxWindow> it(windowList);
          while ( it.current() ) {
            c = it.current();
            it++;
            if (c != win) {
              int h = c->isShaded() ? c->getTitleHeight() : c->getHeight();
              Rect r(c->getXFrame(), c->getYFrame(),
                     c->getWidth() + screen->style()->borderWidth() * 2,
                     h + screen->style()->borderWidth() * 2);

              if ((y < r.bottom()) && (r.top() < y + height)) {
                CALC_POSSIBLE_X();
              }
            }
          }

          x = possible;
        } else if (overlap == -2) {
          x = avail.x();
          possible = avail.bottom();
          if (possible - height > y)
            possible -= height;

          BlackboxWindow *c;
          LinkedListIterator<BlackboxWindow> it(windowList);
          while (it.current()) {
            c = it.current();
            it++;
            if (c != win) {
              int h = c->isShaded() ? c->getTitleHeight() : c->getHeight();
              Rect r(c->getXFrame(), c->getYFrame(),
                     c->getWidth() + screen->style()->borderWidth() * 2,
                     h + screen->style()->borderWidth() * 2);
              CALC_POSSIBLE_Y();
            }
          }

          y = possible;
        }
      } while (overlap != 0 && overlap != -1);
      break;
    }

  case BScreen::SmartColumn:
    {
      // when smart placing windows bigger than the available area, constrain them
      // to the size of the available area
      if (width >= avail.width())
        width = avail.width() - 1;
      if (height >= avail.height())
        height = avail.height() - 1;

      int overlap = 0, min = INT_MAX, possible = 0;

      do {
        if (y + height > avail.bottom()) {
          overlap = -2;
        } else if (x + width > avail.right()) {
          overlap = -1;
        } else {
          CALC_OVERLAP();
        }

        if (overlap == 0) {
          place_x = x;
          place_y = y;
          break;
        }

        if (overlap >= 0 && overlap < min) {
          min = overlap;
          place_x = x;
          place_y = y;
        }

        if (overlap > 0) {
          possible = avail.bottom();
          if (possible - height > y)
            possible -= height;

          BlackboxWindow *c;
          LinkedListIterator<BlackboxWindow> it(windowList);
          while ( it.current() ) {
            c = it.current();
            it++;
            if (c != win) {
              int h = c->isShaded() ? c->getTitleHeight() : c->getHeight();
              Rect r(c->getXFrame(), c->getYFrame(),
                     c->getWidth() + screen->style()->borderWidth() * 2,
                     h + screen->style()->borderWidth() * 2);

              if ((x < r.right()) && (r.left() < x + width)) {
                CALC_POSSIBLE_Y();
              }
            }
          }

          y = possible;
        } else if (overlap == -2) {
          y = avail.y();
          possible = avail.right();
          if (possible - width > x)
            possible -= width;

          BlackboxWindow *c;
          LinkedListIterator<BlackboxWindow> it(windowList);
          while (it.current()) {
            c = it.current();
            it++;
            if (c != win) {
              int h = c->isShaded() ? c->getTitleHeight() : c->getHeight();
              Rect r(c->getXFrame(), c->getYFrame(),
                     c->getWidth() + screen->style()->borderWidth() * 2,
                     h + screen->style()->borderWidth() * 2);
              CALC_POSSIBLE_X();
            }
          }

          x = possible;
        }
      } while (overlap != 0 && overlap != -1);
      break;
    }

  case BScreen::Cascade:
    {
      place_x = cascade_x;
      place_y = cascade_y;

      // if the window is off the edge of the screen, center it
      if (place_x + width > avail.right() ||
          place_y + height > avail.bottom()) {
        place_x = (avail.width() - width) / 2;
        place_y = (avail.height() - height) / 2;
      }

      // go to next cascade placement point
      cascade_x += 32;
      cascade_y += 32;
      // wrap if necessary
      cascade_x %= avail.width();
      cascade_y %= avail.height();
      break;
    }
  }

  win->configure(place_x, place_y, win->getWidth(), win->getHeight());
}
