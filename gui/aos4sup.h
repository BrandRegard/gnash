// 
//   Copyright (C) 2005, 2006, 2007, 2008, 2009 Free Software Foundation, Inc.
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


#ifndef __AOS4SUP_H__
#define __AOS4SUP_H__

#ifdef HAVE_CONFIG_H
#include "gnashconfig.h"
#endif

#include "gui.h"

#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/Picasso96API.h>
#include <proto/layers.h>
#include <proto/Picasso96API.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <exec/execbase.h>
#include <devices/timer.h>

#include <graphics/blitattr.h>
#include <graphics/composite.h>

#ifdef RENDERER_AGG
# include "aos4_agg_glue.h"
#elif defined(RENDERER_CAIRO)
# include "aos4_cairo_glue.h"
#elif defined(RENDERER_OPENGL)
# include "aos4_ogl_glue.h"
#endif

#undef ACTION_END
#include "render_handler.h"
#include "render_handler_agg.h"

static struct TimeVal os4timer_starttime;

#ifdef RENDERER_CAIRO
#include <cairo.h>
#include <cairo-amigaos.h>
#endif

namespace gnash
{

class AOS4Gui : public Gui
{
public:
    AOS4Gui(unsigned long xid, float scale, bool loop, unsigned int depth);
    virtual ~AOS4Gui();
    virtual bool init(int argc, char **argv[]);
    virtual bool createWindow(const char *title, int width, int height);
    virtual bool run();
    virtual bool createMenu();
    virtual bool setupEvents();
    virtual void renderBuffer();
    virtual void setInterval(unsigned int interval);
    virtual void disableCoreTrap();
    virtual void setTimeout(unsigned int timeout);
    void setInvalidatedRegions(const InvalidatedRanges& ranges);
    void key_event(gnash::key::code key, int state, bool down);
	void resize(int width, int height);
private:
    unsigned int _timeout;
    bool         _core_trap;

   	geometry::Range2d<int> _validbounds;
    std::vector< geometry::Range2d<int> > _drawbounds;

	struct MsgPort *_port;
	uint32 _timerSig;
	struct TimeRequest *_timerio;
	struct TimerIFace *ITimer;

    static key::code os4_to_gnash_key(struct IntuiMessage *imsg);
    static int os4_to_gnash_modifier(int state);
	double OS4_GetTicks();
	void TimerExit(void);
	bool TimerInit(void);
	void TimerReset(uint32 microDelay);
	
#ifdef RENDERER_AGG
    AOS4AggGlue      _glue;
#elif defined(RENDERER_CAIRO)
    AOS4CairoGlue    _glue;
#elif defined(RENDERER_OPENGL)
    AOS4OglGlue      _glue;
#endif

};
 
// void xt_event_handler(Widget xtwidget, gpointer instance,
//       XEvent *xevent, Boolean *b);

// end of namespace gnash 
}

// end of __AOS4SUP_H__
#endif