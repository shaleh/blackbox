// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Window.cc for Blackbox - an X11 Window manager
// Copyright (c) 2001 - 2004 Sean 'Shaleh' Perry <shaleh@debian.org>
// Copyright (c) 1997 - 2000, 2002 - 2004
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

// make sure we get bt::textPropertyToString()
#include <X11/Xutil.h>

#include "Window.hh"
#include "Screen.hh"
#include "WindowGroup.hh"
#include "Windowmenu.hh"
#include "Workspace.hh"
#include "blackbox.hh"

#include <Pen.hh>
#include <PixmapCache.hh>

#include <X11/Xatom.h>
#ifdef SHAPE
#  include <X11/extensions/shape.h>
#endif

#include <assert.h>


#if 0
static
void watch_decorations(const char *msg, WindowDecorationFlags flags) {
  fprintf(stderr, "Decorations: %s\n", msg);
  fprintf(stderr, "title   : %d\n",
          (flags & WindowDecorationTitlebar) != 0);
  fprintf(stderr, "handle  : %d\n",
          (flags & WindowDecorationHandle) != 0);
  fprintf(stderr, "grips   : %d\n",
          (flags & WindowDecorationGrip) != 0);
  fprintf(stderr, "border  : %d\n",
          (flags & WindowDecorationBorder) != 0);
  fprintf(stderr, "iconify : %d\n",
          (flags & WindowDecorationIconify) != 0);
  fprintf(stderr, "maximize: %d\n",
          (flags & WindowDecorationMaximize) != 0);
  fprintf(stderr, "close   : %d\n",
          (flags & WindowDecorationClose) != 0);
}
#endif


/*
 * Returns the appropriate WindowType based on the _NET_WM_WINDOW_TYPE
 */
static WindowType window_type_from_atom(const bt::Netwm &netwm, Atom atom) {
  if (atom == netwm.wmWindowTypeDialog())
    return WindowTypeDialog;
  if (atom == netwm.wmWindowTypeDesktop())
    return WindowTypeDesktop;
  if (atom == netwm.wmWindowTypeDock())
    return WindowTypeDock;
  if (atom == netwm.wmWindowTypeMenu())
    return WindowTypeMenu;
  if (atom == netwm.wmWindowTypeSplash())
    return WindowTypeSplash;
  if (atom == netwm.wmWindowTypeToolbar())
    return WindowTypeToolbar;
  if (atom == netwm.wmWindowTypeUtility())
    return WindowTypeUtility;
  return WindowTypeNormal;
}


/*
 * Determine the appropriate decorations and functions for the
 * specified window type.
 */
static void get_decorations(WindowType window_type,
                            WindowDecorationFlags &decorations,
                            WindowFunctionFlags &functions) {
  decorations = AllWindowDecorations;
  functions = AllWindowFunctions;

  // modify the window decorations/functions based on window type
  switch (window_type) {
  case WindowTypeDialog:
    decorations &= ~(WindowDecorationIconify | WindowDecorationMaximize);
    functions &= ~(WindowFunctionShade | WindowFunctionIconify |
                   WindowFunctionMaximize);
    break;

  case WindowTypeDesktop:
  case WindowTypeDock:
  case WindowTypeSplash:
    decorations = functions = 0;
    break;

  case WindowTypeUtility:
    decorations &= ~(WindowDecorationMaximize | WindowDecorationIconify);
    functions   &= ~(WindowFunctionMaximize | WindowFunctionIconify);
    break;

  default:break;
  }
}


/*
 * Initializes the class with default values/the window's set initial values.
 */
BlackboxWindow::BlackboxWindow(Blackbox *b, Window w, BScreen *s) {
  // fprintf(stderr, "BlackboxWindow size: %d bytes\n",
  //         sizeof(BlackboxWindow));

#ifdef    DEBUG
  fprintf(stderr, "BlackboxWindow::BlackboxWindow(): creating 0x%lx\n", w);
#endif // DEBUG

  /*
    set timer to zero... it is initialized properly later, so we check
    if timer is zero in the destructor, and assume that the window is not
    fully constructed if timer is zero...
  */
  timer = (bt::Timer*) 0;
  blackbox = b;
  client.window = w;
  screen = s;
  lastButtonPressTime = 0;

  if (! validateClient()) {
    delete this;
    return;
  }

  // fetch client size and placement
  XWindowAttributes wattrib;
  if (! XGetWindowAttributes(blackbox->XDisplay(),
                             client.window, &wattrib) ||
      ! wattrib.screen || wattrib.override_redirect) {
#ifdef    DEBUG
    fprintf(stderr,
            "BlackboxWindow::BlackboxWindow(): XGetWindowAttributes failed\n");
#endif // DEBUG

    delete this;
    return;
  }

  // set the eventmask early in the game so that we make sure we get
  // all the events we are interested in
  XSetWindowAttributes attrib_set;
  attrib_set.event_mask = PropertyChangeMask | FocusChangeMask |
                          StructureNotifyMask;
  attrib_set.do_not_propagate_mask = ButtonPressMask | ButtonReleaseMask |
                                     ButtonMotionMask;
  XChangeWindowAttributes(blackbox->XDisplay(), client.window,
                          CWEventMask|CWDontPropagate, &attrib_set);

  client.colormap = wattrib.colormap;
  client.workspace = screen->currentWorkspace();
  window_number = bt::BSENTINEL;
  client.normal_hint_flags = 0;
  client.window_group = None;
  client.transient_for = 0;
  client.window_type = WindowTypeNormal;
  client.strut = 0;
  /*
    set the initial size and location of client window (relative to the
    _root window_). This position is the reference point used with the
    window's gravity to find the window's initial position.
  */
  client.rect.setRect(wattrib.x, wattrib.y, wattrib.width, wattrib.height);
  client.old_bw = wattrib.border_width;
  client.current_state = NormalState;

  frame.border_w = 1;
  frame.window = frame.plate = frame.title = frame.handle = None;
  frame.close_button = frame.iconify_button = frame.maximize_button = None;
  frame.right_grip = frame.left_grip = None;
  frame.uborder_pixel = frame.fborder_pixel = 0;
  frame.utitle = frame.ftitle = frame.uhandle = frame.fhandle = None;
  frame.ulabel = frame.flabel = frame.ubutton = frame.fbutton = None;
  frame.pbutton = frame.ugrip = frame.fgrip = None;
  frame.style = screen->resource().windowStyle();

  timer = new bt::Timer(blackbox, this);
  timer->setTimeout(blackbox->resource().autoRaiseDelay());

  client.title = readWMName();
  client.icon_title = readWMIconName();

  // get size, aspect, minimum/maximum size, ewmh and other hints set by the
  // client
  getNetwmHints();
  getWMProtocols();
  getWMHints();
  getWMNormalHints();
  getTransientInfo();
  if (client.window_type == WindowTypeNormal && isTransient())
    client.window_type = WindowTypeDialog;

  ::get_decorations(client.window_type, client.decorations, client.functions);
  getMWMHints();


  if ((client.normal_hint_flags & PMinSize) &&
      (client.normal_hint_flags & PMaxSize) &&
      client.max_width <= client.min_width &&
      client.max_height <= client.min_height) {
    client.decorations &= ~(WindowDecorationMaximize | WindowDecorationGrip);
    client.functions &= ~(WindowFunctionResize | WindowFunctionMaximize);
  }

  frame.window = createToplevelWindow();
  blackbox->insertEventHandler(frame.window, this);

  frame.plate = createChildWindow(frame.window, NoEventMask);
  blackbox->insertEventHandler(frame.plate, this);

  if (client.decorations & WindowDecorationTitlebar)
    createTitlebar();

  if (client.decorations & WindowDecorationHandle)
    createHandle();

  // apply the size and gravity hint to the frame
  upsize();
  applyGravity(frame.rect);

  /*
    the server needs to be grabbed here to prevent client's from sending
    events while we are in the process of configuring their window.
    We hold the grab until after we are done moving the window around.
  */

  XGrabServer(blackbox->XDisplay());

  associateClientWindow();

  blackbox->insertEventHandler(client.window, this);
  blackbox->insertWindow(client.window, this);
  blackbox->insertWindow(frame.plate, this);

  // preserve the window's initial state on first map, and its current state
  // across a restart
  unsigned long initial_state = client.current_state;
  if (! getState())
    client.current_state = initial_state;

  if (client.state.iconic) {
    // prepare the window to be iconified
    client.current_state = IconicState;
    client.state.iconic = False;
  } else if (client.workspace != bt::BSENTINEL &&
             client.workspace != screen->currentWorkspace()) {
    client.current_state = WithdrawnState;
  }

  configure(frame.rect);

  positionWindows();

  XUngrabServer(blackbox->XDisplay());

#ifdef SHAPE
  if (blackbox->hasShapeExtensions() && client.state.shaped)
    configureShape();
#endif // SHAPE

  // now that we know where to put the window and what it should look like
  // we apply the decorations
  decorate();

  if (client.decorations & WindowDecorationBorder)
    XSetWindowBorder(blackbox->XDisplay(), frame.plate, frame.uborder_pixel);

  grabButtons();

  XMapSubwindows(blackbox->XDisplay(), frame.window);

  client.premax = frame.rect;

  if (client.state.shaded) {
    client.state.shaded = false;
    initial_state = client.current_state;
    setShaded(true);

    /*
      At this point in the life of a window, current_state should only be set
      to IconicState if the window was an *icon*, not if it was shaded.
    */
    if (initial_state != IconicState)
      client.current_state = initial_state;
  }

  if (!(client.functions & WindowFunctionMaximize))
    client.state.maximized = 0;

  if (client.state.fullscreen) {
    client.state.fullscreen = false; // trick setFullScreen into working
    setFullScreen(true);
  } else if (client.state.maximized) {
    remaximize();
  }
}


BlackboxWindow::~BlackboxWindow(void) {
#ifdef    DEBUG
  fprintf(stderr, "BlackboxWindow::~BlackboxWindow: destroying 0x%lx\n",
          client.window);
#endif // DEBUG

  if (! timer) // window not managed...
    return;

  if (client.state.moving || client.state.resizing) {
    screen->hideGeometry();
    XUngrabPointer(blackbox->XDisplay(), CurrentTime);
  }

  delete timer;

  if (client.strut) {
    screen->removeStrut(client.strut);
    delete client.strut;
  }

  if (client.window_group) {
    BWindowGroup *group = blackbox->findWindowGroup(client.window_group);
    if (group) group->removeWindow(this);
  }

  // remove ourselves from our transient_for
  if (isTransient()) {
    if (client.transient_for != (BlackboxWindow *) ~0ul)
      client.transient_for->client.transientList.remove(this);

    client.transient_for = (BlackboxWindow*) 0;
  }

  if (! client.transientList.empty()) {
    // reset transient_for for all transients
    BlackboxWindowList::iterator it, end = client.transientList.end();
    for (it = client.transientList.begin(); it != end; ++it)
      (*it)->client.transient_for = (BlackboxWindow*) 0;
  }

  if (frame.title)
    destroyTitlebar();

  if (frame.handle)
    destroyHandle();

  if (frame.plate) {
    blackbox->removeEventHandler(frame.plate);
    XDestroyWindow(blackbox->XDisplay(), frame.plate);
  }

  if (frame.window) {
    blackbox->removeEventHandler(frame.window);
    XDestroyWindow(blackbox->XDisplay(), frame.window);
  }

  blackbox->removeEventHandler(client.window);
  blackbox->removeWindow(client.window);
}


/*
 * Creates a new top level window, with a given location, size, and border
 * width.
 * Returns: the newly created window
 */
Window BlackboxWindow::createToplevelWindow(void) {
  XSetWindowAttributes attrib_create;
  unsigned long create_mask = CWColormap | CWOverrideRedirect | CWEventMask;

  attrib_create.colormap = screen->screenInfo().colormap();
  attrib_create.override_redirect = True;
  attrib_create.event_mask = EnterWindowMask | LeaveWindowMask;

  return XCreateWindow(blackbox->XDisplay(), screen->screenInfo().rootWindow(),
                       0, 0, 1, 1, frame.border_w,
                       screen->screenInfo().depth(), InputOutput,
                       screen->screenInfo().visual(),
                       create_mask, &attrib_create);
}


/*
 * Creates a child window, and optionally associates a given cursor with
 * the new window.
 */
Window BlackboxWindow::createChildWindow(Window parent,
                                         unsigned long event_mask,
                                         Cursor cursor) {
  XSetWindowAttributes attrib_create;
  unsigned long create_mask = CWEventMask;

  attrib_create.event_mask = event_mask;

  if (cursor) {
    create_mask |= CWCursor;
    attrib_create.cursor = cursor;
  }

  return XCreateWindow(blackbox->XDisplay(), parent, 0, 0, 1, 1, 0,
                       screen->screenInfo().depth(), InputOutput,
                       screen->screenInfo().visual(),
                       create_mask, &attrib_create);
}


/*
 * Reparents the client window into the newly created frame.
 *
 * Note: the server must be grabbed before calling this function.
 */
void BlackboxWindow::associateClientWindow(void) {
  XSetWindowBorderWidth(blackbox->XDisplay(), client.window, 0);
  XChangeSaveSet(blackbox->XDisplay(), client.window, SetModeInsert);

  XSelectInput(blackbox->XDisplay(), frame.plate,
               FocusChangeMask | SubstructureRedirectMask);

  unsigned long event_mask = PropertyChangeMask | FocusChangeMask |
                             StructureNotifyMask;
  XSelectInput(blackbox->XDisplay(), client.window,
               event_mask & ~StructureNotifyMask);
  XReparentWindow(blackbox->XDisplay(), client.window, frame.plate, 0, 0);
  XSelectInput(blackbox->XDisplay(), client.window, event_mask);

  XRaiseWindow(blackbox->XDisplay(), frame.plate);
  XMapSubwindows(blackbox->XDisplay(), frame.plate);

#ifdef    SHAPE
  if (blackbox->hasShapeExtensions()) {
    XShapeSelectInput(blackbox->XDisplay(), client.window,
                      ShapeNotifyMask);

    Bool shaped = False;
    int foo;
    unsigned int ufoo;

    XShapeQueryExtents(blackbox->XDisplay(), client.window, &shaped,
                       &foo, &foo, &ufoo, &ufoo, &foo, &foo, &foo,
                       &ufoo, &ufoo);
    client.state.shaped = shaped;
  }
#endif // SHAPE
}


