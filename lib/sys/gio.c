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
#include <unistd.h>
#include <wayland-client.h>

#include <org.freedesktop.portal.Request.h>
#include <org.freedesktop.portal.Screenshot.h>
#include <xdg-output-unstable-v1.h>

#include <SDL3/SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/* Define data specific to this connection */
struct yukino_connection_data {
	struct wl_display *display;

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

struct display_r {
	yukino_rect_t pixel;
	yukino_rect_t logical;
	// int32_t scale_factor;
	unsigned int has_logical_position : 1;
	unsigned int has_logical_size : 1;
};

struct display_data {
	struct display_r **monitors;
	size_t monitors_size;
	struct zxdg_output_manager_v1 *om;
	struct zwp_linux_dmabuf_v1 *dma;
};

static void handle_xdg_output_logical_position(
	void *data, struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y)
{
	struct display_r *d = data;

	d->logical.x = x;
	d->logical.y = y;
	d->has_logical_position = true;
}

static void handle_xdg_output_logical_size(void *data,
	struct zxdg_output_v1 *xdg_output, int32_t width, int32_t height)
{
	struct display_r *d = data;

	d->logical.w = width;
	d->logical.h = height;
	d->has_logical_size = true;
}

static void handle_xdg_output_done(
	void *data, struct zxdg_output_v1 *xdg_output)
{
}

static void handle_xdg_output_name(
	void *data, struct zxdg_output_v1 *xdg_output, const char *name)
{
}

static void handle_xdg_output_description(
	void *data, struct zxdg_output_v1 *xdg_output, const char *description)
{
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
	handle_xdg_output_logical_position,
	handle_xdg_output_logical_size,
	handle_xdg_output_done,
	handle_xdg_output_name,
	handle_xdg_output_description,
};

// Callback when a monitor sends its geometry data
static void output_handle_geometry(void *data, struct wl_output *wl_output,
	int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
	int32_t subpixel, const char *make, const char *model,
	int32_t transform)
{
	struct display_r *r = data;

	r->pixel.x = x;
	r->pixel.y = y;
}

// Callback when a monitor sends its resolution modes
static void output_handle_mode(void *data, struct wl_output *wl_output,
	uint32_t flags, int32_t width, int32_t height, int32_t refresh)
{
	struct display_r *r;

	// We look for the current active resolution mode
	if (!(flags & WL_OUTPUT_MODE_CURRENT))
		return;

	r = data;
	r->pixel.w = width;
	r->pixel.h = height;
}

static void output_handle_done(void *data, struct wl_output *wl_output)
{
}

static void output_handle_scale(
	void *data, struct wl_output *wl_output, int32_t factor)
{
	// struct display_r *r = data;

	// r->scale_factor = factor;
}

// Wire up the wl_output listener
static const struct wl_output_listener output_listener = {
	.geometry = output_handle_geometry,
	.mode = output_handle_mode,
	.done = output_handle_done,
	.scale = output_handle_scale,
};

// Callback to handle global interface registry additions
static void registry_handle_global(void *data, struct wl_registry *registry,
	uint32_t id, const char *interface, uint32_t version)
{
	struct display_data *state = data;

	if (SDL_strcmp(interface, wl_output_interface.name) == 0) {
		state->monitors = realloc(
			state->monitors, sizeof(struct display_r *)
						 * (state->monitors_size + 1));
		state->monitors[state->monitors_size]
			= calloc(1, sizeof(struct display_r));

		struct wl_output *output = wl_registry_bind(
			registry, id, &wl_output_interface, 2);
		wl_output_add_listener(output, &output_listener,
			state->monitors[state->monitors_size]);

		struct zxdg_output_v1 *xdg_output
			= zxdg_output_manager_v1_get_xdg_output(
				state->om, output);
		zxdg_output_v1_add_listener(xdg_output, &xdg_output_listener,
			state->monitors[state->monitors_size]);

		state->monitors_size++;
	} else if (SDL_strcmp(interface, zxdg_output_manager_v1_interface.name)
		   == 0) {
		if (version > 3)
			version = 3;
		state->om = wl_registry_bind(registry, id,
			&zxdg_output_manager_v1_interface, version);
	}
}

static void registry_handle_global_remove(
	void *data, struct wl_registry *registry, uint32_t id)
{
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove,
};

static yukino_result_t yukino_gio_display_resolution(
	yukino_connection_t *conn, uint32_t *w, uint32_t *h)
{
	struct display_data dd;
	double minx, miny, maxx, maxy;
	size_t i;
	double scale;

	memset(&dd, 0, sizeof(dd));

	{
		struct wl_registry *registry
			= wl_display_get_registry(conn->conn_data.display);
		wl_registry_add_listener(registry, &registry_listener, &dd);

		wl_display_roundtrip(conn->conn_data.display);
		wl_display_roundtrip(conn->conn_data.display);

		wl_registry_destroy(registry);
	}

	if (dd.monitors_size == 0)
		return YUKINO_RESULT_UNSUPPORTED;

	// 4. Calculate total desktop dimensions based on individual geometries
	minx = miny = maxx = maxy = 0;

	scale = 0.0;
	for (i = 0; i < dd.monitors_size; i++) {
		struct display_r *m = dd.monitors[i];

		if (m->has_logical_size) {
			double s;

			/* hahahahahah.. hope it's the same for */
			s = (double)m->pixel.w / m->logical.w;
			if (scale < s) scale = s;
			s = (double)m->pixel.h / m->logical.h;
			if (scale < s) scale = s;
		}
	}

	for (i = 0; i < dd.monitors_size; i++) {
		struct display_r *m = dd.monitors[i];
		double x, y, w, h;

		x = m->logical.x * scale;
		y = m->logical.y * scale;
		w = m->logical.w * scale;
		h = m->logical.h * scale;

		if (x < minx)
			minx = x;
		if (y < miny)
			miny = y;
		if ((x + w) > maxx)
			maxx = x + w;
		if ((y + h) > maxy)
			maxy = y + h;

		free(m);
	}
	free(dd.monitors);

	int32_t total_width = roundl(maxx - minx);
	int32_t total_height = roundl(maxy - miny);

	*w = total_width;
	*h = total_height;

	printf("%f %d %d\n", scale, total_width, total_height);

	return YUKINO_RESULT_OK;
}

/* FIXME move all of this shit out of here. it does not belong here */
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
		uint8_t *full_img = NULL;

