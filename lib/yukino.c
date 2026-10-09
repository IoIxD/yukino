/*
 * libyukino -- portable screenshots
 * Copyright (C) 2026 Paper
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

#include "yukino.h"
#include "yukino_c.h"

#include <string.h>

yukino_result_t yukino_connect(yukino_connection_t **pconn)
{
	if (!pconn)
		return YUKINO_RESULT_INVALID_PARAM;

#ifdef YUKINO_GIO
	if (yukino_gio_connect(pconn) == YUKINO_RESULT_OK)
		return YUKINO_RESULT_OK;
#endif

#ifdef YUKINO_XCB
	if (yukino_xcb_connect(pconn) == YUKINO_RESULT_OK)
		return YUKINO_RESULT_OK;
#endif

	return YUKINO_RESULT_UNSUPPORTED;
}

yukino_result_t yukino_display_resolution(
	yukino_connection_t *conn, uint32_t *pw, uint32_t *ph)
{
	uint32_t w, h;

	if (!conn)
		return YUKINO_RESULT_INVALID_PARAM;

	if (!pw)
		pw = &w;
	if (!ph)
		ph = &h;

	return conn->display_resolution ? conn->display_resolution(conn, pw, ph)
									: YUKINO_RESULT_UNSUPPORTED;
}

yukino_result_t yukino_disconnect(yukino_connection_t *conn)
{
	if (!conn)
		return YUKINO_RESULT_INVALID_PARAM;

	return conn->disconnect ? conn->disconnect(conn)
							: YUKINO_RESULT_UNSUPPORTED;
}

yukino_result_t yukino_window_iter_start(yukino_connection_t *conn,
	const yukino_window_t *win, yukino_window_iter_t **pwi)
{
	if (!conn || !pwi)
		return YUKINO_RESULT_INVALID_PARAM;

	return conn->window_iter_start ? conn->window_iter_start(conn, win, pwi)
								   : YUKINO_RESULT_UNSUPPORTED;
}

yukino_result_t yukino_window_iter(
	yukino_connection_t *conn, yukino_window_iter_t *wi, yukino_window_t *win)
{
	if (!conn || !wi || !win)
		return YUKINO_RESULT_INVALID_PARAM;

	return conn->window_iter ? conn->window_iter(conn, wi, win)
							 : YUKINO_RESULT_UNSUPPORTED;
}

yukino_result_t yukino_window_iter_end(
	yukino_connection_t *conn, yukino_window_iter_t *wi)
{
	if (!conn || !wi)
		return YUKINO_RESULT_INVALID_PARAM;

	return conn->window_iter_end ? conn->window_iter_end(conn, wi)
								 : YUKINO_RESULT_UNSUPPORTED;
}

yukino_result_t yukino_window_position(
	yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr)
{
	return conn->window_position ? conn->window_position(conn, win, pr)
								 : YUKINO_RESULT_UNSUPPORTED;
}

yukino_result_t yukino_window_decorated_position(
	yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr)
{
	return conn->window_decorated_position
		? conn->window_decorated_position(conn, win, pr)
		: YUKINO_RESULT_UNSUPPORTED;
}

#define BACKEND_HAS_SCREENSHOT(x) \
	((x)->screenshot && (x)->screenshot_resolution && (x)->screenshot_read \
		&& (x)->screenshot_delete)

/* Fallback screenshot impl, stores everything as RGB888 */
struct yukino_screenshot {
	struct yukino_image img;

	/* Used in the callback */
	unsigned char *ptr; /* data + ((y * w + x) * 3) */
	unsigned char *end; /* data + ((w * h) * 3 */

	/* Data allocated for the image itself */
	unsigned char data[];
};

static yukino_result_t take_cb(void *s_, const unsigned char rgb[3])
{
	yukino_screenshot_t *s = s_;

	if (s->ptr >= s->end)
		return YUKINO_RESULT_FILE_ERROR;

	memcpy(s->ptr, rgb, 3);
	s->ptr += 3;
	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_screenshot_create(yukino_connection_t *conn,
	yukino_screenshot_t **ps, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	yukino_screenshot_t *s;
	yukino_result_t r;

	if (BACKEND_HAS_SCREENSHOT(conn))
		return conn->screenshot(conn, ps, x, y, w, h);

	s = malloc(sizeof(*s) + (w * h * 3));
	if (!s)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	/* Pointless to defer this to read(), there would be no speed gain. */
	if ((r = yukino_screenshot_fix_resolution(conn, &w, &h)) < 0)
		return r;

	/* Init data ptrs */
	s->ptr = s->data;
	s->end = s->data + (w * h * 3);
	if ((r = yukino_screenshot(conn, x, y, w, h, take_cb, s)) < 0)
		return r;

	if ((r = yukino_image(&s->img, s->data, 24, 0xFF0000, 0x00FF00, 0x0000FF,
			 YUKINO_IMAGE_ENDIAN_BIG, w * 4, 0, 0, w, h))
		< 0)
		return r;

	*ps = s;
	return YUKINO_RESULT_OK;
}
yukino_result_t yukino_screenshot_resolution(
	yukino_connection_t *conn, yukino_screenshot_t *s, uint32_t *w, uint32_t *h)
{
	if (BACKEND_HAS_SCREENSHOT(conn))
		return conn->screenshot_resolution(conn, s, w, h);

	return yukino_image_resolution(&s->img, w, h);
}

/* returns YUKINO_RESULT_DONE if there are no bytes left */
yukino_result_t yukino_screenshot_read(
	yukino_connection_t *conn, yukino_screenshot_t *s, unsigned char rgb[3])
{
	if (BACKEND_HAS_SCREENSHOT(conn))
		return conn->screenshot_read(conn, s, rgb);

	return yukino_image_read(&s->img, rgb);
}

yukino_result_t yukino_screenshot_delete(
	yukino_connection_t *conn, yukino_screenshot_t *s)
{
	if (BACKEND_HAS_SCREENSHOT(conn))
		return conn->screenshot_delete(conn, s);

	free(s);
	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_screenshot(yukino_connection_t *conn, uint32_t x,
	uint32_t y, uint32_t w, uint32_t h, yukino_pixel_proc_t pixel_func,
	void *userdata)
{
	yukino_screenshot_t *s;
	yukino_result_t r;
	uint32_t sw, sh;

	/* Try take, but make sure it actually works */
	if (conn->take
		&& ((r = conn->take(conn, x, y, w, h, pixel_func, userdata)) >= 0))
		return r;

	/* Emulate it over the new screenshot API */
	if ((r = yukino_screenshot_create(conn, &s, x, y, w, h)) < 0)
		return r;

	/* Check whether the screenshot is actually what we asked for */
	if ((r = yukino_screenshot_resolution(conn, s, &sw, &sh)) < 0)
		goto cleanup;

	/* FIXME we can handle this pretty easily in both scenarios */
	if (w != sw || h != sh) {
		r = YUKINO_RESULT_INVALID_PARAM;
		goto cleanup;
	}

	for (;;) {
		unsigned char rgb[3];

		r = yukino_screenshot_read(conn, s, rgb);
		if (r < 0)
			goto cleanup;
		if (r == YUKINO_RESULT_DONE)
			break;
		if ((r = pixel_func(userdata, rgb)) < 0)
			goto cleanup;
	}

	/* ALL PEOPLE IS GOOD */
	r = YUKINO_RESULT_OK;

cleanup:
	yukino_screenshot_delete(conn, s);
	return r;
}

YUKINO_INLINE yukino_result_t screenshot_cb(void *conn, uint32_t x, uint32_t y,
	uint32_t w, uint32_t h, yukino_pixel_proc_t pixel_func, void *userdata)
{
	return yukino_screenshot(conn, x, y, w, h, pixel_func, userdata);
}

#define SCREENSHOT(N) \
	yukino_result_t yukino_screenshot_##N(yukino_connection_t *conn, \
		uint32_t x, uint32_t y, uint32_t w, uint32_t h, \
		yukino_write_cb write_cb, void *userdata) \
	{ \
		return yukino_write_##N( \
			x, y, w, h, write_cb, userdata, screenshot_cb, conn); \
	}

SCREENSHOT(ppm)
SCREENSHOT(bmp)
SCREENSHOT(png)

#undef SCREENSHOT

yukino_result_t yukino_lock(yukino_connection_t *conn)
{
	yukino_result_t r;

	if (conn->lock_ref)
		return YUKINO_RESULT_OK;

	r = conn->lock ? conn->lock(conn) : YUKINO_RESULT_UNSUPPORTED;
	if (r < 0)
		return r;

	/* only increase refcount if it succeeded */
	conn->lock_ref++;

	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_unlock(yukino_connection_t *conn)
{
	yukino_result_t r;

	/* do nothing ? */
	if (!conn->lock_ref)
		return YUKINO_RESULT_OK;

	r = conn->unlock ? conn->unlock(conn) : YUKINO_RESULT_UNSUPPORTED;
	if (r < 0)
		return r;

	conn->lock_ref--;
	return r;
}

yukino_result_t yukino_rect_has_point(
	const yukino_rect_t *r, int32_t x, int32_t y)
{
	return ((x >= r->x) && (x <= (r->x + r->w)) && (y >= r->y)
		&& (y <= (r->y + r->h)));
}

/* Not exported, it's here so that impls like xcb don't rewrite it (and get it
 * wrong) */
yukino_result_t yukino_screenshot_fix_resolution(
	yukino_connection_t *conn, uint32_t *w, uint32_t *h)
{
	yukino_result_t r;

	if (*w != YUKINO_SCREENSHOT_DESKTOP_RESOLUTION)
		w = NULL;
	if (*h != YUKINO_SCREENSHOT_DESKTOP_RESOLUTION)
		h = NULL;

	/* Nothing to do? */
	if (!w && !h)
		return YUKINO_RESULT_OK;

	/* Overwrite with the actual desktop resolution */
	if ((r = yukino_display_resolution(conn, w, h)) < 0)
		return r;

	return YUKINO_RESULT_OK;
}
