//
//   Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010 Free Software
//   Foundation, Inc
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

// -----------------------------------------------------------------------------
   
/// \page fb_input FB GUI input devices
///
/// The framebuffer GUI supports various input devices through the modern
/// Linux Input Subsystem (/dev/input/event*). Both standard mice and 
/// touchscreen devices are supported.
///
/// Since there can be multiple input devices in /dev/input/ you have to
/// specify which device to use using the 
///  POINTING_DEVICE environment variable for the mouse and
///  KEYBOARD_DEVICE environment variable for the keyboard

/// \page fb_calibration FB GUI Touchscreen Calibration
///
/// The touchscreen drivers (like "usbtouchscreen") provide raw data from the
/// devices. It is up to the user space program to translate this data to
/// screen coordinates. Normally this is done by the X server so this 
/// conversion needs to be done internally by Gnash itself.
///
/// The current implementation uses a very simple 2-point calibration where
/// the first point is at one fifth of the screen width and height and the
/// second point is at the exact opposite part of the screen (at it's lower
/// right). The SWF file calibrate.swf provides a simple graphical reference
/// for 4:3 sized screens (don't use it for other formats!).
/// 
/// With the current preliminary implementation it's a bit uncomfortable to do 
/// the calibration:
/// 
/// 1) starting gnash with DUMP_RAW environment variable will show raw 
///    coordinates on STDOUT:
///      DUMP_RAW=1 gnash calibrate.swf
/// 
/// 2) Keep touching the upper left reference point for a while. You'll get
///    lots of (similar) coordinates printed. Choose a X/Y coordinate pair you 
///    think is the best one (ie. the average) and write it down.
///
/// 3) Do the same for the lower right reference point.
///
/// From now on, start gnash with the TSCALIB enivronment variable set to
/// the coordinates you just found out. Write the coordinates separated by
/// commas (X,Y,X,Y), like this: 
///   TSCALIB=491,1635,1581,639 gnash yourmovie.swf    

///
/// 10-4-2008  N. Coesel
/// Added support for tslib. Tslib is a library which allows to create
/// a stack / cascade of filters to filter and scale touch-screen output.
/// The source doesn't come with much documentation, but writing your
/// own filter based on an existing filter is really easy. Tslib can 
/// deal with old style H3600 style touchscreens and the newer event 
/// interface. See http://tslib.berlios.de/ to get the source. 
///
/// The check_tslib() routine assumes filtering for movement and presses
/// is properly setup. Scaling is supposed to be performed by tslib or
/// the underlying touchscreen driver.

#ifdef HAVE_CONFIG_H
#include "gnashconfig.h"
#endif

#include "GnashSystemIOHeaders.h"
#include "GnashNumeric.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <cerrno>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <csignal>
#include <cstdlib> // getenv

#ifdef HAVE_TSLIB_H
# include <tslib.h>
#endif
#if defined(ENABLE_TSLIB) && !defined(HAVE_TSLIB_H)
# warning "No tslib.h! Disabling touchscreen support"
# undef ENABLE_TSLIB
#endif

#include "gui.h"
#include "fbsup.h"
#include "log.h"
#include "movie_root.h"
#include "RunResources.h"
#include "GnashSleep.h" // for gnashSleep
#include "Renderer.h"

#include <linux/input.h>    // for /dev/input/event*

#ifdef RENDERER_AGG
#include "fb_glue_agg.h"
#endif

#if 0
#ifdef RENDERER_OPENVG
#include "fb_glue_ovg.h"
#endif

// FIXME: this is just to remind us to implement these too
#ifdef RENDERER_GLES1
#include "fb_glue_gles1.h"
#endif

#ifdef RENDERER_GLES2
#include "fb_glue_gles2.h"
#endif
#endif

namespace gnash {

namespace gui {

//---------------

int terminate_request = false;  // global scope to avoid GUI access

std::auto_ptr<Gui> createFBGui(unsigned long windowid, float scale,
                               bool do_loop, RunResources& r)
{
    GNASH_REPORT_FUNCTION;
    return std::auto_ptr<Gui>(new FBGui(windowid, scale, do_loop, r));
}

/// Called on CTRL-C and alike
void
terminate_signal(int /*signo*/) {
    terminate_request = true;
}

FBGui::FBGui(unsigned long xid, float scale, bool loop, RunResources& r)
    : Gui(xid, scale, loop, r),
      _fd(-1),
      _original_vt(-1),
      _original_kd(-1),
      _own_vt(-1),
      _xpos(0),
      _ypos(0),  
      _rowsize(0),
      _timeout(0)
{
    GNASH_REPORT_FUNCTION;
    
    // initializing to zero helps with debugging and prevents weird bugs
//    memset(mouse_buf, 0, 256);
    memset(&_var_screeninfo, 0, sizeof(fb_var_screeninfo));
    memset(&_fix_screeninfo, 0, sizeof(fb_fix_screeninfo));
    memset(&_cmap, 0, sizeof(struct fb_cmap));

    signal(SIGINT, terminate_signal);
    signal(SIGTERM, terminate_signal);
}

FBGui::~FBGui()
{  
    GNASH_REPORT_FUNCTION;
    
    if (_fd > 0) {
        enable_terminal();
        log_debug(_("Closing framebuffer device"));
        close(_fd);
    }
}

bool
FBGui::set_grayscale_lut8()
{
#define TO_16BIT(x) (x | (x<<8))

    GNASH_REPORT_FUNCTION;

    struct fb_cmap cmap;
    int i;

    log_debug(_("LUT8: Setting up colormap"));

    cmap.start=0;
    cmap.len=256;
    cmap.red = (__u16*)malloc(CMAP_SIZE);
    cmap.green = (__u16*)malloc(CMAP_SIZE);
    cmap.blue = (__u16*)malloc(CMAP_SIZE);
    cmap.transp = NULL;

    for (i=0; i<256; i++) {
        int r = i;
        int g = i;
        int b = i;

        cmap.red[i] = TO_16BIT(r);
        cmap.green[i] = TO_16BIT(g);
        cmap.blue[i] = TO_16BIT(b);
    }

#ifdef ENABLE_FAKE_FRAMEBUFFER
    if (fakefb_ioctl(_fd, FBIOPUTCMAP, &_cmap))
#else
    if (ioctl(_fd, FBIOPUTCMAP, &_cmap))
#endif
    {
        log_error(_("LUT8: Error setting colormap: %s"), strerror(errno));
        return false;
    }

    return true;

#undef TO_16BIT
}

bool
FBGui::init(int argc, char *** argv)
{
    GNASH_REPORT_FUNCTION;

    // Initialize all the input devices

    // Look for Mice that use the PS/2 mouse protocol
    _inputs = InputDevice::scanForDevices(this);
    if (_inputs.empty()) {
        log_error("Found no accessible input event devices");
    }
    
#if 0
    // FIME: moved to fb_glue_agg.cpp
    
    // Open the framebuffer device
#ifdef ENABLE_FAKE_FRAMEBUFFER
    _fd = open(FAKEFB, O_RDWR);
    log_debug("WARNING: Using %s as a fake framebuffer!", FAKEFB);
#else
    _fd = open("/dev/fb0", O_RDWR);
#endif
    if (_fd < 0) {
        log_error("Could not open framebuffer device: %s", strerror(errno));
        return false;
    }
  
    // Load framebuffer properties
#ifdef ENABLE_FAKE_FRAMEBUFFER
    fakefb_ioctl(_fd, FBIOGET_VSCREENINFO, &_var_screeninfo);
    fakefb_ioctl(_fd, FBIOGET_FSCREENINFO, &_fix_screeninfo);
#else
    ioctl(_fd, FBIOGET_VSCREENINFO, &_var_screeninfo);
    ioctl(_fd, FBIOGET_FSCREENINFO, &_fix_screeninfo);
#endif
    log_debug(_("Framebuffer device uses %d bytes of memory."),
              _fix_screeninfo.smem_len);
    log_debug(_("Video mode: %dx%d with %d bits per pixel."),
              _var_screeninfo.xres, _var_screeninfo.yres,
              _var_screeninfo.bits_per_pixel);

    _fbmem.reset(static_cast<boost::uint8_t *>(mmap(0, _fixinfo.smem_len,
                                                    PROT_READ|PROT_WRITE, MAP_SHARED,
                                                    _fd, 0)));
    
    // Set "window" size
    _width    = _var_screeninfo.xres;
    _height   = _var_screeninfo.yres;

    // Let -j -k override "window" size
    optind = 0; opterr = 0; char c;
    while ((c = getopt (argc, *argv, "j:k:X:Y:")) != -1) {
        switch (c) {
            case 'j':
                _width = clamp<int>(atoi(optarg), 1, _width);
                break;
            case 'k':
                _height = clamp<int>(atoi(optarg), 1, _height);
                break;
            case 'X':
                _xpos = atoi(optarg);
                break;
            case 'Y':
                _ypos = atoi(optarg);
                break;
        }
    }

    if ( _xpos < 0 ) _xpos += _var_screeninfo.xres - _width;
    _xpos = clamp<int>(_xpos, 0, _var_screeninfo.xres-_width);

    if ( _ypos < 0 ) _ypos += _var_screeninfo.yres - _height;
    _ypos = clamp<int>(_ypos, 0, _var_screeninfo.yres-_height);

    log_debug("Width:%d, Height:%d", _width, _height);
    log_debug("X:%d, Y:%d", _xpos, _ypos);

    _validbounds.setTo(0, 0, _width - 1, _height - 1);
#endif

    // the current renderer as set on the command line or gnashrc file
    std::string renderer = _runResources.getRenderBackend();

    // map framebuffer into memory
    // Create a new Glue layer
    // if (renderer == "agg") {
    _glue.reset(new FBAggGlue());
    // } else if (renderer = "openvg") {
    //     _glue.reset(new FBOvgGlue);
    // }
        
    // Initialize the glue layer between the renderer and the gui toolkit
    _glue->init(argc, argv);

    return true;
}

bool
FBGui::run()
{
    GNASH_REPORT_FUNCTION;
  
#ifdef USE_TSLIB
    int ts_loop_count;
#endif

    VirtualClock& timer = getClock();
    
    // let the GUI recompute the x/y scale factors to best fit the whole screen
    resize_view(_validbounds.width(), _validbounds.height());

    // This loops endlessly at the frame rate
    while (!terminate_request) {  
        // wait the "heartbeat" inteval
        gnashSleep(_interval * 1000);    
        // TODO: Do we need to check the real time slept or is it OK when we woke
        // up early because of some Linux signal sent to our process (and thus
        // "advance" faster than the "heartbeat" interval)? - Udo

#ifdef USE_TSLIB
        ts_loop_count++; //increase loopcount
#endif
        
        // check input devices
        checkForData();
        
        // advance movie  
        Gui::advance_movie(this);

        // check if we've reached a timeout
        if (_timeout && timer.elapsed() >= _timeout ) {
            break;
        }
    }
  
    return true;
}

void
FBGui::renderBuffer()
{
    GNASH_REPORT_FUNCTION;

    // if ( _drawbounds.size() == 0 ) return; // nothing to do..

#ifdef ENABLE_DOUBLE_BUFFERING

    // Size of a pixel in bytes
    // NOTE: +7 to support 15 bpp
    const unsigned int pixel_size = (var_screeninfo.bits_per_pixel+7)/8;
    
    for (unsigned int bno=0; bno < _drawbounds.size(); bno++) {  
        geometry::Range2d<int>& bounds = _drawbounds[bno];
        assert ( ! bounds.isWorld() );  
    
        // Size, in bytes, of a row that has to be copied
        const unsigned int row_size = (bounds.width()+1) * pixel_size;
      
        // copy each row
        const int minx = bounds.getMinX();
        const int maxy = bounds.getMaxY();

        const int minx1 = minx+_xpos;
        for (int y=bounds.getMinY(), y1=y+_ypos; y<=maxy; ++y, ++y1) {
            const unsigned int pix_idx_in = y*m_rowsize + minx*pixel_size;
            const unsigned int pix_idx_out = y1*m_rowsize + minx1*pixel_size;
            memcpy(&(fbmem[pix_idx_out]), &buffer[pix_idx_in], row_size);
        }
    }  
       
#endif

    // FIXME: should call swapBuffer()
}

bool
FBGui::createWindow(const char* /*title*/, int /*width*/, int /*height*/,
                     int /*xPosition*/, int /*yPosition*/)
{
    GNASH_REPORT_FUNCTION;
    
    return true;
}

bool
FBGui::createMenu()
{
    // no menu support! 
    return true;
}

bool
FBGui::setupEvents()
{
    // events currently not supported!
    return true;
}

void
FBGui::setInterval(unsigned int interval)
{
    _interval = interval;
}

void
FBGui::setTimeout(unsigned int timeout)
{
    _timeout = timeout;
}

// void
// FBGui::setFullscreen()
// {
//     // FB GUI always runs fullscreen; ignore...
// }

// void
// FBGui::unsetFullscreen()
// {
//   // FB GUI always runs fullscreen; ignore...
// }

void
FBGui::showMenu(bool /*show*/)
{
    log_unimpl(_("This GUI does not yet support menus"));
}

bool
FBGui::showMouse(bool /*show*/)
{
    log_unimpl(_("This GUI does not yet support a mouse pointer"));
    // Should return true if the pointer was visible before call,
    // otherwise false;
    return true;
}

void
FBGui::setInvalidatedRegion(const SWFRect& bounds)
{
    GNASH_REPORT_FUNCTION;

//    _glue->setInvalidatedRegion(bounds);
}

void
FBGui::setInvalidatedRegions(const InvalidatedRanges& ranges)
 {
     GNASH_REPORT_FUNCTION;

//    _glue->setInvalidatedRegions(ranges);
}

char *
FBGui::find_accessible_tty(int no)
{
    char* fn;
    
    fn = find_accessible_tty("/dev/vc/%d", no);   if (fn) return fn;
    fn = find_accessible_tty("/dev/tty%d", no);   if (fn) return fn;
    fn = find_accessible_tty("/dev/tty%02x", no); if (fn) return fn;
    fn = find_accessible_tty("/dev/tty%x", no);   if (fn) return fn;
    fn = find_accessible_tty("/dev/tty%02d", no); if (fn) return fn;
  
    if (no==0) {
        fn = find_accessible_tty("/dev/tty", no);  // just "/dev/tty" 
        if (fn) return fn;
    }
    
    return NULL;
}

char *
FBGui::find_accessible_tty(const char* format, int no)
{
    static char fname[1024];
    
    snprintf(fname, sizeof fname, format, no);
    
    if (access(fname, R_OK|W_OK) != -1) {
        return fname;
    }
    
    return NULL; 
}

bool
FBGui::disable_terminal() 
{
    _original_kd = -1;
    
    struct vt_stat vts;
    
    // Find the TTY device name
    
    char* tty = find_accessible_tty(0);
    
    int fd;
  
    if (!tty) {
        log_debug(_("WARNING: Could not detect controlling TTY"));
        return false;
    }
    
    
    // Detect the currently active virtual terminal (so we can switch back to
    // it later)
    
    fd = open(tty, O_RDWR);
    if (fd < 0) {
        log_debug(_("WARNING: Could not open %s"), tty);
        return false;
    }
    
    if (ioctl(fd, VT_GETSTATE, &vts) == -1) {
        log_debug(_("WARNING: Could not get current VT state"));
        close(_fd);
        return false;
    }
    
    _original_vt = vts.v_active;
    log_debug(_("Original TTY NO = %d"), _original_vt);
  
#ifdef REQUEST_NEW_VT
    // Request a new VT number
    if (ioctl(fd, VT_OPENQRY, &_own_vt) == -1) {
        log_debug(_("WARNING: Could not request a new VT"));
        close(fd);
        return false;
    }
  
    log_debug(_("Own TTY NO = %d"), _own_vt);

    if (fd > 0) {
        close(fd);
    }
  
    // Activate our new VT
    tty = find_accessible_tty(_own_vt);
    if (!tty) {
        log_debug(_("WARNING: Could not find device for VT number %d"), _own_vt);
        return false;
    }
  
    _fd = open(tty, O_RDWR);
    if (fd < 0) {
        log_debug(_("WARNING: Could not open %s"), tty);
        return false;
    }
  
    if (ioctl(fd, VT_ACTIVATE, _own_vt) == -1) {
        log_debug(_("WARNING: Could not activate VT number %d"), _own_vt);
        close(fd);
        return false;
    }
  
    if (ioctl(fd, VT_WAITACTIVE, _own_vt) == -1) {
        log_debug(_("WARNING: Error waiting for VT %d becoming active"),
                  _own_vt);
        //close(tty);
        //return false;   don't abort
    }

#else

    _own_vt = _original_vt;   // keep on using the original VT
  
    if (fd > 0) {
        close(fd);
    }
  
    // Activate our new VT
    tty = find_accessible_tty(_own_vt);
    if (!tty) {
        log_debug(_("WARNING: Could not find device for VT number %d"),
                  _own_vt);
        return false;
    }
  
    fd = open(tty, O_RDWR);
    if (fd < 0) {
        log_debug(_("WARNING: Could not open %s"), tty);
        return false;
    }
  
#if 0
    // Become session leader and attach to terminal
    setsid();
    if (ioctl(fd, TIOCSCTTY, 0) == -1) {
    log_debug(_("WARNING: Could not attach controlling terminal (%s)"), tty);
    }
#endif
#endif  // end of if REQUEST_NEW_VT
  
    // Disable keyboard cursor
  
    if (ioctl(fd, KDGETMODE, &_original_kd) == -1) {
        log_debug(_("WARNING: Could not query current keyboard mode on VT"));
    }

    if (ioctl(fd, KDSETMODE, KD_GRAPHICS) == -1) {
        log_debug(_("WARNING: Could not switch to graphics mode on new VT"));
    }

    if (fd > 0) {
        close(fd);
    }
  
    log_debug(_("VT %d ready"), _own_vt);  
  
    // NOTE: We could also implement virtual console switching by using 
    // VT_GETMODE / VT_SETMODE ioctl calls and handling their signals, but
    // probably nobody will ever want to switch consoles, so I don't bother... 
  
    return true;
}

bool
FBGui::enable_terminal() 
{
    log_debug(_("Restoring terminal..."));

    char* tty = find_accessible_tty(_own_vt);
    if (!tty) {
        log_debug(_("WARNING: Could not find device for VT number %d"), _own_vt);
        return false;
    }

    int fd = open(tty, O_RDWR);
    if (fd < 0) {
        log_debug(_("WARNING: Could not open %s"), tty);
        return false;
    }

    if (ioctl(fd, VT_ACTIVATE, _original_vt)) {
        log_debug(_("WARNING: Could not activate VT number %d"), _original_vt);
        close(_fd);
        return false;
    }

    if (ioctl(fd, VT_WAITACTIVE, _original_vt)) {
        log_debug(_("WARNING: Error waiting for VT %d becoming active"),
                  _original_vt);
        //close(tty);
        //return false;   don't abort
    }  
  
    // Restore keyboard
  
    if (ioctl(fd, KDSETMODE, _original_kd)) {
        log_debug(_("WARNING: Could not restore keyboard mode"));
    }  

    if (fd > 0) {
        close(fd);
    }
  
    return true;
}

void
FBGui::checkForData()
{
    // GNASH_REPORT_FUNCTION;

    std::vector<boost::shared_ptr<InputDevice> >::iterator it;

    for (it=_inputs.begin(); it!=_inputs.end(); ++it) {
        (*it)->check();
    }
}

} // end of namespace gui
} // end of namespace gnash

// Local Variables:
// mode: C++
// indent-tabs-mode: nil
// End:
