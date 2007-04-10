// 
//   Copyright (C) 2005, 2006, 2007 Free Software Foundation, Inc.
// 
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

/* $Id: tag_loaders.cpp,v 1.87 2007/04/10 21:09:00 strk Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <zlib.h>

#include "tu_file.h"
#include "utility.h"
#include "action.h"
#include "action_buffer.h"
#include "button_character_def.h"
#include "font.h"
#include "fontlib.h"
#include "log.h"
#include "morph2_character_def.h"
#include "shape.h"
#include "stream.h"
#include "styles.h"
#include "dlist.h"
#include "timers.h"
#include "image.h"
#include "zlib_adapter.h"
#include "sprite_definition.h"
#include "sprite_instance.h"
#include "swf_function.h"
#include "swf_event.h"
#include "as_function.h"
#include "movie_def_impl.h"
#include "swf.h"
#include "swf/TagLoadersTable.h"
#include "generic_character.h"
#include "text_character_def.h" 
#include "edit_text_character_def.h"
#include "execute_tag.h" // for do_action inheritance (DOACTION tag loader)
#include "URL.h"
#include "GnashException.h"
#include "video_stream_def.h"
#include "sound_definition.h"

namespace gnash {

	// @@ TODO get rid of this; make it the normal mode.
	extern bool s_no_recurse_while_loading;

}

namespace gnash {
namespace SWF {
namespace tag_loaders {

//
// Some tag implementations
//


/// Thin wrapper around action_buffer.
class do_action : public execute_tag
{
private:
	action_buffer m_buf;
public:
	void read(stream* in)
	{
	    m_buf.read(in);
	}

	virtual void execute(sprite_instance* m)
	{
	    m->add_action_buffer(&m_buf);
	}

	// Don't override because actions should not be replayed when
	// seeking the movie.
	//void	execute_state(movie* m) {}

	// Tell the caller that we are an action tag.
	virtual bool is_action_tag() const
	{
	    return true;
	}
};

//
// Tag loaders
//


// Silently ignore the contents of this tag.
void null_loader(stream* /*in*/, tag_type /*tag*/, movie_definition* /*m*/)
{
}

// Label the current frame of m with the name from the stream.
void
frame_label_loader(stream* in, tag_type tag, movie_definition* m)
{
    assert(tag == SWF::FRAMELABEL); // 43

    char*	n = in->read_string();
    m->add_frame_name(n);

	// FIXME: support SWF6 "named anchors"
	//
	// If SWF version is >= 6 check the byte after terminating NULL
	// if it is 1 this label can be accessed by #name and it's
	// entrance sets the browser URL with anchor appended
	//
	// To avoid relaying on stream::get_position (see task #5838)
	// we should add a new method to that class
	// (ie: stream::current_tag_length)
	//
	// See server/sample/test_clipping_layer.swf for a testcase.
	//
	size_t end_tag = in->get_tag_end_position();
	size_t curr_pos = in->get_position();
	if ( end_tag != curr_pos )
	{
		if ( end_tag == curr_pos + 1 )
		{
	log_warning("FIXME: anchor-labeled frame not supported");
		}
		else
		{
			IF_VERBOSE_MALFORMED_SWF(
	log_swferror("frame_label_loader end position " SIZET_FMT ", "
			"read up to " SIZET_FMT,
			end_tag, curr_pos);
			);
		}
	}

    delete [] n;
}

/// SWF Tag SetBackgroundColor (9)
class set_background_color : public execute_tag
{
private:
    rgba	m_color;

public:
    void	execute(sprite_instance* m)
	{
	    float	current_alpha = m->get_background_alpha();
	    m_color.m_a = frnd(current_alpha * 255.0f);
	    m->set_background_color(m_color);
	}

    void	execute_state(sprite_instance* m)
	{
	    execute(m);
	}

    void	read(stream* in)
	{
	    m_color.read_rgb(in);

		IF_VERBOSE_PARSE
		(
	    log_parse("  set_background_color: (%d %d %d)",
		      m_color.m_r, m_color.m_g, m_color.m_b);
		);
	}
};


// SWF Tag SetBackgroundColor (9)
void
set_background_color_loader(stream* in, tag_type tag, movie_definition* m)
{
    assert(tag == SWF::SETBACKGROUNDCOLOR); // 9
    assert(m);

    set_background_color* t = new set_background_color;
    t->read(in);

    m->add_execute_tag(t);
}

// Load JPEG compression tables that can be used to load
// images further along in the stream.
void
jpeg_tables_loader(stream* in, tag_type tag, movie_definition* m)
{
    assert(tag == SWF::JPEGTABLES);

	IF_VERBOSE_PARSE
	(
    log_parse("  jpeg_tables_loader");
    	);

#if TU_CONFIG_LINK_TO_JPEGLIB
    std::auto_ptr<jpeg::input> j_in(jpeg::input::create_swf_jpeg2_header_only(in->get_underlying_stream()));
    assert(j_in.get());

    m->set_jpeg_loader(j_in);
#endif // TU_CONFIG_LINK_TO_JPEGLIB
}


// A JPEG image without included tables; those should be in an
// existing jpeg::input object stored in the movie.
void
define_bits_jpeg_loader(stream* in, tag_type tag, movie_definition* m)
{
	assert(tag == SWF::DEFINEBITS); // 6
	assert(in);

    uint16_t	character_id = in->read_u16();

    //
    // Read the image data.
    //

    if (m->get_create_bitmaps() == DO_LOAD_BITMAPS)
	{
	    //bitmap_info*	bi = NULL;
#if TU_CONFIG_LINK_TO_JPEGLIB
	    jpeg::input*	j_in = m->get_jpeg_loader();
	    assert(j_in);
	    j_in->discard_partial_buffer();

	    std::auto_ptr<image::rgb> im ( image::read_swf_jpeg2_with_tables(j_in) );
	    //bi = render::create_bitmap_info_rgb(im);
	    //delete im;
#else
	    log_error("gnash is not linked to jpeglib -- can't load jpeg image data!");
	    return;
#endif


		//assert(im->get_ref_count() == 0);

		bitmap_character_def* ch = new bitmap_character_def(im);

		if ( m->get_bitmap_character_def(character_id) )
		{
			IF_VERBOSE_MALFORMED_SWF(
			log_swferror("DEFINEBITS: Duplicate id (%d) for bitmap character - discarding it", character_id);
			);
		}
		else
		{
			m->add_bitmap_character_def(character_id, ch);
		}
	}

}


void
define_bits_jpeg2_loader(stream* in, tag_type tag, movie_definition* m)
{
    assert(tag == SWF::DEFINEBITSJPEG2); // 21
		
    uint16_t	character_id = in->read_u16();

	IF_VERBOSE_PARSE
	(
		log_parse("  define_bits_jpeg2_loader: charid = %d pos = %lx",
			character_id, in->get_position());
    	);

    //
    // Read the image data.
    //
		
    if (m->get_create_bitmaps() == DO_LOAD_BITMAPS)
	{
	    //bitmap_info*	bi = NULL;
#if TU_CONFIG_LINK_TO_JPEGLIB
	    std::auto_ptr<image::rgb> im ( image::read_jpeg(in->get_underlying_stream()) );
	    //bi = render::create_bitmap_info_rgb(im);
	    //delete im;
#else
	    log_error("gnash is not linked to jpeglib -- can't load jpeg image data!");
	    return;
#endif

    //assert(bi->get_ref_count() == 0);

		if ( m->get_bitmap_character_def(character_id) )
		{
			IF_VERBOSE_MALFORMED_SWF(
			log_swferror("DEFINEBITSJPEG2: Duplicate id (%d) for bitmap character - discarding it", character_id);
			);
		}
		else
		{
			bitmap_character_def* ch = new bitmap_character_def(im);
    			m->add_bitmap_character_def(character_id, ch);
		}
   	}

}


