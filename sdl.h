/*
 * Copyright (C) 2023 Calvin Owens <jcalvinowens@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "dev.h"

#include <stdint.h>
#include <stdbool.h>

struct sdl_ctx;

enum paint_frame_action {
	NOTHING,
	TOGGLE_Y16_RECORD,
	TOGGLE_PAUSE,
	QUIT_PROGRAM,
};

#ifndef IRCAM_NOSDL

struct sdl_ctx *sdl_open(int upscaled_width, int upscaled_height,
			 const struct ircam_desc *desc, bool pb,
			 const char *fontpath, bool hidehelp, bool fullscreen);

int paint_frame(struct sdl_ctx *c, uint32_t seq, const uint8_t *data);

void sdl_loop(struct sdl_ctx *c);

void sdl_close(struct sdl_ctx *c);

#else

struct sdl_ctx {
	bool unused;
};

static inline struct sdl_ctx *sdl_open(int w, int h, const struct ircam_desc *d,
				       bool p, const char *f, bool i, bool l)
{
	return NULL;
}

static inline int paint_frame(struct sdl_ctx *c, uint32_t s, const uint8_t *d)
{
	return NOTHING;
}

static inline void sdl_loop(struct sdl_ctx *c)
{
}

static inline void sdl_close(struct sdl_ctx *c)
{
}

#endif /* IRCAM_NOSDL */
