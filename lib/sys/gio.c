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

/* This file just glues the wayland and xdg crap together.
 * It also desperately needs a better name lmao */

#include "yukino.h"

#include "sys/kwin.h"
#include "sys/wayland.h"
#include "sys/xdg.h"

/* Define data specific to this connection */
struct yukino_connection_data {
	struct yukino_wayland wl;
	struct yukino_xdg xdg;
	struct yukino_kwin kwi;

	unsigned int have_xdg : 1;
	unsigned int have_kwin : 1;
	unsigned int have_wl : 1;

	/* Use KWin window iter and geometry functions.
	 * This is different from have_kwin as we might be able to
	 * initialize kwin but it won't actually work if we try. */
	unsigned int use_kwin : 1;
};

#define YUKINO_CONNECTION_DATA 1
#include "../yukino_c.h"

static yukino_result_t yukino_gio_disconnect(yukino_connection_t *conn)
{
	if (conn->conn_data.have_kwin) yukino_kwin_quit(&conn->conn_data.kwi);
	if (conn->conn_data.have_wl) yukino_wayland_quit(&conn->conn_data.wl);
	if (conn->conn_data.have_xdg) yukino_xdg_quit(&conn->conn_data.xdg);
	free(conn);
	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_gio_display_resolution(
	yukino_connection_t *conn, uint32_t *w, uint32_t *h)
{
	/* Very likely to be more accurate */
	if (conn->conn_data.have_xdg)
		return yukino_xdg_display_resolution(&conn->conn_data.xdg, w, h);

	if (conn->conn_data.have_wl)
		return yukino_wayland_display_resolution(&conn->conn_data.wl, w, h);

	return YUKINO_RESULT_UNSUPPORTED;
}

/* ------------------------------------------------------------------------ */

/* Can't lock here */
static yukino_result_t yukino_gio_lock(yukino_connection_t *conn)
{
	return YUKINO_RESULT_UNSUPPORTED;
}

static yukino_result_t yukino_gio_unlock(yukino_connection_t *conn)
{
	return YUKINO_RESULT_UNSUPPORTED;
}

static yukino_result_t yukino_gio_take(yukino_connection_t *conn, uint32_t x,
	uint32_t y, uint32_t w, uint32_t h, yukino_pixel_proc_t pixel_func,
	void *userdata)
{
	if (conn->conn_data.have_xdg)
		return yukino_xdg_take(&conn->conn_data.xdg, x, y, w, h, pixel_func, userdata);

	return YUKINO_RESULT_UNSUPPORTED;
}

static yukino_result_t yukino_gio_window_iter_start(yukino_connection_t *conn,
	const yukino_window_t *win, yukino_window_iter_t **pwi)
{
	if (conn->conn_data.use_kwin)
		return yukino_kwin_window_iter_start(&conn->conn_data.kwi, win, pwi);

	return YUKINO_RESULT_UNSUPPORTED;
}

static yukino_result_t yukino_gio_window_iter(
	yukino_connection_t *conn, yukino_window_iter_t *wi, yukino_window_t *pw)
{
	if (conn->conn_data.use_kwin)
		return yukino_kwin_window_iter(&conn->conn_data.kwi, wi, pw);

	return YUKINO_RESULT_UNSUPPORTED;
}

static yukino_result_t yukino_gio_window_iter_end(
	yukino_connection_t *conn, yukino_window_iter_t *wi)
{
	if (conn->conn_data.use_kwin)
		return yukino_kwin_window_iter_end(&conn->conn_data.kwi, wi);

	return YUKINO_RESULT_UNSUPPORTED;
}

static yukino_result_t yukino_gio_window_position(
	yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr)
{
	if (conn->conn_data.use_kwin)
		return yukino_kwin_window_position(&conn->conn_data.kwi, win, pr);

	return YUKINO_RESULT_UNSUPPORTED;
}

static yukino_result_t yukino_gio_window_decorated_position(
	yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr)
{
	if (conn->conn_data.use_kwin)
		return yukino_kwin_window_decorated_position(&conn->conn_data.kwi, win, pr);

	return YUKINO_RESULT_UNSUPPORTED;
}

/* ------------------------------------------------------------------------ */

yukino_result_t yukino_gio_connect(yukino_connection_t **pconn)
{
	yukino_connection_t *conn;
	int i;
	yukino_result_t r;

	conn = malloc(sizeof(*conn));
	if (!conn)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	if (yukino_wayland_init(&conn->conn_data.wl) < 0) {
		/* No point */
		free(conn);
		return YUKINO_RESULT_UNSUPPORTED;
	}
	conn->conn_data.have_wl = 1;

	if (yukino_xdg_init(&conn->conn_data.xdg) >= 0)
		conn->conn_data.have_xdg = 1;

	if (yukino_kwin_init(&conn->conn_data.kwi) >= 0)
		conn->conn_data.have_kwin = 1;

	if (!conn->conn_data.have_wl && !conn->conn_data.have_kwin && !conn->conn_data.have_xdg) {
		/* Well fuck */
		free(conn);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	/* Now, check whether kwin actually works */
	if (conn->conn_data.have_kwin) {
		yukino_window_iter_t *wi;

		if (yukino_kwin_window_iter_start(&conn->conn_data.kwi, NULL, &wi) >= 0) {
			yukino_window_t win;

			while ((r = yukino_kwin_window_iter(&conn->conn_data.kwi, wi, &win)) == YUKINO_RESULT_OK);

			if (r == YUKINO_RESULT_DONE)
				conn->conn_data.use_kwin = 1;

			yukino_kwin_window_iter_end(&conn->conn_data.kwi, wi);
		}
	}

	/* Fill the vtable */
	conn->disconnect = yukino_gio_disconnect;
	conn->display_resolution = yukino_gio_display_resolution;

	conn->lock = yukino_gio_lock;
	conn->unlock = yukino_gio_unlock;

	conn->take = yukino_gio_take;

	conn->window_iter_start = yukino_gio_window_iter_start;
	conn->window_iter = yukino_gio_window_iter;
	conn->window_iter_end = yukino_gio_window_iter_end;
	conn->window_position = yukino_gio_window_position;
	conn->window_decorated_position = yukino_gio_window_decorated_position;

	*pconn = conn;
	return YUKINO_RESULT_OK;
}
