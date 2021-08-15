/*-
    * Copyright (c) 1999/2000/2001 Thomas Runge (runge@rostock.zgdv.de)
    * All rights reserved.
    *
    * Redistribution and use in source and binary forms, with or without
    * modification, are permitted provided that the following conditions
    * are met:
    * 1. Redistributions of source code must retain the above copyright
    *    notice, this list of conditions and the following disclaimer.
    * 2. Redistributions in binary form must reproduce the above copyright
    *    notice, this list of conditions and the following disclaimer in the
    *    documentation and/or other materials provided with the distribution.
    *
    * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
    * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
    * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
    * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
    * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
    * SUCH DAMAGE.
    *
    */

/*
 * ToDo:
 *       -stats in extra popup window
 *       -saving?
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef linux
#include <getopt.h>
#endif /* linux */
#include <math.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/cursorfont.h>
#include <X11/Xmu/WinUtil.h>
#ifdef MOTIF
#include <Xm/XmAll.h>
#else /* MOTIF */
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Simple.h>
#include <X11/Xaw/Command.h>

#ifdef NeedWidePrototypes
#undef NeedWidePrototypes
#define XAW_HACK
#endif
#include <X11/Xaw/Scrollbar.h>
#ifdef XAW_HACK
#define NeedWidePrototypes
#endif

#endif /* MOTIF */

#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

#ifdef HAS_XPM
#include <X11/xpm.h>
#include "lupe.xpm"
#endif

#include "lupe.xbm"

static char const id[] = "$Id: xlupe Version 1.1 by Thomas Runge (runge@rostock.zgdv.de) $";

#define APPNAME "xlupe"
#define APPCLASS "XLupe"
#define APPTITLE "xlupe"

#define LUPEWIDTH  300
#define LUPEHEIGHT 200

#define NEW_WIN True
#define OLD_WIN False

int _argc;
char **_argv;
Widget toplevel, drawW, formW, sliderW;
Widget switchB, statsB, jumpB, quitB;
XImage* image;
XtAppContext app_con;

#ifdef MOTIF

XmString stop_str, go_str;
Widget big_formW, pic_formW;
Widget big_frameW, pic_frameW;

#else /* MOTIF */

Widget zlabelW;
char *stop_str, *go_str;

#endif /* MOTIF */

unsigned int width, height, zoom;
int winheight, winwidth;
int dspheight, dspwidth;
Display *dsp;
GC gc, ovlgc;
XtWorkProcId wpid;
XtIntervalId counterId;
XtIntervalId limitId;
Pixmap lupe_pm;
Window rwin, window;
Boolean ready, wantzoom, have_shm, changed, debug, pos_fixed;
Pixel bg_pixel;
XShmSegmentInfo shminfo;
unsigned long limit;
int counter;
int ErrorFlag;

typedef struct
{
 char *winname;
 char *vistype;
 int depth;
 Visual *new_visual;
 Visual *this_visual;
 Colormap cmap;
 Window win;
} visinfo;
visinfo vinf, definf;

typedef struct
{
 Dimension width, height;
 Position x, y;
 Boolean set;
} win_attr;
win_attr old_win;

int NewInterface();
void MakeAthenaInterface();
void MakeMotifInterface();
void PrepareForJump();
void GetWinAttribs();
int HandleXError(Display*, XErrorEvent*);
void CalcTables();
int check_for_xshm();
XImage *alloc_xshm_image(int, int, int);
void destroy_xshm_image(XImage*);
void sliderCB(Widget, XtPointer, XtPointer);
Boolean drawCB(XtPointer);
void counterCB(XtPointer, XtIntervalId*);
void limitCB(XtPointer, XtIntervalId*);
#ifdef MOTIF
void resizeCB(Widget, XtPointer, XtPointer);
void exposeCB(Widget, XtPointer, XtPointer);
#else /* MOTIF */
void resizeCB(Widget, XtPointer, XEvent*);
void exposeCB(Widget, XtPointer, XEvent*);
void SetZlabel(unsigned int zoom);
#endif /* MOTIF */
void zoom_8(XImage *newimage, char *data, int width, int height);
void zoom_any(XImage *newimage, char *data, int width, int height);
void usage(char *progname);
Window Select_Window();
int FillVisinfo(Window window);

static String fbres[] =
{
 "*.background:               #c4c4c4",
 "*.shadowThickness:          1",
 "*.font:                     -*-lucida-medium-r-*-*-11-*-*-*-*-*-*-*",
 "*.fontList:                 -*-lucida-medium-r-*-*-11-*-*-*-*-*-*-*",
#ifdef MOTIF
 "*stats.labelString:         stats",
 "*jump.labelString:          jump",
 "*exit.labelString:          quit",
#else /* MOTIF */
 "*stats.label:               stats",
 "*jump.label:                jump",
 "*exit.label:                quit",
#endif /* MOTIF */
 NULL
};

void quitCB(Widget widget, XtPointer clientDaten, XtPointer aufrufDaten)
{
 exit(EXIT_SUCCESS);
}