void BlackboxWindow::decorate(void) {
  if (client.decorations & WindowDecorationTitlebar) {
    // render focused button texture
    frame.fbutton =
      bt::PixmapCache::find(screen->screenNumber(), frame.style->b_focus,
                            frame.style->button_width,
                            frame.style->button_width, frame.fbutton);

    // render unfocused button texture
    frame.ubutton =
      bt::PixmapCache::find(screen->screenNumber(), frame.style->b_unfocus,
                            frame.style->button_width,
                            frame.style->button_width, frame.ubutton);

    // render pressed button texture
    frame.pbutton =
      bt::PixmapCache::find(screen->screenNumber(), frame.style->b_pressed,
                            frame.style->button_width,
                            frame.style->button_width, frame.pbutton);

    // render focused titlebar texture
    frame.ftitle =
      bt::PixmapCache::find(screen->screenNumber(), frame.style->t_focus,
                            frame.inside_w, frame.style->title_height,
                            frame.ftitle);

    // render unfocused titlebar texture
    frame.utitle =
      bt::PixmapCache::find(screen->screenNumber(), frame.style->t_unfocus,
                            frame.inside_w, frame.style->title_height,
                            frame.utitle);

    // render focused label texture
    frame.flabel =
      bt::PixmapCache::find(screen->screenNumber(),
                            frame.style->l_focus,
                            frame.label_w, frame.style->label_height,
                            frame.flabel);

    // render unfocused label texture
    frame.ulabel =
      bt::PixmapCache::find(screen->screenNumber(),
                            frame.style->l_unfocus,
                            frame.label_w, frame.style->label_height,
                            frame.ulabel);

    XSetWindowBorder(blackbox->XDisplay(), frame.title,
                     screen->resource().borderColor()->
                     pixel(screen->screenNumber()));
  }

  if (client.decorations & WindowDecorationBorder) {
    frame.fborder_pixel =
      frame.style->f_focus.color().pixel(screen->screenNumber());
    frame.uborder_pixel =
      frame.style->f_unfocus.color().pixel(screen->screenNumber());
  }

  if (client.decorations & WindowDecorationHandle) {
    frame.fhandle =
      bt::PixmapCache::find(screen->screenNumber(), frame.style->h_focus,
                            frame.inside_w, frame.style->handle_height,
                            frame.fhandle);

    frame.uhandle =
      bt::PixmapCache::find(screen->screenNumber(), frame.style->h_unfocus,
                            frame.inside_w, frame.style->handle_height,
                            frame.uhandle);

    XSetWindowBorder(blackbox->XDisplay(), frame.handle,
                     screen->resource().borderColor()->
                     pixel(screen->screenNumber()));
  }

  if (client.decorations & WindowDecorationGrip) {
    frame.fgrip =
      bt::PixmapCache::find(screen->screenNumber(), frame.style->g_focus,
                            frame.style->grip_width,
                            frame.style->handle_height, frame.fgrip);

    frame.ugrip =
      bt::PixmapCache::find(screen->screenNumber(), frame.style->g_unfocus,
                            frame.style->grip_width,
                            frame.style->handle_height, frame.ugrip);

    XSetWindowBorder(blackbox->XDisplay(), frame.left_grip,
                     screen->resource().borderColor()->
                     pixel(screen->screenNumber()));
    XSetWindowBorder(blackbox->XDisplay(), frame.right_grip,
                     screen->resource().borderColor()->
                     pixel(screen->screenNumber()));
  }

  XSetWindowBorder(blackbox->XDisplay(), frame.window,
                   screen->resource().borderColor()->
                   pixel(screen->screenNumber()));
}


void BlackboxWindow::createHandle(void) {
  frame.handle = createChildWindow(frame.window,
                                   ButtonPressMask | ButtonReleaseMask |
                                   ButtonMotionMask | ExposureMask);
  blackbox->insertEventHandler(frame.handle, this);

  if (client.decorations & WindowDecorationGrip)
    createGrips();
}


void BlackboxWindow::destroyHandle(void) {
  if (frame.left_grip || frame.right_grip)
    destroyGrips();

  if (frame.fhandle) bt::PixmapCache::release(frame.fhandle);
  if (frame.uhandle) bt::PixmapCache::release(frame.uhandle);

  frame.fhandle = frame.uhandle = None;

  blackbox->removeEventHandler(frame.handle);
  XDestroyWindow(blackbox->XDisplay(), frame.handle);
  frame.handle = None;
}


void BlackboxWindow::createGrips(void) {
  frame.left_grip =
    createChildWindow(frame.handle,
                      ButtonPressMask | ButtonReleaseMask |
                      ButtonMotionMask | ExposureMask,
                      blackbox->resource().resizeBottomLeftCursor());
  blackbox->insertEventHandler(frame.left_grip, this);

  frame.right_grip =
    createChildWindow(frame.handle,
                      ButtonPressMask | ButtonReleaseMask |
                      ButtonMotionMask | ExposureMask,
                      blackbox->resource().resizeBottomRightCursor());
  blackbox->insertEventHandler(frame.right_grip, this);
}


void BlackboxWindow::destroyGrips(void) {
  if (frame.fgrip) bt::PixmapCache::release(frame.fgrip);
  if (frame.ugrip) bt::PixmapCache::release(frame.ugrip);

  frame.fgrip = frame.ugrip = None;

  blackbox->removeEventHandler(frame.left_grip);
  blackbox->removeEventHandler(frame.right_grip);

  XDestroyWindow(blackbox->XDisplay(), frame.left_grip);
  XDestroyWindow(blackbox->XDisplay(), frame.right_grip);
  frame.left_grip = frame.right_grip = None;
}


void BlackboxWindow::createTitlebar(void) {
  frame.title = createChildWindow(frame.window,
                                  ButtonPressMask | ButtonReleaseMask |
                                  ButtonMotionMask | ExposureMask);
  frame.label = createChildWindow(frame.title,
                                  ButtonPressMask | ButtonReleaseMask |
                                  ButtonMotionMask | ExposureMask);
  blackbox->insertEventHandler(frame.title, this);
  blackbox->insertEventHandler(frame.label, this);

  if (client.decorations & WindowDecorationIconify) createIconifyButton();
  if (client.decorations & WindowDecorationMaximize) createMaximizeButton();
  if (client.decorations & WindowDecorationClose) createCloseButton();
}


void BlackboxWindow::destroyTitlebar(void) {
  if (frame.close_button)
    destroyCloseButton();

  if (frame.iconify_button)
    destroyIconifyButton();

  if (frame.maximize_button)
    destroyMaximizeButton();

  if (frame.fbutton) bt::PixmapCache::release(frame.fbutton);
  if (frame.ubutton) bt::PixmapCache::release(frame.ubutton);
  if (frame.pbutton) bt::PixmapCache::release(frame.pbutton);
  if (frame.ftitle)  bt::PixmapCache::release(frame.ftitle);
  if (frame.utitle)  bt::PixmapCache::release(frame.utitle);
  if (frame.flabel)  bt::PixmapCache::release(frame.flabel);
  if (frame.ulabel)  bt::PixmapCache::release(frame.ulabel);

  frame.fbutton = frame.ubutton = frame.pbutton =
   frame.ftitle = frame.utitle =
   frame.flabel = frame.ulabel = None;

  blackbox->removeEventHandler(frame.title);
  blackbox->removeEventHandler(frame.label);

  XDestroyWindow(blackbox->XDisplay(), frame.label);
  XDestroyWindow(blackbox->XDisplay(), frame.title);
  frame.title = frame.label = None;
}


void BlackboxWindow::createCloseButton(void) {
  if (frame.title != None) {
    frame.close_button = createChildWindow(frame.title,
                                           ButtonPressMask |
                                           ButtonReleaseMask |
                                           ButtonMotionMask | ExposureMask);
    blackbox->insertEventHandler(frame.close_button, this);
  }
}


void BlackboxWindow::destroyCloseButton(void) {
  blackbox->removeEventHandler(frame.close_button);
  XDestroyWindow(blackbox->XDisplay(), frame.close_button);
  frame.close_button = None;
}


void BlackboxWindow::createIconifyButton(void) {
  if (frame.title != None) {
    frame.iconify_button = createChildWindow(frame.title,
                                             ButtonPressMask |
                                             ButtonReleaseMask |
                                             ButtonMotionMask | ExposureMask);
    blackbox->insertEventHandler(frame.iconify_button, this);
  }
}


void BlackboxWindow::destroyIconifyButton(void) {
  blackbox->removeEventHandler(frame.iconify_button);
  XDestroyWindow(blackbox->XDisplay(), frame.iconify_button);
  frame.iconify_button = None;
}


void BlackboxWindow::createMaximizeButton(void) {
  if (frame.title != None) {
    frame.maximize_button = createChildWindow(frame.title,
                                              ButtonPressMask |
                                              ButtonReleaseMask |
                                              ButtonMotionMask | ExposureMask);
    blackbox->insertEventHandler(frame.maximize_button, this);
  }
}


void BlackboxWindow::destroyMaximizeButton(void) {
  blackbox->removeEventHandler(frame.maximize_button);
  XDestroyWindow(blackbox->XDisplay(), frame.maximize_button);
  frame.maximize_button = None;
}


void BlackboxWindow::positionButtons(bool redecorate_label) {
  // we need to use signed ints here to detect windows that are too small
  const int
    bw = frame.style->button_width + frame.style->bevel_width + 1,
    by = frame.style->bevel_width + 1;
  int lx = by, lw = frame.inside_w - by;

  if (client.decorations & WindowDecorationIconify) {
    if (frame.iconify_button == None) createIconifyButton();

    XMoveResizeWindow(blackbox->XDisplay(), frame.iconify_button, by, by,
                      frame.style->button_width, frame.style->button_width);
    XMapWindow(blackbox->XDisplay(), frame.iconify_button);

    lx += bw;
    lw -= bw;
  } else if (frame.iconify_button) {
    destroyIconifyButton();
  }

  int bx = frame.inside_w - bw;

  if (client.decorations & WindowDecorationClose) {
    if (frame.close_button == None) createCloseButton();

    XMoveResizeWindow(blackbox->XDisplay(), frame.close_button, bx, by,
                      frame.style->button_width, frame.style->button_width);
    XMapWindow(blackbox->XDisplay(), frame.close_button);

    bx -= bw;
    lw -= bw;
  } else if (frame.close_button) {
    destroyCloseButton();
  }

  if (client.decorations & WindowDecorationMaximize) {
    if (frame.maximize_button == None) createMaximizeButton();

    XMoveResizeWindow(blackbox->XDisplay(), frame.maximize_button, bx, by,
                      frame.style->button_width, frame.style->button_width);
    XMapWindow(blackbox->XDisplay(), frame.maximize_button);

    bx -= bw;
    lw -= bw;
  } else if (frame.maximize_button) {
    destroyMaximizeButton();
  }

  if (lw > by) {
    frame.label_w = lw - by;
    XMoveResizeWindow(blackbox->XDisplay(), frame.label, lx,
                      frame.style->bevel_width,
                      frame.label_w, frame.style->label_height);
    XMapWindow(blackbox->XDisplay(), frame.label);

    if (redecorate_label) {
      frame.flabel =
        bt::PixmapCache::find(screen->screenNumber(),
                              frame.style->l_focus,
                              frame.label_w, frame.style->label_height,
                              frame.flabel);
      frame.ulabel =
        bt::PixmapCache::find(screen->screenNumber(),
                              frame.style->l_unfocus,
                              frame.label_w, frame.style->label_height,
                              frame.ulabel);
    }

    const std::string ellided =
      bt::ellideText(client.title, frame.label_w, "...",
                     screen->screenNumber(), frame.style->font);

    if (ellided != client.visible_title) {
      client.visible_title = ellided;
      blackbox->netwm().setWMVisibleName(client.window, client.visible_title);
    }
  } else {
    XUnmapWindow(blackbox->XDisplay(), frame.label);
  }

  redrawLabel();
  redrawAllButtons();
}


void BlackboxWindow::reconfigure(void) {
  restoreGravity(client.rect);
  upsize();
  applyGravity(frame.rect);
  positionWindows();
  decorate();
  redrawWindowFrame();

  ungrabButtons();
  grabButtons();
}


void BlackboxWindow::grabButtons(void) {
  if (! screen->resource().isSloppyFocus() ||
      screen->resource().doClickRaise())
    // grab button 1 for changing focus/raising
    blackbox->grabButton(Button1, 0, frame.plate, True, ButtonPressMask,
                         GrabModeSync, GrabModeSync, frame.plate, None,
                         screen->resource().allowScrollLock());

  if (client.functions & WindowFunctionMove)
    blackbox->grabButton(Button1, Mod1Mask, frame.window, True,
                         ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
                         GrabModeAsync, frame.window,
                         blackbox->resource().moveCursor(),
                         screen->resource().allowScrollLock());
  if (client.functions & WindowFunctionResize)
    blackbox->grabButton(Button3, Mod1Mask, frame.window, True,
                         ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
                         GrabModeAsync, frame.window,
                         blackbox->resource().resizeBottomRightCursor(),
                         screen->resource().allowScrollLock());
  // alt+middle lowers the window
  blackbox->grabButton(Button2, Mod1Mask, frame.window, True,
                       ButtonReleaseMask, GrabModeAsync, GrabModeAsync,
                       frame.window, None,
                       screen->resource().allowScrollLock());
}


