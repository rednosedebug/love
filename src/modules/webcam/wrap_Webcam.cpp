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

#include "wrap_Webcam.h"
#include "null/Webcam.h"

namespace love
{
namespace webcam
{

Webcam *luax_checkwebcam(lua_State *L, int idx)
{
	return luax_checktype<Webcam>(L, idx);
}

/**
 * love.webcam.new()
 *
 * SCAFFOLDING ONLY: constructs a Webcam handle backed by the null
 * (no-op) backend. No actual camera access happens -- see Webcam.h and
 * null/Webcam.h for the rationale. Each call creates an independent
 * instance (this is not a singleton module function).
 **/
int w_new(lua_State *L)
{
	Webcam *webcam = nullptr;
	luax_catchexcept(L, [&]() { webcam = new null::Webcam(); });
	luax_pushtype(L, webcam);
	webcam->release();
	return 1;
}

int w_Webcam_update(lua_State *L)
{
	Webcam *webcam = luax_checkwebcam(L, 1);
	double dt = luaL_checknumber(L, 2);
	webcam->update(dt);
	return 0;
}

int w_Webcam_getFrame(lua_State *L)
{
	Webcam *webcam = luax_checkwebcam(L, 1);
	auto *frame = webcam->getFrame();
	if (frame == nullptr)
	{
		// Scaffolding only: the null backend never produces a frame, so
		// this always returns nil for now rather than a Texture.
		lua_pushnil(L);
		return 1;
	}
	luax_pushtype(L, frame);
	return 1;
}

int w_Webcam_isOpen(lua_State *L)
{
	Webcam *webcam = luax_checkwebcam(L, 1);
	luax_pushboolean(L, webcam->isOpen());
	return 1;
}

int w_Webcam_close(lua_State *L)
{
	Webcam *webcam = luax_checkwebcam(L, 1);
	webcam->close();
	return 0;
}

static const luaL_Reg webcam_functions[] =
{
	{ "update", w_Webcam_update },
	{ "getFrame", w_Webcam_getFrame },
	{ "isOpen", w_Webcam_isOpen },
	{ "close", w_Webcam_close },
	{ 0, 0 }
};

int luaopen_webcam(lua_State *L)
{
	return luax_register_type(L, &Webcam::type, webcam_functions, nullptr);
}

static const lua_CFunction types[] =
{
	luaopen_webcam,
	0
};

static const luaL_Reg functions[] =
{
	{ "new", w_new },
	{ 0, 0 }
};

extern "C" int luaopen_love_webcam(lua_State *L)
{
	WrappedModule w;
	w.module = nullptr;
	w.name = "webcam";
	w.type = &Module::type;
	w.functions = functions;
	w.types = types;

	return luax_register_module(L, w);
}

} // webcam
} // love
