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

#include <endian.h>
#include <stdint.h>

struct ircam_desc {
	int32_t width;
	int32_t height;
	uint32_t fps;
	uint32_t isize;
	uint32_t iskip;
	uint32_t vsize;
	int32_t v4l2_width;
	int32_t v4l2_height;
	uint32_t v4l2_fmt;
	uint32_t ff_raw_fmt;
	const char name[64];
};

#define NTOH(n) desc->n = le32toh(desc->n)
static inline void ircam_desc_ntoh(struct ircam_desc *desc)
{
	NTOH(width);
	NTOH(height);
	NTOH(fps);
	NTOH(isize);
	NTOH(iskip);
	NTOH(vsize);
	NTOH(v4l2_width);
	NTOH(v4l2_height);
	NTOH(ff_raw_fmt);
}
#undef NTOH

#define HTON(n) desc->n = htole32(desc->n)
static inline void ircam_desc_hton(struct ircam_desc *desc)
{
	HTON(width);
	HTON(height);
	HTON(fps);
	HTON(isize);
	HTON(iskip);
	HTON(vsize);
	HTON(v4l2_width);
	HTON(v4l2_height);
	HTON(ff_raw_fmt);
}
#undef HTON

const struct ircam_desc *default_camera(void);
const struct ircam_desc *lookup_camera_desc(const char *path);