void BlackboxWindow::ungrabButtons(void) {
  blackbox->ungrabButton(Button1, 0, frame.plate);
  blackbox->ungrabButton(Button1, Mod1Mask, frame.window);
  blackbox->ungrabButton(Button2, Mod1Mask, frame.window);
  blackbox->ungrabButton(Button3, Mod1Mask, frame.window);
}


void BlackboxWindow::positionWindows(void) {
  XMoveResizeWindow(blackbox->XDisplay(), frame.window,
                    frame.rect.x(), frame.rect.y(), frame.inside_w,
                    (client.state.shaded) ? frame.style->title_height :
                                            frame.inside_h);
  XSetWindowBorderWidth(blackbox->XDisplay(), frame.window, frame.border_w);
  XSetWindowBorderWidth(blackbox->XDisplay(), frame.plate, frame.mwm_border_w);
  XMoveResizeWindow(blackbox->XDisplay(), frame.plate,
                    frame.margin.left - frame.mwm_border_w - frame.border_w,
                    frame.margin.top - frame.mwm_border_w - frame.border_w,
                    client.rect.width(), client.rect.height());
  XMoveResizeWindow(blackbox->XDisplay(), client.window,
                    0, 0, client.rect.width(), client.rect.height());
  // ensure client.rect contains the real location
  client.rect.setPos(frame.rect.left() + frame.margin.left,
                     frame.rect.top() + frame.margin.top);

  if (client.decorations & WindowDecorationTitlebar) {
    if (frame.title == None) createTitlebar();

    XSetWindowBorderWidth(blackbox->XDisplay(), frame.title, frame.border_w);
    XMoveResizeWindow(blackbox->XDisplay(), frame.title,
                      -frame.border_w, -frame.border_w,
                      frame.inside_w, frame.style->title_height);

    positionButtons();
    XMapSubwindows(blackbox->XDisplay(), frame.title);
    XMapWindow(blackbox->XDisplay(), frame.title);
  } else if (frame.title) {
    destroyTitlebar();
  }

  if (client.decorations & WindowDecorationHandle) {
    if (frame.handle == None) createHandle();
    XSetWindowBorderWidth(blackbox->XDisplay(), frame.handle, frame.border_w);

    // use client.rect here so the value is correct even if shaded
    const int ny = client.rect.height() + frame.margin.top +
                   frame.mwm_border_w - frame.border_w;
    XMoveResizeWindow(blackbox->XDisplay(), frame.handle,
                      -frame.border_w, ny,
                      frame.inside_w, frame.style->handle_height);

    if (client.decorations & WindowDecorationGrip) {
      if (frame.left_grip == None || frame.right_grip == None) createGrips();

      XSetWindowBorderWidth(blackbox->XDisplay(), frame.left_grip,
                            frame.border_w);
      XSetWindowBorderWidth(blackbox->XDisplay(), frame.right_grip,
                            frame.border_w);

      XMoveResizeWindow(blackbox->XDisplay(), frame.left_grip,
                        -frame.border_w, -frame.border_w,
                        frame.style->grip_width, frame.style->handle_height);
      const int nx = frame.inside_w - frame.style->grip_width - frame.border_w;
      XMoveResizeWindow(blackbox->XDisplay(), frame.right_grip,
                        nx, -frame.border_w,
                        frame.style->grip_width, frame.style->handle_height);

      XMapSubwindows(blackbox->XDisplay(), frame.handle);
    } else {
      destroyGrips();
    }

    XMapWindow(blackbox->XDisplay(), frame.handle);
  } else if (frame.handle) {
    destroyHandle();
  }
}


std::string BlackboxWindow::readWMName(void) {
  std::string name;

  if (!blackbox->netwm().readWMName(client.window, name) || name.empty()) {
    XTextProperty text_prop;
    if (XGetWMName(blackbox->XDisplay(), client.window, &text_prop)) {
      name = bt::textPropertyToString(blackbox->XDisplay(), text_prop);
      XFree((char *) text_prop.value);
    }
  }

  if (name.empty()) name ="Unnamed";
  return name;
}


std::string BlackboxWindow::readWMIconName(void) {
  std::string name;

  if (!blackbox->netwm().readWMIconName(client.window, name) || name.empty()) {
    XTextProperty text_prop;
    if (XGetWMIconName(blackbox->XDisplay(), client.window, &text_prop)) {
      name = bt::textPropertyToString(blackbox->XDisplay(), text_prop);
      XFree((char *) text_prop.value);
    }
  }

  if (name.empty()) name = client.title;
  return name;
}


void BlackboxWindow::getNetwmHints(void) {
  // note: wm_name and wm_icon_name are read separately

  bool ret;
  const bt::Netwm& netwm = blackbox->netwm();

  bt::Netwm::AtomList atoms;
  ret = netwm.readWMWindowType(client.window, atoms);
  if (ret) {
    bt::Netwm::AtomList::iterator it = atoms.begin(), end = atoms.end();
    for (; it != end; ++it) {
      if (netwm.isSupportedWMWindowType(*it)) {
        client.window_type = ::window_type_from_atom(netwm, *it);
        break;
      }
    }
    if (client.window_type == WindowTypeDesktop) {
      // make me omnipresent
      setLayer(StackingList::LayerDesktop);
    }
  }

  atoms.clear();
  ret = netwm.readWMState(client.window, atoms);
  if (ret) {
    bt::Netwm::AtomList::iterator it = atoms.begin(), end = atoms.end();
    for (; it != end; ++it) {
      Atom state = *it;
      if (state == netwm.wmStateModal()) {
        if (isTransient())
          client.state.modal = True;
      } else if (state == netwm.wmStateMaximizedVert()) {
        if (client.state.maximized == 0)
          client.state.maximized = 2;
        else if (client.state.maximized == 3)
          client.state.maximized = 1;
      } else if (state == netwm.wmStateMaximizedHorz()) {
        if (client.state.maximized == 0)
          client.state.maximized = 3;
        else if (client.state.maximized == 2)
          client.state.maximized = 1;
      } else if (state == netwm.wmStateShaded()) {
        client.state.shaded = True;
      } else if (state == netwm.wmStateSkipTaskbar()) {
        if (client.state.skip == SKIP_NONE)
          client.state.skip = SKIP_TASKBAR;
        else if (client.state.skip == SKIP_PAGER)
          client.state.skip = SKIP_BOTH;
      } else if (state == netwm.wmStateSkipPager()) {
        if (client.state.skip == SKIP_NONE)
          client.state.skip = SKIP_PAGER;
        else if (client.state.skip == SKIP_TASKBAR)
          client.state.skip = SKIP_BOTH;
      } else if (state == netwm.wmStateHidden()) {
        client.state.iconic = True;
      } else if (state == netwm.wmStateFullscreen()) {
        client.state.fullscreen = True;
        setLayer(StackingList::LayerFullScreen);
      } else if (state == netwm.wmStateAbove()) {
        setLayer(StackingList::LayerAbove);
      } else if (state == netwm.wmStateBelow()) {
        setLayer(StackingList::LayerBelow);
      }
    }
  }

  unsigned int desktop;
  ret = netwm.readWMDesktop(client.window, desktop);
  if (ret) {
    if (desktop != 0xFFFFFFFF)
      client.workspace = desktop;
    else
      client.workspace = bt::BSENTINEL;
  }
}


/*
 * Retrieve which WM Protocols are supported by the client window.  If
 * the WM_DELETE_WINDOW protocol is supported, add the close button to
 * the window's decorations and allow the close behavior.  If the
 * WM_TAKE_FOCUS protocol is supported, save a value that indicates
 * this.
 */
void BlackboxWindow::getWMProtocols(void) {
  Atom *proto;
  int num_return = 0;

  if (XGetWMProtocols(blackbox->XDisplay(), client.window,
                      &proto, &num_return)) {
    for (int i = 0; i < num_return; ++i) {
      if (proto[i] == blackbox->getWMDeleteAtom()) {
        client.decorations |= WindowDecorationClose;
        client.functions |= WindowFunctionClose;
      } else if (proto[i] == blackbox->getWMTakeFocusAtom()) {
        client.state.send_focus_message = True;
      }
    }
    XFree(proto);
  }
}


/*
 * Gets the value of the WM_HINTS property.  If the property is not
 * set, then use a set of default values.
 */
void BlackboxWindow::getWMHints(void) {
  client.focus_mode = F_Passive;

  // remove from current window group
  if (client.window_group) {
    BWindowGroup *group = blackbox->findWindowGroup(client.window_group);
    if (group) group->removeWindow(this);
  }
  client.window_group = None;

  XWMHints *wmhint = XGetWMHints(blackbox->XDisplay(), client.window);
  if (! wmhint)
    return;

  if (wmhint->flags & InputHint) {
    if (wmhint->input == True) {
      if (client.state.send_focus_message)
        client.focus_mode = F_LocallyActive;
    } else {
      if (client.state.send_focus_message)
        client.focus_mode = F_GloballyActive;
      else
        client.focus_mode = F_NoInput;
    }
  }

  if (wmhint->flags & StateHint)
    client.current_state = wmhint->initial_state;

  if (wmhint->flags & WindowGroupHint &&
      wmhint->window_group != screen->screenInfo().rootWindow()) {
    client.window_group = wmhint->window_group;

    // add window to the appropriate group
    BWindowGroup *group = blackbox->findWindowGroup(client.window_group);
    if (! group) { // no group found, create it!
      new BWindowGroup(blackbox, client.window_group);
      group = blackbox->findWindowGroup(client.window_group);
    }
    if (group)
      group->addWindow(this);
  }

  XFree(wmhint);
}


/*
 * Gets the value of the WM_NORMAL_HINTS property.  If the property is
 * not set, then use a set of default values.
 */
void BlackboxWindow::getWMNormalHints(void) {
  long icccm_mask;
  XSizeHints sizehint;

  client.min_width = client.min_height =
    client.width_inc = client.height_inc = 1;
  client.base_width = client.base_height = 0;
  client.win_gravity = NorthWestGravity;
  client.min_aspect_x = client.min_aspect_y =
    client.max_aspect_x = client.max_aspect_y = 1;

  /*
    use the full screen, not the strut modified size. otherwise when
    the availableArea changes max_width/height will be incorrect and
    lead to odd rendering bugs.
  */
  const bt::Rect &rect = screen->screenInfo().rect();
  client.max_width = rect.width();
  client.max_height = rect.height();

  if (! XGetWMNormalHints(blackbox->XDisplay(), client.window,
                          &sizehint, &icccm_mask))
    return;

  client.normal_hint_flags = sizehint.flags;

  if (sizehint.flags & PMinSize) {
    if (sizehint.min_width >= 0)
      client.min_width = sizehint.min_width;
    if (sizehint.min_height >= 0)
      client.min_height = sizehint.min_height;
  }

  if (sizehint.flags & PMaxSize) {
    if (sizehint.max_width > static_cast<signed>(client.min_width))
      client.max_width = sizehint.max_width;
    else
      client.max_width = client.min_width;

    if (sizehint.max_height > static_cast<signed>(client.min_height))
      client.max_height = sizehint.max_height;
    else
      client.max_height = client.min_height;
  }

  if (sizehint.flags & PResizeInc) {
    client.width_inc = sizehint.width_inc;
    client.height_inc = sizehint.height_inc;
  }

  if (sizehint.flags & PAspect) {
    client.min_aspect_x = sizehint.min_aspect.x;
    client.min_aspect_y = sizehint.min_aspect.y;
    client.max_aspect_x = sizehint.max_aspect.x;
    client.max_aspect_y = sizehint.max_aspect.y;
  }

  if (sizehint.flags & PBaseSize) {
    client.base_width = sizehint.base_width;
    client.base_height = sizehint.base_height;
  }

  if (sizehint.flags & PWinGravity)
    client.win_gravity = sizehint.win_gravity;
}


/*
 * Gets the MWM hints for the class' contained window.  This is used
 * while initializing the window to its first state, and not
 * thereafter.
 *
 * Returns: true if the MWM hints are successfully retreived and
 * applied; false if they are not.
 */
void BlackboxWindow::getMWMHints(void) {
  /*
    this structure only contains 3 elements, even though the Motif 2.0
    structure contains 5, because we only use the first 3
  */
  struct PropMwmHints {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
  };
  static const unsigned int PROP_MWM_HINTS_ELEMENTS = 3u;
  enum { // MWM flags
    MWM_HINTS_FUNCTIONS   = 1<<0,
    MWM_HINTS_DECORATIONS = 1<<1
  };
  enum { // MWM functions
    MWM_FUNC_ALL      = 1<<0,
    MWM_FUNC_RESIZE   = 1<<1,
    MWM_FUNC_MOVE     = 1<<2,
    MWM_FUNC_MINIMIZE = 1<<3,
    MWM_FUNC_MAXIMIZE = 1<<4,
    MWM_FUNC_CLOSE    = 1<<5
  };
  enum { // MWM decorations
    MWM_DECOR_ALL      = 1<<0,
    MWM_DECOR_BORDER   = 1<<1,
    MWM_DECOR_RESIZEH  = 1<<2,
    MWM_DECOR_TITLE    = 1<<3,
    MWM_DECOR_MENU     = 1<<4,
    MWM_DECOR_MINIMIZE = 1<<5,
    MWM_DECOR_MAXIMIZE = 1<<6
  };

  Atom atom_return;
  PropMwmHints *mwmhints = 0;
  int format;
  unsigned long num, len;
  int ret = XGetWindowProperty(blackbox->XDisplay(), client.window,
                               blackbox->getMotifWMHintsAtom(), 0,
                               PROP_MWM_HINTS_ELEMENTS, False,
                               blackbox->getMotifWMHintsAtom(), &atom_return,
                               &format, &num, &len,
                               (unsigned char **) &mwmhints);

  if (ret != Success || ! mwmhints || num != PROP_MWM_HINTS_ELEMENTS)
    return;

  if (mwmhints->flags & MWM_HINTS_FUNCTIONS) {
    if (mwmhints->functions & MWM_FUNC_ALL) {
      client.functions = AllWindowFunctions;
    } else {
      client.functions = 0;

      if (mwmhints->functions & MWM_FUNC_RESIZE)
        client.functions |= WindowFunctionResize;
      if (mwmhints->functions & MWM_FUNC_MOVE)
        client.functions |= WindowFunctionMove;
      if (mwmhints->functions & MWM_FUNC_MINIMIZE)
        client.functions |= WindowFunctionIconify;
      if (mwmhints->functions & MWM_FUNC_MAXIMIZE)
        client.functions |= WindowFunctionMaximize;
      if (mwmhints->functions & MWM_FUNC_CLOSE)
        client.functions |= WindowFunctionClose;
    }
  }

  if (mwmhints->flags & MWM_HINTS_DECORATIONS) {
    if (mwmhints->decorations & MWM_DECOR_ALL) {
      client.decorations = AllWindowDecorations;
    } else {
      client.decorations = 0;

      if (mwmhints->decorations & MWM_DECOR_BORDER)
        client.decorations |= WindowDecorationBorder;
      if (mwmhints->decorations & MWM_DECOR_RESIZEH)
        client.decorations |= WindowDecorationHandle;
      if (mwmhints->decorations & MWM_DECOR_TITLE)
        client.decorations |= WindowDecorationTitlebar;
      if (mwmhints->decorations & MWM_DECOR_MINIMIZE)
        client.decorations |= WindowDecorationIconify;
      if (mwmhints->decorations & MWM_DECOR_MAXIMIZE)
        client.decorations |= WindowDecorationMaximize;
    }
  }

  XFree(mwmhints);
}


