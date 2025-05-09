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

#include "sdl.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/timerfd.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_ttf.h>

#include "dev.h"
#include "lavc.h"

/*
 * Use SDL_Fontcache for font caching (see README).
 */
#include "fontcache.h"

extern const uint8_t builtin_ttf_start;
extern const uint8_t builtin_ttf_end;

static const SDL_Color SDL_COLOR_RED = { 0xFF, 0, 0, 0xFF };
static const SDL_Color SDL_COLOR_BLUE = { 0, 0, 0xFF, 0xFF };

/*
 * Lookup table for the Turbo colormap (see README).
 */
#include "turbo.h"

/*
 * 8-bit gamma correction lookup tables are precomputed by mkgamma.py.
 */
#include "gamma.h"

/*
 * Decimal lookup tables for temp_fixp to hundredths:
 *
 *	python3 -c 'print([int(round(i / 64 * 100, 0)) for i in range(64)])'
 *	python3 -c 'print([int(round(i / 100 * 64, 0)) for i in range(100)])'
 */

static const uint8_t b10lookup[64] = {
	0,  2,	3,  5,	6,  8,	9,  11, 12, 14, 16, 17, 19, 20, 22, 23,
	25, 27, 28, 30, 31, 33, 34, 36, 38, 39, 41, 42, 44, 45, 47, 48,
	50, 52, 53, 55, 56, 58, 59, 61, 62, 64, 66, 67, 69, 70, 72, 73,
	75, 77, 78, 80, 81, 83, 84, 86, 88, 89, 91, 92, 94, 95, 97, 98,
};

static const uint8_t b10rev[100] = {
	0,  1,	1,  2,	3,  3,	4,  4,	5,  6,	6,  7,	8,  8,	9,  10, 10,
	11, 12, 12, 13, 13, 14, 15, 15, 16, 17, 17, 18, 19, 19, 20, 20, 21,
	22, 22, 23, 24, 24, 25, 26, 26, 27, 28, 28, 29, 29, 30, 31, 31, 32,
	33, 33, 34, 35, 35, 36, 36, 37, 38, 38, 39, 40, 40, 41, 42, 42, 43,
	44, 44, 45, 45, 46, 47, 47, 48, 49, 49, 50, 51, 51, 52, 52, 53, 54,
	54, 55, 56, 56, 57, 58, 58, 59, 60, 60, 61, 61, 62, 63, 63,
};

struct temp_fixp {
	unsigned major : 12;
	unsigned minor : 6;
	unsigned sign : 1;
};

static const struct temp_fixp abszero = {
	.major = 273,
	.minor = 10, // b10rev[15],
};

static struct temp_fixp raw_to_kelvin(uint16_t raw)
{
	return (struct temp_fixp){
		.major = raw >> 6,
		.minor = raw & 0x003F,
		.sign = 0,
	};
}

static struct temp_fixp kelvin_to_celsius(struct temp_fixp t)
{
	struct temp_fixp ret;

	if (t.major <= abszero.major) {
		ret.major = abszero.major - t.major;
		ret.minor = abszero.minor - t.minor;
		if (ret.minor > abszero.minor)
			ret.major++;

		ret.sign = 1;
		return ret;
	}

	ret.major = t.major - abszero.major;
	ret.minor = t.minor - abszero.minor;
	if (ret.minor > t.minor)
		ret.major--;

	ret.sign = 0;
	return ret;
}

static struct temp_fixp raw_to_celsius(uint16_t raw)
{
	return kelvin_to_celsius(raw_to_kelvin(raw));
}

static struct temp_fixp celsius_to_fahrenheit(struct temp_fixp t)
{
	uint32_t tmp = (t.major * 100 + b10lookup[t.minor]) * 9 / 5;
	uint8_t sign = t.sign;

	if (sign) {
		if (tmp <= 3200) {
			tmp = 3200 - tmp;
			sign = 0;
		} else {
			tmp -= 3200;
		}
	} else {
		tmp += 3200;
	}

	return (struct temp_fixp){
		.major = tmp / 100,
		.minor = b10rev[tmp % 100],
		.sign = sign,
	};
}