#if TU_CONFIG_LINK_TO_ZLIB
void inflate_wrapper(tu_file* in, void* buffer, int buffer_bytes)
    // Wrapper function -- uses Zlib to uncompress in_bytes worth
    // of data from the input file into buffer_bytes worth of data
    // into *buffer.
{
    assert(in);
    assert(buffer);
    assert(buffer_bytes > 0);

    z_stream d_stream; /* decompression stream */

    d_stream.zalloc = (alloc_func)0;
    d_stream.zfree = (free_func)0;
    d_stream.opaque = (voidpf)0;

    d_stream.next_in  = 0;
    d_stream.avail_in = 0;

    d_stream.next_out = (Byte*) buffer;
    d_stream.avail_out = (uInt) buffer_bytes;

    int err = inflateInit(&d_stream);
    if (err != Z_OK) {
	log_error("inflate_wrapper() inflateInit() returned %d", err);
	return;
    }

    uint8_t buf[1];

    for (;;) {
	// Fill a one-byte (!) buffer.
	buf[0] = in->read_byte();
	d_stream.next_in = &buf[0];
	d_stream.avail_in = 1;

	err = inflate(&d_stream, Z_SYNC_FLUSH);
	if (err == Z_STREAM_END) break;
	if (err != Z_OK)
	    {
		log_error("inflate_wrapper() inflate() returned %d", err);
	    }
    }

    err = inflateEnd(&d_stream);
    if (err != Z_OK)
	{
	    log_error("inflate_wrapper() inflateEnd() return %d", err);
	}
}
#endif // TU_CONFIG_LINK_TO_ZLIB


// loads a define_bits_jpeg3 tag. This is a jpeg file with an alpha
// channel using zlib compression.
void
define_bits_jpeg3_loader(stream* in, tag_type tag, movie_definition* m)
{
    assert(tag == SWF::DEFINEBITSJPEG3); // 35

    uint16_t	character_id = in->read_u16();

	IF_VERBOSE_PARSE
	(
		log_parse("  define_bits_jpeg3_loader: charid = %d pos = %lx",
			character_id, in->get_position());
	);

    uint32_t	jpeg_size = in->read_u32();
    uint32_t	alpha_position = in->get_position() + jpeg_size;

    if (m->get_create_bitmaps() == DO_LOAD_BITMAPS)
	{

#if TU_CONFIG_LINK_TO_JPEGLIB == 0 || TU_CONFIG_LINK_TO_ZLIB == 0
	    log_error("gnash is not linked to jpeglib/zlib -- can't load jpeg/zipped image data!");
	    return;
#else
	    //
	    // Read the image data.
	    //
	    		
	    // Read rgb data.
	    std::auto_ptr<image::rgba> im( image::read_swf_jpeg3(in->get_underlying_stream()) );

	    // Read alpha channel.
	    in->set_position(alpha_position);

	    int	buffer_bytes = im->m_width * im->m_height;
	    uint8_t*	buffer = new uint8_t[buffer_bytes];

	    inflate_wrapper(in->get_underlying_stream(), buffer, buffer_bytes);

	    for (int i = 0; i < buffer_bytes; i++)
		{
		    im->m_data[4*i+3] = buffer[i];
		}

	    delete [] buffer;

	    // Create bitmap character.
	    bitmap_character_def* ch = new bitmap_character_def(im);

	    m->add_bitmap_character_def(character_id, ch);
#endif
	}

}


