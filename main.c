/*
 * Linux viewer for Y16 uvcvideo infrared cameras
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

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>
#include <getopt.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <arpa/inet.h>

#include "dev.h"
#include "v4l2.h"
#include "lavc.h"
#include "sdl.h"
#include "inet.h"

static int record_only;
static struct lavc_ctx *record;
static int window_width = 1440;
static int window_height = 1080;
static const char *fontpath;
static int listen_only;
static FILE *remote_socket;
static int hide_init_help;
static int fullscreen;

static sig_atomic_t stop;

static void stopper(int sig)
{
	stop = sig;
}

static int new_periodic_tfd(int64_t interval_ms)
{
	const struct itimerspec t = {
		.it_interval = {
			.tv_sec = interval_ms / 1000,
			.tv_nsec = interval_ms % 1000 * 1000000,
		},
		.it_value = {
			.tv_sec = interval_ms / 1000,
			.tv_nsec = interval_ms % 1000 * 1000000,
		},
	};
	int fd;

	fd = timerfd_create(CLOCK_MONOTONIC, 0);
	if (fd == -1)
		return -1;

	if (timerfd_settime(fd, 0, &t, NULL)) {
		close(fd);
		return -1;
	}

	return fd;
}

static void run_v4l2(const char *devpath, bool render_local)
{
	const struct ircam_desc *desc;
	struct sdl_ctx *ctx = NULL;
	struct v4l2_dev *dev;
	char tmp[PATH_MAX];

	if (devpath) {
		desc = lookup_camera_desc(devpath);
		if (!desc) {
			/*
			 * FIXME: This will turn into a `--force-model` option
			 * when multiple camera models are actually supported.
			 */
			warnx("%s looks incompatible, trying anyway", devpath);
			desc = default_camera();
		}
	} else {
		int i;

		// FIXME: dumb
		for (i = 0; i < 64; i++) {
			snprintf(tmp, sizeof(tmp), "/dev/video%d", i);
			desc = lookup_camera_desc(tmp);
			if (desc) {
				devpath = tmp;
				goto found;
			}
		}

		errx(1, "No compatible IR camera found!");
	}

found:
	dev = v4l2_open(devpath, desc->v4l2_fmt, desc->width, desc->height * 2,
			desc->fps);

	if (remote_socket) {
		struct ircam_desc desc_copy = *desc;
		ircam_desc_hton(&desc_copy);

		if (fwrite(&desc_copy, 1, sizeof(desc_copy), remote_socket) !=
		    sizeof(desc_copy))
			err(1, "remote socket not accepting data");
	}

	if (record_only) {
		char path[PATH_MAX];

		snprintf(path, sizeof(path), "%lld-raw.mkv",
			 (long long)time(NULL));

		record = lavc_start_encode(path, desc->width, desc->height,
					   desc->fps, desc->ff_raw_fmt);
	}

	if (render_local) {
		ctx = sdl_open(window_width, window_height, desc, false,
			       fontpath, hide_init_help, fullscreen);
		if (!ctx)
			errx(1, "can't initialize libsdl");
	}

	v4l2_init_stream(dev);
	while (!stop) {
		struct v4l2_buffer buf = { 0 };
		const uint8_t *data;

		if (v4l2_get_buffer(dev, &buf)) {
			if (errno == EINTR)
				continue;

			err(1, "v4l2 failure");
		}

		if (buf.bytesused != desc->iskip + desc->isize)
			errx(1,
			     "bad image size (%d != %d), is '%s' the "
			     "correct device? Pass '-d' to specify a "
			     "different one",
			     buf.bytesused, desc->isize * 2, devpath);

		data = v4l2_buf_mmap(dev, &buf) + desc->iskip;

		if (record)
			if (lavc_encode(record, buf.sequence, data,
					desc->isize))
				err(1, "can't record");

		if (remote_socket) {
			if (fwrite(data, 1, desc->isize, remote_socket) !=
			    desc->isize) {
				warn("remote socket not accepting data");
				goto out;
			}
		}

		if (ctx) {
			char path[PATH_MAX];

			switch (paint_frame(ctx, buf.sequence, data)) {
			case TOGGLE_Y16_RECORD:
				if (record) {
					lavc_end_encode(record);
					record = NULL;
					break;
				}

				snprintf(path, sizeof(path), "%lld-raw.mkv",
					 (long long)time(NULL));

				record = lavc_start_encode(path, desc->width,
							   desc->height,
							   desc->fps,
							   desc->ff_raw_fmt);
				break;

			case QUIT_PROGRAM:
				goto out;
			}
		}

		v4l2_put_buffer(dev, &buf);
	}
out:
	if (record) {
		lavc_end_encode(record);
		record = NULL;
	}

	sdl_close(ctx);
	v4l2_close(dev);
}

static void run_playback(const char *filepath)
{
	const uint8_t *data = NULL;
	const struct ircam_desc *desc;
	struct lavc_ctx *in_ctx;
	struct sdl_ctx *ctx;
	uint32_t seq = 0;
	bool paused = 0;
	int timer_fd;

	/*
	 * FIXME: Record the descriptor in the MKV metadata. For now, since we
	 * only actually support one camera, we know what it has to be...
	 */
	desc = default_camera();

	in_ctx = lavc_start_decode(filepath);
	timer_fd = new_periodic_tfd(1000 / desc->fps);
	ctx = sdl_open(window_width, window_height, desc, true, fontpath,
		       hide_init_help, fullscreen);
	if (!ctx)
		errx(1, "can't initialize libsdl");

	while (!stop) {
		uint64_t ticks;

		if (!data || !paused) {
			data = lavc_decode(in_ctx);
			if (!data) {
				lavc_decode_loop(in_ctx);
				sdl_loop(ctx);
				seq = 0;
				continue;
			}
		}

		if (read(timer_fd, &ticks, sizeof(ticks)) != sizeof(ticks))
			err(1, "bad timerfd read");

		if (!paused)
			seq += ticks;

		switch (paint_frame(ctx, seq, data)) {
		case TOGGLE_PAUSE:
			paused = !paused;
			break;

		case QUIT_PROGRAM:
			goto out;
		}
	}

out:
	sdl_close(ctx);
	close(timer_fd);
	lavc_end_decode(in_ctx);
}

