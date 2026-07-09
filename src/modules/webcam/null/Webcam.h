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

#ifndef LOVE_WEBCAM_NULL_WEBCAM_H
#define LOVE_WEBCAM_NULL_WEBCAM_H

// LOVE
#include "webcam/Webcam.h"

namespace love
{
namespace webcam
{
namespace null
{

/**
 * SCAFFOLDING BACKEND -- this is the only backend for love.webcam right
 * now. It implements the full Webcam interface but never actually
 * captures anything: update() is a no-op, getFrame() always returns
 * nullptr, and isOpen() always returns false. It exists purely so
 * love.webcam.new() has something concrete to construct while the real
 * platform backends (Android Camera2, V4L2, AVFoundation, etc.) don't
 * exist yet -- see Webcam.h for the rationale.
 **/
class Webcam : public love::webcam::Webcam
{
public:

	Webcam();
	virtual ~Webcam();

	void update(double dt) override;
	love::graphics::Texture *getFrame() override;
	bool isOpen() const override;
	void close() override;

}; // Webcam

} // null
} // webcam
} // love

#endif // LOVE_WEBCAM_NULL_WEBCAM_H
