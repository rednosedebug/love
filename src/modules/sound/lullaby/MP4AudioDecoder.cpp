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

#include "MP4AudioDecoder.h"
#include "common/Exception.h"

#include <cstring>
#include <algorithm>

// This file uses the "new" (FFmpeg >= 5.1, released 2022) AVChannelLayout
// API (av_channel_layout_default, swr_alloc_set_opts2, ch_layout on
// AVCodecContext) instead of the deprecated integer channel-count/
// uint64_t channel-mask APIs it replaced. FFmpeg 5.1 is roughly two
// years old at the time of writing and is what current Debian/Ubuntu
// LTS, Homebrew, and vcpkg ship, but if you're targeting an older
// FFmpeg you'll need to swap these calls for swr_alloc_set_opts() +
// av_get_default_channel_layout() instead.

namespace love
{
namespace sound
{
namespace lullaby
{

static const int AVIO_BUFFER_SIZE = 32 * 1024;

MP4AudioDecoder::MP4AudioDecoder(Stream *stream, int bufferSize)
	: Decoder(stream, bufferSize)
	, avioContext(nullptr)
	, avioBuffer(nullptr)
	, formatContext(nullptr)
	, codecContext(nullptr)
	, swrContext(nullptr)
	, decodedFrame(nullptr)
	, packet(nullptr)
	, audioStreamIndex(-1)
	, timeBase(0.0)
	, duration(-1.0)
	, channels(DEFAULT_CHANNELS)
	, sampleRate(DEFAULT_SAMPLE_RATE)
	, pendingPCMOffset(0)
{
	try
	{
		openStream();
	}
	catch (love::Exception &)
	{
		closeStream();
		throw;
	}
}

MP4AudioDecoder::~MP4AudioDecoder()
{
	closeStream();
}

int MP4AudioDecoder::avioReadThunk(void *opaque, uint8_t *buf, int bufSize)
{
	auto *self = static_cast<MP4AudioDecoder *>(opaque);
	int64 read = self->stream->read(buf, bufSize);
	if (read <= 0)
		return AVERROR_EOF;
	return (int) read;
}

int64_t MP4AudioDecoder::avioSeekThunk(void *opaque, int64_t offset, int whence)
{
	auto *self = static_cast<MP4AudioDecoder *>(opaque);

	if (whence == AVSEEK_SIZE)
		return (int64_t) self->stream->getSize();

	int64_t base = 0;
	if (whence == SEEK_CUR)
		base = self->stream->tell();
	else if (whence == SEEK_END)
		base = (int64_t) self->stream->getSize();
	else if (whence != SEEK_SET)
		return -1;

	int64_t target = (whence == SEEK_SET) ? offset : base + offset;
	if (!self->stream->seek(target))
		return -1;

	return target;
}

void MP4AudioDecoder::openStream()
{
	// Decoder's base constructor already validated stream->isReadable()
	// and stream->isSeekable(), so we can rely on both here.
	stream->seek(0);

	avioBuffer = (unsigned char *) av_malloc(AVIO_BUFFER_SIZE);
	if (!avioBuffer)
		throw love::Exception("Out of memory allocating AVIO buffer");

	avioContext = avio_alloc_context(
		avioBuffer, AVIO_BUFFER_SIZE, 0, this,
		&MP4AudioDecoder::avioReadThunk, nullptr, &MP4AudioDecoder::avioSeekThunk);

	if (!avioContext)
		throw love::Exception("Could not create AVIO context for MP4 audio stream");

	formatContext = avformat_alloc_context();
	if (!formatContext)
		throw love::Exception("Could not allocate AVFormatContext");

	formatContext->pb = avioContext;
	formatContext->flags |= AVFMT_FLAG_CUSTOM_IO;

	// Deliberately do NOT pass a format hint: if this isn't a container
	// libavformat recognizes at all, we want avformat_open_input to fail
	// fast so Sound::newDecoder() falls through to the next decoder in
	// its list, exactly like every other Decoder subclass here does by
	// throwing from its constructor on a format mismatch.
	if (avformat_open_input(&formatContext, nullptr, nullptr, nullptr) != 0)
		throw love::Exception("Not a container FFmpeg recognizes");

	if (avformat_find_stream_info(formatContext, nullptr) < 0)
		throw love::Exception("Could not find stream info in container");

	audioStreamIndex = -1;
	for (unsigned int i = 0; i < formatContext->nb_streams; ++i)
	{
		if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			audioStreamIndex = (int) i;
			break;
		}
	}