static void run_remote(const struct sockaddr_in6 *src)
{
	struct ircam_desc desc;
	struct sdl_ctx *ctx;
	char path[PATH_MAX];
	uint32_t seq = 0;
	int fd;

	fd = get_stream_connect(src);
	if (fd == -1)
		errx(1, "Can't connect");

	if (read(fd, &desc, sizeof(desc)) != sizeof(desc))
		errx(1, "Can't get camera descriptor");

	ircam_desc_ntoh(&desc);
	ctx = sdl_open(window_width, window_height, &desc, false, fontpath,
		       hide_init_help, fullscreen);
	if (!ctx)
		errx(1, "can't initialize libsdl");

	while (!stop) {
		uint8_t data[desc.isize];
		size_t off = 0;
		ssize_t ret;

		do {
			ret = read(fd, data + off, desc.isize - off);
			if (ret <= 0)
				break;

		} while ((off += ret) < desc.isize);

		if (off != desc.isize)
			goto out;

		if (record)
			if (lavc_encode(record, seq, data, desc.isize))
				err(1, "can't record");

		switch (paint_frame(ctx, ++seq, data)) {
		case QUIT_PROGRAM:
			goto out;

		case TOGGLE_Y16_RECORD:
			if (record) {
				lavc_end_encode(record);
				record = NULL;
				break;
			}

			snprintf(path, sizeof(path), "%lld-raw.mkv",
				 (long long)time(NULL));

			record = lavc_start_encode(path, desc.width,
						   desc.height, desc.fps,
						   desc.ff_raw_fmt);
			break;
		}
	}

out:
	if (record) {
		lavc_end_encode(record);
		record = NULL;
	}

	sdl_close(ctx);
	close(fd);
}

__attribute__((noreturn)) static void show_help_and_die(void)
{
	puts("usage: ./ircam [ -c remote | -p recfile | -d dev [-n] [-l] ] "
	     "[-f fontpath] [-w window_pixel_width] [-q]");

	exit(1);
}

int main(int argc, char **argv)
{
	static const struct option opts[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "device", required_argument, NULL, 'd' },
		{ "playback", required_argument, NULL, 'p' },
		{ "width", required_argument, NULL, 'w' },
		{ "record-only", no_argument, NULL, 'n' },
		{ "font", required_argument, NULL, 'f' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "fullscreen", no_argument, NULL, 'F' },
		{ NULL, 0, NULL, 0 },
	};
	char v4[sizeof("::ffff:XXX.XXX.XXX.XXX")];
	struct sockaddr_in6 video_srcaddr = { 0 };
	struct sigaction ignore_action = {
		.sa_handler = SIG_IGN,
	};
	struct sigaction stop_action = {
		.sa_handler = stopper,
	};
	char *v4l2dev = NULL;
	char *filepath = NULL;

	sigaction(SIGINT, &stop_action, NULL);
	sigaction(SIGTERM, &stop_action, NULL);
	sigaction(SIGPIPE, &ignore_action, NULL);
	sigaction(SIGHUP, &ignore_action, NULL);

	while (1) {
		int i = getopt_long(argc, argv, "hd:p:nw:f:lc:qF", opts, NULL);

		switch (i) {
		case 'd':
			v4l2dev = strdup(optarg);
			break;
		case 'n':
			record_only = 1;
			break;
		case 'p':
			filepath = strdup(optarg);
			break;
		case 'w':
			window_width = atoi(optarg);
			window_height = window_width / 4 * 3;
			break;
		case 'f':
			if (access(optarg, R_OK))
				err(1, "bad font '%s'", optarg);

			fontpath = strdup(optarg);
			break;
		case 'l':
			listen_only = 1;
			break;
		case 'c':
			video_srcaddr.sin6_family = AF_INET6;
			if (inet_pton(AF_INET6, optarg,
				      &video_srcaddr.sin6_addr) == 1)
				break;

			snprintf(v4, sizeof(v4), "::ffff:%s", optarg);
			if (inet_pton(AF_INET6, v4, &video_srcaddr.sin6_addr) ==
			    1)
				break;

			errx(1, "Can't parse address '%s'", optarg);
		case 'q':
			hide_init_help = 1;
			break;
		case 'F':
			fullscreen = 1;
			break;
		case 'h':
		default:
			show_help_and_die();
		case -1:
			goto done;
		}
	}
done:

	if (v4l2dev && filepath)
		show_help_and_die();

	if (v4l2dev && video_srcaddr.sin6_family)
		show_help_and_die();

	if (filepath && video_srcaddr.sin6_family)
		show_help_and_die();

	if (record_only || listen_only) {
		if (listen_only) {
			remote_socket =
				fdopen(get_stream_listen_one(8888), "w");
			if (!remote_socket)
				err(1, "Error on remote socket");
		}

		run_v4l2(v4l2dev, false);
		goto out;
	}

	if (filepath) {
		run_playback(filepath);
	} else if (video_srcaddr.sin6_family) {
		video_srcaddr.sin6_port = htons(8888);
		run_remote(&video_srcaddr);
	} else {
		run_v4l2(v4l2dev, true);
	}
out:
	free((void *)fontpath);
	free(v4l2dev);
	free(filepath);
	return 0;
}
