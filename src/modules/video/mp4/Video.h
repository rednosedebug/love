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

#ifndef LOVE_VIDEO_MP4_VIDEO_H
#define LOVE_VIDEO_MP4_VIDEO_H

// STL
#include <vector>

// LOVE
#include "filesystem/File.h"
#include "video/Video.h"
#include "thread/threads.h"
#include "video/VideoStream.h"
#include "MP4VideoStream.h"

namespace love
{
namespace video
{
namespace mp4
{

class Worker;

class Video : public love::video::Video
{
public:
	Video();
	virtual ~Video();

	VideoStream *newVideoStream(love::filesystem::File *file);

	// Probe used by the unified love.video dispatcher (see ../wrap_Video.cpp)
	// to decide whether a given file should go through this backend.
	static bool canDecode(love::filesystem::File *file);

private:
	Worker *workerThread;
}; // Video

class Worker : public love::thread::Threadable
{
public:
	Worker();
	virtual ~Worker();

	// Implements Threadable
	void threadFunction();

	void addStream(MP4VideoStream *stream);
	// Frees itself!
	void stop();

private:

	std::vector<StrongRef<MP4VideoStream>> streams;

	love::thread::MutexRef mutex;
	love::thread::ConditionalRef cond;

	bool stopping;
}; // Worker

} // mp4
} // video
} // love

#endif // LOVE_VIDEO_MP4_VIDEO_H