void
define_bits_lossless_2_loader(stream* in, tag_type tag, movie_definition* m)
{
    // tags 20 || 36
    assert(tag == SWF::DEFINELOSSLESS || tag == SWF::DEFINELOSSLESS2);

    uint16_t	character_id = in->read_u16();
    uint8_t	bitmap_format = in->read_u8();	// 3 == 8 bit, 4 == 16 bit, 5 == 32 bit
    uint16_t	width = in->read_u16();
    uint16_t	height = in->read_u16();

	IF_VERBOSE_PARSE
	(
		log_parse("  defbitslossless2: tag = %d, id = %d, "
			"fmt = %d, w = %d, h = %d",
			tag,
			character_id,
			bitmap_format,
			width,
			height);
	);

    //bitmap_info*	bi = NULL;
    if (m->get_create_bitmaps() == DO_LOAD_BITMAPS)
	{
#if TU_CONFIG_LINK_TO_ZLIB == 0
	    log_error("gnash is not linked to zlib -- can't load zipped image data!");
	    return;
#else
	    if (tag == SWF::DEFINELOSSLESS) // 20
		{
		    // RGB image data.
		    std::auto_ptr<image::rgb> image ( image::create_rgb(width, height) );

		    if (bitmap_format == 3)
			{
			    // 8-bit data, preceded by a palette.

			    const int	bytes_per_pixel = 1;
			    int	color_table_size = in->read_u8();
			    color_table_size++;	// !! SWF stores one less than the actual size

			    int	pitch = (width * bytes_per_pixel + 3) & ~3;

			    int	buffer_bytes = color_table_size * 3 + pitch * height;
			    uint8_t*	buffer = new uint8_t[buffer_bytes];

			    inflate_wrapper(in->get_underlying_stream(), buffer, buffer_bytes);
			    assert(in->get_position() <= in->get_tag_end_position());

			    uint8_t*	color_table = buffer;

			    for (int j = 0; j < height; j++)
				{
				    uint8_t*	image_in_row = buffer + color_table_size * 3 + j * pitch;
				    uint8_t*	image_out_row = image::scanline(image.get(), j);
				    for (int i = 0; i < width; i++)
					{
					    uint8_t	pixel = image_in_row[i * bytes_per_pixel];
					    image_out_row[i * 3 + 0] = color_table[pixel * 3 + 0];
					    image_out_row[i * 3 + 1] = color_table[pixel * 3 + 1];
					    image_out_row[i * 3 + 2] = color_table[pixel * 3 + 2];
					}
				}

			    delete [] buffer;
			}
		    else if (bitmap_format == 4)
			{
			    // 16 bits / pixel
			    const int	bytes_per_pixel = 2;
			    int	pitch = (width * bytes_per_pixel + 3) & ~3;

			    int	buffer_bytes = pitch * height;
			    uint8_t*	buffer = new uint8_t[buffer_bytes];

			    inflate_wrapper(in->get_underlying_stream(), buffer, buffer_bytes);
			    assert(in->get_position() <= in->get_tag_end_position());
			
			    for (int j = 0; j < height; j++)
				{
				    uint8_t*	image_in_row = buffer + j * pitch;
				    uint8_t*	image_out_row = image::scanline(image.get(), j);
				    for (int i = 0; i < width; i++)
					{
					    uint16_t	pixel = image_in_row[i * 2] | (image_in_row[i * 2 + 1] << 8);
					
					    // @@ How is the data packed???  I'm just guessing here that it's 565!
					    image_out_row[i * 3 + 0] = (pixel >> 8) & 0xF8;	// red
					    image_out_row[i * 3 + 1] = (pixel >> 3) & 0xFC;	// green
					    image_out_row[i * 3 + 2] = (pixel << 3) & 0xF8;	// blue
					}
				}
			
			    delete [] buffer;
			}
		    else if (bitmap_format == 5)
			{
			    // 32 bits / pixel, input is ARGB format (???)
			    const int	bytes_per_pixel = 4;
			    int	pitch = width * bytes_per_pixel;

			    int	buffer_bytes = pitch * height;
			    uint8_t*	buffer = new uint8_t[buffer_bytes];

			    inflate_wrapper(in->get_underlying_stream(), buffer, buffer_bytes);
			    assert(in->get_position() <= in->get_tag_end_position());
			
			    // Need to re-arrange ARGB into RGB.
			    for (int j = 0; j < height; j++)
				{
				    uint8_t*	image_in_row = buffer + j * pitch;
				    uint8_t*	image_out_row = image::scanline(image.get(), j);
				    for (int i = 0; i < width; i++)
					{
					    uint8_t	a = image_in_row[i * 4 + 0];
					    uint8_t	r = image_in_row[i * 4 + 1];
					    uint8_t	g = image_in_row[i * 4 + 2];
					    uint8_t	b = image_in_row[i * 4 + 3];
					    image_out_row[i * 3 + 0] = r;
					    image_out_row[i * 3 + 1] = g;
					    image_out_row[i * 3 + 2] = b;
					    a = a;	// Inhibit warning.
					}
				}

			    delete [] buffer;
			}

			if ( m->get_bitmap_character_def(character_id) )
			{
				IF_VERBOSE_MALFORMED_SWF(
				log_swferror("DEFINEBITSLOSSLESS: Duplicate id (%d) for bitmap character - discarding it", character_id);
				);
			}
			else
			{
				bitmap_character_def* ch = new bitmap_character_def(image);

				// add image to movie, under character id.
				m->add_bitmap_character_def(character_id, ch);
			}
		}
	    else
		{
		    // RGBA image data.
		    assert(tag == SWF::DEFINELOSSLESS2); // 36

		    std::auto_ptr<image::rgba> image ( image::create_rgba(width, height) );

		    if (bitmap_format == 3)
			{
			    // 8-bit data, preceded by a palette.

			    const int	bytes_per_pixel = 1;
			    int	color_table_size = in->read_u8();
			    color_table_size++;	// !! SWF stores one less than the actual size

			    int	pitch = (width * bytes_per_pixel + 3) & ~3;

			    int	buffer_bytes = color_table_size * 4 + pitch * height;
			    uint8_t*	buffer = new uint8_t[buffer_bytes];

			    inflate_wrapper(in->get_underlying_stream(), buffer, buffer_bytes);
			    assert(in->get_position() <= in->get_tag_end_position());

			    uint8_t*	color_table = buffer;

			    for (int j = 0; j < height; j++)
				{
				    uint8_t*	image_in_row = buffer + color_table_size * 4 + j * pitch;
				    uint8_t*	image_out_row = image::scanline(image.get(), j);
				    for (int i = 0; i < width; i++)
					{
					    uint8_t	pixel = image_in_row[i * bytes_per_pixel];
					    image_out_row[i * 4 + 0] = color_table[pixel * 4 + 0];
					    image_out_row[i * 4 + 1] = color_table[pixel * 4 + 1];
					    image_out_row[i * 4 + 2] = color_table[pixel * 4 + 2];
					    image_out_row[i * 4 + 3] = color_table[pixel * 4 + 3];
					}
				}

			    delete [] buffer;
			}
		    else if (bitmap_format == 4)
			{
			    // 16 bits / pixel
			    const int	bytes_per_pixel = 2;
			    int	pitch = (width * bytes_per_pixel + 3) & ~3;

			    int	buffer_bytes = pitch * height;
			    uint8_t*	buffer = new uint8_t[buffer_bytes];

			    inflate_wrapper(in->get_underlying_stream(), buffer, buffer_bytes);
			    assert(in->get_position() <= in->get_tag_end_position());
			
			    for (int j = 0; j < height; j++)
				{
				    uint8_t*	image_in_row = buffer + j * pitch;
				    uint8_t*	image_out_row = image::scanline(image.get(), j);
				    for (int i = 0; i < width; i++)
					{
					    uint16_t	pixel = image_in_row[i * 2] | (image_in_row[i * 2 + 1] << 8);
					
					    // @@ How is the data packed???  I'm just guessing here that it's 565!
					    image_out_row[i * 4 + 0] = 255;			// alpha
					    image_out_row[i * 4 + 1] = (pixel >> 8) & 0xF8;	// red
					    image_out_row[i * 4 + 2] = (pixel >> 3) & 0xFC;	// green
					    image_out_row[i * 4 + 3] = (pixel << 3) & 0xF8;	// blue
					}
				}
			
			    delete [] buffer;
			}
		    else if (bitmap_format == 5)
			{
			    // 32 bits / pixel, input is ARGB format

			    inflate_wrapper(in->get_underlying_stream(), image->m_data, width * height * 4);
			    assert(in->get_position() <= in->get_tag_end_position());
			
			    // Need to re-arrange ARGB into RGBA.
			    for (int j = 0; j < height; j++)
				{
				    uint8_t*	image_row = image::scanline(image.get(), j);
				    for (int i = 0; i < width; i++)
					{
					    uint8_t	a = image_row[i * 4 + 0];
					    uint8_t	r = image_row[i * 4 + 1];
					    uint8_t	g = image_row[i * 4 + 2];
					    uint8_t	b = image_row[i * 4 + 3];
					    image_row[i * 4 + 0] = r;
					    image_row[i * 4 + 1] = g;
					    image_row[i * 4 + 2] = b;
					    image_row[i * 4 + 3] = a;
					}
				}
			}

			bitmap_character_def* ch = new bitmap_character_def(image);
//			delete image;

			// add image to movie, under character id.
			m->add_bitmap_character_def(character_id, ch);
		}
#endif // TU_CONFIG_LINK_TO_ZLIB

#if 0
    assert(bi->get_ref_count() == 0);

    bitmap_character*	ch = new bitmap_character(bi);

    // add image to movie, under character id.
    m->add_bitmap_character(character_id, ch);
#endif
    	}
}

// This is like null_loader except it prints a message to nag us to fix it.
void
fixme_loader(stream* /*in*/, tag_type tag, movie_definition* /*m*/)
{
    log_error("  FIXME: tagtype = %d", tag);
}

void define_shape_loader(stream* in, tag_type tag, movie_definition* m)
{
    assert(tag == SWF::DEFINESHAPE
	   || tag == SWF::DEFINESHAPE2
	   || tag == SWF::DEFINESHAPE3);

    uint16_t	character_id = in->read_u16();
		IF_VERBOSE_PARSE
		(
    log_parse("  shape_loader: id = %d", character_id);
    		);

    shape_character_def*	ch = new shape_character_def;
    ch->read(in, tag, true, m);

    m->add_character(character_id, ch);
}