void BlackboxWindow::getTransientInfo(void) {
  if (client.transient_for &&
      client.transient_for != (BlackboxWindow *) ~0ul) {
    // reset transient_for in preparation of looking for a new owner
    client.transient_for->client.transientList.remove(this);
  }

  // we have no transient_for until we find a new one
  client.transient_for = (BlackboxWindow *) 0;

  Window trans_for;
  if (!XGetTransientForHint(blackbox->XDisplay(), client.window,
                            &trans_for)) {
    // transient_for hint not set
    return;
  }

  if (trans_for == client.window) {
    // wierd client... treat this window as a normal window
    return;
  }

  if (trans_for == None || trans_for == screen->screenInfo().rootWindow()) {
    /*
      this is an undocumented interpretation of the ICCCM. a transient
      associated with None/Root/itself is assumed to be a modal root
      transient.  we don't support the concept of a global transient,
      so we just associate this transient with nothing, and perhaps we
      will add support later for global modality.
    */
    client.transient_for = (BlackboxWindow *) ~0ul;
    client.state.modal = True;
    return;
  }

  client.transient_for = blackbox->findWindow(trans_for);
  if (! client.transient_for &&
      client.window_group && trans_for == client.window_group) {
    // no direct transient_for, perhaps this is a group transient?
    BWindowGroup *group = blackbox->findWindowGroup(client.window_group);
    if (group) client.transient_for = group->find(screen);
  }

  if (! client.transient_for || client.transient_for == this) {
    /*
      no transient_for found, or we have a wierd client that wants to
      be a transient for itself, so we treat this window as a normal
      window
    */
    client.transient_for = (BlackboxWindow*) 0;
    return;
  }

  /*
    Check for a circular transient state: this can lock up Blackbox
    when it tries to find the non-transient window for a transient.
  */
  BlackboxWindow *w = this;
  while (w->client.transient_for &&
         w->client.transient_for != (BlackboxWindow *) ~0ul) {
    if (w->client.transient_for == this) {
      client.transient_for = (BlackboxWindow*) 0;
      break;
    }
    w = w->client.transient_for;
  }

  if (client.transient_for) {
    // register ourselves with our new transient_for
    client.transient_for->client.transientList.push_back(this);
    client.workspace = client.transient_for->client.workspace;
  }
}


BlackboxWindow *BlackboxWindow::getTransientFor(void) const {
  if (client.transient_for &&
      client.transient_for != (BlackboxWindow*) ~0ul)
    return client.transient_for;
  return 0;
}


/*
 * This function is responsible for updating both the client and the
 * frame rectangles.  According to the ICCCM a client message is not
 * sent for a resize, only a move.
 */
void BlackboxWindow::configure(int dx, int dy,
                               unsigned int dw, unsigned int dh) {
  bool send_event = ((frame.rect.x() != dx || frame.rect.y() != dy) &&
                     ! client.state.moving);

  if (dw != frame.rect.width() || dh != frame.rect.height()) {
    frame.rect.setRect(dx, dy, dw, dh);
    frame.inside_w = frame.rect.width() - (frame.border_w * 2);
    frame.inside_h = frame.rect.height() - (frame.border_w * 2);

    if (frame.rect.right() <= 0 || frame.rect.bottom() <= 0)
      frame.rect.setPos(0, 0);

    client.rect.setCoords(frame.rect.left() + frame.margin.left,
                          frame.rect.top() + frame.margin.top,
                          frame.rect.right() - frame.margin.right,
                          frame.rect.bottom() - frame.margin.bottom);

#ifdef SHAPE
    if (client.state.shaped)
      configureShape();
#endif // SHAPE

    positionWindows();
    decorate();
    redrawWindowFrame();
  } else {
    frame.rect.setPos(dx, dy);

    XMoveWindow(blackbox->XDisplay(), frame.window,
                frame.rect.x(), frame.rect.y());
    /*
      we may have been called just after an opaque window move, so
      even though the old coords match the new ones no ConfigureNotify
      has been sent yet.  There are likely other times when this will
      be relevant as well.
    */
    if (! client.state.moving) send_event = True;
  }

  if (send_event) {
    // if moving, the update and event will occur when the move finishes
    client.rect.setPos(frame.rect.left() + frame.margin.left,
                       frame.rect.top() + frame.margin.top);

    XEvent event;
    event.type = ConfigureNotify;

    event.xconfigure.display = blackbox->XDisplay();
    event.xconfigure.event = client.window;
    event.xconfigure.window = client.window;
    event.xconfigure.x = client.rect.x();
    event.xconfigure.y = client.rect.y();
    event.xconfigure.width = client.rect.width();
    event.xconfigure.height = client.rect.height();
    event.xconfigure.border_width = client.old_bw;
    event.xconfigure.above = frame.window;
    event.xconfigure.override_redirect = False;

    XSendEvent(blackbox->XDisplay(), client.window, False,
               StructureNotifyMask, &event);
    XFlush(blackbox->XDisplay());
  }
}


#ifdef SHAPE
void BlackboxWindow::configureShape(void) {
  XShapeCombineShape(blackbox->XDisplay(), frame.window, ShapeBounding,
                     frame.margin.left - frame.border_w,
                     frame.margin.top - frame.border_w,
                     client.window, ShapeBounding, ShapeSet);

  int num = 0;
  XRectangle xrect[2];

  if (client.decorations & WindowDecorationTitlebar) {
    xrect[0].x = xrect[0].y = -frame.border_w;
    xrect[0].width = frame.rect.width();
    xrect[0].height = frame.style->title_height + (frame.border_w * 2);
    ++num;
  }

  if (client.decorations & WindowDecorationHandle) {
    xrect[1].x = -frame.border_w;
    xrect[1].y = frame.rect.height() - frame.margin.bottom +
                 frame.mwm_border_w - frame.border_w;
    xrect[1].width = frame.rect.width();
    xrect[1].height = frame.style->handle_height + (frame.border_w * 2);
    ++num;
  }

  XShapeCombineRectangles(blackbox->XDisplay(), frame.window,
                          ShapeBounding, 0, 0, xrect, num,
                          ShapeUnion, Unsorted);
}
#endif // SHAPE


void BlackboxWindow::setWorkspace(unsigned int new_workspace) {
  client.workspace = new_workspace;
  blackbox->netwm().setWMDesktop(client.window, client.workspace);
}


bool BlackboxWindow::setInputFocus(void) {
  if (!isVisible())
    return false;
  if (client.state.focused)
    return true;

  // do not give focus to a window that is about to close
  if (!validateClient())
    return false;

  const bt::Rect &scr = screen->screenInfo().rect();
  if (!frame.rect.intersects(scr)) {
    // client is outside the screen, move it to the center
    configure(scr.x() + (scr.width() - frame.rect.width()) / 2,
              scr.y() + (scr.height() - frame.rect.height()) / 2,
              frame.rect.width(), frame.rect.height());
  }

  if (!client.transientList.empty()) {
    // transfer focus to any modal transients
    BlackboxWindowList::iterator it, end = client.transientList.end();
    for (it = client.transientList.begin(); it != end; ++it) {
      if ((*it)->client.state.modal)
        return (*it)->setInputFocus();
    }
  }

  switch (client.focus_mode) {
  case F_Passive:
  case F_LocallyActive:
    XSetInputFocus(blackbox->XDisplay(), client.window,
                   RevertToPointerRoot, CurrentTime);
    break;

  case F_GloballyActive:
  case F_NoInput:
    /*
     * we could set the focus to none, since the window doesn't accept
     * focus, but we shouldn't set focus to nothing since this would
     * surely make someone angry.  instead, set the focus to the plate
     */
    XSetInputFocus(blackbox->XDisplay(), frame.plate,
                   RevertToPointerRoot, CurrentTime);
    break;
  }

  if (client.state.send_focus_message) {
    XEvent ce;
    ce.xclient.type = ClientMessage;
    ce.xclient.message_type = blackbox->getWMProtocolsAtom();
    ce.xclient.display = blackbox->XDisplay();
    ce.xclient.window = client.window;
    ce.xclient.format = 32;
    ce.xclient.data.l[0] = blackbox->getWMTakeFocusAtom();
    ce.xclient.data.l[1] = blackbox->getLastTime();
    ce.xclient.data.l[2] = 0l;
    ce.xclient.data.l[3] = 0l;
    ce.xclient.data.l[4] = 0l;
    XSendEvent(blackbox->XDisplay(), client.window, False, NoEventMask, &ce);
    XFlush(blackbox->XDisplay());
  }

  blackbox->setFocusedWindow(this);

  return true;
}


void BlackboxWindow::show(void) {
  if (client.state.visible) return;

  if (client.state.iconic)
    screen->removeIcon(this);

  client.state.iconic = false;
  client.state.visible = true;
  setState((client.state.shaded) ? IconicState : NormalState);

  XMapWindow(blackbox->XDisplay(), client.window);
  XMapSubwindows(blackbox->XDisplay(), frame.window);
  XMapWindow(blackbox->XDisplay(), frame.window);

  if (!client.transientList.empty()) {
    BlackboxWindowList::iterator it = client.transientList.begin(),
                                end = client.transientList.end();
    for (; it != end; ++it)
      (*it)->show();
  }

#ifdef DEBUG
  int real_x, real_y;
  Window child;
  XTranslateCoordinates(blackbox->XDisplay(), client.window,
                        screen->screenInfo().rootWindow(),
                        0, 0, &real_x, &real_y, &child);
  fprintf(stderr, "%s -- assumed: (%d, %d), real: (%d, %d)\n", getTitle(),
          client.rect.left(), client.rect.top(), real_x, real_y);
  assert(client.rect.left() == real_x && client.rect.top() == real_y);
#endif
}


void BlackboxWindow::hide(void) {
  if (!client.state.visible) return;

  client.state.visible = false;
  setState(client.state.iconic ? IconicState : client.current_state);

  XUnmapWindow(blackbox->XDisplay(), frame.window);

  /*
   * we don't want this XUnmapWindow call to generate an UnmapNotify
   * event, so we need to clear the event mask on client.window for a
   * split second.  HOWEVER, since X11 is asynchronous, the window
   * could be destroyed in that split second, leaving us with a ghost
   * window... so, we need to do this while the X server is grabbed
   */
  unsigned long event_mask = PropertyChangeMask | FocusChangeMask |
                             StructureNotifyMask;
  XGrabServer(blackbox->XDisplay());
  XSelectInput(blackbox->XDisplay(), client.window,
               event_mask & ~StructureNotifyMask);
  XUnmapWindow(blackbox->XDisplay(), client.window);
  XSelectInput(blackbox->XDisplay(), client.window, event_mask);
  XUngrabServer(blackbox->XDisplay());
}


void BlackboxWindow::close(void) {
  XEvent ce;
  ce.xclient.type = ClientMessage;
  ce.xclient.message_type = blackbox->getWMProtocolsAtom();
  ce.xclient.display = blackbox->XDisplay();
  ce.xclient.window = client.window;
  ce.xclient.format = 32;
  ce.xclient.data.l[0] = blackbox->getWMDeleteAtom();
  ce.xclient.data.l[1] = CurrentTime;
  ce.xclient.data.l[2] = 0l;
  ce.xclient.data.l[3] = 0l;
  ce.xclient.data.l[4] = 0l;
  XSendEvent(blackbox->XDisplay(), client.window, False, NoEventMask, &ce);
  XFlush(blackbox->XDisplay());
}


void BlackboxWindow::iconify(void) {
  if (client.state.iconic) return;

  if (client.transient_for &&
      client.transient_for != (BlackboxWindow *) ~0ul &&
      !client.transient_for->isIconic()) {
    client.transient_for->iconify();
  }

  screen->addIcon(this);

  client.state.iconic = true;
  hide();

  // iconify all transients first
  if (!client.transientList.empty()) {
    BlackboxWindowList::iterator it = client.transientList.begin(),
                                end = client.transientList.end();
    for (; it != end; ++it)
      (*it)->iconify();
  }
}


void BlackboxWindow::maximize(unsigned int button) {
  if (client.state.maximized) {
    client.state.maximized = 0;

    if (!client.state.fullscreen) {
      /*
        when a resize is begun, maximize(0) is called to clear any
        maximization flags currently set.  Otherwise it still thinks
        it is maximized.  so we do not need to call configure()
        because resizing will handle it
      */
      if (! client.state.resizing)
        configure(client.premax);

      redrawAllButtons(); // in case it is not called in configure()
    }

    setState(client.current_state);
    return;
  }

  client.state.maximized = button;

  if (!client.state.fullscreen) {
    frame.changing = screen->availableArea();
    client.premax = frame.rect;

    switch(button) {
    case 1:
      break;

    case 2:
      frame.changing.setX(client.premax.x());
      frame.changing.setWidth(client.premax.width());
      break;

    case 3:
      frame.changing.setY(client.premax.y());
      frame.changing.setHeight(client.premax.height());
      break;

    default:
      assert(0);
    }

    constrain(TopLeft);

    if (client.state.shaded)
      client.state.shaded = False;

    configure(frame.changing);
    redrawAllButtons(); // in case it is not called in configure()
  }

  setState(client.current_state);
}


// re-maximizes the window to take into account availableArea changes
void BlackboxWindow::remaximize(void) {
  if (client.state.shaded)
    return;

  bt::Rect tmp = client.premax;
  const unsigned int button = client.state.maximized;
  client.state.maximized = 0; // trick maximize() into working
  maximize(button);
  client.premax = tmp;
}


void BlackboxWindow::setShaded(bool shaded) {
  assert(client.decorations & WindowDecorationTitlebar);

  if (!!client.state.shaded == !!shaded)
    return;

  client.state.shaded = shaded;
  if (!shaded) {
    if (client.state.maximized) {
      remaximize();
    } else {
      XResizeWindow(blackbox->XDisplay(), frame.window,
                    frame.inside_w, frame.inside_h);
      // set the frame rect to the normal size
      frame.rect.setHeight(client.rect.height() + frame.margin.top +
                           frame.margin.bottom);
    }

    setState(NormalState);
  } else {
    XResizeWindow(blackbox->XDisplay(), frame.window,
                  frame.inside_w, frame.style->title_height);
    // set the frame rect to the shaded size
    frame.rect.setHeight(frame.style->title_height + (frame.border_w * 2));

    setState(IconicState);
  }
}


void BlackboxWindow::setFullScreen(bool b) {
  if (!!client.state.fullscreen == !!b)
    return;

  bool refocus = isFocused();
  client.state.fullscreen = b;
  if (client.state.fullscreen) {
    client.decorations = 0;
    client.functions &= ~(WindowFunctionResize | WindowFunctionMove |
                          WindowFunctionShade);

    if (!client.state.maximized)
      client.premax = frame.rect;
    upsize();

    frame.changing = screen->screenInfo().rect();
    constrain(TopLeft);
    configure(frame.changing);
    if (isVisible())
      screen->changeLayer(this, StackingList::LayerFullScreen);
    setState(client.current_state);
  } else {
    ::get_decorations(client.window_type, client.decorations,
                      client.functions);

    if (client.decorations & WindowDecorationTitlebar)
      createTitlebar();
    if (client.decorations & WindowDecorationHandle)
      createHandle();

    upsize();

    if (!client.state.maximized) {
      configure(client.premax);
      if (isVisible())
        screen->changeLayer(this, StackingList::LayerNormal);
      setState(client.current_state);
    } else {
      if (isVisible())
        screen->changeLayer(this, StackingList::LayerNormal);
      remaximize();
    }
  }

  ungrabButtons();
  grabButtons();

  if (refocus) setInputFocus();
}


void BlackboxWindow::redrawWindowFrame(void) const {
  if (client.decorations & WindowDecorationTitlebar) {
    redrawTitle();
    redrawLabel();
    redrawAllButtons();
  }

  if (client.decorations & WindowDecorationHandle) {
    redrawHandle();

    if (client.decorations & WindowDecorationGrip)
      redrawGrips();
  }

  if (client.decorations & WindowDecorationBorder) {
    if (client.state.focused)
      XSetWindowBorder(blackbox->XDisplay(),
                       frame.plate, frame.fborder_pixel);
    else
      XSetWindowBorder(blackbox->XDisplay(),
                       frame.plate, frame.uborder_pixel);
  }
}


void BlackboxWindow::setFocused(bool focused) {
  if (focused && !isVisible()) return;

  client.state.focused = focused;

  redrawWindowFrame();

  if (client.state.focused) {
    blackbox->setFocusedWindow(this);
    XInstallColormap(blackbox->XDisplay(), client.colormap);
  }
}


void BlackboxWindow::setState(unsigned long new_state) {
  client.current_state = new_state;

  unsigned long state[2];
  state[0] = client.current_state;
  state[1] = None;
  XChangeProperty(blackbox->XDisplay(), client.window,
                  blackbox->getWMStateAtom(), blackbox->getWMStateAtom(), 32,
                  PropModeReplace, (unsigned char *) state, 2);

  const bt::Netwm& netwm = blackbox->netwm();

  netwm.setWMDesktop(client.window, client.workspace);

  bt::Netwm::AtomList atoms;
  if (client.state.modal)
    atoms.push_back(netwm.wmStateModal());

  if (client.state.maximized == 0) {
    /* do nothing */
  } else if (client.state.maximized == 1) {
    atoms.push_back(netwm.wmStateMaximizedVert());
    atoms.push_back(netwm.wmStateMaximizedHorz());
  } else if (client.state.maximized == 2) {
    atoms.push_back(netwm.wmStateMaximizedVert());
  }  else if (client.state.maximized == 3) {
    atoms.push_back(netwm.wmStateMaximizedHorz());
  }

  if (client.state.shaded)
    atoms.push_back(netwm.wmStateShaded());

  switch (client.state.skip) {
  case SKIP_NONE:
    /* do nothing */
    break;
  case SKIP_BOTH:
    atoms.push_back(netwm.wmStateSkipTaskbar());
    atoms.push_back(netwm.wmStateSkipPager());
    break;
  case SKIP_TASKBAR:
    atoms.push_back(netwm.wmStateSkipTaskbar());
    break;
  case SKIP_PAGER:
    atoms.push_back(netwm.wmStateSkipPager());
    break;
  }

  if (client.state.iconic)
    atoms.push_back(netwm.wmStateHidden());

  if (client.state.fullscreen)
    atoms.push_back(netwm.wmStateFullscreen());

  switch (layer()) {
  case StackingList::LayerAbove:
    atoms.push_back(netwm.wmStateAbove());
    break;
  case StackingList::LayerBelow:
    atoms.push_back(netwm.wmStateBelow());
    break;
  default:
    break;
  }

  if (atoms.empty())
    netwm.removeProperty(client.window, netwm.wmState());
  else
    netwm.setWMState(client.window, atoms);

  atoms.clear();

  if (! client.state.iconic) {
    atoms.push_back(netwm.wmActionChangeDesktop());

    if (client.functions & WindowFunctionMove)
      atoms.push_back(netwm.wmActionMove());

    if (client.functions & WindowFunctionIconify)
      atoms.push_back(netwm.wmActionMinimize());

    if (client.functions & WindowFunctionResize) {
      atoms.push_back(netwm.wmActionResize());
      atoms.push_back(netwm.wmActionMaximizeHorz());
      atoms.push_back(netwm.wmActionMaximizeVert());
      atoms.push_back(netwm.wmActionFullscreen());
    }

    if ((client.decorations & WindowDecorationTitlebar) &&
        (client.functions & WindowFunctionShade))
      atoms.push_back(netwm.wmActionShade());
  }

  if (client.functions & WindowFunctionClose)
    atoms.push_back(netwm.wmActionClose());

  netwm.setWMAllowedActions(client.window, atoms);
}


bool BlackboxWindow::getState(void) {
  client.current_state = NormalState;

  Atom atom_return;
  bool ret = False;
  int foo;
  unsigned long *state, ulfoo, nitems;

  if ((XGetWindowProperty(blackbox->XDisplay(), client.window,
                          blackbox->getWMStateAtom(),
                          0l, 2l, False, blackbox->getWMStateAtom(),
                          &atom_return, &foo, &nitems, &ulfoo,
                          (unsigned char **) &state) != Success) ||
      (! state)) {
    return False;
  }

  if (nitems >= 1) {
    client.current_state = static_cast<unsigned long>(state[0]);
    ret = True;
  }

  XFree((void *) state);

  return ret;
}


/*
 *
 */
void BlackboxWindow::clearState(void) {
  XDeleteProperty(blackbox->XDisplay(), client.window,
                  blackbox->getWMStateAtom());

  const bt::Netwm& netwm = blackbox->netwm();
  netwm.removeProperty(client.window, netwm.wmDesktop());
  netwm.removeProperty(client.window, netwm.wmState());
  netwm.removeProperty(client.window, netwm.wmAllowedActions());
  netwm.removeProperty(client.window, netwm.wmVisibleName());
  netwm.removeProperty(client.window, netwm.wmVisibleIconName());
}



/*
 * Positions the bt::Rect r according the the client window position and
 * window gravity.
 */
void BlackboxWindow::applyGravity(bt::Rect &r) {
  // apply horizontal window gravity
  switch (client.win_gravity) {
  default:
  case NorthWestGravity:
  case SouthWestGravity:
  case WestGravity:
    r.setX(client.rect.x());
    break;

  case NorthGravity:
  case SouthGravity:
  case CenterGravity:
    r.setX(client.rect.x() - (frame.margin.left + frame.margin.right) / 2);
    break;

  case NorthEastGravity:
  case SouthEastGravity:
  case EastGravity:
    r.setX(client.rect.x() - (frame.margin.left + frame.margin.right) + 2);
    break;

  case ForgetGravity:
  case StaticGravity:
    r.setX(client.rect.x() - frame.margin.left);
    break;
  }

  // apply vertical window gravity
  switch (client.win_gravity) {
  default:
  case NorthWestGravity:
  case NorthEastGravity:
  case NorthGravity:
    r.setY(client.rect.y());
    break;

  case CenterGravity:
  case EastGravity:
  case WestGravity:
    r.setY(client.rect.y() - ((frame.margin.top + frame.margin.bottom) / 2));
    break;

  case SouthWestGravity:
  case SouthEastGravity:
  case SouthGravity:
    r.setY(client.rect.y() - (frame.margin.bottom + frame.margin.top) + 2);
    break;

  case ForgetGravity:
  case StaticGravity:
    r.setY(client.rect.y() - frame.margin.top);
    break;
  }
}


/*
 * The reverse of the applyGravity function.
 *
 * Positions the bt::Rect r according to the frame window position and
 * window gravity.
 */
void BlackboxWindow::restoreGravity(bt::Rect &r) {
  // restore horizontal window gravity
  switch (client.win_gravity) {
  default:
  case NorthWestGravity:
  case SouthWestGravity:
  case WestGravity:
    r.setX(frame.rect.x());
    break;

  case NorthGravity:
  case SouthGravity:
  case CenterGravity:
    r.setX(frame.rect.x() + (frame.margin.left + frame.margin.right) / 2);
    break;

  case NorthEastGravity:
  case SouthEastGravity:
  case EastGravity:
    r.setX(frame.rect.x() + (frame.margin.left + frame.margin.right) - 2);
    break;

  case ForgetGravity:
  case StaticGravity:
    r.setX(frame.rect.x() + frame.margin.left);
    break;
  }

  // restore vertical window gravity
  switch (client.win_gravity) {
  default:
  case NorthWestGravity:
  case NorthEastGravity:
  case NorthGravity:
    r.setY(frame.rect.y());
    break;

  case CenterGravity:
  case EastGravity:
  case WestGravity:
    r.setY(frame.rect.y() + (frame.margin.top + frame.margin.bottom) / 2);
    break;

  case SouthWestGravity:
  case SouthEastGravity:
  case SouthGravity:
    r.setY(frame.rect.y() + (frame.margin.top + frame.margin.bottom) - 2);
    break;

  case ForgetGravity:
  case StaticGravity:
    r.setY(frame.rect.y() + frame.margin.top);
    break;
  }
}


void BlackboxWindow::redrawTitle(void) const {
  bt::Rect u(0, 0, frame.inside_w, frame.style->title_height);
  bt::drawTexture(screen->screenNumber(),
                  (client.state.focused ? frame.style->t_focus :
                                          frame.style->t_unfocus),
                  frame.title, u, u,
                  (client.state.focused ? frame.ftitle : frame.utitle));
}


void BlackboxWindow::redrawLabel(void) const {
  bt::Rect u(0, 0, frame.label_w, frame.style->label_height);
  Pixmap p = (client.state.focused ? frame.flabel : frame.ulabel);
  if (p == ParentRelative) {
    int icon_width = 0;
    if (client.decorations & WindowDecorationIconify)
      icon_width = frame.style->button_width + frame.style->bevel_width + 1;

    bt::Rect t(-(frame.style->bevel_width + 1 + icon_width),
               -(frame.style->bevel_width),
               frame.inside_w, frame.style->title_height);
    bt::drawTexture(screen->screenNumber(),
                    (client.state.focused ? frame.style->t_focus :
                                            frame.style->t_unfocus),
                    frame.label, t, u,
                    (client.state.focused ? frame.ftitle : frame.utitle));
  } else {
    bt::drawTexture(screen->screenNumber(),
                    (client.state.focused ? frame.style->l_focus :
                                            frame.style->l_unfocus),
                    frame.label, u, u, p);
  }

  bt::Pen pen(screen->screenNumber(),
              ((client.state.focused) ?
               frame.style->l_text_focus : frame.style->l_text_unfocus));
  u.setCoords(u.left()  + frame.style->bevel_width,
              u.top() + frame.style->bevel_width,
              u.right() - frame.style->bevel_width,
              u.bottom() - frame.style->bevel_width);
  bt::drawText(frame.style->font, pen, frame.label, u,
               frame.style->alignment, client.visible_title);
}


void BlackboxWindow::redrawAllButtons(void) const {
  if (frame.iconify_button) redrawIconifyButton(False);
  if (frame.maximize_button) redrawMaximizeButton(client.state.maximized);
  if (frame.close_button) redrawCloseButton(False);
}