		/* FIXME decode URI (maybe glib can do this) */
		const char *path = g_strdup(uri);
		if (strncmp(path, "file://", 7) == 0)
			path += 7;

		int w = 0, h = 0, channels = 0;
		full_img = stbi_load(path, &w, &h, &channels, 4);
		unlink(path);

		if (w < data->temp_pixel_func_w)
			goto done;
		if (h < data->temp_pixel_func_h)
			goto done;

		const uint8_t *pxl = full_img;
		pxl += data->temp_pixel_func_x * 4;
		pxl += data->temp_pixel_func_y * w * 4;
		for (int y = 0; y < data->temp_pixel_func_h; y++) {
			for (int x = 0; x < data->temp_pixel_func_w; x++) {
				yukino_result_t r;

				r = data->temp_pixel_func(
					data->temp_pixel_func_data,
					pxl + (x * 4));
				if (r < 0)
					goto done;
			}
			pxl += w * 4;
		}

	done:
		free(full_img);
	} else {
		g_print("Screenshot failed/cancelled (response=%u)\n",
			response);
	}

	g_main_loop_quit(data->loop);
}

static yukino_result_t yukino_gio_disconnect(yukino_connection_t *conn)
{
	wl_display_disconnect(conn->conn_data.display);
	free(conn);
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

	conn->conn_data.display = wl_display_connect(NULL);
	if (!conn->conn_data.display)
		return YUKINO_RESULT_UNSUPPORTED;

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