void define_shape_morph_loader(stream* in, tag_type tag, movie_definition* m)
{
    assert(tag == SWF::DEFINEMORPHSHAPE); // 46
    uint16_t character_id = in->read_u16();

		IF_VERBOSE_PARSE
		(
    log_parse("  shape_morph_loader: id = %d", character_id);
    		);

    morph2_character_def* morph = new morph2_character_def;
    morph->read(in, tag, true, m);
    m->add_character(character_id, morph);
}

//
// font loaders
//


void	define_font_loader(stream* in, tag_type tag, movie_definition* m)
    // Load a DefineFont or DefineFont2 tag.
{
    assert(tag == SWF::DEFINEFONT || tag == SWF::DEFINEFONT2 || tag == SWF::DEFINEFONT3 ); // 10 || 48 || 75

    uint16_t	font_id = in->read_u16();
		
    font*	f = new font;
    f->read(in, tag, m);

    m->add_font(font_id, f);

    // Automatically keeping fonts in fontlib is
    // problematic.  The app should be responsible for
    // optionally adding fonts to fontlib.
    // //fontlib::add_font(f);
}


// See description in header
void	define_font_info_loader(stream* in, tag_type tag, movie_definition* m)
{
	assert(tag == SWF::DEFINEFONTINFO || tag == SWF::DEFINEFONTINFO2);

	uint16_t font_id = in->read_u16();
		
	font* f = m->get_font(font_id);
	if (f)
	{
		f->read_font_info(in, tag, m);
	}
	else
	{
		IF_VERBOSE_MALFORMED_SWF(
		log_swferror("define_font_info_loader: "
			"can't find font w/ id %d", font_id);
		);
	}
}

/// SWF Tag PlaceObject (4) or PlaceObject2 (9) 
class place_object_2 : public execute_tag
{
public:
    int	m_tag_type;
    char*	m_name;
    float	m_ratio;
    cxform	m_color_transform;
    matrix	m_matrix;
    bool	m_has_matrix;
    bool	m_has_cxform;
    int		m_depth;
    uint16_t	m_character_id;
    int 	m_clip_depth;
    enum place_type {
	PLACE,
	MOVE,
	REPLACE
    } m_place_type;
    std::vector<swf_event*>	m_event_handlers;


    place_object_2()
	:
	m_tag_type(0),
	m_name(NULL),
	m_ratio(0),
	m_has_matrix(false),
	m_has_cxform(false),
	m_depth(0),
	m_character_id(0),
	m_clip_depth(0),
	m_place_type(PLACE)
	{
	}

	~place_object_2()
	{
	    delete [] m_name;
	    m_name = NULL;

	    for (int i = 0, n = m_event_handlers.size(); i < n; i++)
		{
		    delete m_event_handlers[i];
		}
	    m_event_handlers.resize(0);
	}

	// read SWF::PLACEOBJECT 
	void readPlaceObject(stream* in)
	{
		// Original place_object tag; very simple.
		m_character_id = in->read_u16();
		m_depth = in->read_u16()+character::staticDepthOffset;
		m_matrix.read(in);

		IF_VERBOSE_PARSE
		(
			log_parse("  char_id = %d", m_character_id);
			log_parse("  depth = %d (%d)", m_depth, m_depth-character::staticDepthOffset);
			m_matrix.print();
		);

		if (in->get_position() < in->get_tag_end_position())
		{
			m_color_transform.read_rgb(in);

			IF_VERBOSE_PARSE
			(
				log_parse("  cxform:");
				m_color_transform.print();
			);

		}
	}

	// read placeObject2 actions
	void readPlaceActions(stream* in, int movie_version)
	{

		uint16_t reserved = in->read_u16();
		assert(reserved == 0);	// must be 0

		// The logical 'or' of all the following handlers.
		// I don't think we care about this...
		uint32_t all_flags = (movie_version >= 6) ?
			in->read_u32() : in->read_u16();
		UNUSED(all_flags);

		IF_VERBOSE_PARSE (
			log_parse("  actions: flags = 0x%X", all_flags);
		);

		// Read swf_events.
		for (;;)
		{
			// Read event.
			in->align();

			uint32_t flags = (movie_version >= 6) ? in->read_u32() : in->read_u16();

			if (flags == 0)
			{
				// Done with events.
				break;
			}

			uint32_t event_length = in->read_u32();
			if ( in->get_tag_end_position()-in->get_position() <  event_length )
			{
				IF_VERBOSE_MALFORMED_SWF(
				log_swferror("swf_event::read(), "
					"even_length = %u, but only %lu bytes left "
					"to the end of current tag."
					" Breaking for safety.",
					event_length, in->get_tag_end_position()-in->get_position());
				);
				break;
			}

			uint8 ch = key::INVALID;

			if (flags & (1 << 17))	// has keypress event
			{
				ch = in->read_u8();
				event_length--;
			}

			// Read the actions for event(s)
			action_buffer action;
			action.read(in);

			size_t readlen = action.size();
			if (readlen > event_length)
			{
				IF_VERBOSE_MALFORMED_SWF(
				log_swferror("swf_event::read(), "
					"event_length = %d, "
					"but read " SIZET_FMT
					". Breaking for safety.",
					event_length, readlen);
				);
				// or should we just continue here ?
				break;
			}
			else if ( readlen < event_length )
			{
				IF_VERBOSE_MALFORMED_SWF(
				log_swferror("swf_event::read(), "
					"event_length = %d, "
					"but read " SIZET_FMT 
					". Skipping excessive bytes.",
					event_length, readlen);
				);
				in->skip_bytes(event_length - readlen);
			}

			// 13 bits reserved, 19 bits used
			static const event_id s_code_bits[19] =
			{
				event_id::LOAD,
				event_id::ENTER_FRAME,
				event_id::UNLOAD,
				event_id::MOUSE_MOVE,
				event_id::MOUSE_DOWN,
				event_id::MOUSE_UP,
				event_id::KEY_DOWN,
				event_id::KEY_UP,

				event_id::DATA,
				event_id::INITIALIZE,
				event_id::PRESS,
				event_id::RELEASE,
				event_id::RELEASE_OUTSIDE,
				event_id::ROLL_OVER,
				event_id::ROLL_OUT,
				event_id::DRAG_OVER,

				event_id::DRAG_OUT,
				event_id(event_id::KEY_PRESS, key::CONTROL),
				event_id::CONSTRUCT
			};

			// Let's see if the event flag we received is for an event that we know of
			if ((pow(2.0, int( sizeof(s_code_bits) / sizeof(s_code_bits[0]) )) - 1) < flags)
			{
				IF_VERBOSE_MALFORMED_SWF(
				log_swferror("swf_event::read() -- unknown / unhandled event type received, flags = 0x%x", flags);
				);
			}

			for (int i = 0, mask = 1; i < int(sizeof(s_code_bits)/sizeof(s_code_bits[0])); i++, mask <<= 1)
			{
				if (flags & mask)
				{
					swf_event*	ev = new swf_event;
					ev->m_event = s_code_bits[i];
					ev->m_action_buffer = action;
//					log_action("---- actions for event %s", ev->m_event.get_function_name().c_str());

					// hack
					if (i == 17)	// has keypress event ?
					{
						ev->m_event.m_key_code = ch;
					}

					// Create a function to execute the actions.
					std::vector<with_stack_entry>	empty_with_stack;
					swf_function*	func = new swf_function(&ev->m_action_buffer, NULL, 0, empty_with_stack);
					func->set_length(ev->m_action_buffer.get_length());

					ev->m_method.set_as_function(func);

					m_event_handlers.push_back(ev);
				}
			}
		}
	}


