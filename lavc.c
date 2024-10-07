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

#include "lavc.h"

#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <err.h>
#include <unistd.h>
#include <fcntl.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavutil/imgutils.h>

struct lavc_ctx {
	AVPacket *pkt;
	const AVCodec *codec;
	const AVOutputFormat *fmt;
	AVStream *stream;
	AVFormatContext *fctx;
	AVCodecContext *ctx;
	AVFrame *frame;
	int pts_mult;
	bool queued;
};

/**
 * lavc_start_encode() - Initialize a handle for encoding a raw video
 *			 stream to a file.
 *
 * @param path Path to file to write encoded output to.
 * @param width Width of video frame.
 * @param height Height of video frame.
 * @param fps Framerate in frames per second.
 * @param pix_fmt FFMPEG pixel format code.
 *
 * Note that the FFMPEG pixel format codes are differnt than the V4L2
 * codes!
 *
 * Return: Handle for stream.
 */
struct lavc_ctx *lavc_start_encode(const char *path, int width, int height,
				   int fps, int pix_fmt)
{
	struct lavc_ctx *c;

	c = calloc(1, sizeof(*c));
	if (!c)
		errx(1, "can't allocate lavc context");

	avformat_alloc_output_context2(&c->fctx, NULL, NULL, path);
	if (!c->fctx)
		errx(1, "can't allocate format context");

	c->fmt = c->fctx->oformat;

	c->stream = avformat_new_stream(c->fctx, NULL);
	if (!c->stream)
		errx(1, "can't allocate output stream");

	c->pkt = av_packet_alloc();
	if (!c->pkt)
		errx(1, "can't allocate video packet");

	c->codec = avcodec_find_encoder(AV_CODEC_ID_FFV1);
	if (!c->codec)
		errx(1, "can't find FFV1 codec");

	c->ctx = avcodec_alloc_context3(c->codec);
	if (!c->ctx)
		errx(1, "can't allocate video context");

	c->ctx->codec_type = AVMEDIA_TYPE_VIDEO;
	c->ctx->codec_id = AV_CODEC_ID_FFV1;
	c->ctx->width = width;
	c->ctx->height = height;
	c->ctx->time_base = (AVRational){1, 1000};
	c->ctx->framerate = (AVRational){fps, 1};
	c->ctx->pix_fmt = pix_fmt;
	c->pts_mult = 1000 / fps;

	if (avcodec_open2(c->ctx, c->codec, NULL))
		errx(1, "can't open codec");

	c->frame = av_frame_alloc();
	if (!c->frame)
		errx(1, "can't allocate video frame");

	c->frame->format = c->ctx->pix_fmt;
	c->frame->width = width;
	c->frame->height = height;

	if (av_frame_get_buffer(c->frame, 0))
		errx(1, "can't allocate video frame data");

	if (avcodec_parameters_from_context(c->stream->codecpar, c->ctx))
		errx(1, "can't copy codec parameters");

	if (!(c->fmt->flags & AVFMT_NOFILE))
		if (avio_open(&c->fctx->pb, path, AVIO_FLAG_WRITE) < 0)
			err(1, "can't open record file");

	if (avformat_write_header(c->fctx, NULL) < 0)
		errx(1, "can't write header");

	return c;
}

/**
 * lavc_encode() - Push a framebuffer to the encoder.
 * @param c LAVC context handle.
 * @param pts PTS value for frame.
 * @param data Pointer to raw framebuffer.
 * @param len Length of framebuffer data.
 *
 * Return: 0 on success, non-zero on error.
 */
int lavc_encode(struct lavc_ctx *c, uint32_t pts, const uint8_t *data, int len)
{
	int r;

	if (data) {
		if (av_frame_make_writable(c->frame))
			errx(1, "can't make frame writable");

		c->frame->pts = pts * c->pts_mult;
		memcpy(c->frame->data[0], data, len);
	}

	r = avcodec_send_frame(c->ctx, data ? c->frame : NULL);
	if (r < 0)
		errx(1, "can't send frame for encoding");

	while (r >= 0) {
		r = avcodec_receive_packet(c->ctx, c->pkt);
		if (r < 0) {
			switch (r) {
			case AVERROR(EAGAIN):
			case AVERROR_EOF:
				return 0;

			default:
				errx(1, "bad packet from encoder: %d", r);
			}
		}

		c->pkt->pts = pts * c->pts_mult;
		c->pkt->dts = pts * c->pts_mult;
		c->pkt->duration = c->pts_mult;
		if (av_interleaved_write_frame(c->fctx, c->pkt) < 0)
			errx(1, "can't write encoded data");
	}

	return r;
}