void BlackboxWindow::redrawIconifyButton(bool pressed) const {
  bt::Rect u(0, 0, frame.style->button_width, frame.style->button_width);
  Pixmap p = (pressed ? frame.pbutton :
              (client.state.focused ? frame.fbutton : frame.ubutton));
  if (p == ParentRelative) {
    bt::Rect t(-(frame.style->button_width + frame.style->bevel_width + 1),
               -(frame.style->bevel_width + 1),
               frame.inside_w, frame.style->title_height);
    bt::drawTexture(screen->screenNumber(),
                    (client.state.focused ? frame.style->t_focus :
                     frame.style->t_unfocus),
                    frame.iconify_button, t, u,
                    (client.state.focused ? frame.ftitle : frame.utitle));

  } else {
    bt::drawTexture(screen->screenNumber(),
                    (pressed ? frame.style->b_pressed :
                     (client.state.focused ? frame.style->b_focus :
                      frame.style->b_unfocus)),
                    frame.iconify_button, u, u, p);
  }

  bt::drawBitmap(frame.style->iconify,
                 bt::Pen(screen->screenNumber(),
                         client.state.focused
                         ? frame.style->b_pic_focus
                         : frame.style->b_pic_unfocus),
                 frame.iconify_button, u);
}


void BlackboxWindow::redrawMaximizeButton(bool pressed) const {
  bt::Rect u(0, 0, frame.style->button_width, frame.style->button_width);
  Pixmap p = (pressed ? frame.pbutton :
              (client.state.focused ? frame.fbutton : frame.ubutton));
  if (p == ParentRelative) {
    int button_w = frame.style->button_width + frame.style->bevel_width + 1;
    if (client.decorations & WindowDecorationClose)
      button_w *= 2;
    bt::Rect t(-(frame.inside_w - button_w),
               -(frame.style->bevel_width + 1),
               frame.inside_w, frame.style->title_height);
    bt::drawTexture(screen->screenNumber(),
                    (client.state.focused ? frame.style->t_focus :
                     frame.style->t_unfocus),
                    frame.maximize_button, t, u,
                    (client.state.focused ? frame.ftitle : frame.utitle));
  } else {
    bt::drawTexture(screen->screenNumber(),
                    (pressed ? frame.style->b_pressed :
                     (client.state.focused ? frame.style->b_focus :
                      frame.style->b_unfocus)),
                    frame.maximize_button, u, u, p);
  }

  bt::drawBitmap(frame.style->maximize,
                 bt::Pen(screen->screenNumber(),
                         client.state.focused
                         ? frame.style->b_pic_focus
                         : frame.style->b_pic_unfocus),
                 frame.maximize_button, u);
}


void BlackboxWindow::redrawCloseButton(bool pressed) const {
  bt::Rect u(0, 0, frame.style->button_width, frame.style->button_width);
  Pixmap p = (pressed ? frame.pbutton :
              (client.state.focused ? frame.fbutton : frame.ubutton));
  if (p == ParentRelative) {
    const int button_w = frame.style->button_width +
                         frame.style->bevel_width + 1;
    bt::Rect t(-(frame.inside_w - button_w),
               -(frame.style->bevel_width + 1),
               frame.inside_w, frame.style->title_height);
    bt::drawTexture(screen->screenNumber(),
                    (client.state.focused ? frame.style->t_focus :
                     frame.style->t_unfocus),
                    frame.close_button, t, u,
                    (client.state.focused ? frame.ftitle : frame.utitle));
  } else {
    bt::drawTexture(screen->screenNumber(),
                    (pressed ? frame.style->b_pressed :
                     (client.state.focused ? frame.style->b_focus :
                      frame.style->b_unfocus)),
                    frame.close_button, u, u, p);
  }

  bt::drawBitmap(frame.style->close,
                 bt::Pen(screen->screenNumber(),
                         client.state.focused
                         ? frame.style->b_pic_focus
                         : frame.style->b_pic_unfocus),
                 frame.close_button, u);
}


void BlackboxWindow::redrawHandle(void) const {
  bt::Rect u(0, 0, frame.inside_w, frame.style->handle_height);
  bt::drawTexture(screen->screenNumber(),
                  (client.state.focused ? frame.style->h_focus :
                                          frame.style->h_unfocus),
                  frame.handle, u, u,
                  (client.state.focused ? frame.fhandle : frame.uhandle));
}


void BlackboxWindow::redrawGrips(void) const {
  bt::Rect u(0, 0, frame.style->grip_width, frame.style->handle_height);
  Pixmap p = (client.state.focused ? frame.fgrip : frame.ugrip);
  if (p == ParentRelative) {
    bt::Rect t(0, 0, frame.inside_w, frame.style->handle_height);
    bt::drawTexture(screen->screenNumber(),
                    (client.state.focused ? frame.style->h_focus :
                                            frame.style->h_unfocus),
                    frame.right_grip, t, u, p);

    t.setPos(-(frame.inside_w - frame.style->grip_width), 0);
    bt::drawTexture(screen->screenNumber(),
                    (client.state.focused ? frame.style->h_focus :
                                            frame.style->h_unfocus),
                    frame.right_grip, t, u, p);
  } else {
    bt::drawTexture(screen->screenNumber(),
                    (client.state.focused ? frame.style->g_focus :
                                            frame.style->g_unfocus),
                    frame.left_grip, u, u, p);

    bt::drawTexture(screen->screenNumber(),
                    (client.state.focused ? frame.style->g_focus :
                                            frame.style->g_unfocus),
                    frame.right_grip, u, u, p);
  }
}


void
BlackboxWindow::clientMessageEvent(const XClientMessageEvent * const event) {
  if (event->format != 32) return;

  const bt::Netwm& netwm = blackbox->netwm();

  if (event->message_type == blackbox->getWMChangeStateAtom()) {
    if (event->data.l[0] == IconicState) {
      iconify();
    } else if (event->data.l[0] == NormalState) {
      show();
    }
  } else if (event->message_type == netwm.activeWindow()) {
    if (client.workspace != screen->currentWorkspace())
      screen->setCurrentWorkspace(client.workspace);

    if (client.state.iconic)
      show();
    if (setInputFocus())
      screen->raiseWindow(this);
  } else if (event->message_type == netwm.closeWindow()) {
    close();
  } else if (event->message_type == netwm.moveresizeWindow()) {
    XConfigureRequestEvent request;
    request.window = event->window;
    request.x = event->data.l[1];
    request.y = event->data.l[2];
    request.width = event->data.l[3];
    request.height = event->data.l[4];
    request.value_mask = CWX | CWY | CWWidth | CWHeight;

    const int old_gravity = client.win_gravity;
    if (event->data.l[0] != 0)
      client.win_gravity = event->data.l[0];

    configureRequestEvent(&request);

    client.win_gravity = old_gravity;
  } else if (event->message_type == netwm.wmDesktop()) {
    const unsigned int desktop = event->data.l[0];
    if (desktop != 0xFFFFFFFF && desktop != client.workspace) {
      hide();
      setWorkspace(desktop);
    } else if (desktop == 0xFFFFFFFF) {
      setWorkspace(bt::BSENTINEL);
      if (!isVisible())
        show();
    }
  } else if (event->message_type == netwm.wmState()) {
    Atom action = event->data.l[0],
          first = event->data.l[1],
         second = event->data.l[2];

    int max_horz = 0, max_vert = 0,
    skip_taskbar = 0, skip_pager = 0;

    if (first == netwm.wmStateModal() || second == netwm.wmStateModal()) {
      if ((action == netwm.wmStateAdd() ||
           (action == netwm.wmStateToggle() && ! client.state.modal)) &&
          isTransient())
        client.state.modal = True;
      else
        client.state.modal = False;
    }
    if (first == netwm.wmStateMaximizedHorz() ||
        second == netwm.wmStateMaximizedHorz()) {
      if (action == netwm.wmStateAdd() ||
          (action == netwm.wmStateToggle() &&
           ! (client.state.maximized == 1 || client.state.maximized == 3)))
        max_horz = 1;
      else
        max_horz = -1;
    }
    if (first == netwm.wmStateMaximizedVert() ||
        second == netwm.wmStateMaximizedVert()) {
      if (action == netwm.wmStateAdd() ||
          (action == netwm.wmStateToggle() &&
           ! (client.state.maximized == 1 || client.state.maximized == 2)))
        max_vert = 1;
      else
        max_vert = -1;
    }
    if (first == netwm.wmStateShaded() ||
        second == netwm.wmStateShaded()) {
      if (action == netwm.wmStateRemove())
        setShaded(false);
      else if (action == netwm.wmStateAdd())
        setShaded(true);
      else if (action == netwm.wmStateToggle())
        setShaded(!client.state.shaded);
    }
    if (first == netwm.wmStateSkipTaskbar() ||
        second == netwm.wmStateSkipTaskbar()) {
      if (action == netwm.wmStateAdd() ||
          (action == netwm.wmStateToggle() &&
           ! (client.state.skip == SKIP_TASKBAR ||
              client.state.skip == SKIP_BOTH)))
        skip_taskbar = 1;
      else
        skip_taskbar = -1;
    }
    if (first == netwm.wmStateSkipPager() ||
        second == netwm.wmStateSkipPager()) {
      if (action == netwm.wmStateAdd() ||
          (action == netwm.wmStateToggle() &&
           ! (client.state.skip == SKIP_PAGER ||
              client.state.skip == SKIP_BOTH)))
        skip_pager = 1;
      else
        skip_pager = -1;
    }
    if (first == netwm.wmStateHidden() ||
        second == netwm.wmStateHidden()) {
      /* ignore this message */
    }
    if (first == netwm.wmStateFullscreen() ||
        second == netwm.wmStateFullscreen()) {
      if (action == netwm.wmStateAdd() ||
          (action == netwm.wmStateToggle() &&
           ! client.state.fullscreen)) {
        setFullScreen(true);
      } else if (action == netwm.wmStateToggle() ||
                 action == netwm.wmStateRemove()) {
        setFullScreen(false);
      }
    }
    if (first == netwm.wmStateAbove() ||
        second == netwm.wmStateAbove()) {
      if (action == netwm.wmStateAdd() ||
          (action == netwm.wmStateToggle() &&
           layer() != StackingList::LayerAbove)) {
        screen->changeLayer(this, StackingList::LayerAbove);
      } else if (action == netwm.wmStateToggle() ||
                 action == netwm.wmStateRemove()) {
        screen->changeLayer(this, StackingList::LayerNormal);
      }
    }
    if (first == netwm.wmStateBelow() ||
        second == netwm.wmStateBelow()) {
      if (action == netwm.wmStateAdd() ||
          (action == netwm.wmStateToggle() &&
           layer() != StackingList::LayerBelow)) {
        screen->changeLayer(this, StackingList::LayerBelow);
      } else if (action == netwm.wmStateToggle() ||
                 action == netwm.wmStateRemove()) {
        screen->changeLayer(this, StackingList::LayerNormal);
      }
    }

    if (max_horz != 0 || max_vert != 0) {
      if (client.state.maximized)
        maximize(0);
      unsigned int button = 0;
      if (max_horz == 1 && max_vert != 1)
        button = 3;
      else if (max_vert == 1 && max_horz != 1)
        button = 2;
      else if (max_vert == 1 && max_horz == 1)
        button = 1;
      if (button)
        maximize(button);
    }

    if (skip_taskbar != 0 || skip_pager != 0) {
      if (skip_taskbar == 1 && skip_pager != 1)
        client.state.skip = SKIP_TASKBAR;
      else if (skip_pager == 1 && skip_taskbar != 1)
        client.state.skip = SKIP_PAGER;
      else if (skip_pager == 1 && skip_taskbar == 1)
        client.state.skip = SKIP_BOTH;
      else
        client.state.skip = SKIP_NONE;
    }
  } else if (event->message_type == netwm.wmStrut()) {
    if (! client.strut) {
      client.strut = new bt::Netwm::Strut;
      screen->addStrut(client.strut);
    }

    netwm.readWMStrut(client.window, client.strut);
    if (client.strut->left || client.strut->right ||
        client.strut->top || client.strut->bottom) {
      screen->updateStrut();
    } else {
      screen->removeStrut(client.strut);
      delete client.strut;
    }
  }
}


void BlackboxWindow::unmapNotifyEvent(const XUnmapEvent * const event) {
  if (event->window != client.window)
    return;

#ifdef    DEBUG
  fprintf(stderr, "BlackboxWindow::unmapNotifyEvent() for 0x%lx\n",
          client.window);
#endif // DEBUG

  screen->releaseWindow(this, False);
}


void
BlackboxWindow::destroyNotifyEvent(const XDestroyWindowEvent * const event) {
  if (event->window != client.window)
    return;

#ifdef    DEBUG
  fprintf(stderr, "BlackboxWindow::destroyNotifyEvent() for 0x%lx\n",
          client.window);
#endif // DEBUG

  screen->releaseWindow(this, False);
}


void BlackboxWindow::reparentNotifyEvent(const XReparentEvent * const event) {
  if (event->window != client.window || event->parent == frame.plate)
    return;

#ifdef    DEBUG
  fprintf(stderr, "BlackboxWindow::reparentNotifyEvent(): reparent 0x%lx to "
          "0x%lx.\n", client.window, event->parent);
#endif // DEBUG

  XEvent replay;
  replay.xreparent = *event;
  XPutBackEvent(blackbox->XDisplay(), &replay);
  screen->releaseWindow(this, True);
}