	// read SWF::PLACEOBJECT2
	void readPlaceObject2(stream* in, int movie_version)
	{
		in->align();

		bool	has_actions = in->read_uint(1) ? true : false;
		bool	has_clip_bracket = in->read_uint(1) ? true : false;
		bool	has_name = in->read_uint(1) ? true : false;
		bool	has_ratio = in->read_uint(1) ? true : false;
		bool	has_cxform = in->read_uint(1) ? true : false;
		bool	has_matrix = in->read_uint(1) ? true : false;
		bool	has_char = in->read_uint(1) ? true : false;
		bool	flag_move = in->read_uint(1) ? true : false;

		m_depth = in->read_u16()+character::staticDepthOffset;

		if (has_char) m_character_id = in->read_u16();

		if (has_matrix)
		{
			m_has_matrix = true;
			m_matrix.read(in);
		}

		if (has_cxform)
		{
			m_has_cxform = true;
			m_color_transform.read_rgba(in);
		}
				
		if (has_ratio) m_ratio = (float)in->read_u16() / (float)65535;

		if (has_name) m_name = in->read_string();

		if (has_clip_bracket) m_clip_depth = in->read_u16()+character::staticDepthOffset; 

		if (has_actions)
		{
			readPlaceActions(in, movie_version);
		}


		if (has_char == true && flag_move == true)
		{
			// Remove whatever's at m_depth, and put m_character there.
			m_place_type = REPLACE;
		}
		else if (has_char == false && flag_move == true)
		{
			// Moves the object at m_depth to the new location.
			m_place_type = MOVE;
		}
		else if (has_char == true && flag_move == false)
		{
			// Put m_character at m_depth.
			m_place_type = PLACE;
		}

		IF_VERBOSE_PARSE (
			log_parse("  PLACEOBJECT2: depth = %d (%d)", m_depth, m_depth-character::staticDepthOffset);
			if ( has_char ) log_parse("  char id = %d", m_character_id);
			if ( has_matrix ) 
			{
				log_parse("  mat:");
				m_matrix.print();
			}
			if ( has_cxform )
			{
				log_parse("  cxform:");
				m_color_transform.print();
			}
			if ( has_ratio ) log_parse("  ratio: %f", m_ratio);
			if ( has_name ) log_parse("  name = %s", m_name ? m_name : "<null>");
			if ( has_clip_bracket ) log_parse("  clip_depth = %d (%d)", m_clip_depth, m_clip_depth-character::staticDepthOffset);
			log_parse(" m_place_type: %d", m_place_type);
		);
				

                                
		//log_msg("place object at depth %i", m_depth);
	}

	// read SWF::PLACEOBJECT or SWF::PLACEOBJECT2
	void read(stream* in, tag_type tag, int movie_version)
	{

		m_tag_type = tag;

		if (tag == SWF::PLACEOBJECT)
		{
			readPlaceObject(in);
		}
		else
		{
			readPlaceObject2(in, movie_version);
		}
	}

		
	/// Place/move/whatever our object in the given movie.
	void execute(sprite_instance* m)
	{
	    switch (m_place_type) {
	      case PLACE:
		  m->add_display_object(
		      m_character_id,
		      m_name,
		      m_event_handlers,
		      m_depth,
		      m->is_reverse_execution(),	// place_object doesn't do replacement when not in reverse execution
		      m_color_transform,
		      m_matrix,
		      m_ratio,
		      m_clip_depth);
		  break;
		  
	      case MOVE:
		  m->move_display_object(
		      m_depth,
		      m_has_cxform,
		      m_color_transform,
		      m_has_matrix,
		      m_matrix,
		      m_ratio,
		      m_clip_depth);
		  break;
		  
	      case REPLACE:
		  m->replace_display_object(
		      m_character_id,
		      m_name,
		      m_depth,
		      m_has_cxform,
		      m_color_transform,
		      m_has_matrix,
		      m_matrix,
		      m_ratio,
		      m_clip_depth);
		  break;
	    }
	}
    
	/// Proxy for execute(sprite_instance*)
	void execute_state(sprite_instance* m)
	{
	    execute(m);
	}
    
	void execute_state_reverse(sprite_instance* m, int frame)
	{
	    switch (m_place_type) {
	      case PLACE:
		  // reverse of add is remove
		  m->remove_display_object(m_depth, m_tag_type == 4 ? m_character_id : -1);
		  break;
		  
	      case MOVE:
		  // reverse of move is move
		  m->move_display_object(
		      m_depth,
		      m_has_cxform,
		      m_color_transform,
		      m_has_matrix,
		      m_matrix,
		      m_ratio,
		      m_clip_depth);
		  break;
		  
	      case REPLACE:
	      {
		  // reverse of replace is to re-add the previous object.
		  execute_tag*	last_add = m->find_previous_replace_or_add_tag(frame, m_depth, -1);
		  if (last_add) {
		      last_add->execute_state(m);
		  } else {
		      log_error("reverse REPLACE can't find previous replace or add tag(%d, %d)",
				frame, m_depth);
		      
		  }
		  break;
	      }
	    }
	}
    
	virtual uint32	get_depth_id_of_replace_or_add_tag() const
	{
		uint32 depthid = 0;
		if (m_place_type == PLACE || m_place_type == REPLACE)
		{
			int id = -1;
			if (m_tag_type == SWF::PLACEOBJECT)
			{
			    // Old-style PlaceObject; the corresponding Remove
			    // is specific to the character_id.
			    id = m_character_id;
			}
			depthid = ((m_depth & 0x0FFFF) << 16) | (id & 0x0FFFF);
		}
		return depthid;
	}
};


	
void
place_object_2_loader(stream* in, tag_type tag, movie_definition* m)
{
    assert(tag == SWF::PLACEOBJECT || tag == SWF::PLACEOBJECT2);

		IF_VERBOSE_PARSE
		(
    log_parse("  place_object_2");
    		);

    place_object_2*	ch = new place_object_2;
    ch->read(in, tag, m->get_version());

    m->add_execute_tag(ch);
}

// Create and initialize a sprite, and add it to the movie.
void
sprite_loader(stream* in, tag_type tag, movie_definition* m)
{
	assert(tag == SWF::DEFINESPRITE); // 39 - DefineSprite
                
	int	character_id = in->read_u16();

		IF_VERBOSE_PARSE
		(
	log_parse("  sprite:  char id = %d", character_id);
		);

	// A DEFINESPRITE tag as part of a DEFINESPRITE
	// would be a malformed SWF
	if ( ! dynamic_cast<movie_def_impl*>(m) )
	{
		IF_VERBOSE_MALFORMED_SWF(
		log_swferror("nested DEFINESPRITE tags");
		);
	}

	// will automatically read the sprite
	sprite_definition* ch = new sprite_definition(m, in);
	//ch->read(in);

	m->add_character(character_id, ch);
}



