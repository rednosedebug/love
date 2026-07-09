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

#include "MP4VideoStream.h"
#include "common/Exception.h"

#include <cstring>
#include <algorithm>

namespace love
{
namespace video
{
namespace mp4
{

static const int AVIO_BUFFER_SIZE = 64 * 1024;

MP4VideoStream::MP4VideoStream(love::filesystem::File *file)
	: file(file)
	, avioContext(nullptr)
	, avioBuffer(nullptr)
	, formatContext(nullptr)
	, codecContext(nullptr)
	, swsContext(nullptr)
	, decodedFrame(nullptr)
	, scaledFrame(nullptr)
	, packet(nullptr)
	, videoStreamIndex(-1)
	, timeBase(0.0)
	, headerParsed(false)
	, eos(false)
	, frontBuffer(nullptr)
	, backBuffer(nullptr)
	, frameReady(false)
	, lastFrame(0)
	, nextFrame(0)
{
	filename = file->getFilename();
	// bufferMutex (MutexRef) initializes itself via its own default
	// constructor -- no explicit setup needed here, same as
	// TheoraVideoStream's equivalent member.

	frontBuffer = new Frame();
	backBuffer = new Frame();

	try
	{
		openStream();
	}
	catch (love::Exception &)
	{
		closeStream();
		delete backBuffer;
		delete frontBuffer;
		throw;
	}

	frameSync.set(new DeltaSync(), Acquire::NORETAIN);
}

MP4VideoStream::~MP4VideoStream()
{
	closeStream();
	delete frontBuffer;
	delete backBuffer;
}

int MP4VideoStream::avioRead(void *opaque, uint8_t *buf, int bufSize)
{
	auto *self = static_cast<MP4VideoStream *>(opaque);
	int64 read = self->file->read(buf, bufSize);
	if (read <= 0)
		return AVERROR_EOF;
	return (int) read;
}

int64_t MP4VideoStream::avioSeek(void *opaque, int64_t offset, int whence)
{
	auto *self = static_cast<MP4VideoStream *>(opaque);

	if (whence == AVSEEK_SIZE)
		return (int64_t) self->file->getSize();

	int64_t base = 0;
	if (whence == SEEK_CUR)
		base = self->file->tell();
	else if (whence == SEEK_END)
		base = (int64_t) self->file->getSize();
	else if (whence != SEEK_SET)
		return -1;

	int64_t target = (whence == SEEK_SET) ? offset : base + offset;
	if (!self->file->seek(target))
		return -1;

	return target;
}

bool MP4VideoStream::isSupported(love::filesystem::File *file)
{
	// Cheap probe: try to open an AVFormatContext against the file using
	// the same custom AVIO glue used for real playback, and check that
	// there's at least one decodable video stream. This lets
	// video::Video::newVideoStream() dispatch to this backend for MP4/MOV
	// containers (and anything else FFmpeg understands) while still
	// falling back to Theora for classic .ogv files.
	bool wasOpen = file->isOpen();
	if (!wasOpen && !file->open(love::filesystem::File::MODE_READ))
		return false;

	int64 originalPos = file->tell();
	file->seek(0);

	unsigned char *probeBuffer = (unsigned char *) av_malloc(AVIO_BUFFER_SIZE);
	bool ok = false;

	// We can't fully construct MP4VideoStream just to probe (its constructor
	// eagerly decodes the first frame), so do a minimal open/close pass here.
	// avioRead/avioSeek only ever dereference `self->file`, and a
	// MP4VideoStream's `file` member is its very first field, initialized
	// before anything else -- but to avoid relying on that layout, use a
	// tiny local struct with the same first-member shape instead.
	struct ProbeOpaque { StrongRef<love::filesystem::File> file; };
	ProbeOpaque probeOpaque;
	probeOpaque.file = file;

	AVFormatContext *fmt = avformat_alloc_context();
	AVIOContext *io = avio_alloc_context(
		probeBuffer, AVIO_BUFFER_SIZE, 0, &probeOpaque,
		[](void *opaque, uint8_t *buf, int bufSize) -> int {
			auto *p = static_cast<ProbeOpaque *>(opaque);
			int64 read = p->file->read(buf, bufSize);
			if (read <= 0)
				return AVERROR_EOF;
			return (int) read;
		},
		nullptr,
		[](void *opaque, int64_t offset, int whence) -> int64_t {
			auto *p = static_cast<ProbeOpaque *>(opaque);
			if (whence == AVSEEK_SIZE)
				return (int64_t) p->file->getSize();
			int64_t base = 0;
			if (whence == SEEK_CUR)
				base = p->file->tell();
			else if (whence == SEEK_END)
				base = (int64_t) p->file->getSize();
			else if (whence != SEEK_SET)
				return -1;
			int64_t target = (whence == SEEK_SET) ? offset : base + offset;
			if (!p->file->seek(target))
				return -1;
			return target;
		});

	if (fmt && io)
	{
		fmt->pb = io;
		fmt->flags |= AVFMT_FLAG_CUSTOM_IO;

		if (avformat_open_input(&fmt, nullptr, nullptr, nullptr) == 0)
		{
			if (avformat_find_stream_info(fmt, nullptr) >= 0)
			{
				for (unsigned int i = 0; i < fmt->nb_streams; ++i)
				{
					if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
					{
						ok = true;
						break;
					}
				}
			}
			avformat_close_input(&fmt);
		}
		else if (fmt)
		{
			avformat_free_context(fmt);
		}
	}

	if (io)
	{
		av_freep(&io->buffer);
		avio_context_free(&io);
	}

	file->seek(originalPos);
	if (!wasOpen)
		file->close();

	return ok;
}

void MP4VideoStream::openStream()
{
	if (!file->isOpen() && !file->open(love::filesystem::File::MODE_READ))
		throw love::Exception("Could not open MP4 file for reading");

	file->seek(0);

	avioBuffer = (unsigned char *) av_malloc(AVIO_BUFFER_SIZE);
	if (!avioBuffer)
		throw love::Exception("Out of memory allocating AVIO buffer");

	avioContext = avio_alloc_context(
		avioBuffer, AVIO_BUFFER_SIZE, 0, this,
		&MP4VideoStream::avioRead, nullptr, &MP4VideoStream::avioSeek);

	if (!avioContext)
		throw love::Exception("Could not create AVIO context for MP4 stream");

	formatContext = avformat_alloc_context();
	if (!formatContext)
		throw love::Exception("Could not allocate AVFormatContext");

	formatContext->pb = avioContext;
	formatContext->flags |= AVFMT_FLAG_CUSTOM_IO;

	if (avformat_open_input(&formatContext, nullptr, nullptr, nullptr) != 0)
		throw love::Exception("Could not parse MP4/MOV container");

	if (avformat_find_stream_info(formatContext, nullptr) < 0)
		throw love::Exception("Could not find stream info in MP4/MOV container");

	videoStreamIndex = -1;
	for (unsigned int i = 0; i < formatContext->nb_streams; ++i)
	{
		if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoStreamIndex = (int) i;
			break;
		}
	}

