/*
 * Copyright (C) 2025 Calvin Owens <calvin@wbinvd.org>
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

#include "dev.h"

#include <stddef.h>
#include <linux/videodev2.h>
#include <libavutil/pixfmt.h>

#include "v4l2.h"

static const struct ircam_desc supported_descs[] = {
	/*
	 * TOPDON TC001
	 * A-BF RX-450
	 * InfiRay P2 Pro
	 *
	 * The device is a simple uvcvideo camera. It claims to provide 256x384
	 * YUYV (yuyv422) video, but it actually gives you two different views
	 * of the same 16-bit 256x192 image data concatenated together.
	 *
	 * The first bitmap isn't actually YUV: it's really just an 8-bit
	 * grayscale bitmap with a garbage byte inserted between each real byte.
	 * The garbage byte is 0x80, so treating it as though it is YUYV or YVYU
	 * and converting it to RGB will waste CPU but ultimately give you a
	 * grayscale image. It is dynamically scaled, and contains a strict
	 * subset of the data in the second bitmap. We can just ignore it.
	 *
	 * The second bitmap is all we actually need: a true unscaled Y16 bitmap
	 * of the raw temperature values detected by the IR camera sensor.
	 */
	{
		.width = 256,
		.height = 192,
		.fps = 25,
		.isize = 256 * 192 * 2, // gray16le
		.iskip = 256 * 192 * 2, // Skip 8-bit image (see above)
		.vsize = 256 * 192 * 4, // RGBA
		.v4l2_width = 256,
		.v4l2_height = 384,
		.v4l2_fmt = V4L2_PIX_FMT_YUYV,
		.ff_raw_fmt = AV_PIX_FMT_GRAY16LE,
		.name = "TOPDON TC001 or compatible",
	},
};

static unsigned int nr_camera_descs(void)
{
	return sizeof(supported_descs[0]) / sizeof(supported_descs);
}

/**
 * default_camera() - XXX - Don't add more callers of this function.
 * Return: Pointer to an arbitrary camera descriptor (right now, the only one).
 */
const struct ircam_desc *default_camera(void)
{
	return &supported_descs[0];
}

/**
 * lookup_camera_desc() - Return a descriptor matching the specified V4L2 dev.
 * @param path Path to the V4L2 device to attempt to open.
 *
 * Return the driver descriptor for the V4L2 device.
 *
 * Return: Pointer to matching camera descriptor, or NULL if no match is found.
 */
const struct ircam_desc *lookup_camera_desc(const char *path)
{
	unsigned int i;

	for (i = 0; i < nr_camera_descs(); i++) {
		if (v4l2_matches_desc(path, &supported_descs[i]))
			return &supported_descs[i];
	}

	return NULL;
}
