// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// BaseDisplay.cc for Blackbox - an X11 Window manager
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
#include <X11/Xutil.h>
#include <X11/keysym.h>

#ifdef    SHAPE
#  include <X11/extensions/shape.h>
#endif // SHAPE

#ifdef    HAVE_FCNTL_H
#  include <fcntl.h>
#endif // HAVE_FCNTL_H

#ifdef    HAVE_STDIO_H
#  include <stdio.h>
#endif // HAVE_STDIO_H

#ifdef    STDC_HEADERS
#  include <stdlib.h>
#  include <string.h>
#endif // STDC_HEADERS

#ifdef    HAVE_UNISTD_H
#  include <sys/types.h>
#  include <unistd.h>
#endif // HAVE_UNISTD_H

#ifdef    HAVE_SYS_SELECT_H
#  include <sys/select.h>
#endif // HAVE_SYS_SELECT_H

#ifdef    HAVE_SIGNAL_H
#  include <signal.h>
#endif // HAVE_SIGNAL_H

#ifndef   SA_NODEFER
#  ifdef   SA_INTERRUPT
#    define SA_NODEFER SA_INTERRUPT
#  else // !SA_INTERRUPT
#    define SA_NODEFER (0)
#  endif // SA_INTERRUPT
#endif // SA_NODEFER

#ifdef    HAVE_SYS_WAIT_H
#  include <sys/types.h>
#  include <sys/wait.h>
#endif // HAVE_SYS_WAIT_H

#include "i18n.hh"
#include "BaseDisplay.hh"
#include "Color.hh"
#include "Timer.hh"
#include "Util.hh"
#include "Widget.hh"

#include <algorithm>
#include <string>
using std::string;
#include <sstream>
using std::ostringstream;


// X error handler to handle any and all X errors while the application is
// running
static Bool internal_error = False;
static Window last_bad_window = None;

BaseDisplay *base_display;

static int handleXErrors(Display *d, XErrorEvent *e)
{
#ifdef    DEBUG
  char errtxt[128];

  XGetErrorText(d, e->error_code, errtxt, 128);
  fprintf(stderr,
          i18n(BaseDisplaySet, BaseDisplayXError,
               "%s:  X error: %s(%d) opcodes %d/%d\n  resource 0x%lx\n"),
          base_display->getApplicationName(), errtxt, e->error_code,
          e->request_code, e->minor_code, e->resourceid);
#else
  // be quiet gcc
  d = d;
#endif // DEBUG

  if (e->error_code == BadWindow) last_bad_window = e->resourceid;
  if (internal_error) abort();

  return False;
}


// signal handler to allow for proper and gentle shutdown

#ifndef   HAVE_SIGACTION
static
RETSIGTYPE
#else //  HAVE_SIGACTION
void
#endif // HAVE_SIGACTION
signalhandler(int sig) {
  static int re_enter = 0;

  switch (sig) {
  case SIGCHLD:
    int status;
    waitpid(-1, &status, WNOHANG | WUNTRACED);

#ifndef   HAVE_SIGACTION
    // assume broken, braindead sysv signal semantics
    signal(SIGCHLD, (RETSIGTYPE (*)(int)) signalhandler);
#endif // HAVE_SIGACTION

    break;

  default:
    if (base_display->handleSignal(sig)) {

#ifndef   HAVE_SIGACTION
      // assume broken, braindead sysv signal semantics
      signal(sig, (RETSIGTYPE (*)(int)) signalhandler);
#endif // HAVE_SIGACTION

      return;
    }

    fprintf(stderr, i18n(BaseDisplaySet, BaseDisplaySignalCaught,
                         "%s:  signal %d caught\n"),
            base_display->applicationName().c_str(), sig);

    if (! base_display->isStartup() && ! re_enter) {
      internal_error = True;

      re_enter = 1;
      fprintf(stderr, i18n(BaseDisplaySet, BaseDisplayShuttingDown,
                           "shutting down\n"));
      base_display->shutdown();
    }

    if (sig != SIGTERM && sig != SIGINT) {
      fprintf(stderr, i18n(BaseDisplaySet, BaseDisplayAborting,
                           "aborting... dumping core\n"));
      abort();
    }

    exit(0);

    break;
  }
}