void BlackboxWindow::propertyNotifyEvent(const XPropertyEvent * const event) {
  if (event->state == PropertyDelete || ! validateClient())
    return;

#ifdef    DEBUG
  fprintf(stderr, "BlackboxWindow::propertyNotifyEvent(): for 0x%lx\n",
          client.window);
#endif

  switch(event->atom) {
  case XA_WM_CLASS:
  case XA_WM_CLIENT_MACHINE:
  case XA_WM_COMMAND:
    break;

  case XA_WM_TRANSIENT_FOR:
    // determine if this is a transient window
    getTransientInfo();

    // adjust the window decorations based on transience
    if (isTransient()) {
      client.decorations &= ~(WindowDecorationIconify |
                              WindowDecorationMaximize);
      client.functions   &= ~(WindowFunctionShade |
                              WindowFunctionIconify |
                              WindowFunctionMaximize);
    }

    reconfigure();
    break;

  case XA_WM_HINTS:
    getWMHints();
    break;

  case XA_WM_ICON_NAME: {
    client.icon_title = readWMIconName();
    if (client.state.iconic)
      screen->propagateWindowName(this);
    break;
  }

  case XA_WM_NAME: {
    client.title = readWMName();

    client.visible_title =
      bt::ellideText(client.title, frame.label_w, "...",
                     screen->screenNumber(), frame.style->font);
    blackbox->netwm().setWMVisibleName(client.window, client.visible_title);

    if (client.decorations & WindowDecorationTitlebar)
      redrawLabel();

    screen->propagateWindowName(this);
    break;
  }

  case XA_WM_NORMAL_HINTS: {
    getWMNormalHints();

    if ((client.normal_hint_flags & PMinSize) &&
        (client.normal_hint_flags & PMaxSize)) {
      // the window now can/can't resize itself, so the buttons need to be
      // regrabbed.
      ungrabButtons();
      if (client.max_width <= client.min_width &&
          client.max_height <= client.min_height) {
        client.decorations &= ~(WindowDecorationMaximize |
                                WindowDecorationGrip);
        client.functions   &= ~(WindowFunctionResize |
                                WindowFunctionMaximize);
      } else {
        if (! isTransient()) {
          client.decorations |= (WindowDecorationIconify |
                                 WindowDecorationMaximize);
          client.functions   |= (WindowFunctionShade |
                                 WindowFunctionIconify |
                                 WindowFunctionMaximize );
        }
        client.functions |= WindowFunctionResize;
      }
      grabButtons();
    }

    bt::Rect old_rect = frame.rect;

    upsize();

    if (old_rect != frame.rect)
      reconfigure();

    break;
  }

  default:
    if (event->atom == blackbox->getWMProtocolsAtom()) {
      getWMProtocols();

      if (client.decorations & WindowDecorationClose && !frame.close_button) {
        createCloseButton();
        if (client.decorations & WindowDecorationTitlebar) {
          positionButtons(True);
          XMapSubwindows(blackbox->XDisplay(), frame.title);
        }
      }
    } else if (event->atom == blackbox->getMotifWMHintsAtom()) {
      getMWMHints();
    }

    break;
  }
}


void BlackboxWindow::exposeEvent(const XExposeEvent * const event) {
#ifdef DEBUG
  fprintf(stderr, "BlackboxWindow::exposeEvent() for 0x%lx\n", client.window);
#endif

  if (frame.title == event->window)
    redrawTitle();
  else if (frame.label == event->window)
    redrawLabel();
  else if (frame.close_button == event->window)
    redrawCloseButton(False);
  else if (frame.maximize_button == event->window)
    redrawMaximizeButton(client.state.maximized);
  else if (frame.iconify_button == event->window)
    redrawIconifyButton(False);
  else if (frame.handle == event->window)
    redrawHandle();
  else if (frame.left_grip == event->window ||
           frame.right_grip == event->window)
    redrawGrips();
}


void BlackboxWindow::configureRequestEvent(const XConfigureRequestEvent *
                                           const event) {
  if (event->window != client.window || client.state.iconic)
    return;

  if (event->value_mask & CWBorderWidth)
    client.old_bw = event->border_width;

  if (event->value_mask & (CWX | CWY | CWWidth | CWHeight)) {
    bt::Rect req = frame.rect;

    if (event->value_mask & (CWX | CWY)) {
      if (event->value_mask & CWX)
        client.rect.setX(event->x);
      if (event->value_mask & CWY)
        client.rect.setY(event->y);

      applyGravity(req);
    }

    if (event->value_mask & CWWidth)
      req.setWidth(event->width + frame.margin.left + frame.margin.right);

    if (event->value_mask & CWHeight)
      req.setHeight(event->height + frame.margin.top + frame.margin.bottom);

    configure(req.x(), req.y(), req.width(), req.height());
  }

  if (event->value_mask & CWStackMode) {
    switch (event->detail) {
    case Below:
    case BottomIf:
      screen->lowerWindow(this);
      break;

    case Above:
    case TopIf:
    default:
      screen->raiseWindow(this);
      break;
    }
  }
}


void BlackboxWindow::buttonPressEvent(const XButtonEvent * const event) {
#ifdef DEBUG
  fprintf(stderr, "BlackboxWindow::buttonPressEvent() for 0x%lx\n",
          client.window);
#endif

  if (frame.maximize_button == event->window) {
    if (event->button < 4)
      redrawMaximizeButton(True);
  } else if (frame.iconify_button == event->window) {
    if (event->button == 1)
      redrawIconifyButton(True);
  } else if (frame.close_button == event->window) {
    if (event->button == 1)
      redrawCloseButton(True);
  } else if (frame.plate == event->window) {
    if (event->button == 1
        || (event->button == 3 && event->state == Mod1Mask)) {
      if (! client.state.focused)
        setInputFocus();
      else
        XInstallColormap(blackbox->XDisplay(), client.colormap);

      screen->raiseWindow(this);
      XAllowEvents(blackbox->XDisplay(), ReplayPointer, event->time);
    }
  } else if (frame.title == event->window || frame.label == event->window ||
             frame.handle) {
    if (event->button == 1
        || (event->button == 3 && event->state == Mod1Mask)) {
      if (! client.state.focused)
        setInputFocus();
      else
        XInstallColormap(blackbox->XDisplay(), client.colormap);

      if (frame.title == event->window || frame.label == event->window &&
          (client.functions & WindowFunctionShade)) {
        if ((event->time - lastButtonPressTime <=
             blackbox->resource().doubleClickInterval()) ||
            event->state == ControlMask) {
          lastButtonPressTime = 0;
          setShaded(!client.state.shaded);
        } else {
          lastButtonPressTime = event->time;
        }
      }
      frame.grab_x = event->x_root - frame.rect.x() - frame.border_w;
      frame.grab_y = event->y_root - frame.rect.y() - frame.border_w;
      screen->raiseWindow(this);
    } else if (event->button == 2) {
      screen->lowerWindow(this);
    } else if (event->button == 3) {
      const int extra = frame.border_w + frame.mwm_border_w;
      bt::Rect rect(client.rect.x() - extra,
                    client.rect.y() - extra,
                    client.rect.width() + (extra * 2),
                    client.rect.height() + (extra * 2));

      Windowmenu *windowmenu = screen->windowmenu(this);
      windowmenu->popup(event->x_root, event->y_root, rect);
    }
  }
}


void BlackboxWindow::buttonReleaseEvent(const XButtonEvent * const event) {
#ifdef DEBUG
  fprintf(stderr, "BlackboxWindow::buttonReleaseEvent() for 0x%lx\n",
          client.window);
#endif

  if (event->window == frame.maximize_button) {
    if (event->button < 4) {
      if (bt::within(event->x, event->y,
                     frame.style->button_width, frame.style->button_width)) {
        maximize(event->button);
        screen->raiseWindow(this);
      } else {
        redrawMaximizeButton(client.state.maximized);
      }
    }
  } else if (event->window == frame.iconify_button) {
    if (event->button == 1) {
      if (bt::within(event->x, event->y,
                     frame.style->button_width, frame.style->button_width))
        iconify();
      else
        redrawIconifyButton(False);
    }
  } else if (event->window == frame.close_button) {
    if (event->button == 1) {
      if (bt::within(event->x, event->y,
                     frame.style->button_width, frame.style->button_width))
        close();
      redrawCloseButton(False);
    }
  } else if (client.state.moving) {
    client.state.moving = False;

    if (! screen->resource().doOpaqueMove()) {
      /* when drawing the rubber band, we need to make sure we only
       * draw inside the frame... frame.changing_* contain the new
       * coords for the window, so we need to subtract 1 from
       * changing_w/changing_h every where we draw the rubber band
       * (for both moving and resizing)
       */
      bt::Pen pen(screen->screenNumber(), bt::Color(0xff, 0xff, 0xff));
      const int hw = frame.border_w / 2;
      pen.setGCFunction(GXxor);
      pen.setLineWidth(frame.border_w);
      pen.setSubWindowMode(IncludeInferiors);
      XDrawRectangle(blackbox->XDisplay(), screen->screenInfo().rootWindow(),
                     pen.gc(),
                     frame.changing.x() + hw,
                     frame.changing.y() + hw,
                     frame.changing.width() - frame.border_w,
                     frame.changing.height() - frame.border_w);
      XUngrabServer(blackbox->XDisplay());

      configure(frame.changing.x(), frame.changing.y(),
                frame.changing.width(), frame.changing.height());
    } else {
      configure(frame.rect.x(), frame.rect.y(),
                frame.rect.width(), frame.rect.height());
    }
    screen->hideGeometry();
    XUngrabPointer(blackbox->XDisplay(), CurrentTime);
  } else if (client.state.resizing) {
    bt::Pen pen(screen->screenNumber(), bt::Color(0xff, 0xff, 0xff));
    const int hw = frame.border_w / 2;
    pen.setGCFunction(GXxor);
    pen.setLineWidth(frame.border_w);
    pen.setSubWindowMode(IncludeInferiors);
    XDrawRectangle(blackbox->XDisplay(), screen->screenInfo().rootWindow(),
                   pen.gc(),
                   frame.changing.x() + hw,
                   frame.changing.y() + hw,
                   frame.changing.width() - frame.border_w,
                   frame.changing.height() - frame.border_w);
    XUngrabServer(blackbox->XDisplay());

    screen->hideGeometry();

    constrain((event->window == frame.left_grip) ? TopRight : TopLeft);

    // unset maximized state when resized after fully maximized
    if (client.state.maximized == 1)
      maximize(0);
    client.state.resizing = False;
    configure(frame.changing.x(), frame.changing.y(),
              frame.changing.width(), frame.changing.height());

    XUngrabPointer(blackbox->XDisplay(), CurrentTime);
  } else if (event->window == frame.window) {
    if (event->button == 2 && event->state == Mod1Mask)
      XUngrabPointer(blackbox->XDisplay(), CurrentTime);
  }
}


static
void collisionAdjust(int* x, int* y, unsigned int width, unsigned int height,
                     const bt::Rect& rect, int snap_distance) {
  // window corners
  const int wleft = *x,
           wright = *x + width - 1,
             wtop = *y,
          wbottom = *y + height - 1,

            dleft = abs(wleft - rect.left()),
           dright = abs(wright - rect.right()),
             dtop = abs(wtop - rect.top()),
          dbottom = abs(wbottom - rect.bottom());

  // snap left?
  if (dleft < snap_distance && dleft <= dright)
    *x = rect.left();
  // snap right?
  else if (dright < snap_distance)
    *x = rect.right() - width + 1;

  // snap top?
  if (dtop < snap_distance && dtop <= dbottom)
    *y = rect.top();
  // snap bottom?
  else if (dbottom < snap_distance)
    *y = rect.bottom() - height + 1;
}


