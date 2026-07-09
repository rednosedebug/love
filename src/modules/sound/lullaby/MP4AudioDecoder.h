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

#ifndef LOVE_SOUND_LULLABY_MP4_AUDIO_DECODER_H
#define LOVE_SOUND_LULLABY_MP4_AUDIO_DECODER_H

// LOVE
#include "common/Stream.h"
#include "common/int.h"
#include "sound/Decoder.h"

#include <vector>
#include <cstdint>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavformat/avio.h>
}

namespace love
{
namespace sound
{
namespace lullaby
{

/**
 * Decoder implementation that extracts and decodes the first audio track
 * out of an MP4/MOV (or any other libavformat-supported) container, via
 * FFmpeg. This lets love.audio.newSource("video.mp4") work out of the box,
 * and — combined with love::video::mp4::MP4VideoStream for the picture —
 * gives games a way to play back an MP4's audio and video in sync using
 * the existing VideoStream::SourceSync mechanism:
 *
 *   local video  = love.graphics.newVideo("clip.mp4")
 *   local source = love.audio.newSource("clip.mp4", "stream")
 *   video:setSource(source)  -- wires up SourceSync under the hood
 *   source:play()
 *   video:play()
 *
 * This mirrors how VorbisDecoder/MP3Decoder etc. work: Decoder subclasses
 * only deal with audio, resampled/converted to a format
 * love::sound::SoundData / love::audio::Source already understands
 * (signed 16-bit PCM, interleaved).
 **/
class MP4AudioDecoder : public Decoder
{
public:

	MP4AudioDecoder(Stream *stream, int bufferSize);
	virtual ~MP4AudioDecoder();

	love::sound::Decoder *clone() override;
	int decode() override;
	bool seek(double s) override;
	bool rewind() override;
	bool isSeekable() override;
	int getChannelCount() const override;
	int getBitDepth() const override;
	int getSampleRate() const override;
	double getDuration() override;

private:

	static int avioReadThunk(void *opaque, uint8_t *buf, int bufSize);
	static int64_t avioSeekThunk(void *opaque, int64_t offset, int whence);

	void openStream();
	void closeStream();

	// Pulls and decodes packets until at least one resampled frame's
	// worth of PCM has been appended to pendingPCM, or EOF/error.
	// Returns false on EOF/unrecoverable error.
	bool decodeOnePacket();

	// Converts decodedFrame (already received from the codec) through
	// swrContext and appends the result to pendingPCM. Returns false if
	// the conversion produced zero samples.
	bool appendConvertedFrame();

	AVIOContext *avioContext;
	unsigned char *avioBuffer;
	AVFormatContext *formatContext;
	AVCodecContext *codecContext;
	SwrContext *swrContext;
	AVFrame *decodedFrame;
	AVPacket *packet;

	int audioStreamIndex;
	double timeBase;
	double duration;

	int channels;
	int sampleRate;

	// PCM (interleaved, signed 16-bit) not yet consumed by decode().
	// FFmpeg frames don't line up with LOVE's fixed bufferSize, so we
	// buffer the remainder here between decode() calls.
	std::vector<uint8_t> pendingPCM;
	size_t pendingPCMOffset;

}; // MP4AudioDecoder

} // lullaby
} // sound
} // love

#endif // LOVE_SOUND_LULLABY_MP4_AUDIO_DECODER_H
