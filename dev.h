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

/*
 * Constants for TOPDON TC001
 *
 * The device is a simple uvcvideo camera. It claims to provide 256x384 YUYV
 * (yuyv422) video, but it actually gives you two different views of the same
 * 16-bit 256x192 image data concatenated together. If you want a quick look:
 *
 *   ffplay -f v4l2 -input_format yuyv422 -video_size 256x384 -i /dev/video0
 *
 * The first bitmap isn't actually YUV: it's really just an 8-bit grayscale
 * bitmap with a garbage byte inserted between each real byte. The garbage byte
 * is 0x80, so treating it as though it is YUYV or YVYU and converting it to
 * RGB will waste CPU but ultimately give you a grayscale image. It is
 * dynamically scaled, and contains a strict subset of the data in the second
 * bitmap. We can just ignore it.
 *
 * The second bitmap is all we actually need: a true unscaled Y16 bitmap of the
 * raw temperature values detected by the IR camera sensor.
 */

#define WIDTH		256
#define HEIGHT		192
#define FPS		25
#define ISIZE		(WIDTH * HEIGHT * 2) // gray16le
#define ISKIP		ISIZE // Skip 8-bit image (see above)
#define VSIZE		(WIDTH * HEIGHT * 4) // rgba