int HandleXError(Display *dsp, XErrorEvent *event)
{
 char msg[80];

 if(debug)
 {
  XGetErrorText(dsp, event->error_code, msg, 80);
  fprintf(stderr, "Error code %s\n", msg);
 }

 ErrorFlag = 1;
 return 0;
}

/*
 * Check if the X Shared Memory extension is available.
 * Return:  0 = not available
 *          1 = shared XImage support available
 *          2 = shared Pixmap support available also
 */
int check_for_xshm()
{
 int major, minor, ignore;
 Bool pixmaps;

 if(XQueryExtension(dsp, "MIT-SHM", &ignore, &ignore, &ignore))
 {
  if(XShmQueryVersion(dsp, &major, &minor, &pixmaps) == True)
   return(pixmaps==True) ? 2 : 1;
  else
   return 0;
 }
 else
  return 0;
}

/*
 * Allocate a shared memory XImage.
 */
XImage *alloc_xshm_image(int width, int height, int depth)
{
 XImage *img;

 img = XShmCreateImage(dsp, vinf.this_visual, depth, ZPixmap, NULL, &shminfo,
                       width, height);
 if(img == NULL)
 {
  if(debug)
   printf("XShmCreateImage failed!\n");
  return NULL;
 }

 shminfo.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * img->height,
                        IPC_CREAT|0777 );
 if(shminfo.shmid < 0)
 {
  if(debug)
   perror("shmget");
  XDestroyImage(img);
  img = NULL;
  if(debug)
   printf("Shared memory error (shmget), disabling.\n");
  return NULL;
 }

 shminfo.shmaddr = img->data = (char*)shmat(shminfo.shmid, 0, 0);
 if(shminfo.shmaddr == (char *) -1)
 {
  if(debug)
   perror("alloc_xshm_image");
  XDestroyImage(img);
  img = NULL;
  if(debug)
   printf("shmat failed\n");
  return NULL;
 }

 shminfo.readOnly = False;
 ErrorFlag = 0;
 XShmAttach(dsp, &shminfo);
 XSync(dsp, False);

 if(ErrorFlag)
 {
  /* we are on a remote display, this error is normal, don't print it */
  XFlush(dsp);
  ErrorFlag = 0;
  XDestroyImage(img);
  shmdt(shminfo.shmaddr);
  shmctl(shminfo.shmid, IPC_RMID, 0);
  return NULL;
 }

 shmctl(shminfo.shmid, IPC_RMID, 0); /* nobody else needs it */

 return img;
}

void destroy_xshm_image(XImage *img)
{
 XShmDetach(dsp, &shminfo);
 XDestroyImage(img);
 shmdt(shminfo.shmaddr);
}

static XPoint old_p[5];
void btnDownCB(Widget w_, XtPointer clientDaten, XEvent *event)
{
 int x, y;
 unsigned int w, h;

 if(!wantzoom)
  return;

 if(event->xbutton.button != Button1)
  return;

 pos_fixed = False;

 x = event->xbutton.x_root;
 y = event->xbutton.y_root;
 w = (unsigned int) (width / zoom);
 h = (unsigned int) (height / zoom);
 w >>= 1;
 h >>= 1;

 old_p[0].x = x - w;
 old_p[0].y = y - h;
 old_p[1].x = x + w;
 old_p[1].y = old_p[0].y;
 old_p[2].x = old_p[1].x;
 old_p[2].y = y + h;
 old_p[3].x = old_p[0].x;
 old_p[3].y = old_p[2].y;
 old_p[4]   = old_p[0];

 XDrawLines(dsp, vinf.win, ovlgc, old_p, 5, CoordModeOrigin);
}

void btnUpCB(Widget w_, XtPointer clientDaten, XEvent *event)
{
 if(!wantzoom)
  return;

 if(event->xbutton.button == Button3)
  pos_fixed = False;

 if(event->xbutton.button != Button1)
  return;

 XDrawLines(dsp, vinf.win, ovlgc, old_p, 5, CoordModeOrigin);

 pos_fixed = True;
}

void btnMotionCB(Widget w_, XtPointer clientDaten, XEvent *event)
{
 int x, y;
 unsigned int w, h;
 XPoint p[5];

 if(!wantzoom)
  return;

 x = event->xbutton.x_root;
 y = event->xbutton.y_root;
 w = (unsigned int) (width / zoom);
 h = (unsigned int) (height / zoom);
 w >>= 1;
 h >>= 1;

 p[0].x = x - w;
 p[0].y = y - h;
 p[1].x = x + w;
 p[1].y = p[0].y;
 p[2].x = p[1].x;
 p[2].y = y + h;
 p[3].x = p[0].x;
 p[3].y = p[2].y;
 p[4]   = p[0];

 XDrawLines(dsp, vinf.win, ovlgc, old_p, 5, CoordModeOrigin);
 XDrawLines(dsp, vinf.win, ovlgc, p, 5, CoordModeOrigin);

 memcpy(&old_p, &p, 5*sizeof(XPoint));
}

