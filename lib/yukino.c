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

yukino_result_t yukino_window_iter(yukino_connection_t *conn,
	yukino_window_iter_t *wi, yukino_window_t *win)
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

yukino_result_t yukino_screenshot(yukino_connection_t *conn, uint32_t x,
	uint32_t y, uint32_t w, uint32_t h, yukino_pixel_proc_t pixel_func,
	void *userdata)
{
	return conn->take ? conn->take(conn, x, y, w, h, pixel_func, userdata)
			  : YUKINO_RESULT_UNSUPPORTED;
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
