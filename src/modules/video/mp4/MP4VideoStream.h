/**
 * Copyright (c) 2006-2026 LOVE Development Team
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 **/

#ifndef LOVE_VIDEO_MP4_MP4VIDEOSTREAM_H
#define LOVE_VIDEO_MP4_MP4VIDEOSTREAM_H

#include "video/VideoStream.h"

// LOVE
#include "common/int.h"
#include "filesystem/File.h"
#include "thread/threads.h"

#include <string>
#include <vector>

// FFmpeg (C API)
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavformat/avio.h>
#include <libavutil/imgutils.h>
}

namespace love
{
namespace video
{
namespace mp4
{

/**
 * VideoStream backend for MP4 (and any other container FFmpeg's demuxer
 * understands) using libavformat/libavcodec for demuxing+decoding and
 * libswscale to convert the decoded frame into planar YUV420, which is
 * the pixel format the rest of love.video (the Video FX/shader pipeline)
 * already expects, mirroring what TheoraVideoStream provides.
 *
 * Design notes:
 * - love::filesystem::File is wrapped in a custom AVIOContext so FFmpeg
 *   reads through LOVE's own filesystem/physfs layer instead of touching
 *   the OS filesystem directly (consistent with TheoraVideoStream+OggDemuxer).
 * - Only the first video stream found in the container is decoded here.
 *   Audio, if present, is intentionally NOT handled by this class -- for
 *   audio+video sync, the expectation (same as for Theora/Ogg files today)
 *   is that the game also loads the file as an audio Source and drives
 *   playback via VideoStream::SourceSync.
 * - Threading/double buffering follows the exact same contract as
 *   TheoraVideoStream: fillBackBuffer() is a no-op (work happens on the
 *   shared video worker thread via threadedFillBackBuffer), and
 *   swapBuffers() atomically flips front/back under bufferMutex.
 **/
class MP4VideoStream : public love::video::VideoStream
{
public:

	MP4VideoStream(love::filesystem::File *file);
	~MP4VideoStream();

	const void *getFrontBuffer() const override;
	size_t getSize() const override;
	void fillBackBuffer() override;
	bool swapBuffers() override;

	int getWidth() const override;
	int getHeight() const override;
	const std::string &getFilename() const override;
	void setSync(FrameSync *frameSync) override;

	bool isPlaying() const override;

	// Called from the shared video worker thread, same as Theora's.
	void threadedFillBackBuffer(double dt);

	// Returns true if the given file looks like an MP4/MOV/ISO-BMFF
	// (or any other FFmpeg-openable) container with a decodable video
	// stream. Used by video::Video::newVideoStream() to pick a backend.
	static bool isSupported(love::filesystem::File *file);

private:

	// Custom AVIO glue so FFmpeg reads via love::filesystem::File.
	static int avioRead(void *opaque, uint8_t *buf, int bufSize);
	static int64_t avioSeek(void *opaque, int64_t offset, int whence);

	void openStream();
	void closeStream();
	bool decodeNextFrame(); // decodes into decodedFrame/scaledFrame
	// Returns true if seeking landed on and decoded at least one frame.
	bool seekDecoder(double target);
	void copyFrameToBackBuffer();

	StrongRef<love::filesystem::File> file;
	std::string filename;

	// libav state
	AVIOContext *avioContext;
	unsigned char *avioBuffer;
	AVFormatContext *formatContext;
	AVCodecContext *codecContext;
	SwsContext *swsContext;
	AVFrame *decodedFrame;
	AVFrame *scaledFrame; // always AV_PIX_FMT_YUV420P, matches Frame layout
	AVPacket *packet;
	int videoStreamIndex;
	double timeBase; // seconds per pts unit for the video stream

	bool headerParsed;
	bool eos;

	Frame *frontBuffer;
	Frame *backBuffer;

	love::thread::MutexRef bufferMutex;
	bool frameReady;

	double lastFrame;
	double nextFrame;

}; // MP4VideoStream

} // mp4
} // video
} // love

#endif // LOVE_VIDEO_MP4_MP4VIDEOSTREAM_H