void sliderCB(Widget w, XtPointer clientDaten, XtPointer aufrufDaten)
{
#ifdef MOTIF
 XmScaleCallbackStruct *sccb;

 sccb = (XmScaleCallbackStruct*) aufrufDaten;

 if(zoom != sccb->value)
  zoom = sccb->value;
#else /* MOTIF */
 zoom = 10 - (int)(10*(*(float*)aufrufDaten));
 if(!zoom)
  zoom = 1;

 SetZlabel(zoom);
#endif /* MOTIF */

 return;
}

void counterCB(XtPointer clientDaten, XtIntervalId* id)
{
 printf("frame count: %.2f fps\n", counter/10.);
 counter = 0;
 counterId = XtAppAddTimeOut(app_con, 10000, counterCB, (XtPointer)NULL);
}

void limitCB(XtPointer clientDaten, XtIntervalId* id)
{
 ready = True;
 limitId = XtAppAddTimeOut(app_con, limit, limitCB, (XtPointer)NULL);
}

Boolean drawCB(XtPointer clientDaten)
{
 XImage *newimage;
 Window dummy_window1, dummy_window2;
 int x, y;
 static int p_x, p_y, r_x, r_y;
 unsigned int dummy_int;
 unsigned int w, h;
 static size_t data_size=0;
 static char* data=NULL;

 if(limit)
 {
  if(!ready)
   return(False);
  else
   ready = False;
 }

 if(!pos_fixed)
  XQueryPointer(dsp, vinf.win, &dummy_window1, &dummy_window2,
               &r_x, &r_y, &p_x, &p_y, &dummy_int);

 w = (unsigned int) (width / zoom);
 h = (unsigned int) (height / zoom);

 x = p_x - (w>>1);
 y = p_y - (h>>1);

 if(x<0)
  x = 0;
 if(y<0)
  y = 0;

 if(x+w>winwidth)
  x = winwidth-w;
 if(y+h>winheight)
  y = winheight-h;

 if(vinf.win != rwin)
 {
  if(r_x+w>dspwidth)
   x = dspwidth-w-r_x+p_x;
  if(r_y+h>dspheight)
   y = dspheight-h-r_y+p_y;
 }

 if(w>winwidth)
  w = winwidth;
 if(h>winheight)
  h = winheight;

 newimage = XGetImage(dsp, vinf.win, x, y, w, h, AllPlanes, ZPixmap);

 if(ErrorFlag)
 {
  ErrorFlag = 0;
  return(False);
 }

 if(newimage == NULL)
 {
  if(debug)
   fprintf(stderr, "Couldn't get the new image...\n");
  return(False);
 }

 if(changed)
 {
  if(have_shm)
  {
   if(image)
    destroy_xshm_image(image);
   image = alloc_xshm_image(width, height, newimage->depth);
   if(image == NULL)
   {
    if(debug)
     printf("remote display, disabling shared memory.\n");
    have_shm = False;
    goto goon;
   }
   data_size = image->bytes_per_line * image->height;
   data = image->data;
  }
  else
  {
   if(image)
    XFree(image);
   image = XCreateImage(dsp, vinf.this_visual, newimage->depth,
                        ZPixmap, 0, 0, width, height,
                        newimage->bitmap_pad, 0);
   if(data)
    XtFree(data);
   data_size = image->bytes_per_line * image->height;
   data = XtMalloc(data_size);
  }
  changed = False;
 }

 if(vinf.depth == 8)
  memset(data, bg_pixel, data_size);
 else
  memset(data, WhitePixelOfScreen(XtScreen(toplevel)), data_size);

 if(debug)
  counter ++;

 if(zoom == 1)
 {
  memcpy(data, newimage->data, data_size);
 }
 else
 {
  /* zoom_8 is appr. 1/3 faster then zoom_any */
  if(vinf.depth == 8)
   zoom_8(newimage, data, w, h);
  else
   zoom_any(newimage, data, w, h);
 }

 if(have_shm)
 {
  XShmPutImage(dsp, window, gc, image, 0, 0, 0, 0, width, height, False);
 }
 else
 {
  image->data = data;
  XPutImage(dsp, window, gc, image, 0, 0, 0, 0, width, height);
 }

goon:

 XDestroyImage(newimage);

 if(!wantzoom)
  return(True);

 return(False);
}

void zoom_8(XImage *newimage, char *data, int w, int h)
{
 char c;
 int x, y, z, xz, yz;

 xz = -zoom;
 for(x = 0; x < w; x++)
 {
  xz += zoom;
  yz = -zoom;
  for(y = 0; y < h; y++)
  {
   yz += zoom;
   c = newimage->data[x+y*newimage->bytes_per_line];
   for(z=0; z<zoom; z++)
    memset(&data[image->bytes_per_line*(yz+z)+xz], (int)c, zoom);
  }
 }
}

void zoom_any(XImage *newimage, char *data, int w, int h)
{
 char *c;
 int u, v, x, y, z, xz, yz, bpp;

 bpp = newimage->bits_per_pixel>>3;

 xz = -zoom;
 for(x = 0; x < w; x++)
 {
  xz += zoom;
  yz = -zoom;
  for(y = 0; y < h; y++)
  {
   yz += zoom;
   c = &(newimage->data[bpp*x+y*newimage->bytes_per_line]);
   for(z=0; z<zoom; z++)
   {
    u = image->bytes_per_line*(yz+z)+xz*bpp;
    for(v=0; v<zoom; v++)
    memcpy(&data[u+v*bpp], c, bpp);
   }
  }
 }
}