BaseDisplay::BaseDisplay(const string &app_name, const string &dpy_name)
  : popwidget(0), popup_grab(false)
{
  application_name = app_name;

  _startup = True;
  _shutdown = False;
  last_bad_window = None;

  ::base_display = this;

#ifdef    HAVE_SIGACTION
  struct sigaction action;

  action.sa_handler = signalhandler;
  action.sa_mask = sigset_t();
  action.sa_flags = SA_NOCLDSTOP | SA_NODEFER;

  sigaction(SIGPIPE, &action, NULL);
  sigaction(SIGSEGV, &action, NULL);
  sigaction(SIGFPE, &action, NULL);
  sigaction(SIGTERM, &action, NULL);
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGCHLD, &action, NULL);
  sigaction(SIGHUP, &action, NULL);
  sigaction(SIGUSR1, &action, NULL);
  sigaction(SIGUSR2, &action, NULL);
#else // !HAVE_SIGACTION
  signal(SIGPIPE, (RETSIGTYPE (*)(int)) signalhandler);
  signal(SIGSEGV, (RETSIGTYPE (*)(int)) signalhandler);
  signal(SIGFPE, (RETSIGTYPE (*)(int)) signalhandler);
  signal(SIGTERM, (RETSIGTYPE (*)(int)) signalhandler);
  signal(SIGINT, (RETSIGTYPE (*)(int)) signalhandler);
  signal(SIGUSR1, (RETSIGTYPE (*)(int)) signalhandler);
  signal(SIGUSR2, (RETSIGTYPE (*)(int)) signalhandler);
  signal(SIGHUP, (RETSIGTYPE (*)(int)) signalhandler);
  signal(SIGCHLD, (RETSIGTYPE (*)(int)) signalhandler);
#endif // HAVE_SIGACTION

  _display = XOpenDisplay(dpy_name.c_str());
  if (! _display) {
    fprintf(stderr,
            i18n(BaseDisplaySet, BaseDisplayXConnectFail,
               "BaseDisplay::BaseDisplay: connection to X server failed.\n"));
    ::exit(2);
  } else if (fcntl(ConnectionNumber(_display), F_SETFD, 1) == -1) {
    fprintf(stderr,
            i18n(BaseDisplaySet, BaseDisplayCloseOnExecFail,
                 "BaseDisplay::BaseDisplay: couldn't mark display connection "
                 "as close-on-exec\n"));
    ::exit(2);
  }

  screen_count = ScreenCount(_display);
  display_name = XDisplayName(dpy_name.c_str());

#ifdef    SHAPE
  shape.extensions = XShapeQueryExtension(_display, &shape.event_basep,
                                          &shape.error_basep);
#else // !SHAPE
  shape.extensions = False;
#endif // SHAPE

  XSetErrorHandler((XErrorHandler) handleXErrors);

  for (int i = 0; i < screenCount(); i++) {
    ScreenInfo *screeninfo = new ScreenInfo(this, i);
    screenInfoList.push_back(screeninfo);
  }

  NumLockMask = ScrollLockMask = 0;

  const XModifierKeymap* const modmap = XGetModifierMapping(_display);
  if (modmap && modmap->max_keypermod > 0) {
    const int mask_table[] = {
      ShiftMask, LockMask, ControlMask, Mod1Mask,
      Mod2Mask, Mod3Mask, Mod4Mask, Mod5Mask
    };
    const size_t size = (sizeof(mask_table) / sizeof(mask_table[0])) *
                        modmap->max_keypermod;
    // get the values of the keyboard lock modifiers
    // Note: Caps lock is not retrieved the same way as Scroll and Num lock
    // since it doesn't need to be.
    const KeyCode num_lock = XKeysymToKeycode(_display, XK_Num_Lock);
    const KeyCode scroll_lock = XKeysymToKeycode(_display, XK_Scroll_Lock);

    for (size_t cnt = 0; cnt < size; ++cnt) {
      if (! modmap->modifiermap[cnt]) continue;

      if (num_lock == modmap->modifiermap[cnt])
        NumLockMask = mask_table[cnt / modmap->max_keypermod];
      if (scroll_lock == modmap->modifiermap[cnt])
        ScrollLockMask = mask_table[cnt / modmap->max_keypermod];
    }
  }

  MaskList[0] = 0;
  MaskList[1] = LockMask;
  MaskList[2] = NumLockMask;
  MaskList[3] = ScrollLockMask;
  MaskList[4] = LockMask | NumLockMask;
  MaskList[5] = NumLockMask  | ScrollLockMask;
  MaskList[6] = LockMask | ScrollLockMask;
  MaskList[7] = LockMask | NumLockMask | ScrollLockMask;
  MaskListLength = sizeof(MaskList) / sizeof(MaskList[0]);

  if (modmap)
    XFreeModifiermap(const_cast<XModifierKeymap*>(modmap));
}