/**
 * lavc_end_encode() - Shutdown an encoding stream.
 * @param c LAVC context handle.
 *
 * Return: Nothing.
 */
void lavc_end_encode(struct lavc_ctx *c)
{
	av_write_trailer(c->fctx);
	if (!(c->fmt->flags & AVFMT_NOFILE))
		avio_closep(&c->fctx->pb);

	avformat_free_context(c->fctx);
	av_packet_free(&c->pkt);
	avcodec_free_context(&c->ctx);
	av_frame_free(&c->frame);
	free(c);
}

/**
 * lavc_start_decode() - Initialize a handle for decoding a compressed
 *			 video stream from a file.
 *
 * @param path Path to file containing encoded video.
 *
 * Return: Handle for the stream.
 */
struct lavc_ctx *lavc_start_decode(const char *path)
{
	struct lavc_ctx *c;
	uint8_t *tmp_ptr[4];
	int tmp_sz[4];
	int ret, sidx;

	c = calloc(1, sizeof(*c));
	if (!c)
		errx(1, "can't allocate lavc context");

	if (avformat_open_input(&c->fctx, path, NULL, NULL) < 0)
		errx(1, "can't open input file '%s'", path);

	if (avformat_find_stream_info(c->fctx, NULL) < 0)
		errx(1, "no stream information in '%s'", path);

	sidx = av_find_best_stream(c->fctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (sidx < 0)
		errx(1, "no video stream in '%s'", path);

	c->stream = c->fctx->streams[sidx];
	c->codec = avcodec_find_decoder(c->stream->codecpar->codec_id);
	if (!c->codec)
		errx(1, "can't find decoder");

	c->ctx = avcodec_alloc_context3(c->codec);
	if (!c->ctx)
		errx(1, "can't allocate decoder context");

	if (avcodec_parameters_to_context(c->ctx, c->stream->codecpar) < 0)
		errx(1, "can't copy decoder parameters");

	if (avcodec_open2(c->ctx, c->codec, NULL) < 0)
		errx(1, "can't open decoder codec");

	ret = av_image_alloc(tmp_ptr, tmp_sz, c->ctx->width, c->ctx->height,
			     c->ctx->pix_fmt, 1);

	if (ret < 0)
		errx(1, "can't allocate image memory");

	c->frame = av_frame_alloc();
	if (!c->frame)
		errx(1, "can't allocate image frame");

	c->pkt = av_packet_alloc();
	if (!c->pkt)
		errx(1, "can't allocate image packet");

	return c;
}

/**
 * lavc_decode() - Decode the next frame in an encoded stream.
 * @param c LAVC context handle.
 *
 * Call this function repeatedly until it returns NULL on EOF.
 *
 * Return: Pointer to raw framebuffer containing decoded data.
 *
 * The pointer returned by this function is only valid until the next
 * call to this function, or the next call to lavc_decode_loop().
 */
const uint8_t *lavc_decode(struct lavc_ctx *c)
{
	int ret;

	if (!c->queued) {
again:
		if (av_read_frame(c->fctx, c->pkt) < 0)
			return NULL;

		if (c->pkt->stream_index != c->stream->index) {
			av_packet_unref(c->pkt);
			goto again;
		}

		if (avcodec_send_packet(c->ctx, c->pkt) < 0)
			errx(1, "can't submit packet");

		av_packet_unref(c->pkt);
		c->queued = 1;

	} else {
		av_frame_unref(c->frame);
	}

	ret = avcodec_receive_frame(c->ctx, c->frame);
	if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
		goto again;

	return c->frame->data[0];
}

/**
 * lavc_decode_loop() - Loop a decoding stream back to the beginning.
 * @param c LAVC context handle.
 *
 * After this function, lavc_decode() will begin returning frames from
 * the beginning of the file again, until EOF.
 *
 * Return: Nothing.
 */
void lavc_decode_loop(struct lavc_ctx *c)
{
	av_frame_unref(c->frame);
	avformat_seek_file(c->fctx, c->stream->index, 0, 0, 0, 0);
	c->queued = 0;
}

/**
 * lavc_end_decode() - Shutdown a decoding stream.
 * @param c LAVC context handle.
 *
 * Return: Nothing.
 */
void lavc_end_decode(struct lavc_ctx *c)
{
	avcodec_free_context(&c->ctx);
	avformat_close_input(&c->fctx);
	av_packet_free(&c->pkt);
	av_frame_free(&c->frame);
	avformat_free_context(c->fctx);
	free(c);
}