void switchCB(Widget w, XtPointer clientDaten, XtPointer aufrufDaten)
{
 wantzoom=!wantzoom;
 if(wantzoom)
 {
#ifdef MOTIF
  XtVaSetValues(w, XmNlabelString, stop_str, NULL);
#else /* MOTIF */
  XtVaSetValues(w, XtNlabel, stop_str, NULL);
  XtSetSensitive(zlabelW, True);
#endif /* MOTIF */
  XtSetSensitive(sliderW, True);
  wpid = XtAppAddWorkProc(app_con, drawCB, (XtPointer)NULL);
 }
 else
 {
#ifdef MOTIF
  XtVaSetValues(w, XmNlabelString, go_str, NULL);
#else /* MOTIF */
  XtVaSetValues(w, XtNlabel, go_str, NULL);
  XtSetSensitive(zlabelW, False);
#endif /* MOTIF */
  XtSetSensitive(sliderW, False);
 }
 return;
}

void statsCB(Widget w, XtPointer clientDaten, XtPointer aufrufDaten)
{
 printf("window:     %s\n", vinf.winname);
 printf("new depth:  %d\n", vinf.depth);
 printf("new visual: %s\n", vinf.vistype);
 printf("winwidth:   %d\n", winwidth);
 printf("winheight:  %d\n", winheight);
 printf("dspwidth:   %d\n", dspwidth);
 printf("dspheight:  %d\n", dspheight);
}

void jumpCB(Widget w, XtPointer clientDaten, XtPointer aufrufDaten)
{
 Window win;

 win = Select_Window();
 if(!win)
 {
  fprintf(stderr, "Can't get window.\n");
  return;
 }

 if(FillVisinfo(win) == NEW_WIN)
 {
  GetWinAttribs();
  PrepareForJump();
  NewInterface();
 }
 else
 {
  printf("same visual, colormap and depth. not jumping.\n");
 }
}

int FillVisinfo(Window win)
{
 XWindowAttributes winattr;
 char *name;

 if(!win)
  return(OLD_WIN);

 if(!XGetWindowAttributes(dsp, win, &winattr))
 {
  fprintf(stderr, "Can't get window attributes.\n");
  return(OLD_WIN);
 }

 if(vinf.winname)
  XtFree(vinf.winname);

 if(XFetchName(dsp, win, &name))
  vinf.winname = XtNewString(name);
 else
 {
  if(win == rwin)
   vinf.winname = XtNewString(" (root window)");
  else
   vinf.winname = XtNewString(" (has no name)");
 }
 XFree(name);

 if(winattr.depth    == vinf.depth &&
    winattr.visual   == vinf.this_visual &&
    winattr.colormap == vinf.cmap)
 {
  return(OLD_WIN);
 }

 vinf.depth      = winattr.depth;
 vinf.new_visual = winattr.visual;
 vinf.cmap       = winattr.colormap;

 if(winattr.depth    == definf.depth &&
    winattr.visual   == definf.this_visual &&
    winattr.colormap == definf.cmap)
 {
  vinf.win = rwin;
 }
 else
 {
  vinf.win = win;
  winheight = winattr.height;
  winwidth  = winattr.width;
 }

 if(vinf.vistype)
  XtFree(vinf.vistype);

 switch(winattr.visual->class)
 {
  case StaticGray:  vinf.vistype = XtNewString("StaticGray");
                     break;
  case GrayScale:   vinf.vistype = XtNewString("GrayScale");
                     break;
  case StaticColor: vinf.vistype = XtNewString("StaticColor");
                     break;
  case PseudoColor: vinf.vistype = XtNewString("PseudoColor");
                     break;
  case TrueColor:   vinf.vistype = XtNewString("TrueColor");
                     break;
  case DirectColor: vinf.vistype = XtNewString("DirectColor");
                     break;
  default:          vinf.vistype = XtNewString(" *unknown* visualtype ?!?");
                     break;
 }

 return(NEW_WIN);
}

#ifdef MOTIF
void exposeCB(Widget widget, XtPointer clientDaten, XtPointer aufrufDaten)
#else /* MOTIF */
void exposeCB(Widget widget, XtPointer clientDaten, XEvent *event)
#endif /* MOTIF */
{
 if(debug)
  printf("expose.\n");

 if(window && image && !changed)
   XPutImage(XtDisplay(widget), window, gc, image, 0, 0, 0, 0, width, height);

 return;
}

#ifdef MOTIF
void resizeCB(Widget widget, XtPointer clientDaten, XtPointer aufrufDaten)
#else /* MOTIF */
void resizeCB(Widget widget, XtPointer clientDaten, XEvent *event)
#endif /* MOTIF */
{
 Dimension w, h;

 if(debug)
  printf("resize.\n");

 XtVaGetValues(widget, XtNwidth,  &w,
                       XtNheight, &h,
                       NULL);
 width  = (unsigned int) w;
 height = (unsigned int) h;
 changed = True;

 return;
}