struct sdl_ctx {
	const struct ircam_desc *desc;
	SDL_Renderer *r;
	SDL_Texture *t;
	SDL_Window *w;
	FC_Font *f;
	const char *fontpath;
	FILE *fonttmp;
	bool colormap;
	bool showtext;
	bool show_min_max_marker;
	bool fahren;
	int gammafactor;
	int contours;
	bool invert;
	bool rotate;
	bool showhelp;
	bool showlicense;
	bool showinithelp;
	time_t inittsmono;
	uint16_t scale_max;
	uint16_t scale_min;
	SDL_Point crosshair;
	SDL_Color crosshair_color;
	struct lavc_ctx *vrecord;
	uint32_t frame_paint_seq;
	uint8_t textval;
	bool recording;
	bool looped;
	bool paused;
	bool pb;
};

static uint8_t getcolor(struct sdl_ctx *c, int color, uint8_t r)
{
	if (c->contours > 1) {
		uint16_t fr = r * c->contours;
		r = fr & 0xFF;
	}

	if (c->gammafactor)
		r = gammalookup[c->gammafactor][r];

	if (c->invert)
		r = ~r;

	if (c->colormap)
		return turbo_srgb_bytes[r][color];

	return r;
}

static SDL_Point calc_point_from_buf_offset(const struct sdl_ctx *c,
					    const int offset)
{
	if (c->rotate) {
		return (SDL_Point){
			c->desc->width - (offset / 2 % c->desc->width),
			c->desc->height - (offset / 2 / c->desc->width)
		};
	}

	return (SDL_Point){ offset / 2 % c->desc->width,
			    offset / 2 / c->desc->width };
}

static void update_crosshair_color(struct sdl_ctx *c)
{
	c->crosshair_color.r = c->textval;
	c->crosshair_color.g = c->textval;
	c->crosshair_color.b = c->textval;
	c->crosshair_color.a = 255;
}

static void drawtext(struct sdl_ctx *c, int x, int y, const char *fmt, ...)
{
	char txt[1024];
	va_list args;

	va_start(args, fmt);
	vsnprintf(txt, sizeof(txt), fmt, args);
	va_end(args);

	FC_DrawScale(c->f, c->r, x, y, FC_MakeScale(0.2F, 0.2F), txt);
}

static time_t now_mono(void)
{
	struct timespec t;

	if (clock_gettime(CLOCK_MONOTONIC_COARSE, &t))
		err(1, "Bad clock_gettime");

	return t.tv_sec;
}

static void showtexts(struct sdl_ctx *c, struct temp_fixp max,
		      struct temp_fixp ptemp, struct temp_fixp min,
		      uint32_t seq)
{
	char s = 'C';

	if (c->fahren) {
		s = 'F';
		max = celsius_to_fahrenheit(max);
		ptemp = celsius_to_fahrenheit(ptemp);
		min = celsius_to_fahrenheit(min);
	}

	drawtext(c, 0, 0, "%c%u.%02u%c %c%u.%02u%c %c%u.%02u%c",
		 min.sign ? '-' : ' ', min.major, b10lookup[min.minor], s,
		 ptemp.sign ? '-' : ' ', ptemp.major, b10lookup[ptemp.minor], s,
		 max.sign ? '-' : ' ', max.major, b10lookup[max.minor], s);

	if (c->scale_max || c->scale_min) {
		struct temp_fixp fmax = raw_to_celsius(c->scale_max);
		struct temp_fixp fmin = raw_to_celsius(c->scale_min);

		if (c->fahren) {
			fmax = celsius_to_fahrenheit(fmax);
			fmin = celsius_to_fahrenheit(fmin);
		}

		drawtext(c, 0, 7, "[%c%u.%02u%c   %c%u.%02u%c]",
			 fmin.sign ? '-' : ' ', fmin.major,
			 b10lookup[fmin.minor], s, fmax.sign ? '-' : ' ',
			 fmax.major, b10lookup[fmax.minor], s);
	} else {
		drawtext(c, 0, 7, "[   AUTO   AUTO   ]");
	}

	drawtext(c, 0, 14, "[ GAM %s  CON %d ]", gammavals[c->gammafactor],
		 c->contours);

	if (c->recording && !c->pb)
		drawtext(c, 0, 21, "[REC]");

	if (c->vrecord)
		drawtext(c, 20, 21, "[VREC]");

	if (c->paused && c->pb)
		drawtext(c, 46, 21, "[PAUSE]");

	drawtext(c, c->desc->width - 40, 1, "[%05" PRIu32 ".%02" PRIu32 "]",
		 seq / 25, (seq % 25) * 4);

	drawtext(c, c->desc->width - 45, 8, "% 5" PRId64 " DROPS",
		 (int64_t)seq - c->frame_paint_seq);

	if (c->showinithelp) {
		drawtext(c, 90, 50, "HOLD [H] FOR HELP");
		drawtext(c, 90, 64, "THIS PROGRAM COMES WITH");
		drawtext(c, 90, 71, "ABSOLUTELY NO WARRANTY");
		drawtext(c, 90, 78, "HOLD [L] FOR LICENSE");
	}
}

