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

#include <gio/gio.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <org.freedesktop.portal.Request.h>
#include <org.freedesktop.portal.Screenshot.h>

#include <SDL3/SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/* Define data specific to this connection */
struct yukino_connection_data {
	int has_perms;
	OrgFreedesktopPortalScreenshot *screenshot_proxy;
	GError *error;
	GMainLoop *loop;
	GDBusConnection *conn;
	yukino_pixel_proc_t temp_pixel_func;
	int temp_pixel_func_x;
	int temp_pixel_func_y;
	int temp_pixel_func_w;
	int temp_pixel_func_h;
	void *temp_pixel_func_data;
};

#define YUKINO_CONNECTION_DATA 1
#include "../yukino_c.h"

static void on_response(GDBusConnection *conn, const gchar *sender,
	const gchar *path, const gchar *iface, const gchar *signal,
	GVariant *params, gpointer user_data)
{
	struct yukino_connection_data *data = user_data;
	guint32 response;
	g_autoptr(GVariant) results = NULL;
	const gchar *uri;

	g_variant_get(params, "(u@a{sv})", &response, &results);

	if (response == 0 && g_variant_lookup(results, "uri", "&s", &uri)) {
		const char *path = g_strdup(uri);
		if (strncmp(path, "file://", 7) == 0)
			path += 7;

		int w = 0, h = 0, channels = 0;
		uint8_t *full_img = stbi_load(path, &w, &h, &channels, 4);
		unlink(path);

		for (int y = 0; y < data->temp_pixel_func_h; y++) {
			for (int x = 0; x < data->temp_pixel_func_w; x++) {
				yukino_result_t r;
				unsigned char rgb[3] = {0, 0, 0};

				/* the portal always gives us the whole
				 * desktop, so crop out the requested area */
				int ix = data->temp_pixel_func_x + x;
				int iy = data->temp_pixel_func_y + y;

				if (full_img && ix >= 0 && iy >= 0 && ix < w
					&& iy < h) {
					const uint8_t *pxl
						= &full_img[((size_t)iy * w
								    + ix)
							    * 4];
					rgb[0] = pxl[0];
					rgb[1] = pxl[1];
					rgb[2] = pxl[2];
				}

				r = data->temp_pixel_func(
					data->temp_pixel_func_data, rgb);
				if (r < 0)
					goto done;
			}
		}
	done:

		printf("%s\n", path);

	} else {
		g_print("Screenshot failed/cancelled (response=%u)\n",
			response);
	}

	g_main_loop_quit(data->loop);
}

static yukino_result_t yukino_gio_disconnect(yukino_connection_t *conn)
{
	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_gio_display_resolution(
	yukino_connection_t *conn, uint32_t *w, uint32_t *h)
{
	int count = 1;
	SDL_DisplayID di = SDL_GetPrimaryDisplay();
	SDL_DisplayID *disp = &di;
	int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
	float density = 1.0f;

	if (!disp)
		return YUKINO_RESULT_UNSUPPORTED;

	for (int i = 0; i < count; i++) {

		const SDL_DisplayMode *mode;
		SDL_Rect r;

		if (!SDL_GetDisplayBounds(disp[i], &r))
			return YUKINO_RESULT_UNSUPPORTED;

		if (r.x < x0)
			x0 = r.x;
		if (r.y < y0)
			y0 = r.y;
		if (r.x + r.w > x1)
			x1 = r.x + r.w;
		if (r.y + r.h > y1)
			y1 = r.y + r.h;

		mode = SDL_GetCurrentDisplayMode(disp[i]);
		if (mode && mode->pixel_density > density)
			density = mode->pixel_density;
	}

	if (x1 <= x0 || y1 <= y0)
		return YUKINO_RESULT_UNSUPPORTED;

	*w = SDL_lroundf((x1 - x0) * density);
	*h = SDL_lroundf((y1 - y0) * density);

	return YUKINO_RESULT_OK;
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
	conn->conn_data.temp_pixel_func = pixel_func;
	conn->conn_data.temp_pixel_func_x = x;
	conn->conn_data.temp_pixel_func_y = y;
	conn->conn_data.temp_pixel_func_w = w;
	conn->conn_data.temp_pixel_func_h = h;
	conn->conn_data.temp_pixel_func_data = userdata;

	/* Predict request path: unique name ":1.234" -> "1_234" */
	g_autofree gchar *sender = g_strdup(
		g_dbus_connection_get_unique_name(conn->conn_data.conn) + 1);
	g_strdelimit(sender, ".", '_');
	const gchar *token = "myshot1";
	g_autofree gchar *req_path = g_strdup_printf(
		"/org/freedesktop/portal/desktop/request/%s/%s", sender, token);

	g_dbus_connection_signal_subscribe(conn->conn_data.conn,
		"org.freedesktop.portal.Desktop",
		"org.freedesktop.portal.Request", "Response", req_path, NULL,
		G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, on_response,
		&conn->conn_data, NULL);

	GVariantBuilder opts;
	g_variant_builder_init(&opts, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(
		&opts, "{sv}", "handle_token", g_variant_new_string(token));
	g_variant_builder_add(
		&opts, "{sv}", "interactive", g_variant_new_boolean(FALSE));

	g_autofree gchar *handle = NULL;
	if (!org_freedesktop_portal_screenshot_call_screenshot_sync(
		    conn->conn_data.screenshot_proxy, "",
		    g_variant_builder_end(&opts), &handle, NULL,
		    &conn->conn_data.error)) {
		g_printerr("Call failed: %s\n", conn->conn_data.error->message);
		return 1;
	}

	g_main_loop_run(conn->conn_data.loop);

	conn->conn_data.temp_pixel_func = NULL;
	conn->conn_data.temp_pixel_func_data = NULL;

	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

yukino_result_t yukino_gio_connect(yukino_connection_t **pconn)
{
	yukino_connection_t *conn;
	int i;

	conn = malloc(sizeof(*conn));
	if (!conn)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	conn->conn_data.loop = g_main_loop_new(NULL, FALSE);
	conn->conn_data.error = NULL;
	conn->conn_data.screenshot_proxy
		= org_freedesktop_portal_screenshot_proxy_new_for_bus_sync(
			G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE,
			"org.freedesktop.portal.Desktop", /* bus name */
			"/org/freedesktop/portal/desktop", /* object */
			NULL, /* GCancellable* */
			&conn->conn_data.error);

	conn->conn_data.conn = g_dbus_proxy_get_connection(
		G_DBUS_PROXY(conn->conn_data.screenshot_proxy));

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