void iconifyCB(Widget w, XtPointer clientDaten, XEvent *event)
{
 static Boolean old_style = True;

 if(event->type == MapNotify)
 {
  wantzoom = old_style;
  if(wantzoom)
   wpid = XtAppAddWorkProc(app_con, drawCB, (XtPointer)NULL);
  if(debug)
   printf("normal\n");
 }
 if(event->type == UnmapNotify)
 {
  old_style = wantzoom;
  wantzoom = False;
  if(debug)
   printf("iconified\n");
 }
 return;
}

void usage(char *progname)
{
 printf("usage: %s -h -d -s -l <fps>\n", progname);
 printf(" 'h' shows this little help\n");
 printf(" 'b' -> switch debug info on\n");
 printf(" 's' -> switch shared memory off\n");
 printf(" 'l' -> limit to <fps> frames per second\n");

 exit(EXIT_FAILURE);
}

int hasMultipleVisuals()
{
 XVisualInfo viproto;
 XVisualInfo *vip;
 int nvi;

 viproto.screen = DefaultScreen(dsp);
 vip = XGetVisualInfo(dsp, VisualScreenMask, &viproto, &nvi);
 if(debug)
  printf("visual count: %d\n", vinf.depth);

 return(nvi > 1);
}

int NewInterface()
{
 Dimension minW, minH;
 changed = True;

 if(vinf.depth)
 {
  if(old_win.set)
   toplevel = XtVaAppCreateShell(APPNAME, APPCLASS,
        sessionShellWidgetClass, dsp,
                XtNdepth,    vinf.depth,
                XtNcolormap, vinf.cmap,
                XtNvisual,   vinf.new_visual,
                XtNwidth,    old_win.width,
                XtNheight,   old_win.height,
                XtNx,        old_win.x,
                XtNy,        old_win.y,
                NULL);
  else
   toplevel = XtVaAppCreateShell(APPNAME, APPCLASS,
        sessionShellWidgetClass, dsp,
               XtNdepth,    vinf.depth,
               XtNcolormap, vinf.cmap,
               XtNvisual,   vinf.new_visual,
               NULL);
 }
 else
 {
  toplevel = XtVaAppCreateShell(APPNAME, APPCLASS,
        sessionShellWidgetClass, dsp, NULL);
 }

 XtVaSetValues(toplevel,
               XtNallowShellResize, False,
               XtNtitle,            APPTITLE,
               XtNiconPixmap,       lupe_pm,
               NULL);

#ifdef MOTIF
 MakeMotifInterface();
#else /* MOTIF */
 MakeAthenaInterface();
#endif /* MOTIF */

 XtVaGetValues(drawW, XtNwidth,  &minW,
                      XtNheight, &minH,
                      XtNdepth,  &(vinf.depth),
                      NULL);
 if(debug)
  printf("using depth %d\n", vinf.depth);

 width  = (int)minW;
 height = (int)minH;

 XtAddEventHandler(toplevel, StructureNotifyMask, False,
                   (XtEventHandler)iconifyCB, (XtPointer)NULL);

 wpid = XtAppAddWorkProc(app_con, drawCB, (XtPointer)NULL);

 if(debug)
  counterId = XtAppAddTimeOut(app_con, 10000, counterCB, (XtPointer)NULL);

 if(limit)
  limitId = XtAppAddTimeOut(app_con, limit, limitCB, (XtPointer)NULL);

 vinf.this_visual = vinf.new_visual;

 XtRealizeWidget(toplevel);

 window = XtWindow(drawW);
 XtVaGetValues(toplevel, XtNbackground, &bg_pixel, NULL);

 XFreeGC(dsp, gc);
 gc = XCreateGC(dsp, window, 0, NULL);

 XtAddEventHandler(drawW, ButtonPressMask, False,
                   (XtEventHandler)btnDownCB, (XtPointer)NULL);
 XtAddEventHandler(drawW, ButtonReleaseMask, False,
                   (XtEventHandler)btnUpCB, (XtPointer)NULL);
 XtAddEventHandler(drawW, Button1MotionMask, False,
                   (XtEventHandler)btnMotionCB, (XtPointer)NULL);

 return(True);
}