BaseDisplay::~BaseDisplay(void) {
  std::for_each(screenInfoList.begin(), screenInfoList.end(),
                PointerAssassin());

  // we don't create the BTimers, we don't delete them

  XCloseDisplay(_display);
}

BaseDisplay *BaseDisplay::instance()
{
  return ::base_display;
}

void BaseDisplay::eventLoop(void)
{
  run();

  int xfd = ConnectionNumber(_display);

  while ((! _shutdown) && (! internal_error)) {
    if (XPending(_display)) {
      XEvent e;
      XNextEvent(_display, &e);

      if (last_bad_window != None && e.xany.window == last_bad_window)
        continue;

      last_bad_window = None;
      process_event(&e);
    } else {
      fd_set rfds;
      timeval now, tm, *timeout = (timeval *) 0;

      FD_ZERO(&rfds);
      FD_SET(xfd, &rfds);

      if (! timerList.empty()) {
        BTimer *timer = timerList.top();

        gettimeofday(&now, 0);
        tm = timer->timeRemaining(now);

        timeout = &tm;
      }

      select(xfd + 1, &rfds, 0, 0, timeout);

      // check for timer timeout
      gettimeofday(&now, 0);

      // there is a small chance for deadlock here:
      // *IF* the timer list keeps getting refreshed *AND* the time between
      // timer->start() and timer->shouldFire() is within the timer's period
      // then the timer will keep firing.  This should be VERY near impossible.
      while (! timerList.empty()) {
        BTimer *timer = timerList.top();
        if (! timer->shouldFire(now))
          break;

        timerList.pop();

        timer->fireTimeout();
        timer->halt();
        if (timer->isRecurring())
          timer->start();
      }
    }

    BColor::cleanupColorCache();
  }
}

void BaseDisplay::popup(Widget *widget)
{
  popwidgets.push_front(widget);
  popwidget = widget;
  // grab the mouse and keyboard for popup handling
  if (! popup_grab && popwidget->grabKeyboard() &&  popwidget->grabMouse())
    XAllowEvents(*this, SyncPointer, CurrentTime);
  popup_grab = true;
}

void BaseDisplay::popdown(Widget *widget)
{
  if (! popwidget)
    return;

  if (widget != popwidget) {
    fprintf(stderr, "BaseDisplay: widget %p not popwidget\n", widget);
    abort();
  }

  popwidgets.pop_front();

  if (popwidgets.size() == 0) {
    // no more popups
    popwidget->ungrabKeyboard();
    popwidget->ungrabMouse();
    popwidget = 0;
    popup_grab = false;
    XAllowEvents(*this, ReplayPointer, CurrentTime);
    XSync(*this, False);
    return;
  }

  // more popups!
  popwidget = *popwidgets.begin();
}

