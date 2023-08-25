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
#include <error.h>
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
		error(1, errno, "VIDIOC_QUERYCAP");

	if (!(dev->cap.device_caps & V4L2_CAP_VIDEO_CAPTURE))
		error(1, errno, "no capture support!");

	if (!(dev->cap.device_caps & V4L2_CAP_STREAMING))
		error(1, errno, "no streaming support!");

	if (ioctl(dev->v4l2_fd, VIDIOC_REQBUFS, &req))
		error(1, errno, "VIDIOC_REQBUFS");

	if (req.count > MAXBUFS)
		error(1, 0, "too many buffers! %d > %d", req.count, MAXBUFS);

	dev->nr_buffers = req.count;
	for (i = 0; i < dev->nr_buffers; i++) {
		bufs[i] = (struct v4l2_buffer){
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC,
			.memory = V4L2_MEMORY_MMAP,
			.index = i,
		};

		if (ioctl(dev->v4l2_fd, VIDIOC_QUERYBUF, &bufs[i]))
			error(1, errno, "VIDIOC_QUERYBUF");

		dev->buffer_lens[i] = bufs[i].length;
		dev->mmaps[i] = mmap(NULL, bufs[i].length,
				     PROT_READ | PROT_WRITE,
				     MAP_SHARED, dev->v4l2_fd,
				     bufs[i].m.offset);

		if (dev->mmaps[i] == MAP_FAILED)
			error(1, errno, "can't mmap buffer %d", i);
	}

	for (i = 0; i < dev->nr_buffers; i++)
		if (ioctl(dev->v4l2_fd, VIDIOC_QBUF, &bufs[i]))
			error(1, errno, "initial VIDIOC_QBUF");

	i = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(dev->v4l2_fd, VIDIOC_STREAMON, &i))
		error(1, errno, "VIDIOC_STREAMON");
}

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
		error(1, 0, "no memory for v4l2_dev");

	dev->v4l2_fd = open(path, O_RDWR | O_NONBLOCK);
	if (dev->v4l2_fd == -1)
		error(1, errno, "can't open V4L2 dev %s", path);

	if (v4l2_set_format(dev, &pix))
		error(1, errno, "VIDIOC_S_FMT");

	if (v4l2_set_rate(dev, &fp))
		error(1, errno, "VIDIOC_S_PARM");

	v4l2_init_stream(dev);
	return dev;
}

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

const uint8_t *v4l2_buf_mmap(struct v4l2_dev *dev, int index)
{
	return dev->mmaps[index];
}

void v4l2_put_buffer(struct v4l2_dev *dev, const struct v4l2_buffer *buf)
{
	if (ioctl(dev->v4l2_fd, VIDIOC_QBUF, buf))
		error(1, errno, "VIDIOC_QBUF");
}

void v4l2_close(struct v4l2_dev *dev)
{
	int i;

	i = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(dev->v4l2_fd, VIDIOC_STREAMOFF, &i))
		error(1, errno, "VIDIOC_STREAMOFF");

	for (i = 0; i < dev->nr_buffers; i++)
		munmap(dev->mmaps[i], dev->buffer_lens[i]);

	close(dev->v4l2_fd);
	free(dev);
}
