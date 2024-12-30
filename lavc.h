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

#include <stdint.h>
#include <libavutil/pixfmt.h>

struct lavc_ctx;

struct lavc_ctx *lavc_start_encode(const char *path, int width, int height,
				   int fps, int pix_fmt);

int lavc_encode(struct lavc_ctx *c, uint32_t pts, const uint8_t *data, int len);

void lavc_end_encode(struct lavc_ctx *c);

struct lavc_ctx *lavc_start_decode(const char *path);

const uint8_t *lavc_decode(struct lavc_ctx *c);

void lavc_decode_loop(struct lavc_ctx *c);

void lavc_end_decode(struct lavc_ctx *c);