	if (videoStreamIndex < 0)
		throw love::Exception("No video stream found in MP4/MOV container");

	AVStream *stream = formatContext->streams[videoStreamIndex];
	const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
	if (!codec)
		throw love::Exception("Unsupported video codec in MP4/MOV container "
			"(LOVE requires an FFmpeg build with a decoder for this codec, "
			"e.g. H.264)");

	codecContext = avcodec_alloc_context3(codec);
	if (!codecContext)
		throw love::Exception("Could not allocate codec context");

	if (avcodec_parameters_to_context(codecContext, stream->codecpar) < 0)
		throw love::Exception("Could not copy codec parameters");

	// Multi-threaded decode; harmless if the codec doesn't support it.
	codecContext->thread_count = 0;
	codecContext->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

	if (avcodec_open2(codecContext, codec, nullptr) < 0)
		throw love::Exception("Could not open video codec");

	timeBase = av_q2d(stream->time_base);

	decodedFrame = av_frame_alloc();
	scaledFrame = av_frame_alloc();
	packet = av_packet_alloc();

	if (!decodedFrame || !scaledFrame || !packet)
		throw love::Exception("Could not allocate AVFrame/AVPacket");

	int w = codecContext->width;
	int h = codecContext->height;

	if (w <= 0 || h <= 0)
		throw love::Exception("Invalid video dimensions in MP4/MOV container");