#ifdef MOTIF
void MakeMotifInterface()
{
 Atom wmDeleteAtom;

 formW =
   XtVaCreateManagedWidget("kam_mag_main_form",
                           xmFormWidgetClass,
                           toplevel,
                           NULL);

 big_frameW =      
      XtVaCreateManagedWidget("big_frame",
                              xmFrameWidgetClass,
                              formW,
                              XmNshadowType,       XmSHADOW_ETCHED_IN,
                              XmNrightAttachment,  XmATTACH_FORM,
                              XmNrightOffset,      2,  
                              XmNleftAttachment,   XmATTACH_FORM,
                              XmNleftOffset,       2,
                              XmNtopAttachment,    XmATTACH_FORM,
                              XmNtopOffset,        2,
                              XmNbottomAttachment, XmATTACH_FORM,
                              XmNbottomOffset,     2,
                              NULL);

 big_formW =
   XtVaCreateManagedWidget("kam_mag_big_form",
                           xmFormWidgetClass,
                           big_frameW,
                           NULL);

 switchB =
   XtVaCreateManagedWidget("switch",
                           xmPushButtonWidgetClass,
                           big_formW,
                           XmNlabelString,      stop_str,
                           XmNmarginWidth,      5,
                           XmNleftAttachment,   XmATTACH_FORM,
                           XmNleftOffset,       5,
                           XmNbottomAttachment, XmATTACH_FORM,
                           XmNbottomOffset,     5,
                           NULL);
 XtAddCallback(switchB, XmNactivateCallback, switchCB, (XtPointer)NULL);

 statsB =
   XtVaCreateManagedWidget("stats",
                           xmPushButtonWidgetClass,
                           big_formW,
                           XmNleftAttachment,   XmATTACH_WIDGET,
                           XmNleftWidget,       switchB,
                           XmNbottomAttachment, XmATTACH_FORM,
                           XmNbottomOffset,     5,
                           NULL);
 XtAddCallback(statsB, XmNactivateCallback, statsCB, (XtPointer)NULL);

 if(hasMultipleVisuals())
 {
  jumpB =
   XtVaCreateManagedWidget("jump",
                           xmPushButtonWidgetClass,
                           big_formW,
                           XmNleftAttachment,   XmATTACH_WIDGET,
                           XmNleftWidget,       statsB,
                           XmNbottomAttachment, XmATTACH_FORM,
                           XmNbottomOffset,     5,
                           NULL);
  XtAddCallback(jumpB, XmNactivateCallback, jumpCB, (XtPointer)NULL);
 }

 quitB =
   XtVaCreateManagedWidget("exit",
                           xmPushButtonWidgetClass,
                           big_formW,
                           XmNrightAttachment,  XmATTACH_FORM,
                           XmNrightOffset,      5,
                           XmNmarginWidth,      10,
                           XmNbottomAttachment, XmATTACH_FORM,
                           XmNbottomOffset,     5,
                           NULL);
 XtAddCallback(quitB, XmNactivateCallback, quitCB, (XtPointer)NULL);

 sliderW =
   XtVaCreateManagedWidget("slider",
                           xmScaleWidgetClass,
                           big_formW,
                           XmNdecimalPoints,    0,
                           XmNmaximum,          10,
                           XmNminimum,          1,
                           XmNvalue,            zoom,
                           XmNshowValue,        True,
                           XmNscaleMultiple,    1,
                           XmNrightAttachment,  XmATTACH_FORM,
                           XmNrightOffset,      5,
                           XmNtopAttachment,    XmATTACH_FORM,
                           XmNtopOffset,        5,
                           XmNbottomAttachment, XmATTACH_WIDGET,
                           XmNbottomWidget,     quitB,
                           XmNbottomOffset,     5,
                           NULL);
 XtAddCallback(sliderW, XmNdragCallback, sliderCB, (XtPointer)NULL);

 pic_frameW =
   XtVaCreateManagedWidget("pic_frame",
                           xmFrameWidgetClass,
                           big_formW,
                           XmNshadowType,       XmSHADOW_ETCHED_IN,
                           XmNrightAttachment,  XmATTACH_WIDGET,
                           XmNrightWidget,      sliderW,
                           XmNrightOffset,      5,
                           XmNleftAttachment,   XmATTACH_FORM,
                           XmNleftOffset,       5,
                           XmNtopAttachment,    XmATTACH_FORM,
                           XmNtopOffset,        5,
                           XmNbottomAttachment, XmATTACH_WIDGET,
                           XmNbottomWidget,     switchB,
                           XmNbottomOffset,     5,
                           NULL);

 pic_formW =
   XtVaCreateManagedWidget("kam_mag_pig_form",
                           xmFormWidgetClass,
                           pic_frameW,
                           NULL);

 drawW =
   XtVaCreateManagedWidget("draw",
                           xmDrawingAreaWidgetClass,
                           pic_formW,
                           XmNrightAttachment,  XmATTACH_FORM,
                           XmNrightOffset,      2,
                           XmNleftAttachment,   XmATTACH_FORM,
                           XmNleftOffset,       2,
                           XmNtopAttachment,    XmATTACH_FORM,
                           XmNtopOffset,        2,
                           XmNbottomAttachment, XmATTACH_FORM,
                           XmNbottomOffset,     2,
                           NULL);

 XtVaSetValues(sliderW, XmNwidth,  30, NULL);

 XtVaSetValues(drawW, XmNwidth,  LUPEWIDTH,
                      XmNheight, LUPEHEIGHT,
                      NULL);

 XtAddCallback(drawW, XmNexposeCallback, exposeCB, (XtPointer)NULL);
 XtAddCallback(drawW, XmNresizeCallback, resizeCB, (XtPointer)NULL);

 wmDeleteAtom = XmInternAtom(dsp, "WM_DELETE_WINDOW", False);
 XmAddWMProtocolCallback(toplevel, wmDeleteAtom, quitCB, (XtPointer)NULL);
}