	if (audioStreamIndex < 0)
		throw love::Exception("No audio stream found in container");

	AVStream *avstream = formatContext->streams[audioStreamIndex];
	const AVCodec *codec = avcodec_find_decoder(avstream->codecpar->codec_id);
	if (!codec)
		throw love::Exception("Unsupported audio codec in container");

	codecContext = avcodec_alloc_context3(codec);
	if (!codecContext)
		throw love::Exception("Could not allocate audio codec context");

	if (avcodec_parameters_to_context(codecContext, avstream->codecpar) < 0)
		throw love::Exception("Could not copy audio codec parameters");

	if (avcodec_open2(codecContext, codec, nullptr) < 0)
		throw love::Exception("Could not open audio codec");

	timeBase = av_q2d(avstream->time_base);
	if (avstream->duration != AV_NOPTS_VALUE)
		duration = avstream->duration * timeBase;
	else if (formatContext->duration != AV_NOPTS_VALUE)
		duration = formatContext->duration / (double) AV_TIME_BASE;

	channels = codecContext->ch_layout.nb_channels;
	if (channels <= 0)
		channels = DEFAULT_CHANNELS;
	// LOVE's audio pipeline (like most consumers here) expects mono or
	// stereo; downmix anything wider (5.1, 7.1, etc.) to stereo.
	if (channels > 2)
		channels = 2;

	sampleRate = codecContext->sample_rate > 0 ? codecContext->sample_rate : DEFAULT_SAMPLE_RATE;

	AVChannelLayout outLayout;
	av_channel_layout_default(&outLayout, channels);

	if (swr_alloc_set_opts2(
			&swrContext,
			&outLayout, AV_SAMPLE_FMT_S16, sampleRate,
			&codecContext->ch_layout, codecContext->sample_fmt, codecContext->sample_rate,
			0, nullptr) < 0 || !swrContext)
	{
		av_channel_layout_uninit(&outLayout);
		throw love::Exception("Could not create audio resampler context");
	}

	av_channel_layout_uninit(&outLayout);

	if (swr_init(swrContext) < 0)
		throw love::Exception("Could not initialize audio resampler");

	decodedFrame = av_frame_alloc();
	packet = av_packet_alloc();

