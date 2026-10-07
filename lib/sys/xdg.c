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

#include "sys/xdg.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TOKEN "yukino"

static void on_response(GDBusConnection *conn, const gchar *sender,
			const gchar *path, const gchar *iface, const gchar *signal,
			GVariant *params, gpointer user_data)
{
	struct yukino_xdg *data = user_data;
	guint32 response;
	GVariant *results = NULL;
	const gchar *uri;
	yukino_result_t r;

	g_variant_get(params, "(u@a{sv})", &response, &results);

	if (response == 0 && g_variant_lookup(results, "uri", "&s", &uri)) {
		uint8_t *full_img = NULL;
		int w = 0, h = 0, channels = 0;

		{
			char *path;

			r = yukino_uri_get_file_path(uri, &path);
			if (r < 0) {
				data->temp_err = r;
				goto done;
			}

			full_img = stbi_load(path, &w, &h, &channels, 4);
			unlink(path);
			free(path);
		}

		if (!full_img)
			goto done;

		if ((w < data->temp_pixel_func_w) || (h < data->temp_pixel_func_h)) {
			data->temp_err = YUKINO_RESULT_UNSUPPORTED;
			goto done;
		}

		const uint8_t *pxl = full_img;
		pxl += data->temp_pixel_func_x * 4;
		pxl += data->temp_pixel_func_y * w * 4;
		for (int y = 0; y < data->temp_pixel_func_h; y++) {
			for (int x = 0; x < data->temp_pixel_func_w; x++) {
				r = data->temp_pixel_func(
					data->temp_pixel_func_data,
					pxl + (x * 4));
				if (r < 0) {
					data->temp_err = r;
					goto done;
				}
			}
			pxl += w * 4;
		}

done:
		free(full_img);
	} else {
		g_print("Screenshot failed/cancelled (response=%u)\n",
			response);
	}

	g_variant_unref(results);
	g_main_loop_quit(data->loop);
}

yukino_result_t yukino_xdg_take(struct yukino_xdg *conn, uint32_t x,
	uint32_t y, uint32_t w, uint32_t h, yukino_pixel_proc_t pixel_func,
	void *userdata)
{
	GError *err;

	conn->temp_pixel_func = pixel_func;
	conn->temp_pixel_func_x = x;
	conn->temp_pixel_func_y = y;
	conn->temp_pixel_func_w = w;
	conn->temp_pixel_func_h = h;
	conn->temp_pixel_func_data = userdata;
	conn->temp_err = YUKINO_RESULT_OK;

	/* boioioioing */
	{
		const gchar *uname;
		gchar *req_path;

		uname = g_dbus_connection_get_unique_name(conn->conn);
		if (!uname)
			return YUKINO_RESULT_INVALID_PARAM;

		{
			gchar *sender;

			sender = g_strdup(uname + 1);
			if (!sender)
				return YUKINO_RESULT_OUT_OF_MEMORY;

			g_strdelimit(sender, ".", '_');

			req_path = g_strdup_printf(
				"/org/freedesktop/portal/desktop/request/%s/%s", sender, TOKEN);

			g_free(sender);
		}

		if (!req_path)
			return YUKINO_RESULT_OUT_OF_MEMORY;

		g_dbus_connection_signal_subscribe(conn->conn,
			"org.freedesktop.portal.Desktop",
			"org.freedesktop.portal.Request", "Response", req_path, NULL,
			G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, on_response,
			conn, NULL);

		g_free(req_path);
	}

	GVariantBuilder opts;
	g_variant_builder_init(&opts, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(
		&opts, "{sv}", "handle_token", g_variant_new_string(TOKEN));
	g_variant_builder_add(
		&opts, "{sv}", "interactive", g_variant_new_boolean(FALSE));

	err = NULL;
	if (!org_freedesktop_portal_screenshot_call_screenshot_sync(
		conn->screenshot_proxy, "",
		g_variant_builder_end(&opts), NULL, NULL, &err)) {
		g_printerr("Call failed: %s\n", err->message);
		g_error_free(err);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	g_main_loop_run(conn->loop);

	conn->temp_pixel_func = NULL;
	conn->temp_pixel_func_data = NULL;

	return conn->temp_err;
}

yukino_result_t yukino_xdg_init(struct yukino_xdg *xdg)
{
	if (!xdg) return YUKINO_RESULT_INVALID_PARAM;

	memset(xdg, 0, sizeof(*xdg));

	xdg->loop = g_main_loop_new(NULL, FALSE);
	xdg->screenshot_proxy
	= org_freedesktop_portal_screenshot_proxy_new_for_bus_sync(
		G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE,
		"org.freedesktop.portal.Desktop", /* bus name */
		"/org/freedesktop/portal/desktop", /* object */
		NULL, /* GCancellable* */
		NULL);
	if (!xdg->screenshot_proxy) {
		return YUKINO_RESULT_UNSUPPORTED;
	}

	xdg->conn = g_dbus_proxy_get_connection(
		G_DBUS_PROXY(xdg->screenshot_proxy));

	return YUKINO_RESULT_OK;
}

void yukino_xdg_quit(struct yukino_xdg *xdg)
{
	if (!xdg) return;

	/* Ehhhhh */
	if (xdg->loop) {
		g_main_loop_unref(xdg->loop);
		xdg->loop = NULL;
	}

	/* A GDBusConnection owned by proxy. Do not free. */
	xdg->conn = NULL;
	/* ;) */
	g_object_unref(xdg->screenshot_proxy);
}