static void showlicensetext(struct sdl_ctx *c)
{
	drawtext(c, 40, 31, "Linux Infrared Camera Viewer");
	drawtext(c, 40, 38, "Copyright (C) 2024 Calvin Owens");
	drawtext(c, 40, 52, "This program is free software: you can");
	drawtext(c, 40, 59, "redistribute it and/or modify it under the");
	drawtext(c, 40, 66, "terms of the GNU General Public License as");
	drawtext(c, 40, 73, "published by the Free Software Foundation,");
	drawtext(c, 40, 80, "either version 3 of the License, or (at");
	drawtext(c, 40, 87, "your option) any later version.");
	drawtext(c, 40, 101, "This program is distributed in the hope that");
	drawtext(c, 40, 108, "it will be useful, but WITHOUT ANY WARRANTY;");
	drawtext(c, 40, 115, "without even the implied warranty of");
	drawtext(c, 40, 122, "MERCHANTABILITY or FITNESS FOR A PARTICULAR");
	drawtext(c, 40, 129, "PURPOSE. See the GNU General Public License");
	drawtext(c, 40, 136, "for more details.");
	drawtext(c, 40, 150, "You should have received a copy of the GNU");
	drawtext(c, 40, 157, "General Public License along with this");
	drawtext(c, 40, 164, "program. If not see <www.gnu.org/licenses>.");
}

static void showhelptext(struct sdl_ctx *c)
{
	drawtext(c, 40, 30, "D: MANUAL SCALE");
	drawtext(c, 40, 37, "E: AUTO SCALE");
	drawtext(c, 40, 44, "Q/W: MAN SCALE MIN/MAX ++");
	drawtext(c, 40, 51, "A/S: MAN SCALE MIN/MAX --");
	drawtext(c, 40, 58, "Z: MIN TO MINIMUM");
	drawtext(c, 40, 65, "X: MAX TO MAXIMUM");
	drawtext(c, 40, 72, "R: TOGGLE Y16 RECORD");
	drawtext(c, 40, 79, "V: TOGGLE RGBA RECORD");
	drawtext(c, 40, 86, "T: TOGGLE TXT COLOR/ON/OFF");
	drawtext(c, 40, 93, "M: TOGGLE SHOW MIN/MAX MARKER");
	drawtext(c, 40, 100, "G: TOGGLE GAMMA CORR");
	drawtext(c, 40, 107, "Y: TOGGLE CONTOURING");
	drawtext(c, 40, 114, "F: TOGGLE UNITS F/C");
	drawtext(c, 40, 121, "I: TOGGLE INVERT");
	drawtext(c, 40, 128, "U: TOGGLE OUTPUT ROTATION");
	drawtext(c, 40, 135, "C: TOGGLE GRAYSCALE");
	drawtext(c, 40, 142, "ARROW KEYS MOVE CROSS");
	drawtext(c, 40, 149, "SPACEBAR PAUSES PLAYBACK");
	drawtext(c, 40, 156, "L: SHOW LICENSE DETAILS");
	drawtext(c, 40, 163, "H: SHOW THIS HELP TEXT");
}

