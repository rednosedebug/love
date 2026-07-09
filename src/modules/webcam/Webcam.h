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

#ifndef LOVE_WEBCAM_H
#define LOVE_WEBCAM_H

// LOVE
#include "common/Object.h"
#include "common/StrongRef.h"
#include "graphics/Texture.h"

namespace love
{
namespace webcam
{

/**
 * SCAFFOLDING ONLY -- this module intentionally has NO real camera
 * capture logic yet. It exists to establish the API shape
 * (love.webcam.new(), Webcam:update(dt), Webcam:getFrame(), and
 * reusing love.graphics.draw() on the resulting frame) so that a real
 * platform-specific backend (Android Camera2 via JNI, V4L2 on Linux,
 * AVFoundation on macOS/iOS, etc.) can be implemented later without
 * having to redesign the Lua-facing API.
 *
 * This is an Object (like VideoStream), not a Module: love.webcam.new()
 * is meant to be callable more than once (e.g. front camera + back
 * camera handles simultaneously), so each Webcam is its own independent
 * instance rather than a singleton like love.video/love.audio.
 *
 * Every method here is a no-op or returns a harmless placeholder value.
 * getFrame() returns nullptr until a real backend fills it in.
 **/
class Webcam : public Object
{
public:

	static love::Type type;

	virtual ~Webcam() {}

	/**
	 * Advances any pending frame capture. In a real backend, this is
	 * where a new frame from the camera hardware would be pulled and
	 * decoded/converted into a drawable Texture. Currently a no-op.
	 **/
	virtual void update(double dt) = 0;

	/**
	 * Returns the most recent captured frame as a drawable Texture, or
	 * nullptr if no frame is available yet (which, in this scaffolding
	 * version, is always the case since no backend produces frames).
	 * The returned object is intended to be usable directly with
	 * love.graphics.draw(frame, x, y) like any other Drawable/Texture,
	 * rather than introducing a dedicated love.graphics.drawWebcam()
	 * function.
	 **/
	virtual love::graphics::Texture *getFrame() = 0;

	/**
	 * Whether this webcam handle is actively capturing. Always false in
	 * this scaffolding version.
	 **/
	virtual bool isOpen() const = 0;

	/**
	 * Stops capture and releases any underlying camera handle. No-op
	 * until a real backend exists.
	 **/
	virtual void close() = 0;

protected:

	Webcam() {}

}; // Webcam

} // webcam
} // love

#endif // LOVE_WEBCAM_H