	// Convert whatever the decoder outputs into planar YUV420, matching
	// the plane layout TheoraVideoStream/Video already produce for the
	// FX pipeline (love::video::VideoStream::Frame: separate Y/Cb/Cr).
	swsContext = sws_getContext(
		w, h, codecContext->pix_fmt,
		w, h, AV_PIX_FMT_YUV420P,
		SWS_BILINEAR, nullptr, nullptr, nullptr);

	if (!swsContext)
		throw love::Exception("Could not create YUV420 conversion context");

	scaledFrame->format = AV_PIX_FMT_YUV420P;
	scaledFrame->width = w;
	scaledFrame->height = h;

	if (av_frame_get_buffer(scaledFrame, 32) < 0)
		throw love::Exception("Could not allocate scaled frame buffer");

	Frame *buffers[2] = {backBuffer, frontBuffer};
	for (int i = 0; i < 2; i++)
	{
		buffers[i]->yw = w;
		buffers[i]->yh = h;
		buffers[i]->cw = w / 2;
		buffers[i]->ch = h / 2;

		buffers[i]->yplane = new unsigned char[buffers[i]->yw * buffers[i]->yh];
		buffers[i]->cbplane = new unsigned char[buffers[i]->cw * buffers[i]->ch];
		buffers[i]->crplane = new unsigned char[buffers[i]->cw * buffers[i]->ch];

		memset(buffers[i]->yplane, 16, buffers[i]->yw * buffers[i]->yh);
		memset(buffers[i]->cbplane, 128, buffers[i]->cw * buffers[i]->ch);
		memset(buffers[i]->crplane, 128, buffers[i]->cw * buffers[i]->ch);
	}

	headerParsed = true;

