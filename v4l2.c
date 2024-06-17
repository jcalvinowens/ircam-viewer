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

#include "v4l2.h"

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#define MAXBUFS	64

struct v4l2_dev {
	struct v4l2_capability cap;
	int v4l2_fd;

	int nr_buffers;
	unsigned buffer_lens[MAXBUFS];
	void *mmaps[MAXBUFS];
};

static int v4l2_set_format(struct v4l2_dev *dev,
			   const struct v4l2_pix_format *pix)
{
	struct v4l2_format fmt = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.fmt.pix = *pix,
	};

	return ioctl(dev->v4l2_fd, VIDIOC_S_FMT, &fmt);
}

static int v4l2_set_rate(struct v4l2_dev *dev, const struct v4l2_fract *fp)
{
	struct v4l2_streamparm parm = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.parm.capture = {
			.timeperframe = *fp,
		},
	};

	return ioctl(dev->v4l2_fd, VIDIOC_S_PARM, &parm);
}

static void v4l2_init_stream(struct v4l2_dev *dev)
{
	struct v4l2_buffer bufs[MAXBUFS];
	struct v4l2_requestbuffers req = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.memory = V4L2_MEMORY_MMAP,
		.count = MAXBUFS,
	};
	int i;

	if (ioctl(dev->v4l2_fd, VIDIOC_QUERYCAP, &dev->cap))
		err(1, "VIDIOC_QUERYCAP");

	if (!(dev->cap.device_caps & V4L2_CAP_VIDEO_CAPTURE))
		err(1, "no capture support!");

	if (!(dev->cap.device_caps & V4L2_CAP_STREAMING))
		err(1, "no streaming support!");

	if (ioctl(dev->v4l2_fd, VIDIOC_REQBUFS, &req))
		err(1, "VIDIOC_REQBUFS");

	if (req.count > MAXBUFS)
		errx(1, "too many buffers! %d > %d", req.count, MAXBUFS);

	dev->nr_buffers = req.count;
	for (i = 0; i < dev->nr_buffers; i++) {
		bufs[i] = (struct v4l2_buffer){
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC,
			.memory = V4L2_MEMORY_MMAP,
			.index = i,
		};

		if (ioctl(dev->v4l2_fd, VIDIOC_QUERYBUF, &bufs[i]))
			err(1, "VIDIOC_QUERYBUF");

		dev->buffer_lens[i] = bufs[i].length;
		dev->mmaps[i] = mmap(NULL, bufs[i].length,
				     PROT_READ | PROT_WRITE,
				     MAP_SHARED, dev->v4l2_fd,
				     bufs[i].m.offset);

		if (dev->mmaps[i] == MAP_FAILED)
			err(1, "can't mmap buffer %d", i);
	}

	for (i = 0; i < dev->nr_buffers; i++)
		if (ioctl(dev->v4l2_fd, VIDIOC_QBUF, &bufs[i]))
			err(1, "initial VIDIOC_QBUF");

	i = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(dev->v4l2_fd, VIDIOC_STREAMON, &i))
		err(1, "VIDIOC_STREAMON");
}

/**
 * v4l2_open() - Open a video device and begin streaming.
 * @param path Path of device to open (ex. "/dev/video0").
 * @param fmt  The V4L2 pixel format to use (ex. V4L2_PIX_FMT_YUYV).
 * @param width Frame width in pixels.
 * @param height Frame height in pixels.
 * @param fps Framerate, hz.
 *
 * See /usr/include/linux/videodev2.h for a list of V4L2 pixel format
 * codes.
 *
 * Return: Running device handle if successful, NULL on failure.
 */
struct v4l2_dev *v4l2_open(const char *path, uint32_t fmt, int width,
			   int height, int fps)
{
	struct v4l2_dev *dev;
	struct v4l2_pix_format pix = {
		.pixelformat = fmt,
		.width = width,
		.height = height,
	};
	struct v4l2_fract fp = {
		.numerator = 1,
		.denominator = fps,
	};

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		errx(1, "no memory for v4l2_dev");

	dev->v4l2_fd = open(path, O_RDWR | O_NONBLOCK);
	if (dev->v4l2_fd == -1)
		err(1, "can't open V4L2 dev %s", path);

	if (v4l2_set_format(dev, &pix))
		err(1, "VIDIOC_S_FMT");

	if (v4l2_set_rate(dev, &fp))
		err(1, "VIDIOC_S_PARM");

	v4l2_init_stream(dev);
	return dev;
}

/**
 * v4l2_get_buffer() - Get a framebuffer from a running V4L2 device.
 * @param dev Running V4L2 device handle.
 * @param buf Output V4L2 buffer handle.
 *
 * Fetch the next available framebuffer. If no buffer is available,
 * block until the next buffer becomes available. The buffer must freed
 * by the user with v4l2_put_buffer().
 *
 * Return: 0 on success, 1 on error.
 */
int v4l2_get_buffer(struct v4l2_dev *dev, struct v4l2_buffer *buf)
{
	struct pollfd pfd = {0};

	do {
		*buf = (struct v4l2_buffer){
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_MMAP,
		};

		if (ioctl(dev->v4l2_fd, VIDIOC_DQBUF, buf) == 0)
			return 0;

		if (errno != EAGAIN)
			break;

		pfd.fd = dev->v4l2_fd;
		pfd.events = POLLIN;

	} while (poll(&pfd, 1, -1) > 0);

	return 1;
}

/**
 * v4l2_buf_mmap() - Get pointer to raw framebuffer data.
 * @param dev Running V4L2 device handle.
 * @param buf V4L2 buffer with desired frame.
 *
 * Return: Pointer to the backing V4L2_MEMORY_MMAP framebuffer.
 *
 * The address returned by this function is only valid until
 * v4l2_put_buffer() is called on the buffer handle.
 */
const uint8_t *v4l2_buf_mmap(const struct v4l2_dev *dev,
			     const struct v4l2_buffer *buf)
{
	return dev->mmaps[buf->index];
}

/**
 * v4l2_put_buffer() - Free a V4L2 framebuffer.
 * @param dev Running V4L2 device handle.
 * @param buf Buffer handle to free.
 *
 * Return the buffer's resources to the kernel, so they can be used to
 * return a future frame.
 *
 * Return: Nothing.
 */
void v4l2_put_buffer(struct v4l2_dev *dev, const struct v4l2_buffer *buf)
{
	if (ioctl(dev->v4l2_fd, VIDIOC_QBUF, buf))
		err(1, "VIDIOC_QBUF");
}

/**
 * v4l2_open() - Halt streaming and close a V4L2 device.
 * @param dev Running V4L2 device nahdle
 *
 * Return: Nothing.
 */
void v4l2_close(struct v4l2_dev *dev)
{
	int i;

	i = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(dev->v4l2_fd, VIDIOC_STREAMOFF, &i))
		err(1, "VIDIOC_STREAMOFF");

	for (i = 0; i < dev->nr_buffers; i++)
		munmap(dev->mmaps[i], dev->buffer_lens[i]);

	close(dev->v4l2_fd);
	free(dev);
}