#else /* MOTIF */

void MakeAthenaInterface()
{
 formW =
   XtVaCreateManagedWidget("kam_mag_main_form",
                           formWidgetClass,
                           toplevel,
                           NULL);

 drawW =
   XtVaCreateManagedWidget("draw",
                           simpleWidgetClass,
                           formW,
                           XtNtop,    XawChainTop,
                           XtNright,  XawChainRight,
                           XtNleft,   XawChainLeft,
                           XtNbottom, XawChainBottom,
                           XtNwidth,  LUPEWIDTH,
                           XtNheight, LUPEHEIGHT,
                           NULL);

 XtAddEventHandler(drawW, ExposureMask, False,
                   (XtEventHandler)exposeCB, (XtPointer)NULL);
 XtAddEventHandler(drawW, StructureNotifyMask, False,
                   (XtEventHandler)resizeCB, (XtPointer)NULL);

 sliderW =
   XtVaCreateManagedWidget("slider",
                           scrollbarWidgetClass,
                           formW,
                           XtNfromHoriz, drawW,
                           XtNtop,       XawChainTop,
                           XtNbottom,    XawChainBottom,
                           XtNright,     XawChainRight,
                           XtNleft,      XawChainRight,
                           XtNheight,    LUPEHEIGHT,
                           NULL);
 XtAddCallback(sliderW, XtNjumpProc, sliderCB, (XtPointer)NULL);

 XawScrollbarSetThumb(sliderW, 1. - zoom/10., -1);

 zlabelW =
   XtVaCreateManagedWidget("zlabel",
                           labelWidgetClass,
                           formW,
                           XtNfromHoriz, drawW,
                           XtNfromVert,  sliderW,
                           XtNtop,       XawChainBottom,
                           XtNbottom,    XawChainBottom,
                           NULL);
 SetZlabel(zoom);

 switchB =
   XtVaCreateManagedWidget("switch",
                           commandWidgetClass,
                           formW,
                           XtNlabel,    stop_str,
                           XtNfromVert, drawW,
                           XtNright,    XawChainLeft,
                           XtNleft,     XawChainLeft,
                           XtNbottom,   XawChainBottom,
                           XtNtop,      XawChainBottom,
                           NULL);
 XtAddCallback(switchB, XtNcallback, switchCB, (XtPointer)NULL);

 statsB =
   XtVaCreateManagedWidget("stats",
                           commandWidgetClass,
                           formW,
                           XtNfromHoriz, switchB,
                           XtNfromVert,  drawW,
                           XtNright,     XawChainLeft,
                           XtNleft,      XawChainLeft,
                           XtNbottom,    XawChainBottom,
                           XtNtop,       XawChainBottom,
                           NULL);
 XtAddCallback(statsB, XtNcallback, statsCB, (XtPointer)NULL);

 if(hasMultipleVisuals())
 {
  jumpB =
   XtVaCreateManagedWidget("jump",
                           commandWidgetClass,
                           formW,
                           XtNfromHoriz, statsB,
                           XtNfromVert,  drawW,
                           XtNright,     XawChainLeft,
                           XtNleft,      XawChainLeft,
                           XtNbottom,    XawChainBottom,
                           XtNtop,       XawChainBottom,
                           NULL);
  XtAddCallback(jumpB, XtNcallback, jumpCB, (XtPointer)NULL);
 }
 else
  jumpB = statsB;

 quitB =
   XtVaCreateManagedWidget("exit",
                           commandWidgetClass,
                           formW,
                           XtNfromHoriz, jumpB,
                           XtNfromVert,  drawW,
                           XtNleft,      XawChainLeft,
                           XtNright,     XawChainLeft,
                           XtNbottom,    XawChainBottom,
                           XtNtop,       XawChainBottom,
                           NULL);
 XtAddCallback(quitB, XtNcallback, quitCB, (XtPointer)NULL);
}
#endif /* MOTIF */

void GetWinAttribs()
{
 XtVaGetValues(toplevel, XtNx, &old_win.x,
                         XtNy, &old_win.y,
                         XtNwidth,  &old_win.width,
                         XtNheight, &old_win.height,
                         NULL);
 old_win.set = True;
}

void PrepareForJump()
{
 XtUnmapWidget(toplevel);

 XtRemoveWorkProc(wpid);

 if(debug)
  XtRemoveTimeOut(counterId);

 if(limit)
  XtRemoveTimeOut(limitId);

 XtDestroyWidget(toplevel);
}

#ifndef MOTIF
void SetZlabel(unsigned int zoom)
{
 char value[4];
 sprintf(value, "%d", zoom);
 XtVaSetValues(zlabelW, XtNlabel, value, NULL);
}
#endif /* MOTIF */