static void sdl_open_fontcache(struct sdl_ctx *c)
{
	if (c->f)
		FC_FreeFont(c->f);

	c->f = FC_CreateFont();
	if (!c->f)
		errx(1, "Can't instantiate new font");

	FC_LoadFont(c->f, c->r, c->fontpath, 32,
		    FC_MakeColor(c->textval, c->textval, c->textval, 255),
		    TTF_STYLE_NORMAL);
}

static int sdl_poll_one(struct sdl_ctx *c, SDL_Event *evt, uint16_t min,
			uint16_t max)
{
	char path[PATH_MAX];

	switch (evt->type) {
	case SDL_KEYUP:
		switch (evt->key.keysym.scancode) {
		case SDL_SCANCODE_H:
			c->showhelp = 0;
			break;

		case SDL_SCANCODE_L:
			c->showlicense = 0;
			break;
		}

		break;

	case SDL_KEYDOWN:
		switch (evt->key.keysym.scancode) {
		case SDL_SCANCODE_H:
			c->showhelp = 1;
			break;

		case SDL_SCANCODE_L:
			c->showlicense = 1;
			break;

		case SDL_SCANCODE_C:
			c->colormap = !c->colormap;
			break;

		case SDL_SCANCODE_E:
			c->scale_min = 0;
			c->scale_max = 0;
			break;

		case SDL_SCANCODE_T:
			if (!c->showtext) {
				c->showtext = 1;
				c->textval = 255;
				sdl_open_fontcache(c);

			} else if (c->textval == 255) {
				c->textval = 0;
				sdl_open_fontcache(c);

			} else if (c->textval == 0) {
				c->showtext = 0;
			}

			update_crosshair_color(c);
			break;

		case SDL_SCANCODE_M:
			c->show_min_max_marker = !c->show_min_max_marker;
			break;

		case SDL_SCANCODE_F:
			c->fahren = !c->fahren;
			break;

		case SDL_SCANCODE_I:
			c->invert = !c->invert;
			break;

		case SDL_SCANCODE_U:
			c->rotate = !c->rotate;
			break;

		case SDL_SCANCODE_D:
			c->scale_max = max;
			c->scale_min = min;
			break;

		case SDL_SCANCODE_W:
			if ((c->scale_max || c->scale_min) &&
			    !(c->scale_max + 8 < c->scale_max))
				c->scale_max += 8;

			break;

		case SDL_SCANCODE_S:
			if ((c->scale_max || c->scale_min) &&
			    !(c->scale_max - 8 > c->scale_max))
				c->scale_max -= 8;

			break;

		case SDL_SCANCODE_Q:
			if ((c->scale_max || c->scale_min) &&
			    !(c->scale_min + 8 < c->scale_min))
				c->scale_min += 8;

			break;

		case SDL_SCANCODE_A:
			if ((c->scale_max || c->scale_min) &&
			    !(c->scale_min - 8 > c->scale_min))
				c->scale_min -= 8;

			break;

		case SDL_SCANCODE_Z:
			c->scale_min = 0;
			break;

		case SDL_SCANCODE_X:
			c->scale_max = UINT16_MAX;
			break;

		case SDL_SCANCODE_G:
			c->gammafactor = (c->gammafactor + 1) % nr_gammavals;
			break;

		case SDL_SCANCODE_R:
			c->recording = !c->recording;
			return TOGGLE_Y16_RECORD;

		case SDL_SCANCODE_V:
			if (c->vrecord) {
				lavc_end_encode(c->vrecord);
				c->vrecord = NULL;
				break;
			}

			snprintf(path, sizeof(path), "%lld-rgb.mkv",
				 (long long)time(NULL));

			c->vrecord = lavc_start_encode(path, c->desc->width,
						       c->desc->height,
						       c->desc->fps,
						       AV_PIX_FMT_BGRA);
			break;

		case SDL_SCANCODE_Y:
			if (c->contours == 8)
				c->contours = 1;
			else
				c->contours++;

			break;

		case SDL_SCANCODE_RIGHT:
			c->crosshair.x++;
			if (c->crosshair.x >= c->desc->width)
				c->crosshair.x = 0;

			break;

		case SDL_SCANCODE_LEFT:
			c->crosshair.x--;
			if (c->crosshair.x < 0)
				c->crosshair.x = c->desc->width - 1;

			break;

		case SDL_SCANCODE_UP:
			c->crosshair.y--;
			if (c->crosshair.y < 0)
				c->crosshair.y = c->desc->height - 1;

			break;

		case SDL_SCANCODE_DOWN:
			c->crosshair.y++;
			if (c->crosshair.y >= c->desc->height)
				c->crosshair.y = 0;

			break;

		case SDL_SCANCODE_SPACE:
			c->paused = !c->paused;
			return TOGGLE_PAUSE;

		case SDL_SCANCODE_ESCAPE:
			return QUIT_PROGRAM;
		}

		break;

	case SDL_APP_TERMINATING:
	case SDL_QUIT:
		return QUIT_PROGRAM;
	}

	return NOTHING;
}

/**
 * paint_colored_marker() - Paint a marker (cross) given a center point, a size and a color.
 *
 * @param c SDL context handle.
 * @param center_point The center point of the marker.
 * @param size The size (radius) of the marker.
 * @param color The color to use for the marker.
 */
static void paint_colored_marker(const struct sdl_ctx *c,
				 const SDL_Point *center_point, const int size,
				 const SDL_Color *color)
{
	Uint8 original_red_color;
	Uint8 original_green_color;
	Uint8 original_blue_color;
	Uint8 original_alpha;
	SDL_GetRenderDrawColor(c->r, &original_red_color, &original_green_color,
			       &original_blue_color, &original_alpha);
	SDL_SetRenderDrawColor(c->r, color->r, color->g, color->b, color->a);
	SDL_RenderDrawLine(c->r, center_point->x, center_point->y - size,
			   center_point->x, center_point->y + size);
	SDL_RenderDrawLine(c->r, center_point->x - size, center_point->y,
			   center_point->x + size, center_point->y);
	SDL_SetRenderDrawColor(c->r, original_red_color, original_green_color,
			       original_blue_color, original_alpha);
}

/**
 * paint_frame() - Paint a new frame in the SDL window.
 * @param c SDL context handle.
 * @param seq Sequence number of frame.
 * @param data Pointer to raw framebuffer.
 *
 * The framebuffer is assumed to be Y16LE.
 *
 * Return: A paint_frame_action to be taken by the caller.
 */