	// Prime the pipeline with the first frame so getWidth/getHeight-based
	// consumers immediately have valid data, mirroring Theora's behavior
	// of decoding the very first packet during header parsing. If the
	// very first frame can't be decoded (corrupt/empty stream), leave the
	// buffers at their neutral-gray defaults set above instead of copying
	// uninitialized decoder output.
	if (decodeNextFrame())
	{
		copyFrameToBackBuffer();
		love::thread::Lock l(bufferMutex);
		frameReady = true;
	}
}

void MP4VideoStream::closeStream()
{
	if (swsContext) { sws_freeContext(swsContext); swsContext = nullptr; }
	if (packet) { av_packet_free(&packet); packet = nullptr; }
	if (decodedFrame) { av_frame_free(&decodedFrame); decodedFrame = nullptr; }
	if (scaledFrame) { av_frame_free(&scaledFrame); scaledFrame = nullptr; }
	if (codecContext) { avcodec_free_context(&codecContext); codecContext = nullptr; }
	if (formatContext) { avformat_close_input(&formatContext); formatContext = nullptr; }
	if (avioContext)
	{
		av_freep(&avioContext->buffer);
		avio_context_free(&avioContext);
		avioContext = nullptr;
	}
	// avioBuffer ownership was handed to avioContext; don't double free.
	avioBuffer = nullptr;
}

int MP4VideoStream::getWidth() const
{
	return headerParsed ? codecContext->width : 0;
}

int MP4VideoStream::getHeight() const
{
	return headerParsed ? codecContext->height : 0;
}

const std::string &MP4VideoStream::getFilename() const
{
	return filename;
}

void MP4VideoStream::setSync(FrameSync *newSync)
{
	love::thread::Lock l(bufferMutex);
	this->frameSync = newSync;
}

const void *MP4VideoStream::getFrontBuffer() const
{
	return frontBuffer;
}

size_t MP4VideoStream::getSize() const
{
	return sizeof(Frame);
}

bool MP4VideoStream::isPlaying() const
{
	return frameSync->isPlaying() && !eos;
}

bool MP4VideoStream::decodeNextFrame()
{
	while (true)
	{
		int recvRet = avcodec_receive_frame(codecContext, decodedFrame);
		if (recvRet == 0)
		{
			sws_scale(swsContext,
				decodedFrame->data, decodedFrame->linesize,
				0, codecContext->height,
				scaledFrame->data, scaledFrame->linesize);

			int64_t pts = decodedFrame->best_effort_timestamp;
			if (pts == AV_NOPTS_VALUE)
				pts = decodedFrame->pts;

			lastFrame = nextFrame;
			nextFrame = (pts == AV_NOPTS_VALUE) ? nextFrame : pts * timeBase;
			return true;
		}
		else if (recvRet == AVERROR(EAGAIN))
		{
			// Needs more packets before it can produce a frame.
			int readRet = av_read_frame(formatContext, packet);
			if (readRet < 0)
			{
				eos = true;
				return false;
			}

			if (packet->stream_index == videoStreamIndex)
				avcodec_send_packet(codecContext, packet);

			av_packet_unref(packet);
			continue;
		}
		else
		{
			// AVERROR_EOF or a real decode error: treat as end of stream.
			eos = true;
			return false;
		}
	}
}

bool MP4VideoStream::seekDecoder(double target)
{
	int64_t ts = (int64_t) (target / timeBase);

	if (av_seek_frame(formatContext, videoStreamIndex, ts, AVSEEK_FLAG_BACKWARD) < 0)
		return false;

	avcodec_flush_buffers(codecContext);
	eos = false;
	lastFrame = nextFrame = -1;

	// av_seek_frame only guarantees landing on a keyframe at or before
	// the target, which is usually well before it (keyframe intervals of
	// 1-10 seconds are common). Decode forward from there so that by the
	// time this function returns, nextFrame is already at or just past
	// the actual requested position rather than at the keyframe's
	// timestamp -- this makes seeking land on the right picture
	// immediately instead of relying entirely on the natural playback
	// loop in threadedFillBackBuffer() to catch up frame by frame.
	const int maxCatchUpFrames = 512; // safety bound against malformed streams
	int framesDecoded = 0;
	bool decodedAny = false;
	while (nextFrame < target && framesDecoded < maxCatchUpFrames)
	{
		if (!decodeNextFrame())
			break;
		decodedAny = true;
		framesDecoded++;
	}

	return decodedAny;
}

void MP4VideoStream::copyFrameToBackBuffer()
{
	for (int y = 0; y < backBuffer->yh; ++y)
		memcpy(backBuffer->yplane + backBuffer->yw * y,
			scaledFrame->data[0] + scaledFrame->linesize[0] * y,
			backBuffer->yw);

	for (int y = 0; y < backBuffer->ch; ++y)
		memcpy(backBuffer->cbplane + backBuffer->cw * y,
			scaledFrame->data[1] + scaledFrame->linesize[1] * y,
			backBuffer->cw);

	for (int y = 0; y < backBuffer->ch; ++y)
		memcpy(backBuffer->crplane + backBuffer->cw * y,
			scaledFrame->data[2] + scaledFrame->linesize[2] * y,
			backBuffer->cw);
}

void MP4VideoStream::threadedFillBackBuffer(double dt)
{
	frameSync->update(dt);
	double position = frameSync->getPosition();

	bool hasFrame = false;

	if (position < lastFrame)
		hasFrame = seekDecoder(position);

	unsigned int framesBehind = 0;
	bool failedSeek = false;

	while (!eos && position >= nextFrame)
	{
		if (framesBehind++ > 5 && !failedSeek)
		{
			hasFrame = seekDecoder(position) || hasFrame;
			framesBehind = 0;
			failedSeek = true;
			continue;
		}

		if (!decodeNextFrame())
			break;

		hasFrame = true;
	}

	if (hasFrame)
	{
		{
			love::thread::Lock l(bufferMutex);
			frameReady = false;
		}

		copyFrameToBackBuffer();

		{
			love::thread::Lock l(bufferMutex);
			frameReady = true;
		}
	}
}

void MP4VideoStream::fillBackBuffer()
{
	// Done on the shared video worker thread, same as Theora.
}

bool MP4VideoStream::swapBuffers()
{
	if (eos)
		return false;

	if (!frameSync->isPlaying())
		return false;

	love::thread::Lock l(bufferMutex);
	if (!frameReady)
		return false;
	frameReady = false;

	Frame *temp = frontBuffer;
	frontBuffer = backBuffer;
	backBuffer = temp;

	return true;
}

} // mp4
} // video
} // love