//
// end_tag
//

// end_tag doesn't actually need to exist.

void	end_loader(stream* in, tag_type tag, movie_definition* /*m*/)
{
    assert(tag == SWF::END); // 0
    assert(in->get_position() == in->get_tag_end_position());
}


/// SWF Tag RemoveObject2 (28) 
class remove_object_2 : public execute_tag
{   
public:
    int	m_depth, m_id;
    remove_object_2() : m_depth(-1), m_id(-1) {}

    void	read(stream* in, int tag)
	{
	    assert(tag == SWF::REMOVEOBJECT || tag == SWF::REMOVEOBJECT2);

	    if (tag == SWF::REMOVEOBJECT)
		{
		    // Older SWF's allow multiple objects at the same depth;
		    // this m_id disambiguates.  Later SWF's just use one
		    // object per depth.
		    m_id = in->read_u16();
		}
	    m_depth = in->read_u16()+character::staticDepthOffset;
	}

    virtual void	execute(sprite_instance* m)
	{
	    m->remove_display_object(m_depth, m_id);
	}

    virtual void	execute_state(sprite_instance* m)
	{
	    execute(m);
	}

    virtual void	execute_state_reverse(sprite_instance* m, int frame)
	{
	    // reverse of remove is to re-add the previous object.
	    execute_tag*	last_add = m->find_previous_replace_or_add_tag(frame, m_depth, m_id);
	    if (last_add)
		{
		    last_add->execute_state(m);
		}
	    else
		{
		    log_error("reverse REMOVE can't find previous replace or add tag(%d, %d)",
			      frame, m_depth);
					
		}
	}

    virtual bool	is_remove_tag() const { return true; }
};


void	remove_object_2_loader(stream* in, tag_type tag, movie_definition* m)
{
    assert(tag == SWF::REMOVEOBJECT || tag == SWF::REMOVEOBJECT2);

    remove_object_2*	t = new remove_object_2;
    t->read(in, tag);

		IF_VERBOSE_PARSE
		(
    log_parse("  remove_object_2(%d)", t->m_depth);
    		);

    m->add_execute_tag(t);
}


void	button_sound_loader(stream* in, tag_type tag, movie_definition* m)
{
    assert(tag == SWF::DEFINEBUTTONSOUND); // 17

    int	button_character_id = in->read_u16();
    character_def* chdef = m->get_character_def(button_character_id);

    assert ( dynamic_cast<button_character_definition*> (chdef) );
    button_character_definition* ch = static_cast<button_character_definition*> (chdef);
    assert(ch != NULL);

    ch->read(in, tag, m);
}


void	button_character_loader(stream* in, tag_type tag, movie_definition* m)
{
    // 7 || 34
    assert(tag == SWF::DEFINEBUTTON || tag == SWF::DEFINEBUTTON2);

    int	character_id = in->read_u16();

		IF_VERBOSE_PARSE
		(
    log_parse("  button character loader: char_id = %d", character_id);
    		);

    button_character_definition*	ch = new button_character_definition;
    ch->read(in, tag, m);

    m->add_character(character_id, ch);
}


//
// export
//


void	export_loader(stream* in, tag_type tag, movie_definition* m)
    // Load an export tag (for exposing internal resources of m)
{
    assert(tag == SWF::EXPORTASSETS); // 56

    int	count = in->read_u16();

		IF_VERBOSE_PARSE
		(
    log_parse("  export: count = %d", count);
    		);

    // Read the exports.
    for (int i = 0; i < count; i++)
	{
	    uint16_t	id = in->read_u16();
	    char*	symbol_name = in->read_string();
		IF_VERBOSE_PARSE (
	    log_parse("  export: id = %d, name = %s", id, symbol_name);
		);

	    if (font* f = m->get_font(id))
		{
		    // Expose this font for export.
		    m->export_resource(symbol_name, f);
		}
	    else if (character_def* ch = m->get_character_def(id))
		{
		    // Expose this movie/button/whatever for export.
		    m->export_resource(symbol_name, ch);
		}
	    else if (sound_sample* ch = m->get_sound_sample(id))
		{
		    m->export_resource(symbol_name, ch);
		}
	    else
		{
		    log_warning("don't know how to export resource '%s' "
                              "with id %d (can't find that id)",
			      symbol_name, id);
		}

	    delete [] symbol_name;
	}
}


//
// import
//


void	import_loader(stream* in, tag_type tag, movie_definition* m)
{
	assert(tag == SWF::IMPORTASSETS || tag == SWF::IMPORTASSETS2);

	char* source_url = in->read_string();

	// Resolve relative urls against baseurl
	URL abs_url(source_url, get_base_url());

	unsigned char import_version = 0;

	if ( tag == SWF::IMPORTASSETS2 )
	{
		import_version = in->read_uint(8);
		unsigned char reserved = in->read_uint(8);
		UNUSED(reserved);
	}

	int	count = in->read_u16();

	IF_VERBOSE_PARSE
	(
	log_parse("  import: version = %u, source_url = %s (%s), count = %d", import_version, abs_url.str().c_str(), source_url, count);
	//log_parse("  import: version = %u, source_url = %s (%s), count = %d", import_version, abs_url.str().c_str(), source_url, count);
	);


    // Try to load the source movie into the movie library.
    movie_definition*	source_movie = NULL;

    if (s_no_recurse_while_loading == false)
	{
		try {
			source_movie = create_library_movie(abs_url);
		} catch (gnash::GnashException& e) {
			log_error("%s", e.what());
			source_movie = NULL;
		}
		if (source_movie == NULL)
		{
		    // Give up on imports.
		    log_error("can't import movie from url %s", abs_url.str().c_str());
		    return;
		}

		// Quick consistency check, we might as well do
		// something smarter, if we agree on semantic
		if (source_movie == m)
		{
		    IF_VERBOSE_MALFORMED_SWF(
		    log_swferror("Movie attempts to import symbols from itself.");
		    );
		    return;
		}
	}

    // Get the imports.
    for (int i = 0; i < count; i++)
	{
	    uint16_t	id = in->read_u16();
	    char*	symbol_name = in->read_string();
		IF_VERBOSE_PARSE
		(
	    log_parse("  import: id = %d, name = %s", id, symbol_name);
	    	);
	    
	    if (s_no_recurse_while_loading)
		{
		    m->add_import(source_url, id, symbol_name);
		}
	    else
		{
		    // @@ TODO get rid of this, always use
		    // s_no_recurse_while_loading, change
		    // create_movie().

		    boost::intrusive_ptr<resource> res = source_movie->get_exported_resource(symbol_name);
		    if (res == NULL)
			{
			    log_warning("import error: could not find resource '%s' in movie '%s'",
				      symbol_name, source_url);
			}
		    else if (font* f = res->cast_to_font())
			{
			    // Add this shared font to the currently-loading movie.
			    m->add_font(id, f);
			}
		    else if (character_def* ch = res->cast_to_character_def())
			{
			    // Add this character to the loading movie.
			    m->add_character(id, ch);
			}
		    else
			{
			    log_error("import error: resource '%s' from movie '%s' has unknown type",
				      symbol_name, source_url);
			}
		}

	    delete [] symbol_name;
	}

    delete [] source_url;
}