void BaseDisplay::process_event(XEvent *e)
{
  Widget *widget = 0;

  WidgetMapper::iterator it = Widget::mapper.find(e->xany.window);
  if (it != Widget::mapper.end())
    widget = (*it).second;

  if (! widget) {
    // unknown window - perhaps a root window?
    bool root = false;
    int i;
    for (i = 0; ! root && i < screenCount(); i++) {
      if (screenInfo(i)->rootWindow() == e->xany.window)
        root = true;
    }

    if (popwidget) {
      switch(e->type) {
      case ButtonPress:
      case ButtonRelease:
      case KeyPress:
      case KeyRelease:
        if (! root) {
          // close all popups
          do {
            popwidget->hide();
          } while (popwidget);
        } else {
          // send button and key events to popwidget
          widget = popwidget;
        }
        break;

      default:
        break;
      }
    }

    if (! widget)
      return;
  }

  switch (e->type) {
  case ButtonPress:
  case ButtonRelease:
  case MotionNotify:
    if (popup_grab) {
      if (popwidget != widget) {
        if (widget->type() != Widget::Popup ||
            ! widget->rect().contains(Point(e->xbutton.x_root,
                                            e->xbutton.y_root)))
          widget = popwidget;
      }
      XAllowEvents(*this, SyncPointer, CurrentTime);
    }
    switch (e->type) {
    case ButtonPress:   widget->buttonPressEvent(e);   break;
    case ButtonRelease: widget->buttonReleaseEvent(e); break;
    case MotionNotify:
      {
        XEvent realevent;
        unsigned int i = 0;
        while(XCheckTypedWindowEvent(*this, e->xmotion.window, MotionNotify,
                                     &realevent))
          i++;
        if (i > 0)
          e = &realevent;
        widget->pointerMotionEvent(e);
        break;
      }
    }
    break;

  case EnterNotify:
    if (e->xcrossing.mode != NotifyNormal ||
        e->xcrossing.detail == NotifyVirtual  ||
        e->xcrossing.detail == NotifyNonlinearVirtual)
      break;
    widget->enterEvent(e);
    break;

  case LeaveNotify:
    if (e->xcrossing.mode != NotifyNormal)
      break;
    widget->leaveEvent(e);
    break;

  case KeyPress:
    if (popup_grab) {
      widget = popwidget;
      XAllowEvents(*this, SyncKeyboard, CurrentTime);
    }
    widget->keyPressEvent(e);
    break;

  case KeyRelease:
    if (popup_grab) {
      widget = popwidget;
      XAllowEvents(*this, SyncKeyboard, CurrentTime);
    }
    widget->keyReleaseEvent(e);
    break;

  case ConfigureNotify:
    {
      XEvent realevent;
      unsigned int i = 0;
      while(XCheckTypedWindowEvent(*this, e->xconfigure.window, ConfigureNotify,
                                   &realevent))
        i++;
      if (i > 0)
        e = &realevent;
      widget->configureEvent(e);
      break;
    }

  case MapNotify:
    widget->mapEvent(e);
    break;

  case UnmapNotify:
    widget->unmapEvent(e);
    break;

  case FocusIn:
    if (e->xfocus.detail != NotifyAncestor &&
        e->xfocus.detail != NotifyInferior &&
        e->xfocus.detail != NotifyNonlinear)
      break;
    widget->focusInEvent(e);
    break;

  case FocusOut:
    if (e->xfocus.detail != NotifyAncestor &&
        e->xfocus.detail != NotifyInferior &&
        e->xfocus.detail != NotifyNonlinear)
      break;
    widget->focusOutEvent(e);
    break;

  case Expose:
    {
      // compress expose events
      XEvent realevent;
      unsigned int i = 0;
      Rect r(e->xexpose.x, e->xexpose.y, e->xexpose.width, e->xexpose.height);
      while (XCheckTypedWindowEvent(*this, e->xexpose.window,
                                    Expose, &realevent)) {
        Rect r2(realevent.xexpose.x, realevent.xexpose.y,
                realevent.xexpose.width, realevent.xexpose.height);
        if (r.intersects(r2) || widget->type() != Widget::Popup) {
          // merge expose area
          r |= r2;
          i++;
        } else {
          // don't merge disjoint regions in popups/overrides, this
          // causes unnecessary repaints when showing/hiding submenus
          if (widget->isVisible())
            widget->exposeEvent(&realevent);
          break;
        }
      }
      if (i > 0) {
        e = &realevent;
      }
      // use the merged area
      e->xexpose.x = r.x();
      e->xexpose.y = r.y();
      e->xexpose.width = r.width();
      e->xexpose.height = r.height();
      if (widget->isVisible())
        widget->exposeEvent(e);
      break;
    }

  default:
    break;
  }
}

void BaseDisplay::addTimer(BTimer *timer) {
  if (! timer) return;

  timerList.push(timer);
}


void BaseDisplay::removeTimer(BTimer *timer) {
  timerList.release(timer);
}

/*
 * Grabs a button, but also grabs the button in every possible combination
 * with the keyboard lock keys, so that they do not cancel out the event.
 */
void BaseDisplay::grabButton(unsigned int button, unsigned int modifiers,
                             Window grab_window, Bool owner_events,
                             unsigned int event_mask, int pointer_mode,
                             int keyboard_mode, Window confine_to,
                             Cursor cursor) const {
  for (size_t cnt = 0; cnt < MaskListLength; ++cnt) {
    XGrabButton(_display, button, modifiers | MaskList[cnt], grab_window,
                owner_events, event_mask, pointer_mode, keyboard_mode,
                confine_to, cursor);
  }
}

/*
 * Releases the grab on a button, and ungrabs all possible combinations of the
 * keyboard lock keys.
 */
void BaseDisplay::ungrabButton(unsigned int button, unsigned int modifiers,
                               Window grab_window) const {
  for (size_t cnt = 0; cnt < MaskListLength; ++cnt) {
    XUngrabButton(_display, button, modifiers | MaskList[cnt], grab_window);
  }
}


ScreenInfo* BaseDisplay::screenInfo(unsigned int s) {
  if (s < screenInfoList.size()) {
    ScreenInfoList::iterator it = screenInfoList.begin();
    for (; s > 0; ++it, --s); // increment interator to index
    return *it;
  }
  return (ScreenInfo*) 0;
}


ScreenInfo::ScreenInfo(BaseDisplay *d, int num)
{
  _display = d;
  _screen = num;

  _rootwindow = RootWindow(display()->x11Display(), screenNumber());
  _depth = DefaultDepth(display()->x11Display(), screenNumber());

  _rect.setRect( 0, 0, WidthOfScreen(ScreenOfDisplay(display()->x11Display(),
                                                     screenNumber())),
                 HeightOfScreen(ScreenOfDisplay(display()->x11Display(),
                                                screenNumber())));

  // search for a TrueColor Visual... if we can't find one... we will use the
  // default visual for the screen
  XVisualInfo vinfo_template, *vinfo_return;
  int vinfo_nitems;

  vinfo_template.screen = screenNumber();
  vinfo_template.c_class = TrueColor;

  _visual = (Visual *) 0;

  vinfo_return = XGetVisualInfo(*display(), VisualScreenMask | VisualClassMask,
                                &vinfo_template, &vinfo_nitems);
  if (vinfo_return && vinfo_nitems > 0) {
    for (int i = 0; i < vinfo_nitems; i++) {
      if (_depth < (vinfo_return + i)->depth) {
        _depth = (vinfo_return + i)->depth;
        _visual = (vinfo_return + i)->visual;
      }
    }

    XFree(vinfo_return);
  }

  if (_visual) {
    _colormap = XCreateColormap(*display(), _rootwindow, _visual, AllocNone);
  } else {
    _visual = DefaultVisual(display()->x11Display(), screenNumber());
    _colormap = DefaultColormap(display()->x11Display(), screenNumber());
  }

  ostringstream out;
  displaystring = DisplayString(_display->x11Display());
  unsigned int dot = displaystring.rfind('.');
  if (dot != string::npos)
    displaystring.resize(dot);
  out << "DISPLAY=" << displaystring << '.' << screenNumber();
  displaystring = out.str();
}
