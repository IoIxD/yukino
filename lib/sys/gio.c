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

#include "sys/xdg.h"
#include "sys/wayland.h"

/* Define data specific to this connection */
struct yukino_connection_data {
	struct yukino_wayland wl;

	struct yukino_xdg xdg;
};

#define YUKINO_CONNECTION_DATA 1
#include "../yukino_c.h"

static yukino_result_t yukino_gio_disconnect(yukino_connection_t *conn)
{
	yukino_wayland_quit(&conn->conn_data.wl);
	yukino_xdg_quit(&conn->conn_data.xdg);
	free(conn);
	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_gio_display_resolution(yukino_connection_t *conn, uint32_t *w, uint32_t *h)
{
	return yukino_wayland_display_resolution(&conn->conn_data.wl, w, h);
}

/* ------------------------------------------------------------------------ */

static yukino_result_t yukino_gio_lock(yukino_connection_t *conn)
{
	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_gio_unlock(yukino_connection_t *conn)
{
	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_gio_take(yukino_connection_t *conn, uint32_t x,
				       uint32_t y, uint32_t w, uint32_t h, yukino_pixel_proc_t pixel_func,
				       void *userdata)
{
	return yukino_xdg_take(&conn->conn_data.xdg, x, y, w, h, pixel_func, userdata);
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

	if ((r = yukino_xdg_init(&conn->conn_data.xdg)) < 0) {
		free(conn);
		return r;
	}

	if ((r = yukino_wayland_init(&conn->conn_data.wl)) < 0) {
		yukino_xdg_quit(&conn->conn_data.xdg);
		free(conn);
		return r;
	}

	/* Fill the vtable */
	conn->disconnect = yukino_gio_disconnect;
	conn->display_resolution = yukino_gio_display_resolution;

	conn->lock = yukino_gio_lock;
	conn->unlock = yukino_gio_unlock;

	conn->take = yukino_gio_take;

	conn->window_iter_start = NULL;
	conn->window_iter = NULL;
	conn->window_iter_end = NULL;
	conn->window_position = NULL;
	conn->window_decorated_position = NULL;

	*pconn = conn;
	return YUKINO_RESULT_OK;
}