int main(int argc, char** argv)
{
 Boolean wants_shm;
 unsigned long XORvalue;
 int i;

 _argc = argc;
 _argv = argv;
 image = NULL;
 zoom = 2;
 changed = True;
 wantzoom = True;
 pos_fixed = False;
 counter = 0;
 wants_shm = True;
 limit = 0;
 vinf.winname = NULL;
 vinf.depth = 0;
 vinf.vistype = NULL;
 old_win.set = False;

#ifdef MOTIF
 stop_str = XmStringCreateLtoR("stop", XmSTRING_DEFAULT_CHARSET);
 go_str   = XmStringCreateLtoR("cont", XmSTRING_DEFAULT_CHARSET);
#else /* MOTIF */
 stop_str = XtNewString("stop");
 go_str   = XtNewString("cont");
#endif /* MOTIF */

 XtToolkitInitialize();
 app_con = XtCreateApplicationContext();
 XtAppSetFallbackResources(app_con, fbres);
 dsp = XtOpenDisplay(app_con, NULL, APPNAME, APPCLASS, NULL, 0, &_argc, _argv);
 if(!dsp)
 {
  printf("can't open display, exiting...\n");
  usage(_argv[0]);
 }
 rwin = DefaultRootWindow(dsp);
 vinf.new_visual = DefaultVisual(dsp, DefaultScreen(dsp));
 
 definf.depth       = DefaultDepth(dsp, DefaultScreen(dsp));
 definf.this_visual = DefaultVisual(dsp, DefaultScreen(dsp));
 definf.cmap        = DefaultColormap(dsp, DefaultScreen(dsp));

 while((i = getopt(argc, argv, "bhsl:")) != EOF)
  switch(i)
  {
   case 'b':
              debug = True;
              printf("Debugging mode.\n");
             break;
   case 's':
              wants_shm = False;
             break;
   case 'l':
              limit = 1000/strtol(optarg, (char **)NULL, 10);
             break;
   case 'h':
   case '?':
   default:
             usage(argv[0]);
  }

 XSetErrorHandler(HandleXError);

#ifdef HAS_XPM
 if(XpmCreatePixmapFromData(dsp, rwin, lupe_xpm,
                            &lupe_pm, NULL, NULL) < XpmSuccess)
#endif /* HAS_XPM */

 lupe_pm = XCreatePixmapFromBitmapData(dsp, rwin,
                          lupe_bits, lupe_width, lupe_height,
                          BlackPixelOfScreen(XtScreen(toplevel)),
                          WhitePixelOfScreen(XtScreen(toplevel)),
                          DefaultDepthOfScreen(XtScreen(toplevel)));

 /* test if XShm available */
 if(!check_for_xshm())
 {
  if(debug)
   printf("shared memory extension not available\n");
  have_shm = False;
 }
 else
 {
  if(debug)
   printf("found shared memory extension\n");
  if(!wants_shm)
  {
   if(debug)
    printf("   but not using it...\n");
   have_shm = False;
  }
  else
   have_shm = True;
 }

 dspheight = winheight = DisplayHeight(dsp, DefaultScreen(dsp));
 dspwidth  = winwidth  = DisplayWidth(dsp, DefaultScreen(dsp));
 XORvalue = (((unsigned long)1) << definf.depth) - 1;

 gc = XCreateGC(dsp, rwin, 0, NULL);
 ovlgc = XCreateGC(dsp, rwin, 0, NULL);
 XSetForeground(dsp, ovlgc, XORvalue);
 XSetFunction(dsp, ovlgc, GXxor);
 XSetSubwindowMode(dsp, ovlgc, IncludeInferiors);

 NewInterface();
 GetWinAttribs();
 FillVisinfo(window);

 XtAppMainLoop(app_con);

 return(True);
}


/*
 * Routine to let user select a window using the mouse
 * from xwininfo(1)
 */

Window Select_Window()
{
 int status;
 Cursor cursor;
 XEvent event;
 Window target_win = None;
 int buttons = 0;

 /* Make the target cursor */
 cursor = XCreateFontCursor(dsp, XC_crosshair);

 /* Grab the pointer using target cursor, letting it room all over */
 status = XGrabPointer(dsp, rwin, False,
            ButtonPressMask|ButtonReleaseMask, GrabModeSync,
            GrabModeAsync, rwin, cursor, CurrentTime);
 if(status != GrabSuccess)
 {
  fprintf(stderr, "Can't grab the mouse.\n");
  return((Window)NULL);
 }

 /* Let the user select a window... */
 while((target_win == None) || (buttons != 0))
 {
  /* allow one more event */
  XAllowEvents(dsp, SyncPointer, CurrentTime);
  XWindowEvent(dsp, rwin, ButtonPressMask|ButtonReleaseMask, &event);
  switch(event.type)
  {
   case ButtonPress:
      if(target_win == None)
      {
       target_win = event.xbutton.subwindow; /* window selected */
       if(target_win == None)
	    target_win = rwin;
      }
      buttons++;
      break;
    case ButtonRelease:
      if(buttons > 0) /* there may have been some down before we started */
       buttons--;
      break;
   }
 }

 XUngrabPointer(dsp, CurrentTime);      /* Done with pointer */

 if(target_win)
 {
   if(target_win != rwin)
    target_win = XmuClientWindow(dsp, target_win);
 }

 return(target_win);
}