// Read a DefineText tag.
void	define_edit_text_loader(stream* in, tag_type tag, movie_definition* m)
{
	assert(tag == SWF::DEFINEEDITTEXT); // 37

	uint16_t	character_id = in->read_u16();

	edit_text_character_def* ch = new edit_text_character_def(m);
		IF_VERBOSE_PARSE
		(
	log_parse("edit_text_char, id = %d", character_id);
		);
	ch->read(in, tag, m);

	m->add_character(character_id, ch);
}

// See description in header
void
define_text_loader(stream* in, tag_type tag, movie_definition* m)
{
	assert(tag == SWF::DEFINETEXT || tag == SWF::DEFINETEXT2);

	uint16_t	character_id = in->read_u16();
	
	text_character_def* ch = new text_character_def(m);
		IF_VERBOSE_PARSE
		(
	log_parse("text_character, id = %d", character_id);
		);
	ch->read(in, tag, m);

	// IF_VERBOSE_PARSE(print some stuff);

	m->add_character(character_id, ch);
}

//
// do_action
//




void
do_action_loader(stream* in, tag_type tag, movie_definition* m)
{
		IF_VERBOSE_PARSE
		(
    log_parse("tag %d: do_action_loader", tag);
    log_parse("-- actions in frame " SIZET_FMT,
	       m->get_loading_frame());
		);
    
    assert(in);
    assert(tag == SWF::DOACTION); // 12
    assert(m);
    
    do_action*	da = new do_action;
    da->read(in);
    
    m->add_execute_tag(da);
}

void
do_init_action_loader(stream* in, tag_type tag, movie_definition* m)
{
	assert(tag == SWF::INITACTION); // 59


	int sprite_character_id = in->read_u16();
	UNUSED(sprite_character_id);

		IF_VERBOSE_PARSE
		(
	log_parse("  tag %d: do_init_action_loader", tag);
	log_parse("  -- init actions for sprite %d",
		   sprite_character_id);
		);

	do_action* da = new do_action;
	da->read(in);
	//m->add_init_action(sprite_character_id, da);
	m->add_init_action(da);
}

//
// Sound
//

// Load a DefineSound tag.
void
define_sound_loader(stream* in, tag_type tag, movie_definition* m)
{
	assert(tag == SWF::DEFINESOUND); // 14

	sound_handler* handler = get_sound_handler();

	uint16_t	character_id = in->read_u16();

	sound_handler::format_type	format = (sound_handler::format_type) in->read_uint(4);
	int	sample_rate = in->read_uint(2);	// multiples of 5512.5
	bool	sample_16bit = in->read_uint(1) ? true : false;
	bool	stereo = in->read_uint(1) ? true : false;
	int	sample_count = in->read_u32();

	static int	s_sample_rate_table[] = { 5512, 11025, 22050, 44100 };

	IF_VERBOSE_PARSE
	(
		log_parse("define sound: ch=%d, format=%d, "
			"rate=%d, 16=%d, stereo=%d, ct=%d",
			character_id, int(format), sample_rate,
			int(sample_16bit), int(stereo), sample_count);
	);

	// If we have a sound_handler, ask it to init this sound.
	
	if (handler)
	{
		int	data_bytes = 0;
		unsigned char*	data = NULL;

		if (! (sample_rate >= 0 && sample_rate <= 3))
		{
			IF_VERBOSE_MALFORMED_SWF(
			log_swferror("Bad sample rate read from SWF header.");
			);
                	return;
		}

		if (format == sound_handler::FORMAT_ADPCM)
		{
			// Uncompress the ADPCM before handing data to host.
			data_bytes = sample_count * (stereo ? 4 : 2);
			data = new unsigned char[data_bytes];
			sound_handler::adpcm_expand(data, in, sample_count,
				stereo);
			format = sound_handler::FORMAT_NATIVE16;
		}
		else
		{
			// @@ This is pretty awful -- lots of copying, slow reading.
			data_bytes = in->get_tag_end_position() - in->get_position();
			data = new unsigned char[data_bytes];
			for (int i = 0; i < data_bytes; i++)
			{
				data[i] = in->read_u8();
			}

			// Swap bytes on behalf of the host, to make it easier for the handler.
			// @@ I'm assuming this is a good idea?	 Most sound handlers will prefer native endianness?
			if (format == sound_handler::FORMAT_UNCOMPRESSED
			    && sample_16bit)
			{
				#ifndef _TU_LITTLE_ENDIAN_
				// Swap sample bytes to get big-endian format.
				for (int i = 0; i < data_bytes - 1; i += 2)
				{
					swap(&data[i], &data[i+1]);
				}
				#endif // not _TU_LITTLE_ENDIAN_

				format = sound_handler::FORMAT_NATIVE16;
			}
		}
		
		int	handler_id = handler->create_sound(
			data,
			data_bytes,
			sample_count,
			format,
			s_sample_rate_table[sample_rate],
			stereo);

		if (handler_id >= 0)
		{
			sound_sample* sam = new sound_sample(handler_id);
			m->add_sound_sample(character_id, sam);
		}

		delete [] data;
	}
	else
	{
		// is this nice to do?
		log_warning("There is not sound handler currently active, "
			"so character with id %d will NOT be added to "
			"the dictionary.",
			character_id);
	}
}


// Load a StartSound tag.
void
start_sound_loader(stream* in, tag_type tag, movie_definition* m)
{
	sound_handler* handler = get_sound_handler();

	assert(tag == SWF::STARTSOUND); // 15

	uint16_t	sound_id = in->read_u16();

	sound_sample* sam = m->get_sound_sample(sound_id);
	if (sam)
	{
		start_sound_tag*	sst = new start_sound_tag();
		sst->read(in, tag, m, sam);

		IF_VERBOSE_PARSE
		(
		log_parse("start_sound tag: id=%d, stop = %d, loop ct = %d",
			  sound_id, int(sst->m_stop_playback), sst->m_loop_count);
		);
	}
	else
	{
		if (handler)
		{
			IF_VERBOSE_MALFORMED_SWF(
			log_swferror("start_sound_loader: sound_id %d is not defined", sound_id);
			);
		}
	}
	
}