int paint_frame(struct sdl_ctx *c, uint32_t seq, const uint8_t *data)
{
	uint16_t min = UINT16_MAX, max = 0, ptemp;
	SDL_Point min_point = { 0, 0 };
	SDL_Point max_point = { 0, 0 };
	uint16_t orig_min, orig_max;
	uint32_t multinv;
	uint32_t output_index;
	int ret = NOTHING;
	int pitch, i;
	uint8_t *memptr;
	SDL_Event evt;
	SDL_Rect rect;

	// Get temperature at crosshair
	i = c->crosshair.y * c->desc->width * 2 + c->crosshair.x * 2;
	if (c->rotate) {
		// Mirror crosshair if output is rotated
		i = c->desc->width * c->desc->height * 2 - i;
	}
	ptemp = data[i] | data[i + 1] << 8;

	for (i = 0; i < c->desc->width * c->desc->height * 2; i += 2) {
		uint16_t v = data[i] | data[i + 1] << 8;
		if (v > max) {
			max = v;
			max_point = calc_point_from_buf_offset(c, i);
		}
		if (v < min) {
			min = v;
			min_point = calc_point_from_buf_offset(c, i);
		}
	}

	rect.y = 0;
	rect.x = 0;
	rect.w = c->desc->width;
	rect.h = c->desc->height;

	if (SDL_LockTexture(c->t, &rect, (void **)&memptr, &pitch))
		return -1;

	if (c->showinithelp && now_mono() - c->inittsmono > 5)
		c->showinithelp = false;

	orig_max = max;
	orig_min = min;

	if (c->scale_max || c->scale_min) {
		max = c->scale_max;
		min = c->scale_min;
	}

	if (min >= max) {
		memset(memptr, 0, c->desc->width * c->desc->height * 4);
		goto skippaint;
	}

	/*
	 * We need to compute:
	 *
	 *			  V - min
	 *			 ---------
	 *			 max - min
	 *
	 * ...for each distinct pixel value V. Because the denominator is the
	 * same for every pixel, we can calculate the multiplicative inverse of
	 * (max - min) with a single division, and subsequently use hardware
	 * multiplication to compute the ratio for each pixel.
	 */

	multinv = (1UL << 24) / ((uint32_t)max - min);
	for (i = 0; i < c->desc->width * c->desc->height * 2; i += 2) {
		uint32_t raw = (uint32_t)data[i] | data[i + 1] << 8;
		uint8_t pval;

		if (raw <= min)
			pval = 0;
		else if (raw >= max)
			pval = 255;
		else
			pval = (multinv * (raw - min)) >> 16;

		if (c->rotate) {
			/*
			 * Rotating the output by 180 is equivalent to iterating
			 * through the flattened RGBA array backwards, but still
			 * filling BGRA values in the same order (== subtract a
			 * constant of 4).
			 */
			output_index =
				(c->desc->width * c->desc->height - i / 2) * 4 -
				4;
		} else {
			output_index = i / 2 * 4;
		}

		memptr[output_index] = getcolor(c, BLUE, pval);
		memptr[output_index + 1] = getcolor(c, GREEN, pval);
		memptr[output_index + 2] = getcolor(c, RED, pval);
		memptr[output_index + 3] = 255;
	}

skippaint:
	if (c->vrecord)
		if (lavc_encode(c->vrecord, seq, memptr, c->desc->vsize))
			err(1, "can't vrecord");

	SDL_UnlockTexture(c->t);
	SDL_RenderCopy(c->r, c->t, &rect, &rect);

	if (!(c->pb && c->paused))
		c->frame_paint_seq++;

	if (c->showtext) {
		showtexts(c, raw_to_celsius(orig_max), raw_to_celsius(ptemp),
			  raw_to_celsius(orig_min), seq);

		if (!c->showhelp)
			paint_colored_marker(c, &c->crosshair, 2,
					     &c->crosshair_color);

		if (c->show_min_max_marker && !c->paused) {
			paint_colored_marker(c, &min_point, 1, &SDL_COLOR_BLUE);
			paint_colored_marker(c, &max_point, 1, &SDL_COLOR_RED);
		}
	}

	if (c->showhelp)
		showhelptext(c);
	else if (c->showlicense)
		showlicensetext(c);

	SDL_RenderPresent(c->r);

	while (SDL_PollEvent(&evt) && ret == NOTHING)
		ret = sdl_poll_one(c, &evt, min, max);

	return ret;
}

/**
 * sdl_open() - Create a new SDL window.
 * @param upscaled_width Real pixel width of window on desktop.
 * @param upscaled_height Real pixel height of window on desktop.
 * @param pb True for playback mode.
 * @param fontpath Path to font for rendering text.
 *
 * Return: SDL context handle on success, NULL on error.
 */
