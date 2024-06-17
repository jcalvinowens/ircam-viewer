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
#include <linux/videodev2.h>

struct v4l2_dev;

struct v4l2_dev *v4l2_open(const char *path, uint32_t fmt, int w, int h, int f);

int v4l2_get_buffer(struct v4l2_dev *dev, struct v4l2_buffer *buf);

const uint8_t *v4l2_buf_mmap(const struct v4l2_dev *dev,
			     const struct v4l2_buffer *buf);

void v4l2_put_buffer(struct v4l2_dev *dev, const struct v4l2_buffer *buf);

void v4l2_close(struct v4l2_dev *dev);
