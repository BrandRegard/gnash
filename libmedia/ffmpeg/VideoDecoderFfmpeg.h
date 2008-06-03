// VideoDecoderFfmpeg.h: Video decoding using the FFMPEG library.
// 
//   Copyright (C) 2007, 2008 Free Software Foundation, Inc.
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


#ifndef __VIDEODECODERFFMPEG_H__
#define __VIDEODECODERFFMPEG_H__

#ifdef HAVE_CONFIG_H
#include "gnashconfig.h"
#endif

#include "dsodefs.h" //For DSOEXPORT
#include "log.h"
#include "VideoDecoder.h"
#include "MediaParser.h" // for videoCodecType enum

#ifdef HAVE_FFMPEG_AVCODEC_H
extern "C" {
# include "ffmpeg/avcodec.h"
}
#endif

#ifdef HAVE_LIBAVCODEC_AVCODEC_H
extern "C" {
# include "libavcodec/avcodec.h"
}
#endif

namespace gnash {
namespace media {


class VideoDecoderFfmpeg : public VideoDecoder {
  
public:
  DSOEXPORT VideoDecoderFfmpeg(videoCodecType format, int width, int height);
  DSOEXPORT ~VideoDecoderFfmpeg();
  
  void push(const EncodedVideoFrame& buffer);

  std::auto_ptr<image::rgb> pop();
  
  bool peek();
  
  
  /// \brief converts an video frame from (almost) any type to RGB24.
  ///
  /// @param srcCtx The source context that was used to decode srcFrame.
  /// @param srcFrame the source frame to be converted.
  /// @return an AVPicture containing the converted image. Please be advised
  ///         that the RGB data pointer is stored in AVPicture::data[0]. The
  ///         caller owns that pointer, which must be freed with delete [].
  ///         It is advised to wrap the pointer in a boost::scoped_array.
  ///         If conversion fails, AVPicture::data[0] will be NULL.
    DSOEXPORT static AVPicture convertRGB24(AVCodecContext* srcCtx, const AVFrame& srcFrame);

private:

  std::auto_ptr<image::rgb> decode(const boost::uint8_t* input, boost::uint32_t input_size);

  std::auto_ptr<image::rgb> decode(const EncodedVideoFrame* vf)
  {
  	return decode(vf->data(), vf->dataSize());
  }
private:

  AVCodec* _videoCodec;
  AVCodecContext* _videoCodecCtx;
  std::vector<const EncodedVideoFrame*> _video_frames;
};
  
} // gnash.media namespace 
} // gnash namespace

#endif // __VIDEODECODERFFMPEG_H__