struct sdl_ctx *sdl_open(int upscaled_width, int upscaled_height,
			 const struct ircam_desc *desc, bool pb,
			 const char *fontpath, bool hidehelp, bool fullscreen)
{
	const char *window_name = "Linux V4L2/SDL2 IR Camera Viewer";
	FILE *font_tmpfile = NULL;
	uint32_t window_flags, renderer_flags;
	struct sdl_ctx *c;
	char tmp[32];
	void *p;

	if (!fontpath) {
		const uint8_t *src = &builtin_ttf_start;
		size_t len = &builtin_ttf_end - src;

		font_tmpfile = tmpfile();
		if (fwrite(src, 1, len, font_tmpfile) != len)
			err(1, "can't initialize built-in font, try -f");

		snprintf(tmp, sizeof(tmp), "/proc/self/fd/%d",
			 fileno(font_tmpfile));
		fontpath = tmp;
	}

	if (access(fontpath, R_OK))
		err(1, "can't read '%s': pass a path to a valid font with '-f'",
		    fontpath);

	c = calloc(1, sizeof(*c));
	if (!c)
		return NULL;

	c->desc = desc;
	c->inittsmono = now_mono();
	c->pb = pb;
	c->colormap = 1;
	c->showtext = 1;
	c->fahren = 1;
	c->contours = 1;
	c->textval = 255;
	c->crosshair.x = c->desc->width / 2;
	c->crosshair.y = c->desc->height / 2;
	update_crosshair_color(c);
	c->fontpath = strdup(fontpath);
	c->fonttmp = font_tmpfile;

	if (!hidehelp)
		c->showinithelp = true;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
		errx(1, "Can't initialize libsdl: %s", SDL_GetError());

	window_flags = SDL_WINDOW_OPENGL;

	if (fullscreen)
		window_flags |= SDL_WINDOW_FULLSCREEN;

	c->w = SDL_CreateWindow(window_name, SDL_WINDOWPOS_UNDEFINED,
				SDL_WINDOWPOS_UNDEFINED, upscaled_width,
				upscaled_height, window_flags);
	if (!c->w)
		errx(1, "Can't create SDL window: %s", SDL_GetError());

	renderer_flags = SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED;

	c->r = SDL_CreateRenderer(c->w, -1, renderer_flags);
	if (!c->r) {
		warnx("Falling back to SW renderer: %s", SDL_GetError());
		renderer_flags &= ~SDL_RENDERER_ACCELERATED;
		renderer_flags |= SDL_RENDERER_SOFTWARE;
		c->r = SDL_CreateRenderer(c->w, -1, renderer_flags);
		if (!c->r)
			errx(1, "Can't create SW renderer: %s", SDL_GetError());
	}

	SDL_RenderSetLogicalSize(c->r, c->desc->width, c->desc->height);
	SDL_ShowCursor(SDL_DISABLE);

	/*
	 * Everything is flexible about field order except FFV1, which only
	 * supports BGR. So we just use BGR everywhere...
	 */
	c->t = SDL_CreateTexture(c->r, SDL_PIXELFORMAT_BGRA32,
				 SDL_TEXTUREACCESS_STREAMING, c->desc->width,
				 c->desc->height);
	if (!c->t)
		errx(1, "Can't create SDL texture: %s", SDL_GetError());

	if (TTF_Init())
		errx(1, "Can't initialize SDL-TTF: %s", TTF_GetError());

	sdl_open_fontcache(c);

	/*
	 * FIXME: Quick kludge to show something while V4L2 loads...
	 */
	c->pb = true;
	c->paused = true;
	p = alloca(c->desc->width * c->desc->height * 2);
	memset(p, 0, c->desc->width * c->desc->height * 2);
	paint_frame(c, 0, p);
	c->paused = false;
	c->pb = pb;
	return c;
}

/**
 * sdl_close() - Shutdown an SDL window.
 * @param c SDL context handle.
 *
 * Return: Nothing.
 */
void sdl_close(struct sdl_ctx *c)
{
	if (!c)
		return;

	FC_FreeFont(c->f);
	free((void *)c->fontpath);
	TTF_Quit();
	SDL_DestroyTexture(c->t);
	SDL_DestroyRenderer(c->r);
	SDL_DestroyWindow(c->w);
	SDL_Quit();

	if (c->fonttmp)
		fclose(c->fonttmp);

	free(c);
}

/**
 * sdl_loop() - Indicate to SDL the playback has looped.
 * @param c SDL context handle.
 *
 * This ends vrecording, and sets a flag so when the sequence numbers
 * restart the initial help message is not displayed again.
 *
 * Return: Nothing.
 */
void sdl_loop(struct sdl_ctx *c)
{
	c->looped = 1;
	c->frame_paint_seq = 0;

	if (c->vrecord) {
		lavc_end_encode(c->vrecord);
		c->vrecord = NULL;
	}
}