	if (!decodedFrame || !packet)
		throw love::Exception("Could not allocate AVFrame/AVPacket for audio");
}

void MP4AudioDecoder::closeStream()
{
	if (swrContext) { swr_free(&swrContext); swrContext = nullptr; }
	if (packet) { av_packet_free(&packet); packet = nullptr; }
	if (decodedFrame) { av_frame_free(&decodedFrame); decodedFrame = nullptr; }
	if (codecContext) { avcodec_free_context(&codecContext); codecContext = nullptr; }
	if (formatContext) { avformat_close_input(&formatContext); formatContext = nullptr; }
	if (avioContext)
	{
		av_freep(&avioContext->buffer);
		avio_context_free(&avioContext);
		avioContext = nullptr;
	}
	avioBuffer = nullptr;
}

love::sound::Decoder *MP4AudioDecoder::clone()
{
	StrongRef<Stream> s(stream->clone(), Acquire::NORETAIN);
	return new MP4AudioDecoder(s.get(), bufferSize);
}

bool MP4AudioDecoder::appendConvertedFrame()
{
	int outSamples = swr_get_out_samples(swrContext, decodedFrame->nb_samples);
	if (outSamples < 0)
		outSamples = decodedFrame->nb_samples;

	size_t outBytes = (size_t) outSamples * channels * sizeof(int16_t);
	size_t writeOffset = pendingPCM.size();
	pendingPCM.resize(writeOffset + outBytes);

	uint8_t *outPtr = pendingPCM.data() + writeOffset;
	int converted = swr_convert(swrContext, &outPtr, outSamples,
		(const uint8_t **) decodedFrame->data, decodedFrame->nb_samples);

	size_t actualBytes = converted > 0 ? (size_t) converted * channels * sizeof(int16_t) : 0;
	pendingPCM.resize(writeOffset + actualBytes);

	return actualBytes > 0;
}

bool MP4AudioDecoder::decodeOnePacket()
{
	bool flushSent = false;

	while (true)
	{
		int recvRet = avcodec_receive_frame(codecContext, decodedFrame);
		if (recvRet == 0)
			return appendConvertedFrame();

		if (recvRet != AVERROR(EAGAIN) && recvRet != AVERROR_EOF)
		{
			eof = true;
			return false;
		}

		if (recvRet == AVERROR_EOF)
		{
			// Decoder fully drained after our flush packet: genuinely done.
			eof = true;
			return false;
		}

		// EAGAIN: the decoder needs more input before it can output a frame.
		int readRet = av_read_frame(formatContext, packet);
		if (readRet < 0)
		{
			// No more packets in the container. Send a flush packet once so
			// any frames the decoder was holding back get emitted, then loop
			// back to avcodec_receive_frame above to collect them.
			if (!flushSent)
			{
				avcodec_send_packet(codecContext, nullptr);
				flushSent = true;
				continue;
			}

			eof = true;
			return false;
		}

		if (packet->stream_index == audioStreamIndex)
			avcodec_send_packet(codecContext, packet);

		av_packet_unref(packet);
	}
}

int MP4AudioDecoder::decode()
{
	// Drain pendingPCM into `buffer` (bufferSize bytes, from the base
	// Decoder class) first; only pull more from FFmpeg once it's empty.
	while (pendingPCM.size() - pendingPCMOffset < (size_t) bufferSize && !eof)
	{
		if (!decodeOnePacket())
			break;
	}

	size_t available = pendingPCM.size() - pendingPCMOffset;
	size_t toCopy = std::min(available, (size_t) bufferSize);

	if (toCopy > 0)
	{
		memcpy(buffer, pendingPCM.data() + pendingPCMOffset, toCopy);
		pendingPCMOffset += toCopy;
	}

	// Compact the buffer periodically so it doesn't grow unbounded over
	// a long stream.
	if (pendingPCMOffset > 0 && pendingPCMOffset == pendingPCM.size())
	{
		pendingPCM.clear();
		pendingPCMOffset = 0;
	}
	else if (pendingPCMOffset > (size_t) bufferSize * 4)
	{
		pendingPCM.erase(pendingPCM.begin(), pendingPCM.begin() + pendingPCMOffset);
		pendingPCMOffset = 0;
	}

	return (int) toCopy;
}

bool MP4AudioDecoder::seek(double s)
{
	int64_t ts = (int64_t) (s / timeBase);

	if (av_seek_frame(formatContext, audioStreamIndex, ts, AVSEEK_FLAG_BACKWARD) < 0)
		return false;

	avcodec_flush_buffers(codecContext);
	pendingPCM.clear();
	pendingPCMOffset = 0;
	eof = false;

	return true;
}

bool MP4AudioDecoder::rewind()
{
	return seek(0.0);
}

bool MP4AudioDecoder::isSeekable()
{
	return true;
}

int MP4AudioDecoder::getChannelCount() const
{
	return channels;
}

int MP4AudioDecoder::getBitDepth() const
{
	return 16;
}

int MP4AudioDecoder::getSampleRate() const
{
	return sampleRate;
}

double MP4AudioDecoder::getDuration()
{
	return duration;
}

} // lullaby
} // sound
} // love