// Load a SoundStreamHead(2) tag.
void
sound_stream_head_loader(stream* in, tag_type tag, movie_definition* m)
{
	sound_handler* handler = get_sound_handler();

	// 18 || 45
	assert(tag == SWF::SOUNDSTREAMHEAD || tag == SWF::SOUNDSTREAMHEAD2);

	// If we don't have a sound_handler registered stop here
	if (!handler) return;

	// FIXME:
	// no character id for soundstreams... so we make one up... 
	// This only works if there is only one stream in the movie...
	// The right way to do it is to make seperate structures for streams
	// in movie_def_impl.
	
	// extract garbage data
	int	garbage = in->read_uint(8);

	sound_handler::format_type	format = static_cast<sound_handler::format_type>(in->read_uint(4));
	int	sample_rate = in->read_uint(2);	// multiples of 5512.5
	bool	sample_16bit = in->read_uint(1) ? true : false;
	bool	stereo = in->read_uint(1) ? true : false;
	
	// checks if this is a new streams header or just one in the row
	if (format == 0 && sample_rate == 0 && !sample_16bit && !stereo) return;
	
	int	sample_count = in->read_u32();
	if (format == 2) garbage = in->read_uint(16);

	static int	s_sample_rate_table[] = { 5512, 11025, 22050, 44100 };

		IF_VERBOSE_PARSE
		(
	log_parse("sound stream head: format=%d, rate=%d, 16=%d, stereo=%d, ct=%d",
		  int(format), sample_rate, int(sample_16bit), int(stereo), sample_count);
		);

	// Ask sound_handler it to init this sound.
	int	data_bytes = 0;

	if (! (sample_rate >= 0 && sample_rate <= 3))
	{
		IF_VERBOSE_MALFORMED_SWF(
		log_swferror("Bad sample rate read from SWF header.");
		);
		return;
	}

	// Since the ADPCM is converted to NATIVE16, the format is set to that...
	if (format == sound_handler::FORMAT_ADPCM) format = sound_handler::FORMAT_NATIVE16;

	int	handler_id = handler->create_sound(
		NULL,
		data_bytes,
		sample_count,
		format,
		s_sample_rate_table[sample_rate],
		stereo);
	m->set_loading_sound_stream_id(handler_id);

}


// Load a SoundStreamBlock tag.
void
sound_stream_block_loader(stream* in, tag_type tag, movie_definition* m)
{
	sound_handler* handler = get_sound_handler();

	assert(tag == SWF::SOUNDSTREAMBLOCK); // 19

	// discard garbage data
	//int	garbage = in->read_u32();
	in->skip_bytes(4);


	// If we don't have a sound_handler registered stop here
	if (!handler) return;

	int handle_id = m->get_loading_sound_stream_id();

	// store the data with the appropiate sound.
	int	data_bytes = 0;

	// @@ This is pretty awful -- lots of copying, slow reading.
	data_bytes = in->get_tag_end_position() - in->get_position();

	if (data_bytes <= 0) return;
	unsigned char *data = new unsigned char[data_bytes];


	int format = 0;
	bool stereo = true;
	int sample_count = -1;
	
	handler->get_info(handle_id, &format, &stereo);

	if (format == sound_handler::FORMAT_ADPCM)
	{
		// Uncompress the ADPCM before handing data to host.
		sample_count =  data_bytes / (stereo ? 4 : 2);
		data_bytes = sample_count * (stereo ? 4 : 2);
		data = new unsigned char[data_bytes];
		sound_handler::adpcm_expand(data, in, sample_count, stereo);
		format = sound_handler::FORMAT_NATIVE16;
	} else if (format == sound_handler::FORMAT_NATIVE16)
	{
		// Raw data
		sample_count =  data_bytes / (stereo ? 4 : 2);
		for (int i = 0; i < data_bytes; i++)
		{
			data[i] = in->read_u8();
		}

	} else {
	
		for (int i = 0; i < data_bytes; i++)
		{
			data[i] = in->read_u8();
		}

		// Swap bytes on behalf of the host, to make it easier for the handler.
		// @@ I'm assuming this is a good idea?	 Most sound handlers will prefer native endianness?
		/*if (format == sound_handler::FORMAT_UNCOMPRESSED && sample_16bit)
		{
			#ifndef _TU_LITTLE_ENDIAN_
			// Swap sample bytes to get big-endian format.
			for (int i = 0; i < data_bytes - 1; i += 2)
			{
				swap(&data[i], &data[i+1]);
			}
			#endif // not _TU_LITTLE_ENDIAN_

			format = sound_handler::FORMAT_NATIVE16;
		}*/
	}


	// Fill the data on the apropiate sound, and receives the starting point
	// for later "start playing from this frame" events.
	long start = handler->fill_stream_data(data, data_bytes, sample_count, handle_id);

	delete [] data;

	start_stream_sound_tag*	ssst = new start_stream_sound_tag();
	ssst->read(m, handle_id, start);

	// @@ who's going to delete the start_stream_sound_tag ??

}

void
define_video_loader(stream* in, tag_type tag, movie_definition* m)
{
	assert(tag == SWF::DEFINEVIDEOSTREAM); // 60
	uint16_t character_id = in->read_u16();

	video_stream_definition* ch = new video_stream_definition(character_id);
	ch->read(in, tag, m);

	m->add_character(character_id, ch);

}

void
video_loader(stream* in, tag_type tag, movie_definition* m)
{
	assert(tag == SWF::VIDEOFRAME); // 61

	uint16_t character_id = in->read_u16();
	character_def* chdef = m->get_character_def(character_id);

	assert ( dynamic_cast<video_stream_definition*> (chdef) );
	video_stream_definition* ch = static_cast<video_stream_definition*> (chdef);
	assert(ch != NULL);

	ch->read(in, tag, m);

}

void
file_attributes_loader(stream* in, tag_type tag, movie_definition* /*m*/)
{
	assert(tag == SWF::FILEATTRIBUTES); // 69

	typedef struct file_attrs_flags_t {
		unsigned reserved1:3;
		unsigned has_metadata:1;
		unsigned reserved2:3;
		unsigned use_network:1;
		unsigned reserved3:24;
	} file_attrs_flags;

	file_attrs_flags flags;

	flags.reserved1 = in->read_uint(3);
	flags.has_metadata = in->read_uint(1);
	flags.reserved2 = in->read_uint(3);
	flags.use_network = in->read_uint(1);
	flags.reserved3 = in->read_uint(24);

	IF_VERBOSE_PARSE
	(
		log_parse("  file attributes: has_metadata=%s use_network=%s",
			flags.has_metadata ? "true" : "false",
			flags.use_network ? "true" : "false")
	);

	if ( ! flags.use_network )
	{
		log_warning("FileAttributes tag in the SWF requests that "
			"network access is not granted to this movie "
			"(or application?). Anyway Gnash won't care "
			"use white/black listing in your .gnashrc instead");
	}

	// TODO: attach info to movie_definition 

}

void
metadata_loader(stream* in, tag_type tag, movie_definition* /*m*/)
{
	assert(tag == SWF::METADATA); // 77

	// this is supposed to be an XML string
	char* metadata = in->read_string();

	IF_VERBOSE_PARSE (
		log_parse("  metadata = [[\n%s\n]]", metadata);
	);

	log_warning("METADATA tag parsed but unused");

	// TODO: attach to movie_definition instead
	//       (should we parse the XML maybe?)

	delete [] metadata;

}

void
serialnumber_loader(stream* in, tag_type tag, movie_definition* /*m*/)
{
	assert(tag == SWF::SERIALNUMBER); // 41

	std::string serial;
	in->read_string_with_length(in->get_tag_length(), serial);

	IF_VERBOSE_PARSE (
		log_parse("  serialnumber = [[\n%s\n]]", serial.c_str());
	);

	log_msg("SERIALNUMBER: %s", serial.c_str());

	// attach to movie_definition ?

}




} // namespace gnash::SWF::tag_loaders
} // namespace gnash::SWF
} // namespace gnash

// Local Variables:
// mode: C++
// indent-tabs-mode: t
// End:
