// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Application.cc for Blackbox - an X11 Window manager
// Copyright (c) 2001 - 2003 Sean 'Shaleh' Perry <shaleh at debian.org>
// Copyright (c) 1997 - 2000, 2002 - 2003
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

#ifdef    HAVE_CONFIG_H
#  include "../config.h"
#endif // HAVE_CONFIG_H

extern "C" {
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#ifdef    SHAPE
#  include <X11/extensions/shape.h>
#endif // SHAPE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
}

#include "Application.hh"
#include "EventHandler.hh"
#include "Menu.hh"


static bt::Application *base_app = 0;
static sig_atomic_t pending_signals = 0;


static int handleXErrors(Display *d, XErrorEvent *e) {
#ifdef    DEBUG
  char errtxt[128];

  XGetErrorText(d, e->error_code, errtxt, 128);
  fprintf(stderr, "%s:  X error: %s(%d) opcodes %d/%d\n  resource 0x%lx\n",
          base_app->applicationName().c_str(), errtxt, e->error_code,
          e->request_code, e->minor_code, e->resourceid);
#else
  // shutup gcc
  (void) d;
  (void) e;
#endif // DEBUG

  return 0;
}


// generic signal handler - this sets a bit in pending_signals, which
// will be handled later by the event loop (ie. if signal 2 is caught,
// bit 2 is set)
static void signalhandler(int sig) {
  sig_atomic_t mask = 1 << sig;
  switch (sig) {
  case SIGBUS: case SIGFPE: case SIGILL: case SIGSEGV:
    if (pending_signals & mask) {
      fprintf(stderr, "%s: Recursive signal %d caught. dumping core...\n",
              base_app ? base_app->applicationName().c_str() : "unknown", sig );
      abort();
    }
  default: break;
  }

  pending_signals |= mask;
}


bt::Application::Application(const std::string &app_name, const char *dpy_name,
                             bool multi_head)
  : _display(dpy_name, multi_head), _app_name(app_name),
    run_state(STARTUP), xserver_time(CurrentTime), menu_grab(false)
{
  assert(base_app == 0);
  ::base_app = this;

  struct sigaction action;
  action.sa_handler = signalhandler;
  action.sa_mask = sigset_t();
  action.sa_flags = SA_NOCLDSTOP;

  // fatal signals
  sigaction(SIGBUS,  &action, NULL);
  sigaction(SIGFPE,  &action, NULL);
  sigaction(SIGILL,  &action, NULL);
  sigaction(SIGSEGV, &action, NULL);

  // non-fatal signals
  sigaction(SIGPIPE, &action, NULL);
  sigaction(SIGTERM, &action, NULL);
  sigaction(SIGINT,  &action, NULL);
  sigaction(SIGCHLD, &action, NULL);
  sigaction(SIGHUP,  &action, NULL);
  sigaction(SIGUSR1, &action, NULL);
  sigaction(SIGUSR2, &action, NULL);

#ifdef    SHAPE
  shape.extensions = XShapeQueryExtension(_display.XDisplay(),
                                          &shape.event_basep,
                                          &shape.error_basep);
#else // !SHAPE
  shape.extensions = False;
#endif // SHAPE

  XSetErrorHandler((XErrorHandler) handleXErrors);

  NumLockMask = ScrollLockMask = 0;

  const XModifierKeymap* const modmap =
    XGetModifierMapping(_display.XDisplay());
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
    const KeyCode num_lock =
      XKeysymToKeycode(_display.XDisplay(), XK_Num_Lock);
    const KeyCode scroll_lock =
      XKeysymToKeycode(_display.XDisplay(), XK_Scroll_Lock);

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
  MaskList[3] = LockMask | NumLockMask;
  MaskList[4] = ScrollLockMask;
  MaskList[5] = ScrollLockMask | LockMask;
  MaskList[6] = ScrollLockMask | NumLockMask;
  MaskList[7] = ScrollLockMask | LockMask | NumLockMask;
  MaskListLength = sizeof(MaskList) / sizeof(MaskList[0]);

  if (modmap) XFreeModifiermap(const_cast<XModifierKeymap*>(modmap));
}


bt::Application::~Application(void) { }


void bt::Application::startup(void) { }
void bt::Application::shutdown(void) { }


void bt::Application::eventLoop(void) {
  startup();

  setRunState(RUNNING);

  const int xfd = ConnectionNumber(_display.XDisplay());

  while (run_state == RUNNING) {
    if (pending_signals) {
      // handle any pending signals
      for (unsigned int sig = 0; sig < sizeof(pending_signals) * 8; ++sig) {
        if (! (pending_signals & (1u << sig))) continue;
        pending_signals &= ~(1u << sig);

        switch (sig) {
        case SIGBUS: case SIGFPE: case SIGILL: case SIGSEGV:
          // dump core after handling these signals
          setRunState(FATAL_SIGNAL);
          break;

        default:
          break;
        }

        if (! process_signal(sig)) {
          // dump core for unhandled signals
          setRunState(FATAL_SIGNAL);
        }

        if (run_state == FATAL_SIGNAL) {
          fprintf(stderr, "%s: caught fatal signal '%d', dumping core.\n",
                  _app_name.c_str(), sig);
          abort();
        }
      }
    }

    if (run_state != RUNNING) break;

    if (XPending(_display.XDisplay())) {
      XEvent e;
      XNextEvent(_display.XDisplay(), &e);
      process_event(&e);
    } else {
      fd_set rfds;
      timeval now, tm, *timeout = (timeval *) 0;

      FD_ZERO(&rfds);
      FD_SET(xfd, &rfds);

      if (! timerList.empty()) {
        const bt::Timer* const timer = timerList.top();

        gettimeofday(&now, 0);
        tm = timer->timeRemaining(now);

        timeout = &tm;
      }

      int ret = select(xfd + 1, &rfds, 0, 0, timeout);
      if (ret < 0) continue; // perhaps a signal interrupted select(2)

      // check for timer timeout
      gettimeofday(&now, 0);

      // there is a small chance for deadlock here:
      // *IF* the timer list keeps getting refreshed *AND* the time between
      // timer->start() and timer->shouldFire() is within the timer's period
      // then the timer will keep firing.  This should be VERY near impossible.
      while (! timerList.empty()) {
        bt::Timer *timer = timerList.top();
        if (! timer->shouldFire(now))
          break;

        timerList.pop();

        timer->fireTimeout();
        timer->halt();
        if (timer->isRecurring())
          timer->start();
      }
    }
  }

  shutdown();
}

void bt::Application::process_event(XEvent *event) {
  EventHandlerMap::iterator it = eventhandlers.find(event->xany.window);
  if (it == eventhandlers.end()) return;

  bt::EventHandler *handler = it->second;

  // deliver the event
  switch (event->type) {
  case ButtonPress:
  case ButtonRelease:
  case MotionNotify: {
    if (menu_grab) {
      // we have active menus.  we should send all user input events
      // to the menus instead of the normal handler
      if (! dynamic_cast<Menu*>(handler)) {
        // current handler is not a menu.  send the event to the most
        // recent menu instead.
        handler = dynamic_cast<EventHandler*>(menus.front());
      }
      XAllowEvents(_display.XDisplay(), SyncPointer, xserver_time);
    }

    switch (event->type) {
    case ButtonPress: {
      xserver_time = event->xbutton.time;
      // strip the lock key modifiers
      event->xbutton.state &= ~(NumLockMask | ScrollLockMask | LockMask);
      handler->buttonPressEvent(&event->xbutton);
      break;
    }

    case ButtonRelease: {
      xserver_time = event->xbutton.time;
      // strip the lock key modifiers
      event->xbutton.state &= ~(NumLockMask | ScrollLockMask | LockMask);
      handler->buttonReleaseEvent(&event->xbutton);
      break;
    }

    case MotionNotify: {
      xserver_time = event->xmotion.time;
      // compress motion notify events
      XEvent realevent;
      unsigned int i = 0;
      while (XCheckTypedWindowEvent(_display.XDisplay(), event->xmotion.window,
                                    MotionNotify, &realevent)) {
        ++i;
      }

      // if we have compressed some motion events, use the last one
      if ( i > 0 )
        event = &realevent;

      // strip the lock key modifiers
      event->xbutton.state &= ~(NumLockMask | ScrollLockMask | LockMask);
      handler->motionNotifyEvent(&event->xmotion);
      break;
    }
    } // switch

    break;
  }

  case EnterNotify:
  case LeaveNotify: {
    if (menu_grab) {
      // we have active menus.  we should only send enter/leave events
      // to the menus themselves, not to normal windows
      if (! dynamic_cast<Menu*>(handler)) {
        break;
      }
    }

    switch (event->type) {
    case EnterNotify: {
      xserver_time = event->xcrossing.time;
      handler->enterNotifyEvent(&event->xcrossing);
      break;
    }

    case LeaveNotify: {
      xserver_time = event->xcrossing.time;
      handler->leaveNotifyEvent(&event->xcrossing);
      break;
    }
    } // switch

    break;
  }

  case KeyPress:
  case KeyRelease: {
    if (menu_grab) {
      // we have active menus.  we should send all key events to the most
      // recent popup menu, regardless of where the pointer is
      handler = dynamic_cast<EventHandler*>(menus.front());
      XAllowEvents(_display.XDisplay(), SyncKeyboard, xserver_time);
    }

    switch (event->type) {
    case KeyPress: {
      xserver_time = event->xkey.time;
      // strip the lock key modifiers, except numlock, which can be useful
      event->xkey.state &= ~(ScrollLockMask | LockMask);
      handler->keyPressEvent(&event->xkey);
      break;
    }

    case KeyRelease: {
      xserver_time = event->xkey.time;
      // strip the lock key modifiers, except numlock, which can be useful
      event->xkey.state &= ~(ScrollLockMask | LockMask);
      handler->keyReleaseEvent(&event->xkey);
      break;
    }
    } // switch

    break;
  }

  case MapNotify: {
    handler->mapNotifyEvent(&event->xmap);
    break;
  }

  case UnmapNotify: {
    handler->unmapNotifyEvent(&event->xunmap);
    break;
  }

  case ReparentNotify: {
    handler->reparentNotifyEvent(&event->xreparent);
    break;
  }

  case DestroyNotify: {
    handler->destroyNotifyEvent(&event->xdestroywindow);
    break;
  }

  case PropertyNotify: {
    xserver_time = event->xproperty.time;
    handler->propertyNotifyEvent(&event->xproperty);
    break;
  }

  case ConfigureRequest: {
    handler->configureRequestEvent(&event->xconfigurerequest);
    break;
  }

  case Expose: {
    // compress expose events
    XEvent realevent;
    unsigned int i = 0;
    int ex1, ey1, ex2, ey2;
    ex1 = event->xexpose.x;
    ey1 = event->xexpose.y;
    ex2 = ex1 + event->xexpose.width - 1;
    ey2 = ey1 + event->xexpose.height - 1;
    while (XCheckTypedWindowEvent(_display.XDisplay(), event->xexpose.window,
                                  Expose, &realevent)) {
      ++i;

      // merge expose area
      ex1 = std::min(realevent.xexpose.x, ex1);
      ey1 = std::min(realevent.xexpose.y, ey1);
      ex2 = std::max(realevent.xexpose.x + realevent.xexpose.width - 1, ex2);
      ey2 = std::max(realevent.xexpose.y + realevent.xexpose.height - 1, ey2);
    }
    if ( i > 0 )
      event = &realevent;

    // use the merged area
    event->xexpose.x = ex1;
    event->xexpose.y = ey1;
    event->xexpose.width = ex2 - ex1 + 1;
    event->xexpose.height = ey2 - ey1 + 1;

    handler->exposeEvent(&event->xexpose);
    break;
  }

  case ConfigureNotify: {
    // compress configure notify events
    XEvent realevent;
    unsigned int i = 0;
    while (XCheckTypedWindowEvent(_display.XDisplay(),
                                  event->xconfigure.window,
                                  ConfigureNotify, &realevent)) {
      ++i;
    }

    // if we have compressed some configure notify events, use the last one
    if ( i > 0 )
      event = &realevent;

    handler->configureNotifyEvent(&event->xconfigure);
    break;
  }

  case ClientMessage: {
    handler->clientMessageEvent(&event->xclient);
    break;
  }

  case NoExpose: {
    // not handled, ignore
    break;
  }

  default: {
#ifdef SHAPE
    if (event->type == shape.event_basep) {
      handler->shapeEvent((XShapeEvent *) event);
    } else
#endif // SHAPE
      {
#ifdef    DEBUG
        fprintf(stderr, "unhandled event %d\n", event->type);
#endif // DEBUG
      }
    break;
  }
  } // switch
}


void bt::Application::addTimer(bt::Timer *timer) {
  if (! timer) return;
  timerList.push(timer);
}


void bt::Application::removeTimer(bt::Timer *timer) {
  timerList.release(timer);
}


/*
 * Grabs a button, but also grabs the button in every possible combination
 * with the keyboard lock keys, so that they do not cancel out the event.

 * if allow_scroll_lock is true then only the top half of the lock mask
 * table is used and scroll lock is ignored.  This value defaults to false.
 */
void bt::Application::grabButton(unsigned int button, unsigned int modifiers,
                             Window grab_window, bool owner_events,
                             unsigned int event_mask, int pointer_mode,
                             int keyboard_mode, Window confine_to,
                             Cursor cursor, bool allow_scroll_lock) const {
  unsigned int length = (allow_scroll_lock) ? MaskListLength / 2:
                                              MaskListLength;
  for (size_t cnt = 0; cnt < length; ++cnt) {
    XGrabButton(_display.XDisplay(), button, modifiers | MaskList[cnt],
                grab_window, owner_events, event_mask, pointer_mode,
                keyboard_mode, confine_to, cursor);
  }
}


/*
 * Releases the grab on a button, and ungrabs all possible combinations of the
 * keyboard lock keys.
 */
void bt::Application::ungrabButton(unsigned int button, unsigned int modifiers,
                               Window grab_window) const {
  for (size_t cnt = 0; cnt < MaskListLength; ++cnt) {
    XUngrabButton(_display.XDisplay(), button, modifiers | MaskList[cnt],
                  grab_window);
  }
}


bool bt::Application::process_signal(int signal) {
  switch (signal) {
  case SIGCHLD:
    int unused;
    (void) waitpid(-1, &unused, WNOHANG | WUNTRACED);
    break;

  case SIGINT:
  case SIGTERM:
    setRunState(SHUTDOWN);
    break;

  default:
    // generate a core dump for unknown signals
    return false;
  }

  return true;
}


void bt::Application::insertEventHandler(Window window,
                                         bt::EventHandler *handler) {
  eventhandlers.insert(std::pair<Window,bt::EventHandler*>(window, handler));
}


void bt::Application::removeEventHandler(Window window) {
  eventhandlers.erase(window);
}


void bt::Application::openMenu(Menu *menu) {
  menus.push_front(menu);

  if (! menu_grab && // grab mouse and keyboard for the menu
      XGrabKeyboard(_display.XDisplay(), menu->windowID(), True, GrabModeSync,
                    GrabModeAsync, xserver_time) == GrabSuccess &&
      XGrabPointer(_display.XDisplay(), menu->windowID(), True,
                   (ButtonPressMask | ButtonReleaseMask | ButtonMotionMask |
                    PointerMotionMask | LeaveWindowMask),
                   GrabModeSync, GrabModeAsync, None, None,
                   xserver_time) == GrabSuccess) {
    XAllowEvents(_display.XDisplay(), SyncPointer, xserver_time);
  }
  menu_grab = true;
}


void bt::Application::closeMenu(Menu *menu) {
  if (menus.empty() || menu != menus.front()) {
    fprintf(stderr, "BaseDisplay::closeMenu: menu %p not valid.\n",
            (void *) menu);
    return;
  }

  menus.pop_front();
  if (! menus.empty())
    return;

  XAllowEvents(_display.XDisplay(), ReplayPointer, xserver_time);

  XUngrabKeyboard(_display.XDisplay(), xserver_time);
  XUngrabPointer(_display.XDisplay(), xserver_time);

  XSync(_display.XDisplay(), False);
  menu_grab = false;
}