void BlackboxWindow::motionNotifyEvent(const XMotionEvent * const event) {
#ifdef DEBUG
  fprintf(stderr, "BlackboxWindow::motionNotifyEvent() for 0x%lx\n",
          client.window);
#endif

  if ((client.functions & WindowFunctionMove) && ! client.state.resizing &&
      event->state & Button1Mask &&
      (frame.title == event->window || frame.label == event->window ||
       frame.handle == event->window || frame.window == event->window)) {
    if (! client.state.moving) {
      // begin a move
      XGrabPointer(blackbox->XDisplay(), event->window, False,
                   Button1MotionMask | ButtonReleaseMask,
                   GrabModeAsync, GrabModeAsync,
                   None, blackbox->resource().moveCursor(), CurrentTime);

      client.state.moving = True;

      if (! screen->resource().doOpaqueMove()) {
        XGrabServer(blackbox->XDisplay());

        frame.changing = frame.rect;
        screen->showPosition(frame.changing.x(), frame.changing.y());

        bt::Pen pen(screen->screenNumber(), bt::Color(0xff, 0xff, 0xff));
        const int hw = frame.border_w / 2;
        pen.setGCFunction(GXxor);
        pen.setLineWidth(frame.border_w);
        pen.setSubWindowMode(IncludeInferiors);
        XDrawRectangle(blackbox->XDisplay(), screen->screenInfo().rootWindow(),
                       pen.gc(),
                       frame.changing.x() + hw,
                       frame.changing.y() + hw,
                       frame.changing.width() - frame.border_w,
                       frame.changing.height() - frame.border_w);
      }
    } else {
      // continue a move
      int dx = event->x_root - frame.grab_x, dy = event->y_root - frame.grab_y;
      dx -= frame.border_w;
      dy -= frame.border_w;

      const int snap_distance = screen->resource().edgeSnapThreshold();

      if (snap_distance) {
        collisionAdjust(&dx, &dy, frame.rect.width(), frame.rect.height(),
                        screen->availableArea(), snap_distance);
        if (! screen->resource().doFullMax())
          collisionAdjust(&dx, &dy, frame.rect.width(), frame.rect.height(),
                          screen->screenInfo().rect(), snap_distance);
      }

      if (screen->resource().doOpaqueMove()) {
        configure(dx, dy, frame.rect.width(), frame.rect.height());
      } else {
        bt::Pen pen(screen->screenNumber(), bt::Color(0xff, 0xff, 0xff));
        const int hw = frame.border_w / 2;
        pen.setGCFunction(GXxor);
        pen.setLineWidth(frame.border_w);
        pen.setSubWindowMode(IncludeInferiors);
        XDrawRectangle(blackbox->XDisplay(), screen->screenInfo().rootWindow(),
                       pen.gc(),
                       frame.changing.x() + hw,
                       frame.changing.y() + hw,
                       frame.changing.width() - frame.border_w,
                       frame.changing.height() - frame.border_w);

        frame.changing.setPos(dx, dy);

        XDrawRectangle(blackbox->XDisplay(), screen->screenInfo().rootWindow(),
                       pen.gc(),
                       frame.changing.x() + hw,
                       frame.changing.y() + hw,
                       frame.changing.width() - frame.border_w,
                       frame.changing.height() - frame.border_w);
      }

      screen->showPosition(dx, dy);
    }
  } else if ((client.functions & WindowFunctionResize) &&
             (event->state & Button1Mask &&
              (event->window == frame.right_grip ||
               event->window == frame.left_grip)) ||
             (event->state & Button3Mask && event->state & Mod1Mask &&
              event->window == frame.window)) {
    bool left = (event->window == frame.left_grip);

    if (! client.state.resizing) {
      // begin a resize
      XGrabServer(blackbox->XDisplay());
      XGrabPointer(blackbox->XDisplay(), event->window, False,
                   ButtonMotionMask | ButtonReleaseMask,
                   GrabModeAsync, GrabModeAsync, None,
                   ((left) ? blackbox->resource().resizeBottomLeftCursor() :
                    blackbox->resource().resizeBottomRightCursor()),
                   CurrentTime);

      client.state.resizing = True;

      frame.grab_x = event->x;
      frame.grab_y = event->y;
      frame.changing = frame.rect;

      constrain(left ? TopRight : TopLeft);

      bt::Pen pen(screen->screenNumber(), bt::Color(0xff, 0xff, 0xff));
      const int hw = frame.border_w / 2;
      pen.setGCFunction(GXxor);
      pen.setLineWidth(frame.border_w);
      pen.setSubWindowMode(IncludeInferiors);
      XDrawRectangle(blackbox->XDisplay(), screen->screenInfo().rootWindow(),
                     pen.gc(),
                     frame.changing.x() + hw,
                     frame.changing.y() + hw,
                     frame.changing.width() - frame.border_w,
                     frame.changing.height() - frame.border_w);

      showGeometry(frame.changing);
    } else {
      // continue a resize
      bt::Rect curr = frame.changing;

      if (left) {
        int delta =
          std::min<signed>(event->x_root - frame.grab_x,
                           frame.rect.right() -
                           (frame.margin.left + frame.margin.right + 1));
        frame.changing.setCoords(delta, frame.rect.top(),
                                 frame.rect.right(), frame.rect.bottom());
      } else {
        int nw = std::max<signed>(event->x - frame.grab_x + frame.rect.width(),
                                  frame.margin.left + frame.margin.right + 1);
        frame.changing.setWidth(nw);
      }

      int nh = std::max<signed>(event->y - frame.grab_y + frame.rect.height(),
                                frame.margin.top + frame.margin.bottom + 1);
      frame.changing.setHeight(nh);

      constrain(left ? TopRight : TopLeft);

      if (curr != frame.changing) {
        bt::Pen pen(screen->screenNumber(), bt::Color(0xff, 0xff, 0xff));
        const int hw = frame.border_w / 2;
        pen.setGCFunction(GXxor);
        pen.setLineWidth(frame.border_w);
        pen.setSubWindowMode(IncludeInferiors);
        XDrawRectangle(blackbox->XDisplay(), screen->screenInfo().rootWindow(),
                       pen.gc(),
                       curr.x() + hw,
                       curr.y() + hw,
                       curr.width() - frame.border_w,
                       curr.height() - frame.border_w);

        XDrawRectangle(blackbox->XDisplay(), screen->screenInfo().rootWindow(),
                       pen.gc(),
                       frame.changing.x() + hw,
                       frame.changing.y() + hw,
                       frame.changing.width() - frame.border_w,
                       frame.changing.height() - frame.border_w);

        showGeometry(frame.changing);
      }
    }
  }
}


void BlackboxWindow::enterNotifyEvent(const XCrossingEvent * const event) {
  if (event->window != frame.window)
    return;

  if (!screen->resource().isSloppyFocus() || !isVisible())
    return;

  switch (client.window_type) {
  case WindowTypeDesktop:
  case WindowTypeDock:
    return;

  default:
    break;
  }

  XEvent next;
  bool leave = False, inferior = False;

  while (XCheckTypedWindowEvent(blackbox->XDisplay(), event->window,
                                LeaveNotify, &next)) {
    if (next.type == LeaveNotify && next.xcrossing.mode == NotifyNormal) {
      leave = True;
      inferior = (next.xcrossing.detail == NotifyInferior);
    }
  }

  if ((! leave || inferior) && ! isFocused())
    (void) setInputFocus();

  if (screen->resource().doAutoRaise())
    timer->start();
}


void
BlackboxWindow::leaveNotifyEvent(const XCrossingEvent * const /*unused*/) {
  if (! (screen->resource().isSloppyFocus() &&
         screen->resource().doAutoRaise()))
    return;

  if (timer->isTiming())
    timer->stop();
}


#ifdef    SHAPE
void BlackboxWindow::shapeEvent(const XEvent * const /*unused*/)
{ if (client.state.shaped) configureShape(); }
#endif // SHAPE


bool BlackboxWindow::validateClient(void) const {
  XSync(blackbox->XDisplay(), False);

  XEvent e;
  if (XCheckTypedWindowEvent(blackbox->XDisplay(), client.window,
                             DestroyNotify, &e) ||
      XCheckTypedWindowEvent(blackbox->XDisplay(), client.window,
                             UnmapNotify, &e)) {
    XPutBackEvent(blackbox->XDisplay(), &e);

    return False;
  }

  return True;
}


/*
 *
 */
void BlackboxWindow::restore(bool remap) {
  XChangeSaveSet(blackbox->XDisplay(), client.window, SetModeDelete);
  XSelectInput(blackbox->XDisplay(), client.window, NoEventMask);
  XSelectInput(blackbox->XDisplay(), frame.plate, NoEventMask);

  bool reparent = False;
  XEvent ev;
  if (XCheckTypedWindowEvent(blackbox->XDisplay(), client.window,
                             ReparentNotify, &ev)) {
    reparent = True;
    remap = True;
  }

  // do not leave a shaded window as an icon unless it was an icon
  if (client.state.shaded && ! client.state.iconic)
    client.current_state = NormalState;

  // remove the wm hints unless the window is being remapped
  setState(client.current_state);

  restoreGravity(client.rect);

  XGrabServer(blackbox->XDisplay());

  XUnmapWindow(blackbox->XDisplay(), frame.window);
  XUnmapWindow(blackbox->XDisplay(), client.window);

  XSetWindowBorderWidth(blackbox->XDisplay(), client.window, client.old_bw);
  XMoveWindow(blackbox->XDisplay(), client.window,
              client.rect.x() - frame.rect.x(),
              client.rect.y() - frame.rect.y());

  XUngrabServer(blackbox->XDisplay());

  if (! reparent) {
    // according to the ICCCM - if the client doesn't reparent to
    // root, then we have to do it for them
    XReparentWindow(blackbox->XDisplay(), client.window,
                    screen->screenInfo().rootWindow(),
                    client.rect.x(), client.rect.y());
  }

  if (remap) XMapWindow(blackbox->XDisplay(), client.window);
}


// timer for autoraise
void BlackboxWindow::timeout(bt::Timer *)
{ screen->raiseWindow(this); }


/*
 * Set the sizes of all components of the window frame
 * (the window decorations).
 * These values are based upon the current style settings and the client
 * window's dimensions.
 */
void BlackboxWindow::upsize(void) {
  if (client.decorations & WindowDecorationBorder) {
    frame.border_w = screen->resource().borderWidth();
    frame.mwm_border_w = (!isTransient()) ? frame.style->frame_width : 0;
  } else {
    frame.mwm_border_w = frame.border_w = 0;
  }

  frame.margin.top = frame.margin.bottom =
    frame.margin.left = frame.margin.right =
    frame.border_w + frame.mwm_border_w;

  if (client.decorations & WindowDecorationTitlebar)
    frame.margin.top += frame.border_w + frame.style->title_height;

  if (client.decorations & WindowDecorationHandle)
    frame.margin.bottom += frame.border_w + frame.style->handle_height;

  /*
    We first get the normal dimensions and use this to define the inside_w/h
    then we modify the height if shading is in effect.
    If the shade state is not considered then frame.rect gets reset to the
    normal window size on a reconfigure() call resulting in improper
    dimensions appearing in move/resize and other events.
  */
  unsigned int
    height = client.rect.height() + frame.margin.top + frame.margin.bottom,
    width = client.rect.width() + frame.margin.left + frame.margin.right;

  frame.inside_w = width - (frame.border_w * 2);
  frame.inside_h = height - (frame.border_w * 2);

  if (client.state.shaded)
    height = frame.style->title_height + (frame.border_w * 2);
  frame.rect.setSize(width, height);
}

/*
 * show the geometry of the window based on rectangle r.
 * The logical width and height are used here.  This refers to the user's
 * perception of the window size (for example an xterm resizes in cells,
 * not in pixels).  No extra work is needed if there is no difference between
 * the logical and actual dimensions.
 */
void BlackboxWindow::showGeometry(const bt::Rect &r) const {
  unsigned int w = r.width(), h = r.height();

  // remove the window frame
  w -= frame.margin.left + frame.margin.right;
  h -= frame.margin.top + frame.margin.bottom;

  if (client.normal_hint_flags & PResizeInc) {
    if (client.normal_hint_flags & (PMinSize|PBaseSize)) {
      w -= (client.base_width) ? client.base_width : client.min_width;
      h -= (client.base_height) ? client.base_height : client.min_height;
    }

    w /= client.width_inc;
    h /= client.height_inc;
  }

  screen->showGeometry(w, h);
}


/*
 * Calculate the size of the client window and constrain it to the
 * size specified by the size hints of the client window.
 *
 * The physical geometry is placed into frame.changing_{x,y,width,height}.
 * Physical geometry refers to the geometry of the window in pixels.
 */
void BlackboxWindow::constrain(Corner anchor) {
  // frame.changing represents the requested frame size, we need to
  // strip the frame margin off and constrain the client size
  frame.changing.
    setCoords(frame.changing.left() + static_cast<signed>(frame.margin.left),
              frame.changing.top() + static_cast<signed>(frame.margin.top),
              frame.changing.right() - static_cast<signed>(frame.margin.right),
              frame.changing.bottom() -
              static_cast<signed>(frame.margin.bottom));

  unsigned int dw = frame.changing.width(), dh = frame.changing.height();
  const unsigned int base_width = (client.base_width) ? client.base_width :
                                                        client.min_width,
                     base_height = (client.base_height) ? client.base_height :
                                                          client.min_height;

  // constrain to min and max sizes
  if (dw < client.min_width) dw = client.min_width;
  if (dh < client.min_height) dh = client.min_height;
  if (dw > client.max_width) dw = client.max_width;
  if (dh > client.max_height) dh = client.max_height;

  assert(dw >= base_width && dh >= base_height);

  // fit to size increments
  if (client.normal_hint_flags & PResizeInc) {
    dw = (((dw - base_width) / client.width_inc) * client.width_inc)
         + base_width;
    dh = (((dh - base_height) / client.height_inc) * client.height_inc)
         + base_height;
  }

  /*
   * honor aspect ratios (based on twm which is based on uwm)
   *
   * The math looks like this:
   *
   * minAspectX    dwidth     maxAspectX
   * ---------- <= ------- <= ----------
   * minAspectY    dheight    maxAspectY
   *
   * If that is multiplied out, then the width and height are
   * invalid in the following situations:
   *
   * minAspectX * dheight > minAspectY * dwidth
   * maxAspectX * dheight < maxAspectY * dwidth
   *
   */
  if (client.normal_hint_flags & PAspect) {
    unsigned int delta;
    const unsigned int min_asp_x = client.min_aspect_x,
                       min_asp_y = client.min_aspect_y,
                       max_asp_x = client.max_aspect_x,
                       max_asp_y = client.max_aspect_y,
                       w_inc = client.width_inc,
                       h_inc = client.height_inc;
    if (min_asp_x * dh > min_asp_y * dw) {
      delta = ((min_asp_x * dh / min_asp_y - dw) * w_inc) / w_inc;
      if (dw + delta <= client.max_width) {
        dw += delta;
      } else {
        delta = ((dh - (dw * min_asp_y) / min_asp_x) * h_inc) / h_inc;
        if (dh - delta >= client.min_height) dh -= delta;
      }
    }
    if (max_asp_x * dh < max_asp_y * dw) {
      delta = ((max_asp_y * dw / max_asp_x - dh) * h_inc) / h_inc;
      if (dh + delta <= client.max_height) {
        dh += delta;
      } else {
        delta = ((dw - (dh * max_asp_x) / max_asp_y) * w_inc) / w_inc;
        if (dw - delta >= client.min_width) dw -= delta;
      }
    }
  }

  frame.changing.setSize(dw, dh);

  // add the frame margin back onto frame.changing
  frame.changing.setCoords(frame.changing.left() - frame.margin.left,
                           frame.changing.top() - frame.margin.top,
                           frame.changing.right() + frame.margin.right,
                           frame.changing.bottom() + frame.margin.bottom);

  // move frame.changing to the specified anchor
  int dx = frame.rect.right() - frame.changing.right();
  int dy = frame.rect.bottom() - frame.changing.bottom();

  switch (anchor) {
  case TopLeft:
    // nothing to do
    break;

  case TopRight:
    frame.changing.setPos(frame.changing.x() + dx, frame.changing.y());
    break;

  case BottomLeft:
    frame.changing.setPos(frame.changing.x(), frame.changing.y() + dy);
    break;

  case BottomRight:
    frame.changing.setPos(frame.changing.x() + dx, frame.changing.y() + dy);
    break;
  }
}
